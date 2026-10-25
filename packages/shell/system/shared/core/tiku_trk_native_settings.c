/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_native_settings.c - typed saved-record catalogs and guarded
 * persistence.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_native_settings.h"
#include "tiku_trk_native_record.h"
#include "tiku_trk_resource_values.h"
#include "tiku_resource_stream.h"
#include "tiku_resource_jobs.h"
#include "tiku_state_attribute.h"
#include "tiku_work.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define NATIVE_ENTRIES_MAX 1800
#define NATIVE_BYTES_MAX 8192

/** @brief Native attribute grammar or an alias to an existing setting owner.
 */
typedef struct {
    const char *path, *attribute, *canonical;
    tiku_trk_native_record_kind_t kind;
} native_family_t;
static const native_family_t families[] = {
    {"/session/records/columns", TIKU_ATTR_COLUMNS, NULL,
     TIKU_TRK_RECORD_COLUMNS},
    {"/session/records/defaults", NULL, "/settings/tracker/window-defaults",
     0},
    {"/session/records/desk-columns", TIKU_ATTR_DESK_COLUMNS, NULL,
     TIKU_TRK_RECORD_COLUMNS},
    {"/session/records/desk-view-state", TIKU_ATTR_DESK_VIEW_STATE, NULL,
     TIKU_TRK_RECORD_VIEW},
    {"/session/records/deskbar-prefs", NULL, "/settings/deskbar/profile", 0},
    {"/session/records/deskbar-prefs-frame", TIKU_ATTR_DESKBAR_PREFS_FRAME,
     NULL, TIKU_TRK_RECORD_POSITION},
    {"/session/records/disks-columns", TIKU_ATTR_DISKS_COLUMNS, NULL,
     TIKU_TRK_RECORD_COLUMNS},
    {"/session/records/disks-view-state", TIKU_ATTR_DISKS_VIEW_STATE, NULL,
     TIKU_TRK_RECORD_VIEW},
    {"/session/records/dock", TIKU_ATTR_DOCK, NULL, TIKU_TRK_RECORD_DOCK},
    {"/session/records/frame", TIKU_ATTR_FRAME, NULL, TIKU_TRK_RECORD_FRAME},
    {"/session/records/original", TIKU_ATTR_ORIGINAL, NULL,
     TIKU_TRK_RECORD_ORIGINAL},
    {"/session/records/pose-info", TIKU_ATTR_POSE_INFO, NULL,
     TIKU_TRK_RECORD_POSE},
    {"/session/records/replicants", TIKU_ATTR_REPLICANTS, NULL,
     TIKU_TRK_RECORD_REPLICANTS},
    {"/session/records/session", TIKU_ATTR_SESSION, NULL,
     TIKU_TRK_RECORD_SESSION},
    {"/session/records/ui-font", NULL, "/settings/ui/font/profile", 0},
    {"/session/records/view-state", TIKU_ATTR_VIEW_STATE, NULL,
     TIKU_TRK_RECORD_VIEW}};

/** @brief One session-local catalog identity and its last complete decoded
 * record.
 */
typedef struct {
    tiku_store_t *store;
    int family, ready, registered;
    tiku_resource_error_t error;
    char node[TIKU_PATH_MAX], base[128], value_path[144], node_path[144];
    tiku_state_attribute_t *capture;
    tiku_msg_t *value, *persisted;
} native_entry_t;
/** @brief One worker-owned native attribute observation. */
typedef struct {
    size_t entry;
    char node[TIKU_PATH_MAX]; int family;
    tiku_state_attribute_t *capture;
    tiku_msg_t *value;
    tiku_resource_error_t error;
} native_read_t;
/** @brief Copied read batch or one guarded write, detached before owner
 * destruction.
 */
typedef struct native_job {
    struct tiku_trk_native_settings *owner;
    native_read_t *reads; size_t count, entry;
    tiku_store_update_t *update;
    tiku_state_attribute_update_t *attribute;
    tiku_msg_t *proposal;
    char id[TIKU_RESOURCE_ID_MAX];
    tiku_store_update_result_t result;
    int write, held;
} native_job_t;
struct tiku_trk_native_settings {
    tiku_resource_registry_t *registry;
    tiku_trk_record_settings_t *records;
    tiku_resource_stamp_t stamp;
    native_entry_t *entries; size_t count;
    native_job_t *job, *queued;
    char alias_job[TIKU_RESOURCE_ID_MAX];
    uint64_t next_poll;
    int dirty[TIKU_TRK_NATIVE_COUNT], overflow, closing;
    void (*saved)(void *, tiku_trk_native_family_t, const char *,
                  const tiku_msg_t *);
    void *saved_context;
    tiku_msg_t *(*capture)(void *); void *capture_context;
};

