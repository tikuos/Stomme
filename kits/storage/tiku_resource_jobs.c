/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_jobs.c - queued controller work and retained outcomes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_registry_private.h"
#include <stdlib.h>
#include <string.h>

const char *tiku_resource_job_state_name(tiku_resource_job_state_t state)
{
    static const char *const names[] = {"queued", "running", "awaiting_confirmation",
        "succeeded", "failed", "cancelled", "unknown_outcome"};
    return (unsigned)state < sizeof names / sizeof names[0] ? names[state] : "unknown_outcome";
}

void tiku_resource_job_info_clear(tiku_resource_job_info_t *info)
{
    if (info) { tiku_resource_snapshot_clear(&info->result); memset(info, 0, sizeof *info); }
}

/** @brief Whether a job retains active controller work. */
static int active(const resource_job_t *job)
{
    return job->info.id[0] && job->info.state <= TIKU_RESOURCE_JOB_AWAITING_CONFIRMATION;
}

/** @brief Release one job's copied arguments and result. */
static void clear(resource_job_t *job)
{
    tiku_msg_free(job->request);
    tiku_resource_job_info_clear(&job->info);
    memset(job, 0, sizeof *job);
}

void tiku_resource_jobs_drop(tiku_resource_registry_t *registry)
{
    unsigned i;
    if (!registry->jobs) return;
    for (i = 0; i < RESOURCE_JOBS_MAX; i++) clear(&registry->jobs[i]);
    free(registry->jobs); registry->jobs = NULL;
}

/** @brief Advance the job controller stamp without unsigned wraparound. */
static int bump(tiku_resource_registry_t *registry)
{
    if (registry->jobs_stamp.revision == UINT64_MAX) {
        if (tiku_resource_new_id(registry->jobs_stamp.incarnation)) return 0;
        registry->jobs_stamp.revision = 1;
    } else registry->jobs_stamp.revision++;
    return 1;
}

/** @brief Allocate bookkeeping before accepting any controller work. */
static int initialize(tiku_resource_registry_t *registry)
{
    if (registry->jobs) return 1;
    registry->jobs = calloc(RESOURCE_JOBS_MAX, sizeof *registry->jobs);
    if (!registry->jobs) return 0;
    strcpy(registry->jobs_stamp.provider, "jobs");
    if (tiku_resource_new_id(registry->jobs_stamp.incarnation)) {
        free(registry->jobs); registry->jobs = NULL; return 0;
    }
    registry->jobs_stamp.revision = 1;
    return 1;
}

/** @brief Find a retained opaque job identity. */
static resource_job_t *find(tiku_resource_registry_t *registry, const char *id)
{
    unsigned i;
    if (!registry->jobs || !tiku_resource_id_valid(id)) return NULL;
    for (i = 0; i < RESOURCE_JOBS_MAX; i++)
        if (registry->jobs[i].info.id[0] && !strcmp(registry->jobs[i].info.id, id)) return &registry->jobs[i];
    return NULL;
}

/** @brief Expire completed records without evicting pending request identities. */
static void collect(tiku_resource_registry_t *registry, uint64_t now)
{
    unsigned i;
    if (!registry->jobs) return;
    for (i = 0; i < RESOURCE_JOBS_MAX; i++) {
        resource_job_t *job = &registry->jobs[i];
        if (job->info.id[0] && !active(job) && now >= job->completed && now - job->completed >= 60000)
            clear(job);
    }
}

resource_job_t *tiku_resource_jobs_request(tiku_resource_registry_t *registry, const char *principal, const char *id)
{
    unsigned i;
    collect(registry, tiku_resource_registry_now(registry));
    if (!registry->jobs) return NULL;
    for (i = 0; i < RESOURCE_JOBS_MAX; i++) {
        resource_job_t *job = &registry->jobs[i];
        if (job->info.id[0] && !strcmp(job->caller.principal, principal) && !strcmp(job->info.request_id, id)) return job;
    }
    return NULL;
}

/** @brief Check current authority before copying retained job data. */
static int readable(const resource_job_t *job, const tiku_resource_caller_t *caller)
{
    if (!caller || !caller->authenticated || !tiku_resource_id_valid(caller->principal) ||
        strcmp(job->caller.principal, caller->principal) ||
        (caller->scopes & job->read_scopes) != job->read_scopes) return 0;
    return !job->sensitive || ((caller->scopes & TIKU_RESOURCE_CONTENT) &&
        (!job->provider_principal[0] || !strcmp(job->provider_principal, caller->principal) ||
         (caller->scopes & TIKU_RESOURCE_CROSS_APP)));
}

