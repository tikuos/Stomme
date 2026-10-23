/* tiku_reader_layout.c -- where a viewer window's parts go, for Help and
 * for a document window, and the page laid out for the room it has.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_reader_private.h"

static help_layout_t document_layout(tiku_reader_help_window_t *c, tiku_rect_t box);

/** @brief Where the window's parts go in content @p c; the drawing and
 *         the hit testing both read it. */
help_layout_t
tiku_reader_layout(tiku_reader_help_window_t *owner, tiku_rect_t c)
{
    if (owner->document_mode) return document_layout(owner, c);
    help_layout_t l = {0};
    int bh = tiku_ui_button_h();
    int fh = tiku_font_plain()->height;
    int top = c.y + 10;
    int foot = c.y + c.h - fh - 12;

    l.back = (tiku_rect_t){c.x + 12, top, 74, bh};
    l.forward = (tiku_rect_t){c.x + 92, top, 84, bh};
    l.home = (tiku_rect_t){c.x + 182, top, 74, bh};
    l.done = (tiku_rect_t){c.x + c.w - 86, top, 74, bh};
    l.title = (tiku_rect_t){c.x + 266, top, c.w - 364, bh};
    if (l.title.w < 0) l.title.w = 0;
    int width = c.w - 24;
    if (width < 300) width = 300;
    int field = width - 248;
    l.find = (tiku_rect_t){c.x + 12, top + bh + 6, field, bh};
    l.prev = (tiku_rect_t){l.find.x + field + 4, l.find.y, 48, bh};
    l.next = (tiku_rect_t){l.prev.x + 52, l.find.y, 48, bh};
    l.search = (tiku_rect_t){l.next.x + 52, l.find.y, 144, bh};
    int tools = top + 2 * (bh + 6);
    l.outline = (tiku_rect_t){c.x + 12, tools, 80, bh};
    l.bookmarks = (tiku_rect_t){c.x + 96, tools, 88, bh};
    l.bookmark = (tiku_rect_t){c.x + 188, tools, 38, bh};
    l.copy = (tiku_rect_t){c.x + 230, tools, 54, bh};
    l.code = (tiku_rect_t){c.x + 288, tools, 88, bh};
    l.link = (tiku_rect_t){c.x + 380, tools, 88, bh};
    l.paper = (tiku_rect_t){c.x + 12, tools + bh + 8, c.w - 24,
                            foot - (tools + bh + 8) - 8};
    if (c.w < 500) {
        l.copy.x = c.x + 12; l.code.x = c.x + 70;
        l.link.x = c.x + 162;
        l.copy.y = l.code.y = l.link.y = tools + bh + 6;
        l.paper.y += bh + 6; l.paper.h -= bh + 6;
    }
    l.side = (tiku_rect_t){0};
    if (owner->reader && owner->reader->side) {
        if (c.w >= 640) {
            l.side = (tiku_rect_t){l.paper.x, l.paper.y, 188, l.paper.h};
            l.paper.x += 196; l.paper.w -= 196;
        } else {
            l.side = (tiku_rect_t){l.paper.x, l.paper.y, l.paper.w, 96};
            l.paper.y += 104; l.paper.h -= 104;
        }
    }
    /* The paper is kept at least 40 by 24, so no rectangle inside it has
     * a negative size when the window is dragged small. */
    if (l.paper.w < 40) l.paper.w = 40;
    if (l.paper.h < 24) l.paper.h = 24;
    l.page = (tiku_rect_t){l.paper.x + 3, l.paper.y + 3,
                           l.paper.w - 6 - HELP_BAR, l.paper.h - 6};
    if (l.page.w < 16) l.page.w = 16;
    l.bar = (tiku_rect_t){l.paper.x + l.paper.w - HELP_BAR - 1, l.paper.y + 1,
                          HELP_BAR, l.paper.h - 2};
    l.status = (tiku_rect_t){c.x + 12, foot, c.w - 24, fh + 6};
    return l;
}

/**
 * @brief Lay the page out again for the room it already had, and set the
 *        scroll range from it.
 * @note A restored scroll position is clamped to the range, so it is
 *       restored after this call.
 */
void tiku_reader_settle(tiku_reader_help_window_t *c)
{
    if (!c->doc || c->laid_for <= 0) return;
    int font = tiku_reader_font_begin(c);
    tiku_reader_document_layout(&c->doc, c->laid_for);
    tiku_reader_font_end(font);
    tiku_scroll_set_range(&c->bar, tiku_doc_height(c->doc), c->page_h);
}

