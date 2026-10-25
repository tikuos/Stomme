/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_catalog_settings.c - file-type discovery and staged association
 * saves.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_catalog_settings.h"
#include "tiku_trk_type_settings.h"
#include "tiku_resource_jobs.h"
#include "tiku_state_update.h"
#include "tiku_work.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct catalog_job {
    struct tiku_trk_catalog_settings *owner;
    tiku_trk_associations_t draft;
    tiku_store_update_t *update;
    char id[TIKU_RESOURCE_ID_MAX], path[TIKU_RESOURCE_PATH_MAX];
    void (*completed)(void *, tiku_resource_error_t);
    void *context;
    int running, held;
} catalog_job_t;
struct tiku_trk_catalog_settings {
    tiku_trk_type_settings_t *file_types;
    tiku_store_t *store;
    const tiku_trk_apps_t *apps;
    tiku_trk_types_t types;
    tiku_trk_associations_t accepted;
    tiku_resource_registry_t *registry;
    catalog_job_t *job;
    char (*paths)[TIKU_RESOURCE_PATH_MAX];
    size_t count;
    void (*changed)(void *);
    void *context;
};

int tiku_trk_catalog_settings_path(const char *collection,const char *mime,
    const char *leaf,char out[TIKU_RESOURCE_PATH_MAX])
{
    static const char hex[] = "0123456789abcdef";
    char key[2 * TIKU_TYPE_MAX];
    size_t i, n;
    if (!collection || !mime || !(n = strnlen(mime, TIKU_TYPE_MAX)) ||
        n >= TIKU_TYPE_MAX)
        return -1;
    for (i = 0; i < n; i++) {
        key[2 * i] = hex[(unsigned char)mime[i] >> 4];
        key[2 * i + 1] = hex[(unsigned char)mime[i] & 15];
    }
    key[2 * n] = 0;
    if (snprintf(out, TIKU_RESOURCE_PATH_MAX, "%s/%s%s%s", collection, key,
                 leaf ? "/" : "", leaf ? leaf : "") >= TIKU_RESOURCE_PATH_MAX)
        return -1;
    return tiku_resource_path_valid(out)?0:-1;
}

/** @brief Append an owned typed child to an object and release its temporary
 * value.
 */
static int member(tiku_msg_t *object,const char *key,tiku_msg_t *value)
{
    tiku_msg_t *item=tiku_msg_new(0);
    int ok = item && value && tiku_msg_add_string(item, "key", key) &&
             tiku_msg_add_message(item, "value", value) &&
             tiku_msg_add_message(object, "member", item);
    tiku_msg_free(item);tiku_msg_free(value);return ok;
}

/** @brief Start a typed object or array. */
static tiku_msg_t *container(const char *type)
{
    tiku_msg_t *value=tiku_msg_new(0);
    if (value && !tiku_msg_add_string(value, "type", type)) {
        tiku_msg_free(value);
        value = NULL;
    }
    return value;
}

/** @brief Read the actual first launch candidate using the current preferred
 * handler.
 */
static const char *effective(tiku_trk_catalog_settings_t *owner, int row,
                             char out[TIKU_TRK_APP_NAME_MAX])
{
    tiku_trk_cand_t candidates[TIKU_TRK_APP_MAX];
    const char *type = owner->accepted.row[row].type;
    int count = tiku_trk_launch_candidates(owner->apps, &type, 1, owner->store,
                                           candidates, TIKU_TRK_APP_MAX);
    snprintf(out, TIKU_TRK_APP_NAME_MAX, "%s",
             count ? candidates[0].app->name : "");
    return out;
}

/** @brief Describe a loaded type with its native numeric column IDs and
 * attributes.
 */
static tiku_msg_t *type_value(const tiku_trk_typerec_t *record)
{
    tiku_msg_t *out = container("object"), *columns = container("array"),
               *attrs = container("array");
    int i;
    if(!out || !columns || !attrs)goto fail;
    for(i=0;i<record->ncol;i++){
        tiku_msg_t *v =
            tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, record->col[i]);
        int ok = v && tiku_msg_add_message(columns, "item", v);
        tiku_msg_free(v);
        if (!ok)
            goto fail;
    }
    for(i=0;i<record->nattr;i++){
        tiku_msg_t *v =
            tiku_resource_value_string(TIKU_RESOURCE_TEXT, record->attr[i]);
        int ok = v && tiku_msg_add_message(attrs, "item", v);
        tiku_msg_free(v);
        if (!ok)
            goto fail;
    }
    if (!member(
            out, "mime",
            tiku_resource_value_string(TIKU_RESOURCE_TEXT, record->type)) ||
        !member(
            out, "description",
            tiku_resource_value_string(TIKU_RESOURCE_TEXT, record->desc)) ||
        !member(
            out, "icon",
            tiku_resource_value_string(TIKU_RESOURCE_TEXT, record->icon)) ||
        !member(out, "document",
                tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,
                                            record->document)))
        goto fail;
    {int ok=member(out,"columns",columns);columns=NULL;if(!ok)goto fail;}
    {int ok=member(out,"attributes",attrs);attrs=NULL;if(!ok)goto fail;}
    return out;
