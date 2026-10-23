/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_pptx_text.c - a shape's text frame, set.
 *
 * A paragraph's properties at its level come from, in turn: the deck's
 * default text style, the master's title, body or other style, the
 * master's and the layout's placeholder, the shape's own list style, and
 * the paragraph itself.  The frame's insets, anchor, wrap and autofit
 * come the same way.  Paragraphs are then set by the same flow as a
 * document's, in the frame's box, and drawn through the shape's transform.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_math.h"
#include "tiku_office_pptx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define EMU    12700.0f             /* EMUs to a point                   */
#define LEVELS 9                    /* a list style's levels             */

enum {                              /* which run properties are set      */
    XR_SIZE = 1u << 0, XR_BOLD = 1u << 1, XR_ITALIC = 1u << 2,
    XR_UNDER = 1u << 3, XR_STRIKE = 1u << 4, XR_BASE = 1u << 5,
    XR_CAPS = 1u << 6, XR_SPACING = 1u << 7, XR_COLOR = 1u << 8,
    XR_LATIN = 1u << 9, XR_EA = 1u << 10, XR_BACK = 1u << 11
};

/** @brief Run properties, each only where @c set says so. */
typedef struct {
    unsigned      set;              /* XR_*                              */
    float         size, base, spc;  /* points; baseline percent; points  */
    unsigned char bold, italic, under, strike, caps;
    uint32_t      color, back;      /* ARGB                              */
    char          latin[64], ea[64];  /* faces, maybe "+mn-lt"           */
} xr_t;

enum {                              /* which paragraph properties are set */
    XP_ALIGN = 1u << 0, XP_MARL = 1u << 1, XP_INDENT = 1u << 2,
    XP_LINE = 1u << 3, XP_BEFORE = 1u << 4, XP_AFTER = 1u << 5,
    XP_BULLET = 1u << 6, XP_BUFONT = 1u << 7, XP_BUCOLOR = 1u << 8,
    XP_BUSIZE = 1u << 9, XP_TABSZ = 1u << 10
};

/** @brief Paragraph properties, each only where @c set says so. */
typedef struct {
    unsigned      set;              /* XP_*                              */
    unsigned char align;            /* OF_*                              */
    float         marl, indent;     /* points                            */
    float         line;             /* points, or a multiple when < 0    */
    float         before, after;    /* points, or percent of size < 0    */
    int           bullet;           /* 0 none, 1 a character, 2 numbers  */
    char          buchar[16];       /* the character, UTF-8              */
    char          autonum[24];      /* "arabicPeriod" ...                */
    int           start;            /* numbers begin at                  */
    char          bufont[64];       /* the bullet's face                 */
    uint32_t      bucolor;
    float         busize;           /* of the text's size                */
    float         tabsz;            /* default stops' interval           */
    xr_t          r;                /* its a:defRPr                      */
} xp_t;

/** @brief A frame's properties: insets, anchor, wrap, autofit, turn. */
typedef struct {
    float ins[4];                   /* left, top, right, bottom          */
    int   anchor;                   /* 0 top, 1 middle, 2 bottom         */
    int   nowrap;                   /* one line a paragraph, never broken */
    int   vert;                     /* 0 across, 90 or 270 turned        */
    float scale, lnred;             /* autofit's font scale, line cut    */
} xb_t;

/* ------------------------------------------------------------ reading  */

/** @brief An attribute as a number, or @p dflt. */
static double
num(const tiku_xml_node_t *e, const char *name, double dflt)
{
    const char *v = tiku_xml_attr(e, name);

    return v != NULL ? atof(v) : dflt;
}

/** @brief Whether attribute @p name is "1" or "true". */
static int
flag(const tiku_xml_node_t *e, const char *name)
{
    const char *v = tiku_xml_attr(e, name);

    return v != NULL && (v[0] == '1' || v[0] == 't');
}

