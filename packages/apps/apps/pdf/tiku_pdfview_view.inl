/* tiku_pdfview_view.inl -- part of tiku_pdfview.c: the view: pages laid
 * out one under another at one scale, the canvas composed from the pages
 * drawn so far, the thumbnails, the toolbar and the status line.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */

/** @brief Page @p p's width in pixels at the scale and turn shown. */
static int pv_page_w(tiku_pdfview_t *c, int p)
{
    int w = (int)(pv_pw(c, p) * c->scale + 0.5);
    return w > 0 ? w : 1;
}

/** @brief Page @p p's height in pixels at the scale and turn shown. */
static int pv_page_h(tiku_pdfview_t *c, int p)
{
    int h = (int)(pv_ph(c, p) * c->scale + 0.5);
    return h > 0 ? h : 1;
}

/** @brief The page at @p y down the laid-out pages: the last one whose
 *         top is at or above it. */
static int pv_page_at(tiku_pdfview_t *c, long y)
{
    int lo = 0, hi = c->npage - 1;

    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (c->top[mid] <= y) lo = mid; else hi = mid - 1;
    }
    return lo;
}

/** @brief The page the view is on: the one across its middle. */
static int pv_current(tiku_pdfview_t *c)
{
    if (!c->npage || !c->laid_scale) return 0;
    return pv_page_at(c, c->vbar.value + c->laid_h / 2);
}

/** @brief Where page @p p's left edge sits on the canvas. */
static int pv_page_x(tiku_pdfview_t *c, tiku_rect_t canvas, int p)
{
    if (c->width <= canvas.w)
        return canvas.x + (canvas.w - pv_page_w(c, p)) / 2;
    return canvas.x + PV_MARGIN - c->hbar.value;
}

/** @brief The scale the fit asks for in @p canvas, or the one set. */
static double pv_fit_scale(tiku_pdfview_t *c, tiku_rect_t canvas)
{
    double s = c->scale;
    int p = c->laid_scale ? pv_current(c) : 0;

    if (c->fit == PV_BY_WIDTH)
        s = (canvas.w - 2.0 * PV_MARGIN) / pv_pw(c, c->widest);
    else if (c->fit == PV_BY_PAGE)
        s = fmin((canvas.w - 2.0 * PV_MARGIN) / pv_pw(c, p),
                 (canvas.h - 2.0 * PV_MARGIN) / pv_ph(c, p));
    return fmax(0.05, fmin(8.0, s));
}

/** @brief Lay the pages out at the scale @p canvas and the fit give,
 *         keeping the page the view was on where it was: as far down it
 *         as it was laid out before, whatever its turn was then. */
static void pv_relayout(tiku_pdfview_t *c, tiku_rect_t canvas)
{
    double s, frac = 0;
    long y = PV_MARGIN, widest = 0, span;
    int page = 0;

    if (!c->npage) return;
    s = pv_fit_scale(c, canvas);
    if (canvas.w == c->laid_w && canvas.h == c->laid_h &&
        fabs(s - c->laid_scale) < 1e-9) return;
    if (c->laid_scale) {
        page = pv_page_at(c, c->vbar.value);
        span = (page + 1 < c->npage ? c->top[page + 1]
                                    : c->height + PV_GAP - PV_MARGIN) -
               c->top[page];
        frac = span > 0 ? (double)(c->vbar.value - c->top[page]) / span : 0;
    }
    c->scale = s;
    for (int p = 0; p < c->npage; p++) {
        c->top[p] = y;
        y += pv_page_h(c, p) + PV_GAP;
        if (pv_page_w(c, p) > widest) widest = pv_page_w(c, p);
    }
    c->height = y - PV_GAP + PV_MARGIN;
    c->width = widest + 2 * PV_MARGIN;
    c->laid_w = canvas.w;
    c->laid_h = canvas.h;
    c->laid_scale = s;
    tiku_scroll_set_range(&c->vbar, (int)c->height, canvas.h);
    tiku_scroll_set_range(&c->hbar, (int)c->width, canvas.w);
    tiku_scroll_to(&c->vbar, (int)(c->top[page] +
                                   frac * (pv_page_h(c, page) + PV_GAP)));
}

/** @brief Change the fit, or the scale when the fit is none; the page
 *         in view stays in view. */
static void pv_set_scale(tiku_pdfview_t *c, int fit, double scale)
{
    c->fit = fit;
    if (fit == PV_FIXED) c->scale = fmax(0.25, fmin(4.0, scale));
    c->laid_w = 0;                  /* lay out again at the next frame */
    c->selecting = 0;
    pv_changed(c);
}

/** @brief Turn the view @p by degrees more (90 or 270).  The pages drawn
 *         so far turn with it, so they show at once; thumbnails are drawn
 *         again, and the page in view stays in view. */
