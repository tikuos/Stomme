/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_editdoc_undo.c - the history of transactions: undo and redo,
 * joined typing, the saved state and a byte budget kept by whole steps.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_editdoc_private.h"

#include <string.h>

/** @brief What one record charges the budget: its bytes, pieces and
 *         bookkeeping. */
static size_t
record_cost(const tiku_ed_record_t *r)
{
    return sizeof *r + r->npieces * sizeof *r->pieces + r->payload_len +
           (r->nbefore + r->nafter) * sizeof *r->more;
}

void
tiku_ed_history_init(tiku_ed_history_t *h, size_t target)
{
    memset(h, 0, sizeof *h);
    h->target = target;
    h->base_state = 1;
    h->saved_state = 1;
    h->next_state = 2;
}

void
tiku_ed_record_free(tiku_ed_record_t *r)
{
    tiku_ed_free(r->pieces);
    tiku_ed_free(r->payload);
    tiku_ed_free(r->more);
    memset(r, 0, sizeof *r);
}

void
tiku_ed_history_free(tiku_ed_history_t *h)
{
    size_t i;

    for (i = 0; i < h->count; i++)
        tiku_ed_record_free(&h->records[i]);
    tiku_ed_free(h->records);
    h->records = NULL;
    h->count = h->cap = h->current = 0;
    h->cost = 0;
}

uint64_t
tiku_ed_history_state(const tiku_ed_history_t *h)
{
    return h->current ? h->records[h->current - 1].state_after
                      : h->base_state;
}

int
tiku_ed_history_reserve(tiku_ed_history_t *h)
{
    size_t n;
    tiku_ed_record_t *grown;

    /* The new record goes at current; the redo branch above it is
     * dropped, so only current + 1 slots are needed. */
    if (h->current < h->cap)
        return 1;
    n = h->cap ? h->cap * 2 : 16;
    if (n > SIZE_MAX / sizeof *grown)
        return 0;
    grown = tiku_ed_realloc(h->records, n * sizeof *grown);
    if (!grown)
        return 0;
    h->records = grown;
    h->cap = n;
    return 1;
}

void
tiku_ed_history_push(tiku_ed_history_t *h, tiku_ed_record_t *r)
{
    size_t i;

    for (i = h->current; i < h->count; i++) {
        h->cost -= h->records[i].cost;
        tiku_ed_record_free(&h->records[i]);
    }
    r->cost = record_cost(r);
    h->records[h->current] = *r;
    h->count = ++h->current;
    h->cost += r->cost;
}

void
tiku_ed_history_prune(tiku_ed_history_t *h)
{
    while (h->cost > h->target && h->count > 1 && h->current > 0) {
        h->base_state = h->records[0].state_after;
        h->cost -= h->records[0].cost;
        tiku_ed_record_free(&h->records[0]);
        memmove(h->records, h->records + 1,
                (h->count - 1) * sizeof *h->records);
        h->count--;
        h->current--;
    }
}

void
tiku_ed_history_clear(tiku_ed_history_t *h)
{
    size_t i;

    for (i = 0; i < h->count; i++)
        tiku_ed_record_free(&h->records[i]);
    h->count = h->current = 0;
    h->cost = 0;
    h->base_state = h->next_state++;
    h->saved_state = h->base_state;
    h->open = 0;
}

int
tiku_ed_history_joins(const tiku_ed_history_t *h, const tiku_ed_record_t *r)
{
    const tiku_ed_record_t *last;
    size_t j;
    int back = 1, on = 1;

    if (!h->open || h->current == 0 || h->current != h->count)
        return 0;
    last = &h->records[h->current - 1];
    if (r->typed == TIKU_ED_TYPED_NONE || r->typed != last->typed ||
        r->npieces != last->npieces || r->npieces == 0 ||
        r->view.slot != last->view.slot ||
        r->view.generation != last->view.generation ||
        last->state_after == h->saved_state)
        return 0;
    for (j = 0; j < r->npieces; j++) {
        const tiku_ed_piece_t *p = &last->pieces[j], *q = &r->pieces[j];

        /* Insertions join only right after the last ones; Backspace ends
         * where the last deletion was, Delete starts there, at every
         * caret alike. */
        if (r->typed == TIKU_ED_TYPED_INSERT &&
            (q->removed != 0 || q->pre != p->post + p->added))
            return 0;
        back &= q->pre + q->removed == p->post;
        on &= q->pre == p->post;
    }
    return r->typed == TIKU_ED_TYPED_INSERT || back || on;
}