/** @brief An a:rPr, a:defRPr or a:endParaRPr read over @p r. */
static void
read_r(const px_t *x, const tiku_xml_node_t *e, xr_t *r)
{
    const char *u = tiku_xml_attr(e, "u"), *s = tiku_xml_attr(e, "strike");
    const char *cap = tiku_xml_attr(e, "cap"), *face;
    const tiku_xml_node_t *k;

    if (e == NULL) {
        return;
    }
    if (tiku_xml_attr(e, "sz")) r->size = (float)num(e, "sz", 1800) / 100,
                                r->set |= XR_SIZE;
    if (tiku_xml_attr(e, "b")) r->bold = (unsigned char)flag(e, "b"),
                               r->set |= XR_BOLD;
    if (tiku_xml_attr(e, "i")) r->italic = (unsigned char)flag(e, "i"),
                               r->set |= XR_ITALIC;
    if (u) r->under = (unsigned char)(strcmp(u, "none") == 0 ? OF_UL_NONE :
                      strncmp(u, "dbl", 3) == 0 ? OF_UL_DOUBLE : strncmp(u,
                      "heavy", 5) == 0 ? OF_UL_THICK : strstr(u, "otted") ?
                      OF_UL_DOTTED : strstr(u, "ash") ? OF_UL_DASHED
                                                      : OF_UL_SINGLE),
           r->set |= XR_UNDER;
    if (s) r->strike = (unsigned char)(strcmp(s, "sngStrike") == 0 ? 1 :
                                       strcmp(s, "dblStrike") == 0 ? 2 : 0),
           r->set |= XR_STRIKE;
    if (tiku_xml_attr(e, "baseline")) r->base = (float)num(e, "baseline", 0)
                                                / 1000, r->set |= XR_BASE;
    if (cap) r->caps = (unsigned char)(strcmp(cap, "all") == 0 ? 2 :
                                       strcmp(cap, "small") == 0 ? 1 : 0),
             r->set |= XR_CAPS;
    if (tiku_xml_attr(e, "spc")) r->spc = (float)num(e, "spc", 0) / 100,
                                 r->set |= XR_SPACING;
    for (k = tiku_xml_child(e, NULL); k != NULL; k = tiku_xml_next(k, NULL)) {
        if (tiku_xml_is(k, "a:solidFill")) {
            r->color = dml_color(&x->dml, k);
            r->set |= XR_COLOR;
        } else if (tiku_xml_is(k, "a:highlight")) {
            r->back = dml_color(&x->dml, k);
            r->set |= XR_BACK;
        } else if (tiku_xml_is(k, "a:latin") &&
                   (face = tiku_xml_attr(k, "typeface")) != NULL) {
            snprintf(r->latin, sizeof r->latin, "%s", face);
            r->set |= XR_LATIN;
        } else if (tiku_xml_is(k, "a:ea") &&
                   (face = tiku_xml_attr(k, "typeface")) != NULL) {
            snprintf(r->ea, sizeof r->ea, "%s", face);
            r->set |= XR_EA;
        }
    }
}

/** @brief @p src's set properties laid over @p dst. */
static void
over_r(xr_t *dst, const xr_t *src)
{
    unsigned s = src->set;

    if (s & XR_SIZE) dst->size = src->size;
    if (s & XR_BOLD) dst->bold = src->bold;
    if (s & XR_ITALIC) dst->italic = src->italic;
    if (s & XR_UNDER) dst->under = src->under;
    if (s & XR_STRIKE) dst->strike = src->strike;
    if (s & XR_BASE) dst->base = src->base;
    if (s & XR_CAPS) dst->caps = src->caps;
    if (s & XR_SPACING) dst->spc = src->spc;
    if (s & XR_COLOR) dst->color = src->color;
    if (s & XR_BACK) dst->back = src->back;
    if (s & XR_LATIN) memcpy(dst->latin, src->latin, sizeof dst->latin);
    if (s & XR_EA) memcpy(dst->ea, src->ea, sizeof dst->ea);
    dst->set |= s;
}

/** @brief A spacing element (a:lnSpc, a:spcBef ...): points, or a percent
 *         as a negative fraction. */
static float
spacing(const tiku_xml_node_t *e)
{
    const tiku_xml_node_t *pct = tiku_xml_child(e, "a:spcPct");

    return pct != NULL ? -(float)num(pct, "val", 100000) / 100000
                       : (float)num(tiku_xml_child(e, "a:spcPts"), "val", 0)
                         / 100;
}

/** @brief A paragraph's bullet children read into @p p. */
static void
read_bullet(const px_t *x, const tiku_xml_node_t *e, xp_t *p)
{
    const tiku_xml_node_t *k;

    for (k = tiku_xml_child(e, NULL); k != NULL; k = tiku_xml_next(k, NULL)) {
        if (tiku_xml_is(k, "a:buNone")) {
            p->bullet = 0;
            p->set |= XP_BULLET;
        } else if (tiku_xml_is(k, "a:buChar")) {
            p->bullet = 1;
            snprintf(p->buchar, sizeof p->buchar, "%s",
                     tiku_xml_attr(k, "char") ? tiku_xml_attr(k, "char")
                                              : "\xe2\x80\xa2");
            p->set |= XP_BULLET;
        } else if (tiku_xml_is(k, "a:buAutoNum")) {
            p->bullet = 2;
            snprintf(p->autonum, sizeof p->autonum, "%s",
                     tiku_xml_attr(k, "type") ? tiku_xml_attr(k, "type")
                                              : "arabicPeriod");
            p->start = (int)num(k, "startAt", 1);
            p->start = p->start < 1 ? 1 : p->start > 32767 ? 32767
                                                           : p->start;
            p->set |= XP_BULLET;
        } else if (tiku_xml_is(k, "a:buFont")) {
            snprintf(p->bufont, sizeof p->bufont, "%s",
                     tiku_xml_attr(k, "typeface") ?
                     tiku_xml_attr(k, "typeface") : "");
            p->set |= XP_BUFONT;
        } else if (tiku_xml_is(k, "a:buClr")) {
            p->bucolor = dml_color(&x->dml, k);
            p->set |= XP_BUCOLOR;
        } else if (tiku_xml_is(k, "a:buSzPct")) {
            p->busize = (float)num(k, "val", 100000) / 100000;
            p->set |= XP_BUSIZE;
        }
    }
}