static void pv_turn(tiku_pdfview_t *c, int by)
{
    c->turn = (c->turn + by) % 360;
    for (int i = 0; i < PV_TILES; i++) {
        pv_tile_t *t = &c->tile[i];
        if (t->px && pv_turn_pixels(&t->px, &t->w, &t->h, by) != 0)
            t->page = -1;
    }
    for (int i = 0; i < PV_THUMB_KEEP; i++) {
        if (c->thumb[i].pending) continue;  /* dropped when it lands */
        free(c->thumb[i].px);
        memset(&c->thumb[i], 0, sizeof c->thumb[i]);
        c->thumb[i].page = -1;
    }
    pv_find_widest(c);
    c->laid_w = 0;                  /* lay out again at the next frame */
    c->selecting = 0;
    pv_changed(c);
}

/** @brief Scroll by @p dx, @p dy pixels, within the pages. */
static void pv_scroll(tiku_pdfview_t *c, int dx, int dy)
{
    if (dy) tiku_scroll_to(&c->vbar, c->vbar.value + dy);
    if (dx) tiku_scroll_to(&c->hbar, c->hbar.value + dx);
    pv_changed(c);
}

/** @brief Bring page @p p into view with @p y points of it above the top;
 *         with @p remember, where the view was can be gone back to. */
static void pv_go(tiku_pdfview_t *c, int p, double y, int remember)
{
    if (p < 0 || p >= c->npage) return;
    if (remember && c->laid_scale) {
        if (c->nback == PV_HISTORY) {
            memmove(c->back, c->back + 1, sizeof c->back[0] *
                                          (PV_HISTORY - 1));
            c->nback--;
        }
        c->back[c->nback].page = pv_page_at(c, c->vbar.value);
        c->back[c->nback].y = (c->vbar.value -
                               c->top[c->back[c->nback].page]) / c->scale;
        c->nback++;
    }
    if (!c->laid_scale) return;
    tiku_scroll_to(&c->vbar, (int)(c->top[p] + y * c->scale) -
                             (y > 0 ? PV_GAP : PV_MARGIN));
    pv_changed(c);
}

/** @brief Go back to where the view was before the last jump. */
static void pv_back(tiku_pdfview_t *c)
{
    if (!c->nback || !c->laid_scale) return;
    c->nback--;
    tiku_scroll_to(&c->vbar, (int)(c->top[c->back[c->nback].page] +
                                   c->back[c->nback].y * c->scale));
    pv_changed(c);
}

/* ------------------------------------------------------------- layout  */

/** @brief A toolbar button's icon, from the desk's set. */
static const char *pv_tool_icon(int id)
{
    switch (id) {
    case PV_OPEN: return "folder_open";
    case PV_BACK: return "nav_back";
    case PV_OUTLINE: return "doc_outline";
    case PV_THUMBS: return "doc_pages";
    case PV_SIGS: return "doc_signed";
    case PV_PREV: return "page_up";
    case PV_NEXT: return "page_down";
    case PV_ZOOM_OUT: return "zoom_out";
    case PV_ZOOM_IN: return "zoom_in";
    case PV_FIT_WIDTH: return "fit_width";
    case PV_FIT_PAGE: return "fit_page";
    case PV_ROTATE_LEFT: return "rotate_left";
    case PV_ROTATE_RIGHT: return "rotate_right";
    case PV_FIND_PREV: return "find_prev";
    case PV_FIND_NEXT: return "find_next";
    }
    return "file";
}

/** @brief What a toolbar button does, as its tip says it: its name, and
 *         its shortcut when it has one. */
static const char *pv_tool_tip(int id)
{
    switch (id) {
    case PV_OPEN: return "Open (Ctrl+O)";
    case PV_BACK: return "Back (Ctrl+[)";
    case PV_OUTLINE: return "Outline";
    case PV_THUMBS: return "Pages";
    case PV_SIGS: return "Signatures";
    case PV_PREV: return "Previous Page";
    case PV_NEXT: return "Next Page";
    case PV_ZOOM_OUT: return "Zoom Out (Ctrl+-)";
    case PV_ZOOM_IN: return "Zoom In (Ctrl++)";
    case PV_FIT_WIDTH: return "Fit Width";
    case PV_FIT_PAGE: return "Fit Page";
    case PV_ROTATE_LEFT: return "Rotate Left (Ctrl+L)";
    case PV_ROTATE_RIGHT: return "Rotate Right (Ctrl+R)";
    case PV_FIND_PREV: return "Find Previous (Shift+Ctrl+G)";
    case PV_FIND_NEXT: return "Find Next (Ctrl+G)";
    }
    return "";
}

/** @brief A toolbar button's side: square, the button height but at
 *         least 30 pixels. */
static int pv_tool_size(void)
{
    int bh = tiku_ui_button_h();

    return bh > 30 ? bh : 30;
}

/** @brief The find field's width at @p x: what the row has left beside
 *         its two buttons, at most 170; the row's whole width when under 80
 *         is left. */
