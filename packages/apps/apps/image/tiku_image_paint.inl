/* tiku_image_paint.inl -- part of tiku_image_window.c: the toolbar's
 * layout and labels, the view's zoom, turn and pan, and painting through a
 * cache the size of the view.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */

/** @brief Command @p id's full label: its menu item, its button's
 *         narration, and the status line while it is pointed at. */
static const char *image_label(int id)
{
    static const char *labels[] = { "", "Open Image...", "Reload Image",
        "Fit to Window", "Actual Size (1:1)", "Zoom Out", "Zoom In",
        "Rotate Clockwise", "Watch Snapshot (2 seconds)", "Close" };
    return id >= IV_OPEN && id <= IV_CLOSE ? labels[id] : "";
}

/** @brief Command @p id's short label, on its toolbar button. */
static const char *image_tool(int id)
{
    static const char *labels[] = { "", "Open", "Reload", "Fit", "1:1",
                                    "-", "+", "Rotate", "Watch 2s" };
    return id >= IV_OPEN && id <= IV_WATCH ? labels[id] : "";
}

/** @brief Whether command @p id can act now: Reload and Watch need a file,
 *         the view's commands an image. */
static int image_enabled(tiku_image_window_t *c, int id)
{
    if (id == IV_CLOSE) return 1;
    if (id == IV_OPEN) return c->choose && !c->busy;
    if (id == IV_RELOAD) return c->path[0] && !c->busy;
    if (id == IV_WATCH) return c->path[0] && c->image.px;
    return c->image.px != NULL;
}

/** @brief Lay out the toolbar, wrapping when narrow, and the view under
 *         it; drawing and input both use it.  A new view size drops the
 *         cache. */
static void image_layout(tiku_image_window_t *c, tiku_rect_t r)
{
    int bh = tiku_ui_button_h(), x = r.x + 6, y = r.y + 5;
    tiku_dialog_begin(&c->dialog, -1, -1);
    for (int id = IV_OPEN; id <= IV_WATCH; id++) {
        int width = tiku_ui_button_w(image_tool(id), 28);
        if (id == IV_OPEN || id == IV_RELOAD) width += 20;
        if (id == IV_WATCH) width += 12;
        if (x > r.x + 6 && x + width > r.x + r.w - 6) {
            x = r.x + 6; y += bh + 5;
        }
        (void)tiku_dialog_add(&c->dialog, id,
            (tiku_rect_t){x, y, width, bh}, TIKU_DIALOG_BUTTON,
            image_enabled(c, id));
        x += width + 4;
    }
    tiku_dialog_end(&c->dialog);
    y += bh + 6;
    int foot = 2 * (tiku_font_plain()->height + 6) + 8;
    tiku_rect_t view = {r.x + 4, y, r.w - 8, r.y + r.h - foot - y};
    if (view.w < 1) view.w = 1;
    if (view.h < 1) view.h = 1;
    if (view.w != c->viewport.w || view.h != c->viewport.h)
        c->cache_valid = 0;
    c->viewport = view;
}

/** @brief The image's width as turned. */
static int image_width(tiku_image_window_t *c)
{
    return c->rotation & 1 ? c->image.h : c->image.w;
}

/** @brief The image's height as turned. */
static int image_height(tiku_image_window_t *c)
{
    return c->rotation & 1 ? c->image.w : c->image.h;
}

/** @brief The zoom shown: the one set, or with Fit the largest that shows
 *         the whole image, at most 16. */
static double image_zoom(tiku_image_window_t *c)
{
    if (!c->image.px) return 1;
    if (!c->fit) return c->zoom;
    return fmin(16.0, fmin((double)c->viewport.w / image_width(c),
                           (double)c->viewport.h / image_height(c)));
}

/** @brief The source pixel at (@p x, @p y) of the turned image. */
static uint32_t image_pixel(tiku_image_window_t *c, int x, int y)
{
    int sx = x, sy = y;
    if (c->rotation == 1) { sx = y; sy = c->image.h - 1 - x; }
    if (c->rotation == 2) {
        sx = c->image.w - 1 - x; sy = c->image.h - 1 - y;
    }
    if (c->rotation == 3) { sx = c->image.w - 1 - y; sy = x; }
    return c->image.px[(size_t)sy * c->image.w + sx];
}

/** @brief @p pixel blended by its alpha over grey @p bg, a square of the
 *         checkerboard behind transparency. */
static uint32_t image_over(uint32_t pixel, unsigned bg)
{
    unsigned a = pixel >> 24, b = 255 - a;
    unsigned r = (((pixel >> 16) & 255) * a + bg * b + 127) / 255;
    unsigned g = (((pixel >> 8) & 255) * a + bg * b + 127) / 255;
    unsigned v = ((pixel & 255) * a + bg * b + 127) / 255;
    return 0xff000000u | (r << 16) | (g << 8) | v;
}

/** @brief Limit the pan: an image larger than the view keeps covering it,
 *         and a smaller one stays centred. */
static void image_clamp(tiku_image_window_t *c, double zoom)
{
    double x = fmax(0, (image_width(c) * zoom - c->viewport.w) / 2);
    double y = fmax(0, (image_height(c) * zoom - c->viewport.h) / 2);
    c->pan_x = fmax(-x, fmin(x, c->pan_x));
    c->pan_y = fmax(-y, fmin(y, c->pan_y));
}

/** @brief Fill the cache with the view's pixels at the zoom, turn and pan;
 *         a valid cache is kept.  @return 0 when it cannot be had. */
