/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_settings.c - native Desktop setting jobs and snapshots.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_desktop_settings.h"
#include "tiku_trk_resource_values.h"
#include "tiku_resource_jobs.h"
#include "tiku_state_attribute.h"
#include "tiku_work.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { BACKGROUND, ICON_SIZE, HAVE_BACKGROUND, DESKTOP_FIELDS };
static const char *const paths[] = {"/settings/desktop/bg",
                                    "/settings/desktop/icon_size",
                                    "/settings/desktop/have_bg"};
static const char *const attributes[] = {TIKU_TRK_ATTR_BACKGROUND,
                                         TIKU_ATTR_DESK_VIEW_STATE};
#define DESKTOP_RECORD_MAX (TIKU_PATH_MAX + 128)

/** @brief One copied native record and its optional descriptor-bound capture.
 */
typedef struct {
    tiku_state_attribute_t *capture;
    char bytes[DESKTOP_RECORD_MAX];
    size_t length;
    int present;
} desktop_record_t;
/** @brief Isolated persistence work; callbacks alone access the borrowed
 * owner.
 */
typedef struct desktop_setting_job {
    struct tiku_trk_desktop_settings *owner;
    tiku_store_update_t *update;
    tiku_state_attribute_update_t *native[2];
    desktop_record_t records[2];
    tiku_trk_deskprefs_values_t before, proposal;
    char node[TIKU_PATH_MAX], id[TIKU_RESOURCE_ID_MAX];
    tiku_store_update_result_t result;
    int write, index, native_store, held, effects, read_ok;
} desktop_setting_job_t;
struct tiku_trk_desktop_settings {
    tiku_store_t *store;
    tiku_trk_deskprefs_t *model;
    tiku_trk_deskprefs_values_t accepted, persisted, applied;
    int has_feedback;
    desktop_record_t records[2];
    desktop_setting_job_t *job, *queued;
    tiku_resource_registry_t *registry;
    char node[TIKU_PATH_MAX];
    void (*changed)(void *); void *context;
    uint64_t next_poll;
    tiku_resource_error_t error, save_error;
    int ready, partial;
};

/** @brief Match an exact descriptor path. */
static int desktop_index(const char *path)
{
    int i;
    for (i = 0; i < DESKTOP_FIELDS; i++)
        if (!strcmp(path, paths[i]))
            return i;
    return -1;
}

/** @brief Compare fields owned here, excluding the table-owned motion
 * preference.
 */
static int desktop_same(const tiku_trk_deskprefs_values_t *a,
                        const tiku_trk_deskprefs_values_t *b)
{
    return a->icon_size == b->icon_size && a->have_bg == b->have_bg &&
           a->bg.mode == b->bg.mode && a->bg.x == b->bg.x &&
           a->bg.y == b->bg.y && !strcmp(a->bg.path, b->bg.path);
}

/** @brief Encode a native background with named, typed public fields. */
static tiku_msg_t *desktop_value(const tiku_trk_deskprefs_values_t *value,
                                 int index)
{
    tiku_msg_t *out;
    if (index == ICON_SIZE)
        return tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,
                                           value->icon_size);
    if (index == HAVE_BACKGROUND)
        return tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,
                                           value->have_bg);
    out = trk_value_container("object");
    if (trk_value_member(
            out, "path",
            tiku_resource_value_string(TIKU_RESOURCE_TEXT, value->bg.path)) ||
        trk_value_member(out, "mode",
                         tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,
                                                     value->bg.mode)) ||
        trk_value_member(
            out, "x",
            tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, value->bg.x)) ||
        trk_value_member(
            out, "y",
            tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, value->bg.y))) {
        tiku_msg_free(out); return NULL;
    }
    return out;
}