/** @brief Native public shape for one family. */
static tiku_resource_type_t native_type(int family)
{
    tiku_trk_native_record_kind_t kind = families[family].kind;
    return kind == TIKU_TRK_RECORD_COLUMNS ||
                   kind == TIKU_TRK_RECORD_SESSION ||
                   kind == TIKU_TRK_RECORD_REPLICANTS
               ? TIKU_RESOURCE_ARRAY
           : kind == TIKU_TRK_RECORD_ORIGINAL ? TIKU_RESOURCE_TEXT
                                              : TIKU_RESOURCE_OBJECT;
}

/** @brief Refresh the record provider revision after a collection or job
 * changes it.
 */
static int native_stamp(tiku_trk_native_settings_t *owner)
{
    tiku_resource_caller_t caller = {.authenticated = 1, .scopes = 63};
    tiku_msg_t *description = NULL;
    strcpy(caller.principal,"00000000000000000000000000000001");
    if (tiku_resource_describe(owner->registry, &caller, families[0].path,
                               &description))
        return -1;
    strcpy(owner->stamp.provider,
           tiku_msg_find_string(description, "provider", 0));
    strcpy(owner->stamp.incarnation,
           tiku_msg_find_string(description, "incarnation", 0));
    tiku_resource_counter_parse(
        tiku_msg_find_string(description, "revision", 0),
        &owner->stamp.revision);
    tiku_msg_free(description);
    return 0;
}

/** @brief Produce an empty value of the native public shape. */
static tiku_msg_t *native_empty(int family)
{
    return tiku_trk_native_record_decode(families[family].kind,NULL,0);
}

/** @brief Describe saved values without claiming an immediate change to a
 * live view.
 */
static int native_snapshot(const native_entry_t *entry,
                           tiku_resource_snapshot_t *out)
{
    tiku_msg_t *value = entry->value ? tiku_msg_copy(entry->value)
                                     : native_empty(entry->family);
    if (trk_value_snapshot(
            out, value,
            entry->error    ? "last_good_unavailable"
            : !entry->ready ? "loading"
            : entry->value && entry->persisted &&
                    !tiku_resource_value_equal(entry->value, entry->persisted)
                ? "pending_persistence"
                : "saved_for_next_open"))
        return -1;
    tiku_msg_free(out->persisted);
    out->persisted = entry->persisted ? tiku_msg_copy(entry->persisted)
                                      : native_empty(entry->family);
    if (!out->persisted) { tiku_resource_snapshot_clear(out); return -1; }
    if (entry->family != TIKU_TRK_NATIVE_ORIGINAL) {
        tiku_msg_free(out->applied); out->applied = native_empty(entry->family);
        if (!out->applied) { tiku_resource_snapshot_clear(out); return -1; }
    }
    return 0;
}

/** @brief Fill typed entry and source metadata descriptors with copied
 * snapshots.
 */
static int native_initial(native_entry_t *entry,
                          tiku_resource_initial_t *initial)
{
    tiku_msg_t *node = trk_value_container("object");
    initial[0].spec = (tiku_resource_spec_t){
        .path = entry->value_path,
        .kind = entry->family == TIKU_TRK_NATIVE_ORIGINAL
                    ? TIKU_RESOURCE_STATE
                    : TIKU_RESOURCE_SETTING,
        .schema_version = 1,
        .schema = {.type = native_type(entry->family),
                   .text_max = TIKU_PATH_MAX - 1},
        .read_scopes = TIKU_RESOURCE_READ,
        .change_scopes = TIKU_RESOURCE_WRITE,
        .sensitive = 1,
        .persistence = tiku_store_native_attributes(entry->store) ||
                               tiku_store_persistent(entry->store)
                           ? "user"
                           : "session",
        .controller = "native-records"};
    if (native_snapshot(entry, &initial[0].initial)) {
        tiku_msg_free(node);
        return -1;
    }
    initial[1].spec =
        (tiku_resource_spec_t){.path = entry->node_path,
                               .kind = TIKU_RESOURCE_STATE,
                               .schema_version = 1,
                               .schema = {.type = TIKU_RESOURCE_OBJECT},
                               .read_scopes = TIKU_RESOURCE_READ,
                               .sensitive = 1,
                               .persistence = "none",
                               .controller = "native-records"};
    if (trk_value_member(
            node, "node",
            tiku_resource_value_string(TIKU_RESOURCE_TEXT, entry->node)) ||
        trk_value_member(
            node, "storage",
            tiku_resource_value_string(
                TIKU_RESOURCE_TEXT, tiku_store_native_attributes(entry->store)
                                        ? "native_attributes"
                                        : "record_store"))) {
        tiku_msg_free(node);
        return -1;
    }
    return trk_value_snapshot(&initial[1].initial,node,"observed");
}

