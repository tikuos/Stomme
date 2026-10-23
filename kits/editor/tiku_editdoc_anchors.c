/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_editdoc_anchors.c - positions that follow the text: anchors, each
 * view's selections, and the listeners told of every change.
 *
 * The selection rule is adapted from Scintilla 5.6.7 src/Selection.cxx,
 * Copyright 2009 by Neil Hodgson; see LICENSE.Scintilla and UPSTREAM.md.
 *
 * SPDX-License-Identifier: Apache-2.0 AND LicenseRef-Scintilla
 */

#include "tiku_editdoc_private.h"

#include <string.h>

/** @brief A slot's next life: never 0, so a zeroed handle is none. */
static uint32_t
next_generation(uint32_t generation)
{
    return generation + 1 ? generation + 1 : 1;
}

/** @brief Grow a slot array by doubling; @return 0 when it cannot. */
static int
grow_slots(void **slots, size_t *count, size_t size)
{
    size_t n = *count ? *count * 2 : 4;
    char *grown;

    if (n > UINT32_MAX || n > SIZE_MAX / size)
        return 0;
    grown = tiku_ed_realloc(*slots, n * size);
    if (!grown)
        return 0;
    memset(grown + *count * size, 0, (n - *count) * size);
    *slots = grown;
    *count = n;
    return 1;
}

/** @brief Where one anchor goes when [s, e) becomes n bytes: inside or at
 *         either end, to the new text's start or end by its affinity. */
static int64_t
map_anchor(int64_t p, int right, int64_t s, int64_t e, int64_t n)
{
    if (p < s)
        return p;
    if (p <= e)
        return right ? s + n : s;
    return p - (e - s) + n;
}

/** @brief A selection end through a deletion of [s, e): one after @p s up
 *         to @p e goes to @p s, one past @p e moves back with the text. */
static int64_t
after_delete(int64_t p, int64_t s, int64_t e)
{
    if (p > s)
        return p > e ? p - (e - s) : s;
    return p;
}

/** @brief A selection end through @p n bytes inserted at @p s; it moves
 *         past them at s only when it starts a non-empty selection. */
static int64_t
after_insert(int64_t p, int64_t s, int64_t n, int starts)
{
    if (p == s)
        return starts ? p + n : p;
    return p > s ? p + n : p;
}

/** @brief A selection through a replacement of [s, e) by n bytes: a
 *         deletion then an insertion, its start and end decided afresh
 *         between them. */
static void
map_selection(int64_t *anchor, int64_t *caret, int64_t s, int64_t e,
              int64_t n)
{
    int caret_starts, anchor_starts;

    *anchor = after_delete(*anchor, s, e);
    *caret = after_delete(*caret, s, e);
    caret_starts = *caret < *anchor;
    anchor_starts = *anchor < *caret;
    *anchor = after_insert(*anchor, s, n, anchor_starts);
    *caret = after_insert(*caret, s, n, caret_starts);
}

void
tiku_ed_marks_map(tiku_editdoc_t *doc, int64_t s, int64_t e, int64_t n)
{
    size_t i, k;

    for (i = 0; i < doc->nanchors; i++) {
        tiku_ed_anchor_t *a = &doc->anchors[i];

        if (a->live)
            a->pos = map_anchor(a->pos, a->right, s, e, n);
    }
    for (i = 0; i < doc->nviews; i++) {
        tiku_ed_view_t *v = &doc->views[i];

        if (!v->live)
            continue;
        map_selection(&v->anchor, &v->caret, s, e, n);
        for (k = 0; k < v->nmore; k++)
            map_selection(&v->more[k].anchor, &v->more[k].caret, s, e, n);
    }
    tiku_ed_markers_map(doc, s, e, n);
    tiku_ed_notes_map(doc, s, e, n);
}

/** @brief @p p moved back to the CR when it sits between a CR and an LF;
 *         an edit can join the two around a caret. */
static int64_t
snap(const tiku_editdoc_t *doc, int64_t p)
{
    const tiku_ed_bytes_t *b = &doc->text.bytes;

    if (p > 0 && tiku_ed_bytes_at(b, (ptrdiff_t)p - 1) == '\r' &&
        tiku_ed_bytes_at(b, (ptrdiff_t)p) == '\n' && p < b->length)
        return p - 1;
    return p;
}