static int pv_find_w(tiku_rect_t r, int x)
{
    int room = r.x + r.w - 6 - x - 2 * (pv_tool_size() + 2) - 4;

    if (room < 80) room = r.w - 12;
    return room < 170 ? (room > 1 ? room : 1) : 170;
}

/** @brief The toolbar: square icon buttons in groups (the file, the side
 *         column, the pages, the zoom, the turn, finding) with the page and
 *         find fields among them, wrapped when narrow; while a document is
 *         locked, the password panel under them.  @return the row below. */
static int pv_tools_layout(tiku_pdfview_t *c, tiku_rect_t r, pv_layout_t *l)
{
    static const int ids[] = { PV_OPEN, PV_BACK, PV_OUTLINE, PV_THUMBS,
        PV_SIGS, PV_PREV, PV_PAGE_FIELD, PV_NEXT, PV_ZOOM_OUT, PV_ZOOM_IN,
        PV_FIT_WIDTH, PV_FIT_PAGE, PV_ROTATE_LEFT, PV_ROTATE_RIGHT,
        PV_FIND_FIELD, PV_FIND_PREV, PV_FIND_NEXT };
    int bh = tiku_ui_button_h(), bw = pv_tool_size(), x = r.x + 6;
    int y = r.y + 4;
    char of[32];

    snprintf(of, sizeof of, "of %d", c->npage);
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        int id = ids[i], w = bw, field = id == PV_PAGE_FIELD ||
                                          id == PV_FIND_FIELD;
        if (id == PV_SIGS && !c->nsig) continue;    /* signed ones only */
        if (id == PV_OUTLINE || id == PV_PREV || id == PV_ZOOM_OUT ||
            id == PV_ROTATE_LEFT || id == PV_FIND_FIELD) x += 10;
        if (id == PV_PAGE_FIELD)        /* "of N" beside it, once open */
            w = c->npage ? 58 + tiku_measure_width(TIKU_MEASURE_PLAIN, of)
                         : 48;
        if (id == PV_FIND_FIELD) w = pv_find_w(r, x);
        if (x > r.x + 16 && x + w > r.x + r.w - 6) {
            x = r.x + 6;
            y += bw + 4;
        }
        tiku_rect_t box = {x, field ? y + (bw - bh) / 2 : y, w,
                           field ? bh : bw};
        if (id == PV_PAGE_FIELD) {
            l->page = (tiku_rect_t){x + 2, box.y, 46, bh};
            l->of = (tiku_rect_t){x + 54, box.y, w > 52 ? w - 52 : 0, bh};
            (void)tiku_dialog_add(&c->dialog, id, l->page, TIKU_DIALOG_INPUT,
                                  c->doc != NULL);
        } else if (id == PV_FIND_FIELD) {
            l->find = box;
            (void)tiku_dialog_add(&c->dialog, id, box, TIKU_DIALOG_INPUT,
                                  c->doc != NULL);
        } else {
            (void)tiku_dialog_add(&c->dialog, id, box, TIKU_DIALOG_BUTTON,
                                  pv_enabled(c, id));
        }
        x += w + (field ? 4 : 2);
    }
    y += bw + 4;
    if (c->locked) {
        int rh = tiku_measure_row_h();
        l->password_label = (tiku_rect_t){r.x + 8, y, r.w - 16, rh};
        y += rh + 2;
        int width = r.w - 16;
        if (width > 320) width = 320;
        l->password = (tiku_rect_t){r.x + 8, y, width, bh};
        (void)tiku_dialog_add(&c->dialog, PV_PASS_FIELD, l->password,
                              TIKU_DIALOG_INPUT, !c->loading);
        if (c->password_focus && !c->loading) {
            tiku_dialog_focus(&c->dialog, PV_PASS_FIELD);
            c->password_focus = 0;
        }
        x = r.x + 8 + width + 8;
        int uw = tiku_ui_button_w("Unlock", 60),
            cw = tiku_ui_button_w("Cancel", 60);
        if (x + uw + cw + 8 > r.x + r.w - 8) { x = r.x + 8; y += bh + 4; }
        (void)tiku_dialog_add(&c->dialog, PV_UNLOCK,
                              (tiku_rect_t){x, y, uw, bh},
                              TIKU_DIALOG_BUTTON, !c->loading);
        (void)tiku_dialog_add(&c->dialog, PV_CANCEL_UNLOCK,
                              (tiku_rect_t){x + uw + 8, y, cw, bh},
                              TIKU_DIALOG_BUTTON, !c->loading);
        y += bh + 6;
    }
    return y;
}

/** @brief Where everything goes in content @p r; the pages are laid out
 *         again when the canvas or the scale has changed. */
