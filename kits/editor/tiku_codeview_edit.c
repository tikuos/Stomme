/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_edit.c - changing the document from a view: typing,
 * deleting, text-input commits, undo and redo, and the clipboard the
 * host lends.  Each change is one transaction of the document's, so it
 * applies whole or not at all.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"
#include "tiku_editdoc_private.h"

#include <stdint.h>
#include <string.h>

void
tiku_codeview_set_host(tiku_codeview_t *view, const tiku_codeview_host_t *host)
{
    if (view == NULL)
        return;
    if (host != NULL)
        view->host = *host;
    else
        memset(&view->host, 0, sizeof view->host);
}

tiku_edit_status_t
tiku_codeview_set_newline(tiku_codeview_t *view, const char *newline)
{
    if (view == NULL || newline == NULL)
        return TIKU_EDIT_INVALID;
    if (strcmp(newline, "\n") != 0 && strcmp(newline, "\r\n") != 0 &&
        strcmp(newline, "\r") != 0)
        return TIKU_EDIT_INVALID_TEXT;
    strcpy(view->newline, newline);
    return TIKU_EDIT_OK;
}

tiku_edit_status_t
tiku_codeview_status(const tiku_codeview_t *view)
{
    return view != NULL ? view->status : TIKU_EDIT_INVALID;
}

/** @brief The selection, in order. */
static tiku_edit_status_t
ordered(const tiku_codeview_t *v, tiku_edit_pos_t *lo, tiku_edit_pos_t *hi)
{
    tiku_edit_pos_t anchor, caret;
    tiku_edit_status_t st = tiku_codeview_selection(v, &anchor, &caret);

    if (st != TIKU_EDIT_OK)
        return st;
    *lo = anchor < caret ? anchor : caret;
    *hi = anchor < caret ? caret : anchor;
    return TIKU_EDIT_OK;
}

tiku_edit_status_t
tiku_cv_replace(tiku_codeview_t *v, tiku_edit_pos_t lo, tiku_edit_pos_t hi,
                const char *text, size_t length, int typing)
{
    tiku_edit_replace_t r;
    tiku_edit_transaction_t t;
    tiku_edit_pos_t caret;
    int before, after;
    tiku_edit_status_t st;

    if ((uint64_t)length > (uint64_t)INT64_MAX - (uint64_t)lo)
        return TIKU_EDIT_LIMIT;
    caret = lo + (tiku_edit_pos_t)length;
    before = length > 0 ? (unsigned char)text[length - 1]
                        : tiku_editdoc_byte_at(v->doc, lo - 1);
    after = tiku_editdoc_byte_at(v->doc, hi);
    if (before == '\r' && after == '\n')
        caret--;
    memset(&r, 0, sizeof r);
    r.range.start = lo;
    r.range.end = hi;
    r.text = length > 0 ? text : NULL;
    r.length = length;
    memset(&t, 0, sizeof t);
    t.edits = &r;
    t.count = 1;
    t.view = v->view;
    t.select = 1;
    t.anchor = caret;
    t.caret = caret;
    t.flags = typing ? TIKU_EDIT_TYPING : 0u;
    st = tiku_editdoc_apply(v->doc, &t);
    if (st == TIKU_EDIT_OK)
        tiku_codeview_reveal(v);
    return st;
}

tiku_edit_status_t
tiku_codeview_insert(tiku_codeview_t *view, const char *text, size_t length,
                     int typing)
{
    tiku_edit_pos_t lo, hi;
    tiku_edit_status_t st;

    if (view == NULL || (text == NULL && length > 0))
        return TIKU_EDIT_INVALID;
    st = ordered(view, &lo, &hi);
    if (st != TIKU_EDIT_OK)
        return st;
    if (tiku_cv_many(view))
        return tiku_cv_insert_each(view, text, length, typing);
    if (lo == hi && length == 0)
        return TIKU_EDIT_OK;
    return tiku_cv_replace(view, lo, hi, text, length, typing);
}

tiku_edit_status_t
tiku_codeview_delete(tiku_codeview_t *view, int forward)
{
    tiku_edit_pos_t lo, hi;
    tiku_edit_status_t st;

    if (view == NULL)
        return TIKU_EDIT_INVALID;
    st = ordered(view, &lo, &hi);
    if (st != TIKU_EDIT_OK)
        return st;
    if (tiku_cv_many(view))
        return tiku_cv_delete_each(view, forward, 0);
    if (lo < hi)
        return tiku_cv_replace(view, lo, hi, NULL, 0, 0);
    if (forward)
        hi = tiku_editdoc_next(view->doc, lo);
    else
        lo = tiku_editdoc_prev(view->doc, hi);
    if (lo == hi)
        return TIKU_EDIT_NOTHING;
    return tiku_cv_replace(view, lo, hi, NULL, 0, 1);
}

