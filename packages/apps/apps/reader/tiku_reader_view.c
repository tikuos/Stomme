/* tiku_reader_view.c -- a viewer window painted: the toolbar, the page and
 * its highlights, the side list; and the reading tools' input and the
 * commands behind them.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_reader_private.h"

static int reader_offset(tiku_reader_help_window_t *c, int x, int y);
static void reader_tools_draw(tiku_reader_help_window_t *c, tiku_surface_t *s, help_layout_t l);
static int reader_row_live(const tiku_reader_help_window_t *c, int row);

/** @brief The toolbar, the page in its paper, and what the window says. */
void tiku_reader_draw(tiku_window_t *window, tiku_surface_t *s,
                 tiku_rect_t content, void *ctx)
{
    tiku_reader_help_window_t *c = ctx;
    help_layout_t l;
    const char *labels[] = {"", tiku_str("button.back"),
                            tiku_str("button.forward"),
                            tiku_str(c->document_mode ?
                                     "reader.open" : "help.home"),
                            tiku_str(c->document_mode ?
                                     "reader.reload" : "prefs.fonts.done")};
    float pos = 0.0f, frac = 1.0f;

    content = tiku_window_content(window);
    l = tiku_reader_layout(c, content);
    tiku_reader_refit(c, l);
    tiku_reader_controls(c, l);
    if (c->document_mode) tiku_reader_document_menus(c);
    tiku_fill(s, content, TIKU_C_PANEL);
    for (int i = 0; i < c->dialog.count; i++) {
        const tiku_dialog_control_t *d = &c->dialog.control[i];
        unsigned flags = d->enabled ? 0 : TIKU_S_DISABLED;

        if (d->id >= HELP_PAGE) continue;
        if (tiku_dialog_focused(&c->dialog, d->id)) flags |= TIKU_S_FOCUS;
        if (tiku_dialog_pressed(&c->dialog, d->id)) flags |= TIKU_S_PRESSED;
        if (c->document_mode)
            tiku_reader_document_button(c, s, d->rect, labels[d->id], d->id, flags);
        else tiku_ui_button(s, d->rect, labels[d->id], flags);
    }
    if (c->document_mode) tiku_reader_document_chrome(c, s, l);
    reader_tools_draw(c, s, l);
    {
        const char *title = c->doc ? tiku_doc_title(c->doc) : NULL;

        tiku_text_line_draw(s, l.title, TIKU_MEASURE_BOLD,
                            title ? title : c->page, TIKU_C_TEXT);
    }
    tiku_ui_sunken(s, l.paper, TIKU_C_DOC);
    if (c->doc) {
        const char *what = NULL;
        char cut[128];
        int cap = 0;
        tiku_doc_paint_t how = {0};

        cut[0] = 0;
        if (c->oversize) {
            snprintf(cut, sizeof cut, "%s", tiku_str("help.too_long"));
        } else if (tiku_doc_full_first(c->doc, &what, &cap)) {
            snprintf(cut, sizeof cut, "%s %s (%d)",
                     tiku_str("help.cut"), what ? what : "", cap);
        }
        how.scroll = c->bar.value;
        how.focus_link = tiku_dialog_focused(&c->dialog, HELP_PAGE)
                             ? c->focus_link : TIKU_DOC_NONE;
        how.cut = cut[0] ? cut : NULL;
        how.image = tiku_reader_image_draw;
        how.image_ctx = c;
        if (c->reader) c->reader->surface = s;
        int font = tiku_reader_font_begin(c);
        /* Display-list faces of the default size are read at the desktop's
         * size, so a page drawn at another size takes the pixel path. */
        if (tiku_font_size() != font) tiku_gfx_rec_miss(s);
        if (l.page.x < s->clip.x+s->clip.w &&
            l.page.y < s->clip.y+s->clip.h &&
            l.page.x+l.page.w > s->clip.x &&
            l.page.y+l.page.h > s->clip.y)
        {
            struct help_reader *r = c->reader;
            if (r) {
                tiku_html_view_t *v = &r->html;
                tiku_html_view_bind(v,c->doc,r->text,r->length,r->starts);
                v->viewport = l.page;v->scroll = c->bar.value;v->a = r->a;v->b = r->b;
                v->focus_link = how.focus_link;v->paint = how;
                v->match = r->match;v->match_len = r->match_len;v->matches = r->matches;
                snprintf(v->query,sizeof v->query,"%s",r->query.text);
                tiku_html_view_draw(v,s,l.page);
            } else tiku_doc_draw(s,c->doc,l.page,&how);
        }
        tiku_reader_font_end(font);
        if (tiku_scroll_max(&c->bar) > 0 && tiku_doc_height(c->doc) > 0) {
            pos = (float)c->bar.value / (float)tiku_scroll_max(&c->bar);
            frac = (float)l.page.h / (float)tiku_doc_height(c->doc);
        }
    }
    tiku_ui_scrollbar_held(s, l.bar, pos, frac, 0, c->bar.arrow_dir);
    tiku_text_line_draw(s, l.status, TIKU_MEASURE_PLAIN,
                        c->reader && c->reader->hovered[0] ?
                        c->reader->hovered : c->status[0] ? c->status :
                        c->document_mode ? tiku_reader_document_caption(c) : "",
                        tiku_ink(TIKU_INK_FAINT));
    if (c->document_mode) tiku_reader_document_tip(c, s, content);
}

