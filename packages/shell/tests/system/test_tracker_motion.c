#include "tiku_shell_product.h"
/* TikuTracker's outline over an opened pose: drawn over the list while it
 * runs, off with Reduce motion or without a window, and gone at its end.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include "../../system/tracker/tiku_trk_app_private.h"

/** @brief How many pixels of @p at on @p s differ from @p before. */
static int changed_pixels(const tiku_surface_t *s,
                          const tiku_surface_t *before, tiku_rect_t at)
{
    int n = 0;
    for (int y = 0; y < at.h; y++)
        for (int x = 0; x < at.w; x++)
            n += tiku_peek(s, at.x + x, at.y + y) !=
                 tiku_peek(before, x, y);
    return n;
}

/** @brief Whether anything outside @p inside differs from @p ground. */
static int outside_changed(const tiku_surface_t *s, tiku_rect_t inside,
                           tiku_rgb_t ground)
{
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++)
            if ((x < inside.x || x >= inside.x + inside.w ||
                 y < inside.y || y >= inside.y + inside.h) &&
                tiku_peek(s, x, y) != ground)
                return 1;
    return 0;
}

int main(void)
{
    app_t *a = calloc(1, sizeof *a); assert(a);
    char dir[] = "/tmp/tiku-tracker-motion-XXXXXX";
    tiku_rgb_t ground = TIKU_RGB(200, 210, 220);
    assert(mkdtemp(dir));
    a->surf = tiku_surface_new(WIN_W, WIN_H, ground); assert(a->surf);
    a->backend = tiku_backend_posix_open(); assert(a->backend);
    a->view = tiku_trk_poseview_new(a->backend, dir); assert(a->view);
    a->show_nav = 1;
    /* A window, so the browser has a clock to run the outline by. */
    a->host = (tiku_host_t *)a;
    tiku_rect_t list = browser_list_rect(a);
    tiku_rect_t icon = {list.x + 40, list.y + 40, 32, 32};
    tiku_rect_t reach = tiku_motion_outline_bounds(icon);
    tiku_surface_t *before = tiku_grab(a->surf, reach); assert(before);

    /* The open itself is not held up: the outline only starts. */
    browser_open_animation(a, icon);
    assert(a->activation_motion.active && a->dirty);
    a->dirty = 0;
    browser_draw_activation(a);
    assert(changed_pixels(a->surf, before, reach) > 0 && a->dirty);
    /* Its end draws nothing and asks for no further frame. */
    tiku_fill(a->surf, (tiku_rect_t){0, 0, WIN_W, WIN_H}, ground);
    a->activation_motion.since -= TIKU_MOTION_ICON_US;
    a->dirty = 0;
    browser_draw_activation(a);
    assert(!a->activation_motion.active && !a->dirty);
    assert(changed_pixels(a->surf, before, reach) == 0);

    /* A pose at the list's edge: the outline keeps inside the list. */
    browser_open_animation(a, (tiku_rect_t){list.x - 2, list.y - 2, 32, 32});
    browser_draw_activation(a);
    assert(a->activation_motion.active);
    assert(!outside_changed(a->surf, list, ground));
    tiku_fill(a->surf, (tiku_rect_t){0, 0, WIN_W, WIN_H}, ground);

    /* Reduce motion turned on while it runs retires it on the next frame;
     * with it on, or with no window, none starts. */
    a->reduce_motion = 1;
    a->dirty = 0;
    browser_draw_activation(a);
    assert(!a->activation_motion.active && !a->dirty);
    assert(!outside_changed(a->surf, (tiku_rect_t){0, 0, 0, 0}, ground));
    browser_open_animation(a, icon);
    assert(!a->activation_motion.active);
    a->reduce_motion = 0;
    a->host = NULL;
    browser_open_animation(a, icon);
    assert(!a->activation_motion.active);

    tiku_surface_free(before);
    tiku_trk_poseview_free(a->view);
    tiku_backend_close(a->backend);
    tiku_surface_free(a->surf);
    free(a);
    assert(!rmdir(dir));
    puts("PASS: Tracker's opened-pose outline, its clip, its end and "
         "Reduce motion");
    return 0;
}
