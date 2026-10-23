/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_archive.c - an archive opened and listed, its format found from
 * its first bytes; the streams the readers stack (a file, DEFLATE, gzip
 * with every member's CRC and size checked); stored names cleaned into
 * paths; and the folder an extraction goes into.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_archive_private.h"
#include "tiku_crypto.h"
#include "tiku_inflate.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* A file stream reads ahead FILE_BUF bytes; gzip may read PUSHBACK past
 * a member's end, which goes back in front of the next. */
#define FILE_BUF 65536
#define PUSHBACK (2 * 65536 + 64)

/** @brief qsort's order for spans: by where they start. */
static int
span_cmp(const void *x, const void *y)
{
    int64_t a = *(const int64_t *)x, b = *(const int64_t *)y;

    return (a > b) - (a < b);
}

long
arc_read(arc_stream_t *s, unsigned char *buf, long max)
{
    return s->read(s, buf, max);
}

int
arc_read_all(arc_stream_t *s, unsigned char *buf, long n)
{
    while (n > 0) {
        long got = s->read(s, buf, n);

        if (got <= 0) {
            if (s->err == TIKU_ARCHIVE_OK) {
                s->err = TIKU_ARCHIVE_DAMAGED;  /* it ended too soon */
            }
            return -1;
        }
        buf += got;
        n -= got;
    }
    return 0;
}

int
arc_skip(arc_stream_t *s, int64_t n)
{
    unsigned char junk[16384];

    if (s->skip != NULL) {
        return s->skip(s, n);
    }
    while (n > 0) {
        long got = s->read(s, junk, n < (int64_t)sizeof junk
                                        ? (long)n : (long)sizeof junk);

        if (got <= 0) {
            if (s->err == TIKU_ARCHIVE_OK) {
                s->err = TIKU_ARCHIVE_DAMAGED;
            }
            return -1;
        }
        n -= got;
    }
    return 0;
}

void
arc_close(arc_stream_t *s)
{
    if (s != NULL) {
        s->close(s);
    }
}

/** @brief A file's bytes, read ahead a buffer at a time. */
typedef struct {
    arc_stream_t   base;
    int            fd;
    int64_t        pos, end;        /* the next byte to read; the limit  */
    unsigned char *buf;
    long           at, len;         /* buffered bytes still to hand out  */
} file_stream_t;

/** @brief Up to @p max of the file's bytes, from the buffer first. */
static long
file_read(arc_stream_t *s, unsigned char *buf, long max)
{
    file_stream_t *f = (file_stream_t *)s;
    long n;

    if (f->at == f->len) {
        int64_t want = f->end - f->pos;
        ssize_t got;

        if (want <= 0) {
            return 0;
        }
        do {
            got = pread(f->fd, f->buf, (size_t)(want < FILE_BUF ? want
                                                                : FILE_BUF),
                        (off_t)f->pos);
        } while (got < 0 && errno == EINTR);
        if (got <= 0) {
            s->err = TIKU_ARCHIVE_DAMAGED;  /* short file, or a failed read */
            return -1;
        }
        f->pos += got;
        f->at = 0;
        f->len = (long)got;
        if (s->consumed != NULL) {
            *s->consumed += got;
        }
    }
    n = f->len - f->at < max ? f->len - f->at : max;
    memcpy(buf, f->buf + f->at, (size_t)n);
    f->at += n;
    return n;
}

/** @brief Past @p n bytes: through the buffer, then a jump. */
static int
file_skip(arc_stream_t *s, int64_t n)
{
    file_stream_t *f = (file_stream_t *)s;
    long in_buf = f->len - f->at;

    if (n <= in_buf) {
        f->at += (long)n;
        return 0;
    }
    n -= in_buf;
    f->at = f->len = 0;
    if (n > f->end - f->pos) {
        s->err = TIKU_ARCHIVE_DAMAGED;
        return -1;
    }
    f->pos += n;
    if (s->consumed != NULL) {
        *s->consumed += n;
    }
    return 0;
}

/** @brief Free the stream; the file stays open, the archive's. */
static void
file_close(arc_stream_t *s)
{
    free(((file_stream_t *)s)->buf);
    free(s);
}

arc_stream_t *
arc_file_stream(int fd, int64_t start, int64_t len, int64_t *consumed)
{
    struct stat st;

    if (start < 0 || len < -1 ||
        (len == -1 && (fstat(fd, &st) != 0 || st.st_size < start))) {
        return NULL;
    }
    if (len == -1) {
        len = st.st_size - start;
    }
    if (len > INT64_MAX - start) {
        return NULL;
    }
    file_stream_t *f = calloc(1, sizeof *f);

    if (f == NULL || (f->buf = malloc(FILE_BUF)) == NULL) {
        free(f);
        return NULL;
    }
    f->base.read = file_read;
    f->base.skip = file_skip;
    f->base.close = file_close;
    f->base.consumed = consumed;
    f->fd = fd;
    f->pos = start;
    f->end = start + len;
    return &f->base;
}

/** @brief DEFLATE over a stream. */
typedef struct {
    arc_stream_t     base;
    arc_stream_t    *under;
    tiku_inflater_t *z;
} inflate_stream_t;

/** @brief The inflater's input: the stream below's bytes. */
static long
inflate_fill(void *ctx, unsigned char *buf, long max)
{
    return arc_read(((inflate_stream_t *)ctx)->under, buf, max);
}

/** @brief Decoded bytes.  On failure @c err is the stream below's error,
 *         or DAMAGED when that has none. */
static long
inflate_read(arc_stream_t *s, unsigned char *buf, long max)
{
    inflate_stream_t *d = (inflate_stream_t *)s;
    long n = tiku_inflater_read(d->z, buf, max);

    if (n < 0) {
        s->err = d->under->err != TIKU_ARCHIVE_OK ? d->under->err
                                                  : TIKU_ARCHIVE_DAMAGED;
    }
    return n;
}

/** @brief Free the decoder and the stream below it. */
static void
inflate_close(arc_stream_t *s)
{
    inflate_stream_t *d = (inflate_stream_t *)s;

    tiku_inflater_free(d->z);
    arc_close(d->under);
    free(d);
}

arc_stream_t *
arc_inflate_stream(arc_stream_t *under, int deflate64)
{
    inflate_stream_t *d = under != NULL ? calloc(1, sizeof *d) : NULL;

    if (d == NULL) {
        arc_close(under);
        return NULL;
    }
    d->under = under;
    d->z = tiku_inflater_new(inflate_fill, d,
                             deflate64 ? TIKU_INFLATE_DEFLATE64 : 0);
    if (d->z == NULL) {
        arc_close(under);
        free(d);
        return NULL;
    }
    d->base.read = inflate_read;
    d->base.close = inflate_close;
    d->base.consumed = under->consumed;
    return &d->base;
}

/* --------------------------------------------------------------- gzip */

/** @brief gzip (RFC 1952): members one after another, each checked. */
typedef struct {
    arc_stream_t     base;
    arc_stream_t    *under;
    tiku_inflater_t *z;             /* the member being read, or NULL    */
    unsigned char   *pb;            /* bytes read past a member's end    */
    long             pb_at, pb_len;
    uint32_t         crc, size;     /* the member's, so far              */
    int              members, done;
    char            *name;          /* the first member's stored name    */
    size_t           name_max;
} gzip_stream_t;

/** @brief Bytes from below: those read past a member first. */
static long
gz_under(gzip_stream_t *g, unsigned char *buf, long max)
{
    if (g->pb_at < g->pb_len) {
        long n = g->pb_len - g->pb_at < max ? g->pb_len - g->pb_at : max;

        memcpy(buf, g->pb + g->pb_at, (size_t)n);
        g->pb_at += n;
        return n;
    }
    return arc_read(g->under, buf, max);
}

/** @brief The inflater's input. */
static long
gz_fill(void *ctx, unsigned char *buf, long max)
{
    return gz_under(ctx, buf, max);
}

/** @brief One byte from below.  @return it, or -1 at the end. */
static int
gz_byte(gzip_stream_t *g)
{
    unsigned char c;

    return gz_under(g, &c, 1) == 1 ? c : -1;
}

/** @brief Past a NUL-terminated field, kept in @p out when it is given.
 *  @return 0, or -1 when the bytes end first. */
static int
gz_string(gzip_stream_t *g, char *out, size_t max)
{
    size_t n = 0;
    int c;

    while ((c = gz_byte(g)) > 0) {
        if (out != NULL && n + 1 < max) {
            out[n++] = (char)c;
        }
    }
    if (out != NULL && max > 0) {
        out[n] = '\0';
    }
    return c == 0 ? 0 : -1;
}

/** @brief A member's header (RFC 1952 section 2.3), and its decoder.
 *  @return 1 for a member, 0 at the end, -1 for a broken one. */
static int
gz_header(gzip_stream_t *g)
{
    unsigned char h[10];
    long got = gz_under(g, h, 1);
    int flg, i;

    if (got <= 0) {
        return got < 0 || g->members == 0 ? -1 : 0;
    }
    if (h[0] != 0x1f && g->members > 0) {
        return 0;                   /* padding after the last: the end */
    }
    for (i = 1; i < 10; i++) {
        int c = gz_byte(g);

        if (c < 0) {
            return -1;
        }
        h[i] = (unsigned char)c;
    }
    flg = h[3];
    if (h[0] != 0x1f || h[1] != 0x8b || h[2] != 8 || (flg & 0xe0) != 0) {
        return -1;
    }
    if (flg & 4) {                  /* FEXTRA: a length, then its bytes */
        int lo = gz_byte(g), hi = gz_byte(g);

        for (i = 0; lo >= 0 && hi >= 0 && i < (lo | hi << 8); i++) {
            if (gz_byte(g) < 0) {
                return -1;
            }
        }
    }
    if (((flg & 8) && gz_string(g, g->members == 0 ? g->name : NULL,
                                g->name_max) != 0) ||
        ((flg & 16) && gz_string(g, NULL, 0) != 0) ||
        ((flg & 2) && (gz_byte(g) < 0 || gz_byte(g) < 0))) {
        return -1;
    }
    g->z = tiku_inflater_new(gz_fill, g, 0);
    g->crc = g->size = 0;
    return g->z != NULL ? 1 : -1;
}

/** @brief Restore read-ahead bytes and validate the member trailer.
 *  @return OK, NOMEM, DAMAGED, or CHECKSUM. */
static tiku_archive_err_t
gz_trailer(gzip_stream_t *g)
{
    unsigned char *next = malloc(PUSHBACK), t[8];
    long n = next != NULL ? tiku_inflater_tail(g->z, next, PUSHBACK) : -1;
    long i;

    if (next == NULL) {
        return TIKU_ARCHIVE_NOMEM;
    }
    if (n < 0) {
        free(next);
        return TIKU_ARCHIVE_DAMAGED;
    }
    if (g->pb_len - g->pb_at > PUSHBACK - n) {
        free(next);
        return TIKU_ARCHIVE_DAMAGED;
    }
    if (g->pb_len > g->pb_at)
        memcpy(next + n, g->pb + g->pb_at, (size_t)(g->pb_len - g->pb_at));
    g->pb_len = n + g->pb_len - g->pb_at;
    g->pb_at = 0;
    free(g->pb);
    g->pb = next;
    tiku_inflater_free(g->z);
    g->z = NULL;
    for (i = 0; i < 8; i++) {
        int c = gz_byte(g);

        if (c < 0) {
            return TIKU_ARCHIVE_DAMAGED;
        }
        t[i] = (unsigned char)c;
    }
    g->members++;
    return (t[0] | t[1] << 8 | t[2] << 16 | (uint32_t)t[3] << 24) == g->crc &&
           (t[4] | t[5] << 8 | t[6] << 16 | (uint32_t)t[7] << 24) == g->size
           ? TIKU_ARCHIVE_OK : TIKU_ARCHIVE_CHECKSUM;
}

/** @brief Decoded bytes, member after member. */
static long
gz_read(arc_stream_t *s, unsigned char *buf, long max)
{
    gzip_stream_t *g = (gzip_stream_t *)s;

    while (!g->done) {
        long n;

        if (g->z == NULL) {
            int h = gz_header(g);

            if (h <= 0) {
                g->done = 1;
                if (h < 0) {
                    s->err = TIKU_ARCHIVE_DAMAGED;
                    return -1;
                }
                return 0;
            }
        }
        n = tiku_inflater_read(g->z, buf, max);
        if (n > 0) {
            g->crc = tiku_crc32(g->crc, buf, (size_t)n);
            g->size += (uint32_t)n;
            return n;
        }
        tiku_archive_err_t err = n < 0 ? TIKU_ARCHIVE_DAMAGED : gz_trailer(g);

        if (err != TIKU_ARCHIVE_OK) {
            g->done = 1;
            s->err = err;
            if (g->under->err != TIKU_ARCHIVE_OK) {
                s->err = g->under->err;
            }
            return -1;
        }
    }
    return 0;
}

/** @brief Free the decoder, the stream below and the bytes read past a
 *         member's end. */
static void
gz_close(arc_stream_t *s)
{
    gzip_stream_t *g = (gzip_stream_t *)s;

    tiku_inflater_free(g->z);
    arc_close(g->under);
    free(g->pb);
    free(g);
}

arc_stream_t *
arc_gzip_stream(arc_stream_t *under, char *name, size_t max)
{
    gzip_stream_t *g = under != NULL ? calloc(1, sizeof *g) : NULL;

    if (g == NULL) {
        arc_close(under);
        return NULL;
    }
    g->under = under;
    g->name = name;
    g->name_max = max;
    if (name != NULL && max > 0) {
        name[0] = '\0';
    }
    g->base.read = gz_read;
    g->base.close = gz_close;
    g->base.consumed = under->consumed;
    return &g->base;
}

char *
arc_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = malloc(n);

    return d != NULL ? memcpy(d, s, n) : NULL;
}

