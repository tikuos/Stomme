/*
 * TikuDesktop native UI tests.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * ui_font.h - explicit proportional face selection for field geometry.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TEST_UI_FONT_H_
#define TIKU_TEST_UI_FONT_H_
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/** @brief Locate a UI test face using the existing native-font override. */
static const char *ui_test_font(void)
{
    const char *override = getenv("TIKU_TEST_FONT");
    static const char *const paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf"};
    if (override && *override)
        return override;
    for (size_t i = 0; i < sizeof paths / sizeof *paths; i++)
        if (!access(paths[i], R_OK))
            return paths[i];
    fputs("Set TIKU_TEST_FONT to a TrueType UI face.\n", stderr);
    exit(1);
}
#endif
