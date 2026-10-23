/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richcellclip.c - bounded rich table-selection clipboard envelopes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richclip.h"
#include "tiku_richio_private.h"
#include <stdlib.h>
#include <string.h>

static const unsigned char magic[8] = {'T', 'I', 'K', 'U', 'R', 'C', 0, 1};

/** @brief Poll transfer cancellation without retaining caller context. */
static int
cancelled(const tiku_richio_options_t *options)
{
    return options->cancel && options->cancel(options->ctx);
}

/** @brief Require one validated table tree between empty body paragraphs. */
static tiku_richio_status_t
rectangle(const tiku_richsnapshot_t *snapshot, tiku_rich_cells_t *cells)
{
    tiku_rich_block_t block, table, first;
    tiku_rich_paragraph_t p;
    size_t count = tiku_richsnapshot_blocks(snapshot);

    if (count < 5 ||
        tiku_richsnapshot_block(snapshot, 0, &first) != TIKU_RICH_OK ||
        first.kind != TIKU_RICH_BLOCK_PARAGRAPH ||
        tiku_richsnapshot_paragraph(snapshot, 0, &p) != TIKU_RICH_OK ||
        p.length ||
        tiku_richsnapshot_block(snapshot, 1, &table) != TIKU_RICH_OK ||
        table.kind != TIKU_RICH_BLOCK_TABLE || table.parent != first.parent)
        return TIKU_RICHIO_UNSUPPORTED;
    tiku_richsnapshot_block(snapshot, count - 1, &block);
    tiku_richsnapshot_paragraph(snapshot,
        tiku_richsnapshot_paragraphs(snapshot) - 1, &p);
    if (block.kind != TIKU_RICH_BLOCK_PARAGRAPH ||
        block.parent != first.parent || p.length)
        return TIKU_RICHIO_UNSUPPORTED;
    for (size_t i = 2; i < count - 1; i++) {
        tiku_richsnapshot_block(snapshot, i, &block);
        if (block.parent == first.parent)
            return TIKU_RICHIO_UNSUPPORTED;
    }
    *cells = (tiku_rich_cells_t){table.id, 0, 0,
                                table.table.rows, table.table.columns};
    return TIKU_RICHIO_OK;
}

/** @brief Build a temporary table containing only selected source content. */
static tiku_richio_status_t
capture(const tiku_richsnapshot_t *source, const tiku_rich_cells_t *cells,
    const tiku_richio_options_t *options, tiku_richdoc_t **out)
{
    tiku_richdoc_t *doc = NULL;
    tiku_richsnapshot_t *snapshot = NULL;
    tiku_rich_paragraph_t p;
    tiku_rich_block_t cell;
    tiku_richio_status_t status;

    status = tiku_rio_model(tiku_richsnapshot_cell_at(source, cells, 0, &cell));
    if (status != TIKU_RICHIO_OK) return status;
    status = tiku_rio_model(tiku_richdoc_create(&options->document, &doc));
    if (status != TIKU_RICHIO_OK) return status;
    status = tiku_rio_model(tiku_richdoc_snapshot(doc, &snapshot));
    if (status == TIKU_RICHIO_OK) {
        tiku_richsnapshot_paragraph(snapshot, 0, &p);
        tiku_rich_pos_t at = {tiku_richsnapshot_body(snapshot), p.id, 0, 0};

        status = tiku_rio_model(tiku_richdoc_cells_insert(doc, 0, at,
            source, cells, NULL));
    }
    tiku_richsnapshot_release(snapshot);
    if (status == TIKU_RICHIO_OK) {
        tiku_richdoc_clear_history(doc);
        *out = doc;
    } else tiku_richdoc_release(doc);
    return status;
}

