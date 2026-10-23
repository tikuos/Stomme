/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_editdoc.c - the document's life, what it holds, and loading it:
 * a new text is built beside the old and swapped in whole.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_editdoc_private.h"

#include <stdint.h>
#include <string.h>

tiku_edit_status_t
tiku_editdoc_create(const tiku_editdoc_limits_t *limits, tiku_editdoc_t **doc)
{
    tiku_editdoc_t *d;
    size_t history = TIKU_EDITDOC_HISTORY_DEFAULT;

    if (!doc)
        return TIKU_EDIT_INVALID;
    *doc = NULL;
    d = tiku_ed_alloc(sizeof *d);
    if (!d)
        return TIKU_EDIT_NO_MEMORY;
    memset(d, 0, sizeof *d);
    if (!tiku_ed_text_init(&d->text)) {
        tiku_ed_text_free(&d->text);
        tiku_ed_free(d);
        return TIKU_EDIT_NO_MEMORY;
    }
    d->refs = 1;
    d->generation = 1;
    d->exec_line = -1;
    d->max_bytes = TIKU_EDITDOC_MAX_BYTES_DEFAULT;
    if (limits && limits->max_bytes)
        d->max_bytes = limits->max_bytes;
    if (d->max_bytes > (uint64_t)PTRDIFF_MAX)
        d->max_bytes = (uint64_t)PTRDIFF_MAX;
    if (limits && limits->history_bytes)
        history = limits->history_bytes;
    tiku_ed_history_init(&d->history, history);
    *doc = d;
    return TIKU_EDIT_OK;
}

void
tiku_editdoc_retain(tiku_editdoc_t *doc)
{
    if (doc)
        doc->refs++;
}

void
tiku_editdoc_release(tiku_editdoc_t *doc)
{
    if (!doc || doc->refs == 0 || --doc->refs > 0)
        return;
    tiku_ed_text_free(&doc->text);
    tiku_ed_history_free(&doc->history);
    tiku_ed_marks_free(doc);
    tiku_ed_markers_free(doc);
    tiku_ed_notes_free(doc);
    tiku_ed_free(doc);
}

tiku_edit_status_t
tiku_editdoc_load(tiku_editdoc_t *doc, const char *text, size_t length)
{
    tiku_ed_text_t fresh, old;
    tiku_edit_change_t change;
    int64_t old_length, old_lines;

    if (!doc || (!text && length))
        return TIKU_EDIT_INVALID;
    if (doc->busy)
        return TIKU_EDIT_BUSY;
    if (doc->read_only)
        return TIKU_EDIT_READ_ONLY;
    if ((uint64_t)length > doc->max_bytes)
        return TIKU_EDIT_LIMIT;
    if (!tiku_ed_utf8_valid(text, length))
        return TIKU_EDIT_INVALID_TEXT;
    /* The new text is built whole before anything of the old changes. */
    if (!tiku_ed_text_init(&fresh) ||
        !tiku_ed_text_reserve(&fresh, (int64_t)length,
                              tiku_ed_text_line_cost(text,
                                                     (int64_t)length))) {
        tiku_ed_text_free(&fresh);
        return TIKU_EDIT_NO_MEMORY;
    }
    tiku_ed_text_insert(&fresh, 0, text, (int64_t)length);

    tiku_ed_commit_begin();
    old_length = (int64_t)doc->text.bytes.length;
    old_lines = tiku_ed_lines_count(&doc->text.lines);
    old = doc->text;
    doc->text = fresh;
    tiku_ed_markers_drop(doc);
    tiku_ed_notes_drop(doc);
    tiku_ed_marks_map(doc, 0, old_length, (int64_t)length);
    tiku_ed_marks_snap(doc);
    tiku_ed_history_clear(&doc->history);
    doc->generation++;
    tiku_ed_commit_end();
    tiku_ed_text_free(&old);

    memset(&doc->loaded, 0, sizeof doc->loaded);
    doc->loaded.removed = old_length;
    doc->loaded.added = (int64_t)length;
    memset(&change, 0, sizeof change);
    change.cause = TIKU_EDIT_LOADED;
    change.generation_before = doc->generation - 1;
    change.generation_after = doc->generation;
    change.lines_before = old_lines;
    change.lines_after = tiku_ed_lines_count(&doc->text.lines);
    change.count = 1;
    change.detail = &doc->loaded;
    tiku_ed_dispatch(doc, &change);
    return TIKU_EDIT_OK;
}

