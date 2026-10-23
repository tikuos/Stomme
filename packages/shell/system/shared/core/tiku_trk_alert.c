/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_alert.c - alert state, button layout, and the progress rows.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_strings.h"
#include "tiku_trk_alert.h"
#include <stdio.h>
#include <string.h>

void
tiku_trk_alert_for_conflict(tiku_alert_t *a, const tiku_progress_t *pr)
{
    char text[512];
    const char *leaf;

    if (a == NULL || pr == NULL) {
        return;
    }
    leaf = strrchr(pr->conflict_path, '/');
    leaf = (leaf != NULL) ? leaf + 1 : pr->conflict_path;
    snprintf(text, sizeof text,
             tiku_str("alert.msg.exists_here"), leaf);
    /* The engine chose the form: conflict_many asks for the four-way one
     * (FS-018, FS-019). */
    if (pr->conflict_many) {
        tiku_alert_open(a, TIKU_ALERT_WARN,
                            TIKU_TRK_ALERT_TAG_CONFLICT, text,
                            tiku_str("alert.btn.conflict"));
        a->dflt = 2;  /* Skip all */
    } else {
        tiku_alert_open(a, TIKU_ALERT_WARN,
                            TIKU_TRK_ALERT_TAG_CONFLICT, text,
                            tiku_str("alert.btn.replace"));
        a->dflt = 0;  /* Cancel. */
    }
}

void
tiku_trk_alert_for_error(tiku_alert_t *a, const tiku_progress_t *pr)
{
    if (a == NULL || pr == NULL) {
        return;
    }
    tiku_alert_open(a, TIKU_ALERT_STOP, TIKU_TRK_ALERT_TAG_ERROR,
                        pr->error,
                        pr->error_can_continue ? tiku_str("alert.btn.ok")
                                               : tiku_str("button.cancel"));
}

tiku_conflict_t
tiku_trk_alert_conflict_answer(const tiku_alert_t *a)
{
    if (a == NULL || a->chosen < 0) {
        return TIKU_CONFLICT_ASK;
    }
    if (a->nbutton == 2) {
        /* Cancel | Replace */
        return (a->chosen == 1) ? TIKU_CONFLICT_REPLACE
                                : TIKU_CONFLICT_SKIP;
    }
    /* Cancel | Skip | Skip all | Replace all */
    switch (a->chosen) {
    case 1:  return TIKU_CONFLICT_SKIP;
    case 2:  return TIKU_CONFLICT_SKIP_ALL;
    case 3:  return TIKU_CONFLICT_REPLACE_ALL;
    default: return TIKU_CONFLICT_SKIP;
    }
}

/*---------------------------------------------------------------------------*/
/* The progress window                                                        */
/*---------------------------------------------------------------------------*/

void
tiku_trk_progress_reset(tiku_trk_progress_win_t *p)
{
    if (p != NULL) {
        memset(p, 0, sizeof *p);
    }
}

const char *
tiku_trk_progress_caption(tiku_op_t op)
{
    switch (op) {
    case TIKU_OP_COPY:      return "Copying: ";
    case TIKU_OP_MOVE:      return "Moving: ";
    case TIKU_OP_DUPLICATE: return "Duplicating: ";
    case TIKU_OP_TRASH:     return "Emptying Trash... ";
    case TIKU_OP_DELETE:    return "Deleting: ";
    case TIKU_OP_LINK:      return "Creating links: ";
    default:                    return "";
    }
}

void
tiku_trk_progress_update(tiku_trk_progress_win_t *p, int i,
                         const tiku_progress_t *pr, int busy,
                         const char *dest)
{
    int k, any = 0;

    if (p == NULL || i < 0 || i >= TIKU_TRK_PROGRESS_ROWS) {
        return;
    }
    if (!busy) {
        memset(&p->row[i], 0, sizeof p->row[i]);
    } else if (pr != NULL) {
        p->row[i].used = 1;
        snprintf(p->row[i].caption, sizeof p->row[i].caption, "%s",
                 tiku_trk_progress_caption(pr->op));
        /* The current item is taken whenever the engine names one, whether
         * or not the count moved (FS-075). */
        if (pr->current[0] != '\0') {
            snprintf(p->row[i].current, sizeof p->row[i].current, "%s",
                     pr->current);
        }
        if (dest != NULL) {
            snprintf(p->row[i].dest, sizeof p->row[i].dest, "%s", dest);
        }
        p->row[i].done = pr->done + pr->skipped + pr->failed;
        p->row[i].total = pr->total;
    }
    /* The window is open while any row is in use (FS-072). */
    for (k = 0; k < TIKU_TRK_PROGRESS_ROWS; k++) {
        any |= p->row[k].used;
    }
    p->open = any;
}

/* Where a row's pause and stop buttons sit (FS-074). */
#define PROG_ROW_H  48
#define PROG_TOP    14
#define PROG_BTN    18
#define PROG_BTN_GAP 4

void
tiku_trk_progress_button_rect(int i, int which, int w, int *bx, int *by,
                              int *bw, int *bh)
{
    /* Right-hand end of the row, stop outermost, so the destructive button
     * is the one furthest from pause. */
    int right = w - 10;
    int x = right - PROG_BTN - (which == TIKU_TRK_PROGRESS_HIT_STOP
                                    ? 0 : PROG_BTN + PROG_BTN_GAP);

    if (bx != NULL) { *bx = x; }
    if (by != NULL) { *by = PROG_TOP + i * PROG_ROW_H + 4; }
    if (bw != NULL) { *bw = PROG_BTN; }
    if (bh != NULL) { *bh = PROG_BTN; }
}