/** @brief Recover the current saved alias value through its existing settings
 * provider.
 */
static int native_alias(tiku_trk_native_settings_t *owner, int family,
                        tiku_resource_initial_t *out)
{
    size_t i, count = tiku_trk_record_settings_count();
    tiku_resource_initial_t *initial = calloc(count, sizeof *initial);
    int found = -1;
    if (!initial) return -1;
    if (!tiku_trk_record_settings_initial(owner->records, initial))
        for (i = 0; i < count; i++) {
            if (!strcmp(initial[i].spec.path, families[family].canonical)) {
                *out = initial[i];
                memset(&initial[i].initial, 0, sizeof initial[i].initial);
                out->spec.path = families[family].path;
                out->spec.public_read = 0;
                out->spec.sensitive = 1;
                found = 0;
                break;
            }
        }
    for (i = 0; i < count; i++)
        tiku_resource_snapshot_clear(&initial[i].initial);
    free(initial); return found;
}

/** @brief Publish one complete family catalog after pending mutations finish.
 */
static void native_catalog(tiku_trk_native_settings_t *owner, int family)
{
    tiku_resource_initial_t *initial;
    size_t i, count = 0, used = 0;
    int error = 0;
    for (i = 0; i < owner->count; i++)
        if (owner->entries[i].family == family)
            count++;
    initial = calloc(count ? count*2 : 1,sizeof *initial); if (!initial) return;
    for (i = 0; i < owner->count; i++) if (owner->entries[i].family == family) {
            if (native_initial(&owner->entries[i], initial + used)) {
                error = -1;
                break;
            }
            used += 2;
    }
    if (!error && !native_stamp(owner) &&
        !tiku_resource_collection_replace(owner->registry, &owner->stamp,
                                          families[family].path, initial,
                                          count * 2, &owner->stamp)) {
        owner->dirty[family] = 0;
        for (i = 0; i < owner->count; i++)
            if (owner->entries[i].family == family)
                owner->entries[i].registered = 1;
    }
    for (i = 0; i < count * 2; i++)
        tiku_resource_snapshot_clear(&initial[i].initial);
    free(initial);
}

/** @brief Publish saved values and capacity without disturbing an active
 * record mutation.
 */
static void native_publish(tiku_trk_native_settings_t *owner)
{
    size_t i;
    int family, pending = 0, unavailable = 0;
    tiku_resource_snapshot_t status = {0};
    tiku_msg_t *value;
    if (owner->queued || (owner->job && owner->job->write) ||
        owner->alias_job[0])
        return;
    for (family = 0; family < TIKU_TRK_NATIVE_COUNT; family++) {
        if (families[family].canonical) {
            tiku_resource_initial_t alias = {0};
            if (!native_alias(owner, family, &alias))
                trk_value_publish(owner->registry, families[family].path,
                                  &alias.initial);
            tiku_resource_snapshot_clear(&alias.initial);
        } else if (owner->dirty[family]) native_catalog(owner,family);
    }
    for (i = 0; i < owner->count; i++)
        if (owner->entries[i].registered &&
            !owner->dirty[owner->entries[i].family]) {
            tiku_resource_snapshot_t next = {0};
            if (!native_snapshot(&owner->entries[i], &next))
                trk_value_publish(owner->registry,
                                  owner->entries[i].value_path, &next);
            tiku_resource_snapshot_clear(&next);
        }
    for (family = 0; family < TIKU_TRK_NATIVE_COUNT; family++)
        if (owner->dirty[family])
            pending++;
    for (i = 0; i < owner->count; i++)
        if (owner->entries[i].error)
            unavailable++;
    value = trk_value_container("object");
    if (trk_value_member(value, "entries",
                         tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,
                                                     owner->count)) ||
        trk_value_member(value, "capacity",
                         tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,
                                                     NATIVE_ENTRIES_MAX)) ||
        trk_value_member(value, "overflow",
                         tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,
                                                     owner->overflow)) ||
        trk_value_member(
            value, "pending_families",
            tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, pending)) ||
        trk_value_member(
            value, "unavailable_entries",
            tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, unavailable)) ||
        trk_value_member(
            value, "complete",
            tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,
                                        !owner->overflow && !pending))) {
        tiku_msg_free(value);
        return;
    }
    if (!trk_value_snapshot(&status, value, "observed"))
        trk_value_publish(owner->registry, "/session/records/status", &status);
    tiku_resource_snapshot_clear(&status);
}

