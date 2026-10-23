/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_dml_color.c - a theme, and the colours that refer to it.
 *
 * A colour is named in one of six ways -- sRGB, a scheme colour through
 * the slide's colour map, a system colour, a preset name, HSL, linear
 * RGB -- and then changed by any number of modifiers: luminance and
 * saturation in HSL, tints and shades in linear light, alpha.  Office's
 * theme colours are nearly always the scheme's with lumMod and lumOff.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_dml.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief A colour being worked on: channels 0 to 1, and its alpha. */
typedef struct {
    double r, g, b, a;
} rgba_t;

/** @brief The colour map's twelve names, in dml_ctx_t's map order. */
static const char *const mapped[12] = {"bg1", "tx1", "bg2", "tx2",
    "accent1", "accent2", "accent3", "accent4", "accent5", "accent6",
    "hlink", "folHlink"};
/** @brief The theme's colour names, in DML_* order. */
static const char *const scheme[DML_COLORS] = {"dk1", "lt1", "dk2", "lt2",
    "accent1", "accent2", "accent3", "accent4", "accent5", "accent6",
    "hlink", "folHlink"};

/* ------------------------------------------------------------ spaces  */

/** @brief An sRGB channel in linear light. */
static double
linear(double c)
{
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

/** @brief A linear channel as sRGB. */
static double
srgb(double c)
{
    c = c < 0 ? 0 : c > 1 ? 1 : c;
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1 / 2.4) - 0.055;
}

/** @brief RGB as hue (0-360), saturation and luminance (0-1). */
static void
to_hsl(const rgba_t *c, double *h, double *s, double *l)
{
    double mx = fmax(c->r, fmax(c->g, c->b)), mn = fmin(c->r, fmin(c->g,
                                                                   c->b));
    double d = mx - mn;

    *l = (mx + mn) / 2;
    *s = d < 1e-9 ? 0 : *l < 0.5 ? d / (mx + mn) : d / (2 - mx - mn);
    if (d < 1e-9) {
        *h = 0;
    } else if (mx == c->r) {
        *h = fmod((c->g - c->b) / d + 6, 6) * 60;
    } else if (mx == c->g) {
        *h = ((c->b - c->r) / d + 2) * 60;
    } else {
        *h = ((c->r - c->g) / d + 4) * 60;
    }
}

/** @brief One channel of HSL's way back to RGB. */
static double
hue_part(double p, double q, double t)
{
    t = t < 0 ? t + 1 : t > 1 ? t - 1 : t;
    return t < 1.0 / 6 ? p + (q - p) * 6 * t : t < 0.5 ? q :
           t < 2.0 / 3 ? p + (q - p) * (2.0 / 3 - t) * 6 : p;
}

/** @brief Hue, saturation and luminance as RGB into @p c. */
static void
from_hsl(double h, double s, double l, rgba_t *c)
{
    double q, p;

    s = s < 0 ? 0 : s > 1 ? 1 : s;
    l = l < 0 ? 0 : l > 1 ? 1 : l;
    if (s < 1e-9) {
        c->r = c->g = c->b = l;
        return;
    }
    q = l < 0.5 ? l * (1 + s) : l + s - l * s;
    p = 2 * l - q;
    h = fmod(fmod(h, 360) + 360, 360) / 360;
    c->r = hue_part(p, q, h + 1.0 / 3);
    c->g = hue_part(p, q, h);
    c->b = hue_part(p, q, h - 1.0 / 3);
}

/* ----------------------------------------------------------- naming  */

/** @brief "RRGGBB" as a colour; black for nonsense. */
static void
hex(const char *v, rgba_t *c)
{
    unsigned long x = v != NULL ? strtoul(v, NULL, 16) : 0;

    c->r = (double)(x >> 16 & 0xff) / 255;
    c->g = (double)(x >> 8 & 0xff) / 255;
    c->b = (double)(x & 0xff) / 255;
    c->a = 1;
}

