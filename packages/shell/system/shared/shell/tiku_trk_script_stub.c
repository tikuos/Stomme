/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_script_stub.c - the shipping build's answer to -script.
 *
 * The scripted driver and its presentation adapter are test infrastructure
 * and are not shipped: the product TikuDesktop (build/runtime/system) links
 * this file in their place, with the same entry points and none of the
 * engine.  Every scripted path is reached through a pointer that only a
 * successful open returns, so the refused open here removes all of it.  The
 * development flavour (build/dev) links the real files.  The refusal is
 * printed, so a harness that ran the wrong binary is told so.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_trk_script.h"
#include "tiku_trk_script_adapter.h"
#include <stdio.h>
#include <string.h>

tiku_trk_script_t *
tiku_trk_script_open(const char *path)
{
    fprintf(stderr,
            "tracker: this build has no script engine (-script %s "
            TIKU_SHELL_SCRIPT_REFUSAL_MESSAGE
            "which make all builds; see tests/integration/shell/run.sh.\n",
            (path != NULL) ? path : "");
    return NULL;
}

void
tiku_trk_script_free(tiku_trk_script_t *s)
{
    (void)s;
}

int
tiku_trk_script_pump(tiku_trk_script_t *s,
                     const tiku_trk_script_state_t *st,
                     tiku_trk_script_step_t *out)
{
    (void)s;
    (void)st;
    if (out != NULL) {
        memset(out, 0, sizeof *out);
    }
    return 0;                   /* nothing to run, ever */
}

int
tiku_trk_script_waiting(const tiku_trk_script_t *s)
{
    (void)s;
    return 0;                   /* nothing waits, because nothing runs */
}

int
tiku_trk_script_failures(const tiku_trk_script_t *s)
{
    (void)s;
    return 0;
}

int
tiku_trk_script_report(const tiku_trk_script_t *s, const char *who)
{
    (void)s;
    (void)who;
    return 0;
}

void
tiku_trk_script_adapter_init(tiku_trk_script_adapter_t *adapter,
    tiku_workspace_t *workspace, tiku_surface_t *surface,
    tiku_trk_folder_lifecycle_t *folders,
    tiku_trk_desktop_view_t *desktop_view, tiku_trk_deskbar_t *deskbar,
    tiku_alert_t *alert, tiku_trk_find_window_t *find_window,
    tiku_trk_panel_window_t *panel_window,
    tiku_trk_folder_edit_t *folder_edit, tiku_menu_t **popup,
    tiku_menu_t **popup_sub, tiku_rect_t *popup_frame,
    tiku_rect_t *popup_sub_frame, char *status, char *count_text,
    int row_h)
{
    (void)workspace;
    (void)surface;
    (void)folders;
    (void)desktop_view;
    (void)deskbar;
    (void)alert;
    (void)find_window;
    (void)panel_window;
    (void)folder_edit;
    (void)popup;
    (void)popup_sub;
    (void)popup_frame;
    (void)popup_sub_frame;
    (void)status;
    (void)count_text;
    (void)row_h;
    /* Zeroed: the shell stores its redraw hook in it right after the init
     * and reads members back on the scripted path. */
    if (adapter != NULL) {
        memset(adapter, 0, sizeof *adapter);
    }
}

tiku_trk_script_adapter_result_t
tiku_trk_script_adapter_next(tiku_trk_script_adapter_t *adapter,
                             tiku_trk_script_t *script,
                             tiku_event_t *event)
{
    (void)adapter;
    (void)script;
    (void)event;
    return TIKU_TRK_SCRIPT_ADAPTER_STOP;
}
