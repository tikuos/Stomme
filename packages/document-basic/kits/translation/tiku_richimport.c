/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richimport.c - checked, aggregate-bounded document import staging.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richimport_private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
tiku_rimp_active(tiku_rimp_t *r)
{
    if (r->status == TIKU_RICHIO_OK && r->options.cancel &&
        r->options.cancel(r->options.ctx)) r->status = TIKU_RICHIO_CANCELLED;
    return r->status == TIKU_RICHIO_OK;
}

void *
tiku_rimp_allocate(tiku_rimp_t *r, size_t bytes)
{
    void *p;

    if (!tiku_rimp_active(r)) return NULL;
    if (bytes > r->options.memory_bytes - r->used) {
        r->status = TIKU_RICHIO_LIMIT;
        return NULL;
    }
    p = calloc(1, bytes);
    if (!p) r->status = TIKU_RICHIO_NO_MEMORY;
    else r->used += bytes;
    return p;
}

/** @brief Grow an array without exposing relocated element pointers. */
static int
grow(tiku_rimp_t *r, void **array, size_t *capacity, size_t count,
     size_t item, size_t maximum)
{
    size_t next;
    void *p;

    if (count > maximum) { r->status = TIKU_RICHIO_LIMIT; return 0; }
    if (count <= *capacity) return r->status == TIKU_RICHIO_OK;
    next = *capacity ? *capacity : 16;
    while (next < count && next <= maximum / 2) next *= 2;
    if (next < count || next > maximum) next = maximum;
    if (next > SIZE_MAX / item) { r->status = TIKU_RICHIO_LIMIT; return 0; }
    p = tiku_rimp_allocate(r, next * item);
    if (!p) return 0;
    if (*array) memcpy(p, *array, *capacity * item);
    free(*array);
    r->used -= *capacity * item;
    *array = p;
    *capacity = next;
    return 1;
}

char *
tiku_rimp_string(tiku_rimp_t *r, const char *text, size_t length)
{
    tiku_rimp_owned_t *p;

    if (length > SIZE_MAX - sizeof *p - 1) {
        r->status = TIKU_RICHIO_LIMIT;
        return NULL;
    }
    p = tiku_rimp_allocate(r, sizeof *p + length + 1);
    if (!p) return NULL;
    p->next = r->owned;
    r->owned = p;
    if (length) memcpy(p->data, text, length);
    return p->data;
}

size_t
tiku_rimp_block(tiku_rimp_t *r, tiku_rich_block_kind_t kind, size_t parent)
{
    if (!grow(r, (void **)&r->blocks, &r->block_cap, r->block_count + 1,
              sizeof *r->blocks, 65536)) return 0;
    r->blocks[r->block_count++] = (tiku_rich_block_source_t){
        .kind = kind, .parent = parent,
        .paragraph = kind == TIKU_RICH_BLOCK_PARAGRAPH ? r->paragraph_count : 0};
    return r->block_count;
}

void
tiku_rimp_paragraph(tiku_rimp_t *r,
                     const tiku_rich_paragraph_source_t *properties)
{
    size_t maximum = r->options.document.paragraphs ?
                     r->options.document.paragraphs : 250000;

    if (!grow(r, (void **)&r->paragraphs, &r->paragraph_cap,
              r->paragraph_count + 1, sizeof *r->paragraphs, maximum) ||
        !tiku_rimp_block(r, TIKU_RICH_BLOCK_PARAGRAPH, r->parent)) return;
    r->paragraphs[r->paragraph_count] = (tiku_rimp_para_t){
        .value = *properties, .text_at = r->text_count, .run_at = r->run_count};
    r->current = ++r->paragraph_count;
    if (properties->style) r->headings = 1;
}

/** @brief Append text or an indivisible image with canonical run boundaries. */
static void
append(tiku_rimp_t *r, const char *text, size_t length,
       const tiku_rich_chars_t *chars, const char *link,
       const tiku_rich_image_t *image)
{
    size_t maximum = r->options.document.text_bytes ?
                     r->options.document.text_bytes : (size_t)64 << 20;
    tiku_rimp_para_t *p;
    tiku_rich_run_source_t run = {.length = length, .chars = *chars,
                                 .link = link, .image = image};

    if (!length || !tiku_rimp_active(r)) return;
    if (!r->current) { r->status = TIKU_RICHIO_DAMAGED; return; }
    if (length > maximum || r->text_count > maximum - length) {
        r->status = TIKU_RICHIO_LIMIT;
        return;
    }
    if (!grow(r, (void **)&r->text, &r->text_cap, r->text_count + length,
              1, maximum)) return;
    p = &r->paragraphs[r->current - 1];
    if (p->value.run_count) {
        tiku_rich_run_source_t *last = &r->runs[r->run_count - 1];

        if (!image && !last->image && !memcmp(&last->chars, chars, sizeof *chars) &&
            ((!last->link && !link) || (last->link && link &&
                                       !strcmp(last->link, link)))) {
            last->length += length;
            goto copy;
        }
    }
    maximum = r->options.document.runs ? r->options.document.runs : 1000000;
    if (!grow(r, (void **)&r->runs, &r->run_cap, r->run_count + 1,
              sizeof *r->runs, maximum)) return;
    r->runs[r->run_count++] = run;
    p->value.run_count++;
copy:
    memcpy(r->text + r->text_count, text, length);
    r->text_count += length;
    p->value.length += length;
}

