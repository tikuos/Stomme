/*
 * TikuOS applications -- test_editor_baseline.c: Editor on the editor
 * baseline fixtures.  Each fixture is opened through Editor's own file
 * loader, which now opens every one of them, and runs the shared edit
 * script through the document it loaded into, checked byte for byte
 * against the model.  The timings are printed, never asserted.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0
 */

#include "../../apps/editor/tiku_editor_private.h"

#include "../fixtures/editor_fixtures.inc"

static int fails;

/** @brief Count a failure and say which fixture and check it was. */
static void
check(int cond, int fx, const char *what)
{
    if (!cond) {
        printf("  [FAIL] %-16s %s\n", fx_info[fx].name, what);
        fails++;
    }
}

/** @brief An Editor state with a document and view and nothing else:
 *  what the file loader needs. */
static edit_state_t *
bare_state(void)
{
    tiku_editdoc_limits_t limits = {INT_MAX - 1u, TIKU_EDITDOC_HISTORY_DEFAULT};
    edit_state_t *st = calloc(1, sizeof *st);

    if (st == NULL || tiku_editdoc_create(&limits, &st->doc) != TIKU_EDIT_OK ||
        tiku_codeview_create(st->doc, &st->view) != TIKU_EDIT_OK)
        exit(1);
    snprintf(st->newline, sizeof st->newline, "\n");
    return st;
}

/** @brief Whether the document holds exactly @p m's bytes. */
static int
holds(edit_state_t *st, const fx_buf_t *m)
{
    tiku_edit_range_t all = {0, tiku_editdoc_length(st->doc)};
    fx_buf_t got;
    size_t n = 0;
    int same;

    memset(&got, 0, sizeof got);
    fx_room(&got, (size_t)all.end);
    same = tiku_editdoc_read(st->doc, all, got.p, (size_t)all.end + 1, &n) ==
           TIKU_EDIT_OK && n == m->n && !memcmp(got.p, m->p, m->n);
    fx_free(&got);
    return same;
}

/** @brief Apply one scripted edit as Editor does: the range selected in
 *  its view, the text put in its place. */
static int
apply(edit_state_t *st, const fx_op_t *op)
{
    tiku_edit_pos_t a = tiku_editdoc_line_start(st->doc, op->y0) + op->x0;
    tiku_edit_pos_t b = tiku_editdoc_line_start(st->doc, op->y1) + op->x1;

    return tiku_codeview_select(st->view, a, b) == TIKU_EDIT_OK &&
           tiku_codeview_insert(st->view, op->text, op->len, 0) == TIKU_EDIT_OK;
}

/** @brief Run the script on @p st's document, each edit timed, the model
 *         kept in step, the text before the replacement kept in
 *         @p before. */
static void
script(int fx, edit_state_t *st, fx_buf_t *m, fx_buf_t *before,
       fx_result_t *r)
{
    fx_op_t op;
    int i;

    for (i = 0;; i++) {
        uint64_t t0;

        if (i == FX_TYPE_OPS + FX_NEWLINE_OPS)
            fx_put(before, m->p, m->n);
        if (!fx_step(fx, m, i, &op))
            break;
        t0 = fx_now();
        check(apply(st, &op), fx, "a scripted edit");
        t0 = fx_now() - t0;
        if (op.kind == FX_TYPE)
            fx_time(&r->type, t0);
        else if (op.kind == FX_NEWLINE)
            fx_time(&r->newline, t0);
        else
            r->replace = t0;
    }
}

/** @brief Open fixture @p fx from @p dir in Editor, then run the script,
 *         undo and redo the replacement, and report. */
static void
run(int fx, const char *dir)
{
    fx_buf_t src, m, before;
    fx_result_t r;
    char path[512], *copy;
    edit_state_t *st = bare_state();
    tiku_edit_range_t all;
    uint64_t t0;
    size_t heap0, n;

    fx_make(fx, &src);
    check(fx_write(dir, fx, &src, path, sizeof path), fx, "written");
    memset(&r, 0, sizeof r);
    memset(&before, 0, sizeof before);
    heap0 = fx_heap();
    t0 = fx_now();
    r.loaded = tiku_editor_read_document(st, path);
    r.load = fx_now() - t0;
    r.heap = fx_heap() - heap0;
    check(r.loaded, fx, "opened");
    if (!r.loaded) {
        snprintf(r.refusal, sizeof r.refusal, "\"%s\"", st->note);
    } else {
        all = (tiku_edit_range_t){0, tiku_editdoc_length(st->doc)};
        t0 = fx_now();
        copy = malloc((size_t)all.end + 1);
        check(copy && tiku_editdoc_read(st->doc, all, copy, (size_t)all.end + 1,
                                        &n) == TIKU_EDIT_OK, fx, "copied");
        r.copy = fx_now() - t0;
        free(copy);
        memset(&m, 0, sizeof m);
        fx_put(&m, src.p, src.n);
        fx_make_block(src.n);
        script(fx, st, &m, &before, &r);
        check(holds(st, &m), fx, "the script's text");
        t0 = fx_now();
        check(tiku_codeview_undo(st->view) == TIKU_EDIT_OK, fx, "undo");
        r.undo = fx_now() - t0;
        check(holds(st, &before), fx, "undo restores the text");
        t0 = fx_now();
        check(tiku_codeview_redo(st->view) == TIKU_EDIT_OK, fx, "redo");
        r.redo = fx_now() - t0;
        check(holds(st, &m), fx, "redo repeats the replacement");
        fx_free(&m);
    }
    fx_report_row(fx, &src, &r);
    tiku_codeview_destroy(st->view);
    tiku_editdoc_release(st->doc);
    free(st);
    fx_free(&before);
    fx_free(&src);
}

int
main(void)
{
    char dir[512];
    int fx, kept = fx_dir(dir, sizeof dir);

    fx_report_machine("Editor");
    for (fx = 0; fx < FX_COUNT; fx++)
        if (fx_enabled(fx))
            run(fx, dir);
    fx_free(&fx_block);
    fx_dir_done(dir, kept);
    printf("%s\n", fails ? "FAIL: editor baseline" :
           "PASS: editor baseline -- Editor opens every fixture, and its "
           "document matches the model");
    return fails != 0;
}
