/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richview_commands.c - typed commands for text and cell selections.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richview_private.h"
#include <string.h>

tiku_rich_status_t
tiku_richview_query(tiku_richview_t *view, tiku_richcmd_kind_t kind,
                    tiku_richcmd_state_t *out)
{
    tiku_rich_cells_t cells;

    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!view) return TIKU_RICH_INVALID;
    if (tiku_richview_cells(view, &cells) != TIKU_RICH_OK ||
        kind == TIKU_RICHCMD_UNDO || kind == TIKU_RICHCMD_REDO)
        return tiku_richsession_query(view->session, kind, out);
    if (!tiku_richcmd_info(kind)) return TIKU_RICH_UNSUPPORTED;
    if (view->doc->notifying || view->doc->initiator) return TIKU_RICH_BUSY;
    out->generation = view->doc->generation;
    out->enabled = kind == TIKU_RICHCMD_CELL_SET ||
        kind == TIKU_RICHCMD_DELETE || kind == TIKU_RICHCMD_REPLACE_TEXT;
    out->status = out->enabled ? TIKU_RICH_OK : TIKU_RICH_UNSUPPORTED;
    out->reason = out->enabled ? "" : "Command does not support cell ranges.";
    if (tiku_richsession_read_only(view->session)) {
        out->enabled = 0;
        out->status = TIKU_RICH_READ_ONLY;
        out->reason = "The document or view is read-only.";
    }
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richview_execute(tiku_richview_t *view, uint64_t generation,
                      const tiku_richcmd_t *command)
{
    tiku_richcmd_state_t state;
    tiku_rich_cells_t cells;
    tiku_rich_status_t status;

    if (!view || !command) return TIKU_RICH_INVALID;
    if (view->doc->notifying || view->doc->initiator) return TIKU_RICH_BUSY;
    if (generation && generation != view->doc->generation)
        return TIKU_RICH_STALE;
    status = tiku_richview_query(view, command->kind, &state);
    if (status != TIKU_RICH_OK) return status;
    if (!state.enabled) return state.status;
    if (tiku_richview_cells(view, &cells) == TIKU_RICH_OK) {
        if (command->kind == TIKU_RICHCMD_CELL_SET)
            return tiku_richview_cells_shade(view, command->value.color);
        if (command->kind == TIKU_RICHCMD_DELETE)
            return tiku_richview_cells_clear(view);
        if (command->kind == TIKU_RICHCMD_REPLACE_TEXT)
            return tiku_richview_cells_paste_text(view,
                command->value.text.data, command->value.text.length);
    }
    return tiku_richsession_execute(view->session, generation, command);
}
