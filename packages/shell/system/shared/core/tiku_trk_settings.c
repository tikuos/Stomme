/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_settings.c - preference jobs, file conflict checks and typed snapshots.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_settings.h"
#include "tiku_trk_resource_values.h"
#include "tiku_resource_jobs.h"
#include "tiku_atomic_file.h"
#include "tiku_state.h"
#include "tiku_work.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SETTINGS_FILE_MAX 65536u
#define SETTINGS_PATH_MAX 4096

/** @brief Captured complete file bytes and resolved replacement target. */
typedef struct {
    char *bytes;
    size_t size;
    int exists;
    char target[SETTINGS_PATH_MAX];
} settings_file_t;
/** @brief Worker-owned input, result and owner-thread completion link. */
typedef struct settings_job {
    struct tiku_trk_settings *owner;
    tiku_trk_prefs_t next, live;
    settings_file_t before, after;
    char path[SETTINGS_PATH_MAX], id[TIKU_RESOURCE_ID_MAX];
    char resource[TIKU_RESOURCE_PATH_MAX];
    int write;
    uint64_t generation;
    atomic_int cancelled;
    tiku_resource_error_t error;
    tiku_resource_effect_t effect;
} settings_job_t;

struct tiku_trk_settings {
    tiku_trk_prefs_t *model, accepted;
    tiku_resource_registry_t *registry;
    tiku_trk_record_settings_t *records;
    tiku_trk_catalog_settings_t *catalog;
    tiku_trk_desktop_settings_t *desktop;
    tiku_resource_stamp_t stamp;
    settings_file_t file;
    settings_job_t *reader, *writer;
    char path[SETTINGS_PATH_MAX];
    char (*paths)[TIKU_RESOURCE_PATH_MAX];
    void (*changed)(void *);
    void *context;
    uint64_t next_poll;
    uint64_t generation;
    int ready, resync;
    tiku_resource_error_t error;
};

static const char *const themes[] = {"light", "dark", "monochrome"};

/** @brief Release a captured file without changing its target. */
static void file_clear(settings_file_t *file)
{
    free(file->bytes); memset(file, 0, sizeof *file);
}

/** @brief Copy a bounded file capture into independent worker storage. */
static int file_copy(settings_file_t *out, const settings_file_t *in)
{
    *out = *in; out->bytes = NULL;
    if (in->size) {
        out->bytes = malloc(in->size);
        if (!out->bytes) return -1;
        memcpy(out->bytes, in->bytes, in->size);
    }
    return 0;
}

/** @brief Read a regular file up to SETTINGS_FILE_MAX bytes; missing is distinct. */
static int file_read(const char *path, settings_file_t *out)
{
    char *target = realpath(path, NULL);
    struct stat st;
    int fd; ssize_t n; size_t used = 0;
    memset(out, 0, sizeof *out);
    if (snprintf(out->target, sizeof out->target, "%s", target ? target : path) >= (int)sizeof out->target) { free(target); return -1; }
    free(target);
    fd = open(out->target, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || (uint64_t)st.st_size > SETTINGS_FILE_MAX) { close(fd); return -1; }
    out->bytes = malloc(SETTINGS_FILE_MAX + 1);
    if (!out->bytes) { close(fd); return -1; }
    while (used <= SETTINGS_FILE_MAX) {
        n = read(fd, out->bytes + used, SETTINGS_FILE_MAX + 1 - used);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { close(fd); file_clear(out); return -1; }
        if (!n) break;
        used += (size_t)n;
    }
    close(fd);
    if (used > SETTINGS_FILE_MAX || memchr(out->bytes, 0, used)) { file_clear(out); return -1; }
    out->exists = 1; out->size = used; return 0;
}

/** @brief Compare presence, resolved target and complete bytes, ignoring timestamps. */
static int file_equal(const settings_file_t *a, const settings_file_t *b)
{
    return a->exists == b->exists && !strcmp(a->target, b->target) && a->size == b->size && (!a->size || !memcmp(a->bytes, b->bytes, a->size));
}

/** @brief Copy the existing table and unknown lines, without a controller binding. */
static int prefs_copy(tiku_trk_prefs_t *out, const tiku_trk_prefs_t *in)
{
    int i;
    memset(out, 0, sizeof *out);
    out->pref = malloc((size_t)in->n * sizeof *in->pref);
    if (!out->pref) return -1;
    out->n = in->n; memcpy(out->pref, in->pref, (size_t)in->n * sizeof *in->pref);
    if (in->nunknown) {
        out->unknown = calloc((size_t)in->nunknown, sizeof *out->unknown);
        if (!out->unknown) goto fail;
        for (i = 0; i < in->nunknown; i++) {
            out->unknown[i] = strdup(in->unknown[i]);
            if (!out->unknown[i]) goto fail;
            out->nunknown++;
        }
    }
    return 0;
fail:
    tiku_trk_prefs_free(out); return -1;
}

