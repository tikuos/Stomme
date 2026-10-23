/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_fontset_scan.c - finding the faces: every TrueType and OpenType
 * file in the folders fonts are kept in, each face's names, weight,
 * width and slant read from its own tables without loading its outlines.
 * A face's character map is read the first time fs_covers() asks about
 * it.  The folder list holds a Mac's folders, a Mac Office's and a Linux
 * machine's; folders that do not exist are passed over.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_fontset_private.h"
#include "tiku_state.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define SCAN_DEPTH 4            /* folder levels below a fonts folder */
#define SCAN_FACES 64           /* faces a collection may hold       */
#define SCAN_TABLES 256         /* tables a face may list            */
#define NAME_BYTES (256 * 1024) /* a name table read, at most        */
#define CMAP_BYTES (8 << 20)    /* a character map read, at most     */
#define SCAN_NAMES 1024         /* folded names a face keeps, bytes  */

/** @brief Where faces are kept on a Mac and on Linux; "~" is home.  A
 *         Mac Office's own folders carry the faces its documents name. */
static const char *const system_dirs[] = {
    "~/Library/Fonts",
    "/Library/Fonts",
    "/System/Library/Fonts",
    "/Applications/Microsoft Word.app/Contents/Resources/DFonts",
    "/Applications/Microsoft PowerPoint.app/Contents/Resources/DFonts",
    "~/.local/share/fonts",
    "~/.fonts",
    "/usr/local/share/fonts",
    "/usr/share/fonts",
};

static fs_index_t index_;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static char extra[4096];

/** @brief A walk over the folders: those seen, and the files. */
typedef struct {
    struct {
        dev_t dev;
        ino_t ino;
    } *dir;
    int ndir, dircap;
    char **file; /* "name:size" of each file indexed  */
    int nfile, filecap;
} scan_t;

/** @brief One face's tables, where they are in its file. */
typedef struct {
    long name, os2, head, post, fvar;
    unsigned long name_len, fvar_len;
    int cff;
} tables_t;

/** @brief A big-endian 16-bit number. */
static unsigned
be16(const unsigned char *p)
{
    return (unsigned)p[0] << 8 | p[1];
}

/** @brief A big-endian 32-bit number. */
static unsigned long
be32(const unsigned char *p)
{
    return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 |
           (unsigned long)p[2] << 8 | p[3];
}

/** @brief @p n bytes of @p fd at @p off.  @return 0, or -1 short. */
static int
rd(int fd, long off, void *buf, size_t n)
{
    return off >= 0 && pread(fd, buf, n, (off_t)off) == (ssize_t)n ? 0 : -1;
}

