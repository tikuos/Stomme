/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desktop_host_vfs_private.h - native host caches and service adapters.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DESKTOP_HOST_VFS_PRIVATE_H_
#define TIKU_DESKTOP_HOST_VFS_PRIVATE_H_
#include "tiku_trk_desktop_private.h"
#include "tiku_trk_clipboard_resources.h"
#include "tiku_trk_volume_resources.h"
#include "tiku_trk_resource_arguments.h"
#include "tiku_trk_resource_observer.h"
#include "tiku_work.h"
#include "tiku_print_rpc.h"
#define HOST_ACCESS_WINDOWS (TIKU_REMOTE_SESSIONS*TIKU_REMOTE_WINDOWS)
/** @brief One retained immutable publication and its bounded encoded packet.
 */
typedef struct {
    /* retained semantic publication */
    tiku_access_tree_t *tree;
    /* borrowed native window owner */
    tiku_remote_window_t *remote;
    uint64_t connection,incarnation;
    /* opaque window resource identity */
    char window[33];
    /* owned native codec bytes and their size */
    void *wire;size_t bytes;
    /* complete encoding availability */
    int encoded;
} host_access_t;
/** @brief Cached owner-thread facts; captures neither poll devices nor
 * dispatch input.
 */
struct desktop_host_vfs {
    /* desktop owner */
    desktop_t *app;tiku_resource_registry_t *registry;
    tiku_trk_resource_observer_t *display, *input, *audio, *metrics, *access,
        *printers;
    /* native clipboard provider */
    tiku_trk_clipboard_resources_t *clipboard;
    /* cached volume providers */
    tiku_trk_volume_resources_t *volumes;
    char display_id[33], display_paths[5][96];
    /* canvas resource metadata */
    tiku_trk_observation_t display_fields[5];
    /* cached native canvas facts */
    tiku_host_observation_t host;
    /* cached engine facts */
    tiku_sound_observation_t sound;
    /* copied work pool counters */
    tiku_work_stats_t work;
    /* copied registry counters */
    tiku_resource_registry_observation_t registry_stats;
    /* copied native print jobs */
    tiku_print_job_observation_t print_jobs[4];size_t print_count;
    int last_print_known, last_print_status;
    /* last native print request */
    uint64_t last_print_ticket;
    /* last native completion reason */
    char last_print_reason[256];
    /* retained application publications */
    host_access_t access_windows[HOST_ACCESS_WINDOWS];
    int access_count, access_complete;
    int input_seen,pointer_seen,native_active;unsigned modifiers;
    /* next observation time in milliseconds */
    uint64_t next_poll;
};
/** @brief Count retained native access metadata or encoded chunks. */
int desktop_host_access_count(void *,const char *);
/** @brief Copy access metadata or one frozen native-codec chunk. */
tiku_msg_t *desktop_host_access_capture(void *,const char *,int);
/** @brief Refresh retained immutable publications from live remote owners. */
void desktop_host_access_refresh(struct desktop_host_vfs *);
/** @brief Release retained publications and encoded packets. */
void desktop_host_access_clear(struct desktop_host_vfs *);
/** @brief Validate one exact current semantic action without input grants. */
tiku_resource_error_t desktop_host_access_validate(void *, const char *,
                                                   const tiku_msg_t *, int);
/** @brief Queue a generation-checked semantic action through the native
 * owner.
 */
tiku_resource_error_t desktop_host_access_start(void *, const char *,
                                                const char *,
                                                const tiku_msg_t *, int,
                                                tiku_resource_snapshot_t *);
/** @brief Copy native print grants, jobs and cached completion state. */
tiku_msg_t *desktop_host_print_capture(void *,const char *,int);
/** @brief Count prepared grants and current native print jobs. */
int desktop_host_print_count(void *,const char *);
/** @brief Validate a print operation against an existing application grant.
 */
tiku_resource_error_t desktop_host_print_validate(void *, const char *,
                                                  const tiku_msg_t *, int);
/** @brief Submit or cancel through the existing exact print-grant owner. */
tiku_resource_error_t desktop_host_print_start(void *, const char *,
                                               const char *,
                                               const tiku_msg_t *, int,
                                               tiku_resource_snapshot_t *);
#endif