/** @brief Restore scalar/text values without changing GUI revert baselines. */
static void restore(tiku_trk_prefs_t *out, const tiku_trk_prefs_t *in)
{
    int i;
    for (i = 0; i < out->n; i++) {
        out->pref[i].value = in->pref[i].value;
        memcpy(out->pref[i].text, in->pref[i].text, sizeof out->pref[i].text);
    }
}

/** @brief Transfer a prepared model while retaining its GUI binding and opened values. */
static void install(tiku_trk_prefs_t *out, tiku_trk_prefs_t *in)
{
    int i; void *context = out->save_context;
    int (*submit)(void *, const char *) = out->submit_save;
    for (i = 0; i < out->n; i++) in->pref[i].opened = out->pref[i].opened;
    tiku_trk_prefs_free(out); *out = *in; memset(in, 0, sizeof *in);
    out->save_context = context; out->submit_save = submit;
}

/** @brief Map preference names to canonical resource paths without another defaults table. */
static void pref_path(const char *name, char out[TIKU_RESOURCE_PATH_MAX])
{
    size_t i, used;
    if (!strcmp(name, "DarkMode")) { strcpy(out, "/settings/ui/theme"); return; }
    if (!strcmp(name, "GlobalMenuBar")) { strcpy(out, "/settings/ui/global-menu-bar"); return; }
    if (!strcmp(name, "ReduceMotion")) { strcpy(out, "/settings/ui/reduce-motion"); return; }
    if (!strncmp(name, "sound.", 6)) {
        snprintf(out, TIKU_RESOURCE_PATH_MAX, "/settings/sound/%s", name + 6);
        for (i = 16; out[i]; i++) if (out[i] == '.') out[i] = '/';
        if (strcmp(name, "sound.volume") && !strstr(name, ".clip")) strcat(out, "/enabled");
        return;
    }
    strcpy(out, "/settings/tracker/"); used = strlen(out);
    for (i = 0; name[i] && used + 1 < TIKU_RESOURCE_PATH_MAX; i++) out[used++] = (char)tolower((unsigned char)name[i]);
    out[used] = 0;
}

/** @brief The live table index corresponding to a registered path. */
static int index_for(const tiku_trk_settings_t *owner, const char *path)
{
    int i;
    if (owner->desktop && !strcmp(path, "/settings/desktop/reduce_motion")) {
        for (i = 0; i < owner->model->n; i++) if (!strcmp(owner->model->pref[i].name, "ReduceMotion")) return i;
    }
    for (i = 0; i <= owner->model->n; i++) if (!strcmp(owner->paths[i], path)) return i;
    return -1;
}

/** @brief Validate the shared table's scalar bounds and file-representable text. */
static int prefs_valid(const tiku_trk_prefs_t *prefs)
{
    int i;
    for (i = 0; i < prefs->n; i++) {
        const tiku_trk_pref_t *p = &prefs->pref[i];
        if (!strcmp(p->name, "DarkMode") && (p->value < 0 || p->value > 2)) return 0;
        if (!strcmp(p->name, "sound.volume") && (p->value < 0 || p->value > 100)) return 0;
        if (p->type == TIKU_TRK_PREF_BOOL && p->value != 0 && p->value != 1) return 0;
        if (p->type == TIKU_TRK_PREF_TEXT && (!tiku_resource_utf8_valid(p->text, sizeof p->text - 1) || strpbrk(p->text, "\r\n"))) return 0;
    }
    return 1;
}

/** @brief Encode one existing preference, including the theme's public choice names. */
static tiku_msg_t *pref_value(const tiku_trk_pref_t *pref)
{
    if (!strcmp(pref->name, "DarkMode")) return tiku_resource_value_string(TIKU_RESOURCE_ENUM, themes[pref->value]);
    if (pref->type == TIKU_TRK_PREF_TEXT) return tiku_resource_value_string(TIKU_RESOURCE_TEXT, pref->text);
    return tiku_resource_value_integer(pref->type == TIKU_TRK_PREF_BOOL ? TIKU_RESOURCE_BOOLEAN : pref->type == TIKU_TRK_PREF_COLOUR ? TIKU_RESOURCE_COLOR : TIKU_RESOURCE_INTEGER,
        pref->type == TIKU_TRK_PREF_COLOUR ? (int64_t)(uint32_t)pref->value : pref->value);
}

/** @brief Whether a table entry is observed product state rather than an editable preference. */
static int observed(const tiku_trk_pref_t *pref)
{
    return !strcmp(pref->name, "CurrentDevice") || !strcmp(pref->name, "CurrentDevicePort");
}