tiku_edit_status_t
tiku_codeview_undo(tiku_codeview_t *view)
{
    tiku_edit_status_t st;

    if (view == NULL)
        return TIKU_EDIT_INVALID;
    st = tiku_editdoc_undo(view->doc);
    if (st == TIKU_EDIT_OK)
        tiku_codeview_reveal(view);
    return st;
}

tiku_edit_status_t
tiku_codeview_redo(tiku_codeview_t *view)
{
    tiku_edit_status_t st;

    if (view == NULL)
        return TIKU_EDIT_INVALID;
    st = tiku_editdoc_redo(view->doc);
    if (st == TIKU_EDIT_OK)
        tiku_codeview_reveal(view);
    return st;
}

tiku_edit_status_t
tiku_codeview_copy(tiku_codeview_t *view)
{
    tiku_edit_pos_t lo, hi;
    tiku_edit_range_t range;
    tiku_edit_status_t st;
    size_t n, got = 0;
    char *text;
    int taken;

    if (view == NULL || view->host.clip_set == NULL)
        return TIKU_EDIT_INVALID;
    st = ordered(view, &lo, &hi);
    if (st != TIKU_EDIT_OK)
        return st;
    if (tiku_cv_many(view))
        return tiku_cv_copy_each(view);
    if (lo == hi)
        return TIKU_EDIT_NOTHING;
    if ((uint64_t)(hi - lo) >= (uint64_t)SIZE_MAX)
        return TIKU_EDIT_LIMIT;
    n = (size_t)(hi - lo);
    text = tiku_ed_alloc(n + 1);
    if (text == NULL)
        return TIKU_EDIT_NO_MEMORY;
    range.start = lo;
    range.end = hi;
    st = tiku_editdoc_read(view->doc, range, text, n + 1, &got);
    taken = st == TIKU_EDIT_OK &&
            view->host.clip_set(view->host.ctx, text, got) == 0;
    tiku_ed_free(text);
    if (st != TIKU_EDIT_OK)
        return st;
    return taken ? TIKU_EDIT_OK : TIKU_EDIT_LIMIT;
}

tiku_edit_status_t
tiku_codeview_cut(tiku_codeview_t *view)
{
    tiku_edit_status_t st;

    if (view == NULL)
        return TIKU_EDIT_INVALID;
    if (tiku_editdoc_read_only(view->doc))
        return TIKU_EDIT_READ_ONLY;
    st = tiku_codeview_copy(view);
    if (st != TIKU_EDIT_OK)
        return st;
    /* Every selection goes; a caret among them stays where it is. */
    if (tiku_cv_many(view))
        return tiku_cv_insert_each(view, NULL, 0, 0);
    return tiku_codeview_delete(view, 0);
}

tiku_edit_status_t
tiku_codeview_paste(tiku_codeview_t *view)
{
    const char *text;
    size_t n = 0;

    if (view == NULL || view->host.clip_text == NULL)
        return TIKU_EDIT_INVALID;
    text = view->host.clip_text(view->host.ctx, &n);
    if (text == NULL || n == 0)
        return TIKU_EDIT_NOTHING;
    if (tiku_cv_many(view))
        return tiku_cv_paste_each(view, text, n);
    return tiku_codeview_insert(view, text, n, 0);
}

/** @brief The text a key press types, or NULL: its own text, or a
 *         printable ASCII key code when it brought none. */
static const char *
typed(const tiku_event_t *e, char *one)
{
    unsigned char lead = (unsigned char)e->text[0];

    if (lead != 0) {
        if (lead < 0x20u || lead == 0x7Fu)
            return NULL;
        return e->text;
    }
    if (e->key >= 0x20u && e->key < 0x7Fu) {
        one[0] = (char)e->key;
        one[1] = '\0';
        return one;
    }
    return NULL;
}

/** @brief A command's status as an event leaves it: TIKU_EDIT_NOTHING
 *         becomes TIKU_EDIT_OK. */
static tiku_edit_status_t
quiet(tiku_edit_status_t st)
{
    return st == TIKU_EDIT_NOTHING ? TIKU_EDIT_OK : st;
}

/** @brief Run the Command chord @p e, its result kept as the view's
 *         status, when it is one the view takes; 0 for any other, and for
 *         a line, bracket, fold or word chord with Option held. */
