/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_view_text.inl - the PDF view's use of a document's text and
 * links: links followed, text selected and copied, matches found and
 * marked, and the pointer on the pages.  Included by tiku_pdf_view.c
 * alone.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* -------------------------------------------------------------- links  */

/** @brief How far down its page, in points as the view shows it, the
 *         place @p to leads to lands: turned with the view. */
static double vw_dest_y(const tiku_pdf_view_t *v, const tiku_pdf_dest_t *to)
{
    double x = to->left, y = to->top;

    tiku_pdf_turn_point(v->pw[to->page], v->ph[to->page], v->turn, 0, &x,
                        &y);
    return y;
}

/** @brief An address a link gives: to host->address when there is one,
 *         else onto the clipboard; said either way. */
static void vw_open_address(tiku_pdf_view_t *v, const char *uri)
{
    char said[320];

    if (!uri) {
        vw_say(v, "That leads nowhere in this document.");
        return;
    }
    if (v->host.address) {
        snprintf(said, sizeof said, "%.300s", uri);
        vw_say(v, said);
        v->host.address(v->host.ctx, uri);
        return;
    }
    if (tiku_clipboard_import(uri, strlen(uri)))
        snprintf(said, sizeof said, "Address copied: %.280s", uri);
    else snprintf(said, sizeof said, "An address: %.280s", uri);
    vw_say(v, said);
}

/** @brief The page under canvas point @p x, @p y, and the point on it in
 *         the page's own points, as drawn: the view's turn taken back.
 *         @return the page, or -1 for none. */
static int vw_point(const tiku_pdf_view_t *v, tiku_rect_t canvas, int x,
                    int y, double *ux, double *uy)
{
    long at = v->vbar.value + (y - canvas.y);
    int p;

    if (!v->npage || !v->laid_scale) return -1;
    p = vw_page_at(v, at);
    *ux = (x - vw_page_x(v, canvas, p)) / v->scale;
    *uy = (at - v->top[p]) / v->scale;
    tiku_pdf_turn_point(v->pw[p], v->ph[p], v->turn, 1, ux, uy);
    return p;
}

/** @brief The link under canvas point @p x, @p y into @p page; -1 when
 *         there is none, or the page's links are not read yet. */
static int vw_link_at(const tiku_pdf_view_t *v, tiku_rect_t canvas, int x,
                      int y, int *page)
{
    double ux, uy;
    int p = vw_point(v, canvas, x, y, &ux, &uy);

    *page = p;
    if (p < 0 || !v->pages[p].text) return -1;
    for (int i = 0; i < v->pages[p].nlink; i++) {
        const tiku_pdf_link_t *k = &v->pages[p].link[i];
        if (ux >= k->x0 && ux < k->x1 && uy >= k->y0 && uy < k->y1) return i;
    }
    return -1;
}

/** @brief Follow link @p i on page @p p. */
static void vw_follow_link(tiku_pdf_view_t *v, int p, int i)
{
    const tiku_pdf_dest_t *to = &v->pages[p].link[i].to;
    char said[64];

    if (to->page >= 0 && to->page < v->npage) {
        vw_go(v, to->page, vw_dest_y(v, to), 1);
        snprintf(said, sizeof said, "Page %d.", to->page + 1);
        vw_say(v, said);
    } else {
        vw_open_address(v, to->uri);
    }
}

/** @brief Where the link under the pointer leads, as the hint. */
static void vw_hover(tiku_pdf_view_t *v, tiku_rect_t canvas, int x, int y)
{
    int page = -1, i = vw_in(canvas, x, y)
                       ? vw_link_at(v, canvas, x, y, &page) : -1;
    char hint[sizeof v->hint];

    hint[0] = 0;
    if (i >= 0) {
        const tiku_pdf_dest_t *to = &v->pages[page].link[i].to;
        if (to->page >= 0) snprintf(hint, sizeof hint, "Go to page %d",
                                    to->page + 1);
        else if (to->uri) snprintf(hint, sizeof hint, "%.300s", to->uri);
    }
    if (i != v->link_at || (i >= 0 && page != v->link_page) ||
        strcmp(hint, v->hint)) {
        v->link_at = i;
        v->link_page = i >= 0 ? page : -1;
        snprintf(v->hint, sizeof v->hint, "%s", hint);
        vw_changed(v);
    }
}

/* ---------------------------------------------------------- selection  */

/** @brief Order two marks in the text. */
static int vw_before(vw_mark_t a, vw_mark_t b)
{
    return a.page < b.page || (a.page == b.page && a.at < b.at);
}

