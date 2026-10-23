/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_fontset.c - faces chosen for the family names documents use.
 *
 * Within a family the face nearest the slant and weight asked is chosen,
 * normal width preferred.  A family the machine lacks goes to a stand-in
 * with the same advance widths, which keeps the document's line breaks,
 * then to a face of its kind: serif, sans or monospaced.  A character a
 * face does not draw goes to a face for its script.  The tables hold
 * names folded as fs_fold() folds them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_fontset_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Families and their stand-ins, best first: metric-compatible
 *         faces (Carlito, Caladea, Liberation), then what a Mac or Linux
 *         has of the same look. */
static const struct {
    const char *family, *alts;
} stand_ins[] = {
    {"calibri", "carlito|aptos"},
    {"calibrilight", "calibri|carlito"},
    {"cambria", "caladea|georgia"},
    {"cambriamath", "cambria|caladea|stixtwomath"},
    {"aptos", "calibri|carlito"},
    {"aptosdisplay", "aptos|calibri|carlito"},
    {"arial", "liberationsans|arimo|helvetica|helveticaneue|nimbussans"},
    {"helvetica", "arial|liberationsans|arimo|nimbussans"},
    {"helveticaneue", "helvetica|arial|liberationsans"},
    {"arialnarrow", "liberationsansnarrow|helveticaneue|arial"},
    {"timesnewroman", "liberationserif|tinos|times|nimbusroman"},
    {"times", "timesnewroman|liberationserif|tinos|nimbusroman"},
    {"couriernew", "liberationmono|cousine|courier|nimbusmono"},
    {"courier", "couriernew|liberationmono|cousine|nimbusmono"},
    {"segoeui", "selawik|opensans|notosans|helveticaneue"},
    {"georgia", "gelasio|dejavuserif"},
    {"verdana", "dejavusans|bitstreamverasans"},
    {"tahoma", "dejavusans|verdana"},
    {"consolas", "inconsolata|menlo|dejavusansmono"},
    {"lucidaconsole", "menlo|dejavusansmono"},
    {"garamond", "ebgaramond|garamondpremrpro|palatino"},
    {"bookantiqua", "palatino|texgyrepagella|urwpalladiol"},
    {"palatinolinotype", "palatino|texgyrepagella|urwpalladiol"},
    {"centurygothic", "urwgothic|texgyreadventor|avenir"},
    {"comicsansms", "comicneue|chalkboard"},
    {"symbol", "symbolneu|standardsymbolsps"},
    {"liberationsans", "arial|helvetica"},
    {"liberationserif", "timesnewroman|times"},
    {"liberationmono", "couriernew|courier"},
    {"dejavusans", "verdana|helvetica"},
    {"msmincho", "hiraginominchopron|ipamincho|notoserifcjkjp"},
    {"ＭＳ明朝", "msmincho|hiraginominchopron|notoserifcjkjp"},
    {"msgothic", "hiraginosans|hiraginokakugothicpron|notosanscjkjp"},
    {"ＭＳゴシック", "msgothic|hiraginosans|notosanscjkjp"},
    {"yugothic", "hiraginosans|notosanscjkjp"},
    {"yumincho", "hiraginominchopron|notoserifcjkjp"},
    {"simsun", "songtisc|stsong|notoserifcjksc"},
    {"宋体", "simsun|songtisc|stsong|notoserifcjksc"},
    {"simhei", "heitisc|stheiti|pingfangsc|notosanscjksc"},
    {"黑体", "simhei|heitisc|stheiti|notosanscjksc"},
    {"microsoftyahei", "pingfangsc|heitisc|notosanscjksc"},
    {"微软雅黑", "microsoftyahei|pingfangsc|notosanscjksc"},
    {"dengxian", "pingfangsc|heitisc|notosanscjksc"},
    {"等线", "dengxian|pingfangsc|notosanscjksc"},
    {"mingliu", "pingfangtc|heititc|notoserifcjktc"},
    {"pmingliu", "pingfangtc|heititc|notoserifcjktc"},
    {"malgungothic", "applesdgothicneo|notosanscjkkr|nanumgothic"},
    {"맑은고딕", "malgungothic|applesdgothicneo|notosanscjkkr"},
    {"batang", "applemyungjo|nanummyeongjo|notoserifcjkkr"},
};

/** @brief The faces of each kind a machine is likely to have: sans,
 *         monospaced and serif, in the order kind_of() numbers them. */
static const char *const kinds[] = {
    "helvetica|arial|liberationsans|arimo|dejavusans|notosans|freesans|"
    "nimbussans|helveticaneue|lucidagrande|verdana|opensans|roboto",
    "couriernew|menlo|courier|liberationmono|cousine|dejavusansmono|"
    "notosansmono|freemono|nimbusmono|monaco",
    "timesnewroman|times|liberationserif|tinos|dejavuserif|notoserif|"
    "freeserif|nimbusroman|georgia|charter|palatino",
};

