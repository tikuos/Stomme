/* tiku_circuit_source.c -- the import resolver: the chosen file, and the
 * files it links within its directory, which are opened without following
 * symlinks.  No network address, expansion or absolute link is accepted.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_circuit_source.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define INPUT_LIMIT (8u * 1024u * 1024u)   /* the largest file read */
#define READ_CHUNK  65536                   /* bytes read between checks */
/* The resolver's path, as tiku_circuit_source_t holds it. */
#define SOURCE_PATH_MAX (sizeof ((tiku_circuit_source_t *)0)->path)

/** @brief Whether the import this resolver serves was cancelled. */
static int cancelled(tiku_circuitview_local_t *l)
{
    return l->cancel && l->cancel(l->ctx);
}

/**
 * @brief Read an open regular file whole, refusing one that changes size
 *        or time stamps while it is read.
 *
 * @return TIKU_CIRCUIT_OK with the bytes, terminated, and the file's
 *         device:inode key in @p out; or the error, with nothing to free
 */
static tiku_circuit_err_t read_fd(int fd, tiku_circuitview_local_t *l,
                                  tiku_circuit_source_t *out)
{
    struct stat before, after;

    if (fstat(fd, &before) || !S_ISREG(before.st_mode) ||
        before.st_size < 0) {
        return TIKU_CIRCUIT_IO;
    }
    if ((uintmax_t)before.st_size > INPUT_LIMIT) {
        return TIKU_CIRCUIT_LIMIT;
    }
    size_t size = (size_t)before.st_size, at = 0;
    unsigned char *data = malloc(size + 1);
    if (!data) {
        return TIKU_CIRCUIT_NOMEM;
    }
    tiku_circuit_err_t e = TIKU_CIRCUIT_OK;
    while (at < size) {
        if (cancelled(l)) {
            e = TIKU_CIRCUIT_CANCELLED;
            break;
        }
        size_t chunk = size - at > READ_CHUNK ? READ_CHUNK : size - at;
        ssize_t n = read(fd, data + at, chunk);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            e = TIKU_CIRCUIT_IO;
            break;
        }
        at += (size_t)n;
    }
    unsigned char extra;
    ssize_t n;
    do {
        n = read(fd, &extra, 1);
    } while (n < 0 && errno == EINTR && !cancelled(l));
    if (!e && (n != 0 || fstat(fd, &after) ||
               before.st_size != after.st_size ||
               before.st_mtime != after.st_mtime ||
               before.st_ctime != after.st_ctime)) {
        e = TIKU_CIRCUIT_IO;
    }
    if (!e && (before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
               before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)) {
        e = TIKU_CIRCUIT_IO;
    }
    if (cancelled(l)) {
        e = TIKU_CIRCUIT_CANCELLED;
    }
    if (e) {
        free(data);
    } else {
        data[size] = 0;
        out->data = data;
        out->bytes = size;
        snprintf(out->key, sizeof out->key, "%ju:%ju",
                 (uintmax_t)before.st_dev, (uintmax_t)before.st_ino);
    }
    return e;
}

/**
 * @brief Resolve link @p ref against the file @p parent that names it,
 *        both relative to the chosen file's directory.
 *
 * A '..' may only remove a component already below that directory;
 * absolute paths, control characters, backslashes, colons (drive letters
 * and addresses) and '$' are refused.
 * @return 1 with the path in @p out, or 0
 */
static int relative(const char *parent, const char *ref,
                    char out[SOURCE_PATH_MAX])
{
    if (!*ref || *ref == '/' || strlen(ref) >= SOURCE_PATH_MAX ||
        strlen(parent) >= SOURCE_PATH_MAX) {
        return 0;
    }
    for (const unsigned char *c = (const unsigned char *)ref; *c; c++) {
        if (*c < ' ' || *c == 0x7f || *c == '\\' || *c == ':' ||
            *c == '$') {
            return 0;
        }
    }
    char joined[2 * SOURCE_PATH_MAX];
    const char *slash = strrchr(parent, '/');
    int prefix = slash ? (int)(slash - parent + 1) : 0;
    snprintf(joined, sizeof joined, "%.*s%s", prefix, parent, ref);
    out[0] = 0;
    size_t used = 0;
    for (char *p = joined; *p;) {
        while (*p == '/') {
            p++;
        }
        if (!*p) {
            break;
        }
        size_t n = strcspn(p, "/");
        if (n == 1 && p[0] == '.') {
            p += n;
            continue;
        }
        if (n == 2 && p[0] == '.' && p[1] == '.') {
            if (!used) {
                return 0;
            }
            char *last = strrchr(out, '/');
            used = last ? (size_t)(last - out) : 0;
            out[used] = 0;
            p += n;
            continue;
        }
        if (used + n + 2 > SOURCE_PATH_MAX) {
            return 0;
        }
        if (used) {
            out[used++] = '/';
        }
        memcpy(out + used, p, n);
        used += n;
        out[used] = 0;
        p += n;
    }
    return used != 0;
}