/** @brief Whether @p part, @p n bytes, may be made on a disk: not "..",
 *         no control character, at most ARC_PART_MAX bytes. */
static int
part_ok(const char *part, size_t n)
{
    size_t i;

    if (n > ARC_PART_MAX || (n == 2 && part[0] == '.' && part[1] == '.')) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if ((unsigned char)part[i] < 0x20 || part[i] == 0x7f) {
            return 0;
        }
    }
    return 1;
}

char *
arc_clean_name(const char *raw, size_t n, int backslash, unsigned *flags,
               int *dir)
{
    char *out = malloc(n + 1);
    size_t i = 0, o = 0;

    if (out == NULL) {
        return NULL;
    }
    *dir = n > 0 && (raw[n - 1] == '/' || (backslash && raw[n - 1] == '\\'));
    if (n >= 2 && raw[1] == ':' && ((raw[0] | 0x20) >= 'a' &&
                                    (raw[0] | 0x20) <= 'z')) {
        i = 2;                      /* a drive: C: */
    }
    while (i < n) {
        size_t start = i;

        while (i < n && raw[i] != '/' && !(backslash && raw[i] == '\\')) {
            i++;
        }
        if (i - start == 0 || (i - start == 1 && raw[start] == '.')) {
            i++;
            continue;               /* empty and "." parts go */
        }
        if (!part_ok(raw + start, i - start)) {
            *flags |= TIKU_ARC_REFUSED;
        }
        if (o > 0) {
            out[o++] = '/';
        }
        while (start < i) {         /* kept printable, even refused */
            unsigned char c = (unsigned char)raw[start++];

            out[o++] = (char)(c < 0x20 || c == 0x7f ? '?' : c);
        }
        i++;
    }
    out[o] = '\0';
    if (o > ARC_PATH_MAX) {
        *flags |= TIKU_ARC_REFUSED;
    }
    return out;
}