/** @brief An a:pPr or a:lvlNpPr read over @p p. */
static void
read_p(const px_t *x, const tiku_xml_node_t *e, xp_t *p)
{
    const char *al = tiku_xml_attr(e, "algn");
    const tiku_xml_node_t *k;

    if (e == NULL) {
        return;
    }
    if (al) p->align = (unsigned char)(strcmp(al, "ctr") == 0 ? OF_CENTER :
                       strcmp(al, "r") == 0 ? OF_RIGHT : strcmp(al, "l") == 0
                       ? OF_LEFT : OF_JUSTIFY), p->set |= XP_ALIGN;
    if (tiku_xml_attr(e, "marL")) p->marl = (float)num(e, "marL", 0) / EMU,
                                  p->set |= XP_MARL;
    if (tiku_xml_attr(e, "indent")) p->indent = (float)num(e, "indent", 0) /
                                                EMU, p->set |= XP_INDENT;
    if (tiku_xml_attr(e, "defTabSz")) p->tabsz = (float)num(e, "defTabSz",
                                                            914400) / EMU,
                                      p->set |= XP_TABSZ;
    if ((k = tiku_xml_child(e, "a:lnSpc")) != NULL) {
        p->line = spacing(k);
        p->set |= XP_LINE;
    }
    if ((k = tiku_xml_child(e, "a:spcBef")) != NULL) {
        p->before = spacing(k);
        p->set |= XP_BEFORE;
    }
    if ((k = tiku_xml_child(e, "a:spcAft")) != NULL) {
        p->after = spacing(k);
        p->set |= XP_AFTER;
    }
    read_bullet(x, e, p);
    read_r(x, tiku_xml_child(e, "a:defRPr"), &p->r);
}

/** @brief @p src's set properties laid over @p dst. */
static void
over_p(xp_t *dst, const xp_t *src)
{
    unsigned s = src->set;

    if (s & XP_ALIGN) dst->align = src->align;
    if (s & XP_MARL) dst->marl = src->marl;
    if (s & XP_INDENT) dst->indent = src->indent;
    if (s & XP_LINE) dst->line = src->line;
    if (s & XP_BEFORE) dst->before = src->before;
    if (s & XP_AFTER) dst->after = src->after;
    if (s & XP_BULLET) {
        dst->bullet = src->bullet;
        memcpy(dst->buchar, src->buchar, sizeof dst->buchar);
        memcpy(dst->autonum, src->autonum, sizeof dst->autonum);
        dst->start = src->start;
    }
    if (s & XP_BUFONT) memcpy(dst->bufont, src->bufont, sizeof dst->bufont);
    if (s & XP_BUCOLOR) dst->bucolor = src->bucolor;
    if (s & XP_BUSIZE) dst->busize = src->busize;
    if (s & XP_TABSZ) dst->tabsz = src->tabsz;
    dst->set |= s;
    over_r(&dst->r, &src->r);
}

/** @brief A list style's nine levels (a:lstStyle, a titleStyle ...) laid
 *         over @p lv. */
static void
levels_over(const px_t *x, const tiku_xml_node_t *list, xp_t *lv)
{
    char name[16];
    int i;

    for (i = 0; list != NULL && i < LEVELS; i++) {
        xp_t p;

        memset(&p, 0, sizeof p);
        snprintf(name, sizeof name, "a:lvl%dpPr", i + 1);
        read_p(x, tiku_xml_child(list, name), &p);
        over_p(&lv[i], &p);
    }
}

