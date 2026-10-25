/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desktop_access_vfs.c - retained semantic publications and owner
 * actions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_desktop_host_vfs_private.h"
#include <limits.h>
#define ACCESS_WIRE_BUDGET (2u*1024u*1024u)
/** @brief Release one immutable tree hold and its serialized packet. */
static void release_entry(host_access_t *entry)
{
    tiku_access_release(entry->tree);
    free(entry->wire);
    memset(entry, 0, sizeof *entry);
}
void desktop_host_access_clear(struct desktop_host_vfs *owner)
{
    for (int i = 0; i < HOST_ACCESS_WINDOWS; i++)
        release_entry(&owner->access_windows[i]);
    owner->access_count=0;
}
void desktop_host_access_refresh(struct desktop_host_vfs *owner)
{
    desktop_t *app = owner->app;
    int used[HOST_ACCESS_WINDOWS] = {0};
    size_t budget = ACCESS_WIRE_BUDGET;
    owner->access_complete = 1;
    for (int i = 0; i < TIKU_REMOTE_SESSIONS; i++)
        for (int j = 0; j < TIKU_REMOTE_WINDOWS; j++) {
            tiku_remote_session_t *session = &app->remote.session[i];
            tiku_remote_window_t *remote = &session->win[j];
            int at = i * TIKU_REMOTE_WINDOWS + j;
            host_access_t *entry = &owner->access_windows[at];
            char window[33];
            if (!session->used || session->fd < 0 || !remote->opened ||
                !remote->window || !remote->access ||
                tiku_trk_window_resources_id(app->window_resources,
                                             remote->window, window,
                                             sizeof window))
                continue;
            used[at] = 1;
            if (entry->tree != remote->access ||
                entry->connection != session->generation ||
                entry->incarnation != remote->generation ||
                strcmp(entry->window, window)) {
                release_entry(entry);
                entry->tree = remote->access;
                tiku_access_retain(entry->tree);
                entry->remote = remote;
                entry->connection = session->generation;
                entry->incarnation = remote->generation;
                memcpy(entry->window, window, sizeof window);
            }
            if (entry->wire && entry->bytes > budget) {
                free(entry->wire);
                entry->wire = NULL;
                entry->bytes = 0;
                entry->encoded = 0;
            }
            if (!entry->wire)
                entry->encoded = !tiku_access_encode_limited(
                    entry->tree, budget, &entry->wire, &entry->bytes);
            if (!entry->encoded)
                owner->access_complete = 0;
            else
                budget -= entry->bytes;
        }
    owner->access_count=0;
    for (int i = 0; i < HOST_ACCESS_WINDOWS; i++)
        if (used[i])
            owner->access_count++;
        else
            release_entry(&owner->access_windows[i]);
}
/** @brief Resolve one retained native publication by compact snapshot index.
 */
static host_access_t *entry_at(struct desktop_host_vfs *owner,int index)
{
    for (int i = 0; i < HOST_ACCESS_WINDOWS; i++)
        if (owner->access_windows[i].tree && index-- == 0)
            return &owner->access_windows[i];
    return NULL;
}
int desktop_host_access_count(void *context,const char *path)
{
    struct desktop_host_vfs *owner=context;
    if(!strcmp(path,"/accessibility/trees")) {
        if(!owner->access_complete)return -1;
        size_t count = 0;
        for (int i = 0; i < HOST_ACCESS_WINDOWS; i++)
            if (owner->access_windows[i].tree)
                count += (owner->access_windows[i].bytes + 2047) / 2048;
        return count>INT_MAX?-1:(int)count;
    }
    return owner->access_count;
}
/** @brief Append the complete native ownership and semantic generation tuple.
 */
static int identity(tiku_msg_t *out,const host_access_t *entry)
{
    return TRK_TEXT(out, "window", entry->window) ||
           trk_counter(out, "connection", entry->connection) ||
           trk_counter(out, "incarnation", entry->incarnation) ||
           trk_counter(out, "document", tiku_access_document(entry->tree)) ||
           trk_counter(out, "generation", tiku_access_generation(entry->tree));
}
/** @brief Copy one native codec chunk without changing its retained
 * publication.
 */
