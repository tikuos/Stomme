/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_assist.c - a list of words offered for a range of the
 * text and a tip beside it: kept on anchors, drawn over the text, and
 * given keys and presses before the text is.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"
#include "tiku_editdoc_private.h"
#include "tiku_ui.h"

#include <string.h>

/* Words a list shows at a time, the narrowest it is drawn, and the width
 * a tip's lines wrap at, in pixels. */
#define LIST_ROWS 5
#define LIST_LEAST 60
#define TIP_WRAP 320
/* Bytes of one drawn line of a tip, and a NUL. */
#define TIP_LINE 512

/** @brief A list row's height: the interface face's height and 4 pixels. */
static int
row_h(void)
{
    return tiku_font_plain()->height + 4;
}

/** @brief The words the list shows at a time: a rich list's own count,
 *         or LIST_ROWS. */
static size_t
list_rows(const tiku_codeview_t *v)
{
    return v->completion_rows > 0 ? v->completion_rows : LIST_ROWS;
}

/** @brief The width the list's word @p i takes: a rich list's kind, label
 *         and detail, or the word alone. */
static int
word_width(const tiku_codeview_t *v, const tiku_font_t *f, size_t i)
{
    const tiku_codeview_completion_item_t *item;

    if (v->completion_items == NULL)
        return tiku_text_width(f, v->list_items[i]);
    item = &v->completion_items[i];
    return 20 + tiku_text_width(f, item->label) + 12 +
           tiku_text_width(f, item->detail);
}

/** @brief Whether @p x, @p y is inside @p r. */
static int
inside(tiku_rect_t r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/** @brief The part of @p a inside @p b, empty when they do not meet. */
static tiku_rect_t
meet_rect(tiku_rect_t a, tiku_rect_t b)
{
    int x0 = a.x > b.x ? a.x : b.x;
    int y0 = a.y > b.y ? a.y : b.y;
    int x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    tiku_rect_t r;

    r.x = x0;
    r.y = y0;
    r.w = x1 > x0 ? x1 - x0 : 0;
    r.h = y1 > y0 ? y1 - y0 : 0;
    return r;
}

/** @brief Where the caret at @p pos is drawn, when its row is one of
 *         those shown or the one after them; a box is placed from it. */
static int
shown_point(tiku_codeview_t *v, tiku_edit_pos_t pos, int64_t *x, int64_t *y)
{
    int64_t top = tiku_cv_text_y(v);

    if (tiku_codeview_point(v, pos, x, y) != TIKU_EDIT_OK || *y < top)
        return 0;
    return (*y - top) / tiku_codeview_row_height(v) <=
           tiku_codeview_rows_visible(v);
}

/** @brief The width a tip's lines wrap at in the view's area now. */
static int
tip_width(const tiku_codeview_t *v)
{
    int width = tiku_cv_area(v).w - 14;

    if (width > TIP_WRAP)
        width = TIP_WRAP;
    return width > 1 ? width : 1;
}

/** @brief Put @p r under the row at @p y, or over it when only there is
 *         room, and keep it across inside @p area. */
static void
place(tiku_rect_t *r, int64_t y, int step, tiku_rect_t area)
{
    int64_t below = y + step;

    if (below + r->h > (int64_t)area.y + area.h && y - r->h >= area.y)
        r->y = (int)(y - r->h);
    else
        r->y = (int)below;
    if ((int64_t)r->x + r->w > (int64_t)area.x + area.w)
        r->x = area.x + area.w - r->w;
    if (r->x < area.x)
        r->x = area.x;
}

/** @brief The list's box, and the first word it shows in @p first: under
 *         or over the row of its range's start, its words lined up with
 *         the text there.  Empty while no word is shown. */
static tiku_rect_t
list_box(tiku_codeview_t *v, size_t *first)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t r = { 0, 0, 0, 0 };
    tiku_edit_pos_t start;
    int64_t x, y;
    size_t i, rows;
    int widest = 0;

    if (first != NULL)
        *first = 0;
    if (!v->list_shown || v->list_count == 0 ||
        tiku_editdoc_anchor_pos(v->doc, v->list_start, &start) !=
            TIKU_EDIT_OK ||
        !shown_point(v, start, &x, &y))
        return r;
    rows = v->list_count < list_rows(v) ? v->list_count : list_rows(v);
    for (i = 0; i < v->list_count; i++) {
        int w = word_width(v, f, i);

        if (w > widest)
            widest = w;
    }
    r.w = widest + 10 < LIST_LEAST ? LIST_LEAST : widest + 10;
    if (v->completion_items != NULL && r.w > tiku_cv_area(v).w)
        r.w = tiku_cv_area(v).w;
    r.h = (int)rows * row_h() + 2;
    r.x = (int)(x - 5);
    place(&r, y, tiku_codeview_row_height(v), tiku_cv_area(v));
    if (first != NULL)
        *first = v->list_current - v->list_current % list_rows(v);
    return r;
}

