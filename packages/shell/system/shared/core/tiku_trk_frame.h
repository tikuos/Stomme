/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_frame.h - where a window was, and which windows were open.
 *
 * Each folder's window frame is kept beside its arrangement, so a folder
 * opens where the user left it, at the size it had.  A folder never opened
 * before inherits a frame, and the session record lists the windows open
 * when the session ended.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_FRAME_H_
#define TIKU_TRK_FRAME_H_

#include "tiku_model.h"
#include "tiku_state.h"

/** @brief A window's rectangle, as it is stored. */
typedef struct {
    int32_t x, y, w, h;
} tiku_trk_frame_t;

/** @brief How many windows a session record holds. */
#define TIKU_TRK_SESSION_MAX 16

/** @brief One remembered window: its path, which identifies it, and whether
 *         it was minimized. */
typedef struct {
    char path[TIKU_PATH_MAX];
    int  minimized;
} tiku_trk_session_entry_t;

/** @brief How much of a window stays on screen when it is clamped: TAB
 *         pixels down from its top, and GRAB pixels across. */
#define TIKU_TRK_FRAME_TAB  24
#define TIKU_TRK_FRAME_GRAB 60

/** @brief The smallest frame: a stored one below it reads as none, and
 *         clamping grows a frame to it. */
#define TIKU_TRK_FRAME_MIN_W 180
#define TIKU_TRK_FRAME_MIN_H 120

/**
 * @brief Read the frame @p path's window had.
 *
 * A record smaller than TIKU_TRK_FRAME_MIN_W by TIKU_TRK_FRAME_MIN_H reads as
 * none.
 *
 * @return 1 when one was found, else 0 with @p out untouched.
 */
int tiku_trk_frame_load(tiku_store_t *s, const char *path,
                        tiku_trk_frame_t *out);

/**
 * @brief Write it back, but only if it moved or was resized (CW-002).
 *
 * A window left as it opened costs no store write.
 *
 * @param was The frame the window opened with, or NULL when it had none.
 * @return 1 when something was written.
 */
int tiku_trk_frame_save(tiku_store_t *s, const char *path,
                        const tiku_trk_frame_t *now,
                        const tiku_trk_frame_t *was);

/**
 * @brief Clamp a frame onto a screen of @p sw by @p sh.
 *
 * A frame from a bigger screen, or one saved when the window was dragged
 * mostly off, is moved so its tab stays on screen and can be grabbed.  The
 * size is kept between TIKU_TRK_FRAME_MIN_W by TIKU_TRK_FRAME_MIN_H and the
 * screen.
 */
void tiku_trk_frame_clamp(tiku_trk_frame_t *f, int sw, int sh);

/**
 * @brief The frame a folder opened for the first time should get (CW-005).
 *
 * Its parent's, offset so it does not land exactly on top of it; failing
 * that the saved default; failing that nothing, and the caller cascades.
 * The root inherits nothing and takes no default (CW-069).
 *
 * @return 1 when a frame was found.
 */
int tiku_trk_frame_inherit(tiku_store_t *s, const char *path,
                           tiku_trk_frame_t *out);

/**
 * @brief Read the fallback every unvisited folder starts from.
 *
 * @return 1 when one was found; a record smaller than the minimum reads as
 *         none.
 */
int tiku_trk_frame_default(tiku_store_t *s, tiku_trk_frame_t *out);
/** @brief Write the frame every unvisited folder will start from. */
void tiku_trk_frame_set_default(tiku_store_t *s,
                                const tiku_trk_frame_t *f);

/*---------------------------------------------------------------------------*/
/* The session (AW-004)                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Write the set of open windows, each as a shown one.
 *
 * Order matters: it is the order they will be reopened in, which decides
 * which ends up in front.  An empty set clears the record.
 *
 * @return how many were written.
 */
int tiku_trk_session_save(tiku_store_t *s,
                          const char paths[][TIKU_PATH_MAX], int n);

/**
 * @brief Read the paths back, minimized or not.
 *
 * @return how many paths were written to @p out.
 */
int tiku_trk_session_load(tiku_store_t *s,
                          char out[][TIKU_PATH_MAX], int max);

/**
 * @brief Write the full session record: one line per entry, 'M' for a
 *        minimized window or 'O' for a shown one, then its path.
 *
 * tiku_trk_session_save() is the path-only form of this.  An empty list
 * clears the record.
 *
 * @return how many entries were written, or 0 when the write failed.
 */
int tiku_trk_session_save_entries(tiku_store_t *s,
                                  const tiku_trk_session_entry_t *entries,
                                  int n);
/**
 * @brief Read the session into @p out, up to @p max entries: each a path
 *        and whether its window was minimized.
 *
 * A line with no 'M' or 'O' prefix reads as a shown window.
 *
 * @return how many were read.
 */
int tiku_trk_session_load_entries(tiku_store_t *s,
                                  tiku_trk_session_entry_t *out, int max);

/** @brief Forget it, so the next start opens the default instead. */
void tiku_trk_session_clear(tiku_store_t *s);

#endif /* TIKU_TRK_FRAME_H_ */