/** @brief Encode a complete editable profile using the existing table's file keys. */
static tiku_msg_t *profile_value(const tiku_trk_prefs_t *prefs)
{
    tiku_msg_t *result = tiku_msg_new(0); int i;
    if (!result || !tiku_msg_add_string(result, "type", "object")) goto fail;
    for (i = 0; i < prefs->n; i++) {
        tiku_msg_t *member, *value; int bad;
        if (observed(&prefs->pref[i])) continue;
        member = tiku_msg_new(0); value = pref_value(&prefs->pref[i]);
        bad = !member || !value || !tiku_msg_add_string(member, "key", prefs->pref[i].name) ||
            !tiku_msg_add_message(member, "value", value) || !tiku_msg_add_message(result, "member", member);
        tiku_msg_free(member); tiku_msg_free(value);
        if (bad) goto fail;
    }
    return result;
fail:
    tiku_msg_free(result); return NULL;
}

/** @brief Copy a committed preference into three independently owned snapshot values. */
static int pref_snapshot(const tiku_trk_pref_t *pref, tiku_resource_snapshot_t *out)
{
    memset(out, 0, sizeof *out);
    out->desired = pref_value(pref); out->persisted = pref_value(pref); out->applied = pref_value(pref);
    strcpy(out->apply_state, "applied");
    if (out->desired && out->persisted && out->applied) return 0;
    tiku_resource_snapshot_clear(out); return -1;
}

/** @brief Encode one scalar entry or the complete editable profile. */
static int settings_snapshot(const tiku_trk_prefs_t *prefs, int index, tiku_resource_snapshot_t *out)
{
    if (index < prefs->n) return pref_snapshot(&prefs->pref[index], out);
    memset(out, 0, sizeof *out);
    out->desired = profile_value(prefs); out->persisted = profile_value(prefs); out->applied = profile_value(prefs);
    strcpy(out->apply_state, "applied");
    if (out->desired && out->persisted && out->applied) return 0;
    tiku_resource_snapshot_clear(out); return -1;
}

/** @brief Find one typed profile member after the codec has checked key uniqueness. */
static tiku_msg_t *profile_member(const tiku_msg_t *profile, const char *key)
{
    int i;
    for (i = 0; i < tiku_msg_count(profile, "member"); i++) {
        tiku_msg_t *member = tiku_msg_find_message(profile, "member", i), *value = NULL;
        if (!strcmp(tiku_msg_find_string(member, "key", 0), key)) value = tiku_msg_find_message(member, "value", 0);
        tiku_msg_free(member); if (value) return value;
    }
    return NULL;
}

/** @brief Apply a profile through each scalar controller and reject interlocked disagreement. */
static tiku_resource_error_t replace_profile(tiku_trk_settings_t *, tiku_trk_prefs_t *, const tiku_msg_t *);

/** @brief Validate and apply one typed request to a candidate table using its interlocks. */
static tiku_resource_error_t replace(tiku_trk_settings_t *owner, tiku_trk_prefs_t *prefs,
    const char *path, const tiku_msg_t *value)
{
    int i = index_for(owner, path), boolean; int64_t number;
    const char *type = tiku_msg_find_string(value, "type", 0), *text;
    tiku_trk_pref_t *pref;
    if (i < 0 || !type || tiku_resource_value_check(value)) return TIKU_RESOURCE_VALUE;
    if (i == prefs->n) return replace_profile(owner, prefs, value);
    pref = &prefs->pref[i];
    if (!strcmp(pref->name, "CurrentDevice") || !strcmp(pref->name, "CurrentDevicePort")) return TIKU_RESOURCE_READ_ONLY;
    if (!strcmp(pref->name, "DarkMode")) {
        if (strcmp(type, "enum")) return TIKU_RESOURCE_VALUE;
        text = tiku_msg_find_string(value, "value", 0);
        for (i = 0; i < 3; i++) if (!strcmp(themes[i], text)) break;
        if (i == 3) return TIKU_RESOURCE_VALUE;
        tiku_trk_prefs_set(prefs, pref->name, i);
    } else if (pref->type == TIKU_TRK_PREF_TEXT) {
        text = tiku_msg_find_string(value, "value", 0);
        if (strcmp(type, "text") || !text || strlen(text) >= sizeof pref->text || strpbrk(text, "\r\n") || (*text && (*text == ' ' || *text == '\t'))) return TIKU_RESOURCE_VALUE;
        tiku_trk_prefs_set_text(prefs, pref->name, text);
    } else {
        if (pref->type == TIKU_TRK_PREF_BOOL) {
            if (strcmp(type, "boolean") || !tiku_msg_find_bool(value, "value", 0, &boolean)) return TIKU_RESOURCE_VALUE;
            number = boolean;
        } else {
            if (strcmp(type, pref->type == TIKU_TRK_PREF_COLOUR ? "color" : "integer") || !tiku_msg_find_int64(value, "value", 0, &number)) return TIKU_RESOURCE_VALUE;
            if (pref->type == TIKU_TRK_PREF_COLOUR ? number < 0 || (uint64_t)number > UINT32_MAX : number < INT32_MIN || number > INT32_MAX) return TIKU_RESOURCE_VALUE;
        }
        tiku_trk_prefs_set(prefs, pref->name, (int32_t)number);
    }
    return prefs_valid(prefs) ? TIKU_RESOURCE_OK : TIKU_RESOURCE_VALUE;
}

