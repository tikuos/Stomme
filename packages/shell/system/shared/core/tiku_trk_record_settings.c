/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_record_settings.c - Deskbar, Fonts and folder-default persistence
 * and resources.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_record_settings.h"
#include "tiku_trk_frame.h"
#include "tiku_state_update.h"
#include "tiku_resource_jobs.h"
#include "tiku_work.h"
#include "tiku_font.h"
#include <stddef.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/** @brief Metadata for one existing Deskbar setting. */
typedef struct {
    const char *key;
    size_t offset;
    tiku_resource_type_t type;
    int low, high, count;
} record_field_t;
#define FIELD(name, type, low, high, count)                                   \
    {#name, offsetof(tiku_trk_deskbar_settings_t, name), type, low, high,     \
     count}
static const record_field_t fields[] = {
    FIELD(sort_apps, TIKU_RESOURCE_BOOLEAN, 0, 1, 1),
    FIELD(tracker_first, TIKU_RESOURCE_BOOLEAN, 0, 1, 1),
    FIELD(show_expander, TIKU_RESOURCE_BOOLEAN, 0, 1, 1),
    FIELD(expand_new_apps, TIKU_RESOURCE_BOOLEAN, 0, 1, 1),
    FIELD(hide_app_names, TIKU_RESOURCE_BOOLEAN, 0, 1, 1),
    FIELD(icon_size, TIKU_RESOURCE_INTEGER, TIKU_TRK_DB_ICON_MIN,
          TIKU_TRK_DB_ICON_MAX, 1),
    FIELD(recent_enabled, TIKU_RESOURCE_BOOLEAN, 0, 1, 3),
    FIELD(recent_count, TIKU_RESOURCE_INTEGER, 0, TIKU_TRK_DB_RECENT_LIMIT, 3),
    FIELD(always_on_top, TIKU_RESOURCE_BOOLEAN, 0, 1, 1),
    FIELD(auto_raise, TIKU_RESOURCE_BOOLEAN, 0, 1, 1),
    FIELD(auto_hide, TIKU_RESOURCE_BOOLEAN, 0, 1, 1),
    FIELD(show_clock, TIKU_RESOURCE_BOOLEAN, 0, 1, 1),
    FIELD(show_seconds, TIKU_RESOURCE_BOOLEAN, 0, 1, 1),
    FIELD(show_day_of_week, TIKU_RESOURCE_BOOLEAN, 0, 1, 1),
    FIELD(show_timezone, TIKU_RESOURCE_BOOLEAN, 0, 1, 1),
    FIELD(font_size, TIKU_RESOURCE_INTEGER, TIKU_TRK_FONT_MIN,
          TIKU_TRK_FONT_MAX, 1),
    FIELD(font_family, TIKU_RESOURCE_INTEGER, 0, 0, 1),
    FIELD(show_replicants, TIKU_RESOURCE_BOOLEAN, 0, 1, 1)};
enum {
    BAR_FIELDS = sizeof fields / sizeof fields[0],
    FONT_FAMILY = BAR_FIELDS,
    FONT_SIZE,
    FONT_DROPPED,
    BAR_PROFILE,
    FONT_PROFILE,
    WINDOW_DEFAULTS,
    RECORD_COUNT
};

/** @brief One complete candidate model and its isolated worker update. */
typedef struct record_job {
    struct tiku_trk_record_settings *owner;
    tiku_store_update_t *update;
    tiku_trk_deskbar_settings_t bar, before_bar;
    tiku_trk_fontprefs_values_t font, before_font;
    tiku_trk_frame_t defaults, before_defaults;
    int have_defaults, had_defaults;
    char id[TIKU_RESOURCE_ID_MAX];
    int index, write, held;
} record_job_t;
struct tiku_trk_record_settings {
    tiku_store_t *store;
    tiku_trk_deskbar_prefs_t *bar;
    tiku_trk_fontprefs_t *font;
    tiku_trk_deskbar_settings_t accepted_bar;
    tiku_trk_fontprefs_values_t accepted_font, applied_font;
    tiku_resource_registry_t *registry;
    record_job_t *job, *queued;
    char paths[RECORD_COUNT][96];
    void (*changed)(void *); void *context;
    uint64_t next_poll;
    int resync;
    tiku_resource_error_t last_error;
};