/** @brief The gap between characters nearest canvas point @p x, @p y:
 *         the nearest character's either side, as its middle falls.
 *         @return 0 when the page's text is not read yet. */
static int vw_mark_at(const tiku_pdf_view_t *v, tiku_rect_t canvas, int x,
                      int y, vw_mark_t *m)
{
    double ux, uy, best = 1e30;
    int p = vw_point(v, canvas, x, y, &ux, &uy);
    long hit = -1;

    if (p < 0 || !v->pages[p].text) return 0;
    for (long i = 0; i < v->pages[p].nch; i++) {
        const tiku_pdf_char_t *k = &v->pages[p].ch[i];
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
        const tiku_pdf_char_t *k = &v->pages[p].ch[hit];
        m->at = hit + (ux > (k->x0 + k->x1) / 2 ? 1 : 0);
    }
    return 1;
}

/** @brief The selection's two ends, in reading order. */
static void vw_range(const tiku_pdf_view_t *v, vw_mark_t *from, vw_mark_t *to)
{
    int flip = vw_before(v->focus, v->anchor);

    *from = flip ? v->focus : v->anchor;
    *to = flip ? v->anchor : v->focus;
}

/** @brief Page @p p's text selected, all of it.  @return 0 while it is
 *         still being read. */
static int vw_select_page(tiku_pdf_view_t *v, int p)
{
    if (p < 0 || p >= v->npage || !v->pages[p].text) {
        vw_say(v, "That page's text is still being read.");
        return 0;
    }
    v->anchor = (vw_mark_t){p, 0};
    v->focus = (vw_mark_t){p, v->pages[p].nch};
    v->selected = v->pages[p].nch > 0;
    vw_say(v, v->selected ? "The page's text is selected."
                          : "This page has no text to select.");
    return 1;
}

/** @brief @p cp onto @p out at @p n as UTF-8, if it fits in @p cap.
 *  @return the length after it. */
static size_t vw_utf8(char *out, size_t n, size_t cap, uint32_t cp)
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
static size_t vw_selection_text(const tiku_pdf_view_t *v,
                                const vw_mark_t *from, const vw_mark_t *to,
                                char *out, size_t room, long *chars)
{
    size_t n = 0;

    *chars = 0;
    for (int p = from->page; p <= to->page; p++) {
        const vw_page_t *pg = &v->pages[p];
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
                (void)vw_utf8(out, n, room + 1, cp);
            }
            n += k;
            (*chars)++;
        }
    }
    return n;
}

/** @brief The selection's text on the clipboard: all of it, but for what
 *         passes the clipboard's limit, cut at a character and said so.
 *  @return 1 when something was copied. */
static int vw_copy(tiku_pdf_view_t *v)
{
    char *text, said[160];
    vw_mark_t from, to;
    size_t need, n;
    long total, taken;
    int ok;

    if (!v->selected) {
        vw_say(v, "Drag across text to select it first.");
        return 0;
    }
    if (!v->may_copy) {
        vw_say(v, "The author of this PDF does not allow its text to be "
                  "copied.");
        return 0;
    }
    vw_range(v, &from, &to);
    need = vw_selection_text(v, &from, &to, NULL, 0, &total);
    if (need > TIKU_CLIPBOARD_LIMIT) need = TIKU_CLIPBOARD_LIMIT;
    if ((text = malloc(need + 1)) == NULL) {
        vw_say(v, "There is not enough memory to copy that text.");
        return 0;
    }
    n = vw_selection_text(v, &from, &to, text, need, &taken);
    text[n] = 0;
    ok = tiku_clipboard_import(text, n);
    free(text);
    if (!ok) {
        vw_say(v, "The clipboard would not take that text.");
        return 0;
    }
    if (taken < total)
        snprintf(said, sizeof said, "Copied the first %zu bytes: the "
                 "clipboard holds no more.", n);
    else snprintf(said, sizeof said, "Copied %ld characters.", total);
    vw_say(v, said);
    return 1;
}

/* -------------------------------------------------------------- marks  */

/** @brief Characters @p a up to @p b of page @p p selected: each line's
 *         run of boxes tinted as one, so neighbours leave no seams. */
static void vw_tint_run(tiku_pdf_view_t *v, int p, int x, int y,
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
        if (open) vw_tint_box(v, p, x, y, run, 0x603d7fe0u);
        run[0] = k->x0; run[1] = k->y0; run[2] = k->x1; run[3] = k->y1;
        open = 1;
    }
    if (open) vw_tint_box(v, p, x, y, run, 0x603d7fe0u);
}

/** @brief What is marked on page @p p where it is shown at @p x, @p y:
 *         the selection, what a search found, the link under the pointer. */
