/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_editdoc_alloc.c - the engine's allocations, through one allocator a
 * test can replace, with a count of those made inside a commit.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_editdoc_private.h"

#include <stdlib.h>

/** @brief The allocator in force: the C library's unless a test set one. */
static tiku_ed_allocator_t hooks = { malloc, realloc, free };

/** @brief Nonzero while a commit runs on this thread. */
static _Thread_local int committing;

/** @brief Allocations attempted inside a commit, over the process. */
static unsigned long commit_allocations;

void *
tiku_ed_alloc(size_t n)
{
    if (committing)
        commit_allocations++;
    return n ? hooks.alloc_fn(n) : NULL;
}

void *
tiku_ed_realloc(void *p, size_t n)
{
    if (committing)
        commit_allocations++;
    return n ? hooks.realloc_fn(p, n) : NULL;
}

void
tiku_ed_free(void *p)
{
    if (p)
        hooks.free_fn(p);
}

void
tiku_ed_commit_begin(void)
{
    committing++;
}

void
tiku_ed_commit_end(void)
{
    committing--;
}

void
tiku_ed_test_allocator(const tiku_ed_allocator_t *allocator)
{
    if (allocator) {
        hooks = *allocator;
    } else {
        hooks.alloc_fn = malloc;
        hooks.realloc_fn = realloc;
        hooks.free_fn = free;
    }
}

unsigned long
tiku_ed_test_commit_allocations(void)
{
    return commit_allocations;
}