static tiku_msg_t *wire_row(struct desktop_host_vfs *owner,int index)
{
    for(int i=0;i<HOST_ACCESS_WINDOWS;i++) {
        host_access_t *entry = &owner->access_windows[i];
        if (!entry->tree || !entry->encoded)
            continue;
        int rows = (int)((entry->bytes + 2047) / 2048);
        if (index >= rows) {
            index -= rows;
            continue;
        }
        size_t offset = (size_t)index * 2048, size = entry->bytes - offset;
        if (size > 2048)
            size = 2048;
        char encoded[2733];
        if (trk_base64_encode((const unsigned char *)entry->wire + offset,
                              size, encoded, sizeof encoded) < 0)
            return NULL;
        tiku_msg_t *out=trk_value_container("object");
        if (identity(out, entry) ||
            TRK_TEXT(out, "encoding", "tiku-access-wire-base64") ||
            TRK_INT(out, "offset", offset) ||
            trk_counter(out, "bytes", entry->bytes) ||
            TRK_TEXT(out, "data", encoded) ||
            TRK_BOOL(out, "complete", offset + size == entry->bytes)) {
            tiku_msg_free(out);
            return NULL;
        }
        return out;
    }
    return NULL;
}
tiku_msg_t *desktop_host_access_capture(void *context, const char *path,
                                        int index)
{
    struct desktop_host_vfs *owner=context;tiku_msg_t *out;
    if(!strcmp(path,"/accessibility/trees"))return wire_row(owner,index);
    out=trk_value_container("object");int bad=0;
    if(!strcmp(path,"/accessibility/capabilities")) {
        bad |= TRK_BOOL(out, "native_endpoint_active",
                        owner->host.accessibility_endpoint);
        bad |= TRK_BOOL(out, "native_host_available", owner->host.available);
        bad |= TRK_BOOL(out, "remote_semantic_publications",
                        TIKU_SHELL_REMOTE_SERVICES);
        bad |= TRK_BOOL(out, "shell_and_embedded_trees", 0);
        bad |= TRK_TEXT(out, "remote_publication_reason",
                        TIKU_SHELL_REMOTE_SERVICES
                            ? ""
                            : "product_remote_semantic_service_disabled");
        bad |= TRK_INT(out, "published_windows", owner->access_count);
        bad |= TRK_BOOL(out, "tree_capture_complete", owner->access_complete);
        bad |= TRK_INT(out, "wire_capture_bytes", ACCESS_WIRE_BUDGET);
        bad |= TRK_TEXT(out, "tree_encoding", "tiku-access-wire-base64");
        bad |= TRK_BOOL(out, "input_gesture_grant", 0);
    } else {
        host_access_t *entry = entry_at(owner, index);
        if (!entry) {
            tiku_msg_free(out);
            return NULL;
        }
        bad |= identity(out, entry);
        bad |= trk_counter(out, "nodes", tiku_access_count(entry->tree));
        if(!strcmp(path,"/accessibility/applications")) {
            bad |= TRK_TEXT(out, "owner", entry->remote->session->name);
            bad |= TRK_TEXT(out, "scope", "application_window_publication");
        } else {
            bad |= TRK_BOOL(out, "wire_available", entry->encoded);
            bad |= TRK_TEXT(
                out, "reason",
                entry->encoded
                    ? ""
                    : "native_tree_exceeds_capture_budget_or_encoding_refused");
        }
    }
    if(bad){tiku_msg_free(out);return NULL;}return out;
}
/** @brief Resolve current native window and validate a semantic operation's
 * ranges.
 */
