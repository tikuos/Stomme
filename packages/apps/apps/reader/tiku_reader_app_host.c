/* tiku_reader_app_host.c -- the application calls TikuReader, Help and
 * TikuMarker share, around one viewer window.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_reader_app_host.h"
#include "tiku_reader_private.h"
#include "tiku_app_resources.h"
#include "tiku_window_app.h"
#include "tiku_msg.h"
#include "tiku_work.h"
#include <stdlib.h>

/** @brief The running application: the desktop's calls and the window. */
typedef struct {
    tiku_app_services_t services;
    tiku_window_app_t *host;
    tiku_reader_help_window_t reader;
    int dirty;
} reader_app_t;

/** @brief Ask the desktop's file panel for a document, starting in the
 *         shown document's folder, or at home. */
static void choose(void *ctx)
{
    reader_app_t *a = ctx;
    if (a->services.pick)
        a->services.pick(a->services.ctx, tiku_window_app_id(a->host,
            a->reader.window), TIKU_APP_PICK_LOCAL_OPEN,
            a->reader.root[0] ? a->reader.root : getenv("HOME"), NULL);
}

/** @brief Hand a device path a link names back to the desktop to open. */
static int open_path(void *ctx, const char *path)
{
    reader_app_t *a = ctx;
    return a->services.open_path &&
           a->services.open_path(a->services.ctx, path);
}

void tiku_reader_app_stop(void *state)
{
    reader_app_t *a = state;
    tiku_reader_help_window_destroy(&a->reader);
    tiku_work_collect();
    tiku_window_app_free(a->host);
    free(a);
}

int tiku_reader_app_start(void **state, const tiku_app_services_t *services,
                          const tiku_reader_profile_t *profile)
{
    if (!state || !services) return -1;
    *state = NULL;
    reader_app_t *a = calloc(1, sizeof *a);
    if (!a) return -1;
    a->services = *services;
    a->host = tiku_window_app_new(services);
    if (!a->host) { free(a); return -1; }
    const tiku_msg_t *m = profile ? NULL : services->startup_message;
    if (m) {
        const char *root = tiku_msg_find_string(m, "root", 0);
        const char *page = tiku_msg_find_string(m, "page", 0);
        const char *html = tiku_msg_find_string(m, "html", 0);
        const char *title = tiku_msg_find_string(m, "title", 0);
        tiku_reader_help_window_init(&a->reader,
            tiku_window_app_workspace(a->host), root ? root : "", 1024, 768,
            &a->dirty);
        tiku_reader_help_window_opener(&a->reader, open_path, a);
        if (html) tiku_reader_help_window_html(&a->reader, title, html);
        else tiku_reader_help_window_open_page(&a->reader,
                                               page ? page : "index.html");
    } else {
        tiku_reader_document_window_init_profile(&a->reader,
            tiku_window_app_workspace(a->host), 1024, 768, &a->dirty,
            choose, a, profile);
        tiku_reader_help_window_opener(&a->reader, open_path, a);
        if (services->startup_document)
            tiku_reader_document_window_file(&a->reader,
                                             services->startup_document);
        /* A document that cannot be read still leaves a window open,
         * showing the welcome page and why the document was refused. */
        tiku_reader_help_window_open(&a->reader);
    }
    if (!m && services->help_page)
        services->help_page(services->ctx, a->reader.profile->help_page);
    if (!tiku_window_app_sync(a->host, 1)) {
        tiku_reader_app_stop(a);
        return -1;
    }
    *state = a;
    return 0;
}

void tiku_reader_app_tick(void *state, int64_t now)
{
    reader_app_t *a = state; (void)now;
    tiku_work_collect();
    tiku_window_app_sync(a->host, a->dirty); a->dirty = 0;
}

int tiku_reader_app_event(void *state, uint32_t id, const tiku_event_t *ev)
{
    reader_app_t *a = state;
    tiku_window_app_event(a->host, id, ev); a->dirty = 1;
    return !a->reader.window;
}

int tiku_reader_app_pick(void *state, uint32_t id, int command)
{
    reader_app_t *a = state;
    tiku_window_app_pick(a->host, id, command); a->dirty = 1;
    return !a->reader.window;
}

void tiku_reader_app_picked(void *state, uint32_t id, const char *path)
{
    reader_app_t *a = state; (void)id;
    if (path) tiku_reader_document_window_file(&a->reader, path);
}

void tiku_reader_app_closed(void *state, uint32_t id)
{
    reader_app_t *a = state;
    tiku_window_app_closed(a->host, id);
}

