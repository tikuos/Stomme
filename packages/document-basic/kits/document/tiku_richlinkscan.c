/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richlinkscan.c - bounded hyperlink traversal over retained source runs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richlinkscan.h"
#include "tiku_richdoc_private.h"
#include <string.h>

/** @brief Source, bounded cursor and nearest complete link occurrence. */
struct tiku_richlinkscan {
    tiku_rd_root_t *root;
    uint64_t generation;
    size_t paragraph, run, byte; /**< scan cursors in document traversal order */
    size_t origin_paragraph, origin_byte;
    int forward;
    tiku_rich_link_match_t pending, found; /**< open run group and candidate */
    tiku_rich_status_t status;
};

tiku_rich_status_t
tiku_richlinkscan_create(const tiku_richsnapshot_t *source,
    tiku_rich_pos_t position, int forward, tiku_richlinkscan_t **out)
{
    tiku_richlinkscan_t *scan;
    tiku_rich_status_t status;

    if (!out) return TIKU_RICH_INVALID;
    *out = NULL;
    if (!source || (forward != 0 && forward != 1)) return TIKU_RICH_INVALID;
    status = tiku_richsnapshot_position(source, position);
    if (status != TIKU_RICH_OK) return status;
    scan = tiku_rd_alloc(source->memory, sizeof *scan, &status);
    if (!scan) return status;
    scan->root = source->root;
    atomic_fetch_add_explicit(&scan->root->refs, 1, memory_order_relaxed);
    scan->generation = source->generation;
    scan->origin_paragraph = tiku_rd_find(source->root, position.paragraph);
    scan->origin_byte = position.byte;
    scan->paragraph = forward ? scan->origin_paragraph : 0;
    scan->forward = forward;
    scan->status = TIKU_RICH_PENDING;
    *out = scan;
    return TIKU_RICH_OK;
}

/** @brief Close a run group and retain the nearest eligible occurrence. */
static void
complete(tiku_richlinkscan_t *scan)
{
    int before;

    if (!scan->pending.target) return;
    before = scan->paragraph < scan->origin_paragraph ||
        (scan->paragraph == scan->origin_paragraph &&
         scan->pending.first.byte < scan->origin_byte);
    if (scan->forward ? !before : before) scan->found = scan->pending;
    if (!before)
        scan->status = scan->found.target ? TIKU_RICH_OK : TIKU_RICH_NOT_FOUND;
    memset(&scan->pending, 0, sizeof scan->pending);
}

/** @brief Inspect one run, coalescing adjacent formatting with equal targets. */
static void
next_run(tiku_richlinkscan_t *scan, const tiku_rich_paragraph_t *p)
{
    const tiku_rich_run_t *run = &p->runs[scan->run];

    if (scan->pending.target &&
        (!run->link || strcmp(run->link, scan->pending.target))) complete(scan);
    if (scan->status != TIKU_RICH_PENDING) return;
    if (run->link) {
        if (!scan->pending.target)
            scan->pending = (tiku_rich_link_match_t){
                .first = {scan->root->body, p->id, scan->byte, TIKU_RICH_AFTER},
                .last = {scan->root->body, p->id, 0, TIKU_RICH_BEFORE},
                .target = run->link, .generation = scan->generation};
        scan->pending.last.byte = scan->byte + run->length;
    }
    scan->byte += run->length;
    scan->run++;
}

tiku_rich_status_t
tiku_richlinkscan_step(tiku_richlinkscan_t *scan, size_t work)
{
    if (!scan) return TIKU_RICH_INVALID;
    while (work-- && scan->status == TIKU_RICH_PENDING) {
        const tiku_rich_paragraph_t *p;

        if (scan->paragraph == scan->root->count) {
            scan->status = scan->found.target ? TIKU_RICH_OK : TIKU_RICH_NOT_FOUND;
            break;
        }
        p = &scan->root->paragraphs[scan->paragraph]->value;
        if (scan->run < p->run_count) next_run(scan, p);
        else {
            complete(scan);
            scan->paragraph++;
            scan->run = scan->byte = 0;
            if (scan->status == TIKU_RICH_PENDING && !scan->forward &&
                scan->paragraph > scan->origin_paragraph)
                scan->status = scan->found.target ? TIKU_RICH_OK :
                                                   TIKU_RICH_NOT_FOUND;
        }
    }
    return scan->status;
}

tiku_rich_status_t
tiku_richlinkscan_result(const tiku_richlinkscan_t *scan,
                          tiku_rich_link_match_t *out)
{
    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!scan) return TIKU_RICH_INVALID;
    if (scan->status == TIKU_RICH_OK) *out = scan->found;
    return scan->status;
}

void
tiku_richlinkscan_release(tiku_richlinkscan_t *scan)
{
    if (!scan) return;
    tiku_rd_root_drop(scan->root);
    tiku_rd_free(scan);
}