void
fs_fold(const char *name, char *out, size_t max)
{
    size_t k = 0;

    for (; name != NULL && *name && k + 1 < max; name++) {
        unsigned char c = (unsigned char)*name;

        if (c == ' ' || c == '-' || c == '_' || c == '\'' || c == '"') {
            continue;
        }
        out[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    if (max > 0) {
        out[k] = '\0';
    }
}

/** @brief Unicode mappings for the upper half of Mac Roman. */
static const uint16_t name_macroman[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0,
    0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8, 0x00EA, 0x00EB,
    0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3, 0x00F2, 0x00F4, 0x00F6,
    0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC, 0x2020, 0x00B0, 0x00A2, 0x00A3,
    0x00A7, 0x2022, 0x00B6, 0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8,
    0x2260, 0x00C6, 0x00D8, 0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5,
    0x2202, 0x2211, 0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6,
    0x00F8, 0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
    0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153, 0x2013,
    0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178,
    0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02, 0x2021, 0x00B7, 0x201A,
    0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1, 0x00CB, 0x00C8, 0x00CD, 0x00CE,
    0x00CF, 0x00CC, 0x00D3, 0x00D4, 0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9,
    0x0131, 0x02C6, 0x02DC, 0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD,
    0x02DB, 0x02C7};

/** @brief A name record's @p n bytes as UTF-8 into @p out: UTF-16 for the
 *         Unicode and Windows platforms, single bytes for the Mac's. */
static void
name_utf8(const unsigned char *p, size_t n, int utf16, char *out, size_t max)
{
    size_t k = 0, i;

    for (i = 0; i < n && k + 4 < max; i += utf16 ? 2 : 1) {
        unsigned u = utf16 ? (i + 1 < n ? be16(p + i) : 0) : p[i];

        if (!utf16 && u >= 128)
            u = name_macroman[u - 128];

        if (u == 0) {
            break;
        }
        if (u < 0x80) {
            out[k++] = (char)u;
        } else if (u < 0x800) {
            out[k++] = (char)(0xc0 | u >> 6);
            out[k++] = (char)(0x80 | (u & 0x3f));
        } else {
            out[k++] = (char)(0xe0 | u >> 12);
            out[k++] = (char)(0x80 | (u >> 6 & 0x3f));
            out[k++] = (char)(0x80 | (u & 0x3f));
        }
    }
    out[k] = '\0';
}

/** @brief @p name, folded, added to the face's list unless it is there
 *         or the list's SCAN_NAMES bytes are full. */
static void
add_name(char *list, const char *name)
{
    char folded[128];
    char *p = list;
    size_t used, k;

    fs_fold(name, folded, sizeof folded);
    if (folded[0] == '\0') {
        return;
    }
    for (; *p; p += strlen(p) + 1) {
        if (strcmp(p, folded) == 0) {
            return;
        }
    }
    used = (size_t)(p - list);
    k = strlen(folded) + 1;
    if (used + k + 1 <= SCAN_NAMES) {
        memcpy(p, folded, k);
        p[k] = '\0';
    }
}

/**
 * @brief The face's family names from its name table: every language's
 *        family and typographic family into @p list, the English family
 *        (or the first) into @p f; the subfamily into @p style.
 */
static void
read_names(const unsigned char *t, unsigned long len, fs_face_t *f, char *list,
           char *style, size_t style_max)
{
    unsigned count = len >= 6 ? be16(t + 2) : 0, i;
    unsigned long strings = len >= 6 ? be16(t + 4) : 0;
    char name[256];

    for (i = 0; i < count && 6 + 12 * (unsigned long)(i + 1) <= len; i++) {
        const unsigned char *r = t + 6 + 12 * i;
        unsigned plat = be16(r), enc = be16(r + 2), lang = be16(r + 4);
        unsigned id = be16(r + 6);
        unsigned long n = be16(r + 8), off = strings + be16(r + 10);
        int utf16 = plat == 0 || plat == 3;

        if ((id != 1 && id != 2 && id != 16) || off + n > len ||
            (plat == 1 && enc != 0) || plat == 2 || plat > 3 ||
            (plat == 3 && enc != 0 && enc != 1 && enc != 10)) {
            continue;
        }
        name_utf8(t + off, n, utf16, name, sizeof name);
        if (id == 2 && (style[0] == '\0' || lang == 0x409)) {
            snprintf(style, style_max, "%s", name);
        } else if (id != 2) {
            add_name(list, name);
            if (id == 1 &&
                (f->family[0] == '\0' || (plat == 3 && lang == 0x409))) {
                snprintf(f->family, sizeof f->family, "%s", name);
            }
        }
    }
}

/** @brief A weight and slant from a style's name, for a face whose OS/2
 *         table does not say. */
static void
style_guess(const char *style, fs_face_t *f)
{
    static const struct {
        const char *word;
        int weight;
    } w[] = {{"thin", 100},      {"extralight", 200}, {"light", 300},
             {"medium", 500},    {"semibold", 600},   {"demibold", 600},
             {"extrabold", 800}, {"black", 900},      {"heavy", 900},
             {"bold", 700}};
    char folded[64];
    size_t i;

    fs_fold(style, folded, sizeof folded);
    for (i = 0; i < sizeof w / sizeof w[0]; i++) {
        if (strstr(folded, w[i].word) != NULL) {
            f->weight = w[i].weight;
            break;
        }
    }
    f->italic |=
        strstr(folded, "italic") != NULL || strstr(folded, "oblique") != NULL;
}

/** @brief Where the tables of the face whose directory is at @p dir are.
 *  @return 0, or -1 for a face with no outlines this can draw (glyf or
 *          CFF), no name table or no head table. */
static int
find_tables(int fd, long dir, tables_t *t)
{
    unsigned char h[12], r[16];
    unsigned n, i;
    int outlines = 0;

    memset(t, 0, sizeof *t);
    if (rd(fd, dir, h, 12) != 0 ||
        (be32(h) != 0x00010000ul && memcmp(h, "true", 4) != 0 &&
         memcmp(h, "OTTO", 4) != 0)) {
        return -1;
    }
    n = be16(h + 4);
    for (i = 0; i < n && i < SCAN_TABLES; i++) {
        if (rd(fd, dir + 12 + 16 * (long)i, r, 16) != 0) {
            return -1;
        }
        if (memcmp(r, "name", 4) == 0) {
            t->name = (long)be32(r + 8);
            t->name_len = be32(r + 12);
        } else if (memcmp(r, "OS/2", 4) == 0) {
            t->os2 = (long)be32(r + 8);
        } else if (memcmp(r, "head", 4) == 0) {
            t->head = (long)be32(r + 8);
        } else if (memcmp(r, "post", 4) == 0) {
            t->post = (long)be32(r + 8);
        } else if (memcmp(r, "fvar", 4) == 0) {
            t->fvar = (long)be32(r + 8);
            t->fvar_len = be32(r + 12);
        } else if (memcmp(r, "glyf", 4) == 0) {
            outlines = 1;
        } else if (memcmp(r, "CFF ", 4) == 0 || memcmp(r, "CFF2", 4) == 0) {
            outlines = t->cff = 1;
        }
    }
    return outlines && t->name != 0 && t->head != 0 ? 0 : -1;
}

/** @brief The face's weight, width, slant, pitch and serifs, from OS/2,
 *         head and post, as they say them. */
static void
read_style(int fd, const tables_t *t, fs_face_t *f)
{
    unsigned char os2[64], head[54], post[16];

    f->weight = 400;
    f->width = 5;
    f->serif = -1;
    if (t->os2 != 0 && rd(fd, t->os2, os2, sizeof os2) == 0) {
        unsigned w = be16(os2 + 4), sel = be16(os2 + 62);

        f->weight = w >= 1 && w <= 9       ? (int)w * 100
                    : w >= 50 && w <= 1000 ? (int)w
                                           : 400;
        f->width =
            be16(os2 + 6) >= 1 && be16(os2 + 6) <= 9 ? (int)be16(os2 + 6) : 5;
        f->italic = (sel & 0x201u) != 0; /* ITALIC, OBLIQUE */
        if (os2[32] == 2) {              /* panose: Latin text */
            f->serif = os2[33] >= 11 && os2[33] <= 13  ? 0
                       : os2[33] >= 2 && os2[33] <= 10 ? 1
                                                       : -1;
            f->mono = os2[35] == 9; /* proportion: mono */
        }
    } else if (rd(fd, t->head, head, sizeof head) == 0) {
        f->weight = be16(head + 44) & 1u ? 700 : 400;
        f->italic = (be16(head + 44) & 2u) != 0;
    }
    if (t->post != 0 && rd(fd, t->post, post, sizeof post) == 0) {
        f->mono |= be32(post + 12) != 0;
    }
}

/** @brief Room for one face more.  @return it, cleared, or NULL. */
static fs_face_t *
face_room(void)
{
    if (index_.n == index_.cap) {
        int cap = index_.cap ? index_.cap * 2 : 256;
        fs_face_t *g = realloc(index_.face, (size_t)cap * sizeof *g);

        if (g == NULL) {
            return NULL;
        }
        index_.face = g;
        index_.cap = cap;
    }
    memset(&index_.face[index_.n], 0, sizeof index_.face[0]);
    return &index_.face[index_.n];
}

/** @brief Read an instance's English or first available subfamily name. */
static void
instance_name(const unsigned char *name, size_t length, unsigned wanted,
              char out[64])
{
    unsigned count = length >= 6 ? be16(name + 2) : 0;
    size_t strings = length >= 6 ? be16(name + 4) : 0;
    out[0] = 0;
    for (unsigned i = 0; i < count && 6 + (size_t)(i + 1) * 12 <= length; i++) {
        const unsigned char *r = name + 6 + i * 12;
        unsigned platform = be16(r), encoding = be16(r + 2);
        size_t at = strings + be16(r + 10), bytes = be16(r + 8);
        if (be16(r + 6) != wanted || at > length || bytes > length - at ||
            platform > 3 || platform == 2 || (platform == 1 && encoding) ||
            (platform == 3 && encoding != 0 && encoding != 1 && encoding != 10))
            continue;
        if (!out[0] || (platform == 3 && be16(r + 4) == 0x409))
            name_utf8(name + at, bytes, platform != 1, out, 64);
    }
}
/** @brief Resolve bounded named-axis coordinates into searchable style data. */
static int
instance_style(const unsigned char *axes, unsigned axis_count,
               unsigned axis_size, const unsigned char *record, fs_face_t *out)
{
    for (unsigned i = 0; i < axis_count; i++) {
        const unsigned char *axis = axes + i * axis_size;
        int32_t minimum = (int32_t)be32(axis + 4);
        int32_t normal = (int32_t)be32(axis + 8);
        int32_t maximum = (int32_t)be32(axis + 12);
        int32_t value = (int32_t)be32(record + 4 + i * 4);
        if (minimum > normal || normal > maximum || value < minimum ||
            value > maximum)
            return 0;
        if (!memcmp(axis, "wght", 4)) {
            int weight = value / 65536;
            if (weight < 1 || weight > 1000)
                return 0;
            out->weight = weight;
        } else if (!memcmp(axis, "ital", 4) || !memcmp(axis, "slnt", 4))
            out->italic = value != 0;
        else if (!memcmp(axis, "wdth", 4)) {
            static const int widths[] = {50,  63,  75,  88, 100,
                                         113, 125, 150, 200};
            int width = value / 65536, nearest = 0,
                distance = abs(width - widths[0]);
            for (unsigned n = 1; n < 9; n++)
                if (abs(width - widths[n]) < distance) {
                    nearest = n;
                    distance = abs(width - widths[n]);
                }
            out->width = nearest + 1;
        }
    }
    return 1;
}
/** @brief Preserve conventional variants and name other fixed instances as
 * families. */
static int
instance_family(const char *style, fs_face_t *face, char aliases[SCAN_NAMES])
{
    char folded[64];
    fs_fold(style, folded, sizeof folded);
    static const char *const variants[] = {
        "regular", "normal",  "roman",      "book",       "bold",
        "italic",  "oblique", "bolditalic", "boldoblique"};
    for (size_t i = 0; i < sizeof variants / sizeof *variants; i++)
        if (!strcmp(folded, variants[i]))
            return 1;
    char family[64];
    size_t a = strlen(face->family), b = strlen(style);
    if (a + b + 2 > sizeof family)
        return 0;
    memcpy(family, face->family, a);
    family[a] = ' ';
    memcpy(family + a + 1, style, b + 1);
    strcpy(face->family, family);
    memset(aliases, 0, SCAN_NAMES);
    add_name(aliases, family);
    return 1;
}
/** @brief Add at most 64 named TrueType instances with at most sixteen axes. */
static void
add_instances(int fd, const tables_t *tables, const unsigned char *names,
              size_t name_length, const fs_face_t *original,
              const char *original_aliases)
{
#if TIKU_HAVE_FREETYPE && TIKU_HAVE_HARFBUZZ
    if (!tables->fvar || tables->cff || tables->fvar_len < 16 ||
        tables->fvar_len > 65536)
        return;
    unsigned char *data = malloc(tables->fvar_len);
    if (!data)
        return;
    if (rd(fd, tables->fvar, data, tables->fvar_len)) {
        free(data);
        return;
    }
    unsigned offset = be16(data + 4), axes = be16(data + 8),
             axis_size = be16(data + 10);
    unsigned count = be16(data + 12), instance_size = be16(data + 14);
    size_t instances = (size_t)offset + (size_t)axes * axis_size;
    if (be32(data) != 0x00010000 || offset < 16 || !axes || axes > 16 ||
        axis_size < 20 || count > 64 || instance_size < 4 + axes * 4 ||
        instances > tables->fvar_len ||
        (size_t)count * instance_size > tables->fvar_len - instances) {
        free(data);
        return;
    }
    fs_face_t base = *original;
    for (unsigned i = 0; i < count; i++) {
        const unsigned char *record = data + instances + i * instance_size;
        fs_face_t instance = base;
        char style[64], aliases[SCAN_NAMES];
        memcpy(aliases, original_aliases, SCAN_NAMES);
        instance_name(names, name_length, be16(record), style);
        style_guess(style, &instance);
        if (!style[0] ||
            !instance_style(data + offset, axes, axis_size, record,
                            &instance) ||
            !instance_family(style, &instance, aliases))
            continue;
        instance.index = (int)((i + 1) << 16) | (base.index & 65535);
        instance.names = NULL;
        instance.path = NULL;
        instance.cover = NULL;
        instance.ncover = -1;
        size_t used = 0;
        while (aliases[used])
            used += strlen(aliases + used) + 1;
        fs_face_t *slot = face_room();
        if (!slot)
            break;
        instance.names = malloc(used + 1);
        instance.path = strdup(base.path);
        if (!instance.names || !instance.path) {
            free(instance.names);
            free(instance.path);
            continue;
        }
        memcpy(instance.names, aliases, used + 1);
        *slot = instance;
        index_.n++;
    }
    free(data);
#endif
}

/** @brief Face @p index of the file @p path, its tables at @p dir, into
 *         the index when it can be drawn and names its family. */
static void
add_face(int fd, const char *path, int index, long dir)
{
    tables_t t;
    unsigned char *name;
    char style[64] = "", list[SCAN_NAMES] = "";
    fs_face_t *f;
    size_t used;

    if (find_tables(fd, dir, &t) != 0 || (f = face_room()) == NULL) {
        return;
    }
    t.name_len = t.name_len < NAME_BYTES ? t.name_len : NAME_BYTES;
    name = malloc(t.name_len + 1);
    if (name == NULL || rd(fd, t.name, name, t.name_len) != 0) {
        free(name);
        return;
    }
    read_names(name, t.name_len, f, list, style, sizeof style);
    read_style(fd, &t, f);
    if (t.os2 == 0) {
        style_guess(style, f);
    }
    for (used = 0; list[used]; used += strlen(list + used) + 1) {
    }
    f->names = malloc(used + 1);
    f->path = strdup(path);
    if (f->family[0] == '\0' || f->names == NULL || f->path == NULL) {
        free(f->names);
        free(f->path);
        free(name);
        return;
    }
    memcpy(f->names, list, used + 1);
    f->index = index;
    f->cff = t.cff;
    f->ncover = -1;
    index_.n++;
    add_instances(fd, &t, name, t.name_len, f, list);
    free(name);
}

/** @brief Whether @p name ends in .ttf, .ttc, .otf or .otc. */
static int
font_file(const char *name)
{
    const char *dot = strrchr(name, '.');

    return dot != NULL &&
           (strcasecmp(dot, ".ttf") == 0 || strcasecmp(dot, ".ttc") == 0 ||
            strcasecmp(dot, ".otf") == 0 || strcasecmp(dot, ".otc") == 0);
}

/** @brief Whether a file of @p leaf's name and @p size was indexed, as
 *         the same face in two applications' folders; noted if not. */
static int
file_seen(scan_t *s, const char *leaf, long long size)
{
    char key[320];
    int i;

    snprintf(key, sizeof key, "%s:%lld", leaf, size);
    for (i = 0; i < s->nfile; i++) {
        if (strcmp(s->file[i], key) == 0) {
            return 1;
        }
    }
    if (s->nfile == s->filecap) {
        int cap = s->filecap ? s->filecap * 2 : 256;
        char **g = realloc(s->file, (size_t)cap * sizeof *g);

        if (g == NULL) {
            return 0;
        }
        s->file = g;
        s->filecap = cap;
    }
    if ((s->file[s->nfile] = strdup(key)) != NULL) {
        s->nfile++;
    }
    return 0;
}

/** @brief Every face the file @p path holds: one, or a collection's. */
static void
add_file(const char *path)
{
    unsigned char h[12], off[4];
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    unsigned long n, i;

    if (fd < 0) {
        return;
    }
    if (rd(fd, 0, h, 12) == 0 && memcmp(h, "ttcf", 4) == 0) {
        n = be32(h + 8);
        for (i = 0; i < n && i < SCAN_FACES; i++) {
            if (rd(fd, 12 + 4 * (long)i, off, 4) == 0) {
                add_face(fd, path, (int)i, (long)be32(off));
            }
        }
    } else {
        add_face(fd, path, 0, 0);
    }
    close(fd);
}

/** @brief Whether the folder of @p st was walked already; noted if not. */
static int
dir_seen(scan_t *s, const struct stat *st)
{
    int i;

    for (i = 0; i < s->ndir; i++) {
        if (s->dir[i].dev == st->st_dev && s->dir[i].ino == st->st_ino) {
            return 1;
        }
    }
    if (s->ndir == s->dircap) {
        int cap = s->dircap ? s->dircap * 2 : 64;
        void *g = realloc(s->dir, (size_t)cap * sizeof *s->dir);

        if (g == NULL) {
            return 1;
        }
        s->dir = g;
        s->dircap = cap;
    }
    s->dir[s->ndir].dev = st->st_dev;
    s->dir[s->ndir++].ino = st->st_ino;
    return 0;
}

/** @brief The font files in folder @p dir and in the folders under it,
 *         @p depth levels down; a folder already walked is passed over. */
static void
walk(scan_t *s, const char *dir, int depth)
{
    struct stat st;
    struct dirent *e;
    DIR *d;
    char path[TIKU_FONTSET_PATH];

    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode) || dir_seen(s, &st) ||
        (d = opendir(dir)) == NULL) {
        return;
    }
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' ||
            snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >=
                (int)sizeof path ||
            stat(path, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode) && depth > 0) {
            walk(s, path, depth - 1);
        } else if (S_ISREG(st.st_mode) && font_file(e->d_name) &&
                   !file_seen(s, e->d_name, (long long)st.st_size)) {
            add_file(path);
        }
    }
    closedir(d);
}