/** @brief A frame's a:bodyPr laid over @p b. */
static void
body_over(const tiku_xml_node_t *e, xb_t *b)
{
    static const char *const ins[4] = {"lIns", "tIns", "rIns", "bIns"};
    const char *v;
    int k;

    if (e == NULL) {
        return;
    }
    for (k = 0; k < 4; k++) {
        if (tiku_xml_attr(e, ins[k]) != NULL) {
            b->ins[k] = (float)num(e, ins[k], 0) / EMU;
        }
    }
    if ((v = tiku_xml_attr(e, "anchor")) != NULL) {
        b->anchor = strcmp(v, "ctr") == 0 ? 1 : strcmp(v, "b") == 0 ? 2 : 0;
    }
    if ((v = tiku_xml_attr(e, "wrap")) != NULL) {
        b->nowrap = strcmp(v, "none") == 0;
    }
    if ((v = tiku_xml_attr(e, "vert")) != NULL) {
        b->vert = strcmp(v, "horz") == 0 ? 0 : strcmp(v, "vert270") == 0
                  ? 270 : 90;
    }
    if (tiku_xml_child(e, "a:normAutofit") != NULL) {
        b->scale = (float)num(tiku_xml_child(e, "a:normAutofit"),
                              "fontScale", 100000) / 100000;
        b->lnred = (float)num(tiku_xml_child(e, "a:normAutofit"),
                              "lnSpcReduction", 0) / 100000;
    }
}

/* ----------------------------------------------------------- the chain  */

/** @brief The master's text style for placeholder @p ph: title, body, or
 *         the others'; NULL for a shape that is not one. */
static const tiku_xml_node_t *
master_style(const px_t *x, const tiku_xml_node_t *ph)
{
    const char *t = tiku_xml_attr(ph, "type");

    if (ph == NULL) {
        return NULL;
    }
    if (t != NULL && (strcmp(t, "title") == 0 || strcmp(t, "ctrTitle") == 0))
        return tiku_xml_child(x->styles, "p:titleStyle");
    if (t == NULL || strcmp(t, "body") == 0 || strcmp(t, "subTitle") == 0 ||
        strcmp(t, "obj") == 0) {
        return tiku_xml_child(x->styles, "p:bodyStyle");
    }
    return tiku_xml_child(x->styles, "p:otherStyle");
}

/** @brief Shape @p sp's child @p local (txBody, style) in whichever
 *         namespace holds it: a slide's p:, a saved diagram's dsp:. */
static const tiku_xml_node_t *
own(const tiku_xml_node_t *sp, const char *local)
{
    char name[32];
    const tiku_xml_node_t *k;

    snprintf(name, sizeof name, "p:%s", local);
    if ((k = tiku_xml_child(sp, name)) != NULL) {
        return k;
    }
    snprintf(name, sizeof name, "dsp:%s", local);
    return tiku_xml_child(sp, name);
}

/** @brief The levels and frame a shape's text has: each source in turn,
 *         from the deck's defaults to the shape itself. */
static void
chain(const px_t *x, const tiku_xml_node_t *sp, xp_t *lv, xb_t *b)
{
    const tiku_xml_node_t *ph = px_ph(sp);
    const tiku_xml_node_t *lp = ph ? px_placeholder(x->layout, ph, 0)
                                   : NULL;
    const tiku_xml_node_t *mp = ph ? px_placeholder(x->master, lp ? px_ph(lp)
                                                    : ph, 1) : NULL;
    const tiku_xml_node_t *body = own(sp, "txBody");

    memset(lv, 0, sizeof(xp_t) * LEVELS);
    memset(b, 0, sizeof *b);
    b->ins[0] = b->ins[2] = 7.2f;
    b->ins[1] = b->ins[3] = 3.6f;
    b->scale = 1;
    levels_over(x, x->deftext, lv);
    levels_over(x, master_style(x, ph), lv);
    levels_over(x, tiku_xml_path(mp, "p:txBody/a:lstStyle"), lv);
    levels_over(x, tiku_xml_path(lp, "p:txBody/a:lstStyle"), lv);
    levels_over(x, tiku_xml_child(body, "a:lstStyle"), lv);
    body_over(tiku_xml_path(mp, "p:txBody/a:bodyPr"), b);
    body_over(tiku_xml_path(lp, "p:txBody/a:bodyPr"), b);
    body_over(tiku_xml_child(body, "a:bodyPr"), b);
}

/* -------------------------------------------------------------- looks  */

/** @brief Run properties made a look: the theme's faces for "+mn-lt" and
 *         the like, the style reference's colour when the text names none,
 *         the frame's autofit scale. */