/** @brief Validate a complete editable table before persistence or live mutation. */
static tiku_resource_error_t replace_profile(tiku_trk_settings_t *owner,
    tiku_trk_prefs_t *prefs, const tiku_msg_t *profile)
{
    int i, count = 0;
    if (strcmp(tiku_msg_find_string(profile, "type", 0), "object")) return TIKU_RESOURCE_VALUE;
    for (i = 0; i < prefs->n; i++) {
        tiku_msg_t *value; tiku_resource_error_t error;
        if (observed(&prefs->pref[i])) continue;
        value = profile_member(profile, prefs->pref[i].name);
        if (!value) return TIKU_RESOURCE_VALUE;
        error = replace(owner, prefs, owner->paths[i], value); tiku_msg_free(value);
        if (error) return error;
        count++;
    }
    if (tiku_msg_count(profile, "member") != count) return TIKU_RESOURCE_VALUE;
    for (i = 0; i < prefs->n; i++) {
        tiku_msg_t *requested, *actual; int same;
        if (observed(&prefs->pref[i])) continue;
        requested = profile_member(profile, prefs->pref[i].name); actual = pref_value(&prefs->pref[i]);
        same = requested && actual && tiku_resource_value_equal(requested, actual);
        tiku_msg_free(requested); tiku_msg_free(actual);
        if (!same) return TIKU_RESOURCE_VALUE;
    }
    return TIKU_RESOURCE_OK;
}

/** @brief Refuse replacement when the captured file or cancellation state changed. */
static int check_file(void *opaque)
{
    settings_job_t *job = opaque;
    settings_file_t current;
    int bad = atomic_load(&job->cancelled) || file_read(job->path, &current);
    if (bad) return -1;
    bad = !file_equal(&current, &job->before); file_clear(&current); return bad ? -1 : 0;
}

/** @brief Serialize the worker's copied model into the replacement file. */
static int produce(FILE *stream, void *opaque)
{
    settings_job_t *job = opaque;
    return tiku_trk_prefs_write(stream, &job->next);
}

/** @brief Capture the exact bytes a successful replacement must retain. */
static int encoded(const tiku_trk_prefs_t *prefs, settings_file_t *file)
{
    FILE *stream = tmpfile();
    long size; int bad;
    memset(file, 0, sizeof *file);
    if (!stream) return -1;
    bad = tiku_trk_prefs_write(stream, prefs);
    size = ftell(stream);
    if (bad || size < 0 || (unsigned long)size > SETTINGS_FILE_MAX || fseek(stream, 0, SEEK_SET)) { fclose(stream); return -1; }
    file->bytes = malloc((size_t)size + 1);
    if (!file->bytes) { fclose(stream); return -1; }
    file->size = (size_t)size;
    bad = fread(file->bytes, 1, file->size, stream) != file->size;
    if (fclose(stream)) bad = 1;
    if (bad) file_clear(file);
    return bad ? -1 : 0;
}

/** @brief Parse captured bytes from a private stream using the existing file grammar. */
static int decode(const settings_file_t *file, tiku_trk_prefs_t *prefs)
{
    FILE *stream; int result;
    tiku_trk_prefs_init(prefs);
    if (!prefs->pref) return -1;
    if (!file->size) return 0;
    stream = tmpfile(); if (!stream) return -1;
    result = fwrite(file->bytes, 1, file->size, stream) != file->size || fseek(stream, 0, SEEK_SET);
    if (!result) result = tiku_trk_prefs_read(prefs, stream);
    if (fclose(stream)) result = -1;
    return result || !prefs_valid(prefs) ? -1 : 0;
}

/** @brief Perform bounded file reads and guarded durable replacement on a worker. */
static void run(void *opaque)
{
    settings_job_t *job = opaque;
    tiku_atomic_result_t result;
    settings_file_t expected = {0};
    if (atomic_load(&job->cancelled)) { job->error = TIKU_RESOURCE_CANCELLED; return; }
    if (!job->write) {
        if (file_read(job->path, &job->after)) { job->error = TIKU_RESOURCE_PERSISTENCE_FAILED; return; }
        if (job->before.target[0] && file_equal(&job->before, &job->after)) return;
        if (decode(&job->after, &job->next) || prefs_copy(&job->live, &job->next)) job->error = TIKU_RESOURCE_PERSISTENCE_FAILED;
        return;
    }
    if (tiku_state_mkparents(job->before.target)) { job->error = TIKU_RESOURCE_PERSISTENCE_FAILED; return; }
    if (encoded(&job->next, &expected)) { job->error = TIKU_RESOURCE_PERSISTENCE_FAILED; return; }
    result = tiku_atomic_file_guarded(job->before.target, produce, check_file, job);
    job->error = result == TIKU_ATOMIC_OK ? TIKU_RESOURCE_OK : result == TIKU_ATOMIC_CONFLICT ? TIKU_RESOURCE_STALE : result == TIKU_ATOMIC_UNCERTAIN ? TIKU_RESOURCE_UNKNOWN_OUTCOME : TIKU_RESOURCE_PERSISTENCE_FAILED;
    if (result == TIKU_ATOMIC_OK || result == TIKU_ATOMIC_UNCERTAIN) {
        job->effect = TIKU_RESOURCE_EFFECT_UNKNOWN;
        if (file_read(job->path, &job->after)) job->error = TIKU_RESOURCE_UNKNOWN_OUTCOME;
        else if (!job->after.exists || strcmp(job->after.target, job->before.target) ||
            job->after.size != expected.size || (expected.size && memcmp(job->after.bytes, expected.bytes, expected.size))) job->error = TIKU_RESOURCE_UNKNOWN_OUTCOME;
    }
    file_clear(&expected);
}