/** @brief Copy a status with the current job-controller revision. */
static tiku_resource_error_t copy_info(tiku_resource_registry_t *registry, const resource_job_t *job,
                                      tiku_resource_job_info_t *out)
{
    *out = job->info;
    memset(&out->result, 0, sizeof out->result);
    out->stamp = registry->jobs_stamp;
    if (!tiku_resource_registry_copy(&out->result, &job->info.result)) {
        memset(out, 0, sizeof *out); return TIKU_RESOURCE_NO_MEMORY;
    }
    return TIKU_RESOURCE_OK;
}

/** @brief Update a retained receipt when its asynchronous request finishes. */
static void receipt_finish(tiku_resource_registry_t *registry, const resource_job_t *job)
{
    unsigned i;
    for (i = 0; i < registry->limits.receipts; i++) {
        resource_receipt_t *receipt = &registry->receipts[i];
        if (!receipt->id[0] || strcmp(receipt->job, job->info.id)) continue;
        receipt->pending = 0; receipt->completed = job->completed; receipt->error = job->info.error; receipt->effect = job->info.effect;
        tiku_resource_snapshot_clear(&receipt->result);
        if (!tiku_resource_registry_copy(&receipt->result, &job->info.result)) receipt->error = TIKU_RESOURCE_UNKNOWN_OUTCOME;
    }
}

/** @brief Retain a terminal result and bound completed-job capacity. */
static void terminal(tiku_resource_registry_t *registry, resource_job_t *job,
    tiku_resource_job_state_t state, tiku_resource_error_t error, tiku_resource_effect_t effect)
{
    unsigned i, completed = 0;
    resource_job_t *oldest = NULL;
    resource_node_t *node = tiku_resource_registry_node(registry, job->info.path);
    if (node && node->provider && !strcmp(node->provider->stamp.incarnation, job->expected.incarnation) &&
        !strcmp(node->provider->key, job->expected.provider)) job->info.resource_stamp = node->provider->stamp;
    if (state == TIKU_RESOURCE_JOB_SUCCEEDED) job->info.progress = 1000;
    job->info.state = state; job->info.error = error; job->info.effect = effect;
    job->completed = tiku_resource_registry_now(registry);
    job->info.expires_at = job->completed > UINT64_MAX - 60000 ? UINT64_MAX : job->completed + 60000;
    bump(registry);
    receipt_finish(registry, job);
    for (i = 0; i < RESOURCE_JOBS_MAX; i++) {
        resource_job_t *item = &registry->jobs[i];
        if (!item->info.id[0] || active(item)) continue;
        completed++;
        if (item != job && (!oldest || item->completed < oldest->completed)) oldest = item;
    }
    if (completed > RESOURCE_JOBS_DONE_MAX && oldest) clear(oldest);
}

/** @brief Reject direct synchronous execution of an asynchronous provider. */
static tiku_resource_error_t asynchronous_only(void *owner, const char *path,
    const tiku_msg_t *value, int invoke, tiku_resource_snapshot_t *out)
{
    (void)owner; (void)path; (void)value; (void)invoke; (void)out;
    return TIKU_RESOURCE_UNSUPPORTED;
}

tiku_resource_error_t tiku_resource_register_async(tiku_resource_registry_t *registry,
    const char *key, const char *prefix, const char *principal,
    const tiku_resource_initial_t *values, size_t count,
    const tiku_resource_async_controller_t *controller, void *owner, tiku_resource_stamp_t *out)
{
    tiku_resource_controller_t synchronous = {0};
    resource_provider_t *provider;
    tiku_resource_error_t error;
    if (!controller || !controller->start || controller->execution_ms > 300000) return TIKU_RESOURCE_INVALID;
    synchronous.validate = controller->validate; synchronous.apply = asynchronous_only;
    error = tiku_resource_register(registry, key, prefix, principal, values, count, &synchronous, owner, out);
    if (error) return error;
    provider = tiku_resource_registry_provider(registry, key);
    provider->async = *controller; provider->asynchronous = 1;
    return TIKU_RESOURCE_OK;
}