static const of_style_t *
look(const px_t *x, const xr_t *r, uint32_t fallback, float scale)
{
    of_style_t st;
    const char *latin = r->set & XR_LATIN ? dml_font(&x->dml, r->latin)
                                          : NULL;

    of_style_default(&st);
    st.font = latin ? latin : x->theme.minor[0] ? x->theme.minor : "Calibri";
    st.font_ea = r->set & XR_EA ? dml_font(&x->dml, r->ea) : NULL;
    st.size = (r->set & XR_SIZE ? r->size : 18) * scale;
    st.size = st.size < 1 ? 1 : st.size;
    st.bold = r->bold;
    st.italic = r->italic;
    st.underline = r->under;
    st.strike = r->strike;
    st.caps = r->caps == 2;
    st.smallcaps = r->caps == 1;
    st.spacing = r->spc;
    st.color = r->set & XR_COLOR ? r->color : fallback;
    st.back = r->back;
    st.vert = (signed char)(r->base > 0 ? 1 : r->base < 0 ? -1 : 0);
    return of_style(x->d, &st);
}

/** @brief An automatic number's text: 1. a) (iv) and the rest. */
static void
auto_number(const char *type, int n, char *out, size_t max)
{
    int fmt = strncmp(type, "roman", 5) == 0 ? (strstr(type, "Uc")
              ? OF_NUM_UROMAN : OF_NUM_LROMAN) : strncmp(type, "alpha", 5)
              == 0 ? (strstr(type, "Uc") ? OF_NUM_ULETTER : OF_NUM_LLETTER)
              : OF_NUM_DECIMAL;
    char v[16];

    of_number(n, fmt, v, sizeof v);
    snprintf(out, max, "%s%s%s", strstr(type, "ParenBoth") ? "(" : "", v,
             strstr(type, "Period") ? "." : strstr(type, "Paren") ? ")" :
             strstr(type, "Minus") ? " -" : "");
}

/** @brief A bullet's character as Unicode, its Symbol or Wingdings code
 *         mapped, into @p out. */
static void
bullet_text(const char *s, const char *font, char *out, size_t max)
{
    const unsigned char *p = (const unsigned char *)s;
    uint32_t cp = *p;

    if (cp >= 0xe0 && p[1] && p[2]) {
        cp = (cp & 0x0f) << 12 | (uint32_t)(p[1] & 0x3f) << 6 | (p[2] & 0x3f);
    } else if (cp >= 0xc0 && p[1]) {
        cp = (cp & 0x1f) << 6 | (p[1] & 0x3f);
    }
    cp = font != NULL && font[0] ? of_symbol(font, cp) : cp;
    cp = cp >= 0xe000 && cp < 0xf900 ? 0x2022 : cp;
    if (cp < 0x80) {
        snprintf(out, max, "%c", (int)cp);
    } else if (cp < 0x800) {
        snprintf(out, max, "%c%c", 0xc0 | (int)(cp >> 6),
                 0x80 | (int)(cp & 0x3f));
    } else {
        snprintf(out, max, "%c%c%c", 0xe0 | (int)(cp >> 12),
                 0x80 | (int)(cp >> 6 & 0x3f), 0x80 | (int)(cp & 0x3f));
    }
}

/* ---------------------------------------------------------- paragraphs  */

/** @brief What building a frame's paragraphs keeps between them: the
 *         automatic numbers' counts, the look text falls back to. */
typedef struct {
    int      count[LEVELS];         /* each level's number so far        */
    int      numbered[LEVELS];      /* the last at the level had one     */
    uint32_t color;                 /* for text that names no colour     */
    float    scale, lnred;          /* autofit                           */
} xs_t;

/** @brief A paragraph's layout from its merged properties. */
static void
para_layout(const xp_t *p, float size, const xs_t *s, of_para_t *out)
{
    of_pstyle_default(&out->ps);
    out->ps.align = p->align;
    out->ps.left = p->marl;
    out->ps.first = p->indent;
    out->ps.widows = 0;
    if (p->set & XP_LINE) {
        out->ps.line_rule = p->line < 0 ? OF_LINE_AUTO : OF_LINE_EXACT;
        out->ps.line = p->line < 0 ? -p->line - s->lnred : p->line *
                       s->scale;
        out->ps.line = out->ps.line > 0.2f ? out->ps.line : 0.2f;
    }
    out->ps.before = p->before < 0 ? -p->before * size : p->before *
                     s->scale;
    out->ps.after = p->after < 0 ? -p->after * size : p->after * s->scale;
}