/** @brief Convert guarded storage completion to the public resource outcome.
 */
static tiku_resource_error_t native_error(tiku_store_update_result_t result)
{
    return result == TIKU_STORE_UPDATE_OK         ? TIKU_RESOURCE_OK
           : result == TIKU_STORE_UPDATE_CONFLICT ? TIKU_RESOURCE_STALE
           : result == TIKU_STORE_UPDATE_UNCERTAIN
               ? TIKU_RESOURCE_UNKNOWN_OUTCOME
           : result == TIKU_STORE_UPDATE_CANCELLED ? TIKU_RESOURCE_CANCELLED
           : result == TIKU_STORE_UPDATE_NO_MEMORY
               ? TIKU_RESOURCE_NO_MEMORY
               : TIKU_RESOURCE_PERSISTENCE_FAILED;
}

/** @brief Release copied work without dereferencing a detached owner or
 * store.
 */
static void native_release(void *opaque)
{
    native_job_t *job = opaque; size_t i;
    if (job->held) return;
    for (i = 0; i < job->count; i++) {
        tiku_state_attribute_free(job->reads[i].capture);
        tiku_msg_free(job->reads[i].value);
    }
    free(job->reads);
    tiku_store_update_free(job->update);
    tiku_state_attribute_update_free(job->attribute);
    tiku_msg_free(job->proposal);
    free(job);
}

/** @brief Read copied native paths or persist one isolated proposal on the
 * worker.
 */
static void native_run(void *opaque)
{
    native_job_t *job = opaque; size_t i;
    job->result = TIKU_STORE_UPDATE_OK;
    if (job->write) {
        if (job->attribute)
            job->result = tiku_state_attribute_run(job->attribute);
        else { tiku_store_update_run(job->update); return; }
        if (job->result != TIKU_STORE_UPDATE_OK &&
            job->result != TIKU_STORE_UPDATE_UNCERTAIN)
            return;
    }
    for (i = 0; i < job->count; i++) {
        native_read_t *read = &job->reads[i]; size_t length; const void *bytes;
        read->capture = tiku_state_attribute_read(
            read->node, families[read->family].attribute);
        if (!read->capture) {
            read->error = TIKU_RESOURCE_PERSISTENCE_FAILED;
            continue;
        }
        bytes = tiku_state_attribute_bytes(read->capture,&length);
        read->value = tiku_trk_native_record_decode(
            families[read->family].kind, bytes, length);
        if (!read->value) read->error = TIKU_RESOURCE_VALUE;
        if (job->write && (!read->value || !tiku_resource_value_equal(
                                               read->value, job->proposal)))
            job->result = TIKU_STORE_UPDATE_UNCERTAIN;
    }
}

/** @brief Decode cached sidecar records without filesystem I/O. */
static void native_cached(native_entry_t *entry)
{
    unsigned char bytes[NATIVE_BYTES_MAX];
    int length = tiku_state_read(entry->store, entry->node,
                                 families[entry->family].attribute, bytes,
                                 sizeof bytes);
    tiku_msg_t *value = tiku_trk_native_record_decode(
                   families[entry->family].kind, bytes,
                   length < 0 ? 0 : (size_t)length),
               *persisted = NULL;
    entry->ready = 1;
    entry->error = value ? TIKU_RESOURCE_OK : TIKU_RESOURCE_VALUE;
    if (value) { tiku_msg_free(entry->value); entry->value = value; }
    errno = 0;
    length = tiku_store_persisted_read(entry->store, entry->node,
                                       families[entry->family].attribute,
                                       bytes, sizeof bytes);
    if (length >= 0 || errno == ENOENT)
        persisted =
            tiku_trk_native_record_decode(families[entry->family].kind, bytes,
                                          length < 0 ? 0 : (size_t)length);
    if (persisted) {
        tiku_msg_free(entry->persisted);
        entry->persisted = persisted;
    } else
        entry->error = TIKU_RESOURCE_PERSISTENCE_FAILED;
}

static int native_post_write(tiku_trk_native_settings_t *, native_job_t *);

/** @brief Install collected copies, complete the job before publication, then
 * dispatch queued work.
 */
