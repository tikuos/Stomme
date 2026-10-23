/* tiku_reader_browser.c -- a document window's browser chrome: its icon
 * buttons and their tips, the Location field, which opens local files
 * only, and the Find bar's buttons.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_reader_private.h"

static const char *document_tool_label(int id);
static tiku_rect_t document_tip_rect(tiku_reader_help_window_t *c,
                                     int tool, tiku_rect_t content);
static void document_location_open(tiku_reader_help_window_t *c);

/** @brief A toolbar button drawn as its icon, lit under the pointer;
 *         @p label goes with it for narration. */
void tiku_reader_document_button(tiku_reader_help_window_t *c,
                            tiku_surface_t *s, tiku_rect_t box,
                            const char *label, int id, unsigned flags)
{
    const char *icon = id == HELP_BACK ? "nav_back" :
        id == HELP_FORWARD || id == HELP_GO ? "nav_forward" :
        id == HELP_HOME ? "folder_open" :
        id == HELP_DONE ? "nav_reload" : "magnifier";
    if (c->reader && c->reader->hover_tool == id) flags |= TIKU_S_HOT;
    /* The label is for narration and is not drawn; the icon is the face. */
    tiku_ui_button(s, box, label, flags | TIKU_S_ICON);
    int shift = (flags & TIKU_S_PRESSED) ? 1 : 0;
    int size = box.h - 6;
    if (size > 28) size = 28;
    tiku_icon_paint_dim(s, icon, box.x + (box.w-size)/2 + shift,
        box.y + (box.h-size)/2 + shift, size,
        flags & TIKU_S_DISABLED ? 0.72f : 0.0f, TIKU_C_PANEL);
}

/** @brief The tip of toolbar control @p id: its name and shortcut, or ""
 *         for a control without one. */
static const char *document_tool_label(int id)
{
    switch (id) {
    case HELP_BACK: return tiku_str("reader.tip_back");
    case HELP_FORWARD: return tiku_str("reader.tip_forward");
    case HELP_DONE: return tiku_str("reader.tip_reload");
    case HELP_HOME: return tiku_str("reader.tip_open");
    case HELP_FIND_TOGGLE: return tiku_str("reader.tip_find");
    case HELP_GO: return tiku_str("reader.tip_go");
    case HELP_FIND_CLOSE: return tiku_str("reader.tip_find_close");
    default: return "";
    }
}

/** @brief The document's name for the status line: its title, or else its
 *         file name. */
const char *tiku_reader_document_caption(tiku_reader_help_window_t *c)
{
    const char *title = c->doc ? tiku_doc_title(c->doc) : NULL;
    if (title && *title) return title;
    const char *slash = strrchr(c->page, '/');
    return slash ? slash+1 : "";
}

/** @brief Where control @p tool's tip goes in @p content: below it, or
 *         above when there is no room; empty when it has none.  Drawing
 *         and the hover damage both use it. */
static tiku_rect_t document_tip_rect(tiku_reader_help_window_t *c,
                                     int tool, tiku_rect_t content)
{
    for (int i = 0; i < c->dialog.count; i++) {
        const tiku_dialog_control_t *d = &c->dialog.control[i];
        if (d->id != tool) continue;
        const char *text = document_tool_label(d->id);
        if (!*text) break;
        tiku_rect_t tip = tiku_ui_tip_size(text);
        tip.x = d->rect.x; tip.y = d->rect.y + d->rect.h + 4;
        if (tip.x + tip.w > content.x + content.w)
            tip.x = content.x + content.w - tip.w;
        if (tip.y + tip.h > content.y + content.h)
            tip.y = d->rect.y - tip.h - 4;
        return tip;
    }
    return (tiku_rect_t){0};
}

/** @brief Paint the tip of the toolbar control under the pointer; it is
 *         drawn last, so nothing paints over it. */
void tiku_reader_document_tip(tiku_reader_help_window_t *c, tiku_surface_t *s,
                         tiku_rect_t content)
{
    if (!c->reader || !c->reader->hover_tool) return;
    tiku_rect_t tip = document_tip_rect(c, c->reader->hover_tool, content);
    if (tip.w > 0) tiku_ui_tip(s, tip,
                               document_tool_label(c->reader->hover_tool));
}