/** @brief The text offset at (@p x, @p y), measured at the page's size. */
static int reader_offset(tiku_reader_help_window_t *c, int x, int y)
{
    int font = tiku_reader_font_begin(c);
    int offset = tiku_html_text_offset(c->doc,c->reader->starts,c->reader->length,
                                       x,y,&c->reader->block);
    tiku_reader_font_end(font);
    return offset;
}

/** @brief Draw the reading tools: their buttons, the Find field, and the
 *         side list of outline, search results or bookmarks. */
static void reader_tools_draw(tiku_reader_help_window_t *c, tiku_surface_t *s, help_layout_t l)
{
    struct help_reader *r = c->reader; if (!r) return;
    static const char *const labels[] = {"", "", "", "", "", "", "", "help.prev", "help.next",
        "help.search_all", "help.copy", "help.copy_code", "help.outline", "help.add_bookmark", "help.bookmarks", "help.copy_link"};
    for (int i = 0; i < c->dialog.count; i++) {
        const tiku_dialog_control_t *d = &c->dialog.control[i];
        if (d->id <= HELP_FIND || d->id >= HELP_SIDE) continue;
        unsigned flags = tiku_dialog_focused(&c->dialog, d->id) ? TIKU_S_FOCUS : 0;
        if (tiku_dialog_pressed(&c->dialog, d->id)) flags |= TIKU_S_PRESSED;
        const char *label = labels[d->id];
        if (d->id == HELP_SEARCH && r->search) label = "reader.stop_search";
        else if (d->id == HELP_SEARCH && c->document_mode)
            label = "reader.search_folder";
        tiku_ui_button(s, d->rect, tiku_str(label), flags);
    }
    if (l.find.w) {
        int focused = c->dialog.focus == HELP_FIND;
        tiku_ui_textfield_scroll(s, l.find, r->query.text,
            focused ? r->query.caret : -1,
            focused ? r->query.sel_a : 0, focused ? r->query.sel_b : 0,
            focused ? TIKU_S_FOCUS : 0, r->input.scroll);
        if (!r->query.text[0] && !focused)
            tiku_text_line_draw(s,
                (tiku_rect_t){l.find.x+6,l.find.y,l.find.w-12,l.find.h},
                TIKU_MEASURE_PLAIN, tiku_str("help.find_hint"),
                tiku_ink(TIKU_INK_FAINT));
    }
    if (!l.side.w) return;
    if (r->side == 1) {
        tiku_reader_outline(c);
        int selected = 0;
        for (int i = 0; i < r->nrows; i++)
            if (c->doc->block[r->rows[i].block].y <= c->bar.value) selected = i;
        if (c->dialog.focus != HELP_SIDE || r->follow_outline) {
            int moved = selected != r->list.cursor || r->follow_outline;
            tiku_list_select_only(&r->list, selected);
            if (moved) tiku_list_reveal(&r->list, l.side);
            r->follow_outline = 0;
        }
    } else if (r->side == 3) {
        r->nrows = r->nmarks; memcpy(r->rows, r->marks, r->nmarks*sizeof *r->rows);
        tiku_list_set_count(&r->list, r->nrows);
    }
    tiku_list_set_row_h(&r->list, r->side == 2 ? 46 : tiku_measure_row_h());
    tiku_ui_sunken(s, l.side, TIKU_C_DOC);
    for (int i = r->list.top; i < r->nrows && i < r->list.top+tiku_list_visible_rows(&r->list, l.side); i++) {
        tiku_rect_t row = tiku_list_row_rect(&r->list, l.side, i);
        int selected = tiku_list_selected(&r->list, i);
        if (selected) tiku_fill(s, row, TIKU_C_SELECT);
        tiku_rgb_t ink = selected ? TIKU_C_SELTEXT : TIKU_C_TEXT;
        int indent = r->side == 1 ? 16 + (r->rows[i].level-1)*12 : 0;
        if (r->side == 1 && r->rows[i].children)
            tiku_text_line_draw(s, (tiku_rect_t){row.x+indent-12,row.y,16,22}, TIKU_MEASURE_PLAIN,
                r->folded[r->rows[i].block] ? "+" : "-", ink);
        tiku_text_line_draw(s, (tiku_rect_t){row.x+5+indent,row.y,row.w-10-indent,22}, TIKU_MEASURE_PLAIN, r->rows[i].title, ink);
        if (r->side == 2) tiku_text_line_draw(s, (tiku_rect_t){row.x+5,row.y+22,row.w-10,22},
            TIKU_MEASURE_PLAIN, r->rows[i].snippet, ink);
    }
}