/** @brief Faces for scripts a document's own font seldom draws. */
static const struct {
    uint32_t lo, hi;
    const char *faces;
} scripts[] = {
    {0x0590, 0x05ff, "arialhebrew|notosanshebrew|dejavusans|arial"},
    {0x0600, 0x06ff, "geezapro|notosansarabic|notonaskharabic|dejavusans"},
    {0x0900, 0x097f,
     "kohinoordevanagari|notosansdevanagari|"
     "devanagarisangammn|lohitdevanagari"},
    {0x0e00, 0x0e7f, "thonburi|notosansthai|ayuthaya|tlwgtypo"},
    {0x1100, 0x11ff, "applesdgothicneo|notosanscjkkr|nanumgothic"},
    {0x2000, 0x2bff,
     "applesymbols|notosanssymbols|notosanssymbols2|"
     "dejavusans|symbola|stixgeneral|stixtwomath|"
     "arialunicodems"},
    {0x3000, 0x9fff,
     "hiraginosans|pingfangsc|hiraginosansgb|heitisc|"
     "stheiti|notosanscjksc|notosanscjkjp|notosanscjk|"
     "sourcehansans|wenquanyizenhei|droidsansfallback|"
     "arialunicodems"},
    {0xac00, 0xd7af,
     "applesdgothicneo|notosanscjkkr|nanumgothic|"
     "malgungothic"},
    {0xf900, 0xfaff,
     "hiraginosans|pingfangsc|notosanscjksc|"
     "droidsansfallback"},
    {0xff00, 0xffef,
     "hiraginosans|pingfangsc|notosanscjksc|"
     "droidsansfallback"},
    {0x1f000, 0x1faff,
     "applesymbols|notosanssymbols2|symbola|"
     "segoeuisymbol"},
};

/** @brief Whether the folded list @p names holds @p key. */
static int
named(const char *names, const char *key)
{
    for (; *names; names += strlen(names) + 1) {
        if (strcmp(names, key) == 0) {
            return 1;
        }
    }
    return 0;
}

/** @brief How far @p f is from what is asked, lower nearer: a slant that
 *         differs outweighs the rest; weight and width count together,
 *         one width step as much as 200 of weight, with heavier preferred
 *         for bold and lighter otherwise. */
static int
distance(const fs_face_t *f, int weight, int italic)
{
    int d = f->weight - weight;

    if (d < 0) {
        d = weight >= 600 ? -d * 2 + 1 : -d * 2;
    } else {
        d = weight >= 600 ? d * 2 : d * 2 + 1;
    }
    return (f->italic != (italic != 0)) * 100000 + d +
           (f->width > 5 ? f->width - 5 : 5 - f->width) * 400;
}

/** @brief Face @p f into @p out, with what it lacks of @p weight and
 *         @p italic marked for synthesis. */
static void
give(const fs_face_t *f, int weight, int italic, tiku_fontset_face_t *out)
{
    memset(out, 0, sizeof *out);
    snprintf(out->path, sizeof out->path, "%s", f->path);
    snprintf(out->family, sizeof out->family, "%s", f->family);
    out->index = f->index;
    out->weight = f->weight;
    out->italic = f->italic;
    out->cff = f->cff;
    out->bold_synth = weight >= 600 && f->weight < 600;
    out->italic_synth = italic && !f->italic;
}

/**
 * @brief The nearest face of the family folded to @p key -- that draws
 *        @p cp, when it is not 0.  @return 1 with @p out filled, 0 when
 *        the family has no such face.
 */
static int
family_best(fs_index_t *ix, const char *key, int weight, int italic,
            uint32_t cp, tiku_fontset_face_t *out)
{
    const fs_face_t *best = NULL;
    int bestd = 0, i;

    for (i = 0; i < ix->n; i++) {
        fs_face_t *f = &ix->face[i];
        int d;

        if (!named(f->names, key) || (cp && !fs_covers(ix, f, cp))) {
            continue;
        }
        d = distance(f, weight, italic);
        if (best == NULL || d < bestd) {
            best = f;
            bestd = d;
        }
    }
    if (best != NULL) {
        give(best, weight, italic, out);
    }
    return best != NULL;
}

/** @brief The first family of the '|'-joined @p list the machine has.
 *  @return 1 with @p out filled, 0 when it has none of them. */
static int
list_best(fs_index_t *ix, const char *list, int weight, int italic, uint32_t cp,
          tiku_fontset_face_t *out)
{
    char key[64];

    while (list != NULL && *list) {
        const char *bar = strchr(list, '|');
        size_t n = bar ? (size_t)(bar - list) : strlen(list);

        if (n < sizeof key) {
            memcpy(key, list, n);
            key[n] = '\0';
            if (family_best(ix, key, weight, italic, cp, out)) {
                return 1;
            }
        }
        list = bar ? bar + 1 : NULL;
    }
    return 0;
}

