/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_inspector_private.h - native resource inspector and transport mailbox.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_INSPECTOR_PRIVATE_H_
#define TIKU_INSPECTOR_PRIVATE_H_
#include "tiku_app.h"
#include "tiku_app_resources.h"
#include "tiku_codeview.h"
#include "tiku_dialog.h"
#include "tiku_field_event.h"
#include "tiku_list_paint.h"
#include "tiku_resource_client.h"
#include <pthread.h>
/** @brief Native controls; mutating controls are absent from exported menus. */
enum {
    RI_CLOSE = 1,
    RI_CONNECT,
    RI_INSPECT,
    RI_UP,
    RI_MORE,
    RI_APPLY,
    RI_INVOKE,
    RI_CANCEL,
    RI_RECEIPT,
    RI_STREAM,
    RI_COPY_ENDPOINT,
    RI_ENDPOINT = 30
};
/** @brief One copied owner-thread command for the transport thread. */
typedef struct {
    int command; /**< native control */
    char directory[256], path[TIKU_RESOURCE_PATH_MAX],
        cursor[33]; /**< explicit inputs */
    tiku_msg_t *description,
        *value; /**< owned proposal and displayed revision */
} ri_request_t;
/** @brief One complete immutable transport result transferred to the UI. */
typedef struct {
    int command,
        refresh_required; /**< operation and changed/retired observation */
    tiku_resource_error_t error; /**< protocol or local validation result */
    char status[256], job[33],
        mutation[33]; /**< native progress and reconciliation IDs */
    char directory
        [256]; /**< endpoint whose grant and observations were captured */
    tiku_msg_t *description, *snapshot, *page, *result,
        *event;                    /**< owned public metadata */
    tiku_resource_caller_t caller; /**< authenticated scope grant */
    unsigned events;               /**< observed watch events */
    char *content;                 /**< owned bounded UTF-8 stream preview */
} ri_reply_t;
/** @brief Opaque mailbox; channel and model lifetimes stay on separate threads. */
typedef struct ri_transport ri_transport_t;
/** @brief Start the transport thread without opening a connection on the UI thread. */
ri_transport_t *ri_transport_open(void);
/** @brief Consume request fields on successful queueing; return zero, or -1 busy/closed. */
int ri_transport_submit(ri_transport_t *, ri_request_t *);
/** @brief Take the latest complete result without blocking on transport I/O. */
ri_reply_t *ri_transport_collect(ri_transport_t *);
/** @brief Whether a copied command is queued or executing. */
int ri_transport_busy(ri_transport_t *);
/** @brief Join bounded transport work before freeing the mailbox. */
void ri_transport_close(ri_transport_t *);
/** @brief Free an owned result and its copied messages. */
void ri_reply_free(ri_reply_t *);
/** @brief Inspector UI model; transport replies are adopted on the application thread. */
typedef struct {
    const tiku_app_services_t *services; /**< native host */
    tiku_surface_t *surface;
    uint32_t id; /**< window canvas and identity */
    int w, h, focus, dirty, refresh_required,
        pending;          /**< geometry and interaction */
    tiku_dialog_t dialog; /**< native buttons and endpoint field */
    tiku_field_t endpoint;
    tiku_field_input_t endpoint_input; /**< private endpoint path */
    tiku_editdoc_t *doc[3];
    tiku_codeview_t *view[3]; /**< path, response and proposal */
    tiku_list_t list;         /**< paged resource catalog */
    char rows[128][TIKU_RESOURCE_PATH_MAX],
        kinds[128][24]; /**< owned public entries */
    int count;
    char cursor[33], job[33], mutation[33],
        status[256];     /**< page and operation state */
    char connected[256]; /**< endpoint bound to the displayed observations */
    unsigned events;     /**< observed native changes */
    int64_t painted;     /**< last native frame time in microseconds */
    tiku_msg_t *description,
        *snapshot; /**< displayed metadata and native snapshot */
    tiku_resource_caller_t caller; /**< displayed authenticated grant */
    ri_transport_t *transport;     /**< independent channel owner */
} ri_app_t;
/** @brief Paint the inspector after native state or focus changes. */
void ri_paint(ri_app_t *);
/** @brief Dispatch one current native control; nonzero closes the window. */
int ri_action(ri_app_t *, int command);
/** @brief Adopt completed observations and job progress on the UI thread. */
void ri_collect(ri_app_t *);
#endif