tiku_edit_pos_t
tiku_editdoc_length(const tiku_editdoc_t *doc)
{
    return doc ? (tiku_edit_pos_t)doc->text.bytes.length : 0;
}

int
tiku_editdoc_byte_at(const tiku_editdoc_t *doc, tiku_edit_pos_t pos)
{
    if (!doc || pos < 0 || pos >= (tiku_edit_pos_t)doc->text.bytes.length)
        return -1;
    return tiku_ed_bytes_at(&doc->text.bytes, (ptrdiff_t)pos);
}

/** @brief Whether @p r lies inside the text, the right way round. */
static int
range_ok(const tiku_editdoc_t *doc, tiku_edit_range_t r)
{
    return r.start >= 0 && r.start <= r.end &&
           r.end <= (tiku_edit_pos_t)doc->text.bytes.length;
}

tiku_edit_status_t
tiku_editdoc_read(const tiku_editdoc_t *doc, tiku_edit_range_t range,
                  char *out, size_t cap, size_t *length)
{
    size_t n;

    if (!doc || !length)
        return TIKU_EDIT_INVALID;
    if (!range_ok(doc, range))
        return TIKU_EDIT_INVALID_RANGE;
    n = (size_t)(range.end - range.start);
    *length = n;
    if (!out || cap <= n)
        return TIKU_EDIT_SHORT;
    tiku_ed_bytes_copy(&doc->text.bytes, out, (ptrdiff_t)range.start,
                       (ptrdiff_t)n);
    out[n] = 0;
    return TIKU_EDIT_OK;
}

tiku_edit_status_t
tiku_editdoc_each(tiku_editdoc_t *doc, tiku_edit_range_t range,
                  tiku_edit_bytes_fn fn, void *ctx)
{
    if (!doc || !fn)
        return TIKU_EDIT_INVALID;
    if (!range_ok(doc, range))
        return TIKU_EDIT_INVALID_RANGE;
    /* The bytes handed out point into the buffer, so the document refuses
     * changes until the reader returns. */
    doc->busy++;
    tiku_ed_bytes_each(&doc->text.bytes, (ptrdiff_t)range.start,
                       (ptrdiff_t)(range.end - range.start), fn, ctx);
    doc->busy--;
    return TIKU_EDIT_OK;
}

int64_t
tiku_editdoc_lines(const tiku_editdoc_t *doc)
{
    return doc ? tiku_ed_lines_count(&doc->text.lines) : 0;
}

tiku_edit_pos_t
tiku_editdoc_line_start(const tiku_editdoc_t *doc, int64_t line)
{
    if (!doc || line < 0 || line > tiku_ed_lines_count(&doc->text.lines))
        return -1;
    return tiku_ed_lines_start(&doc->text.lines, line);
}

tiku_edit_pos_t
tiku_editdoc_line_end(const tiku_editdoc_t *doc, int64_t line)
{
    return doc ? tiku_ed_text_line_end(&doc->text, line) : -1;
}

int64_t
tiku_editdoc_line_of(const tiku_editdoc_t *doc, tiku_edit_pos_t pos)
{
    if (!doc || pos < 0 || pos > (tiku_edit_pos_t)doc->text.bytes.length)
        return -1;
    return tiku_ed_lines_find(&doc->text.lines, pos);
}

/** @brief The byte at @p pos as an unsigned value, 0 outside. */
static int
at(const tiku_editdoc_t *doc, tiku_edit_pos_t pos)
{
    return tiku_ed_bytes_at(&doc->text.bytes, (ptrdiff_t)pos);
}