/**
 * @brief Open @p path below directory @p root one component at a time,
 *        each through its parent's descriptor and without following a
 *        symlink, so none can lead out of the root between check and open.
 *
 * A symlink is refused even when it points inside the root.
 * @return The file's descriptor, or -1 with errno set
 */
static int open_child(int root, const char *path)
{
    char copy[SOURCE_PATH_MAX];

    snprintf(copy, sizeof copy, "%s", path);
    int dir = dup(root);
    if (dir < 0) {
        return -1;
    }
    char *part = copy, *slash;
    while ((slash = strchr(part, '/')) != NULL) {
        *slash = 0;
        int next = openat(dir, part,
                          O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        int saved = errno;
        close(dir);
        errno = saved;
        if (next < 0) {
            return -1;
        }
        dir = next;
        part = slash + 1;
    }
    int fd = openat(dir, part, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    int saved = errno;
    close(dir);
    errno = saved;
    return fd;
}

/**
 * @brief The resolver's read: with no @p parent, the chosen file, whose
 *        directory is held for the rest of the import; otherwise the link
 *        @p ref, confined to that directory.
 */
static tiku_circuit_err_t local_read(void *ctx, const char *parent,
    const char *ref, tiku_circuit_source_t *out,
    tiku_circuit_diagnostic_t *diag)
{
    tiku_circuitview_local_t *l = ctx;

    if (cancelled(l)) {
        return TIKU_CIRCUIT_CANCELLED;
    }
    if (!parent) {
        char *absolute = realpath(ref, NULL);
        if (!absolute) {
            return errno == ENOENT ? TIKU_CIRCUIT_MISSING : TIKU_CIRCUIT_IO;
        }
        if (strlen(absolute) >= sizeof out->path) {
            free(absolute);
            return TIKU_CIRCUIT_LIMIT;
        }
        char *base = strrchr(absolute, '/');
        snprintf(out->path, sizeof out->path, "%s", base + 1);
        if (base == absolute) {
            base[1] = 0;
        } else {
            *base = 0;
        }
        tiku_circuitview_local_close(l);
        l->directory = open(absolute, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        free(absolute);
        if (l->directory < 0) {
            return TIKU_CIRCUIT_IO;
        }
    } else if (l->directory < 0 || !relative(parent, ref, out->path)) {
        if (diag) {
            snprintf(diag->message, sizeof diag->message,
                     "Blocked link: use a relative file within the "
                     "selected root directory");
        }
        if (diag) {
            diag->code = TIKU_CIRCUIT_UNSUPPORTED;
        }
        return TIKU_CIRCUIT_UNSUPPORTED;
    }
    int fd = open_child(l->directory, out->path);
    if (fd < 0) {
        tiku_circuit_err_t e = errno == ENOENT ? TIKU_CIRCUIT_MISSING :
                                                 TIKU_CIRCUIT_IO;
        if (diag) {
            diag->code = e;
            snprintf(diag->message, sizeof diag->message,
                     "Cannot open linked file (missing, unreadable, or a "
                     "symlink)");
        }
        return e;
    }
    tiku_circuit_err_t e = read_fd(fd, l, out);
    close(fd);
    return e;
}

/** @brief The resolver's release: free what local_read() returned. */
static void local_release(void *ctx, tiku_circuit_source_t *source)
{
    (void)ctx;
    free((void *)source->data);
    memset(source, 0, sizeof *source);
}

tiku_circuit_resolver_t tiku_circuitview_local_resolver(
    tiku_circuitview_local_t *l)
{
    return (tiku_circuit_resolver_t){l, local_read, local_release};
}

void tiku_circuitview_local_close(tiku_circuitview_local_t *l)
{
    if (l->directory >= 0) {
        close(l->directory);
    }
    l->directory = -1;
}

tiku_circuit_err_t tiku_circuitview_local_font(const char *path,
    tiku_circuitview_local_t *l, unsigned char **data, size_t *bytes)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);

    if (fd < 0) {
        return TIKU_CIRCUIT_IO;
    }
    tiku_circuit_source_t source = {0};
    tiku_circuit_err_t e = read_fd(fd, l, &source);
    close(fd);
    if (!e) {
        *data = (unsigned char *)source.data;
        *bytes = source.bytes;
    }
    return e;
}
