/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_editor_resources.c - native edits, context refusal and version-checked saves.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_editor_private.h"
#include "tiku_app_resources.h"
#include <assert.h>
extern const tiku_app_resource_export_t tiku_app_resources_v1;
static int paints;
/** @brief Count the adapter's repaint request without a display server. */
void tiku_editor_paint(edit_state_t *state)
{
    (void)state;
    paints++;
}
/** @brief Leave native scrolling outside the headless controller assertions. */
void tiku_editor_reveal(edit_state_t *state)
{
    (void)state;
}
/** @brief Observe menu publication without host input or a window. */
void tiku_editor_publish(edit_state_t *state)
{
    (void)state;
}
/** @brief Expose the native document's dirty predicate. */
int tiku_editor_modified(const edit_state_t *state)
{
    return tiku_editdoc_modified(state->doc);
}
/** @brief Clear pending native edit state after a successful controller operation. */
void tiku_editor_edited(edit_state_t *state)
{
    state->pending = state->allow_open = 0;
}
/** @brief Disable native transport in this direct owner-callback test. */
static int send_message(void *state, const tiku_msg_t *message)
{
    (void)state;
    (void)message;
    return 0;
}
/** @brief Copy the current opaque context precondition from a public capture. */
static void token(edit_state_t *state, tiku_app_resources_t *owner,
                  const char *root, char out[65])
{
    char path[256];
    snprintf(path, sizeof path, "%s/selection", root);
    tiku_resource_snapshot_t value = {0};
    assert(!tiku_app_resources_v1.capture(state, owner, path, &value));
    assert(tiku_app_resource_text(value.applied, "context", out, 65));
    tiku_resource_snapshot_clear(&value);
}
/** @brief Build one exact object command with its current selection precondition. */
static tiku_msg_t *input(const char *context)
{
    tiku_msg_t *value = tiku_app_resource_object(0);
    assert(!tiku_app_resource_member(
        value, "context",
        tiku_resource_value_string(TIKU_RESOURCE_TEXT, context)));
    return value;
}
/** @brief Invoke a native operation and release its owned response. */
static tiku_resource_error_t invoke(edit_state_t *state,
                                    tiku_app_resources_t *owner,
                                    const char *root, const char *command,
                                    tiku_msg_t *value)
{
    char path[256];
    snprintf(path, sizeof path, "%s/commands/%s", root, command);
    tiku_resource_snapshot_t out = {0};
    tiku_resource_effect_t effect = 0;
    tiku_resource_error_t result = tiku_app_resources_v1.change(
        state, owner, path, value, 1, &out, &effect);
    tiku_resource_snapshot_clear(&out);
    tiku_msg_free(value);
    return result;
}
/** @brief Verify native text without reading serialized private model state. */
static void text(edit_state_t *state, const char *expected)
{
    char bytes[128];
    size_t n = 0;
    assert(!tiku_editdoc_read(
        state->doc, (tiku_edit_range_t){0, tiku_editdoc_length(state->doc)},
        bytes, sizeof bytes, &n));
    assert(!strcmp(bytes, expected));
}
/** @brief Exercise native owner operations in an isolated temporary directory. */
int main(void)
{
    char dir[] = "/tmp/tiku-resource-editor-XXXXXX";
    assert(mkdtemp(dir));
    setenv("STOMME_RESOURCE_DIRECTORY", dir, 1);
    tiku_app_services_t services = {.send_message = send_message};
    tiku_app_descriptor_t descriptor = {.id = "editor", .name = "Editor"};
    tiku_app_resources_t *owner =
        tiku_app_resources_new(&services, &descriptor, &tiku_app_resources_v1);
    assert(owner);
    edit_state_t state = {.services = &services};
    assert(!tiku_editdoc_create(NULL, &state.doc));
    assert(!tiku_codeview_create(state.doc, &state.view));
    state.panes[0] = state.view;
    state.preferences =
        (editor_preferences_t){.tab = 4, .column = 80, .numbers = 1};
    for (size_t i = 0;; i++) {
        tiku_resource_initial_t initial = {0}, decoded = {0};
        int found = tiku_app_resources_v1.catalog(&state, owner, i, &initial);
        assert(found >= 0);
        if (!found)
            break;
        char path[512];
        snprintf(path, sizeof path,
                 "/apps/00000000000000000000000000000001/%s",
                 initial.spec.path);
        initial.spec.path = path;
        tiku_msg_t *wire = tiku_resource_initial_message(&initial);
        assert(wire && !tiku_resource_initial_decode(wire, &decoded));
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
        tiku_resource_initial_clear(&decoded);
        tiku_msg_free(wire);
        tiku_resource_snapshot_clear(&initial.initial);
    }
    snprintf(state.newline, sizeof state.newline, "\n");
    char root[128], context[65], old[65];
    snprintf(
        root, sizeof root, "documents/%s",
        tiku_app_resources_document(owner, tiku_editdoc_identity(state.doc)));
    token(&state, owner, root, context);
    tiku_msg_t *value = input(context);
    assert(!tiku_app_resource_member(
        value, "text",
        tiku_resource_value_string(TIKU_RESOURCE_TEXT, "hello")));
    assert(!invoke(&state, owner, root, "replace-text", value));
    text(&state, "hello");
    assert(tiku_editdoc_modified(state.doc) && paints == 1);
    assert(invoke(&state, owner, root, "undo", input(context)) ==
           TIKU_RESOURCE_STALE);
    text(&state, "hello");
    token(&state, owner, root, context);
    assert(!invoke(&state, owner, root, "undo", input(context)));
    text(&state, "");
    token(&state, owner, root, context);
    assert(!invoke(&state, owner, root, "redo", input(context)));
    text(&state, "hello");
    token(&state, owner, root, old);
    assert(!tiku_codeview_select(state.view, 0, 0));
    assert(invoke(&state, owner, root, "undo", input(old)) ==
           TIKU_RESOURCE_STALE);
    text(&state, "hello");
    token(&state, owner, root, context);
    snprintf(state.path, sizeof state.path, "%s/document.txt", dir);
    assert(!invoke(&state, owner, root, "save", input(context)));
    assert(!tiku_editdoc_modified(state.doc));
    assert(!tiku_codeview_insert(state.view, "!", 1, 0));
    token(&state, owner, root, context);
    FILE *f = fopen(state.path, "w");
    assert(f);
    assert(fputs("external", f) >= 0);
    assert(!fclose(f));
    assert(invoke(&state, owner, root, "save", input(context)) ==
           TIKU_RESOURCE_APPLY_FAILED);
    assert(tiku_editdoc_modified(state.doc));
    text(&state, "!hello");
    tiku_editdoc_set_read_only(state.doc, 1);
    token(&state, owner, root, context);
    value = input(context);
    assert(!tiku_app_resource_member(
        value, "text", tiku_resource_value_string(TIKU_RESOURCE_TEXT, "bad")));
    assert(invoke(&state, owner, root, "replace-text", value) ==
           TIKU_RESOURCE_READ_ONLY);
    text(&state, "!hello");
    tiku_editdoc_set_read_only(state.doc, 0);
    uint64_t identity = tiku_editdoc_identity(state.doc);
    assert(!tiku_editdoc_load(state.doc, "fresh", 5));
    assert(tiku_editdoc_identity(state.doc) != identity);
    assert(invoke(&state, owner, root, "undo", input(context)) ==
           TIKU_RESOURCE_UNSUPPORTED);
    text(&state, "fresh");
    char stable[33], presentation[33];
    uint64_t lifetime = tiku_editdoc_identity(state.doc);
    snprintf(stable, sizeof stable, "%s",
             tiku_app_resources_document(owner, lifetime));
    snprintf(
        presentation, sizeof presentation, "%s",
        tiku_app_resources_document_named(owner, "presentation", lifetime));
    assert(strcmp(stable, presentation));
    for (uint64_t i = 1; i <= 200; i++)
        assert(tiku_app_resources_document_named(owner, "other", i));
    assert(!strcmp(stable, tiku_app_resources_document(owner, lifetime)));
    char many[161];
    memset(many, 'x', 160);
    many[160] = 0;
    assert(!tiku_editdoc_load(state.doc, many, 160));
    snprintf(
        root, sizeof root, "documents/%s",
        tiku_app_resources_document(owner, tiku_editdoc_identity(state.doc)));
    tiku_edit_selection_t ranges[80];
    for (size_t i = 0; i < 80; i++)
        ranges[i] = (tiku_edit_selection_t){.anchor = (int64_t)i * 2,
                                            .caret = (int64_t)i * 2};
    assert(!tiku_codeview_select_many(state.view, ranges, 80, 79));
    token(&state, owner, root, old);
    ranges[60].anchor = ranges[60].caret = 121;
    assert(!tiku_codeview_select_many(state.view, ranges, 80, 79));
    assert(invoke(&state, owner, root, "undo", input(old)) ==
           TIKU_RESOURCE_STALE);
    token(&state, owner, root, context);
    assert(!tiku_editdoc_load(state.doc, "\xc3\xa9", 2));
    snprintf(
        root, sizeof root, "documents/%s",
        tiku_app_resources_document(owner, tiku_editdoc_identity(state.doc)));
    token(&state, owner, root, context);
    value = input(context);
    assert(!tiku_app_resource_member(
               value, "offset",
               tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, 0)) &&
           !tiku_app_resource_member(
               value, "limit",
               tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, 1)));
    assert(invoke(&state, owner, root, "read-text", value) ==
           TIKU_RESOURCE_LIMIT);
    tiku_app_resources_free(owner);
    tiku_codeview_destroy(state.view);
    tiku_editdoc_release(state.doc);
    unlink(state.path);
    assert(!rmdir(dir));
    puts(
        "PASS Editor resources: native edit/save/undo, selection staleness, replacement, read-only and disk conflict");
    return 0;
}
