/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_circuit_association.c - circuit files route to TikuCircuit: its
 * manifest's five types and their File Associations rows, the types a
 * host disk's names are given, .sch among them, the kinds' French names,
 * a person's own preference, and a plain .xml left unclaimed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_manifest.h"
#include "tiku_strings.h"
#include "tiku_trk_associations.h"
#include "tiku_trk_launch_controller.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int opens;                   /* documents handed to TikuCircuit  */
static char opened[512];            /* the last of them                 */

/** @brief Record the document the native controller was handed. */
static int
document_opened(void *ctx, const tiku_trk_app_t *app, const char *path)
{
    assert(ctx == &opens && !strcmp(app->name, "TikuCircuit"));
    opens++;
    snprintf(opened, sizeof opened, "%s", path);
    return 1;
}

/** @brief Each of TikuCircuit's five types listed once, by its format's
 *         name, and no other type. */
static void
listed(tiku_trk_apps_t *apps, tiku_store_t *store)
{
    static const struct { const char *type, *label; } kinds[] = {
        {"application/x-kicad-schematic", "KiCad schematic (.kicad_sch)"},
        {"application/x-excellon", "Drill drawing (.drl, .xnc, .xln)"},
        {"application/x-gerber", "Gerber artwork (.gbr and layer files)"},
        {"application/x-gerber-job", "Gerber job (.gbrjob)"},
        {"application/x-schematic",
         "Schematic (.sch; content checked on open)"},
    };
    tiku_trk_associations_t rows;

    tiku_trk_associations_load(&rows, apps, NULL, store);
    assert(rows.count == 5);
    for (size_t k = 0; k < sizeof kinds / sizeof kinds[0]; k++) {
        int found = 0;

        for (int i = 0; i < rows.count; i++) {
            if (!strcmp(rows.row[i].type, kinds[k].type)) {
                assert(!strcmp(rows.row[i].label, kinds[k].label));
                found++;
            }
        }
        assert(found == 1);
    }
}

/** @brief Each circuit type's kind, read in French from the catalogue. */
static void
french_kinds(void)
{
    static const struct { const char *type, *kind; } kinds[] = {
        {"application/x-kicad-schematic", "Schéma KiCad"},
        {"application/x-excellon", "Plan de perçage"},
        {"application/x-gerber", "Typon Gerber"},
        {"application/x-gerber-job", "Tâche Gerber"},
        {"application/x-schematic", "Schéma (.sch)"},
    };
    tiku_model_t model;

    assert(tiku_strings_load_file("assets/strings/fr.xml") > 0);
    for (size_t k = 0; k < sizeof kinds / sizeof kinds[0]; k++) {
        memset(&model, 0, sizeof model);
        model.kind = TIKU_KIND_FILE;
        snprintf(model.type, sizeof model.type, "%s", kinds[k].type);
        assert(!strcmp(tiku_model_kind_string(&model), kinds[k].kind));
    }
    tiku_strings_reset();
}

/** @brief The type each name below is given, or NULL for one no
 *         application here opens. */
static const struct { const char *name, *type; } names[] = {
    {"board.KICAD_SCH", "application/x-kicad-schematic"},
    {"board.kicad_sch", "application/x-kicad-schematic"},
    {"board.DRL", "application/x-excellon"},
    {"board.xnc", "application/x-excellon"},
    {"board.xln", "application/x-excellon"},
    {"board.GBR", "application/x-gerber"},
    {"board.ger", "application/x-gerber"},
    {"board.gbrx", "application/x-gerber"},
    {"board.gtl", "application/x-gerber"},
    {"board.gbl", "application/x-gerber"},
    {"board.gts", "application/x-gerber"},
    {"board.gbs", "application/x-gerber"},
    {"board.gtp", "application/x-gerber"},
    {"board.gbp", "application/x-gerber"},
    {"board.gto", "application/x-gerber"},
    {"board.gbo", "application/x-gerber"},
    {"board.gko", "application/x-gerber"},
    {"board.sch", "application/x-schematic"},
    {"board.xml", NULL},
    {"board.gbrjob", "application/x-gerber-job"},
};