/** @brief The paragraph's bullet or number made its label. */
static void
para_label(const px_t *x, const xp_t *p, int level, float size, xs_t *s,
           of_para_t *out)
{
    char text[32];
    xr_t r = p->r;
    int k;

    if (p->bullet == 0) {
        s->numbered[level] = 0;
        return;
    }
    if (p->bullet == 2) {
        s->count[level] = s->numbered[level] ? s->count[level] + 1 : p->start;
        s->numbered[level] = 1;
        auto_number(p->autonum, s->count[level], text, sizeof text);
    } else {
        bullet_text(p->buchar, p->bufont, text, sizeof text);
    }
    for (k = level + 1; k < LEVELS; k++) {
        s->numbered[k] = 0;
    }
    r.size = size * (p->set & XP_BUSIZE ? p->busize : 1) / s->scale;
    r.set |= XR_SIZE;
    if ((p->set & XP_BUFONT) && p->bullet == 2) {
        snprintf(r.latin, sizeof r.latin, "%s", p->bufont);
        r.set |= XR_LATIN;
    }
    if (p->set & XP_BUCOLOR) {
        r.color = p->bucolor;
        r.set |= XR_COLOR;
    }
    out->label = of_strdup(&x->d->arena, text);
    out->label_st = look(x, &r, s->color, s->scale);
    out->label_follow = 0;
}

/** @brief What an equation's runs' looks are found with. */
typedef struct {
    const px_t *x;
    const xp_t *p;                  /* the paragraph's properties        */
    const xs_t *s;                  /* the story's colour and scale      */
} px_tint_t;

/** @brief Math run @p r's look: paragraph @p p's, with its own a:rPr.
 *         NULL out of memory. */
static const of_style_t *
math_look(const px_t *x, const xp_t *p, const xs_t *s,
          const tiku_xml_node_t *r)
{
    xr_t own = p->r;

    read_r(x, tiku_xml_child(r, "a:rPr"), &own);
    return look(x, &own, s->color, s->scale);
}

/** @brief Math run @p r's colour: of_omml()'s tint for slides. */
static uint32_t
math_tint(void *ctx, const tiku_xml_node_t *r)
{
    const px_tint_t *t = ctx;
    const of_style_t *st = math_look(t->x, t->p, t->s, r);

    return st != NULL ? st->color : 0;
}

/** @brief An a14:m's equations as runs of @p runs: an m:oMathPara's set
 *         apart, each on a line of its own; a lone m:oMath in the line;
 *         each sized as its first run is, in the text's colour.
 *  @return 0, or -1 out of memory. */
static int
add_math(const px_t *x, const tiku_xml_node_t *k, const xp_t *p, xs_t *s,
         of_vec_t *runs)
{
    const tiku_xml_node_t *para = tiku_xml_child(k, "m:oMathPara");
    const tiku_xml_node_t *e = tiku_xml_child(para ? para : k, "m:oMath");
    px_tint_t t = { x, p, s };
    const of_style_t *st;
    of_run_t *run;
    of_math_t *m;
    int n = 0;

    for (; e != NULL; e = tiku_xml_next(e, "m:oMath")) {
        st = of_omml_first(e) != NULL ? math_look(x, p, s, of_omml_first(e))
                                      : look(x, &p->r, s->color, s->scale);
        if (st == NULL || of_omml(x->d, e, para != NULL, st->size,
                                  s->color, math_tint, &t, &m) != 0) {
            return -1;
        }
        if (m != NULL && n++ > 0 && para != NULL) {
            if ((run = of_vec_push(runs)) == NULL) {
                return -1;
            }
            run->kind = OR_BREAK;   /* each on a line of its own */
            run->st = st;
        }
        if (m != NULL && (run = of_vec_push(runs)) == NULL) {
            return -1;
        }
        if (m != NULL) {
            run->kind = OR_MATH;
            run->st = st;
            run->math = m;
        }
    }
    return 0;
}

/** @brief A run of text, a break, a field or equations, as runs of
 *         @p runs; the choice of alternative content made. */
static int
add_run(const px_t *x, const tiku_xml_node_t *k, const xp_t *p, xs_t *s,
        of_vec_t *runs)
{
    const tiku_xml_node_t *c;
    xr_t r = p->r;
    of_run_t *run;
    char text[4096], num[16];

    if (tiku_xml_is(k, "a14:m")) {
        return add_math(x, k, p, s, runs);
    }
    for (c = tiku_xml_is(k, "mc:AlternateContent") ? tiku_xml_child(
             of_alternate(k), NULL) : NULL; c; c = tiku_xml_next(c, NULL)) {
        if (add_run(x, c, p, s, runs) != 0) {
            return -1;
        }
    }
    if (!tiku_xml_is(k, "a:r") && !tiku_xml_is(k, "a:br") &&
        !tiku_xml_is(k, "a:fld")) {
        return 0;
    }
    read_r(x, tiku_xml_child(k, "a:rPr"), &r);
    if ((run = of_vec_push(runs)) == NULL) {
        return -1;
    }
    run->st = look(x, &r, s->color, s->scale);
    if (tiku_xml_is(k, "a:br")) {
        run->kind = OR_BREAK;
        return run->st != NULL ? 0 : -1;
    }
    run->kind = OR_TEXT;
    (void)tiku_xml_text(tiku_xml_child(k, "a:t"), text, sizeof text);
    if (tiku_xml_is(k, "a:fld") && tiku_xml_attr(k, "type") != NULL &&
        strcmp(tiku_xml_attr(k, "type"), "slidenum") == 0) {
        snprintf(num, sizeof num, "%d", x->number);
        run->text = of_strdup(&x->d->arena, num);
    } else {
        run->text = of_strdup(&x->d->arena, text);
    }
    return run->st != NULL && run->text != NULL ? 0 : -1;
}

