/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_volume_resources.c - cached mount lifetimes and native unmount
 * requests.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_volume_resources.h"
#include "tiku_trk_resource_arguments.h"
#include "tiku_trk_resource_observer.h"
#include <stdlib.h>
/** @brief One retained native mount identity and its provider incarnation. */
typedef struct {
    struct tiku_trk_volume_resources *owner;
    tiku_volume_t volume;
    char id[33],base[64],paths[8][128];
    tiku_trk_observation_t fields[8];
    tiku_trk_resource_observer_t *observer;
} volume_entry_t;
struct tiku_trk_volume_resources {
    tiku_resource_registry_t *registry;
    tiku_trk_volume_request_fn request;
    void *context;
    volume_entry_t entries[TIKU_VOLUME_MAX];
    tiku_trk_resource_observer_t *status;
    uint64_t next_poll,revision;
    int available,expected,registered;
};
static const char *const field_names[] = {
    "identity", "mounts", "capacity",        "free-space",
    "flags",    "state",  "actions/unmount", "actions/eject"};
/** @brief Match the identity facts supplied by the native mount inventory. */
static int same(const tiku_volume_t *a,const tiku_volume_t *b)
{
    return a->dev == b->dev && !strcmp(a->mount, b->mount) &&
           !strcmp(a->source, b->source) && !strcmp(a->fstype, b->fstype);
}
/** @brief Read the current cached identity without a filesystem scan. */
static int live(volume_entry_t *entry,tiku_volume_t *out)
{
    tiku_volumes_t volumes;int count=tiku_volumes_cached(&volumes,NULL);
    if(count<0||count>TIKU_VOLUME_MAX||count!=volumes.n)return 0;
    for (int i = 0; i < count; i++)
        if (same(&entry->volume, &volumes.v[i])) {
            if (out)
                *out = volumes.v[i];
            return 1;
        }
    return 0;
}
/** @brief These providers expose object states rather than stream rows. */
static int count_items(void *context, const char *path)
{
    (void)context;
    (void)path;
    return 0;
}
/** @brief Copy capacity and policy from one native cached record. */
static tiku_msg_t *capture(void *context,const char *path,int index)
{
    volume_entry_t *entry = context;
    tiku_volume_t *volume = &entry->volume;
    tiku_msg_t *out = trk_value_container("object");
    int bad = 0;
    (void)index;
    if(!entry->owner->available){tiku_msg_free(out);return NULL;}
    const char *field=path+strlen(entry->base)+1;
    if(!strcmp(field,"identity")) {
        bad |= TRK_TEXT(out, "id", entry->id);
        bad |= TRK_TEXT(out, "name", volume->name);
        bad |= trk_counter(out, "device", (uint64_t)volume->dev);
        bad |= TRK_TEXT(out, "source", volume->source);
        bad |= TRK_TEXT(out, "filesystem", volume->fstype);
        bad |= TRK_TEXT(out, "identity_scope", "observed_native_mount_record");
    } else if(!strcmp(field,"mounts"))bad|=TRK_TEXT(out,"path",volume->mount);
    else if (!strcmp(field, "capacity")) {
        bad |= trk_counter(out, "bytes", volume->total);
        bad |= TRK_BOOL(out, "known", volume->total != 0);
    } else if (!strcmp(field, "free-space")) {
        bad |= trk_counter(out, "bytes", volume->avail);
        bad |= TRK_BOOL(out, "known", volume->total != 0);
    } else if (!strcmp(field, "flags")) {
        bad |= TRK_BOOL(out, "read_only", volume->read_only);
        bad |= TRK_BOOL(out, "boot", volume->is_boot);
        bad |= TRK_BOOL(out, "removable", volume->removable);
        bad |= TRK_BOOL(out, "shared", volume->shared);
        bad |= TRK_BOOL(out, "persistent", volume->persistent);
        bad |= TRK_INT(out, "model_flags", tiku_volume_flags(volume));
    } else {
        char why[200]={0};int allowed=!volume->is_boot;
        if (!allowed)
            snprintf(why, sizeof why,
                     "The cached native record identifies the boot volume.");
        bad |= TRK_TEXT(out, "phase", "mounted");
        bad |= TRK_BOOL(out, "unmount_supported", allowed);
        bad |= TRK_TEXT(out, "unmount_reason", allowed ? "" : why);
        bad |= TRK_BOOL(out, "eject_supported", 0);
        bad |= TRK_TEXT(out, "eject_reason",
                        "no_native_physical_eject_controller");
        bad |= TRK_BOOL(out, "atomic_mount_incarnation", 0);
        bad |= TRK_TEXT(out, "completion",
                        "native_desktop_worker_and_notifications");
    }
    if(bad){tiku_msg_free(out);return NULL;}return out;
}
/** @brief Require an exact live record and native policy before dispatch. */
static tiku_resource_error_t validate(void *context, const char *path,
                                      const tiku_msg_t *value, int invoke)
{
    volume_entry_t *entry = context;
    tiku_volume_t volume;
    const char *type = tiku_msg_find_string(value, "type", 0);
    if (!invoke || !type || strcmp(type, "object") ||
        tiku_msg_count(value, "member"))
        return TIKU_RESOURCE_VALUE;
    if(!entry->owner->available)return TIKU_RESOURCE_UNAVAILABLE;
    if(!live(entry,&volume))return TIKU_RESOURCE_OWNER_LOST;
    const char *action = strrchr(path, '/');
    if (!action || strcmp(action, "/unmount"))
        return TIKU_RESOURCE_UNSUPPORTED;
    tiku_volumes_t one={.n=1};char why[200]={0};one.v[0]=volume;
    return tiku_volume_may_unmount(&one, volume.mount, why, sizeof why)
               ? 0
               : TIKU_RESOURCE_UNSUPPORTED;
}
/** @brief Request native save-and-unmount handling, preserving its
 * asynchronous outcome.
 */
