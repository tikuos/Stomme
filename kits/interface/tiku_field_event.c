/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_field_event.c - pointer, clipboard and undo over the field model.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_field_event.h"
#include "tiku_measure.h"
#include "tiku_clipboard.h"
#include <string.h>

void tiku_field_input_init(tiku_field_input_t *in)
{ memset(in, 0, sizeof *in); }
/** @brief Where the code point at @p at ends, at most @p len. */
static int next(const char *s, int at, int len)
{
    if (at < len) at++;
    while (at < len && ((unsigned char)s[at] & 0xc0) == 0x80) at++;
    return at;
}
/** @brief Where the code point before @p at starts. */
static int prev(const char *s, int at)
{
    if (at > 0) at--;
    while (at > 0 && ((unsigned char)s[at] & 0xc0) == 0x80) at--;
    return at;
}
/** @brief The plain face's width of the first @p at bytes of @p s. */
static int width(const char *s, int at)
{
    char prefix[TIKU_FIELD_MAX];
    memcpy(prefix, s, (size_t)at); prefix[at] = 0;
    return tiku_measure_width(TIKU_MEASURE_PLAIN, prefix);
}
/** @brief Scroll so that the caret is inside @p r. */
static void visible(tiku_field_t *e, tiku_field_input_t *in, tiku_rect_t r)
{
    int x = width(e->text, e->caret), room = r.w - 6;
    if (room < 1) room = 1;
    if (x < in->scroll) in->scroll = x;
    if (x > in->scroll + room) in->scroll = x - room;
    if (in->scroll < 0) in->scroll = 0;
}
/** @brief The caret offset nearest window x @p x in the field at @p r. */
static int caret_at(tiku_field_t *e, tiku_field_input_t *in,
                    tiku_rect_t r, int x)
{
    int old = 0;

    /* The same origin tiku_ui_textfield_scroll draws the string at: a
     * hit test one inset away puts the caret a glyph off the click. */
    x = x - r.x - 3 + in->scroll;
    for (int at = 0; at < e->len;) {
        int end = next(e->text, at, e->len), w = width(e->text, end);
        if (x < old + (w - old) / 2) return at;
        old = w; at = end;
    }
    return e->len;
}
/** @brief Move the caret to @p at, extending the selection with
 *         @p shift and dropping it without. */
static void move(tiku_field_t *e, int at, int shift)
{
    if (shift) {
        if (e->sel_a == e->sel_b) e->sel_a = e->caret;
        e->sel_b = at;
    } else e->sel_a = e->sel_b = at;
    e->caret = at;
}
/** @brief Copy @p e's text, caret and selection into @p s. */
static void snapshot(tiku_field_snapshot_t *s, const tiku_field_t *e)
{
    memcpy(s->text, e->text, sizeof s->text);
    s->caret = e->caret; s->a = e->sel_a; s->b = e->sel_b;
}
/** @brief Push a snapshot of @p e, the oldest falling off a full stack. */
static void push(tiku_field_snapshot_t *stack, int *n, const tiku_field_t *e)
{
    if (*n == TIKU_FIELD_HISTORY) {
        memmove(stack, stack + 1, (TIKU_FIELD_HISTORY - 1) * sizeof *stack);
        (*n)--;
    }
    snapshot(&stack[(*n)++], e);
}
/** @brief Step back one edit, or forward one with @p redo; a read-only
 *         field is left alone. */
static void undo(tiku_field_t *e, tiku_field_input_t *in, int redo)
{
    tiku_field_snapshot_t *from = redo ? in->redo : in->undo;
    int *n = redo ? &in->nredo : &in->nundo;
    if (!e->editable || !*n) return;
    push(redo ? in->undo : in->redo, redo ? &in->nundo : &in->nredo, e);
    tiku_field_snapshot_t *s = &from[--*n];
    memcpy(e->text, s->text, sizeof e->text);
    e->len = (int)strlen(e->text);
    e->caret = s->caret; e->sel_a = s->a; e->sel_b = s->b;
}
/** @brief Whether @p s may go into @p e: no control or denied byte, and
 *         UTF-8 with no malformed or overlong sequence, surrogate, or
 *         code point past U+10FFFF. */
static int valid(const tiku_field_t *e, const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned cp;
        int n, min;
        if (*p < 0x80) {
            if (*p < 32 || *p == 127 || (e->deny && strchr(e->deny, *p))) return 0;
            p++; continue;
        }
        if (*p >= 0xc2 && *p <= 0xdf) { n = 1; cp = *p & 31; min = 0x80; }
        else if (*p >= 0xe0 && *p <= 0xef) { n = 2; cp = *p & 15; min = 0x800; }
        else if (*p >= 0xf0 && *p <= 0xf4) { n = 3; cp = *p & 7; min = 0x10000; }
        else return 0;
        p++;
        while (n--) {
            if ((*p & 0xc0) != 0x80) return 0;
            cp = (cp << 6) | (*p++ & 63);
        }
        if (cp < (unsigned)min || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff)) return 0;
    }
    return 1;
}
/**
 * @brief Replace bytes @p lo to @p hi with @p text, as one undo step.
 * @return 1, also for a read-only field left unchanged; -1 when @p text
 *         is refused or does not fit.
 */
