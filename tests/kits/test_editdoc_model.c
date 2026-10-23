/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_editdoc_model.c - the document against a plain model of itself:
 * a string, copies of it for history, positions moved by the rules as
 * written.  Thousands of random edits, undos, redos, saves, loads,
 * anchors and selections per seed, everything compared after each.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_editdoc.h"

#define MAXTEXT   2048          /* the model's longest text              */
#define STEPS     4096          /* history the model keeps               */
#define ANCHORS   8             /* anchors per run                       */
#define VIEWS     3             /* views per run                         */
#define PIECES    3             /* replacements per transaction          */

static int fails;
static unsigned long long seed;
static long applied, stepped, longest;  /* what the runs exercised */

/** @brief Count a failure, naming the seed and operation that broke. */
static void
check(int cond, const char *row, const char *what, int op)
{
    if (!cond) {
        if (fails < 20)
            printf("  [FAIL] %-9s %s (seed %llu, op %d)\n", row, what,
                   seed, op);
        fails++;
    }
}

/** @brief The next of a fixed stream of numbers, below @p n. */
static unsigned
rnd(unsigned n)
{
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return (unsigned)(seed % n);
}

/** @brief One replacement as the model applies it. */
typedef struct {
    int64_t pre, post;          /* start before and after                */
    int64_t removed, added;     /* bytes out and in                      */
    char out[16], in[16];       /* the bytes                             */
} mpiece_t;

/** @brief One step of the model's history. */
typedef struct {
    mpiece_t piece[PIECES];     /* its replacements, ascending           */
    int count;                  /* how many                              */
    int view;                   /* the view it came from, or -1          */
    int64_t before[2], after[2];/* that view's selection around it       */
    int id;                     /* its state, never reused               */
} mstep_t;

/** @brief The whole model. */
typedef struct {
    char text[MAXTEXT];         /* the text                              */
    int64_t n;                  /* its length                            */
    mstep_t step[STEPS];        /* the history                           */
    int count, current;         /* steps held, steps applied             */
    int base, saved, next_id;   /* states: before step 0, saved, next    */
    int64_t anchor[ANCHORS];    /* anchor positions                      */
    int right[ANCHORS];         /* their affinities                      */
    int alive[ANCHORS];         /* which exist                           */
    int64_t sel[VIEWS][2];      /* each view's anchor and caret          */
} model_t;

static model_t m;
static tiku_editdoc_t *doc;
static tiku_edit_anchor_t handle[ANCHORS];
static tiku_edit_view_t view[VIEWS];

/** @brief The rules as the contract states them, written again from the
 *         words: an anchor inside or at the ends of [s, e) goes to the
 *         new text's start or end by affinity; beyond, it shifts. */
static int64_t
move_anchor(int64_t p, int right, int64_t s, int64_t e, int64_t n)
{
    if (p < s)
        return p;
    if (p > e)
        return p + n - (e - s);
    return right ? s + n : s;
}

/** @brief A selection: delete then insert; an end at the insertion point
 *         moves only if it starts a non-empty selection. */
static void
move_selection(int64_t *a, int64_t *c, int64_t s, int64_t e, int64_t n)
{
    int a_first, c_first;

    if (*a > s)
        *a = *a > e ? *a - (e - s) : s;
    if (*c > s)
        *c = *c > e ? *c - (e - s) : s;
    a_first = *a < *c;
    c_first = *c < *a;
    if (*a > s || (*a == s && a_first))
        *a += n;
    if (*c > s || (*c == s && c_first))
        *c += n;
}

/** @brief Replace [s, s+out) of the model text by @p in, moving every
 *         anchor and selection. */
static void
model_replace(int64_t s, int64_t out, const char *in, int64_t n)
{
    int i;

    memmove(m.text + s + n, m.text + s + out, (size_t)(m.n - s - out));
    memcpy(m.text + s, in, (size_t)n);
    m.n += n - out;
    for (i = 0; i < ANCHORS; i++)
        if (m.alive[i])
            m.anchor[i] = move_anchor(m.anchor[i], m.right[i], s, s + out, n);
    for (i = 0; i < VIEWS; i++)
        move_selection(&m.sel[i][0], &m.sel[i][1], s, s + out, n);
}

