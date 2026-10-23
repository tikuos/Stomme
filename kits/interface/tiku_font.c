/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_font.c - the UI faces: sizes, families, faces read from files,
 * and drawing text.
 *
 * Coverage is blended against the pixel already on the surface, so a
 * label reads correctly on any background.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_dl.h"
#include "tiku_font.h"
#include "tiku_measure.h"
#include "tiku_font_data.h"
#include "tiku_ttf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The faces handed out are mutable copies with a stable address, so a
 * caller that kept the pointer sees a size change. */
static tiku_font_t current_plain;
static tiku_font_t current_bold;
static int current_size;
static int current_family;
static unsigned font_generation = 1;

/*
 * A face read from a file.  The baked tables are const and cannot hold
 * one, so a built face owns its glyphs and coverage, drawn as they are
 * asked for, and is rebuilt when the size changes.
 */
#define BUILT_MAX     6         /* faces alive at once: sizes x weights */
#define CACHE_SLOTS   512       /* per face; a power of two */
#define CACHE_CEILING 384       /* glyphs kept before the slate is wiped */

typedef struct {
    unsigned              cp;
    int                   used;
    int                   present;    /* the file has it, ink or not */
    tiku_ttf_glyph_t glyph;
} cache_slot_t;

struct tiku_face_src {
    const tiku_ttf_t *ttf;     /* file_regular's or file_bold's */
    int                    px;
    int                    bold;
    int                    filled;
    cache_slot_t           slot[CACHE_SLOTS];
};

typedef struct {
    tiku_font_t      face;
    tiku_face_src_t *src;
    /*
     * The same face at twice the size, kept in the same slot as its 1x
     * face: faces are handed out by address and this table evicts, so a
     * pointer to another entry could come to name the face that slot was
     * given to since.  It is freed with its 1x face.
     */
    tiku_font_t      hiface;
    tiku_face_src_t *hisrc;
    int                   px, bold, live;
    unsigned              stamp;      /* when it was last handed out */
} built_face_t;

static built_face_t built[BUILT_MAX];
static unsigned built_clock;
/* A face handed out a moment ago but not yet copied into the pair in
 * force: making the second face of a pair must not evict the first,
 * which the caller still holds. */
static const tiku_font_t *built_pinned;
static tiku_ttf_t *file_regular;
static tiku_ttf_t *file_bold;
static char file_family[64];

/** @brief Let go of every glyph a cache is holding. */
static void
cache_empty(tiku_face_src_t *src)
{
    int i;

    if (src == NULL) {
        return;
    }
    for (i = 0; i < CACHE_SLOTS; i++) {
        if (src->slot[i].used) {
            tiku_ttf_free_glyph(&src->slot[i].glyph);
            src->slot[i].used = 0;
        }
    }
    src->filled = 0;
}

/** @brief Give back what one built face holds. */
static void
built_free(built_face_t *b)
{
    if (b->src != NULL) {
        cache_empty(b->src);
        free(b->src);
    }
    if (b->hisrc != NULL) {
        cache_empty(b->hisrc);
        free(b->hisrc);
    }
    memset(b, 0, sizeof *b);
}

/** @brief Where @p cp lives, or would: open addressing, linear probe. */
static cache_slot_t *
cache_find(tiku_face_src_t *src, unsigned cp)
{
    unsigned h = (cp * 2654435761u) & (unsigned)(CACHE_SLOTS - 1);
    int step;

    for (step = 0; step < CACHE_SLOTS; step++) {
        cache_slot_t *slot = &src->slot[h];

        if (!slot->used || slot->cp == cp) {
            return slot;
        }
        h = (h + 1u) & (unsigned)(CACHE_SLOTS - 1);
    }
    return NULL;
}

/**
 * @brief The glyph for @p cp at this face's size, drawing it if need be.
 *
 * @return NULL when the file has no such letter.
 */