tiku_resource_error_t tiku_resource_job_submit(tiku_resource_registry_t *registry,
    const tiku_resource_caller_t *caller, const char *connection, const char *path,
    const tiku_resource_stamp_t *expected, const char *id, const tiku_msg_t *value,
    int invoke, unsigned deadline_ms, tiku_resource_job_info_t *out)
{
    resource_node_t *node;
    resource_provider_t *provider;
    resource_receipt_t *receipt;
    resource_job_t *job, *slot = NULL;
    tiku_resource_error_t error;
    uint64_t now;
    unsigned i, running = 0, owned = 0, execution;
    int existing;
    if (out) memset(out, 0, sizeof *out);
    if (!tiku_resource_registry_thread(registry) || !out || !tiku_resource_id_valid(connection) ||
        !tiku_resource_path_valid(path) || !tiku_resource_registry_stamp_valid(expected) ||
        !tiku_resource_id_valid(id) || (invoke != 0 && invoke != 1) || !deadline_ms || deadline_ms > 30000)
        return TIKU_RESOURCE_INVALID;
    if (registry->dispatching) return TIKU_RESOURCE_BUSY;
    node = tiku_resource_registry_node(registry, path);
    if (!node || !node->provider || !tiku_resource_registry_visible(node, caller)) return TIKU_RESOURCE_NOT_FOUND;
    if ((!invoke && node->spec.kind != TIKU_RESOURCE_SETTING) ||
        (invoke && node->spec.kind != TIKU_RESOURCE_ACTION)) return TIKU_RESOURCE_READ_ONLY;
    if (!tiku_resource_registry_authorized(node, caller, invoke)) return TIKU_RESOURCE_DENIED;
    provider = node->provider;
    if (!provider->asynchronous) return TIKU_RESOURCE_UNSUPPORTED;
    error = tiku_resource_registry_validate(&node->spec, value);
    if (error) return error;
    job = tiku_resource_jobs_request(registry, caller->principal, id);
    if (job) {
        if (strcmp(job->info.path, path) || job->invoke != invoke ||
            strcmp(job->expected.provider, expected->provider) ||
            strcmp(job->expected.incarnation, expected->incarnation) || job->expected.revision != expected->revision || job->acceptance_ms != deadline_ms ||
            !tiku_resource_value_equal(job->request, value)) return TIKU_RESOURCE_ID_REUSED;
        if (!readable(job, caller)) return TIKU_RESOURCE_DENIED;
        return copy_info(registry, job, out);
    }
    if (node->unavailable) return TIKU_RESOURCE_UNAVAILABLE;
    if (!tiku_resource_registry_matches(provider, expected)) return TIKU_RESOURCE_STALE;
    now = tiku_resource_registry_now(registry);
    execution = provider->async.execution_ms ? provider->async.execution_ms : 300000;
    if (!now || now > UINT64_MAX - 300000 || provider->stamp.revision == UINT64_MAX || registry->epoch == UINT64_MAX)
        return TIKU_RESOURCE_UNAVAILABLE;
    if (!initialize(registry)) return TIKU_RESOURCE_NO_MEMORY;
    for (i = 0; i < RESOURCE_JOBS_MAX; i++) {
        resource_job_t *item = &registry->jobs[i];
        if (!item->info.id[0] && !slot) slot = item;
        if (active(item)) { running++; if (!strcmp(item->caller.principal, caller->principal)) owned++; }
    }
    if (!slot || running >= RESOURCE_JOBS_ACTIVE_MAX || owned >= 16) return TIKU_RESOURCE_BUSY;
    receipt = tiku_resource_registry_receipt(registry, caller->principal, id, &existing);
    if (existing) return TIKU_RESOURCE_ID_REUSED;
    if (!receipt) return TIKU_RESOURCE_BUSY;
    if (provider->async.validate) {
        registry->dispatching = 1;
        error = provider->async.validate(provider->owner, path, value, invoke);
        registry->dispatching = 0;
        if (error) return error;
    }
    slot->request = tiku_msg_copy(value);
    if (!slot->request || tiku_resource_new_id(slot->info.id)) { clear(slot); return TIKU_RESOURCE_NO_MEMORY; }
    tiku_resource_registry_receipt_clear(receipt);
    receipt->request = tiku_msg_copy(value);
    if (!receipt->request) { clear(slot); return TIKU_RESOURCE_NO_MEMORY; }
    strcpy(slot->info.request_id, id); strcpy(slot->info.path, path);
    strcpy(slot->connection, connection); slot->caller = *caller; slot->expected = *expected;
    slot->info.resource_stamp = *expected;
    slot->invoke = invoke; slot->read_scopes = node->spec.read_scopes; slot->sensitive = node->spec.sensitive;
    strcpy(slot->provider_principal, provider->principal);
    slot->acceptance_ms = deadline_ms; slot->acceptance_deadline = now + deadline_ms; slot->execution_deadline = now + execution;
    slot->info.expires_at = slot->execution_deadline;
    strcpy(receipt->id, id); strcpy(receipt->principal, caller->principal); strcpy(receipt->path, path);
    strcpy(receipt->job, slot->info.id); strcpy(receipt->provider_principal, provider->principal);
    receipt->expected = *expected; receipt->invoke = invoke; receipt->pending = 1;
    receipt->read_scopes = node->spec.read_scopes; receipt->sensitive = node->spec.sensitive;
    if (!bump(registry)) { clear(slot); tiku_resource_registry_receipt_clear(receipt); return TIKU_RESOURCE_UNAVAILABLE; }
    error = copy_info(registry, slot, out);
    if (error) { clear(slot); tiku_resource_registry_receipt_clear(receipt); }
    return error;
}

