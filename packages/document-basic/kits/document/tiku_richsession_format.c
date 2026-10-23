/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richsession_format.c - caret formatting, queries and history state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richcmd_private.h"

#include <string.h>

int
tiku_rs_caret_command(tiku_richcmd_kind_t kind)
{
    return tiku_rc_toggle_bit(kind) ||
        kind == TIKU_RICHCMD_CHARACTER_FORMAT ||
        kind == TIKU_RICHCMD_CLEAR_CHARACTER_OVERRIDES ||
        kind == TIKU_RICHCMD_CHARACTER_STYLE;
}

void
tiku_rs_typing(const tiku_richsession_t *session, tiku_rich_run_t *run)
{
    if (!session)
        return;
    tiku_rd_chars_reset(&run->chars, session->pending.clear);
    run->reset &= ~session->pending.clear;
    if (session->pending.style_set)
        run->style = session->pending.style;
    tiku_rd_chars_overlay(&run->chars, &session->pending.chars);
}

tiku_rich_status_t
tiku_richsession_format(const tiku_richsession_t *session,
                         tiku_rich_selection_t *out)
{
    tiku_richcmd_context_t context;
    tiku_richsnapshot_t snapshot = {0};
    tiku_rich_run_t run;
    const tiku_rich_paragraph_t *p;
    tiku_rich_status_t status;

    if (!out)
        return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!session)
        return TIKU_RICH_INVALID;
    context = tiku_rs_context(session);
    snapshot.root = session->doc->root;
    status = tiku_richsnapshot_selection(&snapshot, context.first,
                                         context.last, out);
    if (status != TIKU_RICH_OK || !out->empty)
        return status;
    p = &snapshot.root->paragraphs[
        tiku_rd_find(snapshot.root, context.first.paragraph)]->value;
    run = tiku_rd_typing_run(p, context.first.byte);
    tiku_rs_typing(session, &run);
    out->character_style = run.style;
    tiku_rd_resolve_value(snapshot.root, p, &run, &out->chars, &out->props);
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richsession_query(const tiku_richsession_t *session,
    tiku_richcmd_kind_t kind, tiku_richcmd_state_t *out)
{
    tiku_richcmd_context_t context;
    tiku_rich_selection_t selection;
    tiku_rich_status_t status;

    if (!out)
        return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!session)
        return TIKU_RICH_INVALID;
    context = tiku_rs_context(session);
    status = tiku_richcmd_query(session->doc, &context, kind, out);
    if (status != TIKU_RICH_OK || !tiku_rs_caret_command(kind))
        return status;
    status = tiku_richsession_format(session, &selection);
    if (status != TIKU_RICH_OK || !selection.empty)
        return status;
    out->check = tiku_rc_checked(kind, &selection);
    if (out->status == TIKU_RICH_NO_CHANGE) {
        out->enabled = 1;
        out->status = TIKU_RICH_OK;
        out->reason = NULL;
    }
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_rs_format(tiku_richsession_t *session, const tiku_richcmd_t *command,
                 const tiku_richcmd_state_t *state)
{
    tiku_rs_pending_t pending = session->pending;
    tiku_rich_chars_t chars = {0};
    tiku_rich_status_t status;
    unsigned bit = tiku_rc_toggle_bit(command->kind), clear = 0;
    int on = state->check != TIKU_RICHCMD_CHECK_ON;

    if (bit) {
        chars.present = bit;
        if (bit == TIKU_RICH_BOLD) chars.bold = on;
        if (bit == TIKU_RICH_ITALIC) chars.italic = on;
        if (bit == TIKU_RICH_UNDERLINE) chars.underline = on;
        if (bit == TIKU_RICH_STRIKE) chars.strike = on;
    } else if (command->kind == TIKU_RICHCMD_CHARACTER_FORMAT) {
        clear = command->value.character.clear;
        if (clear & ~511u)
            return TIKU_RICH_INVALID_STYLE;
        status = tiku_rd_chars(&command->value.character.values, &chars);
        if (status != TIKU_RICH_OK)
            return status;
    } else if (command->kind == TIKU_RICHCMD_CLEAR_CHARACTER_OVERRIDES)
        clear = 511u;
    else {
        if (!tiku_rd_style_ref(session->doc->root, command->value.style,
                               TIKU_RICH_CHARACTER_STYLE))
            return TIKU_RICH_INVALID_STYLE;
        pending.style_set = 1;
        pending.style = command->value.style;
    }
    tiku_rd_chars_reset(&pending.chars, clear);
    pending.clear |= clear;
    tiku_rd_chars_overlay(&pending.chars, &chars);
    if (pending.clear == session->pending.clear &&
        pending.style_set == session->pending.style_set &&
        pending.style == session->pending.style &&
        tiku_rd_chars_equal(&pending.chars, &session->pending.chars))
        return TIKU_RICH_NO_CHANGE;
    session->pending = pending;
    return TIKU_RICH_OK;
}

void
tiku_rs_history(tiku_richdoc_t *doc, const tiku_rd_history_t *entry,
                  int forward)
{
    tiku_richsession_t *session;

    for (session = doc->sessions; session; session = session->next) {
        if (!entry)
            session->pending = (tiku_rs_pending_t){0};
        else if (tiku_richsession_id(session) == entry->origin)
            session->pending = forward ? entry->pending_after :
                                         entry->pending_before;
        if (session->pending.style_set &&
            !tiku_rd_style_ref(doc->root, session->pending.style,
                                TIKU_RICH_CHARACTER_STYLE)) {
            session->pending.style = 0;
            session->pending.style_set = 0;
        }
    }
}
