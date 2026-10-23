/* tiku_pdfview_text.inl -- part of tiku_pdfview.c: what the document
 * says, read by the jobs and kept here: the outline and the side column,
 * links, selecting and copying text, finding, and the pointer on the pages.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */

/* ------------------------------------------------------------ outline  */

/** @brief The entries shown: each one, and under it only if unfolded. */
static void pv_outline_rows(tiku_pdfview_t *c)
{
    c->nrows = 0;
    for (int i = 0; i < c->noutline && c->nrows < TIKU_LIST_MAX; i++) {
        c->rows[c->nrows++] = i;
        if (c->folded[i]) i += c->outline[i].below;
    }
    (void)tiku_list_set_count(&c->list, c->nrows);
}

/** @brief Fold the outline as the document keeps it: an entry with
 *         entries under it is folded when the document marks it closed, or
 *         when the whole outline would not fit the list. */
static void pv_outline_start(tiku_pdfview_t *c)
{
    for (int i = 0; i < c->noutline; i++)
        c->folded[i] = c->outline[i].below > 0 &&
                       (!c->outline[i].open || c->noutline > TIKU_LIST_MAX);
    c->list.top = 0;
    c->follow = 1;
    pv_outline_rows(c);
}

/** @brief How far below its page's top destination @p to lies, in points,
 *         with the page turned as the view shows it. */
static double pv_dest_y(tiku_pdfview_t *c, const tiku_pdf_dest_t *to)
{
    double x = to->left, y = to->top;

    tiku_pdf_turn_point(c->pw[to->page], c->ph[to->page], c->turn, 0, &x,
                        &y);
    return y;
}

/** @brief Follow outline entry @p i: its place, or its address. */
static void pv_outline_open(tiku_pdfview_t *c, int i)
{
    const tiku_pdf_dest_t *to = &c->outline[i].to;

    if (to->page >= 0 && to->page < c->npage)
        pv_go(c, to->page, pv_dest_y(c, to), 1);
    else pv_open_address(c, to->uri);
}

/** @brief The row of the last entry at or above where the view is, or -1
 *         while the view is above them all. */
static int pv_outline_here(tiku_pdfview_t *c)
{
    int page = pv_page_at(c, c->vbar.value + PV_MARGIN), here = -1;
    double y = (c->vbar.value + PV_MARGIN - c->top[page]) / c->scale;

    for (int r = 0; r < c->nrows; r++) {
        const tiku_pdf_dest_t *to = &c->outline[c->rows[r]].to;
        if (to->page < 0 || to->page >= c->npage) continue;
        if (to->page > page || (to->page == page && pv_dest_y(c, to) > y + 1))
            break;
        here = r;
    }
    return here;
}

/** @brief The side column: the signatures or the thumbnails when shown,
 *         else the outline, its row for where the view is marked unless
 *         the list has the keyboard. */
static void pv_side_draw(tiku_pdfview_t *c, tiku_surface_t *s, tiku_rect_t r)
{
    int shown;

    if (c->sigs) {
        pv_sigs_draw(c, s, r);
        return;
    }
    if (c->thumbs) {
        pv_thumbs_draw(c, s, r);
        return;
    }
    tiku_list_set_row_h(&c->list, tiku_measure_row_h());
    tiku_ui_sunken(s, r, TIKU_C_DOC);
    if (c->dialog.focus != PV_SIDE && c->laid_scale) {
        int here = pv_outline_here(c);
        if (here < 0) {
            tiku_list_select_none(&c->list);
        } else if (here != tiku_list_chosen(&c->list) || c->follow) {
            tiku_list_select_only(&c->list, here);
            (void)tiku_list_reveal(&c->list, r);
        }
        c->follow = 0;
    }
    shown = tiku_list_visible_rows(&c->list, r);
    for (int row = c->list.top; row < c->nrows && row < c->list.top + shown;
         row++) {
        const tiku_pdf_outline_t *o = &c->outline[c->rows[row]];
        tiku_rect_t box = tiku_list_row_rect(&c->list, r, row);
        int sel = tiku_list_selected(&c->list, row), indent = 16 +
                  (o->level > 8 ? 8 : o->level) * 12;
        tiku_rgb_t ink = sel ? TIKU_C_SELTEXT : TIKU_C_TEXT;

        if (sel) tiku_fill(s, box, TIKU_C_SELECT);
        if (o->below)
            tiku_text_line_draw(s, (tiku_rect_t){box.x + indent - 12, box.y,
                                12, box.h}, TIKU_MEASURE_PLAIN,
                                c->folded[c->rows[row]] ? "+" : "-", ink);
        tiku_text_line_draw(s, (tiku_rect_t){box.x + indent, box.y,
                            box.w - indent - 4, box.h}, TIKU_MEASURE_PLAIN,
                            o->title, ink);
    }
}

