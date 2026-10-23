/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_editdoc_transaction.c - apply, undo and redo in two phases: every
 * check and allocation first, then a commit that cannot fail, allocates
 * nothing and calls nobody.  Listeners hear of it after.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_editdoc_private.h"

#include <string.h>

/** @brief A transaction checked and copied, with everything its commit
 *         needs allocated. */
typedef struct {
    tiku_ed_record_t record;    /* its pieces and payload, owned        */
    tiku_ed_view_t *view;       /* the view it came from, or NULL       */
    int join;                   /* merges into the last record          */
    tiku_ed_record_t merged;    /* the last record merged with this one,
                                   when joining                         */
    tiku_edit_selection_t main; /* the main selection after, when the
                                   transaction sets several             */
} prepared_t;

/** @brief The byte at @p pos, 0 outside. */
static int
at(const tiku_editdoc_t *doc, int64_t pos)
{
    return tiku_ed_bytes_at(&doc->text.bytes, (ptrdiff_t)pos);
}

/** @brief Whether @p bytes are exactly one caret step: one character, or
 *         a CRLF. */
static int
one_step(const char *bytes, int64_t n)
{
    if (n == 2 && bytes[0] == '\r' && bytes[1] == '\n')
        return 1;
    return n >= 1 && tiku_ed_utf8_length((unsigned char)bytes[0]) == n;
}

/** @brief The kind of typing step @p r is: an insertion at every piece,
 *         one caret step deleted at every piece, or neither. */
static int
typed_kind(const tiku_ed_record_t *r)
{
    int insert = 1, del = 1;
    size_t j;

    for (j = 0; j < r->npieces; j++) {
        const tiku_ed_piece_t *p = &r->pieces[j];

        insert &= p->added > 0;
        del &= p->added == 0 &&
               one_step(r->payload + p->removed_at, p->removed);
    }
    return insert ? TIKU_ED_TYPED_INSERT
                  : del ? TIKU_ED_TYPED_DELETE : TIKU_ED_TYPED_NONE;
}

/** @brief Check the replacements: in the text, on character boundaries,
 *         valid text, ascending and not overlapping.  Counts the ones
 *         that change something, and the bytes they remove and add. */
static tiku_edit_status_t
check_edits(const tiku_editdoc_t *doc, const tiku_edit_transaction_t *t,
            size_t *count, uint64_t *removed, uint64_t *added)
{
    const tiku_edit_replace_t *prev = NULL;
    uint64_t length = (uint64_t)doc->text.bytes.length, grown;
    size_t i;

    *count = 0;
    *removed = *added = 0;
    if (t->count && !t->edits)
        return TIKU_EDIT_INVALID;
    for (i = 0; i < t->count; i++) {
        const tiku_edit_replace_t *e = &t->edits[i];

        if (e->length && !e->text)
            return TIKU_EDIT_INVALID;
        if (e->range.start < 0 || e->range.start > e->range.end ||
            (uint64_t)e->range.end > length ||
            !tiku_editdoc_is_boundary(doc, e->range.start) ||
            !tiku_editdoc_is_boundary(doc, e->range.end))
            return TIKU_EDIT_INVALID_RANGE;
        if ((uint64_t)e->length > doc->max_bytes)
            return TIKU_EDIT_LIMIT;
        if (!tiku_ed_utf8_valid(e->text, e->length))
            return TIKU_EDIT_INVALID_TEXT;
        if (e->range.start == e->range.end && e->length == 0)
            continue;
        if (prev && !(prev->range.start < e->range.start &&
                      prev->range.end <= e->range.start))
            return TIKU_EDIT_INVALID_RANGE;
        if ((uint64_t)e->length > UINT64_MAX - *added)
            return TIKU_EDIT_LIMIT;
        *added += e->length;
        *removed += (uint64_t)(e->range.end - e->range.start);
        (*count)++;
        prev = e;
    }
    grown = length - *removed;
    if (*added > doc->max_bytes || grown > doc->max_bytes - *added ||
        *removed > SIZE_MAX - *added)
        return TIKU_EDIT_LIMIT;
    return TIKU_EDIT_OK;
}

