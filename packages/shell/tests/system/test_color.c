#include <stdio.h>
#include "tiku_trk_color.h"
int main(void) {
    tiku_trk_color_t c = {1,2,3,0}, out = {0};
    if (tiku_trk_color_parse(&c, sizeof c, &out) != 0 || out.r != 1 || out.a != 255) return 1;
    tiku_trk_color_set(&out, &(tiku_trk_color_t){9,8,7,255});
    if (out.r != 9 || out.g != 8 || out.b != 7) return 1;
    puts("[PASS] desktop colour payload parsing and forwarding state"); return 0;
}