/** @brief Bytes of @p s, up to @p n, that fit in @p width pixels of @p f;
 *         at least one character when @p n is not 0. */
static size_t
fit(const tiku_font_t *f, const char *s, size_t n, int width)
{
    size_t at = 0;
    int w = 0;

    while (at < n) {
        unsigned char c = (unsigned char)s[at];
        size_t len = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        unsigned cp = c < 0x80 ? c : c & (c < 0xe0 ? 0x1f : c < 0xf0 ? 0x0f
                                                                   : 0x07);
        size_t k;

        if (at + len > n)
            len = n - at;
        for (k = 1; k < len; k++)
            cp = (cp << 6) | ((unsigned char)s[at + k] & 0x3f);
        w += tiku_text_advance(f, cp);
        if (w > width && at > 0)
            break;
        at += len;
    }
    return at;
}

/** @brief Takes one wrapped line of a tip: its @p n bytes at @p s, and its
 *         index among the tip's lines. */
typedef void (*line_fn)(void *ctx, const char *s, size_t n, int index);

/** @brief Break @p text at each LF, then at the last space that keeps a
 *         line within @p width, or inside a word longer than that, and
 *         hand each line to @p fn.  @return the lines, widest in
 *         @p widest. */
static int
wrap(const char *text, int width, line_fn fn, void *ctx, int *widest)
{
    const tiku_font_t *f = tiku_font_plain();
    int lines = 0;

    *widest = 0;
    for (;;) {
        const char *lf = strchr(text, '\n');
        size_t n = lf != NULL ? (size_t)(lf - text) : strlen(text);

        do {
            size_t take = fit(f, text, n, width), cut = take;
            int w;

            if (take > TIP_LINE - 1) {
                take = TIP_LINE - 1;
                while (take > 1 && (text[take] & 0xc0) == 0x80)
                    take--;
                cut = take;
            }
            if (take < n) {
                while (cut > 0 && text[cut] != ' ')
                    cut--;
                if (cut == 0)
                    cut = take;
            }
            w = 0;
            {
                char line[TIP_LINE];

                memcpy(line, text, cut);
                line[cut] = '\0';
                w = tiku_text_width(f, line);
            }
            if (w > *widest)
                *widest = w;
            if (fn != NULL)
                fn(ctx, text, cut, lines);
            lines++;
            text += cut;
            n -= cut;
            while (n > 0 && *text == ' ') {
                text++;
                n--;
            }
        } while (n > 0);
        if (lf == NULL)
            break;
        text = lf + 1;
    }
    return lines;
}

/** @brief The tip's box: beside a shown list, under it or over it when
 *         there is no room beside, or else under or over its position's
 *         row. */