/** @brief Decode an exact background object or an offered icon size. */
static int desktop_replace(tiku_trk_deskprefs_values_t *out, int index,
                           const tiku_msg_t *input)
{
    tiku_msg_t *field, *canonical;
    int64_t number; int i, ok;
    const char *text, *type;
    if (tiku_resource_value_check(input)) return -1;
    type = tiku_msg_find_string(input, "type", 0);
    if (index == ICON_SIZE) {
        if (strcmp(type, "integer") ||
            !tiku_msg_find_int64(input, "value", 0, &number))
            return -1;
        for (i = 0; i < TIKU_TRK_DESK_ICON_STEPS; i++)
            if (number == tiku_trk_deskprefs_icon_step(i))
                break;
        if (i == TIKU_TRK_DESK_ICON_STEPS) return -1;
        out->icon_size = (int)number; return 0;
    }
    if (index != BACKGROUND || strcmp(type, "object") ||
        tiku_msg_count(input, "member") != 4)
        return -1;
    field = trk_value_find(input, "path");
    text = tiku_msg_find_string(field, "value", 0);
    type = tiku_msg_find_string(field, "type", 0);
    ok = text && type && !strcmp(type, "text") &&
         strlen(text) < sizeof out->bg.path && (!*text || *text == '/') &&
         !strpbrk(text, "\r\n\t");
    if (ok) strcpy(out->bg.path, text);
    tiku_msg_free(field); if (!ok) return -1;
    for (i = 0; i < 3; i++) {
        field = trk_value_find(input, i == 0 ? "mode" : i == 1 ? "x" : "y");
        type = tiku_msg_find_string(field, "type", 0);
        ok = type && !strcmp(type, "integer") &&
             tiku_msg_find_int64(field, "value", 0, &number) &&
             number >= (i == 0 ? 0 : -32768) && number <= (i == 0 ? 3 : 32768);
        tiku_msg_free(field); if (!ok) return -1;
        if (i == 0) out->bg.mode = (tiku_trk_bg_mode_t)number;
        else if (i == 1) out->bg.x = (int)number; else out->bg.y = (int)number;
    }
    out->have_bg = out->bg.path[0] != 0;
    canonical = desktop_value(out, index);
    ok = canonical && tiku_resource_value_equal(canonical, input);
    tiku_msg_free(canonical); return ok ? 0 : -1;
}

/** @brief Read the native view word and optional icon field without integer
 * overflow.
 */
static int icon_record(const desktop_record_t *record, int *size,
                       const char **suffix, char mode[5])
{
    const char *word = record->bytes, *space;
    char *end; long value; size_t length;
    if (!record->present || !record->length) {
        strcpy(mode, "icon");
        *suffix = " 0 0 0 0 0 -1 -1 0 0";
        return 0;
    }
    space = strchr(word, ' ');
    length = space ? (size_t)(space - word) : strlen(word);
    if (!((length == 4 &&
           (!memcmp(word, "icon", 4) || !memcmp(word, "mini", 4) ||
            !memcmp(word, "list", 4)))))
        return -1;
    memcpy(mode, word, length); mode[length] = 0;
    if (!space) { *suffix = ""; return 0; }
    errno = 0; value = strtol(space + 1, &end, 10);
    if (errno || end == space + 1 || (*end && *end != ' ') || value < 16 ||
        value > 128)
        return -1;
    *size = (int)value; *suffix = end; return 0;
}

/** @brief Decode both native records into a copied model, retaining the icon
 * fallback.
 */
static int desktop_decode(const desktop_record_t records[2],
                          tiku_trk_deskprefs_values_t *value)
{
    const char *suffix; char mode[5]; tiku_trk_bg_t bg;
    if (records[0].present && records[0].length) {
        if (!tiku_trk_bg_parse(records[0].bytes, &bg)) return -1;
        if (!tiku_resource_utf8_valid(bg.path,sizeof bg.path-1)) return -1;
        value->bg = bg; value->have_bg = bg.path[0] != 0;
    } else {
        tiku_trk_deskprefs_values_t defaults;
        tiku_trk_deskprefs_default(&defaults);
        value->bg = defaults.bg; value->have_bg = 0;
    }
    return icon_record(&records[1], &value->icon_size, &suffix, mode);
}

/** @brief Copy bounded native bytes, rejecting embedded terminators and
 * oversized records.
 */
