/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_math_font.c - the face equations are set in, and what its
 * OpenType MATH table says: the lengths a typesetter keeps (the axis,
 * gaps, shifts, rule thicknesses), each glyph's italic correction and
 * where an accent centres over it, and bigger variants of a glyph or the
 * parts it is built from to any size.  A face without the table gets
 * TeX's lengths in ems, and its shapes are scaled or drawn instead.
 * Glyphs' extents are measured from their outlines, once each.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_math.h"
#include "tiku_glyphpath.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MATH_MAX (4u << 20)         /* a MATH table read, at most        */
#define INK_EM   200.0f             /* an em measured in so many units   */

/** @brief One glyph's extent, once measured. */
struct mf_ink {
    int      face;                  /* -1: an empty slot                 */
    unsigned gid;
    float    box[4];                /* x0 y0 x1 y1, font units, y up     */
};

/** @brief TeX's lengths, thousandths of an em, for a face with no MATH
 *         table (per cents where the table has them). */
static const short fallback[MC_COUNT] = {
    70, 50, 1500, 1300, 150, 250, 450, 450, 150, 344, 50, 360, 290, 108,
    386, 200, 344, 50, 111, 111, 167, 600, 444, 677, 345, 686, 120, 280,
    111, 111, 200, 167, 394, 677, 345, 686, 40, 120, 40, 40, 120, 350,
    100, 120, 40, 40, 120, 40, 40, 50, 150, 40, 40, 278, -556, 60};

/* ---------------------------------------------------------------- bytes  */

/** @brief Big-endian unsigned 16 bits of @p b (@p n bytes) at @p off; 0
 *         past its end. */
static unsigned
u16(const unsigned char *b, size_t n, size_t off)
{
    return off + 2 <= n ? (unsigned)(b[off] << 8 | b[off + 1]) : 0;
}

/** @brief Big-endian signed 16 bits of @p b (@p n bytes) at @p off; 0
 *         past its end. */
static int
s16(const unsigned char *b, size_t n, size_t off)
{
    return (int)(int16_t)u16(b, n, off);
}

/** @brief Big-endian unsigned 32 bits of @p b (@p n bytes) at @p off; 0
 *         past its end. */
static uint32_t
u32(const unsigned char *b, size_t n, size_t off)
{
    return off + 4 <= n ? (uint32_t)b[off] << 24 | (uint32_t)b[off + 1] << 16
                          | (uint32_t)b[off + 2] << 8 | b[off + 3] : 0;
}

/** @brief @p n bytes of file @p f at @p off into @p buf.  @return 0, or
 *         -1. */
static int
read_at(FILE *f, long off, unsigned char *buf, size_t n)
{
    return off >= 0 && fseek(f, off, SEEK_SET) == 0 &&
           fread(buf, 1, n, f) == n ? 0 : -1;
}

/** @brief Table @p tag of face @p index of the font file @p path, to
 *         free(), its length in @p len; NULL when it has none. */
static unsigned char *
table(const char *path, int index, const char *tag, size_t *len)
{
    unsigned char head[16], rec[16], *t = NULL;
    FILE *f = fopen(path, "rb");
    long at = 0;
    unsigned i, n;

    if (f == NULL || read_at(f, 0, head, 12) != 0) {
        goto done;
    }
    if (memcmp(head, "ttcf", 4) == 0) {     /* a collection: its face */
        if (index < 0 || (uint32_t)index >= u32(head, 12, 8) ||
            read_at(f, 12 + 4L * index, head, 4) != 0 ||
            read_at(f, at = (long)u32(head, 4, 0), head, 12) != 0) {
            goto done;
        }
    }
    n = u16(head, 12, 4);
    for (i = 0; i < n && i < 256; i++) {
        if (read_at(f, at + 12 + 16L * i, rec, 16) != 0) {
            break;
        }
        if (memcmp(rec, tag, 4) == 0 && u32(rec, 16, 12) > 0 &&
            u32(rec, 16, 12) <= MATH_MAX &&
            (t = malloc(u32(rec, 16, 12))) != NULL) {
            *len = u32(rec, 16, 12);
            if (read_at(f, (long)u32(rec, 16, 8), t, *len) != 0) {
                free(t);
                t = NULL;
            }
            break;
        }
    }
done:
    if (f != NULL) {
        fclose(f);
    }
    return t;
}

/* --------------------------------------------------------------- open  */

/** @brief The MATH table's constants read into @p m.  @return 0, or -1
 *         for a table with none. */
