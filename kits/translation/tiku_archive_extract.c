/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_archive_extract.c - an archive's chosen entries made under a
 * destination folder.  Each is reached by walking down from the folder
 * with openat and O_NOFOLLOW, so no link is followed on the way; a link
 * made must point inside the folder; a file's data stops at the size its
 * entry gives, and a ZIP entry's CRC is checked; a file that fails is
 * removed.  Folders get their modes and times after their contents.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_archive_private.h"
#include "tiku_crypto.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define COPY_BUF 65536

/** @brief A folder whose mode and time are set once it is full. */
typedef struct {
    char    *path;
    unsigned mode;
    int64_t  mtime;
} dir_fix_t;

/** @brief An extraction under way. */
typedef struct {
    tiku_archive_t               *a;
    const tiku_archive_extract_t *opt;
    int                root;        /* the destination folder            */
    int64_t            done, total; /* bytes written, of those chosen    */
    int64_t            consumed;    /* the archive's bytes read          */
    int                by_input;    /* sizes unknown: go by bytes read   */
    tiku_archive_err_t first;       /* the first entry's trouble         */
    unsigned char     *made;        /* a byte an entry: made this time   */
    dir_fix_t         *dirs;
    long               ndirs, dcap;
    unsigned char     *buf;         /* COPY_BUF bytes to copy through    */
} ext_t;

/** @brief Pass @p why for entry @p i to the caller's report, and keep the
 *         first trouble other than REPLACED. */
static void
report(ext_t *x, long i, tiku_archive_err_t why)
{
    if (x->opt->report != NULL) {
        x->opt->report(x->opt->ctx, i, why);
    }
    if (x->first == TIKU_ARCHIVE_OK && why != TIKU_ARCHIVE_REPLACED) {
        x->first = why;
    }
}

/** @brief Report progress, by bytes written or, with a size unknown, by
 *         the archive's bytes read.  @return nonzero when the caller
 *         cancels. */
static int
cancelled(ext_t *x)
{
    return x->opt->progress != NULL &&
           x->opt->progress(x->opt->ctx, x->by_input ? x->consumed : x->done,
                            x->by_input ? x->a->file_size : x->total);
}

/** @brief Entry @p i's path as made, the strip folder taken off; NULL
 *         when the entry is not chosen. */
static const char *
chosen(const ext_t *x, long i)
{
    const tiku_archive_entry_t *e = &x->a->entry[i].pub;
    const char *strip = x->opt->strip;
    size_t n = strip != NULL ? strlen(strip) : 0;

    if ((x->opt->which != NULL && !x->opt->which[i]) ||
        ((e->flags & TIKU_ARC_METADATA) && !x->opt->metadata)) {
        return NULL;
    }
    if (n == 0) {
        return e->path[0] != '\0' ? e->path : NULL;
    }
    return strncmp(e->path, strip, n) == 0 && e->path[n] == '/' &&
           e->path[n + 1] != '\0' ? e->path + n + 1 : NULL;
}

/* ------------------------------------------------------------ walking */

/**
 * @brief Down from the destination to @p rel's folder, making the folders
 *        on the way, never through a link.  The last part goes to
 *        @p leaf.  @return the folder's descriptor, or -1 with @p why.
 */
static int
walk(ext_t *x, const char *rel, char *leaf, tiku_archive_err_t *why)
{
    int fd = dup(x->root);
    const char *p = rel, *slash;

    while (fd >= 0 && (slash = strchr(p, '/')) != NULL) {
        char part[ARC_PART_MAX + 1];
        int next;

        snprintf(part, sizeof part, "%.*s", (int)(slash - p), p);
        next = openat(fd, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW |
                                O_CLOEXEC);
        if (next < 0 && errno == ENOENT &&
            (mkdirat(fd, part, 0755) == 0 || errno == EEXIST)) {
            next = openat(fd, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW |
                                    O_CLOEXEC);
        }
        if (next < 0) {
            struct stat st;

            /* A link where a folder should be is UNSAFE, whatever errno
             * the host gives; anything else that stops the walk is WRITE. */
            *why = fstatat(fd, part, &st, AT_SYMLINK_NOFOLLOW) == 0 &&
                   S_ISLNK(st.st_mode) ? TIKU_ARCHIVE_UNSAFE
                                       : TIKU_ARCHIVE_WRITE;
        }
        close(fd);
        fd = next;
        p = slash + 1;
    }
    if (fd < 0 && *why == TIKU_ARCHIVE_OK) {
        *why = TIKU_ARCHIVE_WRITE;
    }
    snprintf(leaf, ARC_PART_MAX + 1, "%s", p);
    return fd;
}

