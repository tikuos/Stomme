/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mount.h - one namespace over several backends.
 *
 * A namespace over several backends, itself a backend: each is attached
 * at a path prefix, and every operation goes to the backend whose prefix
 * is the longest that claims the path.  Paths are rewritten both ways, a
 * path that is only an ancestor of a mount is listed as a directory of the
 * mounts beneath it, and ids leaving here carry the mount's slot in their
 * top byte, so two stores' inode 12345 stay apart.  The layout is set out
 * in kits/storage/doc/namespace.md.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MOUNT_H_
#define TIKU_MOUNT_H_

#include <stddef.h>

#include "tiku_model.h"

/**
 * @brief How many backends one namespace may hold.
 *
 * The local files and seven devices.  The table is searched on every
 * operation, so it is kept small.
 */
#define TIKU_MOUNT_MAX 8

/** @brief An empty namespace, or NULL when memory runs out. */
tiku_backend_t *tiku_mount_open(void);

/**
 * @brief Attach @p b at @p prefix.
 *
 * @param prefix Absolute, no trailing slash except for the root "/".
 * @param owned  Nonzero to close @p b when the namespace closes or the
 *               mount is removed; with zero the caller closes it.
 * @return 0, or -1 if the prefix is bad, taken, or the table is full.
 */
int tiku_mount_add(tiku_backend_t *router, const char *prefix,
                   tiku_backend_t *b, int owned);

/**
 * @brief Detach whatever is at @p prefix, closing it when it is owned.
 *
 * The slot is emptied in place, so the slot numbers in ids already handed
 * out keep naming the same mounts.
 *
 * @return 0, or -1 when nothing is mounted there.
 */
int tiku_mount_remove(tiku_backend_t *router, const char *prefix);

/**
 * @brief Take the mount at @p prefix out of the table without closing
 *        it, and return its backend, which the caller closes later.
 * @return the backend, or NULL for no such mount.
 */
tiku_backend_t *tiku_mount_detach(tiku_backend_t *router,
                                  const char *prefix);

/**
 * @brief Which mounting of slot @p i this is: a number that changes each
 *        time the slot is filled, so a result made for one board can be
 *        told from one for the next in its place.  0 for an empty slot.
 */
unsigned tiku_mount_gen_at(const tiku_backend_t *router, int i);

/**
 * @brief Mark @p raw, an id a slot's own backend minted, as the router
 *        marks every id that leaves it: for a change record fetched from
 *        that backend directly.
 */
uint64_t tiku_mount_tag_id(int slot, uint64_t raw);

/**
 * @brief Make an entry a slot's backend listed look as the router would
 *        have shown it: its path under @p prefix, its id marked with
 *        @p slot.  It does not read the table, so it can run on a
 *        worker.
 */
void tiku_mount_adopt_entry(const char *prefix, int slot, tiku_model_t *m);

/** @brief How many mounts there are, ignoring the gaps left by removal. */
int tiku_mount_count(const tiku_backend_t *router);

/** @brief How many slots there are to walk: TIKU_MOUNT_MAX for a
 *         namespace, 0 for anything else. */
int tiku_mount_slots(const tiku_backend_t *router);

/** @brief The prefix of slot @p i, or NULL for an empty slot or past the
 *         end. */
const char *tiku_mount_prefix_at(const tiku_backend_t *router, int i);

/**
 * @brief The backend of slot @p i, or NULL for an empty slot or past the
 *        end.
 */
tiku_backend_t *tiku_mount_at(const tiku_backend_t *router, int i);

/** @brief Whether @p b is a namespace. */
int tiku_mount_is(const tiku_backend_t *b);

/**
 * @brief The backend that serves @p path, router or not.
 *
 * A plain backend answers for itself, with @p local a copy of @p path; a
 * namespace routes and strips the prefix.
 *
 * @note Code that depends on the kind of store a path is on asks this: a
 *       namespace's own ops are "mount" for every path.
 *
 * @return the serving backend, or NULL when nothing serves @p path.
 */
tiku_backend_t *tiku_backend_serving(tiku_backend_t *b, const char *path,
                                     char *local, size_t max);

/**
 * @brief The state store of whatever serves @p path.
 *
 * state_store() on the ops table takes no path, so a namespace answers it
 * with its root's store.  State for a device node belongs in that device's
 * sidecar, which this returns for a path on the device.
 *
 * @return the store, or NULL when nothing serves @p path or the store
 *         that does keeps no state.
 */
struct tiku_store *tiku_backend_store_for(tiku_backend_t *b,
                                          const char *path);

/**
 * @brief Who serves @p path, and what it is called there.
 *
 * @param local Receives the path as the answering backend knows it;
 *              may be NULL when only the backend is wanted.
 * @return the backend, or NULL when the path is a synthetic ancestor or
 *         belongs to nothing at all.
 */
tiku_backend_t *tiku_mount_route(const tiku_backend_t *router,
                                 const char *path, char *local,
                                 size_t max);

#endif /* TIKU_MOUNT_H_ */