/** @brief Encode the native fallback frame; an empty object means none. */
static tiku_msg_t *default_value(const tiku_trk_frame_t *frame, int present)
{
    static const char *const keys[] = {"x", "y", "width", "height"};
    const int32_t values[] = {frame->x, frame->y, frame->w, frame->h};
    tiku_msg_t *out = tiku_msg_new(0);
    int i;
    if (!out || !tiku_msg_add_string(out, "type", "object")) goto fail;
    for (i = 0; present && i < 4; i++) {
        tiku_msg_t *entry = tiku_msg_new(0),
                   *v = tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,
                                                    values[i]);
        int ok = entry && v && tiku_msg_add_string(entry, "key", keys[i]) &&
                 tiku_msg_add_message(entry, "value", v) &&
                 tiku_msg_add_message(out, "member", entry);
        tiku_msg_free(entry); tiku_msg_free(v); if (!ok) goto fail;
    }
    return out;
fail:
    tiku_msg_free(out); return NULL;
}

/** @brief Decode exactly four native integer fields or an empty clearing
 * object.
 */
static int default_replace(tiku_trk_frame_t *out, int *present,
                           const tiku_msg_t *input)
{
    static const char *const keys[] = {"x", "y", "width", "height"};
    int32_t fields[4] = {0};
    const char *type;
    int i, count, seen = 0;
    if (tiku_resource_value_check(input)) return -1;
    type = tiku_msg_find_string(input, "type", 0);
    count = tiku_msg_count(input, "member");
    if (strcmp(type, "object") || (count && count != 4)) return -1;
    for (i = 0; i < count; i++) {
        tiku_msg_t *entry = tiku_msg_find_message(input, "member", i),
                   *v = tiku_msg_find_message(entry, "value", 0);
        const char *key = tiku_msg_find_string(entry, "key", 0),
                   *kind = tiku_msg_find_string(v, "type", 0);
        int k; int64_t n;
        if (!entry || !v || !key || !kind) {
            tiku_msg_free(v);
            tiku_msg_free(entry);
            return -1;
        }
        for (k = 0; k < 4 && strcmp(key, keys[k]); k++) {}
        if (k == 4 || (seen & (1 << k)) || !kind || strcmp(kind, "integer") ||
            !tiku_msg_find_int64(v, "value", 0, &n) || n < INT32_MIN ||
            n > INT32_MAX) {
            tiku_msg_free(v); tiku_msg_free(entry); return -1;
        }
        seen |= 1 << k;
        fields[k] = (int32_t)n;
        tiku_msg_free(v);
        tiku_msg_free(entry);
    }
    if (count &&
        (fields[2] < TIKU_TRK_FRAME_MIN_W || fields[3] < TIKU_TRK_FRAME_MIN_H))
        return -1;
    *out = (tiku_trk_frame_t){fields[0], fields[1], fields[2], fields[3]};
    *present = count != 0;
    return 0;
}

/** @brief Compare a copied default with the cache before dispatching a queued
 * save.
 */
static int default_current(tiku_trk_record_settings_t *owner,
                           const record_job_t *job)
{
    tiku_trk_frame_t frame = {0};
    int present = tiku_trk_frame_default(owner->store, &frame);
    return present == job->had_defaults &&
           (!present || (frame.x == job->before_defaults.x &&
                         frame.y == job->before_defaults.y &&
                         frame.w == job->before_defaults.w &&
                         frame.h == job->before_defaults.h));
}

/** @brief Observe cached defaults separately from their last known saved
 * record.
 */
static int default_snapshot(tiku_trk_record_settings_t *owner,
                            tiku_resource_snapshot_t *out)
{
    tiku_trk_frame_t live = {0}, saved = {0};
    int present = tiku_trk_frame_default(owner->store, &live);
    int persisted =
        tiku_store_persisted_read(owner->store, TIKU_KEY_SESSION,
                                  TIKU_ATTR_DEFAULTS, &saved, sizeof saved);
    persisted = persisted == (int)sizeof saved &&
                saved.w >= TIKU_TRK_FRAME_MIN_W &&
                saved.h >= TIKU_TRK_FRAME_MIN_H;
    out->desired = default_value(&live, present);
    out->persisted = default_value(&saved, persisted);
    out->applied = default_value(&live, present);
    if (!out->desired || !out->persisted || !out->applied) {
        tiku_resource_snapshot_clear(out);
        return -1;
    }
    strcpy(out->apply_state,
           tiku_resource_value_equal(out->desired, out->persisted)
               ? "applied"
               : "pending_persistence");
    return 0;
}

