/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richsession_observer.c - allocation-free session selection delivery.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

/** @brief Compare endpoints, including affinity but excluding padding bytes. */
static int
same(tiku_rich_pos_t a, tiku_rich_pos_t b)
{
    return a.body == b.body && a.paragraph == b.paragraph &&
        a.byte == b.byte && a.affinity == b.affinity;
}

tiku_rich_status_t
tiku_richsession_observe_selection(tiku_richsession_t *session,
    tiku_rich_selection_fn callback, void *context)
{
    if (!session) return TIKU_RICH_INVALID;
    if (session->doc->notifying) return TIKU_RICH_BUSY;
    session->selection_changed = callback;
    session->selection_context = callback ? context : NULL;
    session->observed_anchor = session->anchor->position;
    session->observed_caret = session->caret->position;
    return TIKU_RICH_OK;
}

void
tiku_rs_notify_selection(tiku_richsession_t *session)
{
    tiku_rich_selection_change_t change;
    int notifying;

    if (!session->selection_changed ||
        (same(session->observed_anchor, session->anchor->position) &&
         same(session->observed_caret, session->caret->position))) return;
    change = (tiku_rich_selection_change_t){
        .generation = session->doc->generation,
        .previous_anchor = session->observed_anchor,
        .previous_caret = session->observed_caret,
        .anchor = session->anchor->position,
        .caret = session->caret->position};
    session->observed_anchor = change.anchor;
    session->observed_caret = change.caret;
    notifying = session->doc->notifying;
    session->doc->notifying = 1;
    session->selection_changed(session->selection_context, session, &change);
    session->doc->notifying = notifying;
}