/** @brief Copy the replacements into a record: pieces in both coordinate
 *         systems, and the removed and added bytes behind them. */
static tiku_edit_status_t
build_record(const tiku_editdoc_t *doc, const tiku_edit_transaction_t *t,
             size_t count, size_t payload_len, tiku_ed_record_t *r)
{
    size_t i, k = 0, off = 0;
    int64_t delta = 0;

    memset(r, 0, sizeof *r);
    if (count == 0)
        return TIKU_EDIT_OK;
    if (count > SIZE_MAX / sizeof *r->pieces)
        return TIKU_EDIT_LIMIT;
    r->pieces = tiku_ed_alloc(count * sizeof *r->pieces);
    r->payload = tiku_ed_alloc(payload_len ? payload_len : 1);
    if (!r->pieces || !r->payload) {
        tiku_ed_record_free(r);
        return TIKU_EDIT_NO_MEMORY;
    }
    for (i = 0; i < t->count; i++) {
        const tiku_edit_replace_t *e = &t->edits[i];
        tiku_ed_piece_t *p = &r->pieces[k];

        if (e->range.start == e->range.end && e->length == 0)
            continue;
        p->pre = e->range.start;
        p->post = e->range.start + delta;
        p->removed = e->range.end - e->range.start;
        p->added = (int64_t)e->length;
        p->removed_at = off;
        tiku_ed_bytes_copy(&doc->text.bytes, r->payload + off,
                           (ptrdiff_t)p->pre, (ptrdiff_t)p->removed);
        off += (size_t)p->removed;
        p->added_at = off;
        if (e->length)
            memcpy(r->payload + off, e->text, e->length);
        off += e->length;
        delta += p->added - p->removed;
        k++;
    }
    r->npieces = count;
    r->payload_len = payload_len;
    return TIKU_EDIT_OK;
}

/** @brief The byte at @p q in the text as it will be after @p r, 0
 *         outside: from the payload inside a piece's new text, from the
 *         text now elsewhere. */
static int
byte_after(const tiku_editdoc_t *doc, const tiku_ed_record_t *r, int64_t q)
{
    size_t lo = 0, hi = r->npieces;
    const tiku_ed_piece_t *p;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;

        if (r->pieces[mid].post <= q)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == 0)
        return at(doc, q);
    p = &r->pieces[lo - 1];
    if (q < p->post + p->added)
        return (unsigned char)r->payload[p->added_at + (size_t)(q - p->post)];
    return at(doc, p->pre + p->removed + (q - p->post - p->added));
}

/** @brief Whether @p q will be a caret position after @p r. */
static int
caret_after(const tiku_editdoc_t *doc, const tiku_ed_record_t *r, int64_t q,
            int64_t length)
{
    int b;

    if (q < 0 || q > length)
        return 0;
    b = byte_after(doc, r, q);
    if (q < length && (b & 0xc0) == 0x80)
        return 0;
    return !(q > 0 && q < length && b == '\n' &&
             byte_after(doc, r, q - 1) == '\r');
}

/** @brief Room for @p r applied forward or backward: the text's peak
 *         growth in the order the pieces go, and every line start the
 *         insertions could add on the way. */
static int
reserve_for(tiku_editdoc_t *doc, const tiku_ed_record_t *r, int inverse)
{
    int64_t growth = 0, peak = 0, lines = 0;
    size_t k;

    for (k = r->npieces; k-- > 0;) {
        const tiku_ed_piece_t *p = &r->pieces[k];
        int64_t in = inverse ? p->removed : p->added;
        int64_t out = inverse ? p->added : p->removed;
        size_t from = inverse ? p->removed_at : p->added_at;

        growth += in - out;
        if (growth > peak)
            peak = growth;
        lines += tiku_ed_text_line_cost(r->payload + from, in);
    }
    return tiku_ed_text_reserve(&doc->text, peak, lines);
}

/** @brief Apply @p r's pieces from the last to the first, so each one's
 *         coordinates still hold, moving anchors, selections, markers,
 *         indicators and annotations with the text.  Never allocates. */