static pv_layout_t pv_layout(tiku_pdfview_t *c, tiku_rect_t r)
{
    pv_layout_t l;
    int fh = tiku_font_plain()->height + 6, below;

    memset(&l, 0, sizeof l);
    tiku_dialog_begin(&c->dialog, -1, -1);
    below = pv_tools_layout(c, r, &l);
    l.tools = (tiku_rect_t){r.x, r.y, r.w, below - r.y};
    l.status = (tiku_rect_t){r.x + 8, r.y + r.h - fh - 3, r.w - 16, fh};
    tiku_rect_t body = {r.x + 4, below, r.w - 8, l.status.y - 3 - below};
    if (body.h < 1) body.h = 1;
    if (c->side && (c->sigs ? c->nsig : c->thumbs ? c->npage
                                                  : c->noutline) > 0 &&
        body.w >= 2 * PV_SIDE_W) {
        l.side = (tiku_rect_t){body.x, body.y, PV_SIDE_W, body.h};
        body.x += PV_SIDE_W + 4;
        body.w -= PV_SIDE_W + 4;
    }
    l.vbar = (tiku_rect_t){body.x + body.w - PV_BAR, body.y, PV_BAR, body.h};
    l.canvas = (tiku_rect_t){body.x, body.y, body.w - PV_BAR, body.h};
    if (c->npage) {
        pv_relayout(c, l.canvas);
        if (c->width > l.canvas.w) {
            l.canvas.h -= PV_BAR;
            l.vbar.h -= PV_BAR;
            l.hbar = (tiku_rect_t){l.canvas.x, l.canvas.y + l.canvas.h,
                                   l.canvas.w, PV_BAR};
            pv_relayout(c, l.canvas);
        }
        pv_recent_apply(c);
    }
    if (l.side.w)
        (void)tiku_dialog_add(&c->dialog, PV_SIDE, l.side, TIKU_DIALOG_INPUT,
                              1);
    (void)tiku_dialog_add(&c->dialog, PV_CANVAS, l.canvas, TIKU_DIALOG_INPUT,
                          1);
    tiku_dialog_end(&c->dialog);
    if (c->dialog.focus == TIKU_DIALOG_NONE)
        (void)tiku_dialog_focus(&c->dialog, PV_CANVAS);
    if (c->dialog.focus != PV_PAGE_FIELD && c->npage) {
        char now[16];
        snprintf(now, sizeof now, "%d", pv_current(c) + 1);
        if (strcmp(now, c->go.text)) tiku_field_start(&c->go, now);
    }
    return l;
}

/* ------------------------------------------------------------- canvas  */

/** @brief Fill @p w by @p h pixels at @p x, @p y of the canvas with
 *         @p argb, its alpha byte saying how much shows. */
static void pv_tint(tiku_pdfview_t *c, int x, int y, int w, int h,
                   uint32_t argb)
{
    unsigned a = argb >> 24, r = (argb >> 16) & 255, g = (argb >> 8) & 255;
    unsigned b = argb & 255;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > c->view_w) w = c->view_w - x;
    if (y + h > c->view_h) h = c->view_h - y;
    for (int j = 0; j < h; j++) {
        uint32_t *d = c->view + (size_t)(y + j) * c->view_w + x;
        for (int i = 0; i < w; i++, d++) {
            unsigned dr = (*d >> 16) & 255, dg = (*d >> 8) & 255, db = *d & 255;
            dr = (r * a + dr * (255 - a) + 127) / 255;
            dg = (g * a + dg * (255 - a) + 127) / 255;
            db = (b * a + db * (255 - a) + 127) / 255;
            *d = 0xff000000u | dr << 16 | dg << 8 | db;
        }
    }
}

/** @brief Box @p in of page @p p, in its points as drawn, as the view
 *         shows it: turned, its corners in order, into @p out. */
static void pv_turn_box(tiku_pdfview_t *c, int p, const float *in,
                        double *out)
{
    double x0 = in[0], y0 = in[1], x1 = in[2], y1 = in[3];

    tiku_pdf_turn_point(c->pw[p], c->ph[p], c->turn, 0, &x0, &y0);
    tiku_pdf_turn_point(c->pw[p], c->ph[p], c->turn, 0, &x1, &y1);
    out[0] = fmin(x0, x1);
    out[1] = fmin(y0, y1);
    out[2] = fmax(x0, x1);
    out[3] = fmax(y0, y1);
}

/** @brief A box in page @p p's points, tinted where the page is shown
 *         at @p px, @p py on the canvas, turned as the view is. */
static void pv_tint_box(tiku_pdfview_t *c, int p, int px, int py,
                        const float *box, uint32_t argb)
{
    double r[4];
    int x0, y0;

    pv_turn_box(c, p, box, r);
    x0 = px + (int)floor(r[0] * c->scale);
    y0 = py + (int)floor(r[1] * c->scale);
    pv_tint(c, x0, y0, px + (int)ceil(r[2] * c->scale) - x0,
            py + (int)ceil(r[3] * c->scale) - y0, argb);
}

/** @brief Page @p p's drawn tile at the scale shown; without @p exact,
 *         the one nearest that scale when none is at it.  NULL for none. */
