/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_app_resource_native.c - copied native window, menu and control surfaces.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_app_resources_private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TEXT(o, k, v)                                                         \
    tiku_app_resource_member(                                                 \
        o, k, tiku_resource_value_string(TIKU_RESOURCE_TEXT, v))
#define INT(o, k, v)                                                          \
    tiku_app_resource_member(                                                 \
        o, k, tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, v))
#define BOOL(o, k, v)                                                         \
    tiku_app_resource_member(                                                 \
        o, k, tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, (v) != 0))
static const char *const base_paths[] = {"identity",       "state",
                                         "windows",        "windows/entries",
                                         "menus",          "menus/entries",
                                         "controls",       "controls/entries",
                                         "commands",       "commands/entries",
                                         "commands/invoke"};
/** @brief Find a currently held runner window by its native application ID. */
static app_resource_window_t *window(tiku_app_resources_t *owner, uint32_t id)
{
    for (int i = 0; i < APP_RESOURCE_WINDOWS; i++)
        if (owner->windows[i].id == id)
            return &owner->windows[i];
    return NULL;
}
void tiku_app_resources_window(tiku_app_resources_t *owner, uint32_t id,
                               const char *title, int width, int height,
                               int closed)
{
    if (!owner || !id)
        return;
    app_resource_window_t *value = window(owner, id);
    if (closed) {
        if (value) {
            free(value->menus);
            free(value->controls);
            memset(value, 0, sizeof *value);
        }
        return;
    }
    if (!value)
        for (int i = 0; i < APP_RESOURCE_WINDOWS; i++)
            if (!owner->windows[i].id) {
                value = &owner->windows[i];
                value->id = id;
                break;
            }
    if (!value)
        return;
    if (title)
        snprintf(value->title, sizeof value->title, "%s", title);
    if (width > 0)
        value->width = width;
    if (height > 0)
        value->height = height;
}
void tiku_app_resources_menus(tiku_app_resources_t *owner, uint32_t id,
                              const tiku_menuset_t *menus)
{
    app_resource_window_t *value = owner ? window(owner, id) : NULL;
    if (!value || !menus || menus->nmenu < 0 ||
        menus->nmenu > TIKU_MENUSET_MENUS)
        return;
    for (int i = 0; i < menus->nmenu; i++)
        if (menus->menu[i].nitem < 0 ||
            menus->menu[i].nitem > TIKU_MENUSET_ITEMS)
            return;
    if (!value->menus)
        value->menus = malloc(sizeof *menus);
    if (value->menus)
        *value->menus = *menus;
}
void tiku_app_resources_controls(tiku_app_resources_t *owner, uint32_t id,
                                 const struct tiku_dl *dl)
{
    app_resource_window_t *value = owner ? window(owner, id) : NULL;
    if (!value)
        return;
    value->controls_complete = 0;
    if (!dl)
        return;
    size_t capacity = 128u * 1024u;
    char *facts = malloc(capacity);
    if (!facts)
        return;
    size_t bytes = tiku_dl_facts(dl, facts, capacity);
    if (bytes >= capacity - 1) {
        free(facts);
        return;
    }
    free(value->controls);
    value->controls = facts;
    value->controls_complete = 1;
}
/** @brief Count native rows without manufacturing application model state. */
static size_t native_count(tiku_app_resources_t *owner, const char *path)
{
    size_t count = 0;
    for (int i = 0; i < APP_RESOURCE_WINDOWS; i++) {
        app_resource_window_t *w = &owner->windows[i];
        if (!w->id)
            continue;
        if (!strcmp(path, "windows"))
            count++;
        else if (!strcmp(path, "controls")) {
            for (const char *p = w->controls; p && *p; p++)
                count += *p == '\n';
        } else if(w->menus)for(int j=0;j<w->menus->nmenu;j++) {
            if(!strcmp(path,"menus"))count++;
            else
                for (int k = 0; k < w->menus->menu[j].nitem; k++) {
                    tiku_menu_entry_t *entry = &w->menus->menu[j].item[k];
                    count += !entry->separator && entry->command != 0;
                }
        }
    }
    return count;
}
tiku_resource_error_t
tiku_app_resource_base_capture(tiku_app_resources_t *owner, const char *path,
                               tiku_resource_snapshot_t *out)
{
    tiku_msg_t *value = tiku_app_resource_object(0);
    int bad = 0;
    if (!strcmp(path, "identity")) {
        char connection[32];
        snprintf(connection, sizeof connection, "%llu",
                 (unsigned long long)owner->connection);
        bad |= TEXT(value, "id", owner->descriptor->id);
        bad |= TEXT(value, "name", owner->descriptor->name);
        bad |= TEXT(value, "kind", "application_provider");
        bad |= TEXT(value, "connection", connection);
        bad |= TEXT(value, "namespace",
                    tiku_resource_provider_client_prefix(owner->client));
    } else if (!strcmp(path, "state")) {
        int complete = owner->ready;
        for (size_t i = 0; i < owner->count; i++)
            if (owner->entries[i].unavailable ||
                (owner->entries[i].initial.spec.kind == TIKU_RESOURCE_STREAM &&
                 !owner->entries[i].stream_ready))
                complete = 0;
        bad |= BOOL(value, "available", owner->started);
        bad |= BOOL(value, "registered", owner->phase == 2);
        bad |= BOOL(value, "capture_complete", complete);
        bad |= BOOL(value, "model_extension", owner->extension != NULL);
        bad |= BOOL(value, "input_gesture_grant", 0);
        bad |= INT(value, "schema_version", 1);
    } else if (!strcmp(path, "windows") || !strcmp(path, "menus") ||
               !strcmp(path, "controls") || !strcmp(path, "commands")) {
        bad |= INT(value, "count", native_count(owner, path));
        bad |= INT(value, "version", 1);
        bad |= TEXT(value, "entries", "entries");
        if (!strcmp(path, "controls")) {
            int complete = 1;
            for (int i = 0; i < APP_RESOURCE_WINDOWS; i++)
                if (owner->windows[i].id &&
                    !owner->windows[i].controls_complete)
                    complete = 0;
            bad |= BOOL(value, "complete", complete);
            bad |=
                TEXT(value, "encoding", "native-display-list-control-facts");
        }
        if (!strcmp(path, "commands"))
            bad |= BOOL(value, "native_menu_dispatch",
                        owner->descriptor->pick != NULL);
    } else {
        tiku_msg_free(value);
        return TIKU_RESOURCE_UNSUPPORTED;
    }
    if (bad) {
        tiku_msg_free(value);
        return TIKU_RESOURCE_NO_MEMORY;
    }
    return tiku_app_resource_snapshot(out, value, "observed")
               ? TIKU_RESOURCE_NO_MEMORY
               : 0;
}
int tiku_app_resource_base_catalog(tiku_app_resources_t *owner, size_t index,
                                   tiku_resource_initial_t *out)
{
    if (index >= sizeof base_paths / sizeof base_paths[0])
        return 0;
    const char *path = base_paths[index];
    int stream = strstr(path, "/entries") != NULL,
        action = !strcmp(path, "commands/invoke");
    out->spec =
        (tiku_resource_spec_t){.path = path,
                               .kind = action      ? TIKU_RESOURCE_ACTION
                                       : stream    ? TIKU_RESOURCE_STREAM
                                       : index < 2 ? TIKU_RESOURCE_STATE
                                                   : TIKU_RESOURCE_COLLECTION,
                               .schema_version = 1,
                               .schema = {.type = TIKU_RESOURCE_OBJECT},
                               .persistence = "none"};
    if (stream || action)
        return tiku_app_resource_snapshot(
                   &out->initial,
                   stream ? tiku_app_resource_stream_metadata()
                          : tiku_app_resource_object(0),
                   stream ? "awaiting_owner_capture" : "observed")
                   ? -1
                   : 1;
    return tiku_app_resource_base_capture(owner, path, &out->initial) ? -1 : 1;
}
/** @brief Append a complete typed row to a bounded frozen NDJSON capture. */
static int append(void **bytes, size_t *size, size_t *capacity,
                  tiku_msg_t *value)
{
    char row[16000];
    int n = value ? tiku_resource_value_render(value, row, sizeof row) : -1;
    tiku_msg_free(value);
    if (n < 0 || (size_t)n > TIKU_RESOURCE_STREAM_BYTES - *size)
        return -1;
    size_t needed = *size + (size_t)n;
    if (needed > *capacity) {
        size_t next = *capacity ? *capacity * 2 : 4096;
        if (next < needed)
            next = needed;
        if (next > TIKU_RESOURCE_STREAM_BYTES)
            next = TIKU_RESOURCE_STREAM_BYTES;
        void *fresh = realloc(*bytes, next);
        if (!fresh)
            return -1;
        *bytes = fresh;
        *capacity = next;
    }
    if (n)
        memcpy((char *)*bytes + *size, row, (size_t)n);
    *size = needed;
    return 0;
}
tiku_resource_error_t
tiku_app_resource_base_stream(tiku_app_resources_t *owner, const char *path,
                              void **bytes, size_t *size)
{
    if (strcmp(path, "windows/entries") && strcmp(path, "menus/entries") &&
        strcmp(path, "commands/entries") && strcmp(path, "controls/entries"))
        return TIKU_RESOURCE_UNSUPPORTED;
    size_t capacity = 0;
    *bytes = NULL;
    *size = 0;
    int bad = 0;
    for (int i = 0; i < APP_RESOURCE_WINDOWS && !bad; i++) {
        app_resource_window_t *w = &owner->windows[i];
        if (!w->id)
            continue;
        if (!strcmp(path, "windows/entries")) {
            tiku_msg_t *row = tiku_app_resource_object(0);
            bad |= INT(row, "window", w->id);
            bad |= TEXT(row, "title", w->title);
            bad |= INT(row, "width", w->width);
            bad |= INT(row, "height", w->height);
            bad |= append(bytes, size, &capacity, row);
        } else if (!strcmp(path, "controls/entries")) {
            if (!w->controls_complete) {
                bad = 1;
                break;
            }
            for (const char *p = w->controls; p && *p && !bad;) {
                const char *end = strchr(p, '\n');
                size_t n = end ? (size_t)(end - p) : strlen(p);
                char fact[4097];
                if (n >= sizeof fact) {
                    bad = 1;
                    break;
                }
                memcpy(fact, p, n);
                fact[n] = 0;
                tiku_msg_t *row = tiku_app_resource_object(0);
                bad |= INT(row, "window", w->id);
                bad |= TEXT(row, "fact", fact);
                bad |= append(bytes, size, &capacity, row);
                p = end ? end + 1 : NULL;
            }
        } else if (w->menus)
            for (int j = 0; j < w->menus->nmenu && !bad; j++) {
                tiku_menu_list_t *menu = &w->menus->menu[j];
                if (!strcmp(path, "menus/entries")) {
                    tiku_msg_t *row = tiku_app_resource_object(0);
                    bad |= INT(row, "window", w->id);
                    bad |= TEXT(row, "title", menu->title);
                    bad |= INT(row, "items", menu->nitem);
                    bad |= append(bytes, size, &capacity, row);
                } else
                    for (int k = 0; k < menu->nitem && !bad; k++) {
                        tiku_menu_entry_t *entry = &menu->item[k];
                        if (entry->separator || !entry->command)
                            continue;
                        tiku_msg_t *row = tiku_app_resource_object(0);
                        bad |= INT(row, "window", w->id);
                        bad |= INT(row, "command", entry->command);
                        bad |= TEXT(row, "label", entry->label);
                        bad |= TEXT(row, "menu", menu->title);
                        bad |= BOOL(row, "enabled", entry->enabled);
                        bad |= BOOL(row, "checked", entry->marked);
                        bad |= append(bytes, size, &capacity, row);
                    }
            }
    }
    if (bad) {
        free(*bytes);
        *bytes = NULL;
        *size = 0;
        return TIKU_RESOURCE_UNAVAILABLE;
    }
    return 0;
}
tiku_resource_error_t tiku_app_resource_base_change(
    tiku_app_resources_t *owner, const char *path, const tiku_msg_t *value,
    int invoke, tiku_resource_snapshot_t *out, tiku_resource_effect_t *effect)
{
    if (strcmp(path, "commands/invoke") || !invoke)
        return TIKU_RESOURCE_UNSUPPORTED;
    const char *keys[] = {"window", "command"};
    int64_t id, command;
    if (!tiku_app_resource_keys(value, keys, 2) ||
        !tiku_app_resource_integer(value, "window", 1, UINT32_MAX, &id) ||
        !tiku_app_resource_integer(value, "command", 1, INT32_MAX, &command))
        return TIKU_RESOURCE_VALUE;
    app_resource_window_t *w = window(owner, (uint32_t)id);
    if (!w)
        return TIKU_RESOURCE_OWNER_LOST;
    if (!owner->descriptor->pick || !w->menus)
        return TIKU_RESOURCE_UNSUPPORTED;
    int found = 0;
    for (int i = 0; i < w->menus->nmenu; i++)
        for (int j = 0; j < w->menus->menu[i].nitem; j++) {
            tiku_menu_entry_t *entry = &w->menus->menu[i].item[j];
            if (entry->command == command && !entry->separator)
                found |= entry->enabled;
        }
    if (!found)
        return TIKU_RESOURCE_UNSUPPORTED;
    tiku_msg_t *result = tiku_app_resource_object(0);
    int bad = TEXT(result, "phase", "native_menu_dispatched") |
              INT(result, "window", id) | INT(result, "command", command) |
              BOOL(result, "input_gesture_granted", 0);
    if (bad) {
        tiku_msg_free(result);
        return TIKU_RESOURCE_NO_MEMORY;
    }
    if (tiku_app_resource_snapshot(out, result, "request_dispatched"))
        return TIKU_RESOURCE_NO_MEMORY;
    if (owner->descriptor->pick(owner->model, (uint32_t)id, (int)command) ||
        !native_count(owner, "windows"))
        tiku_app_resources_request_exit(owner);
    *effect = TIKU_RESOURCE_EFFECT_PARTIAL;
    return 0;
}