/** @brief Clear @p leaf in @p dir for entry @p i: a file or link there is
 *         removed and REPLACED reported, so a later entry replaces an
 *         earlier one of the same name; a folder stays.
 *  @return 0 when the name is free; -1 when a folder holds it or the
 *          removal fails. */
static int
clear(ext_t *x, long i, int dir, const char *leaf)
{
    struct stat st;

    if (fstatat(dir, leaf, &st, AT_SYMLINK_NOFOLLOW) != 0) {
        return 0;
    }
    if (S_ISDIR(st.st_mode) || unlinkat(dir, leaf, 0) != 0) {
        return -1;
    }
    report(x, i, TIKU_ARCHIVE_REPLACED);
    return 0;
}

/**
 * @brief Whether a link at @p rel may point to @p target: relative, with
 *        no drive or backslash, never climbing above the destination
 *        when its parts are read from the link's own folder, and with no
 *        ".." after a named part, since that part may be a link.
 */
static int
link_ok(const char *rel, const char *target)
{
    long depth = 0;
    int named = 0;
    const char *p;

    if (target[0] == '\0' || target[0] == '/' || strchr(target, '\\') ||
        (target[0] != '\0' && target[1] == ':')) {
        return 0;
    }
    for (p = rel; (p = strchr(p, '/')) != NULL; p++) {
        depth++;                    /* the link's folder's depth */
    }
    for (p = target; *p != '\0';) {
        size_t n = strcspn(p, "/");

        if (n == 2 && p[0] == '.' && p[1] == '.') {
            if (named || --depth < 0) {
                return 0;
            }
        } else if (!(n == 0 || (n == 1 && p[0] == '.'))) {
            named = 1;
        }
        p += n + (p[n] == '/');
    }
    return 1;
}

/* -------------------------------------------------------------- files */

/**
 * @brief Entry @p i's bytes from @p data into a new file, checked against
 *        its size when it is known and its CRC when it has one: CHECKSUM
 *        when either differs.  A file that fails is removed.
 */
static tiku_archive_err_t
make_file(ext_t *x, long i, int dir, const char *leaf, arc_stream_t *data)
{
    const arc_entry_t *e = &x->a->entry[i];
    int64_t want = e->pub.size, got = 0;
    uint32_t crc = 0;
    tiku_archive_err_t err = TIKU_ARCHIVE_OK;
    int fd = openat(dir, leaf, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW |
                               O_CLOEXEC, 0600);

    if (fd < 0) {
        return errno == ELOOP ? TIKU_ARCHIVE_UNSAFE : TIKU_ARCHIVE_WRITE;
    }
    while (err == TIKU_ARCHIVE_OK) {
        long n = arc_read(data, x->buf, COPY_BUF);

        if (n <= 0) {
            err = n < 0 ? data->err : TIKU_ARCHIVE_OK;
            break;
        }
        if (want >= 0 && got + n > want) {
            err = TIKU_ARCHIVE_CHECKSUM;    /* more than it said: stop */
        } else if (write(fd, x->buf, (size_t)n) != (ssize_t)n) {
            err = TIKU_ARCHIVE_WRITE;
        }
        crc = tiku_crc32(crc, x->buf, (size_t)n);
        got += n;
        x->done += n;
        if (err == TIKU_ARCHIVE_OK && cancelled(x)) {
            err = TIKU_ARCHIVE_CANCELLED;
        }
    }
    if (err == TIKU_ARCHIVE_OK &&
        ((want >= 0 && got != want) || (e->has_crc && crc != e->crc))) {
        err = TIKU_ARCHIVE_CHECKSUM;
    }
    if (err == TIKU_ARCHIVE_OK) {
        struct timespec t[2];

        (void)fchmod(fd, e->pub.mode != 0 ? (e->pub.mode & 0777) | 0400
                                          : 0644);
        t[0].tv_sec = t[1].tv_sec = (time_t)e->pub.mtime;
        t[0].tv_nsec = t[1].tv_nsec = 0;
        if (e->pub.mtime != INT64_MIN) {
            (void)futimens(fd, t);
        }
    }
    if (close(fd) != 0 && err == TIKU_ARCHIVE_OK) {
        err = TIKU_ARCHIVE_WRITE;
    }
    if (err != TIKU_ARCHIVE_OK) {
        (void)unlinkat(dir, leaf, 0);
    }
    return err;
}

