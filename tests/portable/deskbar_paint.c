/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * deskbar_paint.c - native Deskbar pixel fixture over themes and damage clips.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_deskbar_paint.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static tiku_rgb_t pixels[64*64];
/** @brief Compare native Deskbar pixels across themes, sizes and damage clips. */
int main(void){uint64_t hash=1469598103934665603ULL;unsigned cases=0;
 tiku_surface_t s={.w=64,.h=64,.scale=1,.px=pixels};
 for(int theme=0;theme<3;++theme){tiku_theme_set(theme==1?tiku_theme_dark():theme==2?tiku_theme_mono():NULL);
 for(int h=1;h<=41;h+=4)for(int width=1;width<=97;width+=8)for(int state=0;state<4;++state)for(int damage=0;damage<2;++damage){
 memset(pixels,0x71,sizeof pixels);tiku_clip_damage(&s,damage?&(tiku_rect_t){9,9,32,31}:NULL);
 tiku_deskbar_row_paint(&s,(tiku_rect_t){3,4,width,h},state&1);
 tiku_deskbar_window_icon(&s,12,12,state&1,state>>1);tiku_deskbar_expander(&s,35,18,state&1);
 for(unsigned i=0;i<64*64;++i){hash^=pixels[i];hash*=1099511628211ULL;}++cases;
 }}printf("%u %016llx\n",cases,(unsigned long long)hash);return cases==3432&&hash==UINT64_C(0xcb7f0cf027c90cc3)?0:1;}
