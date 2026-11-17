/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_static_app.h - static lifecycle and deferred application leases.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_STATIC_APP_H_
#define TIKU_STATIC_APP_H_
#include "tiku_allocator.h"
#include "tiku_event_queue.h"
#include "tiku_resource.h"

/** @brief Owner-thread application; lifecycle calls cannot reenter callbacks. */
typedef struct tiku_static_app tiku_static_app_t;
/** @brief Noncopyable lease; zero initialize and release before reusing. */
typedef struct { tiku_static_app_t *app; } tiku_app_lease_t;
/** @brief Static callbacks; state starts zeroed and has descriptor.state_bytes.
 * @note stop cancels work, including after failed start. dispose runs after
 * leases retire. dispatch handles one event or CANCEL per call. Callbacks
 * must bound their work; provider completions run outside these callbacks. */
typedef struct {
    size_t state_bytes;
    tiku_resource_error_t (*start)(void *state, tiku_static_app_t *, void *argument);
    void (*dispatch)(void *state, tiku_queue_result_t, const tiku_input_event_t *);
    void (*visibility)(void *state, int foreground);
    void (*stop)(void *state);
    void (*dispose)(void *state);
} tiku_static_app_ops_t;
/** @brief Allocate state and input; callbacks and allocator outlive the owner. */
tiku_static_app_t *tiku_static_app_create(tiku_allocator_t, const tiku_static_app_ops_t *, size_t events);
/** @brief Start once; failure invokes stop and leaves an owner to destroy. */
tiku_resource_error_t tiku_static_app_start(tiku_static_app_t *, void *argument);
/** @brief Deliver at most budget queued events while foreground; return count. */
size_t tiku_static_app_dispatch(tiku_static_app_t *, size_t budget);
/** @brief Queue foreground input; overflow schedules interaction cancellation. */
int tiku_static_app_post(tiku_static_app_t *, const tiku_input_event_t *);
/** @brief Change visibility; background clears input and cancels interactions. */
int tiku_static_app_foreground(tiku_static_app_t *, int foreground);
/** @brief Stop once and invalidate leases before the stop callback; idempotent. */
int tiku_static_app_stop(tiku_static_app_t *);
/** @brief Dispose stopped state after leases retire; busy leaves owner intact. */
int tiku_static_app_destroy(tiku_static_app_t **);
/** @brief Retain state during deferred work; allowed during start and running. */
int tiku_app_lease_begin(tiku_static_app_t *, tiku_app_lease_t *);
/** @brief Borrow live state, or NULL after stopping; does not release the lease. */
void *tiku_app_lease_state(const tiku_app_lease_t *);
/** @brief Release without invoking application callbacks; empty is accepted. */
void tiku_app_lease_release(tiku_app_lease_t *);
#endif
