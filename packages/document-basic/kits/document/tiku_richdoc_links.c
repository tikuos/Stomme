/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_links.c - validated, reference-counted hyperlink targets.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <string.h>

/** @brief An immutable target shared by run arrays across retained roots. */
typedef struct {
    atomic_size_t refs;        /**< owning runs and temporary builders */
    char text[];              /**< NUL-terminated UTF-8 target */
} rich_link_t;

/** @brief Recover allocation metadata from an internally owned target. */
static rich_link_t *
owner(const char *target)
{
    return (rich_link_t *)((char *)target - offsetof(rich_link_t, text));
}

/** @brief Decode one ASCII hexadecimal digit; negative means invalid. */
static int
hex(unsigned char byte)
{
    if (byte >= '0' && byte <= '9') return byte - '0';
    if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10;
    if (byte >= 'A' && byte <= 'F') return byte - 'A' + 10;
    return -1;
}

/** @brief Match a lowercase ASCII scheme prefix without changing the input. */
static int
prefix(const char *text, const char *scheme)
{
    unsigned char byte;

    while (*scheme) {
        byte = (unsigned char)*text++;
        if (byte >= 'A' && byte <= 'Z')
            byte += 'a' - 'A';
        if (byte != (unsigned char)*scheme++)
            return 0;
    }
    return 1;
}

tiku_rich_status_t
tiku_rich_link_validate(const char *target)
{
    size_t i, length = 0, offset;
    int a, b;
    unsigned char byte;

    if (!target || !*target)
        return TIKU_RICH_OK;
    while (length <= 2048 && target[length])
        length++;
    if (length > 2048)
        return TIKU_RICH_LIMIT;
    if (tiku_rd_utf8(target, length, 1) != TIKU_RICH_OK)
        return TIKU_RICH_INVALID_LINK;
    for (i = 0; i < length; i++) {
        byte = (unsigned char)target[i];
        if (byte <= ' ' || byte == 127 || byte == '\\')
            return TIKU_RICH_INVALID_LINK;
        if (byte != '%')
            continue;
        if (length - i < 3 || (a = hex(target[i + 1])) < 0 ||
            (b = hex(target[i + 2])) < 0)
            return TIKU_RICH_INVALID_LINK;
        byte = (unsigned char)(a * 16 + b);
        if (byte < ' ' || byte == 127 || byte == '\\')
            return TIKU_RICH_INVALID_LINK;
        i += 2;
    }
    if (target[0] == '#')
        return length > 1 ? TIKU_RICH_OK : TIKU_RICH_INVALID_LINK;
    if (prefix(target, "mailto:"))
        return length > 7 ? TIKU_RICH_OK : TIKU_RICH_INVALID_LINK;
    if (prefix(target, "https://"))
        offset = 8;
    else if (prefix(target, "http://"))
        offset = 7;
    else
        return TIKU_RICH_UNSUPPORTED;
    if (length <= offset || target[offset] == '/' ||
        target[offset] == '?' || target[offset] == '#')
        return TIKU_RICH_INVALID_LINK;
    return TIKU_RICH_OK;
}

const char *
tiku_rd_link_new(tiku_rd_memory_t *memory, const char *target,
                  tiku_rich_status_t *status)
{
    rich_link_t *link;
    size_t length;

    *status = tiku_rich_link_validate(target);
    if (*status != TIKU_RICH_OK || !target || !*target)
        return NULL;
    length = strlen(target);
    link = tiku_rd_alloc(memory, sizeof *link + length + 1, status);
    if (!link)
        return NULL;
    atomic_init(&link->refs, 1);
    memcpy(link->text, target, length + 1);
    return link->text;
}

void
tiku_rd_link_retain(const char *target)
{
    if (target)
        atomic_fetch_add_explicit(&owner(target)->refs, 1,
                                   memory_order_relaxed);
}

void
tiku_rd_link_drop(const char *target)
{
    if (target && atomic_fetch_sub_explicit(&owner(target)->refs, 1,
                                             memory_order_acq_rel) == 1)
        tiku_rd_free(owner(target));
}

void
tiku_rd_link_account(const char *target, int add, size_t *bytes)
{
    if (target) tiku_rd_account(owner(target), add, bytes);
}

int
tiku_rd_link_equal(const char *a, const char *b)
{
    return a == b || (a && b && !strcmp(a, b));
}
