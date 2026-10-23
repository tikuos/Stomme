/* PDFs route to TikuPDF: its manifest's type, the File Associations row,
 * a person's own preference, and a document handed to the app.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include "tiku_manifest.h"
#include "tiku_trk_associations.h"
#include "../../system/shared/ui/commands/tiku_trk_launch_controller.c"

static int opens;
static char opened[512];

/** @brief Record the document the native controller was handed. */
static int pdf_opened(void *ctx, const tiku_trk_app_t *app, const char *path)
{
    assert(ctx == &opens && !strcmp(app->name, "TikuPDF"));
    opens++;
    snprintf(opened, sizeof opened, "%s", path);
    return 1;
}

/** @brief The manifest's type is offered, routed, and can be overridden. */
int main(void)
{
    tiku_trk_apps_t apps;
    tiku_manifest_t m;
    tiku_trk_cand_t candidates[TIKU_TRK_APP_MAX];
    tiku_trk_associations_t rows;
    const char *pdf = "application/pdf";
    int at, found = 0;

    tiku_trk_launch_clear(&apps);
    assert(tiku_manifest_read("apps/pdf/pdf.app", &m, NULL, 0) == 0);
    assert(!strcmp(m.id, "pdf") && !strcmp(m.name, "TikuPDF"));
    at = tiku_trk_launch_add_document(&apps, m.name, "/runner",
                                      "/apps/pdf/TikuPDF.so", m.types);
    assert(at == 0 && apps.count == 1);
    tiku_store_t *store = tiku_store_memory_open();
    assert(tiku_trk_launch_candidates(&apps, &pdf, 1, store, candidates,
                                      TIKU_TRK_APP_MAX) == 1);
    assert(candidates[0].app == &apps.app[0]);
    assert(tiku_trk_launch_relation(&apps.app[0], "image/png") ==
           TIKU_TRK_REL_NONE);

    /* File Associations lists the type by what it is, not by who wins. */
    tiku_trk_associations_load(&rows, &apps, NULL, store);
    for (int i = 0; i < rows.count; i++)
        if (!strcmp(rows.row[i].type, pdf)) {
            assert(!strcmp(rows.row[i].label, "PDF document (.pdf)"));
            found = 1;
        }
    assert(found);

    /* A person's choice of another reader wins, and can be taken back. */
    tiku_trk_launch_add(&apps, "Another reader", "/bin/true", pdf);
    tiku_trk_launch_set_preferred(store, pdf, "Another reader");
    assert(tiku_trk_launch_candidates(&apps, &pdf, 1, store, candidates,
                                      TIKU_TRK_APP_MAX) == 2);
    assert(!strcmp(candidates[0].app->name, "Another reader"));
    tiku_trk_launch_set_preferred(store, pdf, "");
    tiku_trk_launch_candidates(&apps, &pdf, 1, store, candidates,
                               TIKU_TRK_APP_MAX);
    assert(candidates[0].app == &apps.app[0]);

    char status[256];
    tiku_trk_launch_controller_t controller;
    tiku_trk_launch_controller_init(&controller, &apps, store, status,
                                    sizeof status, NULL, &opens);
    controller.open_document = pdf_opened;
    const char *paths[] = {"/tmp/RP2350 datasheet.PDF"};
    assert(tiku_trk_launch_controller_run(&controller, &apps.app[0], paths,
                                          1));
    assert(opens == 1 && !strcmp(opened, paths[0]));
    tiku_store_free(store);
    puts("PASS: PDFs open in TikuPDF, listed as PDF document, "
         "overridable");
    return 0;
}
