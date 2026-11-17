/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_event_queue.h - bounded input and deferred producer ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_EVENT_QUEUE_H_
#define TIKU_EVENT_QUEUE_H_
#include "tiku_allocator.h"
#include "tiku_input.h"

/** @brief Serialized input queue; producers and consumer share one owner thread. */
typedef struct tiku_event_queue tiku_event_queue_t;
/** @brief Noncopyable deferred producer ticket; complete or cancel before reuse. */
typedef struct {
    tiku_event_queue_t *queue;
    uint64_t generation;
} tiku_event_ticket_t;
/** @brief A cancellation result requires resetting pressed keys, pointer and preedit. */
typedef enum {
    TIKU_QUEUE_CANCEL = -1,
    TIKU_QUEUE_EMPTY = 0,
    TIKU_QUEUE_EVENT = 1
} tiku_queue_result_t;

/** @brief Create an open queue; allocation failure releases partial construction. */
tiku_event_queue_t *tiku_event_queue_create(tiku_allocator_t allocator, size_t capacity);
/** @brief Close and drop pending input; outstanding tickets retain queue storage. */
void tiku_event_queue_close(tiku_event_queue_t *queue);
/** @brief Reopen with a fresh generation; refuses an open or exhausted queue. */
int tiku_event_queue_open(tiku_event_queue_t *queue);
/** @brief Destroy a closed queue without tickets; busy failure leaves it intact. */
int tiku_event_queue_destroy(tiku_event_queue_t **queue);
/** @brief Enqueue input; overflow cancels input, invalidates tickets and schedules CANCEL. */
int tiku_event_queue_push(tiku_event_queue_t *queue, const tiku_input_event_t *event);
/** @brief Take one event or cancellation; output is written for EVENT only. */
tiku_queue_result_t tiku_event_queue_take(tiku_event_queue_t *queue,
                                          tiku_input_event_t *event);
/** @brief Acquire an empty ticket; its queue remains alive until completion. */
int tiku_event_ticket_begin(tiku_event_queue_t *queue, tiku_event_ticket_t *ticket);
/** @brief Consume the ticket and post if still current; stale completion is dropped. */
int tiku_event_ticket_post(tiku_event_ticket_t *ticket, const tiku_input_event_t *event);
/** @brief Consume a ticket without posting; accepts NULL or an empty ticket. */
void tiku_event_ticket_cancel(tiku_event_ticket_t *ticket);
#endif