/** @brief Each folder of the ':'-joined @p dirs, "~" read as home. */
static void
walk_list(scan_t *s, const char *dirs)
{
    const char *home = getenv("HOME");
    char one[TIKU_FONTSET_PATH];

    while (dirs != NULL && *dirs) {
        const char *colon = strchr(dirs, ':');
        size_t n = colon ? (size_t)(colon - dirs) : strlen(dirs);

        if (n > 0 && n < sizeof one) {
            if (dirs[0] == '~' && home != NULL) {
                snprintf(one, sizeof one, "%s%.*s", home, (int)n - 1, dirs + 1);
            } else {
                snprintf(one, sizeof one, "%.*s", (int)n, dirs);
            }
            walk(s, one, SCAN_DEPTH);
        }
        dirs = colon ? colon + 1 : NULL;
    }
}

/** @brief The index built: the folders asked for, the fonts folder, then
 *         the system's; or only the folders $TIKU_FONTSET_ONLY names. */
static void
build(void)
{
    const char *only = getenv("TIKU_FONTSET_ONLY");
    char dir[TIKU_FONTSET_PATH];
    scan_t s;
    size_t i;
    int k;

    memset(&s, 0, sizeof s);
    pthread_mutex_init(&index_.lock, NULL);
    if (only != NULL) {
        walk_list(&s, only);
    } else {
        walk_list(&s, extra);
        walk_list(&s, tiku_fonts_dir(dir, sizeof dir));
        for (i = 0; i < sizeof system_dirs / sizeof system_dirs[0]; i++) {
            walk_list(&s, system_dirs[i]);
        }
    }
    for (k = 0; k < s.nfile; k++) {
        free(s.file[k]);
    }
    free(s.file);
    free(s.dir);
}