/** @brief A pointer move with nothing held: note the toolbar control and
 *         the link under the pointer, damaging only what changed.
 *  @return 1 when it was such a move. */
int tiku_reader_document_hover(tiku_reader_help_window_t *c,
                           const tiku_event_t *ev, help_layout_t l)
{
    struct help_reader *r = c->reader;
    if (!r || ev->type != TIKU_EVENT_POINTER_MOVE || r->dragging ||
        r->input.dragging || r->location_input.dragging || c->bar.tracking ||
        c->dialog.capture != TIKU_DIALOG_NONE)
        return 0;
    int tool = 0;
    char href[sizeof r->hovered] = "";
    for (int i = 0; c->document_mode && i < c->dialog.count; i++) {
        const tiku_dialog_control_t *d = &c->dialog.control[i];
        if (*document_tool_label(d->id) &&
            ev->x >= d->rect.x && ev->x < d->rect.x+d->rect.w &&
            ev->y >= d->rect.y && ev->y < d->rect.y+d->rect.h) tool = d->id;
    }
    if (c->doc && ev->x >= l.page.x && ev->x < l.page.x+l.page.w &&
        ev->y >= l.page.y && ev->y < l.page.y+l.page.h) {
        tiku_doc_hit_t hit = tiku_doc_hit(c->doc, ev->x-l.page.x,
                                        ev->y-l.page.y+c->bar.value);
        if (hit.kind == TIKU_DOC_HIT_LINK)
            snprintf(href, sizeof href, "%s", tiku_doc_link_href(c->doc, hit.link));
    }
    if (tool != r->hover_tool) {
        tiku_rect_t content = tiku_window_content(c->window);
        c->hover_damage = tiku_rect_union(
            document_tip_rect(c, tool, content),
            document_tip_rect(c, r->hover_tool, content));
        for (int i = 0; i < c->dialog.count; i++)
            if (c->dialog.control[i].id == tool ||
                c->dialog.control[i].id == r->hover_tool)
                c->hover_damage = tiku_rect_union(c->hover_damage,
                                                   c->dialog.control[i].rect);
        r->hover_tool = tool;
    }
    if (strcmp(href, r->hovered)) {
        snprintf(r->hovered, sizeof r->hovered, "%s", href);
        c->hover_damage = tiku_rect_union(c->hover_damage, l.status);
    }
    if (c->hover_damage.w > 0 && c->dirty) *c->dirty = 1;
    return 1;
}

/** @brief Put the shown document's path in the Location field; its file
 *         name when the path is too long for the field. */
void tiku_reader_document_location_sync(tiku_reader_help_window_t *c)
{
    struct help_reader *r = c->reader;
    if (!r) return;
    const char *text = c->page;
    if (strlen(text) >= TIKU_FIELD_MAX) {
        const char *slash = strrchr(text, '/');
        text = slash ? slash + 1 : "";
    }
    /* A name too long for the field leaves the field empty. */
    if (strlen(text) >= TIKU_FIELD_MAX) text = "";
    tiku_field_start(&r->location, text);
    tiku_field_input_init(&r->location_input);
    r->location_dirty = 0;
}

/** @brief Show or hide the Find bar, with the keyboard on it or back on
 *         the page; the controls are rebuilt first, so the focus goes to a
 *         control that exists. */
void tiku_reader_document_find(tiku_reader_help_window_t *c, int visible)
{
    if (!c->reader || !c->window) return;
    c->reader->find_visible = visible;
    tiku_reader_controls(c, tiku_reader_layout(c, tiku_window_content(c->window)));
    tiku_dialog_focus(&c->dialog, visible ? HELP_FIND : HELP_PAGE);
    if (visible) tiku_field_key(&c->reader->query, TIKU_FIELD_SELECT_ALL, 0, 0);
    if (c->dirty) *c->dirty = 1;
}

/** @brief Open what the Location field holds: an absolute path, or one
 *         relative to the shown document's folder; a web address is
 *         refused. */