tiku_trk_progress_hit_t
tiku_trk_progress_hit(const tiku_trk_progress_win_t *p, int w, int x, int y,
                      int *row)
{
    int i, k, visible = 0;

    if (p == NULL || !p->open) {
        return TIKU_TRK_PROGRESS_HIT_NONE;
    }
    for (i = 0; i < TIKU_TRK_PROGRESS_ROWS; i++) {
        if (!p->row[i].used) {
            continue;
        }
        for (k = TIKU_TRK_PROGRESS_HIT_PAUSE; k <= TIKU_TRK_PROGRESS_HIT_STOP;
             k++) {
            int bx, by, bw, bh;

            tiku_trk_progress_button_rect(visible, k, w, &bx, &by, &bw, &bh);
            if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
                if (row != NULL) { *row = i; }
                return (tiku_trk_progress_hit_t)k;
            }
        }
        visible++;
    }
    return TIKU_TRK_PROGRESS_HIT_NONE;
}

const char *
tiku_trk_progress_row_text(const tiku_trk_progress_win_t *p, int i)
{
    if (p == NULL || i < 0 || i >= TIKU_TRK_PROGRESS_ROWS ||
        !p->row[i].used) {
        return "";
    }
    return p->row[i].paused ? "Paused: click to resume or stop"
                            : p->row[i].current;
}

int
tiku_trk_progress_wanted(tiku_op_t op, int items)
{
    if (op == TIKU_OP_LINK) {
        return (items > TIKU_TRK_PROGRESS_LINK_MIN);
    }
    return 1;
}

void
tiku_trk_progress_sample(tiku_trk_progress_win_t *p, int i, int64_t bytes,
                         int64_t now)
{
    tiku_trk_progress_row_t *r;
    int k;

    if (p == NULL || i < 0 || i >= TIKU_TRK_PROGRESS_ROWS) {
        return;
    }
    r = &p->row[i];
    r->total_bytes = bytes;
    /* Shift the samples along, newest first; the oldest falls off. */
    for (k = TIKU_TRK_PROGRESS_SLOTS - 1; k > 0; k--) {
        r->bytes[k] = r->bytes[k - 1];
        r->at[k] = r->at[k - 1];
    }
    r->bytes[0] = bytes;
    r->at[0] = now;
    if (r->nsample < TIKU_TRK_PROGRESS_SLOTS) {
        r->nsample++;
    }
}

int64_t
tiku_trk_progress_speed(const tiku_trk_progress_win_t *p, int i)
{
    const tiku_trk_progress_row_t *r;
    int last;
    int64_t db, dt;

    if (p == NULL || i < 0 || i >= TIKU_TRK_PROGRESS_ROWS) {
        return 0;
    }
    r = &p->row[i];
    if (r->nsample < 2) {
        return 0;
    }
    last = r->nsample - 1;
    db = r->bytes[0] - r->bytes[last];
    dt = r->at[0] - r->at[last];
    if (dt <= 0 || db <= 0) {
        return 0;
    }
    return db * 1000000 / dt;
}

int64_t
tiku_trk_progress_eta(const tiku_trk_progress_win_t *p, int i)
{
    int64_t speed = tiku_trk_progress_speed(p, i);
    const tiku_trk_progress_row_t *r;
    int64_t left;

    if (speed <= 0 || p == NULL) {
        return -1;
    }
    r = &p->row[i];
    if (r->total <= 0 || r->done <= 0 || r->done >= r->total) {
        return -1;
    }
    /* The bytes still to write are estimated from the bytes per item so
     * far: only the item count says how much is left. */
    left = r->total_bytes * (r->total - r->done) / r->done;
    return left / speed;
}

int
tiku_trk_progress_show_speed(int64_t since_open)
{
    int64_t cycle = TIKU_TRK_PROGRESS_SPEED_US +
                    TIKU_TRK_PROGRESS_ESTIMATE_US;
    int64_t at;

    if (since_open < 0) {
        return 1;
    }
    at = since_open % cycle;
    return (at < TIKU_TRK_PROGRESS_SPEED_US);
}

size_t
tiku_trk_progress_say(const tiku_trk_progress_win_t *p, char *out,
                      size_t max)
{
    size_t used = 0u;
    int i;

    if (out == NULL || max == 0u) {
        return 0u;
    }
    out[0] = '\0';
    if (p == NULL || !p->open) {
        return (size_t)snprintf(out, max, "idle");
    }
    for (i = 0; i < TIKU_TRK_PROGRESS_ROWS; i++) {
        const tiku_trk_progress_row_t *row = &p->row[i];
        const char *slash;

        if (!row->used) {
            continue;
        }
        /* The leaf of the current item's path keeps the line short. */
        slash = strrchr(row->current, '/');
        used += (size_t)snprintf(out + used,
                                 (used < max) ? max - used : 0u,
                                 "%s%s, %d of %d|", row->caption,
                                 (slash != NULL) ? slash + 1 : row->current,
                                 row->done, row->total);
        if (used >= max) {
            break;
        }
    }
    if (used == 0u) {
        used = (size_t)snprintf(out, max, "idle");
    }
    return used;
}
