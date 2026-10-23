/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_glyph.c - from a code in a string to ink: which program the
 * font carries, which glyph the code reaches in it, and its coverage.
 *
 * The three programs a PDF embeds -- TrueType, CFF and Type 1 -- are
 * read by the interface kit; here is the map from a code to a glyph in
 * each (ISO 32000 9.6.6 and 9.7.4), and a stand-in face for a font the
 * file names without carrying, drawn by the characters the text layer
 * already worked out.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_private.h"
#include "tiku_fontdir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FONT_MAX (16L * 1024L * 1024L)

/* Faces on macOS and Linux tried when the fonts folder has none: sans,
 * serif and fixed, regular and bold. */
static const char *const fallback[][2] = {
    {"sans", "/System/Library/Fonts/Supplemental/Arial.ttf"},
    {"sans-bold", "/System/Library/Fonts/Supplemental/Arial Bold.ttf"},
    {"serif", "/System/Library/Fonts/Supplemental/Times New Roman.ttf"},
    {"serif-bold",
     "/System/Library/Fonts/Supplemental/Times New Roman Bold.ttf"},
    {"mono", "/System/Library/Fonts/Supplemental/Courier New.ttf"},
    {"mono-bold", "/System/Library/Fonts/Supplemental/Courier New Bold.ttf"},
    {"sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"},
    {"sans-bold", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"},
    {"serif", "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"},
    {"serif-bold", "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf"},
    {"mono", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"},
    {"mono-bold", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"},
    {"sans", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"},
    {"serif",
     "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf"},
    {"mono",
     "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf"},
    {"sans", "/usr/share/fonts/dejavu/DejaVuSans.ttf"},
    {"serif", "/usr/share/fonts/dejavu/DejaVuSerif.ttf"},
    {"mono", "/usr/share/fonts/dejavu/DejaVuSansMono.ttf"},
};

/* Faces for Chinese, Japanese and Korean, by language and look; "any"
 * serves every one.  A collection's face follows a #: Noto CJK keeps
 * JP, KR, SC and TC in that order. */
#define NOTO "/usr/share/fonts/opentype/noto/"
static const char *const cjk_fallback[][2] = {
    {"jp", "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc"},
    {"jp-serif", "/System/Library/Fonts/ヒラギノ明朝 ProN.ttc"},
    {"sc", "/System/Library/Fonts/Hiragino Sans GB.ttc"},
    {"sc", "/System/Library/Fonts/STHeiti Light.ttc#1"},
    {"sc-serif", "/System/Library/Fonts/Supplemental/Songti.ttc#6"},
    {"tc", "/System/Library/Fonts/STHeiti Light.ttc"},
    {"tc-serif", "/System/Library/Fonts/Supplemental/Songti.ttc#7"},
    {"kr", "/System/Library/Fonts/AppleSDGothicNeo.ttc"},
    {"kr", "/System/Library/Fonts/Supplemental/AppleGothic.ttf"},
    {"kr-serif", "/System/Library/Fonts/Supplemental/AppleMyungjo.ttf"},
    {"jp", NOTO "NotoSansCJK-Regular.ttc"},
    {"kr", NOTO "NotoSansCJK-Regular.ttc#1"},
    {"sc", NOTO "NotoSansCJK-Regular.ttc#2"},
    {"tc", NOTO "NotoSansCJK-Regular.ttc#3"},
    {"jp-serif", NOTO "NotoSerifCJK-Regular.ttc"},
    {"kr-serif", NOTO "NotoSerifCJK-Regular.ttc#1"},
    {"sc-serif", NOTO "NotoSerifCJK-Regular.ttc#2"},
    {"tc-serif", NOTO "NotoSerifCJK-Regular.ttc#3"},
    {"any", "/System/Library/Fonts/Supplemental/Arial Unicode.ttf"},
    {"any", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc"},
    {"any", "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc"},
    {"any", "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf"},
    {"any", "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc"},
};
#undef NOTO

void
tiku_pdf_fonts(tiku_pdf_t *doc, const char *dir)
{
    if (doc != NULL) {
        snprintf(doc->fontdir, sizeof doc->fontdir, "%s", dir ? dir : "");
        memset(doc->subst_chosen, 0, sizeof doc->subst_chosen);
    }
}

/** @brief Split "file#face" into the file, in @p file, and the face. */
static unsigned
subst_face(const char *spec, char *file, size_t max)
{
    const char *hash = strrchr(spec, '#');

    snprintf(file, max, "%.*s", hash != NULL ? (int)(hash - spec)
                                             : (int)strlen(spec), spec);
    return hash != NULL ? (unsigned)strtoul(hash + 1, NULL, 10) : 0u;
}

/** @brief Whether the face @p spec names ("file" or "file#face") is a
 *         font this machine has. */
static int
subst_there(const char *spec)
{
    char file[512];

    (void)subst_face(spec, file, sizeof file);
    return tiku_ttf_is_font(file);
}

/** @brief The stand-in @p path names, opened once per document. */
static tiku_ttf_t *
subst_open(tiku_pdf_t *doc, const char *path)
{
    char file[512];
    unsigned face;
    int i;

    for (i = 0; i < doc->nsubst; i++) {
        if (strcmp(doc->subst_path[i], path) == 0) {
            return doc->subst[i];
        }
    }
    if (doc->nsubst == PDF_SUBST_MAX) {
        return NULL;
    }
    face = subst_face(path, file, sizeof file);
    doc->subst[doc->nsubst] = tiku_ttf_open_face(file, face);
    if (doc->subst[doc->nsubst] == NULL) {
        return NULL;
    }
    snprintf(doc->subst_path[doc->nsubst], sizeof doc->subst_path[0], "%s",
             path);
    return doc->subst[doc->nsubst++];
}

/** @brief Whether @p s has @p word in it, case aside. */
static int
has_word(const char *s, const char *word)
{
    size_t n = strlen(word), i;

    for (i = 0; s[i] != '\0'; i++) {
        size_t k;

        for (k = 0; k < n && s[i + k] != '\0'; k++) {
            int a = s[i + k], b = word[k];

            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
        }
        if (k == n) {
            return 1;
        }
    }
    return 0;
}

/** @brief Which stand-in @p base with @p flags asks for: sans, serif or
 *        fixed (0, 2, 4), plus one for bold. */
static int
subst_kind(const char *base, long flags)
{
    int serif = (flags & 2) != 0 || has_word(base, "Times") ||
                has_word(base, "Serif") || has_word(base, "Georgia") ||
                has_word(base, "Roman") || has_word(base, "Book");
    int mono = (flags & 1) != 0 || has_word(base, "Courier") ||
               has_word(base, "Mono");
    int bold = has_word(base, "Bold") || has_word(base, "Black") ||
               has_word(base, "Heavy") || ((flags & (1L << 18)) != 0);

    if (has_word(base, "Sans") || has_word(base, "Arial") ||
        has_word(base, "Helvetica")) {
        serif = 0;
    }
    return (mono ? 4 : serif ? 2 : 0) + bold;
}

/** @brief The family in the fonts folder for kind @p k, into @p out. */
static void
folder_pick(const char *dir, int k, char *out, size_t max)
{
    tiku_fontdir_t *folder = malloc(sizeof *folder);    /* paths: not stack */
    const tiku_fontdir_entry_t *pick = NULL;
    int n, i, mono = k >= 4, serif = k == 2 || k == 3;

    if (folder == NULL) {
        return;
    }
    n = tiku_fontdir_scan(folder, dir);
    for (i = 0; i < n && pick == NULL; i++) {
        const char *name = folder->font[i].name;
        int is_mono = has_word(name, "Mono") || has_word(name, "Courier");
        int is_serif = has_word(name, "Serif") && !has_word(name, "Sans");

        if ((mono && is_mono) || (!mono && serif && is_serif) ||
            (!mono && !serif && !is_mono && !is_serif)) {
            pick = &folder->font[i];
        }
    }
    if (pick == NULL && n > 0) {
        pick = &folder->font[0];
    }
    if (pick != NULL) {
        snprintf(out, max, "%s", (k & 1) && pick->bold[0] ? pick->bold
                                                          : pick->regular);
    }
    free(folder);
}

/**
 * @brief The file standing in for kind @p k: the fonts folder first, by
 *        family, then what the machine has.  Chosen once per document.
 */
static const char *
subst_file(tiku_pdf_t *doc, int k)
{
    static const char *const kinds[] = {"sans", "sans-bold", "serif",
                                        "serif-bold", "mono", "mono-bold"};
    char *pick = doc->subst_pick[k];
    size_t i, pass;

    if (doc->subst_chosen[k]) {
        return pick;
    }
    doc->subst_chosen[k] = 1;
    pick[0] = '\0';
    if (doc->fontdir[0] != '\0') {
        folder_pick(doc->fontdir, k, pick, sizeof doc->subst_pick[k]);
    }
    /* The kind asked for, then for bold its regular face. */
    for (pass = 0; pass < 2 && pick[0] == '\0'; pass++) {
        const char *want = kinds[pass == 0 ? k : k & ~1];

        for (i = 0; i < sizeof fallback / sizeof fallback[0]; i++) {
            if (strcmp(fallback[i][0], want) == 0 &&
                tiku_ttf_is_font(fallback[i][1])) {
                snprintf(pick, sizeof doc->subst_pick[k], "%s",
                         fallback[i][1]);
                break;
            }
        }
    }
    return pick;
}

/** @brief A face to stand in for @p base with @p flags, or NULL. */
static tiku_ttf_t *
substitute(tiku_pdf_t *doc, const char *base, long flags)
{
    const char *file = subst_file(doc, subst_kind(base, flags));

    return file[0] != '\0' ? subst_open(doc, file) : NULL;
}

/** @brief The language composite font name @p base says, by words the
 *         faces of four languages use: 0 JP, 1 SC, 2 TC, 3 KR; -1 for
 *         none. */
static int
cjk_named(const char *base)
{
    static const char *const words[4][9] = {
        {"Mincho", "Gothic", "Heisei", "Koz", "Hiragino", "MS-", "Meiryo",
         "Osaka", "Ryumin"},
        {"Sim", "STSong", "STHei", "STKai", "STFang", "YaHei", "FangSong",
         "KaiTi", "DengXian"},
        {"Ming", "MSung", "MHei", "JhengHei", "Kai", "DFKai", "", "", ""},
        {"Batang", "Gulim", "Dotum", "Gungsuh", "Malgun", "HYSMyeongJo",
         "HYGoThic", "Myeong", "Nanum"},
    };
    int lang, i;

    for (lang = 0; lang < 4; lang++) {
        for (i = 0; i < 9; i++) {
            if (words[lang][i][0] != '\0' && has_word(base, words[lang][i])) {
                return lang;
            }
        }
    }
    for (i = 0; base[i] != '\0'; i++) {
        if ((unsigned char)base[i] >= 0x80) {
            return 0;               /* a name in its own script: CJK */
        }
    }
    return -1;
}

/**
 * @brief Which CJK stand-in composite font @p font asks for: its
 *        language by the CIDs' collection, else its CMap's name, else its
 *        own name; then its look.  @return 0 to 7 (language * 2 + serif),
 *        or -1 when nothing says it is Chinese, Japanese or Korean.
 */
static int
cjk_kind(tiku_pdf_t *doc, pdf_obj_t *font, pdf_obj_t *cidfont,
         const char *base, long flags)
{
    pdf_obj_t *ord = pdf_get(doc, pdf_get(doc, cidfont, "CIDSystemInfo"),
                             "Ordering");
    pdf_obj_t *enc = pdf_get(doc, font, "Encoding");
    const char *e = enc != NULL && enc->kind == PDF_NAME
                    ? (const char *)enc->u.s.p : "";
    char o[16] = "";
    int lang;

    if (ord != NULL && ord->kind == PDF_STRING) {
        snprintf(o, sizeof o, "%.*s", (int)ord->u.s.n,
                 (const char *)ord->u.s.p);
    }
    lang = strncmp(o, "Japan", 5) == 0 ? 0 : strcmp(o, "GB1") == 0 ? 1 :
           strcmp(o, "CNS1") == 0 ? 2 :
           strcmp(o, "Korea1") == 0 || strcmp(o, "KR") == 0 ? 3 : -1;
    if (lang < 0) {
        lang = has_word(e, "GB") ? 1 :
               has_word(e, "CNS") || has_word(e, "B5") ||
               has_word(e, "HK") ? 2 :
               has_word(e, "KS") || has_word(e, "UHC") ||
               has_word(e, "AKR") ? 3 :
               has_word(e, "JIS") || has_word(e, "RKSJ") ||
               has_word(e, "EUC") ? 0 : cjk_named(base);
    }
    if (lang < 0) {
        return -1;
    }
    return lang * 2 + (((flags & 2) != 0 || has_word(base, "Min") ||
                        has_word(base, "Song") || has_word(base, "Sung") ||
                        has_word(base, "Batang") || has_word(base, "Myeong") ||
                        has_word(base, "Serif")) &&
                       !has_word(base, "Goth") && !has_word(base, "Sans"));
}

/** @brief The file standing in for CJK kind @p k: its language and look,
 *         then its language, then any face that has CJK at all. */
static const char *
cjk_file(tiku_pdf_t *doc, int k)
{
    static const char *const kinds[] = {"jp", "jp-serif", "sc", "sc-serif",
                                        "tc", "tc-serif", "kr", "kr-serif"};
    char *pick = doc->cjk_pick[k];
    size_t i, pass;

    if (doc->cjk_chosen[k]) {
        return pick;
    }
    doc->cjk_chosen[k] = 1;
    pick[0] = '\0';
    for (pass = 0; pass < 3 && pick[0] == '\0'; pass++) {
        const char *want = pass == 0 ? kinds[k] : pass == 1 ? kinds[k & ~1]
                                                            : "any";

        for (i = 0; i < sizeof cjk_fallback / sizeof cjk_fallback[0]; i++) {
            if (strcmp(cjk_fallback[i][0], want) == 0 &&
                subst_there(cjk_fallback[i][1])) {
                snprintf(pick, sizeof doc->cjk_pick[k], "%s",
                         cjk_fallback[i][1]);
                break;
            }
        }
    }
    return pick;
}

/** @brief A face to stand in for composite font @p font: CJK when it is
 *         Chinese, Japanese or Korean, else as a simple font's. */
static tiku_ttf_t *
substitute_cid(tiku_pdf_t *doc, pdf_obj_t *font, pdf_obj_t *cidfont)
{
    pdf_obj_t *base = pdf_get(doc, cidfont, "BaseFont");
    const char *name = base != NULL && base->kind == PDF_NAME
                       ? (const char *)base->u.s.p : "";
    long flags = pdf_int(pdf_get(doc, pdf_get(doc, cidfont,
                                              "FontDescriptor"), "Flags"), 0);
    int k = cjk_kind(doc, font, cidfont, name, flags);
    const char *file = k >= 0 ? cjk_file(doc, k) : "";

    if (file[0] != '\0') {
        return subst_open(doc, file);
    }
    return substitute(doc, name, flags);
}

/** @brief A Type 0 font's descendant, the font its glyphs are in. */
static pdf_obj_t *
descendant(tiku_pdf_t *doc, pdf_obj_t *font)
{
    pdf_obj_t *kids = pdf_get(doc, font, "DescendantFonts");

    return kids != NULL && kids->kind == PDF_ARRAY && kids->u.a.n > 0
           ? pdf_resolve(doc, kids->u.a.item[0]) : NULL;
}

int
pdf_standin_char_widths(tiku_pdf_t *doc, pdf_obj_t *font, double *width)
{
    pdf_obj_t *cidfont = descendant(doc, font);
    tiku_ttf_t *t = cidfont != NULL ? substitute_cid(doc, font, cidfont)
                                    : NULL;
    double em = t != NULL ? tiku_ttf_upem(t) : 0;
    long c;
    int any = 0;

    for (c = 0; em > 0 && c < 65536; c++) {
        unsigned gid = tiku_ttf_glyph(t, (unsigned)c);

        if (gid != 0) {
            width[c] = tiku_ttf_advance(t, gid) / em;
            any = 1;
        }
    }
    return any;
}

int
pdf_standin_widths(tiku_pdf_t *doc, pdf_obj_t *font, const uint32_t *uni,
                   double *width)
{
    pdf_obj_t *desc = pdf_get(doc, font, "FontDescriptor");
    pdf_obj_t *base = pdf_get(doc, font, "BaseFont");
    tiku_ttf_t *t;
    double em;
    int c, any = 0;

    if (pdf_get(doc, desc, "FontFile") || pdf_get(doc, desc, "FontFile2") ||
        pdf_get(doc, desc, "FontFile3")) {
        return 0;                   /* an embedded font gets no stand-in */
    }
    t = substitute(doc, base != NULL && base->kind == PDF_NAME
                        ? (const char *)base->u.s.p : "",
                   pdf_int(pdf_get(doc, desc, "Flags"), 0));
    em = t != NULL ? tiku_ttf_upem(t) : 0;
    for (c = 0; em > 0 && c < 256; c++) {
        unsigned gid = uni[c] != 0 ? tiku_ttf_glyph(t, uni[c]) : 0;

        if (gid != 0) {
            width[c] = tiku_ttf_advance(t, gid) / em;
            any = 1;
        }
    }
    return any;
}

/* --------------------------------------------------------------- loading  */

/** @brief The names a simple font's codes reach, from its Encoding. */
static void
load_names(tiku_pdf_t *doc, pdf_gfont_t *g, pdf_obj_t *font)
{
    pdf_obj_t *enc = pdf_get(doc, font, "Encoding");
    int c;
    const char *base = NULL;
    pdf_obj_t *diff = NULL;

    if (enc != NULL && enc->kind == PDF_NAME) {
        base = (const char *)enc->u.s.p;
    } else if (enc != NULL && enc->kind == PDF_DICT) {
        pdf_obj_t *b = pdf_get(doc, enc, "BaseEncoding");

        base = (b != NULL && b->kind == PDF_NAME) ? (const char *)b->u.s.p
                                                  : NULL;
        diff = pdf_get(doc, enc, "Differences");
    }
    g->names_from_pdf = enc != NULL;
    if (enc == NULL && g->kind == GF_TTF) {
        return;                     /* a TrueType face maps its own codes */
    }
    /* Standard names first: the text layer's characters name them. */
    for (c = 0; c < 256; c++) {
        uint32_t u;
        char name[32];

        if (g->uni == NULL || (base == NULL && g->kind == GF_TTF)) {
            break;                  /* nothing said, the face decides */
        }
        u = g->uni[c];
        if (u == 0) {
            continue;
        }
        if (u < 128 && ((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z'))) {
            snprintf(name, sizeof name, "%c", (int)u);
        } else if (u > 0 && u < 0x10000) {
            /* Named through the standard strings when one has this
             * character, else by uniXXXX, which some fonts use. */
            int k, hit = 0;

            for (k = 1; k < 391; k++) {
                const char *std = tiku_cff_standard_string((unsigned)k);

                if (pdf_glyph_unicode(std) == u) {
                    snprintf(name, sizeof name, "%s", std);
                    hit = 1;
                    break;
                }
            }
            if (!hit) {
                snprintf(name, sizeof name, "uni%04X", (unsigned)u);
            }
        } else {
            continue;
        }
        g->names[c] = strdup(name);
    }
    if (diff != NULL && diff->kind == PDF_ARRAY) {
        long code = 0;
        int i;

        for (i = 0; i < diff->u.a.n; i++) {
            pdf_obj_t *o = pdf_resolve(doc, diff->u.a.item[i]);

            if (o == NULL) {
                continue;
            }
            if (o->kind == PDF_INT || o->kind == PDF_REAL) {
                code = pdf_int(o, 0);
            } else if (o->kind == PDF_NAME && code >= 0 && code < 256) {
                free(g->names[code]);
                g->names[code] = strdup((const char *)o->u.s.p);
                code++;
            }
        }
    }
}

/** @brief The font program the descriptor carries, opened by its kind. */
static void
load_program(tiku_pdf_t *doc, pdf_gfont_t *g, pdf_obj_t *desc)
{
    pdf_obj_t *ff;
    const char *which[] = {"FontFile2", "FontFile3", "FontFile"};
    int i;

    for (i = 0; i < 3; i++) {
        long n = 0;

        ff = pdf_get(doc, desc, which[i]);
        if (ff == NULL || ff->kind != PDF_STREAM) {
            continue;
        }
        if (pdf_stream_data(doc, ff, &g->blob, &n, NULL) != 0) {
            continue;
        }
        g->bloblen = n;
        if (n > FONT_MAX || n < 4) {
            break;
        }
        if (i == 0 || (g->blob[0] == 'O' && memcmp(g->blob, "OTTO", 4) == 0) ||
            (g->blob[0] == 0 && g->blob[1] == 1) ||
            memcmp(g->blob, "true", 4) == 0) {
            g->ttf = tiku_ttf_open_mem(g->blob, (size_t)n);
            if (g->ttf != NULL) {
                g->kind = GF_TTF;
                g->owns_ttf = 1;
                g->upem = tiku_ttf_upem(g->ttf);
            }
        } else if (i == 1 || g->blob[0] == 1) {
            g->cff = tiku_cff_open(g->blob, (size_t)n);
            if (g->cff != NULL) {
                g->kind = GF_CFF;
                g->upem = tiku_cff_upem(g->cff);
            }
        } else {
            g->t1 = tiku_t1_open(g->blob, (size_t)n);
            if (g->t1 != NULL) {
                g->kind = GF_T1;
                g->upem = tiku_t1_upem(g->t1);
            }
        }
        if (g->kind != GF_NONE) {
            return;
        }
        free(g->blob);
        g->blob = NULL;
        g->bloblen = 0;
    }
}

/** @brief A CID font's CIDToGIDMap, when it is a stream. */
static void
load_cid2gid(tiku_pdf_t *doc, pdf_gfont_t *g, pdf_obj_t *cidfont)
{
    pdf_obj_t *map = pdf_get(doc, cidfont, "CIDToGIDMap");
    unsigned char *data = NULL;
    long n = 0, i;

    if (map == NULL || map->kind != PDF_STREAM ||
        pdf_stream_data(doc, map, &data, &n, NULL) != 0) {
        return;
    }
    g->ncid2gid = n / 2;
    g->cid2gid = malloc((size_t)(g->ncid2gid ? g->ncid2gid : 1) *
                        sizeof *g->cid2gid);
    if (g->cid2gid != NULL) {
        for (i = 0; i < g->ncid2gid; i++) {
            g->cid2gid[i] = (unsigned short)(data[2 * i] << 8 |
                                             data[2 * i + 1]);
        }
    } else {
        g->ncid2gid = 0;
    }
    free(data);
}

pdf_gfont_t *
pdf_gfont_load(tiku_pdf_t *doc, pdf_obj_t *dict, const pdf_tfont_t *tf)
{
    pdf_gfont_t *g = calloc(1, sizeof *g);
    pdf_obj_t *sub = pdf_get(doc, dict, "Subtype"), *desc, *base;
    pdf_obj_t *face = dict;
    long flags;

    if (g == NULL) {
        return NULL;
    }
    g->upem = 1000;
    g->uni = tf != NULL ? tf->uni : NULL;
    g->width = tf != NULL ? tf->width : NULL;
    g->two_byte = pdf_is_name(sub, "Type0");
    if (pdf_is_name(sub, "Type3")) {
        pdf_obj_t *m = pdf_get(doc, dict, "FontMatrix");

        g->kind = GF_T3;
        g->charprocs = pdf_get(doc, dict, "CharProcs");
        g->t3res = pdf_get(doc, dict, "Resources");
        g->fontmatrix = pdf_mat(0.001, 0, 0, 0.001, 0, 0);
        if (m != NULL && m->kind == PDF_ARRAY && m->u.a.n == 6) {
            double v[6];
            int i;

            for (i = 0; i < 6; i++) {
                v[i] = pdf_number(pdf_resolve(doc, m->u.a.item[i]), 0);
            }
            g->fontmatrix = pdf_mat(v[0], v[1], v[2], v[3], v[4], v[5]);
        }
        load_names(doc, g, dict);
        return g;
    }
    if (g->two_byte) {
        face = descendant(doc, dict);
        if (face == NULL) {
            face = dict;
        }
        load_cid2gid(doc, g, face);
        g->uni = NULL;              /* glyphs are CIDs, not codes */
        g->by_unicode = tf != NULL && tf->by_unicode;
    }
    desc = pdf_get(doc, face, "FontDescriptor");
    flags = pdf_int(pdf_get(doc, desc, "Flags"), 0);
    g->symbolic = (flags & 4) != 0 && (flags & 32) == 0;
    load_program(doc, g, desc);
    if (g->kind == GF_NONE) {
        base = pdf_get(doc, face, "BaseFont");
        g->ttf = g->two_byte ? substitute_cid(doc, dict, face) :
                 substitute(doc, base != NULL && base->kind == PDF_NAME
                            ? (const char *)base->u.s.p : "", flags);
        if (g->ttf != NULL) {
            g->kind = GF_TTF;
            g->substitute = 1;
            g->upem = tiku_ttf_upem(g->ttf);
        }
        if (g->ttf != NULL && g->two_byte && !g->by_unicode) {
            g->uni = g->own_uni = pdf_tfont_glyph_unicode(tf);
        }
    }
    if (!g->two_byte) {
        load_names(doc, g, dict);
    }
    return g;
}

void
pdf_gfont_free(pdf_gfont_t *g)
{
    int i;

    if (g == NULL) {
        return;
    }
    if (g->owns_ttf) {
        tiku_ttf_close(g->ttf);
    }
    tiku_cff_close(g->cff);
    tiku_t1_close(g->t1);
    free(g->blob);
    for (i = 0; i < 256; i++) {
        free(g->names[i]);
    }
    free(g->cid2gid);
    free(g->own_uni);
    free(g);
}

/* ---------------------------------------------------------------- glyphs  */

/** @brief A glyph number a name like g12, cid12, G12 or index12 carries. */
static long
numbered(const char *name)
{
    const char *p = name;

    while (*p && (*p < '0' || *p > '9')) {
        p++;
    }
    if (p == name || *p == '\0' ||
        (p - name != 1 && strncmp(name, "cid", 3) != 0 &&
         strncmp(name, "glyph", 5) != 0 && strncmp(name, "index", 5) != 0)) {
        return -1;
    }
    return strtol(p, NULL, 10);
}

/** @brief The glyph of a simple font's @p code in a TrueType face. */
static unsigned
ttf_glyph_of(const pdf_gfont_t *g, long code)
{
    unsigned gid = 0;
    uint32_t u = g->uni != NULL ? g->uni[code] : 0;

    if (g->substitute) {
        return u != 0 ? tiku_ttf_glyph(g->ttf, u) : 0;
    }
    if (g->symbolic || !g->names_from_pdf) {
        gid = tiku_ttf_glyph(g->ttf, (unsigned)code);
        if (gid == 0) gid = tiku_ttf_glyph(g->ttf, 0xF000u + (unsigned)code);
        if (gid == 0) gid = tiku_ttf_glyph(g->ttf, 0xF100u + (unsigned)code);
    }
    if (gid == 0 && u != 0) {
        gid = tiku_ttf_glyph(g->ttf, u);
    }
    if (gid == 0) {
        gid = tiku_ttf_glyph(g->ttf, (unsigned)code);
        if (gid == 0) gid = tiku_ttf_glyph(g->ttf, 0xF000u + (unsigned)code);
    }
    if (gid == 0 && tiku_ttf_glyph(g->ttf, 'A') == 0 &&
        tiku_ttf_glyph(g->ttf, 0xF041u) == 0) {
        gid = (unsigned)code;       /* no character map at all */
    }
    return gid;
}

/**
 * @brief A composite font's glyph @p key: a CID through CIDToGIDMap or
 *        the CFF's charset, or a character (a stand-in, or a font whose
 *        CMap's codes are Unicode) through the face's own map.
 */
static int
cid_outline(pdf_gfont_t *g, long key, const tiku_hint_t *hint,
            tiku_path_t *path, double *adv_units)
{
    unsigned gid = (unsigned)key;
    uint32_t u = g->by_unicode ? (uint32_t)key
                 : g->uni != NULL ? g->uni[key] : 0;

    if (g->cid2gid != NULL && !g->by_unicode) {
        gid = key < g->ncid2gid ? g->cid2gid[key] : 0;
    }
    if (g->kind == GF_CFF && !g->by_unicode) {
        return tiku_cff_outline(g->cff, tiku_cff_glyph_cid(g->cff, gid), hint,
                                path);
    }
    if (g->kind != GF_TTF) {
        return 0;
    }
    if (g->substitute || g->by_unicode) {
        gid = u != 0 ? tiku_ttf_glyph(g->ttf, u) : 0;
        if (gid == 0) {
            return 0;
        }
    }
    *adv_units = tiku_ttf_advance(g->ttf, gid);
    return tiku_ttf_outline(g->ttf, gid, hint, path);
}

/** @brief Draw the outline of @p code into @p path at unit scale, y up. */
static int
outline(pdf_gfont_t *g, long code, tiku_path_t *path, double *adv_units)
{
    tiku_hint_t hint;
    unsigned gid;

    memset(&hint, 0, sizeof hint);
    hint.scale = 1.0f;
    *adv_units = -1;
    if (g->two_byte) {
        return cid_outline(g, code, &hint, path, adv_units);
    }
    if (g->kind == GF_T1) {
        const char *name = g->names[code];

        if (name == NULL || !tiku_t1_has(g->t1, name)) {
            name = tiku_t1_encoded(g->t1, (unsigned)code);
        }
        if (name == NULL && g->names[code] != NULL) {
            name = g->names[code];
        }
        if (name == NULL) {
            return 0;
        }
        {
            float adv = 0;
            int ok = tiku_t1_outline(g->t1, name, &hint, path, &adv);

            *adv_units = adv;
            return ok;
        }
    }
    if (g->kind == GF_CFF) {
        const char *name = g->names[code];
        long num;

        gid = name != NULL ? tiku_cff_glyph_named(g->cff, name) : 0;
        if (gid == 0 && name != NULL && (num = numbered(name)) >= 0) {
            gid = (unsigned)num;
        }
        if (gid == 0) {
            gid = tiku_cff_glyph_coded(g->cff, (unsigned)code);
        }
        return gid != 0 ? tiku_cff_outline(g->cff, gid, &hint, path) : 0;
    }
    if (g->kind == GF_TTF) {
        gid = ttf_glyph_of(g, code);
        if (gid == 0 && g->names[code] == NULL && !g->substitute) {
            return 0;
        }
        *adv_units = tiku_ttf_advance(g->ttf, gid);
        return tiku_ttf_outline(g->ttf, gid, &hint, path);
    }
    return 0;
}

int
pdf_gfont_mask(pdf_gfont_t *g, long code, const pdf_mat_t *m, pdf_mask_t *out)
{
    tiku_path_t *path;
    float t[6];
    double u = g->upem > 0 ? 1.0 / g->upem : 0.001, adv = -1, sx = 1;
    int x0, y0, w, h;

    memset(out, 0, sizeof *out);
    if (g->kind == GF_NONE || g->kind == GF_T3 || code < 0 ||
        code >= (g->two_byte ? 65536 : 256)) {
        return 0;
    }
    /* A stand-in is squeezed to the width the file laid the text by. */
    path = tiku_path_new();
    if (path == NULL) {
        return 0;
    }
    if (g->substitute && g->width != NULL && g->width[code] > 0) {
        double have;

        if (!outline(g, code, path, &adv)) {
            tiku_path_free(path);
            return 0;
        }
        have = adv > 0 ? adv * u : 0;
        if (have > 0 && g->width[code] < have * 0.98) {
            sx = g->width[code] / have;
        }
        tiku_path_reset(path);
    }
    /* Font units, with y already flipped by the hint, to the device:
     * scale by the em, then through the text render matrix. */
    t[0] = (float)(m->a * u * sx);
    t[1] = (float)(m->b * u * sx);
    t[2] = (float)(-m->c * u);
    t[3] = (float)(-m->d * u);
    t[4] = (float)m->e;
    t[5] = (float)m->f;
    tiku_path_transform(path, t);
    if (!outline(g, code, path, &adv) || tiku_path_failed(path)) {
        tiku_path_free(path);
        return 0;
    }
    tiku_path_bounds(path, &x0, &y0, &w, &h);
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        tiku_path_free(path);
        return 0;
    }
    out->a = tiku_path_render(path, x0, y0, w, h, 0);
    tiku_path_free(path);
    if (out->a == NULL) {
        return 0;
    }
    out->x0 = x0;
    out->y0 = y0;
    out->w = w;
    out->h = h;
    return 1;
}

/* --------------------------------------------------------- fonts kept  */

#define FONTS_KEEP  48              /* fonts kept from page to page      */
#define FONTS_PAGE  512             /* and one page may ask for, at most */

/** @brief A font read for the document: both its views, and the page
 *         that last asked for it. */
struct pdf_fontkeep {
    pdf_obj_t     *dict;            /* the font dictionary, the key      */
    pdf_tfont_t   *text;            /* its text view                     */
    pdf_gfont_t   *drawn;           /* its drawn view, once asked for    */
    int            tried;           /* the drawn view has been tried     */
    unsigned long  used;            /* the page that last asked for it   */
};

void
pdf_fonts_page(tiku_pdf_t *doc)
{
    doc->epoch++;
    while (doc->nfonts > FONTS_KEEP) {
        int i, old = 0;

        for (i = 1; i < doc->nfonts; i++) {
            if (doc->fonts[i].used < doc->fonts[old].used) {
                old = i;
            }
        }
        pdf_gfont_free(doc->fonts[old].drawn);  /* it borrows the text */
        pdf_tfont_free(doc->fonts[old].text);
        doc->fonts[old] = doc->fonts[--doc->nfonts];
    }
}

/** @brief Where @p dict is kept, read now if it was not.  @return its
 *         index, or -1 when it is no font or the page has asked for too
 *         many.  An index, since reading one may move the rest. */
static int
kept(tiku_pdf_t *doc, pdf_obj_t *dict)
{
    struct pdf_fontkeep *k;
    pdf_tfont_t *text;
    int i;

    if (dict == NULL || dict->kind != PDF_DICT) {
        return -1;
    }
    for (i = 0; i < doc->nfonts; i++) {
        if (doc->fonts[i].dict == dict) {
            doc->fonts[i].used = doc->epoch;
            return i;
        }
    }
    if (doc->nfonts >= FONTS_PAGE || (text = pdf_tfont_load(doc, dict)) ==
                                     NULL) {
        return -1;
    }
    if (doc->nfonts == doc->cfonts) {
        int c = doc->cfonts ? doc->cfonts * 2 : 16;

        k = realloc(doc->fonts, (size_t)c * sizeof *k);
        if (k == NULL) {
            pdf_tfont_free(text);
            return -1;
        }
        doc->fonts = k;
        doc->cfonts = c;
    }
    k = &doc->fonts[doc->nfonts];
    memset(k, 0, sizeof *k);
    k->dict = dict;
    k->text = text;
    k->used = doc->epoch;
    return doc->nfonts++;
}

pdf_tfont_t *
pdf_font_text(tiku_pdf_t *doc, pdf_obj_t *dict)
{
    int i = kept(doc, dict);

    return i >= 0 ? doc->fonts[i].text : NULL;
}

pdf_gfont_t *
pdf_font_drawn(tiku_pdf_t *doc, pdf_obj_t *dict)
{
    int i = kept(doc, dict);
    pdf_gfont_t *g;

    if (i < 0) {
        return NULL;
    }
    if (!doc->fonts[i].tried) {
        doc->fonts[i].tried = 1;
        g = pdf_gfont_load(doc, dict, doc->fonts[i].text);
        doc->fonts[i].drawn = g;
    }
    return doc->fonts[i].drawn;
}

void
pdf_fonts_free(tiku_pdf_t *doc)
{
    int i;

    for (i = 0; i < doc->nfonts; i++) {
        pdf_gfont_free(doc->fonts[i].drawn);
        pdf_tfont_free(doc->fonts[i].text);
    }
    free(doc->fonts);
    doc->fonts = NULL;
    doc->nfonts = doc->cfonts = 0;
}
