/*
 * KintsugiDesktop v0.01 Application Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * tiku_print_rpc.c - explicit user print grants and exact asynchronous
 * ownership. SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_print_rpc.h"
#include "tiku_access.h"
#include <limits.h>
#include <string.h>

/** @brief Whether a captured request still belongs to the same live window. */
static int
live(const tiku_print_slot_t *slot)
{
    return slot->ticket && slot->session && slot->session->used &&
           slot->window && slot->session->generation == slot->connection &&
           slot->window->generation == slot->incarnation &&
           slot->window->session == slot->session &&
           slot->window->win_id == slot->window_id && slot->window->window &&
           slot->window->opened;
}
/** @brief Queue a correlated status through the owning connection. */
static void
reply(tiku_remote_session_t *session, uint32_t window, int64_t request,
      uint32_t what, int status, const char *reason)
{
    tiku_msg_t *message = tiku_msg_new(what);
    if (message && tiku_msg_add_int32(message, "window", window) &&
        tiku_msg_add_int64(message, "request", request) &&
        tiku_msg_add_int32(message,
                           what == TIKU_MSG_PRINT_READY ? "accepted" : "status",
                           status) &&
        tiku_msg_add_string(message, "reason", reason ? reason : ""))
        (void)tiku_remote_session_tell(session, message);
    tiku_msg_free(message);
}
/** @brief Remove ownership before calling a possibly reentrant native cancel.
 */
static void
clear(tiku_print_rpc_t *rpc, tiku_print_slot_t *slot)
{
    uint64_t ticket = slot->ticket;
    int active = slot->active;
    memset(slot, 0, sizeof *slot);
    if (active && rpc->cancel)
        rpc->cancel(rpc->context, ticket);
}
/** @brief Resolve the source window without trusting an application's ID alone.
 */
