/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_roster.h - what the Deskbar lists: this user's running
 * processes, and the windows of a workspace.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DESKBAR_ROSTER_H_
#define TIKU_TRK_DESKBAR_ROSTER_H_

#include "tiku_trk_deskbar.h"
#include "tiku_window.h"

/**
 * @brief Bring the Deskbar's process entries in line with this user's
 *        running processes: new ones added, ended ones pruned (DB-015).
 * @return nonzero when the list changed.
 */
int tiku_trk_deskbar_scan_processes(tiku_trk_deskbar_t *deskbar);

/** @brief Publish a workspace's open windows beneath one Deskbar team. */
void tiku_trk_deskbar_publish_workspace(tiku_trk_deskbar_t *deskbar,
    int team, tiku_workspace_t *workspace);

#endif /* TIKU_TRK_DESKBAR_ROSTER_H_ */
