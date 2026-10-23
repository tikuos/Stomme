/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_archive_tar.c - tar (POSIX ustar and pax, GNU's long names and
 * big numbers, and the old v7 headers under them all), read as a stream,
 * a 512-byte header at a time: a plain one skipped through by seeking, a
 * gzipped one by decoding.  Pax and GNU records change the entry after
 * them, so they are folded into it, never listed; pax's global records
 * and volume labels are skipped.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_archive_private.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK     512
#define PAX_MAX   (1L << 20)        /* a pax record set read, the longest */

/** @brief What records before an entry say about it. */
typedef struct {
    char   *path, *link;            /* pax's or GNU's, or NULL           */
    int64_t size, mtime;            /* pax's, or -1 / INT64_MIN          */
    int     sparse;                 /* GNU sparse: data kept in pieces   */
} pending_t;

/** @brief A header field's number: octal, or GNU's base-256 when its
 *         first byte's top bit is set.  @return it, or -1 for garbage. */
static int64_t
number(const unsigned char *f, size_t n)
{
    int64_t v = 0;
    size_t i = 0;

    if (f[0] & 0x80) {
        if (f[0] != 0x80) {
            return -1;              /* negative, or too big to hold */
        }
        for (i = 1; i < n; i++) {
            if (v > (INT64_MAX >> 8)) {
                return -1;
            }
            v = v << 8 | f[i];
        }
        return v;
    }
    while (i < n && f[i] == ' ') {
        i++;
    }
    for (; i < n && f[i] >= '0' && f[i] <= '7'; i++) {
        if (v > (INT64_MAX >> 3)) {
            return -1;
        }
        v = v << 3 | (f[i] - '0');
    }
    return i == n || f[i] == ' ' || f[i] == '\0' ? v : -1;
}

int
arc_tar_header_ok(const unsigned char *block)
{
    unsigned long sum = 0;
    long ssum = 0;
    int64_t want = number(block + 148, 8);
    int i, zero = 1;

    for (i = 0; i < BLOCK; i++) {
        unsigned char c = i >= 148 && i < 156 ? ' ' : block[i];

        sum += c;
        ssum += (signed char)c;
        zero &= block[i] == 0;
    }
    /* Old writers summed signed chars; either sum is taken. */
    return !zero && want >= 0 &&
           (want == (int64_t)sum || want == (int64_t)ssum);
}

arc_stream_t *
arc_tar_stream(tiku_archive_t *a, int64_t *consumed)
{
    arc_stream_t *s = arc_file_stream(a->fd, 0, a->file_size, consumed);

    return a->format == TIKU_ARCHIVE_TAR_GZ ? arc_gzip_stream(s, NULL, 0)
                                            : s;
}

/** @brief An entry's data, @p n bytes, read whole as a record: a pax
 *         set or a GNU long name.  @return it, NUL-ended, or NULL. */
static char *
record(arc_tar_t *t, int64_t n, int64_t cap)
{
    int64_t pad = (BLOCK - n % BLOCK) % BLOCK;
    char *buf;