static void vw_marks(tiku_pdf_view_t *v, int p, int x, int y)
{
    const vw_page_t *pg = &v->pages[p];

    if (v->selected && pg->text) {
        vw_mark_t from, to;
        vw_range(v, &from, &to);
        if (p >= from.page && p <= to.page) {
            long a = p == from.page ? from.at : 0;
            long b = p == to.page ? to.at : pg->nch;
            vw_tint_run(v, p, x, y, pg->ch, a, b < pg->nch ? b : pg->nch);
        }
    }
    for (int m = 0; m < v->nmatch; m++) {
        if (v->match[m].page != p) continue;
        for (int r = 0; r < v->match[m].nr; r++)
            vw_tint_box(v, p, x, y, v->match[m].r[r], m == v->cur ?
                        0x90ff8c00u : 0x70ffd400u);
    }
    if (v->link_page == p && v->link_at >= 0 && pg->text &&
        v->link_at < pg->nlink) {
        const tiku_pdf_link_t *k = &pg->link[v->link_at];
        float box[4] = { k->x0, k->y0, k->x1, k->y1 };
        vw_tint_box(v, p, x, y, box, 0x302a6fdbu);
    }
}

/* ------------------------------------------------------------- finding  */

/** @brief The UTF-8 in @p text as folded characters, spaces run together
 *         and trimmed, case kept with @p keep_case.  @return how many,
 *         into @p q. */
static int vw_query(const char *text, uint32_t *q, int max, int keep_case)
{
    const unsigned char *s = (const unsigned char *)text;
    int n = 0;

    while (*s && n < max) {
        uint32_t cp = *s++;
        int more = cp >= 0xf0 ? 3 : cp >= 0xe0 ? 2 : cp >= 0xc0 ? 1 : 0;
        if (more) cp &= 0x3f >> more;
        while (more-- > 0 && (*s & 0xc0) == 0x80) cp = cp << 6 | (*s++ & 0x3f);
        cp = vw_fold(cp, keep_case);
        if (cp == ' ' && (n == 0 || q[n - 1] == ' ')) continue;
        q[n++] = cp;
    }
    while (n > 0 && q[n - 1] == ' ') n--;
    return n;
}

/** @brief Show match @p m: the view goes to it and the status counts. */
static void vw_show_match(tiku_pdf_view_t *v, int m)
{
    char said[96];
    const vw_match_t *hit = &v->match[m];
    double box[4] = { 0, 0, 0, 0 };

    if (hit->nr) vw_turn_box(v, hit->page, hit->r[0], box);
    v->cur = m;
    if (v->laid_scale) {
        long at = v->top[hit->page] + (long)(box[1] * v->scale);
        if (at < v->vbar.value || at > v->vbar.value + v->laid_h - 40)
            tiku_scroll_to(&v->vbar, (int)(at - v->laid_h / 3));
    } else {
        v->start_page = hit->page;
        v->start_y = box[1];
    }
    snprintf(said, sizeof said, "Match %d of %d%s.", m + 1, v->nmatch,
             v->searching ? " so far" : "");
    vw_say(v, said);
}

/** @brief The next match in @p dir from where the view is, or from the
 *         one shown; while the search runs, waiting for one to arrive. */
static void vw_find_step(tiku_pdf_view_t *v, int dir)
{
    int page = v->laid_scale ? vw_page_at(v, v->vbar.value) : 0, m;

    v->want_next = 0;
    if (!v->nmatch) {
        if (v->searching) v->want_next = dir;
        return;
    }
    if (v->cur >= 0) {
        m = v->cur + dir;
        if (m >= v->nmatch && v->searching) {
            v->want_next = dir;
            return;
        }
        vw_show_match(v, (m + v->nmatch) % v->nmatch);
        return;
    }
    if (dir > 0) {
        for (m = 0; m < v->nmatch && v->match[m].page < page; m++) {}
        if (m == v->nmatch && v->searching) {
            v->want_next = dir;
            return;
        }
    } else {
        for (m = v->nmatch - 1; m >= 0 && v->match[m].page > page; m--) {}
    }
    vw_show_match(v, (m + v->nmatch) % v->nmatch);
}

/** @brief Find @p text in @p dir: new text, or new @p flags, start a
 *         search of the whole document in stretches; the same text and
 *         flags step to the next match.  @return 0 with nothing open, an
 *         empty @p text, or no worker free. */
