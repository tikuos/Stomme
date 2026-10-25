/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_state_private.h - record store ownership and staged persistence state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_STATE_PRIVATE_H_
#define TIKU_STATE_PRIVATE_H_
#include "tiku_state.h"
#define STATE_NODE_MAX 256
#define STATE_ATTR_MAX 64
#define STATE_VAL_MAX 4096
#define SIDECAR_MAGIC "TRKSTATE1"

enum store_kind { STORE_XATTR, STORE_SIDECAR, STORE_MEMORY };
typedef struct state_rec {
    char node[STATE_NODE_MAX], attr[STATE_ATTR_MAX];
    unsigned char *val;
    size_t len;
    struct state_rec *next;
} state_rec_t;
struct tiku_store {
    enum store_kind kind;
    state_rec_t *recs;
    char path[512];
    int dirty, writable, needs_reload;
    struct tiku_store *persisted;
    struct tiku_store_update *update;
};
/** @brief Copy bounded cached records into an independent volatile store. */
tiku_store_t *tiku_state_private_copy(const tiku_store_t *);
#endif