static void native_done(void *opaque)
{
    native_job_t *job = opaque;
    tiku_trk_native_settings_t *owner = job->owner;
    size_t i;
    if (!owner) return;
    if (job->update) {
        job->result = tiku_store_update_finish(job->update);
        if (job->result == TIKU_STORE_UPDATE_NO_MEMORY) {
            job->held = 1;
            return;
        }
        if (job->result == TIKU_STORE_UPDATE_OK)
            native_cached(&owner->entries[job->entry]);
    }
    job->held = 0; owner->job = NULL;
    for (i = 0; i < job->count; i++) {
        native_read_t *read = &job->reads[i];
        native_entry_t *entry = &owner->entries[read->entry];
        if (job->write && job->result != TIKU_STORE_UPDATE_OK &&
            job->result != TIKU_STORE_UPDATE_UNCERTAIN)
            continue;
        entry->error = read->error;
        if (read->capture) {
            tiku_state_attribute_free(entry->capture);
            entry->capture = read->capture;
            read->capture = NULL;
            entry->ready = 1;
        }
        if (read->value) {
            tiku_msg_t *persisted = tiku_msg_copy(read->value);
            if (!persisted) entry->error = TIKU_RESOURCE_NO_MEMORY;
            else {
                tiku_msg_free(entry->persisted); entry->persisted = persisted;
                tiku_msg_free(entry->value);
                entry->value = read->value;
                read->value = NULL;
            }
        }
    }
    if (job->write) {
        native_entry_t *entry = &owner->entries[job->entry];
        if (job->result == TIKU_STORE_UPDATE_OK && owner->saved &&
            entry->persisted)
            owner->saved(owner->saved_context, entry->family, entry->node,
                         entry->persisted);
        tiku_resource_error_t error = native_error(job->result);
        tiku_resource_snapshot_t out = {0};
        if (!error && native_snapshot(&owner->entries[job->entry], &out))
            error = TIKU_RESOURCE_UNKNOWN_OUTCOME;
        tiku_resource_job_complete(owner->registry, job->id,
                                   error ? NULL : &out, error,
                                   error == TIKU_RESOURCE_UNKNOWN_OUTCOME
                                       ? TIKU_RESOURCE_EFFECT_UNKNOWN
                                   : error ? TIKU_RESOURCE_EFFECT_NONE
                                           : TIKU_RESOURCE_EFFECT_PARTIAL);
        tiku_resource_snapshot_clear(&out);
    }
    if (owner->queued) {
        native_job_t *next = owner->queued; owner->queued = NULL;
        if (native_post_write(owner,next)) {
            tiku_resource_job_complete(owner->registry, next->id, NULL,
                                       TIKU_RESOURCE_BUSY,
                                       TIKU_RESOURCE_EFFECT_NONE);
            native_release(next);
        }
    }
    native_publish(owner);
}

/** @brief Post copied native work under a serialized worker key. */
static int native_post_write(tiku_trk_native_settings_t *owner,
                             native_job_t *job)
{
    tiku_work_job_t work = {.run = native_run,
                            .done = native_done,
                            .free = native_release,
                            .ctx = job};
    job->owner = owner; strcpy(work.key,"desktop-native-records");
    if (tiku_work_post(&work)) return -1;
    owner->job = job; return 0;
}

/** @brief Match an alias or one existing opaque entry value path. */
static int native_find(tiku_trk_native_settings_t *owner, const char *path,
                       int *alias)
{
    size_t i; *alias = -1;
    for (i = 0; i < TIKU_TRK_NATIVE_COUNT; i++)
        if (families[i].canonical && !strcmp(path, families[i].path)) {
            *alias = (int)i;
            return -1;
        }
    for (i = 0; i < owner->count; i++)
        if (!strcmp(path, owner->entries[i].value_path))
            return (int)i;
    return -1;
}