tiku_richio_status_t
tiku_richcellclip_write(const tiku_richsnapshot_t *snapshot,
    const tiku_rich_cells_t *cells, const tiku_richio_options_t *options,
    unsigned char **out, size_t *length)
{
    tiku_richio_options_t opt;
    tiku_richdoc_t *doc = NULL;
    tiku_richsnapshot_t *selected = NULL;
    unsigned char *xml = NULL, *packet;
    size_t bytes = 0;
    tiku_richio_status_t status;

    if (out) *out = NULL;
    if (length) *length = 0;
    if (!snapshot || !cells || !out || !length) return TIKU_RICHIO_INVALID;
    status = tiku_rio_clip_options(options, &opt);
    if (status != TIKU_RICHIO_OK) return status;
    if (cancelled(&opt)) return TIKU_RICHIO_CANCELLED;
    status = capture(snapshot, cells, &opt, &doc);
    if (status == TIKU_RICHIO_OK && cancelled(&opt))
        status = TIKU_RICHIO_CANCELLED;
    if (status == TIKU_RICHIO_OK)
        status = tiku_rio_model(tiku_richdoc_snapshot(doc, &selected));
    if (status == TIKU_RICHIO_OK)
        status = tiku_richio_write_xml(selected, &opt, &xml, &bytes);
    tiku_richsnapshot_release(selected);
    tiku_richdoc_release(doc);
    if (status == TIKU_RICHIO_OK && cancelled(&opt))
        status = TIKU_RICHIO_CANCELLED;
    if (status == TIKU_RICHIO_OK) {
        packet = malloc(bytes + 16);
        if (!packet) status = TIKU_RICHIO_NO_MEMORY;
        else {
            memcpy(packet, magic, sizeof magic);
            for (unsigned i = 0; i < 8; i++)
                packet[8 + i] = (unsigned char)((uint64_t)bytes >> (i * 8));
            memcpy(packet + 16, xml, bytes);
            *out = packet;
            *length = bytes + 16;
        }
    }
    free(xml);
    return status;
}

tiku_richio_status_t
tiku_richcellclip_read(const void *data, size_t length,
    const tiku_richio_options_t *options, tiku_richdoc_t **out,
    tiku_rich_cells_t *cells)
{
    const unsigned char *packet = data;
    tiku_richio_options_t opt;
    tiku_richdoc_t *doc = NULL;
    tiku_richsnapshot_t *snapshot = NULL;
    tiku_richio_status_t status;
    tiku_rich_cells_t selected = {0};
    uint64_t bytes = 0;

    if (out) *out = NULL;
    if (cells) memset(cells, 0, sizeof *cells);
    if (!data || !out || !cells) return TIKU_RICHIO_INVALID;
    if (length > TIKU_RICHCLIP_LIMIT) return TIKU_RICHIO_LIMIT;
    if (length < 16 || memcmp(packet, magic, 7)) return TIKU_RICHIO_DAMAGED;
    if (packet[7] != magic[7]) return TIKU_RICHIO_UNSUPPORTED;
    for (unsigned i = 0; i < 8; i++)
        bytes |= (uint64_t)packet[8 + i] << (i * 8);
    if (!bytes || bytes != length - 16) return TIKU_RICHIO_DAMAGED;
    status = tiku_rio_clip_options(options, &opt);
    if (status != TIKU_RICHIO_OK) return status;
    if (cancelled(&opt)) return TIKU_RICHIO_CANCELLED;
    status = tiku_richio_read_xml(packet + 16, (size_t)bytes, &opt, &doc);
    if (status == TIKU_RICHIO_OK)
        status = tiku_rio_model(tiku_richdoc_snapshot(doc, &snapshot));
    if (status == TIKU_RICHIO_OK) status = rectangle(snapshot, &selected);
    tiku_richsnapshot_release(snapshot);
    if (status == TIKU_RICHIO_OK && cancelled(&opt))
        status = TIKU_RICHIO_CANCELLED;
    if (status == TIKU_RICHIO_OK) {
        *out = doc;
        *cells = selected;
    } else tiku_richdoc_release(doc);
    return status;
}
