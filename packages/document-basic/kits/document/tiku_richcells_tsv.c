/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richcells_tsv.c - rectangular quoted TSV validation and decoding.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richcells_private.h"
#include <string.h>

/** @brief Measure or emit one field, leaving its delimiter unconsumed. */
static tiku_rich_status_t
field(const char *text, size_t length, size_t *at, char *out, size_t *used)
{
    int quoted = *at < length && text[*at] == '"', closed = !quoted;

    if (quoted) (*at)++;
    while (*at < length) {
        char ch = text[(*at)++];

        if (quoted && ch == '"') {
            if (*at < length && text[*at] == '"') (*at)++;
            else { closed = 1; break; }
        } else if (!quoted && (ch == '\t' || ch == '\r' || ch == '\n')) {
            (*at)--;
            break;
        }
        if (ch == '\r') {
            if (*at < length && text[*at] == '\n') (*at)++;
            ch = '\n';
        }
        if (out) out[*used] = ch;
        (*used)++;
    }
    if (!closed || (*at < length && text[*at] != '\t' &&
        text[*at] != '\r' && text[*at] != '\n'))
        return TIKU_RICH_INVALID_TEXT;
    return TIKU_RICH_OK;
}

/** @brief Check rectangular dimensions and optional field storage. */
static tiku_rich_status_t
decode(const char *text, size_t length, tiku_rd_tsv_t *out, size_t *bytes)
{
    size_t at = 0, used = 0, count = 0;
    unsigned rows = 0, columns = 0, width = 0;
    tiku_rich_status_t status;

    for (;;) {
        size_t start = used;

        if (count == 4096 || width == 64 || rows == 256)
            return TIKU_RICH_LIMIT;
        status = field(text, length, &at, out->text, &used);
        if (status != TIKU_RICH_OK) return status;
        if (out->cells)
            out->cells[count] = (tiku_rd_textcell_t){start, used - start};
        count++; width++;
        if (at < length && text[at] == '\t') { at++; continue; }
        if (rows && width != columns) return TIKU_RICH_INVALID_RANGE;
        columns = width; rows++; width = 0;
        if (at == length) break;
        if (text[at++] == '\r' && at < length && text[at] == '\n') at++;
        if (at == length) break;
    }
    out->rows = rows;
    out->columns = columns;
    out->count = count;
    *bytes = used;
    return TIKU_RICH_OK;
}

void
tiku_rd_tsv_drop(tiku_rd_tsv_t *table)
{
    tiku_rd_free(table->cells);
    tiku_rd_free(table->text);
    memset(table, 0, sizeof *table);
}

tiku_rich_status_t
tiku_rd_tsv_parse(tiku_rd_memory_t *memory, const char *text, size_t length,
                  tiku_rd_tsv_t *out)
{
    tiku_rich_status_t status;
    size_t bytes;

    memset(out, 0, sizeof *out);
    if (!text && length) return TIKU_RICH_INVALID;
    if (length == SIZE_MAX || length >= memory->limit) return TIKU_RICH_LIMIT;
    status = decode(text, length, out, &bytes);
    if (status != TIKU_RICH_OK) goto fail;
    out->text = tiku_rd_alloc(memory, bytes + 1, &status);
    if (!out->text) goto fail;
    out->cells = tiku_rd_alloc(memory,
        out->count * sizeof *out->cells, &status);
    if (!out->cells) goto fail;
    status = decode(text, length, out, &bytes);
    for (size_t i = 0; status == TIKU_RICH_OK && i < out->count; i++)
        status = tiku_rd_utf8(out->text + out->cells[i].start,
                              out->cells[i].length, 1);
    if (status == TIKU_RICH_OK) return status;
fail:
    tiku_rd_tsv_drop(out);
    return status;
}
