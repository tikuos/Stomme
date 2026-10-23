/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_pdf_image.c - a picture on the page: its samples decoded, its
 * soft mask or stencil made an alpha, and the image laid over the unit
 * square the CTM places.
 *
 * ISO 32000 8.9: samples of 1 to 16 bits in a colour space, or a
 * one-bit stencil that paints the fill colour.  JPEGs and JPEG 2000s go
 * through the kit's decoders; what they cannot read becomes a grey box
 * of the image's size.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_private.h"
#include "tiku_jpeg.h"
#include "tiku_jpx.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define IMG_MAX_PIXELS  (24L * 1024L * 1024L)
#define GREY            0xc0

/** @brief @p key of @p d, or its inline-image short name @p alt. */
static pdf_obj_t *
key(tiku_pdf_t *doc, pdf_obj_t *d, const char *k, const char *alt)
{
    pdf_obj_t *o = pdf_get(doc, d, k);

    return o != NULL ? o : pdf_get(doc, d, alt);
}

/** @brief A Decode array's numbers into @p v, @return how many. */
static int
decode_array(tiku_pdf_t *doc, pdf_obj_t *d, double *v, int max)
{
    pdf_obj_t *a = key(doc, d, "Decode", "D");
    int i, n = 0;

    if (a == NULL || a->kind != PDF_ARRAY) {
        return 0;
    }
    for (i = 0; i < a->u.a.n && n < max; i++) {
        v[n++] = pdf_number(pdf_resolve(doc, a->u.a.item[i]), 0);
    }
    return n;
}

/** @brief The bytes a row of @p w samples of @p n by @p bpc bits takes. */
static long
row_bytes(long w, int n, int bpc)
{
    return (w * n * bpc + 7) / 8;
}

/** @brief The whole image the one grey, opaque. */
static int
grey_box(pdf_img_t *img)
{
    img->rgb = malloc((size_t)img->w * (size_t)img->h * 3);
    if (img->rgb == NULL) {
        return -1;
    }
    memset(img->rgb, GREY, (size_t)img->w * (size_t)img->h * 3);
    return 0;
}

/** @brief A JPEG's pixels into @p img; a kind the decoder refuses is a
 *        grey box. */
static int
from_jpeg(pdf_img_t *img, const unsigned char *data, long n, int invert)
{
    uint32_t *px = NULL;
    int w = 0, h = 0;
    long i, total;

    if (tiku_jpeg_decode(data, n, &w, &h, &px) != 0 || px == NULL ||
        w <= 0 || h <= 0) {
        free(px);
        return grey_box(img);
    }
    img->w = w;
    img->h = h;
    total = (long)w * h;
    img->rgb = malloc((size_t)total * 3);
    if (img->rgb == NULL) {
        free(px);
        return -1;
    }
    for (i = 0; i < total; i++) {
        unsigned r = (px[i] >> 16) & 0xff, g = (px[i] >> 8) & 0xff;
        unsigned b = px[i] & 0xff;

        if (invert) {
            r = 255 - r;
            g = 255 - g;
            b = 255 - b;
        }
        img->rgb[i * 3] = (unsigned char)r;
        img->rgb[i * 3 + 1] = (unsigned char)g;
        img->rgb[i * 3 + 2] = (unsigned char)b;
    }
    free(px);
    return 0;
}

/** @brief Channel @p k of @p j at eight bits: as coded when it has
 *         fewer, stretched, unless @p raw keeps them as indices. */
static unsigned
jpx_eight(const tiku_jpx_t *j, int k, long i, int raw)
{
    unsigned v = j->comp[k][i];

    return j->prec[k] < 8 && !raw ? v * 255u / ((1u << j->prec[k]) - 1u) : v;
}

/** @brief A JPEG 2000 mask's first channel into @p out, @p w by @p h,
 *         spread or thinned to that size.  @return 0, or -1. */
static int
jpx_grey(const unsigned char *data, long n, int w, int h, unsigned char *out)
{
    tiku_jpx_t j;
    long x, y;

    if (tiku_jpx_decode(data, n, IMG_MAX_PIXELS, &j) != 0) {
        return -1;
    }
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            out[y * w + x] = (unsigned char)jpx_eight(
                &j, 0, (y * j.h / h) * j.w + x * j.w / w, 0);
        }
    }
    tiku_jpx_free(&j);
    return 0;
}