/** @brief A gzip's one file beside others: "name 2.ext" when "name.ext"
 *         is taken, never over it.  @return 0 with @p leaf set to a free
 *         name; -1 when none up to "name 999.ext" is. */
static int
unique_leaf(int dir, char *leaf)
{
    char base[ARC_PART_MAX + 1], try[ARC_PART_MAX + 1];
    const char *dot = strrchr(leaf, '.');
    size_t stem = dot != NULL && dot != leaf ? (size_t)(dot - leaf)
                                             : strlen(leaf);
    struct stat st;
    int k;

    snprintf(base, sizeof base, "%s", leaf);
    for (k = 1; k < 1000; k++) {
        if (k == 1) {
            snprintf(try, sizeof try, "%s", base);
        } else {
            snprintf(try, sizeof try, "%.*s %d%s", (int)stem, base, k,
                     base + stem);
        }
        if (fstatat(dir, try, &st, AT_SYMLINK_NOFOLLOW) != 0) {
            snprintf(leaf, ARC_PART_MAX + 1, "%s", try);
            return 0;
        }
    }
    return -1;
}

/** @brief A folder remembered, to be given its mode and time last. */
static void
remember_dir(ext_t *x, const char *rel, const tiku_archive_entry_t *e)
{
    if (x->ndirs == x->dcap) {
        long cap = x->dcap ? x->dcap * 2 : 32;
        dir_fix_t *more = realloc(x->dirs, (size_t)cap * sizeof *more);

        if (more == NULL) {
            return;                 /* the folder keeps its defaults */
        }
        x->dirs = more;
        x->dcap = cap;
    }
    x->dirs[x->ndirs].path = arc_strdup(rel);
    x->dirs[x->ndirs].mode = e->mode;
    x->dirs[x->ndirs].mtime = e->mtime;
    if (x->dirs[x->ndirs].path != NULL) {
        x->ndirs++;
    }
}

/** @brief A hard link to a file this extraction made, by its path.
 *  @return UNSAFE when no file made so far has that path. */
static tiku_archive_err_t
make_hardlink(ext_t *x, long i, int dir, const char *leaf)
{
    const char *target = x->a->entry[i].pub.link;
    tiku_archive_err_t why = TIKU_ARCHIVE_OK;
    char tleaf[ARC_PART_MAX + 1], *clean;
    unsigned flags = 0;
    int isdir, tdir = -1;
    long k;

    clean = target != NULL ? arc_clean_name(target, strlen(target), 0,
                                            &flags, &isdir) : NULL;
    for (k = 0; clean != NULL && k < i; k++) {
        const char *rel = x->made[k] ? chosen(x, k) : NULL;

        if (rel != NULL && x->a->entry[k].pub.type == TIKU_ARC_FILE &&
            strcmp(x->a->entry[k].pub.path, clean) == 0) {
            tdir = walk(x, rel, tleaf, &why);
            break;
        }
    }
    free(clean);
    if (tdir < 0) {
        return why != TIKU_ARCHIVE_OK ? why : TIKU_ARCHIVE_UNSAFE;
    }
    if (clear(x, i, dir, leaf) != 0 ||
        linkat(tdir, tleaf, dir, leaf, 0) != 0) {
        why = TIKU_ARCHIVE_WRITE;
    }
    close(tdir);
    return why;
}

/**
 * @brief Entry @p i made at @p rel: a folder, a file from @p data, or a
 *        link.  @return its trouble; OK when made.
 */
static tiku_archive_err_t
make_entry(ext_t *x, long i, const char *rel, arc_stream_t *data)
{
    const tiku_archive_entry_t *e = &x->a->entry[i].pub;
    tiku_archive_err_t why = TIKU_ARCHIVE_OK;
    char leaf[ARC_PART_MAX + 1];
    int dir = walk(x, rel, leaf, &why);

    if (dir < 0) {
        return why;
    }
    if (e->type == TIKU_ARC_DIR) {
        if (mkdirat(dir, leaf, 0755) != 0 && errno != EEXIST) {
            why = TIKU_ARCHIVE_WRITE;
        } else {
            remember_dir(x, rel, e);
        }
    } else if (e->type == TIKU_ARC_SYMLINK) {
        why = e->link == NULL || !link_ok(rel, e->link)
              ? TIKU_ARCHIVE_UNSAFE
              : clear(x, i, dir, leaf) != 0 ||
                symlinkat(e->link, dir, leaf) != 0 ? TIKU_ARCHIVE_WRITE
                                                   : TIKU_ARCHIVE_OK;
    } else if (e->type == TIKU_ARC_HARDLINK) {
        why = make_hardlink(x, i, dir, leaf);
    } else if (e->type != TIKU_ARC_FILE || data == NULL) {
        why = TIKU_ARCHIVE_UNSUPPORTED;
    } else if (x->a->format == TIKU_ARCHIVE_GZ ? unique_leaf(dir, leaf) != 0
                                               : clear(x, i, dir, leaf) != 0) {
        why = TIKU_ARCHIVE_WRITE;
    } else {
        why = make_file(x, i, dir, leaf, data);
    }
    close(dir);
    if (why == TIKU_ARCHIVE_OK) {
        x->made[i] = 1;
    }
    return why;
}