static const tiku_ttf_glyph_t *
cache_glyph(tiku_face_src_t *src, unsigned cp)
{
    cache_slot_t *slot;

    if (src == NULL || src->ttf == NULL) {
        return NULL;
    }
    /* A page of Chinese is thousands of letters, so past CACHE_CEILING
     * the cache is emptied whole; a redraw refills what it uses. */
    if (src->filled >= CACHE_CEILING) {
        cache_empty(src);
    }
    slot = cache_find(src, cp);
    if (slot == NULL) {
        return NULL;
    }
    if (slot->used) {
        return slot->present ? &slot->glyph : NULL;
    }
    memset(&slot->glyph, 0, sizeof slot->glyph);
    /*
     * Present and inkless is not absent: a space has an advance and
     * nothing to draw, and taken as absent its width would come from the
     * baked face.
     */
    slot->present = tiku_ttf_render(src->ttf, cp, src->px,
                                         &slot->glyph) ? 1 : 0;
    if (!slot->present) {
        memset(&slot->glyph, 0, sizeof slot->glyph);
    }
    slot->cp = cp;
    slot->used = 1;
    src->filled++;
    return slot->present ? &slot->glyph : NULL;
}

/**
 * @brief Build the face for @p ttf at @p px: metrics now, glyphs later.
 *
 * @return 1 when the face is usable.
 */
static int
built_make(built_face_t *b, const tiku_ttf_t *ttf, int px, int bold)
{
    tiku_face_src_t *src = calloc(1u, sizeof *src);
    int ascent = 0, height = 0;

    if (src == NULL) {
        return 0;
    }
    src->ttf = ttf;
    src->px = px;
    src->bold = bold;
    tiku_ttf_metrics(ttf, px, &ascent, &height);
    memset(b, 0, sizeof *b);
    b->src = src;
    b->px = px;
    b->bold = bold;
    b->live = 1;
    b->face.src = src;
    b->face.glyphs = NULL;
    b->face.bits = NULL;
    b->face.count = 0;          /* nothing baked: it all comes from src */
    b->face.first = 0;
    b->face.ascent = (ascent > 0) ? ascent : px;
    b->face.height = (height > 0) ? height : px + 2;

    /*
     * And the same face at twice the size, for a screen at scale 2.
     * Metrics only here: its glyphs are drawn the first time one is asked
     * for, as the 1x face's are.  Its advances are not used, since
     * tiku_text() steps the pen by the logical advance whichever face drew
     * the letter.  If it cannot be had, a scaled screen replicates the 1x
     * glyphs.
     */
    b->face.hi = NULL;
    b->hisrc = calloc(1u, sizeof *b->hisrc);
    if (b->hisrc != NULL) {
        int hi_ascent = 0, hi_height = 0;

        b->hisrc->ttf = ttf;
        b->hisrc->px = px * 2;
        b->hisrc->bold = bold;
        tiku_ttf_metrics(ttf, px * 2, &hi_ascent, &hi_height);
        b->hiface.src = b->hisrc;
        b->hiface.glyphs = NULL;
        b->hiface.bits = NULL;
        b->hiface.count = 0;
        b->hiface.first = 0;
        b->hiface.ascent = (hi_ascent > 0) ? hi_ascent : px * 2;
        b->hiface.height = (hi_height > 0) ? hi_height : px * 2 + 2;
        b->hiface.hi = NULL;    /* the ladder stops here */
        b->face.hi = &b->hiface;
    }
    return 1;
}

