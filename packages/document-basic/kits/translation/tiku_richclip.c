/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richclip.c - versioned fragment envelopes and checked model capture.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richclip.h"
#include "tiku_richio_private.h"
#include <stdlib.h>
#include <string.h>

static const unsigned char magic[8] = {'T', 'I', 'K', 'U', 'R', 'F', 0, 1};

tiku_richio_status_t
tiku_rio_clip_options(const tiku_richio_options_t *input,
                      tiku_richio_options_t *out)
{
    tiku_richio_options_t requested = {0};
    tiku_richio_status_t status;
    size_t ceiling = TIKU_RICHCLIP_LIMIT - 16;

    if (input) requested = *input;
    if (!requested.memory_bytes) requested.memory_bytes = (size_t)64 << 20;
    if (!requested.document.memory_bytes)
        requested.document.memory_bytes = (size_t)64 << 20;
    status = tiku_rio_options(&requested, out);
    if (status != TIKU_RICHIO_OK) return status;
    if (out->memory_bytes <= 18) return TIKU_RICHIO_LIMIT;
    /* The XML buffer and complete envelope coexist during encoding. */
    if (ceiling > (out->memory_bytes - 18) / 2)
        ceiling = (out->memory_bytes - 18) / 2;
    if (out->xml_bytes > ceiling) out->xml_bytes = ceiling;
    return TIKU_RICHIO_OK;
}

/** @brief Poll the caller without retaining its context. */
static int
cancelled(const tiku_richio_options_t *options)
{
    return options->cancel && options->cancel(options->ctx);
}

tiku_richio_status_t
tiku_richclip_write(const tiku_richfragment_t *fragment,
    const tiku_richio_options_t *options, unsigned char **out, size_t *length)
{
    tiku_richio_options_t opt;
    tiku_richsnapshot_t *snapshot = NULL;
    unsigned char *xml = NULL, *packet = NULL;
    size_t size = 0;
    tiku_richio_status_t status;

    if (out) *out = NULL;
    if (length) *length = 0;
    if (!fragment || !out || !length) return TIKU_RICHIO_INVALID;
    status = tiku_rio_clip_options(options, &opt);
    if (status != TIKU_RICHIO_OK) return status;
    if (cancelled(&opt)) return TIKU_RICHIO_CANCELLED;
    status = tiku_rio_model(tiku_richfragment_snapshot(fragment, &snapshot));
    if (status == TIKU_RICHIO_OK)
        status = tiku_richio_write_xml(snapshot, &opt, &xml, &size);
    tiku_richsnapshot_release(snapshot);
    if (status != TIKU_RICHIO_OK) return status;
    if (cancelled(&opt)) status = TIKU_RICHIO_CANCELLED;
    else {
        packet = malloc(size + 16);
        if (!packet) status = TIKU_RICHIO_NO_MEMORY;
        else {
            memcpy(packet, magic, sizeof magic);
            for (unsigned i = 0; i < 8; i++)
                packet[8 + i] = (unsigned char)((uint64_t)size >> (i * 8));
            memcpy(packet + 16, xml, size);
            *out = packet;
            *length = size + 16;
        }
    }
    free(xml);
    return status;
}

/** @brief Capture a flat body's full range without flattening table content. */
static tiku_richio_status_t
capture(tiku_richdoc_t *doc, tiku_richfragment_t **out)
{
    tiku_richsnapshot_t *snapshot = NULL;
    tiku_rich_paragraph_t first, last;
    size_t count;
    tiku_richio_status_t status;

    status = tiku_rio_model(tiku_richdoc_snapshot(doc, &snapshot));
    if (status != TIKU_RICHIO_OK) return status;
    count = tiku_richsnapshot_paragraphs(snapshot);
    if (!count) status = TIKU_RICHIO_DAMAGED;
    else if (tiku_richsnapshot_blocks(snapshot) != count)
        status = TIKU_RICHIO_UNSUPPORTED;
    else {
        tiku_richsnapshot_paragraph(snapshot, 0, &first);
        tiku_richsnapshot_paragraph(snapshot, count - 1, &last);
        status = tiku_rio_model(tiku_richfragment_create(snapshot,
            (tiku_rich_pos_t){tiku_richsnapshot_body(snapshot),
                first.id, 0, TIKU_RICH_BEFORE},
            (tiku_rich_pos_t){tiku_richsnapshot_body(snapshot),
                last.id, last.length, TIKU_RICH_AFTER}, out));
    }
    tiku_richsnapshot_release(snapshot);
    return status;
}

tiku_richio_status_t
tiku_richclip_read(const void *data, size_t length,
    const tiku_richio_options_t *options, tiku_richfragment_t **out)
{
    const unsigned char *packet = data;
    tiku_richio_options_t opt;
    tiku_richdoc_t *doc = NULL;
    tiku_richfragment_t *fragment = NULL;
    tiku_richio_status_t status;
    uint64_t size = 0;

    if (!out) return TIKU_RICHIO_INVALID;
    *out = NULL;
    if (!data) return TIKU_RICHIO_INVALID;
    if (length > TIKU_RICHCLIP_LIMIT) return TIKU_RICHIO_LIMIT;
    if (length < 16) return TIKU_RICHIO_DAMAGED;
    if (memcmp(packet, magic, 7)) return TIKU_RICHIO_DAMAGED;
    if (packet[7] != magic[7]) return TIKU_RICHIO_UNSUPPORTED;
    for (unsigned i = 0; i < 8; i++) size |= (uint64_t)packet[8 + i] << (i * 8);
    if (!size || size != length - 16) return TIKU_RICHIO_DAMAGED;
    status = tiku_rio_clip_options(options, &opt);
    if (status != TIKU_RICHIO_OK) return status;
    if (cancelled(&opt)) return TIKU_RICHIO_CANCELLED;
    status = tiku_richio_read_xml(packet + 16, (size_t)size, &opt, &doc);
    if (status == TIKU_RICHIO_OK) {
        if (cancelled(&opt)) status = TIKU_RICHIO_CANCELLED;
        else status = capture(doc, &fragment);
    }
    tiku_richdoc_release(doc);
    if (status == TIKU_RICHIO_OK && cancelled(&opt))
        status = TIKU_RICHIO_CANCELLED;
    if (status == TIKU_RICHIO_OK) *out = fragment;
    else tiku_richfragment_release(fragment);
    return status;
}
