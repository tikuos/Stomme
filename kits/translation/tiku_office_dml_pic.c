/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_dml_pic.c - pictures given to the writer, and what a
 * picture's a:blip does to it on the way: a duotone between two inks,
 * greyscale, brightness and contrast, one colour made another, opacity.
 *
 * A picture is read from its package once for each way it is changed.
 * Left as it is, it goes to the writer as its file -- a JPEG untouched;
 * changed, it is decoded, each pixel changed, and given as pixels.  A
 * Windows metafile is kept as it is, to be drawn wherever it is shown.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_dml.h"
#include "tiku_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEAR 4                      /* a:clrChange's match, per channel  */

/** @brief A picture part, what was done to it, the writer's number. */
struct dml_pic {
    char           part[256];
    dml_blip_t     fx;
    int            n;               /* -1 when it cannot be read         */
    unsigned char *meta;            /* a metafile's bytes, drawn instead */
    size_t         nmeta;
};

/** @brief A per-cent attribute (100000 whole) as a fraction. */
static float
frac(const tiku_xml_node_t *e, const char *name, float dflt)
{
    const char *v = tiku_xml_attr(e, name);

    return v != NULL ? (float)(atof(v) / 100000.0) : dflt;
}

void
dml_blip(const dml_ctx_t *c, const tiku_xml_node_t *blip, dml_blip_t *fx)
{
    const tiku_xml_node_t *duo = tiku_xml_child(blip, "a:duotone");
    const tiku_xml_node_t *lum = tiku_xml_child(blip, "a:lum");
    const tiku_xml_node_t *chg = tiku_xml_child(blip, "a:clrChange");
    const tiku_xml_node_t *k;
    int i = 0;

    memset(fx, 0, sizeof *fx);
    fx->alpha = frac(tiku_xml_child(blip, "a:alphaModFix"), "amt", 1);
    fx->bright = frac(lum, "bright", 0);
    fx->contrast = frac(lum, "contrast", 0);
    if (tiku_xml_child(blip, "a:grayscl") != NULL) {
        fx->tone = DML_TONE_GREY;
    }
    for (k = tiku_xml_child(duo, NULL); k != NULL && i < 2;
         k = tiku_xml_next(k, NULL)) {
        if ((fx->ink[i] = dml_color_of(c, k)) != 0) {
            i++;
        }
    }
    if (i == 2) {
        fx->tone = DML_TONE_DUO;
    }
    if (chg != NULL) {
        fx->from = dml_color(c, tiku_xml_child(chg, "a:clrFrom"));
        fx->to = dml_color(c, tiku_xml_child(chg, "a:clrTo"));
        fx->change = 1;
    }
}

/** @brief Whether @p fx leaves a picture as its file has it. */
static int
plain(const dml_blip_t *fx)
{
    return fx == NULL || (fx->tone == DML_TONE_NONE && fx->bright == 0 &&
                          fx->contrast == 0 && !fx->change &&
                          fx->alpha >= 1);
}

/** @brief Whether two changes are the same change. */
static int
same(const dml_blip_t *a, const dml_blip_t *b)
{
    if (plain(a) || plain(b)) {
        return plain(a) && plain(b);
    }
    return a->tone == b->tone && a->ink[0] == b->ink[0] &&
           a->ink[1] == b->ink[1] && a->bright == b->bright &&
           a->contrast == b->contrast && a->change == b->change &&
           a->from == b->from && a->to == b->to && a->alpha == b->alpha;
}

/** @brief A channel, 0 to 1, given a:lum's contrast about the middle,
 *         then its brightness as a move toward white or black. */
static float
lum(float v, const dml_blip_t *fx)
{
    float k = fx->contrast >= 1 ? 1000 : fx->contrast >= 0 ?
              1 / (1 - fx->contrast) : 1 + fx->contrast;

    v = (v - 0.5f) * k + 0.5f;
    v = v < 0 ? 0 : v > 1 ? 1 : v;
    return fx->bright >= 0 ? v + (1 - v) * fx->bright
                           : v * (1 + fx->bright);
}

/** @brief Whether @p a and @p b are one colour, give or take a little. */
static int
near(uint32_t a, uint32_t b)
{
    int s;

    for (s = 0; s < 24; s += 8) {
        if (abs((int)(a >> s & 255) - (int)(b >> s & 255)) > NEAR) {
            return 0;
        }
    }
    return 1;
}

