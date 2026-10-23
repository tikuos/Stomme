/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richsession_typing.c - explicit typing input and undo-group boundaries.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

tiku_rich_status_t
tiku_richsession_break_undo(tiku_richsession_t *session)
{
    tiku_rd_history_t *entry;

    if (!session)
        return TIKU_RICH_INVALID;
    if (session->doc->notifying)
        return TIKU_RICH_BUSY;
    entry = session->doc->undo;
    if (entry && entry->origin == tiku_richsession_id(session))
        entry->groupable = 0;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richsession_type(tiku_richsession_t *session, uint64_t generation,
                       const char *text, size_t length)
{
    const tiku_richcmd_t command = {.kind = TIKU_RICHCMD_REPLACE_TEXT,
        .value.text = {text, length}};
    tiku_rich_status_t status;

    if (!session)
        return TIKU_RICH_INVALID;
    if (session->doc->notifying || session->doc->initiator)
        return TIKU_RICH_BUSY;
    session->coalescing = 1;
    status = tiku_richsession_execute(session, generation, &command);
    session->coalescing = 0;
    return status;
}
