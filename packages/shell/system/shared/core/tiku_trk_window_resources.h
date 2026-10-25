/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_window_resources.h - live workspace and window resource owners.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_WINDOW_RESOURCES_H_
#define TIKU_TRK_WINDOW_RESOURCES_H_
#include "tiku_resource_jobs.h"
#include "tiku_window.h"
/** @brief Window operations routed through the shell's native controllers. */
typedef enum {
    TIKU_TRK_WINDOW_FOCUS, TIKU_TRK_WINDOW_MAXIMIZE, TIKU_TRK_WINDOW_MINIMIZE,
    TIKU_TRK_WINDOW_MOVE, TIKU_TRK_WINDOW_CLOSE, TIKU_TRK_WINDOW_RESIZE,
    TIKU_TRK_WINDOW_RESTORE, TIKU_TRK_WINDOW_ACTIONS
} tiku_trk_window_action_t;
/** @brief Copied connection identity and supported operation mask. */
typedef struct {
    char kind[16], name[64];
    uint64_t connection, generation;
    uint32_t local_id;
    unsigned actions;
} tiku_trk_window_source_t;
/** @brief Owner-thread registrations for one composited workspace. */
typedef struct tiku_trk_window_resources tiku_trk_window_resources_t;
/** @brief Register a workspace; callbacks inspect and control current owners without I/O.
 *  @note source returns zero on success. Call poll after native reconciliation and
 *        before servicing requests. All arguments and callbacks outlive the owner. */
tiku_trk_window_resources_t *tiku_trk_window_resources_open(tiku_resource_registry_t *,
    tiku_workspace_t *, int width, int height,
    int (*source)(void *, tiku_window_t *, tiku_trk_window_source_t *),
    tiku_resource_error_t (*perform)(void *, tiku_window_t *, tiku_trk_window_action_t, int, int), void *);
/** @brief Reconcile lifetimes and publish changed state on the owner thread. */
void tiku_trk_window_resources_poll(tiku_trk_window_resources_t *);
/** @brief Resolve a current opaque window identity; NULL for a retired native owner. */
tiku_window_t *tiku_trk_window_resources_find(tiku_trk_window_resources_t *,const char *id);
/** @brief Copy the current opaque identity for a native window; -1 when unregistered. */
int tiku_trk_window_resources_id(tiku_trk_window_resources_t *,tiku_window_t *,char *,size_t);
/** @brief Retire registrations before their workspace and registry are released. */
void tiku_trk_window_resources_close(tiku_trk_window_resources_t *);
#endif