int
tiku_editdoc_is_boundary(const tiku_editdoc_t *doc, tiku_edit_pos_t pos)
{
    tiku_edit_pos_t length;

    if (!doc)
        return 0;
    length = (tiku_edit_pos_t)doc->text.bytes.length;
    if (pos < 0 || pos > length)
        return 0;
    return pos == length || (at(doc, pos) & 0xc0) != 0x80;
}

int
tiku_editdoc_is_caret(const tiku_editdoc_t *doc, tiku_edit_pos_t pos)
{
    return tiku_editdoc_is_boundary(doc, pos) &&
           !(pos > 0 && at(doc, pos - 1) == '\r' && at(doc, pos) == '\n');
}

tiku_edit_pos_t
tiku_editdoc_next(const tiku_editdoc_t *doc, tiku_edit_pos_t pos)
{
    tiku_edit_pos_t length;

    if (!doc)
        return 0;
    length = (tiku_edit_pos_t)doc->text.bytes.length;
    if (pos < 0)
        return 0;
    if (pos >= length)
        return length;
    if (at(doc, pos) == '\r' && at(doc, pos + 1) == '\n')
        return pos + 2;
    if (at(doc, pos) == '\n' && pos > 0 && at(doc, pos - 1) == '\r')
        return pos + 1;
    for (pos++; pos < length && (at(doc, pos) & 0xc0) == 0x80; pos++)
        ;
    return pos;
}

tiku_edit_pos_t
tiku_editdoc_prev(const tiku_editdoc_t *doc, tiku_edit_pos_t pos)
{
    tiku_edit_pos_t length;

    if (!doc || pos <= 0)
        return 0;
    length = (tiku_edit_pos_t)doc->text.bytes.length;
    if (pos > length)
        return length;
    if (at(doc, pos - 1) == '\n' && pos >= 2 && at(doc, pos - 2) == '\r')
        return pos - 2;
    if (at(doc, pos - 1) == '\r' && at(doc, pos) == '\n')
        return pos - 1;
    for (pos--; pos > 0 && (at(doc, pos) & 0xc0) == 0x80; pos--)
        ;
    return pos;
}

uint64_t
tiku_editdoc_generation(const tiku_editdoc_t *doc)
{
    return doc ? doc->generation : 0;
}

uint64_t
tiku_editdoc_state(const tiku_editdoc_t *doc)
{
    return doc ? tiku_ed_history_state(&doc->history) : 0;
}

void
tiku_editdoc_saved(tiku_editdoc_t *doc, uint64_t state)
{
    if (doc)
        doc->history.saved_state = state;
}

int
tiku_editdoc_modified(const tiku_editdoc_t *doc)
{
    return doc && tiku_ed_history_state(&doc->history) !=
                  doc->history.saved_state;
}

void
tiku_editdoc_set_read_only(tiku_editdoc_t *doc, int read_only)
{
    if (doc)
        doc->read_only = read_only != 0;
}

int
tiku_editdoc_read_only(const tiku_editdoc_t *doc)
{
    return doc && doc->read_only;
}

tiku_edit_status_t
tiku_editdoc_snapshot(const tiku_editdoc_t *doc,
                      tiku_edit_snapshot_t *snapshot)
{
    size_t n;
    char *text;

    if (!doc || !snapshot)
        return TIKU_EDIT_INVALID;
    memset(snapshot, 0, sizeof *snapshot);
    n = (size_t)doc->text.bytes.length;
    text = tiku_ed_alloc(n + 1);
    if (!text)
        return TIKU_EDIT_NO_MEMORY;
    tiku_ed_bytes_copy(&doc->text.bytes, text, 0, (ptrdiff_t)n);
    text[n] = 0;
    snapshot->text = text;
    snapshot->length = n;
    snapshot->generation = doc->generation;
    snapshot->state = tiku_ed_history_state(&doc->history);
    return TIKU_EDIT_OK;
}

void
tiku_edit_snapshot_free(tiku_edit_snapshot_t *snapshot)
{
    if (snapshot) {
        tiku_ed_free(snapshot->text);
        memset(snapshot, 0, sizeof *snapshot);
    }
}
