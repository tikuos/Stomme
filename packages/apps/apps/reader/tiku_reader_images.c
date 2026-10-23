/* tiku_reader_images.c -- local and embedded PNG image resources: sizes read from the
 * file's header, their pixels decoded when drawn, within a budget that lets
 * go of the least recently drawn.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_reader_private.h"

static int reader_image_allowed(const char *path, int *wide, int *high);
static int reader_image_index(tiku_reader_help_window_t *c, const char *src);

/** @brief Whether @p path is a PNG, BMP or JPEG of at most 4 MiB whose
 *         header gives a size within 2048 by 2048 and one megapixel; the
 *         size into @p wide and @p high, read before any pixels are. */
static int reader_image_allowed(const char *path, int *wide, int *high)
{
    struct stat st; unsigned char h[32]; unsigned w = 0, height = 0;
    if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size < 24 ||
        st.st_size > 4 * 1024 * 1024) return 0;
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    size_t n = fread(h, 1, sizeof h, f);
    if (n >= 24 && !memcmp(h, "\211PNG\r\n\032\n", 8)) {
        for (int i = 16; i < 20; i++) w = (w << 8) | h[i];
        for (int i = 20; i < 24; i++) height = (height << 8) | h[i];
    } else if (n >= 26 && h[0] == 'B' && h[1] == 'M') {
        for (int i = 21; i >= 18; i--) w = (w << 8) | h[i];
        for (int i = 25; i >= 22; i--) height = (height << 8) | h[i];
        if (height & 0x80000000u) height = 0u - height;
    } else if (n >= 2 && h[0] == 255 && h[1] == 216) {
        fseek(f, 2, SEEK_SET);
        for (int i = 0; i < 256; i++) {
            int mark = fgetc(f), tag = fgetc(f);
            if (mark != 255 || tag < 0 || tag == 218 || tag == 217) break;
            while (tag == 255) tag = fgetc(f);
            int a = fgetc(f), b = fgetc(f), len = a * 256 + b;
            if (a < 0 || b < 0 || len < 2) break;
            if ((tag == 192 || tag == 194) && len >= 7 &&
                fread(h, 1, 5, f) == 5) {
                height = h[1] * 256u + h[2]; w = h[3] * 256u + h[4]; break;
            }
            if (fseek(f, len - 2, SEEK_CUR)) break;
        }
    }
    fclose(f);
    *wide = (int)w; *high = (int)height;
    return w > 0 && height > 0 && w <= 2048 && height <= 2048 &&
           (unsigned long)w * height <= 1024 * 1024;
}

/** @brief The index of the image @p src names, entered the first time
 *         with its resolved path and size, or marked failed when it cannot
 *         be shown.  @return -1 when there is no room for it. */
static int reader_image_index(tiku_reader_help_window_t *c, const char *src)
{
    struct help_reader *r = c->reader; if (!r) return -1;
    for (int i = 0; i < r->nimages; i++) if (!strcmp(r->images[i].src, src)) return i;
    size_t length = strnlen(src,HELP_IMAGE_METADATA);
    if (length == HELP_IMAGE_METADATA || length+1+r->image_sources+
        (size_t)r->image_cap*sizeof *r->images > HELP_IMAGE_METADATA) return -1;
    if (r->nimages == r->image_cap) {
        int cap = r->image_cap ? r->image_cap * 2 : 16;
        if ((size_t)cap*sizeof *r->images+r->image_sources+length+1 > HELP_IMAGE_METADATA)
            cap = r->nimages+1;
        if ((size_t)cap*sizeof *r->images+r->image_sources+length+1 > HELP_IMAGE_METADATA) return -1;
        help_image_t *images = realloc(r->images,
                                       (size_t)cap * sizeof *images);
        if (!images) return -1;
        memset(images + r->image_cap, 0,
               (size_t)(cap - r->image_cap) * sizeof *images);
        r->images = images; r->image_cap = cap;
    }
    char *reference = malloc(length+1); if (!reference) return -1;
    memcpy(reference,src,length+1);
    int i = r->nimages++; r->images[i].src = reference; r->image_sources += length+1;
    help_image_t *im = &r->images[i];
    im->embedded = !strncasecmp(src,"data:",5);
    if (im->embedded)
        im->failed = !tiku_image_data_png_size(src,length,&im->width,&im->height);
    else im->failed = !tiku_reader_path(c,src,im->path,sizeof im->path) ||
        !reader_image_allowed(im->path,&im->width,&im->height);
    return i;
}

/** @brief Evict the least recently drawn pixels, keeping every image's
 *         size, so scrolling never moves the text. */