/**
 * @brief The samples of @p dict (its own stream, or @p raw) as one grey
 *        per pixel, @p w by @p h: how a soft mask and a stencil are both
 *        read.  @return them, or NULL.
 */
static unsigned char *
grey_samples(tiku_pdf_t *doc, pdf_obj_t *dict, const unsigned char *raw,
             long rawn, int *w, int *h, int stencil, pdf_obj_t *res)
{
    unsigned char *data = NULL, *out;
    long n = 0, total, i;
    const char *filter = NULL;
    int bpc = stencil ? 1 : pdf_int(key(doc, dict, "BitsPerComponent",
                                        "BPC"), 8);
    double dec[2];
    int ndec = decode_array(doc, dict, dec, 2);
    int inv = ndec == 2 && dec[0] > dec[1];
    pdf_cs_t *cs = NULL;

    *w = pdf_int(key(doc, dict, "Width", "W"), 0);
    *h = pdf_int(key(doc, dict, "Height", "H"), 0);
    if (*w <= 0 || *h <= 0 || (long)*w * *h > IMG_MAX_PIXELS) {
        return NULL;
    }
    if (raw != NULL ? pdf_filter_bytes(doc, dict, raw, rawn, &data, &n, &filter)
                    : pdf_stream_data(doc, dict, &data, &n, &filter)) {
        return NULL;
    }
    total = (long)*w * *h;
    out = malloc((size_t)total);
    if (out == NULL) {
        free(data);
        return NULL;
    }
    if (filter != NULL) {
        if (!strcmp(filter, "DCTDecode")) {
            pdf_img_t j = {0};

            if (from_jpeg(&j, data, n, 0) == 0 && j.rgb != NULL &&
                j.w == *w && j.h == *h) {
                for (i = 0; i < total; i++) {
                    out[i] = j.rgb[i * 3];
                }
            } else {
                memset(out, 255, (size_t)total);
            }
            pdf_image_free(&j);
        } else if (strcmp(filter, "JPXDecode") != 0 ||
                   jpx_grey(data, n, *w, *h, out) != 0) {
            memset(out, 255, (size_t)total);    /* unreadable: no mask */
        }
        free(data);
        return out;
    }
    if (!stencil) {
        cs = pdf_cs_load(doc, key(doc, dict, "ColorSpace", "CS"), res);
    }
    for (i = 0; i < total; i++) {
        long y = i / *w, x = i % *w;
        long rb = row_bytes(*w, cs != NULL ? cs->n : 1, bpc);
        long bit = x * (cs != NULL ? cs->n : 1) * bpc;

        if (y * rb + bit / 8 + (bpc + 7) / 8 > n) {
            out[i] = stencil ? (unsigned char)(inv ? 0 : 255) : 255;
            continue;
        }
        if (stencil) {
            int b = (data[y * rb + bit / 8] >> (7 - bit % 8)) & 1;

            out[i] = (unsigned char)((b ^ inv) ? 0 : 255);    /* 0 paints */
        } else {
            unsigned char rgb[3];

            pdf_cs_sample(cs, data + y * rb, bpc, x, ndec >= 2 ? dec : NULL,
                          rgb);
            out[i] = rgb[0];
        }
    }
    pdf_cs_free(cs);
    free(data);
    return out;
}

/** @brief @p m of @p mw by @p mh resampled to @p w by @p h, nearest. */
static unsigned char *
resample(const unsigned char *m, int mw, int mh, int w, int h)
{
    unsigned char *out = malloc((size_t)w * (size_t)h);
    int x, y;

    if (out == NULL) {
        return NULL;
    }
    for (y = 0; y < h; y++) {
        int sy = (int)((long)y * mh / h);

        for (x = 0; x < w; x++) {
            out[(size_t)y * w + x] = m[(size_t)sy * mw + (long)x * mw / w];
        }
    }
    return out;
}