/** @brief Where selection @p s starts. */
static int64_t
lo_of(const tiku_edit_selection_t *s)
{
    return s->anchor < s->caret ? s->anchor : s->caret;
}

/** @brief Where selection @p s ends. */
static int64_t
hi_of(const tiku_edit_selection_t *s)
{
    return s->anchor < s->caret ? s->caret : s->anchor;
}

/** @brief Whether @p b, starting no earlier than @p a, becomes one with
 *         it: they overlap, or one is a caret touching the other. */
static int
joins(const tiku_edit_selection_t *a, const tiku_edit_selection_t *b)
{
    if (a->anchor == a->caret || b->anchor == b->caret)
        return lo_of(b) <= hi_of(a);
    return lo_of(b) < hi_of(a);
}

/** @brief @p a grown over @p b, facing the way it does, or @p b's way
 *         when it is a caret. */
static void
take_in(tiku_edit_selection_t *a, const tiku_edit_selection_t *b)
{
    int64_t lo = lo_of(a) < lo_of(b) ? lo_of(a) : lo_of(b);
    int64_t hi = hi_of(a) > hi_of(b) ? hi_of(a) : hi_of(b);
    int back = a->anchor != a->caret ? a->caret < a->anchor
                                     : b->caret < b->anchor;

    a->anchor = back ? hi : lo;
    a->caret = back ? lo : hi;
}

/** @brief Whether @p a comes before @p b: by start, then by end. */
static int
before(const tiku_edit_selection_t *a, const tiku_edit_selection_t *b)
{
    return lo_of(a) != lo_of(b) ? lo_of(a) < lo_of(b) : hi_of(a) <= hi_of(b);
}

void
tiku_ed_selections_merge(tiku_edit_selection_t *main,
                         tiku_edit_selection_t *more, size_t *n)
{
    tiku_edit_selection_t cur, next;
    size_t i = 0, kept = 0;
    int have = 0, cur_main = 0, main_read = 0, next_main;

    /* One sweep in order of starts, the main one read in its place; what
     * is written back never passes what has been read. */
    for (;;) {
        if (!main_read && (i == *n || before(main, &more[i]))) {
            next = *main;
            next_main = main_read = 1;
        } else if (i < *n) {
            next = more[i++];
            next_main = 0;
        } else {
            break;
        }
        if (have && joins(&cur, &next)) {
            if (next_main) {
                take_in(&next, &cur);
                cur = next;
            } else {
                take_in(&cur, &next);
            }
            cur_main |= next_main;
            continue;
        }
        if (have && cur_main)
            *main = cur;
        else if (have)
            more[kept++] = cur;
        cur = next;
        cur_main = next_main;
        have = 1;
    }
    if (cur_main)
        *main = cur;
    else
        more[kept++] = cur;
    *n = kept;
}

void
tiku_ed_marks_snap(tiku_editdoc_t *doc)
{
    size_t i, k;

    for (i = 0; i < doc->nviews; i++) {
        tiku_ed_view_t *v = &doc->views[i];
        tiku_edit_selection_t main;

        if (!v->live)
            continue;
        main.anchor = snap(doc, v->anchor);
        main.caret = snap(doc, v->caret);
        for (k = 0; k < v->nmore; k++) {
            v->more[k].anchor = snap(doc, v->more[k].anchor);
            v->more[k].caret = snap(doc, v->more[k].caret);
        }
        /* Mapping keeps them in order, but can bring them together. */
        tiku_ed_selections_merge(&main, v->more, &v->nmore);
        v->anchor = main.anchor;
        v->caret = main.caret;
    }
    tiku_ed_markers_settle(doc);
    tiku_ed_notes_settle(doc);
}