static int record_bytes(desktop_record_t *out, const void *bytes,
                        size_t length, int present)
{
    if (length >= sizeof out->bytes || (length && memchr(bytes, 0, length)))
        return -1;
    if (length && bytes != out->bytes) memcpy(out->bytes, bytes, length);
    out->bytes[length] = 0;
    out->length = length;
    out->present = present;
    return 0;
}

/** @brief Copy an attribute into worker-owned storage through its appropriate
 * store.
 */
static int record_read(desktop_setting_job_t *job, int index)
{
    desktop_record_t *record = &job->records[index];
    if (job->native_store) {
        size_t length; const void *bytes;
        record->capture =
            tiku_state_attribute_read(job->node, attributes[index]);
        if (!record->capture) return -1;
        bytes = tiku_state_attribute_bytes(record->capture, &length);
        return record_bytes(record, bytes, length,
                            tiku_state_attribute_present(record->capture));
    } else {
        int length = tiku_state_read(tiku_store_update_model(job->update),
                                     job->node, attributes[index],
                                     record->bytes, sizeof record->bytes);
        if (length < 0) return record_bytes(record, NULL, 0, 0);
        return record_bytes(record, record->bytes, (size_t)length, 1);
    }
}

/** @brief Map storage completion to the resource error contract. */
static tiku_resource_error_t desktop_error(tiku_store_update_result_t result)
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

/** @brief Build a committed snapshot; loading or failed reads retain their
 * last accepted value.
 */
static int desktop_snapshot(tiku_trk_desktop_settings_t *owner, int index,
                            tiku_resource_snapshot_t *out)
{
    tiku_trk_deskprefs_values_t actual =
        owner->has_feedback ? owner->applied : owner->accepted;
    memset(out,0,sizeof *out);
    if (owner->has_feedback && !actual.have_bg) {
        actual.bg = owner->accepted.bg;
        actual.bg.path[0] = 0;
    }
    out->desired = desktop_value(&owner->accepted, index);
    out->persisted = desktop_value(&owner->persisted, index);
    out->applied = desktop_value(&actual,index);
    if (!out->desired || !out->persisted || !out->applied) {
        tiku_resource_snapshot_clear(out);
        return -1;
    }
    strcpy(out->apply_state,
           owner->error    ? "last_good_unavailable"
           : !owner->ready ? "loading"
           : !tiku_resource_value_equal(out->desired, out->persisted)
               ? "pending_persistence"
           : !tiku_resource_value_equal(out->desired, out->applied)
               ? "fallback"
               : "applied");
    return 0;
}

/** @brief Publish changed values without replacing identities or active
 * collection members.
 */
static void desktop_publish(tiku_trk_desktop_settings_t *owner)
{
    int i;
    for (i = 0; owner->registry && i < DESKTOP_FIELDS; i++) {
        tiku_resource_snapshot_t next = {0};
        if (!desktop_snapshot(owner, i, &next))
            trk_value_publish(owner->registry, paths[i], &next);
        tiku_resource_snapshot_clear(&next);
    }
}

/** @brief Release detached or collected work without accessing the live
 * store.
 */
static void desktop_release(void *opaque)
{
    desktop_setting_job_t *job = opaque; int i;
    if (job->held) return;
    for (i = 0; i < 2; i++) {
        tiku_state_attribute_free(job->records[i].capture);
        tiku_state_attribute_update_free(job->native[i]);
    }
    tiku_store_update_free(job->update); free(job);
}

/** @brief Run copied persistence and capture native results without touching
 * live models.
 */
static void desktop_run(void *opaque)
{
    desktop_setting_job_t *job = opaque; int i;
    job->result = TIKU_STORE_UPDATE_OK;
    if (job->native_store) {
        for (i = 0; i < 2 && job->write; i++) if (job->native[i]) {
            job->result = tiku_state_attribute_run(job->native[i]);
            if (job->result == TIKU_STORE_UPDATE_OK ||
                job->result == TIKU_STORE_UPDATE_UNCERTAIN)
                job->effects++;
            if (job->result != TIKU_STORE_UPDATE_OK) break;
        }
    } else {
        tiku_store_update_run(job->update); return;
    }
    job->read_ok = 1;
    for (i = 0; i < 2; i++)
        if (record_read(job, i)) {
            job->read_ok = 0;
            job->result = job->effects ? TIKU_STORE_UPDATE_UNCERTAIN
                                       : TIKU_STORE_UPDATE_FAILED;
            break;
        }
}