fail:tiku_msg_free(columns);tiku_msg_free(attrs);tiku_msg_free(out);return NULL;
}

/** @brief Encode identity and candidate metadata for one association. */
static tiku_msg_t *details(tiku_trk_catalog_settings_t *owner,int row)
{
    tiku_trk_cand_t candidates[TIKU_TRK_APP_MAX];
    const char *type = owner->accepted.row[row].type;
    tiku_msg_t *out=container("object"),*apps=container("array");int i,count;
    if(!out || !apps)goto fail;
    count = tiku_trk_launch_candidates(owner->apps, &type, 1, NULL, candidates,
                                       TIKU_TRK_APP_MAX);
    for(i=0;i<count;i++){
        tiku_msg_t *v = tiku_resource_value_string(TIKU_RESOURCE_TEXT,
                                                   candidates[i].app->name);
        int ok = v && tiku_msg_add_message(apps, "item", v);
        tiku_msg_free(v);
        if (!ok)
            goto fail;
    }
    if (!member(out, "mime",
                tiku_resource_value_string(TIKU_RESOURCE_TEXT, type)) ||
        !member(out, "label",
                tiku_resource_value_string(TIKU_RESOURCE_TEXT,
                                           owner->accepted.row[row].label)))
        goto fail;
    {int ok=member(out,"candidates",apps);apps=NULL;if(!ok)goto fail;}
    return out;
fail:tiku_msg_free(apps);tiku_msg_free(out);return NULL;
}

/** @brief Build one copied native catalog or preferred-handler snapshot. */
static int snapshot(tiku_trk_catalog_settings_t *owner, size_t index,
                    tiku_resource_snapshot_t *out)
{
    tiku_msg_t *v;
    int row;
    char actual[TIKU_TRK_APP_NAME_MAX], persisted[TIKU_TRK_APP_NAME_MAX];
    memset(out, 0, sizeof *out);
    strcpy(out->apply_state, "applied");
    if(index<2)return 0;
    if (index >= 2 + (size_t)owner->accepted.count * 2)
        v = type_value(
            &owner->types.rec[index - 2 - owner->accepted.count * 2]);
    else{
        row=(int)(index-2)/2;
        if(index%2)v=details(owner,row);
        else{
            const char *saved=owner->accepted.row[row].saved;
            if (tiku_trk_launch_preferred_persisted(
                    owner->store, owner->accepted.row[row].type, persisted,
                    sizeof persisted))
                goto fail;
            out->desired=tiku_resource_value_string(TIKU_RESOURCE_TEXT,saved);
            out->persisted =
                tiku_resource_value_string(TIKU_RESOURCE_TEXT, persisted);
            out->applied = tiku_resource_value_string(
                TIKU_RESOURCE_TEXT, effective(owner, row, actual));
            if (strcmp(saved, persisted))
                strcpy(out->apply_state, "pending_persistence");
            else if (saved[0] && strcmp(saved, actual))
                strcpy(out->apply_state, "fallback");
            if(out->desired && out->persisted && out->applied)return 0;
            goto fail;
        }
    }
    out->desired = v;
    out->persisted = v ? tiku_msg_copy(v) : NULL;
    out->applied = v ? tiku_msg_copy(v) : NULL;
    if(out->desired && out->persisted && out->applied)return 0;
fail:tiku_resource_snapshot_clear(out);return -1;
}

/** @brief Find an exact editable association path. */
static int row_for(tiku_trk_catalog_settings_t *owner,const char *path)
{
    int i;
    for (i = 0; i < owner->accepted.count; i++)
        if (!strcmp(owner->paths[2 + 2 * i], path))
            return i;
    return -1;
}

/** @brief Permit Automatic or a related installed application, with no
 * truncation.
 */
