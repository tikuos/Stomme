/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desktop_print_vfs.c - native print grants, job facts and bounded
 * control.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_desktop_host_vfs_private.h"
/** @brief Resolve an exact live application-owned print grant. */
static tiku_print_slot_t *grant(struct desktop_host_vfs *owner,uint64_t ticket)
{
#if TIKU_SHELL_REMOTE_SERVICES
    for(int i=0;i<TIKU_PRINT_SLOTS;i++) {
        tiku_print_slot_t *slot=&owner->app->printing.slots[i];
        if (slot->ticket == ticket && slot->session && slot->session->used &&
            slot->session->fd >= 0 && slot->window && slot->window->opened &&
            slot->window->window &&
            slot->session->generation == slot->connection &&
            slot->window->session == slot->session &&
            slot->window->generation == slot->incarnation &&
            slot->window->win_id == slot->window_id)
            return slot;
    }
#else
    (void)owner;(void)ticket;
#endif
    return NULL;
}
int desktop_host_print_count(void *context,const char *path)
{
    struct desktop_host_vfs *owner = context;
    int count = (int)owner->print_count;
    (void)path;
#if TIKU_SHELL_REMOTE_SERVICES
    for (int i = 0; i < TIKU_PRINT_SLOTS; i++)
        count += owner->app->printing.slots[i].ticket != 0;
#endif
    return count;
}
/** @brief Copy prepared application grants and active native worker metadata.
 */
static tiku_msg_t *job_row(struct desktop_host_vfs *owner,int index)
{
    tiku_msg_t *out=trk_value_container("object");int bad=0;
#if TIKU_SHELL_REMOTE_SERVICES
    for(int i=0;i<TIKU_PRINT_SLOTS;i++) {
        tiku_print_slot_t *slot = &owner->app->printing.slots[i];
        if (!slot->ticket)
            continue;
        if (index-- != 0)
            continue;
        char window[33]={0};int valid=grant(owner,slot->ticket)!=NULL;
        if (valid && (tiku_trk_window_resources_id(
                         owner->app->window_resources, slot->window->window,
                         window, sizeof window)))
            valid = 0;
        bad |= TRK_TEXT(out, "kind", "application_grant");
        bad |= trk_counter(out, "ticket", slot->ticket);
        bad |= trk_counter(out, "connection", slot->connection);
        bad |= trk_counter(out, "incarnation", slot->incarnation);
        bad |= TRK_TEXT(out, "window", valid ? window : "");
        bad |= TRK_BOOL(out, "owner_live", valid);
        bad |= TRK_TEXT(out, "phase",
                        slot->active ? "native_dispatched"
                                     : "awaiting_final_pdf");
        bad |= TRK_TEXT(out, "title", slot->title);
        bad|=TRK_INT(out,"preparation_deadline_us",slot->deadline);goto result;
    }
#endif
    if (index < 0 || (size_t)index >= owner->print_count) {
        tiku_msg_free(out);
        return NULL;
    }
    tiku_print_job_observation_t *job=&owner->print_jobs[index];
    bad |= TRK_TEXT(out, "kind", "native_job");
    bad |= trk_counter(out, "ticket", job->request);
    bad |= trk_counter(out, "pdf_bytes", job->bytes);
    bad |= TRK_BOOL(out, "started", job->started);
    bad |=
        TRK_BOOL(out, "cancellation_requested", job->cancellation_requested);
    bad |= TRK_BOOL(out, "status_known", job->finished);
    bad |= TRK_INT(out, "status", job->status);
    bad |= TRK_TEXT(out, "title", job->title);
    bad |= TRK_TEXT(out, "reason", job->reason);
#if TIKU_SHELL_REMOTE_SERVICES
result:
#endif
    if(bad){tiku_msg_free(out);return NULL;}return out;
}
tiku_msg_t *desktop_host_print_capture(void *context,const char *path,int index)
{
    struct desktop_host_vfs *owner = context;
    if (!strcmp(path, "/printers/native/jobs"))
        return job_row(owner, index);
    tiku_msg_t *out=trk_value_container("object");int bad=0;
    if(!strcmp(path,"/printers/native/identity")) {
        bad |= TRK_TEXT(out, "kind", "native_print_dialog_service");
        bad |= TRK_TEXT(out, "host", owner->host.backend);
        bad |= TRK_BOOL(out, "physical_printer_inventory", 0);
    } else if(!strcmp(path,"/printers/native/capabilities")) {
        bad |= TRK_BOOL(out, "submit_supported",
                        owner->host.available && TIKU_SHELL_REMOTE_SERVICES);
        bad |= TRK_BOOL(out, "cancel_supported", TIKU_SHELL_REMOTE_SERVICES);
        bad |= TRK_BOOL(out, "existing_user_grant_required", 1);
        bad |= TRK_BOOL(out, "vfs_input_gesture_grant", 0);
        bad |= TRK_INT(out, "maximum_inline_pdf_bytes", 3072);
        bad |= TRK_INT(out, "native_pdf_bytes", TIKU_PRINT_PDF_MAX);
        bad |= TRK_BOOL(out, "physical_completion", 0);
        bad |= TRK_TEXT(out, "reason",
                        !TIKU_SHELL_REMOTE_SERVICES
                            ? "product_remote_print_service_disabled"
                        : owner->host.available
                            ? ""
                            : "native_display_host_unavailable");
    } else {
        bad |= TRK_BOOL(out, "native_host_available", owner->host.available);
        bad |= TRK_BOOL(out, "native_endpoint_created",
                        owner->host.print_endpoint);
        bad |= TRK_INT(out, "current_native_jobs", owner->print_count);
        bad |= TRK_INT(out, "current_grants_and_jobs",
                       desktop_host_print_count(owner, path));
        bad |= TRK_BOOL(out, "last_completion_known", owner->last_print_known);
        bad |= trk_counter(out, "last_ticket", owner->last_print_ticket);
        bad |= TRK_INT(out, "last_status", owner->last_print_status);
        bad |= TRK_TEXT(out, "last_reason", owner->last_print_reason);
        bad |= TRK_TEXT(out, "completion_scope",
                        "native_dialog_submission_or_cancellation");
    }
    if(bad){tiku_msg_free(out);return NULL;}return out;
}
/** @brief Decode the existing grant tuple and optional final PDF without
 * effects.
 */
