/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_office_font.c - the faces one document is set in.
 *
 * A family, weight and slant asked for is found once (tiku_fontset.h)
 * and opened once, its line measured in font units.  A character the
 * face has no glyph for goes to a face that has one, found for its
 * script; each face and character's fallback is found once and kept.
 * Faces here are one document's: nothing is shared between documents,
 * so workers need no locks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief A character a face lacks, and the face and glyph drawing it. */
struct of_fall {
    int      face;                  /* asked of; -1 marks an empty slot  */
    uint32_t cp;                    /* the character                     */
    int      to;                    /* the face that draws it            */
    unsigned gid;                   /* its glyph there, 0 for none       */
};

/** @brief Room for one face more.  @return it, cleared, or NULL. */
static of_face_t *
face_room(of_faces_t *fs)
{
    if (fs->nface == fs->facecap) {
        int cap = fs->facecap ? fs->facecap * 2 : 16;
        of_face_t *g = realloc(fs->face, (size_t)cap * sizeof *g);

        if (g == NULL) {
            return NULL;
        }
        fs->face = g;
        fs->facecap = cap;
    }
    memset(&fs->face[fs->nface], 0, sizeof fs->face[0]);
    return &fs->face[fs->nface];
}

/** @brief Face @p f found under @p key, opened and measured.  @return
 *         its index, or -1 when it cannot be opened. */
static int
open_face(of_faces_t *fs, const tiku_fontset_face_t *f, const char *key)
{
    of_face_t *o = face_room(fs);

    if (o == NULL) {
        return -1;
    }
    o->f = *f;
    o->pdf = -1;
    snprintf(o->key, sizeof o->key, "%s", key);
    o->ttf = tiku_ttf_open_face(f->path, (unsigned)f->index);
    if (o->ttf == NULL) {
        return -1;
    }
    o->upem = tiku_ttf_upem(o->ttf);
    tiku_ttf_line(o->ttf, &o->ascent, &o->height);
    tiku_ttf_lines(o->ttf, &o->under, &o->under_size, &o->strike,
                   &o->strike_size);
    if (o->height <= 0 || o->height > o->upem * 5 / 2) {
        o->ascent = o->upem * 95 / 100;     /* a maths face's line is made */
        o->height = o->upem * 117 / 100;    /* for stacks: set text's own  */
    }
    return fs->nface++;
}

int
of_face(of_faces_t *fs, const char *family, int bold, int italic)
{
    tiku_fontset_face_t f;
    char key[sizeof fs->face[0].key];
    int i;

    snprintf(key, sizeof key, "%s|%d|%d", family ? family : "", bold != 0,
             italic != 0);
    for (i = 0; i < fs->nface; i++) {
        if (strcmp(fs->face[i].key, key) == 0) {
            return i;
        }
    }
    if (tiku_fontset_find(family, bold ? 700 : 400, italic, &f) != 0) {
        return -1;
    }
    return open_face(fs, &f, key);
}

/** @brief The face @p f, as a fallback for text asked bold and slanted as
 *         @p like was: found or opened.  @return its index, or -1. */
static int
fallback_face(of_faces_t *fs, const tiku_fontset_face_t *f,
              const of_face_t *like)
{
    char key[sizeof fs->face[0].key];
    int i, bold = like->f.weight >= 600 || like->f.bold_synth;
    int italic = like->f.italic || like->f.italic_synth;
    tiku_fontset_face_t g = *f;

    snprintf(key, sizeof key, "#%s#%d|%d|%d", f->path, f->index, bold,
             italic);
    for (i = 0; i < fs->nface; i++) {
        if (strcmp(fs->face[i].key, key) == 0) {
            return i;
        }
    }
    g.bold_synth = bold && g.weight < 600;
    g.italic_synth = italic && !g.italic;
    return open_face(fs, &g, key);
}

/** @brief The slot for (@p face, @p cp) in the table of fallbacks: its
 *         entry, or the empty one it would go in. */
static struct of_fall *
fall_slot(of_faces_t *fs, int face, uint32_t cp)
{
    unsigned k = (cp * 2654435761u ^ (unsigned)face * 40503u) &
                 (unsigned)(fs->fallcap - 1);

    while (fs->fall[k].face >= 0 &&
           (fs->fall[k].face != face || fs->fall[k].cp != cp)) {
        k = (k + 1) & (unsigned)(fs->fallcap - 1);
    }
    return &fs->fall[k];
}

/** @brief The table of fallbacks twice as large.  @return 0, or -1. */
static int
fall_grow(of_faces_t *fs)
{
    struct of_fall *old = fs->fall;
    int oldcap = fs->fallcap, cap = oldcap ? oldcap * 2 : 256, i;

    if ((fs->fall = malloc((size_t)cap * sizeof *fs->fall)) == NULL) {
        fs->fall = old;
        return -1;
    }
    fs->fallcap = cap;
    for (i = 0; i < cap; i++) {
        fs->fall[i].face = -1;
    }
    for (i = 0; i < oldcap; i++) {
        if (old[i].face >= 0) {
            *fall_slot(fs, old[i].face, old[i].cp) = old[i];
        }
    }
    free(old);
    return 0;
}

int
of_face_for(of_faces_t *fs, int face, uint32_t cp, unsigned *gid)
{
    of_face_t *f = &fs->face[face];
    tiku_fontset_face_t found;
    struct of_fall *slot;
    char family[128];
    const char *bar;
    int to;

    *gid = tiku_ttf_glyph(f->ttf, cp);
    if (*gid != 0 || cp < 0x20 || (cp >= 0x7f && cp < 0xa0)) {
        return face;
    }
    if ((fs->nfall + 1) * 2 > fs->fallcap && fall_grow(fs) != 0) {
        return face;
    }
    slot = fall_slot(fs, face, cp);
    if (slot->face < 0) {
        bar = strchr(f->key, '|');
        snprintf(family, sizeof family, "%.*s", bar ? (int)(bar - f->key)
                 : 0, f->key);
        to = tiku_fontset_cover(cp, family, f->f.weight, f->f.italic,
                                &found) == 0
             ? fallback_face(fs, &found, &fs->face[face]) : -1;
        f = &fs->face[face];        /* the faces may have moved */
        slot->face = face;
        slot->cp = cp;
        slot->to = to >= 0 ? to : face;
        slot->gid = to >= 0 ? tiku_ttf_glyph(fs->face[to].ttf, cp) : 0;
        fs->nfall++;
    }
    *gid = slot->gid;
    return slot->to;
}

float
of_face_advance(const of_faces_t *fs, int face, unsigned gid, float size)
{
    const of_face_t *f = &fs->face[face];

    return (float)tiku_ttf_advance(f->ttf, gid) * size / (float)f->upem;
}

void
of_faces_unwritten(of_faces_t *fs)
{
    int i;

    for (i = 0; i < fs->nface; i++) {
        fs->face[i].pdf = -1;
    }
}

void
of_faces_free(of_faces_t *fs)
{
    int i;

    for (i = 0; i < fs->nface; i++) {
        tiku_ttf_close(fs->face[i].ttf);
    }
    free(fs->face);
    free(fs->fall);
    memset(fs, 0, sizeof *fs);
}