/** @brief One a:p as a paragraph: its merged properties, its label, its
 *         runs, and its end's look for when it is empty.  NULL out of
 *         memory. */
static of_para_t *
paragraph(const px_t *x, const tiku_xml_node_t *a, const xp_t *lv, xs_t *s)
{
    const tiku_xml_node_t *ppr = tiku_xml_child(a, "a:pPr"), *k;
    int level = (int)num(ppr, "lvl", 0);
    of_para_t *out = of_alloc(&x->d->arena, sizeof *out);
    xp_t p;
    xr_t first;
    of_vec_t runs;

    if (out == NULL) {
        return NULL;
    }
    level = level < 0 ? 0 : level >= LEVELS ? LEVELS - 1 : level;
    p = lv[level];
    read_p(x, ppr, &p);
    first = p.r;
    read_r(x, tiku_xml_path(a, "a:r/a:rPr"), &first);
    para_layout(&p, (first.set & XR_SIZE ? first.size : 18) * s->scale, s,
                out);
    if (tiku_xml_child(a, "a:r") != NULL || tiku_xml_child(a, "a:fld")) {
        para_label(x, &p, level, (first.set & XR_SIZE ? first.size : 18) *
                   s->scale, s, out);
    }
    of_vec_init(&runs, sizeof(of_run_t));
    for (k = tiku_xml_child(a, NULL); k != NULL; k = tiku_xml_next(k, NULL)) {
        if (add_run(x, k, &p, s, &runs) != 0) {
            of_vec_free(&runs);
            return NULL;
        }
    }
    out->run = of_vec_freeze(&runs, &x->d->arena, &out->nrun);
    of_vec_free(&runs);
    read_r(x, tiku_xml_child(a, "a:endParaRPr"), &p.r);
    out->mark = look(x, &p.r, s->color, s->scale);
    return out->mark != NULL ? out : NULL;
}

/** @brief The frame's paragraphs as a story.  NULL out of memory. */
static of_story_t *
story(const px_t *x, const tiku_xml_node_t *body, const xp_t *lv, xs_t *s)
{
    const tiku_xml_node_t *a;
    of_story_t *st = of_alloc(&x->d->arena, sizeof *st);
    of_vec_t blocks;
    of_block_t *b;

    if (st == NULL) {
        return NULL;
    }
    of_vec_init(&blocks, sizeof(of_block_t));
    for (a = tiku_xml_child(body, "a:p"); a; a = tiku_xml_next(a, "a:p")) {
        if ((b = of_vec_push(&blocks)) == NULL ||
            (b->para = paragraph(x, a, lv, s)) == NULL) {
            of_vec_free(&blocks);
            return NULL;
        }
        b->kind = OB_PARA;
    }
    st->block = of_vec_freeze(&blocks, &x->d->arena, &st->nblock);
    of_vec_free(&blocks);
    return st;
}

/* ------------------------------------------------------------- drawing  */

int
px_has_text(const tiku_xml_node_t *shape)
{
    char text[8];

    return tiku_xml_text(own(shape, "txBody"), text,
                         sizeof text) > 0;
}

/** @brief The colour text takes when it names none: the shape style's
 *         font reference, else the scheme's text colour. */
static uint32_t
text_color(px_t *x, const tiku_xml_node_t *sp)
{
    const tiku_xml_node_t *ref = tiku_xml_child(own(sp, "style"),
                                                "a:fontRef");
    uint32_t c = ref != NULL ? dml_color(&x->dml, ref) : 0;

    if (c == 0 && x->dml.theme != NULL) {
        c = x->dml.theme->clr[x->dml.map[1]];   /* tx1 */
    }
    return c ? c : 0xff000000u;
}

/** @brief Every fragment moved across by @p dx. */
static void
pg_shift(of_vec_t *frags, float dx)
{
    of_frag_t *f = frags->p;
    int i;

    for (i = 0; dx != 0 && i < frags->n; i++) {
        f[i].x += dx;
    }
}

/** @brief The laid-out fragments drawn through @p m: in the box at
 *         (@p x0, @p y0), turned for vertical text. */