fs_index_t *
fs_index(void)
{
    (void)pthread_once(&once, build);
    return &index_;
}

const char *
fs_extra_dirs(void)
{
    return extra;
}

void
tiku_fontset_dirs(const char *dirs)
{
    snprintf(extra, sizeof extra, "%s", dirs != NULL ? dirs : "");
}

int
tiku_fontset_count(void)
{
    return fs_index()->n;
}

/** @brief Range @p lo to @p hi appended, or joined to the last when they
 *         touch or overlap.  @return 0, or -1 out of memory. */
static int
add_range(uint32_t **r, int *n, int *cap, uint32_t lo, uint32_t hi)
{
    if (*n > 0 && lo <= (*r)[2 * *n - 1] + 1 && lo >= (*r)[2 * *n - 2]) {
        if (hi > (*r)[2 * *n - 1]) {
            (*r)[2 * *n - 1] = hi;
        }
        return 0;
    }
    if (*n == *cap) {
        int c = *cap ? *cap * 2 : 64;
        uint32_t *g = realloc(*r, (size_t)c * 2 * sizeof *g);

        if (g == NULL) {
            return -1;
        }
        *r = g;
        *cap = c;
    }
    (*r)[2 * *n] = lo;
    (*r)[2 * *n + 1] = hi;
    (*n)++;
    return 0;
}