/** @brief One pixel, 0xAARRGGBB, changed as @p fx says. */
static uint32_t
change(uint32_t px, const dml_blip_t *fx)
{
    float ch[3], y, a;
    int i;

    if (fx->change && near(px, fx->from)) {
        px = fx->to;
    }
    for (i = 0; i < 3; i++) {
        ch[i] = (float)(px >> (16 - 8 * i) & 255) / 255;
        if (fx->bright != 0 || fx->contrast != 0) {
            ch[i] = lum(ch[i], fx);
        }
    }
    y = 0.299f * ch[0] + 0.587f * ch[1] + 0.114f * ch[2];
    for (i = 0; i < 3; i++) {
        if (fx->tone == DML_TONE_GREY) {
            ch[i] = y;
        } else if (fx->tone == DML_TONE_DUO) {
            int s = 16 - 8 * i;

            ch[i] = ((float)(fx->ink[0] >> s & 255) * (1 - y) +
                     (float)(fx->ink[1] >> s & 255) * y) / 255;
        }
    }
    a = (float)(px >> 24) * fx->alpha;
    return (uint32_t)(a + 0.5f) << 24 |
           (uint32_t)(ch[0] * 255 + 0.5f) << 16 |
           (uint32_t)(ch[1] * 255 + 0.5f) << 8 |
           (uint32_t)(ch[2] * 255 + 0.5f);
}

/** @brief A picture's bytes given to the writer as @p fx changes them.
 *  @return its number, or -1. */
static int
changed(dml_ctx_t *c, const unsigned char *b, size_t len,
        const dml_blip_t *fx)
{
    tiku_image_t im;
    size_t i, n;
    int k;

    if (plain(fx)) {
        return tiku_pdf_writer_image(c->w, b, len, NULL, NULL);
    }
    if (tiku_image_decode(b, len, &im) != TIKU_IMG_OK) {
        return -1;
    }
    n = (size_t)im.w * (size_t)im.h;
    for (i = 0; i < n; i++) {
        im.px[i] = change(im.px[i], fx);
    }
    k = tiku_pdf_writer_pixels(c->w, im.px, im.w, im.h);
    tiku_image_free(&im);
    return k;
}

int
dml_picture(dml_ctx_t *c, const char *part, const dml_blip_t *fx)
{
    struct dml_pic *p;
    unsigned char *b;
    size_t len = 0;
    int i;

    for (i = 0; i < c->npics; i++) {
        if (strcmp(c->pics[i].part, part) == 0 && same(&c->pics[i].fx, fx)) {
            return c->pics[i].n;
        }
    }
    if (c->npics == c->piccap) {
        int cap = c->piccap ? c->piccap * 2 : 16;

        if ((p = realloc(c->pics, (size_t)cap * sizeof *p)) == NULL) {
            return -1;
        }
        c->pics = p;
        c->piccap = cap;
    }
    p = &c->pics[c->npics++];
    memset(p, 0, sizeof *p);
    snprintf(p->part, sizeof p->part, "%s", part);
    if (fx != NULL) {
        p->fx = *fx;
    }
    b = part[0] ? of_pkg_read(c->pkg, part, OF_PART_MAX, &len) : NULL;
    p->n = b != NULL ? changed(c, b, len, fx) : -1;
    if (p->n < 0 && b != NULL && of_metafile_is(b, len)) {
        p->meta = b;                /* kept, to be drawn as it says */
        p->nmeta = len;
        return -1;
    }
    free(b);
    return p->n;
}

const unsigned char *
dml_metafile(const dml_ctx_t *c, const char *part, size_t *len)
{
    int i;

    for (i = 0; i < c->npics; i++) {
        if (c->pics[i].meta != NULL && strcmp(c->pics[i].part, part) == 0) {
            *len = c->pics[i].nmeta;
            return c->pics[i].meta;
        }
    }
    return NULL;
}

void
dml_done(dml_ctx_t *c)
{
    int i;

    for (i = 0; i < c->npics; i++) {
        free(c->pics[i].meta);
    }
    free(c->pics);
    c->pics = NULL;
    c->npics = c->piccap = 0;
}
