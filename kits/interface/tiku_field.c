/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_field.c - the editing state behind a single-line text field.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_clipboard.h"
#include "tiku_text_input.h"
#include "tiku_field.h"

#include <stdio.h>
#include <string.h>

/** @brief Where the code point before the caret starts. */
static int field_previous(const tiku_field_t *e)
{
    int at = e->caret > 0 ? e->caret - 1 : 0;
    while (at > 0 && ((unsigned char)e->text[at] & 0xc0) == 0x80) at--;
    return at;
}
/** @brief Where the code point after the caret ends. */
static int field_next(const tiku_field_t *e)
{
    int at = e->caret < e->len ? e->caret + 1 : e->len;
    while (at < e->len && ((unsigned char)e->text[at] & 0xc0) == 0x80) at++;
    return at;
}

tiku_field_result_t tiku_field_commit(tiku_field_t *e, const char *text)
{
    tiku_text_input_t checked;
    if (!e || !e->active || !e->editable || !text) return TIKU_FIELD_IGNORED;
    size_t n = strlen(text);
    int lo = e->sel_a < e->sel_b ? e->sel_a : e->sel_b;
    int hi = e->sel_a > e->sel_b ? e->sel_a : e->sel_b;
    if (!n) return TIKU_FIELD_HANDLED;
    if (lo < 0 || hi > e->len || e->len < 0 || e->len >= TIKU_FIELD_MAX ||
        (lo < e->len && ((unsigned char)e->text[lo] & 0xc0) == 0x80) ||
        (hi < e->len && ((unsigned char)e->text[hi] & 0xc0) == 0x80)) return TIKU_FIELD_REJECTED;
    if (!tiku_text_input_set(&checked, text, n) ||
        n + (size_t)(e->len - hi + lo) >= sizeof e->text) return TIKU_FIELD_REJECTED;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)text[i] < 32 || text[i] == 127 ||
            (e->deny && strchr(e->deny, text[i]))) return TIKU_FIELD_REJECTED;
    memmove(e->text + lo + n, e->text + hi, (size_t)(e->len - hi) + 1);
    memcpy(e->text + lo, checked.text, n);
    e->len += (int)n - (hi - lo);
    e->caret = e->sel_a = e->sel_b = lo + (int)n;
    return TIKU_FIELD_HANDLED;
}

/** @brief Put the selection on the desk's text clipboard; with nothing
 *         selected the clipboard keeps what it holds. */
static void
copy_selection(tiku_field_t *e)
{
    int from, to;

    if (tiku_field_selection(e, &from, &to)) {
        char taken[TIKU_FIELD_MAX];
        size_t n = (size_t)(to - from);

        if (n >= sizeof taken) {
            n = sizeof taken - 1u;
        }
        memcpy(taken, e->text + from, n);
        taken[n] = '\0';
        tiku_clipboard_set(taken);
    }
}

void
tiku_field_start(tiku_field_t *e, const char *text)
{
    char kept[sizeof e->text];

    if (e == NULL) {
        return;
    }
    /* Copied aside first: text may be this field's own text. */
    snprintf(kept, sizeof kept, "%s", (text != NULL) ? text : "");
    memset(e, 0, sizeof *e);
    e->deny = NULL;
    memcpy(e->text, kept, sizeof kept);
    e->len = (int)strlen(e->text);
    e->caret = e->len;
    e->sel_a = 0;
    e->sel_b = e->len;
    e->active = 1;
    e->editable = 1;
}

void
tiku_field_set_editable(tiku_field_t *e, int editable)
{
    if (e != NULL) {
        e->editable = editable ? 1 : 0;
    }
}

void
tiku_field_deny(tiku_field_t *e, const char *deny)
{
    if (e != NULL) {
        e->deny = deny;
    }
}

void
tiku_field_metrics(const tiku_field_t *e, int min_w, int max_w,
                      int (*measure_prefix)(const char *, int, void *),
                      void *ctx, int *out_w, int *out_scroll)
{
    int text_w, caret_px, want, w, scroll = 0;

    if (e == NULL || measure_prefix == NULL) {
        if (out_w != NULL) { *out_w = min_w; }
        if (out_scroll != NULL) { *out_scroll = 0; }
        return;
    }
    text_w = measure_prefix(e->text, e->len, ctx);
    caret_px = measure_prefix(e->text, e->caret, ctx);
    want = text_w + 12;             /* caret room and the sunken frame */
    w = want;
    if (w < min_w) { w = min_w; }
    if (w > max_w) { w = max_w; }
    if (want > w) {
        /* The box is full: slide so that the caret stays visible, but
         * not past the text's end, which would leave blank space and hide
         * text. */
        if (caret_px + 8 > w - 8) {
            scroll = caret_px + 8 - (w - 8);
        }
        if (scroll > want - w) {
            scroll = want - w;
        }
        if (scroll < 0) {
            scroll = 0;
        }
    }
    if (out_w != NULL) { *out_w = w; }
    if (out_scroll != NULL) { *out_scroll = scroll; }
}

void
tiku_field_stop(tiku_field_t *e)
{
    if (e != NULL) {
        e->active = 0;
    }
}