/** @brief Pointer input for the side column: the signatures and the
 *         thumbnails take their own; on the outline the wheel scrolls, and
 *         a click on an entry's sign folds it, on its title follows it. */
static int pv_side_pointer(tiku_pdfview_t *c, const tiku_event_t *ev,
                           const pv_layout_t *l)
{
    int row;

    if (!l->side.w || !pv_in(l->side, ev->x, ev->y)) return 0;
    if (c->sigs) return pv_sigs_pointer(c, ev, l);
    if (c->thumbs) return pv_thumbs_pointer(c, ev, l);
    if (ev->type == TIKU_EVENT_WHEEL) {
        (void)tiku_list_scroll_to(&c->list, c->list.top + ev->dy, l->side);
        pv_changed(c);
        return 1;
    }
    if (ev->type != TIKU_EVENT_POINTER_DOWN || ev->button != 1) return 1;
    row = tiku_list_at(&c->list, l->side, ev->x, ev->y);
    if (row >= 0) {
        int i = c->rows[row];
        int indent = 16 + (c->outline[i].level > 8 ? 8 : c->outline[i].level)
                          * 12;
        tiku_list_select_only(&c->list, row);
        if (c->outline[i].below && ev->x < l->side.x + indent) {
            c->folded[i] = !c->folded[i];
            pv_outline_rows(c);
        } else {
            pv_outline_open(c, i);
        }
    }
    pv_changed(c);
    return 1;
}

/** @brief Keys for the side column: the signatures and the thumbnails
 *         take their own; on the outline the arrows move and fold, Return
 *         follows. */
static int pv_side_key(tiku_pdfview_t *c, const tiku_event_t *ev,
                       const pv_layout_t *l)
{
    int row = c->list.cursor, i;

    if (ev->key == TIKU_KEY_TAB || ev->key == TIKU_KEY_ESCAPE) return 0;
    if (c->sigs) return pv_sigs_key(c, ev);
    if (c->thumbs) return pv_thumbs_key(c, ev);
    if (row >= 0 && row < c->nrows) {
        i = c->rows[row];
        if ((ev->key == TIKU_KEY_LEFT || ev->key == TIKU_KEY_RIGHT) &&
            c->outline[i].below) {
            c->folded[i] = ev->key == TIKU_KEY_LEFT;
            pv_outline_rows(c);
            pv_changed(c);
            return 1;
        }
        if (ev->key == TIKU_KEY_RETURN) {
            pv_outline_open(c, i);
            return 1;
        }
    }
    (void)tiku_list_key(&c->list, ev->key, ev->modifiers, l->side);
    pv_changed(c);
    return 1;
}

/* -------------------------------------------------------------- links  */

/** @brief Put the address a link gives on the clipboard and say so in
 *         the status line; no browser is opened. */
