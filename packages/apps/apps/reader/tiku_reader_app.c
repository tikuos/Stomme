/* tiku_reader_app.c -- TikuReader's descriptor: Help and local documents.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_app_product.h"
#include "tiku_reader_app_host.h"

/** @brief Start TikuReader, or Help when a startup message asks for it. */
static int start(void **state, const tiku_app_services_t *services)
{
    return tiku_reader_app_start(state, services, NULL);
}

static const tiku_app_descriptor_t descriptor = {
    .id = TIKU_PRODUCT_READER_ID, .name = TIKU_PRODUCT_READER_NAME, .start = start,
    .stop = tiku_reader_app_stop, .event_in = tiku_reader_app_event,
    .tick = tiku_reader_app_tick, .pick = tiku_reader_app_pick,
    .picked = tiku_reader_app_picked, .closed = tiku_reader_app_closed
};
/** @brief The one symbol a loader looks for; see tiku_app.h. */
__attribute__((visibility("default"))) const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, sizeof(tiku_app_descriptor_t), &descriptor
};
