/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_supervisor_state.h - parent-published desktop supervision snapshot.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SUPERVISOR_STATE_H_
#define TIKU_SUPERVISOR_STATE_H_
#include <stdint.h>
/** @brief Same-host record sent once through the inherited supervisor socket. */
typedef struct {
    uint32_t version, restart_count;
    int64_t supervisor, child;
    int32_t has_exit, signal, status;
} tiku_trk_supervisor_state_t;
#endif