/** @brief Validate exact typed fields without persistence or mutation. */
static tiku_resource_error_t native_validate(void *opaque, const char *path,
                                             const tiku_msg_t *value,
                                             int invoke)
{
    tiku_trk_native_settings_t *owner = opaque;
    int alias, index = native_find(owner, path, &alias);
    unsigned char bytes[NATIVE_BYTES_MAX]; size_t length;
    if (owner->queued || (owner->job && owner->job->write) ||
        owner->alias_job[0])
        return TIKU_RESOURCE_BUSY;
    if (!strcmp(path, "/session/records/capture") ||
        !strcmp(path, "/session/records/clear")) {
        const char *type = tiku_msg_find_string(value,"type",0);
        if (!invoke || !type || strcmp(type, "object") ||
            tiku_resource_value_check(value) ||
            tiku_msg_count(value, "member"))
            return TIKU_RESOURCE_VALUE;
        return !strcmp(path, "/session/records/capture") && !owner->capture
                   ? TIKU_RESOURCE_UNAVAILABLE
                   : TIKU_RESOURCE_OK;
    }
    if (alias >= 0)
        return tiku_trk_record_settings_validate(
            owner->records, families[alias].canonical, value, invoke);
    if (index < 0) return TIKU_RESOURCE_NOT_FOUND;
    if (invoke || owner->entries[index].family == TIKU_TRK_NATIVE_ORIGINAL ||
        !tiku_store_writable(owner->entries[index].store))
        return TIKU_RESOURCE_READ_ONLY;
    if (!owner->entries[index].ready ||
        (tiku_store_native_attributes(owner->entries[index].store) &&
         !owner->entries[index].capture))
        return TIKU_RESOURCE_BUSY;
    if (!tiku_store_native_attributes(owner->entries[index].store)) {
        native_entry_t *entry = &owner->entries[index];
        unsigned char current[NATIVE_BYTES_MAX];
        int size = tiku_state_read(entry->store, entry->node,
                                   families[entry->family].attribute, current,
                                   sizeof current);
        tiku_msg_t *decoded = tiku_trk_native_record_decode(
            families[entry->family].kind, current,
            size < 0 ? 0 : (size_t)size);
        int same = decoded && entry->value &&
                   tiku_resource_value_equal(decoded, entry->value);
        tiku_msg_free(decoded); if (!same) return TIKU_RESOURCE_STALE;
    }
    return tiku_trk_native_record_encode(
               families[owner->entries[index].family].kind, value, bytes,
               sizeof bytes, &length)
               ? TIKU_RESOURCE_VALUE
               : TIKU_RESOURCE_OK;
}

/** @brief Copy a typed proposal and complete native baseline before queueing
 * persistence.
 */
static tiku_resource_error_t native_start(void *opaque, const char *id,
                                          const char *path,
                                          const tiku_msg_t *value, int invoke,
                                          tiku_resource_snapshot_t *out)
{
    tiku_trk_native_settings_t *owner = opaque;
    tiku_resource_error_t error = native_validate(owner, path, value, invoke);
    native_job_t *job;
    native_entry_t *entry;
    int alias, index;
    unsigned char bytes[NATIVE_BYTES_MAX];
    size_t length;
    (void)out;
    if (error) return error;
    if (invoke) {
        size_t i;
        tiku_msg_t *proposal = !strcmp(path, "/session/records/capture")
                                   ? owner->capture(owner->capture_context)
                                   : trk_value_container("array");
        if (!proposal) return TIKU_RESOURCE_UNAVAILABLE;
        for (i = 0; i < owner->count; i++)
            if (owner->entries[i].family == TIKU_TRK_NATIVE_SESSION &&
                !strcmp(owner->entries[i].node, TIKU_KEY_SESSION))
                break;
        error = i == owner->count
                    ? TIKU_RESOURCE_NOT_FOUND
                    : native_start(owner, id, owner->entries[i].value_path,
                                   proposal, 0, out);
        tiku_msg_free(proposal); return error;
    }
    index = native_find(owner,path,&alias);
    if (alias >= 0) {
        error = tiku_trk_record_settings_start(
            owner->records, id, families[alias].canonical, value, invoke);
        if (error == TIKU_RESOURCE_PENDING) strcpy(owner->alias_job,id);
        return error;
    }
    entry = &owner->entries[index];
    if (tiku_trk_native_record_encode(families[entry->family].kind, value,
                                      bytes, sizeof bytes, &length))
        return TIKU_RESOURCE_VALUE;
    job = calloc(1,sizeof *job); if (!job) return TIKU_RESOURCE_NO_MEMORY;
    job->entry = (size_t)index;
    job->write = 1;
    strcpy(job->id, id);
    job->proposal = tiku_msg_copy(value);
    if (!job->proposal) { native_release(job); return TIKU_RESOURCE_NO_MEMORY; }
    if (tiku_store_native_attributes(entry->store)) {
        job->attribute = tiku_state_attribute_prepare(entry->capture, bytes,
                                                      length, length != 0);
        job->reads = calloc(1, sizeof *job->reads);
        job->count = job->reads ? 1 : 0;
        if (!job->attribute || !job->reads) {
            native_release(job);
            return TIKU_RESOURCE_NO_MEMORY;
        }
        job->reads[0].entry = job->entry;
        job->reads[0].family = entry->family;
        strcpy(job->reads[0].node, entry->node);
    } else {
        job->update = tiku_store_update_begin(entry->store);
        if (!job->update) {
            int cause = errno; native_release(job);
            return cause == EBUSY    ? TIKU_RESOURCE_BUSY
                   : cause == ENOMEM ? TIKU_RESOURCE_NO_MEMORY
                   : cause == EACCES ? TIKU_RESOURCE_READ_ONLY
                                     : TIKU_RESOURCE_PERSISTENCE_FAILED;
        }
        if (tiku_state_write(tiku_store_update_model(job->update), entry->node,
                             families[entry->family].attribute, bytes,
                             length)) {
            native_release(job); return TIKU_RESOURCE_PERSISTENCE_FAILED;
        }
    }
    if (owner->job) {
        owner->queued = job;
        job->owner = owner;
        return TIKU_RESOURCE_PENDING;
    }
    if (native_post_write(owner, job)) {
        native_release(job);
        return TIKU_RESOURCE_BUSY;
    }
    return TIKU_RESOURCE_PENDING;
}