int
arc_is_metadata(const char *path)
{
    const char *leaf = strrchr(path, '/');

    leaf = leaf != NULL ? leaf + 1 : path;
    return strncmp(path, "__MACOSX", 8) == 0 &&
           (path[8] == '/' || path[8] == '\0') ? 1
           : strncmp(leaf, "._", 2) == 0;
}

int
arc_spans_overlap(int64_t *span, long n)
{
    long i;

    qsort(span, (size_t)n, 2 * sizeof *span, span_cmp);
    for (i = 1; i < n; i++) {
        if (span[2 * i] < span[2 * i - 1]) {
            return 1;
        }
    }
    return 0;
}

arc_entry_t *
arc_add(tiku_archive_t *a)
{
    if (a->count == a->cap) {
        long cap = a->cap ? a->cap * 2 : 64;
        arc_entry_t *more = realloc(a->entry, (size_t)cap * sizeof *more);

        if (more == NULL) {
            return NULL;
        }
        a->entry = more;
        a->cap = cap;
    }
    memset(&a->entry[a->count], 0, sizeof a->entry[0]);
    a->entry[a->count].pub.size = -1;
    a->entry[a->count].pub.packed = -1;
    a->entry[a->count].pub.mtime = INT64_MIN;
    return &a->entry[a->count++];
}