static int desktop_post(tiku_trk_desktop_settings_t *, desktop_setting_job_t *);

/** @brief Reconcile completed work, then dispatch a proposal queued behind a
 * reload.
 */
static void desktop_done(void *opaque)
{
    desktop_setting_job_t *job = opaque;
    tiku_trk_desktop_settings_t *owner = job->owner;
    tiku_trk_deskprefs_values_t decoded, before, persisted;
    tiku_resource_error_t previous_error;
    int i, was_ready;
    if (!owner) return;
    before = owner->accepted;
    persisted = owner->persisted;
    previous_error = owner->error;
    was_ready = owner->ready;
    if (!job->native_store) {
        job->result = tiku_store_update_finish(job->update);
        if (job->write && job->result == TIKU_STORE_UPDATE_OK) job->effects = 1;
        if (job->result == TIKU_STORE_UPDATE_NO_MEMORY) {
            job->held = 1;
            return;
        }
        if (job->result == TIKU_STORE_UPDATE_OK) {
            job->read_ok = 1;
            for (i = 0; i < 2; i++) {
                desktop_record_t *record = &job->records[i];
                int length =
                    tiku_state_read(owner->store, owner->node, attributes[i],
                                    record->bytes, sizeof record->bytes);
                if (record_bytes(record, record->bytes,
                                 length < 0 ? 0u : (size_t)length,
                                 length >= 0))
                    job->read_ok = 0;
            }
        }
    }
    job->held = 0; owner->job = NULL; owner->error = desktop_error(job->result);
    decoded = owner->accepted;
    if (job->read_ok &&
        (job->result == TIKU_STORE_UPDATE_OK || job->effects) &&
        !desktop_decode(job->records, &decoded)) {
        for (i = 0; i < 2; i++) {
            tiku_state_attribute_free(owner->records[i].capture);
            owner->records[i] = job->records[i];
            job->records[i].capture = NULL;
        }
        owner->accepted = decoded; owner->ready = 1;
        if (job->native_store) owner->persisted = decoded;
        else {
            desktop_record_t saved[2] = {0};
            tiku_trk_deskprefs_values_t persisted = owner->persisted;
            int bad = 0;
            for (i = 0; i < 2; i++) {
                int length;
                errno = 0;
                length = tiku_store_persisted_read(
                    owner->store, owner->node, attributes[i], saved[i].bytes,
                    sizeof saved[i].bytes);
                if (length < 0 && errno != ENOENT) bad = 1;
                if (record_bytes(&saved[i], saved[i].bytes,
                                 length < 0 ? 0u : (size_t)length,
                                 length >= 0))
                    bad = 1;
            }
            if (bad || desktop_decode(saved, &persisted))
                owner->error = TIKU_RESOURCE_PERSISTENCE_FAILED;
            else owner->persisted = persisted;
        }
        owner->model->value.bg = decoded.bg;
        owner->model->value.have_bg = decoded.have_bg;
        owner->model->value.icon_size = decoded.icon_size;
    } else if (!owner->error)
        owner->error = TIKU_RESOURCE_VALUE;
    if (job->write) {
        owner->save_error = owner->error;
        owner->partial = job->effects > 0 && owner->error;
    }
    if (owner->changed && (job->write || was_ready != owner->ready ||
                           previous_error != owner->error ||
                           !desktop_same(&before, &owner->accepted) ||
                           !desktop_same(&persisted, &owner->persisted)))
        owner->changed(owner->context);
    if (job->id[0] && owner->registry) {
        tiku_resource_snapshot_t out = {0};
        tiku_resource_error_t error = owner->error;
        if (!error && desktop_snapshot(owner, job->index, &out))
            error = TIKU_RESOURCE_UNKNOWN_OUTCOME;
        tiku_resource_job_complete(
            owner->registry, job->id, error ? NULL : &out, error,
            error == TIKU_RESOURCE_UNKNOWN_OUTCOME
                ? TIKU_RESOURCE_EFFECT_UNKNOWN
            : job->effects ? TIKU_RESOURCE_EFFECT_PARTIAL
                           : TIKU_RESOURCE_EFFECT_NONE);
        tiku_resource_snapshot_clear(&out);
    }
    desktop_publish(owner);
    if (owner->queued) {
        desktop_setting_job_t *next = owner->queued; owner->queued = NULL;
        if (!desktop_same(&next->before, &owner->accepted) ||
            desktop_post(owner, next)) {
            owner->error = owner->save_error = TIKU_RESOURCE_STALE;
            if (next->id[0] && owner->registry)
                tiku_resource_job_complete(owner->registry, next->id, NULL,
                                           TIKU_RESOURCE_STALE,
                                           TIKU_RESOURCE_EFFECT_NONE);
            desktop_release(next);
            if (owner->changed)
                owner->changed(owner->context);
        }
    }
}