/** @brief The ranges a format 4 or 12 subtable @p t maps, into @p f. */
static void
read_subtable(const unsigned char *t, unsigned long len, fs_face_t *f)
{
    unsigned format = len >= 2 ? be16(t) : 0;
    unsigned long i, n;
    int cap = 0;

    f->ncover = 0;
    if (format == 12 && len >= 16) {
        n = be32(t + 12);
        for (i = 0; i < n && 16 + 12 * (i + 1) <= len; i++) {
            (void)add_range(&f->cover, &f->ncover, &cap,
                            (uint32_t)be32(t + 16 + 12 * i),
                            (uint32_t)be32(t + 20 + 12 * i));
        }
    } else if (format == 4 && len >= 14) {
        n = be16(t + 6) / 2;
        for (i = 0; i < n && 16 + 8 * n <= len; i++) {
            uint32_t lo = be16(t + 16 + 2 * n + 2 * i);
            uint32_t hi = be16(t + 14 + 2 * i);
            unsigned delta = be16(t + 16 + 4 * n + 2 * i);
            unsigned offset = be16(t + 16 + 6 * n + 2 * i);
            uint32_t first = 0, last = 0;
            int active = 0;

            /* Format 4 segments must be disjoint and ordered. */
            if (lo > hi || (i && lo <= be16(t + 14 + 2 * (i - 1)))) {
                f->ncover = 0;
                break;
            }
            if (lo <= hi && lo != 0xffff) {
                for (uint32_t cp = lo; cp <= hi && cp < 0xffff; cp++) {
                    unsigned glyph = (cp + delta) & 0xffff;
                    if (offset) {
                        unsigned long at =
                            16 + 6 * n + 2 * i + offset + 2 * (cp - lo);
                        glyph = at + 2 <= len ? be16(t + at) : 0;
                        if (glyph)
                            glyph = (glyph + delta) & 0xffff;
                    }
                    if (glyph) {
                        if (!active)
                            first = cp;
                        last = cp;
                        active = 1;
                    } else if (active) {
                        (void)add_range(&f->cover, &f->ncover, &cap, first,
                                        last);
                        active = 0;
                    }
                }
                if (active)
                    (void)add_range(&f->cover, &f->ncover, &cap, first, last);
            }
        }
    }
}

