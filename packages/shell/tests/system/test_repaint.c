#include <stdio.h>
#include "tiku_trk_repaint.h"

#define VIEW ((tiku_trk_repaint_rect_t){0, 0, 400, 200})

int main(void) {
    tiku_trk_repaint_t r; tiku_trk_repaint_rect_t out[4];
    int y = -1, d;

    tiku_trk_repaint_init(&r);
    /* A row appearing inside the view moves the rows under it and paints
     * one row, rather than repainting from the insertion down. */
    tiku_trk_repaint_insert(&r, VIEW, 20, 40);
    d = tiku_trk_repaint_take_slide(&r, &y);
    if (d != 20 || y != 40) {
        printf("[FAIL] repaint  insert slid %d at y %d, wanted 20 at 40\n",
               d, y);
        return 1;
    }
    if (tiku_trk_repaint_take(&r, out, 4) != 1 || out[0].y != 40 ||
        out[0].h != 20) {
        printf("[FAIL] repaint  insert painted y %d h %d, wanted one row "
               "at 40\n", out[0].y, out[0].h);
        return 1;
    }
    /* Above the view there is nothing to move: it is a scroll. */
    tiku_trk_repaint_insert(&r, VIEW, 20, -20);
    if (r.scroll_delta != 20 || tiku_trk_repaint_take_slide(&r, &y) != 0) {
        printf("[FAIL] repaint  insert above the view slid instead of "
               "scrolling (delta %d)\n", r.scroll_delta);
        return 1;
    }
    r.scroll_delta = 0;
    /* A row going leaves the rows under it moved up and the bottom row's
     * worth of pixels stale. */
    tiku_trk_repaint_remove(&r, VIEW, 20, 40);
    d = tiku_trk_repaint_take_slide(&r, &y);
    if (d != -20 || y != 60) {
        printf("[FAIL] repaint  remove slid %d at y %d, wanted -20 at 60\n",
               d, y);
        return 1;
    }
    if (tiku_trk_repaint_take(&r, out, 4) != 1 || out[0].y != 180) {
        printf("[FAIL] repaint  remove painted y %d, wanted the bottom row "
               "at 180\n", out[0].y);
        return 1;
    }
    /* A rectangle already recorded names pixels the blit moves, so it moves
     * with them -- otherwise the row that changed is blitted stale and the
     * one that did not is painted. */
    tiku_trk_repaint_add(&r, (tiku_trk_repaint_rect_t){0, 100, 400, 20});
    tiku_trk_repaint_insert(&r, VIEW, 20, 40);
    (void)tiku_trk_repaint_take_slide(&r, &y);
    if (tiku_trk_repaint_take(&r, out, 4) != 2 || out[0].y != 120) {
        printf("[FAIL] repaint  the recorded row stayed at y %d while the "
               "pixels under it moved to 120\n", out[0].y);
        return 1;
    }
    /* Two slides in one batch are not one blit: the batch gives up. */
    tiku_trk_repaint_init(&r);
    tiku_trk_repaint_insert(&r, VIEW, 20, 40);
    tiku_trk_repaint_insert(&r, VIEW, 20, 80);
    if (tiku_trk_repaint_take_slide(&r, &y) != 0) {
        printf("[FAIL] repaint  two inserts in a batch still claimed a "
               "single blit\n");
        return 1;
    }
    if (tiku_trk_repaint_take(&r, out, 4) != 1 || out[0].h != 200) {
        printf("[FAIL] repaint  two inserts painted h %d, wanted the whole "
               "view\n", out[0].h);
        return 1;
    }
    /* A band that GREW reports the strips it grew over, not the whole
     * rectangle: the union would repaint every row the band covers, which
     * is the thing the difference exists to avoid (PVL-070). */
    tiku_trk_repaint_band(&r, (tiku_trk_repaint_rect_t){0,20,100,40},
                             (tiku_trk_repaint_rect_t){0,0,100,60});
    if (tiku_trk_repaint_take(&r, out, 4) != 1 ||
        out[0].y != 0 || out[0].h != 20) {
        printf("[FAIL] repaint  a grown band reported y %d h %d, wanted the "
               "20-pixel strip it grew over\n", out[0].y, out[0].h);
        return 1;
    }
    /* Unmoved is unchanged: nothing to paint again. */
    tiku_trk_repaint_band(&r, (tiku_trk_repaint_rect_t){0,0,100,60},
                             (tiku_trk_repaint_rect_t){0,0,100,60});
    if (tiku_trk_repaint_take(&r, out, 4) != 0) {
        printf("[FAIL] repaint  a band that did not move asked for paint\n");
        return 1;
    }
    puts("[PASS] repaint  a row in or out slides the rest, and one paints");
    return 0;
}
