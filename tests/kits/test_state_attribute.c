/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_state_attribute.c - native attribute conflicts, identity and cancellation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_state_attribute.h"
#include "tiku_state.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(test) do { if (!(test)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); exit(1); } } while (0)

/** @brief Create a regular node without a shell command. */
static void node(const char *path)
{
    FILE *file = fopen(path, "wb"); CHECK(file && !fclose(file));
}

/** @brief Copy a proposal and release its immutable input capture. */
static tiku_state_attribute_update_t *proposal(const char *path, const char *value)
{
    tiku_state_attribute_t *capture = tiku_state_attribute_read(path, "_trk/test");
    tiku_state_attribute_update_t *update;
    CHECK(capture);
    update = tiku_state_attribute_prepare(capture, value, value ? strlen(value) + 1u : 0u, value != NULL);
    tiku_state_attribute_free(capture); CHECK(update); return update;
}

/** @brief Run, check and release one copied update. */
static void finish(tiku_state_attribute_update_t *update, tiku_store_update_result_t expected)
{
    CHECK(tiku_state_attribute_run(update) == expected); tiku_state_attribute_update_free(update);
}

int main(void)
{
    char directory[] = "/tmp/stomme-attribute-XXXXXX", path[512], saved[520], link[520], other[520], bytes[64];
    tiku_store_t *store = tiku_store_xattr_open();
    tiku_state_attribute_t *capture; tiku_state_attribute_update_t *update;
    size_t length;
    CHECK(store && mkdtemp(directory));
    snprintf(path, sizeof path, "%s/node", directory); snprintf(saved, sizeof saved, "%s.saved", path);
    snprintf(link, sizeof link, "%s.link", path); snprintf(other, sizeof other, "%s.other", path); node(path); node(other);
    capture = tiku_state_attribute_read(path, "_trk/test"); CHECK(capture && !tiku_state_attribute_present(capture));
    CHECK(!tiku_state_attribute_bytes(capture, &length) && !length); tiku_state_attribute_free(capture);
    update = proposal(path, "first"); finish(update, TIKU_STORE_UPDATE_OK);
    CHECK(tiku_state_read(store, path, "_trk/test", bytes, sizeof bytes) == 6 && !strcmp(bytes, "first"));
    update = proposal(path, "stale"); CHECK(!tiku_state_write(store, path, "_trk/test", "external", 9));
    finish(update, TIKU_STORE_UPDATE_CONFLICT);
    CHECK(tiku_state_read(store, path, "_trk/test", bytes, sizeof bytes) == 9 && !strcmp(bytes, "external"));
    update = proposal(path, "fresh"); CHECK(!tiku_state_write(store, path, "_trk/unknown", "kept", 5)); finish(update, TIKU_STORE_UPDATE_OK);
    CHECK(tiku_state_read(store, path, "_trk/unknown", bytes, sizeof bytes) == 5 && !strcmp(bytes, "kept"));
    update = proposal(path, "cancelled"); tiku_state_attribute_cancel(update); finish(update, TIKU_STORE_UPDATE_CANCELLED);
    CHECK(tiku_state_read(store, path, "_trk/test", bytes, sizeof bytes) == 6 && !strcmp(bytes, "fresh"));
    update = proposal(path, "replacement"); CHECK(!rename(path, saved)); node(path); CHECK(!tiku_state_write(store,path,"_trk/test","fresh",6)); finish(update, TIKU_STORE_UPDATE_CONFLICT);
    CHECK(tiku_state_read(store, path, "_trk/test", bytes, sizeof bytes) == 6 && !strcmp(bytes,"fresh")); CHECK(!unlink(path) && !rename(saved, path));
    update = proposal(path, "redirected"); CHECK(!rename(path, saved) && !symlink(other, path)); finish(update, TIKU_STORE_UPDATE_CONFLICT);
    CHECK(tiku_state_read(store, other, "_trk/test", bytes, sizeof bytes) < 0); CHECK(!unlink(path) && !rename(saved, path));
    CHECK(!symlink(path, link)); update = proposal(link, "resolved"); CHECK(!unlink(link) && !symlink(other, link));
    finish(update, TIKU_STORE_UPDATE_OK);
    CHECK(tiku_state_read(store, path, "_trk/test", bytes, sizeof bytes) == 9 && !strcmp(bytes, "resolved"));
    CHECK(tiku_state_read(store, other, "_trk/test", bytes, sizeof bytes) < 0);
    update = proposal(path, NULL); finish(update, TIKU_STORE_UPDATE_OK);
    update = proposal(path, NULL); finish(update, TIKU_STORE_UPDATE_OK);
    capture = tiku_state_attribute_read(path, "_trk/test"); CHECK(capture);
    CHECK(!tiku_state_attribute_prepare(capture, "x", TIKU_STATE_ATTRIBUTE_MAX + 1u, 1) && errno == EINVAL);
    update = tiku_state_attribute_prepare(capture, NULL, 0, 1); CHECK(update); tiku_state_attribute_free(capture); finish(update, TIKU_STORE_UPDATE_OK);
    capture = tiku_state_attribute_read(path, "_trk/test"); CHECK(capture && tiku_state_attribute_present(capture));
    CHECK(tiku_state_attribute_bytes(capture, &length) && !length); tiku_state_attribute_free(capture);
    CHECK(!unlink(link) && !unlink(other) && !unlink(path) && !rmdir(directory)); tiku_store_free(store);
    puts("native attribute updates: passed"); return 0;
}