static void pv_open_address(tiku_pdfview_t *c, const char *uri)
{
    char said[320];

    if (!uri) {
        pv_say(c, "That entry leads nowhere in this document.");
        return;
    }
    if (tiku_clipboard_import(uri, strlen(uri)))
        snprintf(said, sizeof said, "Address copied; no browser was "
                 "launched: %.240s", uri);
    else snprintf(said, sizeof said, "An address: %.280s", uri);
    pv_say(c, said);
}

/** @brief The page under canvas point @p x, @p y, and the point on it in
 *         the page's own points, as drawn: the view's turn taken back.
 *         @return the page, or -1 for none. */
static int pv_point(tiku_pdfview_t *c, tiku_rect_t canvas, int x, int y,
                    double *ux, double *uy)
{
    long at = c->vbar.value + (y - canvas.y);
    int p;

    if (!c->npage || !c->laid_scale) return -1;
    p = pv_page_at(c, at);
    *ux = (x - pv_page_x(c, canvas, p)) / c->scale;
    *uy = (at - c->top[p]) / c->scale;
    tiku_pdf_turn_point(c->pw[p], c->ph[p], c->turn, 1, ux, uy);
    return p;
}

/** @brief The link under canvas point @p x, @p y, its page into @p page;
 *         -1 when there is none, or while the page's links are unread. */
static int pv_link_at(tiku_pdfview_t *c, tiku_rect_t canvas, int x, int y,
                      int *page)
{
    double ux, uy;
    int p = pv_point(c, canvas, x, y, &ux, &uy);

    *page = p;
    if (p < 0 || !c->pages[p].text) return -1;
    for (int i = 0; i < c->pages[p].nlink; i++) {
        const tiku_pdf_link_t *k = &c->pages[p].link[i];
        if (ux >= k->x0 && ux < k->x1 && uy >= k->y0 && uy < k->y1) return i;
    }
    return -1;
}

/** @brief Follow link @p i on page @p p. */
static void pv_follow(tiku_pdfview_t *c, int p, int i)
{
    const tiku_pdf_dest_t *to = &c->pages[p].link[i].to;
    char said[64];

    if (to->page >= 0 && to->page < c->npage) {
        pv_go(c, to->page, pv_dest_y(c, to), 1);
        snprintf(said, sizeof said, "Page %d. Ctrl+[ goes back.",
                 to->page + 1);
        pv_say(c, said);
    } else {
        pv_open_address(c, to->uri);
    }
}

/** @brief What the link under the pointer would do, for the status line. */
static void pv_hover(tiku_pdfview_t *c, tiku_rect_t canvas, int x, int y)
{
    int page, i = pv_in(canvas, x, y) ? pv_link_at(c, canvas, x, y, &page)
                                      : -1;
    char hint[sizeof c->hint];

    hint[0] = 0;
    if (i >= 0) {
        const tiku_pdf_dest_t *to = &c->pages[page].link[i].to;
        if (to->page >= 0) snprintf(hint, sizeof hint, "Go to page %d",
                                    to->page + 1);
        else snprintf(hint, sizeof hint, "%.300s", to->uri);
    }
    if (i != c->link_at || (i >= 0 && page != c->link_page) ||
        strcmp(hint, c->hint)) {
        c->link_at = i;
        c->link_page = i >= 0 ? page : -1;
        snprintf(c->hint, sizeof c->hint, "%s", hint);
        pv_changed(c);
    }
}

/* ---------------------------------------------------------- selection  */

/** @brief Order two marks in the text. */
static int pv_before(pv_mark_t a, pv_mark_t b)
{
    return a.page < b.page || (a.page == b.page && a.at < b.at);
}

/** @brief The gap between characters nearest canvas point @p x, @p y:
 *         the nearest character's either side, as its middle falls.
 *         @return 0 while the page's text is unread. */