static int replace(tiku_field_t *e, tiku_field_input_t *in,
                     int lo, int hi, const char *text)
{
    size_t n = strlen(text);
    if (!e->editable) return 1;
    if (!valid(e, text) ||
        n + (size_t)(e->len - hi + lo) >= sizeof e->text) return -1;
    if (lo == hi && !n) return 1;
    push(in->undo, &in->nundo, e); in->nredo = 0;
    memmove(e->text + lo + n, e->text + hi, (size_t)(e->len - hi) + 1);
    memcpy(e->text + lo, text, n);
    e->len += (int)n - (hi - lo);
    move(e, lo + (int)n, 0);
    return 1;
}
/**
 * @brief One key press: a Cmd shortcut, a caret move, a deletion or text.
 * @return 1 when handled, 0 when it is not the field's, -1 when its text
 *         was refused.
 */
static int key(tiku_field_t *e, tiku_field_input_t *in, const tiku_input_event_t *ev)
{
    int lo = e->sel_a < e->sel_b ? e->sel_a : e->sel_b;
    int hi = e->sel_a > e->sel_b ? e->sel_a : e->sel_b;
    int shift = !!(ev->modifiers & TIKU_MOD_SHIFT);
    if (ev->modifiers & TIKU_MOD_CMD) {
        switch (ev->key) {
        case 'a': case 'A': tiku_field_key(e, TIKU_FIELD_SELECT_ALL, 0, 0); break;
        case 'c': case 'C': tiku_field_key(e, TIKU_FIELD_COPY, 0, 0); break;
        case 'x': case 'X':
            if (e->editable && lo != hi) {
                tiku_field_key(e, TIKU_FIELD_COPY, 0, 0); replace(e, in, lo, hi, "");
            }
            break;
        case 'v': case 'V':
            return tiku_clipboard_text()[0]
                   ? replace(e, in, lo, hi, tiku_clipboard_text()) : 1;
        case 'z': case 'Z': undo(e, in, shift); break;
        default: return 0;
        }
        return 1;
    }
    if (ev->modifiers & TIKU_MOD_OPTION) return 0;
    switch (ev->key) {
    case TIKU_KEY_LEFT:
        move(e, !shift && lo != hi ? lo : prev(e->text, e->caret), shift); break;
    case TIKU_KEY_RIGHT:
        move(e, !shift && lo != hi ? hi : next(e->text, e->caret, e->len), shift); break;
    case TIKU_KEY_HOME: move(e, 0, shift); break;
    case TIKU_KEY_END: move(e, e->len, shift); break;
    case TIKU_KEY_BACKSPACE:
        replace(e, in, lo != hi ? lo : prev(e->text, lo), hi, ""); break;
    case TIKU_KEY_DELETE:
        replace(e, in, lo, lo != hi ? hi : next(e->text, hi, e->len), ""); break;
    default:
        if (!ev->text[0]) return 0;
        return replace(e, in, lo, hi, ev->text);
    }
    return 1;
}
tiku_field_result_t tiku_field_event(tiku_field_t *e,
    tiku_field_input_t *in, const tiku_input_event_t *ev, tiku_rect_t r, int focused)
{
    if (!e->active) return TIKU_FIELD_IGNORED;
    int commit = tiku_text_input_event(&in->commit, ev);
    if (ev->type == TIKU_EVENT_TEXT_INPUT) {
        if (!focused) { tiku_text_input_reset(&in->commit); return TIKU_FIELD_IGNORED; }
        if (commit == TIKU_TEXT_REJECTED) return TIKU_FIELD_REJECTED;
        if (commit == TIKU_TEXT_COMPLETE) {
            int lo = e->sel_a < e->sel_b ? e->sel_a : e->sel_b;
            int hi = e->sel_a > e->sel_b ? e->sel_a : e->sel_b;
            if (replace(e, in, lo, hi, in->commit.text) < 0) return TIKU_FIELD_REJECTED;
            visible(e, in, r);
        }
        return TIKU_FIELD_HANDLED;
    }
    if (ev->type == TIKU_EVENT_DEACTIVATED) in->dragging = 0;
    if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1 &&
        ev->x >= r.x && ev->x < r.x + r.w &&
        ev->y >= r.y && ev->y < r.y + r.h) {
        move(e, caret_at(e, in, r, ev->x), !!(ev->modifiers & TIKU_MOD_SHIFT));
        in->anchor = e->sel_a; in->dragging = 1;
    } else if (in->dragging && (ev->type == TIKU_EVENT_POINTER_MOVE ||
               (ev->type == TIKU_EVENT_POINTER_UP && ev->button == 1))) {
        e->sel_a = in->anchor;
        e->sel_b = e->caret = caret_at(e, in, r, ev->x);
        if (ev->type == TIKU_EVENT_POINTER_UP) in->dragging = 0;
    } else if (focused && ev->type == TIKU_EVENT_KEY_DOWN) {
        if (ev->key == TIKU_KEY_RETURN) return TIKU_FIELD_COMMITTED;
        if (ev->key == TIKU_KEY_ESCAPE) return TIKU_FIELD_CANCELLED;
        int result = key(e, in, ev);
        if (!result) return TIKU_FIELD_IGNORED;
        if (result < 0) return TIKU_FIELD_REJECTED;
    } else return TIKU_FIELD_IGNORED;
    visible(e, in, r);
    return TIKU_FIELD_HANDLED;
}