/** @brief The built face for @p px and @p bold, making it if need be. */
static const tiku_font_t *
built_face(int px, int bold)
{
    const tiku_ttf_t *src = (bold && file_bold != NULL) ? file_bold
                                                            : file_regular;
    int i, spare = -1;

    if (src == NULL) {
        return NULL;
    }
    for (i = 0; i < BUILT_MAX; i++) {
        if (built[i].live && built[i].px == px && built[i].bold == bold &&
            built[i].src != NULL && built[i].src->ttf == src) {
            built[i].stamp = ++built_clock;
            return &built[i].face;
        }
        if (!built[i].live && spare < 0) {
            spare = i;
        }
    }
    if (spare < 0) {
        /*
         * Every slot is taken, so the one asked for longest ago goes;
         * only one, since faces are handed out by address and a caller
         * may be holding two.  The pair in force, the face pinned by an
         * unfinished adopt() and the two most recently asked for are
         * skipped.
         */
        unsigned newest = 0, second = 0;

        for (i = 0; i < BUILT_MAX; i++) {
            if (built[i].stamp > newest) {
                second = newest;
                newest = built[i].stamp;
            } else if (built[i].stamp > second) {
                second = built[i].stamp;
            }
        }
        for (i = 0; i < BUILT_MAX; i++) {
            if (&built[i].face == built_pinned ||
                built[i].src == current_plain.src ||
                built[i].src == current_bold.src ||
                built[i].stamp >= second) {
                continue;
            }
            if (spare < 0 || built[i].stamp < built[spare].stamp) {
                spare = i;
            }
        }
        if (spare < 0) {
            return NULL;        /* all held: callers use a baked face */
        }
        built_free(&built[spare]);
    }
    if (!built_make(&built[spare], src, px, bold)) {
        return NULL;
    }
    built[spare].stamp = ++built_clock;
    return &built[spare].face;
}

/** @brief Whether the interface is drawn from files right now. */
static int
using_files(void)
{
    return file_regular != NULL;
}

#define FAMILY_COUNT ((int)(sizeof family_names / sizeof family_names[0]))
#define SIZE_COUNT   ((int)(sizeof face_sizes / sizeof face_sizes[0]))

/** @brief The rung nearest @p px, snapping down. */
static int
rung_of(int px)
{
    int i, best = 0;

    for (i = 1; i < SIZE_COUNT; i++) {
        if (px >= face_sizes[i]) {
            best = i;
        }
    }
    return best;
}

/** @brief Put family @p family at rung @p rung into the faces handed out. */
static void
adopt(int family, int rung)
{
    const tiku_font_t *plain = NULL, *bold = NULL;

    if (using_files()) {
        plain = built_face(face_sizes[rung], 0);
        built_pinned = plain;
        bold = built_face(face_sizes[rung], 1);
        built_pinned = NULL;
    }
    if (plain == NULL || bold == NULL) {
        plain = plain_faces[family][rung];
        bold = bold_faces[family][rung];
    }
    if (current_family != family) font_generation++;
    current_plain = *plain;
    current_bold = *bold;
    current_family = family;
    current_size = face_sizes[rung];
}

int
tiku_font_set_size(int px)
{
    adopt(current_family, rung_of(px));
    return current_size;
}

int
tiku_font_family_count(void)
{
    return FAMILY_COUNT;
}

const char *
tiku_font_family_name(int family)
{
    if (family < 0 || family >= FAMILY_COUNT) {
        return NULL;
    }
    return family_names[family];
}

int
tiku_font_set_family(int family)
{
    if (current_size == 0) {
        (void)tiku_font_set_size(12);
    }
    if (family >= 0 && family < FAMILY_COUNT) {
        adopt(family, rung_of(current_size));
    }
    return current_family;
}

unsigned tiku_font_generation(void) { return font_generation; }

int
tiku_font_family(void)
{
    return current_family;
}

const char *
tiku_font_current_family(void)
{
    if (using_files()) {
        return file_family;
    }
    return family_names[current_family];
}

int
tiku_font_set_files(const char *regular, const char *bold)
{
    tiku_ttf_t *r, *b = NULL;
    int i;

    if (regular == NULL) {
        return 0;
    }
    r = tiku_ttf_open(regular);
    if (r == NULL) {
        return 0;               /* what cannot be read is not adopted */
    }
    if (bold != NULL) {
        b = tiku_ttf_open(bold);
    }
    for (i = 0; i < BUILT_MAX; i++) {
        built_free(&built[i]);
    }
    tiku_ttf_close(file_regular);
    tiku_ttf_close(file_bold);
    font_generation++;
    file_regular = r;
    file_bold = b;
    snprintf(file_family, sizeof file_family, "%s",
             (tiku_ttf_family(r) != NULL) ? tiku_ttf_family(r)
                                               : "");
    if (current_size == 0) {
        current_size = 12;
    }
    adopt(current_family, rung_of(current_size));
    if (!using_files()) {
        return 0;
    }
    return 1;
}

