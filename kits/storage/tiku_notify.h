/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_notify.h - change notices, and where they come from.
 *
 * Node, path and query notices go through one handler, so a query window
 * updates by the same code as a folder window.  Notices are derived by
 * diffing a re-listing against what the view holds.  A device also reports
 * its own changes through /sys/vfs/events, which tiku_backend_tiku.c reads:
 * a changed value becomes an ATTR notice, and a created, removed or moved
 * node has the folder listed again and diffed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_NOTIFY_H_
#define TIKU_NOTIFY_H_

#include "tiku_model.h"

/** @brief What happened to a node. */
typedef enum {
    TIKU_EV_CREATED = 0,
    TIKU_EV_REMOVED,
    TIKU_EV_MOVED,        /* renamed, or moved between directories    */
    TIKU_EV_CHANGED,      /* stat changed: size, mtime, permissions   */
    TIKU_EV_ATTR          /* a value or descriptor changed            */
} tiku_evop_t;

/** @brief Which facts a CHANGED or ATTR notice concerns (MA-072).  Zero
 *         means the notice cannot say, and everything the row shows is read
 *         again. */
#define TIKU_EVF_SIZE  0x01u
#define TIKU_EVF_DATE  0x02u
#define TIKU_EVF_KIND  0x04u   /* kind, type, icon-bearing facts   */
#define TIKU_EVF_VALUE 0x08u   /* value/meta: Value and attr columns */
#define TIKU_EVF_CAP   0x10u
#define TIKU_EVF_PERM  0x20u

/**
 * @brief One notice.
 *
 * It carries the whole model, so a parked notice can be replayed when its
 * row arrives.
 */
typedef struct {
    tiku_evop_t  op;
    char             path[TIKU_PATH_MAX];
    char             from[TIKU_PATH_MAX];  /* MOVED: the old path    */
    char             dir[TIKU_PATH_MAX];   /* the parent it concerns */
    tiku_model_t model;                    /* valid unless REMOVED   */
    int64_t          at;                       /* arrival, microseconds  */
    unsigned         fields;                   /* TIKU_EVF_*, or 0   */
} tiku_node_event_t;

/** @brief The most notices one batch holds. */
#define TIKU_EVENTS_MAX 128

/** @brief A batch of notices, in arrival order. */
typedef struct {
    tiku_node_event_t ev[TIKU_EVENTS_MAX];
    int              count;
    int              overflowed;   /* more happened than fits: re-list   */
} tiku_evlist_t;

/** @brief Empty @p l and clear its overflow mark. */
void tiku_evlist_clear(tiku_evlist_t *l);

/** @brief Append a notice.  @return 1 when it fit; a full list is marked
 *         overflowed instead. */
int tiku_evlist_add(tiku_evlist_t *l, tiku_evop_t op,
                        const tiku_model_t *m, const char *from,
                        int64_t at);

/** @brief Append a notice that names the facts it concerns, as
 *         TIKU_EVF_* bits (MA-072). */
int tiku_evlist_add_fields(tiku_evlist_t *l, tiku_evop_t op,
                               const tiku_model_t *m, const char *from,
                               int64_t at, unsigned fields);

/**
 * @brief Derive the notices between two listings of one directory.
 *
 * An entry in @p now and not in @p was is a creation; the reverse is a
 * removal; one whose facts moved is a change.  A removal and a creation that
 * share a node id in the same pass are one move, not two events, so the row
 * keeps its selection and its place.
 *
 * @return Notices produced.
 */
int tiku_notify_diff(const tiku_model_t *was, int nwas,
                         const tiku_model_t *now, int nnow,
                         int64_t at, tiku_evlist_t *out);

/**
 * @brief Collapse a burst of notices to one final notice per node.
 *
 * The newest model wins. CREATED survives updates but not REMOVED, and
 * MOVED keeps the first old path (PVL-094, PVN-054).
 */
int tiku_evlist_coalesce(const tiku_evlist_t *in,
                             tiku_evlist_t *out);

/*---------------------------------------------------------------------------*/
/* Parking                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief How long a parked notice waits for its row to appear, in
 *        microseconds.
 *
 * A change can arrive before the creation it belongs to.
 */
#define TIKU_PARK_LIFETIME_US 10000000

/** @brief Notices held for nodes the view does not show yet, at most 32. */
typedef struct {
    tiku_node_event_t ev[32];
    int              count;
} tiku_park_t;

/** @brief Drop every parked notice. */
void tiku_park_clear(tiku_park_t *p);

/** @brief Hold a notice for a node the view does not know yet; when all 32
 *         places are taken, the oldest notice gives way.  @return 1, or 0
 *         for a NULL argument. */
int tiku_park_add(tiku_park_t *p, const tiku_node_event_t *e);

/**
 * @brief Take back every parked notice for @p path, oldest first.
 *
 * Notices come out in arrival order, so the latest value is applied
 * last.
 *
 * @return How many were written to @p out.
 */
int tiku_park_take(tiku_park_t *p, const char *path,
                       tiku_node_event_t *out, int max);

/**
 * @brief Drop notices older than the lifetime.
 *
 * @return How many were discarded.
 */
int tiku_park_expire(tiku_park_t *p, int64_t now);

/** @brief How many notices are parked; 0 for NULL. */
int tiku_park_count(const tiku_park_t *p);

#endif /* TIKU_NOTIFY_H_ */