static int vw_find(tiku_pdf_view_t *v, const char *text, int dir,
                   unsigned flags)
{
    uint32_t q[VW_QUERY];

    if (!v->doc) return 0;
    if (!vw_query(text, q, VW_QUERY, (flags & TIKU_PDF_FIND_CASE) != 0)) {
        vw_say(v, "Type what to find.");
        return 0;
    }
    if (strcmp(text, v->query) == 0 && flags == v->find_flags) {
        vw_find_step(v, dir);
        vw_changed(v);
        return 1;
    }
    v->search_gen++;
    atomic_store(&v->doc->search, v->search_gen);
    free(v->match);
    v->match = NULL;
    v->nmatch = v->searched = 0;
    v->cur = -1;
    v->find_flags = flags;
    snprintf(v->query, sizeof v->query, "%s", text);
    v->searching = vw_search_post(v, 0);
    v->want_next = v->searching ? dir : 0;
    vw_say(v, v->searching ? "Searching..." : "The workers are busy. Try "
                                              "again in a moment.");
    return v->searching;
}

/* ------------------------------------------------------------- canvas  */

/** @brief A press on the pages: on a link, a click follows it and a drag
 *         selects its text all the same. */
static void vw_press(tiku_pdf_view_t *v, const tiku_event_t *ev,
                     const vw_layout_t *l)
{
    v->pressing = 1;
    v->press_x = ev->x;
    v->press_y = ev->y;
    v->press_link = vw_link_at(v, l->canvas, ev->x, ev->y, &v->press_page);
    v->selecting = vw_mark_at(v, l->canvas, ev->x, ev->y, &v->anchor);
}

/** @brief A drag from a press: the selection follows it, and the pages
 *         scroll when it leaves the canvas above or below. */
static void vw_drag(tiku_pdf_view_t *v, const tiku_event_t *ev,
                    const vw_layout_t *l)
{
    int x = ev->x, y = ev->y, moved = abs(x - v->press_x) +
                                      abs(y - v->press_y) > 3;

    if (y < l->canvas.y) vw_scroll(v, 0, -VW_LINE / 2);
    if (y >= l->canvas.y + l->canvas.h) vw_scroll(v, 0, VW_LINE / 2);
    if (y < l->canvas.y) y = l->canvas.y;
    if (y >= l->canvas.y + l->canvas.h) y = l->canvas.y + l->canvas.h - 1;
    if (v->selecting && moved && vw_mark_at(v, l->canvas, x, y, &v->focus)) {
        v->selected = vw_before(v->anchor, v->focus) ||
                      vw_before(v->focus, v->anchor);
        vw_changed(v);
    }
}

/** @brief The release that ends a press: a click where it began follows
 *         the link there, or lets the selection go. */
static void vw_release(tiku_pdf_view_t *v, const tiku_event_t *ev,
                       const vw_layout_t *l)
{
    int moved = abs(ev->x - v->press_x) + abs(ev->y - v->press_y) > 3;
    int page;

    v->pressing = 0;
    if (!moved && v->press_link >= 0 &&
        vw_link_at(v, l->canvas, ev->x, ev->y, &page) == v->press_link &&
        page == v->press_page) {
        v->selected = 0;
        v->hint[0] = 0;                 /* what it did is said instead */
        v->link_at = v->link_page = -1;
        vw_follow_link(v, page, v->press_link);
    } else if (!moved) {
        v->selected = 0;
    }
    v->selecting = 0;
    vw_changed(v);
}

/** @brief The pointer on the pages: the wheel scrolls (and zooms with
 *         Command), a click follows a link, a drag selects text. */
static int vw_canvas_pointer(tiku_pdf_view_t *v, const tiku_event_t *ev,
                             const vw_layout_t *l)
{
    int inside = vw_in(l->canvas, ev->x, ev->y);

    if (!v->doc) return 0;
    if (ev->type == TIKU_EVENT_WHEEL && inside) {
        if (ev->modifiers & TIKU_MOD_CMD) vw_zoom_step(v, ev->dy < 0 ? 1 : -1);
        else if (ev->modifiers & TIKU_MOD_SHIFT)
            vw_scroll(v, ev->dy * VW_LINE, 0);
        else vw_scroll(v, ev->dx * VW_LINE, ev->dy * VW_LINE);
        return 1;
    }
    if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1 && inside) {
        vw_press(v, ev, l);
        return 1;
    }
    if (ev->type == TIKU_EVENT_POINTER_MOVE && v->pressing) {
        vw_drag(v, ev, l);
        return 1;
    }
    if (ev->type == TIKU_EVENT_POINTER_MOVE) {
        vw_hover(v, l->canvas, ev->x, ev->y);
        return inside;
    }
    if (ev->type == TIKU_EVENT_POINTER_UP && v->pressing) {
        vw_release(v, ev, l);
        return 1;
    }
    return 0;
}