#define RR_MEMBER tiku_app_resource_member
#define RR_TEXT(o, k, v)                                                      \
    RR_MEMBER(o, k, tiku_resource_value_string(TIKU_RESOURCE_TEXT, v))
#define RR_INT(o, k, v)                                                       \
    RR_MEMBER(o, k, tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, v))
/** @brief Resolve a live page identity to its owner-local relative suffix. */
static const char *resource_suffix(reader_app_t *app,
                                   tiku_app_resources_t *owner,
                                   const char *path)
{
    const char *id =
        tiku_app_resources_document(owner, app->reader.resource_document);
    return id && !strncmp(path, "documents/", 10) && strlen(path) > 42 &&
                   !strncmp(path + 10, id, 32) && path[42] == '/'
               ? path + 43
               : NULL;
}
/** @brief Copy native reading selection and page generation. */
static tiku_msg_t *resource_selection(reader_app_t *app)
{
 struct help_reader *reader=app->reader.reader;if(!reader)return NULL;
 tiku_msg_t *value = tiku_app_resource_object(0);
 int bad =
     RR_INT(value, "anchor", reader->a) | RR_INT(value, "caret", reader->b);
 bad |=
     RR_MEMBER(value, "generation",
               tiku_app_resource_counter_value(app->reader.resource_document));
 if(bad){tiku_msg_free(value);return NULL;}return value;
}
/** @brief Capture page, view and selection state without treating repaint flags as edits. */
static tiku_resource_error_t resource_capture(void *state,
                                              tiku_app_resources_t *owner,
                                              const char *path,
                                              tiku_resource_snapshot_t *out)
{
    reader_app_t *app = state;
    tiku_reader_help_window_t *window = &app->reader;
    const char *name = resource_suffix(app, owner, path);
    if (!name)
        return TIKU_RESOURCE_UNSUPPORTED;
    struct help_reader *reader = window->reader;
    tiku_msg_t *value = NULL;
    int bad = 0;
    if (!strcmp(name, "path"))
        value = tiku_resource_value_string(TIKU_RESOURCE_TEXT, window->page);
    else if (!strcmp(name, "type"))
        value = tiku_resource_value_string(TIKU_RESOURCE_TEXT,
                                           "text/html; charset=utf-8");
    else if (!strcmp(name, "dirty"))
        value = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 0);
    else if (!strcmp(name, "selection")) {
        value = resource_selection(app);
        char token[65];
        if (!value || tiku_app_resource_token(value, token))
            bad = 1;
        else
            bad |= RR_TEXT(value, "context", token);
    } else if (!strcmp(name, "identity")) {
        value = tiku_app_resource_object(0);
        bad |= RR_TEXT(
            value, "id",
            tiku_app_resources_document(owner, window->resource_document));
        bad |= RR_MEMBER(
            value, "generation",
            tiku_app_resource_counter_value(window->resource_document));
        bad |= RR_INT(value, "window",
                      tiku_window_app_id(app->host, window->window));
        bad |=
            RR_MEMBER(value, "read_only",
                      tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 1));
    } else if (!strcmp(name, "view")) {
        value = tiku_app_resource_object(0);
        bad |= RR_INT(value, "scroll", window->bar.value);
        bad |= RR_INT(value, "text_size", reader ? reader->text_size : 0);
        bad |=
            RR_INT(value, "reading_width", reader ? reader->reading_width : 0);
    } else if (!strcmp(name, "objects")) {
        value = tiku_app_resource_object(0);
        bad |= RR_INT(value, "links", window->doc ? window->doc->nlink : 0);
        bad |= RR_INT(value, "images", window->doc ? window->doc->nimage : 0);
        bad |= RR_INT(value, "blocks", window->doc ? window->doc->nblock : 0);
    } else if (!strcmp(name, "commands") ||
               !strcmp(name, "commands/read-text")) {
        value = tiku_app_resource_object(0);
        bad |= RR_TEXT(value, "scope", "native_read_only_page");
        bad |= RR_INT(value, "count", 1);
    } else
        return TIKU_RESOURCE_UNSUPPORTED;
    if (bad) {
        tiku_msg_free(value);
        return TIKU_RESOURCE_UNAVAILABLE;
    }
    return tiku_app_resource_snapshot(out, value, "observed")
               ? TIKU_RESOURCE_NO_MEMORY
               : 0;
}
/** @brief Enumerate read-only document facts and bounded native text inspection. */
static int resource_catalog(void *state, tiku_app_resources_t *owner,
                            size_t index, tiku_resource_initial_t *out)
{
    static const char *const keys[] = {
        "identity", "path",      "type",
        "dirty",    "selection", "view",
        "commands", "objects",   "commands/read-text"};
    if (index >= 9)
        return 0;
    reader_app_t *app = state;
    const char *id = tiku_app_resources_document(
                   owner, app->reader.resource_document),
               *path = tiku_app_resources_path(owner, "documents/%s/%s", id,
                                               keys[index]);
    out->spec = (tiku_resource_spec_t){
        .path = path,
        .kind = index == 8   ? TIKU_RESOURCE_ACTION
                : index >= 5 ? TIKU_RESOURCE_COLLECTION
                             : TIKU_RESOURCE_STATE,
        .schema_version = 1,
        .schema = {.type = index == 1 || index == 2 ? TIKU_RESOURCE_TEXT
                           : index == 3             ? TIKU_RESOURCE_BOOLEAN
                                                    : TIKU_RESOURCE_OBJECT,
                   .text_max = 4096},
        .persistence = "none"};
    return resource_capture(app, owner, path, &out->initial) ? -1 : 1;
}
/** @brief Inspect bounded native flattened text with page and selection preconditions. */
static tiku_resource_error_t
resource_change(void *state, tiku_app_resources_t *owner, const char *path,
                const tiku_msg_t *value, int invoke,
                tiku_resource_snapshot_t *out, tiku_resource_effect_t *effect)
{
    (void)effect;
    reader_app_t *app = state;
    const char *name = resource_suffix(app, owner, path);
    if (!name || strcmp(name, "commands/read-text") || !invoke)
        return TIKU_RESOURCE_UNSUPPORTED;
    const char *keys[] = {"context", "offset", "limit"};
    int64_t offset, limit;
    char expected[65], actual[65];
    if (!tiku_app_resource_keys(value, keys, 3) ||
        !tiku_app_resource_text(value, "context", expected, sizeof expected))
        return TIKU_RESOURCE_VALUE;
    tiku_msg_t *selection = resource_selection(app);
    int bad = !selection || tiku_app_resource_token(selection, actual);
    tiku_msg_free(selection);
    if (bad)
        return TIKU_RESOURCE_UNAVAILABLE;
    if (strcmp(expected, actual))
        return TIKU_RESOURCE_STALE;
    struct help_reader *reader = app->reader.reader;
    if (!reader)
        return TIKU_RESOURCE_UNAVAILABLE;
    if (!tiku_app_resource_integer(value, "offset", 0, reader->length,
                                   &offset) ||
        !tiku_app_resource_integer(value, "limit", 1, 4096, &limit) ||
        (offset < reader->length &&
         ((unsigned char)reader->text[offset] & 0xc0) == 0x80))
        return TIKU_RESOURCE_VALUE;
    size_t end = offset + limit;
    if (end > (size_t)reader->length)
        end = reader->length;
    while (end > (size_t)offset && end < (size_t)reader->length &&
           ((unsigned char)reader->text[end] & 0xc0) == 0x80)
        end--;
    if (end == (size_t)offset && end < (size_t)reader->length)
        return TIKU_RESOURCE_LIMIT;
    char text[4097];
    memcpy(text, reader->text + offset, end - offset);
    text[end - offset] = 0;
    tiku_msg_t *result = tiku_app_resource_object(0);
    bad = RR_TEXT(result, "text", text) | RR_INT(result, "next", end) |
          RR_MEMBER(result, "eof",
                    tiku_resource_value_integer(
                        TIKU_RESOURCE_BOOLEAN, end == (size_t)reader->length));
    if (bad) {
        tiku_msg_free(result);
        return TIKU_RESOURCE_NO_MEMORY;
    }
    return tiku_app_resource_snapshot(out, result, "native_complete")
               ? TIKU_RESOURCE_NO_MEMORY
               : 0;
}
/** @brief Catalog schemas change when the native reader adopts a replacement page. */
static uint64_t resource_revision(void *state)
{
    return ((reader_app_t *)state)->reader.resource_document;
}
/** @brief Optional versioned native application resource callbacks shared by reader profiles. */
__attribute__((visibility("default")))
const tiku_app_resource_export_t tiku_app_resources_v1 = {
    .version = 1,
    .size = sizeof(tiku_app_resource_export_t),
    .catalog_revision = resource_revision,
    .catalog = resource_catalog,
    .capture = resource_capture,
    .change = resource_change};