static void document_location_open(tiku_reader_help_window_t *c)
{
    struct help_reader *r = c->reader;
    if (!r) return;
    const char *text = r->location.text;
    char path[TIKU_PATH_MAX];
    if (!*text || strstr(text, "://") || !strncmp(text, "//", 2) ||
        (*text != '/' && strchr(text, ':'))) {
        tiku_reader_say(c, "Enter a local document path. "
                        "This viewer does not open web addresses.");
        tiku_dialog_focus(&c->dialog, HELP_LOCATION); return;
    }
    int n = *text == '/' ? snprintf(path, sizeof path, "%s", text) :
        snprintf(path, sizeof path, "%s/%s", c->root, text);
    if ((*text != '/' && !c->root[0]) || n < 0 || n >= (int)sizeof path) {
        tiku_reader_say(c, "Choose Open or enter a complete local document path.");
        tiku_dialog_focus(&c->dialog, HELP_LOCATION); return;
    }
    if (tiku_reader_document_window_file(c, path)) {
        tiku_reader_document_location_sync(c);
        tiku_dialog_focus(&c->dialog, HELP_PAGE);
    } else tiku_dialog_focus(&c->dialog, HELP_LOCATION);
}

/** @brief Carry out a Location or Find command, from a button or a key.
 *  @return 1 when @p id was one. */
int tiku_reader_document_action(tiku_reader_help_window_t *c, int id)
{
    if (!c->reader) return 0;
    switch (id) {
    case HELP_GO: document_location_open(c); break;
    case HELP_FIND_TOGGLE: tiku_reader_document_find(c, !c->reader->find_visible); break;
    case HELP_FIND_CLOSE: tiku_reader_document_find(c, 0); break;
    case HELP_COPY:
        if (c->dialog.focus == HELP_LOCATION)
            tiku_field_key(&c->reader->location, TIKU_FIELD_COPY, 0, 0);
        else if (c->dialog.focus == HELP_FIND)
            tiku_field_key(&c->reader->query, TIKU_FIELD_COPY, 0, 0);
        else return 0;
        break;
    case HELP_LOCATION:
        tiku_dialog_focus(&c->dialog, HELP_LOCATION);
        tiku_field_key(&c->reader->location, TIKU_FIELD_SELECT_ALL, 0, 0);
        if (strlen(c->page) >= TIKU_FIELD_MAX)
            tiku_reader_say(c, "Editing the filename in the current folder; Open accepts complete long paths.");
        break;
    default: return 0;
    }
    return 1;
}

/** @brief Draw the Location field, the buttons beside it and on the Find
 *         bar, the rule over the status line and the profile's icon. */
void tiku_reader_document_chrome(tiku_reader_help_window_t *c, tiku_surface_t *s,
                            help_layout_t l)
{
    struct help_reader *r = c->reader;
    if (!r) return;
    int focused = c->dialog.focus == HELP_LOCATION;
    tiku_text_line_draw(s, l.location_label, TIKU_MEASURE_PLAIN,
                        tiku_str("reader.location"), TIKU_C_TEXT);
    tiku_ui_textfield_scroll(s, l.location,
        !focused && !r->location_dirty ? c->page : r->location.text,
        focused ? r->location.caret : -1,
        focused ? r->location.sel_a : 0, focused ? r->location.sel_b : 0,
        focused ? TIKU_S_FOCUS : 0, focused ? r->location_input.scroll : 0);
    if (!r->location.text[0] && !focused)
        tiku_text_line_draw(s, (tiku_rect_t){l.location.x+6,l.location.y,
            l.location.w-12,l.location.h}, TIKU_MEASURE_PLAIN,
            tiku_str("reader.location_hint"), tiku_ink(TIKU_INK_FAINT));
    for (int i = 0; i < c->dialog.count; i++) {
        const tiku_dialog_control_t *d = &c->dialog.control[i];
        if (d->id < HELP_GO) continue;
        unsigned flags = tiku_dialog_focused(&c->dialog, d->id) ? TIKU_S_FOCUS : 0;
        if (tiku_dialog_pressed(&c->dialog, d->id)) flags |= TIKU_S_PRESSED;
        if (d->id == HELP_FIND_TOGGLE)
            tiku_reader_document_button(c, s, d->rect, tiku_str("reader.find_button"), d->id,
                            flags | (r->find_visible ? TIKU_S_PRESSED : 0));
        else if (d->id == HELP_GO)
            tiku_reader_document_button(c, s, d->rect, tiku_str("reader.go"), d->id, flags);
        else tiku_ui_button(s, d->rect, "×", flags);
    }
    tiku_hline(s, l.paper.x, l.status.y-3, l.paper.w,
                tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_1));
    (void)tiku_icon_paint(s, c->profile->icon, l.status.x-22,
                          l.status.y+(l.status.h-16)/2, 16);
}