int
tiku_field_selection(const tiku_field_t *e, int *from, int *to)
{
    int lo, hi;

    if (e == NULL || e->sel_a == e->sel_b) {
        return 0;
    }
    lo = (e->sel_a < e->sel_b) ? e->sel_a : e->sel_b;
    hi = (e->sel_a < e->sel_b) ? e->sel_b : e->sel_a;
    if (from != NULL) { *from = lo; }
    if (to != NULL)   { *to = hi; }
    return 1;
}

/** @brief Remove the selected range, leaving the caret where it
 *         began. */
static void
delete_selection(tiku_field_t *e)
{
    int lo, hi;

    if (!tiku_field_selection(e, &lo, &hi)) {
        return;
    }
    memmove(e->text + lo, e->text + hi, (size_t)(e->len - hi) + 1u);
    e->len -= (hi - lo);
    e->caret = lo;
    e->sel_a = e->sel_b = lo;
}

/** @brief Move the caret, extending or dropping the selection. */
static void
move_caret(tiku_field_t *e, int to, int shift)
{
    if (to < 0)       { to = 0; }
    if (to > e->len)  { to = e->len; }
    if (shift) {
        /* The anchor stays where the selection began, so shift-left after
         * shift-right shrinks the range. */
        if (e->sel_a == e->sel_b) {
            e->sel_a = e->caret;
        }
        e->sel_b = to;
    } else {
        e->sel_a = e->sel_b = to;
    }
    e->caret = to;
}

tiku_field_result_t
tiku_field_key(tiku_field_t *e, tiku_field_key_t key, int shift,
                  char ch)
{
    if (e == NULL || !e->active) {
        return TIKU_FIELD_IGNORED;
    }
    switch (key) {
    case TIKU_FIELD_CHAR:
        if (!e->editable) {
            return TIKU_FIELD_IGNORED;
        }
        if (ch < 0x20 || ch == 0x7f) {
            return TIKU_FIELD_IGNORED;
        }
        if (e->deny != NULL && strchr(e->deny, ch) != NULL) {
            /* Swallowed as it is typed, so the field never shows a
             * character the name cannot hold. */
            return TIKU_FIELD_IGNORED;
        }
        delete_selection(e);
        if (e->len + 1 >= (int)sizeof e->text) {
            return TIKU_FIELD_HANDLED;
        }
        memmove(e->text + e->caret + 1, e->text + e->caret,
                (size_t)(e->len - e->caret) + 1u);
        e->text[e->caret] = ch;
        e->len++;
        e->caret++;
        e->sel_a = e->sel_b = e->caret;
        return TIKU_FIELD_HANDLED;

    case TIKU_FIELD_LEFT:
        move_caret(e, field_previous(e), shift);
        return TIKU_FIELD_HANDLED;
    case TIKU_FIELD_RIGHT:
        move_caret(e, field_next(e), shift);
        return TIKU_FIELD_HANDLED;
    case TIKU_FIELD_HOME:
        move_caret(e, 0, shift);
        return TIKU_FIELD_HANDLED;
    case TIKU_FIELD_END:
        move_caret(e, e->len, shift);
        return TIKU_FIELD_HANDLED;

    case TIKU_FIELD_BACKSPACE:
        if (!e->editable) {
            return TIKU_FIELD_HANDLED;
        }
        if (tiku_field_selection(e, NULL, NULL)) {
            delete_selection(e);
        } else if (e->caret > 0) {
            int at = field_previous(e);
            memmove(e->text + at, e->text + e->caret,
                    (size_t)(e->len - e->caret) + 1u);
            e->len -= e->caret - at;
            e->caret = at;
            e->sel_a = e->sel_b = e->caret;
        }
        return TIKU_FIELD_HANDLED;

    case TIKU_FIELD_DELETE:
        if (!e->editable) {
            return TIKU_FIELD_HANDLED;
        }
        if (tiku_field_selection(e, NULL, NULL)) {
            delete_selection(e);
        } else if (e->caret < e->len) {
            int at = field_next(e);
            memmove(e->text + e->caret, e->text + at,
                    (size_t)(e->len - at) + 1u);
            e->len -= at - e->caret;
        }
        return TIKU_FIELD_HANDLED;

    case TIKU_FIELD_SELECT_ALL:
        e->sel_a = 0;
        e->sel_b = e->len;
        e->caret = e->len;
        return TIKU_FIELD_HANDLED;

    case TIKU_FIELD_COPY:
        copy_selection(e);
        return TIKU_FIELD_HANDLED;

    case TIKU_FIELD_CUT:
        if (!e->editable) {
            return TIKU_FIELD_HANDLED;
        }
        copy_selection(e);
        delete_selection(e);
        return TIKU_FIELD_HANDLED;

    case TIKU_FIELD_PASTE: {
        if (!e->editable || !tiku_clipboard_text()[0]) {
            return TIKU_FIELD_HANDLED;
        }
        return tiku_field_commit(e, tiku_clipboard_text());
    }

    case TIKU_FIELD_COMMIT:
        e->active = 0;
        return TIKU_FIELD_COMMITTED;

    case TIKU_FIELD_CANCEL:
        e->active = 0;
        return TIKU_FIELD_CANCELLED;

    default:
        return TIKU_FIELD_IGNORED;
    }
}
