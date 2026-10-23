/* Native image viewer and bounded binary decoder regression tests.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include "../../apps/image/tiku_image_window.c"
#include "../fixtures/image_fixtures.inc"

/** @brief Pump completion callbacks, with a strict deadline for stuck jobs. */
static void settle(tiku_image_window_t *c)
{
    for (int i = 0; i < 5000 && (c ? c->busy : tiku_work_busy()); i++) {
        (void)tiku_work_collect(); usleep(1000);
    }
    assert(c ? !c->busy : !tiku_work_busy());
}

/** @brief Store little-endian fields without unaligned host accesses. */
static void put32(unsigned char *out, unsigned n)
{
    for (int i = 0; i < 4; i++) out[i] = (unsigned char)(n >> (8 * i));
}

/** @brief Deterministic sensor-like colour ramp, with a real binary header. */
static unsigned char *bitmap(int w, int h, size_t *len)
{
    size_t stride = ((size_t)w * 3 + 3) & ~(size_t)3;
    *len = 54 + stride * h;
    unsigned char *b = calloc(1, *len);
    assert(b);
    b[0] = 'B'; b[1] = 'M'; put32(b + 2, (unsigned)*len);
    put32(b + 10, 54); put32(b + 14, 40);
    put32(b + 18, w); put32(b + 22, h); b[26] = 1; b[28] = 24;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            unsigned char *p = b + 54 + y * stride + x * 3;
            p[0] = (unsigned char)(x * 255 / w);
            p[1] = (unsigned char)(y * 255 / h);
            p[2] = (unsigned char)(((x / 24 + y / 24) & 1) ? 180 : 50);
        }
    return b;
}

/** @brief Test fixtures are confined to a unique temporary directory. */
static void write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb"); assert(f);
    assert(fwrite(data, 1, len, f) == len); assert(!fclose(f));
}

/** @brief Decoders preserve failure state and reject incomplete snapshots. */
static void decoder_tests(void)
{
    tiku_image_t im = {0};
    assert(tiku_image_decode(jpeg_420, sizeof jpeg_420, &im) == TIKU_IMG_OK);
    assert(im.w == 16 && im.h == 16); tiku_image_free(&im);
    assert(tiku_image_decode(png_palette, sizeof png_palette, &im) ==
           TIKU_IMG_OK);
    assert(im.w == 8 && im.h == 4);
    uint32_t *kept = im.px;
    assert(tiku_image_decode("garbage", 7, &im) == TIKU_IMG_NOT_IMAGE);
    assert(im.px == kept && im.w == 8);
    assert(tiku_image_decode(png_palette, TIKU_IMAGE_BYTES_MAX + 1u, &im)
           == TIKU_IMG_BAD);
    for (size_t n = 1; n < sizeof jpeg_420; n++)
        assert(tiku_image_decode(jpeg_420, n, &im) != TIKU_IMG_OK);
    for (size_t n = 1; n < sizeof png_palette; n++)
        assert(tiku_image_decode(png_palette, n, &im) != TIKU_IMG_OK);
    assert(im.px == kept); tiku_image_free(&im);
    unsigned char damaged[sizeof png_palette];
    memcpy(damaged, png_palette, sizeof damaged);
    damaged[sizeof damaged - 1] ^= 1;
    assert(tiku_image_decode(damaged, sizeof damaged, &im) == TIKU_IMG_BAD);
    const unsigned char short_sof[] = {255,216,255,192,0,2};
    const unsigned char short_dqt[] = {255,216,255,219,0,3,0};
    assert(tiku_image_decode(short_sof, sizeof short_sof, &im) != TIKU_IMG_OK);
    assert(tiku_image_decode(short_dqt, sizeof short_dqt, &im) != TIKU_IMG_OK);
    size_t len;
    unsigned char *b = bitmap(2, 3, &len);
    put32(b + 22, 0x80000000u);
    assert(tiku_image_decode(b, len, &im) == TIKU_IMG_BAD);
    put32(b + 22, 5000); put32(b + 18, 5000);
    assert(tiku_image_decode(b, len, &im) == TIKU_IMG_BAD);
    free(b);
}

/** @brief Open buttons activate on release; menu and keyboard share actions. */
static void chosen(void *ctx)
{
    (*(int *)ctx)++;
}