static tiku_resource_error_t start(void *context, const char *job,
                                   const char *path, const tiku_msg_t *value,
                                   int invoke, tiku_resource_snapshot_t *out)
{
    volume_entry_t *entry = context;
    tiku_resource_error_t error = validate(context, path, value, invoke);
    (void)job;
    if(error)return error;
    tiku_volume_t volume;
    if (!live(entry, &volume))
        return TIKU_RESOURCE_OWNER_LOST;
    char why[200] = {0};
    int accepted =
        entry->owner->request(entry->owner->context, &volume, why, sizeof why);
    tiku_msg_t *result=trk_value_container("object");
    if (TRK_BOOL(result, "accepted", accepted) ||
        TRK_TEXT(result, "phase",
                 accepted ? "native_unmount_requested"
                          : "native_request_refused") ||
        TRK_TEXT(result, "reason", why) || TRK_BOOL(result, "completed", 0) ||
        TRK_TEXT(result, "volume", entry->id)) {
        tiku_msg_free(result);return TIKU_RESOURCE_UNKNOWN_OUTCOME;
    }
    return trk_value_snapshot(out, result, "request_dispatched")
               ? TIKU_RESOURCE_UNKNOWN_OUTCOME
               : 0;
}
/** @brief Register an independent incarnation for one current native record.
 */
static int install(tiku_trk_volume_resources_t *owner, volume_entry_t *entry,
                   const tiku_volume_t *volume)
{
    entry->owner = owner;
    entry->volume = *volume;
    if (tiku_resource_new_id(entry->id))
        return -1;
    char id[33], key[64];
    memcpy(id, entry->id, sizeof id);
    snprintf(entry->base, sizeof entry->base, "/volumes/%s", id);
    snprintf(key, sizeof key, "volume-%s", id);
    for(int i=0;i<8;i++) {
        snprintf(entry->paths[i], sizeof entry->paths[i], "%s/%s", entry->base,
                 field_names[i]);
        entry->fields[i]=(tiku_trk_observation_t){entry->paths[i],i>=6?2:0,3};
    }
    const tiku_resource_async_controller_t controller = {.validate = validate,
                                                         .start = start};
    entry->observer = tiku_trk_resource_observer_open_controlled(
        owner->registry, key, entry->base, entry->fields, 8, count_items,
        capture, &controller, entry);
    return entry->observer?0:-1;
}
/** @brief Copy inventory availability and explicit registration capacity
 * feedback.
 */
