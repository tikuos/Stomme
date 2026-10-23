/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_archive_zip.c - ZIP (PKWARE's APPNOTE 6.3.10): listed from the
 * central directory at its end, ZIP64 included, offsets counted from
 * wherever the archive starts in its file.  A name is UTF-8 when it reads
 * as UTF-8, else DOS's code page 437 (Latin-1 when marked UTF-8); an
 * Info-ZIP UTF-8 name whose CRC matches takes its place.  An archive in
 * which two entries' data overlap, the construction of a zip bomb, is
 * refused as damaged.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_archive_private.h"
#include "tiku_crypto.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define EOCD_MIN   22               /* the end record, without comment   */
#define EOCD_SCAN  (EOCD_MIN + 65535)
#define ENTRY_MIN  46               /* a central record, without names   */
#define ENTRIES_MAX 10000000L

/** @brief A little-endian 16-bit number out of a record. */
static uint32_t
le16(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

/** @brief A little-endian 32-bit number out of a record. */
static uint32_t
le32(const unsigned char *p)
{
    return le16(p) | le16(p + 2) << 16;
}

/** @brief A little-endian 64-bit number out of a record. */
static uint64_t
le64(const unsigned char *p)
{
    return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32;
}

/** @brief @p n bytes at @p off.  @return 0, or -1 when fewer are there. */
static int
read_at(const tiku_archive_t *a, int64_t off, unsigned char *buf, size_t n)
{
    ssize_t got;

    if (off < 0 || off > a->file_size || (int64_t)n > a->file_size - off) {
        return -1;
    }
    got = pread(a->fd, buf, n, (off_t)off);
    return got == (ssize_t)n ? 0 : -1;
}

/** @brief Where the central directory is, its size and how many entries
 *         it has, from the end record found nearest the file's end, and
 *         in @p at where that record is.
 *  @return 0, -1 when no end record fits, or -2 on allocation refusal. */
static int
find_directory(tiku_archive_t *a, int64_t *cd_off, int64_t *cd_size,
               int64_t *entries, int64_t *at)
{
    int64_t scan = a->file_size < EOCD_SCAN ? a->file_size : EOCD_SCAN;
    unsigned char *tail = scan >= EOCD_MIN ? malloc((size_t)scan) : NULL;
    int64_t i;

    if (scan < EOCD_MIN)
        return -1;
    if (tail == NULL)
        return -2;
    if (read_at(a, a->file_size - scan, tail, (size_t)scan) != 0) {
        free(tail);
        return -1;
    }
    for (i = scan - EOCD_MIN; i >= 0; i--) {
        if (le32(tail + i) == 0x06054b50u &&
            i + EOCD_MIN + (int64_t)le16(tail + i + 20) <= scan) {
            if (a->strict_zip &&
                (i + EOCD_MIN + le16(tail + i + 20) != scan ||
                 le16(tail + i + 4) || le16(tail + i + 6) ||
                 le16(tail + i + 8) != le16(tail + i + 10)))
                continue;
            break;
        }
    }
    if (i < 0) {
        free(tail);
        return -1;
    }
    *entries = le16(tail + i + 10);
    *cd_size = le32(tail + i + 12);
    *cd_off = le32(tail + i + 16);
    *at = a->file_size - scan + i;
    free(tail);
    return 0;
}

/** @brief ZIP64's end record, when its locator stands just before the end
 *         record at @p at: its fields replace the end record's.
 *  @return 0, also when there is no locator; -1 when the record it
 *          locates is not there. */
static int
find_zip64(tiku_archive_t *a, int64_t *cd_off, int64_t *cd_size,
           int64_t *entries, int64_t *at)
{
    unsigned char loc[20], rec[56];
    int64_t where;

    if (*at < 20 || read_at(a, *at - 20, loc, 20) != 0 ||
        le32(loc) != 0x07064b50u) {
        return 0;                   /* no locator: not ZIP64 */
    }
    where = (int64_t)le64(loc + 8);
    /* The record is looked for just before its locator first: the
     * locator's own offset is right only when nothing comes before the
     * archive in its file. */
    if (read_at(a, *at - 20 - 56, rec, 56) == 0 &&
        le32(rec) == 0x06064b50u) {
        where = *at - 20 - 56;
    } else if (read_at(a, where, rec, 56) != 0 ||
               le32(rec) != 0x06064b50u) {
        return -1;
    }
    *entries = (int64_t)le64(rec + 32);
    *cd_size = (int64_t)le64(rec + 40);
    *cd_off = (int64_t)le64(rec + 48);
    *at = where;
    return *entries < 0 || *cd_size < 0 || *cd_off < 0 ? -1 : 0;
}

/* UTF-8 archive entry names. */

/** @brief CP437's upper half, as Unicode. */
static const uint16_t cp437[128] = {
    0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7, 0x00ea,
    0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5, 0x00c9, 0x00e6,
    0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9, 0x00ff, 0x00d6, 0x00dc,
    0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192, 0x00e1, 0x00ed, 0x00f3, 0x00fa,
    0x00f1, 0x00d1, 0x00aa, 0x00ba, 0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc,
    0x00a1, 0x00ab, 0x00bb, 0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561,
    0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b,
    0x2510, 0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f,
    0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567, 0x2568,
    0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b, 0x256a, 0x2518,
    0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580, 0x03b1, 0x00df, 0x0393,
    0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4, 0x03a6, 0x0398, 0x03a9, 0x03b4,
    0x221e, 0x03c6, 0x03b5, 0x2229, 0x2261, 0x00b1, 0x2265, 0x2264, 0x2320,
    0x2321, 0x00f7, 0x2248, 0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2,
    0x25a0, 0x00a0
};

/** @brief Whether @p n bytes have UTF-8's shape: each lead byte followed
 *         by its continuation bytes.  Overlong forms and surrogates pass. */
static int
is_utf8(const unsigned char *s, size_t n)
{
    size_t i = 0;

    while (i < n) {
        unsigned c = s[i++];
        int more;

        if (c < 0x80) {
            continue;
        }
        more = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2
             : c >= 0xf0 && c <= 0xf4 ? 3 : -1;
        if (more < 0 || n - i < (size_t)more) {
            return 0;
        }
        while (more-- > 0) {
            if ((s[i++] & 0xc0) != 0x80) {
                return 0;
            }
        }
    }
    return 1;
}

char *
arc_utf8(const char *raw, size_t n, int cp)
{
    const unsigned char *s = (const unsigned char *)raw;
    char *out;
    size_t i, o = 0;

    if (is_utf8(s, n)) {
        out = malloc(n + 1);
        if (out != NULL) {
            memcpy(out, raw, n);
            out[n] = '\0';
        }
        return out;
    }
    if ((out = malloc(3 * n + 1)) == NULL) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        unsigned u = s[i] < 0x80 ? s[i] : cp ? cp437[s[i] - 0x80] : s[i];

        if (u < 0x80) {
            out[o++] = (char)u;
        } else if (u < 0x800) {
            out[o++] = (char)(0xc0 | u >> 6);
            out[o++] = (char)(0x80 | (u & 0x3f));
        } else {
            out[o++] = (char)(0xe0 | u >> 12);
            out[o++] = (char)(0x80 | ((u >> 6) & 0x3f));
            out[o++] = (char)(0x80 | (u & 0x3f));
        }
    }
    out[o] = '\0';
    return out;
}

/* ZIP central directory entries. */

/** @brief A method's name, for show. */
static const char *
method_name(int m)
{
    switch (m) {
    case 0:  return "stored";
    case 8:  return "deflate";
    case 9:  return "deflate64";
    case 12: return "bzip2";
    case 14: return "LZMA";
    case 93: return "zstd";
    case 95: return "xz";
    case 98: return "PPMd";
    case 99: return "AES";
    }
    return "unknown";
}

/** @brief DOS's local date and time, in seconds since 1970; INT64_MIN
 *         for no date. */
static int64_t
dos_time(uint32_t time, uint32_t date)
{
    struct tm tm;

    if (date == 0) {
        return INT64_MIN;
    }
    memset(&tm, 0, sizeof tm);
    tm.tm_year = (int)((date >> 9) & 0x7f) + 80;
    tm.tm_mon = (int)((date >> 5) & 0xf) - 1;
    tm.tm_mday = (int)(date & 0x1f);
    tm.tm_hour = (int)((time >> 11) & 0x1f);
    tm.tm_min = (int)((time >> 5) & 0x3f);
    tm.tm_sec = (int)(time & 0x1f) * 2;
    tm.tm_isdst = -1;
    return (int64_t)mktime(&tm);
}

/** @brief What a central record's fields say, the extras applied. */
typedef struct {
    uint64_t usize, csize, offset;
    int64_t  mtime;
    int no_memory; /**< Unicode extra decoding allocation failed */
    char    *unicode;               /* Info-ZIP's UTF-8 name, or NULL    */
} zip_fields_t;

/** @brief The extra fields (APPNOTE 4.5) a listing uses: ZIP64's sizes
 *         and offset, times, and a UTF-8 name checked by its CRC. */
static void
extras(const unsigned char *x, size_t n, const unsigned char *name,
       size_t nlen, zip_fields_t *f)
{
    while (n >= 4) {
        uint32_t id = le16(x), len = le16(x + 2);
        const unsigned char *d = x + 4;
        size_t k = 0;

        if (len > n - 4) {
            return;
        }
        if (id == 0x0001) {         /* ZIP64: the overflowed ones, in order */
            if (f->usize == 0xffffffffu && k + 8 <= len) {
                f->usize = le64(d + k), k += 8;
            }
            if (f->csize == 0xffffffffu && k + 8 <= len) {
                f->csize = le64(d + k), k += 8;
            }
            if (f->offset == 0xffffffffu && k + 8 <= len) {
                f->offset = le64(d + k);
            }
        } else if (id == 0x5455 && len >= 5 && (d[0] & 1)) {
            f->mtime = (int64_t)(int32_t)le32(d + 1);
        } else if (id == 0x000a && len >= 32 && le16(d + 4) == 1 &&
                   le16(d + 6) >= 24 && f->mtime == INT64_MIN) {
            f->mtime = (int64_t)(le64(d + 8) / 10000000u) - 11644473600LL;
        } else if (id == 0x7075 && len >= 5 && d[0] == 1 &&
                   le32(d + 1) == tiku_crc32(0, name, nlen)) {
            free(f->unicode);
            f->unicode = arc_utf8((const char *)d + 5, len - 5, 0);
            f->no_memory |= f->unicode == NULL;
        }
        x += 4 + len;
        n -= 4 + len;
    }
}

/** @brief One central record at @p r into an entry.  @return its length,
 *         -1 for a broken record, or -2 for an allocation refusal. */
static long
entry_from(tiku_archive_t *a, const unsigned char *r, size_t room)
{
    zip_fields_t f = {.mtime = INT64_MIN};
    uint32_t made = le16(r + 4) >> 8, flags = le16(r + 8), attr;
    size_t nlen = le16(r + 28), xlen = le16(r + 30), clen = le16(r + 32);
    arc_entry_t *e;
    char *name;
    int dir;

    if (le32(r) != 0x02014b50u || ENTRY_MIN + nlen + xlen + clen > room)
        return -1;
    e = arc_add(a);
    if (!e)
        return -2;
    f.usize = le32(r + 24);
    f.csize = le32(r + 20);
    f.offset = le32(r + 42);
    extras(r + ENTRY_MIN + nlen, xlen, r + ENTRY_MIN, nlen, &f);
    if (f.no_memory) {
        free(f.unicode);
        return -2;
    }
    attr = le32(r + 38);
    name = f.unicode != NULL ? f.unicode
         : arc_utf8((const char *)r + ENTRY_MIN, nlen, !(flags & 0x800));
    e->pub.path = name != NULL ? arc_clean_name(name, strlen(name),
        made == 0 || made == 6 || made == 10 || made == 14,
        &e->pub.flags, &dir) : NULL;
    if (!e->pub.path) {
        free(name);
        return -2;
    }
    if (a->strict_zip && (strlen(e->pub.path) != nlen ||
                          memcmp(e->pub.path, r + ENTRY_MIN, nlen) ||
                          le16(r + 34) || (flags & ~0x808u))) {
        free(name);
        return -1;
    }
    free(name);
    if (e->pub.path == NULL || f.usize > INT64_MAX || f.csize > INT64_MAX ||
        f.offset > INT64_MAX) {
        return -1;
    }
    e->method = (int)le16(r + 10);
    e->zip_flags = flags;
    e->pub.method = method_name(e->method);
    e->pub.size = (int64_t)f.usize;
    e->pub.packed = (int64_t)f.csize;
    e->pub.mtime = f.mtime != INT64_MIN ? f.mtime
                                        : dos_time(le16(r + 12), le16(r + 14));
    e->crc = le32(r + 16);
    e->has_crc = 1;
    e->offset = (int64_t)f.offset + a->zip_base;
    if (made == 3 || made == 19) {  /* Unix and macOS keep st_mode */
        e->pub.mode = (attr >> 16) & 0777;
        dir |= ((attr >> 16) & 0170000) == 0040000;
        if (((attr >> 16) & 0170000) == 0120000) {
            e->pub.type = TIKU_ARC_SYMLINK;
        }
    }
    dir |= (made == 0 || made == 10 || made == 14) && (attr & 0x10);
    if (dir) {
        e->pub.type = TIKU_ARC_DIR;
    }
    if ((flags & 0x41) || e->method == 99) {
        e->pub.flags |= TIKU_ARC_ENCRYPTED;
    } else if (e->method != 0 && e->method != 8 && e->method != 9) {
        e->pub.flags |= TIKU_ARC_UNREADABLE;
    }
    if (arc_is_metadata(e->pub.path)) {
        e->pub.flags |= TIKU_ARC_METADATA;
    }
    if (e->pub.path[0] == '\0' && e->pub.type == TIKU_ARC_DIR) {
        free((char *)e->pub.path);  /* "/" is the destination itself */
        a->count--;
    }
    return (long)(ENTRY_MIN + nlen + xlen + clen);
}

/** @brief Where each entry's data starts, from its local header, and
 *         whether any two entries' spans overlap.  @return 0, or -1 for a
 *         missing local header, data running into the directory, an
 *         or overlap; -2 when its span storage is refused. */
static int
place_data(tiku_archive_t *a)
{
    int64_t *span = malloc((size_t)(a->count ? a->count : 1) * 2 *
                           sizeof *span);
    long i;

    if (span == NULL) {
        return -2;
    }
    for (i = 0; i < a->count; i++) {
        arc_entry_t *e = &a->entry[i];
        unsigned char h[30];

        if (read_at(a, e->offset, h, 30) != 0 || le32(h) != 0x04034b50u) {
            free(span);
            return -1;
        }
        if (a->strict_zip) {
            unsigned char name[ARC_PATH_MAX + 1];
            size_t n = strlen(e->pub.path);

            if (n > ARC_PATH_MAX || le16(h + 26) != n ||
                read_at(a, e->offset + 30, name, n) ||
                memcmp(name, e->pub.path, n) ||
                le16(h + 8) != (unsigned)e->method ||
                le16(h + 6) != e->zip_flags ||
                (le16(h + 6) & ~0x808u) ||
                (!(le16(h + 6) & 8) &&
                 (le32(h + 14) != e->crc ||
                  le32(h + 18) != e->pub.packed ||
                  le32(h + 22) != e->pub.size))) {
                free(span);
                return -1;
            }
        }
        span[2 * i] = e->offset;
        e->offset += 30 + (int64_t)le16(h + 26) + le16(h + 28);
        span[2 * i + 1] = e->offset + e->pub.packed;
        if (e->pub.packed < 0 || span[2 * i + 1] > a->zip_cd) {
            free(span);
            return -1;              /* its data runs into the directory */
        }
    }
    i = arc_spans_overlap(span, a->count);
    free(span);
    return i ? -1 : 0;
}

tiku_archive_err_t
arc_zip_list(tiku_archive_t *a)
{
    int64_t cd_off, cd_size, entries, at, pos = 0;
    unsigned char *cd;

    int directory = find_directory(a, &cd_off, &cd_size, &entries, &at);
    if (directory == -2)
        return TIKU_ARCHIVE_NOMEM;
    if (directory != 0 ||
        find_zip64(a, &cd_off, &cd_size, &entries, &at) != 0 || cd_size > at ||
        entries > ENTRIES_MAX || entries * ENTRY_MIN > cd_size) {
        return TIKU_ARCHIVE_DAMAGED;
    }
    /* Where the directory is, against where it says: the difference is
     * whatever stands before the archive (a self-extractor's code). */
    a->zip_cd = at - cd_size;
    a->zip_base = a->zip_cd - cd_off;
    if ((a->directory_limit && (uint64_t)cd_size > a->directory_limit) ||
        (a->entry_limit && (uint64_t)entries > a->entry_limit))
        return TIKU_ARCHIVE_TOO_LARGE;
    if (a->strict_zip && a->zip_base)
        return TIKU_ARCHIVE_DAMAGED;
    if (a->zip_base < 0 ||
        (cd = malloc((size_t)(cd_size ? cd_size : 1))) == NULL) {
        return a->zip_base < 0 ? TIKU_ARCHIVE_DAMAGED : TIKU_ARCHIVE_NOMEM;
    }
    if (read_at(a, a->zip_cd, cd, (size_t)cd_size) != 0) {
        free(cd);
        return TIKU_ARCHIVE_DAMAGED;
    }
    for (; entries > 0; entries--) {
        long used = cd_size - pos >= ENTRY_MIN
                    ? entry_from(a, cd + pos, (size_t)(cd_size - pos)) : -1;

        if (used < 0) {
            free(cd);
            return used == -2 ? TIKU_ARCHIVE_NOMEM : TIKU_ARCHIVE_DAMAGED;
        }
        pos += used;
    }
    free(cd);
    if (a->strict_zip && pos != cd_size)
        return TIKU_ARCHIVE_DAMAGED;
    int placed = place_data(a);
    return placed == 0    ? arc_zip_links(a)
           : placed == -2 ? TIKU_ARCHIVE_NOMEM
                          : TIKU_ARCHIVE_DAMAGED;
}

arc_stream_t *
arc_zip_data(tiku_archive_t *a, const arc_entry_t *e,
             tiku_archive_err_t *why)
{
    arc_stream_t *s;

    if (e->pub.flags & TIKU_ARC_ENCRYPTED) {
        *why = TIKU_ARCHIVE_ENCRYPTED;
        return NULL;
    }
    if (e->method != 0 && e->method != 8 && e->method != 9) {
        *why = TIKU_ARCHIVE_UNSUPPORTED;
        return NULL;
    }
    s = arc_file_stream(a->fd, e->offset, e->pub.packed, a->consumed);
    if (s != NULL && e->method != 0) {
        s = arc_inflate_stream(s, e->method == 9);
    }
    *why = s != NULL ? TIKU_ARCHIVE_OK : TIKU_ARCHIVE_NOMEM;
    return s;
}

tiku_archive_err_t
arc_zip_links(tiku_archive_t *a)
{
    long i;

    for (i = 0; i < a->count; i++) {
        arc_entry_t *e = &a->entry[i];
        tiku_archive_err_t why;
        arc_stream_t *s;
        char target[ARC_LINK_MAX + 1];
        long n;

        if (e->pub.type != TIKU_ARC_SYMLINK || e->pub.size > ARC_LINK_MAX ||
            (s = arc_zip_data(a, e, &why)) == NULL) {
            continue;
        }
        n = arc_read(s, (unsigned char *)target, ARC_LINK_MAX);
        arc_close(s);
        if (n > 0) {
            target[n] = '\0';
            e->pub.link = arc_utf8(target, (size_t)n, 0);
        }
    }
    return TIKU_ARCHIVE_OK;
}
