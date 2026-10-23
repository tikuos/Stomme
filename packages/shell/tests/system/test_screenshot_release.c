/* A screenshot waits for a frame without the pressed camera button. */
#define desktop_shot_desk capture
#include "../../system/desktop/tiku_trk_desktop_draw.c"
#undef desktop_shot_desk
#include <assert.h>
static int captured;
void capture(desktop_t *app) { (void)app; captured++; }
int main(void)
{
    desktop_t *app = calloc(1, sizeof *app); assert(app);
    app->shot_pending = app->shot_pressed = 1;
    desktop_screenshot_tick(app);
    int failed = captured || !app->shot_pending || app->shot_pressed;
    desktop_screenshot_tick(app);
    failed |= captured != 1 || app->shot_pending;
    free(app);
    puts(failed ? "FAIL D-201" : "PASS D-201"); return failed;
}