/** @brief Whether entry @p i can be made at all, before its data is read;
 *         what stops it is reported.  @return 1 when it can. */
static int
makeable(ext_t *x, long i)
{
    unsigned f = x->a->entry[i].pub.flags;
    tiku_archive_err_t why = f & TIKU_ARC_REFUSED ? TIKU_ARCHIVE_UNSAFE
                           : f & TIKU_ARC_ENCRYPTED ? TIKU_ARCHIVE_ENCRYPTED
                           : f & TIKU_ARC_UNREADABLE ? TIKU_ARCHIVE_UNSUPPORTED
                           : TIKU_ARCHIVE_OK;

    if (why != TIKU_ARCHIVE_OK) {
        report(x, i, why);
    }
    return why == TIKU_ARCHIVE_OK;
}

/** @brief Report entry @p i's trouble, if any.  @return 1 when the
 *         extraction must stop: CANCELLED or NOMEM. */
static int
settle(ext_t *x, long i, tiku_archive_err_t why)
{
    if (why == TIKU_ARCHIVE_OK) {
        return 0;
    }
    report(x, i, why);
    return why == TIKU_ARCHIVE_CANCELLED || why == TIKU_ARCHIVE_NOMEM;
}

/* ------------------------------------------------------------ formats */

/** @brief A ZIP's chosen entries, each read from where its data is. */
static tiku_archive_err_t
extract_zip(ext_t *x)
{
    long i;

    for (i = 0; i < x->a->count; i++) {
        const char *rel = chosen(x, i);
        tiku_archive_err_t why = TIKU_ARCHIVE_OK;
        arc_stream_t *data = NULL;

        if (rel == NULL || !makeable(x, i)) {
            continue;
        }
        if (x->a->entry[i].pub.type == TIKU_ARC_FILE &&
            (data = arc_zip_data(x->a, &x->a->entry[i], &why)) == NULL) {
            report(x, i, why);
            continue;
        }
        why = make_entry(x, i, rel, data);
        arc_close(data);
        if (settle(x, i, why)) {
            return why;
        }
    }
    return TIKU_ARCHIVE_OK;
}

/** @brief A tar entry's data as a stream of its own. */
typedef struct {
    arc_stream_t base;
    arc_tar_t   *t;
} tar_data_t;

/** @brief The current entry's bytes. */
static long
tar_data_read(arc_stream_t *s, unsigned char *buf, long max)
{
    tar_data_t *d = (tar_data_t *)s;
    long n = arc_tar_read(d->t, buf, max);

    if (n < 0) {
        s->err = d->t->s->err != TIKU_ARCHIVE_OK ? d->t->s->err
                                                 : TIKU_ARCHIVE_DAMAGED;
    }
    return n;
}

/** @brief A tar (or tar in gzip) read through once, the chosen made as
 *         they pass; it stops once the last chosen one is made. */