/** @brief Fit, rotate, high zoom and extreme pan keep bounded cached pixels. */
static void transform_tests(tiku_image_window_t *c)
{
    image_layout(c, tiku_window_content(c->window));
    assert(image_zoom(c) > 0);
    uint32_t first = c->image.px[0];
    image_action(c, IV_ROTATE);
    assert(image_width(c) == c->image.h);
    assert(image_pixel(c, c->image.h - 1, 0) == first);
    for (int i = 0; i < 3; i++) image_action(c, IV_ROTATE);
    assert(c->rotation == 0 && c->image.px[0] == first);
    image_scale(c, 10000); assert(c->zoom == 16);
    c->pan_x = 1e9; c->pan_y = -1e9;
    assert(image_render(c));
    assert(c->cache.w == c->viewport.w && c->cache.h == c->viewport.h);
    assert(fabs(c->pan_x) <= c->image.w * 8);
    image_action(c, IV_ACTUAL); assert(c->zoom == 1 && !c->fit);
    image_action(c, IV_FIT); assert(c->fit);
    tiku_rect_t original = c->window->frame;
    assert(tiku_image_window_zoom(c, (tiku_rect_t){0, 22, 1000, 650}));
    assert(c->window->frame.x == 2 && c->window->frame.y == 24);
    assert(tiku_image_window_zoom(c, (tiku_rect_t){0, 22, 1000, 650}));
    assert(!memcmp(&original, &c->window->frame, sizeof original));
    c->window->frame.w = 280;
    image_layout(c, tiku_window_content(c->window));
    for (int i = 0; i < c->dialog.count; i++) {
        tiku_rect_t r = c->dialog.control[i].rect;
        assert(r.x + r.w < c->window->frame.x + c->window->frame.w);
        for (int k = 0; k < i; k++) {
            tiku_rect_t other = c->dialog.control[k].rect;
            assert(r.y >= other.y + other.h || r.x >= other.x + other.w);
        }
    }
    c->window->frame = original;
    image_changed(c);
}

/** @brief Refreshed files keep framing; failures pause instead of discarding. */
static void watch_tests(tiku_image_window_t *c, const char *file,
                         unsigned char *bmp, size_t len)
{
    image_action(c, IV_ACTUAL); image_action(c, IV_WATCH);
    tiku_image_window_tick(c, 1); assert(c->busy);
    uint32_t *before = c->image.px;
    tiku_image_window_tick(c, 2); settle(c);
    assert(c->image.px == before && c->watching);
    bmp[len - 1] ^= 127;
    write_file(file, bmp, len);
    tiku_image_window_tick(c, 3000000); settle(c);
    assert(c->image.px != before && !c->fit && c->zoom == 1);
    before = c->image.px;
    write_file(file, "incomplete", 10);
    tiku_image_window_tick(c, 6000000); settle(c);
    assert(c->image.px == before && !c->watching);
    assert(strstr(c->status, "Previous image kept"));
    write_file(file, bmp, len);
}

/** @brief Run worker, lifetime and native input regressions without hardware. */
int main(int argc, char **argv)
{
    (void)argv;
    decoder_tests();
    char dir[] = "/tmp/tikuview-test.XXXXXX";
    assert(mkdtemp(dir));
    char file[256], shot[256];
    snprintf(file, sizeof file, "%s/snapshot.bmp", dir);
    snprintf(shot, sizeof shot, "%s/viewer.png", dir);
    size_t len;
    unsigned char *bmp = bitmap(640, 480, &len);
    write_file(file, bmp, len);
    int dirty = 0, picks = 0;
    tiku_surface_t *s = tiku_surface_new(1000, 680, TIKU_C_BACKDROP);
    tiku_workspace_t *workspace = tiku_workspace_new(s);
    tiku_image_window_t *c = tiku_image_window_new(workspace,
        &dirty, chosen, &picks);
    assert(c); tiku_image_window_open(c);
    assert(c->window && !c->image.px);
    image_layout(c, tiku_window_content(c->window));
    tiku_rect_t button = c->dialog.control[0].rect;
    tiku_event_t ev = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                       .x = button.x + 5, .y = button.y + 5};
    image_event(c->window, &ev, c); assert(!picks);
    ev.type = TIKU_EVENT_POINTER_UP; image_event(c->window, &ev, c);
    assert(picks == 1);
    assert(tiku_image_window_file(c, file)); settle(c);
    assert(c->image.w == 640 && c->image.h == 480);
    transform_tests(c); watch_tests(c, file, bmp, len);
    tiku_image_window_file(c, file); settle(c);
    image_action(c, IV_FIT);
    c->dialog.focus = TIKU_DIALOG_NONE;
    tiku_workspace_draw(workspace);
    if (argc > 1) assert(tiku_surface_png(s, shot) == 0);
    assert(tiku_image_window_bytes(c, "camera snapshot", bmp, len));
    memset(bmp, 0, len); free(bmp); settle(c);
    assert(c->image.w == 640 && !c->path[0]);
    assert(!image_enabled(c, IV_RELOAD) && !image_enabled(c, IV_WATCH));
    assert(tiku_image_window_file(c, dir)); settle(c);
    assert(c->image.w == 640 && strstr(c->status, "regular file"));
    assert(tiku_image_window_file(c, file));
    tiku_image_window_close(c); settle(c);
    assert(!c->window && !c->image.px && !c->watching);
    tiku_image_window_open(c); assert(!c->image.px && !c->path[0]);
    assert(tiku_image_window_file(c, file));
    tiku_image_window_destroy(c); settle(NULL);
    tiku_workspace_free(workspace); tiku_surface_free(s);
    if (argc > 1) printf("Preview: %s\n", shot);
    else { unlink(file); rmdir(dir); }
    puts("Image viewer: decoder, transforms, controls, refresh and lifetime PASS");
    return 0;
}
