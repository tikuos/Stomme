/*
 * KintsugiDesktop v0.01 Interface Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * tiku_ime.c - scalar-indexed marked text and atomic UTF-8 preedit events.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ime.h"
#include <string.h>
#include <limits.h>
/** @brief Convert a Unicode scalar index to a byte offset in validated UTF-8.
 */
static size_t
offset(const char *text, size_t scalar)
{
    size_t at = 0;
    while (scalar && text[at]) {
        at++;
        while (((unsigned char)text[at] & 0xc0) == 0x80)
            at++;
        scalar--;
    }
    return scalar ? SIZE_MAX : at;
}
void tiku_ime_submitted(tiku_ime_t *ime)
{
    ime->cancel = ime->active = 0;
    tiku_text_input_reset_v2(&ime->pending);
}

void tiku_ime_committed(tiku_ime_t *ime)
{
    ime->text[0] = 0;
    ime->selected = 0;
    ime->cancel = ime->active = 0;
    tiku_text_input_reset_v2(&ime->pending);
}
void tiku_ime_cancel(tiku_ime_t *ime)
{
    tiku_ime_committed(ime);
    ime->cancel = 1;
}
int tiku_ime_begin(tiku_ime_t *ime)
{
    tiku_ime_committed(ime);
    if (ime->sequence == UINT_MAX) {
        ime->cancel = 1;
        return 0;
    }
    ime->token = ++ime->sequence;
    ime->active = 1;
    return 1;
}
int tiku_ime_set(tiku_ime_t *ime, const char *text, size_t bytes,
    size_t selected)
{
    size_t count = 0;
    for (size_t i = 0; ime->text[i]; i++)
        count += ((unsigned char)ime->text[i] & 0xc0) != 0x80;
    return tiku_ime_replace(ime, 0, count, text, bytes, selected);
}

int tiku_ime_replace(tiku_ime_t *ime, size_t first, size_t removed,
                 const char *text, size_t bytes, size_t selected)
{
    if (!ime->active) {
        if (ime->sequence == UINT_MAX) {
            tiku_ime_cancel(ime);
            return 0;
        }
        ime->token = ++ime->sequence;
        ime->active = 1;
        ime->cancel = 0;
    }
    char assembled[TIKU_TEXT_INPUT_V2_MAX];
    size_t a = offset(ime->text, first), b;
    if (removed > SIZE_MAX - first)
        goto reject;
    b = offset(ime->text, first + removed);
    size_t old = strlen(ime->text);
    if (!text || a == SIZE_MAX || b == SIZE_MAX || bytes >= sizeof assembled ||
        old - b >= sizeof assembled - bytes ||
        a >= sizeof assembled - bytes - (old - b))
        goto reject;
    memcpy(assembled, ime->text, a);
    memcpy(assembled + a, text, bytes);
    memcpy(assembled + a + bytes, ime->text + b, old - b + 1);
    size_t caret = offset(assembled, selected);
    if (caret == SIZE_MAX ||
        !tiku_text_input_set_v2(&ime->pending, assembled, a + bytes + old - b))
        goto reject;
    memcpy(ime->text, assembled, a + bytes + old - b + 1);
    ime->selected = caret;
    ime->cancel = !ime->pending.length;
    return 1;
reject:
    tiku_ime_cancel(ime);
    return 0;
}
int tiku_ime_next(tiku_ime_t *ime, tiku_input_event_t *event)
{
    if (ime->cancel) {
        memset(event, 0, sizeof *event);
        event->type = TIKU_EVENT_PREEDIT;
        event->button = TIKU_TEXT_REJECT;
        event->repeat = ime->token;
        ime->cancel = 0;
        return 1;
    }
    if (!tiku_text_input_next_v2(&ime->pending, event))
        return 0;
    event->type = TIKU_EVENT_PREEDIT;
    event->x = (int)ime->selected;
    event->repeat = ime->token;
    return 1;
}