static int
constants(mf_t *m)
{
    size_t c = u16(m->math, m->nmath, 4);
    int i;

    if (c == 0 || c + 214 > m->nmath) {
        return -1;
    }
    m->c[MC_SCRIPT_PCT] = s16(m->math, m->nmath, c);
    m->c[MC_SCRIPTSCRIPT_PCT] = s16(m->math, m->nmath, c + 2);
    m->c[MC_DELIM_MIN] = (int)u16(m->math, m->nmath, c + 4);
    m->c[MC_DISPLAY_OP_MIN] = (int)u16(m->math, m->nmath, c + 6);
    for (i = MC_LEADING; i < MC_RAD_DEGREE_RAISE; i++) {
        m->c[i] = s16(m->math, m->nmath, c + 8 + 4 * (size_t)(i -
                                                              MC_LEADING));
    }
    m->c[MC_RAD_DEGREE_RAISE] = s16(m->math, m->nmath, c + 212);
    return 0;
}

/** @brief Face @p name taken when the machine has it as itself, with a
 *         MATH table.  @return 0, or -1. */
static int
try_face(mf_t *m, const char *name)
{
    tiku_fontset_face_t f;
    const of_face_t *fc;
    int k;

    if (name == NULL || name[0] == '\0' ||
        tiku_fontset_find(name, 400, 0, &f) != 0 || !f.exact ||
        (k = of_face(m->fs, name, 0, 0)) < 0) {
        return -1;
    }
    fc = &m->fs->face[k];
    m->math = table(fc->f.path, fc->f.index, "MATH", &m->nmath);
    if (m->math == NULL || constants(m) != 0) {
        free(m->math);
        m->math = NULL;
        return -1;
    }
    m->face = k;
    m->upem = fc->upem > 0 ? fc->upem : 1000;
    m->has_math = 1;
    return 0;
}

int
mf_open(mf_t *m, of_faces_t *fs, const char *want)
{
    static const char *const known[] = {
        "Cambria Math", "STIX Two Math", "Latin Modern Math", "STIX Math",
        "XITS Math", "Libertinus Math", "TeX Gyre Termes Math",
        "TeX Gyre Pagella Math", "DejaVu Math TeX Gyre", "Asana Math",
        "Fira Math", "Noto Sans Math"};
    size_t i;

    memset(m, 0, sizeof *m);
    m->fs = fs;
    m->face = -1;
    if (try_face(m, want) == 0) {
        return 0;
    }
    for (i = 0; i < sizeof known / sizeof known[0]; i++) {
        if (try_face(m, known[i]) == 0) {
            return 0;
        }
    }
    if ((m->face = of_face(fs, "Times New Roman", 0, 0)) < 0) {
        return -1;                  /* no faces at all */
    }
    m->upem = fs->face[m->face].upem > 0 ? fs->face[m->face].upem : 1000;
    for (i = 0; i < MC_COUNT; i++) {
        m->c[i] = i == MC_SCRIPT_PCT || i == MC_SCRIPTSCRIPT_PCT ||
                  i == MC_RAD_DEGREE_RAISE
                  ? fallback[i] : fallback[i] * m->upem / 1000;
    }
    return 0;
}

void
mf_close(mf_t *m)
{
    free(m->math);
    free(m->ink);
    memset(m, 0, sizeof *m);
    m->face = -1;
}

float
mf_const(const mf_t *m, int which, float size)
{
    if (which < 0 || which >= MC_COUNT) {
        return 0;
    }
    if (which == MC_SCRIPT_PCT || which == MC_SCRIPTSCRIPT_PCT ||
        which == MC_RAD_DEGREE_RAISE) {
        return (float)m->c[which];
    }
    return (float)m->c[which] * size / (float)m->upem;
}

/* ---------------------------------------------------------------- ink  */

/** @brief The slot for (@p face, @p gid): its entry, or the empty one it
 *         would go in. */
static struct mf_ink *
ink_slot(mf_t *m, int face, unsigned gid)
{
    unsigned k = (gid * 2654435761u ^ (unsigned)face * 40503u) &
                 (unsigned)(m->inkcap - 1);

    while (m->ink[k].face >= 0 &&
           (m->ink[k].face != face || m->ink[k].gid != gid)) {
        k = (k + 1) & (unsigned)(m->inkcap - 1);
    }
    return &m->ink[k];
}

/** @brief The table of extents twice as large.  @return 0, or -1. */
static int
ink_grow(mf_t *m)
{
    struct mf_ink *old = m->ink;
    int oldcap = m->inkcap, cap = oldcap ? oldcap * 2 : 256, i;

    if ((m->ink = malloc((size_t)cap * sizeof *m->ink)) == NULL) {
        m->ink = old;
        return -1;
    }
    m->inkcap = cap;
    for (i = 0; i < cap; i++) {
        m->ink[i].face = -1;
    }
    for (i = 0; i < oldcap; i++) {
        if (old[i].face >= 0) {
            *ink_slot(m, old[i].face, old[i].gid) = old[i];
        }
    }
    free(old);
    return 0;
}

