/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_fontset.h - the machine's faces, found by the names documents use.
 *
 * A document names its fonts: "Calibri", "Times New Roman", "ＭＳ 明朝".
 * tiku_fontset_find() answers with that family's face nearest the weight
 * and slant asked, else a stand-in with the same widths (Carlito for
 * Calibri), else a face of the same kind, and marks what the face lacks
 * for synthesis; tiku_fontset_cover() finds a face for a character.
 * Faces come from the folders given, the fonts folder and the system's
 * folders, a Mac Office's own among them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_FONTSET_H_
#define TIKU_FONTSET_H_

#include <stdint.h>

/** @brief Room for a face's path, its terminator included. */
#define TIKU_FONTSET_PATH 1024

/** @brief A face found, and what drawing with it has to make up. */
typedef struct {
    char path[TIKU_FONTSET_PATH]; /* the file                          */
    int index; /**< collection face in low bits; named instance in bits 16..30
                */
    char family[64];    /* the family it is                  */
    int weight, italic; /* the face's own: 100 to 900, slant */
    int bold_synth;     /* heavier asked than it is: embolden */
    int italic_synth;   /* a slant asked it has not: shear   */
    int exact;          /* the family asked for, not a stand-in */
    int cff;            /* PostScript outlines, not TrueType */
} tiku_fontset_face_t;

/**
 * @brief Look in @p dirs (folders joined by ':') first, then in the fonts
 *        folder (tiku_fonts_dir()) and the system's folders.
 *        $TIKU_FONTSET_ONLY, when set, names the only folders looked in.
 * @note Call before the first search: the faces are found once, by the
 *       first call that needs them.
 */
void tiku_fontset_dirs(const char *dirs);

/** @brief How many faces were found; the first call of this module that
 *         needs them looks for them. */
int tiku_fontset_count(void);

/** @brief Copy indexed face metadata; zero succeeds and -1 clears output.
 *  @note Index is 0..count-1. Enumeration uses the immutable discovered index;
 *        faces retain their source family/variant, with no substitution. */
int tiku_fontset_at(int index, tiku_fontset_face_t *out);

/**
 * @brief The face for @p family (as a document names it), @p weight (400
 *        regular, 700 bold) and @p italic: the family's own, a stand-in
 *        with its widths, or one of its kind -- serif, sans, monospaced --
 *        and failing those the nearest face of any family.
 * @return 0 with @p out filled; -1 when the machine has no faces at all.
 */
int tiku_fontset_find(const char *family, int weight, int italic,
                      tiku_fontset_face_t *out);

/**
 * @brief A face that draws @p cp, when the one @p family found does not:
 *        of the character's script first, then of the family's kind,
 *        then any upright face.  @return 0 with @p out filled, -1 when
 *        none has it.
 */
int tiku_fontset_cover(uint32_t cp, const char *family, int weight, int italic,
                       tiku_fontset_face_t *out);

/** @brief Whether @p family reads as monospaced (1), serif (2), or
 *         neither (0), by its name or the face found for it. */
int tiku_fontset_kind(const char *family);

#endif /* TIKU_FONTSET_H_ */