static int choice_valid(tiku_trk_catalog_settings_t *owner, int row,
                        const char *choice)
{
    tiku_trk_cand_t candidates[TIKU_TRK_APP_MAX];
    const char *type = owner->accepted.row[row].type;
    int i, count;
    if (!choice ||
        !tiku_resource_utf8_valid(choice, TIKU_TRK_APP_NAME_MAX - 1) ||
        strpbrk(choice, "\r\n"))
        return 0;
    if(!*choice)return 1;
    count = tiku_trk_launch_candidates(owner->apps, &type, 1, NULL, candidates,
                                       TIKU_TRK_APP_MAX);
    for(i=0;i<count;i++)if(!strcmp(candidates[i].app->name,choice))return 1;
    return 0;
}

/** @brief Observe saved handlers from the reconciled live cache without file
 * I/O.
 */
static void refresh(tiku_trk_catalog_settings_t *owner)
{
    int i,changed=0;for(i=0;i<owner->accepted.count;i++){
        char before[TIKU_TRK_APP_NAME_MAX];
        tiku_trk_association_t *row=&owner->accepted.row[i];
        strcpy(before,row->saved);
        tiku_trk_launch_preferred(owner->store, row->type, row->saved,
                                  sizeof row->saved);
        strcpy(row->chosen,row->saved);
        if(strcmp(before,row->saved))changed=1;
    }
    if(changed && owner->changed)owner->changed(owner->context);
}

/** @brief Publish changed snapshots with the combined provider's current
 * revision.
 */
static void publish(tiku_trk_catalog_settings_t *owner)
{
    tiku_resource_caller_t caller={.authenticated=1,.scopes=63};size_t i;
    if(!owner->registry)return;
    strcpy(caller.principal,"00000000000000000000000000000001");
    for(i=2;i<2+(size_t)owner->accepted.count*2;i++){
        tiku_resource_snapshot_t old={0},next={0};
        if (tiku_resource_read(owner->registry, &caller, owner->paths[i],
                               &old) ||
            snapshot(owner, i, &next)) {
            tiku_resource_snapshot_clear(&old);
            tiku_resource_snapshot_clear(&next);
            continue;
        }
        if (!tiku_resource_value_equal(old.desired, next.desired) ||
            !tiku_resource_value_equal(old.persisted, next.persisted) ||
            !tiku_resource_value_equal(old.applied, next.applied) ||
            strcmp(old.apply_state, next.apply_state))
            tiku_resource_publish(owner->registry, &old.stamp, owner->paths[i],
                                  &next, &next.stamp);
        tiku_resource_snapshot_clear(&old);tiku_resource_snapshot_clear(&next);
    }
}

/** @brief Release a worker after collection or a proposal before dispatch. */
static void release(void *opaque)
{
    catalog_job_t *job = opaque;
    if (job->held)
        return;
    tiku_store_update_free(job->update);
    free(job);
}

/** @brief Complete resource and GUI outcomes after refreshing the live cache.
 */
static void complete(catalog_job_t *job,tiku_resource_error_t error)
{
    tiku_trk_catalog_settings_t *owner = job->owner;
    int row;
    refresh(owner);
    owner->job = NULL;
    if(job->id[0] && owner->registry){
        tiku_resource_snapshot_t out={0};row=row_for(owner,job->path);
        if (!error && (row < 0 || snapshot(owner, 2 + 2 * row, &out)))
            error = TIKU_RESOURCE_UNKNOWN_OUTCOME;
        tiku_resource_job_complete(owner->registry, job->id,
                                   error ? NULL : &out, error,
                                   error == TIKU_RESOURCE_UNKNOWN_OUTCOME
                                       ? TIKU_RESOURCE_EFFECT_UNKNOWN
                                   : error ? TIKU_RESOURCE_EFFECT_NONE
                                           : TIKU_RESOURCE_EFFECT_PARTIAL);
        tiku_resource_snapshot_clear(&out);
    }
    publish(owner);if(job->completed)job->completed(job->context,error);
}

/** @brief Reconcile completed storage on the owner thread, retrying
 * allocation failure.
 */
