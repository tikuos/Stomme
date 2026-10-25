/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_inspector_transport.c - copied SDK requests on a dedicated channel thread.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_inspector_private.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
struct ri_transport {
    pthread_t thread;
    pthread_mutex_t mutex;
    int stop, queued, busy, failed;
    ri_request_t request;
    ri_reply_t *reply;
};
/** @brief Channel-thread state; no pointer to the native UI model is retained. */
typedef struct {
    ri_transport_t *mailbox;
    tiku_resource_client_t *client;
    char directory[256], watch[33], job[33], mutation[33];
    unsigned events;
    int refresh_required;
} connection_t;
/** @brief Release copied command payloads. */
static void request_free(ri_request_t *r)
{
    tiku_msg_free(r->description);
    tiku_msg_free(r->value);
    memset(r, 0, sizeof *r);
}
void ri_reply_free(ri_reply_t *r)
{
    if (!r)
        return;
    tiku_msg_free(r->description);
    tiku_msg_free(r->snapshot);
    tiku_msg_free(r->page);
    tiku_msg_free(r->result);
    tiku_msg_free(r->event);
    free(r->content);
    free(r);
}
/** @brief Transfer a completed result while preserving bounded mailbox storage. */
static void deliver(connection_t *c, ri_reply_t *reply)
{
    if (!reply)
        return;
    if (c->client) {
        (void)tiku_resource_client_info(c->client, &reply->caller, NULL);
        const char *id = tiku_resource_client_mutation(c->client);
        if (*id)
            snprintf(c->mutation, sizeof c->mutation, "%s", id);
    }
    snprintf(reply->job, sizeof reply->job, "%s", c->job);
    snprintf(reply->mutation, sizeof reply->mutation, "%s", c->mutation);
    reply->events = c->events;
    reply->refresh_required |= c->refresh_required;
    snprintf(reply->directory, sizeof reply->directory, "%s", c->directory);
    pthread_mutex_lock(&c->mailbox->mutex);
    ri_reply_free(c->mailbox->reply);
    c->mailbox->reply = reply;
    c->mailbox->busy = 0;
    pthread_mutex_unlock(&c->mailbox->mutex);
}
/** @brief Keep endpoint and provider identity changes explicit, without retrying effects. */
static tiku_resource_error_t connect_to(connection_t *c, const char *directory)
{
    tiku_resource_caller_t caller;
    if (c->client && !strcmp(directory, c->directory) &&
        !tiku_resource_client_info(c->client, &caller, NULL))
        return TIKU_RESOURCE_OK;
    tiku_resource_client_close(c->client);
    c->client = NULL;
    c->watch[0] = 0;
    c->job[0] = 0;
    c->client = tiku_resource_client_open(directory, 3000);
    snprintf(c->directory, sizeof c->directory, "%s", directory);
    return c->client ? TIKU_RESOURCE_OK : TIKU_RESOURCE_UNAVAILABLE;
}
/** @brief Bind the displayed schema to the exact value snapshot used by the proposal. */
static tiku_resource_error_t align_stamp(tiku_msg_t *description,
                                         const tiku_msg_t *snapshot)
{
    tiku_resource_stamp_t a, b;
    if (!tiku_resource_client_stamp(description, &a) ||
        !tiku_resource_client_stamp(snapshot, &b) ||
        strcmp(a.provider, b.provider) || strcmp(a.incarnation, b.incarnation))
        return TIKU_RESOURCE_STALE;
    (void)tiku_msg_remove(description, "revision");
    return tiku_msg_add_string(description, "revision",
                               tiku_msg_find_string(snapshot, "revision", 0))
               ? TIKU_RESOURCE_OK
               : TIKU_RESOURCE_NO_MEMORY;
}
/** @brief Capture description, value and the first page from the same owner. */
static tiku_resource_error_t
inspect(connection_t *c, const ri_request_t *request, ri_reply_t *out)
{
    if (c->watch[0]) {
        (void)tiku_resource_client_unwatch(c->client, c->watch);
        c->watch[0] = 0;
    }
    tiku_resource_error_t e = tiku_resource_client_observe(
        c->client, "describe", request->path, &out->description);
    if (e)
        return e;
    const char *kind = tiku_msg_find_string(out->description, "kind", 0);
    if (!kind)
        return TIKU_RESOURCE_INVALID;
    int directory = !strcmp(kind, "directory"),
        collection = !strcmp(kind, "collection");
    if (directory || collection) {
        e = tiku_resource_client_list(c->client, request->path, NULL,
                                      &out->page);
        if (e)
            return e;
    }
    if (!directory) {
        e = tiku_resource_client_observe(c->client, "read", request->path,
                                         &out->snapshot);
        if (e)
            return e;
        e = align_stamp(out->description, out->snapshot);
        if (e)
            return e;
    }
    c->refresh_required = 0;
    if (tiku_msg_find_string(out->description, "provider", 0)) {
        tiku_msg_t *initial = NULL;
        e = tiku_resource_client_watch(c->client, out->description,
                                       request->path, &initial);
        if (!e) {
            const char *id = tiku_msg_find_string(initial, "subscription", 0);
            if (tiku_resource_id_valid(id))
                strcpy(c->watch, id);
            tiku_resource_stamp_t a, b;
            if (tiku_resource_client_stamp(initial, &a) &&
                tiku_resource_client_stamp(out->description, &b) &&
                a.revision != b.revision)
                c->refresh_required = 1;
        }
        tiku_msg_free(initial);
        if (e && e != TIKU_RESOURCE_LIMIT && e != TIKU_RESOURCE_UNSUPPORTED)
            return e;
    }
    return TIKU_RESOURCE_OK;
}
/** @brief Execute one copied user command through the public SDK. */
static void command(connection_t *c, const ri_request_t *request)
{
    ri_reply_t *out = calloc(1, sizeof *out);
    if (!out) {
        pthread_mutex_lock(&c->mailbox->mutex);
        c->mailbox->failed = 1;
        c->mailbox->busy = 0;
        pthread_mutex_unlock(&c->mailbox->mutex);
        return;
    }
    out->command = request->command;
    if ((request->command == RI_APPLY || request->command == RI_INVOKE ||
         request->command == RI_CANCEL) &&
        strcmp(c->directory, request->directory)) {
        out->error = TIKU_RESOURCE_STALE;
        goto done;
    }
    out->error = connect_to(c, request->directory);
    if (out->error)
        goto done;
    switch (request->command) {
    case RI_CONNECT:
    case RI_INSPECT:
        out->error = inspect(c, request, out);
        break;
    case RI_MORE:
        out->error = tiku_resource_client_list(c->client, request->path,
                                               request->cursor, &out->page);
        break;
    case RI_APPLY:
    case RI_INVOKE:
        out->error = tiku_resource_client_change(
            c->client, request->description, request->value,
            request->command == RI_INVOKE, &out->result);
        if (!out->error) {
            c->refresh_required = 1;
            const char *job = tiku_msg_find_string(out->result, "job", 0);
            if (tiku_resource_id_valid(job))
                strcpy(c->job, job);
        }
        break;
    case RI_CANCEL:
        out->error = c->job[0] ? tiku_resource_client_cancel(c->client, c->job,
                                                             &out->result)
                               : TIKU_RESOURCE_UNAVAILABLE;
        break;
    case RI_RECEIPT:
        out->error = c->mutation[0] ? tiku_resource_client_receipt(
                                          c->client, c->mutation, &out->result)
                                    : TIKU_RESOURCE_UNAVAILABLE;
        break;
    case RI_STREAM: {
        void *bytes = NULL;
        size_t n = 0;
        out->error = tiku_resource_client_stream(
            c->client, request->description, 65536, &bytes, &n);
        if (!out->error && strlen(bytes) == n &&
            tiku_resource_utf8_valid(bytes, n))
            out->content = bytes;
        else {
            if (!out->error)
                snprintf(
                    out->status, sizeof out->status,
                    "Binary stream (%zu bytes); use the command client to export.",
                    n);
            free(bytes);
        }
        break;
    }
    default:
        out->error = TIKU_RESOURCE_INVALID;
        break;
    }
done:
    if (out->error) {
        c->refresh_required = 1;
        tiku_msg_free(out->result);
        out->result = tiku_resource_client_error(c->client);
        snprintf(out->status, sizeof out->status,
                 "%s; refresh observations before retrying a proposal.",
                 tiku_resource_error_name(out->error));
    } else if (!out->status[0])
        snprintf(out->status, sizeof out->status, "%s",
                 c->refresh_required
                     ? "Changed during inspection; refresh before applying."
                     : "Native resource response received.");
    deliver(c, out);
}
/** @brief Observe job progress and watch changes without touching UI-owned documents. */
static void observe(connection_t *c)
{
    if (!c->client)
        return;
    ri_reply_t *out = calloc(1, sizeof *out);
    if (!out)
        return;
    int changed = 0;
    if (c->job[0]) {
        changed = 1;
        out->error = tiku_resource_client_job(c->client, c->job, &out->result);
        const char *state = tiku_msg_find_string(out->result, "state", 0),
                   *code = tiku_msg_find_string(out->result, "code", 0);
        if (out->error || !state) {
            snprintf(
                out->status, sizeof out->status,
                "Job status unavailable; retain its ID for reconciliation.");
            c->job[0] = 0;
        } else {
            snprintf(out->status, sizeof out->status, "Job %s: %s", state,
                     code ? code : "");
            if (strcmp(state, "queued") && strcmp(state, "running") &&
                strcmp(state, "awaiting-confirmation")) {
                c->job[0] = 0;
                c->refresh_required = 1;
            }
        }
    }
    for (int i = 0; i < 16; i++) {
        tiku_msg_t *event = NULL;
        tiku_resource_error_t e =
            tiku_resource_client_event(c->client, 0, &event);
        if (e == TIKU_RESOURCE_PENDING)
            break;
        const char *subscription =
            tiku_msg_find_string(event, "subscription", 0);
        if (!e && (!subscription || !c->watch[0] ||
                   strcmp(subscription, c->watch))) {
            tiku_msg_free(event);
            continue;
        }
        changed = 1;
        c->refresh_required = 1;
        c->events++;
        tiku_msg_free(out->event);
        out->event = event;
        if (!out->error)
            out->error = e;
        if (!out->result)
            snprintf(
                out->status, sizeof out->status, "%s",
                e == TIKU_RESOURCE_LIMIT
                    ? "Watch overflow; refresh to obtain a complete snapshot."
                : e ? "Resource connection unavailable; reconnect to inspect."
                    : "Resource changed or retired; refresh before applying.");
        if (e) {
            if (e != TIKU_RESOURCE_LIMIT) {
                tiku_resource_client_close(c->client);
                c->client = NULL;
                c->watch[0] = 0;
            }
            break;
        }
    }
    if (changed)
        deliver(c, out);
    else
        ri_reply_free(out);
}
/** @brief Own the channel and mailbox until a bounded in-flight operation returns. */
static void *run(void *context)
{
    ri_transport_t *m = context;
    connection_t c = {.mailbox = m};
    unsigned ticks = 0;
    for (;;) {
        ri_request_t request = {0};
        int queued, stop, unread;
        pthread_mutex_lock(&m->mutex);
        stop = m->stop;
        queued = m->queued;
        unread = m->reply != NULL;
        if (queued) {
            request = m->request;
            memset(&m->request, 0, sizeof m->request);
            m->queued = 0;
            m->busy = 1;
        }
        pthread_mutex_unlock(&m->mutex);
        if (stop) {
            request_free(&request);
            break;
        }
        if (queued) {
            command(&c, &request);
            request_free(&request);
        } else if (!unread && ++ticks % 10 == 0)
            observe(&c);
        usleep(10000);
    }
    tiku_resource_client_close(c.client);
    return NULL;
}
ri_transport_t *ri_transport_open(void)
{
    ri_transport_t *m = calloc(1, sizeof *m);
    if (!m)
        return NULL;
    if (pthread_mutex_init(&m->mutex, NULL)) {
        free(m);
        return NULL;
    }
    if (pthread_create(&m->thread, NULL, run, m)) {
        pthread_mutex_destroy(&m->mutex);
        free(m);
        return NULL;
    }
    return m;
}
int ri_transport_submit(ri_transport_t *m, ri_request_t *request)
{
    if (!m || !request)
        return -1;
    pthread_mutex_lock(&m->mutex);
    if (m->stop || m->busy || m->queued || m->reply) {
        pthread_mutex_unlock(&m->mutex);
        return -1;
    }
    m->request = *request;
    memset(request, 0, sizeof *request);
    m->queued = 1;
    pthread_mutex_unlock(&m->mutex);
    return 0;
}
ri_reply_t *ri_transport_collect(ri_transport_t *m)
{
    if (!m)
        return NULL;
    pthread_mutex_lock(&m->mutex);
    ri_reply_t *reply = m->reply;
    m->reply = NULL;
    if (!reply && m->failed) {
        reply = calloc(1, sizeof *reply);
        if (reply) {
            reply->error = TIKU_RESOURCE_NO_MEMORY;
            snprintf(reply->status, sizeof reply->status,
                     "Out of memory; the command was not dispatched.");
            m->failed = 0;
        }
    }
    pthread_mutex_unlock(&m->mutex);
    return reply;
}
int ri_transport_busy(ri_transport_t *m)
{
    if (!m)
        return 0;
    pthread_mutex_lock(&m->mutex);
    int busy = m->queued || m->busy;
    pthread_mutex_unlock(&m->mutex);
    return busy;
}
void ri_transport_close(ri_transport_t *m)
{
    if (!m)
        return;
    pthread_mutex_lock(&m->mutex);
    m->stop = 1;
    pthread_mutex_unlock(&m->mutex);
    pthread_join(m->thread, NULL);
    request_free(&m->request);
    ri_reply_free(m->reply);
    pthread_mutex_destroy(&m->mutex);
    free(m);
}
