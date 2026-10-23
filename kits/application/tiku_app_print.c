/*
 * KintsugiDesktop v0.01 Application Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * tiku_app_print.c - explicit native print preparation and captured PDF
 * requests. SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_access.h"
#include "tiku_app.h"
#include "tiku_print_rpc.h"
#include <string.h>

/** @brief Queue one immutable print message with its exact caller correlation.
 */
static int
send(const tiku_app_services_t *services, uint32_t window, int64_t request,
     uint32_t what, const char *title, const void *pdf, size_t bytes)
{
    if (!services || !services->send_message || !window || request <= 0)
        return 0;
    tiku_msg_t *message = tiku_msg_new(what);
    int accepted = message && tiku_msg_add_int32(message, "window", window) &&
                   tiku_msg_add_int64(message, "request", request) &&
                   (!title || tiku_msg_add_string(message, "title", title)) &&
                   (!pdf || tiku_msg_add_blob(message, "pdf", pdf, bytes)) &&
                   services->send_message(services->ctx, message);
    tiku_msg_free(message);
    return accepted;
}
int
tiku_app_print_prepare(const tiku_app_services_t *services, uint32_t window,
                       int64_t request, const char *title)
{
    if (!title || strlen(title) > 255 ||
        tiku_access_characters(title, strlen(title)) < 0)
        return 0;
    return send(services, window, request, TIKU_MSG_PRINT_PREPARE, title, NULL,
                0);
}
int
tiku_app_print_pdf(const tiku_app_services_t *services, uint32_t window,
                   int64_t request, const void *pdf, size_t bytes)
{
    if (!pdf || bytes < 5 || bytes > TIKU_PRINT_PDF_MAX ||
        memcmp(pdf, "%PDF-", 5))
        return 0;
    return send(services, window, request, TIKU_MSG_PRINT_PDF, NULL, pdf,
                bytes);
}
void
tiku_app_print_cancel(const tiku_app_services_t *services, uint32_t window,
                      int64_t request)
{
    (void)send(services, window, request, TIKU_MSG_PRINT_CANCEL, NULL, NULL, 0);
}