void
tiku_font_use_baked(void)
{
    int i;

    if (using_files()) font_generation++;

    for (i = 0; i < BUILT_MAX; i++) {
        built_free(&built[i]);
    }
    tiku_ttf_close(file_regular);
    tiku_ttf_close(file_bold);
    file_regular = NULL;
    file_bold = NULL;
    file_family[0] = '\0';
    if (current_size == 0) {
        current_size = 12;
    }
    adopt(current_family, rung_of(current_size));
}

int
tiku_font_size(void)
{
    if (current_size == 0) {
        (void)tiku_font_set_size(12);
    }
    return current_size;
}

const tiku_font_t *
tiku_font_plain(void)
{
    if (current_size == 0) {
        (void)tiku_font_set_size(12);
    }
    return &current_plain;
}

const tiku_font_t *
tiku_font_at(int px)
{
    int rung;

    if (current_size == 0) {
        (void)tiku_font_set_size(12);
    }
    rung = rung_of(px);
    if (using_files()) {
        const tiku_font_t *face = built_face(face_sizes[rung], 0);

        if (face != NULL) {
            return face;
        }
    }
    return plain_faces[current_family][rung];
}

int
tiku_font_mono_px(void)
{
    /* Follows the interface size the user chose: the bigger the rest of
     * the desktop is, the bigger a terminal's characters are. */
    if (current_size == 0) {
        (void)tiku_font_set_size(12);
    }
    return current_size >= 14 ? 15 : 13;
}

const tiku_font_t *tiku_font_for_measure(int face)
{
    if (face >= TIKU_MEASURE_FONT_BASE && face < TIKU_MEASURE_FONT_BASE+4096) {
        unsigned family = ((unsigned)face >> 10) & 3u;
        unsigned cut = ((unsigned)face >> 8) & 3u;
        int pixels = face & 255;
        if (family == TIKU_MEASURE_FAMILY_MONO) {
            int doubled = !(pixels & 1) && pixels >= 18 && pixels <= 46;
            const tiku_font_t *f = tiku_font_styled(tiku_font_mono_at(
                                            doubled ? pixels/2 : pixels,0),cut);
            return doubled && f->hi ? f->hi : f;
        }
        (void)tiku_font_size();
        if (family == TIKU_MEASURE_FAMILY_CURRENT && using_files()) {
            const tiku_font_t *f = built_face(pixels,cut & 1u);
            if (f) return f;
        }
        int index = family == TIKU_MEASURE_FAMILY_CURRENT ? current_family :
                    family == TIKU_MEASURE_FAMILY_SERIF ? 1 : 0;
        int doubled = pixels >= 20;
        int rung = rung_of(doubled ? pixels/2 : pixels);
        const tiku_font_t *f = cut & TIKU_FONT_BOLD ? bold_faces[index][rung] :
                                                   plain_faces[index][rung];
        return doubled && f->hi ? f->hi : f;
    }
    if (face == TIKU_MEASURE_BOLD) return tiku_font_bold();
    if (face == TIKU_MEASURE_MONO) return tiku_font_mono(0);
    return face >= 10 ? tiku_font_at(face) : tiku_font_plain();
}

const tiku_font_t *
tiku_font_mono(int bold)
{
    return tiku_font_mono_at(tiku_font_mono_px(), bold);
}

const tiku_font_t *
tiku_font_mono_at(int px, int bold)
{
    int i, best = 0, n = (int)(sizeof mono_sizes / sizeof mono_sizes[0]);

    for (i = 1; i < n; i++) {
        if (px >= mono_sizes[i]) {
            best = i;
        }
    }
    return bold ? monobold_faces[best] : mono_faces[best];
}

int
tiku_font_mono_cell(int bold)
{
    return tiku_text_width(tiku_font_mono(bold), "M");
}