/** @brief Where face @p index's table directory starts in file @p fd:
 *         a collection's offset for it, else the file's start. */
static long
face_dir(int fd, int index)
{
    unsigned char h[12];
    index &= 65535;

    if (rd(fd, 0, h, 12) == 0 && memcmp(h, "ttcf", 4) == 0 &&
        rd(fd, 12 + 4 * (long)index, h, 4) == 0) {
        return (long)be32(h);
    }
    return 0;
}

/** @brief Table @p tag's offset in the directory at @p dir, its length
 *         into @p len; 0 when there is none. */
static long
find_table(int fd, long dir, const char *tag, unsigned long *len)
{
    unsigned char h[16];
    unsigned i, n = rd(fd, dir, h, 12) == 0 ? be16(h + 4) : 0;

    for (i = 0; i < n && i < SCAN_TABLES; i++) {
        if (rd(fd, dir + 12 + 16 * (long)i, h, 16) == 0 &&
            memcmp(h, tag, 4) == 0) {
            *len = be32(h + 12);
            return (long)be32(h + 8);
        }
    }
    return 0;
}

/** @brief The best format 4 or 12 subtable of the cmap at @p cmap: the
 *         full Unicode one, else the BMP one, else the symbol one; 0 for
 *         none. */
static long
best_subtable(int fd, long cmap)
{
    unsigned char h[8];
    unsigned i, n = rd(fd, cmap, h, 4) == 0 ? be16(h + 2) : 0;
    long best = 0, at;
    int rank = 0, r;

    for (i = 0; i < n && i < 64; i++) {
        unsigned plat, enc, format;

        if (rd(fd, cmap + 4 + 8 * (long)i, h, 8) != 0) {
            break;
        }
        plat = be16(h);
        enc = be16(h + 2);
        at = cmap + (long)be32(h + 4);
        format = rd(fd, at, h, 2) == 0 ? be16(h) : 0;
        r = format != 4 && format != 12                           ? 0
            : (plat == 3 && enc == 10) || (plat == 0 && enc == 4) ? 3
            : (plat == 3 && enc == 1) || (plat == 0 && enc <= 3)  ? 2
            : plat == 3 && enc == 0                               ? 1
                                                                  : 0;
        if (r > rank) {
            rank = r;
            best = at;
        }
    }
    return best;
}