/** @brief Refresh a provider stamp from one currently registered resource. */
static int refresh(tiku_trk_settings_t *owner)
{
    tiku_resource_caller_t caller = {.authenticated = 1, .scopes = TIKU_RESOURCE_READ};
    tiku_msg_t *value = NULL;
    strcpy(caller.principal, "00000000000000000000000000000001");
    if (tiku_resource_describe(owner->registry, &caller, owner->paths[0], &value)) return -1;
    strcpy(owner->stamp.provider, tiku_msg_find_string(value, "provider", 0));
    strcpy(owner->stamp.incarnation, tiku_msg_find_string(value, "incarnation", 0));
    tiku_resource_counter_parse(tiku_msg_find_string(value, "revision", 0), &owner->stamp.revision);
    tiku_msg_free(value); return 0;
}

/** @brief Reconcile published settings with the live model after a complete save/reload. */
static void publish(tiku_trk_settings_t *owner)
{
    tiku_resource_caller_t caller = {.authenticated = 1, .scopes = TIKU_RESOURCE_READ};
    int i;
    if (!owner->registry) return;
    owner->resync = 0;
    if (refresh(owner)) { owner->resync = 1; return; }
    strcpy(caller.principal, "00000000000000000000000000000001");
    for (i = 0; i <= owner->model->n; i++) {
        tiku_resource_snapshot_t old = {0}, next = {0};
        tiku_resource_error_t error = tiku_resource_read(owner->registry, &caller, owner->paths[i], &old);
        if ((error && error != TIKU_RESOURCE_UNAVAILABLE) || settings_snapshot(owner->model, i, &next)) { owner->resync = 1; tiku_resource_snapshot_clear(&old); continue; }
        if (error || !tiku_resource_value_equal(old.desired, next.desired)) {
            if (tiku_resource_publish(owner->registry, &owner->stamp, owner->paths[i], &next, &owner->stamp)) { owner->error = TIKU_RESOURCE_UNAVAILABLE; owner->resync = 1; }
        }
        tiku_resource_snapshot_clear(&old); tiku_resource_snapshot_clear(&next);
    }
    if (owner->desktop) {
        tiku_resource_snapshot_t next = {0}; int index = index_for(owner, "/settings/desktop/reduce_motion");
        if (index >= 0 && !settings_snapshot(owner->model, index, &next)) trk_value_publish(owner->registry, "/settings/desktop/reduce_motion", &next);
        tiku_resource_snapshot_clear(&next);
    }
}

/** @brief Install completed models and publish job outcomes on the owner thread. */
static void done(void *opaque)
{
    settings_job_t *job = opaque;
    tiku_trk_settings_t *owner = job->owner;
    int changed;
    if (!owner) return;
    if (job->write) owner->writer = NULL;
    else owner->reader = NULL;
    if (!job->write && job->generation != owner->generation) return;
    owner->error = job->error;
    if (!owner->ready && !job->write && job->after.target[0] && job->error) {
        file_clear(&owner->file); owner->file = job->after;
        memset(&job->after, 0, sizeof job->after); owner->ready = 1;
    }
    changed = !job->error && (job->write || !owner->ready || !file_equal(&owner->file, &job->after));
    if (changed) {
        install(owner->model, &job->live); install(&owner->accepted, &job->next);
        file_clear(&owner->file); owner->file = job->after; memset(&job->after, 0, sizeof job->after); owner->ready = 1;
        if (owner->changed) owner->changed(owner->context);
    }
    if (job->id[0] && owner->registry) {
        tiku_resource_snapshot_t value = {0};
        int i = index_for(owner, job->resource);
        if (!job->error && settings_snapshot(owner->model, i, &value)) job->error = TIKU_RESOURCE_UNKNOWN_OUTCOME;
        tiku_resource_error_t error = tiku_resource_job_complete(owner->registry, job->id, job->error ? NULL : &value, job->error,
            job->error ? job->effect : TIKU_RESOURCE_EFFECT_PARTIAL);
        if (error || job->error == TIKU_RESOURCE_UNKNOWN_OUTCOME) { owner->resync = 1; owner->error = error ? error : job->error; }
        tiku_resource_snapshot_clear(&value);
    }
    if (changed || owner->resync) publish(owner);
}