/** @brief A document window's own input, before the page's: the tool
 *         hover, Ctrl+L and Ctrl+F, Escape out of the Location field or
 *         Find, and the Location field's editing.  @return 1 when taken. */
int tiku_reader_document_event(tiku_reader_help_window_t *c, const tiku_event_t *ev,
                           help_layout_t l)
{
    struct help_reader *r = c->reader;
    if (!r) return 0;
    if (ev->type == TIKU_EVENT_POINTER_MOVE) {
        r->hover_tool = 0;
        for (int i = 0; i < c->dialog.count; i++) {
            const tiku_dialog_control_t *d = &c->dialog.control[i];
            if (*document_tool_label(d->id) &&
                ev->x >= d->rect.x && ev->x < d->rect.x+d->rect.w &&
                ev->y >= d->rect.y && ev->y < d->rect.y+d->rect.h)
                r->hover_tool = d->id;
        }
    } else if (ev->type == TIKU_EVENT_POINTER_DOWN ||
               ev->type == TIKU_EVENT_KEY_DOWN ||
               ev->type == TIKU_EVENT_DEACTIVATED) r->hover_tool = 0;
    int key = ev->type == TIKU_EVENT_KEY_DOWN;
    if (ev->type == TIKU_EVENT_DEACTIVATED) r->location_input.dragging = 0;
    if (key && (ev->modifiers & TIKU_MOD_CMD)) {
        if (ev->key == 'l' || ev->key == 'L') {
            tiku_reader_document_action(c, HELP_LOCATION); return 1;
        }
        if (ev->key == 'f' || ev->key == 'F') {
            tiku_reader_document_find(c, 1); return 1;
        }
        /* Navigation accelerators are not text-editing commands. */
        if (ev->key == 'o' || ev->key == 'O' ||
            ev->key == 'r' || ev->key == 'R' ||
            ev->key == 'd' || ev->key == 'g' || ev->key == '+' ||
            ev->key == '=' || ev->key == '-' || ev->key == '0') return 0;
    }
    if (key && (ev->modifiers & TIKU_MOD_OPTION) &&
        (ev->key == TIKU_KEY_LEFT || ev->key == TIKU_KEY_RIGHT)) return 0;
    if (key && ev->key == TIKU_KEY_ESCAPE) {
        if (c->dialog.focus == HELP_LOCATION) {
            tiku_reader_document_location_sync(c);
            tiku_reader_say(c, "");
            tiku_dialog_focus(&c->dialog, HELP_PAGE); return 1;
        }
        if (r->find_visible) { tiku_reader_document_find(c, 0); return 1; }
    }
    if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1 &&
        ev->x >= l.location.x && ev->x < l.location.x+l.location.w &&
        ev->y >= l.location.y && ev->y < l.location.y+l.location.h)
        tiku_dialog_focus(&c->dialog, HELP_LOCATION);
    if (c->dialog.focus != HELP_LOCATION ||
        (key && ev->key == TIKU_KEY_TAB)) return 0;
    char old[TIKU_FIELD_MAX]; snprintf(old, sizeof old, "%s", r->location.text);
    tiku_field_result_t result = tiku_field_event(&r->location,
        &r->location_input, ev, l.location, 1);
    if (strcmp(old, r->location.text)) r->location_dirty = 1;
    if (result == TIKU_FIELD_COMMITTED) document_location_open(c);
    if (result == TIKU_FIELD_REJECTED)
        tiku_reader_say(c, "Address is invalid or too long; use Open for long paths.");
    return key || ev->type == TIKU_EVENT_TEXT_INPUT || r->location_input.dragging;
}