/** @brief The character map of face @p f read from its file: its best
 *         subtable's characters.  Nothing is covered when it cannot be
 *         read. */
static void
read_cover(fs_face_t *f)
{
    int fd = open(f->path, O_RDONLY | O_CLOEXEC);
    unsigned long cmap_len = 0, sub_len = 0;
    unsigned char h[8], *t = NULL;
    long cmap, best;

    f->ncover = 0;
    if (fd < 0) {
        return;
    }
    cmap = find_table(fd, face_dir(fd, f->index), "cmap", &cmap_len);
    best = cmap != 0 ? best_subtable(fd, cmap) : 0;
    if (best != 0 && rd(fd, best, h, 8) == 0) {
        sub_len = be16(h) >= 8 ? be32(h + 4) : be16(h + 2);
        sub_len = sub_len < CMAP_BYTES && sub_len <= cmap_len ? sub_len : 0;
        t = sub_len ? malloc(sub_len) : NULL;
        if (t != NULL && rd(fd, best, t, sub_len) == 0) {
            read_subtable(t, sub_len, f);
        }
    }
    free(t);
    close(fd);
}

int
fs_covers(fs_index_t *ix, fs_face_t *f, uint32_t cp)
{
    int lo = 0, hi;

    pthread_mutex_lock(&ix->lock); /* the map is read once */
    if (f->ncover < 0) {
        read_cover(f);
    }
    pthread_mutex_unlock(&ix->lock);
    hi = f->ncover - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;

        if (cp < f->cover[2 * mid]) {
            hi = mid - 1;
        } else if (cp > f->cover[2 * mid + 1]) {
            lo = mid + 1;
        } else {
            return 1;
        }
    }
    return 0;
}