/** @brief The image's alpha from its SMask, or its Mask when a stencil. */
static void
load_alpha(tiku_pdf_t *doc, pdf_obj_t *dict, pdf_img_t *img, pdf_obj_t *res)
{
    pdf_obj_t *sm = pdf_get(doc, dict, "SMask");
    pdf_obj_t *mk = pdf_get(doc, dict, "Mask");
    unsigned char *m = NULL;
    int mw = 0, mh = 0;

    if (sm != NULL && sm->kind == PDF_STREAM) {
        m = grey_samples(doc, sm, NULL, 0, &mw, &mh, 0, res);
    } else if (mk != NULL && mk->kind == PDF_STREAM) {
        /* A stencil mask: a 0 shows the image, a 1 hides it, which is
         * the stencil's own sense of painted. */
        m = grey_samples(doc, mk, NULL, 0, &mw, &mh, 1, res);
    }
    if (m == NULL) {
        return;
    }
    if (img->alpha != NULL) {       /* the picture's own, as it asked */
        free(m);
        return;
    }
    if (mw == img->w && mh == img->h) {
        img->alpha = m;
    } else {
        img->alpha = resample(m, mw, mh, img->w, img->h);
        free(m);
    }
}

/** @brief The device space a JPEG 2000's @p ncol colour channels are
 *         in, as the file says or by how many there are. */
static pdf_cs_t *
jpx_space(const tiku_jpx_t *j, int ncol)
{
    int parts = j->space == TIKU_JPX_GRAY ? 1 :
                j->space == TIKU_JPX_CMYK ? 4 :
                j->space == TIKU_JPX_SRGB || j->space == TIKU_JPX_SYCC ? 3 :
                ncol >= 4 ? 4 : ncol >= 3 ? 3 : 1;

    return ncol < 1 ? NULL : pdf_cs_device(parts <= ncol ? parts : 1);
}

/** @brief @p j's first colour channels through @p cs into @p img's
 *         RGB.  @return 0, or -1. */
static int
jpx_colour(const tiku_jpx_t *j, const pdf_cs_t *cs, pdf_img_t *img)
{
    long i, total = (long)j->w * j->h;
    unsigned char s[TIKU_JPX_COMPS];
    int k;

    img->rgb = malloc((size_t)total * 3);
    if (img->rgb == NULL) {
        return -1;
    }
    for (i = 0; i < total; i++) {
        for (k = 0; k < cs->n && k < TIKU_JPX_COMPS; k++) {
            s[k] = (unsigned char)jpx_eight(j, k, i, cs->kind == CS_INDEXED);
        }
        pdf_cs_sample(cs, s, 8, 0, NULL, img->rgb + i * 3);
    }
    return 0;
}

/** @brief @p j's opacity made @p img's alpha when SMaskInData asks and
 *         no SMask overrides it (8.9.5.3), the colours taken back out of
 *         it when they were multiplied by it. */
static void
jpx_alpha(tiku_pdf_t *doc, pdf_obj_t *dict, const tiku_jpx_t *j,
          pdf_img_t *img)
{
    int in = pdf_int(pdf_get(doc, dict, "SMaskInData"), 0), k;
    long i, total = (long)j->w * j->h;

    if (in == 0 || j->alpha < 0 || pdf_get(doc, dict, "SMask") != NULL ||
        (img->alpha = malloc((size_t)total)) == NULL) {
        return;
    }
    for (i = 0; i < total; i++) {
        unsigned a = jpx_eight(j, j->alpha, i, 0);

        img->alpha[i] = (unsigned char)a;
        for (k = 0; (in == 2 || j->premultiplied) && a > 0 && k < 3; k++) {
            unsigned c = img->rgb[i * 3 + k] * 255u / a;

            img->rgb[i * 3 + k] = (unsigned char)(c > 255 ? 255 : c);
        }
    }
}

/**
 * @brief A JPEG 2000's pixels into @p img, at the size it decodes to: in
 *        the dictionary's colour space when the channels can fill it,
 *        else the file's own, its opacity the alpha when asked (7.4.9).
 *        One the decoder refuses is a grey box.
 */
static int
from_jpx(tiku_pdf_t *doc, pdf_obj_t *dict, pdf_img_t *img,
         const unsigned char *data, long n, pdf_obj_t *res)
{
    tiku_jpx_t j;
    pdf_cs_t *cs;
    int ncol, rc;

    if (tiku_jpx_decode(data, n, IMG_MAX_PIXELS, &j) != 0) {
        return (long)img->w * img->h > IMG_MAX_PIXELS ? -1 : grey_box(img);
    }
    ncol = j.alpha >= 0 ? j.alpha : j.ncomp;
    cs = pdf_cs_load(doc, key(doc, dict, "ColorSpace", "CS"), res);
    if (cs == NULL || cs->kind == CS_PATTERN || cs->n > ncol) {
        pdf_cs_free(cs);
        cs = jpx_space(&j, ncol);
    }
    img->w = j.w;
    img->h = j.h;
    rc = cs == NULL ? grey_box(img) : jpx_colour(&j, cs, img);
    if (rc == 0) {
        jpx_alpha(doc, dict, &j, img);
    }
    pdf_cs_free(cs);
    tiku_jpx_free(&j);
    return rc;
}

