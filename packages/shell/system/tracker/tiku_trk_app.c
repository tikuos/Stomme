/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_app.c - the single-window browser: a Tracker list view over a
 * backend, run as its own application.
 *
 * One host window, drawn entirely by this shell and wired to the pose
 * engine: the title strip sorts, rows select by the engine's rules, the
 * keyboard navigates, and a device's values update live because the loop
 * pumps the backend between events.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_trk_app_private.h"
#include "tiku_work.h"

/** @brief Paint a menu row's icon, dimmed over the panel colour when the
 *         row is disabled.  @return what the icon painter returns. */
int
browser_paint_menu_icon(tiku_surface_t *s, const char *icon, int x, int y,
                int size, int dimmed)
{
    return dimmed
        ? tiku_icon_paint_dim(s, icon, x, y, size, 0.55f,
                                  TIKU_C_PANEL)
        : tiku_icon_paint(s, icon, x, y, size);
}

/*---------------------------------------------------------------------------*/
/* THE RUN                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief The browser shell, start to finish: the arguments, the boot, the
 *        loop of turns until the window closes, and the teardown.
 * @return the exit code.
 */
static int
tracker_run(int argc, char **argv)
{
    app_t a;
    browser_options_t opt;
    browser_loop_t lp;
    int code;

    memset(&a, 0, sizeof a);
    memset(&opt, 0, sizeof opt);
    opt.baud = 115200;
    memset(&lp, 0, sizeof lp);
    (void)tiku_strings_init(NULL, NULL);
    tiku_trk_cwmenu_init(&a.menus);
    a.bar_open = -1;
    a.show_nav = 1;
    /* -1: no row to select after an operation.  Row 0 is a real row
     * (FS-050). */
    a.select_next = -1;
    code = browser_parse_args(argc, argv, &opt);
    if (code >= 0) {
        return code;
    }

    code = browser_boot(&a, &opt, &lp);
    if (code >= 0) {
        return code;
    }

    while (a.running) {
        (void)tiku_work_collect();
        browser_sync_navigator_preference(&a, 0);
        /* Events first, then the device: the loop pumps both without
         * blocking on either, so the window answers while a slow link is
         * read. */
        browser_turn_begin(&a, &lp);
        browser_turn_end(&a, &opt, &lp);
    }

    return browser_teardown(&a, &lp);
}

const tiku_app_descriptor_t tiku_trk_application = {
    .id = "org.tikuos.tracker.browser",
    .name = TIKU_SHELL_TRACKER_NAME,
    .run = tracker_run
};
