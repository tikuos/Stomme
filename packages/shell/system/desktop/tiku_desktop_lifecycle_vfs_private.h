/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desktop_lifecycle_vfs_private.h - copied native lifecycle observations.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DESKTOP_LIFECYCLE_VFS_PRIVATE_H_
#define TIKU_DESKTOP_LIFECYCLE_VFS_PRIVATE_H_
#include "tiku_trk_desktop_private.h"
#include "tiku_trk_app_resources.h"
#include "tiku_trk_lifecycle_resources.h"
#include "../supervisor/tiku_supervisor_state.h"
#include "tiku_trk_resource_values.h"
#include "tiku_work.h"
#define LIFE_TEXT(o,k,v) trk_value_member(o,k,tiku_resource_value_string(TIKU_RESOURCE_TEXT,v))
#define LIFE_INT(o,k,v) trk_value_member(o,k,tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,v))
#define LIFE_BOOL(o,k,v) trk_value_member(o,k,tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,(v)!=0))
/** @brief One copied application connection or pending child. */
typedef struct {
    char token[96],name[64],manifest[64];
    int kind,index;
    uint64_t generation;
    long pid;
} lifecycle_app_t;
/** @brief Native caches retained by the desktop thread. */
struct desktop_lifecycle_vfs {
    desktop_t *app;
    tiku_resource_registry_t *registry;
    tiku_trk_lifecycle_resources_t *entities;
    tiku_trk_app_resources_t *catalog;
    tiku_trk_resource_observer_t *services,*notifications;
    tiku_resource_stamp_t actions[2];
    int actions_live[2];
    tiku_work_observation_t work[128];size_t work_count;
    lifecycle_app_t apps[64];int app_count,apps_complete;
    struct {uint64_t generation;long pid;} peers[TIKU_REMOTE_SESSIONS];
    uint64_t file_generation,next_poll;
    tiku_progress_t file_progress;
    int file_available,file_busy;
    int supervisor_fd,supervisor_available;
    tiku_trk_supervisor_state_t supervisor;
};
/** @brief Count loaded catalog observation rows without filesystem access. */
int desktop_lifecycle_catalog_count(void *,const char *);
/** @brief Copy one loaded manifest, capability, type or association row. */
tiku_msg_t *desktop_lifecycle_catalog_capture(void *,const char *,int);
/** @brief Dispatch one installed launch through its existing native owner. */
tiku_resource_error_t desktop_lifecycle_catalog_run(void *,const tiku_trk_app_t *,const char *,tiku_msg_t **);
/** @brief Register session and file-manager action owners. */
int desktop_lifecycle_actions_open(struct desktop_lifecycle_vfs *);
/** @brief Refresh the native scripting identity and ordering precondition. */
void desktop_lifecycle_actions_poll(struct desktop_lifecycle_vfs *);
/** @brief Retire native action owners before their registry is destroyed. */
void desktop_lifecycle_actions_close(struct desktop_lifecycle_vfs *);
/** @brief Copy a stable counter into a typed text member. */
static inline int lifecycle_counter(tiku_msg_t *out,const char *key,unsigned long long counter)
{
    char text[32];snprintf(text,sizeof text,"%llu",counter);return LIFE_TEXT(out,key,text);
}
#endif