/** @brief Samples of any depth in any colour space to RGB triples. */
static int
from_samples(tiku_pdf_t *doc, pdf_obj_t *dict, pdf_img_t *img,
             const unsigned char *data, long n, pdf_obj_t *res)
{
    int bpc = pdf_int(key(doc, dict, "BitsPerComponent", "BPC"), 8);
    pdf_cs_t *cs = pdf_cs_load(doc, key(doc, dict, "ColorSpace", "CS"), res);
    double dec[16];
    int ndec = decode_array(doc, dict, dec, 16), comps = cs != NULL ? cs->n : 1;
    long rb, y, x;

    if (bpc != 1 && bpc != 2 && bpc != 4 && bpc != 8 && bpc != 16) {
        pdf_cs_free(cs);
        return grey_box(img);
    }
    img->rgb = malloc((size_t)img->w * (size_t)img->h * 3);
    if (img->rgb == NULL) {
        pdf_cs_free(cs);
        return -1;
    }
    rb = row_bytes(img->w, comps, bpc);
    for (y = 0; y < img->h; y++) {
        const unsigned char *row = data + y * rb;

        for (x = 0; x < img->w; x++) {
            unsigned char *px = img->rgb + ((size_t)y * img->w + x) * 3;

            if ((y + 1) * rb > n) {
                px[0] = px[1] = px[2] = GREY;   /* short data: grey */
            } else {
                pdf_cs_sample(cs, row, bpc, x,
                              ndec >= 2 * comps ? dec : NULL, px);
            }
        }
    }
    pdf_cs_free(cs);
    return 0;
}

int
pdf_image_decode(tiku_pdf_t *doc, pdf_obj_t *dict, const unsigned char *raw,
                 long rawn, pdf_obj_t *res, pdf_img_t *out)
{
    unsigned char *data = NULL;
    long n = 0;
    const char *filter = NULL;
    pdf_obj_t *im = key(doc, dict, "ImageMask", "IM");
    int rc, big;

    memset(out, 0, sizeof *out);
    pdf_obj_t *interpolation = key(doc, dict, "Interpolate", "I");
    out->interpolate =
        interpolation && interpolation->kind == PDF_BOOL && interpolation->u.b;
    out->w = pdf_int(key(doc, dict, "Width", "W"), 0);
    out->h = pdf_int(key(doc, dict, "Height", "H"), 0);
    if (out->w <= 0 || out->h <= 0) {
        return -1;
    }
    big = (long)out->w * out->h > IMG_MAX_PIXELS;   /* JPX alone copes */
    if (im != NULL && im->kind == PDF_BOOL && im->u.b) {
        int w, h;

        out->stencil = 1;
        out->alpha = big ? NULL : grey_samples(doc, dict, raw, rawn, &w, &h,
                                               1, res);
        return out->alpha != NULL ? 0 : -1;
    }
    if (raw != NULL ? pdf_filter_bytes(doc, dict, raw, rawn, &data, &n, &filter)
                    : pdf_stream_data(doc, dict, &data, &n, &filter)) {
        return big ? -1 : grey_box(out);
    }
    if (filter != NULL && !strcmp(filter, "JPXDecode")) {
        rc = from_jpx(doc, dict, out, data, n, res);
    } else if (big) {
        rc = -1;
    } else if (filter == NULL) {
        rc = from_samples(doc, dict, out, data, n, res);
    } else if (!strcmp(filter, "DCTDecode")) {
        double dec[8];
        int ndec = decode_array(doc, dict, dec, 8);

        rc = from_jpeg(out, data, n, ndec >= 2 && dec[0] > dec[1]);
    } else {
        rc = grey_box(out);
    }
    free(data);
    if (rc == 0 && raw == NULL) {
        load_alpha(doc, dict, out, res);
    }
    return rc;
}

void
pdf_image_free(pdf_img_t *img)
{
    if (img != NULL) {
        free(img->rgb);
        free(img->alpha);
        img->rgb = img->alpha = NULL;
    }
}

/* --------------------------------------------------------------- drawing  */

/**
 * @brief @p img shrunk by whole factors to about the size it is drawn
 *        at, each new sample the mean of its box.  @return 0, or -1 out
 *        of memory.
 */
