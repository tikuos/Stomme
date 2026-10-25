/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_state_update.c - staged record persistence, merge and cache ownership.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_state_update.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); exit(1); } } while (0)

/** @brief Write an exact test record through the public cache API. */
static void put(tiku_store_t *store, const char *key, const char *value)
{
    CHECK(!tiku_state_write(store, "/node", key, value, strlen(value) + 1));
}

/** @brief Check a complete record value, including its terminator. */
static void value(tiku_store_t *store, const char *key, const char *expected)
{
    char buffer[128]; int count = tiku_state_read(store, "/node", key, buffer, sizeof buffer);
    CHECK(count == (int)strlen(expected) + 1 && !strcmp(buffer, expected));
}

/** @brief Run and finish one staged update. */
static tiku_store_update_result_t finish(tiku_store_update_t *update)
{
    tiku_store_update_result_t result;
    tiku_store_update_run(update); result = tiku_store_update_finish(update); tiku_store_update_free(update); return result;
}

int main(void)
{
    char directory[] = "/tmp/stomme-state-update-XXXXXX", path[512], saved[520], persisted[128];
    tiku_store_t *a, *b, *disk, *memory, *xattr;
    tiku_store_update_t *update, *other;
    struct stat before, after; FILE *file;
    CHECK(mkdtemp(directory) && !setenv("XDG_DATA_HOME", directory, 1));
    snprintf(path, sizeof path, "%s/tracker/devices/test.state", directory); snprintf(saved, sizeof saved, "%s.saved", path);
    a = tiku_store_sidecar_open("test"); CHECK(a && tiku_store_persistent(a)); put(a, "owned", "one"); put(a, "old", "kept"); CHECK(!tiku_store_flush(a));
    put(a, "owned", "unsaved"); CHECK(tiku_store_persisted_read(a, "/node", "owned", persisted, sizeof persisted) == 4 && !strcmp(persisted, "one"));
    put(a, "owned", "one"); CHECK(tiku_store_persisted_read(a, "/node", "absent", persisted, sizeof persisted) == -1 && errno == ENOENT);
    update = tiku_store_update_begin(a); CHECK(update); tiku_store_update_free(update); put(a, "prepared", "released"); CHECK(!tiku_store_flush(a));
    update = tiku_store_update_begin(a); CHECK(update); put(tiku_store_update_model(update), "owned", "two");
    other = tiku_store_update_begin(a); CHECK(!other && errno == EBUSY);
    put(a, "cached", "later"); CHECK(tiku_store_flush(a) == -1 && errno == EBUSY);
    b = tiku_store_sidecar_open("test"); CHECK(b); put(b, "foreign", "external"); CHECK(!tiku_store_flush(b)); tiku_store_free(b);
    CHECK(finish(update) == TIKU_STORE_UPDATE_OK); value(a, "owned", "two"); value(a, "foreign", "external"); value(a, "cached", "later");
    disk = tiku_store_sidecar_open("test"); value(disk, "owned", "two"); value(disk, "foreign", "external"); CHECK(tiku_state_read(disk, "/node", "cached", persisted, sizeof persisted) < 0); tiku_store_free(disk);
    CHECK(!tiku_store_flush(a)); disk = tiku_store_sidecar_open("test"); value(disk, "cached", "later"); tiku_store_free(disk);
    CHECK(!stat(path, &before)); update = tiku_store_update_begin(a); CHECK(update && finish(update) == TIKU_STORE_UPDATE_OK); CHECK(!stat(path, &after) && before.st_ino == after.st_ino);
    update = tiku_store_update_begin(a); CHECK(update); put(tiku_store_update_model(update), "owned", "three");
    b = tiku_store_sidecar_open("test"); put(b, "owned", "outside"); CHECK(!tiku_store_flush(b)); tiku_store_free(b);
    CHECK(finish(update) == TIKU_STORE_UPDATE_CONFLICT); value(a, "owned", "two");
    disk = tiku_store_sidecar_open("test"); value(disk, "owned", "outside"); tiku_store_free(disk);
    update = tiku_store_update_begin(a); CHECK(update && finish(update) == TIKU_STORE_UPDATE_OK); value(a, "owned", "outside");
    update = tiku_store_update_begin(a); CHECK(update); put(tiku_store_update_model(update), "owned", "cancelled"); tiku_store_update_cancel(update); CHECK(finish(update) == TIKU_STORE_UPDATE_CANCELLED); value(a, "owned", "outside");
    update = tiku_store_update_begin(a); CHECK(update); put(tiku_store_update_model(update), "owned", "detached-write"); tiku_store_update_run(update);
    tiku_store_update_detach(update); tiku_store_update_free(update); put(a, "cached2", "retained");
    CHECK(tiku_store_flush(a) == -1 && errno == EBUSY); value(a, "owned", "outside");
    disk = tiku_store_sidecar_open("test"); value(disk, "owned", "detached-write"); tiku_store_free(disk);
    update = tiku_store_update_begin(a); CHECK(update && finish(update) == TIKU_STORE_UPDATE_OK); value(a, "owned", "detached-write"); value(a, "cached2", "retained");
    update = tiku_store_update_begin(a); CHECK(update); put(tiku_store_update_model(update), "owned", "worker"); tiku_store_update_run(update);
    put(a, "owned", "newer"); CHECK(tiku_store_update_finish(update) == TIKU_STORE_UPDATE_UNCERTAIN); tiku_store_update_free(update); value(a, "owned", "newer");
    CHECK(!tiku_store_flush(a)); disk = tiku_store_sidecar_open("test"); value(disk, "owned", "newer"); tiku_store_free(disk);
    update = tiku_store_update_begin(a); CHECK(update); CHECK(tiku_state_forget(tiku_store_update_model(update), "/node") > 0); CHECK(finish(update) == TIKU_STORE_UPDATE_OK); CHECK(tiku_store_count(a) == 0);
    put(a, "owned", "again"); CHECK(!tiku_store_flush(a));
    CHECK(!rename(path, saved) && !mkdir(path, 0700)); update = tiku_store_update_begin(a); CHECK(update); put(tiku_store_update_model(update), "owned", "fail"); CHECK(finish(update) == TIKU_STORE_UPDATE_FAILED); value(a, "owned", "again");
    CHECK(!rmdir(path) && !rename(saved, path));
    file = fopen(path, "ab"); CHECK(file && fputc(1, file) != EOF && !fclose(file));
    update = tiku_store_update_begin(a); CHECK(update); put(tiku_store_update_model(update), "owned", "bad"); CHECK(finish(update) == TIKU_STORE_UPDATE_FAILED); value(a, "owned", "again");
    update = tiku_store_update_begin(a); CHECK(update); tiku_store_free(a); tiku_store_update_run(update); CHECK(tiku_store_update_finish(update) == TIKU_STORE_UPDATE_FAILED); tiku_store_update_free(update);
    memory = tiku_store_memory_open(); CHECK(memory && !tiku_store_persistent(memory)); put(memory, "a", "volatile"); update = tiku_store_update_begin(memory); CHECK(update); put(tiku_store_update_model(update), "a", "new"); CHECK(finish(update) == TIKU_STORE_UPDATE_OK); value(memory, "a", "new"); CHECK(tiku_store_persisted_read(memory, "/node", "a", persisted, sizeof persisted) == 4 && !strcmp(persisted, "new")); tiku_store_free(memory);
    xattr = tiku_store_xattr_open(); CHECK(xattr && !tiku_store_update_begin(xattr) && errno == ENOTSUP); tiku_store_free(xattr);
    CHECK(!unlink(path)); snprintf(path, sizeof path, "%s/tracker/devices", directory); CHECK(!rmdir(path)); snprintf(path, sizeof path, "%s/tracker", directory); CHECK(!rmdir(path) && !rmdir(directory));
    puts("PASS staged state: external merge, cached edits, conflict, cancellation, unknown outcome, deletion, malformed file and detached owner"); return 0;
}