void
tiku_rimp_text(tiku_rimp_t *r, const char *text, size_t length,
               const tiku_rich_chars_t *chars, const char *link)
{
    append(r, text, length, chars, link, NULL);
}

int
tiku_rimp_picture(tiku_rimp_t *r, const tiku_rich_image_t *image,
                  const tiku_rich_chars_t *chars, const char *link)
{
    size_t bytes = (size_t)image->pixel_width * image->pixel_height * 4;
    tiku_rimp_image_t *node;

    if (bytes > r->options.memory_bytes - r->used) {
        r->status = TIKU_RICHIO_LIMIT;
        return 0;
    }
    r->used += bytes;
    node = tiku_rimp_allocate(r, sizeof *node);
    if (!node) { r->used -= bytes; return 0; }
    node->value = *image;
    append(r, "\xef\xbf\xbc", 3, chars, link, &node->value);
    if (r->status != TIKU_RICHIO_OK) {
        free(node);
        r->used -= bytes + sizeof *node;
        return 0;
    }
    node->next = r->images;
    r->images = node;
    return 1;
}

tiku_richio_status_t
tiku_rimp_finish(tiku_rimp_t *r, tiku_richdoc_t **out)
{
    tiku_rich_paragraph_source_t empty = {0}, *paragraphs;
    tiku_rich_style_source_t styles[6] = {0};
    tiku_rich_source_t source = {0};
    tiku_richdoc_t *doc = NULL;

    if (!r->block_count) tiku_rimp_paragraph(r, &empty);
    if (!tiku_rimp_active(r)) return r->status;
    paragraphs = tiku_rimp_allocate(r, r->paragraph_count * sizeof *paragraphs);
    if (!paragraphs) return r->status;
    for (size_t i = 0; i < r->paragraph_count; i++) {
        paragraphs[i] = r->paragraphs[i].value;
        paragraphs[i].text = r->text ? r->text + r->paragraphs[i].text_at : "";
        paragraphs[i].runs = paragraphs[i].run_count ?
                            r->runs + r->paragraphs[i].run_at : NULL;
    }
    for (size_t i = 0; i < 6; i++) {
        snprintf(styles[i].name, sizeof styles[i].name, "Heading %zu", i + 1);
        styles[i].chars = (tiku_rich_chars_t){
            .present = TIKU_RICH_SIZE | TIKU_RICH_BOLD,
            .size = (float)(24 - i * 2), .bold = 1};
        styles[i].paragraph = (tiku_rich_para_t){
            .present = TIKU_RICH_KEEP | TIKU_RICH_SPACING | TIKU_RICH_OUTLINE,
            .keep_next = 1, .keep_lines = 1, .before = 12, .after = 6,
            .outline = (unsigned char)(i + 1)};
    }
    source.paragraphs = paragraphs;
    source.paragraph_count = r->paragraph_count;
    source.blocks = r->blocks;
    source.block_count = r->block_count;
    source.styles = r->headings ? styles : NULL;
    source.style_count = r->headings ? 6 : 0;
    r->status = tiku_rio_model(tiku_richdoc_create(&r->options.document, &doc));
    if (tiku_rimp_active(r))
        r->status = tiku_rio_model(tiku_richdoc_import(doc, &source, 0));
    if (tiku_rimp_active(r)) *out = doc;
    else tiku_richdoc_release(doc);
    free(paragraphs);
    r->used -= r->paragraph_count * sizeof *paragraphs;
    return r->status;
}

void
tiku_rimp_dispose(tiku_rimp_t *r)
{
    while (r->images) {
        tiku_rimp_image_t *image = r->images;

        r->images = image->next;
        free((void *)image->value.pixels);
        free(image);
    }
    while (r->owned) {
        tiku_rimp_owned_t *p = r->owned;

        r->owned = p->next;
        free(p);
    }
    free(r->text);
    free(r->paragraphs);
    free(r->runs);
    free(r->blocks);
}