/** @brief Compare native model fields without reading structure padding. */
static int models_same(const tiku_trk_deskbar_settings_t *a,
                       const tiku_trk_fontprefs_values_t *af,
                       const tiku_trk_deskbar_settings_t *b,
                       const tiku_trk_fontprefs_values_t *bf)
{
    int i,j;
    if (af->family != bf->family || af->size != bf->size ||
        strcmp(af->dropped, bf->dropped))
        return 0;
    for(i=0;i<BAR_FIELDS;i++){
        const int *av = (const int *)((const unsigned char *)a +
                                      fields[i].offset),
                  *bv = (const int *)((const unsigned char *)b +
                                      fields[i].offset);
        for(j=0;j<fields[i].count;j++)if(av[j]!=bv[j])return 0;
    }
    return 1;
}

/** @brief Index one exact resource path. */
static int index_for(tiku_trk_record_settings_t *owner, const char *path)
{
    int i; for(i=0;i<RECORD_COUNT;i++)if(!strcmp(owner->paths[i],path))return i;
    return -1;
}

/** @brief Encode one scalar or fixed-length array from the Deskbar's native
 * fields.
 */
static tiku_msg_t *field_value(const tiku_trk_deskbar_settings_t *bar,
                               int index)
{
    const record_field_t *field = &fields[index];
    const int *values =
        (const int *)((const unsigned char *)bar + field->offset);
    tiku_msg_t *result; int i;
    if(field->count==1)return tiku_resource_value_integer(field->type,*values);
    result = tiku_msg_new(0);
    if (!result || !tiku_msg_add_string(result, "type", "array"))
        goto fail;
    for(i=0;i<field->count;i++){
        tiku_msg_t *item=tiku_resource_value_integer(field->type,values[i]);
        int ok = item && tiku_msg_add_message(result, "item", item);
        tiku_msg_free(item);
        if (!ok)
            goto fail;
    }
    return result;
fail:tiku_msg_free(result);return NULL;
}

/** @brief Encode a field, font alias or complete owner profile. */
static tiku_msg_t *value(const tiku_trk_deskbar_settings_t *bar,
                         const tiku_trk_fontprefs_values_t *font, int index)
{
    tiku_msg_t *result;int begin,end,i;
    if(index<BAR_FIELDS)return field_value(bar,index);
    if (index == FONT_FAMILY)
        return tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,
                                           font->family);
    if (index == FONT_SIZE)
        return tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, font->size);
    if (index == FONT_DROPPED)
        return tiku_resource_value_string(TIKU_RESOURCE_TEXT, font->dropped);
    begin = index == BAR_PROFILE ? 0 : FONT_FAMILY;
    end = index == BAR_PROFILE ? BAR_FIELDS : FONT_DROPPED + 1;
    result = tiku_msg_new(0);
    if (!result || !tiku_msg_add_string(result, "type", "object"))
        goto fail;
    for(i=begin;i<end;i++){
        const char *key = i < BAR_FIELDS     ? fields[i].key
                          : i == FONT_FAMILY ? "family"
                          : i == FONT_SIZE   ? "size"
                                             : "dropped";
        tiku_msg_t *member=tiku_msg_new(0),*child=value(bar,font,i);
        int ok = member && child && tiku_msg_add_string(member, "key", key) &&
                 tiku_msg_add_message(member, "value", child) &&
                 tiku_msg_add_message(result, "member", member);
        tiku_msg_free(member);tiku_msg_free(child);if(!ok)goto fail;
    }
    return result;
fail:tiku_msg_free(result);return NULL;
}

/** @brief Decode a declared scalar or array, enforcing each existing numeric
 * range.
 */
static int set_field(tiku_trk_deskbar_settings_t *bar, int index,
                     const tiku_msg_t *input)
{
    const record_field_t *field = &fields[index];
    int i, *out = (int *)((unsigned char *)bar + field->offset);
    const char *type=tiku_msg_find_string(input,"type",0);
    if (!type ||
        strcmp(type, field->count == 1 ? tiku_resource_type_name(field->type)
                                       : "array") ||
        (field->count > 1 && tiku_msg_count(input, "item") != field->count))
        return -1;
    for(i=0;i<field->count;i++){
        tiku_msg_t *child =
            field->count == 1 ? NULL : tiku_msg_find_message(input, "item", i);
        const tiku_msg_t *scalar=child?child:input;int boolean;int64_t number;
        int ok = field->type == TIKU_RESOURCE_BOOLEAN
                     ? tiku_msg_find_bool(scalar, "value", 0, &boolean)
                     : tiku_msg_find_int64(scalar, "value", 0, &number);
        if(field->type==TIKU_RESOURCE_BOOLEAN)number=ok?boolean:0;
        if (!ok || number < field->low ||
            number >
                (index == 16 ? tiku_font_family_count() - 1 : field->high)) {
            tiku_msg_free(child);
            return -1;
        }
        out[i]=(int)number;tiku_msg_free(child);
    }
    return 0;
}