/** @brief Glyph @p gid of face @p face measured from its outline, at
 *         INK_EM units to the em, into @p box (font units, y up). */
static void
measure(const of_face_t *f, unsigned gid, float *box)
{
    tiku_path_t *path = tiku_path_new();
    float k = INK_EM / (float)(f->upem > 0 ? f->upem : 1000);
    tiku_hint_t hint;
    int b[4] = {0, 0, 0, 0};

    memset(&hint, 0, sizeof hint);
    hint.scale = k;
    if (path != NULL && tiku_ttf_outline(f->ttf, gid, &hint, path) &&
        !tiku_path_failed(path)) {
        tiku_path_bounds(path, &b[0], &b[1], &b[2], &b[3]);
    }
    tiku_path_free(path);
    box[0] = (float)b[0] / k;
    box[2] = (float)(b[0] + b[2]) / k;
    box[1] = (float)-(b[1] + b[3]) / k;     /* the outline's y runs down */
    box[3] = (float)-b[1] / k;
    if (b[2] <= 0 || b[3] <= 0) {
        box[0] = box[1] = box[2] = box[3] = 0;
    }
}

void
mf_ink(mf_t *m, int face, unsigned gid, float *box)
{
    struct mf_ink *e;

    box[0] = box[1] = box[2] = box[3] = 0;
    if (face < 0 || face >= m->fs->nface) {
        return;
    }
    if ((m->nink + 1) * 2 > m->inkcap && ink_grow(m) != 0) {
        measure(&m->fs->face[face], gid, box);
        return;
    }
    e = ink_slot(m, face, gid);
    if (e->face < 0) {
        e->face = face;
        e->gid = gid;
        measure(&m->fs->face[face], gid, e->box);
        m->nink++;
    }
    memcpy(box, e->box, sizeof e->box);
}

/* ---------------------------------------------------------- the table  */

/** @brief Glyph @p gid's index in the coverage table at @p off, or -1. */
static int
coverage(const mf_t *m, size_t off, unsigned gid)
{
    const unsigned char *b = m->math;
    size_t n = m->nmath, i, count = u16(b, n, off + 2);

    if (off == 0 || off >= n) {
        return -1;
    }
    if (u16(b, n, off) == 1) {      /* a list of glyphs, in order */
        size_t lo = 0, hi = count;

        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            unsigned g = u16(b, n, off + 4 + 2 * mid);

            if (g == gid) {
                return (int)mid;
            }
            if (g < gid) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return -1;
    }
    for (i = 0; u16(b, n, off) == 2 && i < count; i++) {   /* ranges */
        size_t r = off + 4 + 6 * i;

        if (gid >= u16(b, n, r) && gid <= u16(b, n, r + 2)) {
            return (int)(u16(b, n, r + 4) + gid - u16(b, n, r));
        }
    }
    return -1;
}

/** @brief A value of the MathGlyphInfo subtable @p which (0 italic
 *         corrections, 1 accents' attachments) for glyph @p gid; @p dflt
 *         when it has none. */
static int
glyph_value(const mf_t *m, int which, unsigned gid, int dflt)
{
    size_t info = u16(m->math, m->nmath, 6), sub, k;
    int i;

    if (!m->has_math || info == 0) {
        return dflt;
    }
    sub = u16(m->math, m->nmath, info + 2 * (size_t)which);
    if (sub == 0) {
        return dflt;
    }
    sub += info;
    i = coverage(m, sub + u16(m->math, m->nmath, sub), gid);
    k = sub + 4 + 4 * (size_t)i;
    return i < 0 || i >= (int)u16(m->math, m->nmath, sub + 2)
           ? dflt : s16(m->math, m->nmath, k);
}

int
mf_italic(const mf_t *m, unsigned gid)
{
    return glyph_value(m, 0, gid, 0);
}

int
mf_accent_at(const mf_t *m, unsigned gid)
{
    return glyph_value(m, 1, gid, -1);
}

/** @brief How often the extenders of the assembly at @p at repeat to
 *         reach @p size, into @p reps, and how far each joint then
 *         overlaps, never less than @p lo, into @p over.  @return 0, or
 *         -1 for an assembly with no parts. */