void
tiku_ed_marks_free(tiku_editdoc_t *doc)
{
    size_t i;

    for (i = 0; i < doc->nviews; i++)
        tiku_ed_free(doc->views[i].more);
    tiku_ed_free(doc->anchors);
    tiku_ed_free(doc->views);
    tiku_ed_free(doc->listeners);
    doc->anchors = NULL;
    doc->views = NULL;
    doc->listeners = NULL;
    doc->nanchors = doc->nviews = doc->nlisteners = 0;
}

tiku_ed_view_t *
tiku_ed_view_get(const tiku_editdoc_t *doc, tiku_edit_view_t view)
{
    tiku_ed_view_t *v;

    if (!doc || view.slot == 0 || view.slot > doc->nviews)
        return NULL;
    v = &doc->views[view.slot - 1];
    return v->live && v->generation == view.generation ? v : NULL;
}

int
tiku_ed_view_room(tiku_ed_view_t *v, size_t n)
{
    tiku_edit_selection_t *grown;
    size_t cap;

    if (n <= v->more_cap)
        return 1;
    if (n > (SIZE_MAX / sizeof *grown - 4) / 2)
        return 0;
    cap = n + n / 2 + 4;
    grown = tiku_ed_realloc(v->more, cap * sizeof *grown);
    if (!grown)
        return 0;
    v->more = grown;
    v->more_cap = cap;
    return 1;
}

/** @brief The live anchor a handle names, or NULL. */
static tiku_ed_anchor_t *
anchor_get(const tiku_editdoc_t *doc, tiku_edit_anchor_t anchor)
{
    tiku_ed_anchor_t *a;

    if (!doc || anchor.slot == 0 || anchor.slot > doc->nanchors)
        return NULL;
    a = &doc->anchors[anchor.slot - 1];
    return a->live && a->generation == anchor.generation ? a : NULL;
}

tiku_edit_status_t
tiku_editdoc_anchor_create(tiku_editdoc_t *doc, tiku_edit_pos_t pos,
                           tiku_edit_affinity_t affinity,
                           tiku_edit_anchor_t *anchor)
{
    size_t i;

    if (!doc || !anchor)
        return TIKU_EDIT_INVALID;
    if (!tiku_editdoc_is_boundary(doc, pos))
        return TIKU_EDIT_INVALID_RANGE;
    for (i = 0; i < doc->nanchors && doc->anchors[i].live; i++)
        ;
    if (i == doc->nanchors &&
        !grow_slots((void **)&doc->anchors, &doc->nanchors,
                    sizeof *doc->anchors))
        return TIKU_EDIT_NO_MEMORY;
    doc->anchors[i].generation =
        next_generation(doc->anchors[i].generation);
    doc->anchors[i].live = 1;
    doc->anchors[i].right = affinity == TIKU_EDIT_RIGHT;
    doc->anchors[i].pos = pos;
    anchor->slot = (uint32_t)i + 1;
    anchor->generation = doc->anchors[i].generation;
    return TIKU_EDIT_OK;
}

tiku_edit_status_t
tiku_editdoc_anchor_pos(const tiku_editdoc_t *doc, tiku_edit_anchor_t anchor,
                        tiku_edit_pos_t *pos)
{
    const tiku_ed_anchor_t *a = anchor_get(doc, anchor);

    if (!a || !pos)
        return TIKU_EDIT_INVALID;
    *pos = a->pos;
    return TIKU_EDIT_OK;
}

tiku_edit_status_t
tiku_editdoc_anchor_move(tiku_editdoc_t *doc, tiku_edit_anchor_t anchor,
                         tiku_edit_pos_t pos)
{
    tiku_ed_anchor_t *a = anchor_get(doc, anchor);

    if (!a)
        return TIKU_EDIT_INVALID;
    if (!tiku_editdoc_is_boundary(doc, pos))
        return TIKU_EDIT_INVALID_RANGE;
    a->pos = pos;
    return TIKU_EDIT_OK;
}

void
tiku_editdoc_anchor_destroy(tiku_editdoc_t *doc, tiku_edit_anchor_t anchor)
{
    tiku_ed_anchor_t *a = anchor_get(doc, anchor);

    if (a) {
        a->live = 0;
        a->generation = next_generation(a->generation);
    }
}