/** @brief Encode changed native fields while preserving the view record's
 * other columns.
 */
static int desktop_post(tiku_trk_desktop_settings_t *owner,
                        desktop_setting_job_t *job)
{
    tiku_work_job_t work = {.run = desktop_run,
                            .done = desktop_done,
                            .free = desktop_release,
                            .ctx = job};
    int i;
    job->owner = owner;
    job->native_store = tiku_store_native_attributes(owner->store);
    strcpy(job->node, owner->node);
    if (!job->native_store) {
        job->update = tiku_store_update_begin(owner->store);
        if (!job->update)
            return -1;
    }
    for (i = 0; job->write && i < 2; i++) {
        char record[DESKTOP_RECORD_MAX]; int length;
        if ((i == 0 && job->index == ICON_SIZE) ||
            (i == 1 && job->index == BACKGROUND))
            continue;
        if (i == 0)
            length =
                tiku_trk_bg_format(&job->proposal.bg, record, sizeof record);
        else {
            const char *suffix;
            char mode[5];
            int ignored = owner->accepted.icon_size;
            if (icon_record(&owner->records[1], &ignored, &suffix, mode))
                return -1;
            length = snprintf(record, sizeof record, "%s %d%s", mode,
                              job->proposal.icon_size, suffix);
        }
        if (length < 0 || length >= (int)sizeof record) return -1;
        if (job->native_store) {
            if (!owner->records[i].capture) return -1;
            job->native[i] = tiku_state_attribute_prepare(
                owner->records[i].capture, record, (size_t)length, 1);
            if (!job->native[i]) return -1;
        } else if (tiku_state_write(tiku_store_update_model(job->update),
                                    owner->node, attributes[i], record,
                                    (size_t)length))
            return -1;
    }
    strcpy(work.key, "desktop-native-settings");
    if (tiku_work_post(&work)) return -1;
    owner->job = job; return 0;
}

/** @brief Accept one write or queue it behind an isolated reload. */
static int desktop_accept(tiku_trk_desktop_settings_t *owner,
                          desktop_setting_job_t *job)
{
    if (!owner->ready || owner->queued || (owner->job && owner->job->write))
        return -1;
    job->before = owner->accepted;
    owner->error = owner->save_error = TIKU_RESOURCE_OK;
    owner->partial = 0;
    if (owner->job) { owner->queued = job; job->owner = owner; return 0; }
    return desktop_post(owner, job);
}

tiku_trk_desktop_settings_t *
tiku_trk_desktop_settings_open(tiku_store_t *store, const char *node,
                               tiku_trk_deskprefs_t *model,
                               void (*changed)(void *), void *context)
{
    tiku_trk_desktop_settings_t *owner;
    if (!store || !node || !*node || strlen(node) >= TIKU_PATH_MAX || !model)
        return NULL;
    owner = calloc(1, sizeof *owner); if (!owner) return NULL;
    owner->store = store;
    owner->model = model;
    owner->accepted = owner->persisted = model->value;
    owner->changed = changed;
    owner->context = context;
    strcpy(owner->node, node);
    tiku_trk_desktop_settings_poll(owner, 1); return owner;
}

/** @brief Restore native controls without changing the table-owned motion
 * value.
 */