static pv_tile_t *pv_tile(tiku_pdfview_t *c, int p, int exact)
{
    pv_tile_t *best = NULL;

    for (int i = 0; i < PV_TILES; i++) {
        pv_tile_t *t = &c->tile[i];
        if (t->page != p || t->pending || !t->px) continue;
        if (fabs(t->scale - c->scale) < 1e-9) return t;
        if (!exact && (!best || fabs(t->scale - c->scale) <
                                fabs(best->scale - c->scale)))
            best = t;
    }
    return best;
}

/** @brief Whether page @p p is being drawn at the scale shown. */
static int pv_pending(tiku_pdfview_t *c, int p)
{
    for (int i = 0; i < PV_TILES; i++)
        if (c->tile[i].page == p && c->tile[i].pending &&
            fabs(c->tile[i].scale - c->scale) < 1e-9) return 1;
    return 0;
}

/** @brief Tile @p t over the page's @p w by @p h pixels at @p x, @p y of
 *         the canvas: copied, or stretched while a sharper one is drawn. */
static void pv_blit(tiku_pdfview_t *c, const pv_tile_t *t, int x, int y,
                    int w, int h)
{
    int y0 = y < 0 ? 0 : y, y1 = y + h < c->view_h ? y + h : c->view_h;
    int x0 = x < 0 ? 0 : x, x1 = x + w < c->view_w ? x + w : c->view_w;

    for (int j = y0; j < y1; j++) {
        uint32_t *d = c->view + (size_t)j * c->view_w;
        if (t->w == w && t->h == h) {
            memcpy(d + x0, t->px + (size_t)(j - y) * t->w + (x0 - x),
                   (size_t)(x1 - x0) * sizeof *d);
            continue;
        }
        const uint32_t *row = t->px + (size_t)((long)(j - y) * t->h / h) *
                                      t->w;
        for (int i = x0; i < x1; i++)
            d[i] = row[(long)(i - x) * t->w / w];
    }
}

/** @brief Page @p p onto the canvas: its pixels or what stands for them
 *         while they are drawn, then what is marked on it. */
static void pv_compose_page(tiku_pdfview_t *c, tiku_rect_t canvas, int p)
{
    int w = pv_page_w(c, p), h = pv_page_h(c, p);
    int x = pv_page_x(c, canvas, p) - canvas.x;
    int y = (int)(c->top[p] - c->vbar.value);
    pv_tile_t *t = pv_tile(c, p, 0);

    pv_tint(c, x - 1, y - 1, w + 2, h + 2, 0xff202225u);
    pv_tint(c, x + 2, y + h + 1, w, 2, 0x60000000u);
    if (t) {
        t->used = c->frame;
        pv_blit(c, t, x, y, w, h);
    } else {
        pv_tint(c, x, y, w, h, 0xffffffffu);
    }
    if (!pv_tile(c, p, 1) && !pv_pending(c, p)) (void)pv_request(c, p);
    c->pages[p].used = c->frame;
    pv_marks(c, p, x, y);
}

/** @brief The canvas: the pages across it, drawn or waiting to be. */
static void pv_compose(tiku_pdfview_t *c, tiku_rect_t canvas)
{
    int first, last;

    if (c->view_w != canvas.w || c->view_h != canvas.h) {
        free(c->view);
        c->view = malloc((size_t)canvas.w * canvas.h * sizeof *c->view);
        c->view_w = c->view ? canvas.w : 0;
        c->view_h = c->view ? canvas.h : 0;
    }
    if (!c->view) return;
    c->frame++;
    for (long i = 0; i < (long)c->view_w * c->view_h; i++)
        c->view[i] = 0xff4b4e52u;
    if (!c->npage) return;
    first = pv_page_at(c, c->vbar.value);
    last = pv_page_at(c, c->vbar.value + canvas.h - 1);
    for (int p = first; p <= last; p++) pv_compose_page(c, canvas, p);
    /* The pages either side next, so paging on finds them drawn. */
    if (last + 1 < c->npage && !pv_tile(c, last + 1, 1) &&
        !pv_pending(c, last + 1)) (void)pv_request(c, last + 1);
    if (first > 0 && !pv_tile(c, first - 1, 1) && !pv_pending(c, first - 1))
        (void)pv_request(c, first - 1);
}

/* --------------------------------------------------------- thumbnails  */

/** @brief One thumbnail's row: its box, its number, a gap. */
static int pv_thumb_row(void)
{
    return PV_THUMB_H + tiku_measure_row_h() + 10;
}

/** @brief Thumbnail @p p in its row of column @p r, asked for when not
 *         drawn; @p here marks the page in view. */