static tiku_rect_t
tip_box(tiku_codeview_t *v)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t r = { 0, 0, 0, 0 }, list, area = tiku_cv_area(v);
    tiku_edit_pos_t at;
    int64_t x, y;
    int widest, lines;

    if (!v->tip_shown ||
        tiku_editdoc_anchor_pos(v->doc, v->tip_at, &at) != TIKU_EDIT_OK ||
        !shown_point(v, at, &x, &y))
        return r;
    lines = wrap(v->tip_text, tip_width(v), NULL, NULL, &widest);
    r.w = widest + 12;
    r.h = lines * (f->height + 2) + 4;
    list = list_box(v, NULL);
    if (list.w > 0) {
        r.x = list.x + list.w + 2;
        r.y = list.y;
        if ((int64_t)r.x + r.w <= (int64_t)area.x + area.w)
            return r;
        r.x = list.x;
        place(&r, list.y - 2, list.h + 4, area);
        return r;
    }
    r.x = (int)(x - 6);
    place(&r, y, tiku_codeview_row_height(v), area);
    return r;
}

/** @brief Free the list's words, a rich list's items and its range. */
static void
list_drop(tiku_codeview_t *v)
{
    tiku_editdoc_anchor_destroy(v->doc, v->list_start);
    tiku_editdoc_anchor_destroy(v->doc, v->list_end);
    tiku_ed_free(v->list_items);
    tiku_ed_free(v->completion_items);
    v->completion_items = NULL;
    v->completion_indices = NULL;
    v->completion_rows = 0;
    v->completion_accept[0] = '\0';
    v->list_items = NULL;
    v->list_count = v->list_current = 0;
    v->list_shown = 0;
}

tiku_edit_status_t
tiku_codeview_list_show(tiku_codeview_t *view,
                        const tiku_codeview_list_t *list)
{
    tiku_edit_anchor_t start, end;
    tiku_edit_status_t st;
    size_t i, bytes, words;
    char **items, *text;

    if (view == NULL || list == NULL || (list->count && list->items == NULL))
        return TIKU_EDIT_INVALID;
    if (list->range.start > list->range.end ||
        !tiku_editdoc_is_boundary(view->doc, list->range.start) ||
        !tiku_editdoc_is_boundary(view->doc, list->range.end))
        return TIKU_EDIT_INVALID_RANGE;
    if (list->count > SIZE_MAX / sizeof *items)
        return TIKU_EDIT_LIMIT;
    words = list->count * sizeof *items;
    bytes = words;
    for (i = 0; i < list->count; i++) {
        size_t n;

        if (list->items[i] == NULL)
            return TIKU_EDIT_INVALID;
        n = strlen(list->items[i]) + 1;
        if (bytes > SIZE_MAX - n)
            return TIKU_EDIT_LIMIT;
        bytes += n;
    }
    items = bytes ? tiku_ed_alloc(bytes) : NULL;
    if (bytes && items == NULL)
        return TIKU_EDIT_NO_MEMORY;
    st = tiku_editdoc_anchor_create(view->doc, list->range.start,
                                    TIKU_EDIT_LEFT, &start);
    if (st == TIKU_EDIT_OK) {
        st = tiku_editdoc_anchor_create(view->doc, list->range.end,
                                        TIKU_EDIT_RIGHT, &end);
        if (st != TIKU_EDIT_OK)
            tiku_editdoc_anchor_destroy(view->doc, start);
    }
    if (st != TIKU_EDIT_OK) {
        tiku_ed_free(items);
        return st;
    }
    text = items != NULL ? (char *)items + words : NULL;
    for (i = 0; i < list->count; i++) {
        size_t n = strlen(list->items[i]) + 1;

        items[i] = text;
        memcpy(text, list->items[i], n);
        text += n;
    }
    list_drop(view);
    view->list_items = items;
    view->list_count = list->count;
    view->list_current = list->current < list->count ? list->current : 0;
    view->list_start = start;
    view->list_end = end;
    view->list_inserts = list->inserts != 0;
    view->list_shown = 1;
    view->completion_accepted = 0;
    return TIKU_EDIT_OK;
}