/** @brief Release worker snapshots after completion or detached owner shutdown. */
static void release(void *opaque)
{
    settings_job_t *job = opaque;
    file_clear(&job->before); file_clear(&job->after);
    tiku_trk_prefs_free(&job->next); tiku_trk_prefs_free(&job->live); free(job);
}

/** @brief Queue one worker and preserve the owner until its completion link detaches. */
static int post(tiku_trk_settings_t *owner, settings_job_t *job)
{
    tiku_work_job_t work = {.run = run, .done = done, .free = release, .ctx = job};
    strcpy(work.key, "desktop-settings"); job->owner = owner; job->generation = owner->generation;
    if (tiku_work_post(&work)) { release(job); return -1; }
    if (job->write) owner->writer = job; else owner->reader = job;
    return 0;
}

/** @brief Compare values and preserved file lines independently of GUI revert baselines. */
static int prefs_same(const tiku_trk_prefs_t *a, const tiku_trk_prefs_t *b)
{
    int i;
    if (a->n != b->n || a->nunknown != b->nunknown) return 0;
    for (i = 0; i < a->n; i++)
        if (a->pref[i].value != b->pref[i].value || strcmp(a->pref[i].text, b->pref[i].text)) return 0;
    for (i = 0; i < a->nunknown; i++) if (strcmp(a->unknown[i], b->unknown[i])) return 0;
    return 1;
}

/** @brief Save changed GUI proposals while leaving page-only navigation without I/O. */
static int submit(void *opaque, const char *path)
{
    tiku_trk_settings_t *owner = opaque;
    if (owner->ready && !owner->error && !strcmp(path, owner->path) && prefs_same(owner->model, &owner->accepted)) return 0;
    settings_job_t *job = calloc(1, sizeof *job);
    int bad = !job || !owner->ready || owner->writer || strcmp(path, owner->path) || !prefs_valid(owner->model);
    if (!bad) bad = prefs_copy(&job->next, owner->model) || prefs_copy(&job->live, owner->model) || file_copy(&job->before, &owner->file);
    restore(owner->model, &owner->accepted);
    if (bad) { if (job) release(job); owner->error = TIKU_RESOURCE_BUSY; return -1; }
    strcpy(job->path, owner->path); job->write = 1; owner->generation++;
    return post(owner, job);
}

/** @brief Validate requests without persistence or model changes on the registry thread. */
static tiku_resource_error_t validate(void *opaque, const char *path, const tiku_msg_t *value, int invoke)
{
    tiku_trk_settings_t *owner = opaque;
    tiku_trk_prefs_t candidate = {0}; tiku_resource_error_t error;
    if (owner->desktop && !strncmp(path, "/settings/desktop/", 18) && strcmp(path, "/settings/desktop/reduce_motion")) return tiku_trk_desktop_settings_validate(owner->desktop, path, value, invoke);
    if (owner->catalog && (!strncmp(path, "/settings/associations/", 23) || !strncmp(path, "/settings/file-types/", 21))) return tiku_trk_catalog_settings_validate(owner->catalog, path, value, invoke);
    if (owner->records && index_for(owner, path) < 0) return tiku_trk_record_settings_validate(owner->records, path, value, invoke);
    if (invoke) return TIKU_RESOURCE_VALUE;
    if (!owner->ready || owner->writer) return TIKU_RESOURCE_BUSY;
    if (prefs_copy(&candidate, &owner->accepted)) return TIKU_RESOURCE_NO_MEMORY;
    error = replace(owner, &candidate, path, value); tiku_trk_prefs_free(&candidate); return error;
}

/** @brief Copy an accepted resource request and enqueue persistence without owner I/O. */
static tiku_resource_error_t start(void *opaque, const char *id, const char *path,
    const tiku_msg_t *value, int invoke, tiku_resource_snapshot_t *out)
{
    tiku_trk_settings_t *owner = opaque;
    settings_job_t *job; tiku_resource_error_t error;
    (void)invoke; (void)out;
    if (owner->desktop && !strncmp(path, "/settings/desktop/", 18) && strcmp(path, "/settings/desktop/reduce_motion")) return tiku_trk_desktop_settings_start(owner->desktop, id, path, value, invoke);
    if (owner->catalog && (!strncmp(path, "/settings/associations/", 23) || !strncmp(path, "/settings/file-types/", 21))) return tiku_trk_catalog_settings_start(owner->catalog, id, path, value, invoke);
    if (owner->records && index_for(owner, path) < 0) return tiku_trk_record_settings_start(owner->records, id, path, value, invoke);
    if (!owner->ready || owner->writer) return TIKU_RESOURCE_BUSY;
    job = calloc(1, sizeof *job); if (!job) return TIKU_RESOURCE_NO_MEMORY;
    if (prefs_copy(&job->next, &owner->accepted) || file_copy(&job->before, &owner->file)) { release(job); return TIKU_RESOURCE_NO_MEMORY; }
    error = replace(owner, &job->next, path, value);
    if (error || prefs_copy(&job->live, &job->next)) { release(job); return error ? error : TIKU_RESOURCE_NO_MEMORY; }
    strcpy(job->id, id); strcpy(job->path, owner->path); strcpy(job->resource, path); job->write = 1;
    owner->generation++;
    if (post(owner, job)) return TIKU_RESOURCE_BUSY;
    return TIKU_RESOURCE_PENDING;
}

