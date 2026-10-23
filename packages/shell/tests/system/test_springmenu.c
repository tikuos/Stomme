#include <stdio.h>
#include "tiku_trk_springmenu.h"
int main(void) {
    tiku_trk_springmenu_t m; tiku_trk_springmenu_reset(&m);
    tiku_trk_springmenu_enter(&m, 10, 10, 100, 40, 100);
    if (tiku_trk_springmenu_pulse(&m, 400000) || !tiku_trk_springmenu_pulse(&m, 500100)) return 1;
    if (!tiku_trk_springmenu_inside(&m, 0, 0) || tiku_trk_springmenu_inside(&m, 200, 200)) return 1;
    if (!tiku_trk_springmenu_item_enabled(1, 0, 0) || tiku_trk_springmenu_item_enabled(0, 0, 0) ||
        tiku_trk_springmenu_item_enabled(1, 1, 1)) return 1;
    tiku_trk_springmenu_leave(&m, 200, 200);
    if (m.armed) return 1;
    puts("[PASS] spring-loaded delay, margin and type gating"); return 0;
}