void
tiku_codeview_list_hide(tiku_codeview_t *view)
{
    if (view != NULL && view->list_shown)
        list_drop(view);
}

int
tiku_codeview_list_shown(const tiku_codeview_t *view, size_t *current)
{
    if (current != NULL)
        *current = view != NULL ? view->list_current : 0;
    return view != NULL && view->list_shown;
}

tiku_edit_status_t
tiku_codeview_list_insert(tiku_codeview_t *view)
{
    tiku_edit_replace_t edit;
    tiku_edit_transaction_t t;
    tiku_edit_pos_t start, end;
    tiku_edit_status_t st;

    if (view == NULL)
        return TIKU_EDIT_INVALID;
    if (!view->list_shown || view->list_count == 0)
        return TIKU_EDIT_NOTHING;
    if (tiku_editdoc_anchor_pos(view->doc, view->list_start, &start) !=
            TIKU_EDIT_OK ||
        tiku_editdoc_anchor_pos(view->doc, view->list_end, &end) !=
            TIKU_EDIT_OK)
        return TIKU_EDIT_INVALID;
    edit.range.start = start;
    edit.range.end = end;
    edit.text = view->list_items[view->list_current];
    edit.length = strlen(edit.text);
    memset(&t, 0, sizeof t);
    t.edits = &edit;
    t.count = 1;
    t.view = view->view;
    t.select = 1;
    t.anchor = t.caret = start + (tiku_edit_pos_t)edit.length;
    st = tiku_editdoc_apply(view->doc, &t);
    if (st == TIKU_EDIT_OK) {
        if (view->completion_items != NULL) {
            view->completion_result =
                view->completion_indices[view->list_current];
            view->completion_accepted = 1;
        }
        list_drop(view);
        tiku_codeview_reveal(view);
    }
    return st;
}

tiku_edit_status_t
tiku_codeview_tip_show(tiku_codeview_t *view, tiku_edit_pos_t pos,
                       const char *text)
{
    tiku_edit_anchor_t at;
    tiku_edit_status_t st;
    size_t n;
    char *copy;

    if (view == NULL || text == NULL)
        return TIKU_EDIT_INVALID;
    if (!tiku_editdoc_is_boundary(view->doc, pos))
        return TIKU_EDIT_INVALID_RANGE;
    n = strlen(text) + 1;
    if (!tiku_ed_utf8_valid(text, n - 1))
        return TIKU_EDIT_INVALID_TEXT;
    copy = tiku_ed_alloc(n);
    if (copy == NULL)
        return TIKU_EDIT_NO_MEMORY;
    st = tiku_editdoc_anchor_create(view->doc, pos, TIKU_EDIT_LEFT, &at);
    if (st != TIKU_EDIT_OK) {
        tiku_ed_free(copy);
        return st;
    }
    memcpy(copy, text, n);
    tiku_codeview_tip_hide(view);
    view->tip_text = copy;
    view->tip_at = at;
    view->tip_shown = 1;
    view->tip_highlight.start = 0;
    view->tip_highlight.end = 0;
    return TIKU_EDIT_OK;
}

void
tiku_codeview_tip_hide(tiku_codeview_t *view)
{
    if (view == NULL || !view->tip_shown)
        return;
    tiku_editdoc_anchor_destroy(view->doc, view->tip_at);
    tiku_ed_free(view->tip_text);
    tiku_ed_free(view->tip_arguments);
    view->tip_arguments = NULL;
    view->tip_argument_count = 0;
    view->tip_text = NULL;
    view->tip_shown = 0;
}

int
tiku_codeview_tip_shown(const tiku_codeview_t *view)
{
    return view != NULL && view->tip_shown;
}

/** @brief Whether @p e is one of the extra keys that put the chosen word
 *         of a rich list in: a printable ASCII character the list names,
 *         typed without Option. */