static void pv_thumb_draw(tiku_pdfview_t *c, tiku_surface_t *s,
                          tiku_rect_t r, int p, int here)
{
    pv_tile_t *t = pv_thumb(c, p);
    int drawn = t && t->px;
    double sc = pv_thumb_scale(c, p);
    int w = drawn ? t->w : (int)(pv_pw(c, p) * sc + 0.5);
    int h = drawn ? t->h : (int)(pv_ph(c, p) * sc + 0.5);
    int top = r.y + p * pv_thumb_row() - c->thumb_top + 6;
    int x = r.x + (r.w - w) / 2, y = top + (PV_THUMB_H - h) / 2;
    char num[16];

    if (here)
        tiku_fill(s, (tiku_rect_t){x - 4, y - 4, w + 8, h + 8},
                  TIKU_C_SELECT);
    if (drawn) {
        t->used = c->frame;
        tiku_picture(s, x, y, w, h, t->px);
    } else {
        tiku_fill(s, (tiku_rect_t){x, y, w, h}, 0xffffffu);
    }
    tiku_frame(s, (tiku_rect_t){x - 1, y - 1, w + 2, h + 2},
               tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_4));
    snprintf(num, sizeof num, "%d", p + 1);
    w = tiku_measure_width(TIKU_MEASURE_PLAIN, num) + 2;
    tiku_text_line_draw(s, (tiku_rect_t){r.x + (r.w - w) / 2,
                        top + PV_THUMB_H + 4, w, tiku_measure_row_h()},
                        TIKU_MEASURE_PLAIN, num, TIKU_C_TEXT);
    if (!t) (void)pv_thumb_request(c, p);
}

/**
 * @brief The side column as the pages drawn small, the one the view is on
 *        marked.  It follows the view to a new page and is otherwise left
 *        where it was scrolled.
 */
static void pv_thumbs_draw(tiku_pdfview_t *c, tiku_surface_t *s,
                           tiku_rect_t r)
{
    tiku_rect_t clip = s->clip;
    int rh = pv_thumb_row(), cur = pv_current(c);
    long most = (long)c->npage * rh - r.h;

    tiku_ui_sunken(s, r, TIKU_C_DOC);
    if (cur != c->thumb_seen) {
        if ((long)cur * rh < c->thumb_top) c->thumb_top = cur * rh;
        if ((long)(cur + 1) * rh > c->thumb_top + r.h)
            c->thumb_top = (cur + 1) * rh - r.h;
        c->thumb_seen = cur;
    }
    if (c->thumb_top > most) c->thumb_top = (int)most;
    if (c->thumb_top < 0) c->thumb_top = 0;
    tiku_clip_set(s, r);
    for (int p = c->thumb_top / rh; p < c->npage &&
                                    (long)p * rh < c->thumb_top + r.h; p++)
        pv_thumb_draw(c, s, r, p, p == cur);
    s->clip = clip;
}

/** @brief The wheel scrolls the thumbnails; a click goes to its page. */
static int pv_thumbs_pointer(tiku_pdfview_t *c, const tiku_event_t *ev,
                             const pv_layout_t *l)
{
    int rh = pv_thumb_row(), p;

    if (ev->type == TIKU_EVENT_WHEEL) {
        c->thumb_top += ev->dy * rh / 2;
        pv_changed(c);
        return 1;
    }
    if (ev->type != TIKU_EVENT_POINTER_DOWN || ev->button != 1) return 1;
    p = (ev->y - l->side.y + c->thumb_top) / rh;
    if (p >= 0 && p < c->npage) pv_go(c, p, 0, 1);
    pv_changed(c);
    return 1;
}

/** @brief Keys for the thumbnails: up and down go a page, Home and End
 *         to the ends. */
static int pv_thumbs_key(tiku_pdfview_t *c, const tiku_event_t *ev)
{
    int p = pv_current(c);

    if (ev->key == TIKU_KEY_UP && p > 0) pv_go(c, p - 1, 0, 0);
    else if (ev->key == TIKU_KEY_DOWN && p + 1 < c->npage)
        pv_go(c, p + 1, 0, 0);
    else if (ev->key == TIKU_KEY_HOME) pv_go(c, 0, 0, 0);
    else if (ev->key == TIKU_KEY_END) pv_go(c, c->npage - 1, 0, 0);
    else return 0;
    pv_changed(c);
    return 1;
}

/* -------------------------------------------------------------- paint  */

/**
 * @brief A toolbar button as the desk's toolbars draw one: its icon on the
 *        panel, raised under the pointer, sunk while pressed or while its
 *        mode is on, dimmed when it can do nothing; Unlock and Cancel are
 *        worded buttons.
 */