/* --------------------------------------------------------------- open */

/** @brief The file's name without its folder. */
static const char *
leaf_of(const char *path)
{
    const char *slash = strrchr(path, '/');

    return slash != NULL ? slash + 1 : path;
}

/** @brief A gzip of one file: the entry, named by the member's stored
 *         name or the archive's own without ".gz". */
static tiku_archive_err_t
gz_single(tiku_archive_t *a, const char *stored)
{
    arc_entry_t *e = arc_add(a);
    char name[256];
    const char *base = stored[0] != '\0' ? leaf_of(stored)
                                         : leaf_of(a->path);
    size_t n = strlen(base);
    int dir;

    if (e == NULL) {
        return TIKU_ARCHIVE_NOMEM;
    }
    if (stored[0] == '\0' && n > 3 && strcasecmp(base + n - 3, ".gz") == 0) {
        n -= 3;
    }
    snprintf(name, sizeof name, "%.*s", (int)n, base);
    e->pub.path = arc_clean_name(name, strlen(name), 0, &e->pub.flags,
                                 &dir);
    if (e->pub.path == NULL) {
        return TIKU_ARCHIVE_NOMEM;
    }
    if (e->pub.path[0] == '\0') {
        e->pub.flags |= TIKU_ARC_REFUSED;
    }
    e->pub.packed = a->file_size;
    e->pub.method = "deflate";
    return TIKU_ARCHIVE_OK;
}