static int
accept_key(const tiku_codeview_t *v, const tiku_event_t *e)
{
    return !(e->modifiers & TIKU_MOD_OPTION) && e->key >= ' ' &&
           e->key <= '~' && strchr(v->completion_accept, (int)e->key);
}

int
tiku_cv_assist_event(tiku_codeview_t *v, const tiku_event_t *e)
{
    tiku_rect_t r;
    size_t first;

    if (!v->list_shown && !v->tip_shown)
        return 0;
    if (e->type == TIKU_EVENT_KEY_DOWN) {
        if (e->key == TIKU_KEY_ESCAPE) {
            tiku_codeview_list_hide(v);
            tiku_codeview_tip_hide(v);
            return 1;
        }
        if (!v->list_shown || !v->list_inserts ||
            (e->modifiers & TIKU_MOD_CMD))
            return 0;
        if (e->key == TIKU_KEY_UP && v->list_count > 0)
            v->list_current = v->list_current > 0 ? v->list_current - 1
                                                  : v->list_count - 1;
        else if (e->key == TIKU_KEY_DOWN && v->list_count > 0)
            v->list_current = (v->list_current + 1) % v->list_count;
        if (e->key == TIKU_KEY_UP || e->key == TIKU_KEY_DOWN)
            return 1;
        if (e->key == TIKU_KEY_RETURN || e->key == TIKU_KEY_TAB ||
            accept_key(v, e)) {
            if (v->list_count > 0)
                v->status = tiku_codeview_list_insert(v);
            return 1;
        }
        return 0;
    }
    if (e->type != TIKU_EVENT_POINTER_DOWN || e->button != 1)
        return 0;
    r = list_box(v, &first);
    if (inside(r, e->x, e->y)) {
        int row = (e->y - r.y - 1) / row_h();

        if (e->y > r.y && row < (int)list_rows(v) &&
            first + (size_t)row < v->list_count) {
            v->list_current = first + (size_t)row;
            if (v->list_inserts)
                v->status = tiku_codeview_list_insert(v);
        }
        return 1;
    }
    return inside(tip_box(v), e->x, e->y);
}

void
tiku_cv_assist_follow(tiku_codeview_t *v)
{
    tiku_edit_pos_t at, anchor, caret;

    if (!v->tip_shown)
        return;
    if (v->tip_arguments != NULL) {
        tiku_cv_signature_follow(v);
        return;
    }
    if (tiku_editdoc_anchor_pos(v->doc, v->tip_at, &at) != TIKU_EDIT_OK ||
        tiku_codeview_selection(v, &anchor, &caret) != TIKU_EDIT_OK ||
        caret < at ||
        tiku_editdoc_line_of(v->doc, caret) !=
            tiku_editdoc_line_of(v->doc, at))
        tiku_codeview_tip_hide(v);
}

/* The surface a tip is drawn on, the tip's box and its view. */
typedef struct {
    tiku_surface_t *s;
    tiku_rect_t box;
    tiku_codeview_t *view;
} tip_paint_t;

/** @brief Draw bytes @p a to @p b of @p text, a tip's line drawn at
 *         @p x, @p y, highlighted: on the selection's ground, in its
 *         ink. */
static void
highlight_part(tip_paint_t *p, const char *text, size_t a, size_t b, int x,
               int y)
{
    const tiku_font_t *f = tiku_font_plain();
    char part[TIP_LINE];
    tiku_rect_t ground;

    memcpy(part, text, a);
    part[a] = '\0';
    x += tiku_text_width(f, part);
    memcpy(part, text + a, b - a);
    part[b - a] = '\0';
    ground.x = x;
    ground.y = y;
    ground.w = tiku_text_width(f, part);
    ground.h = f->height;
    tiku_fill(p->s, ground, TIKU_C_SELECT);
    tiku_text(p->s, f, x, y + f->ascent, part, TIKU_C_SELTEXT);
}