/**
 * @brief Whether an outline row still names a block of the page shown.
 *
 * The rows are rebuilt while drawing, so between a page arriving and the
 * next frame they still hold the last page's block numbers; and the fold
 * flags are made again with the page, and can be smaller.
 */
static int reader_row_live(const tiku_reader_help_window_t *c, int row)
{
    const struct help_reader *r = c->reader;

    return row >= 0 && row < r->nrows && c->doc != NULL &&
           r->rows[row].block >= 0 && r->rows[row].block < c->doc->nblock &&
           r->rows[row].block < r->folded_cap;
}

/** @brief Open the side list's chosen row: an outline row scrolls to its
 *         heading; a result or a bookmark opens its page, then finds the
 *         words searched for or scrolls to the block.  The row is copied
 *         first, since opening a page rebuilds the list. */
void tiku_reader_side_open(tiku_reader_help_window_t *c)
{
    struct help_reader *r = c->reader;
    int i = r->list.cursor; if (i < 0 || i >= r->nrows) return;
    help_row_t row = r->rows[i]; int mode = r->side;
    if (mode == 1 && !reader_row_live(c, i)) return;
    if (mode == 1) tiku_scroll_to(&c->bar, c->doc->block[row.block].y);
    else if (c->document_mode ? tiku_reader_document_window_file(c, row.page) :
             tiku_reader_help_window_go(c, row.page) == TIKU_READER_HELP_PAGE) {
        if (mode == 2)
            (void)tiku_reader_help_window_find(c, r->search_query, 0);
        else tiku_scroll_to(&c->bar, tiku_doc_place_y(c->doc, (tiku_doc_place_t){row.block,0}));
    }
}