/** @brief Resolve the matching live owner without keeping a controller pointer. */
static resource_node_t *live_node(tiku_resource_registry_t *registry, const resource_job_t *job)
{
    resource_node_t *node = tiku_resource_registry_node(registry, job->info.path);
    return node && node->provider && !strcmp(node->provider->key, job->expected.provider) &&
        !strcmp(node->provider->stamp.incarnation, job->expected.incarnation) ? node : NULL;
}

/** @brief Reconcile a completed result before publishing it to a live resource. */
static tiku_resource_error_t finish(tiku_resource_registry_t *registry, resource_job_t *job,
    const tiku_resource_snapshot_t *value, tiku_resource_error_t error, tiku_resource_effect_t effect)
{
    resource_node_t *node = live_node(registry, job);
    tiku_resource_snapshot_t copy = {0};
    if (!node) {
        job->rejected = TIKU_RESOURCE_OWNER_LOST;
        terminal(registry, job, TIKU_RESOURCE_JOB_UNKNOWN, TIKU_RESOURCE_UNKNOWN_OUTCOME, TIKU_RESOURCE_EFFECT_UNKNOWN);
        return TIKU_RESOURCE_OWNER_LOST;
    }
    if (!tiku_resource_registry_matches(node->provider, &job->expected)) {
        job->rejected = TIKU_RESOURCE_STALE;
        if (effect != TIKU_RESOURCE_EFFECT_NONE) tiku_resource_registry_uncertain(registry, node);
        terminal(registry, job, effect == TIKU_RESOURCE_EFFECT_NONE ? TIKU_RESOURCE_JOB_FAILED : TIKU_RESOURCE_JOB_UNKNOWN,
            effect == TIKU_RESOURCE_EFFECT_NONE ? TIKU_RESOURCE_STALE : TIKU_RESOURCE_UNKNOWN_OUTCOME, effect);
        return TIKU_RESOURCE_STALE;
    }
    if (value && value->desired) {
        if (tiku_resource_registry_snapshot_check(node, value) || !tiku_resource_registry_copy(&copy, value)) {
            tiku_resource_registry_uncertain(registry, node);
            terminal(registry, job, TIKU_RESOURCE_JOB_UNKNOWN, TIKU_RESOURCE_UNKNOWN_OUTCOME, TIKU_RESOURCE_EFFECT_UNKNOWN);
            return TIKU_RESOURCE_UNKNOWN_OUTCOME;
        }
        tiku_resource_registry_install(registry, node, &copy);
        if (!tiku_resource_registry_copy(&job->info.result, &node->value)) error = TIKU_RESOURCE_UNKNOWN_OUTCOME;
        job->info.resource_stamp = node->provider->stamp;
    } else if (!error || effect != TIKU_RESOURCE_EFFECT_NONE) {
        tiku_resource_registry_uncertain(registry, node);
        error = TIKU_RESOURCE_UNKNOWN_OUTCOME; effect = TIKU_RESOURCE_EFFECT_UNKNOWN;
    }
    if (error == TIKU_RESOURCE_UNKNOWN_OUTCOME) effect = TIKU_RESOURCE_EFFECT_UNKNOWN;
    terminal(registry, job, error == TIKU_RESOURCE_UNKNOWN_OUTCOME ? TIKU_RESOURCE_JOB_UNKNOWN :
        error == TIKU_RESOURCE_CANCELLED ? TIKU_RESOURCE_JOB_CANCELLED : error ? TIKU_RESOURCE_JOB_FAILED : TIKU_RESOURCE_JOB_SUCCEEDED,
        error, effect);
    return TIKU_RESOURCE_OK;
}

