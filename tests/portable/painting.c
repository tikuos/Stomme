/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * painting.c - reference controls, text and damage rendered as RGB words.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdint.h>
#include "tiku_gfx.h"
#include "tiku_font.h"
#include "tiku_ui.h"
#include "tiku_slider_paint.h"
static uint32_t pixels[800*480];
/** @brief Emit the fixed scene for comparison with its desktop reference. */
int main(void) {
 tiku_surface_t surface={0}; surface.px=pixels;surface.w=800;surface.h=480;tiku_clip_reset(&surface);
 tiku_ui_panel(&surface,(tiku_rect_t){0,0,800,480});
 tiku_ui_button(&surface,(tiku_rect_t){20,20,160,40},"Portable panel",0);
 tiku_ui_button(&surface,(tiku_rect_t){200,20,160,40},"Pressed",TIKU_S_PRESSED);
 tiku_slider_t slider;tiku_slider_init(&slider,0,100,25,1);
 tiku_slider_draw(&slider,&surface,(tiku_rect_t){20,90,500,28});
 tiku_ui_checkbox(&surface,(tiku_rect_t){20,150,300,26},"Record audio",TIKU_S_ON);
 tiku_text(&surface,tiku_font_plain(),20,220,"ASCII and Latin-1: caf\xc3\xa9",0x123456);
 tiku_text(&surface,tiku_font_bold(),20,250,"Storage and wireless",0x234567);
 tiku_clip_damage(&surface,&(tiku_rect_t){600,300,40,30});
 tiku_fill(&surface,(tiku_rect_t){590,290,80,60},0xff0000);
 for (size_t i = 0; i < 800*480; ++i) {
  uint32_t value = pixels[i];
  unsigned char bytes[4] = {(unsigned char)value, (unsigned char)(value >> 8),
                            (unsigned char)(value >> 16), (unsigned char)(value >> 24)};
  if (fwrite(bytes, 1, 4, stdout) != 4) return 1;
 }
 return 0;
}
