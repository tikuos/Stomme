/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_soundprefs.h - which moments sound, and how loud, as settings.
 *
 * One line per event in the settings file ("sound.beep on"), one naming
 * the file it plays ("sound.beep.clip") and one for the volume
 * ("sound.volume 80"), so the Sounds window and a hand edit of the file are
 * the same act.  tiku_trk_soundprefs_apply() is the one path into the kit.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SOUNDPREFS_H_
#define TIKU_TRK_SOUNDPREFS_H_

#include "tiku_sound.h"
#include "tiku_trk_prefs.h"

/** @brief How far one step moves the volume, out of 100. */
#define TIKU_TRK_SOUND_VOLUME_STEP 10
/** @brief How many files the sounds folder can offer. */
#define TIKU_TRK_SOUND_OFFER_MAX 24

/** @brief The files in the sounds folder, by name, in order. */
typedef struct {
    char name[TIKU_TRK_SOUND_OFFER_MAX][TIKU_TRK_PREF_TEXT_MAX];
    int  count;
} tiku_trk_soundprefs_offer_t;

/** @brief The setting's name for @p event: "sound." and the kit's name. */
const char *tiku_trk_soundprefs_key(tiku_sound_event_t event);

/** @brief Whether @p event is on in @p prefs. */
int tiku_trk_soundprefs_enabled(tiku_trk_prefs_t *prefs,
                                tiku_sound_event_t event);

/** @brief Turn @p event on or off in @p prefs; nothing is applied. */
void tiku_trk_soundprefs_set_enabled(tiku_trk_prefs_t *prefs,
                                     tiku_sound_event_t event, int on);

/** @brief The volume in @p prefs, clamped to 0..100. */
int tiku_trk_soundprefs_volume(tiku_trk_prefs_t *prefs);

/**
 * @brief Move the volume one step up or down.
 * @return 1 when it moved, 0 at either end.
 */
int tiku_trk_soundprefs_step_volume(tiku_trk_prefs_t *prefs, int up);

/** @brief The setting naming @p event's clip: "sound.<name>.clip". */
const char *tiku_trk_soundprefs_clip_key(tiku_sound_event_t event);

/** @brief The file @p event plays from the sounds folder, "" for baked. */
const char *tiku_trk_soundprefs_clip(tiku_trk_prefs_t *prefs,
                                     tiku_sound_event_t event);

/** @brief Name the file @p event plays; "" or NULL means the baked one. */
void tiku_trk_soundprefs_set_clip(tiku_trk_prefs_t *prefs,
                                  tiku_sound_event_t event,
                                  const char *name);

/**
 * @brief The .score and .wav files in the sounds folder (tiku_sounds_dir),
 *        sorted.  @return how many.
 */
int tiku_trk_soundprefs_offer(tiku_trk_soundprefs_offer_t *out);

/**
 * @brief Move @p event to the next choice: the baked sound, then each
 *        file on offer, then the baked sound again.
 * @return 1 when it changed, 0 with nothing on offer.
 */
int tiku_trk_soundprefs_cycle_clip(tiku_trk_prefs_t *prefs,
                                   const tiku_trk_soundprefs_offer_t *offer,
                                   tiku_sound_event_t event);

/** @brief Whether any sound setting differs from its default. */
int tiku_trk_soundprefs_defaultable(tiku_trk_prefs_t *prefs);

/** @brief Put every sound setting back to its default. */
void tiku_trk_soundprefs_defaults(tiku_trk_prefs_t *prefs);

/** @brief Push the enables, the file choices and the volume in @p prefs
 *         into the kit; an event whose file is unset or unreadable gets its
 *         baked score. */
void tiku_trk_soundprefs_apply(tiku_trk_prefs_t *prefs);

#endif /* TIKU_TRK_SOUNDPREFS_H_ */
