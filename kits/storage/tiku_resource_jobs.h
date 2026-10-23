/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_jobs.h - bounded asynchronous controller requests.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RESOURCE_JOBS_H_
#define TIKU_RESOURCE_JOBS_H_
#include "tiku_resource_registry.h"

/** @brief Observable asynchronous request states. */
typedef enum {
    TIKU_RESOURCE_JOB_QUEUED,
    TIKU_RESOURCE_JOB_RUNNING,
    TIKU_RESOURCE_JOB_AWAITING_CONFIRMATION,
    TIKU_RESOURCE_JOB_SUCCEEDED,
    TIKU_RESOURCE_JOB_FAILED,
    TIKU_RESOURCE_JOB_CANCELLED,
    TIKU_RESOURCE_JOB_UNKNOWN
} tiku_resource_job_state_t;

/** @brief Cancellation acceptance; accepted running work still needs completion. */
typedef enum {
    TIKU_RESOURCE_CANCEL_ACCEPTED,
    TIKU_RESOURCE_CANCEL_TOO_LATE,
    TIKU_RESOURCE_CANCEL_UNSUPPORTED
} tiku_resource_cancel_t;

/** @brief Asynchronous controller callbacks executed on the registry thread.
 *  @note start returns pending after copying worker inputs, or a complete result.
 *        Workers deliver completion on the registry thread. They retain their
 *        own data lifetime and cannot use a retired controller's owner pointer.
 *        Callbacks return without blocking; pending results have empty values. */
typedef struct {
    tiku_resource_error_t (*validate)(void *, const char *, const tiku_msg_t *, int);
    tiku_resource_error_t (*start)(void *, const char *job, const char *path,
        const tiku_msg_t *value, int invoke, tiku_resource_snapshot_t *result);
    tiku_resource_cancel_t (*cancel)(void *, const char *job);
    unsigned execution_ms; /**< zero selects the 300000 ms maximum */
} tiku_resource_async_controller_t;

/** @brief Caller-owned job status; release its result with job_info_clear. */
typedef struct {
    char id[TIKU_RESOURCE_ID_MAX], request_id[TIKU_RESOURCE_ID_MAX];
    char path[TIKU_RESOURCE_PATH_MAX];
    tiku_resource_stamp_t stamp, resource_stamp;
    tiku_resource_job_state_t state;
    tiku_resource_error_t error;
    tiku_resource_effect_t effect;
    unsigned progress; /**< 0 to 1000; succeeded reports 1000 */
    int cancel_requested;
    uint64_t expires_at; /**< monotonic milliseconds */
    tiku_resource_snapshot_t result;
} tiku_resource_job_info_t;

/** @brief Stable protocol key for a job state. */
const char *tiku_resource_job_state_name(tiku_resource_job_state_t);
/** @brief Release a copied job result. */
void tiku_resource_job_info_clear(tiku_resource_job_info_t *);
/** @brief Register a provider which accepts queued controller changes. */
tiku_resource_error_t tiku_resource_register_async(tiku_resource_registry_t *,
    const char *key, const char *prefix, const char *principal,
    const tiku_resource_initial_t *, size_t count,
    const tiku_resource_async_controller_t *, void *owner, tiku_resource_stamp_t *);
/** @brief Queue a validated request, retaining its ID before any owner dispatch.
 *  @note connection identifies the authenticated broker connection, not payload.
 *        deadline_ms bounds queued acceptance and is between 1 and 30000. */
tiku_resource_error_t tiku_resource_job_submit(tiku_resource_registry_t *,
    const tiku_resource_caller_t *, const char *connection, const char *path,
    const tiku_resource_stamp_t *, const char *request_id, const tiku_msg_t *,
    int invoke, unsigned deadline_ms, tiku_resource_job_info_t *out);
/** @brief Process queued callbacks and deadlines; return starts. */
int tiku_resource_jobs_pump(tiku_resource_registry_t *);
/** @brief Complete running work on the registry thread with a current lifetime.
 *  @note The input snapshot is borrowed; stale/retired completions cannot publish. */
tiku_resource_error_t tiku_resource_job_complete(tiku_resource_registry_t *,
    const char *id, const tiku_resource_snapshot_t *, tiku_resource_error_t,
    tiku_resource_effect_t);
/** @brief Set progress and optional confirmation wait on live running work. */
tiku_resource_error_t tiku_resource_job_progress(tiku_resource_registry_t *,
    const char *id, unsigned progress, int awaiting_confirmation);
/** @brief Read a retained job outcome after caller and scope checks. */
tiku_resource_error_t tiku_resource_job_status(tiku_resource_registry_t *,
    const tiku_resource_caller_t *, const char *id, tiku_resource_job_info_t *out);
/** @brief Cancel queued work or ask its live controller to cancel running work. */
tiku_resource_error_t tiku_resource_job_cancel(tiku_resource_registry_t *,
    const tiku_resource_caller_t *, const char *id,
    const tiku_resource_stamp_t *expected, tiku_resource_cancel_t *out);
/** @brief Cancel queued requests from one retiring broker connection. */
void tiku_resource_jobs_disconnect(tiku_resource_registry_t *, const char *connection);

#endif
