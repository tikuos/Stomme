/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_inspector.c - native Resource Inspector over the public local SDK.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_inspector_private.h"
#include "tiku_client.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/** @brief Layout parts share one fixed-width catalog and resizable detail views. */
static tiku_rect_t rect(ri_app_t *p, int part)
{
    switch (part) {
    case 0:
        return (tiku_rect_t){72, 43, p->w - 196, 30};
    case 1:
        return (tiku_rect_t){288, 133, p->w - 300, (p->h - 260) * 2 / 3};
    case 2:
        return (tiku_rect_t){288, 151 + (p->h - 260) * 2 / 3, p->w - 300,
                             (p->h - 260) / 3};
    case 3:
        return (tiku_rect_t){12, 133, 260, p->h - 203};
    case 4:
        return (tiku_rect_t){85, 10, p->w - 207, 26};
    default:
        return (tiku_rect_t){0, 0, 0, 0};
    }
}
/** @brief Test a logical coordinate against a native control rectangle. */
static int inside(tiku_rect_t r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}
/** @brief Copy one bounded editor document without returning a partial string. */
static char *document(ri_app_t *p, int index, size_t maximum)
{
    tiku_edit_snapshot_t s = {0};
    if (tiku_editdoc_length(p->doc[index]) > (int64_t)maximum ||
        tiku_editdoc_snapshot(p->doc[index], &s))
        return NULL;
    char *text = s.text;
    s.text = NULL;
    tiku_edit_snapshot_free(&s);
    return text;
}
/** @brief Replace an owner-thread document and retain the response view's read-only policy. */
static void load(ri_app_t *p, int index, const char *text)
{
    tiku_editdoc_set_read_only(p->doc[index], 0);
    (void)tiku_editdoc_load(p->doc[index], text, strlen(text));
    tiku_editdoc_set_read_only(p->doc[index], index == 1);
    p->dirty = 1;
}
/** @brief Expose complete current catalog names to the existing list painter. */
static const char *row_name(void *context, int row)
{
    ri_app_t *p = context;
    if (row < 0 || row >= p->count)
        return NULL;
    const char *name = strrchr(p->rows[row], '/');
    return name && name[1] ? name + 1 : p->rows[row];
}
/** @brief Enforce the displayed descriptor/grant before enabling mutation controls. */
static int can_change(ri_app_t *p, int invoke)
{
    const char *kind = tiku_msg_find_string(p->description, "kind", 0),
               *available =
                   tiku_msg_find_string(p->description, "availability", 0);
    int64_t scopes = 0;
    int host = 1, sensitive = 0;
    if (!kind || strcmp(kind, invoke ? "action" : "setting") || !available ||
        strcmp(available, "available") || p->refresh_required || p->pending ||
        ri_transport_busy(p->transport))
        return 0;
    if (strcmp(p->endpoint.text, p->connected))
        return 0;
    char *path = document(p, 0, TIKU_RESOURCE_PATH_MAX - 1);
    const char *shown = tiku_msg_find_string(p->description, "path", 0);
    int same = path && shown && !strcmp(path, shown);
    free(path);
    if (!same)
        return 0;
    tiku_msg_find_int64(p->description, "change_scopes", 0, &scopes);
    tiku_msg_find_bool(p->description, "host_control_required", 0, &host);
    tiku_msg_find_bool(p->description, "sensitive", 0, &sensitive);
    uint64_t needed = (uint64_t)scopes |
                      (invoke ? TIKU_RESOURCE_INVOKE : TIKU_RESOURCE_WRITE) |
                      (host ? TIKU_RESOURCE_HOST_CONTROL : 0) |
                      (sensitive ? TIKU_RESOURCE_CONTENT : 0);
    return (p->caller.scopes & needed) == needed;
}
/** @brief Rebuild native controls with their current model-derived availability. */
static void controls(ri_app_t *p)
{
    int idle = !ri_transport_busy(p->transport);
    const char *kind = tiku_msg_find_string(p->description, "kind", 0);
    tiku_dialog_begin(&p->dialog, RI_INSPECT, TIKU_DIALOG_NONE);
    tiku_dialog_add(&p->dialog, RI_ENDPOINT, rect(p, 4), TIKU_DIALOG_INPUT, 1);
    tiku_dialog_add(&p->dialog, RI_CONNECT,
                    (tiku_rect_t){p->w - 110, 10, 98, 26}, TIKU_DIALOG_BUTTON,
                    idle);
    tiku_dialog_add(&p->dialog, RI_INSPECT,
                    (tiku_rect_t){p->w - 110, 45, 98, 28}, TIKU_DIALOG_BUTTON,
                    idle);
    const int ids[] = {RI_UP,     RI_MORE,    RI_APPLY, RI_INVOKE,
                       RI_CANCEL, RI_RECEIPT, RI_STREAM};
    const int enabled[] = {idle,
                           idle && p->cursor[0],
                           can_change(p, 0),
                           can_change(p, 1),
                           idle && p->job[0],
                           idle && p->mutation[0],
                           idle && kind && !strcmp(kind, "stream")};
    for (size_t i = 0; i < 7; i++)
        tiku_dialog_add(&p->dialog, ids[i],
                        (tiku_rect_t){12 + (int)i * ((p->w - 72) / 7 + 8), 85,
                                      (p->w - 72) / 7, 28},
                        TIKU_DIALOG_BUTTON, enabled[i]);
    tiku_dialog_end(&p->dialog);
}
void ri_paint(ri_app_t *p)
{
    controls(p);
    tiku_fill(p->surface, (tiku_rect_t){0, 0, p->w, p->h}, TIKU_C_PANEL);
    tiku_text_line_draw(p->surface, (tiku_rect_t){12, 10, 70, 26}, 0,
                        "Endpoint", TIKU_C_TEXT);
    tiku_text_line_draw(p->surface, (tiku_rect_t){12, 44, 56, 28}, 0, "Path",
                        TIKU_C_TEXT);
    tiku_text_line_draw(p->surface, (tiku_rect_t){12, 115, 260, 18}, 1,
                        "Resource catalog", TIKU_C_TEXT);
    tiku_text_line_draw(p->surface, (tiku_rect_t){288, 115, p->w - 300, 18}, 1,
                        "Description, values and results", TIKU_C_TEXT);
    tiku_rect_t proposal = rect(p, 2);
    tiku_text_line_draw(
        p->surface, (tiku_rect_t){proposal.x, proposal.y - 18, proposal.w, 18},
        1, "Proposal / action arguments (typed JSON)", TIKU_C_TEXT);
    tiku_ui_textfield_sel(p->surface, rect(p, 4), p->endpoint.text,
                          p->focus == -1 ? p->endpoint.caret : -1,
                          p->endpoint.sel_a, p->endpoint.sel_b,
                          p->focus == -1 ? TIKU_S_FOCUS : 0);
    for (int i = 0; i < 3; i++) {
        tiku_codeview_set_bounds(p->view[i], rect(p, i));
        tiku_codeview_set_focus(p->view[i], p->focus == i);
        tiku_codeview_paint(p->view[i], p->surface);
    }
    tiku_ui_sunken(p->surface, rect(p, 3), 0xffffff);
    tiku_list_draw(&p->list, p->surface, rect(p, 3), row_name, p);
    for (int i = 0; i < p->dialog.count; i++) {
        const tiku_dialog_control_t *c = &p->dialog.control[i];
        if (c->id == RI_ENDPOINT)
            continue;
        const char *label = c->id == RI_CONNECT   ? "Connect"
                            : c->id == RI_INSPECT ? "Inspect"
                            : c->id == RI_UP      ? "Up"
                            : c->id == RI_MORE    ? "Next page"
                            : c->id == RI_APPLY   ? "Apply value"
                            : c->id == RI_INVOKE  ? "Invoke action"
                            : c->id == RI_CANCEL  ? "Cancel job"
                            : c->id == RI_RECEIPT ? "Reconcile"
                                                  : "Read stream";
        unsigned flags = c->enabled ? 0 : TIKU_S_DISABLED;
        if (tiku_dialog_pressed(&p->dialog, c->id))
            flags |= TIKU_S_PRESSED;
        if (tiku_dialog_focused(&p->dialog, c->id))
            flags |= TIKU_S_FOCUS;
        tiku_ui_button(p->surface, c->rect, label, flags);
    }
    char ids[160];
    snprintf(ids, sizeof ids, "Request: %s   Job: %s   Events: %u",
             p->mutation[0] ? p->mutation : "none",
             p->job[0] ? p->job : "none", p->events);
    tiku_text_line_draw(p->surface,
                        (tiku_rect_t){12, p->h - 61, p->w - 24, 24}, 0, ids,
                        TIKU_C_TEXT);
    tiku_text_line_draw(p->surface,
                        (tiku_rect_t){12, p->h - 35, p->w - 24, 24}, 0,
                        p->status, TIKU_C_TEXT);
    p->services->frame(p->services->ctx, p->id, p->surface->px, p->w, p->h);
    p->dirty = 0;
}
/** @brief Render a complete public reply without losing its result metadata. */
static void show(ri_app_t *p, ri_reply_t *r)
{
    if (r->content) {
        load(p, 1, r->content);
        return;
    }
    tiku_msg_t *m = tiku_msg_new(0);
    int ok = m != NULL;
    if (r->description)
        ok &= tiku_msg_add_message(m, "description", r->description);
    if (r->snapshot)
        ok &= tiku_msg_add_message(m, "snapshot", r->snapshot);
    if (r->result)
        ok &= tiku_msg_add_message(m, "response", r->result);
    if (r->event)
        ok &= tiku_msg_add_message(m, "event", r->event);
    ok &= tiku_msg_add_int64(m, "granted_scopes", p->caller.scopes);
    char *text = malloc(1024 * 1024);
    int n = ok && text ? tiku_resource_client_format(m, text, 1024 * 1024 - 1)
                       : -1;
    if (n >= 0) {
        text[n] = 0;
        load(p, 1, text);
    } else
        snprintf(
            p->status, sizeof p->status,
            "Response exceeds the inspector display bound; use the command client.");
    free(text);
    tiku_msg_free(m);
}
void ri_collect(ri_app_t *p)
{
    ri_reply_t *r = ri_transport_collect(p->transport);
    if (!r)
        return;
    p->caller = r->caller;
    p->refresh_required = r->refresh_required;
    p->events = r->events;
    snprintf(p->connected, sizeof p->connected, "%s", r->directory);
    snprintf(p->job, sizeof p->job, "%s", r->job);
    p->pending = r->job[0] != 0;
    snprintf(p->mutation, sizeof p->mutation, "%s", r->mutation);
    snprintf(p->status, sizeof p->status, "%s", r->status);
    if (r->description) {
        tiku_msg_free(p->description);
        p->description = tiku_msg_copy(r->description);
        tiku_msg_free(p->snapshot);
        p->snapshot = tiku_msg_copy(r->snapshot);
        const char *kind = tiku_msg_find_string(p->description, "kind", 0);
        tiku_msg_t *v = kind && !strcmp(kind, "action")
                            ? NULL
                            : tiku_msg_find_message(p->snapshot, "desired", 0);
        char *text = malloc(TIKU_RESOURCE_FRAME_MAX + 1);
        int n = v && text ? tiku_resource_literal_format(
                                v, text, TIKU_RESOURCE_FRAME_MAX)
                          : -1;
        if (n >= 0) {
            text[n] = 0;
            load(p, 2, text);
        } else
            load(p, 2, "{}");
        free(text);
        tiku_msg_free(v);
    }
    if (r->page) {
        p->count = tiku_msg_count(r->page, "entry");
        if (p->count > 128)
            p->count = 128;
        for (int i = 0; i < p->count; i++) {
            tiku_msg_t *entry = tiku_msg_find_message(r->page, "entry", i);
            const char *path = tiku_msg_find_string(entry, "path", 0),
                       *kind = tiku_msg_find_string(entry, "kind", 0);
            snprintf(p->rows[i], sizeof p->rows[i], "%s", path ? path : "");
            snprintf(p->kinds[i], sizeof p->kinds[i], "%s", kind ? kind : "");
            tiku_msg_free(entry);
        }
        const char *cursor = tiku_msg_find_string(r->page, "next_cursor", 0);
        snprintf(p->cursor, sizeof p->cursor, "%s", cursor ? cursor : "");
        tiku_list_set_count(&p->list, p->count);
    } else if (r->command == RI_INSPECT || r->command == RI_CONNECT) {
        p->count = 0;
        p->cursor[0] = 0;
        tiku_list_set_count(&p->list, 0);
    }
    show(p, r);
    p->dirty = 1;
    ri_reply_free(r);
}
int ri_action(ri_app_t *p, int command)
{
    if (command == RI_CLOSE)
        return 1;
    if (command == RI_COPY_ENDPOINT) {
        if (p->services->clip_set)
            p->services->clip_set(p->services->ctx, p->endpoint.text);
        return 0;
    }
    char *path = document(p, 0, TIKU_RESOURCE_PATH_MAX - 1);
    if (!path || !tiku_resource_path_valid(path)) {
        free(path);
        snprintf(p->status, sizeof p->status,
                 "Enter a canonical resource path, starting with /.");
        p->dirty = 1;
        return 0;
    }
    if (command == RI_UP) {
        char *slash = strrchr(path, '/');
        if (slash && slash != path)
            *slash = 0;
        else
            strcpy(path, "/");
        load(p, 0, path);
        command = RI_INSPECT;
    }
    if (command == RI_APPLY || command == RI_INVOKE || command == RI_STREAM ||
        command == RI_MORE) {
        const char *shown = tiku_msg_find_string(p->description, "path", 0);
        if (!shown || strcmp(path, shown) ||
            strcmp(p->endpoint.text, p->connected)) {
            free(path);
            snprintf(
                p->status, sizeof p->status,
                "Inspect the edited endpoint and path before using its controls.");
            p->dirty = 1;
            return 0;
        }
    }
    if ((command == RI_APPLY && !can_change(p, 0)) ||
        (command == RI_INVOKE && !can_change(p, 1))) {
        free(path);
        return 0;
    }
    ri_request_t request = {.command = command};
    snprintf(request.directory, sizeof request.directory, "%s",
             p->endpoint.text);
    snprintf(request.path, sizeof request.path, "%s", path);
    free(path);
    snprintf(request.cursor, sizeof request.cursor, "%s", p->cursor);
    request.description = tiku_msg_copy(p->description);
    if (command == RI_APPLY || command == RI_INVOKE) {
        char *proposal = document(p, 2, TIKU_RESOURCE_FRAME_MAX);
        tiku_resource_error_t e =
            proposal
                ? tiku_resource_literal_parse("json", proposal, &request.value)
                : TIKU_RESOURCE_LIMIT;
        free(proposal);
        if (e) {
            snprintf(p->status, sizeof p->status, "Proposal rejected: %s",
                     tiku_resource_error_name(e));
            tiku_msg_free(request.description);
            p->dirty = 1;
            return 0;
        }
    }
    if (ri_transport_submit(p->transport, &request)) {
        tiku_msg_free(request.description);
        tiku_msg_free(request.value);
        snprintf(p->status, sizeof p->status,
                 "A resource request is pending.");
    } else
        snprintf(p->status, sizeof p->status,
                 "Waiting for the native resource owner...");
    p->dirty = 1;
    return 0;
}
/** @brief Delegate native clipboard text to the SDK editor view. */
static int clip_set(void *context, const char *text, size_t length)
{
    ri_app_t *p = context;
    (void)length;
    return p->services->clip_set
               ? p->services->clip_set(p->services->ctx, text)
               : -1;
}
/** @brief Borrow the current native clipboard text. */
static const char *clip_text(void *context, size_t *length)
{
    ri_app_t *p = context;
    unsigned stamp = 0;
    const char *s = p->services->clip_text
                        ? p->services->clip_text(p->services->ctx, &stamp)
                        : NULL;
    if (length)
        *length = s ? strlen(s) : 0;
    return s;
}
/** @brief Resize all editor bounds after replacing the native canvas. */
static int resize(ri_app_t *p, int w, int h)
{
    if (w < 760 || h < 560)
        return 0;
    tiku_surface_t *s = tiku_surface_new(w, h, TIKU_C_PANEL);
    if (!s)
        return 0;
    tiku_surface_free(p->surface);
    p->surface = s;
    p->w = w;
    p->h = h;
    p->dirty = 1;
    return 1;
}
/** @brief Route native fields, list navigation and editor focus through existing controls. */
static int input(void *state, uint32_t window, const tiku_event_t *e)
{
    ri_app_t *p = state;
    (void)window;
    if (e->type == TIKU_EVENT_CLOSE)
        return 1;
    if (e->type == TIKU_EVENT_RESIZE) {
        (void)resize(p, e->width, e->height);
        ri_paint(p);
        return 0;
    }
    if (e->type == TIKU_EVENT_POINTER_DOWN) {
        p->focus = -2;
        if (inside(rect(p, 4), e->x, e->y))
            p->focus = -1;
        for (int i = 0; i < 3; i++)
            if (inside(rect(p, i), e->x, e->y)) {
                p->focus = i;
                tiku_dialog_focus(&p->dialog, TIKU_DIALOG_NONE);
            }
        if (inside(rect(p, 3), e->x, e->y)) {
            p->focus = 3;
            int row = tiku_list_at(&p->list, rect(p, 3), e->x, e->y);
            if (row >= 0 && row < p->count) {
                tiku_list_select_only(&p->list, row);
                load(p, 0, p->rows[row]);
                (void)ri_action(p, RI_INSPECT);
            }
        }
    }
    if (p->focus == -1)
        (void)tiku_field_event(&p->endpoint, &p->endpoint_input, e, rect(p, 4),
                               1);
    else if (p->focus >= 0 && p->focus < 3) {
        if (e->type == TIKU_EVENT_KEY_DOWN && e->key == TIKU_KEY_TAB)
            p->focus = -2;
        else {
            if (p->focus == 0 && e->type == TIKU_EVENT_KEY_DOWN &&
                e->key == TIKU_KEY_RETURN) {
                (void)ri_action(p, RI_INSPECT);
                ri_paint(p);
                return 0;
            }
            if (tiku_codeview_event(p->view[p->focus], e)) {
                ri_paint(p);
                return 0;
            }
        }
    } else if (p->focus == 3) {
        if (e->type == TIKU_EVENT_WHEEL) {
            int top = p->list.top - e->dy;
            (void)tiku_list_scroll_to(&p->list, top, rect(p, 3));
        }
        if (e->type == TIKU_EVENT_KEY_DOWN &&
            tiku_list_key(&p->list, e->key, e->modifiers, rect(p, 3))) {
            int row = tiku_list_chosen(&p->list);
            if (row >= 0 && row < p->count)
                load(p, 0, p->rows[row]);
        }
        if (e->type == TIKU_EVENT_KEY_DOWN && e->key == TIKU_KEY_RETURN)
            (void)ri_action(p, RI_INSPECT);
    }
    controls(p);
    tiku_dialog_result_t result = tiku_dialog_event(&p->dialog, e);
    if (result.activated != TIKU_DIALOG_NONE &&
        result.activated != RI_ENDPOINT && ri_action(p, result.activated))
        return 1;
    ri_paint(p);
    return 0;
}
/** @brief Collect copied results and tick the focused native editor's caret. */
static void tick(void *state, int64_t now)
{
    ri_app_t *p = state;
    ri_collect(p);
    if (p->dirty || now - p->painted >= 250000) {
        tiku_event_t event = {.type = TIKU_EVENT_TICK, .time_us = now};
        if (p->focus >= 0 && p->focus < 3)
            (void)tiku_codeview_event(p->view[p->focus], &event);
        ri_paint(p);
        p->painted = now;
    }
}
/** @brief Retire the channel thread before freeing its native UI recipients. */
static void stop(void *state)
{
    ri_app_t *p = state;
    if (!p)
        return;
    ri_transport_close(p->transport);
    for (int i = 0; i < 3; i++) {
        tiku_codeview_destroy(p->view[i]);
        tiku_editdoc_release(p->doc[i]);
    }
    tiku_msg_free(p->description);
    tiku_msg_free(p->snapshot);
    tiku_surface_free(p->surface);
    free(p);
}
/** @brief Start one native inspector with a dedicated resource connection owner. */
static int start(void **state, const tiku_app_services_t *services)
{
    ri_app_t *p = calloc(1, sizeof *p);
    if (!p)
        return -1;
    p->services = services;
    p->w = 840;
    p->h = 620;
    p->focus = 0;
    p->surface = tiku_surface_new(p->w, p->h, TIKU_C_PANEL);
    p->transport = ri_transport_open();
    tiku_codeview_host_t host = {
        .ctx = p, .clip_set = clip_set, .clip_text = clip_text};
    for (int i = 0; i < 3; i++) {
        tiku_editdoc_limits_t limits = {
            .max_bytes = i == 0 ? TIKU_RESOURCE_PATH_MAX - 1 : 1024 * 1024};
        if (tiku_editdoc_create(&limits, &p->doc[i]) ||
            tiku_codeview_create(p->doc[i], &p->view[i])) {
            stop(p);
            return -1;
        }
        tiku_codeview_set_host(p->view[i], &host);
        tiku_codeview_set_options(
            p->view[i], i ? TIKU_CODEVIEW_WRAP | TIKU_CODEVIEW_VSCROLL : 0);
    }
    if (!p->surface || !p->transport) {
        stop(p);
        return -1;
    }
    tiku_field_start(&p->endpoint, getenv("STOMME_RESOURCE_DIRECTORY")
                                       ? getenv("STOMME_RESOURCE_DIRECTORY")
                                       : "");
    tiku_field_input_init(&p->endpoint_input);
    tiku_list_init(&p->list, 1);
    tiku_dialog_init(&p->dialog);
    load(p, 0, "/");
    load(p, 1, "Select a resource to inspect its native owner.");
    load(p, 2, "{}");
    p->id = services->open(services->ctx, "Resource Inspector", p->w, p->h);
    if (!p->id) {
        stop(p);
        return -1;
    }
    if (services->sizeable)
        (void)services->sizeable(services->ctx, p->id);
    tiku_menuset_t menus = {.nmenu = 1};
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title,
             "Resource Inspector");
    const char *labels[] = {"Inspect", "Up", "Copy endpoint", "Close"};
    const int commands[] = {RI_INSPECT, RI_UP, RI_COPY_ENDPOINT, RI_CLOSE};
    menus.menu[0].nitem = 4;
    for (int i = 0; i < 4; i++) {
        snprintf(menus.menu[0].item[i].label,
                 sizeof menus.menu[0].item[i].label, "%s", labels[i]);
        menus.menu[0].item[i].command = commands[i];
        menus.menu[0].item[i].enabled = 1;
    }
    if (services->menus)
        services->menus(services->ctx, p->id, &menus);
    *state = p;
    (void)ri_action(p, RI_CONNECT);
    ri_paint(p);
    return 0;
}
/** @brief Native exported menus contain observation and clipboard controls. */
static int pick(void *state, uint32_t window, int command)
{
    (void)window;
    ri_app_t *p = state;
    if (command != RI_CLOSE && command != RI_INSPECT && command != RI_UP &&
        command != RI_COPY_ENDPOINT)
        return 0;
    return ri_action(p, command);
}
static const tiku_app_descriptor_t descriptor = {
    .id = "org.stomme.resource-inspector",
    .name = "Resource Inspector",
    .start = start,
    .stop = stop,
    .event_in = input,
    .tick = tick,
    .pick = pick};
#ifdef TIKU_APP_SO
/** @brief Export the native inspector through the application ABI. */
__attribute__((visibility("default")))
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t), &descriptor};
#else
/** @brief Run the inspector as an SDK application process. */
int main(void)
{
    return tiku_client_run(&descriptor);
}
#endif
