/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_native_record.h - typed public fields of native layout and session records.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_NATIVE_RECORD_H_
#define TIKU_TRK_NATIVE_RECORD_H_
#include "tiku_resource.h"
/** @brief Native record grammars; related attributes share their existing layout. */
typedef enum {
    TIKU_TRK_RECORD_COLUMNS, TIKU_TRK_RECORD_VIEW, TIKU_TRK_RECORD_POSE,
    TIKU_TRK_RECORD_FRAME, TIKU_TRK_RECORD_POSITION, TIKU_TRK_RECORD_DOCK,
    TIKU_TRK_RECORD_SESSION, TIKU_TRK_RECORD_REPLICANTS, TIKU_TRK_RECORD_ORIGINAL
} tiku_trk_native_record_kind_t;
/** @brief Decode supported native fields; NULL means malformed, unsupported or no memory.
 *  @note Absent/empty records become an empty object or array, or empty original path. */
tiku_msg_t *tiku_trk_native_record_decode(tiku_trk_native_record_kind_t, const void *, size_t length);
/** @brief Encode an exact typed replacement; reject unknown keys, ranges and versions.
 *  @note Empty objects/arrays clear the record. Original paths are observed through decode.
 *  @return 0 with a complete result, or -1 without a successful partial record. */
int tiku_trk_native_record_encode(tiku_trk_native_record_kind_t, const tiku_msg_t *, void *, size_t capacity, size_t *length);
#endif