/** @brief Lay the page out for the room it has now. */
void tiku_reader_refit(tiku_reader_help_window_t *c, help_layout_t l)
{
    if (!c->doc) return;
    int font = tiku_reader_font_begin(c);
    tiku_doc_place_t place = tiku_doc_place(c->doc, c->bar.value);
    int had = tiku_doc_laid(c->doc);
    int changed = c->doc->laid_width != l.page.w ||
        c->doc->laid_height != tiku_measure_height(TIKU_MEASURE_PLAIN);
    c->page_h = l.page.h;
    c->laid_for = l.page.w;
    /* Laid out every time: tiku_doc_layout() returns at once when neither
     * the width nor the text size has changed, and a text size that
     * changed alone needs the new geometry. */
    tiku_reader_document_layout(&c->doc, l.page.w);
    tiku_scroll_set_range(&c->bar, tiku_doc_height(c->doc), l.page.h);
    if (had && changed)
        tiku_scroll_to(&c->bar, tiku_doc_place_y(c->doc, place));
    /* A page asked for by name is scrolled to it once it has a height. */
    if (c->anchor[0]) {
        int y = tiku_doc_anchor_y(c->doc, c->anchor);

        if (y != TIKU_DOC_NONE) tiku_scroll_to(&c->bar, y);
        else tiku_reader_say(c, tiku_str("help.no_such_place"));
        c->anchor[0] = 0;
    }
    tiku_reader_font_end(font);
}

/** @brief Register what can be pressed, and what may take the keyboard. */
void tiku_reader_controls(tiku_reader_help_window_t *c, help_layout_t l)
{
    tiku_dialog_begin(&c->dialog, TIKU_DIALOG_NONE,
                      c->document_mode ? TIKU_DIALOG_NONE : HELP_DONE);
    tiku_dialog_add(&c->dialog, HELP_PAGE, l.page, TIKU_DIALOG_INPUT, 1);
    tiku_dialog_add(&c->dialog, HELP_BACK, l.back, TIKU_DIALOG_BUTTON,
                    c->nback > 0);
    tiku_dialog_add(&c->dialog, HELP_FORWARD, l.forward, TIKU_DIALOG_BUTTON,
                    c->nforward > 0);
    tiku_dialog_add(&c->dialog, HELP_HOME, l.home, TIKU_DIALOG_BUTTON,
                    c->document_mode ? c->choose_file != NULL :
                    strcmp(c->page, "index.html") != 0);
    tiku_dialog_add(&c->dialog, HELP_DONE, l.done, TIKU_DIALOG_BUTTON,
                    !c->document_mode || c->page[0] == '/');
    if (c->reader) {
        if (l.find.w)
            tiku_dialog_add(&c->dialog, HELP_FIND, l.find,
                            TIKU_DIALOG_INPUT, 1);
        const int ids[] = {HELP_PREV, HELP_NEXT, HELP_SEARCH,
            HELP_COPY, HELP_CODE,
            HELP_OUTLINE, HELP_BOOKMARK, HELP_BOOKMARKS, HELP_LINK};
        const tiku_rect_t boxes[] = {l.prev, l.next, l.search, l.copy, l.code,
            l.outline, l.bookmark, l.bookmarks, l.link};
        for (int i = 0; i < 9; i++)
            if (boxes[i].w) tiku_dialog_add(&c->dialog, ids[i], boxes[i],
                            TIKU_DIALOG_BUTTON, 1);
        if (c->document_mode) {
            tiku_dialog_add(&c->dialog, HELP_LOCATION, l.location,
                            TIKU_DIALOG_INPUT, 1);
            tiku_dialog_add(&c->dialog, HELP_GO, l.go,
                            TIKU_DIALOG_BUTTON, 1);
            tiku_dialog_add(&c->dialog, HELP_FIND_TOGGLE, l.find_toggle,
                            TIKU_DIALOG_BUTTON, 1);
            if (l.find_close.w)
                tiku_dialog_add(&c->dialog, HELP_FIND_CLOSE, l.find_close,
                                TIKU_DIALOG_BUTTON, 1);
        }
        if (l.side.w) tiku_dialog_add(&c->dialog, HELP_SIDE, l.side,
                                      TIKU_DIALOG_INPUT, 1);
    }
    tiku_dialog_end(&c->dialog);
}