tiku_edit_status_t
tiku_editdoc_view_attach(tiku_editdoc_t *doc, tiku_edit_view_t *view)
{
    size_t i;

    if (!doc || !view)
        return TIKU_EDIT_INVALID;
    for (i = 0; i < doc->nviews && doc->views[i].live; i++)
        ;
    if (i == doc->nviews &&
        !grow_slots((void **)&doc->views, &doc->nviews, sizeof *doc->views))
        return TIKU_EDIT_NO_MEMORY;
    doc->views[i].generation = next_generation(doc->views[i].generation);
    doc->views[i].live = 1;
    doc->views[i].anchor = 0;
    doc->views[i].caret = 0;
    view->slot = (uint32_t)i + 1;
    view->generation = doc->views[i].generation;
    return TIKU_EDIT_OK;
}

void
tiku_editdoc_view_detach(tiku_editdoc_t *doc, tiku_edit_view_t view)
{
    tiku_ed_view_t *v = tiku_ed_view_get(doc, view);

    if (v) {
        v->live = 0;
        v->generation = next_generation(v->generation);
        tiku_ed_free(v->more);
        v->more = NULL;
        v->nmore = v->more_cap = 0;
    }
}

tiku_edit_status_t
tiku_editdoc_view_selection(const tiku_editdoc_t *doc, tiku_edit_view_t view,
                            tiku_edit_pos_t *anchor, tiku_edit_pos_t *caret)
{
    const tiku_ed_view_t *v = tiku_ed_view_get(doc, view);

    if (!v || !anchor || !caret)
        return TIKU_EDIT_INVALID;
    *anchor = v->anchor;
    *caret = v->caret;
    return TIKU_EDIT_OK;
}

/** @brief End @p view's typing step, when it is typing. */
static void
end_typing(tiku_editdoc_t *doc, tiku_edit_view_t view)
{
    tiku_ed_history_t *h = &doc->history;

    if (h->open && h->current > 0 &&
        h->records[h->current - 1].view.slot == view.slot &&
        h->records[h->current - 1].view.generation == view.generation)
        h->open = 0;
}

tiku_edit_status_t
tiku_editdoc_view_select(tiku_editdoc_t *doc, tiku_edit_view_t view,
                         tiku_edit_pos_t anchor, tiku_edit_pos_t caret)
{
    tiku_ed_view_t *v = tiku_ed_view_get(doc, view);

    if (!v)
        return TIKU_EDIT_INVALID;
    if (!tiku_editdoc_is_caret(doc, anchor) ||
        !tiku_editdoc_is_caret(doc, caret))
        return TIKU_EDIT_INVALID_RANGE;
    if (v->anchor != anchor || v->caret != caret || v->nmore)
        v->selection_serial++;
    v->anchor = anchor;
    v->caret = caret;
    v->nmore = 0;
    end_typing(doc, view);
    return TIKU_EDIT_OK;
}

void
tiku_ed_selections_sort(tiku_edit_selection_t *more, size_t n)
{
    size_t i, j;

    /* An insertion sort, allocating nothing: linear on input nearly in
     * order, quadratic at worst. */
    for (i = 1; i < n; i++) {
        tiku_edit_selection_t t = more[i];

        for (j = i; j > 0 && !before(&more[j - 1], &t); j--)
            more[j] = more[j - 1];
        more[j] = t;
    }
}

tiku_edit_status_t
tiku_editdoc_view_select_many(tiku_editdoc_t *doc, tiku_edit_view_t view,
                              const tiku_edit_selection_t *selections,
                              size_t count, size_t main)
{
    tiku_ed_view_t *v = tiku_ed_view_get(doc, view);
    tiku_edit_selection_t m;
    size_t i, k = 0;

    if (!v || !selections || count == 0 || main >= count)
        return TIKU_EDIT_INVALID;
    for (i = 0; i < count; i++)
        if (!tiku_editdoc_is_caret(doc, selections[i].anchor) ||
            !tiku_editdoc_is_caret(doc, selections[i].caret))
            return TIKU_EDIT_INVALID_RANGE;
    if (!tiku_ed_view_room(v, count - 1))
        return TIKU_EDIT_NO_MEMORY;
    for (i = 0; i < count; i++)
        if (i != main)
            v->more[k++] = selections[i];
    tiku_ed_selections_sort(v->more, k);
    m = selections[main];
    tiku_ed_selections_merge(&m, v->more, &k);
    v->anchor = m.anchor;
    v->caret = m.caret;
    v->nmore = k;
    v->selection_serial++;
    end_typing(doc, view);
    return TIKU_EDIT_OK;
}