static void
commit_pieces(tiku_editdoc_t *doc, const tiku_ed_record_t *r, int inverse)
{
    size_t k;

    for (k = r->npieces; k-- > 0;) {
        const tiku_ed_piece_t *p = &r->pieces[k];
        int64_t pos = inverse ? p->post : p->pre;
        int64_t out = inverse ? p->added : p->removed;
        int64_t in = inverse ? p->removed : p->added;
        const char *src = r->payload + (inverse ? p->removed_at : p->added_at);

        tiku_ed_text_delete(&doc->text, pos, out);
        tiku_ed_text_insert(&doc->text, pos, src, in);
        tiku_ed_marks_map(doc, pos, pos + out, in);
    }
    tiku_ed_marks_snap(doc);
}

/** @brief Whether the selections @p t sets will all be at caret
 *         positions after @p r. */
static int
selects_carets(const tiku_editdoc_t *doc, const tiku_edit_transaction_t *t,
               const tiku_ed_record_t *r, int64_t length)
{
    size_t i;

    if (t->nselections == 0)
        return caret_after(doc, r, t->anchor, length) &&
               caret_after(doc, r, t->caret, length);
    for (i = 0; i < t->nselections; i++)
        if (!caret_after(doc, r, t->selections[i].anchor, length) ||
            !caret_after(doc, r, t->selections[i].caret, length))
            return 0;
    return 1;
}

/**
 * @brief Keep the view's other selections in the record, with room for
 *        those after: the transaction's own, merged, or as many as the
 *        view has now, which mapping can only reduce.
 *
 * The view gets room for them too, so the commit allocates nothing.
 */
static tiku_edit_status_t
keep_selections(prepared_t *w, const tiku_edit_transaction_t *t)
{
    tiku_ed_record_t *r = &w->record;
    tiku_ed_view_t *v = w->view;
    tiku_edit_selection_t *out;
    size_t after = !t->select ? v->nmore :
                   t->nselections > 0 ? t->nselections - 1 : 0, i, k = 0;

    r->nbefore = v->nmore;
    if (r->nbefore + after == 0)
        return TIKU_EDIT_OK;
    if (after > SIZE_MAX / sizeof *r->more - r->nbefore)
        return TIKU_EDIT_LIMIT;
    r->more = tiku_ed_alloc((r->nbefore + after) * sizeof *r->more);
    if (!r->more)
        return TIKU_EDIT_NO_MEMORY;
    if (r->nbefore)
        memcpy(r->more, v->more, r->nbefore * sizeof *r->more);
    if (t->select && t->nselections > 0) {
        out = r->more + r->nbefore;
        for (i = 0; i < t->nselections; i++)
            if (i != t->main)
                out[k++] = t->selections[i];
        tiku_ed_selections_sort(out, k);
        w->main = t->selections[t->main];
        tiku_ed_selections_merge(&w->main, out, &k);
        after = k;
    }
    r->nafter = after;
    return tiku_ed_view_room(v, after) ? TIKU_EDIT_OK : TIKU_EDIT_NO_MEMORY;
}

