/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_view_paint.inl - the PDF view's layout and painting: the
 * pages one under another at one scale, the canvas composed from the
 * pages drawn so far, and the scrollbars beside it.  Included by
 * tiku_pdf_view.c alone.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @brief Page @p p's width in pixels at the scale and turn shown. */
static int vw_page_w(const tiku_pdf_view_t *v, int p)
{
    int w = (int)(vw_pw(v, p) * v->scale + 0.5);
    return w > 0 ? w : 1;
}

/** @brief Page @p p's height in pixels at the scale and turn shown. */
static int vw_page_h(const tiku_pdf_view_t *v, int p)
{
    int h = (int)(vw_ph(v, p) * v->scale + 0.5);
    return h > 0 ? h : 1;
}

/** @brief The page at @p y down the laid-out pages: the last one whose
 *         top is at or above it. */
static int vw_page_at(const tiku_pdf_view_t *v, long y)
{
    int lo = 0, hi = v->npage - 1;

    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (v->top[mid] <= y) lo = mid; else hi = mid - 1;
    }
    return lo;
}

/** @brief The page the view is on: the one across its middle, or the
 *         one shown alone. */
static int vw_current(const tiku_pdf_view_t *v)
{
    if (v->single) return v->single_page < v->npage ? v->single_page : 0;
    if (!v->npage || !v->laid_scale) return 0;
    return vw_page_at(v, v->vbar.value + v->laid_h / 2);
}

/** @brief Where page @p p's left edge sits on the canvas. */
static int vw_page_x(const tiku_pdf_view_t *v, tiku_rect_t canvas, int p)
{
    if (v->width <= canvas.w)
        return canvas.x + (canvas.w - vw_page_w(v, p)) / 2;
    return canvas.x + VW_MARGIN - v->hbar.value;
}

/** @brief The scale the fit asks for in @p canvas, or the one set. */
static double vw_fit_scale(const tiku_pdf_view_t *v, tiku_rect_t canvas)
{
    double s = v->scale;
    int p = v->laid_scale ? vw_current(v) :
            v->start_page >= 0 && v->start_page < v->npage ? v->start_page
                                                           : 0;

    if (v->single)
        s = fmin(canvas.w / vw_pw(v, p), canvas.h / vw_ph(v, p));
    else if (v->fit == TIKU_PDF_VIEW_FIT_WIDTH)
        s = (canvas.w - 2.0 * VW_MARGIN) / vw_pw(v, v->widest);
    else if (v->fit == TIKU_PDF_VIEW_FIT_PAGE)
        s = fmin((canvas.w - 2.0 * VW_MARGIN) / vw_pw(v, p),
                 (canvas.h - 2.0 * VW_MARGIN) / vw_ph(v, p));
    return fmax(0.05, fmin(8.0, s));
}

/** @brief Where the view stands in page terms before the pages move: the
 *         page at its top, and how far down it, as a fraction. */
static int vw_anchor(const tiku_pdf_view_t *v, double *frac)
{
    int page = vw_page_at(v, v->vbar.value);
    long span = (page + 1 < v->npage ? v->top[page + 1]
                                     : v->height + VW_GAP - VW_MARGIN) -
                v->top[page];

    *frac = span > 0 ? (double)(v->vbar.value - v->top[page]) / span : 0;
    return page;
}

/** @brief Lay the pages out at the scale @p canvas and the fit give,
 *         keeping the page the view was on where it was -- or, the first
 *         time, starting where the program asked. */