/** @brief Carry out a reading tools' command, from a button, menu or key.
 *  @return 1 when @p command was one. */
int tiku_reader_action(tiku_reader_help_window_t *c, int command)
{
    struct help_reader *r = c->reader; if (!r) return 0;
    if (tiku_reader_typography(c, command)) return 1;
    switch (command) {
    case HELP_PREV: case HELP_NEXT: tiku_reader_help_window_find(c, r->query.text, command == HELP_PREV); break;
    case HELP_SEARCH: tiku_reader_search(c); break;
    case HELP_CANCEL_SEARCH: tiku_reader_search_cancel(c); break;
    case HELP_RESULTS: tiku_reader_search_results(c); break;
    case HELP_COPY: tiku_reader_help_window_copy(c, 0); break;
    case HELP_CODE: tiku_reader_help_window_copy(c, 1); break;
    case HELP_OUTLINE: r->side = r->side == 1 ? 0 : 1; r->list.top = 0; r->follow_outline = 1; break;
    case HELP_BOOKMARK: tiku_reader_bookmark(c); break;
    case HELP_BOOKMARKS: r->side = r->side == 3 ? 0 : 3; r->list.top = 0; break;
    case HELP_LINK: {
        const char *href = c->doc && c->focus_link >= 0 ? tiku_doc_link_href(c->doc, c->focus_link) : NULL;
        tiku_reader_say(c, href && tiku_clipboard_import(href, strlen(href)) ? "Link copied; no browser was launched." : "Select a link with Tab first.");
        break;
    }
    default: return 0;
    }
    return 1;
}

/** @brief The reading tools' input: Ctrl+F, Ctrl+G, the Find field,
 *         Ctrl+C, Ctrl+A and Ctrl+D, the side list, and selecting the page's
 *         text with the pointer.  @return 1 when taken. */