/** @brief Everything that can fail, done before the commit. */
static tiku_edit_status_t
prepare(tiku_editdoc_t *doc, const tiku_edit_transaction_t *t, prepared_t *w)
{
    size_t count;
    uint64_t removed, added;
    int64_t length;
    tiku_edit_status_t st;

    memset(w, 0, sizeof *w);
    if (t->view.slot) {
        w->view = tiku_ed_view_get(doc, t->view);
        if (!w->view)
            return TIKU_EDIT_INVALID;
    } else if (t->select) {
        return TIKU_EDIT_INVALID;
    }
    if (t->select && t->nselections > 0 &&
        (!t->selections || t->main >= t->nselections))
        return TIKU_EDIT_INVALID;
    st = check_edits(doc, t, &count, &removed, &added);
    if (st != TIKU_EDIT_OK)
        return st;
    st = build_record(doc, t, count, (size_t)(removed + added), &w->record);
    if (st != TIKU_EDIT_OK)
        return st;
    length = (int64_t)(doc->text.bytes.length - removed + added);
    if (t->select && !selects_carets(doc, t, &w->record, length)) {
        tiku_ed_record_free(&w->record);
        return TIKU_EDIT_INVALID_RANGE;
    }
    if (count == 0)
        return TIKU_EDIT_OK;
    w->record.view = t->view;
    if (w->view) {
        w->record.before_anchor = w->view->anchor;
        w->record.before_caret = w->view->caret;
        st = keep_selections(w, t);
        if (st != TIKU_EDIT_OK) {
            tiku_ed_record_free(&w->record);
            return st;
        }
    }
    if (t->flags & TIKU_EDIT_TYPING)
        w->record.typed = typed_kind(&w->record);
    w->join = tiku_ed_history_joins(&doc->history, &w->record);
    if ((w->join && !tiku_ed_history_merge(&doc->history, &w->record,
                                           w->record.nafter, &w->merged)) ||
        (!w->join && !tiku_ed_history_reserve(&doc->history)) ||
        !reserve_for(doc, &w->record, 0)) {
        tiku_ed_record_free(&w->merged);
        tiku_ed_record_free(&w->record);
        return TIKU_EDIT_NO_MEMORY;
    }
    return TIKU_EDIT_OK;
}

/** @brief Give view @p v the selections @p t sets, prepared in @p w. */
static void
set_selections(tiku_ed_view_t *v, const prepared_t *w,
               const tiku_edit_transaction_t *t)
{
    if (t->nselections == 0) {
        v->anchor = t->anchor;
        v->caret = t->caret;
        v->nmore = 0;
        return;
    }
    v->anchor = w->main.anchor;
    v->caret = w->main.caret;
    if (w->record.nafter)
        memcpy(v->more, w->record.more + w->record.nbefore,
               w->record.nafter * sizeof *v->more);
    v->nmore = w->record.nafter;
}

/** @brief Tell listeners about a committed change of @p r. */
static void
announce(tiku_editdoc_t *doc, tiku_edit_cause_t cause,
         const tiku_ed_record_t *r, const tiku_edit_view_t *view,
         uint64_t generation, int64_t lines)
{
    tiku_edit_change_t c;

    memset(&c, 0, sizeof c);
    c.cause = cause;
    c.generation_before = generation;
    c.generation_after = doc->generation;
    c.lines_before = lines;
    c.lines_after = tiku_ed_lines_count(&doc->text.lines);
    c.count = r->npieces;
    if (view && tiku_ed_view_get(doc, *view))
        c.view = *view;
    c.detail = r->pieces;
    c.inverse = cause == TIKU_EDIT_UNDONE;
    tiku_ed_dispatch(doc, &c);
}

/** @brief A transaction that changes no text: at most a selection set. */
static tiku_edit_status_t
select_only(tiku_editdoc_t *doc, const tiku_edit_transaction_t *t,
            prepared_t *w)
{
    tiku_edit_status_t st = TIKU_EDIT_OK;

    if (t->select && t->nselections > 0)
        st = tiku_editdoc_view_select_many(doc, t->view, t->selections,
                                           t->nselections, t->main);
    else if (t->select)
        st = tiku_editdoc_view_select(doc, t->view, t->anchor, t->caret);
    tiku_ed_record_free(&w->merged);
    tiku_ed_record_free(&w->record);
    return st;
}

