/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_document_resources.c - typed native document commands and refusal invariants.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_document_resources.h"
#include <assert.h>
/** @brief Supply deterministic grapheme geometry without native fonts. */
static tiku_rich_status_t shape(void *ctx, const tiku_rich_chars_t *chars,
                                const char *text, size_t length,
                                const uint8_t *boundaries,
                                tiku_rich_shape_t *out)
{
    (void)ctx;
    (void)chars;
    (void)text;
    size_t end = 1;
    while (end < length && !boundaries[end])
        end++;
    *out = (tiku_rich_shape_t){.bytes = end,
                               .glyph_count = 1,
                               .caret_count = 2,
                               .advance = 6,
                               .ascent = 9,
                               .descent = 3,
                               .glyphs = {{1, 1, 0, 0}},
                               .carets = {{0, 0}, {end, 6}}};
    return 0;
}
/** @brief Keep owner transport disabled while testing its native callback boundary. */
static int send_message(void *ctx, const tiku_msg_t *message)
{
    (void)ctx;
    (void)message;
    return 0;
}
/** @brief Build a typed invocation carrying a current selection token. */
static tiku_msg_t *input(dr_document_t *d, tiku_msg_t *payload)
{
    char context[65];
    assert(!dr_token(d, context));
    tiku_msg_t *value = tiku_app_resource_object(0);
    assert(!DR_TEXT(value, "context", context));
    if (payload)
        assert(!DR_MEMBER(value, "payload", payload));
    return value;
}
/** @brief Invoke a native command by the machine-readable catalog key. */
static tiku_resource_error_t invoke(tiku_app_resources_t *owner,
                                    dr_document_t *d,
                                    tiku_richcmd_kind_t command,
                                    tiku_msg_t *value)
{
    char path[256];
    snprintf(path, sizeof path, "documents/%s/commands/%s",
             tiku_app_resources_document(owner, tiku_richdoc_identity(d->doc)),
             tiku_richcmd_info(command)->key);
    tiku_resource_snapshot_t out = {0};
    tiku_resource_effect_t effect = 0;
    tiku_resource_error_t result =
        dr_change(owner, d, path, value, 1, &out, &effect);
    tiku_resource_snapshot_clear(&out);
    tiku_msg_free(value);
    return result;
}
/** @brief Check plain text through the immutable native snapshot API. */
static void holds(tiku_richdoc_t *doc, const char *expected)
{
    tiku_richsnapshot_t *snapshot = NULL;
    char text[128];
    size_t n;
    assert(!tiku_richdoc_snapshot(doc, &snapshot));
    assert(!tiku_richsnapshot_text(snapshot, text, sizeof text, &n));
    assert(!strcmp(text, expected));
    tiku_richsnapshot_release(snapshot);
}
/** @brief Verify each native provider preserves selection guards, history and validation. */
int main(void)
{
    setenv("STOMME_RESOURCE_DIRECTORY", "/tmp", 1);
    tiku_app_services_t services = {.send_message = send_message};
    tiku_app_descriptor_t descriptor = {.id = "document-test",
                                        .name = "Document"};
    tiku_app_resources_t *owner =
        tiku_app_resources_new(&services, &descriptor, NULL);
    assert(owner);
    tiku_richdoc_t *doc = NULL;
    assert(!tiku_richdoc_create(NULL, &doc));
    uint64_t identity = tiku_richdoc_identity(doc);
    assert(identity);
    tiku_richdoc_t *other = NULL;
    assert(!tiku_richdoc_create(NULL, &other));
    assert(tiku_richdoc_identity(other) != identity);
    tiku_richdoc_release(other);
    tiku_richlayout_options_t options = {.width = 320,
                                         .provider = {.shape = shape}};
    tiku_richview_t *view = NULL;
    assert(!tiku_richview_create(doc, &options, &view));
    dr_document_t d = {.doc = doc,
                       .view = view,
                       .path = "",
                       .type = "application/x-tiku-rich-document"};
    for(size_t i=0;;i++){
        tiku_resource_initial_t initial = {0};
        int found = dr_catalog(owner, &d, i, &initial);
        assert(found >= 0);
        if (!found)
            break;
        char path[512];
        snprintf(path, sizeof path,
                 "/apps/00000000000000000000000000000001/%s",
                 initial.spec.path);
        initial.spec.path = path;
        tiku_resource_provider_client_t *client =
            tiku_resource_provider_client_assigned(
                "00000000000000000000000000000003",
                "00000000000000000000000000000001", TIKU_RESOURCE_FEATURES);
        assert(client);
        tiku_msg_t *stage = tiku_resource_provider_client_stage(
            client, "00000000000000000000000000000002", 0, 1, &initial, 1);
        assert(stage);
        tiku_msg_free(stage);
        tiku_resource_provider_client_free(client);
        tiku_resource_snapshot_clear(&initial.initial);
    }
    tiku_msg_t *stale = input(&d, NULL);
    assert(!invoke(
        owner, &d, TIKU_RICHCMD_REPLACE_TEXT,
        input(&d, tiku_resource_value_string(TIKU_RESOURCE_TEXT, "hello"))));
    holds(doc, "hello");
    assert(invoke(owner, &d, TIKU_RICHCMD_UNDO, stale) == TIKU_RESOURCE_STALE);
    holds(doc, "hello");
    assert(!invoke(owner, &d, TIKU_RICHCMD_UNDO, input(&d, NULL)));
    holds(doc, "");
    assert(!invoke(owner, &d, TIKU_RICHCMD_REDO, input(&d, NULL)));
    holds(doc, "hello");
    tiku_richsession_t *session = tiku_richview_session(view);
    tiku_rich_pos_t a, b;
    assert(!tiku_richsession_selection(session, &a, &b));
    a.byte = 0;
    b.byte = 5;
    stale = input(&d, NULL);
    uint64_t generation = tiku_richdoc_generation(doc);
    assert(!tiku_richsession_select(session, generation, a, b));
    assert(tiku_richdoc_generation(doc) == generation);
    assert(invoke(owner, &d, TIKU_RICHCMD_UNDO, stale) == TIKU_RESOURCE_STALE);
    assert(!invoke(owner, &d, TIKU_RICHCMD_BOLD, input(&d, NULL)));
    tiku_rich_selection_t format;
    assert(!tiku_richsession_format(session, &format));
    assert(format.chars.bold);
    assert(!invoke(
        owner, &d, TIKU_RICHCMD_LINK_SET,
        input(&d, tiku_resource_value_string(TIKU_RESOURCE_TEXT,
                                             "https://example.test/page"))));
    assert(!tiku_richsession_format(session, &format));
    assert(format.link && !strcmp(format.link, "https://example.test/page"));
    tiku_msg_t *invalid = tiku_app_resource_object(0);
    assert(!DR_INT(invalid, "rows", 0) && !DR_INT(invalid, "columns", 2));
    generation = tiku_richdoc_generation(doc);
    assert(invoke(owner, &d, TIKU_RICHCMD_TABLE_INSERT, input(&d, invalid)) ==
           TIKU_RESOURCE_VALUE);
    assert(tiku_richdoc_generation(doc) == generation);
    holds(doc, "hello");
    tiku_richdoc_set_read_only(doc, 1);
    assert(invoke(owner, &d, TIKU_RICHCMD_REPLACE_TEXT,
                  input(&d, tiku_resource_value_string(TIKU_RESOURCE_TEXT,
                                                       "refused"))) ==
           TIKU_RESOURCE_READ_ONLY);
    holds(doc, "hello");
    tiku_richdoc_set_read_only(doc, 0);
    assert(!tiku_richsession_selection(session, &a, &b));
    a = b;
    assert(
        !tiku_richsession_select(session, tiku_richdoc_generation(doc), a, b));
    tiku_msg_t *image = tiku_app_resource_object(0),
               *pixels = tiku_app_resource_object(1);
    assert(!tiku_app_resource_item(
        pixels, tiku_resource_value_string(TIKU_RESOURCE_TEXT, "ff123456")));
    assert(!DR_INT(image, "pixel_width", 1) &&
           !DR_INT(image, "pixel_height", 1) && !DR_INT(image, "width", 12) &&
           !DR_INT(image, "height", 12) &&
           !DR_TEXT(image, "alt", "test pixel") &&
           !DR_MEMBER(image, "pixels", pixels));
    assert(!invoke(owner, &d, TIKU_RICHCMD_IMAGE_INSERT, input(&d, image)));
    assert(tiku_richdoc_generation(doc) > generation);
    assert(!invoke(owner, &d, TIKU_RICHCMD_UNDO, input(&d, NULL)));
    holds(doc, "hello");
    tiku_msg_t *read = input(&d, NULL);
    assert(!DR_INT(read, "offset", 0) && !DR_INT(read, "limit", 32));
    char path[256];
    snprintf(path, sizeof path, "documents/%s/commands/read-objects",
             tiku_app_resources_document(owner, tiku_richdoc_identity(doc)));
    tiku_resource_snapshot_t observed = {0};
    tiku_resource_effect_t effect = 0;
    assert(!dr_change(owner, &d, path, read, 1, &observed, &effect));
    tiku_msg_t *wire = tiku_resource_snapshot_message(&observed);
    assert(wire);
    size_t wire_size = tiku_msg_flat_size(wire);
    void *encoded = malloc(wire_size);
    assert(encoded && tiku_msg_flatten(wire, encoded, wire_size, NULL));
    assert(!tiku_resource_message_check_nested(encoded, wire_size, 3));
    free(encoded);
    tiku_msg_free(wire);
    tiku_msg_free(read);
    tiku_resource_snapshot_clear(&observed);
    tiku_richview_destroy(view);
    tiku_richdoc_release(doc);
    tiku_app_resources_free(owner);
    puts(
        "PASS document resources: native history, selection guards, formats, links, images, invalid tables and read-only refusal");
    return 0;
}