/** @brief List a gzip: a tar when its first 512 decoded bytes are a tar
 *         header, else one file. */
static tiku_archive_err_t
open_gzip(tiku_archive_t *a, tiku_archive_progress_t progress, void *ctx)
{
    unsigned char block[512];
    char stored[256];
    arc_stream_t *s = arc_gzip_stream(arc_file_stream(a->fd, 0,
                                      a->file_size, NULL), stored,
                                      sizeof stored);
    int tar;

    if (s == NULL) {
        return TIKU_ARCHIVE_NOMEM;
    }
    tar = arc_read_all(s, block, 512) == 0 && arc_tar_header_ok(block);
    arc_close(s);
    if (!tar) {
        a->format = TIKU_ARCHIVE_GZ;
        return gz_single(a, stored);
    }
    a->format = TIKU_ARCHIVE_TAR_GZ;
    return arc_tar_list(a, progress, ctx);
}

/** @brief Find the format from the first @p n bytes and list the archive;
 *         a file that is no gzip, ZIP or tar is tried as a ZIP with
 *         something before it.  @return OK, the listing's error, or
 *         NOT_YET for a format tiku_archive_identify() names, else
 *         UNKNOWN. */
static tiku_archive_err_t
sniff(tiku_archive_t *a, const unsigned char *head, long n,
      tiku_archive_progress_t progress, void *ctx)
{
    if (n >= 3 && head[0] == 0x1f && head[1] == 0x8b && head[2] == 8) {
        return open_gzip(a, progress, ctx);
    }
    if (n >= 4 && head[0] == 'P' && head[1] == 'K' &&
        ((head[2] == 3 && head[3] == 4) || (head[2] == 5 && head[3] == 6))) {
        a->format = TIKU_ARCHIVE_ZIP;
        return arc_zip_list(a);
    }
    if (n >= 512 && arc_tar_header_ok(head)) {
        a->format = TIKU_ARCHIVE_TAR;
        return arc_tar_list(a, progress, ctx);
    }
    a->format = TIKU_ARCHIVE_ZIP;   /* with data before it */
    if (arc_zip_list(a) == TIKU_ARCHIVE_OK) {
        return TIKU_ARCHIVE_OK;
    }
    return tiku_archive_identify(a->path) != NULL ? TIKU_ARCHIVE_NOT_YET
                                                  : TIKU_ARCHIVE_UNKNOWN;
}