/** @brief Apply a scalar, array or profile to copied native models. */
static int replace(tiku_trk_deskbar_settings_t *bar,
                   tiku_trk_fontprefs_values_t *font, int index,
                   const tiku_msg_t *input)
{
    tiku_msg_t *expected,*actual;int equal;
    if(tiku_resource_value_check(input))return -1;
    if(index==BAR_PROFILE || index==FONT_PROFILE){
        int begin = index == BAR_PROFILE ? 0 : FONT_FAMILY,
            end = index == BAR_PROFILE ? BAR_FIELDS : FONT_DROPPED + 1, i;
        const char *type=tiku_msg_find_string(input,"type",0);
        if (strcmp(type, "object") ||
            tiku_msg_count(input, "member") != end - begin)
            return -1;
        for(i=begin;i<end;i++){
            const char *key = i < BAR_FIELDS     ? fields[i].key
                              : i == FONT_FAMILY ? "family"
                              : i == FONT_SIZE   ? "size"
                                                 : "dropped";
            int j, found = 0;
            for(j=0;j<end-begin;j++){
                tiku_msg_t *member = tiku_msg_find_message(input, "member", j),
                           *child = tiku_msg_find_message(member, "value", 0);
                int match=!strcmp(tiku_msg_find_string(member,"key",0),key);
                if (match) {
                    found = 1;
                    if (replace(bar, font, i, child)) {
                        tiku_msg_free(member);
                        tiku_msg_free(child);
                        return -1;
                    }
                }
                tiku_msg_free(member);tiku_msg_free(child);
            }
            if(!found)return -1;
        }
    }else if(index<BAR_FIELDS){
        if(set_field(bar,index,input))return -1;
        if(index==16 && font->family!=bar->font_family)font->dropped[0]=0;
        font->family=bar->font_family;font->size=bar->font_size;
    }else if(index==FONT_FAMILY || index==FONT_SIZE){
        if(set_field(bar,index==FONT_FAMILY?16:15,input))return -1;
        font->family = bar->font_family;
        font->size = bar->font_size;
        if (index == FONT_FAMILY)
            font->dropped[0] = 0;
    }else{
        const char *text = tiku_msg_find_string(input, "value", 0),
                   *type = tiku_msg_find_string(input, "type", 0);
        if (strcmp(type, "text") || !text ||
            strlen(text) >= sizeof font->dropped || strpbrk(text, "\r\n"))
            return -1;
        strcpy(font->dropped,text);
    }
    tiku_trk_deskbar_settings_normalize(bar);
    font->family = bar->font_family;
    font->size = bar->font_size;
    expected = tiku_msg_copy(input);
    actual = value(bar, font, index);
    equal = expected && actual && tiku_resource_value_equal(expected, actual);
    tiku_msg_free(expected);tiku_msg_free(actual);return equal?0:-1;
}

/** @brief Capture actual font fallback after the existing GUI apply callback.
 */
static void feedback(tiku_trk_record_settings_t *owner)
{
    owner->applied_font=owner->accepted_font;
    owner->applied_font.size = tiku_font_size();
    owner->applied_font.family = tiku_font_family();
    if (owner->accepted_font.dropped[0] &&
        strcmp(tiku_font_current_family(), owner->accepted_font.dropped))
        owner->applied_font.dropped[0] = 0;
}

/** @brief Build independently owned desired, saved and applied resource
 * values.
 */
static int snapshot(tiku_trk_record_settings_t *owner, int index,
                    tiku_resource_snapshot_t *out)
{
    tiku_trk_deskbar_settings_t applied=owner->accepted_bar;
    memset(out,0,sizeof *out);
    if(index==WINDOW_DEFAULTS)return default_snapshot(owner,out);
    applied.font_size = owner->applied_font.size;
    applied.font_family = owner->applied_font.family;
    out->desired = value(&owner->accepted_bar, &owner->accepted_font, index);
    out->persisted = value(&owner->accepted_bar, &owner->accepted_font, index);
    out->applied=value(&applied,&owner->applied_font,index);
    strcpy(out->apply_state,
           out->desired && out->applied &&
                   tiku_resource_value_equal(out->desired, out->applied)
               ? "applied"
               : "fallback");
    if(out->desired && out->persisted && out->applied)return 0;
    tiku_resource_snapshot_clear(out);return -1;
}