/** @brief Register the preference table with typed schemas and its existing controllers. */
static int register_settings(tiku_trk_settings_t *owner)
{
    size_t count = (size_t)owner->model->n + 1 + (owner->records ? tiku_trk_record_settings_count() : 0) + tiku_trk_catalog_settings_count(owner->catalog) + tiku_trk_desktop_settings_count(owner->desktop) + (owner->desktop != NULL);
    tiku_resource_initial_t *initial = calloc(count, sizeof *initial);
    tiku_resource_async_controller_t controller = {.validate = validate, .start = start, .execution_ms = 300000};
    int i, error = -1;
    if (!initial) return -1;
    for (i = 0; i < owner->model->n; i++) {
        const tiku_trk_pref_t *pref = &owner->model->pref[i];
        int theme = !strcmp(pref->name, "DarkMode");
        initial[i].spec = (tiku_resource_spec_t){.path = owner->paths[i], .kind = !strcmp(pref->name, "CurrentDevice") || !strcmp(pref->name, "CurrentDevicePort") ? TIKU_RESOURCE_STATE : TIKU_RESOURCE_SETTING, .schema_version = 1,
            .schema = {.type = theme ? TIKU_RESOURCE_ENUM : pref->type == TIKU_TRK_PREF_BOOL ? TIKU_RESOURCE_BOOLEAN : pref->type == TIKU_TRK_PREF_TEXT ? TIKU_RESOURCE_TEXT : pref->type == TIKU_TRK_PREF_COLOUR ? TIKU_RESOURCE_COLOR : TIKU_RESOURCE_INTEGER,
                .integer_min = INT32_MIN, .integer_max = INT32_MAX, .text_max = TIKU_TRK_PREF_TEXT_MAX - 1,
                .choices = theme ? themes : NULL, .choice_count = theme ? 3 : 0},
            .read_scopes = TIKU_RESOURCE_READ, .change_scopes = TIKU_RESOURCE_WRITE, .public_read = 1, .persistence = "user", .controller = "desktop-preferences"};
        if (!strcmp(pref->name, "sound.volume")) { initial[i].spec.schema.integer_min = 0; initial[i].spec.schema.integer_max = 100; }
        if (pref->type == TIKU_TRK_PREF_COLOUR) { initial[i].spec.schema.integer_min = 0; initial[i].spec.schema.integer_max = UINT32_MAX; }
        if (pref_snapshot(pref, &initial[i].initial)) goto done;
    }
    initial[i].spec = (tiku_resource_spec_t){.path = owner->paths[i], .kind = TIKU_RESOURCE_SETTING, .schema_version = 1,
        .schema = {.type = TIKU_RESOURCE_OBJECT}, .read_scopes = TIKU_RESOURCE_READ, .change_scopes = TIKU_RESOURCE_WRITE,
        .public_read = 1, .persistence = "user", .controller = "desktop-preferences"};
    if (settings_snapshot(owner->model, i, &initial[i].initial)) goto done;
    if (owner->records && tiku_trk_record_settings_initial(owner->records, initial + owner->model->n + 1)) goto done;
    if (owner->catalog && tiku_trk_catalog_settings_initial(owner->catalog, initial + owner->model->n + 1 + (owner->records ? tiku_trk_record_settings_count() : 0))) goto done;
    if (owner->desktop && tiku_trk_desktop_settings_initial(owner->desktop, initial + count - 1 - tiku_trk_desktop_settings_count(owner->desktop))) goto done;
    if (owner->desktop) {
        int motion = index_for(owner, "/settings/desktop/reduce_motion");
        if (motion < 0) goto done;
        initial[count - 1].spec = initial[motion].spec;
        initial[count - 1].spec.path = "/settings/desktop/reduce_motion";
        if (pref_snapshot(&owner->model->pref[motion], &initial[count - 1].initial)) goto done;
    }
    error = tiku_resource_register_async(owner->registry, "settings", "/settings", NULL, initial, count, &controller, owner, &owner->stamp);
    if (!error && owner->records) tiku_trk_record_settings_bind(owner->records, owner->registry);
    if (!error && owner->catalog) tiku_trk_catalog_settings_bind(owner->catalog, owner->registry);
    if (!error && owner->desktop) tiku_trk_desktop_settings_bind(owner->desktop, owner->registry);
done:
    for (i = 0; (size_t)i < count; i++) tiku_resource_snapshot_clear(&initial[i].initial);
    free(initial); return error;
}