static tiku_resource_error_t
arguments(struct desktop_host_vfs *owner, const tiku_msg_t *value,
          tiku_remote_window_t **remote, uint32_t *node, uint64_t *document,
          uint64_t *generation, unsigned *operation, int *first, int *last,
          char text[4097], int *has_text)
{
    const char *keys[] = {"window", "document", "generation", "node",
                          "action", "first",    "last",       "text"};
    char window[33], action[16];
    int64_t number;
    const char *names[] = {"focus",    "select", "replace",
                           "activate", "insert", "delete"};
    if (!trk_arguments_keys(value, keys, 8) ||
        !trk_arguments_text(value, "window", window, sizeof window) ||
        !trk_arguments_counter(value, "document", document) ||
        !trk_arguments_counter(value, "generation", generation) ||
        !trk_arguments_integer(value, "node", 0, UINT32_MAX, &number) ||
        !trk_arguments_text(value, "action", action, sizeof action))
        return TIKU_RESOURCE_VALUE;
    *node = (uint32_t)number;
    tiku_window_t *native =
        tiku_trk_window_resources_find(owner->app->window_resources, window);
    *remote=native?tiku_remote_window_of(&owner->app->remote,native):NULL;
    if (!*remote || !(*remote)->session || !(*remote)->session->used ||
        (*remote)->session->fd < 0 || !(*remote)->opened || !(*remote)->access)
        return TIKU_RESOURCE_OWNER_LOST;
    tiku_access_tree_t *tree=(*remote)->access;
    if (tiku_access_document(tree) != *document ||
        tiku_access_generation(tree) != *generation)
        return TIKU_RESOURCE_STALE;
    const tiku_access_node_t *target = tiku_access_node(tree, *node);
    if (!target)
        return TIKU_RESOURCE_VALUE;
    unsigned at;
    for (at = 0; at < 6 && strcmp(action, names[at]); at++) {
    }
    if (at == 6)
        return TIKU_RESOURCE_VALUE;
    *operation = at + 1;
    if(target->state&TIKU_ACCESS_DISABLED)return TIKU_RESOURCE_UNSUPPORTED;
    *first=*last=0;*has_text=trk_arguments_has(value,"text");
    if (*operation == TIKU_ACCESS_SELECT ||
        *operation == TIKU_ACCESS_REPLACE ||
        *operation == TIKU_ACCESS_INSERT || *operation == TIKU_ACCESS_DELETE) {
        int length=tiku_access_characters(target->text,strlen(target->text));
        if (!trk_arguments_integer(value, "first", 0, length, &number))
            return TIKU_RESOURCE_VALUE;
        *first=(int)number;*last=*first;
        if(*operation!=TIKU_ACCESS_INSERT) {
            if (!trk_arguments_integer(value, "last", *first, length, &number))
                return TIKU_RESOURCE_VALUE;
            *last=(int)number;
        } else if(trk_arguments_has(value,"last"))return TIKU_RESOURCE_VALUE;
        if (*operation != TIKU_ACCESS_SELECT &&
            !(target->state & TIKU_ACCESS_EDITABLE))
            return TIKU_RESOURCE_UNSUPPORTED;
    } else if (trk_arguments_has(value, "first") ||
               trk_arguments_has(value, "last"))
        return TIKU_RESOURCE_VALUE;
    if(*operation==TIKU_ACCESS_REPLACE||*operation==TIKU_ACCESS_INSERT) {
        if (!trk_arguments_text(value, "text", text, 4097))
            return TIKU_RESOURCE_VALUE;
    } else if(*has_text)return TIKU_RESOURCE_VALUE;
    if (*operation == TIKU_ACCESS_ACTIVATE &&
        !(target->state & TIKU_ACCESS_ACTIONABLE))
        return TIKU_RESOURCE_UNSUPPORTED;
    return 0;
}
tiku_resource_error_t desktop_host_access_validate(void *context,
                                                   const char *path,
                                                   const tiku_msg_t *value,
                                                   int invoke)
{
    struct desktop_host_vfs *owner = context;
    tiku_remote_window_t *remote;
    uint32_t node;
    uint64_t document, generation;
    unsigned action;
    int first, last, has_text;
    char text[4097];
    if (!invoke || strcmp(path, "/accessibility/actions/perform-action"))
        return TIKU_RESOURCE_UNSUPPORTED;
    if(!TIKU_SHELL_REMOTE_SERVICES)return TIKU_RESOURCE_UNSUPPORTED;
    return arguments(owner, value, &remote, &node, &document, &generation,
                     &action, &first, &last, text, &has_text);
}
tiku_resource_error_t desktop_host_access_start(void *context, const char *job,
                                                const char *path,
                                                const tiku_msg_t *value,
                                                int invoke,
                                                tiku_resource_snapshot_t *out)
{
    struct desktop_host_vfs *owner = context;
    tiku_resource_error_t error =
        desktop_host_access_validate(context, path, value, invoke);
    (void)job;
    if(error)return error;
    tiku_remote_window_t *remote;
    uint32_t node;
    uint64_t document, generation;
    unsigned action;
    int first, last, has_text;
    char text[4097];
    error = arguments(owner, value, &remote, &node, &document, &generation,
                      &action, &first, &last, text, &has_text);
    if (error)
        return error;
    if (!desktop_access_perform(owner->app, remote, node, document, generation,
                                action, first, last, has_text ? text : NULL,
                                0))
        return TIKU_RESOURCE_UNKNOWN_OUTCOME;
    tiku_msg_t *result=trk_value_container("object");
    if (TRK_TEXT(result, "phase", "owner_action_queued") ||
        TRK_BOOL(result, "application_completion_known", 0) ||
        TRK_BOOL(result, "input_gesture_granted", 0)) {
        tiku_msg_free(result);return TIKU_RESOURCE_UNKNOWN_OUTCOME;
    }
    return trk_value_snapshot(out, result, "request_dispatched")
               ? TIKU_RESOURCE_UNKNOWN_OUTCOME
               : 0;
}