static void vw_relayout(tiku_pdf_view_t *v, tiku_rect_t canvas)
{
    double s, frac = 0;
    long y = VW_MARGIN, widest = 0;
    int page = 0, first = !v->laid_scale;

    if (!v->npage) return;
    s = vw_fit_scale(v, canvas);
    if (canvas.w == v->laid_w && canvas.h == v->laid_h &&
        fabs(s - v->laid_scale) < 1e-9) return;
    if (!first) page = vw_anchor(v, &frac);
    v->scale = s;
    for (int p = 0; p < v->npage; p++) {
        v->top[p] = y;
        y += vw_page_h(v, p) + VW_GAP;
        if (vw_page_w(v, p) > widest) widest = vw_page_w(v, p);
    }
    v->height = y - VW_GAP + VW_MARGIN;
    v->width = widest + 2 * VW_MARGIN;
    v->laid_w = canvas.w;
    v->laid_h = canvas.h;
    v->laid_scale = s;
    tiku_scroll_set_range(&v->vbar, (int)v->height, canvas.h);
    tiku_scroll_set_range(&v->hbar, (int)v->width, canvas.w);
    if (v->start_page >= 0) {
        page = v->start_page < v->npage ? v->start_page : v->npage - 1;
        tiku_scroll_to(&v->vbar, (int)(v->top[page] + v->start_y * s) -
                                 (v->start_y > 0 ? VW_GAP : VW_MARGIN));
        v->start_page = -1;
        return;
    }
    tiku_scroll_to(&v->vbar, first ? 0 : (int)(v->top[page] + frac *
                                         (vw_page_h(v, page) + VW_GAP)));
}

/** @brief Change the fit, and with TIKU_PDF_VIEW_FIXED the scale, held
 *         to 0.25 to 4; the page in view stays in view. */
static void vw_set_scale(tiku_pdf_view_t *v, int fit, double scale)
{
    v->fit = fit;
    if (fit == TIKU_PDF_VIEW_FIXED) v->scale = fmax(0.25, fmin(4.0, scale));
    v->laid_w = 0;                  /* lay out again when next drawn */
    v->selecting = 0;
    vw_changed(v);
}

/** @brief Turn the view @p by degrees more.  The pages drawn so far turn
 *         with it, so they show at once; the page in view stays. */
static void vw_turn(tiku_pdf_view_t *v, int by)
{
    v->turn = (v->turn + by) % 360;
    for (int i = 0; i < VW_TILES; i++) {
        vw_tile_t *t = &v->tile[i];
        if (t->px && vw_turn_pixels(&t->px, &t->w, &t->h, by) != 0)
            t->page = -1;
    }
    vw_find_widest(v);
    v->laid_w = 0;
    v->selecting = 0;
    vw_changed(v);
}

/** @brief Scroll by @p dx, @p dy pixels, within the pages. */
static void vw_scroll(tiku_pdf_view_t *v, int dx, int dy)
{
    if (dy) tiku_scroll_to(&v->vbar, v->vbar.value + dy);
    if (dx) tiku_scroll_to(&v->hbar, v->hbar.value + dx);
    vw_changed(v);
}

/** @brief Bring page @p p into view with @p y points of it above the top;
 *         with @p remember, where the view was can be gone back to.
 *         Before the first layout it is where the view will start. */
static void vw_go(tiku_pdf_view_t *v, int p, double y, int remember)
{
    if (p < 0 || (v->npage && p >= v->npage)) return;
    if (v->single) {                /* the new page fitted, alone */
        v->single_page = p;
        v->laid_w = 0;
        vw_changed(v);
    }
    if (!v->laid_scale) {
        v->start_page = p;
        v->start_y = y;
        return;
    }
    if (remember) {
        if (v->nback == VW_HISTORY) {
            memmove(v->back, v->back + 1, sizeof v->back[0] *
                                          (VW_HISTORY - 1));
            v->nback--;
        }
        v->back[v->nback].page = vw_page_at(v, v->vbar.value);
        v->back[v->nback].y = (v->vbar.value -
                               v->top[v->back[v->nback].page]) / v->scale;
        v->nback++;
    }
    tiku_scroll_to(&v->vbar, (int)(v->top[p] + y * v->scale) -
                             (y > 0 ? VW_GAP : VW_MARGIN));
    vw_changed(v);
}

/** @brief Go back to where the view was before the last jump.
 *  @return 0 when there is nowhere to go back to. */