static tiku_archive_err_t
extract_tar(ext_t *x, long wanted)
{
    arc_tar_t t = { arc_tar_stream(x->a, &x->consumed), 0, 0, 0 };
    tar_data_t data = { { tar_data_read, NULL, NULL, TIKU_ARCHIVE_OK,
                          NULL }, &t };
    tiku_archive_err_t stop = TIKU_ARCHIVE_OK;
    arc_entry_t e;
    long i = 0;
    int got = 0;

    if (t.s == NULL) {
        return TIKU_ARCHIVE_NOMEM;
    }
    memset(&e, 0, sizeof e);
    while (wanted > 0 && (got = arc_tar_next(&t, &e)) == 1) {
        const char *rel;

        /* Headers and listed entries in step: the listing left out
         * only the archive's own folder. */
        while (i < x->a->count && x->a->entry[i].offset < e.offset) {
            i++;
        }
        rel = i < x->a->count && x->a->entry[i].offset == e.offset
              ? chosen(x, i) : NULL;
        free((char *)e.pub.path);
        free((char *)e.pub.link);
        memset(&e, 0, sizeof e);
        if (rel == NULL) {
            continue;
        }
        wanted--;
        if (makeable(x, i) &&
            settle(x, i, stop = make_entry(x, i, rel, &data.base))) {
            break;
        }
        stop = TIKU_ARCHIVE_OK;
    }
    if (wanted > 0 && stop == TIKU_ARCHIVE_OK && got < 0) {
        stop = TIKU_ARCHIVE_DAMAGED;
    }
    arc_close(t.s);
    return stop;
}

/** @brief Extract a gzip's one file. */
static tiku_archive_err_t
extract_gz(ext_t *x)
{
    const char *rel = chosen(x, 0);
    arc_stream_t *s;
    tiku_archive_err_t why;

    if (rel == NULL || !makeable(x, 0)) {
        return TIKU_ARCHIVE_OK;
    }
    s = arc_gzip_stream(arc_file_stream(x->a->fd, 0, x->a->file_size,
                                        &x->consumed), NULL, 0);
    if (s == NULL) {
        return TIKU_ARCHIVE_NOMEM;
    }
    why = make_entry(x, 0, rel, s);
    arc_close(s);
    return settle(x, 0, why) ? why : TIKU_ARCHIVE_OK;
}

/* ---------------------------------------------------------------- API */

/** @brief Set the folders' modes and times once every entry is made. */
static void
fix_dirs(ext_t *x)
{
    while (x->ndirs > 0) {
        dir_fix_t *d = &x->dirs[--x->ndirs];
        tiku_archive_err_t why = TIKU_ARCHIVE_OK;
        char leaf[ARC_PART_MAX + 1];
        int parent = walk(x, d->path, leaf, &why);
        int fd = parent >= 0 ? openat(parent, leaf, O_RDONLY | O_DIRECTORY |
                                      O_NOFOLLOW | O_CLOEXEC) : -1;

        if (fd >= 0) {
            struct timespec t[2];

            if (d->mode != 0) {
                (void)fchmod(fd, (d->mode & 0777) | 0700);
            }
            t[0].tv_sec = t[1].tv_sec = (time_t)d->mtime;
            t[0].tv_nsec = t[1].tv_nsec = 0;
            if (d->mtime != INT64_MIN) {
                (void)futimens(fd, t);
            }
            close(fd);
        }
        if (parent >= 0) {
            close(parent);
        }
        free(d->path);
    }
    free(x->dirs);
}

tiku_archive_err_t
tiku_archive_extract(tiku_archive_t *a, const char *dest,
                     const tiku_archive_extract_t *opt)
{
    static const tiku_archive_extract_t all;
    ext_t x;
    long i, wanted = 0;
    tiku_archive_err_t err;

    memset(&x, 0, sizeof x);
    x.a = a;
    x.opt = opt != NULL ? opt : &all;
    x.made = calloc((size_t)(a->count ? a->count : 1), 1);
    x.buf = malloc(COPY_BUF);
    x.root = open(dest, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (x.made == NULL || x.buf == NULL || x.root < 0) {
        err = x.root < 0 ? TIKU_ARCHIVE_WRITE : TIKU_ARCHIVE_NOMEM;
    } else {
        for (i = 0; i < a->count; i++) {
            if (chosen(&x, i) != NULL) {
                wanted++;
                x.by_input |= a->entry[i].pub.size < 0;
                x.total += a->entry[i].pub.size > 0 ? a->entry[i].pub.size : 0;
            }
        }
        a->consumed = &x.consumed;
        err = a->format == TIKU_ARCHIVE_ZIP ? extract_zip(&x)
            : a->format == TIKU_ARCHIVE_GZ ? extract_gz(&x)
            : extract_tar(&x, wanted);
        a->consumed = NULL;
        fix_dirs(&x);
    }
    if (x.root >= 0) {
        close(x.root);
    }
    free(x.made);
    free(x.buf);
    if (err == TIKU_ARCHIVE_CANCELLED || err == TIKU_ARCHIVE_NOMEM ||
        err == TIKU_ARCHIVE_WRITE) {
        return err;
    }
    return x.first != TIKU_ARCHIVE_OK ? x.first : err;
}