static void desktop_restore(tiku_trk_desktop_settings_t *owner)
{
    owner->model->value.bg = owner->accepted.bg;
    owner->model->value.have_bg = owner->accepted.have_bg;
    owner->model->value.icon_size = owner->accepted.icon_size;
}

void tiku_trk_desktop_settings_feedback(tiku_trk_desktop_settings_t *owner,
                                        const tiku_trk_bg_t *background,
                                        int icon_size)
{
    if (!owner || icon_size < 16 || icon_size > 128) return;
    owner->has_feedback = 1;
    owner->applied.icon_size = icon_size;
    owner->applied.have_bg = background && background->path[0];
    if (background) owner->applied.bg = *background;
}

int tiku_trk_desktop_settings_submit(tiku_trk_desktop_settings_t *owner)
{
    desktop_setting_job_t *job;
    tiku_msg_t *bg, *icon;
    int invalid, changed_bg, changed_icon;
    if (!owner) return -1;
    job = calloc(1,sizeof *job);
    if (!job) {
        desktop_restore(owner);
        owner->save_error = TIKU_RESOURCE_NO_MEMORY;
        return -1;
    }
    job->proposal = owner->model->value; job->write = 1;
    if (!job->proposal.have_bg) job->proposal.bg.path[0] = 0;
    changed_icon = job->proposal.icon_size != owner->accepted.icon_size;
    {
        tiku_trk_deskprefs_values_t before = owner->accepted;
        before.icon_size = job->proposal.icon_size;
        changed_bg = !desktop_same(&before,&job->proposal);
    }
    desktop_restore(owner);
    if (!changed_bg && !changed_icon) { desktop_release(job); return 0; }
    job->index = changed_bg && changed_icon ? -1
                 : changed_bg               ? BACKGROUND
                                            : ICON_SIZE;
    bg = changed_bg ? desktop_value(&job->proposal,BACKGROUND) : NULL;
    icon = changed_icon ? desktop_value(&job->proposal,ICON_SIZE) : NULL;
    invalid =
        (changed_bg && desktop_replace(&job->proposal, BACKGROUND, bg)) ||
        (changed_icon && desktop_replace(&job->proposal, ICON_SIZE, icon));
    tiku_msg_free(bg); tiku_msg_free(icon);
    if (invalid || desktop_accept(owner, job)) {
        owner->save_error = invalid ? TIKU_RESOURCE_VALUE : TIKU_RESOURCE_BUSY;
        desktop_release(job);
        return -1;
    }
    return 0;
}

void tiku_trk_desktop_settings_poll(tiku_trk_desktop_settings_t *owner,
                                    uint64_t now)
{
    desktop_setting_job_t *job;
    if (!owner) return;
    if (owner->job && owner->job->held) {
        job = owner->job;
        desktop_done(job);
        if (!job->held)
            desktop_release(job);
        return;
    }
    if (owner->job || now < owner->next_poll) return;
    desktop_publish(owner); owner->next_poll = now + 500;
    job = calloc(1, sizeof *job); if (!job) return;
    if (desktop_post(owner, job)) desktop_release(job);
}

