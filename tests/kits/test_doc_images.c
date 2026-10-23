/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_doc_images.c - document image dimensions, aspect ratio and missing resources.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include "tiku_doc.h"

static tiku_doc_t doc;
static int failures, width = 200, height = 100, allowed = 1;

/** @brief Supply deterministic intrinsic dimensions without files or pixels. */
static int size(void *ctx, const char *src, int *w, int *h)
{
    (void)ctx;(void)src; *w = width; *h = height; return allowed;
}

/** @brief Record an assertion while retaining subsequent checks. */
static void check(int condition, const char *message)
{
    printf("[%s] images %s\n",condition ? "PASS" : "FAIL",message); failures += !condition;
}

/** @brief Parse a document and resolve images at the requested width. */
static void read_image(const char *source, int wide)
{
    tiku_doc_read(&doc,source); tiku_doc_layout(&doc,wide);
}

int main(void)
{
    tiku_doc_init(&doc); tiku_doc_images(&doc,size,NULL);
    read_image("<img src='local' width='80'>",300);
    check(doc.nimage == 1 && doc.image[0].w == 80 && doc.image[0].h == 40,
          "HTML width scales intrinsic aspect ratio");
    read_image("<img src='local' height='30'>",300);
    check(doc.image[0].w == 60 && doc.image[0].h == 30,"HTML height scales intrinsic aspect ratio");
    read_image("<img src='local' width='80' height='80'>",300);
    check(doc.image[0].w == 80 && doc.image[0].h == 40,"two dimensions bound an image while preserving aspect ratio");
    read_image("<img src='local' width='80' style='width:120px;height:40px'>",300);
    check(doc.image[0].w == 80 && doc.image[0].h == 40,"CSS dimensions override HTML dimensions as an aspect-preserving box");
    read_image("<img src='local' style='width:50%;max-width:70px'>",300);
    check(doc.image[0].w == 70 && doc.image[0].h == 35,"percent and max-width image dimensions share the containing width");
    read_image("<div style='width:90px;padding:10px'><img src='local'></div>",300);
    check(doc.image[0].w == 90 && doc.image[0].h == 45 && doc.line[0].x == 10,
          "intrinsic images fit their container content width");
    tiku_doc_layout(&doc,60);
    check(doc.image[0].w <= 40 && tiku_doc_check(&doc),"images reflow within a narrower padded container");
    allowed = 0;
    read_image("<figure><img src='refused' alt='Alternate text'><figcaption>Caption text</figcaption></figure>",300);
    check(doc.image[0].w == 0 && doc.npiece && tiku_doc_check(&doc),"refused resources retain alternate text and caption flow");
    int figure = -1, caption = -1;
    for (int i = 0; i < doc.nblock; i++) {
        if (doc.block[i].flags & TIKU_DOC_BLOCK_FIGURE) figure = i;
        if (doc.block[i].kind == TIKU_DOC_BLOCK_CAPTION) caption = i;
    }
    check(figure >= 0 && caption > figure && doc.block[caption].parent == figure &&
          doc.block[doc.image[0].block].parent == figure && doc.block[figure].end > caption,
          "a figure retains its image and caption in one bounded container range");
    allowed = 1; width = INT32_MAX;
    read_image("<img src='oversized' alt='Oversized alternate'>",300);
    check(doc.image[0].w == 0 && tiku_doc_check(&doc),"invalid callback dimensions use alternate text without coordinate overflow");
    printf("Images: %d failure(s)\n",failures);return failures != 0;
}
