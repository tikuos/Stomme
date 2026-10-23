/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richsession.c - independent directional selections and command origin.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <string.h>

tiku_rich_status_t
tiku_richsession_create(tiku_richdoc_t *doc, tiku_richsession_t **out)
{
    tiku_richsession_t *session;
    tiku_rich_pos_t start;
    tiku_rich_status_t status = TIKU_RICH_OK;

    if (!out)
        return TIKU_RICH_INVALID;
    *out = NULL;
    if (!doc)
        return TIKU_RICH_INVALID;
    if (doc->notifying)
        return TIKU_RICH_BUSY;
    session = tiku_rd_alloc(doc->memory, sizeof *session, &status);
    if (!session)
        return status;
    start = (tiku_rich_pos_t){doc->root->body,
        doc->root->paragraphs[0]->value.id, 0, TIKU_RICH_AFTER};
    status = tiku_richanchor_create(doc, start, &session->anchor);
    if (status == TIKU_RICH_OK)
        status = tiku_richanchor_create(doc, start, &session->caret);
    if (status != TIKU_RICH_OK) {
        tiku_richanchor_release(session->anchor);
        tiku_rd_free(session);
        return status;
    }
    session->doc = doc;
    session->next = doc->sessions;
    doc->sessions = session;
    *out = session;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richsession_destroy(tiku_richsession_t *session)
{
    tiku_richsession_t **at;

    if (!session)
        return TIKU_RICH_OK;
    if (session->doc->notifying)
        return TIKU_RICH_BUSY;
    at = &session->doc->sessions;
    while (*at != session)
        at = &(*at)->next;
    *at = session->next;
    tiku_richanchor_release(session->anchor);
    tiku_richanchor_release(session->caret);
    tiku_rd_free(session);
    return TIKU_RICH_OK;
}

uint64_t
tiku_richsession_id(const tiku_richsession_t *session)
{
    return session ? session->anchor->id : 0;
}

tiku_rich_status_t
tiku_richsession_selection(const tiku_richsession_t *session,
    tiku_rich_pos_t *anchor, tiku_rich_pos_t *caret)
{
    if (anchor)
        memset(anchor, 0, sizeof *anchor);
    if (caret)
        memset(caret, 0, sizeof *caret);
    if (!session || !anchor || !caret || anchor == caret)
        return TIKU_RICH_INVALID;
    *anchor = session->anchor->position;
    *caret = session->caret->position;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richsession_select(tiku_richsession_t *session, uint64_t generation,
                         tiku_rich_pos_t anchor, tiku_rich_pos_t caret)
{
    tiku_richsnapshot_t snapshot = {0};
    tiku_rich_status_t status;
    int order;

    if (!session)
        return TIKU_RICH_INVALID;
    if (session->doc->notifying)
        return TIKU_RICH_BUSY;
    if (generation && generation != session->doc->generation)
        return TIKU_RICH_STALE;
    snapshot.root = session->doc->root;
    status = tiku_richsnapshot_compare(&snapshot, anchor, caret, &order);
    if (status != TIKU_RICH_OK)
        return status;
    if (session->anchor->revision == UINT64_MAX ||
        session->caret->revision == UINT64_MAX)
        return TIKU_RICH_LIMIT;
    tiku_richanchor_set(session->anchor, anchor);
    tiku_richanchor_set(session->caret, caret);
    session->pending = (tiku_rs_pending_t){0};
    tiku_richsession_break_undo(session);
    tiku_rs_notify_selection(session);
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richsession_set_read_only(tiku_richsession_t *session, int read_only)
{
    if (!session)
        return TIKU_RICH_INVALID;
    if (session->doc->notifying)
        return TIKU_RICH_BUSY;
    session->read_only = !!read_only;
    tiku_richsession_break_undo(session);
    return TIKU_RICH_OK;
}

int
tiku_richsession_read_only(const tiku_richsession_t *session)
{
    return session && (session->read_only || session->doc->read_only);
}

tiku_richcmd_context_t
tiku_rs_context(const tiku_richsession_t *session)
{
    tiku_richcmd_context_t context = {.generation = session->doc->generation,
        .first = session->anchor->position, .last = session->caret->position,
        .read_only = session->read_only};
    tiku_richsnapshot_t snapshot = {.root = session->doc->root};
    int order = 0;

    tiku_richsnapshot_compare(&snapshot, context.first, context.last, &order);
    if (order > 0) {
        context.first = session->caret->position;
        context.last = session->anchor->position;
    }
    return context;
}

tiku_rich_status_t
tiku_richsession_execute(tiku_richsession_t *session, uint64_t generation,
                          const tiku_richcmd_t *command)
{
    tiku_richcmd_context_t context;
    tiku_richcmd_state_t state;
    tiku_rich_status_t status;

    if (!session || !command)
        return TIKU_RICH_INVALID;
    if (session->doc->notifying || session->doc->initiator)
        return TIKU_RICH_BUSY;
    if (generation && generation != session->doc->generation)
        return TIKU_RICH_STALE;
    status = tiku_richsession_query(session, command->kind, &state);
    if (status != TIKU_RICH_OK)
        return status;
    if (!state.enabled)
        return state.status;
    context = tiku_rs_context(session);
    if (context.first.paragraph == context.last.paragraph &&
        context.first.byte == context.last.byte &&
        tiku_rs_caret_command(command->kind)) {
        status = tiku_rs_format(session, command, &state);
        if (status == TIKU_RICH_OK || status == TIKU_RICH_NO_CHANGE)
            tiku_richsession_break_undo(session);
        return status;
    }
    session->collapse = (command->kind >= TIKU_RICHCMD_REPLACE_TEXT &&
                        command->kind <= TIKU_RICHCMD_PASTE_FRAGMENT) ||
                        command->kind == TIKU_RICHCMD_IMAGE_INSERT ||
                        command->kind == TIKU_RICHCMD_TABLE_INSERT ||
                        command->kind == TIKU_RICHCMD_TABLE_REMOVE ||
                        (command->kind >= TIKU_RICHCMD_ROW_BEFORE &&
                         command->kind <= TIKU_RICHCMD_COLUMN_REMOVE);
    session->typing = command->kind == TIKU_RICHCMD_REPLACE_TEXT ||
                      command->kind == TIKU_RICHCMD_PARAGRAPH_BREAK;
    session->doc->initiator = session;
    status = tiku_richcmd_execute(session->doc, &context, command);
    session->doc->initiator = NULL;
    session->collapse = session->typing = 0;
    return status;
}