tiku_resource_error_t tiku_resource_job_complete(tiku_resource_registry_t *registry, const char *id,
    const tiku_resource_snapshot_t *value, tiku_resource_error_t error, tiku_resource_effect_t effect)
{
    resource_job_t *job;
    uint64_t now;
    if (!tiku_resource_registry_thread(registry) || !tiku_resource_id_valid(id) ||
        (unsigned)effect > TIKU_RESOURCE_EFFECT_UNKNOWN || error == TIKU_RESOURCE_PENDING) return TIKU_RESOURCE_INVALID;
    if (registry->dispatching) return TIKU_RESOURCE_BUSY;
    job = find(registry, id);
    if (!job) return TIKU_RESOURCE_OUTCOME_UNAVAILABLE;
    if (!active(job)) return job->rejected ? job->rejected : TIKU_RESOURCE_UNAVAILABLE;
    if (job->info.state == TIKU_RESOURCE_JOB_QUEUED) return TIKU_RESOURCE_INVALID;
    now = tiku_resource_registry_now(registry);
    if (!now || now >= job->execution_deadline) {
        job->rejected = TIKU_RESOURCE_DEADLINE;
        resource_node_t *node = live_node(registry, job);
        if (node) tiku_resource_registry_uncertain(registry, node);
        terminal(registry, job, TIKU_RESOURCE_JOB_UNKNOWN, TIKU_RESOURCE_UNKNOWN_OUTCOME, TIKU_RESOURCE_EFFECT_UNKNOWN);
        return TIKU_RESOURCE_DEADLINE;
    }
    return finish(registry, job, value, error, effect);
}

/** @brief Whether another running request reserves this provider revision. */
static int reserved(tiku_resource_registry_t *registry, const resource_job_t *job)
{
    unsigned i;
    for (i = 0; i < RESOURCE_JOBS_MAX; i++) {
        resource_job_t *other = &registry->jobs[i];
        if (other != job && active(other) && other->info.state != TIKU_RESOURCE_JOB_QUEUED &&
            !strcmp(other->expected.provider, job->expected.provider) &&
            !strcmp(other->expected.incarnation, job->expected.incarnation)) return 1;
    }
    return 0;
}