tiku_trk_settings_t *tiku_trk_settings_open_with_desktop(tiku_trk_prefs_t *model, const char *path,
    tiku_resource_registry_t *registry, tiku_trk_record_settings_t *records, tiku_trk_catalog_settings_t *catalog, tiku_trk_desktop_settings_t *desktop, void (*changed)(void *), void *context)
{
    tiku_trk_settings_t *owner; int i;
    if (!model || !model->pref || !path || !*path || strlen(path) >= SETTINGS_PATH_MAX || model->submit_save) return NULL;
    for (i = 0; i < model->n; i++) {
        tiku_trk_pref_t *pref = &model->pref[i];
        if (!strcmp(pref->name, "DarkMode") && (pref->value < 0 || pref->value > 2)) pref->value = 0;
        if (!strcmp(pref->name, "sound.volume")) pref->value = pref->value < 0 ? 0 : pref->value > 100 ? 100 : pref->value;
    }
    if (!prefs_valid(model)) return NULL;
    owner = calloc(1, sizeof *owner); if (!owner) return NULL;
    owner->model = model; owner->registry = registry; owner->records = records; owner->catalog = catalog; owner->desktop = desktop; owner->changed = changed; owner->context = context; strcpy(owner->path, path);
    owner->paths = calloc((size_t)model->n + 1, sizeof *owner->paths);
    if (!owner->paths || prefs_copy(&owner->accepted, model)) goto fail;
    for (i = 0; i < model->n; i++) pref_path(model->pref[i].name, owner->paths[i]);
    strcpy(owner->paths[model->n], "/settings/profile");
    if (registry && register_settings(owner)) goto fail;
    model->save_context = owner; model->submit_save = submit;
    tiku_trk_settings_poll(owner, 1); return owner;
fail:
    tiku_trk_prefs_free(&owner->accepted); free(owner->paths); free(owner); return NULL;
}

tiku_trk_settings_t *tiku_trk_settings_open_with_catalog(tiku_trk_prefs_t *model, const char *path,
    tiku_resource_registry_t *registry, tiku_trk_record_settings_t *records, tiku_trk_catalog_settings_t *catalog, void (*changed)(void *), void *context)
{
    return tiku_trk_settings_open_with_desktop(model, path, registry, records, catalog, NULL, changed, context);
}

tiku_trk_settings_t *tiku_trk_settings_open_with_records(tiku_trk_prefs_t *model, const char *path,
    tiku_resource_registry_t *registry, tiku_trk_record_settings_t *records, void (*changed)(void *), void *context)
{
    return tiku_trk_settings_open_with_catalog(model, path, registry, records, NULL, changed, context);
}

tiku_trk_settings_t *tiku_trk_settings_open(tiku_trk_prefs_t *model, const char *path,
    tiku_resource_registry_t *registry, void (*changed)(void *), void *context)
{
    return tiku_trk_settings_open_with_records(model, path, registry, NULL, changed, context);
}

void tiku_trk_settings_poll(tiku_trk_settings_t *owner, uint64_t now)
{
    settings_job_t *job;
    if (!owner || !now || now < owner->next_poll || owner->reader || owner->writer) return;
    owner->next_poll = now + 100;
    job = calloc(1, sizeof *job); if (!job) return;
    if (owner->ready && !owner->error && file_copy(&job->before, &owner->file)) { release(job); return; }
    strcpy(job->path, owner->path); post(owner, job);
}

void tiku_trk_settings_close(tiku_trk_settings_t *owner)
{
    if (!owner) return;
    if (owner->reader) { owner->reader->owner = NULL; atomic_store(&owner->reader->cancelled, 1); }
    if (owner->writer) { owner->writer->owner = NULL; atomic_store(&owner->writer->cancelled, 1); }
    owner->model->submit_save = NULL; owner->model->save_context = NULL;
    if (owner->registry) tiku_resource_unregister(owner->registry, &owner->stamp);
    file_clear(&owner->file); tiku_trk_prefs_free(&owner->accepted); free(owner->paths); free(owner);
}

tiku_resource_error_t tiku_trk_settings_error(const tiku_trk_settings_t *owner)
{
    return owner ? owner->error : TIKU_RESOURCE_INVALID;
}

size_t tiku_trk_settings_count(const tiku_trk_settings_t *owner)
{
    return owner ? (size_t)owner->model->n + 1 + (owner->records ? tiku_trk_record_settings_count() : 0) + tiku_trk_catalog_settings_count(owner->catalog) + tiku_trk_desktop_settings_count(owner->desktop) + (owner->desktop != NULL) : 0;
}
