/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richio_text.c - bounded UTF-8 text import and checked model creation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richio_private.h"
#include <stdlib.h>
#include <string.h>

/** @brief Normalize line endings and count paragraphs, polling cancellation. */
static tiku_richio_status_t
normalize(const char *input, size_t length,
          const tiku_richio_options_t *options,
          char *text, size_t *used, size_t *paragraphs)
{
    size_t start = length >= 3 && !memcmp(input, "\xef\xbb\xbf", 3) ? 3 : 0;

    *used = 0;
    *paragraphs = 1;
    for (size_t i = start; i < length; i++) {
        if (!(i % 4096) && options->cancel && options->cancel(options->ctx))
            return TIKU_RICHIO_CANCELLED;
        char ch = input[i];

        if (ch == '\r') {
            ch = '\n';
            if (i + 1 < length && input[i + 1] == '\n') i++;
        }
        text[(*used)++] = ch;
        if (ch == '\n') (*paragraphs)++;
    }
    return TIKU_RICHIO_OK;
}

tiku_richio_status_t
tiku_richio_read_text(const void *input, size_t length,
    const tiku_richio_options_t *options, tiku_richdoc_t **out)
{
    tiku_richio_options_t settings;
    tiku_richio_status_t status;
    tiku_rich_source_t source = {0};
    tiku_rich_paragraph_source_t *paragraphs = NULL;
    tiku_richdoc_t *doc = NULL;
    char *text;
    size_t used, count, start = 0, limit;

    if (!out) return TIKU_RICHIO_INVALID;
    *out = NULL;
    if (!input && length) return TIKU_RICHIO_INVALID;
    status = tiku_rio_options(options, &settings);
    if (status != TIKU_RICHIO_OK) return status;
    limit = settings.document.text_bytes ? settings.document.text_bytes :
                                           (size_t)64 << 20;
    if (length > limit || length >= settings.memory_bytes)
        return TIKU_RICHIO_LIMIT;
    if (settings.cancel && settings.cancel(settings.ctx))
        return TIKU_RICHIO_CANCELLED;
    text = malloc(length + 1);
    if (!text) return TIKU_RICHIO_NO_MEMORY;
    status = normalize(input, length, &settings, text, &used, &count);
    limit = settings.document.paragraphs ? settings.document.paragraphs :
                                           250000;
    if (status == TIKU_RICHIO_OK && (count > limit || count >
        (settings.memory_bytes - length - 1) / sizeof *paragraphs))
        status = TIKU_RICHIO_LIMIT;
    if (status == TIKU_RICHIO_OK) {
        paragraphs = calloc(count, sizeof *paragraphs);
        if (!paragraphs) status = TIKU_RICHIO_NO_MEMORY;
    }
    for (size_t i = 0; status == TIKU_RICHIO_OK && i <= used; i++) {
        if (i < used && text[i] != '\n') continue;
        paragraphs[source.paragraph_count++] = (tiku_rich_paragraph_source_t){
            .text = text + start, .length = i - start};
        start = i + 1;
    }
    if (status == TIKU_RICHIO_OK && settings.cancel &&
        settings.cancel(settings.ctx)) status = TIKU_RICHIO_CANCELLED;
    if (status == TIKU_RICHIO_OK)
        status = tiku_rio_model(tiku_richdoc_create(&settings.document, &doc));
    source.paragraphs = paragraphs;
    source.paragraph_count = count;
    if (status == TIKU_RICHIO_OK)
        status = tiku_rio_model(tiku_richdoc_import(doc, &source, 0));
    if (status == TIKU_RICHIO_OK) *out = doc;
    else tiku_richdoc_release(doc);
    free(paragraphs);
    free(text);
    return status;
}
