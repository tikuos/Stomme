/*
 * KintsugiDesktop v0.01 Application Kit tests.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * test_print_rpc.c - focused user grants, replay and asynchronous ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_print_rpc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned replies, dispatched, cancelled;
static uint32_t last_what;
static int32_t last_status;
/** @brief Observe exact owner replies without opening an external service. */
int
__wrap_tiku_remote_session_tell(tiku_remote_session_t *session,
                                const tiku_msg_t *message)
{
    assert(session && session->used);
    replies++;
    last_what = tiku_msg_what(message);
    assert(tiku_msg_find_int32(
        message, last_what == TIKU_MSG_PRINT_READY ? "accepted" : "status", 0,
        &last_status));
    return 1;
}
/** @brief Accept one copied final PDF, without a native or physical printer. */
static int
dispatch(void *context, uint64_t ticket, const void *pdf, size_t bytes,
         const char *title)
{
    (void)context;
    assert(ticket && bytes == 6 && !memcmp(pdf, "%PDF-x", 6) &&
           !strcmp(title, "Fixture"));
    dispatched++;
    return 1;
}
/** @brief Record cancellation for an exact active ticket. */
static void
cancel(void *context, uint64_t ticket)
{
    (void)context;
    assert(ticket);
    cancelled++;
}
/** @brief Build one protocol request whose ID is local to its window. */
static tiku_msg_t *
message(uint32_t what, int64_t request)
{
    tiku_msg_t *m = tiku_msg_new(what);
    assert(m);
    assert(tiku_msg_add_int32(m, "window", 7) &&
           tiku_msg_add_int64(m, "request", request));
    if (what == TIKU_MSG_PRINT_PREPARE)
        assert(tiku_msg_add_string(m, "title", "Fixture"));
    if (what == TIKU_MSG_PRINT_PDF)
        assert(tiku_msg_add_blob(m, "pdf", "%PDF-x", 6));
    return m;
}
/** @brief Refuse stale authority while preserving the original accepted
 * operation. */
int
main(void)
{
    tiku_remote_session_t session = {.used = 1, .generation = 11, .fd = -1};
    session.win[0] = (tiku_remote_window_t){.session = &session,
                                            .generation = 22,
                                            .win_id = 7,
                                            .opened = 1,
                                            .window = (struct tiku_window *)1,
                                            .focused = 1,
                                            .link_gesture = 100};
    tiku_print_rpc_t rpc = {.dispatch = dispatch, .cancel = cancel};
    tiku_msg_t *prepare = message(TIKU_MSG_PRINT_PREPARE, 9),
               *pdf = message(TIKU_MSG_PRINT_PDF, 9);
    assert(tiku_print_rpc_message(&rpc, &session, prepare, 101));
    assert(last_what == TIKU_MSG_PRINT_READY && last_status == 1 &&
           rpc.slots[3].ticket);
    uint64_t ticket = rpc.slots[3].ticket;
    tiku_msg_t *replay = message(TIKU_MSG_PRINT_PREPARE, 10);
    assert(tiku_print_rpc_message(&rpc, &session, replay, 102));
    assert(last_status == 0 && !session.win[0].link_gesture);
    tiku_remote_session_t stranger = {.used = 1, .generation = 88, .fd = -1};
    assert(tiku_print_rpc_message(&rpc, &stranger, pdf, 102));
    assert(last_status == -1 && !dispatched);
    assert(tiku_print_rpc_message(&rpc, &session, pdf, 102) && dispatched == 1);
    assert(tiku_print_rpc_message(&rpc, &session, pdf, 103) &&
           dispatched == 1 && !cancelled);
    tiku_print_rpc_done(&rpc, ticket, 1, "Submitted");
    assert(last_what == TIKU_MSG_PRINT_DONE && last_status == 1);
    session.win[0].link_gesture = 100;
    assert(tiku_print_rpc_message(&rpc, &session, prepare, 2000101));
    assert(last_status == 0);
    session.win[0].link_gesture = 300;
    assert(tiku_print_rpc_message(&rpc, &session, prepare, 301));
    assert(last_status == 1);
    tiku_print_rpc_poll(&rpc, 60000301);
    assert(last_status == -1 && !rpc.slots[3].ticket);
    session.win[0].link_gesture = 400;
    assert(tiku_print_rpc_message(&rpc, &session, prepare, 401));
    assert(tiku_print_rpc_message(&rpc, &session, pdf, 402) && dispatched == 2);
    ticket = rpc.slots[3].ticket;
    session.win[0].generation++;
    tiku_print_rpc_poll(&rpc, 403);
    assert(cancelled == 1 && !rpc.slots[3].ticket);
    unsigned before = replies;
    tiku_print_rpc_done(&rpc, ticket, 1, "Late");
    assert(replies == before);
    session.win[0].link_gesture = 500;
    assert(tiku_print_rpc_message(&rpc, &session, prepare, 501));
    assert(tiku_print_rpc_message(&rpc, &session, pdf, 502) && dispatched == 3);
    session.generation++;
    tiku_print_rpc_poll(&rpc, 503);
    assert(cancelled == 2);
    session.win[0].link_gesture = 600;
    assert(tiku_print_rpc_message(&rpc, &session, prepare, 601));
    assert(tiku_print_rpc_message(&rpc, &session, pdf, 602));
    tiku_print_rpc_close(&rpc);
    assert(cancelled == 3);
    session.win[0].link_gesture=700;
    assert(tiku_print_rpc_message(&rpc,&session,prepare,701));ticket=rpc.slots[3].ticket;
    unsigned old_dispatched=dispatched;
    assert(!tiku_print_rpc_control(&rpc,ticket,session.generation+1,session.win[0].generation,"%PDF-x",6,0,702)&&dispatched==old_dispatched);
    assert(!tiku_print_rpc_control(&rpc,ticket,session.generation,session.win[0].generation+1,NULL,0,1,702)&&rpc.slots[3].ticket==ticket);
    assert(tiku_print_rpc_control(&rpc,ticket,session.generation,session.win[0].generation,"%PDF-x",6,0,702)==1&&dispatched==old_dispatched+1&&!session.win[0].link_gesture);
    assert(tiku_print_rpc_control(&rpc,ticket,session.generation,session.win[0].generation,NULL,0,1,703)==1&&!rpc.slots[3].ticket);
    assert(!tiku_print_rpc_control(&rpc,ticket,session.generation,session.win[0].generation,NULL,0,1,704));
    tiku_msg_free(prepare);
    tiku_msg_free(pdf);
    tiku_msg_free(replay);
    puts("print grants: focus, expiry, one-use authority, replay, "
         "window/connection replacement and late completion refusal PASS");
    return 0;
}