int tiku_resource_jobs_pump(tiku_resource_registry_t *registry)
{
    unsigned i;
    uint64_t now;
    int started = 0;
    if (!tiku_resource_registry_thread(registry) || registry->dispatching) return -1;
    now = tiku_resource_registry_now(registry);
    if (!now) return -1;
    collect(registry, now);
    if (!registry->jobs) return 0;
    for (i = 0; i < RESOURCE_JOBS_MAX; i++) {
        resource_job_t *job = &registry->jobs[i];
        resource_node_t *node;
        if (!active(job)) continue;
        node = live_node(registry, job);
        if (!node) {
            job->rejected = TIKU_RESOURCE_OWNER_LOST;
            terminal(registry, job, job->info.state == TIKU_RESOURCE_JOB_QUEUED ? TIKU_RESOURCE_JOB_FAILED : TIKU_RESOURCE_JOB_UNKNOWN,
                job->info.state == TIKU_RESOURCE_JOB_QUEUED ? TIKU_RESOURCE_OWNER_LOST : TIKU_RESOURCE_UNKNOWN_OUTCOME,
                job->info.state == TIKU_RESOURCE_JOB_QUEUED ? TIKU_RESOURCE_EFFECT_NONE : TIKU_RESOURCE_EFFECT_UNKNOWN);
        } else if (job->info.state == TIKU_RESOURCE_JOB_QUEUED) {
            tiku_resource_snapshot_t result = {0};
            tiku_resource_error_t error;
            if (now >= job->acceptance_deadline || now >= job->execution_deadline) {
                terminal(registry, job, TIKU_RESOURCE_JOB_FAILED, TIKU_RESOURCE_DEADLINE, TIKU_RESOURCE_EFFECT_NONE); continue;
            }
            if (!tiku_resource_registry_matches(node->provider, &job->expected)) {
                terminal(registry, job, TIKU_RESOURCE_JOB_FAILED, TIKU_RESOURCE_STALE, TIKU_RESOURCE_EFFECT_NONE); continue;
            }
            if (reserved(registry, job)) continue;
            if (node->provider->async.validate) {
                registry->dispatching = 1;
                error = node->provider->async.validate(node->provider->owner, job->info.path, job->request, job->invoke);
                registry->dispatching = 0;
                if (error) { terminal(registry, job, TIKU_RESOURCE_JOB_FAILED, error, TIKU_RESOURCE_EFFECT_NONE); continue; }
            }
            job->info.state = TIKU_RESOURCE_JOB_RUNNING; bump(registry); started++;
            registry->dispatching = 1;
            error = node->provider->async.start(node->provider->owner, job->info.id, job->info.path, job->request, job->invoke, &result);
            registry->dispatching = 0;
            if (error == TIKU_RESOURCE_PENDING && (result.desired || result.persisted || result.applied))
                tiku_resource_job_complete(registry, job->info.id, NULL, TIKU_RESOURCE_UNKNOWN_OUTCOME, TIKU_RESOURCE_EFFECT_UNKNOWN);
            else if (error != TIKU_RESOURCE_PENDING)
                tiku_resource_job_complete(registry, job->info.id, &result, error,
                    !error || result.desired ? TIKU_RESOURCE_EFFECT_PARTIAL : TIKU_RESOURCE_EFFECT_NONE);
            tiku_resource_snapshot_clear(&result);
        } else if (now >= job->execution_deadline) {
            job->rejected = TIKU_RESOURCE_DEADLINE;
            tiku_resource_registry_uncertain(registry, node);
            terminal(registry, job, TIKU_RESOURCE_JOB_UNKNOWN, TIKU_RESOURCE_UNKNOWN_OUTCOME, TIKU_RESOURCE_EFFECT_UNKNOWN);
        }
    }
    return started;
}

tiku_resource_error_t tiku_resource_job_status(tiku_resource_registry_t *registry,
    const tiku_resource_caller_t *caller, const char *id, tiku_resource_job_info_t *out)
{
    resource_job_t *job;
    if (out) memset(out, 0, sizeof *out);
    if (!tiku_resource_registry_thread(registry) || !out || !tiku_resource_id_valid(id)) return TIKU_RESOURCE_INVALID;
    collect(registry, tiku_resource_registry_now(registry));
    job = find(registry, id);
    if (!job || !readable(job, caller)) return TIKU_RESOURCE_OUTCOME_UNAVAILABLE;
    return copy_info(registry, job, out);
}

tiku_resource_error_t tiku_resource_job_progress(tiku_resource_registry_t *registry,
    const char *id, unsigned progress, int awaiting)
{
    resource_job_t *job;
    if (!tiku_resource_registry_thread(registry) || !tiku_resource_id_valid(id) || progress > 1000 || (awaiting != 0 && awaiting != 1)) return TIKU_RESOURCE_INVALID;
    if (registry->dispatching) return TIKU_RESOURCE_BUSY;
    job = find(registry, id);
    if (!job || !active(job) || job->info.state == TIKU_RESOURCE_JOB_QUEUED) return TIKU_RESOURCE_OWNER_LOST;
    if (tiku_resource_registry_now(registry) >= job->execution_deadline)
        return tiku_resource_job_complete(registry, id, NULL, TIKU_RESOURCE_UNKNOWN_OUTCOME, TIKU_RESOURCE_EFFECT_UNKNOWN);
    job->info.progress = progress;
    job->info.state = awaiting ? TIKU_RESOURCE_JOB_AWAITING_CONFIRMATION : TIKU_RESOURCE_JOB_RUNNING;
    return bump(registry) ? TIKU_RESOURCE_OK : TIKU_RESOURCE_UNAVAILABLE;
}