tiku_trk_native_settings_t *
tiku_trk_native_settings_open(tiku_resource_registry_t *registry,
                              tiku_trk_record_settings_t *records)
{
    tiku_trk_native_settings_t *owner;
    tiku_resource_initial_t initial[TIKU_TRK_NATIVE_COUNT + 3] = {0};
    int i, error = 0;
    tiku_resource_async_controller_t controller = {.validate = native_validate,
                                                   .start = native_start,
                                                   .execution_ms = 300000};
    if (!registry || !records) return NULL;
    owner = calloc(1,sizeof *owner); if (!owner) return NULL;
    owner->registry = registry;
    owner->records = records;
    owner->entries = calloc(NATIVE_ENTRIES_MAX, sizeof *owner->entries);
    if (!owner->entries) { free(owner); return NULL; }
    for (i = 0; i < TIKU_TRK_NATIVE_COUNT; i++) {
        if (families[i].canonical) {
            if (native_alias(owner, i, &initial[i])) {
                error = -1;
                break;
            }
        } else {
            initial[i].spec =
                (tiku_resource_spec_t){.path = families[i].path,
                                       .kind = TIKU_RESOURCE_COLLECTION,
                                       .schema_version = 1,
                                       .schema = {.type = TIKU_RESOURCE_NULL},
                                       .read_scopes = TIKU_RESOURCE_READ,
                                       .sensitive = 1,
                                       .persistence = "none",
                                       .controller = "native-records"};
            if (trk_value_snapshot(&initial[i].initial,
                                   trk_value_container("null"), "observed")) {
                error = -1;
                break;
            }
        }
    }
    initial[TIKU_TRK_NATIVE_COUNT].spec =
        (tiku_resource_spec_t){.path = "/session/records/status",
                               .kind = TIKU_RESOURCE_STATE,
                               .schema_version = 1,
                               .schema = {.type = TIKU_RESOURCE_OBJECT},
                               .read_scopes = TIKU_RESOURCE_READ,
                               .sensitive = 1,
                               .persistence = "none",
                               .controller = "native-records"};
    if (trk_value_snapshot(&initial[TIKU_TRK_NATIVE_COUNT].initial,
                           trk_value_container("object"), "observed"))
        error = -1;
    for (i = 0; i < 2; i++) {
        initial[TIKU_TRK_NATIVE_COUNT + 1 + i].spec = (tiku_resource_spec_t){
            .path = i ? "/session/records/clear" : "/session/records/capture",
            .kind = TIKU_RESOURCE_ACTION,
            .schema_version = 1,
            .schema = {.type = TIKU_RESOURCE_OBJECT},
            .read_scopes = TIKU_RESOURCE_READ,
            .change_scopes = TIKU_RESOURCE_INVOKE,
            .sensitive = 1,
            .persistence = "none",
            .controller = "native-records"};
        if (trk_value_snapshot(&initial[TIKU_TRK_NATIVE_COUNT + 1 + i].initial,
                               trk_value_container("null"), "observed"))
            error = -1;
    }
    if (!error)
        error = tiku_resource_register_async(
            registry, "native-records", "/session/records", NULL, initial,
            TIKU_TRK_NATIVE_COUNT + 3, &controller, owner, &owner->stamp);
    for (i = 0; i < TIKU_TRK_NATIVE_COUNT + 3; i++)
        tiku_resource_snapshot_clear(&initial[i].initial);
    if (error) { free(owner->entries); free(owner); return NULL; }
    return owner;
}

void tiku_trk_native_settings_bind_saved(
    tiku_trk_native_settings_t *owner,
    void (*saved)(void *, tiku_trk_native_family_t, const char *,
                  const tiku_msg_t *),
    void *context)
{
    if (owner) { owner->saved = saved; owner->saved_context = context; }
}