static tiku_resource_error_t arguments(struct desktop_host_vfs *owner,
                                       const char *path,
                                       const tiku_msg_t *value,
                                       tiku_print_slot_t **slot, int *cancel,
                                       unsigned char pdf[3072], int *bytes)
{
    const char *keys[] = {"window", "ticket", "connection", "incarnation",
                          "pdf_base64"};
    uint64_t ticket, connection, incarnation;
    char window[33], current[33], encoded[4097];
    if (!trk_arguments_keys(value, keys, 5) ||
        !trk_arguments_counter(value, "ticket", &ticket) ||
        !trk_arguments_counter(value, "connection", &connection) ||
        !trk_arguments_counter(value, "incarnation", &incarnation) ||
        !trk_arguments_text(value, "window", window, sizeof window))
        return TIKU_RESOURCE_VALUE;
    *cancel=!strcmp(path,"/printers/native/actions/cancel");
    if (!*cancel && strcmp(path, "/printers/native/actions/submit"))
        return TIKU_RESOURCE_UNSUPPORTED;
    *slot=grant(owner,ticket);if(!*slot)return TIKU_RESOURCE_OWNER_LOST;
    if ((*slot)->connection != connection ||
        (*slot)->incarnation != incarnation ||
        tiku_trk_window_resources_id(owner->app->window_resources,
                                     (*slot)->window->window, current,
                                     sizeof current) ||
        strcmp(current, window))
        return TIKU_RESOURCE_OWNER_LOST;
    *bytes=0;
    if (*cancel)
        return trk_arguments_has(value, "pdf_base64") ? TIKU_RESOURCE_VALUE
                                                      : 0;
    if (!trk_arguments_text(value, "pdf_base64", encoded, sizeof encoded) ||
        (*bytes = trk_base64_decode(encoded, pdf, 3072)) < 5 ||
        memcmp(pdf, "%PDF-", 5))
        return TIKU_RESOURCE_VALUE;
    if((*slot)->active)return TIKU_RESOURCE_BUSY;
    if (tiku_remote_gesture_clock() >= (*slot)->deadline)
        return TIKU_RESOURCE_DEADLINE;
    return owner->app->host?0:TIKU_RESOURCE_UNAVAILABLE;
}
tiku_resource_error_t desktop_host_print_validate(void *context,
                                                  const char *path,
                                                  const tiku_msg_t *value,
                                                  int invoke)
{
    struct desktop_host_vfs *owner = context;
    tiku_print_slot_t *slot;
    unsigned char pdf[3072];
    int cancel, bytes;
    if(!invoke||!TIKU_SHELL_REMOTE_SERVICES)return TIKU_RESOURCE_UNSUPPORTED;
    return arguments(owner,path,value,&slot,&cancel,pdf,&bytes);
}
tiku_resource_error_t desktop_host_print_start(void *context, const char *job,
                                               const char *path,
                                               const tiku_msg_t *value,
                                               int invoke,
                                               tiku_resource_snapshot_t *out)
{
    tiku_resource_error_t error =
        desktop_host_print_validate(context, path, value, invoke);
    (void)job;
    if(error)return error;
#if TIKU_SHELL_REMOTE_SERVICES
    struct desktop_host_vfs *owner = context;
    tiku_print_slot_t *slot;
    unsigned char pdf[3072];
    int cancel, bytes;
    error = arguments(owner, path, value, &slot, &cancel, pdf, &bytes);
    if (error)
        return error;
    uint64_t ticket = slot->ticket;
    int accepted = tiku_print_rpc_control(
        &owner->app->printing, ticket, slot->connection, slot->incarnation,
        pdf, (size_t)bytes, cancel, tiku_remote_gesture_clock());
    tiku_msg_t *result=trk_value_container("object");
    if (TRK_BOOL(result, "accepted", accepted > 0) ||
        TRK_TEXT(result, "phase",
                 accepted > 0 ? (cancel ? "cancellation_requested"
                                        : "native_submission_requested")
                              : "native_request_refused") ||
        trk_counter(result, "ticket", ticket) ||
        TRK_BOOL(result, "physical_completion_known", 0)) {
        tiku_msg_free(result);
        return TIKU_RESOURCE_UNKNOWN_OUTCOME;
    }
    return trk_value_snapshot(out, result, "request_dispatched")
               ? TIKU_RESOURCE_UNKNOWN_OUTCOME
               : 0;
#else
    (void)out;return TIKU_RESOURCE_UNSUPPORTED;
#endif
}