static void reader_image_room(struct help_reader *r, size_t bytes)
{
    while (r->image_bytes + bytes > HELP_IMAGE_BYTES) {
        int oldest = -1;
        for (int i = 0; i < r->nimages; i++)
            if (r->images[i].image.px &&
                (oldest < 0 || r->images[i].used < r->images[oldest].used))
                oldest = i;
        if (oldest < 0) break;
        tiku_image_t *im = &r->images[oldest].image;
        r->image_bytes -= (size_t)im->w * im->h * sizeof *im->px;
        tiku_image_free(im);
    }
}

/** @brief Image @p i's decoded pixels, decoding it when none are held;
 *         NULL when it cannot be shown. */
static tiku_image_t *reader_image_pixels(tiku_reader_help_window_t *c, int i)
{
    struct help_reader *r = c->reader;
    help_image_t *entry = &r->images[i];
    if (entry->failed) return NULL;
    entry->used = ++r->image_clock;
    if (!entry->image.px) {
        char path[TIKU_PATH_MAX]; int width = 0, height = 0;
        /* An evicted image may have changed on disk since layout: its
         * confinement and size are checked again before its pixels are
         * loaded. */
        if (!entry->embedded && (!tiku_reader_path(c, entry->src, path, sizeof path) ||
            !reader_image_allowed(path, &width, &height) ||
            width != entry->width || height != entry->height)) {
            entry->failed = 1; return NULL;
        }
        size_t bytes = (size_t)entry->width * entry->height *
                       sizeof(uint32_t);
        reader_image_room(r, bytes);
        tiku_image_t im = {0};
        tiku_image_err_t status = entry->embedded ?
            tiku_image_data_png(entry->src,strlen(entry->src),&im) : tiku_image_load(path,&im);
        if (status || !im.px ||
            im.w != entry->width || im.h != entry->height) {
            tiku_image_free(&im); entry->failed = 1; return NULL;
        }
        entry->image = im;
        r->image_bytes += bytes;
    }
    return &entry->image;
}

/** @brief The image's intrinsic dimensions, before document layout applies sizing.
 *  @return 0 when it cannot be shown. */
int tiku_reader_image_size(void *ctx, const char *src, int *w, int *h)
{
    tiku_reader_help_window_t *c = ctx;
    int i = reader_image_index(c, src); if (i < 0) return 0;
    help_image_t *im = &c->reader->images[i]; if (im->failed) return 0;
    *w = im->width; *h = im->height;
    return 1;
}

/** @brief Draw image @p src scaled into @p where and blended over the page,
 *         within the clip; a frame and a note when it cannot be shown. */
void tiku_reader_image_draw(void *ctx, const char *src, tiku_rect_t where)
{
    tiku_reader_help_window_t *c = ctx; struct help_reader *r = c->reader;
    int i = reader_image_index(c, src); if (i < 0 || !r->surface) return;
    tiku_image_t *im = reader_image_pixels(c, i);
    if (!im) {
        tiku_frame(r->surface, where, tiku_ink(TIKU_INK_FAINT));
        if (where.w > 80 && where.h >= tiku_font_plain()->height)
            tiku_text_line_draw(r->surface, where, TIKU_MEASURE_PLAIN,
                                "Image unavailable; Reload to retry",
                                tiku_ink(TIKU_INK_FAINT));
        return;
    }
    tiku_rect_t clip = r->surface->clip;
    tiku_gfx_rec_miss(r->surface);
    for (int y = 0; y < where.h; y++) {
        if (where.y + y < clip.y || where.y + y >= clip.y + clip.h) continue;
        for (int x = 0; x < where.w; x++) {
            if (where.x + x < clip.x || where.x + x >= clip.x + clip.w) continue;
            uint32_t px = im->px[(long)y * im->h / where.h * im->w + (long)x * im->w / where.w];
            unsigned a = px >> 24;
            if (a) {
                int scale = r->surface->scale > 0 ? r->surface->scale : 1;
                size_t stride = (size_t)r->surface->w*scale;
                uint32_t bg = r->surface->px[(size_t)(where.y+y)*scale*stride+(where.x+x)*scale];
                unsigned red = (((px >> 16) & 255) * a + ((bg >> 16) & 255) * (255-a)) / 255;
                unsigned green = (((px >> 8) & 255) * a + ((bg >> 8) & 255) * (255-a)) / 255;
                unsigned blue = ((px & 255) * a + (bg & 255) * (255-a)) / 255;
                tiku_pixel(r->surface, where.x+x, where.y+y, TIKU_RGB(red, green, blue));
            }
        }
    }
}