/** @brief Publish current owner values after another controller advanced the
 * shared stamp.
 */
static void publish(tiku_trk_record_settings_t *owner)
{
    tiku_resource_caller_t caller = {.authenticated = 1, .scopes = 63};
    tiku_resource_stamp_t stamp;
    tiku_msg_t *description = NULL;
    int i;
    if(!owner->registry)return;
    owner->resync=0;strcpy(caller.principal,"00000000000000000000000000000001");
    if (tiku_resource_describe(owner->registry, &caller, owner->paths[0],
                               &description)) {
        owner->resync = 1;
        return;
    }
    strcpy(stamp.provider, tiku_msg_find_string(description, "provider", 0));
    strcpy(stamp.incarnation,
           tiku_msg_find_string(description, "incarnation", 0));
    tiku_resource_counter_parse(
        tiku_msg_find_string(description, "revision", 0), &stamp.revision);
    tiku_msg_free(description);
    for(i=0;i<RECORD_COUNT;i++){
        tiku_resource_snapshot_t before = {0}, next = {0};
        tiku_resource_error_t error = tiku_resource_read(
            owner->registry, &caller, owner->paths[i], &before);
        if ((error && error != TIKU_RESOURCE_UNAVAILABLE) ||
            snapshot(owner, i, &next)) {
            owner->resync = 1;
            tiku_resource_snapshot_clear(&before);
            continue;
        }
        if (error ||
            !tiku_resource_value_equal(before.desired, next.desired) ||
            !tiku_resource_value_equal(before.persisted, next.persisted) ||
            !tiku_resource_value_equal(before.applied, next.applied) ||
            strcmp(before.apply_state, next.apply_state)) {
            if (tiku_resource_publish(owner->registry, &stamp, owner->paths[i],
                                      &next, &stamp))
                owner->resync = 1;
        }
        tiku_resource_snapshot_clear(&before);
        tiku_resource_snapshot_clear(&next);
    }
}

/** @brief Restore the committed GUI models, preserving each window's revert
 * baseline.
 */
static void restore(tiku_trk_record_settings_t *owner)
{
    owner->bar->value = owner->accepted_bar;
    owner->font->value = owner->accepted_font;
}

/** @brief Run isolated storage persistence with no live owner pointer access.
 */
static void run(void *context)
{
    record_job_t *job=context;tiku_store_update_run(job->update);
}

/** @brief Map persistence effects without reporting failed durable writes as
 * success.
 */
static tiku_resource_error_t error_for(tiku_store_update_result_t result)
{
    return result == TIKU_STORE_UPDATE_OK          ? TIKU_RESOURCE_OK
           : result == TIKU_STORE_UPDATE_CONFLICT  ? TIKU_RESOURCE_STALE
           : result == TIKU_STORE_UPDATE_CANCELLED ? TIKU_RESOURCE_CANCELLED
           : result == TIKU_STORE_UPDATE_UNCERTAIN
               ? TIKU_RESOURCE_UNKNOWN_OUTCOME
           : result == TIKU_STORE_UPDATE_NO_MEMORY
               ? TIKU_RESOURCE_NO_MEMORY
               : TIKU_RESOURCE_PERSISTENCE_FAILED;
}

/** @brief Free a worker job after its owner has finished or detached its
 * update.
 */
static void release(void *context)
{
    record_job_t *job = context;
    if (job->held)
        return;
    tiku_store_update_free(job->update);
    free(job);
}

/** @brief Start a copied proposal after the current reload has completed. */
static int post(tiku_trk_record_settings_t *,record_job_t *);

/** @brief Reconcile the store, live models and retained resource job on the
 * owner thread.
 */
