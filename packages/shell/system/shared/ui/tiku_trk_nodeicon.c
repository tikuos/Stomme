/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_nodeicon.c - the icon lookup order, the one-time probe for a
 * node's own picture, and the node-icon cache.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_thumb.h"
#include "tiku_trk_nodeicon.h"
#include "tiku_trk_types.h"
#include <stdio.h>
#include <string.h>

void
tiku_trk_nodeicon_init(tiku_trk_nodeicon_t *c)
{
    if (c != NULL) {
        memset(c, 0, sizeof *c);
    }
}

int
tiku_trk_nodeicon_hint(tiku_store_t *store, const tiku_thumb_q_t *q,
                       tiku_model_t *m)
{
    char art[TIKU_NAME_MAX];

    if (m == NULL) {
        return TIKU_ICONHINT_NONE;
    }
    if (m->facts.icon_hint != TIKU_ICONHINT_UNASKED) {
        return m->facts.icon_hint;
    }
    /* A file that would get a thumbnail is hinted TIKU_ICONHINT_THUMB before
     * the thumbnail exists, so its first draw does not settle on the type
     * icon (IV-037). */
    if (q != NULL && tiku_thumb_wanted(q, m->path)) {
        m->facts.icon_hint = TIKU_ICONHINT_THUMB;
    } else if (store != NULL &&
               tiku_state_read(store, m->path, TIKU_TRK_ATTR_ICON, art,
                                   sizeof art) > 0) {
        m->facts.icon_hint = TIKU_ICONHINT_ATTR;
    } else {
        m->facts.icon_hint = TIKU_ICONHINT_NONE;
    }
    return m->facts.icon_hint;
}

int
tiku_trk_nodeicon_possible(const tiku_model_t *m)
{
    if (m == NULL) {
        return 0;
    }
    /* From the hint alone: an entry not yet asked answers 0, with no store
     * read, since the draw path calls this for every row. */
    return (m->facts.icon_hint == TIKU_ICONHINT_ATTR ||
            m->facts.icon_hint == TIKU_ICONHINT_THUMB);
}

/** @brief The cache entry for @p path, or NULL. */
static tiku_trk_nodeicon_ent_t *
find(tiku_trk_nodeicon_t *c, const char *path)
{
    int i;

    for (i = 0; i < c->n; i++) {
        if (c->e[i].used && strcmp(c->e[i].path, path) == 0) {
            return &c->e[i];
        }
    }
    return NULL;
}

/** @brief A free slot, or when the cache is full the first entry that is
 *         not permanent; NULL when every entry is. */
static tiku_trk_nodeicon_ent_t *
slot(tiku_trk_nodeicon_t *c)
{
    int i;

    for (i = 0; i < c->n; i++) {
        if (!c->e[i].used) {
            return &c->e[i];
        }
    }
    if (c->n < TIKU_TRK_NODEICON_MAX) {
        return &c->e[c->n++];
    }
    /* Full: the first entry that is not permanent is reused. */
    for (i = 0; i < c->n; i++) {
        if (!c->e[i].permanent) {
            return &c->e[i];
        }
    }
    return NULL;
}

tiku_trk_icon_from_t
tiku_trk_nodeicon_resolve(tiku_trk_nodeicon_t *c, tiku_store_t *store,
                          const tiku_model_t *m, char *out, size_t max)
{
    tiku_trk_nodeicon_ent_t *e;
    char art[TIKU_NAME_MAX];

    if (out != NULL && max > 0u) {
        out[0] = '\0';
    }
    if (c == NULL || m == NULL) {
        return TIKU_TRK_ICON_FROM_UNKNOWN;
    }
    if (m->facts.icon_hint == TIKU_ICONHINT_THUMB) {
        /* Nothing to read: the caller loads the picture.  It still gets the
         * type icon's name, which the row draws until the thumbnail exists
         * (IV-031). */
        if (out != NULL) {
            snprintf(out, max, "%s", tiku_model_icon_name(m));
        }
        return TIKU_TRK_ICON_FROM_THUMB;
    }
    e = find(c, m->path);
    if (e != NULL) {
        if (out != NULL) {
            snprintf(out, max, "%s", e->name);
        }
        return TIKU_TRK_ICON_FROM_NODE;
    }
    if (tiku_trk_nodeicon_possible(m) && store != NULL) {
        c->reads++;
        if (tiku_state_read(store, m->path, TIKU_TRK_ATTR_ICON, art,
                                sizeof art) > 0) {
            /* The entry's own icon wins over its type's (IV-001). */
            art[sizeof art - 1u] = '\0';
            e = slot(c);
            if (e != NULL) {
                memset(e, 0, sizeof *e);
                snprintf(e->path, sizeof e->path, "%s", m->path);
                snprintf(e->name, sizeof e->name, "%s", art);
                e->used = 1;
            }
            if (out != NULL) {
                snprintf(out, max, "%s", art);
            }
            return TIKU_TRK_ICON_FROM_NODE;
        }
    }
    if (m->type[0] != '\0') {
        int k;

        for (k = 0; k < c->miss_n; k++) {
            if (strcmp(c->miss[k], m->type) == 0) {
                /* A type remembered as having no art: the generic icon,
                 * without the database walk (IV-011). */
                if (out != NULL) {
                    snprintf(out, max, "%s", tiku_model_icon_name(m));
                }
                return TIKU_TRK_ICON_FROM_GENERIC;
            }
        }
    }
    if (m->type[0] != '\0' && c->types != NULL) {
        const tiku_trk_typerec_t *tr = tiku_trk_types_find(c->types,
                                                           m->type);

        if (tr != NULL && tr->icon[0] != '\0') {
            /* The type database's icon, which an installation can
             * change. */
            if (out != NULL) {
                snprintf(out, max, "%s", tr->icon);
            }
            return TIKU_TRK_ICON_FROM_TYPE;
        }
    }
    if (out != NULL) {
        snprintf(out, max, "%s", tiku_model_icon_name(m));
    }
    if (m->type[0] != '\0') {
        /* The type is the source only when its icon differs from an
         * untyped file's; otherwise the answer is the generic icon, which
         * a caller may share across every unregistered type. */
        tiku_model_t bare = *m;

        bare.type[0] = '\0';
        if (strcmp(tiku_model_icon_name(m),
                   tiku_model_icon_name(&bare)) != 0) {
            return TIKU_TRK_ICON_FROM_TYPE;
        }
        if (c->miss_n < TIKU_TRK_NODEICON_MISS_MAX) {
            /* No art anywhere for this type: remembered, so the next
             * lookup skips the walk (IV-011). */
            snprintf(c->miss[c->miss_n], TIKU_TYPE_MAX, "%s", m->type);
            c->miss_n++;
        }
    }
    return TIKU_TRK_ICON_FROM_GENERIC;
}