/** @brief Selection ends between a CR and an LF step back to the CR. */
static void
model_snap(void)
{
    int i, k;

    for (i = 0; i < VIEWS; i++)
        for (k = 0; k < 2; k++) {
            int64_t p = m.sel[i][k];

            if (p > 0 && p < m.n && m.text[p - 1] == '\r' && m.text[p] == '\n')
                m.sel[i][k] = p - 1;
        }
}

/** @brief Apply a model step forward or back; @p restore sets the origin
 *         view's selection as undo and redo do. */
static void
model_step(const mstep_t *st, int back, int restore)
{
    int k;

    for (k = st->count - 1; k >= 0; k--) {
        const mpiece_t *p = &st->piece[k];

        if (back)
            model_replace(p->post, p->added, p->out, p->removed);
        else
            model_replace(p->pre, p->removed, p->in, p->added);
    }
    model_snap();
    if (restore && st->view >= 0) {
        m.sel[st->view][0] = back ? st->before[0] : st->after[0];
        m.sel[st->view][1] = back ? st->before[1] : st->after[1];
    }
}

/** @brief Whether @p pos is a character boundary of the model text. */
static int
model_boundary(int64_t pos)
{
    return pos >= 0 && pos <= m.n &&
           (pos == m.n || ((unsigned char)m.text[pos] & 0xc0) != 0x80);
}

/** @brief Whether @p pos may hold a caret in the model text. */
static int
model_caret(int64_t pos)
{
    return model_boundary(pos) &&
           !(pos > 0 && pos < m.n && m.text[pos - 1] == '\r' &&
             m.text[pos] == '\n');
}

/** @brief A random character boundary of the model text. */
static int64_t
any_boundary(void)
{
    int64_t pos;

    do
        pos = rnd((unsigned)m.n + 1);
    while (!model_boundary(pos));
    return pos;
}

/** @brief A random caret position of the model text. */
static int64_t
any_caret(void)
{
    int64_t pos;

    do
        pos = rnd((unsigned)m.n + 1);
    while (!model_caret(pos));
    return pos;
}

/** @brief Random text of whole tokens, line breaks among them. */
static int64_t
random_text(char *out, int max_tokens)
{
    static const char *const tokens[] = {
        "a", "b", "\r", "\n", "\r\n", "\xc3\xa9", "\xf0\x9f\x98\x80"
    };
    int count = (int)rnd((unsigned)max_tokens + 1), i;
    int64_t n = 0;

    for (i = 0; i < count; i++) {
        const char *tok = tokens[rnd(sizeof tokens / sizeof *tokens)];
        size_t len = strlen(tok);

        memcpy(out + n, tok, len);
        n += (int64_t)len;
    }
    return n;
}

/** @brief Everything the document shows, compared with the model. */
static void
compare(int op)
{
    static char got[MAXTEXT + 1];
    size_t n = 0;
    tiku_edit_range_t all = { 0, tiku_editdoc_length(doc) };
    int64_t i, line = 0, starts = 1;
    int k;

    check(tiku_editdoc_read(doc, all, got, sizeof got, &n) == TIKU_EDIT_OK &&
          (int64_t)n == m.n && !memcmp(got, m.text, n), "MODEL-TEXT",
          "the bytes", op);
    for (i = 0; i < m.n; i++)
        if (m.text[i] == '\n' ||
            (m.text[i] == '\r' && (i + 1 >= m.n || m.text[i + 1] != '\n')))
            starts++;
    check(tiku_editdoc_lines(doc) == starts, "MODEL-LINES", "line count", op);
    check(tiku_editdoc_line_start(doc, 0) == 0, "MODEL-LINES", "line 0", op);
    for (i = 0; i < m.n; i++) {
        int brk = m.text[i] == '\n' ||
                  (m.text[i] == '\r' &&
                   (i + 1 >= m.n || m.text[i + 1] != '\n'));

        if (tiku_editdoc_line_of(doc, i) != line) {
            check(0, "MODEL-LINES", "the line of a position", op);
            break;
        }
        if (brk) {
            line++;
            check(tiku_editdoc_line_start(doc, line) == i + 1, "MODEL-LINES",
                  "a line's start", op);
        }
    }
    check(tiku_editdoc_line_of(doc, m.n) == starts - 1, "MODEL-LINES",
          "the end's line", op);
    check(tiku_editdoc_modified(doc) ==
          ((m.current ? m.step[m.current - 1].id : m.base) != m.saved),
          "MODEL-SAVED", "modified", op);
    check(tiku_editdoc_can_undo(doc) == (m.current > 0) &&
          tiku_editdoc_can_redo(doc) == (m.current < m.count), "MODEL-HIST",
          "undo and redo available", op);
    for (k = 0; k < ANCHORS; k++) {
        tiku_edit_pos_t pos = -1;

        if (m.alive[k])
            check(tiku_editdoc_anchor_pos(doc, handle[k], &pos) == 0 &&
                  pos == m.anchor[k], "MODEL-ANCHOR", "an anchor", op);
    }
    for (k = 0; k < VIEWS; k++) {
        tiku_edit_pos_t a = -1, c = -1;

        check(tiku_editdoc_view_selection(doc, view[k], &a, &c) == 0 &&
              a == m.sel[k][0] && c == m.sel[k][1], "MODEL-VIEW",
              "a selection", op);
    }
}

