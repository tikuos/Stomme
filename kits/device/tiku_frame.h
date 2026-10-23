/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_frame.h - the bytes that frame a message on a board's console.
 *
 * A frame is END, a marker byte naming its channel, the payload with END,
 * ESC and NUL each replaced by ESC and its escaped form, then END.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_FRAME_H_
#define TIKU_FRAME_H_

/** @brief The byte that opens and closes a frame. */
#define TIKU_FRAME_END      0xC0u
/** @brief The byte before an escaped form. */
#define TIKU_FRAME_ESC      0xDBu
/** @brief The escaped forms of END, ESC and NUL inside a payload. */
#define TIKU_FRAME_ESC_END  0xDCu
#define TIKU_FRAME_ESC_ESC  0xDDu
#define TIKU_FRAME_ESC_NUL  0xDEu

/** @brief The marker a board's window session rides. */
#define TIKU_FRAME_MARK_SESSION 0xF1u

#endif /* TIKU_FRAME_H_ */
