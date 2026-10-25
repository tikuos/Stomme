/*
 * KintsugiDesktop v0.01 Application Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_PRINT_RPC_H_
#define TIKU_PRINT_RPC_H_
#include "tiku_print_native.h"
#include "tiku_remote.h"
#define TIKU_PRINT_SLOTS 4
#define TIKU_MSG_PRINT_PREPARE 0x70727072u
#define TIKU_MSG_PRINT_READY 0x70727264u
#define TIKU_MSG_PRINT_PDF 0x70727064u
#define TIKU_MSG_PRINT_CANCEL 0x70726361u
#define TIKU_MSG_PRINT_DONE 0x7072646eu
/** @brief One request owned by an exact connection/window incarnation. */
typedef struct {
    tiku_remote_session_t *session;
    tiku_remote_window_t *window;
    uint64_t connection, incarnation, ticket;
    int64_t request, deadline;
    uint32_t window_id;
    int active;
    char title[256];
} tiku_print_slot_t;
/** @brief Owner-thread print grants; dispatch copies PDF before returning. */
typedef struct {
    tiku_print_slot_t slots[TIKU_PRINT_SLOTS];
    uint64_t serial;
    int (*dispatch)(void *context, uint64_t ticket, const void *pdf,
                    size_t bytes, const char *title);
    void (*cancel)(void *context, uint64_t ticket);
    void *context;
} tiku_print_rpc_t;
/** @brief Consume native print messages; one when recognized, zero otherwise.
 * Preparation requires a focused one-use host gesture. A PDF must follow
 * within sixty seconds and use its exact connection/window/request grant. */
int tiku_print_rpc_message(tiku_print_rpc_t *rpc,
                           tiku_remote_session_t *session,
                           const tiku_msg_t *message, int64_t now);
/** @brief Cancel grants whose windows closed/replaced or whose PDF timed out.
 */
void tiku_print_rpc_poll(tiku_print_rpc_t *rpc, int64_t now);
/** @brief Deliver native completion to a live matching owner, consuming its
 * grant. */
void tiku_print_rpc_done(tiku_print_rpc_t *rpc, uint64_t ticket, int status,
                         const char *reason);
/** @brief Cancel outstanding native jobs and clear borrowed ownership. */
void tiku_print_rpc_close(tiku_print_rpc_t *rpc);
/** @brief Dispatch final PDF or cancellation through an exact existing print grant.
 *  @return 1 on acceptance, 0 for stale/invalid ownership or preparation, -1 when
 *          native dispatch refuses. No user gesture or new grant is created. */
int tiku_print_rpc_control(tiku_print_rpc_t *,uint64_t ticket,uint64_t connection,
    uint64_t incarnation,const void *pdf,size_t bytes,int cancel,int64_t now);

#endif