void tiku_trk_native_settings_bind_capture(tiku_trk_native_settings_t *owner,
                                           tiku_msg_t *(*capture)(void *),
                                           void *context)
{
    if (owner) { owner->capture = capture; owner->capture_context = context; }
}

int tiku_trk_native_settings_add(tiku_trk_native_settings_t *owner,
                                 tiku_trk_native_family_t family,
                                 tiku_store_t *store, const char *node)
{
    size_t i; native_entry_t *entry; char id[TIKU_RESOURCE_ID_MAX];
    if (!owner || !store || !node || !*node || strlen(node) >= TIKU_PATH_MAX ||
        (unsigned)family >= TIKU_TRK_NATIVE_COUNT ||
        families[family].canonical)
        return -1;
    for (i = 0; i < owner->count; i++)
        if (owner->entries[i].family == (int)family &&
            owner->entries[i].store == store &&
            !strcmp(owner->entries[i].node, node))
            return 0;
    if (owner->count == NATIVE_ENTRIES_MAX) { owner->overflow = 1; return -1; }
    if (tiku_resource_new_id(id)) return -1;
    entry = &owner->entries[owner->count++];
    entry->store = store;
    entry->family = family;
    strcpy(entry->node, node);
    snprintf(entry->base,sizeof entry->base,"%s/%s",families[family].path,id);
    snprintf(entry->value_path, sizeof entry->value_path, "%s/value",
             entry->base);
    snprintf(entry->node_path, sizeof entry->node_path, "%s/node",
             entry->base);
    owner->dirty[family] = 1; return 0;
}

void tiku_trk_native_settings_poll(tiku_trk_native_settings_t *owner,
                                   uint64_t now)
{
    size_t i, count = 0; native_job_t *job;
    if (!owner) return;
    if (owner->job && owner->job->held) {
        job = owner->job;
        native_done(job);
        if (!job->held)
            native_release(job);
        return;
    }
    if (owner->alias_job[0]) {
        tiku_resource_caller_t caller = {.authenticated = 1, .scopes = 63};
        tiku_resource_job_info_t info = {0};
        strcpy(caller.principal,"00000000000000000000000000000001");
        if (tiku_resource_job_status(owner->registry, &caller,
                                     owner->alias_job, &info) ||
            (info.state != TIKU_RESOURCE_JOB_QUEUED &&
             info.state != TIKU_RESOURCE_JOB_RUNNING))
            owner->alias_job[0] = 0;
        tiku_resource_job_info_clear(&info); if (owner->alias_job[0]) return;
    }
    if (owner->closing || owner->job || now < owner->next_poll) return;
    for (i = 0; i < owner->count; i++) {
        if (tiku_store_native_attributes(owner->entries[i].store)) count++;
        else native_cached(&owner->entries[i]);
    }
    native_publish(owner); owner->next_poll = now + 1000;
    if (!count) return;
    job = calloc(1,sizeof *job); if (!job) return;
    job->reads = calloc(count, sizeof *job->reads);
    if (!job->reads) {
        free(job);
        return;
    }
    for (i = 0; i < owner->count; i++)
        if (tiku_store_native_attributes(owner->entries[i].store)) {
            native_read_t *read = &job->reads[job->count++];
            read->entry = i;
            read->family = owner->entries[i].family;
            strcpy(read->node, owner->entries[i].node);
        }
    if (native_post_write(owner,job)) native_release(job);
}

void tiku_trk_native_settings_close(tiku_trk_native_settings_t *owner)
{
    struct timespec now; uint64_t deadline = 0, tick; size_t i;
    if (!owner) return;
    owner->closing = 1;
    if (!clock_gettime(CLOCK_MONOTONIC, &now))
        deadline = (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000 + 5000;
    while ((owner->job || owner->alias_job[0]) && deadline &&
           !clock_gettime(CLOCK_MONOTONIC, &now)) {
        tick = (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
        if (tick >= deadline)
            break;
        tiku_work_collect();
        tiku_trk_native_settings_poll(owner, tick);
        if (owner->job)
            usleep(1000);
    }
    if (owner->job) {
        native_job_t *job = owner->job;
        job->owner = NULL;
        tiku_store_update_detach(job->update);
        if (job->held) { job->held = 0; native_release(job); }
    }
    if (owner->queued) native_release(owner->queued);
    tiku_resource_unregister(owner->registry,&owner->stamp);
    for (i = 0; i < owner->count; i++) {
        tiku_state_attribute_free(owner->entries[i].capture);
        tiku_msg_free(owner->entries[i].value);
        tiku_msg_free(owner->entries[i].persisted);
    }
    free(owner->entries); free(owner);
}