static int pv_mark_at(tiku_pdfview_t *c, tiku_rect_t canvas, int x, int y,
                      pv_mark_t *m)
{
    double ux, uy, best = 1e30;
    int p = pv_point(c, canvas, x, y, &ux, &uy);
    long hit = -1;

    if (p < 0 || !c->pages[p].text) return 0;
    for (long i = 0; i < c->pages[p].nch; i++) {
        const tiku_pdf_char_t *k = &c->pages[p].ch[i];
        double dx, dy;
        if (k->x1 <= k->x0) continue;
        dx = ux < k->x0 ? k->x0 - ux : ux > k->x1 ? ux - k->x1 : 0;
        dy = uy < k->y0 ? k->y0 - uy : uy > k->y1 ? uy - k->y1 : 0;
        if (dy * 4 + dx < best) {
            best = dy * 4 + dx;
            hit = i;
        }
    }
    m->page = p;
    m->at = 0;
    if (hit >= 0) {
        const tiku_pdf_char_t *k = &c->pages[p].ch[hit];
        m->at = hit + (ux > (k->x0 + k->x1) / 2 ? 1 : 0);
    }
    return 1;
}

/** @brief The selection's two ends, in reading order. */
static void pv_range(tiku_pdfview_t *c, pv_mark_t *from, pv_mark_t *to)
{
    int flip = pv_before(c->focus, c->anchor);

    *from = flip ? c->focus : c->anchor;
    *to = flip ? c->anchor : c->focus;
}

/** @brief Page @p p's text selected, all of it. */
static void pv_select_page(tiku_pdfview_t *c, int p)
{
    if (p < 0 || p >= c->npage || !c->pages[p].text) {
        pv_say(c, "That page's text is still being read.");
        return;
    }
    c->anchor = (pv_mark_t){p, 0};
    c->focus = (pv_mark_t){p, c->pages[p].nch};
    c->selected = c->pages[p].nch > 0;
    pv_say(c, c->selected ? "The page's text is selected; Ctrl+C copies "
                            "it." : "This page has no text to select.");
}