static void pv_button(tiku_pdfview_t *c, tiku_surface_t *s,
                      const tiku_dialog_control_t *d)
{
    unsigned flags = d->enabled ? 0 : TIKU_S_DISABLED;
    int held = tiku_dialog_pressed(&c->dialog, d->id), size = d->rect.h - 6;
    int on = d->enabled &&          /* a mode shows only where it works */
             ((d->id == PV_OUTLINE && c->side && !c->thumbs && !c->sigs &&
               c->noutline) || (d->id == PV_THUMBS && c->side && c->thumbs) ||
              (d->id == PV_SIGS && c->side && c->sigs) ||
              (d->id == PV_FIT_WIDTH && c->fit == PV_BY_WIDTH) ||
              (d->id == PV_FIT_PAGE && c->fit == PV_BY_PAGE));

    if (tiku_dialog_focused(&c->dialog, d->id)) flags |= TIKU_S_FOCUS;
    if (held || on) flags |= TIKU_S_PRESSED;
    if (c->hover_tool == d->id) flags |= TIKU_S_HOT;
    if (d->id == PV_UNLOCK || d->id == PV_CANCEL_UNLOCK) {
        tiku_ui_button(s, d->rect, pv_label(d->id), flags);
        return;
    }
    /* The label is for narration and is not drawn; the icon is the face. */
    tiku_ui_button(s, d->rect, pv_label(d->id), flags | TIKU_S_ICON);
    if (size > 24) size = 24;
    (void)tiku_icon_paint_dim(s, pv_tool_icon(d->id),
                              d->rect.x + (d->rect.w - size) / 2 + held,
                              d->rect.y + (d->rect.h - size) / 2 + held, size,
                              d->enabled ? 0.0f : 0.72f, TIKU_C_PANEL);
}

/** @brief The tip of the button under the pointer, below it and kept
 *         inside @p r. */
static void pv_tool_tip_draw(tiku_pdfview_t *c, tiku_surface_t *s,
                             tiku_rect_t r)
{
    for (int i = 0; i < c->dialog.count && c->hover_tool; i++) {
        const tiku_dialog_control_t *d = &c->dialog.control[i];
        const char *text = pv_tool_tip(d->id);
        tiku_rect_t tip;

        if (d->id != c->hover_tool || d->kind != TIKU_DIALOG_BUTTON ||
            !*text) continue;
        tip = tiku_ui_tip_size(text);
        tip.x = d->rect.x;
        tip.y = d->rect.y + d->rect.h + 4;
        if (tip.x + tip.w > r.x + r.w - 4) tip.x = r.x + r.w - 4 - tip.w;
        tiku_ui_tip(s, tip, text);
    }
}

/** @brief The toolbar button under the pointer, lit and tipped; none once
 *         something is pressed or typed, or the window loses the focus. */
static void pv_tool_hover(tiku_pdfview_t *c, const tiku_event_t *ev)
{
    int tool = 0;

    if (ev->type == TIKU_EVENT_POINTER_MOVE &&
        c->dialog.capture == TIKU_DIALOG_NONE) {
        for (int i = 0; i < c->dialog.count; i++) {
            const tiku_dialog_control_t *d = &c->dialog.control[i];
            if (d->kind == TIKU_DIALOG_BUTTON && pv_in(d->rect, ev->x, ev->y))
                tool = d->id;
        }
    }
    if (tool != c->hover_tool) {
        c->hover_tool = tool;
        pv_changed(c);
    }
}

/** @brief A text field with a hint in it while it is empty; the password
 *         a locked document asks for is drawn as a star a byte. */
static void pv_field(tiku_pdfview_t *c, tiku_surface_t *s, tiku_rect_t r,
                     int id, const char *hint)
{
    int pw = id == PV_PASS_FIELD;
    tiku_field_t *f = pw ? &c->pass : id == PV_FIND_FIELD ? &c->find : &c->go;
    tiku_field_input_t *in = pw ? &c->pass_in : id == PV_FIND_FIELD
                                             ? &c->find_in : &c->go_in;
    int focused = c->dialog.focus == id;
    char shown[TIKU_FIELD_MAX];

    snprintf(shown, sizeof shown, "%s", f->text);
    if (pw) memset(shown, '*', strlen(shown));
    tiku_ui_textfield_scroll(s, r, shown, focused ? f->caret : -1,
                             focused ? f->sel_a : 0, focused ? f->sel_b : 0,
                             focused ? TIKU_S_FOCUS : 0, in->scroll);
    if (!f->text[0] && !focused)
        tiku_text_line_draw(s, (tiku_rect_t){r.x + 6, r.y, r.w - 12, r.h},
                            TIKU_MEASURE_PLAIN, hint,
                            tiku_ink(TIKU_INK_FAINT));
}

/** @brief The scrollbars, as the view stands. */
static void pv_bars(tiku_pdfview_t *c, tiku_surface_t *s,
                    const pv_layout_t *l)
{
    float pos = 0, frac = 1;

    if (tiku_scroll_max(&c->vbar) > 0 && c->height > 0) {
        pos = (float)c->vbar.value / (float)tiku_scroll_max(&c->vbar);
        frac = (float)l->canvas.h / (float)c->height;
    }
    tiku_ui_scrollbar_held(s, l->vbar, pos, frac, 0, c->vbar.arrow_dir);
    if (!l->hbar.w) return;
    pos = tiku_scroll_max(&c->hbar) > 0 ?
          (float)c->hbar.value / (float)tiku_scroll_max(&c->hbar) : 0;
    frac = c->width > 0 ? (float)l->canvas.w / (float)c->width : 1;
    tiku_ui_scrollbar_held(s, l->hbar, pos, frac, 1, c->hbar.arrow_dir);
}