/** @brief Piece @p j of the last record and of @p r merged into @p o, a
 *         Backspace when @p back, their bytes copied to @p out from @p off;
 *         @p shift holds what @p r's earlier pieces moved later text.
 *         @return where the bytes end. */
static size_t
merge_piece(const tiku_ed_record_t *last, const tiku_ed_record_t *r,
            size_t j, int back, char *out, size_t off, int64_t *shift,
            tiku_ed_piece_t *o)
{
    const tiku_ed_piece_t *p = &last->pieces[j], *q = &r->pieces[j];
    const char *pr = last->payload + p->removed_at;
    const char *pa = last->payload + p->added_at;
    const char *qr = r->payload + q->removed_at;
    const char *qa = r->payload + q->added_at;

    *o = *p;
    o->removed_at = off;
    if (r->typed == TIKU_ED_TYPED_INSERT) {
        /* removed, added, then the new added bytes. */
        memcpy(out + off, pr, (size_t)p->removed);
        off += (size_t)p->removed;
        o->added_at = off;
        memcpy(out + off, pa, (size_t)p->added);
        memcpy(out + off + p->added, qa, (size_t)q->added);
        off += (size_t)(p->added + q->added);
        o->added = p->added + q->added;
        o->post = p->post + *shift;
        *shift += q->added;
        return off;
    }
    if (back) {
        /* Backspace: the new bytes come first. */
        memcpy(out + off, qr, (size_t)q->removed);
        memcpy(out + off + q->removed, pr, (size_t)p->removed);
        o->pre = p->pre - q->removed;
        o->post = p->post - q->removed - *shift;
    } else {
        /* Delete: the new bytes follow. */
        memcpy(out + off, pr, (size_t)p->removed);
        memcpy(out + off + p->removed, qr, (size_t)q->removed);
        o->post = p->post - *shift;
    }
    off += (size_t)(p->removed + q->removed);
    o->removed = p->removed + q->removed;
    o->added_at = off;
    *shift += q->removed;
    return off;
}

int
tiku_ed_history_merge(const tiku_ed_history_t *h, const tiku_ed_record_t *r,
                      size_t after, tiku_ed_record_t *m)
{
    const tiku_ed_record_t *last = &h->records[h->current - 1];
    size_t len = last->payload_len, extra = r->payload_len, j, off = 0;
    int back = r->typed == TIKU_ED_TYPED_DELETE &&
               r->pieces[0].pre + r->pieces[0].removed == last->pieces[0].post;
    int64_t shift = 0;

    memset(m, 0, sizeof *m);
    if (extra > SIZE_MAX - len || last->nbefore > SIZE_MAX / sizeof *m->more -
                                                    after)
        return 0;
    m->pieces = tiku_ed_alloc(r->npieces * sizeof *m->pieces);
    m->payload = tiku_ed_alloc(len + extra ? len + extra : 1);
    if (last->nbefore + after)
        m->more = tiku_ed_alloc((last->nbefore + after) * sizeof *m->more);
    if (!m->pieces || !m->payload || (last->nbefore + after && !m->more)) {
        tiku_ed_record_free(m);
        return 0;
    }
    for (j = 0; j < r->npieces; j++)
        off = merge_piece(last, r, j, back, m->payload, off, &shift,
                          &m->pieces[j]);
    m->npieces = r->npieces;
    m->payload_len = len + extra;
    if (last->nbefore)
        memcpy(m->more, last->more, last->nbefore * sizeof *m->more);
    m->nbefore = last->nbefore;
    return 1;
}

void
tiku_ed_history_replace_last(tiku_ed_history_t *h, tiku_ed_record_t *m)
{
    tiku_ed_record_t *last = &h->records[h->current - 1];
    tiku_ed_piece_t *pieces = last->pieces;
    char *payload = last->payload;
    tiku_edit_selection_t *more = last->more;

    h->cost -= last->cost;
    last->pieces = m->pieces;
    last->npieces = m->npieces;
    last->payload = m->payload;
    last->payload_len = m->payload_len;
    last->more = m->more;
    last->nbefore = m->nbefore;
    last->nafter = m->nafter;
    last->state_after = h->next_state++;
    last->cost = record_cost(last);
    h->cost += last->cost;
    memset(m, 0, sizeof *m);
    m->pieces = pieces;
    m->payload = payload;
    m->more = more;
}