/** @brief How many mono cuts there are, numbered as their TIKU_FONT_*
 *         bits: regular, bold, italic, bold italic. */
#define MONO_CUTS  4u

/** @brief Rung @p i's mono face in the cut @p style names. */
static const tiku_font_t *
mono_cut(int i, unsigned style)
{
    switch (style) {
    case TIKU_FONT_BOLD:                    return monobold_faces[i];
    case TIKU_FONT_ITALIC:                  return monoitalic_faces[i];
    case TIKU_FONT_BOLD | TIKU_FONT_ITALIC: return monobolditalic_faces[i];
    default:                                return mono_faces[i];
    }
}

const tiku_font_t *
tiku_font_styled(const tiku_font_t *f, unsigned style)
{
    int i, family, n = (int)(sizeof mono_sizes / sizeof mono_sizes[0]);

    if (f == NULL || (style & ~(TIKU_FONT_BOLD | TIKU_FONT_ITALIC)) != 0) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        if (f == mono_faces[i] || f == monobold_faces[i] ||
            f == monoitalic_faces[i] || f == monobolditalic_faces[i]) {
            return mono_cut(i, style);
        }
    }
    if (style & TIKU_FONT_ITALIC) {
        return NULL;
    }
    if (f == &current_plain || f == &current_bold) {
        return style ? &current_bold : &current_plain;
    }
    for (family = 0; family < FAMILY_COUNT; family++) {
        for (i = 0; i < SIZE_COUNT; i++) {
            if (f == plain_faces[family][i] || f == bold_faces[family][i]) {
                return style ? bold_faces[family][i] : plain_faces[family][i];
            }
        }
    }
    return NULL;
}

const tiku_font_t *
tiku_font_bold(void)
{
    if (current_size == 0) {
        (void)tiku_font_set_size(12);
    }
    return &current_bold;
}

/**
 * @brief The next code point in @p text, stepping @p text past it.
 *
 * Text is UTF-8 (a file called "caf\u00e9" is five bytes and four
 * letters), so the text path walks code points, not bytes.  A malformed
 * sequence ends at the first byte that does not continue it, and never
 * past the terminator.
 */
static unsigned
utf8_next(const char **text)
{
    const unsigned char *p = (const unsigned char *)*text;
    unsigned cp = *p;
    int extra;

    if (cp < 0x80u) {
        extra = 0;
    } else if ((cp & 0xE0u) == 0xC0u) {
        cp &= 0x1Fu; extra = 1;
    } else if ((cp & 0xF0u) == 0xE0u) {
        cp &= 0x0Fu; extra = 2;
    } else if ((cp & 0xF8u) == 0xF0u) {
        cp &= 0x07u; extra = 3;
    } else {
        extra = 0;              /* a stray continuation byte stands alone */
    }
    while (extra-- > 0 && (p[1] & 0xC0u) == 0x80u) {
        cp = (cp << 6) | (unsigned)(*++p & 0x3Fu);
    }
    *text = (const char *)(p + 1);
    return cp;
}

typedef struct face_glyph face_glyph_t;
static int face_glyph(const tiku_font_t *f, unsigned cp,
                      face_glyph_t *out);

/** @brief The baked face of this size and weight in the current family. */
static const tiku_font_t *
baked_face(int px, int bold)
{
    int rung = rung_of(px);

    return bold ? bold_faces[current_family][rung]
                : plain_faces[current_family][rung];
}

/** @brief One glyph, whether it was baked or drawn a moment ago. */
struct face_glyph {
    int                  adv;
    int                  w, h, ox, oy;
    const unsigned char *cover;
    int                  times;     /* copies of the ink drawn, 1 or more */
    int                  step;      /* logical pixels between the copies */
};

/*
 * Punctuation past the baked range, drawn from a letter the face carries:
 * the ellipsis as three periods, the dashes from the hyphen, curly quotes
 * as straight ones and the bullet as a middle dot.  A joined stand-in
 * places its copies the base's inked width apart, less a column, so they
 * meet.
 */