static int
plan(const mf_t *m, size_t at, int lo, int size, int *reps, int *over)
{
    const unsigned char *b = m->math;
    size_t n = m->nmath, cnt = u16(b, n, at + 4), i;
    int fixed = 0, ext = 0, joints = -1;

    for (i = 0; i < cnt && i < 32; i++) {
        size_t p = at + 6 + 10 * i;

        *(u16(b, n, p + 8) & 1 ? &ext : &fixed) += (int)u16(b, n, p + 6) -
                                                   lo;
    }
    if (cnt == 0 || cnt > 32 || fixed + ext <= 0) {
        return -1;
    }
    for (*reps = 0; ext > 0 && fixed + *reps * ext + lo < size &&
         *reps < 40; (*reps)++) {}
    for (i = 0; i < cnt; i++) {
        joints += u16(b, n, at + 6 + 10 * i + 8) & 1 ? *reps : 1;
    }
    *over = joints > 0 ? (fixed + *reps * ext + lo - size) / joints + lo : 0;
    *over = *over < lo ? lo : *over;
    return 0;
}

/** @brief An assembly's parts laid end to end to reach @p size: each
 *         extender repeated as often as needed, every joint overlapping
 *         by as much as brings them out at the size, never less than
 *         @p lo nor more than the connector there.  @return 0, or -1. */
static int
assemble(const mf_t *m, size_t at, int lo, int size, mf_parts_t *out)
{
    const unsigned char *b = m->math;
    size_t n = m->nmath, cnt = u16(b, n, at + 4), i;
    int reps, over, k = 0, pos = 0;

    if (plan(m, at, lo, size, &reps, &over) != 0) {
        return -1;
    }
    for (i = 0; i < cnt; i++) {
        size_t p = at + 6 + 10 * i;
        int times = u16(b, n, p + 8) & 1 ? reps : 1, r;

        for (r = 0; r < times && k < MF_PARTS; r++, k++) {
            int conn = (int)u16(b, n, p + 2);   /* its start's connector */

            pos -= k > 0 ? (over < conn ? over : conn) : 0;
            out->gid[k] = u16(b, n, p);
            out->at[k] = pos;
            pos += (int)u16(b, n, p + 6);
        }
    }
    out->n = k;
    out->size = pos;
    out->built = 1;
    return k > 0 ? 0 : -1;
}

/** @brief Where glyph @p gid's construction along an axis (@p vert: up)
 *         starts in the table; 0 when it has none. */
static size_t
construction(const mf_t *m, unsigned gid, int vert)
{
    const unsigned char *b = m->math;
    size_t n = m->nmath, vars = u16(b, n, 8);
    int idx;

    if (!m->has_math || vars == 0) {
        return 0;
    }
    idx = coverage(m, vars + u16(b, n, vars + (vert ? 2 : 4)), gid);
    if (idx < 0 || idx >= (int)u16(b, n, vars + (vert ? 6 : 8))) {
        return 0;
    }
    return vars + u16(b, n, vars + 10 + 2 * ((vert ? 0 :
                                              u16(b, n, vars + 6)) +
                                             (size_t)idx));
}

unsigned
mf_fit(const mf_t *m, unsigned gid, int vert, int size)
{
    size_t con = construction(m, gid, vert), i, nv;
    unsigned best = gid;

    nv = con != 0 ? u16(m->math, m->nmath, con + 2) : 0;
    for (i = 0; i < nv; i++) {      /* in order of size: the last within */
        if ((int)u16(m->math, m->nmath, con + 4 + 4 * i + 2) > size) {
            break;
        }
        best = u16(m->math, m->nmath, con + 4 + 4 * i);
    }
    return best;
}

int
mf_stretch(mf_t *m, unsigned gid, int vert, int size, mf_parts_t *out)
{
    const unsigned char *b = m->math;
    size_t n = m->nmath, vars = u16(b, n, 8), con, i, nv;

    memset(out, 0, sizeof *out);
    if ((con = construction(m, gid, vert)) == 0) {
        return -1;
    }
    nv = u16(b, n, con + 2);
    for (i = 0; i < nv; i++) {      /* the first variant big enough */
        if ((int)u16(b, n, con + 4 + 4 * i + 2) >= size) {
            out->n = 1;
            out->gid[0] = u16(b, n, con + 4 + 4 * i);
            out->size = (int)u16(b, n, con + 4 + 4 * i + 2);
            return 0;
        }
    }
    if (u16(b, n, con) != 0 &&
        assemble(m, con + u16(b, n, con), (int)u16(b, n, vars), size,
                 out) == 0) {
        return 0;
    }
    if (nv > 0) {                   /* the biggest there is */
        out->n = 1;
        out->gid[0] = u16(b, n, con + 4 + 4 * (nv - 1));
        out->size = (int)u16(b, n, con + 4 + 4 * (nv - 1) + 2);
        return 0;
    }
    return -1;
}
