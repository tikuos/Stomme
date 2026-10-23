/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_launcher.c - enter whichever shell this build linked.
 *
 * This file holds the only main().  mk/system.mk links it into
 * build/dev/TikuTracker and into both TikuDesktop flavours (build/dev and
 * build/runtime/system), so tiku_trk_application is tiku_trk_app.c's
 * descriptor in TikuTracker and tiku_trk_desktop.c's in TikuDesktop.  Both
 * enter through the Application kit's registry (kits/application/
 * tiku_app.h), which looks a linked-in shell up by id as it would a loaded
 * application.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_report.h"
#include "tiku_trk_application.h"
#include <stdio.h>

/** @brief Register the descriptor this binary linked, tiku_trk_application,
 *         and run it. */
int
main(int argc, char **argv)
{
    /* Installed first, so a fault from here on leaves a report saying
     * what the process was doing. */
    tiku_report_install(TIKU_SHELL_DESKTOP_NAME, NULL);
    tiku_app_registry_t registry;
    const tiku_app_descriptor_t *application;

    tiku_app_registry_init(&registry);
    if (tiku_app_registry_add(&registry, &tiku_trk_application) < 0) {
        fprintf(stderr, "tracker: application registration failed\n");
        return 1;
    }
    application = tiku_app_registry_find(&registry,
                                               tiku_trk_application.id);
    if (application == NULL) {
        fprintf(stderr, "tracker: registered application was not found\n");
        return 1;
    }
    return tiku_app_run(application, argc, argv);
}