static void done(void *context)
{
    record_job_t *job = context;
    tiku_trk_record_settings_t *owner = job->owner;
    tiku_store_update_result_t result;
    tiku_trk_deskbar_prefs_t bar; tiku_trk_fontprefs_t font;int changed=0;
    if(!owner)return;
    result=tiku_store_update_finish(job->update);
    if(result==TIKU_STORE_UPDATE_NO_MEMORY){job->held=1;return;}
    job->held=0;owner->job=NULL;
    if (job->write && owner->last_error != TIKU_RESOURCE_BUSY)
        owner->last_error = error_for(result);
    if(result==TIKU_STORE_UPDATE_OK || result==TIKU_STORE_UPDATE_UNCERTAIN){
        tiku_trk_deskbar_prefs_init(&bar);
        tiku_trk_deskbar_prefs_load(&bar, owner->store);
        tiku_trk_fontprefs_load(&font, owner->store);
        changed = !models_same(&owner->accepted_bar, &owner->accepted_font,
                               &bar.value, &font.value);
        owner->accepted_bar = bar.value;
        owner->accepted_font = font.value;
        restore(owner);
        if(changed && owner->changed)owner->changed(owner->context);
        feedback(owner);
    }
    if(job->id[0] && owner->registry){
        tiku_resource_snapshot_t out = {0};
        tiku_resource_error_t error = error_for(result);
        if (!error && snapshot(owner, job->index, &out))
            error = TIKU_RESOURCE_UNKNOWN_OUTCOME;
        tiku_resource_job_complete(owner->registry, job->id,
                                   error ? NULL : &out, error,
                                   error == TIKU_RESOURCE_UNKNOWN_OUTCOME
                                       ? TIKU_RESOURCE_EFFECT_UNKNOWN
                                   : error ? TIKU_RESOURCE_EFFECT_NONE
                                           : TIKU_RESOURCE_EFFECT_PARTIAL);
        tiku_resource_snapshot_clear(&out);
    }
    if(job->write && !changed && owner->changed)owner->changed(owner->context);
    publish(owner);
    if(owner->queued){
        record_job_t *next=owner->queued;owner->queued=NULL;
        if ((next->index == WINDOW_DEFAULTS
                 ? !default_current(owner, next)
                 : !models_same(&next->before_bar, &next->before_font,
                                &owner->accepted_bar,
                                &owner->accepted_font)) ||
            post(owner, next)) {
            owner->last_error=TIKU_RESOURCE_STALE;
            if(owner->changed)owner->changed(owner->context);
            if (next->id[0] && owner->registry)
                tiku_resource_job_complete(owner->registry, next->id, NULL,
                                           TIKU_RESOURCE_STALE,
                                           TIKU_RESOURCE_EFFECT_NONE);
            release(next);
        }
    }
}

/** @brief Stage complete native records in a copied store before worker
 * submission.
 */
static int post(tiku_trk_record_settings_t *owner,record_job_t *job)
{
    tiku_work_job_t work={.run=run,.done=done,.free=release,.ctx=job};
    job->update=tiku_store_update_begin(owner->store);if(!job->update)return -1;
    if(job->write && job->index==WINDOW_DEFAULTS){
        if (!default_current(owner, job) ||
            tiku_state_write(tiku_store_update_model(job->update),
                             TIKU_KEY_SESSION, TIKU_ATTR_DEFAULTS,
                             job->have_defaults ? (const void *)&job->defaults
                                                : (const void *)"",
                             job->have_defaults ? sizeof job->defaults : 0))
            return -1;
    }else if(job->write){
        tiku_trk_deskbar_prefs_t bar = {.value = job->bar};
        tiku_trk_fontprefs_t font = {.value = job->font};
        tiku_store_t *model=tiku_store_update_model(job->update);
        if (tiku_trk_deskbar_prefs_save(&bar, model) ||
            !tiku_trk_fontprefs_save(&font, model))
            return -1;
    }
    strcpy(work.key,"desktop-record-settings");job->owner=owner;
    if(tiku_work_post(&work))return -1;
    owner->job=job;return 0;
}

/** @brief Queue one proposal behind a reload, with its original model
 * precondition.
 */
static int accept(tiku_trk_record_settings_t *owner,record_job_t *job)
{
    job->before_bar=owner->accepted_bar;job->before_font=owner->accepted_font;
    job->had_defaults =
        tiku_trk_frame_default(owner->store, &job->before_defaults);
    if(owner->queued || (owner->job && owner->job->write))return -1;
    owner->last_error=TIKU_RESOURCE_OK;
    if(owner->job){job->owner=owner;owner->queued=job;return 0;}
    return post(owner,job);
}

/** @brief Retain the latest native font choice behind a native font save or reload. */
static int accept_font(tiku_trk_record_settings_t *owner, record_job_t *job)
{
    if (!owner->job)
        return accept(owner, job);
    if ((owner->job->write &&
         (owner->job->id[0] || owner->job->index != FONT_PROFILE)) ||
        (owner->queued &&
         (owner->queued->id[0] || owner->queued->index != FONT_PROFILE)))
        return -1;
    job->before_bar =
        owner->job->write ? owner->job->bar : owner->accepted_bar;
    job->before_font =
        owner->job->write ? owner->job->font : owner->accepted_font;
    job->owner = owner;
    if (owner->queued)
        release(owner->queued);
    owner->queued = job;
    owner->last_error = TIKU_RESOURCE_OK;
    return 0;
}