static void done(void *opaque)
{
    catalog_job_t *job = opaque;
    tiku_store_update_result_t result;
    tiku_resource_error_t error;
    if(!job->owner)return;
    result=tiku_store_update_finish(job->update);
    if(result==TIKU_STORE_UPDATE_NO_MEMORY){job->held=1;return;}
    job->held=0;
    error = result == TIKU_STORE_UPDATE_OK         ? TIKU_RESOURCE_OK
            : result == TIKU_STORE_UPDATE_CONFLICT ? TIKU_RESOURCE_STALE
            : result == TIKU_STORE_UPDATE_UNCERTAIN
                ? TIKU_RESOURCE_UNKNOWN_OUTCOME
            : result == TIKU_STORE_UPDATE_CANCELLED
                ? TIKU_RESOURCE_CANCELLED
                : TIKU_RESOURCE_PERSISTENCE_FAILED;
    complete(job,error);
}

/** @brief Persist copied records without accessing live desktop catalogs. */
static void run(void *opaque)
{
    catalog_job_t *job = opaque;
    tiku_store_update_run(job->update);
}

/** @brief Stage changed rows when no other session owner holds the store. */
static void dispatch(tiku_trk_catalog_settings_t *owner)
{
    catalog_job_t *job=owner->job;int i;
    tiku_work_job_t work={.run=run,.done=done,.free=release,.ctx=job};
    if(!job || job->running)return;
    job->update=tiku_store_update_begin(owner->store);
    if (!job->update) {
        if (errno == EBUSY)
            return;
        complete(job, TIKU_RESOURCE_PERSISTENCE_FAILED);
        release(job);
        return;
    }
    for(i=0;i<job->draft.count;i++){
        tiku_trk_association_t *row = &job->draft.row[i];
        char saved[TIKU_TRK_APP_NAME_MAX];
        if(!strcmp(row->saved,row->chosen))continue;
        tiku_trk_launch_preferred(owner->store,row->type,saved,sizeof saved);
        if (strcmp(saved, row->saved)) {
            complete(job, TIKU_RESOURCE_STALE);
            release(job);
            return;
        }
    }
    if (!tiku_trk_associations_save(&job->draft,
                                    tiku_store_update_model(job->update))) {
        complete(job, TIKU_RESOURCE_PERSISTENCE_FAILED);
        release(job);
        return;
    }
    strcpy(work.key,"desktop-association-settings");
    if (tiku_work_post(&work)) {
        complete(job, TIKU_RESOURCE_BUSY);
        release(job);
        return;
    }
    job->running = 1;
}

tiku_trk_catalog_settings_t *tiku_trk_catalog_settings_open(
    tiku_store_t *store, const tiku_trk_apps_t *apps,
    const tiku_trk_types_t *types, void (*changed)(void *), void *context)
{
    tiku_trk_catalog_settings_t *owner;size_t i;
    if(!store || !apps || !types)return NULL;
    owner=calloc(1,sizeof *owner);if(!owner)return NULL;
    owner->store = store;
    owner->apps = apps;
    owner->types = *types;
    owner->changed = changed;
    owner->context = context;
    tiku_trk_associations_load(&owner->accepted,apps,types,store);
    owner->count=2+(size_t)owner->accepted.count*2+(size_t)types->n;
    owner->paths = calloc(owner->count, sizeof *owner->paths);
    if (!owner->paths) {
        free(owner);
        return NULL;
    }
    strcpy(owner->paths[0], "/settings/associations");
    strcpy(owner->paths[1], "/settings/file-types");
    for(i=0;i<(size_t)owner->accepted.count;i++){
        if (tiku_trk_catalog_settings_path(
                owner->paths[0], owner->accepted.row[i].type, "preferred",
                owner->paths[2 + 2 * i]) ||
            tiku_trk_catalog_settings_path(owner->paths[0],
                                           owner->accepted.row[i].type,
                                           "details", owner->paths[3 + 2 * i]))
            goto fail;
    }
    for (i = 0; i < (size_t)types->n; i++)
        if (tiku_trk_catalog_settings_path(
                owner->paths[1], types->rec[i].type, NULL,
                owner->paths[2 + owner->accepted.count * 2 + i]))
            goto fail;
    return owner;
fail:free(owner->paths);free(owner);return NULL;
}

tiku_trk_catalog_settings_t *tiku_trk_catalog_settings_open_with_file(
    tiku_store_t *store, const tiku_trk_apps_t *apps,
    const tiku_trk_types_t *types, const char *path, void (*changed)(void *),
    void *context)
{
    tiku_trk_catalog_settings_t *owner =
        tiku_trk_catalog_settings_open(store, apps, types, changed, context);
    if (!owner) return NULL;
    owner->file_types = tiku_trk_type_settings_open(path, types);
    if (!owner->file_types) {
        tiku_trk_catalog_settings_close(owner);
        return NULL;
    }
    owner->count = 2 + (size_t)owner->accepted.count * 2;
    return owner;
}