static int
shrink(const pdf_img_t *img, int fx, int fy, pdf_img_t *out)
{
    int x, y, k;

    memset(out, 0, sizeof *out);
    out->w = (img->w + fx - 1) / fx;
    out->h = (img->h + fy - 1) / fy;
    out->stencil = img->stencil;
    if (img->rgb != NULL) {
        out->rgb = malloc((size_t)out->w * (size_t)out->h * 3);
    }
    if (img->alpha != NULL) {
        out->alpha = malloc((size_t)out->w * (size_t)out->h);
    }
    if ((img->rgb != NULL && out->rgb == NULL) ||
        (img->alpha != NULL && out->alpha == NULL)) {
        pdf_image_free(out);
        return -1;
    }
    for (y = 0; y < out->h; y++) {
        for (x = 0; x < out->w; x++) {
            long sum[4] = {0, 0, 0, 0}, cnt = 0;
            int sx, sy;

            for (sy = y * fy; sy < (y + 1) * fy && sy < img->h; sy++) {
                for (sx = x * fx; sx < (x + 1) * fx && sx < img->w; sx++) {
                    size_t i = (size_t)sy * img->w + sx;

                    if (img->rgb != NULL) {
                        for (k = 0; k < 3; k++) {
                            sum[k] += img->rgb[i * 3 + k];
                        }
                    }
                    if (img->alpha != NULL) {
                        sum[3] += img->alpha[i];
                    }
                    cnt++;
                }
            }
            if (cnt == 0) {
                cnt = 1;
            }
            if (out->rgb != NULL) {
                for (k = 0; k < 3; k++) {
                    out->rgb[((size_t)y * out->w + x) * 3 + k] =
                        (unsigned char)(sum[k] / cnt);
                }
            }
            if (out->alpha != NULL) {
                out->alpha[(size_t)y * out->w + x] =
                    (unsigned char)(sum[3] / cnt);
            }
        }
    }
    return 0;
}

/** @brief @p v held to 0..@p hi, as an int; a NaN is 0. */
static int
within(double v, int hi)
{
    return v > 0 ? (v < hi ? (int)v : hi) : 0;
}

/** @brief The device pixels the unit square covers under @p ctm,
 *        clipped to @p pw by @p ph.  @return 0 when none. */
static int
device_box(const pdf_mat_t *ctm, int pw, int ph, int *box)
{
    double minx = 0, maxx = 0, miny = 0, maxy = 0;
    int k;

    for (k = 0; k < 4; k++) {
        double x, y;

        pdf_mat_apply(ctm, (k == 1 || k == 2) ? 1 : 0, (k >= 2) ? 1 : 0,
                      &x, &y);
        if (k == 0 || x < minx) minx = x;
        if (k == 0 || x > maxx) maxx = x;
        if (k == 0 || y < miny) miny = y;
        if (k == 0 || y > maxy) maxy = y;
    }
    box[0] = within(minx, pw);
    box[1] = within(miny, ph);
    box[2] = within(maxx + 0.9999, pw);
    box[3] = within(maxy + 0.9999, ph);
    return box[0] < box[2] && box[1] < box[3];
}

/** @brief How many source samples land on one device pixel each way:
 *        the whole factors the image is shrunk by first. */
static void
shrink_factors(const pdf_mat_t *ctm, const pdf_img_t *img, int *fx, int *fy)
{
    double dw = sqrt(ctm->a * ctm->a + ctm->b * ctm->b);
    double dh = sqrt(ctm->c * ctm->c + ctm->d * ctm->d);

    *fx = (int)(img->w / (dw > 1 ? dw : 1));     /* a NaN is 1 */
    *fy = (int)(img->h / (dh > 1 ? dh : 1));
    if (*fx < 1) *fx = 1;
    if (*fy < 1) *fy = 1;
}

/** @brief The matrix taking device points back to the unit square. */
static pdf_mat_t
inverse(const pdf_mat_t *m)
{
    double det = m->a * m->d - m->b * m->c;

    return pdf_mat(m->d / det, -m->b / det, -m->c / det, m->a / det,
                   (m->c * m->f - m->d * m->e) / det,
                   (m->b * m->e - m->a * m->f) / det);
}

/** @brief Sample clamped image texels with premultiplied-alpha interpolation.
 */
