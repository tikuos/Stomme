/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_archive_read.c - an entry found by its path, and an entry read
 * whole into memory, as an office document's XML and pictures are read
 * from its ZIP.
 *
 * The bytes come through the streams an extraction uses, bounded by the
 * caller's limit and by the entry's size, and are checked against that
 * size and, where the format keeps one, its CRC before they are returned.
 * A tar's entry is found by reading the tar up to it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_archive_private.h"
#include "tiku_crypto.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define READ_STEP 65536             /* first room for an unknown size    */

/** @brief Twice the room for an entry of unknown size, at most room for
 *         @p max + 1 bytes, one past the limit, and a zero byte.
 *  @return 0, or -1 with @p *buf freed. */
static int
grow(unsigned char **buf, size_t *cap, size_t max)
{
    size_t to = *cap * 2 > max + 2 ? max + 2 : *cap * 2;
    unsigned char *g = to > *cap ? realloc(*buf, to) : NULL;

    if (g == NULL) {
        free(*buf);
        *buf = NULL;
        return -1;
    }
    *buf = g;
    *cap = to;
    return 0;
}

/**
 * @brief @p s read to its end, at most @p max bytes of it, checked against
 *        @p e's size and CRC.  A stated size is read into room for exactly
 *        that, and a byte past it fails the check; an unknown one grows.
 * @return OK with the buffer in @p out, a zero byte after its end; else
 *         TOO_LARGE, NOMEM, CHECKSUM or the stream's error.
 */
static tiku_archive_err_t
slurp(arc_stream_t *s, const arc_entry_t *e, size_t max,
      unsigned char **out, size_t *len)
{
    int known = e->pub.size >= 0;
    size_t cap = known ? (size_t)e->pub.size + 1 : READ_STEP, n = 0;
    unsigned char *buf = malloc(cap), probe;
    uint32_t crc = 0;
    long got = 0;

    while (buf != NULL) {
        if (n + 1 >= cap && known) {
            got = arc_read(s, &probe, 1);   /* nothing past its size */
            break;
        }
        if (n + 1 >= cap && grow(&buf, &cap, max) != 0) {
            return n > max ? TIKU_ARCHIVE_TOO_LARGE : TIKU_ARCHIVE_NOMEM;
        }
        if ((got = arc_read(s, buf + n, (long)(cap - 1 - n))) <= 0) {
            break;
        }
        crc = tiku_crc32(crc, buf + n, (size_t)got);
        if ((n += (size_t)got) > max) {
            free(buf);
            return TIKU_ARCHIVE_TOO_LARGE;
        }
    }
    if (buf == NULL) {
        return TIKU_ARCHIVE_NOMEM;
    }
    if (got != 0 || (known && n != (size_t)e->pub.size) ||
        (e->has_crc && crc != e->crc)) {
        free(buf);
        return got < 0 && s->err != TIKU_ARCHIVE_OK ? s->err
               : TIKU_ARCHIVE_CHECKSUM;
    }
    buf[n] = '\0';
    *out = buf;
    *len = n;
    return TIKU_ARCHIVE_OK;
}

/** @brief The current entry's bytes, of a tar walked a header at a time. */
typedef struct {
    arc_stream_t base;
    arc_tar_t   *t;
} tar_part_t;

/** @brief Up to @p max of the current tar entry's bytes. */
static long
tar_part_read(arc_stream_t *s, unsigned char *buf, long max)
{
    tar_part_t *d = (tar_part_t *)s;
    long n = arc_tar_read(d->t, buf, max);

    if (n < 0) {
        s->err = TIKU_ARCHIVE_DAMAGED;
    }
    return n;
}

/** @brief Entry @p i of a tar: the tar read through up to its header, then
 *         its bytes. */
static tiku_archive_err_t
read_tar(tiku_archive_t *a, long i, size_t max, unsigned char **out,
         size_t *len)
{
    arc_tar_t t = { arc_tar_stream(a, NULL), 0, 0, 0 };
    tar_part_t part = { { tar_part_read, NULL, NULL, TIKU_ARCHIVE_OK,
                          NULL }, &t };
    tiku_archive_err_t err = TIKU_ARCHIVE_DAMAGED;
    arc_entry_t e;
    int got;

    if (t.s == NULL) {
        return TIKU_ARCHIVE_NOMEM;
    }
    memset(&e, 0, sizeof e);
    while ((got = arc_tar_next(&t, &e)) == 1) {
        int found = e.offset == a->entry[i].offset;

        free((char *)e.pub.path);
        free((char *)e.pub.link);
        memset(&e, 0, sizeof e);
        if (found) {
            err = slurp(&part.base, &a->entry[i], max, out, len);
            break;
        }
    }
    arc_close(t.s);
    return err;
}

long
tiku_archive_find(const tiku_archive_t *a, const char *path)
{
    long i;

    if (a == NULL || path == NULL) {
        return -1;
    }
    for (i = 0; i < a->count; i++) {
        if (strcmp(a->entry[i].pub.path, path) == 0) {
            return i;
        }
    }
    for (i = 0; i < a->count; i++) {
        if (strcasecmp(a->entry[i].pub.path, path) == 0) {
            return i;
        }
    }
    return -1;
}

tiku_archive_err_t
tiku_archive_read(tiku_archive_t *a, long i, size_t max,
                  unsigned char **out, size_t *len)
{
    const arc_entry_t *e;
    tiku_archive_err_t err;
    arc_stream_t *s;

    *out = NULL;
    *len = 0;
    if (a == NULL || i < 0 || i >= a->count) {
        return TIKU_ARCHIVE_MISSING;
    }
    e = &a->entry[i];
    if (e->pub.type != TIKU_ARC_FILE) {
        return TIKU_ARCHIVE_UNSUPPORTED;
    }
    if (e->pub.size >= 0 && (uint64_t)e->pub.size > max) {
        return TIKU_ARCHIVE_TOO_LARGE;
    }
    if (a->format == TIKU_ARCHIVE_TAR || a->format == TIKU_ARCHIVE_TAR_GZ) {
        return read_tar(a, i, max, out, len);
    }
    if (a->format == TIKU_ARCHIVE_GZ) {
        s = arc_gzip_stream(arc_file_stream(a->fd, 0, a->file_size, NULL),
                            NULL, 0);
        err = s != NULL ? TIKU_ARCHIVE_OK : TIKU_ARCHIVE_NOMEM;
    } else {
        s = arc_zip_data(a, e, &err);
    }
    if (s == NULL) {
        return err;
    }
    err = slurp(s, e, max, out, len);
    arc_close(s);
    return err;
}