static int vw_back(tiku_pdf_view_t *v)
{
    if (!v->nback || !v->laid_scale) return 0;
    v->nback--;
    tiku_scroll_to(&v->vbar, (int)(v->top[v->back[v->nback].page] +
                                   v->back[v->nback].y * v->scale));
    vw_changed(v);
    return 1;
}

/** @brief Where the canvas and the bars go in @p r; the pages are laid
 *         out again when the canvas or the scale has changed. */
static vw_layout_t vw_layout(tiku_pdf_view_t *v, tiku_rect_t r)
{
    vw_layout_t l;

    memset(&l, 0, sizeof l);
    if (v->single) {                /* no bars: the page alone */
        l.canvas = r;
        if (v->npage) vw_relayout(v, l.canvas);
        return l;
    }
    l.vbar = (tiku_rect_t){r.x + r.w - VW_BAR, r.y, VW_BAR, r.h};
    l.canvas = (tiku_rect_t){r.x, r.y, r.w - VW_BAR, r.h};
    if (l.canvas.w < 1) l.canvas.w = 1;
    if (l.canvas.h < 1) l.canvas.h = 1;
    if (!v->npage) return l;
    vw_relayout(v, l.canvas);
    if (v->width > l.canvas.w && l.canvas.h > VW_BAR) {
        l.canvas.h -= VW_BAR;
        l.vbar.h -= VW_BAR;
        l.hbar = (tiku_rect_t){l.canvas.x, l.canvas.y + l.canvas.h,
                               l.canvas.w, VW_BAR};
        vw_relayout(v, l.canvas);
    }
    return l;
}

/* ------------------------------------------------------------- canvas  */

/** @brief Fill @p w by @p h pixels at @p x, @p y of the canvas with
 *         @p argb, its alpha byte saying how much shows. */
