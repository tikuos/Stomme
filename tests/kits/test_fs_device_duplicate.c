/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_fs_device_duplicate.c - a duplicate inside a device volume goes
 * through the device's backend, not the local filesystem (Stomme#21).
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_fs.h"
#include "tiku_mount.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define CHECK(t) do{if(!(t)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#t);exit(1);}}while(0)

/** @brief Run the job to its end; the first error it asked about, or "". */
static void finish(tiku_fs_t *fs, char *first, size_t max)
{
    first[0] = '\0';
    for (int guard = 0; guard < 20000 && tiku_fs_busy(fs); guard++) {
        if (tiku_fs_step(fs, 8) != 0) continue;
        if (!tiku_fs_progress(fs)->error_ask) break;
        if (first[0] == '\0') snprintf(first, max, "%s", tiku_fs_progress(fs)->error);
        tiku_fs_resolve_error(fs, 1);
    }
}

int main(void)
{
    char dir[] = "/tmp/stomme-device-duplicate-XXXXXX", local[600], mounted[600], copy[600], err[256], got[16];
    tiku_backend_t *ns = tiku_mount_open(), *board = tiku_backend_posix_open();
    tiku_fs_t *fs;
    FILE *file;
    const char *paths[1];

    CHECK(mkdtemp(dir) != NULL && ns != NULL && board != NULL);
    /* A board: a backend with a device identity, mounted under /board. */
    snprintf(board->devid, sizeof board->devid, "%s", "uid-board");
    CHECK(tiku_mount_add(ns, "/", tiku_backend_posix_open(), 1) == 0 && tiku_mount_add(ns, "/board", board, 1) == 0);
    CHECK((fs = tiku_fs_new(ns)) != NULL);
    snprintf(local, sizeof local, "%s/node", dir);
    CHECK((file = fopen(local, "w")) != NULL && fwrite("value", 1, 5, file) == 5 && fclose(file) == 0);
    snprintf(mounted, sizeof mounted, "/board%s/node", dir);
    paths[0] = mounted;
    CHECK(tiku_fs_duplicate(fs, paths, 1) == 0);
    finish(fs, err, sizeof err);
    if (err[0] != '\0') fprintf(stderr, "duplicate: %s\n", err);
    CHECK(err[0] == '\0' && tiku_fs_progress(fs)->failed == 0 && tiku_fs_progress(fs)->done == 1);
    snprintf(copy, sizeof copy, "%s/node copy", dir);
    CHECK((file = fopen(copy, "r")) != NULL && fread(got, 1, sizeof got, file) == 5 && fclose(file) == 0);
    CHECK(memcmp(got, "value", 5) == 0);
    tiku_fs_free(fs);
    ns->ops->close(ns);
    CHECK(unlink(copy) == 0 && unlink(local) == 0 && rmdir(dir) == 0);
    puts("PASS device duplicate: a file duplicated inside a device volume is copied through its backend");
    return 0;
}