/** @brief A preset colour's name as RGB: the names slides use. */
static void
preset(const char *v, rgba_t *c)
{
    static const struct { const char *name, *rgb; } p[] = {
        {"black", "000000"}, {"white", "FFFFFF"}, {"red", "FF0000"},
        {"green", "008000"}, {"lime", "00FF00"}, {"blue", "0000FF"},
        {"yellow", "FFFF00"}, {"cyan", "00FFFF"}, {"aqua", "00FFFF"},
        {"magenta", "FF00FF"}, {"fuchsia", "FF00FF"}, {"gray", "808080"},
        {"grey", "808080"}, {"silver", "C0C0C0"}, {"maroon", "800000"},
        {"navy", "000080"}, {"olive", "808000"}, {"orange", "FFA500"},
        {"purple", "800080"}, {"teal", "008080"}, {"ltGray", "D3D3D3"},
        {"dkGray", "A9A9A9"}, {"darkBlue", "00008B"}, {"darkRed", "8B0000"},
        {"darkGreen", "006400"}, {"brown", "A52A2A"}, {"pink", "FFC0CB"},
        {"gold", "FFD700"}};
    size_t i;

    for (i = 0; v != NULL && i < sizeof p / sizeof p[0]; i++) {
        if (strcmp(v, p[i].name) == 0) {
            hex(p[i].rgb, c);
            return;
        }
    }
    hex("000000", c);
}

/** @brief A scheme colour's name as its colour: through the map for
 *         bg1, tx1 ..., the theme's own for dk1, lt1 ..., phClr as the
 *         style reference gave it. */
static void
scheme_color(const dml_ctx_t *ctx, const char *v, rgba_t *c)
{
    uint32_t argb = 0xff000000u;
    int i;

    for (i = 0; v != NULL && i < 12; i++) {
        if (strcmp(v, mapped[i]) == 0) {
            argb = ctx->theme ? ctx->theme->clr[ctx->map[i]] : argb;
            v = NULL;
        }
    }
    for (i = 0; v != NULL && i < DML_COLORS; i++) {
        if (strcmp(v, scheme[i]) == 0) {
            argb = ctx->theme ? ctx->theme->clr[i] : argb;
            v = NULL;
        }
    }
    if (v != NULL && strcmp(v, "phClr") == 0) {
        argb = ctx->ph ? ctx->ph : argb;
    }
    c->r = (double)(argb >> 16 & 0xff) / 255;
    c->g = (double)(argb >> 8 & 0xff) / 255;
    c->b = (double)(argb & 0xff) / 255;
    c->a = (double)(argb >> 24) / 255;
}

/** @brief A percentage attribute (100000 = 1), @p dflt when absent. */
static double
pct(const tiku_xml_node_t *e, const char *name, double dflt)
{
    const char *v = tiku_xml_attr(e, name);

    return v != NULL ? atof(v) / 100000.0 : dflt;
}

/* ---------------------------------------------------------- modifiers  */

/** @brief One modifier of HSL's: luminance, saturation, hue. */
static int
hsl_mod(const tiku_xml_node_t *m, rgba_t *c)
{
    const char *n = m->name + 2;
    double h, s, l, v = pct(m, "val", 0);

    if (strncmp(n, "lum", 3) && strncmp(n, "sat", 3) && strncmp(n, "hue", 3)
        && strcmp(n, "comp") != 0) {
        return 0;
    }
    to_hsl(c, &h, &s, &l);
    if (strcmp(n, "lumMod") == 0) l *= v;
    else if (strcmp(n, "lumOff") == 0) l += v;
    else if (strcmp(n, "lum") == 0) l = v;
    else if (strcmp(n, "satMod") == 0) s *= v;
    else if (strcmp(n, "satOff") == 0) s += v;
    else if (strcmp(n, "sat") == 0) s = v;
    else if (strcmp(n, "hueMod") == 0) h *= v;
    else if (strcmp(n, "hueOff") == 0) h += v * 100000.0 / 60000.0;
    else if (strcmp(n, "hue") == 0) h = v * 100000.0 / 60000.0;
    else if (strcmp(n, "comp") == 0) h += 180;
    from_hsl(h, s, l, c);
    return 1;
}

/** @brief Every modifier under @p e, in order, applied to @p c. */
static void
modify(const tiku_xml_node_t *e, rgba_t *c)
{
    const tiku_xml_node_t *m;

    for (m = tiku_xml_child(e, NULL); m != NULL; m = tiku_xml_next(m, NULL)) {
        const char *n = m->name + 2;
        double v = pct(m, "val", 0);

        if (strncmp(m->name, "a:", 2) != 0 || hsl_mod(m, c)) {
            continue;
        }
        if (strcmp(n, "alpha") == 0) c->a = v;
        else if (strcmp(n, "alphaMod") == 0) c->a *= v;
        else if (strcmp(n, "alphaOff") == 0) c->a += v;
        else if (strcmp(n, "shade") == 0) {
            c->r = srgb(linear(c->r) * v);
            c->g = srgb(linear(c->g) * v);
            c->b = srgb(linear(c->b) * v);
        } else if (strcmp(n, "tint") == 0) {
            c->r = srgb(1 - (1 - linear(c->r)) * v);
            c->g = srgb(1 - (1 - linear(c->g)) * v);
            c->b = srgb(1 - (1 - linear(c->b)) * v);
        } else if (strcmp(n, "inv") == 0) {
            c->r = 1 - c->r;
            c->g = 1 - c->g;
            c->b = 1 - c->b;
        } else if (strcmp(n, "gray") == 0) {
            c->r = c->g = c->b = 0.299 * c->r + 0.587 * c->g + 0.114 * c->b;
        }
    }
}