tiku_trk_record_settings_t *tiku_trk_record_settings_open(
    tiku_store_t *store, tiku_trk_deskbar_prefs_t *bar,
    tiku_trk_fontprefs_t *font, void (*changed)(void *), void *context)
{
    tiku_trk_record_settings_t *owner;int i;
    if(!store || !bar || !font)return NULL;
    owner=calloc(1,sizeof *owner);if(!owner)return NULL;
    owner->store = store;
    owner->bar = bar;
    owner->font = font;
    owner->changed = changed;
    owner->context = context;
    tiku_trk_deskbar_prefs_init(bar);
    tiku_trk_deskbar_prefs_load(bar, store);
    tiku_trk_fontprefs_load(font, store);
    owner->accepted_bar=bar->value;owner->accepted_font=font->value;
    for (i = 0; i < BAR_FIELDS; i++)
        snprintf(owner->paths[i], sizeof owner->paths[i],
                 "/settings/deskbar/%s", fields[i].key);
    strcpy(owner->paths[FONT_FAMILY], "/settings/ui/font/family");
    strcpy(owner->paths[FONT_SIZE], "/settings/ui/font/size");
    strcpy(owner->paths[FONT_DROPPED], "/settings/ui/font/dropped");
    strcpy(owner->paths[BAR_PROFILE], "/settings/deskbar/profile");
    strcpy(owner->paths[FONT_PROFILE], "/settings/ui/font/profile");
    strcpy(owner->paths[WINDOW_DEFAULTS],"/settings/tracker/window-defaults");
    if(owner->changed)owner->changed(context);
    feedback(owner);return owner;
}

int tiku_trk_record_settings_submit(tiku_trk_record_settings_t *owner,int font)
{
    record_job_t *job;if(!owner)return -1;
    job = calloc(1, sizeof *job);
    if (!job) {
        restore(owner);
        owner->last_error = TIKU_RESOURCE_NO_MEMORY;
        return -1;
    }
    job->bar=owner->bar->value;job->font=owner->font->value;job->write=1;
    job->index = font ? FONT_PROFILE : BAR_PROFILE;
    if (font) {
        job->bar.font_family = job->font.family;
        job->bar.font_size = job->font.size;
    } else {
        job->font.family = job->bar.font_family;
        job->font.size = job->bar.font_size;
        if (job->bar.font_family != owner->accepted_bar.font_family)
            job->font.dropped[0] = 0;
    }
    tiku_trk_deskbar_settings_normalize(&job->bar);
    job->font.family = job->bar.font_family;
    job->font.size = job->bar.font_size;
    restore(owner);
    if (font ? accept_font(owner, job) : accept(owner, job)) {
        owner->last_error = TIKU_RESOURCE_BUSY;
        release(job);
        return -1;
    }
    return 0;
}

void tiku_trk_record_settings_poll(tiku_trk_record_settings_t *owner,
                                   uint64_t now)
{
    record_job_t *job;if(!owner)return;
    if (owner->job && owner->job->held) {
        job = owner->job;
        done(job);
        if (!job->held)
            release(job);
        return;
    }
    if(owner->job || now<owner->next_poll)return;
    publish(owner);
    owner->next_poll=now+500;job=calloc(1,sizeof *job);if(!job)return;
    if(post(owner,job))release(job);
}

/** @brief Read a monotonic wait deadline; zero means the clock is
 * unavailable.
 */
static uint64_t milliseconds(void)
{
    struct timespec now;
    if(clock_gettime(CLOCK_MONOTONIC,&now))return 0;
    return (uint64_t)now.tv_sec*1000+(uint64_t)now.tv_nsec/1000000;
}

void tiku_trk_record_settings_close(tiku_trk_record_settings_t *owner)
{
    uint64_t deadline,now;if(!owner)return;deadline=milliseconds()+5000;
    while (owner->job && (now = milliseconds()) && now < deadline) {
        tiku_work_collect();
        if (owner->job && owner->job->held)
            tiku_trk_record_settings_poll(owner, 0);
        if (owner->job)
            usleep(1000);
    }
    if (owner->job) {
        record_job_t *job = owner->job;
        job->owner = NULL;
        tiku_store_update_detach(job->update);
        if (job->held) {
            job->held = 0;
            release(job);
        }
    }
    if(owner->queued)release(owner->queued);
    free(owner);
}

size_t tiku_trk_record_settings_count(void){return RECORD_COUNT;}

