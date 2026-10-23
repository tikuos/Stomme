/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richcmd_private.h - shared command range and dispatch helpers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHCMD_PRIVATE_H_
#define TIKU_RICHCMD_PRIVATE_H_

#include "tiku_richcmd.h"
#include "tiku_richdoc_private.h"

/** @brief Character-property bit for a toggle identity, or zero. */
unsigned tiku_rc_toggle_bit(tiku_richcmd_kind_t kind);
/** @brief Resolve a caret-relative grid command and check its dimensions. */
tiku_rich_status_t tiku_rc_grid_edit(const tiku_rd_root_t *root,
    uint64_t paragraph, tiku_richcmd_kind_t kind, tiku_rich_edit_t *edit);
/** @brief Alignment for an alignment command; caller checks its identity. */
tiku_rich_align_t tiku_rc_alignment(tiku_richcmd_kind_t kind);
/** @brief Derive a command's indicator from resolved selection formatting. */
tiku_richcmd_check_t tiku_rc_checked(tiku_richcmd_kind_t kind,
    const tiku_rich_selection_t *selection);
/** @brief Count selected text slices or touched paragraphs in a checked range.
 *  @note Last at byte zero excludes that paragraph, except for a caret. */
size_t tiku_rc_count(const tiku_richdoc_t *doc,
    const tiku_richcmd_context_t *context, tiku_richcmd_scope_t scope);
/** @brief Apply one checked command to selected paragraphs in one group. */
tiku_rich_status_t tiku_rc_apply(tiku_richdoc_t *doc,
    const tiku_richcmd_context_t *context, const tiku_richcmd_t *command,
    const tiku_richcmd_state_t *state);

#endif /* TIKU_RICHCMD_PRIVATE_H_ */
