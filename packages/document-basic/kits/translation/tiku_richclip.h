/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richclip.h - bounded serialized rich-text fragments.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHCLIP_H_
#define TIKU_RICHCLIP_H_
#include "tiku_richio.h"
#include "tiku_richcells.h"

/** @brief MIME type of a versioned binary fragment envelope. */
#define TIKU_RICHCLIP_MIME "application/x-tiku-richfragment"
/** @brief MIME type of a versioned rich rectangular cell envelope. */
#define TIKU_RICHCELLCLIP_MIME "application/x-tiku-richcells"
/** @brief Maximum complete envelope size, including its 16-byte header. */
#define TIKU_RICHCLIP_LIMIT (16u * 1024u * 1024u)

/** @brief Encode selected paragraphs, styles, links and inline pictures.
 *  @note The result is binary, not a clipboard text string or native file.
 *        Options bound XML, scratch and model allocations separately.
 *        Scratch defaults to 64 MiB; the complete payload is at most 16 MiB.
 *        No clipboard publication or source mutation occurs.
 *  @return OK with malloc-owned *out and *length; free with free().
 *          Failure clears both outputs. */
tiku_richio_status_t tiku_richclip_write(const tiku_richfragment_t *fragment,
    const tiku_richio_options_t *options, unsigned char **out, size_t *length);

/** @brief Validate an untrusted envelope and return an owned fragment.
 *  @note Refuses unknown versions, trailing bytes, tables and invalid XML.
 *        Model memory defaults to 64 MiB, including temporary import and
 *        fragment capture. Custom allocator context must outlive *out.
 *        Input is borrowed only during this call. No destination is edited.
 *  @return OK with *out, released with tiku_richfragment_release();
 *          failure clears *out. Cancellation is polled at codec boundaries. */
tiku_richio_status_t tiku_richclip_read(const void *data, size_t length,
    const tiku_richio_options_t *options, tiku_richfragment_t **out);
/** @brief Encode selected cells, retaining styles, resources and shading.
 *  @note Includes nested trees. Copies only the selected cells and their
 *        referenced styles. Uses the same limits and ownership as richclip.
 *        The temporary document also consumes the configured model budget. */
tiku_richio_status_t tiku_richcellclip_write(
    const tiku_richsnapshot_t *snapshot, const tiku_rich_cells_t *cells,
    const tiku_richio_options_t *options, unsigned char **out, size_t *length);
/** @brief Validate a cell envelope into an owned document and its rectangle.
 *  @note Release *out with tiku_richdoc_release(); take a snapshot for paste.
 *        Refuses extra content, malformed/unknown envelopes and excess depth.
 *        Both outputs are cleared on failure. Input is borrowed during call. */
tiku_richio_status_t tiku_richcellclip_read(const void *data, size_t length,
    const tiku_richio_options_t *options, tiku_richdoc_t **out,
    tiku_rich_cells_t *cells);
#endif