static tiku_remote_window_t *
window_of(tiku_remote_session_t *session, uint32_t id)
{
    if (!session || !session->used)
        return NULL;
    for (unsigned i = 0; i < TIKU_REMOTE_WINDOWS; i++) {
        tiku_remote_window_t *window = session->win + i;
        if (window->session == session && window->opened && window->window &&
            window->win_id == id)
            return window;
    }
    return NULL;
}
/** @brief Issue a bounded grant after consuming an explicit focused gesture. */
static void
prepare(tiku_print_rpc_t *rpc, tiku_remote_session_t *session,
        const tiku_msg_t *message, int64_t now, uint32_t id, int64_t request)
{
    tiku_remote_window_t *window = window_of(session, id);
    const char *title = tiku_msg_find_string(message, "title", 0);
    tiku_print_slot_t *free_slot = NULL;
    for (unsigned i = 0; i < TIKU_PRINT_SLOTS; i++) {
        tiku_print_slot_t *slot = rpc->slots + i;
        if (slot->ticket && slot->session == session &&
            slot->connection == session->generation && slot->window_id == id &&
            slot->request == request) {
            reply(session, id, request, TIKU_MSG_PRINT_READY, 0,
                  "Print request already exists.");
            return;
        }
        if (!slot->ticket)
            free_slot = slot;
    }
    int64_t gesture = window ? window->link_gesture : 0;
    if (window)
        window->link_gesture = 0;
    if (!free_slot || !window || !window->focused || !gesture ||
        now < gesture || now - gesture > 2000000 || !title ||
        strlen(title) > 255 ||
        tiku_access_characters(title, strlen(title)) < 0 ||
        rpc->serial == UINT64_MAX) {
        reply(session, id, request, TIKU_MSG_PRINT_READY, 0,
              "Native print preparation refused.");
        return;
    }
    *free_slot = (tiku_print_slot_t){
        .session = session,
        .window = window,
        .connection = session->generation,
        .incarnation = window->generation,
        .ticket = ++rpc->serial,
        .request = request,
        .window_id = id,
        .deadline = now > INT64_MAX - 60000000 ? INT64_MAX : now + 60000000};
    strcpy(free_slot->title, title);
    reply(session, id, request, TIKU_MSG_PRINT_READY, 1,
          "Preparing the final PDF.");
}
int
tiku_print_rpc_message(tiku_print_rpc_t *rpc, tiku_remote_session_t *session,
                       const tiku_msg_t *message, int64_t now)
{
    uint32_t what = tiku_msg_what(message);
    if (what != TIKU_MSG_PRINT_PREPARE && what != TIKU_MSG_PRINT_PDF &&
        what != TIKU_MSG_PRINT_CANCEL)
        return 0;
    int32_t id;
    int64_t request;
    if (!rpc || !session || !session->used ||
        !tiku_msg_find_int32(message, "window", 0, &id) ||
        !tiku_msg_find_int64(message, "request", 0, &request) || request <= 0)
        return 1;
    if (what == TIKU_MSG_PRINT_PREPARE) {
        prepare(rpc, session, message, now, (uint32_t)id, request);
        return 1;
    }
    tiku_print_slot_t *slot = NULL;
    for (unsigned i = 0; i < TIKU_PRINT_SLOTS; i++)
        if (rpc->slots[i].session == session &&
            rpc->slots[i].connection == session->generation &&
            rpc->slots[i].window_id == (uint32_t)id &&
            rpc->slots[i].request == request && live(rpc->slots + i)) {
            slot = rpc->slots + i;
            break;
        }
    if (!slot) {
        reply(session, (uint32_t)id, request, TIKU_MSG_PRINT_DONE, -1,
              "Print grant is no longer available.");
        return 1;
    }
    if (what == TIKU_MSG_PRINT_CANCEL) {
        reply(session, slot->window_id, request, TIKU_MSG_PRINT_DONE, 0,
              "Printing cancelled.");
        clear(rpc, slot);
        return 1;
    }
    if (slot->active)
        return 1;
    size_t bytes = 0;
    const void *pdf = tiku_msg_find_blob(message, "pdf", 0, &bytes);
    if (now >= slot->deadline || !pdf || bytes < 5 ||
        bytes > TIKU_PRINT_PDF_MAX || memcmp(pdf, "%PDF-", 5) ||
        !rpc->dispatch ||
        !rpc->dispatch(rpc->context, slot->ticket, pdf, bytes, slot->title)) {
        reply(session, slot->window_id, request, TIKU_MSG_PRINT_DONE, -1,
              "Final PDF print request refused.");
        clear(rpc, slot);
    } else
        slot->active = 1;
    return 1;
}
void
tiku_print_rpc_done(tiku_print_rpc_t *rpc, uint64_t ticket, int status,
                    const char *reason)
{
    if (!rpc)
        return;
    for (unsigned i = 0; i < TIKU_PRINT_SLOTS; i++)
        if (rpc->slots[i].ticket == ticket) {
            tiku_print_slot_t slot = rpc->slots[i];
            memset(rpc->slots + i, 0, sizeof slot);
            if (live(&slot))
                reply(slot.session, slot.window_id, slot.request,
                      TIKU_MSG_PRINT_DONE, status, reason);
            return;
        }
}
void
tiku_print_rpc_poll(tiku_print_rpc_t *rpc, int64_t now)
{
    if (!rpc)
        return;
    for (unsigned i = 0; i < TIKU_PRINT_SLOTS; i++) {
        tiku_print_slot_t *slot = rpc->slots + i;
        if (!slot->ticket)
            continue;
        if (!live(slot) || (!slot->active && now >= slot->deadline)) {
            if (live(slot))
                reply(slot->session, slot->window_id, slot->request,
                      TIKU_MSG_PRINT_DONE, -1,
                      "Final PDF preparation timed out.");
            clear(rpc, slot);
        }
    }
}
void
tiku_print_rpc_close(tiku_print_rpc_t *rpc)
{
    if (rpc)
        for (unsigned i = 0; i < TIKU_PRINT_SLOTS; i++)
            clear(rpc, rpc->slots + i);
}

int tiku_print_rpc_control(tiku_print_rpc_t *rpc,uint64_t ticket,uint64_t connection,
    uint64_t incarnation,const void *pdf,size_t bytes,int cancel_request,int64_t now)
{
    if(!rpc||!ticket)return 0;
    tiku_print_slot_t *slot=NULL;
    for(unsigned i=0;i<TIKU_PRINT_SLOTS;i++)if(rpc->slots[i].ticket==ticket){slot=&rpc->slots[i];break;}
    if(!slot||!live(slot)||slot->connection!=connection||slot->incarnation!=incarnation)return 0;
    if(!cancel_request&&(slot->active||now>=slot->deadline||!pdf||bytes<5||bytes>TIKU_PRINT_PDF_MAX||memcmp(pdf,"%PDF-",5)))return 0;
    tiku_msg_t *message=tiku_msg_new(cancel_request?TIKU_MSG_PRINT_CANCEL:TIKU_MSG_PRINT_PDF);
    int ready=message&&tiku_msg_add_int32(message,"window",slot->window_id)&&tiku_msg_add_int64(message,"request",slot->request)&&
        (cancel_request||tiku_msg_add_blob(message,"pdf",pdf,bytes));
    if(!ready){tiku_msg_free(message);return 0;}
    (void)tiku_print_rpc_message(rpc,slot->session,message,now);tiku_msg_free(message);
    if(cancel_request)return 1;
    return slot->ticket==ticket&&slot->active?1:-1;
}