/** @brief Put the focused link on the screen if it is off it. */
void tiku_reader_reveal(tiku_reader_help_window_t *c, help_layout_t l)
{
    int first = 0, n = 0;
    tiku_rect_t r;

    if (!c->doc || c->focus_link == TIKU_DOC_NONE) return;
    if (!tiku_doc_link_pieces(c->doc, c->focus_link, &first, &n)) return;
    r = tiku_doc_piece_rect(c->doc, first);
    if (r.y < c->bar.value) tiku_scroll_to(&c->bar, r.y);
    else if (r.y + r.h > c->bar.value + l.page.h) {
        tiku_scroll_to(&c->bar, r.y + r.h - l.page.h);
    }
}

/** @brief A document window's parts: the icon buttons and the Location
 *         field on one row, the field below on a narrow window; the Find
 *         bar over the status line when shown; and the page, narrowed to
 *         the reading width when one is set.  Help has none of these. */
static help_layout_t document_layout(tiku_reader_help_window_t *c, tiku_rect_t box)
{
    help_layout_t l = {0};
    int bh = tiku_ui_button_h(), fh = tiku_font_plain()->height;
    int x = box.x + 6, y = box.y + 5;
    int bw = bh > 30 ? bh : 30;
    if (bw > (box.w-26)/5) bw = (box.w-26)/5;
    l.back = (tiku_rect_t){x, y, bw, bw};
    l.forward = (tiku_rect_t){x + bw + 2, y, bw, bw};
    l.done = (tiku_rect_t){x + 2 * (bw + 2), y, bw, bw};
    l.home = (tiku_rect_t){x + 3 * (bw + 2) + 4, y, bw, bw};
    l.find_toggle = (tiku_rect_t){box.x + box.w - bw - 6, y, bw, bw};
    int address_x = l.home.x + bw + 6;
    int address_right = l.find_toggle.x - 4;
    if (address_right - address_x < 210) {
        y += bw + 4;
        address_x = x; address_right = box.x + box.w - 6;
    }
    int row_h = bw > bh ? bw : bh;
    l.location = (tiku_rect_t){address_x, y + (row_h-bh)/2,
        address_right - address_x - bw - 2, bh};
    l.go = (tiku_rect_t){address_right - bw, y + (row_h-bw)/2, bw, bw};
    y += row_h + 5;
    l.status = (tiku_rect_t){x, box.y + box.h - fh - 8, box.w - 16, fh + 4};
    l.status.x += 22; l.status.w -= 22;
    int bottom = l.status.y - 5;
    if (c->reader && c->reader->find_visible) {
        int prev = tiku_ui_button_w(tiku_str("help.prev"), 48);
        int next = tiku_ui_button_w(tiku_str("help.next"), 48);
        int search = tiku_ui_button_w(tiku_str("reader.search_folder"), 100);
        bottom -= bh + 8;
        l.find_close = (tiku_rect_t){x, bottom, bh, bh};
        l.find = (tiku_rect_t){x + bh + 4, bottom,
            box.w - bh - prev - next - search - 32, bh};
        /* Folder search stays in Edit on narrow windows. */
        if (l.find.w < 100) { l.find.w += search + 4; search = 0; }
        if (l.find.w < 70) l.find.w += prev + next + 8;
        else {
            l.prev = (tiku_rect_t){l.find.x + l.find.w + 4, bottom, prev, bh};
            l.next = (tiku_rect_t){l.prev.x + prev + 4, bottom, next, bh};
        }
        if (search)
            l.search = (tiku_rect_t){l.next.x + next + 4, bottom, search, bh};
        bottom -= 4;
    }
    l.paper = (tiku_rect_t){box.x + 4, y, box.w - 8, bottom - y};
    if (c->reader && c->reader->side) {
        if (box.w >= 640) {
            l.side = (tiku_rect_t){l.paper.x, y, 188, l.paper.h};
            l.paper.x += 192; l.paper.w -= 192;
        } else {
            l.side = (tiku_rect_t){l.paper.x, y, l.paper.w, 96};
            l.paper.y += 100; l.paper.h -= 100;
        }
    }
    if (l.paper.w < 40) l.paper.w = 40;
    if (l.paper.h < 24) l.paper.h = 24;
    l.page = (tiku_rect_t){l.paper.x + 10, l.paper.y + 8,
        l.paper.w - HELP_BAR - 20, l.paper.h - 16};
    if (c->reader && c->reader->reading_width &&
        l.page.w > c->reader->reading_width) {
        l.page.x += (l.page.w - c->reader->reading_width) / 2;
        l.page.w = c->reader->reading_width;
    }
    l.bar = (tiku_rect_t){l.paper.x + l.paper.w - HELP_BAR - 1,
        l.paper.y + 1, HELP_BAR, l.paper.h - 2};
    return l;
}
