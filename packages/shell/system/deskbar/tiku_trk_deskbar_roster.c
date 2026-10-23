/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_roster.c - what the Deskbar lists: this user's running
 * processes, and the windows of a workspace.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_sys.h"
#include "tiku_trk_deskbar_roster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief What one pass of the roster is building, across the callbacks. */
struct roster_pass {
    tiku_trk_deskbar_t *deskbar;    /* the bar being brought up to date */
    long                live[512];  /* the pids seen in this pass       */
    int                 nlive;      /* live[] in use                    */
    int                 changed;    /* an entry or instance came or went */
};

/** @brief Add one process that tiku_sys_processes() reports to its entry
 *         and to the pass's live pids. */
static void
roster_note(const tiku_sys_process_t *process, void *ctx)
{
    struct roster_pass *pass = ctx;

    /* The shell's own process is skipped: the shell seeds its entry as
     * TikuTracker, and listing it again would show it twice. */
    if (strcmp(process->name, "desktop") == 0 ||
        strcmp(process->name, TIKU_SHELL_DESKTOP_NAME) == 0) {
        return;
    }
    (void)tiku_trk_deskbar_add_team_pid(pass->deskbar, process->name,
                                        process->pid, process->command,
                                        &pass->changed);
    if (pass->nlive < (int)(sizeof pass->live / sizeof pass->live[0])) {
        pass->live[pass->nlive++] = process->pid;
    }
}

int
tiku_trk_deskbar_scan_processes(tiku_trk_deskbar_t *deskbar)
{
    struct roster_pass pass;

    if (deskbar == NULL) {
        return 0;
    }
    memset(&pass, 0, sizeof pass);
    pass.deskbar = deskbar;
    if (tiku_sys_processes(roster_note, &pass) < 0) {
        return 0;
    }
    /* Then what has quit since the last pass: an entry whose last
     * instance went disappears (DB-015). */
    if (tiku_trk_deskbar_prune_teams(deskbar, pass.live, pass.nlive)) {
        pass.changed = 1;
    }
    return pass.changed;
}

void
tiku_trk_deskbar_publish_workspace(tiku_trk_deskbar_t *deskbar,
    int team, tiku_workspace_t *workspace)
{
    tiku_trk_dbwin_t windows[TIKU_TRK_DB_WINS_MAX];
    tiku_window_t *focused;
    int i, count, published = 0;

    if (deskbar == NULL || workspace == NULL) {
        return;
    }
    focused = tiku_workspace_focused(workspace);
    count = tiku_workspace_count(workspace);
    for (i = 0; i < count && published < TIKU_TRK_DB_WINS_MAX; i++) {
        tiku_window_t *window = tiku_workspace_at(workspace, i);

        if (window == NULL || !window->open) {
            continue;
        }
        memset(&windows[published], 0, sizeof windows[published]);
        tiku_trk_deskbar_strip_title(TIKU_SHELL_TRACKER_NAME, window->title,
            windows[published].title, sizeof windows[published].title);
        windows[published].state = window->minimized
            ? TIKU_TRK_DBW_MINIMIZED : TIKU_TRK_DBW_SHOWN;
        windows[published].current = window == focused;
        windows[published].tag = window->tag;
        published++;
    }
    tiku_trk_deskbar_set_windows(deskbar, team, windows, published,
                                 focused != NULL);
}
