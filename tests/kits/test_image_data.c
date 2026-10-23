/*
 * TikuOS Translation Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_image_data.c - embedded PNG bytes, dimensions and malformed URI handling.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tiku_image.h"

static int failures;
static const char uri[] = "data:image/png;base64,"
    "iVBORw0KGgoAAAANSUhEUgAAABQAAAAICAYAAAD5nd/tAAAAFklEQVR4nGM4wSXyn5qYYdTAUQNJxwCGyS8wuJRVjAAAAABJRU5ErkJggg==";

/** @brief Record an assertion while retaining subsequent checks. */
static void check(int condition, const char *message)
{
    printf("[%s] data PNG %s\n",condition ? "PASS" : "FAIL",message); failures += !condition;
}

int main(void)
{
    int w = 0, h = 0;
    check(tiku_image_data_png_size(uri,strlen(uri),&w,&h) && w == 20 && h == 8,
          "header inspection returns bounded intrinsic dimensions");
    tiku_image_t image = {0};
    check(tiku_image_data_png(uri,strlen(uri),&image) == TIKU_IMG_OK &&
          image.px && image.w == 20 && image.h == 8 && image.px[0] == 0xffc80a14u,
          "decoded embedded pixels match their authored source");
    tiku_image_free(&image);
    const char *bad[] = {"data:image/svg+xml;base64,AAAA", "data:image/png,AAAA",
        "data:image/png;base64,A===", "data:image/png;base64,AB==",
        "data:image/png;base64,AA=A", "data:image/png;base64,AAAA=",
        "data:image/png;base64,!!!!", "https://example.com/image.png"};
    int valid = 1;
    for (size_t i = 0; i < sizeof bad/sizeof bad[0]; i++) {
        image.w = 99;
        valid &= !tiku_image_data_png_size(bad[i],strlen(bad[i]),&w,&h) &&
            tiku_image_data_png(bad[i],strlen(bad[i]),&image) != TIKU_IMG_OK &&
            image.w == 99 && !image.px;
    }
    check(valid,"refused MIME types, paths, padding and pad bits leave output untouched");
    valid = 1;
    for (size_t n = 0; n < strlen(uri); n++) {
        char *prefix = malloc(n ? n : 1);
        if (!prefix) { valid = 0; break; }
        memcpy(prefix,uri,n);
        image.w = 99;
        tiku_image_err_t status = tiku_image_data_png(prefix,n,&image);
        if (status == TIKU_IMG_OK) tiku_image_free(&image);
        else valid &= image.w == 99 && !image.px;
        free(prefix);
    }
    check(valid,"truncated non-terminated byte ranges remain bounded");
    char *oversized = malloc(TIKU_IMAGE_DATA_URI_MAX+1);
    check(oversized != NULL,"encoded-budget fixture allocated");
    if (oversized) {
        memset(oversized,'A',TIKU_IMAGE_DATA_URI_MAX+1);
        memcpy(oversized,"data:image/png;base64,",22);
        check(!tiku_image_data_png_size(oversized,TIKU_IMAGE_DATA_URI_MAX+1,&w,&h),
              "encoded resource budget rejects oversized input before allocation");
        free(oversized);
    }
    char corrupted[sizeof uri]; memcpy(corrupted,uri,sizeof uri); corrupted[90] = 'A';
    image.w = 99;
    check(tiku_image_data_png(corrupted,strlen(corrupted),&image) != TIKU_IMG_OK &&
          image.w == 99 && !image.px,"malformed PNG payloads retain caller output");
    printf("Data PNG: %d failure(s)\n",failures);return failures != 0;
}