tiku_edit_status_t
tiku_editdoc_apply(tiku_editdoc_t *doc, const tiku_edit_transaction_t *t)
{
    prepared_t w;
    tiku_ed_history_t *h;
    uint64_t generation;
    int64_t lines;
    tiku_edit_status_t st;

    if (!doc || !t)
        return TIKU_EDIT_INVALID;
    if (doc->busy)
        return TIKU_EDIT_BUSY;
    if (doc->read_only)
        return TIKU_EDIT_READ_ONLY;
    if (t->generation && t->generation != doc->generation)
        return TIKU_EDIT_STALE;
    st = prepare(doc, t, &w);
    if (st != TIKU_EDIT_OK)
        return st;
    if (w.record.npieces == 0)
        return select_only(doc, t, &w);

    h = &doc->history;
    generation = doc->generation;
    lines = tiku_ed_lines_count(&doc->text.lines);
    tiku_ed_commit_begin();
    commit_pieces(doc, &w.record, 0);
    if (w.view && t->select)
        set_selections(w.view, &w, t);
    if (w.view) {
        w.record.after_anchor = w.view->anchor;
        w.record.after_caret = w.view->caret;
        if (w.view->nmore)
            memcpy(w.record.more + w.record.nbefore, w.view->more,
                   w.view->nmore * sizeof *w.view->more);
        w.record.nafter = w.view->nmore;
    }
    if (w.join) {
        if (w.view && w.view->nmore)
            memcpy(w.merged.more + w.merged.nbefore, w.view->more,
                   w.view->nmore * sizeof *w.view->more);
        w.merged.nafter = w.view ? w.view->nmore : 0;
        tiku_ed_history_replace_last(h, &w.merged);
        h->records[h->current - 1].after_anchor = w.record.after_anchor;
        h->records[h->current - 1].after_caret = w.record.after_caret;
    } else {
        w.record.state_before = tiku_ed_history_state(h);
        w.record.state_after = h->next_state++;
        tiku_ed_history_push(h, &w.record);
    }
    h->open = w.record.typed != TIKU_ED_TYPED_NONE;
    doc->generation++;
    tiku_ed_commit_end();

    tiku_ed_record_free(&w.merged);
    tiku_ed_history_prune(h);
    announce(doc, TIKU_EDIT_EDITED,
             w.join ? &w.record : &h->records[h->current - 1],
             &t->view, generation, lines);
    if (w.join)
        tiku_ed_record_free(&w.record);
    return TIKU_EDIT_OK;
}

/** @brief Undo or redo the record next to the current state. */
static tiku_edit_status_t
step(tiku_editdoc_t *doc, int back)
{
    tiku_ed_history_t *h;
    tiku_ed_record_t *r;
    tiku_ed_view_t *v;
    uint64_t generation;
    int64_t lines;
    size_t n;

    if (!doc)
        return TIKU_EDIT_INVALID;
    if (doc->busy)
        return TIKU_EDIT_BUSY;
    if (doc->read_only)
        return TIKU_EDIT_READ_ONLY;
    h = &doc->history;
    if (back ? h->current == 0 : h->current == h->count)
        return TIKU_EDIT_NOTHING;
    r = &h->records[back ? h->current - 1 : h->current];
    v = tiku_ed_view_get(doc, r->view);
    n = back ? r->nbefore : r->nafter;
    if ((v && !tiku_ed_view_room(v, n)) || !reserve_for(doc, r, back))
        return TIKU_EDIT_NO_MEMORY;

    generation = doc->generation;
    lines = tiku_ed_lines_count(&doc->text.lines);
    tiku_ed_commit_begin();
    commit_pieces(doc, r, back);
    if (v) {
        v->anchor = back ? r->before_anchor : r->after_anchor;
        v->caret = back ? r->before_caret : r->after_caret;
        if (n)
            memcpy(v->more, r->more + (back ? 0 : r->nbefore),
                   n * sizeof *v->more);
        v->nmore = n;
    }
    h->current += back ? -1 : 1;
    h->open = 0;
    doc->generation++;
    tiku_ed_commit_end();
    announce(doc, back ? TIKU_EDIT_UNDONE : TIKU_EDIT_REDONE, r, &r->view,
             generation, lines);
    return TIKU_EDIT_OK;
}

tiku_edit_status_t
tiku_editdoc_undo(tiku_editdoc_t *doc)
{
    return step(doc, 1);
}

tiku_edit_status_t
tiku_editdoc_redo(tiku_editdoc_t *doc)
{
    return step(doc, 0);
}

int
tiku_editdoc_can_undo(const tiku_editdoc_t *doc)
{
    return doc && doc->history.current > 0;
}

int
tiku_editdoc_can_redo(const tiku_editdoc_t *doc)
{
    return doc && doc->history.current < doc->history.count;
}

void
tiku_editdoc_seal(tiku_editdoc_t *doc)
{
    if (doc)
        doc->history.open = 0;
}