static uint32_t
smooth_sample(const pdf_img_t *image, double u, double v, uint32_t fill)
{
    double x = u * image->w - 0.5, y = (1 - v) * image->h - 0.5;
    int left = (int)floor(x), top = (int)floor(y);
    double fx = x - left, fy = y - top, alpha = 0, color[3] = {0};
    for (unsigned i = 0; i < 4; i++) {
        int sx = left + (int)(i & 1), sy = top + (int)((i >> 1) & 1);
        sx = sx < 0 ? 0 : sx >= image->w ? image->w - 1 : sx;
        sy = sy < 0 ? 0 : sy >= image->h ? image->h - 1 : sy;
        size_t at = (size_t)sy * image->w + sx;
        double weight = (i & 1 ? fx : 1 - fx) * (i & 2 ? fy : 1 - fy);
        double a = weight * (image->alpha ? image->alpha[at] : 255);
        alpha += a;
        for (unsigned channel = 0; channel < 3; channel++)
            color[channel] +=
                a * (image->rgb ? image->rgb[at * 3 + channel]
                                : (fill >> (16 - 8 * channel)) & 255);
    }
    uint32_t result = (uint32_t)(alpha + 0.5) << 24;
    for (unsigned channel = 0; channel < 3; channel++)
        result |= (uint32_t)(alpha > 0 ? color[channel] / alpha + 0.5 : 0)
                  << (16 - 8 * channel);
    return result;
}
/** @brief One device pixel of the image: sampled, masked, clipped, laid. */
static void
image_pixel(pdf_pix_t *pix, const pdf_img_t *src, const pdf_mat_t *inv,
            const pdf_mask_t *clip, uint32_t fill, int ca, int x, int y)
{
    double u, v;
    int sx, sy, a = ca, shape = 255;
    size_t i;

    pdf_mat_apply(inv, x + 0.5, y + 0.5, &u, &v);
    if (!(u >= 0 && u < 1 && v >= 0 && v < 1)) {
        return;                     /* off the image, or a NaN */
    }
    sx = (int)(u * src->w);
    sy = (int)((1 - v) * src->h);           /* the first row is the top */
    if (sx >= src->w) sx = src->w - 1;
    if (sy >= src->h) sy = src->h - 1;
    i = (size_t)sy * src->w + sx;
    uint32_t color = src->rgb ? 0xff000000u | (uint32_t)src->rgb[i * 3] << 16 |
                                    (uint32_t)src->rgb[i * 3 + 1] << 8 |
                                    src->rgb[i * 3 + 2]
                              : fill;
    if (src->interpolate) {
        color = smooth_sample(src, u, v, fill);
        a = (a * (int)(color >> 24) + 127) / 255;
    } else if (src->alpha)
        a = (a * src->alpha[i] + 127) / 255;
    if (clip != NULL && clip->a != NULL) {
        int kx = x - clip->x0, ky = y - clip->y0;

        if (kx < 0 || kx >= clip->w || ky < 0 || ky >= clip->h) {
            return;
        }
        shape = clip->a[(size_t)ky * clip->w + kx];
        a = (a * shape + 127) / 255;
    }
    if (a > 0) {
        pdf_blend(pix, pix->px + (size_t)y * pix->w + x, color, a, shape);
    }
}

void
pdf_image_draw(pdf_pix_t *pix, const pdf_img_t *img, const pdf_mat_t *ctm,
               const pdf_mask_t *clip, uint32_t fill, double alpha)
{
    pdf_img_t small;
    const pdf_img_t *src = img;
    pdf_mat_t inv;
    int box[4], x, y, fx, fy, ca = (int)(alpha * 255 + 0.5);

    if (img->w <= 0 || img->h <= 0 || ctm->a * ctm->d == ctm->b * ctm->c ||
        ca <= 0 || (img->rgb == NULL && img->alpha == NULL) ||
        !device_box(ctm, pix->w, pix->h, box)) {
        return;
    }
    if (ca > 255) ca = 255;
    shrink_factors(ctm, img, &fx, &fy);
    if ((fx > 1 || fy > 1) && shrink(img, fx, fy, &small) == 0) {
        small.interpolate = img->interpolate;
        src = &small;
    }
    inv = inverse(ctm);
    for (y = box[1]; y < box[3]; y++) {
        for (x = box[0]; x < box[2]; x++) {
            image_pixel(pix, src, &inv, clip, fill, ca, x, y);
        }
    }
    if (src == &small) {
        pdf_image_free(&small);
    }
}
