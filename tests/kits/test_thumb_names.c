/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_thumb_names.c - thumbnail cache files are named by eight hex digits.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../../kits/translation/tiku_thumb.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    static const char *paths[] = {
        "/home/a.png", "/home/user/Pictures/holiday-2026-11-22.jpg",
        "/Card/radio/station.png", "x"
    };
    char out[512];

    assert(setenv("HOME", "/h", 1) == 0);
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
        assert(blob_path(paths[i], out, sizeof out) == 0);
        assert(!strncmp(out, "/h/.cache/tiku-tracker/", 23));
        /* Eight hex digits and ".raw", whatever the width of a long. */
        assert(strlen(out + 23) == 12 && !strcmp(out + 31, ".raw"));
    }
    puts("PASS: thumbnail cache names are eight hex digits");
    return 0;
}