/** @brief The stand-ins for the family folded to @p key, or NULL. */
static const char *
stand_in(const char *key)
{
    size_t i;

    for (i = 0; i < sizeof stand_ins / sizeof stand_ins[0]; i++) {
        if (strcmp(stand_ins[i].family, key) == 0) {
            return stand_ins[i].alts;
        }
    }
    return NULL;
}

/** @brief The kind @p key's name says: 1 monospaced, 2 serif, 0 sans,
 *         -1 when it says nothing. */
static int
kind_by_name(const char *key)
{
    static const char *const mono[] = {
        "mono", "courier", "consol", "code", "typewriter", "menlo", "monaco"};
    static const char *const serif[] = {
        "serif",   "times",   "roman",    "georgia",     "garamond", "cambria",
        "book",    "minion",  "palatino", "baskerville", "caslon",   "bodoni",
        "century", "mincho",  "song",     "ming",        "batang",   "明朝",
        "宋",      "caladea", "tinos",    "charter"};
    static const char *const sans[] = {
        "sans",     "gothic",  "arial", "helvet",  "verdana", "tahoma",
        "segoe",    "calibri", "aptos", "grotesk", "carlito", "arimo",
        "ゴシック", "黑",      "黒",    "고딕"};
    size_t i;

    for (i = 0; i < sizeof mono / sizeof mono[0]; i++) {
        if (strstr(key, mono[i]) != NULL) {
            return 1;
        }
    }
    for (i = 0; i < sizeof sans / sizeof sans[0]; i++) {
        if (strstr(key, sans[i]) != NULL) {
            return 0;
        }
    }
    for (i = 0; i < sizeof serif / sizeof serif[0]; i++) {
        if (strstr(key, serif[i]) != NULL) {
            return 2;
        }
    }
    return -1;
}

/** @brief The kind of the family folded to @p key: by its name, else by
 *         what its face says of itself; sans when neither says. */
static int
kind_of(fs_index_t *ix, const char *key)
{
    int k = kind_by_name(key), i;

    for (i = 0; k < 0 && i < ix->n; i++) {
        if (named(ix->face[i].names, key)) {
            k = ix->face[i].mono ? 1 : ix->face[i].serif == 1 ? 2 : 0;
        }
    }
    return k < 0 ? 0 : k;
}

int
tiku_fontset_kind(const char *family)
{
    char key[128];

    fs_fold(family, key, sizeof key);
    return kind_of(fs_index(), key);
}

int
tiku_fontset_find(const char *family, int weight, int italic,
                  tiku_fontset_face_t *out)
{
    fs_index_t *ix = fs_index();
    char key[128];
    const char *alts;
    int i, best = -1;

    memset(out, 0, sizeof *out);
    if (ix->n == 0) {
        return -1;
    }
    fs_fold(family, key, sizeof key);
    if (key[0] && family_best(ix, key, weight, italic, 0, out)) {
        out->exact = 1;
        return 0;
    }
    if (key[0] && (alts = stand_in(key)) != NULL &&
        list_best(ix, alts, weight, italic, 0, out)) {
        return 0;
    }
    if (list_best(ix, kinds[kind_of(ix, key)], weight, italic, 0, out)) {
        return 0;
    }
    for (i = 0; i < ix->n; i++) {
        if (best < 0 || distance(&ix->face[i], weight, italic) <
                            distance(&ix->face[best], weight, italic)) {
            best = i;
        }
    }
    give(&ix->face[best], weight, italic, out);
    return 0;
}

int
tiku_fontset_cover(uint32_t cp, const char *family, int weight, int italic,
                   tiku_fontset_face_t *out)
{
    fs_index_t *ix = fs_index();
    char key[128];
    size_t s;
    int i;

    memset(out, 0, sizeof *out);
    fs_fold(family, key, sizeof key);
    for (s = 0; s < sizeof scripts / sizeof scripts[0]; s++) {
        if (cp >= scripts[s].lo && cp <= scripts[s].hi &&
            list_best(ix, scripts[s].faces, weight, italic, cp, out)) {
            return 0;
        }
    }
    if (list_best(ix, kinds[kind_of(ix, key)], weight, italic, cp, out)) {
        return 0;
    }
    for (i = 0; i < ix->n; i++) {
        if (!ix->face[i].italic && fs_covers(ix, &ix->face[i], cp)) {
            give(&ix->face[i], weight, italic, out);
            return 0;
        }
    }
    return -1;
}

int
tiku_fontset_at(int index, tiku_fontset_face_t *out)
{
    if (!out)
        return -1;
    memset(out, 0, sizeof *out);
    fs_index_t *ix = fs_index();
    if (index < 0 || index >= ix->n)
        return -1;
    const fs_face_t *face = ix->face + index;
    snprintf(out->path, sizeof out->path, "%s", face->path);
    snprintf(out->family, sizeof out->family, "%s", face->family);
    out->index = face->index;
    out->weight = face->weight;
    out->italic = face->italic;
    out->cff = face->cff;
    out->exact = 1;
    return 0;
}