static const struct stand_in {
    unsigned cp;
    unsigned base;
    int      times;
    int      joined;
} stand_ins[] = {
    { 0x2013u, '-', 1, 0 },     /* en dash */
    { 0x2014u, '-', 2, 1 },     /* em dash */
    { 0x2018u, '\'', 1, 0 },
    { 0x2019u, '\'', 1, 0 },
    { 0x201Cu, '"', 1, 0 },
    { 0x201Du, '"', 1, 0 },
    { 0x2022u, 0xB7u, 1, 0 },   /* bullet */
    { 0x2026u, '.', 3, 0 },     /* horizontal ellipsis */
};

/** @brief How many columns of @p g's coverage carry ink: its width
 *         without the blank columns at either side. */
static int
inked_width(const tiku_font_t *f, const tiku_glyph_t *g)
{
    const unsigned char *cover = f->bits + g->off;
    int x, y, lo = g->w, hi = -1;

    for (y = 0; y < g->h; y++) {
        for (x = 0; x < g->w; x++) {
            if (cover[(long)y * g->w + x] != 0u) {
                lo = (x < lo) ? x : lo;
                hi = (x > hi) ? x : hi;
            }
        }
    }
    return (hi >= lo) ? hi - lo + 1 : g->w;
}

/** @brief The stand-in for @p cp in baked face @p f: a rule whose letter
 *         @p f carries, or NULL. */
static const struct stand_in *
stand_in_of(const tiku_font_t *f, unsigned cp)
{
    size_t i;

    for (i = 0; i < sizeof stand_ins / sizeof stand_ins[0]; i++) {
        long b = (long)stand_ins[i].base - (long)f->first;

        if (stand_ins[i].cp == cp) {
            return (b >= 0 && b < (long)f->count) ? &stand_ins[i] : NULL;
        }
    }
    return NULL;
}

/**
 * @brief Fill @p out with @p cp from @p f; punctuation a baked face lacks
 *        is drawn from the letter stand_ins names for it.
 *
 * @return 1 when the face has that letter or its stand-in; 0 when it does
 *         not, with @p out holding the face's first glyph, or an empty one
 *         a third of the height wide.
 */
static int
face_glyph(const tiku_font_t *f, unsigned cp, face_glyph_t *out)
{
    out->times = 1;
    out->step = 0;

    if (f->src != NULL) {
        const tiku_ttf_glyph_t *g = cache_glyph(f->src, cp);

        if (g == NULL) {
            /*
             * Not in the file, so the baked face of the same size and
             * weight answers: a font dropped in for one script leaves the
             * other scripts in the baked face.
             */
            const tiku_font_t *baked = baked_face(f->src->px,
                                                       f->src->bold);

            if (baked != NULL && baked->glyphs != NULL) {
                return face_glyph(baked, cp, out);
            }
            memset(out, 0, sizeof *out);
            out->adv = f->height / 3 + 1;
            return 0;
        }
        out->adv = g->adv;
        out->w = g->w;
        out->h = g->h;
        out->ox = g->ox;
        out->oy = g->oy;
        out->cover = g->cover;      /* NULL for a glyph with no ink */
        return 1;
    }
    if (f->glyphs == NULL || f->count <= 0) {
        memset(out, 0, sizeof *out);
        out->adv = f->height / 3 + 1;
        return 0;               /* a face with nothing in it draws nothing */
    }
    {
        long i = (long)cp - (long)f->first;
        int have = (i >= 0 && i < (long)f->count);
        const struct stand_in *si = have ? NULL : stand_in_of(f, cp);
        const tiku_glyph_t *g;

        if (si != NULL) {
            i = (long)si->base - (long)f->first;
            have = 1;
        }
        g = &f->glyphs[have ? i : 0];
        out->adv = g->adv;
        out->w = g->w;
        out->h = g->h;
        out->ox = g->ox;
        out->oy = g->oy;
        out->cover = f->bits + g->off;
        if (si != NULL) {
            int inked = inked_width(f, g);

            out->times = si->times;
            out->step = si->joined ? ((inked > 2) ? inked - 1 : inked)
                                   : g->adv;
            out->adv += (si->times - 1) * out->step;
        }
        return have;
    }
}

