/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_state_attribute.c - descriptor-bound native attribute persistence.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_state_attribute.h"
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__) || defined(__APPLE__)
#include <sys/xattr.h>
#endif

struct tiku_state_attribute {
    char *path;
    char name[256];
    dev_t device;
    ino_t inode;
    int present;
    size_t length;
    unsigned char *bytes;
};
struct tiku_state_attribute_update {
    tiku_state_attribute_t baseline;
    unsigned char *bytes;
    size_t length;
    int present;
    atomic_int cancelled;
};

/** @brief Read through a pinned descriptor using the host attribute API. */
static ssize_t attribute_get(int fd, const char *name, void *bytes, size_t length)
{
#if defined(__APPLE__)
    return fgetxattr(fd, name, bytes, length, 0, 0);
#elif defined(__linux__)
    return fgetxattr(fd, name, bytes, length);
#else
    (void)fd; (void)name; (void)bytes; (void)length;
    errno = ENOTSUP;
    return -1;
#endif
}

/** @brief Whether the host reported an absent attribute. */
static int attribute_absent(void)
{
#if defined(__APPLE__)
    return errno == ENOATTR;
#else
    return errno == ENODATA;
#endif
}

/** @brief Open the resolved node without following a replacement symlink. */
static int attribute_open(const char *path, struct stat *st)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return -1;
    if (fstat(fd, st) != 0 || (!S_ISREG(st->st_mode) && !S_ISDIR(st->st_mode))) {
        int error = errno;
        if (!S_ISREG(st->st_mode) && !S_ISDIR(st->st_mode)) error = EINVAL;
        close(fd); errno = error; return -1;
    }
    return fd;
}

/** @brief Copy a bounded attribute; absence remains distinct from empty bytes. */
static int attribute_copy(int fd, tiku_state_attribute_t *capture)
{
    ssize_t length = attribute_get(fd, capture->name, NULL, 0);
    ssize_t actual;
    if (length < 0) {
        if (attribute_absent()) { capture->present = 0; return 0; }
        return -1;
    }
    if ((size_t)length > TIKU_STATE_ATTRIBUTE_MAX) { errno = EFBIG; return -1; }
    capture->bytes = malloc(length ? (size_t)length : 1u);
    if (!capture->bytes) return -1;
    actual = attribute_get(fd, capture->name, capture->bytes, length ? (size_t)length : 1u);
    if (actual < 0 || actual != length) { errno = EAGAIN; return -1; }
    capture->length = (size_t)length;
    capture->present = 1;
    return 0;
}

tiku_state_attribute_t *tiku_state_attribute_read(const char *node, const char *name)
{
    tiku_state_attribute_t *capture;
    struct stat st = {0};
    int fd, error;
    if (!node || !name || !*name || strlen(name) > 250u) { errno = EINVAL; return NULL; }
    capture = calloc(1, sizeof *capture);
    if (!capture) return NULL;
    capture->path = realpath(node, NULL);
    if (!capture->path) { free(capture); return NULL; }
    (void)snprintf(capture->name, sizeof capture->name, "user.%s", name);
    fd = attribute_open(capture->path, &st);
    if (fd < 0) { tiku_state_attribute_free(capture); return NULL; }
    capture->device = st.st_dev;
    capture->inode = st.st_ino;
    error = attribute_copy(fd, capture) == 0 ? 0 : errno;
    close(fd);
    if (error) { tiku_state_attribute_free(capture); errno = error; return NULL; }
    return capture;
}

void tiku_state_attribute_free(tiku_state_attribute_t *capture)
{
    if (!capture) return;
    free(capture->path); free(capture->bytes); free(capture);
}

int tiku_state_attribute_present(const tiku_state_attribute_t *capture)
{
    return capture && capture->present;
}

const void *tiku_state_attribute_bytes(const tiku_state_attribute_t *capture, size_t *length)
{
    if (length) *length = capture ? capture->length : 0;
    return capture ? capture->bytes : NULL;
}

tiku_state_attribute_update_t *tiku_state_attribute_prepare(const tiku_state_attribute_t *capture,
    const void *bytes, size_t length, int present)
{
    tiku_state_attribute_update_t *update;
    if (!capture || length > TIKU_STATE_ATTRIBUTE_MAX || (length && !bytes) ||
        (present != 0 && present != 1) || (!present && length)) { errno = EINVAL; return NULL; }
    update = calloc(1, sizeof *update);
    if (!update) return NULL;
    update->baseline = *capture;
    update->baseline.path = strdup(capture->path);
    update->baseline.bytes = malloc(capture->length ? capture->length : 1u);
    update->bytes = malloc(length ? length : 1u);
    atomic_init(&update->cancelled, 0);
    if (!update->baseline.path || !update->baseline.bytes || !update->bytes) {
        tiku_state_attribute_update_free(update); return NULL;
    }
    if (capture->length) memcpy(update->baseline.bytes, capture->bytes, capture->length);
    if (length) memcpy(update->bytes, bytes, length);
    update->length = length;
    update->present = present;
    return update;
}

/** @brief Set or remove one attribute through the verified node descriptor. */
static int attribute_write(int fd, const tiku_state_attribute_update_t *update)
{
#if defined(__APPLE__)
    return update->present ? fsetxattr(fd, update->baseline.name, update->bytes, update->length, 0, 0)
                           : fremovexattr(fd, update->baseline.name, 0);
#elif defined(__linux__)
    return update->present ? fsetxattr(fd, update->baseline.name, update->bytes, update->length, 0)
                           : fremovexattr(fd, update->baseline.name);
#else
    (void)fd; (void)update; errno = ENOTSUP; return -1;
#endif
}

tiku_store_update_result_t tiku_state_attribute_run(tiku_state_attribute_update_t *update)
{
    tiku_state_attribute_t actual = {0};
    const tiku_state_attribute_t *base;
    tiku_store_update_result_t result = TIKU_STORE_UPDATE_FAILED;
    struct stat st = {0};
    int fd;
    if (!update) return result;
    if (atomic_load(&update->cancelled)) return TIKU_STORE_UPDATE_CANCELLED;
    base = &update->baseline;
    fd = attribute_open(base->path, &st);
    if (fd < 0) return errno == ENOENT || errno == ELOOP ? TIKU_STORE_UPDATE_CONFLICT : result;
    if (st.st_dev != base->device || st.st_ino != base->inode) {
        result = TIKU_STORE_UPDATE_CONFLICT; goto done;
    }
    memcpy(actual.name, base->name, sizeof actual.name);
    if (attribute_copy(fd, &actual) != 0) goto done;
    if (actual.present != base->present || actual.length != base->length ||
        (base->length && memcmp(actual.bytes, base->bytes, base->length))) {
        result = TIKU_STORE_UPDATE_CONFLICT; goto done;
    }
    if (atomic_load(&update->cancelled)) { result = TIKU_STORE_UPDATE_CANCELLED; goto done; }
    if (!update->present && !actual.present) { result = TIKU_STORE_UPDATE_OK; goto done; }
    if (attribute_write(fd, update) != 0) goto done;
    result = fsync(fd) == 0 ? TIKU_STORE_UPDATE_OK : TIKU_STORE_UPDATE_UNCERTAIN;
done:
    free(actual.bytes); close(fd); return result;
}

void tiku_state_attribute_cancel(tiku_state_attribute_update_t *update)
{
    if (update) atomic_store(&update->cancelled, 1);
}

void tiku_state_attribute_update_free(tiku_state_attribute_update_t *update)
{
    if (!update) return;
    free(update->baseline.path); free(update->baseline.bytes); free(update->bytes); free(update);
}