int tiku_reader_tools_event(tiku_reader_help_window_t *c, const tiku_event_t *ev, help_layout_t l)
{
    struct help_reader *r = c->reader; if (!r) return 0;
    int key = ev->type == TIKU_EVENT_KEY_DOWN, down = ev->type == TIKU_EVENT_POINTER_DOWN;
    if (ev->type == TIKU_EVENT_DEACTIVATED) r->dragging = 0;
    if (key && (ev->modifiers & TIKU_MOD_CMD) && ev->key == 'f') {
        tiku_dialog_focus(&c->dialog, HELP_FIND); return 1;
    }
    if (key && (ev->modifiers & TIKU_MOD_CMD) && ev->key == 'g') {
        tiku_reader_help_window_find(c, r->query.text, (ev->modifiers & TIKU_MOD_SHIFT) != 0); return 1;
    }
    if (down && ev->x >= l.find.x && ev->x < l.find.x+l.find.w && ev->y >= l.find.y && ev->y < l.find.y+l.find.h)
        tiku_dialog_focus(&c->dialog, HELP_FIND);
    if (c->dialog.focus == HELP_FIND && !(key && (ev->key == TIKU_KEY_TAB || ev->key == TIKU_KEY_ESCAPE))) {
        char old[TIKU_FIELD_MAX]; snprintf(old, sizeof old, "%s", r->query.text);
        tiku_field_result_t result = tiku_field_event(&r->query, &r->input, ev, l.find, 1);
        if (strcmp(old, r->query.text)) {
            r->match = -1; r->matches = 0;
            tiku_reader_say(c, c->document_mode ?
                "Press Enter to find in this page, or choose Search folder." :
                "Press Enter to find in this page, or choose Search all help.");
        }
        if (result == TIKU_FIELD_REJECTED) tiku_reader_say(c, "Search text is invalid or too long.");
        if (result == TIKU_FIELD_COMMITTED) tiku_reader_help_window_find(c, r->query.text, (ev->modifiers & TIKU_MOD_SHIFT) != 0);
        if (key || ev->type == TIKU_EVENT_TEXT_INPUT || r->input.dragging) return 1;
    }
    if (key && (ev->modifiers & TIKU_MOD_CMD)) {
        if (ev->key == 'c') { tiku_reader_help_window_copy(c, 0); return 1; }
        if (ev->key == 'a' && c->dialog.focus == HELP_PAGE) { r->a = 0; r->b = r->length; return 1; }
        if (ev->key == 'd') { tiku_reader_bookmark(c); return 1; }
    }
    if (l.side.w && ev->x >= l.side.x && ev->x < l.side.x+l.side.w) {
        if (ev->type == TIKU_EVENT_WHEEL) { tiku_list_scroll_to(&r->list, r->list.top+ev->dy, l.side); return 1; }
        if (down && ev->button == 1) {
            int row = tiku_list_at(&r->list, l.side, ev->x, ev->y);
            if (row >= 0) {
                tiku_dialog_focus(&c->dialog, HELP_SIDE); tiku_list_select_only(&r->list, row);
                if (r->side == 1 && reader_row_live(c, row) &&
                    r->rows[row].children &&
                    ev->x < l.side.x+20+(r->rows[row].level-1)*12)
                    r->folded[r->rows[row].block] ^= 1;
                else tiku_reader_side_open(c);
                return 1;
            }
        }
    }
    if (key && c->dialog.focus == HELP_SIDE && ev->key != TIKU_KEY_TAB && ev->key != TIKU_KEY_ESCAPE) {
        if (r->side == 1 && reader_row_live(c, r->list.cursor) &&
            (ev->key == TIKU_KEY_LEFT || ev->key == TIKU_KEY_RIGHT))
            r->folded[r->rows[r->list.cursor].block] = ev->key == TIKU_KEY_LEFT;
        else if (ev->key == TIKU_KEY_RETURN) tiku_reader_side_open(c);
        else tiku_list_key(&r->list, ev->key, ev->modifiers, l.side);
        return 1;
    }
    if (!c->doc) return 0;
    int inside = ev->x >= l.page.x && ev->x < l.page.x+l.page.w && ev->y >= l.page.y && ev->y < l.page.y+l.page.h;
    if (inside && ev->type == TIKU_EVENT_WHEEL) tiku_dialog_focus(&c->dialog, HELP_PAGE);
    if (ev->type == TIKU_EVENT_POINTER_MOVE) {
        r->hovered[0] = 0;
        tiku_doc_hit_t h = tiku_doc_hit(c->doc, ev->x-l.page.x, ev->y-l.page.y+c->bar.value);
        if (inside && h.kind == TIKU_DOC_HIT_LINK) snprintf(r->hovered, sizeof r->hovered, "%s", tiku_doc_link_href(c->doc, h.link));
    }
    if (down && ev->button == 1 && inside) {
        tiku_doc_hit_t h = tiku_doc_hit(c->doc, ev->x-l.page.x, ev->y-l.page.y+c->bar.value);
        if (h.kind == TIKU_DOC_HIT_LINK) { r->a = r->b = 0; return 0; }
        int at = reader_offset(c, ev->x-l.page.x, ev->y-l.page.y+c->bar.value);
        if (at >= 0) { r->a = r->b = at; r->dragging = 1; tiku_dialog_focus(&c->dialog, HELP_PAGE); return 1; }
    }
    if (ev->type == TIKU_EVENT_POINTER_MOVE && r->dragging) {
        if (ev->y < l.page.y) tiku_scroll_to(&c->bar, c->bar.value-tiku_scroll_line(&c->bar));
        if (ev->y >= l.page.y+l.page.h) tiku_scroll_to(&c->bar, c->bar.value+tiku_scroll_line(&c->bar));
        int at = reader_offset(c, ev->x-l.page.x, ev->y-l.page.y+c->bar.value);
        if (at >= 0) r->b = at;
        return 1;
    }
    if (ev->type == TIKU_EVENT_POINTER_UP) r->dragging = 0;
    return 0;
}