size_t
tiku_editdoc_view_selections(const tiku_editdoc_t *doc, tiku_edit_view_t view,
                             tiku_edit_selection_t *out, size_t cap,
                             size_t *main)
{
    const tiku_ed_view_t *v = tiku_ed_view_get(doc, view);
    tiku_edit_selection_t m;
    size_t i, at = 0;

    if (!v)
        return 0;
    m.anchor = v->anchor;
    m.caret = v->caret;
    while (at < v->nmore && lo_of(&v->more[at]) < lo_of(&m))
        at++;
    for (i = 0; out && i < cap && i <= v->nmore; i++)
        out[i] = i < at ? v->more[i] : i == at ? m : v->more[i - 1];
    if (main)
        *main = at;
    return v->nmore + 1;
}

tiku_edit_status_t
tiku_editdoc_listen(tiku_editdoc_t *doc, tiku_edit_listen_fn fn, void *ctx,
                    tiku_edit_listener_t *listener)
{
    size_t i;

    if (!doc || !fn || !listener)
        return TIKU_EDIT_INVALID;
    for (i = 0; i < doc->nlisteners && doc->listeners[i].live; i++)
        ;
    if (i == doc->nlisteners &&
        !grow_slots((void **)&doc->listeners, &doc->nlisteners,
                    sizeof *doc->listeners))
        return TIKU_EDIT_NO_MEMORY;
    doc->listeners[i].generation =
        next_generation(doc->listeners[i].generation);
    doc->listeners[i].live = 1;
    doc->listeners[i].fn = fn;
    doc->listeners[i].ctx = ctx;
    /* One added during a notification hears the next one, not this. */
    doc->listeners[i].since = doc->busy ? doc->dispatch : 0;
    listener->slot = (uint32_t)i + 1;
    listener->generation = doc->listeners[i].generation;
    return TIKU_EDIT_OK;
}

void
tiku_editdoc_unlisten(tiku_editdoc_t *doc, tiku_edit_listener_t listener)
{
    tiku_ed_listener_t *l;

    if (!doc || listener.slot == 0 || listener.slot > doc->nlisteners)
        return;
    l = &doc->listeners[listener.slot - 1];
    if (l->live && l->generation == listener.generation) {
        l->live = 0;
        l->fn = NULL;
        l->generation = next_generation(l->generation);
    }
}

void
tiku_ed_dispatch(tiku_editdoc_t *doc, const tiku_edit_change_t *change)
{
    size_t i;
    uint64_t serial;

    if (doc->nlisteners == 0)
        return;
    tiku_editdoc_retain(doc);
    doc->busy++;
    serial = ++doc->dispatch;
    /* By index, the array read afresh each time: a listener may add or
     * remove listeners, and the array may move. */
    for (i = 0; i < doc->nlisteners; i++) {
        tiku_ed_listener_t *l = &doc->listeners[i];

        if (l->live && l->since != serial)
            l->fn(l->ctx, doc, change);
    }
    doc->busy--;
    tiku_editdoc_release(doc);
}

void
tiku_edit_change_span(const tiku_edit_change_t *change, size_t i,
                      tiku_edit_range_t *before, tiku_edit_range_t *after)
{
    const tiku_ed_piece_t *p;
    tiku_edit_range_t pre, post;

    if (!change || i >= change->count || !change->detail)
        return;
    p = (const tiku_ed_piece_t *)change->detail + i;
    pre.start = p->pre;
    pre.end = p->pre + p->removed;
    post.start = p->post;
    post.end = p->post + p->added;
    if (before)
        *before = change->inverse ? post : pre;
    if (after)
        *after = change->inverse ? pre : post;
}