    if (n < 0 || n > cap || (buf = malloc((size_t)n + 1)) == NULL) {
        return NULL;
    }
    if (arc_read_all(t->s, (unsigned char *)buf, (long)n) != 0 ||
        arc_skip(t->s, pad) != 0) {
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    return buf;
}

/** @brief A decimal number of a pax value, its fraction ignored. */
static int64_t
pax_number(const char *v, int *ok)
{
    char *end;
    long long x = strtoll(v, &end, 10);

    *ok = end != v && (*end == '\0' || *end == '.');
    return (int64_t)x;
}

/** @brief Pax records (POSIX.1-2001) "LEN KEY=VALUE\n" into @p p.
 *  @return 0, or -1 for a broken set. */
static int
pax(char *set, int64_t n, pending_t *p)
{
    int64_t at = 0;

    while (at < n) {
        char *end, *key = set + at, *eq;
        long len = strtol(key, &end, 10);
        int ok = 1;

        if (end == key || *end != ' ' || len <= end - key || len > n - at ||
            set[at + len - 1] != '\n') {
            return -1;
        }
        set[at + len - 1] = '\0';
        key = end + 1;
        if ((eq = strchr(key, '=')) == NULL) {
            return -1;
        }
        *eq = '\0';
        if (!strcmp(key, "path") || !strcmp(key, "linkpath")) {
            char **dst = key[0] == 'p' ? &p->path : &p->link;

            free(*dst);
            *dst = arc_strdup(eq + 1);
        } else if (!strcmp(key, "size")) {
            p->size = pax_number(eq + 1, &ok);
        } else if (!strcmp(key, "mtime")) {
            p->mtime = pax_number(eq + 1, &ok);
        } else if (!strncmp(key, "GNU.sparse.", 11)) {
            p->sparse = 1;
        }
        if (!ok || p->size < -1) {
            return -1;
        }
        at += len;
    }
    return 0;
}

/** @brief A header's name: ustar's prefix and name joined, or the name
 *         alone; pax's or GNU's when one came before. */
static char *
header_name(const unsigned char *h, pending_t *p)
{
    char buf[256 + 1 + 100 + 1];
    size_t n = strnlen((const char *)h, 100);

    if (p->path != NULL) {
        char *s = p->path;

        p->path = NULL;
        return s;
    }
    if (!memcmp(h + 257, "ustar\0", 6) && h[345] != '\0') {
        size_t k = strnlen((const char *)h + 345, 155);

        snprintf(buf, sizeof buf, "%.*s/%.*s", (int)k, h + 345, (int)n, h);
    } else {
        snprintf(buf, sizeof buf, "%.*s", (int)n, h);
    }
    return arc_strdup(buf);
}

/** @brief The entry kind for tar type flag @p flag; devices, FIFOs and
 *         sparse or continued files are marked unreadable in @p flags. */
static int
kind(int flag, const char *raw_name, unsigned *flags)
{
    size_t n = strlen(raw_name);

    switch (flag) {
    case '1': return TIKU_ARC_HARDLINK;
    case '2': return TIKU_ARC_SYMLINK;
    case '5':
    case 'D': return TIKU_ARC_DIR;
    case '3':
    case '4':
    case '6':
        *flags |= TIKU_ARC_UNREADABLE;
        return TIKU_ARC_OTHER;      /* devices and FIFOs are not made */
    case 'S':
    case 'M':
        *flags |= TIKU_ARC_UNREADABLE;
        return TIKU_ARC_FILE;       /* sparse, continued: in pieces */
    }
    /* Anything else is a file (POSIX); an old one ends in '/' for a folder. */
    return flag == '\0' && n > 0 && raw_name[n - 1] == '/' ? TIKU_ARC_DIR
                                                           : TIKU_ARC_FILE;
}

/** @brief A listed entry from its header and the records before it.
 *  @return 1, or -1 for a bad size or name, or no memory. */
static int
entry_from(arc_tar_t *t, const unsigned char *h, pending_t *p,
           arc_entry_t *e)
{
    int64_t size = p->size >= 0 ? p->size : number(h + 124, 12);
    int64_t mtime = p->mtime != INT64_MIN ? p->mtime : number(h + 136, 12);
    char *raw = header_name(h, p), *name;
    int dir;

    if (raw == NULL || size < 0) {
        free(raw);
        return -1;
    }
    name = arc_utf8(raw, strlen(raw), 0);
    e->pub.type = kind(h[156], raw, &e->pub.flags);
    e->pub.path = name != NULL ? arc_clean_name(name, strlen(name), 0,
                                                &e->pub.flags, &dir) : NULL;
    free(raw);
    free(name);
    if (e->pub.path == NULL) {
        return -1;
    }
    if (dir) {
        e->pub.type = TIKU_ARC_DIR;
    }
    if (p->link != NULL || h[157] != '\0') {
        char lk[101];

        snprintf(lk, sizeof lk, "%.100s", h + 157);
        e->pub.link = p->link != NULL ? p->link : arc_strdup(lk);
        p->link = NULL;
    }
    if (p->sparse) {
        e->pub.flags |= TIKU_ARC_UNREADABLE;
    }
    if (arc_is_metadata(e->pub.path)) {
        e->pub.flags |= TIKU_ARC_METADATA;
    }
    e->pub.size = e->pub.type == TIKU_ARC_FILE ? size : 0;
    e->pub.mtime = mtime >= 0 || p->mtime != INT64_MIN ? mtime : INT64_MIN;
    e->pub.mode = (unsigned)(number(h + 100, 8) & 0777);
    e->offset = t->index++;
    t->left = size;
    t->pad = (BLOCK - size % BLOCK) % BLOCK;
    return 1;
}

/** @brief A record before the next entry, folded into @p p.
 *  @return 0, or -1 for a broken one. */
static int
fold(arc_tar_t *t, int flag, int64_t size, pending_t *p)
{
    char *data;

    if (flag == 'g' || flag == 'V') {
        return arc_skip(t->s, size + (BLOCK - size % BLOCK) % BLOCK);
    }
    data = record(t, size, flag == 'x' ? PAX_MAX : ARC_NAME_MAX);
    if (data == NULL) {
        return -1;
    }
    if (flag == 'x') {
        int ok = pax(data, size, p) == 0;

        free(data);
        return ok ? 0 : -1;
    }
    free(flag == 'L' ? p->path : p->link);
    *(flag == 'L' ? &p->path : &p->link) = data;
    return 0;
}

int
arc_tar_next(arc_tar_t *t, arc_entry_t *e)
{
    pending_t p = { NULL, NULL, -1, INT64_MIN, 0 };
    unsigned char h[BLOCK];
    int got = -1;

    if (arc_skip(t->s, t->left + t->pad) != 0) {
        return -1;
    }
    t->left = t->pad = 0;
    for (;;) {
        long n = arc_read(t->s, h, BLOCK);
        int64_t size;

        if (n == 0) {
            got = 0;                /* the end, unmarked */
            break;
        }
        if (n < 0 || (n < BLOCK && arc_read_all(t->s, h + n,
                                                BLOCK - n) != 0)) {
            break;
        }
        if (h[0] == '\0' && !arc_tar_header_ok(h)) {
            got = 0;                /* the end's zero blocks */
            break;
        }
        if (!arc_tar_header_ok(h) || (size = number(h + 124, 12)) < 0) {
            break;
        }
        if (strchr("xgLKV", h[156]) == NULL || h[156] == '\0') {
            got = entry_from(t, h, &p, e);
            break;
        }
        if (fold(t, h[156], size, &p) != 0) {
            break;
        }
    }
    free(p.path);
    free(p.link);
    return got;
}

long
arc_tar_read(arc_tar_t *t, unsigned char *buf, long max)
{
    long n;

    if (t->left <= 0) {
        return 0;
    }
    n = arc_read(t->s, buf, t->left < max ? (long)t->left : max);
    if (n > 0) {
        t->left -= n;
    }
    return n;
}

tiku_archive_err_t
arc_tar_list(tiku_archive_t *a, tiku_archive_progress_t progress, void *ctx)
{
    int64_t consumed = 0;
    arc_tar_t t = { arc_tar_stream(a, &consumed), 0, 0, 0 };
    tiku_archive_err_t err = TIKU_ARCHIVE_OK;
    arc_entry_t e;
    int got;

    if (t.s == NULL) {
        return TIKU_ARCHIVE_NOMEM;
    }
    memset(&e, 0, sizeof e);
    while ((got = arc_tar_next(&t, &e)) == 1) {
        arc_entry_t *slot;

        if (e.pub.path[0] == '\0' && e.pub.type == TIKU_ARC_DIR) {
            free((char *)e.pub.path);   /* "." is the destination itself */
            free((char *)e.pub.link);
            memset(&e, 0, sizeof e);
            continue;
        }
        slot = arc_add(a);

        if (slot == NULL) {
            free((char *)e.pub.path);
            free((char *)e.pub.link);
            err = TIKU_ARCHIVE_NOMEM;
            break;
        }
        e.pub.packed = a->format == TIKU_ARCHIVE_TAR ? e.pub.size : -1;
        e.pub.method = a->format == TIKU_ARCHIVE_TAR ? "stored" : "gzip";
        *slot = e;
        memset(&e, 0, sizeof e);
        if (progress != NULL && progress(ctx, consumed, a->file_size)) {
            err = TIKU_ARCHIVE_CANCELLED;
            break;
        }
    }
    if (got < 0 && err == TIKU_ARCHIVE_OK) {
        /* What came before a break stays listed; with nothing before
         * it, the tar is damaged. */
        a->damaged = 1;
        err = a->count > 0 ? TIKU_ARCHIVE_OK : TIKU_ARCHIVE_DAMAGED;
    }
    arc_close(t.s);
    return err;
}