static void vw_tint(tiku_pdf_view_t *v, int x, int y, int w, int h,
                   uint32_t argb)
{
    unsigned a = argb >> 24, r = (argb >> 16) & 255, g = (argb >> 8) & 255;
    unsigned b = argb & 255;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > v->view_w) w = v->view_w - x;
    if (y + h > v->view_h) h = v->view_h - y;
    for (int j = 0; j < h; j++) {
        uint32_t *d = v->view + (size_t)(y + j) * v->view_w + x;
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
static void vw_turn_box(const tiku_pdf_view_t *v, int p, const float *in,
                        double *out)
{
    double x0 = in[0], y0 = in[1], x1 = in[2], y1 = in[3];

    tiku_pdf_turn_point(v->pw[p], v->ph[p], v->turn, 0, &x0, &y0);
    tiku_pdf_turn_point(v->pw[p], v->ph[p], v->turn, 0, &x1, &y1);
    out[0] = fmin(x0, x1);
    out[1] = fmin(y0, y1);
    out[2] = fmax(x0, x1);
    out[3] = fmax(y0, y1);
}

/** @brief A box in page @p p's points, tinted where the page is shown
 *         at @p px, @p py on the canvas, turned as the view is. */
static void vw_tint_box(tiku_pdf_view_t *v, int p, int px, int py,
                        const float *box, uint32_t argb)
{
    double r[4];
    int x0, y0;

    vw_turn_box(v, p, box, r);
    x0 = px + (int)floor(r[0] * v->scale);
    y0 = py + (int)floor(r[1] * v->scale);
    vw_tint(v, x0, y0, px + (int)ceil(r[2] * v->scale) - x0,
            py + (int)ceil(r[3] * v->scale) - y0, argb);
}

/** @brief Page @p p as drawn at the scale shown; else, unless @p exact,
 *         as drawn nearest it.  NULL when there is none. */
static vw_tile_t *vw_tile(tiku_pdf_view_t *v, int p, int exact)
{
    vw_tile_t *best = NULL;

    for (int i = 0; i < VW_TILES; i++) {
        vw_tile_t *t = &v->tile[i];
        if (t->page != p || t->pending || !t->px) continue;
        if (fabs(t->scale - v->scale) < 1e-9) return t;
        if (!exact && (!best || fabs(t->scale - v->scale) <
                                fabs(best->scale - v->scale)))
            best = t;
    }
    return best;
}

/** @brief Whether page @p p is being drawn at the scale shown. */
static int vw_pending(const tiku_pdf_view_t *v, int p)
{
    for (int i = 0; i < VW_TILES; i++)
        if (v->tile[i].page == p && v->tile[i].pending &&
            fabs(v->tile[i].scale - v->scale) < 1e-9) return 1;
    return 0;
}

/** @brief Tile @p t over the page's @p w by @p h pixels at @p x, @p y of
 *         the canvas: copied, or stretched while a sharper one is drawn. */
static void vw_blit(tiku_pdf_view_t *v, const vw_tile_t *t, int x, int y,
                    int w, int h)
{
    int y0 = y < 0 ? 0 : y, y1 = y + h < v->view_h ? y + h : v->view_h;
    int x0 = x < 0 ? 0 : x, x1 = x + w < v->view_w ? x + w : v->view_w;

    for (int j = y0; j < y1; j++) {
        uint32_t *d = v->view + (size_t)j * v->view_w;
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

/** @brief Page @p p onto the canvas: its pixels or white while they are
 *         drawn, then what is marked on it. */
static void vw_compose_page(tiku_pdf_view_t *v, tiku_rect_t canvas, int p)
{
    int w = vw_page_w(v, p), h = vw_page_h(v, p);
    int x = vw_page_x(v, canvas, p) - canvas.x;
    int y = (int)(v->top[p] - v->vbar.value);
    vw_tile_t *t = vw_tile(v, p, 0);

    vw_tint(v, x - 1, y - 1, w + 2, h + 2, 0xff202225u);
    vw_tint(v, x + 2, y + h + 1, w, 2, 0x60000000u);
    if (t) {
        t->used = v->frame;
        vw_blit(v, t, x, y, w, h);
    } else {
        vw_tint(v, x, y, w, h, 0xffffffffu);
    }
    if (!vw_tile(v, p, 1) && !vw_pending(v, p)) (void)vw_request(v, p);
    v->pages[p].used = v->frame;
    vw_marks(v, p, x, y);
}

/** @brief The canvas as one page alone: centred on black, the pages
 *         either side asked for next. */
static void vw_compose_single(tiku_pdf_view_t *v, tiku_rect_t canvas)
{
    int p = vw_current(v), w = vw_page_w(v, p), h = vw_page_h(v, p);
    int x = (canvas.w - w) / 2, y = (canvas.h - h) / 2;
    vw_tile_t *t = vw_tile(v, p, 0);

    for (long i = 0; i < (long)v->view_w * v->view_h; i++)
        v->view[i] = 0xff000000u;
    if (t) {
        t->used = v->frame;
        vw_blit(v, t, x, y, w, h);
    } else {
        vw_tint(v, x, y, w, h, 0xffffffffu);
    }
    if (!vw_tile(v, p, 1) && !vw_pending(v, p)) (void)vw_request(v, p);
    v->pages[p].used = v->frame;
    for (int n = p - 1; n <= p + 1; n += 2)
        if (n >= 0 && n < v->npage && !vw_tile(v, n, 1) && !vw_pending(v, n))
            (void)vw_request(v, n);
}

/** @brief The canvas: the pages across it, drawn or waiting to be, and
 *         the pages either side asked for next. */
static void vw_compose(tiku_pdf_view_t *v, tiku_rect_t canvas)
{
    int first, last;

    if (v->view_w != canvas.w || v->view_h != canvas.h) {
        free(v->view);
        v->view = malloc((size_t)canvas.w * canvas.h * sizeof *v->view);
        v->view_w = v->view ? canvas.w : 0;
        v->view_h = v->view ? canvas.h : 0;
    }
    if (!v->view) return;
    v->frame++;
    if (v->single) {
        vw_compose_single(v, canvas);
        return;
    }
    for (long i = 0; i < (long)v->view_w * v->view_h; i++)
        v->view[i] = VW_GROUND;
    first = vw_page_at(v, v->vbar.value);
    last = vw_page_at(v, v->vbar.value + canvas.h - 1);
    for (int p = first; p <= last; p++) vw_compose_page(v, canvas, p);
    if (last + 1 < v->npage && !vw_tile(v, last + 1, 1) &&
        !vw_pending(v, last + 1)) (void)vw_request(v, last + 1);
    if (first > 0 && !vw_tile(v, first - 1, 1) && !vw_pending(v, first - 1))
        (void)vw_request(v, first - 1);
}

/* --------------------------------------------------------------- bars  */

/** @brief The scrollbars, as the view stands. */
static void vw_bars(tiku_pdf_view_t *v, tiku_surface_t *s,
                    const vw_layout_t *l)
{
    float pos = 0, frac = 1;

    if (tiku_scroll_max(&v->vbar) > 0 && v->height > 0) {
        pos = (float)v->vbar.value / (float)tiku_scroll_max(&v->vbar);
        frac = (float)l->canvas.h / (float)v->height;
    }
    tiku_ui_scrollbar_held(s, l->vbar, pos, frac, 0, v->vbar.arrow_dir);
    if (!l->hbar.w) return;
    pos = tiku_scroll_max(&v->hbar) > 0 ?
          (float)v->hbar.value / (float)tiku_scroll_max(&v->hbar) : 0;
    frac = v->width > 0 ? (float)l->canvas.w / (float)v->width : 1;
    tiku_ui_scrollbar_held(s, l->hbar, pos, frac, 1, v->hbar.arrow_dir);
}

/** @brief Presses and drags on the scrollbars and their arrows. */
static int vw_bars_pointer(tiku_pdf_view_t *v, const tiku_event_t *ev,
                           const vw_layout_t *l)
{
    int down = ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1;

    for (int horiz = 0; v->npage && horiz < 2; horiz++) {
        tiku_rect_t r = horiz ? l->hbar : l->vbar;
        tiku_scroll_t *sb = horiz ? &v->hbar : &v->vbar;
        int *drag = horiz ? &v->hdrag : &v->vdrag;
        int len = horiz ? r.w : r.h, arrow = horiz ? r.h : r.w;
        int at = horiz ? ev->x - r.x : ev->y - r.y;

        if (down && r.w && vw_in(r, ev->x, ev->y)) {
            if (at < arrow) tiku_scroll_to(sb, sb->value - VW_LINE);
            else if (at >= len - arrow) tiku_scroll_to(sb, sb->value + VW_LINE);
            else if (len - 2 * arrow > 0) {
                (void)tiku_scroll_press(sb, len - 2 * arrow, at - arrow);
                *drag = 1;
            }
            vw_changed(v);
            return 1;
        }
        if (*drag && ev->type == TIKU_EVENT_POINTER_MOVE) {
            (void)tiku_scroll_drag(sb, len - 2 * arrow, at - arrow);
            vw_changed(v);
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

void tiku_pdf_view_draw(tiku_pdf_view_t *v, tiku_surface_t *s,
                        tiku_rect_t r)
{
    tiku_rect_t clip;
    vw_layout_t l;

    if (!v || !s || r.w <= 0 || r.h <= 0) return;
    clip = s->clip;
    tiku_clip_set(s, r);
    if (!v->npage) {                /* nothing open: blank */
        tiku_fill(s, r, TIKU_C_DOC);
        tiku_frame(s, r, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_4));
        s->clip = clip;
        return;
    }
    l = vw_layout(v, r);
    vw_compose(v, l.canvas);
    if (v->view)
        tiku_picture(s, l.canvas.x, l.canvas.y, v->view_w, v->view_h,
                     v->view);
    if (v->single) {
        s->clip = clip;
        return;
    }
    tiku_frame(s, l.canvas, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_4));
    vw_bars(v, s, &l);
    if (l.hbar.w)                   /* the corner the two bars leave */
        tiku_fill(s, (tiku_rect_t){l.vbar.x, l.hbar.y, VW_BAR, VW_BAR},
                  TIKU_C_PANEL);
    s->clip = clip;
}