tiku_archive_err_t
tiku_archive_open(const char *path, tiku_archive_progress_t progress,
                  void *ctx, tiku_archive_t **out)
{
    unsigned char head[512];
    struct stat st;
    tiku_archive_t *a;
    tiku_archive_err_t err;
    ssize_t n;

    *out = NULL;
    a = calloc(1, sizeof *a);
    if (a == NULL || (a->path = arc_strdup(path)) == NULL) {
        free(a);
        return TIKU_ARCHIVE_NOMEM;
    }
    a->fd = open(path, O_RDONLY | O_CLOEXEC);
    if (a->fd < 0 || fstat(a->fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        tiku_archive_close(a);
        return TIKU_ARCHIVE_MISSING;
    }
    a->file_size = (int64_t)st.st_size;
    n = pread(a->fd, head, sizeof head, 0);
    err = n > 0 ? sniff(a, head, (long)n, progress, ctx)
                : TIKU_ARCHIVE_UNKNOWN;
    if (err != TIKU_ARCHIVE_OK) {
        tiku_archive_close(a);
        return err;
    }
    *out = a;
    return TIKU_ARCHIVE_OK;
}

void
tiku_archive_close(tiku_archive_t *a)
{
    long i;

    if (a == NULL) {
        return;
    }
    for (i = 0; i < a->count; i++) {
        free((char *)a->entry[i].pub.path);
        free((char *)a->entry[i].pub.link);
    }
    free(a->entry);
    if (a->fd >= 0) {
        close(a->fd);
    }
    free(a->path);
    free(a);
}

tiku_archive_err_t
tiku_archive_open_package(const char *path, size_t file_bytes,
    size_t directory_bytes, size_t entries, tiku_archive_t **out)
{
    tiku_archive_t *a;
    struct stat st;
    unsigned char head[4];
    tiku_archive_err_t status;

    if (!out) return TIKU_ARCHIVE_DAMAGED;
    *out = NULL;
    if (!path || !*path || !file_bytes || !directory_bytes || !entries)
        return TIKU_ARCHIVE_DAMAGED;
    a = calloc(1, sizeof *a);
    if (!a) return TIKU_ARCHIVE_NOMEM;
    a->fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (a->fd < 0 || fstat(a->fd, &st) || !S_ISREG(st.st_mode)) {
        tiku_archive_close(a);
        return TIKU_ARCHIVE_MISSING;
    }
    if (st.st_size < 0 || (uint64_t)st.st_size > file_bytes) {
        tiku_archive_close(a);
        return TIKU_ARCHIVE_TOO_LARGE;
    }
    a->file_size = st.st_size;
    a->directory_limit = directory_bytes;
    a->entry_limit = entries;
    a->strict_zip = 1;
    a->format = TIKU_ARCHIVE_ZIP;
    status = pread(a->fd, head, 4, 0) == 4 &&
        (!memcmp(head, "PK\003\004", 4) ||
         !memcmp(head, "PK\005\006", 4))
        ? arc_zip_list(a) : TIKU_ARCHIVE_UNKNOWN;
    if (status != TIKU_ARCHIVE_OK) tiku_archive_close(a);
    else *out = a;
    return status;
}

tiku_archive_format_t
tiku_archive_format(const tiku_archive_t *a)
{
    return a->format;
}

const char *
tiku_archive_format_name(const tiku_archive_t *a)
{
    switch (a->format) {
    case TIKU_ARCHIVE_ZIP:    return "ZIP archive";
    case TIKU_ARCHIVE_TAR:    return "tar archive";
    case TIKU_ARCHIVE_TAR_GZ: return "gzip-compressed tar archive";
    case TIKU_ARCHIVE_GZ:     return "gzip-compressed file";
    }
    return "archive";
}

int
tiku_archive_damaged(const tiku_archive_t *a)
{
    return a->damaged;
}

long
tiku_archive_count(const tiku_archive_t *a)
{
    return a->count;
}

const tiku_archive_entry_t *
tiku_archive_entry(const tiku_archive_t *a, long i)
{
    return i >= 0 && i < a->count ? &a->entry[i].pub : NULL;
}

const char *
tiku_archive_strerror(tiku_archive_err_t e)
{
    static const char *const what[] = {
        "done", "the file could not be opened", "it is not an archive",
        "archives of this kind are not read yet", "the archive is damaged",
        "it is stored in a way not read yet", "it needs a password",
        "its contents do not match their checksum",
        "its name or link would reach outside the folder",
        "the disk refused it", "a later entry has the same name",
        "cancelled", "there is not enough memory",
        "it is larger than can be read here"
    };

    return (unsigned)e < sizeof what / sizeof what[0] ? what[e] : "failed";
}

const char *
tiku_archive_identify(const char *path)
{
    static const struct {
        const char *magic;
        size_t len;
        const char *what;
    } kinds[] = {
        { "\xfd" "7zXZ", 5, "an xz-compressed file" },
        { "BZh", 3, "a bzip2-compressed file" },
        { "7z\xbc\xaf\x27\x1c", 6, "a 7-Zip archive" },
        { "\x28\xb5\x2f\xfd", 4, "a zstd-compressed file" },
        { "Rar!\x1a\x07", 6, "a RAR archive" }
    };
    unsigned char h[8] = {0};
    size_t i;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    ssize_t n = fd >= 0 ? pread(fd, h, sizeof h, 0) : -1;

    if (fd >= 0) {
        close(fd);
    }
    for (i = 0; n >= 4 && i < sizeof kinds / sizeof kinds[0]; i++) {
        if (memcmp(h, kinds[i].magic, kinds[i].len) == 0) {
            return kinds[i].what;
        }
    }
    return NULL;
}

void
tiku_archive_folder_name(const char *path, char *out, size_t max)
{
    static const char *const suffix[] = {
        ".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst", ".tgz", ".tbz2",
        ".tbz", ".txz", ".tar", ".zip", ".gz", ".7z"
    };
    const char *leaf = leaf_of(path);
    size_t n = strlen(leaf), i;

    for (i = 0; i < sizeof suffix / sizeof suffix[0]; i++) {
        size_t k = strlen(suffix[i]);

        if (n >= k && strcasecmp(leaf + n - k, suffix[i]) == 0) {
            n -= k;
            break;
        }
    }
    if (n == 0) {
        snprintf(out, max, "Archive");
    } else {
        snprintf(out, max, "%.*s", (int)n, leaf);
    }
}

int
tiku_archive_new_folder(const char *parent, const char *name, char *out,
                        size_t max)
{
    int k;

    for (k = 1; k < 1000; k++) {
        int w = k == 1 ? snprintf(out, max, "%s/%s", parent, name)
                       : snprintf(out, max, "%s/%s %d", parent, name, k);

        if (w < 0 || (size_t)w >= max) {
            return -1;
        }
        if (mkdir(out, 0755) == 0) {
            return 0;
        }
        if (errno != EEXIST) {
            return -1;
        }
    }
    return -1;
}