static int
command_key(tiku_codeview_t *v, const tiku_event_t *e)
{
    unsigned k = e->key;
    int shift = (e->modifiers & TIKU_MOD_SHIFT) != 0;

    if (k >= 'A' && k <= 'Z')
        k += 'a' - 'A';
    if ((e->modifiers & TIKU_MOD_OPTION) &&
        (k == '/' || k == 'd' || k == 'k' || k == 'm' || k == TIKU_KEY_UP ||
         k == TIKU_KEY_DOWN || k == TIKU_KEY_BACKSPACE ||
         k == TIKU_KEY_DELETE || k == '[' || k == ']' || k == '{' ||
         k == '}'))
        return 0;
    switch (k) {
    case '[':
    case ']':
    case '{':
    case '}':
        if (!shift && k != '{' && k != '}')
            return 0;
        v->status = quiet(tiku_codeview_fold_at(v, k == ']' || k == '}'));
        return 1;
    case '/':
        v->status = quiet(tiku_codeview_toggle_comment(v));
        return 1;
    case 'd':
        if (shift)
            return 0;
        v->status = tiku_codeview_duplicate_lines(v);
        return 1;
    case 'k':
        if (!shift)
            return 0;
        v->status = quiet(tiku_codeview_delete_lines(v));
        return 1;
    case 'm':
        v->status = quiet(tiku_codeview_goto_bracket(v, shift));
        return 1;
    case TIKU_KEY_UP:
    case TIKU_KEY_DOWN:
        if (!shift)
            return 0;
        v->status = quiet(tiku_codeview_move_lines(v, k == TIKU_KEY_DOWN));
        return 1;
    case TIKU_KEY_BACKSPACE:
    case TIKU_KEY_DELETE:
        if (shift)
            return 0;
        v->status = quiet(tiku_codeview_delete_word(v,
                                                    k == TIKU_KEY_DELETE));
        return 1;
    case 'c':
        v->status = tiku_codeview_copy(v);
        return 1;
    case 'x':
        v->status = tiku_codeview_cut(v);
        return 1;
    case 'v':
        v->status = tiku_codeview_paste(v);
        return 1;
    case 'a':
        v->status = tiku_codeview_select_all(v);
        return 1;
    case 'z':
        v->status = shift ? tiku_codeview_redo(v) : tiku_codeview_undo(v);
        return 1;
    case 'y':
        v->status = tiku_codeview_redo(v);
        return 1;
    default:
        return 0;
    }
}

/** @brief Tab: the lines one selection spans indented a level, or else
 *         a level typed over each selection, as a key types. */
static tiku_edit_status_t
tab(tiku_codeview_t *v)
{
    tiku_edit_pos_t anchor, caret, lo, hi;

    if (tiku_codeview_selection(v, &anchor, &caret) != TIKU_EDIT_OK)
        return TIKU_EDIT_INVALID;
    lo = anchor < caret ? anchor : caret;
    hi = anchor < caret ? caret : anchor;
    if (tiku_editdoc_line_of(v->doc, lo) < tiku_editdoc_line_of(v->doc, hi) &&
        !tiku_cv_many(v))
        return tiku_codeview_indent(v, 0);
    if (v->indent_spaces > 0)
        return tiku_codeview_insert(v, "                                ",
                                    (size_t)v->indent_spaces, 1);
    return tiku_codeview_insert(v, "\t", 1, 1);
}

int
tiku_cv_edit_key(tiku_codeview_t *v, const tiku_event_t *e)
{
    char one[2];
    const char *text;
    int plain = (e->modifiers & (TIKU_MOD_CMD | TIKU_MOD_OPTION)) == 0;

    if (e->modifiers & TIKU_MOD_CMD)
        return command_key(v, e);
    switch (e->key) {
    case TIKU_KEY_RETURN:
        if (!plain || (e->modifiers & TIKU_MOD_SHIFT))
            return 0;
        v->status = tiku_codeview_newline(v);
        return 1;
    case TIKU_KEY_TAB:
        if (!plain)
            return 0;
        if (e->modifiers & TIKU_MOD_SHIFT) {
            v->status = quiet(tiku_codeview_indent(v, 1));
            return 1;
        }
        v->status = tab(v);
        return 1;
    case TIKU_KEY_BACKSPACE:
    case TIKU_KEY_DELETE:
        if (!plain)
            return 0;
        v->status = tiku_codeview_delete(v, e->key == TIKU_KEY_DELETE);
        if (v->status == TIKU_EDIT_NOTHING)
            v->status = TIKU_EDIT_OK;
        return 1;
    default:
        break;
    }
    if (e->key < 0x20u && e->text[0] == '\0')
        return 0;
    text = typed(e, one);
    if (text == NULL)
        return 0;
    if (tiku_cv_type_closer(v, text))
        return 1;
    v->status = tiku_codeview_insert(v, text, strnlen(text, sizeof e->text),
                                     1);
    return 1;
}

int
tiku_cv_edit_commit(tiku_codeview_t *v, int commit)
{
    if (commit == TIKU_TEXT_COMPLETE && v->list_shown && v->list_inserts &&
        v->input.length == 1 && v->input.text[0] != '\0' &&
        strchr(v->completion_accept, v->input.text[0]) != NULL)
        v->status = tiku_codeview_list_insert(v);
    else if (commit == TIKU_TEXT_COMPLETE)
        v->status = tiku_codeview_insert(v, v->input.text,
                                         (size_t)v->input.length, 0);
    else if (commit == TIKU_TEXT_REJECTED)
        v->status = TIKU_EDIT_INVALID_TEXT;
    return 1;
}