int tiku_trk_record_settings_initial(tiku_trk_record_settings_t *owner,
                                     tiku_resource_initial_t *initial)
{
    int i;for(i=0;i<RECORD_COUNT;i++){
        tiku_resource_schema_t schema = {.type = TIKU_RESOURCE_INTEGER,
                                         .integer_min = 0,
                                         .integer_max =
                                             tiku_font_family_count() - 1};
        if (i < BAR_FIELDS) {
            schema.type =
                fields[i].count == 1 ? fields[i].type : TIKU_RESOURCE_ARRAY;
            schema.integer_min = fields[i].low;
            schema.integer_max =
                i == 16 ? tiku_font_family_count() - 1 : fields[i].high;
        } else if (i == FONT_SIZE) {
            schema.integer_min = TIKU_TRK_FONT_MIN;
            schema.integer_max = TIKU_TRK_FONT_MAX;
        } else if (i == FONT_DROPPED) {
            schema.type = TIKU_RESOURCE_TEXT;
            schema.text_max = 63;
        } else if (i == BAR_PROFILE || i == FONT_PROFILE ||
                   i == WINDOW_DEFAULTS)
            schema.type = TIKU_RESOURCE_OBJECT;
        initial[i].spec = (tiku_resource_spec_t){
            .path = owner->paths[i],
            .kind = TIKU_RESOURCE_SETTING,
            .schema_version = 1,
            .schema = schema,
            .public_read = 1,
            .read_scopes = TIKU_RESOURCE_READ,
            .change_scopes = TIKU_RESOURCE_WRITE,
            .persistence =
                tiku_store_persistent(owner->store) ? "user" : "session",
            .controller = "desktop-record-settings"};
        if(snapshot(owner,i,&initial[i].initial))return -1;
    }
    return 0;
}

void tiku_trk_record_settings_bind(tiku_trk_record_settings_t *owner,
                                   tiku_resource_registry_t *registry)
{
    if(owner)owner->registry=registry;
}

tiku_resource_error_t
tiku_trk_record_settings_validate(tiku_trk_record_settings_t *owner,
                                  const char *path, const tiku_msg_t *input,
                                  int invoke)
{
    tiku_trk_deskbar_settings_t bar; tiku_trk_fontprefs_values_t font;int index;
    if (!owner || invoke || (index = index_for(owner, path)) < 0)
        return TIKU_RESOURCE_NOT_FOUND;
    if (owner->queued || (owner->job && owner->job->write))
        return TIKU_RESOURCE_BUSY;
    if (index == WINDOW_DEFAULTS) {
        tiku_trk_frame_t frame;
        int present;
        return default_replace(&frame, &present, input) ? TIKU_RESOURCE_VALUE
                                                        : TIKU_RESOURCE_OK;
    }
    bar=owner->accepted_bar;font=owner->accepted_font;
    return replace(&bar,&font,index,input)?TIKU_RESOURCE_VALUE:TIKU_RESOURCE_OK;
}

tiku_resource_error_t
tiku_trk_record_settings_start(tiku_trk_record_settings_t *owner,
                               const char *id, const char *path,
                               const tiku_msg_t *input, int invoke)
{
    tiku_resource_error_t error =
        tiku_trk_record_settings_validate(owner, path, input, invoke);
    record_job_t *job;
    if(error)return error;
    job=calloc(1,sizeof *job);if(!job)return TIKU_RESOURCE_NO_MEMORY;
    job->bar = owner->accepted_bar;
    job->font = owner->accepted_font;
    job->index = index_for(owner, path);
    job->write = 1;
    strcpy(job->id, id);
    if ((job->index == WINDOW_DEFAULTS
             ? default_replace(&job->defaults, &job->have_defaults, input)
             : replace(&job->bar, &job->font, job->index, input)) ||
        accept(owner, job)) {
        release(job);
        return TIKU_RESOURCE_BUSY;
    }
    return TIKU_RESOURCE_PENDING;
}

const char *
tiku_trk_record_settings_status(const tiku_trk_record_settings_t *owner)
{
    if(!owner)return "";
    if (owner->last_error == TIKU_RESOURCE_BUSY)
        return "Another save is pending. Retry.";
    if ((owner->job && owner->job->write) || owner->queued)
        return "Saving settings...";
    if(owner->last_error==TIKU_RESOURCE_STALE)return "Settings changed. Retry.";
    if (owner->last_error == TIKU_RESOURCE_UNKNOWN_OUTCOME)
        return "Save outcome unknown. Check settings.";
    return owner->last_error?"Could not save settings. Retry.":"";
}