/** @brief A file of each name, made on a host disk: given its type, its
 *         circuit icon, and handed to TikuCircuit unless nothing opens it. */
static void
opened_by_name(tiku_trk_apps_t *apps, tiku_store_t *store)
{
    char status[256], directory[] = "/tmp/tiku-circuit-association-XXXXXX";
    tiku_trk_launch_controller_t controller;
    tiku_backend_t *backend = tiku_backend_posix_open();
    int handed = 0;

    tiku_trk_launch_controller_init(&controller, apps, store, status,
                                    sizeof status, NULL, &opens);
    controller.open_document = document_opened;
    assert(backend != NULL && mkdtemp(directory) != NULL);
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        const char *type = names[i].type;
        const tiku_trk_app_t *handler;
        tiku_model_t model;
        char path[512];
        FILE *f;

        snprintf(path, sizeof path, "%s/%s", directory, names[i].name);
        f = fopen(path, "w");
        assert(f != NULL && !fclose(f));
        assert(!backend->ops->stat(backend, path, &model));
        assert(!strcmp(model.type,
                       type ? type : "application/octet-stream"));
        handler = tiku_trk_launch_controller_handler(&controller, &model);
        if (type != NULL) {
            const char *paths[] = {path};

            assert(handler == &apps->app[0]);
            assert(tiku_trk_launch_controller_run(&controller, handler,
                                                  paths, 1));
            assert(!strcmp(opened, path));
            assert(!strcmp(tiku_model_icon_name(&model), "circuit"));
            handed++;
        } else {
            assert(handler == NULL);
        }
        assert(!unlink(path));
    }
    assert(opens == handed && handed == 19 && !rmdir(directory));
    tiku_backend_close(backend);
}

/** @brief Listed, named in French, opened by name, and overridable. */
int
main(void)
{
    const char *type = "application/x-kicad-schematic";
    tiku_trk_cand_t candidates[TIKU_TRK_APP_MAX];
    tiku_trk_associations_t rows;
    tiku_manifest_t manifest;
    tiku_trk_apps_t apps;
    tiku_store_t *store;
    int found = 0;

    assert(!tiku_manifest_read("apps/circuit/circuit.app", &manifest, NULL,
                               0));
    assert(!strcmp(manifest.types,
                   "application/x-kicad-schematic,application/x-excellon,"
                   "application/x-gerber,application/x-gerber-job,"
                   "application/x-schematic"));
    tiku_trk_launch_clear(&apps);
    assert(tiku_trk_launch_add_document(&apps, manifest.name, "/runner",
                                        "/apps/circuit/TikuCircuit.so",
                                        manifest.types) == 0);
    assert(tiku_trk_launch_relation(&apps.app[0], "text/plain") ==
           TIKU_TRK_REL_NONE);
    assert(tiku_trk_launch_relation(&apps.app[0],
                                    "application/octet-stream") ==
           TIKU_TRK_REL_NONE);
    store = tiku_store_memory_open();
    assert(store != NULL);
    listed(&apps, store);
    french_kinds();
    opened_by_name(&apps, store);

    /* A person's choice of another viewer wins. */
    tiku_trk_launch_add(&apps, "Other viewer", "/bin/true", type);
    tiku_trk_launch_set_preferred(store, type, "Other viewer");
    tiku_trk_associations_load(&rows, &apps, NULL, store);
    for (int i = 0; i < rows.count; i++) {
        found += !strcmp(rows.row[i].chosen, "Other viewer");
    }
    assert(found == 1);
    assert(tiku_trk_launch_candidates(&apps, &type, 1, store, candidates,
                                      TIKU_TRK_APP_MAX) == 2);
    assert(!strcmp(candidates[0].app->name, "Other viewer"));
    tiku_store_free(store);
    puts("PASS: schematic, drill, Gerber, job and .sch routing, their rows, "
         "French kinds, icons, Tracker handoff, user override, .xml "
         "unclaimed");
    return 0;
}