/** @brief @p cp onto @p out as UTF-8, if it fits in @p cap. */
static size_t pv_utf8(char *out, size_t n, size_t cap, uint32_t cp)
{
    char b[4];
    size_t k;

    if (cp < 0x80) { b[0] = (char)cp; k = 1; }
    else if (cp < 0x800) {
        b[0] = (char)(0xc0 | cp >> 6); b[1] = (char)(0x80 | (cp & 0x3f));
        k = 2;
    } else if (cp < 0x10000) {
        b[0] = (char)(0xe0 | cp >> 12);
        b[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
        b[2] = (char)(0x80 | (cp & 0x3f));
        k = 3;
    } else {
        b[0] = (char)(0xf0 | cp >> 18);
        b[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
        b[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
        b[3] = (char)(0x80 | (cp & 0x3f));
        k = 4;
    }
    if (n + k >= cap) return n;
    memcpy(out + n, b, k);
    return n + k;
}

/**
 * @brief The selection from @p from to @p to as UTF-8 into @p out (room
 *        for @p room bytes and a NUL), up to the first character that
 *        does not fit; with @p out NULL, only measured.  @return its
 *        bytes, and in @p *chars its characters, a newline between pages.
 */
static size_t pv_selection_text(const tiku_pdfview_t *c, const pv_mark_t *from,
                                const pv_mark_t *to, char *out, size_t room,
                                long *chars)
{
    size_t n = 0;

    *chars = 0;
    for (int p = from->page; p <= to->page; p++) {
        const pv_page_t *pg = &c->pages[p];
        long a = p == from->page ? from->at : 0;
        long b = p == to->page ? to->at : pg->nch;

        for (long i = p > from->page ? -1 : a; i < b; i++) {
            uint32_t cp;
            size_t k;

            if (i >= 0 && (!pg->text || i >= pg->nch)) break;
            cp = i < 0 ? '\n' : pg->ch[i].cp;
            k = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
            if (out != NULL) {
                if (n + k > room) return n;
                (void)pv_utf8(out, n, room + 1, cp);
            }
            n += k;
            (*chars)++;
        }
    }
    return n;
}

/** @brief Copy the selection's text to the clipboard, cut at a character
 *         when it passes the clipboard's limit, and say so; refused when
 *         the document does not allow copying. */
static void pv_copy(tiku_pdfview_t *c)
{
    char *text, said[160];
    pv_mark_t from, to;
    size_t need, n;
    long total, taken;
    int ok;

    if (!c->selected) {
        pv_say(c, "Drag across text to select it, or choose Select Page "
                  "Text.");
        return;
    }
    if (!c->may_copy) {
        pv_say(c, "The author of this PDF does not allow its text to be "
                  "copied.");
        return;
    }
    pv_range(c, &from, &to);
    need = pv_selection_text(c, &from, &to, NULL, 0, &total);
    if (need > TIKU_CLIPBOARD_LIMIT) need = TIKU_CLIPBOARD_LIMIT;
    if ((text = malloc(need + 1)) == NULL) {
        pv_say(c, "There is not enough memory to copy that text.");
        return;
    }
    n = pv_selection_text(c, &from, &to, text, need, &taken);
    text[n] = 0;
    ok = tiku_clipboard_import(text, n);
    free(text);
    if (!ok) {
        pv_say(c, "The clipboard would not take that text.");
        return;
    }
    if (taken < total)
        snprintf(said, sizeof said, "Copied the first %zu bytes: the "
                 "clipboard holds no more.", n);
    else snprintf(said, sizeof said, "Copied %ld characters.", total);
    pv_say(c, said);
}

/* -------------------------------------------------------------- marks  */

/** @brief Characters @p a up to @p b of page @p p selected: each line's
 *         run of boxes tinted as one, so neighbours do not overlap into
 *         seams. */
static void pv_tint_run(tiku_pdfview_t *c, int p, int x, int y,
                        const tiku_pdf_char_t *ch, long a, long b)
{
    float run[4];
    int open = 0;

    for (long i = a; i < b; i++) {
        const tiku_pdf_char_t *k = &ch[i];
        if (k->x1 <= k->x0) continue;
        if (open && k->y0 < run[3] - 1 && k->y1 > run[1] + 1 &&
            k->x0 <= run[2] + 2 && k->x1 >= run[0]) {
            if (k->x1 > run[2]) run[2] = k->x1;
            if (k->y0 < run[1]) run[1] = k->y0;
            if (k->y1 > run[3]) run[3] = k->y1;
            continue;
        }
        if (open) pv_tint_box(c, p, x, y, run, 0x603d7fe0u);
        run[0] = k->x0; run[1] = k->y0; run[2] = k->x1; run[3] = k->y1;
        open = 1;
    }
    if (open) pv_tint_box(c, p, x, y, run, 0x603d7fe0u);
}

/** @brief What is marked on page @p p where it is shown at @p x, @p y:
 *         the selection, the search's matches, the signature fields and the
 *         link under the pointer. */
static void pv_marks(tiku_pdfview_t *c, int p, int x, int y)
{
    const pv_page_t *pg = &c->pages[p];

    if (c->selected && pg->text) {
        pv_mark_t from, to;
        pv_range(c, &from, &to);
        if (p >= from.page && p <= to.page) {
            long a = p == from.page ? from.at : 0;
            long b = p == to.page ? to.at : pg->nch;
            pv_tint_run(c, p, x, y, pg->ch, a, b < pg->nch ? b : pg->nch);
        }
    }
    for (int m = 0; m < c->nmatch; m++) {
        if (c->match[m].page != p) continue;
        for (int r = 0; r < c->match[m].nr; r++)
            pv_tint_box(c, p, x, y, c->match[m].r[r], m == c->cur ?
                        0x90ff8c00u : 0x70ffd400u);
    }
    pv_sig_marks(c, p, x, y);
    if (c->link_page == p && c->link_at >= 0 && pg->text &&
        c->link_at < pg->nlink) {
        const tiku_pdf_link_t *k = &pg->link[c->link_at];
        float box[4] = { k->x0, k->y0, k->x1, k->y1 };
        pv_tint_box(c, p, x, y, box, 0x302a6fdbu);
    }
}

/* ------------------------------------------------------------- finding  */

/** @brief The UTF-8 in @p text as folded characters, spaces run together
 *         and trimmed, case kept with @p keep_case.  @return how many,
 *         into @p q. */
static int pv_query(const char *text, uint32_t *q, int max, int keep_case)
{
    const unsigned char *s = (const unsigned char *)text;
    int n = 0;

    while (*s && n < max) {
        uint32_t cp = *s++;
        int more = cp >= 0xf0 ? 3 : cp >= 0xe0 ? 2 : cp >= 0xc0 ? 1 : 0;
        if (more) cp &= 0x3f >> more;
        while (more-- > 0 && (*s & 0xc0) == 0x80) cp = cp << 6 | (*s++ & 0x3f);
        cp = pv_fold(cp, keep_case);
        if (cp == ' ' && (n == 0 || q[n - 1] == ' ')) continue;
        q[n++] = cp;
    }
    while (n > 0 && q[n - 1] == ' ') n--;
    return n;
}

/** @brief Show match @p m: the view goes to it and the status counts. */
static void pv_show_match(tiku_pdfview_t *c, int m)
{
    const pv_match_t *hit = &c->match[m];
    double box[4] = { 0, 0, 0, 0 };

    if (hit->nr) pv_turn_box(c, hit->page, hit->r[0], box);
    c->cur = m;
    if (c->laid_scale) {
        long at = c->top[hit->page] + (long)(box[1] * c->scale);
        if (at < c->vbar.value || at > c->vbar.value + c->laid_h - 40)
            tiku_scroll_to(&c->vbar, (int)(at - c->laid_h / 3));
    }
    pv_search_status(c);
}

/** @brief The next match in @p dir from where the view is, or from the
 *         one shown; while the search runs, waiting for one to arrive. */
static void pv_find_step(tiku_pdfview_t *c, int dir)
{
    int page = c->laid_scale ? pv_page_at(c, c->vbar.value) : 0, m;

    c->want_next = 0;
    if (!c->nmatch) {
        if (c->searching) c->want_next = dir;
        return;
    }
    if (c->cur >= 0) {
        m = c->cur + dir;
        if (m >= c->nmatch && c->searching) {
            c->want_next = dir;
            return;
        }
        pv_show_match(c, (m + c->nmatch) % c->nmatch);
        return;
    }
    if (dir > 0) {
        for (m = 0; m < c->nmatch && c->match[m].page < page; m++) {}
        if (m == c->nmatch && c->searching) {
            c->want_next = dir;
            return;
        }
    } else {
        for (m = c->nmatch - 1; m >= 0 && c->match[m].page > page; m--) {}
    }
    pv_show_match(c, (m + c->nmatch) % c->nmatch);
}

/** @brief Find the query in @p dir: a new query starts a search of the
 *         whole document in stretches; the same one steps along. */
static void pv_find(tiku_pdfview_t *c, int dir)
{
    uint32_t q[TIKU_FIELD_MAX];

    if (!c->doc) return;
    if (!pv_query(c->find.text, q, TIKU_FIELD_MAX, c->match_case)) {
        pv_say(c, "Type what to find.");
        return;
    }
    if (strcmp(c->find.text, c->query) == 0 && !c->search_failed) {
        pv_find_step(c, dir);
        pv_changed(c);
        return;
    }
    c->search_gen++;
    atomic_store(&c->doc->search, c->search_gen);
    free(c->match);
    c->match = NULL;
    c->nmatch = c->searched = 0;
    c->search_failed = c->search_limited = 0;
    c->cur = -1;
    snprintf(c->query, sizeof c->query, "%s", c->find.text);
    c->searching = pv_search_post(c, 0);
    c->search_failed = !c->searching;
    c->want_next = c->searching ? dir : 0;
    pv_say(c, c->searching ? "Searching..." : "The workers are busy. Try "
                                              "again in a moment.");
}

/** @brief The page field's number: the view goes there. */
static void pv_go_typed(tiku_pdfview_t *c)
{
    char *end;
    long p = strtol(c->go.text, &end, 10);
    char said[96];

    if (end == c->go.text || p < 1 || p > c->npage) {
        snprintf(said, sizeof said, "There is no page %s; this document has "
                 "%d.", c->go.text, c->npage);
        pv_say(c, said);
        return;
    }
    pv_go(c, (int)p - 1, 0, 1);
    tiku_dialog_focus(&c->dialog, PV_CANVAS);
}

/* ------------------------------------------------------------- canvas  */

/** @brief The pointer on the pages: the wheel scrolls (and zooms with
 *         Ctrl), a click follows a link, a drag selects text. */
static int pv_canvas_pointer(tiku_pdfview_t *c, const tiku_event_t *ev,
                             const pv_layout_t *l)
{
    int inside = pv_in(l->canvas, ev->x, ev->y), page;

    if (!c->doc) return 0;
    if (ev->type == TIKU_EVENT_WHEEL && inside && c->doc) {
        if (ev->modifiers & TIKU_MOD_CMD) pv_zoom_step(c, ev->dy < 0 ? 1 : -1);
        else if (ev->modifiers & TIKU_MOD_SHIFT)
            pv_scroll(c, ev->dy * PV_LINE, 0);
        else pv_scroll(c, ev->dx * PV_LINE, ev->dy * PV_LINE);
        return 1;
    }
    if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1 && inside &&
        c->doc) {
        c->pressing = 1;
        c->press_x = ev->x;
        c->press_y = ev->y;
        /* On a link too: a click follows it, a drag selects its text. */
        c->press_link = pv_link_at(c, l->canvas, ev->x, ev->y, &c->press_page);
        c->selecting = pv_mark_at(c, l->canvas, ev->x, ev->y, &c->anchor);
        return 1;
    }
    if (ev->type == TIKU_EVENT_POINTER_MOVE && c->pressing) {
        int x = ev->x, y = ev->y, moved = abs(x - c->press_x) +
                                          abs(y - c->press_y) > 3;
        if (y < l->canvas.y) pv_scroll(c, 0, -PV_LINE / 2);
        if (y >= l->canvas.y + l->canvas.h) pv_scroll(c, 0, PV_LINE / 2);
        if (y < l->canvas.y) y = l->canvas.y;
        if (y >= l->canvas.y + l->canvas.h) y = l->canvas.y + l->canvas.h - 1;
        if (c->selecting && moved &&
            pv_mark_at(c, l->canvas, x, y, &c->focus)) {
            c->selected = pv_before(c->anchor, c->focus) ||
                          pv_before(c->focus, c->anchor);
            pv_changed(c);
        }
        return 1;
    }
    if (ev->type == TIKU_EVENT_POINTER_MOVE) {
        pv_hover(c, l->canvas, ev->x, ev->y);
        return 0;
    }
    if (ev->type == TIKU_EVENT_POINTER_UP && c->pressing) {
        int moved = abs(ev->x - c->press_x) + abs(ev->y - c->press_y) > 3;
        c->pressing = 0;
        if (!moved && c->press_link >= 0 &&
            pv_link_at(c, l->canvas, ev->x, ev->y, &page) == c->press_link &&
            page == c->press_page) {
            c->selected = 0;
            pv_follow(c, page, c->press_link);
        } else if (!moved) {
            c->selected = 0;
        }
        c->selecting = 0;
        pv_menus(c);
        pv_changed(c);
        return 1;
    }
    return 0;
}