/** @brief Whether @p f carries @p cp at all, itself or by a stand-in. */
static int
face_has(const tiku_font_t *f, unsigned cp)
{
    if (f->src != NULL) {
        return cache_glyph(f->src, cp) != NULL;
    }
    {
        long i = (long)cp - (long)f->first;

        return (i >= 0 && i < (long)f->count) || stand_in_of(f, cp) != NULL;
    }
}

int
tiku_text_width(const tiku_font_t *f, const char *text)
{
    int w = 0;

    if (f == NULL || text == NULL) {
        return 0;
    }
    while (*text != '\0') {
        face_glyph_t g;

        (void)face_glyph(f, utf8_next(&text), &g);
        w += g.adv;
    }
    return w;
}

int
tiku_text_advance(const tiku_font_t *f, unsigned cp)
{
    face_glyph_t g;

    if (f == NULL) {
        return 0;
    }
    (void)face_glyph(f, cp, &g);
    return g.adv;
}

int
tiku_text_height(const tiku_font_t *f)
{
    return (f != NULL) ? f->height : 0;
}

/** @brief Blend @p c over the native pixel at (x,y), coverage @p a. */
static void
blend(tiku_surface_t *s, int sc, int x, int y, tiku_rgb_t c,
      unsigned a)
{
    tiku_rgb_t d;
    unsigned dr, dg, db, sr, sg, sb;
    long stride = (long)s->w * sc;

    if (a == 0u ||
        x < s->clip.x * sc || y < s->clip.y * sc ||
        x >= (s->clip.x + s->clip.w) * sc ||
        y >= (s->clip.y + s->clip.h) * sc) {
        return;
    }
    if (a >= 255u) {
        s->px[(long)y * stride + x] = c;
        return;
    }
    d = s->px[(long)y * stride + x];
    dr = (d >> 16) & 0xFFu; dg = (d >> 8) & 0xFFu; db = d & 0xFFu;
    sr = (c >> 16) & 0xFFu; sg = (c >> 8) & 0xFFu; sb = c & 0xFFu;
    dr = (sr * a + dr * (255u - a)) / 255u;
    dg = (sg * a + dg * (255u - a)) / 255u;
    db = (sb * a + db * (255u - a)) / 255u;
    s->px[(long)y * stride + x] = TIKU_RGB(dr, dg, db);
}

/**
 * @brief How a stream names the face @p f, or -1 when it cannot.
 *
 * One byte: 0 to 5 are the six faces the interface is drawn in at whatever
 * size that is, TIKU_DL_MONO_SIZED onward is a mono face at its own size,
 * and any other larger value is a plain face at that many pixels (10, 12,
 * 14 or 16), which is how a status line drawn smaller than the body says
 * so.  -1 is a face this cannot name, one read from a dropped file; the
 * caller then marks a miss.
 */
static int
face_id(const tiku_font_t *f)
{
    int i;

    if (f == tiku_font_bold()) {
        return TIKU_DL_BOLD;
    }
    if (f == tiku_font_mono(0)) {
        return TIKU_DL_MONO;
    }
    if (f == tiku_font_mono(1)) {
        return TIKU_DL_MONO_BOLD;
    }
    if (f == tiku_font_styled(tiku_font_mono(0), TIKU_FONT_ITALIC)) {
        return TIKU_DL_MONO_ITALIC;
    }
    if (f == tiku_font_styled(tiku_font_mono(0),
                              TIKU_FONT_BOLD | TIKU_FONT_ITALIC)) {
        return TIKU_DL_MONO_BOLD_ITALIC;
    }
    if (f == tiku_font_plain()) {
        return TIKU_DL_PLAIN;
    }
    for (i = 0; i < (int)(sizeof mono_sizes / sizeof mono_sizes[0]); i++) {
        unsigned style;

        for (style = 0; style < MONO_CUTS; style++) {
            if (f == mono_cut(i, style)) {
                return TIKU_DL_MONO_SIZED + i * (int)MONO_CUTS + (int)style;
            }
        }
    }
    for (i = 0; i < SIZE_COUNT; i++) {
        if (face_sizes[i] > TIKU_DL_MONO_BOLD_ITALIC &&
            tiku_font_at(face_sizes[i]) == f) {
            return face_sizes[i];
        }
    }
    return -1;
}

