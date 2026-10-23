/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_state.h - where a node's UI state lives.
 *
 * Every caller that reads or writes pose info, column sets or view state
 * goes through this one interface, so "the object holds its own state"
 * (local files, xattr) and "the viewer holds it for the object" (device
 * nodes, sidecar) are one code path.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_STATE_H_
#define TIKU_STATE_H_

#include <stddef.h>
#include <stdint.h>

/** @brief Attribute names.  Local files carry these as extended attributes,
 *         prefixed for the xattr namespace; the sidecar stores them under
 *         the same names. */
#define TIKU_ATTR_POSE_INFO   "_trk/pinfo"
#define TIKU_ATTR_COLUMNS     "_trk/columns"
#define TIKU_ATTR_VIEW_STATE  "_trk/viewstate"
/** @brief The Desktop's and the Disks window's own slots (MA-064):
 *         attribute names of their own, written on real nodes, because the
 *         xattr store can only write where a file is. */
#define TIKU_ATTR_DESK_VIEW_STATE  "_trk/desk_viewstate"
#define TIKU_ATTR_DESK_COLUMNS     "_trk/desk_columns"
#define TIKU_ATTR_DISKS_VIEW_STATE "_trk/d_viewstate"
#define TIKU_ATTR_DISKS_COLUMNS    "_trk/d_columns"
/** @brief Where a trashed item came from, written under the item's path in
 *         the Trash so the record is found with it there. */
#define TIKU_ATTR_ORIGINAL    "_trk/original_path"
/** @brief Where a folder's window was, kept per folder beside its
 *         arrangement, so each folder opens where it was left. */
#define TIKU_ATTR_FRAME       "_trk/frame"
/** @brief The set of windows that were open when the session ended, and
 *         the state a folder falls back to when neither it nor its parent
 *         has one. */
#define TIKU_ATTR_SESSION     "_trk/session"
#define TIKU_ATTR_DEFAULTS    "_trk/window_default"
/** @brief Where the Deskbar is docked and how wide it is, kept under the
 *         session slot: the bar belongs to the desktop, not to a folder. */
#define TIKU_ATTR_DOCK        "_trk/dock"
#define TIKU_ATTR_DESKBAR_PREFS "_trk/deskbar_prefs"
/** @brief The face dropped in the fonts folder that the interface is drawn
 *         in, stored as a family name; a family no longer in the folder
 *         falls back to the default face. */
#define TIKU_ATTR_UI_FONT "_trk/ui_font"
#define TIKU_ATTR_DESKBAR_PREFS_FRAME "_trk/deskbar_prefs_frame"
#define TIKU_ATTR_REPLICANTS "_trk/replicants"

/** @brief Reserved node keys: slots for state that has no node of its own
 *         to hold it. */
#define TIKU_KEY_DEV_ROOT  "\x01root"
#define TIKU_KEY_DESKTOP   "\x01" "desk"
/** @brief The session's own slot: the set of open windows belongs to no
 *         folder. */
#define TIKU_KEY_SESSION   "\x01session"

typedef struct tiku_store tiku_store_t;

/**
 * @brief Open the store for local files (extended attributes).
 *
 * State lives on the file itself and moves with it.
 */
tiku_store_t *tiku_store_xattr_open(void);

/**
 * @brief Open the per-device sidecar store.
 *
 * One file per device: $XDG_DATA_HOME/tracker/devices/<devid>.state, with
 * ~/.local/share standing in for an unset $XDG_DATA_HOME.
 *
 * @param devid  From tiku_ident_key().
 */
tiku_store_t *tiku_store_sidecar_open(const char *devid);

/** @brief Open a volatile store, for tests and for nodes whose state must
 *         not persist. */
tiku_store_t *tiku_store_memory_open(void);

/** @brief Free @p s; a sidecar with unsaved changes is written first. */
void tiku_store_free(tiku_store_t *s);

/**
 * @brief Read one attribute of one node.
 *
 * @param node  Absolute path, or a TIKU_KEY_* slot.
 * @return Bytes read, or -1 when absent.
 */
int tiku_state_read(tiku_store_t *s, const char *node,
                        const char *attr, void *buf, size_t max);

/** @brief Write one attribute.  @return 0, or -1 when the store refused. */
int tiku_state_write(tiku_store_t *s, const char *node,
                         const char *attr, const void *buf, size_t len);

/**
 * @brief Whether this store can persist at all: 0 for a sidecar with
 *        nowhere to write.
 *
 * @note When this is 0, a caller goes on without saving state.
 */
int tiku_store_writable(const tiku_store_t *s);

/**
 * @brief Persist pending changes: a sidecar is written whole, and the other
 *        stores have nothing pending.
 *
 * The sidecar batches: callers flush on directory switch, window close
 * and link loss, not on every change.
 */
int tiku_store_flush(tiku_store_t *s);

/**
 * @brief Forget one node's state held in the store's records.
 *
 * @note Only for a user-invoked "forget arrangements this device no longer
 *       has".  Nothing on the pose-teardown path may call this: a device
 *       unplugged or a directory switched must leave its arrangement intact,
 *       or reconnecting loses the user's desktop.
 * @return How many records went, or -1 for a NULL argument.
 */
int tiku_state_forget(tiku_store_t *s, const char *node);

/**
 * @brief Visit every attribute @p node carries.
 *
 * @param fn Called with the attribute name and its raw bytes; returning
 *           non-zero stops the walk.
 * @return how many were visited, or -1 when the names could not be read or
 *         a value could not be read or buffered; such a value is skipped
 *         and the walk goes on.
 */
int tiku_state_each(tiku_store_t *s, const char *node,
                        int (*fn)(const char *attr, const void *val,
                                  size_t len, void *ctx), void *ctx);

/** @brief Records held (tests and the store's own reporting). */
int tiku_store_count(const tiku_store_t *s);

/**
 * @brief Make the directory @p path lives in, and its parents.
 *
 * Settings, the thumbnail cache and the Deskbar's folder are written under
 * directories a fresh account does not have; a caller makes them with this
 * before writing.
 *
 * @return 0 when the directory exists afterwards.
 */
int tiku_state_mkparents(const char *path);

/**
 * @brief Where this Tracker's Desktop is: $TIKU_DESKTOP, else
 *        ~/.config/tracker/Desktop, which is not the host session's
 *        desktop directory.
 *
 * @return @p out.
 */
const char *tiku_desktop_dir(char *out, size_t max);

/**
 * @brief Where faces dropped in live: $TIKU_FONTS, else
 *        ~/.config/tracker/fonts.
 */
const char *tiku_fonts_dir(char *out, size_t max);

/**
 * @brief Where sounds dropped in live: $TIKU_SOUNDS, else
 *        ~/.config/tracker/sounds.
 */
const char *tiku_sounds_dir(char *out, size_t max);

#endif /* TIKU_STATE_H_ */