uint32_t
dml_color(const dml_ctx_t *ctx, const tiku_xml_node_t *parent)
{
    const tiku_xml_node_t *e;
    uint32_t c;

    for (e = tiku_xml_child(parent, NULL); e; e = tiku_xml_next(e, NULL)) {
        if ((c = dml_color_of(ctx, e)) != 0) {
            return c;
        }
    }
    return 0;
}

uint32_t
dml_color_of(const dml_ctx_t *ctx, const tiku_xml_node_t *e)
{
    rgba_t c = {0, 0, 0, 1};
    double k;

    if (e == NULL || strncmp(e->name, "a:", 2) != 0 ||
        strstr(e->name, "Clr") == NULL ||
        strcmp(e->name, "a:clrChange") == 0) {
        return 0;
    }
    if (tiku_xml_is(e, "a:srgbClr")) {
        hex(tiku_xml_attr(e, "val"), &c);
    } else if (tiku_xml_is(e, "a:schemeClr")) {
        scheme_color(ctx, tiku_xml_attr(e, "val"), &c);
    } else if (tiku_xml_is(e, "a:sysClr")) {
        hex(tiku_xml_attr(e, "lastClr") ? tiku_xml_attr(e, "lastClr") :
            strcmp(tiku_xml_attr(e, "val") ? tiku_xml_attr(e, "val") : "",
                   "window") == 0 ? "FFFFFF" : "000000", &c);
    } else if (tiku_xml_is(e, "a:prstClr")) {
        preset(tiku_xml_attr(e, "val"), &c);
    } else if (tiku_xml_is(e, "a:hslClr")) {
        from_hsl(pct(e, "hue", 0) * 100000.0 / 60000.0, pct(e, "sat", 0),
                 pct(e, "lum", 0), &c);
    } else if (tiku_xml_is(e, "a:scrgbClr")) {
        c.r = srgb(pct(e, "r", 0));
        c.g = srgb(pct(e, "g", 0));
        c.b = srgb(pct(e, "b", 0));
    }
    modify(e, &c);
    k = c.a < 0 ? 0 : c.a > 1 ? 1 : c.a;
    return (uint32_t)(k * 255 + 0.5) << 24 |
           (uint32_t)(fmin(fmax(c.r, 0), 1) * 255 + 0.5) << 16 |
           (uint32_t)(fmin(fmax(c.g, 0), 1) * 255 + 0.5) << 8 |
           (uint32_t)(fmin(fmax(c.b, 0), 1) * 255 + 0.5);
}

/* ------------------------------------------------------------- theme  */

uint32_t
dml_lum(uint32_t argb, double mod, double off)
{
    rgba_t c;
    double h, s, l;

    c.r = (double)(argb >> 16 & 0xff) / 255;
    c.g = (double)(argb >> 8 & 0xff) / 255;
    c.b = (double)(argb & 0xff) / 255;
    to_hsl(&c, &h, &s, &l);
    from_hsl(h, s, l * mod + off, &c);
    return (argb & 0xff000000u) |
           (uint32_t)(fmin(fmax(c.r, 0), 1) * 255 + 0.5) << 16 |
           (uint32_t)(fmin(fmax(c.g, 0), 1) * 255 + 0.5) << 8 |
           (uint32_t)(fmin(fmax(c.b, 0), 1) * 255 + 0.5);
}

void
dml_map_read(dml_ctx_t *c, const tiku_xml_node_t *map)
{
    static const unsigned char usual[12] = {DML_LT1, DML_DK1, DML_LT2,
        DML_DK2, DML_ACCENT1, DML_ACCENT2, DML_ACCENT3, DML_ACCENT4,
        DML_ACCENT5, DML_ACCENT6, DML_HLINK, DML_FOLHLINK};
    int i, k;

    memcpy(c->map, usual, sizeof usual);
    for (i = 0; map != NULL && i < 12; i++) {
        const char *v = tiku_xml_attr(map, mapped[i]);

        for (k = 0; v != NULL && k < DML_COLORS; k++) {
            if (strcmp(v, scheme[k]) == 0) {
                c->map[i] = (unsigned char)k;
            }
        }
    }
}

