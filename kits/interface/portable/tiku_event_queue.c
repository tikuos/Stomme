/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_event_queue.c - bounded input with generation-checked deferred producers.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_event_queue.h"

struct tiku_event_queue {
    tiku_owned_buffer_t self;
    tiku_owned_buffer_t storage;
    size_t capacity, head, count, tickets;
    uint64_t generation;
    int open, cancel;
};

tiku_event_queue_t *tiku_event_queue_create(tiku_allocator_t allocator, size_t capacity)
{
    tiku_owned_buffer_t self;
    tiku_event_queue_t *queue;
    if (!capacity || capacity > SIZE_MAX / sizeof(tiku_input_event_t) ||
        !tiku_buffer_init(&self, allocator, _Alignof(tiku_event_queue_t)) ||
        !tiku_buffer_resize(&self, 1, sizeof(tiku_event_queue_t), 1)) {
        return NULL;
    }
    queue = self.data;
    queue->self = self;
    if (!tiku_buffer_init(&queue->storage, allocator, _Alignof(tiku_input_event_t)) ||
        !tiku_buffer_resize(&queue->storage, capacity, sizeof(tiku_input_event_t), 1)) {
        tiku_buffer_dispose(&self);
        return NULL;
    }
    queue->capacity = capacity;
    queue->generation = 1;
    queue->open = 1;
    return queue;
}

void tiku_event_queue_close(tiku_event_queue_t *queue)
{
    if (queue) {
        queue->open = 0;
        queue->count = queue->head = 0;
        queue->cancel = 1;
    }
}

int tiku_event_queue_open(tiku_event_queue_t *queue)
{
    if (!queue || queue->open || queue->generation == UINT64_MAX) {
        return 0;
    }
    ++queue->generation;
    queue->open = 1;
    return 1;
}

int tiku_event_queue_destroy(tiku_event_queue_t **queue)
{
    tiku_owned_buffer_t self;
    if (!queue || !*queue) {
        return 1;
    }
    if ((*queue)->open || (*queue)->tickets) {
        return 0;
    }
    self = (*queue)->self;
    tiku_buffer_dispose(&(*queue)->storage);
    tiku_buffer_dispose(&self);
    *queue = NULL;
    return 1;
}

int tiku_event_queue_push(tiku_event_queue_t *queue, const tiku_input_event_t *event)
{
    tiku_input_event_t *events;
    size_t index;
    if (!queue || !queue->open || !event || queue->cancel) {
        return 0;
    }
    if (queue->count == queue->capacity) {
        queue->head = queue->count = 0;
        queue->cancel = 1;
        if (queue->generation == UINT64_MAX) {
            queue->open = 0;
        } else {
            ++queue->generation;
        }
        return 0;
    }
    events = queue->storage.data;
    index = queue->head + queue->count;
    if (index >= queue->capacity) {
        index -= queue->capacity;
    }
    events[index] = *event;
    ++queue->count;
    return 1;
}

tiku_queue_result_t tiku_event_queue_take(tiku_event_queue_t *queue,
                                          tiku_input_event_t *event)
{
    tiku_input_event_t *events;
    if (!queue || !event) {
        return TIKU_QUEUE_EMPTY;
    }
    if (queue->cancel) {
        queue->cancel = 0;
        return TIKU_QUEUE_CANCEL;
    }
    if (!queue->count) {
        return TIKU_QUEUE_EMPTY;
    }
    events = queue->storage.data;
    *event = events[queue->head];
    if (++queue->head == queue->capacity) {
        queue->head = 0;
    }
    --queue->count;
    return TIKU_QUEUE_EVENT;
}

int tiku_event_ticket_begin(tiku_event_queue_t *queue, tiku_event_ticket_t *ticket)
{
    if (!queue || !queue->open || !ticket || ticket->queue ||
        queue->tickets == SIZE_MAX) {
        return 0;
    }
    ++queue->tickets;
    ticket->queue = queue;
    ticket->generation = queue->generation;
    return 1;
}

void tiku_event_ticket_cancel(tiku_event_ticket_t *ticket)
{
    if (ticket && ticket->queue) {
        --ticket->queue->tickets;
        ticket->queue = NULL;
        ticket->generation = 0;
    }
}

int tiku_event_ticket_post(tiku_event_ticket_t *ticket, const tiku_input_event_t *event)
{
    int accepted = 0;
    if (ticket && ticket->queue) {
        if (ticket->generation == ticket->queue->generation) {
            accepted = tiku_event_queue_push(ticket->queue, event);
        }
        tiku_event_ticket_cancel(ticket);
    }
    return accepted;
}