const tiku_font_t *
tiku_font_mono_stream(unsigned id)
{
    unsigned n = (unsigned)(sizeof mono_sizes / sizeof mono_sizes[0]);

    if (id < TIKU_DL_MONO_SIZED || id >= TIKU_DL_MONO_SIZED + MONO_CUTS * n) {
        return NULL;
    }
    id -= TIKU_DL_MONO_SIZED;
    return mono_cut((int)(id / MONO_CUTS), id % MONO_CUTS);
}

void
tiku_text(tiku_surface_t *s, const tiku_font_t *f, int x, int y,
               const char *text, tiku_rgb_t c)
{
    const tiku_font_t *hi;
    int sc, pen;
    int rec;

    if (s == NULL || f == NULL || text == NULL) {
        return;
    }
    rec = tiku_gfx_rec_enter(s);
    if (rec) {
        int id = face_id(f);

        if (id < 0) {
            tiku_dl_miss(s->record);
        } else {
            (void)tiku_dl_text(s->record, (tiku_dl_face_t)id, x, y, text, c);
        }
    }
    sc = (s->scale > 1) ? s->scale : 1;
    /* An even scale draws the 2x face, replicated half as much.  The pen
     * still steps by the logical advance, so the text fills the space
     * its layout measured. */
    hi = (sc > 1 && (sc & 1) == 0) ? f->hi : NULL;
    pen = x * sc;
    while (*text != '\0') {
        unsigned cp = utf8_next(&text);
        face_glyph_t logical, ink;
        int gx, gy, rx, ry, k, rep = sc;

        (void)face_glyph(f, cp, &logical);
        ink = logical;
        /* A letter the 2x face does not carry is drawn from the 1x face,
         * replicated. */
        if (hi != NULL && face_has(hi, cp)) {
            (void)face_glyph(hi, cp, &ink);
            rep = sc / 2;
        }

        for (k = 0; k < ink.times; k++) {
            int at = pen + k * ink.step * rep;

            for (gy = 0; ink.cover != NULL && gy < ink.h; gy++) {
                const unsigned char *row = ink.cover + (long)gy * ink.w;

                for (gx = 0; gx < ink.w; gx++) {
                    int nx = at + (ink.ox + gx) * rep;
                    int ny = y * sc + (ink.oy + gy) * rep;

                    for (ry = 0; ry < rep; ry++) {
                        for (rx = 0; rx < rep; rx++) {
                            blend(s, sc, nx + rx, ny + ry, c, row[gx]);
                        }
                    }
                }
            }
        }
        /* Always the logical advance: what the layout was measured
         * against, whichever face happened to draw the letter. */
        pen += logical.adv * sc;
    }
    tiku_gfx_rec_leave(s, rec);
}

int
tiku_text_centered(tiku_surface_t *s, const tiku_font_t *f,
                        tiku_rect_t r, const char *text,
                        tiku_rgb_t c)
{
    int tw = tiku_text_width(f, text);
    int x = r.x + (r.w - tw) / 2;
    /* Centre ascent plus descent in r, with no line gap: R5 labels sit
     * centred on their ink. */
    int y = r.y + (r.h - f->height) / 2 + f->ascent;
    int rec = tiku_gfx_rec_enter(s);

    if (rec) {
        int id = face_id(f);

        if (id < 0) {
            tiku_dl_miss(s->record);
        } else {
            (void)tiku_dl_text_centered(s->record, (tiku_dl_face_t)id, r,
                                        text, c);
        }
    }
    tiku_text(s, f, x, y, text, c);
    tiku_gfx_rec_leave(s, rec);
    return x;
}
