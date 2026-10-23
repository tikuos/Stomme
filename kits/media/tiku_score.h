/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_score.h - a sound written down: notes, a waveform, an envelope.
 *
 * A score is text, one instruction per line; blank lines and lines that
 * start with '#' are skipped:
 *
 *     wave sine|triangle|square      the waveform for every note
 *     attack <ms>                    the rise at the start of each note
 *     release <ms>                   the fall at its end
 *     note <hz> <ms> <amplitude>     one tone, amplitude 0 to 1
 *     rest <ms>                      silence
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SCORE_H_
#define TIKU_SCORE_H_

#include <stddef.h>
#include <stdint.h>

/**
 * @brief The longest a clip may be, in seconds: room for a dropped-in
 *        file, and a bound on what one rendering may take.
 */
#define TIKU_SCORE_MAX_S 30u

/**
 * @brief How many frames @p text renders to at @p rate, without rendering.
 * @return the count, or 0 for a score with no notes or a line it cannot
 *         read; a score longer than TIKU_SCORE_MAX_S is cut there.
 */
size_t tiku_score_frames(const char *text, unsigned rate);

/**
 * @brief Render @p text into @p out, at most @p max mono 16-bit frames.
 *
 * Every note is shaped by the attack and release in force, so a clip
 * starts and ends at silence; the peak never exceeds the amplitude
 * asked for.  @return the frames written, 0 for a score that does not
 * read.
 */
size_t tiku_score_render(const char *text, unsigned rate, int16_t *out,
                         size_t max);

#endif /* TIKU_SCORE_H_ */