/** @brief The status line: what is hovered or said, and where the view
 *         is, at the right. */
static void pv_status(tiku_pdfview_t *c, tiku_surface_t *s, tiku_rect_t r)
{
    char where[96] = "";
    int w = 0;

    if (c->npage) {
        char turned[16] = "";
        if (c->turn) snprintf(turned, sizeof turned, "   %d\xc2\xb0", c->turn);
        snprintf(where, sizeof where, "Page %d of %d   %d%%%s",
                 pv_current(c) + 1, c->npage, (int)(c->scale * 100 + 0.5),
                 turned);
        w = tiku_measure_width(TIKU_MEASURE_PLAIN, where) + 8;
        tiku_text_line_draw(s, (tiku_rect_t){r.x + r.w - w, r.y, w, r.h},
                            TIKU_MEASURE_PLAIN, where, TIKU_C_TEXT);
    }
    tiku_text_line_draw(s, (tiku_rect_t){r.x, r.y, r.w - w - 8, r.h},
                        TIKU_MEASURE_PLAIN, c->hint[0] ? c->hint : c->status,
                        tiku_ink(TIKU_INK_FAINT));
}

/** @brief The whole window, laid out afresh each time. */
static void pv_draw(tiku_window_t *w, tiku_surface_t *s, tiku_rect_t r,
                    void *ctx)
{
    tiku_pdfview_t *c = ctx;
    tiku_rect_t clip = s->clip;
    pv_layout_t l;
    char of[32];

    (void)w;
    tiku_clip_set(s, r);
    tiku_ui_panel(s, r);
    l = pv_layout(c, r);
    for (int i = 0; i < c->dialog.count; i++) {
        const tiku_dialog_control_t *d = &c->dialog.control[i];
        if (d->kind == TIKU_DIALOG_BUTTON) pv_button(c, s, d);
    }
    pv_field(c, s, l.page, PV_PAGE_FIELD, "");
    snprintf(of, sizeof of, "of %d", c->npage);
    tiku_text_line_draw(s, l.of, TIKU_MEASURE_PLAIN, c->npage ? of : "",
                        TIKU_C_TEXT);
    pv_field(c, s, l.find, PV_FIND_FIELD, "Find");
    if (c->locked) {
        char label[320];
        const char *name = strrchr(c->locked_path, '/');
        snprintf(label, sizeof label, "%s: %.240s",
                 c->sealed ? "Key password" : "Password",
                 name ? name + 1 : c->locked_path);
        tiku_text_line_draw(s, l.password_label, TIKU_MEASURE_BOLD, label,
                            TIKU_C_TEXT);
        pv_field(c, s, l.password, PV_PASS_FIELD, "Password");
    }
    if (l.side.w) pv_side_draw(c, s, l.side);
    if (c->npage) {
        pv_compose(c, l.canvas);
        if (c->view)
            tiku_picture(s, l.canvas.x, l.canvas.y, c->view_w, c->view_h,
                         c->view);
    } else {
        tiku_fill(s, l.canvas, TIKU_C_DOC);     /* no document: blank */
    }
    tiku_frame(s, l.canvas, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_4));
    pv_bars(c, s, &l);
    pv_status(c, s, l.status);
    pv_tool_tip_draw(c, s, r);      /* last: nothing paints over it */
    s->clip = clip;
}

/** @brief Presses and drags on the scrollbars and their arrows. */
static int pv_bars_pointer(tiku_pdfview_t *c, const tiku_event_t *ev,
                           const pv_layout_t *l)
{
    int down = ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1;

    for (int horiz = 0; horiz < 2; horiz++) {
        tiku_rect_t r = horiz ? l->hbar : l->vbar;
        tiku_scroll_t *sb = horiz ? &c->hbar : &c->vbar;
        int *drag = horiz ? &c->hdrag : &c->vdrag;
        int len = horiz ? r.w : r.h, arrow = horiz ? r.h : r.w;
        int at = horiz ? ev->x - r.x : ev->y - r.y;

        if (down && r.w && pv_in(r, ev->x, ev->y)) {
            if (at < arrow) tiku_scroll_to(sb, sb->value - PV_LINE);
            else if (at >= len - arrow) tiku_scroll_to(sb, sb->value + PV_LINE);
            else if (len - 2 * arrow > 0) {
                (void)tiku_scroll_press(sb, len - 2 * arrow, at - arrow);
                *drag = 1;
            }
            pv_changed(c);
            return 1;
        }
        if (*drag && ev->type == TIKU_EVENT_POINTER_MOVE) {
            (void)tiku_scroll_drag(sb, len - 2 * arrow, at - arrow);
            pv_changed(c);
            return 1;
        }
        if (*drag && ev->type == TIKU_EVENT_POINTER_UP) {
            tiku_scroll_release(sb);
            *drag = 0;
            return 1;
        }
    }
    return 0;
}
