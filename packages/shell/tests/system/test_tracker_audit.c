/* Tracker scheduler, column, selection and command regressions. */
#include "../../system/shared/ui/poseview/tiku_trk_poseview_private.h"
#include "tiku_trk_cwmenu.h"
#include "tiku_trk_alert.h"

static int fails;
static void check(int ok, const char *id)
{
    if (!ok) { printf("FAIL %s\n", id); fails++; }
}
static int nothing(void *ctx) { (void)ctx; return 1; }
static void deadlines(void)
{
    tiku_trk_loop_t *loop = tiku_trk_loop_new();
    tiku_trk_run_accumulating(loop, "a", nothing, NULL, NULL, NULL, NULL,
                              10000000, 0, 0);
    tiku_trk_run_later(loop, nothing, NULL, NULL, 20000000);
    tiku_trk_run_accumulating(loop, "a", nothing, NULL, NULL, NULL, NULL,
                              40000000, 0, 0);
    check(tiku_trk_loop_next_due(loop) <= 20000000, "D-030");
    tiku_trk_loop_free(loop);
}
static void columns(void)
{
    tiku_trk_poseview_t v = {0};
    for (int i = 0; i < 4; i++) {
        v.columns[i].field = i;
        v.columns[i].width = 80;
    }
    v.column_count = 4;
    check(tiku_trk_poseview_column_after(&v, 0, 2) &&
          v.columns[0].field == 1 && v.columns[1].field == 2 &&
          v.columns[2].field == 0 && v.columns[3].field == 3, "D-035");
    tiku_trk_poseview_copy_layout(&v);
    int serial = pv_g_layout_clip.serial;
    tiku_trk_poseview_copy_layout(&v);
    check(pv_g_layout_clip.serial == serial + 1, "D-037");
}
static void selection(void)
{
    tiku_trk_poseview_t v = {0};
    tiku_trk_pose_t poses[2] = {0};
    unsigned char was[2] = {0};
    v.poses = poses; v.count = 2; v.mode = TIKU_TRK_MODE_LIST;
    v.band = 1; v.band_was = was; v.single_only = 1;
    v.view_w = 300;
    poses[0].index = 0; poses[1].index = 1;
    int hits = tiku_trk_poseview_select_rect(&v, -100, -100, 10000, 10000, 0);
    check(hits == 1 && tiku_trk_poseview_selection_count(&v) == 1, "D-041");
    tiku_trk_poseview_select_rect(&v, 10000, 10000, 10001, 10001, 0);
    check(tiku_trk_poseview_selection_count(&v) == 0, "D-041 shrink");
}
static void progress(void)
{
    tiku_trk_progress_win_t p = {0};
    int x, y, w, h, row = -1;
    p.open = 1; p.row[2].used = 1;
    tiku_trk_progress_button_rect(0, TIKU_TRK_PROGRESS_HIT_PAUSE,
                                  400, &x, &y, &w, &h);
    check(tiku_trk_progress_hit(&p, 400, x, y, &row) ==
          TIKU_TRK_PROGRESS_HIT_PAUSE && row == 2, "D-039");
}
int main(void)
{
    deadlines(); columns(); selection(); progress();
    check(!strcmp(tiku_trk_sc_label(TIKU_TRK_CMD_EDIT_TEMPLATES, 0),
                  "Edit templates\342\200\246"), "D-033");
    check(TIKU_TRK_CMD_APP_PICK != TIKU_TRK_CMD_RECALC_SIZE, "D-036");
    puts(fails ? "FAIL: tracker audit" : "PASS: tracker audit");
    return fails != 0;
}
