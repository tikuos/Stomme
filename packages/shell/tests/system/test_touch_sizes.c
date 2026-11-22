/* A touch panel's sizes: the face the desktop starts with, and a folder
 * window's count line and scroll bar, which grow with the face.
 * SPDX-License-Identifier: Apache-2.0 */
#define TIKU_SHELL_TOUCH 1
#include <assert.h>
#include <stdio.h>
#include "../../system/deskbar/tiku_trk_deskbar_prefs.c"
#include "tiku_trk_folder_window.h"

int main(void)
{
    tiku_trk_deskbar_settings_t settings;
    int h;

    tiku_trk_deskbar_settings_default(&settings);
    assert(settings.font_size == 16);
    assert(tiku_font_set_size(16) == 16);
    h = tiku_font_plain()->height;
    assert(h + 3 > 16);
    assert(tiku_trk_folder_count_h() == h + 3);
    assert(tiku_trk_folder_scrollbar_w() == h + 3);
    /* A smaller face never takes them below the mouse sizes. */
    assert(tiku_font_set_size(10) == 10);
    assert(tiku_trk_folder_count_h() >= 16);
    assert(tiku_trk_folder_scrollbar_w() >= 15);
    puts("PASS: a touch panel starts at 16 px, and a folder's count line and "
         "scroll bar follow the face");
    return 0;
}