static void
draw_frags(px_t *x, const of_vec_t *frags, const dml_mat_t *m, float x0,
           float y0, int vert)
{
    tiku_pdf_writer_t *w = x->dml.w;
    int i;

    tiku_pdf_writer_save(w);
    tiku_pdf_writer_transform(w, m->a, m->b, m->c, m->d, m->e, m->f);
    tiku_pdf_writer_transform(w, 1, 0, 0, 1, x0, y0);
    if (vert == 90) {
        tiku_pdf_writer_transform(w, 0, 1, -1, 0, 0, 0);
    } else if (vert == 270) {
        tiku_pdf_writer_transform(w, 0, -1, 1, 0, 0, 0);
    }
    for (i = 0; i < frags->n; i++) {
        of_draw_frag(x->d, x->dml.fs, &((of_frag_t *)frags->p)[i], 0, 0, w);
    }
    tiku_pdf_writer_restore(w);
}

/** @brief A story set as wide as the frame (as tall, for vertical text)
 *         and, when @p draw, drawn in it: moved as the frame's anchor says,
 *         and back into its box when it does not wrap.  @return its height,
 *         or -1. */
static float
set_story(px_t *x, of_story_t *st, const xb_t *b, const float *box,
          const dml_mat_t *m, int draw)
{
    float bw = box[2] - box[0] - b->ins[0] - b->ins[2];
    float bh = box[3] - box[1] - b->ins[1] - b->ins[3];
    float lw = b->vert ? bh : bw, h, room, y0, dx = 0;
    of_pageno_t no = { x->number, 0, OF_NUM_DECIMAL };
    of_vec_t frags;

    of_vec_init(&frags, sizeof(of_frag_t));
    h = of_layout_story(x->d, x->dml.fs, st, b->nowrap ? 4000 : (lw > 1 ? lw
                        : 1), &no, &frags, &x->d->arena);
    if (h >= 0 && draw) {
        if (b->nowrap && st->nblock > 0) {
            int al = st->block[0].para->ps.align;

            dx = al == OF_CENTER ? (lw - 4000) / 2 : al == OF_RIGHT
                 ? lw - 4000 : 0;
        }
        room = (b->vert ? bw : bh) - h;
        y0 = b->anchor == 1 ? room / 2 : b->anchor == 2 ? room : 0;
        pg_shift(&frags, dx);
        draw_frags(x, &frags, m, box[0] + b->ins[0] + (b->vert == 90 ? bw -
                   y0 : b->vert == 270 ? y0 : 0), box[1] + b->ins[1] +
                   (b->vert == 270 ? bh : b->vert ? 0 : y0), b->vert);
    }
    of_vec_free(&frags);
    return h;
}

int
px_text(void *ctx, const tiku_xml_node_t *shape, const float *box,
        const dml_mat_t *m)
{
    px_t *x = ctx;
    xp_t lv[LEVELS];
    xb_t b;
    xs_t s;
    of_story_t *st;

    if (!px_has_text(shape)) {
        return 0;
    }
    chain(x, shape, lv, &b);
    memset(&s, 0, sizeof s);
    s.color = text_color(x, shape);
    s.scale = b.scale;
    s.lnred = b.lnred;
    x->d->tab = lv[0].set & XP_TABSZ && lv[0].tabsz > 1 ? lv[0].tabsz : 72;
    if ((st = story(x, own(shape, "txBody"), lv, &s)) == NULL ||
        of_math_prepare(x->d, x->dml.fs) != 0) {
        return -1;
    }
    return set_story(x, st, &b, box, m, 1) >= 0 ? 0 : -1;
}

float
px_body(px_t *x, const tiku_xml_node_t *body, const float *box,
        const float *ins, int anchor, const dml_mat_t *m, uint32_t color,
        int bold, int draw)
{
    xp_t lv[LEVELS];
    xb_t b;
    xs_t s;
    of_story_t *st;
    int i;

    memset(lv, 0, sizeof lv);
    memset(&b, 0, sizeof b);
    memset(&s, 0, sizeof s);
    levels_over(x, x->deftext, lv);
    for (i = 0; bold && i < LEVELS; i++) {
        lv[i].r.bold = 1;
        lv[i].r.set |= XR_BOLD;
    }
    levels_over(x, tiku_xml_child(body, "a:lstStyle"), lv);
    memcpy(b.ins, ins, sizeof b.ins);
    b.anchor = anchor;
    b.scale = s.scale = 1;
    s.color = color;
    if ((st = story(x, body, lv, &s)) == NULL ||
        of_math_prepare(x->d, x->dml.fs) != 0) {
        return -1;
    }
    return set_story(x, st, &b, box, m, draw);
}