/** @brief A font scheme's face of kind @p kind ("a:latin", "a:ea") into
 *         @p out. */
static void
scheme_font(const tiku_xml_node_t *f, const char *kind, char *out,
            size_t max)
{
    const char *v = tiku_xml_attr(tiku_xml_child(f, kind), "typeface");

    if (v != NULL) {
        snprintf(out, max, "%s", v);
    }
}

/** @brief The colour, font and format schemes under @p elems (a theme's
 *         a:themeElements, or an override's root) over @p t: each one
 *         there replaces @p t's own. */
static void
theme_read(const tiku_xml_node_t *elems, dml_theme_t *t)
{
    const tiku_xml_node_t *cs = tiku_xml_child(elems, "a:clrScheme");
    const tiku_xml_node_t *fs = tiku_xml_child(elems, "a:fontScheme");
    const tiku_xml_node_t *fm = tiku_xml_child(elems, "a:fmtScheme");
    dml_ctx_t c;
    int i;

    memset(&c, 0, sizeof c);
    for (i = 0; cs != NULL && i < DML_COLORS; i++) {
        const tiku_xml_node_t *slot = tiku_xml_child(cs, NULL);

        while (slot != NULL && strcmp(slot->name + 2, scheme[i]) != 0) {
            slot = tiku_xml_next(slot, NULL);
        }
        if (slot != NULL) {
            t->clr[i] = dml_color(&c, slot) | 0xff000000u;
        }
    }
    if (fs != NULL) {
        scheme_font(tiku_xml_child(fs, "a:majorFont"), "a:latin", t->major,
                    sizeof t->major);
        scheme_font(tiku_xml_child(fs, "a:minorFont"), "a:latin", t->minor,
                    sizeof t->minor);
        scheme_font(tiku_xml_child(fs, "a:majorFont"), "a:ea", t->major_ea,
                    sizeof t->major_ea);
        scheme_font(tiku_xml_child(fs, "a:minorFont"), "a:ea", t->minor_ea,
                    sizeof t->minor_ea);
    }
    if (fm != NULL) {
        t->fills = tiku_xml_child(fm, "a:fillStyleLst");
        t->lines = tiku_xml_child(fm, "a:lnStyleLst");
        t->bgfills = tiku_xml_child(fm, "a:bgFillStyleLst");
    }
}

int
dml_theme_load(of_pkg_t *pkg, const char *part, dml_theme_t *t)
{
    static const uint32_t office[DML_COLORS] = {   /* Office's own theme */
        0xff000000u, 0xffffffffu, 0xff44546au, 0xffe7e6e6u, 0xff4472c4u,
        0xffed7d31u, 0xffa5a5a5u, 0xffffc000u, 0xff5b9bd5u, 0xff70ad47u,
        0xff0563c1u, 0xff954f72u};

    memset(t, 0, sizeof *t);
    snprintf(t->major, sizeof t->major, "Calibri Light");
    snprintf(t->minor, sizeof t->minor, "Calibri");
    memcpy(t->clr, office, sizeof office);
    if (part == NULL || (t->xml = of_pkg_xml(pkg, part)) == NULL) {
        return -1;
    }
    t->part = strdup(part);
    theme_read(tiku_xml_child(tiku_xml_root(t->xml), "a:themeElements"), t);
    return 0;
}

int
dml_theme_override(of_pkg_t *pkg, const char *part, const dml_theme_t *base,
                   dml_theme_t *t)
{
    *t = *base;
    t->xml = NULL;
    t->part = NULL;
    if (part == NULL || (t->xml = of_pkg_xml(pkg, part)) == NULL) {
        return -1;
    }
    t->part = strdup(part);
    theme_read(tiku_xml_root(t->xml), t);
    return 0;
}

void
dml_theme_free(dml_theme_t *t)
{
    tiku_xml_free(t->xml);
    free(t->part);
    memset(t, 0, sizeof *t);
}

const char *
dml_font(const dml_ctx_t *c, const char *name)
{
    const dml_theme_t *t = c->theme;

    if (name == NULL || name[0] != '+' || t == NULL) {
        return name;
    }
    if (strncmp(name, "+mj-lt", 6) == 0) {
        return t->major;
    }
    if (strncmp(name, "+mn-lt", 6) == 0) {
        return t->minor;
    }
    if (strncmp(name, "+mj-ea", 6) == 0) {
        return t->major_ea[0] ? t->major_ea : NULL;
    }
    if (strncmp(name, "+mn-ea", 6) == 0) {
        return t->minor_ea[0] ? t->minor_ea : NULL;
    }
    return NULL;
}
