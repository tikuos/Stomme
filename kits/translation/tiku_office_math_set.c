/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_math_set.c - an equation's structures, each set by the
 * rule TeX and the OpenType MATH table give it: fractions about the
 * axis, scripts clear of each other, roots grown to their radicands,
 * large operators with their limits over and under or beside, fences
 * grown to what they hold, matrices and arrays lined up by column, and
 * accents, limits, bars and braces over or under their bases.  Lengths
 * are points, y down from the baseline.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_math.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GRID_MAX 64                 /* rows, and columns, a grid sets    */

/** @brief The first character of UTF-8 @p s; 0 for none. */
static uint32_t
first_cp(const char *s)
{
    const unsigned char *p = (const unsigned char *)(s != NULL ? s : "");
    uint32_t c = p[0];

    if (c < 0x80) {
        return c;
    }
    if (c < 0xe0 && (p[1] & 0xc0) == 0x80) {
        return (c & 0x1f) << 6 | (p[1] & 0x3f);
    }
    if (c < 0xf0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) {
        return (c & 0x0f) << 12 | (p[1] & 0x3f) << 6 | (p[2] & 0x3f);
    }
    if ((p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80 &&
        (p[3] & 0xc0) == 0x80) {
        return (c & 7) << 18 | (p[1] & 0x3f) << 12 | (p[2] & 0x3f) << 6 |
               (p[3] & 0x3f);
    }
    return 0xfffd;
}

/** @brief Piece @p b moved by (@p dx, @p dy), its extent about the
 *         baseline moving with it. */
static void
move(ml_t *ml, mb_t *b, float dx, float dy)
{
    ml_shift(ml, b, dx, dy);
    b->asc -= dy;
    b->desc += dy;
}

/** @brief Piece @p b moved up or down to sit centred on the axis. */
static void
on_axis(ml_t *ml, mb_t *b, const ms_t *st)
{
    move(ml, b, 0, (b->asc - b->desc) / 2 - ml_c(ml, MC_AXIS, st));
}

/** @brief Piece @p out grown to reach as high and as low as @p b. */
static void
reach(mb_t *out, const mb_t *b)
{
    out->asc = fmaxf(out->asc, b->asc);
    out->desc = fmaxf(out->desc, b->desc);
}

/** @brief Piece @p out made all that was set from item @p start on: one
 *         structure, no longer a lone glyph or a slanted letter. */
static void
whole(const ml_t *ml, mb_t *out, int start)
{
    out->from = start;
    out->to = ml->nit;
    out->glyph = 0;
    out->italic = 0;
    out->ic = 0;
    out->gid = 0;
}

/* ---------------------------------------------------------- fractions  */

/** @brief The style a fraction's part takes: text in a display (unless
 *         the fraction is @p small), else a script's; a denominator
 *         (@p den) cramped. */
static ms_t
part_style(const ml_t *ml, const ms_t *st, int den, int small)
{
    ms_t s = *st;

    if (st->level == 0 && !small) {
        s.level = 1;
        s.cramped = st->cramped || den;
        return s;
    }
    return ml_smaller(ml, st, den);
}

/** @brief A fraction written along the line: its parts, a slash between. */
static void
linear(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *out)
{
    mb_t b;

    ml_set(ml, n->kid[0], st, out);
    ml_text(ml, "/", OV_NORMAL, ml->color, OC_ORD, st, &b);
    ml_append(ml, out, &b, 0);
    ml_set(ml, n->kid[1], st, &b);
    ml_append(ml, out, &b, 0);
}

/** @brief A skewed fraction: its parts a script's size, the numerator
 *         raised and the denominator lowered either side of a slash. */
static void
skewed(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *out)
{
    ms_t s = ml_smaller(ml, st, 0);
    mb_t bar, den;
    float g;

    ml_set(ml, n->kid[0], &s, out);
    move(ml, out, 0, -ml_c(ml, MC_SUP_UP, st));
    ml_set(ml, n->kid[1], &s, &den);
    move(ml, &den, 0, ml_c(ml, MC_SUB_DOWN, st));
    ml_stretch(ml, '/', 1, out->asc + den.desc, st, &bar);
    on_axis(ml, &bar, st);
    g = (ml_c(ml, MC_SKEW_HGAP, st) - bar.w) / 2;   /* the slash centred */
    ml_append(ml, out, &bar, g);
    ml_append(ml, out, &den, g);
}

/** @brief Where a stack's or a fraction's parts sit: the numerator's
 *         baseline @p u up and the denominator's @p v down, far enough
 *         from the rule (@p rule thick; 0 for none) or from each other. */
static void
stack_shifts(ml_t *ml, const ms_t *st, const mb_t *num, const mb_t *den,
             float rule, float *u, float *v)
{
    int d = st->level == 0;
    float axis = ml_c(ml, MC_AXIS, st), gap, clear;

    if (rule <= 0) {                /* no rule: a stack */
        *u = ml_c(ml, d ? MC_STACK_TOP_UP_D : MC_STACK_TOP_UP, st);
        *v = ml_c(ml, d ? MC_STACK_BOTTOM_DOWN_D : MC_STACK_BOTTOM_DOWN,
                  st);
        gap = ml_c(ml, d ? MC_STACK_GAP_D : MC_STACK_GAP, st);
        clear = (*u - num->desc) - (den->asc - *v);
        if (clear < gap) {
            *u += (gap - clear) / 2;
            *v += (gap - clear) / 2;
        }
        return;
    }
    *u = fmaxf(ml_c(ml, d ? MC_FRAC_NUM_UP_D : MC_FRAC_NUM_UP, st),
               axis + rule / 2 + num->desc +
               ml_c(ml, d ? MC_FRAC_NUM_GAP_D : MC_FRAC_NUM_GAP, st));
    *v = fmaxf(ml_c(ml, d ? MC_FRAC_DEN_DOWN_D : MC_FRAC_DEN_DOWN, st),
               den->asc - axis + rule / 2 +
               ml_c(ml, d ? MC_FRAC_DEN_GAP_D : MC_FRAC_DEN_GAP, st));
}

void
ml_frac(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *out)
{
    ms_t ns = part_style(ml, st, 0, n->flags & OMF_SMALL);
    ms_t ds = part_style(ml, st, 1, n->flags & OMF_SMALL);
    float rule = n->flags & OMF_NOBAR ? 0 : ml_c(ml, MC_FRAC_RULE, st);
    float pad = st->size * 0.06f, u, v, w;
    int start = ml->nit;
    mb_t num, den;

    if (n->nkid < 2) {
        ml_empty(ml, out);
        return;
    }
    if (n->flags & OMF_LINEAR) {
        linear(ml, n, st, out);
    } else if (n->flags & OMF_SKEW) {
        skewed(ml, n, st, out);
    } else {
        ml_set(ml, n->kid[0], &ns, &num);
        ml_set(ml, n->kid[1], &ds, &den);
        stack_shifts(ml, st, &num, &den, rule, &u, &v);
        w = fmaxf(num.w, den.w) + 2 * pad;  /* the rule a little over */
        move(ml, &num, (w - num.w) / 2, -u);
        move(ml, &den, (w - den.w) / 2, v);
        if (rule > 0) {
            ml_rule(ml, 0, -(ml_c(ml, MC_AXIS, st) + rule / 2), w, rule);
        }
        ml_empty(ml, out);
        out->w = w;
        reach(out, &num);
        reach(out, &den);
    }
    whole(ml, out, start);
    out->lcls = out->rcls = n->flags & OMF_LINEAR ? OC_ORD : OC_INNER;
}

/* ------------------------------------------------------------ scripts  */

/** @brief How far a superscript @p sup rises (@p u) and a subscript
 *         @p sub falls (@p v), either NULL, beside base @p b: from a lone
 *         glyph's baseline, from anything else's top and bottom. */
static void
script_shifts(ml_t *ml, const ms_t *st, const mb_t *b, const mb_t *sup,
              const mb_t *sub, float *u, float *v)
{
    float d;

    *u = b->glyph ? 0 : b->asc - ml_c(ml, MC_SUP_DROP_MAX, st);
    *v = b->glyph ? 0 : b->desc + ml_c(ml, MC_SUB_DROP_MIN, st);
    if (sup != NULL) {
        *u = fmaxf(*u, ml_c(ml, st->cramped ? MC_SUP_UP_CRAMPED : MC_SUP_UP,
                            st));
        *u = fmaxf(*u, sup->desc + ml_c(ml, MC_SUP_BOTTOM_MIN, st));
    }
    if (sub != NULL) {
        *v = fmaxf(*v, ml_c(ml, MC_SUB_DOWN, st));
        *v = fmaxf(*v, sub->asc - ml_c(ml, MC_SUB_TOP_MAX, st));
    }
    if (sup != NULL && sub != NULL) {
        d = ml_c(ml, MC_SUBSUP_GAP_MIN, st) -
            ((*u - sup->desc) - (sub->asc - *v));
        if (d > 0) {                /* too close: the subscript lower */
            *v += d;
            d = ml_c(ml, MC_SUP_BOTTOM_MAX_SUB, st) - (*u - sup->desc);
            if (d > 0) {            /* then both up, the gap kept */
                *u += d;
                *v -= d;
            }
        }
    }
}

/** @brief Superscript @p sup and subscript @p sub (either NULL) put
 *         beside a base from @p x, the superscript @p ic further, raised
 *         @p u and lowered @p v; before the base (@p pre) they line up on
 *         its side.  @return the width they take. */
static float
place_scripts(ml_t *ml, mb_t *sup, mb_t *sub, float u, float v, float x,
              float ic, int pre)
{
    float w = fmaxf(sup != NULL ? sup->w + ic : 0, sub != NULL ? sub->w : 0);

    if (sup != NULL) {
        move(ml, sup, pre ? x + w - sup->w : x + ic, -u);
    }
    if (sub != NULL) {
        move(ml, sub, pre ? x + w - sub->w : x, v);
    }
    return w;
}

void
ml_scripts(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *out)
{
    ms_t up = ml_smaller(ml, st, 0), down = ml_smaller(ml, st, 1);
    int start = ml->nit, i;
    mb_t p[5], *sc[5];
    float u, v, pre = 0, x;

    for (i = 0; i < 5; i++) {       /* base, sub, sup, pre-sub, pre-sup */
        sc[i] = i > 0 && i < n->nkid && n->kid[i] != NULL ? &p[i] : NULL;
        ml_set(ml, i < n->nkid ? n->kid[i] : NULL,
               i == 0 ? st : i % 2 ? &down : &up, &p[i]);
    }
    if (sc[3] != NULL || sc[4] != NULL) {
        script_shifts(ml, st, &p[0], sc[4], sc[3], &u, &v);
        pre = place_scripts(ml, sc[4], sc[3], u, v, 0, 0, 1);
    }
    script_shifts(ml, st, &p[0], sc[2], sc[1], &u, &v);
    x = pre + p[0].w;
    x += place_scripts(ml, sc[2], sc[1], u, v, x,
                       p[0].italic ? p[0].ic : 0, 0);
    move(ml, &p[0], pre, 0);
    ml_empty(ml, out);
    out->w = x + (sc[1] != NULL || sc[2] != NULL ?
                  ml_c(ml, MC_SPACE_AFTER_SCRIPT, st) : 0);
    for (i = 0; i < 5; i++) {
        reach(out, &p[i]);
    }
    whole(ml, out, start);
    out->lcls = sc[3] != NULL || sc[4] != NULL ? OC_ORD : p[0].lcls;
    out->rcls = p[0].rcls;
}

/* -------------------------------------------------------------- roots  */

/** @brief Degree @p deg of a root set two sizes down into @p dg, up by
 *         the root's sign @p sign as far as the face says.  @return where
 *         the sign then starts. */
static float
degree(ml_t *ml, const of_mnode_t *deg, const ms_t *st, const mb_t *sign,
       mb_t *dg)
{
    ms_t s = ml_smaller(ml, st, 0);
    float x = ml_c(ml, MC_RAD_KERN_BEFORE, st);
    float raise = (sign->asc + sign->desc) *
                  ml_c(ml, MC_RAD_DEGREE_RAISE, st) / 100;

    s = ml_smaller(ml, &s, 0);
    ml_set(ml, deg, &s, dg);
    if (deg == NULL) {
        return 0;
    }
    move(ml, dg, x, sign->desc - raise - dg->desc);
    return fmaxf(x + dg->w + ml_c(ml, MC_RAD_KERN_AFTER, st), 0);
}

void
ml_root(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *out)
{
    float gap = ml_c(ml, st->level == 0 ? MC_RAD_GAP_D : MC_RAD_GAP, st);
    float th = ml_c(ml, MC_RAD_RULE, st), top, x;
    int start = ml->nit;
    ms_t in = *st;
    mb_t rad, sign, dg, seg[3];

    in.cramped = 1;
    ml_set(ml, n->nkid > 0 ? n->kid[0] : NULL, &in, &rad);
    ml_stretch(ml, 0x221a, 1, rad.asc + rad.desc + gap + th, st, &sign);
    if (sign.asc + sign.desc > rad.asc + rad.desc + gap + th) {
        gap += (sign.asc + sign.desc - (rad.asc + rad.desc + gap + th)) / 2;
    }
    top = rad.asc + gap + th;       /* the sign's top, and the rule's */
    move(ml, &sign, 0, sign.asc - top);
    x = degree(ml, n->nkid > 1 ? n->kid[1] : NULL, st, &sign, &dg);
    move(ml, &sign, x, 0);
    move(ml, &rad, x + sign.w, 0);
    ml_rule(ml, x + sign.w - th / 2, -top, rad.w + th / 2, th);
    ml_empty(ml, out);
    out->w = x + sign.w + rad.w;
    out->asc = top + ml_c(ml, MC_RAD_EXTRA, st);
    reach(out, &sign);
    reach(out, &rad);
    reach(out, &dg);
    seg[0] = dg;                    /* read degree, sign, radicand */
    seg[1] = sign;
    seg[2] = rad;
    ml_order(ml, start, seg, 3);
    whole(ml, out, start);
}

/* --------------------------------------------------- large operators  */

/** @brief The character large operator @p n stands for: an integral
 *         when it names none, as Word's do. */
static uint32_t
op_char(const of_mnode_t *n)
{
    uint32_t cp = first_cp(n->text);

    return cp != 0 ? cp : 0x222b;
}

/** @brief Whether @p cp is an integral, whose limits stay beside it. */
static int
integral(uint32_t cp)
{
    return (cp >= 0x222b && cp <= 0x2233) || (cp >= 0x2a0b && cp <= 0x2a1c);
}

/** @brief Large operator @p n's character into @p op: as tall as a
 *         display wants or as @p need, centred on the axis. */
static void
op_glyph(ml_t *ml, const of_mnode_t *n, const ms_t *st, float need,
         mb_t *op)
{
    ms_t big = *st;
    const struct of_mitem *g;

    if (ml->f->has_math && (st->level == 0 || need > 0)) {
        ml_stretch(ml, op_char(n), 1, fmaxf(need, st->level == 0 ?
                   ml_c(ml, MC_DISPLAY_OP_MIN, st) : 0), st, op);
    } else {                        /* no bigger glyph: a bigger size */
        big.size *= st->level == 0 ? 1.4f : 1;
        ml_text(ml, first_cp(n->text) ? n->text : "\xe2\x88\xab",
                OV_NORMAL, ml->color, OC_OP, &big, op);
    }
    g = op->to - op->from == 1 && ml->it[op->from].kind == MI_GLYPH ?
        &ml->it[op->from] : NULL;
    op->ic = g != NULL && g->face == ml->f->face ?
             (float)mf_italic(ml->f, g->gid) * g->size / (float)ml->f->upem
             : 0;
    on_axis(ml, op, st);
    op->lcls = op->rcls = OC_OP;
    op->glyph = 0;                  /* scripts by its edges, as TeX's */
}

/** @brief Limits @p lo and @p hi (either NULL) put under and over
 *         operator @p op, all centred over the widest, the upper right
 *         and the lower left by half its slant.  @return that width. */
static float
limits_over(ml_t *ml, const ms_t *st, mb_t *op, mb_t *lo, mb_t *hi)
{
    float w = fmaxf(op->w, fmaxf(lo != NULL ? lo->w : 0,
                                 hi != NULL ? hi->w : 0));
    float rise, drop;

    move(ml, op, (w - op->w) / 2, 0);
    if (hi != NULL) {
        rise = fmaxf(ml_c(ml, MC_UPPER_RISE_MIN, st),
                     ml_c(ml, MC_UPPER_GAP_MIN, st) + hi->desc);
        move(ml, hi, (w - hi->w + op->ic) / 2, -(op->asc + rise));
    }
    if (lo != NULL) {
        drop = fmaxf(ml_c(ml, MC_LOWER_DROP_MIN, st),
                     ml_c(ml, MC_LOWER_GAP_MIN, st) + lo->asc);
        move(ml, lo, (w - lo->w - op->ic) / 2, op->desc + drop);
    }
    return w;
}

/** @brief Operator @p op with large operator @p n's limits, a script's
 *         size, over and under it in a display, beside it otherwise, as
 *         piece @p out. */
static void
limits(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *op, mb_t *out)
{
    ms_t up = ml_smaller(ml, st, 0), down = ml_smaller(ml, st, 1);
    int over = st->level == 0 && ((n->flags & OMF_LIMITS) ||
               (!(n->flags & OMF_BESIDE) && !integral(op_char(n))));
    mb_t lo, hi, *l = n->nkid > 0 && n->kid[0] != NULL ? &lo : NULL;
    mb_t *h = n->nkid > 1 && n->kid[1] != NULL ? &hi : NULL;
    float u, v;

    ml_set(ml, l != NULL ? n->kid[0] : NULL, &down, &lo);
    ml_set(ml, h != NULL ? n->kid[1] : NULL, &up, &hi);
    ml_empty(ml, out);
    if (over) {
        out->w = limits_over(ml, st, op, l, h);
    } else {
        script_shifts(ml, st, op, h, l, &u, &v);
        out->w = op->w + place_scripts(ml, h, l, u, v, op->w, op->ic, 0);
    }
    reach(out, op);
    reach(out, &lo);
    reach(out, &hi);
}

void
ml_nary(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *out)
{
    float axis = ml_c(ml, MC_AXIS, st), need = 0;
    int start = ml->nit;
    mb_t op, body, seg[2];

    ml_set(ml, n->nkid > 2 ? n->kid[2] : NULL, st, &body);
    if (n->flags & OMF_GROW) {      /* as tall as what it operates on */
        need = 2 * fmaxf(body.asc - axis, body.desc + axis);
    }
    op_glyph(ml, n, st, need, &op);
    limits(ml, n, st, &op, out);
    seg[0].from = op.from;          /* read the operator, then its body */
    seg[0].to = ml->nit;
    seg[1] = body;
    if (body.to > body.from) {
        ml_append(ml, out, &body, st->size * 3 / 18);
    }
    ml_order(ml, start, seg, 2);
    whole(ml, out, start);
    out->lcls = OC_OP;
    out->rcls = body.to > body.from ? body.rcls : OC_OP;
}

/* ------------------------------------------------------------- fences  */

/** @brief Fence character @p s (its first) grown to @p need about the
 *         axis, into @p b; nothing for none. */
static void
fence_char(ml_t *ml, const char *s, float need, const ms_t *st, mb_t *b)
{
    uint32_t cp = first_cp(s);

    if (cp == 0) {
        ml_empty(ml, b);
        return;
    }
    ml_stretch(ml, cp, 1, need, st, b);
    on_axis(ml, b, st);
}

/** @brief The separator before part @p i (1 on) of a fence into @p b:
 *         the i-th character of @p list, its last repeated -- a comma or
 *         a semicolon as punctuation, anything else grown to @p need as
 *         the fence is.  @return the space it keeps before it. */
static float
separator(ml_t *ml, const char *list, int i, float need, const ms_t *st,
          mb_t *b)
{
    const unsigned char *u = (const unsigned char *)(list ? list : "");
    char one[8];
    size_t at = 0, len = 0;
    int k = 1;

    while (u[at] != 0) {
        for (len = 1; len < 4 && (u[at + len] & 0xc0) == 0x80; len++) {}
        if (k++ == i || u[at + len] == 0) {
            break;
        }
        at += len;
    }
    memcpy(one, u + at, len);
    one[len] = '\0';
    if (len > 0 && om_class(first_cp(one)) == OC_PUNCT) {
        ml_text(ml, one, OV_NORMAL, ml->color, OC_PUNCT, st, b);
        return 0;
    }
    fence_char(ml, one, need, st, b);
    return st->size / 18;
}

void
ml_fence(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *out)
{
    mb_t *p = n->nkid > 0 ? malloc((size_t)(3 * n->nkid + 1) * sizeof *p)
                          : NULL;
    float asc = 0, desc = 0, need, gap;
    int start = ml->nit, i, k = 0;
    mb_t b, *seg = p != NULL ? p + n->nkid : NULL;

    for (i = 0; p != NULL && i < n->nkid; i++) {
        ml_set(ml, n->kid[i], st, &p[k]);
        asc = fmaxf(asc, p[k].asc);
        desc = fmaxf(desc, p[k++].desc);
    }
    need = 2 * fmaxf(asc - ml_c(ml, MC_AXIS, st),
                     desc + ml_c(ml, MC_AXIS, st));
    need = n->flags & OMF_GROW ? fmaxf(need * 0.901f, need - st->size / 2)
                               : 0;         /* TeX's slack */
    fence_char(ml, n->open, need, st, out);
    for (i = 0; seg != NULL && i < k; i++) {
        seg[0] = i == 0 ? *out : seg[0];
        gap = 0;
        if (i > 0) {                /* a separator before each part */
            gap = separator(ml, n->sep, i, need, st, &b);
            seg[2 * i] = b;
            ml_append(ml, out, &b, gap);
            gap = gap > 0 ? gap : st->size * 3 / 18;
        }
        ml_append(ml, out, &p[i], gap);
        seg[2 * i + 1] = p[i];
    }
    fence_char(ml, n->close, need, st, &b);
    ml_append(ml, out, &b, 0);
    if (seg != NULL) {              /* read as written: fence, parts ... */
        seg[2 * k] = b;
        ml_order(ml, start, seg, 2 * k + 1);
    }
    whole(ml, out, start);
    out->lcls = OC_OPEN;
    out->rcls = OC_CLOSE;
    free(p);
}

/* ------------------------------------------------- matrices and arrays  */

/** @brief Row @p r of an array cut at its alignment marks into cells,
 *         each set as a row, each after the first given the space its
 *         first atom keeps from what went before.  @return how many, at
 *         most GRID_MAX, set into @p cell. */
static int
split_row(ml_t *ml, const of_mnode_t *r, const ms_t *st, mb_t *cell)
{
    of_mnode_t *const *kid = r != NULL && r->kind == OM_ROW ? r->kid : NULL;
    int n = kid != NULL ? r->nkid : 0, i, from = 0, k = 0;
    float g;

    if (kid == NULL) {
        ml_set(ml, r, st, &cell[0]);
        return 1;
    }
    for (i = 0; i <= n && k < GRID_MAX; i++) {
        if (i < n && (kid[i] == NULL || kid[i]->kind != OM_ALIGN)) {
            continue;
        }
        ml_row(ml, kid + from, i - from, st, &cell[k]);
        g = k == 0 ? 0 : cell[k].lcls == OC_REL ? st->size * 5 / 18 :
            cell[k].lcls == OC_BIN ? st->size * 4 / 18 : 0;
        move(ml, &cell[k], g, 0);   /* "x &= 1": the = keeps its space */
        cell[k++].w += g;
        from = i + 1;
    }
    return k;
}

/** @brief A grid's column widths and its rows' baselines down from its
 *         top, each row a strut's height and depth at least, @p em the
 *         size; its height into @p total. */
static void
measure(const mb_t *cell, int rows, int cols, float em, float *colw,
        float *base, float *total)
{
    float y = 0, asc, desc = 0;
    int r, c;

    for (c = 0; c < cols; c++) {
        for (colw[c] = 0, r = 0; r < rows; r++) {
            colw[c] = fmaxf(colw[c], cell[r * cols + c].w);
        }
    }
    for (r = 0; r < rows; r++) {
        y += r > 0 ? desc + em * 0.15f : 0;
        for (asc = em * 0.75f, desc = em * 0.3f, c = 0; c < cols; c++) {
            asc = fmaxf(asc, cell[r * cols + c].asc);
            desc = fmaxf(desc, cell[r * cols + c].desc);
        }
        y += asc;
        base[r] = y;
    }
    *total = y + desc;
}

/** @brief Where in its column, @p colw wide, cell @p c of grid @p n
 *         (@p w wide) starts: by the matrix's own alignment, else an
 *         array's (right then left by turns), else centred. */
static float
align_in(const of_mnode_t *n, int c, int cols, float w, float colw)
{
    char a = n->kind == OM_MATRIX && n->align != NULL ? (char)n->align[c] :
             n->kind == OM_ARRAY && cols > 1 ? (c % 2 ? 'l' : 'r') : 'c';

    return a == 'l' ? 0 : a == 'r' ? colw - w : (colw - w) / 2;
}

/** @brief A grid's cells set: a matrix's in order, an array's rows each
 *         cut at its marks.  @return its columns; its rows in @p rows. */
static int
grid_cells(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *cell,
           int *rows)
{
    int r, c, cols = 1, k;

    if (n->kind == OM_MATRIX) {
        *rows = n->rows;
        for (r = 0; r < n->rows * n->cols; r++) {
            ml_set(ml, r < n->nkid ? n->kid[r] : NULL, st, &cell[r]);
        }
        return n->cols;
    }
    *rows = n->nkid < GRID_MAX ? n->nkid : GRID_MAX;
    for (r = 0; r < *rows; r++) {   /* as many columns as the most marks */
        k = split_row(ml, n->kid[r], st, &cell[r * GRID_MAX]);
        for (c = k; c < GRID_MAX; c++) {
            ml_empty(ml, &cell[r * GRID_MAX + c]);
        }
        cols = k > cols ? k : cols;
    }
    for (r = 0; r < *rows; r++) {   /* packed to the columns there are */
        memmove(&cell[r * cols], &cell[r * GRID_MAX],
                (size_t)cols * sizeof *cell);
    }
    return cols;
}

/** @brief Grid @p n's cells (@p rows by @p cols) put in their places,
 *         the grid's foot on the baseline; @p out takes its extent. */
static void
place_cells(ml_t *ml, const of_mnode_t *n, mb_t *cell, int rows, int cols,
            const ms_t *st, mb_t *out)
{
    float colw[GRID_MAX], base[GRID_MAX], total, x;
    int r, c;

    measure(cell, rows, cols, st->size, colw, base, &total);
    for (r = 0; r < rows; r++) {
        for (c = 0, x = 0; c < cols; c++) {
            mb_t *b = &cell[r * cols + c];

            move(ml, b, x + align_in(n, c, cols, b->w, colw[c]),
                 base[r] - total);
            x += colw[c];
            if (c + 1 < cols && (n->kind == OM_MATRIX || c % 2)) {
                x += st->size;      /* between columns, and pairs */
            }
        }
        out->w = fmaxf(out->w, x);
    }
    out->asc = total;
    out->desc = 0;
}

void
ml_grid(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *out)
{
    int fits = n->kind == OM_MATRIX && n->rows > 0 && n->cols > 0 &&
               n->rows <= GRID_MAX && n->cols <= GRID_MAX;
    size_t cells = fits ? (size_t)n->rows * (size_t)n->cols :
                   (size_t)(n->nkid < GRID_MAX ? n->nkid : GRID_MAX) *
                   GRID_MAX;
    mb_t *cell = cells > 0 ? calloc(cells, sizeof *cell) : NULL;
    int start = ml->nit, rows = 0, cols;
    ms_t in = *st;

    ml_empty(ml, out);
    if (cell == NULL || (n->kind == OM_MATRIX && !fits)) {
        free(cell);
        return;
    }
    in.level = n->kind == OM_MATRIX && st->level < 1 ? 1 : st->level;
    cols = grid_cells(ml, n, &in, cell, &rows);
    place_cells(ml, n, cell, rows, cols, st, out);
    whole(ml, out, start);
    on_axis(ml, out, st);
    out->lcls = out->rcls = OC_ORD;
    free(cell);
}

/* ------------------------------------------- over and under a base  */

/** @brief The accent the math face draws for @p cp: its combining form
 *         when the face has that, else its spacing form. */
static uint32_t
accent_char(const ml_t *ml, uint32_t cp)
{
    static const uint16_t pair[][2] = {        /* combining, spacing */
        {0x300, 0x60}, {0x301, 0xb4}, {0x302, 0x2c6}, {0x302, '^'},
        {0x303, 0x2dc}, {0x303, '~'}, {0x304, 0xaf}, {0x305, 0x203e},
        {0x306, 0x2d8}, {0x307, 0x2d9}, {0x308, 0xa8}, {0x30a, 0x2da},
        {0x30c, 0x2c7}, {0x20d6, 0x2190}, {0x20d7, 0x2192},
        {0x20e1, 0x2194}};
    const of_face_t *fc = &ml->f->fs->face[ml->f->face];
    size_t i;

    for (i = 0; i < sizeof pair / sizeof pair[0]; i++) {
        if (pair[i][1] == cp && tiku_ttf_glyph(fc->ttf, pair[i][0]) != 0) {
            return pair[i][0];
        }
        if (pair[i][0] == cp && tiku_ttf_glyph(fc->ttf, cp) == 0) {
            return pair[i][1];
        }
    }
    return cp;
}

/** @brief Piece @p b's one glyph, when that is all it is; else NULL. */
static const struct of_mitem *
lone(const ml_t *ml, const mb_t *b)
{
    return b->to - b->from == 1 && b->from < ml->nit &&
           ml->it[b->from].kind == MI_GLYPH ? &ml->it[b->from] : NULL;
}

/** @brief Where over piece @p b, from its left, an accent centres: the
 *         face's attachment point for a lone glyph, the middle of its ink
 *         when the face names none, else the middle of the piece. */
static float
attach_at(ml_t *ml, const mb_t *b)
{
    const struct of_mitem *g = lone(ml, b);
    const of_face_t *fc;
    float box[4], k;
    int at;

    if (g == NULL) {
        return b->w / 2;
    }
    fc = &ml->f->fs->face[g->face];
    k = g->size / (float)(fc->upem > 0 ? fc->upem : 1000);
    at = g->face == ml->f->face ? mf_accent_at(ml->f, g->gid) : -1;
    if (at >= 0) {
        return g->x + (float)at * k;
    }
    mf_ink(ml->f, g->face, g->gid, box);
    return box[2] > box[0] ? g->x + (box[0] + box[2]) / 2 * k : b->w / 2;
}

/** @brief How high above its baseline piece @p b's ink begins: a lone
 *         glyph's own ink, whose depth a piece keeps no shallower than
 *         its baseline; anything else's depth. */
static float
ink_bottom(ml_t *ml, const mb_t *b)
{
    const struct of_mitem *g = lone(ml, b);
    const of_face_t *fc = g != NULL ? &ml->f->fs->face[g->face] : NULL;
    float box[4];

    if (g == NULL) {
        return -b->desc;
    }
    mf_ink(ml->f, g->face, g->gid, box);
    return box[3] > box[1] ? box[1] * g->size /
                             (float)(fc->upem > 0 ? fc->upem : 1000) - g->y
                           : -b->desc;
}

/** @brief Accent @p cp over base @p b, into @p acc, centred where the
 *         face says accents sit: the widest that fits across it, raised as
 *         far as the base stands taller than the math face's accents
 *         expect, never into it; a text face's marks, made for no height
 *         in particular, just clear of the base's ink. */
static void
accent(ml_t *ml, uint32_t cp, const ms_t *st, const mb_t *b, mb_t *acc)
{
    float at = attach_at(ml, b), up, bottom;

    ml_fit(ml, accent_char(ml, cp), b->w, st, acc);
    bottom = ink_bottom(ml, acc);
    up = fmaxf(0, b->asc - ml_c(ml, MC_ACCENT_BASE, st));
    up = ml->f->has_math ? fmaxf(up, b->asc - bottom)
                         : b->asc - bottom + st->size * 0.08f;
    move(ml, acc, at - attach_at(ml, acc), -up);
}

/** @brief Whether node @p n is one character drawn across what it goes
 *         over or under: a brace, a bracket, an arrow, a bar, a hat. */
static int
spans(const of_mnode_t *n)
{
    static const uint16_t set[] = {
        '^', '_', '~', 0xaf, 0x2c6, 0x2dc, 0x302, 0x303, 0x305, 0x332,
        0x203e, 0x20d6, 0x20d7, 0x20e1, 0x2190, 0x2192, 0x2194, 0x21d0,
        0x21d2, 0x21d4, 0x23b4, 0x23b5, 0x23dc, 0x23dd, 0x23de, 0x23df,
        0xfe37, 0xfe38};
    const unsigned char *t;
    size_t i;

    if (n == NULL || n->kind != OM_TOKEN || n->text == NULL) {
        return 0;
    }
    t = (const unsigned char *)n->text;
    if (t[t[0] < 0x80 ? 1 : t[0] < 0xe0 ? 2 : t[0] < 0xf0 ? 3 : 4] != 0) {
        return 0;                   /* more than one character */
    }
    for (i = 0; i < sizeof set / sizeof set[0]; i++) {
        if (set[i] == first_cp(n->text)) {
            return 1;
        }
    }
    return 0;
}

/** @brief Piece @p s put over (@p dir -1) or under (1) piece @p b,
 *         @p gap between, centred over @p w. */
static void
stack_on(ml_t *ml, const mb_t *b, mb_t *s, int dir, float gap, float w)
{
    move(ml, s, (w - s->w) / 2, dir < 0 ? -(b->asc + gap + s->desc)
                                        : b->desc + gap + s->asc);
}

/** @brief A base with what goes under and over it, into @p out: limits a
 *         script's size, or characters grown across it; a stretchy base
 *         (an arrow) grown across them instead. */
static void
under_over(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *out)
{
    ms_t sm = ml_smaller(ml, st, 0);
    const of_mnode_t *part[3];
    int start = ml->nit, i;
    mb_t p[3];
    float w = 0;

    for (i = 0; i < 3; i++) {       /* base, under, over */
        part[i] = i < n->nkid ? n->kid[i] : NULL;
        ml_empty(ml, &p[i]);
    }
    for (i = 1; i < 3; i++) {       /* the scripts that keep their size */
        if (part[i] != NULL && !spans(part[i])) {
            ml_set(ml, part[i], &sm, &p[i]);
            w = fmaxf(w, p[i].w);
        }
    }
    if (spans(part[0]) && w > 0) {
        ml_stretch(ml, first_cp(part[0]->text), 0, w + st->size * 0.4f, st,
                   &p[0]);
        p[0].lcls = p[0].rcls = part[0]->cls;
    } else {
        ml_set(ml, part[0], st, &p[0]);
    }
    for (i = 1; i < 3; i++) {       /* ... and those grown across it */
        if (spans(part[i])) {
            ml_stretch(ml, first_cp(part[i]->text), 0, p[0].w, st, &p[i]);
        }
    }
    w = fmaxf(p[0].w, fmaxf(p[1].w, p[2].w));
    move(ml, &p[0], (w - p[0].w) / 2, 0);
    *out = p[0];
    for (i = 1; i < 3; i++) {
        if (part[i] != NULL) {
            stack_on(ml, &p[0], &p[i], i == 1 ? 1 : -1, ml_c(ml, i == 1 ?
                     MC_LOWER_GAP_MIN : MC_UPPER_GAP_MIN, st), w);
            reach(out, &p[i]);
        }
    }
    out->w = w;
    ml_order(ml, start, p, 3);      /* read the base, then the rest */
}

/** @brief A rule over base @p b (under it for a bar at the bottom of
 *         @p n), apart from it as the face says. */
static void
bar(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *b)
{
    int top = !(n->flags & OMF_BOTTOM);
    float g = ml_c(ml, top ? MC_OVERBAR_GAP : MC_UNDERBAR_GAP, st);
    float th = ml_c(ml, top ? MC_OVERBAR_RULE : MC_UNDERBAR_RULE, st);
    float extra = ml_c(ml, top ? MC_OVERBAR_EXTRA : MC_UNDERBAR_EXTRA, st);

    ml_rule(ml, 0, top ? -(b->asc + g + th) : b->desc + g, b->w, th);
    if (top) {
        b->asc += g + th + extra;
    } else {
        b->desc += g + th + extra;
    }
}

/** @brief Node @p n's brace (or bracket) grown across base @p b and put
 *         over or under it. */
static void
brace(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *b)
{
    int top = (n->flags & OMF_TOP) != 0;
    uint32_t cp = first_cp(n->text);
    mb_t s;

    ml_stretch(ml, cp != 0 ? cp : top ? 0x23de : 0x23df, 0, b->w, st, &s);
    stack_on(ml, b, &s, top ? -1 : 1, ml_c(ml, top ? MC_STRETCH_GAP_ABOVE
                                                   : MC_STRETCH_GAP_BELOW,
                                           st), b->w);
    reach(b, &s);
    b->w = fmaxf(b->w, s.w);
}

void
ml_over(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *out)
{
    int start = ml->nit;
    ms_t in = *st;
    mb_t s;

    if (n->kind == OM_UNDEROVER) {
        under_over(ml, n, st, out);
        whole(ml, out, start);
        return;
    }
    in.cramped = n->kind == OM_ACCENT || n->kind == OM_BAR || in.cramped;
    ml_set(ml, n->nkid > 0 ? n->kid[0] : NULL, &in, out);
    if (n->kind == OM_ACCENT) {
        accent(ml, first_cp(n->text) ? first_cp(n->text) : 0x302, st, out,
               &s);
        reach(out, &s);
    } else if (n->kind == OM_BAR) {
        bar(ml, n, st, out);
    } else {
        brace(ml, n, st, out);
    }
    whole(ml, out, start);
}
