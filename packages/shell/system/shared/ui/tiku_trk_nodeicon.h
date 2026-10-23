/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_nodeicon.h - where an entry's icon comes from, and the cache of
 * node icons.
 *
 * Three sources, in order: the node's own picture (an icon attribute or a
 * thumbnail), its type's, and the generic one for its kind.  Whether a node
 * has a picture of its own is asked once, when the entry is taken in, and
 * kept on the model as its icon hint; the draw path reads the store only
 * for entries whose hint says yes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_NODEICON_H_
#define TIKU_TRK_NODEICON_H_

#include "tiku_model.h"
#include "tiku_state.h"
#include "tiku_thumb.h"

/** @brief Where the icon being drawn came from. */
typedef enum {
    TIKU_TRK_ICON_FROM_UNKNOWN = 0,
    TIKU_TRK_ICON_FROM_NODE,        /* the entry's own icon attribute     */
    /* The node's thumbnail: a bitmap the caller loads, where the other
     * sources name art in the art set. */
    TIKU_TRK_ICON_FROM_THUMB,
    TIKU_TRK_ICON_FROM_TYPE,        /* its type's                         */
    TIKU_TRK_ICON_FROM_GENERIC      /* the fallback for its kind          */
} tiku_trk_icon_from_t;

/** @brief The attribute an entry's own icon lives in. */
#define TIKU_TRK_ATTR_ICON "_trk/icon"

/** @brief How many types without art of their own are remembered. */
#define TIKU_TRK_NODEICON_MISS_MAX 8

/** @brief How many node icons are remembered at once. */
#define TIKU_TRK_NODEICON_MAX 64

struct tiku_trk_types;

/** @brief One remembered node icon. */
typedef struct {
    char     path[TIKU_PATH_MAX];   /* the entry it belongs to        */
    char     name[TIKU_NAME_MAX];   /* the art it resolved to         */
    int      permanent;                 /* preloaded; never evicted       */
    int      used;                      /* the slot holds an entry        */
} tiku_trk_nodeicon_ent_t;

/** @brief The node-icon cache. */
typedef struct {
    tiku_trk_nodeicon_ent_t e[TIKU_TRK_NODEICON_MAX];
    int                     n;          /* e[] slots ever used            */
    int                     reads;      /* store reads resolve has made   */
    uint64_t                generation; /* bumped when cached art goes    */
    const struct tiku_trk_types *types;   /* where a type's art is
                                          * declared, or NULL       */
    /* Types known to have no art of their own, so resolve skips the
     * database walk for them (IV-011). */
    char                    miss[TIKU_TRK_NODEICON_MISS_MAX]
                                [TIKU_TYPE_MAX];
    int                     miss_n;     /* miss[] in use                  */
} tiku_trk_nodeicon_t;

/** @brief How many types the negative cache remembers. */
int tiku_trk_nodeicon_misses(const tiku_trk_nodeicon_t *c);

/** @brief An empty cache. */
void tiku_trk_nodeicon_init(tiku_trk_nodeicon_t *c);

/**
 * @brief Ask, once, whether @p m has a picture of its own (IV-005, IV-037).
 *
 * The probe: whether a thumbnail is wanted for it, else whether it has an
 * icon attribute.  A model already asked returns its stored hint without a
 * probe.
 *
 * @param q  The thumbnail queue, for the "generate thumbnails" setting;
 *           NULL means no thumbnails, and only the attribute is looked for.
 * @return the resulting TIKU_TRK_ICONHINT_*.
 */
int tiku_trk_nodeicon_hint(tiku_store_t *store,
                           const tiku_thumb_q_t *q, tiku_model_t *m);

/**
 * @brief Whether @p m could have an icon of its own at all (IV-005).
 *
 * Answered from the hint the model carries, without a store read; an entry
 * not yet asked answers 0.
 */
int tiku_trk_nodeicon_possible(const tiku_model_t *m);

/**
 * @brief The icon to draw for @p m, and where it came from.
 *
 * @param out  Receives the art's name.  A thumbnail gets one too -- the
 *             fallback its row draws until the picture has been made.
 * @return where the icon came from.
 */
tiku_trk_icon_from_t tiku_trk_nodeicon_resolve(tiku_trk_nodeicon_t *c,
                                               tiku_store_t *store,
                                               const tiku_model_t *m,
                                               char *out, size_t max);

/**
 * @brief Let the cache consult the type database for a type's icon.
 *
 * The database's icon for a type is used ahead of the compiled table, so a
 * type the user gave an icon wears it (AW-013, IV-011).  Setting it forgets
 * the remembered misses.
 */
void tiku_trk_nodeicon_set_types(tiku_trk_nodeicon_t *c,
                                 const struct tiku_trk_types *db);

/**
 * @brief Remember @p path's icon as permanent: eviction and a deletion
 *        notice leave it (IV-041).
 */
void tiku_trk_nodeicon_preload(tiku_trk_nodeicon_t *c, const char *path,
                               const char *name);

/**
 * @brief Forget @p path's icon, preloaded or not, because the entry
 *        changed (IV-038).
 *
 * @return 1 when something was dropped.
 */
int tiku_trk_nodeicon_changed(tiku_trk_nodeicon_t *c, const char *path);
/** @brief Generation incremented whenever cached art is invalidated. */
uint64_t tiku_trk_nodeicon_generation(const tiku_trk_nodeicon_t *c);

/**
 * @brief Forget @p path's icon because the entry went (IV-041).
 *
 * A preloaded entry is kept.
 *
 * @return 1 when something was dropped.
 */
int tiku_trk_nodeicon_deleted(tiku_trk_nodeicon_t *c, const char *path);

/**
 * @brief Forget @p path's icon, preloaded or not, because the file it was
 *        preloaded from has gone (IV-041).
 *
 * tiku_trk_nodeicon_deleted() keeps a preloaded entry; this does not.
 *
 * @return 1 when something was dropped.
 */
int tiku_trk_nodeicon_removing(tiku_trk_nodeicon_t *c, const char *path);

/** @brief Whether an attribute name is one that changes an icon (IV-038). */
int tiku_trk_nodeicon_attr_matters(const char *attr);

/**
 * @brief Reset @p m's icon hint to unasked after such a change (IV-038).
 *
 * The next tiku_trk_nodeicon_hint() then probes again, so a newly given
 * icon is found.
 */
void tiku_trk_nodeicon_unhint(tiku_model_t *m);

#endif /* TIKU_TRK_NODEICON_H_ */