size_t tiku_trk_catalog_settings_count(const tiku_trk_catalog_settings_t *owner)
{
    return owner
               ? owner->count + tiku_trk_type_settings_count(owner->file_types)
               : 0;
}

int tiku_trk_catalog_settings_initial(tiku_trk_catalog_settings_t *owner,
                                      tiku_resource_initial_t *initial)
{
    size_t i;for(i=0;i<owner->count;i++){
        int preferred=i>=2 && i<2+(size_t)owner->accepted.count*2 && !(i%2);
        initial[i].spec = (tiku_resource_spec_t){
            .path = owner->paths[i],
            .kind = i < 2       ? TIKU_RESOURCE_COLLECTION
                    : preferred ? TIKU_RESOURCE_SETTING
                                : TIKU_RESOURCE_STATE,
            .schema_version = 1,
            .schema = {.type = preferred ? TIKU_RESOURCE_TEXT
                                         : TIKU_RESOURCE_OBJECT,
                       .text_max = TIKU_TRK_APP_NAME_MAX - 1},
            .read_scopes = TIKU_RESOURCE_READ,
            .change_scopes = preferred ? TIKU_RESOURCE_WRITE : 0,
            .public_read = 1,
            .persistence =
                preferred ? (tiku_store_persistent(owner->store) ? "user"
                                                                 : "session")
                          : "none",
            .controller =
                preferred ? "desktop-associations" : "loaded-type-catalog"};
        if(snapshot(owner,i,&initial[i].initial))return -1;
    }
    return owner->file_types ? tiku_trk_type_settings_initial(
                                   owner->file_types, initial + owner->count)
                             : 0;
}

void tiku_trk_catalog_settings_bind(tiku_trk_catalog_settings_t *owner,
                                    tiku_resource_registry_t *registry)
{
    if (owner) {
        owner->registry = registry;
        tiku_trk_type_settings_bind(owner->file_types, registry);
    }
}

tiku_resource_error_t
tiku_trk_catalog_settings_validate(tiku_trk_catalog_settings_t *owner,
                                   const char *path, const tiku_msg_t *input,
                                   int invoke)
{
    int row;const char *choice,*type;
    if (owner && owner->file_types &&
        !strncmp(path, "/settings/file-types/", 21))
        return tiku_trk_type_settings_validate(owner->file_types, path, input,
                                               invoke);
    if(!owner || (row=row_for(owner,path))<0)return TIKU_RESOURCE_NOT_FOUND;
    if(invoke)return TIKU_RESOURCE_VALUE;
    if(owner->job)return TIKU_RESOURCE_BUSY;
    if(tiku_resource_value_check(input))return TIKU_RESOURCE_VALUE;
    type = tiku_msg_find_string(input, "type", 0);
    choice = tiku_msg_find_string(input, "value", 0);
    return !strcmp(type, "text") && choice_valid(owner, row, choice)
               ? TIKU_RESOURCE_OK
               : TIKU_RESOURCE_VALUE;
}

tiku_resource_error_t
tiku_trk_catalog_settings_start(tiku_trk_catalog_settings_t *owner,
                                const char *id, const char *path,
                                const tiku_msg_t *input, int invoke)
{
    tiku_resource_error_t error;catalog_job_t *job;int row;
    if (owner && owner->file_types &&
        !strncmp(path, "/settings/file-types/", 21))
        return tiku_trk_type_settings_start(owner->file_types, id, path, input,
                                            invoke);
    error=tiku_trk_catalog_settings_validate(owner,path,input,invoke);
    if(error)return error;
    job=calloc(1,sizeof *job);if(!job)return TIKU_RESOURCE_NO_MEMORY;
    job->owner=owner;job->draft=owner->accepted;row=row_for(owner,path);
    strcpy(job->draft.row[row].chosen,
           tiku_msg_find_string(input, "value", 0));
    strcpy(job->id, id);
    strcpy(job->path, path);
    owner->job=job;return TIKU_RESOURCE_PENDING;
}