static tiku_msg_t *status_capture(void *context,const char *path,int index)
{
    tiku_trk_volume_resources_t *owner = context;
    tiku_msg_t *out = trk_value_container("object");
    (void)path;
    (void)index;
    if (TRK_BOOL(out, "available", owner->available) ||
        TRK_INT(out, "native_records", owner->expected) ||
        TRK_INT(out, "registered", owner->registered) ||
        TRK_INT(out, "native_capacity", TIKU_VOLUME_MAX) ||
        TRK_BOOL(out, "registration_complete",
                 owner->expected == owner->registered) ||
        TRK_BOOL(out, "inventory_complete",
                 owner->available && owner->expected < TIKU_VOLUME_MAX) ||
        trk_counter(out, "cache_revision", owner->revision) ||
        TRK_TEXT(out, "source", "native_cached_mount_table")) {
        tiku_msg_free(out);
        return NULL;
    }
    return out;
}
void tiku_trk_volume_resources_poll(tiku_trk_volume_resources_t *owner,
                                    uint64_t now)
{
    if(!owner||now<owner->next_poll)return;
    owner->next_poll = now + 250;
    tiku_volumes_t volumes;
    int n = tiku_volumes_cached(&volumes, &owner->revision);
    owner->available=n>=0&&n<=TIKU_VOLUME_MAX&&n==volumes.n;
    if(owner->available) {
        owner->expected=n;
        for(int i=0;i<TIKU_VOLUME_MAX;i++)if(owner->entries[i].observer) {
                int found = -1;
                for (int j = 0; j < n; j++)
                    if (same(&owner->entries[i].volume, &volumes.v[j])) {
                        found = j;
                        break;
                    }
                if (found < 0) {
                    tiku_trk_resource_observer_close(
                        owner->entries[i].observer);
                    memset(&owner->entries[i], 0, sizeof owner->entries[i]);
                } else
                    owner->entries[i].volume = volumes.v[found];
        }
        for(int i=0;i<n;i++) {
            int found = 0;
            for (int j = 0; j < TIKU_VOLUME_MAX; j++)
                if (owner->entries[j].observer &&
                    same(&owner->entries[j].volume, &volumes.v[i]))
                    found = 1;
            if(found)continue;
            for (int j = 0; j < TIKU_VOLUME_MAX; j++)
                if (!owner->entries[j].observer) {
                    (void)install(owner, &owner->entries[j], &volumes.v[i]);
                    break;
                }
        }
    }
    owner->registered=0;
    for (int i = 0; i < TIKU_VOLUME_MAX; i++)
        if (owner->entries[i].observer) {
            owner->registered++;
            tiku_trk_resource_observer_poll(owner->entries[i].observer, now);
        }
    tiku_trk_resource_observer_poll(owner->status,now);
}
tiku_trk_volume_resources_t *
tiku_trk_volume_resources_open(tiku_resource_registry_t *registry,
                               tiku_trk_volume_request_fn request,
                               void *context)
{
    static const tiku_trk_observation_t status[]={ {"/volumes/status",0,3} };
    if(!registry||!request)return NULL;
    tiku_trk_volume_resources_t *owner = calloc(1, sizeof *owner);
    if (!owner)
        return NULL;
    owner->registry = registry;
    owner->request = request;
    owner->context = context;
    owner->status = tiku_trk_resource_observer_open(
        registry, "native-volumes", "/volumes/status", status, 1, count_items,
        status_capture, owner);
    if (!owner->status) {
        free(owner);
        return NULL;
    }
    tiku_trk_volume_resources_poll(owner, 1);
    return owner;
}
void tiku_trk_volume_resources_close(tiku_trk_volume_resources_t *owner)
{
    if(!owner)return;
    for (int i = 0; i < TIKU_VOLUME_MAX; i++)
        tiku_trk_resource_observer_close(owner->entries[i].observer);
    tiku_trk_resource_observer_close(owner->status);free(owner);
}