/** @brief A random transaction, applied to both. */
static void
op_apply(int op)
{
    tiku_edit_replace_t r[PIECES];
    mstep_t st;
    char text[PIECES][16];
    int64_t cut[2 * PIECES], delta = 0, grown = 0;
    tiku_edit_transaction_t t;
    int count = 1 + (int)rnd(PIECES), k, i, changes = 0;

    /* Starts anywhere, ends a few bytes on: local edits that let the
     * text grow to the model's cap. */
    for (i = 0; i < count; i++)
        cut[i] = any_boundary();
    for (i = 1; i < count; i++) {               /* sort the starts */
        int64_t x = cut[i];
        int j = i;

        while (j > 0 && cut[j - 1] > x) {
            cut[j] = cut[j - 1];
            j--;
        }
        cut[j] = x;
    }
    for (k = count - 1; k >= 0; k--) {
        int64_t limit = k + 1 < count ? cut[2 * (k + 1)] : m.n;
        int64_t e = cut[k] + (int64_t)rnd(7);

        if (e > limit)
            e = limit;
        while (!model_boundary(e))
            e--;
        cut[2 * k + 1] = e;
        cut[2 * k] = cut[k];
    }
    memset(&st, 0, sizeof st);
    for (k = 0; k < count; k++) {
        int64_t n = random_text(text[k], 4);

        r[k].range.start = cut[2 * k];
        r[k].range.end = cut[2 * k + 1];
        r[k].text = text[k];
        r[k].length = (size_t)n;
        if (k && r[k].range.start == r[k - 1].range.start)
            return;                             /* ambiguous; not sent */
        grown += n - (r[k].range.end - r[k].range.start);
        if (r[k].range.end - r[k].range.start > 15)
            return;                             /* model pieces are small */
    }
    if (m.n + grown >= MAXTEXT - 64)
        return;
    memset(&t, 0, sizeof t);
    t.edits = r;
    t.count = (size_t)count;
    st.view = -1;
    if (rnd(2)) {
        st.view = (int)rnd(VIEWS);
        t.view = view[st.view];
        st.before[0] = m.sel[st.view][0];
        st.before[1] = m.sel[st.view][1];
    }
    for (k = 0, st.count = 0; k < count; k++) {
        mpiece_t *p = &st.piece[st.count];

        if (r[k].range.start == r[k].range.end && r[k].length == 0)
            continue;
        p->pre = r[k].range.start;
        p->post = r[k].range.start + delta;
        p->removed = r[k].range.end - r[k].range.start;
        p->added = (int64_t)r[k].length;
        memcpy(p->out, m.text + p->pre, (size_t)p->removed);
        memcpy(p->in, r[k].text, r[k].length);
        delta += p->added - p->removed;
        st.count++;
        changes++;
    }
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_OK, "MODEL-APPLY",
          "a valid transaction applies", op);
    if (!changes)
        return;
    applied++;
    model_step(&st, 0, 0);
    if (st.view >= 0) {
        st.after[0] = m.sel[st.view][0];
        st.after[1] = m.sel[st.view][1];
    }
    if (m.current == STEPS)
        return;                                 /* never within a run */
    st.id = m.next_id++;
    m.step[m.current++] = st;
    m.count = m.current;
}