int tiku_trk_catalog_settings_submit(
    tiku_trk_catalog_settings_t *owner, const tiku_trk_associations_t *draft,
    void (*completed)(void *, tiku_resource_error_t), void *context)
{
    catalog_job_t *job;int i,j;
    if (!owner || !draft || owner->job ||
        draft->count != owner->accepted.count)
        return -1;
    for(i=0;i<draft->count;i++){
        const tiku_trk_association_t *row=&draft->row[i];
        if (!memchr(row->type, 0, sizeof row->type) ||
            !memchr(row->saved, 0, sizeof row->saved) ||
            !memchr(row->chosen, 0, sizeof row->chosen) ||
            strcmp(row->type, owner->accepted.row[i].type))
            return -1;
        if (strcmp(row->saved, row->chosen) &&
            !choice_valid(owner, i, row->chosen))
            return -1;
        for(j=0;j<i;j++)if(!strcmp(row->type,draft->row[j].type))return -1;
    }
    job=calloc(1,sizeof *job);if(!job)return -1;
    job->owner = owner;
    job->draft = *draft;
    job->completed = completed;
    job->context = context;
    owner->job = job;
    return 0;
}

/** @brief Find a loaded association by its exact MIME identity. */
static int row_for_type(tiku_trk_catalog_settings_t *owner, const char *type)
{
    int row;
    for (row = 0; row < owner->accepted.count; row++)
        if (!strcmp(owner->accepted.row[row].type, type)) return row;
    return -1;
}

tiku_resource_error_t
tiku_trk_catalog_settings_prefer_validate(tiku_trk_catalog_settings_t *owner,
                                          const char *const *types, int count,
                                          const char *handler)
{
    int i, row;
    if (!owner || !types || count < 1 || count > TIKU_TRK_APP_TYPES_MAX ||
        !handler)
        return TIKU_RESOURCE_VALUE;
    if (owner->job) return TIKU_RESOURCE_BUSY;
    for (i = 0; i < count; i++) {
        if (!types[i] || strnlen(types[i], TIKU_TYPE_MAX) >= TIKU_TYPE_MAX)
            return TIKU_RESOURCE_VALUE;
        row = row_for_type(owner, types[i]);
        if (row < 0) return TIKU_RESOURCE_NOT_FOUND;
        if (!choice_valid(owner, row, handler)) return TIKU_RESOURCE_VALUE;
    }
    return TIKU_RESOURCE_OK;
}

tiku_resource_error_t tiku_trk_catalog_settings_prefer(
    tiku_trk_catalog_settings_t *owner, const char *const *types, int count,
    const char *handler, void (*completed)(void *, tiku_resource_error_t),
    void *context)
{
    tiku_resource_error_t error = tiku_trk_catalog_settings_prefer_validate(
        owner, types, count, handler);
    catalog_job_t *job;
    int i;
    if (error) return error;
    job = calloc(1, sizeof *job);
    if (!job) return TIKU_RESOURCE_NO_MEMORY;
    refresh(owner);
    job->owner = owner; job->draft = owner->accepted;
    for (i = 0; i < count; i++)
        strcpy(job->draft.row[row_for_type(owner, types[i])].chosen, handler);
    job->completed = completed; job->context = context; owner->job = job;
    return TIKU_RESOURCE_OK;
}

void tiku_trk_catalog_settings_detach(tiku_trk_catalog_settings_t *owner,
                                      void *context)
{
    if (owner && owner->job && owner->job->context == context) {
        owner->job->completed = NULL;
        owner->job->context = NULL;
    }
}

void tiku_trk_catalog_settings_poll(tiku_trk_catalog_settings_t *owner)
{
    if(!owner)return;
    if (owner->job && owner->job->held) {
        catalog_job_t *job = owner->job;
        done(job);
        if (!job->held)
            release(job);
    }
    refresh(owner);publish(owner);dispatch(owner);
    tiku_trk_type_settings_poll(owner->file_types);
}

void tiku_trk_catalog_settings_close(tiku_trk_catalog_settings_t *owner)
{
    struct timespec now;uint64_t deadline=0,current=0;
    if(!owner)return;
    if (!clock_gettime(CLOCK_MONOTONIC, &now))
        deadline = (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000 + 5000;
    while(owner->job && deadline){
        tiku_trk_catalog_settings_poll(owner);tiku_work_collect();
        if(clock_gettime(CLOCK_MONOTONIC,&now))break;
        current = (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
        if (current >= deadline)
            break;
        if(owner->job)usleep(1000);
    }
    if (owner->job) {
        catalog_job_t *job = owner->job;
        job->owner = NULL;
        tiku_store_update_detach(job->update);
        if(!job->running || job->held){job->held=0;release(job);}
    }
    tiku_trk_type_settings_close(owner->file_types);
    free(owner->paths);free(owner);
}