tiku_resource_error_t tiku_resource_job_cancel(tiku_resource_registry_t *registry,
    const tiku_resource_caller_t *caller, const char *id,
    const tiku_resource_stamp_t *expected, tiku_resource_cancel_t *out)
{
    resource_job_t *job;
    resource_node_t *node;
    if (!tiku_resource_registry_thread(registry) || !out || !tiku_resource_id_valid(id) || !tiku_resource_registry_stamp_valid(expected)) return TIKU_RESOURCE_INVALID;
    if (registry->dispatching) return TIKU_RESOURCE_BUSY;
    job = find(registry, id);
    if (!job || !readable(job, caller)) return TIKU_RESOURCE_OUTCOME_UNAVAILABLE;
    if (strcmp(expected->provider, registry->jobs_stamp.provider) ||
        strcmp(expected->incarnation, registry->jobs_stamp.incarnation) || expected->revision != registry->jobs_stamp.revision)
        return TIKU_RESOURCE_STALE;
    if (!active(job)) { *out = TIKU_RESOURCE_CANCEL_TOO_LATE; return TIKU_RESOURCE_OK; }
    node = live_node(registry, job);
    if (!node) return TIKU_RESOURCE_OWNER_LOST;
    if (!tiku_resource_registry_authorized(node, caller, job->invoke)) return TIKU_RESOURCE_DENIED;
    if (job->info.state == TIKU_RESOURCE_JOB_QUEUED) {
        terminal(registry, job, TIKU_RESOURCE_JOB_CANCELLED, TIKU_RESOURCE_CANCELLED, TIKU_RESOURCE_EFFECT_NONE);
        *out = TIKU_RESOURCE_CANCEL_ACCEPTED; return TIKU_RESOURCE_OK;
    }
    if (!node->provider->async.cancel) { *out = TIKU_RESOURCE_CANCEL_UNSUPPORTED; return TIKU_RESOURCE_OK; }
    if (job->info.cancel_requested) { *out = TIKU_RESOURCE_CANCEL_ACCEPTED; return TIKU_RESOURCE_OK; }
    registry->dispatching = 1;
    *out = node->provider->async.cancel(node->provider->owner, id);
    registry->dispatching = 0;
    if ((unsigned)*out > TIKU_RESOURCE_CANCEL_UNSUPPORTED) return TIKU_RESOURCE_UNKNOWN_OUTCOME;
    if (*out == TIKU_RESOURCE_CANCEL_ACCEPTED) { job->info.cancel_requested = 1; bump(registry); }
    return TIKU_RESOURCE_OK;
}

void tiku_resource_jobs_disconnect(tiku_resource_registry_t *registry, const char *connection)
{
    unsigned i;
    if (!tiku_resource_registry_thread(registry) || registry->dispatching || !connection || !registry->jobs) return;
    for (i = 0; i < RESOURCE_JOBS_MAX; i++) {
        resource_job_t *job = &registry->jobs[i];
        if (active(job) && job->info.state == TIKU_RESOURCE_JOB_QUEUED && !strcmp(job->connection, connection))
            terminal(registry, job, TIKU_RESOURCE_JOB_CANCELLED, TIKU_RESOURCE_CANCELLED, TIKU_RESOURCE_EFFECT_NONE);
    }
}

void tiku_resource_jobs_owner_lost(tiku_resource_registry_t *registry, const resource_provider_t *provider)
{
    unsigned i;
    if (!registry->jobs) return;
    for (i = 0; i < RESOURCE_JOBS_MAX; i++) {
        resource_job_t *job = &registry->jobs[i];
        if (!active(job) || strcmp(job->expected.provider, provider->key) || strcmp(job->expected.incarnation, provider->stamp.incarnation)) continue;
        job->rejected = TIKU_RESOURCE_OWNER_LOST;
        terminal(registry, job, job->info.state == TIKU_RESOURCE_JOB_QUEUED ? TIKU_RESOURCE_JOB_FAILED : TIKU_RESOURCE_JOB_UNKNOWN,
            job->info.state == TIKU_RESOURCE_JOB_QUEUED ? TIKU_RESOURCE_OWNER_LOST : TIKU_RESOURCE_UNKNOWN_OUTCOME,
            job->info.state == TIKU_RESOURCE_JOB_QUEUED ? TIKU_RESOURCE_EFFECT_NONE : TIKU_RESOURCE_EFFECT_UNKNOWN);
    }
}