/** @brief A random refused transaction: nothing may change. */
static void
op_refuse(int op)
{
    tiku_edit_replace_t r[2];
    tiku_edit_transaction_t t;
    int64_t a = any_boundary();

    memset(&t, 0, sizeof t);
    t.edits = r;
    t.count = 2;
    r[0].range.start = a;
    r[0].range.end = a;
    r[0].text = "x";
    r[0].length = 1;
    r[1] = r[0];                                /* two inserts at one point */
    check(tiku_editdoc_apply(doc, &t) == TIKU_EDIT_INVALID_RANGE,
          "TXN-003", "refused", op);
}

/** @brief Undo or redo, both. */
static void
op_history(int op, int back)
{
    tiku_edit_status_t st = back ? tiku_editdoc_undo(doc)
                                 : tiku_editdoc_redo(doc);

    if (back ? m.current == 0 : m.current == m.count) {
        check(st == TIKU_EDIT_NOTHING, "HIS-001", "nothing to step", op);
        return;
    }
    check(st == TIKU_EDIT_OK, "HIS-001", "a step", op);
    stepped++;
    if (back)
        model_step(&m.step[--m.current], 1, 1);
    else
        model_step(&m.step[m.current++], 0, 1);
}

/** @brief One run: a fresh document and model, @p ops operations. */
static void
run(int ops)
{
    int op, k;

    memset(&m, 0, sizeof m);
    m.base = m.saved = 1;
    m.next_id = 2;
    check(tiku_editdoc_create(NULL, &doc) == TIKU_EDIT_OK, "DOC-001",
          "create", 0);
    for (k = 0; k < VIEWS; k++)
        check(tiku_editdoc_view_attach(doc, &view[k]) == 0, "MRK-008",
              "view", 0);
    for (op = 1; op <= ops; op++) {
        unsigned what = rnd(100);

        if (what < 45) {
            op_apply(op);
        } else if (what < 60) {
            op_history(op, 1);
        } else if (what < 70) {
            op_history(op, 0);
        } else if (what < 74) {
            tiku_editdoc_saved(doc, tiku_editdoc_state(doc));
            m.saved = m.current ? m.step[m.current - 1].id : m.base;
        } else if (what < 82) {
            k = (int)rnd(VIEWS);
            m.sel[k][0] = any_caret();
            m.sel[k][1] = any_caret();
            check(tiku_editdoc_view_select(doc, view[k], m.sel[k][0],
                                           m.sel[k][1]) == 0, "MRK-008",
                  "select", op);
        } else if (what < 90) {
            k = (int)rnd(ANCHORS);
            if (m.alive[k]) {
                tiku_editdoc_anchor_destroy(doc, handle[k]);
                m.alive[k] = 0;
            } else {
                m.anchor[k] = any_boundary();
                m.right[k] = (int)rnd(2);
                m.alive[k] = tiku_editdoc_anchor_create(doc, m.anchor[k],
                                 m.right[k] ? TIKU_EDIT_RIGHT : TIKU_EDIT_LEFT,
                                 &handle[k]) == TIKU_EDIT_OK;
                check(m.alive[k], "MRK-005", "anchor", op);
            }
        } else if (what < 99) {
            op_refuse(op);
        } else {
            char text[64];
            int64_t n = random_text(text, 12);

            check(tiku_editdoc_load(doc, text, (size_t)n) == 0, "DOC-012",
                  "load", op);
            model_replace(0, m.n, text, n);
            model_snap();
            m.count = m.current = 0;
            m.base = m.saved = m.next_id++;
        }
        compare(op);
        if (m.n > longest)
            longest = m.n;
    }
    tiku_editdoc_release(doc);
}

int
main(int argc, char **argv)
{
    int seeds = argc > 1 ? atoi(argv[1]) : 16, s;

    for (s = 1; s <= seeds; s++) {
        seed = 0x9e3779b97f4a7c15ull * (unsigned long long)s + 1;
        run(3000);
    }
    printf("%s (%d seeds x 3000 operations: %ld edits, %ld undo/redo, "
           "texts up to %ld bytes)\n",
           fails ? "FAIL: editor model" :
           "PASS: editor model -- text, lines, history, saved state, anchors "
           "and selections against a plain model", seeds, applied, stepped,
           longest);
    return fails != 0;
}