void tiku_trk_desktop_settings_close(tiku_trk_desktop_settings_t *owner)
{
    struct timespec now; uint64_t deadline = 0, tick; int i;
    if (!owner) return;
    if (!clock_gettime(CLOCK_MONOTONIC, &now))
        deadline = (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000 + 5000;
    while (owner->job && deadline && !clock_gettime(CLOCK_MONOTONIC, &now)) {
        tick = (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
        if (tick >= deadline)
            break;
        tiku_work_collect();
        if (owner->job && owner->job->held)
            tiku_trk_desktop_settings_poll(owner, 0);
        if (owner->job)
            usleep(1000);
    }
    if (owner->job) {
        desktop_setting_job_t *job = owner->job; job->owner = NULL;
        tiku_store_update_detach(job->update);
        if (job->held) { job->held = 0; desktop_release(job); }
    }
    if (owner->queued) desktop_release(owner->queued);
    for (i = 0; i < 2; i++)
        tiku_state_attribute_free(owner->records[i].capture);
    free(owner);
}

const char *
tiku_trk_desktop_settings_status(const tiku_trk_desktop_settings_t *owner)
{
    tiku_resource_error_t error;
    if (!owner) return "";
    error = owner->save_error ? owner->save_error : owner->error;
    if (owner->partial) return "Some settings saved. Retry.";
    if (error == TIKU_RESOURCE_BUSY) return "Another save is pending. Retry.";
    if (owner->queued || (owner->job && owner->job->write))
        return "Saving settings...";
    if (error == TIKU_RESOURCE_STALE) return "Desktop settings changed. Retry.";
    if (error == TIKU_RESOURCE_UNKNOWN_OUTCOME)
        return "Save outcome unknown. Check settings.";
    return error ? "Could not save Desktop settings. Retry." : "";
}

size_t
tiku_trk_desktop_settings_count(const tiku_trk_desktop_settings_t *owner)
{
    return owner ? DESKTOP_FIELDS : 0;
}

int tiku_trk_desktop_settings_initial(tiku_trk_desktop_settings_t *owner,
                                      tiku_resource_initial_t *initial)
{
    int i;
    for (i = 0; i < DESKTOP_FIELDS; i++) {
        initial[i].spec = (tiku_resource_spec_t){
            .path = paths[i],
            .kind = i == HAVE_BACKGROUND ? TIKU_RESOURCE_STATE
                                         : TIKU_RESOURCE_SETTING,
            .schema_version = 1,
            .schema = {.type = i == BACKGROUND  ? TIKU_RESOURCE_OBJECT
                               : i == ICON_SIZE ? TIKU_RESOURCE_INTEGER
                                                : TIKU_RESOURCE_BOOLEAN,
                       .integer_min = 16,
                       .integer_max = 128},
            .read_scopes = TIKU_RESOURCE_READ,
            .change_scopes = TIKU_RESOURCE_WRITE,
            .sensitive = i == BACKGROUND,
            .public_read = i != BACKGROUND,
            .persistence = tiku_store_native_attributes(owner->store) ||
                                   tiku_store_persistent(owner->store)
                               ? "user"
                               : "session",
            .controller = "desktop-native-settings"};
        if (desktop_snapshot(owner, i, &initial[i].initial)) return -1;
    }
    return 0;
}

void tiku_trk_desktop_settings_bind(tiku_trk_desktop_settings_t *owner,
                                    tiku_resource_registry_t *registry)
{
    if (owner) owner->registry = registry;
}

tiku_resource_error_t
tiku_trk_desktop_settings_validate(tiku_trk_desktop_settings_t *owner,
                                   const char *path, const tiku_msg_t *input,
                                   int invoke)
{
    tiku_trk_deskprefs_values_t candidate; int index = desktop_index(path);
    if (!owner || index < 0) return TIKU_RESOURCE_NOT_FOUND;
    if (invoke || index == HAVE_BACKGROUND) return TIKU_RESOURCE_READ_ONLY;
    if (!owner->ready || owner->queued || (owner->job && owner->job->write))
        return TIKU_RESOURCE_BUSY;
    candidate = owner->accepted;
    return desktop_replace(&candidate, index, input) ? TIKU_RESOURCE_VALUE
                                                     : TIKU_RESOURCE_OK;
}

tiku_resource_error_t
tiku_trk_desktop_settings_start(tiku_trk_desktop_settings_t *owner,
                                const char *id, const char *path,
                                const tiku_msg_t *input, int invoke)
{
    tiku_resource_error_t error =
        tiku_trk_desktop_settings_validate(owner, path, input, invoke);
    desktop_setting_job_t *job;
    if (error) return error;
    job = calloc(1, sizeof *job); if (!job) return TIKU_RESOURCE_NO_MEMORY;
    job->index = desktop_index(path);
    job->proposal = owner->accepted;
    job->write = 1;
    strcpy(job->id, id);
    if (desktop_replace(&job->proposal, job->index, input) ||
        desktop_accept(owner, job)) {
        desktop_release(job);
        return TIKU_RESOURCE_BUSY;
    }
    return TIKU_RESOURCE_PENDING;
}
