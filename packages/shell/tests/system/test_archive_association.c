/* Archives route to TikuArchive: its manifest's four types, their File
 * Associations rows, a person's own preference, the types names are given
 * on a host disk, and an archive handed to the app.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include "tiku_manifest.h"
#include "tiku_trk_associations.h"
#include "../../system/shared/ui/commands/tiku_trk_launch_controller.c"

static int opens;
static char opened[512];

/** @brief Record the archive the native controller was handed. */
static int archive_opened(void *ctx, const tiku_trk_app_t *app,
                          const char *path)
{
    assert(ctx == &opens && !strcmp(app->name, "TikuArchive"));
    opens++;
    snprintf(opened, sizeof opened, "%s", path);
    return 1;
}

/** @brief Each type offered to TikuArchive, and listed by what it is. */
static void offered(tiku_trk_apps_t *apps, tiku_store_t *store)
{
    static const struct { const char *type, *label; } kinds[] = {
        { "application/zip", "ZIP archive (.zip)" },
        { "application/x-tar", "Tar archive (.tar)" },
        { "application/x-compressed-tar", "Compressed tar (.tar.gz, .tgz)" },
        { "application/gzip", "Gzip-compressed file (.gz)" }
    };
    tiku_trk_cand_t candidates[TIKU_TRK_APP_MAX];
    tiku_trk_associations_t rows;

    tiku_trk_associations_load(&rows, apps, NULL, store);
    for (size_t k = 0; k < sizeof kinds / sizeof kinds[0]; k++) {
        const char *type = kinds[k].type;
        int found = 0;

        assert(tiku_trk_launch_candidates(apps, &type, 1, store, candidates,
                                          TIKU_TRK_APP_MAX) == 1);
        assert(candidates[0].app == &apps->app[0]);
        for (int i = 0; i < rows.count; i++)
            if (!strcmp(rows.row[i].type, type)) {
                assert(!strcmp(rows.row[i].label, kinds[k].label));
                found = 1;
            }
        assert(found);
    }
}

/** @brief Offered, listed, overridable, and handed an archive. */
int main(void)
{
    tiku_trk_apps_t apps;
    tiku_manifest_t m;
    tiku_trk_cand_t candidates[TIKU_TRK_APP_MAX];
    const char *zip = "application/zip";
    char status[256];
    tiku_trk_launch_controller_t controller;
    const char *paths[] = {"/tmp/SDK 2.4.ZIP"};

    tiku_trk_launch_clear(&apps);
    assert(tiku_manifest_read("apps/archive/archive.app", &m, NULL, 0) == 0);
    assert(!strcmp(m.id, "archive") && !strcmp(m.name, "TikuArchive"));
    assert(tiku_trk_launch_add_document(&apps, m.name, "/runner",
                                        "/apps/archive/TikuArchive.so",
                                        m.types) == 0);
    tiku_store_t *store = tiku_store_memory_open();
    offered(&apps, store);
    assert(tiku_trk_launch_relation(&apps.app[0], "application/pdf") ==
           TIKU_TRK_REL_NONE);

    /* A person's choice of another tool wins, and can be taken back. */
    tiku_trk_launch_add(&apps, "Another unpacker", "/bin/true", zip);
    tiku_trk_launch_set_preferred(store, zip, "Another unpacker");
    assert(tiku_trk_launch_candidates(&apps, &zip, 1, store, candidates,
                                      TIKU_TRK_APP_MAX) == 2);
    assert(!strcmp(candidates[0].app->name, "Another unpacker"));
    tiku_trk_launch_set_preferred(store, zip, "");
    (void)tiku_trk_launch_candidates(&apps, &zip, 1, store, candidates,
                                     TIKU_TRK_APP_MAX);
    assert(candidates[0].app == &apps.app[0]);

    tiku_trk_launch_controller_init(&controller, &apps, store, status,
                                    sizeof status, NULL, &opens);
    controller.open_document = archive_opened;
    assert(tiku_trk_launch_controller_run(&controller, &apps.app[0], paths,
                                          1));
    assert(opens == 1 && !strcmp(opened, paths[0]));
    tiku_store_free(store);
    puts("PASS: ZIP, tar, tar.gz and gzip open in TikuArchive, listed by "
         "kind, overridable");
    return 0;
}