static int image_render(tiku_image_window_t *c)
{
    if (c->cache_valid) return 1;
    int width = c->viewport.w, height = c->viewport.h;
    if ((size_t)width > TIKU_IMAGE_PIXELS_MAX / (size_t)height) return 0;
    if (c->cache.w != width || c->cache.h != height) {
        tiku_image_free(&c->cache);
        c->cache.px = malloc((size_t)width * height * sizeof(uint32_t));
        if (!c->cache.px) return 0;
        c->cache.w = width; c->cache.h = height;
    }
    double zoom = image_zoom(c);
    image_clamp(c, zoom);
    double ox = (width - image_width(c) * zoom) / 2 + c->pan_x;
    double oy = (height - image_height(c) * zoom) / 2 + c->pan_y;
    for (int y = 0; y < height; y++) {
        int sy = (int)floor((y - oy) / zoom);
        for (int x = 0; x < width; x++) {
            int sx = (int)floor((x - ox) / zoom);
            uint32_t pixel = 0xff303030u;
            if (sx >= 0 && sy >= 0 && sx < image_width(c) &&
                sy < image_height(c)) {
                unsigned bg = ((x / 10 + y / 10) & 1) ? 208 : 240;
                pixel = image_over(image_pixel(c, sx, sy), bg);
            }
            c->cache.px[(size_t)y * width + x] = pixel;
        }
    }
    c->cache_valid = 1;
    return 1;
}

/** @brief Draw toolbar button @p d: Watch as a checkbox, Open and Reload
 *         as an icon and their short label, the others as worded buttons. */
static void image_button(tiku_image_window_t *c, tiku_surface_t *s,
                          const tiku_dialog_control_t *d)
{
    unsigned flags = d->enabled ? 0 : TIKU_S_DISABLED;
    if (tiku_dialog_focused(&c->dialog, d->id)) flags |= TIKU_S_FOCUS;
    if (tiku_dialog_pressed(&c->dialog, d->id)) flags |= TIKU_S_PRESSED;
    if (d->id == IV_WATCH) {
        tiku_ui_checkbox(s, d->rect, image_tool(d->id), flags |
                         (c->watching ? TIKU_S_ON : 0));
    } else if (d->id == IV_OPEN || d->id == IV_RELOAD) {
        if (c->hover == d->id) flags |= TIKU_S_HOT;
        /* The full label is for narration and is not drawn; the icon and
         * the short label are the face. */
        tiku_ui_button(s, d->rect, image_label(d->id), flags | TIKU_S_ICON);
        tiku_icon_paint_dim(s, d->id == IV_OPEN ? "folder_open" :
            "nav_reload", d->rect.x + 4,
            d->rect.y + (d->rect.h - 18) / 2, 18,
            d->enabled ? 0 : 0.72f, TIKU_C_PANEL);
        tiku_text_line_draw(s, (tiku_rect_t){d->rect.x + 26, d->rect.y,
            d->rect.w - 28, d->rect.h}, 0, image_tool(d->id),
            tiku_ink(d->enabled ? TIKU_INK_PLAIN : TIKU_INK_FAINT));
    } else tiku_ui_button(s, d->rect, image_tool(d->id), flags);
}

/** @brief The empty view: what can be opened, and what Watch 2s does. */
static void image_empty(tiku_surface_t *s, tiku_rect_t r)
{
    int x = r.x + 24, y = r.y + r.h / 3;
    tiku_icon_paint(s, "image_viewer", x, y, 48);
    tiku_text_line_draw(s, (tiku_rect_t){x + 62, y, r.w - 100, 28},
                         1, "Open an image or camera snapshot", TIKU_C_TEXT);
    tiku_text_block_draw(s,
        (tiku_rect_t){x + 62, y + 32, r.w - 100, r.h - (y-r.y) - 38},
        0, "PNG, baseline JPEG and BMP. Use Open to choose a local image. "
        "Watch 2s follows a snapshot file updated by your capture tool; "
        "it does not start or connect a camera.", TIKU_C_TEXT, 4);
}

/** @brief Draw the window: the toolbar, the view from its cache, and the
 *         two status lines. */
static void image_draw(tiku_window_t *w, tiku_surface_t *s,
                        tiku_rect_t r, void *ctx)
{
    tiku_image_window_t *c = ctx;
    (void)w;
    tiku_rect_t clip = s->clip;
    tiku_clip_set(s, r);
    tiku_ui_panel(s, r);
    image_layout(c, r);
    for (int i = 0; i < c->dialog.count; i++)
        image_button(c, s, &c->dialog.control[i]);
    tiku_fill(s, c->viewport, TIKU_C_DOC);
    if (c->image.px && image_render(c))
        tiku_picture(s, c->viewport.x, c->viewport.y,
                     c->cache.w, c->cache.h, c->cache.px);
    else if (!c->image.px) image_empty(s, c->viewport);
    else tiku_text_line_draw(s, c->viewport, 0,
        "Not enough memory to draw this viewport.", TIKU_C_TEXT);
    tiku_frame(s, c->viewport, tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_4));
    int fh = tiku_font_plain()->height + 6;
    char info[384];
    if (c->image.px) snprintf(info, sizeof info,
        "%d x %d px  |  %.1f KiB  |  %.0f%% %s  |  %.240s",
        c->image.w, c->image.h, c->bytes / 1024.0, image_zoom(c) * 100,
        c->fit ? "(fit)" : "", c->label);
    else snprintf(info, sizeof info, TIKU_PRODUCT_IMAGE_DESCRIPTION);
    tiku_text_line_draw(s, (tiku_rect_t){r.x + 8, r.y+r.h-2*fh-4,
        r.w - 16, fh}, 0, info, TIKU_C_TEXT);
    tiku_text_line_draw(s, (tiku_rect_t){r.x + 8, r.y+r.h-fh-4,
        r.w - 16, fh}, 0, c->hover ? image_label(c->hover) : c->status,
        TIKU_C_TEXT);
    s->clip = clip;
}