int
tiku_trk_nodeicon_misses(const tiku_trk_nodeicon_t *c)
{
    return (c != NULL) ? c->miss_n : 0;
}

void
tiku_trk_nodeicon_set_types(tiku_trk_nodeicon_t *c,
                            const struct tiku_trk_types *db)
{
    if (c != NULL) {
        c->types = db;
        /* The misses were answers from the old database. */
        c->miss_n = 0;
    }
}

void
tiku_trk_nodeicon_preload(tiku_trk_nodeicon_t *c, const char *path,
                          const char *name)
{
    tiku_trk_nodeicon_ent_t *e;

    if (c == NULL || path == NULL || name == NULL) {
        return;
    }
    e = find(c, path);
    if (e == NULL) {
        e = slot(c);
    }
    if (e == NULL) {
        return;
    }
    memset(e, 0, sizeof *e);
    snprintf(e->path, sizeof e->path, "%s", path);
    snprintf(e->name, sizeof e->name, "%s", name);
    e->permanent = 1;
    e->used = 1;
}

int
tiku_trk_nodeicon_changed(tiku_trk_nodeicon_t *c, const char *path)
{
    tiku_trk_nodeicon_ent_t *e;

    if (c == NULL || path == NULL) {
        return 0;
    }
    e = find(c, path);
    if (e == NULL) {
        return 0;
    }
    /* Dropped whether or not it is permanent: the icon changed
     * (IV-038). */
    memset(e, 0, sizeof *e);
    c->generation++;
    return 1;
}

int
tiku_trk_nodeicon_deleted(tiku_trk_nodeicon_t *c, const char *path)
{
    tiku_trk_nodeicon_ent_t *e;

    if (c == NULL || path == NULL) {
        return 0;
    }
    e = find(c, path);
    if (e == NULL || e->permanent) {
        /* A preloaded entry is kept (IV-041). */
        return 0;
    }
    memset(e, 0, sizeof *e);
    c->generation++;
    return 1;
}

int
tiku_trk_nodeicon_removing(tiku_trk_nodeicon_t *c, const char *path)
{
    tiku_trk_nodeicon_ent_t *e;

    if (c == NULL || path == NULL) {
        return 0;
    }
    e = find(c, path);
    if (e == NULL) {
        return 0;
    }
    /* Dropped whether or not it is permanent. */
    memset(e, 0, sizeof *e);
    c->generation++;
    return 1;
}

uint64_t
tiku_trk_nodeicon_generation(const tiku_trk_nodeicon_t *c)
{
    return c != NULL ? c->generation : 0;
}

void
tiku_trk_nodeicon_unhint(tiku_model_t *m)
{
    if (m != NULL) {
        m->facts.icon_hint = TIKU_ICONHINT_UNASKED;
    }
}

int
tiku_trk_nodeicon_attr_matters(const char *attr)
{
    if (attr == NULL) {
        return 0;
    }
    /* The icon attribute, the thumbnail, and the thumbnail's stamp, which
     * changes when the thumbnail is regenerated. */
    return (strcmp(attr, TIKU_TRK_ATTR_ICON) == 0 ||
            strcmp(attr, TIKU_ATTR_THUMB) == 0 ||
            strcmp(attr, TIKU_ATTR_THUMB_TIME) == 0);
}