/** @brief Draw @p n bytes of @p text as line @p index of the tip, the
 *         part of it the tip's highlight covers highlighted. */
static void
tip_line(void *ctx, const char *text, size_t n, int index)
{
    const tiku_font_t *f = tiku_font_plain();
    tip_paint_t *p = ctx;
    tiku_edit_range_t h = p->view->tip_highlight;
    tiku_edit_pos_t at = text - p->view->tip_text;
    tiku_edit_pos_t a = h.start > at ? h.start - at : 0;
    tiku_edit_pos_t b = h.end - at < (tiku_edit_pos_t)n ? h.end - at
                                                        : (tiku_edit_pos_t)n;
    int y = p->box.y + 3 + index * (f->height + 2);
    char line[TIP_LINE];

    memcpy(line, text, n);
    line[n] = '\0';
    tiku_text(p->s, f, p->box.x + 6, y + f->ascent, line, TIKU_C_TEXT);
    if (a < b && a >= 0 && b > 0)
        highlight_part(p, text, (size_t)a, (size_t)b, p->box.x + 6, y);
}

/** @brief Draw the rich list's item @p item in the list's row @p row,
 *         chosen or not: its kind's letter in a box, its label, and its
 *         detail faint after it, cut to the row. */
static void
paint_item(tiku_surface_t *s, tiku_rect_t row,
           const tiku_codeview_completion_item_t *item, int chosen)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rgb_t ink = chosen ? TIKU_C_SELTEXT : TIKU_C_TEXT;
    char badge[2];
    int baseline = row.y + 2 + f->ascent;
    tiku_rect_t saved = s->clip, box;

    badge[0] = ".fvTk"[item->kind];
    badge[1] = '\0';
    tiku_clip_set(s, meet_rect(row, saved));
    box.x = row.x + 3;
    box.y = row.y + 2;
    box.w = 14;
    box.h = row.h - 4;
    tiku_frame(s, box, ink);
    tiku_text(s, f, row.x + 6, baseline, badge, ink);
    tiku_text(s, f, row.x + 22, baseline, item->label, ink);
    tiku_text(s, f, row.x + 34 + tiku_text_width(f, item->label), baseline,
              item->detail, chosen ? ink : tiku_ink(TIKU_INK_FAINT));
    tiku_clip_set(s, saved);
}

void
tiku_cv_assist_paint(tiku_codeview_t *v, tiku_surface_t *s)
{
    tiku_rect_t r;
    size_t first, i;
    int widest;

    if (v->tip_arguments != NULL)
        tiku_cv_signature_follow(v);
    r = list_box(v, &first);
    if (r.w > 0) {
        tiku_rect_t rest = { r.x + 1, r.y + 1, r.w - 2, r.h - 2 };

        tiku_fill(s, rest, TIKU_C_DOC);
        for (i = first; i < v->list_count && i < first + list_rows(v);
             i++) {
            tiku_rect_t row = { r.x + 1, r.y + 1 + (int)(i - first) * row_h(),
                                r.w - 2, row_h() };
            int chosen = i == v->list_current;

            if (v->completion_items == NULL) {
                tiku_ui_list_row(s, row, v->list_items[i], chosen);
            } else {
                tiku_ui_list_row(s, row, "", chosen);
                paint_item(s, row, &v->completion_items[i], chosen);
            }
        }
        tiku_frame(s, r, TIKU_C_TEXT);
    }
    r = tip_box(v);
    if (r.w > 0) {
        tip_paint_t p;

        p.s = s;
        p.box = r;
        p.view = v;
        tiku_fill(s, r, TIKU_C_NOTE);
        tiku_frame(s, r, TIKU_C_TEXT);
        (void)wrap(v->tip_text, tip_width(v), tip_line, &p, &widest);
    }
}

void
tiku_cv_assist_free(tiku_codeview_t *v)
{
    tiku_codeview_list_hide(v);
    tiku_codeview_tip_hide(v);
}
