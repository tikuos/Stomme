/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_replicants.c - the tray: replicants added, synced, stored
 * and removed, and the applications' own tray icons.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_deskbar_private.h"

/**
 * @brief Store each replicant's label, path and height on one line.
 */
static void
save_replicants(tiku_trk_deskbar_t *db)
{
    char data[TIKU_TRK_DB_TRAY_MAX * (TIKU_PATH_MAX + 20)];
    size_t at = 0;
    int i;

    if (db == NULL || db->store == NULL ||
        !tiku_store_writable(db->store)) {
        return;
    }
    data[0] = '\0';
    for (i = 0; i < db->tray_count; i++) {
        int n = snprintf(data + at, sizeof data - at, "%s\t%s\t%d\n",
                         db->tray[i].label, db->tray[i].path,
                         db->tray[i].height);
        if (n < 0 || (size_t)n >= sizeof data - at) {
            break;
        }
        at += (size_t)n;
    }
    (void)tiku_state_write(db->store, TIKU_KEY_SESSION,
                               TIKU_ATTR_REPLICANTS, data, at);
    (void)tiku_store_flush(db->store);
}

int
tiku_trk_deskbar_add_replicant(tiku_trk_deskbar_t *db, const char *label,
                               const char *path)
{
    tiku_trk_replicant_t *r;

    if (db == NULL || label == NULL || path == NULL ||
        db->tray_count >= TIKU_TRK_DB_TRAY_MAX) {
        return -1;
    }
    for (int i = 0; i < db->tray_count; i++) {
        if (strcmp(db->tray[i].path, path) == 0) {
            return db->tray[i].id;         /* already subscribed */
        }
        if (strncmp(db->tray[i].label, label,
                    sizeof db->tray[i].label - 1u) == 0) {
            /* A different node under a label already taken is refused
             * (DB-038). */
            return -1;
        }
    }
    r = &db->tray[db->tray_count++];
    r->id = ++db->next_replicant;
    snprintf(r->label, sizeof r->label, "%s", label);
    snprintf(r->path, sizeof r->path, "%s", path);
    r->valid = 0;
    r->height = TIKU_TRK_DB_MAX_REPLICANT_H;
    /* The node's description is read once, here; the tray formats each
     * reading by it, as "16h 26m" for 59182 seconds. */
    if (db->backend != NULL && db->backend->ops->stat != NULL) {
        tiku_model_t m;

        if (db->backend->ops->stat(db->backend, path, &m) == 0) {
            r->desc_known = tiku_desc_parse(m.facts.meta, &r->desc);
            r->node_id = m.node_id;
        }
    }
    save_replicants(db);
    return r->id;
}

int
tiku_trk_deskbar_sync_replicants(tiku_trk_deskbar_t *db)
{
    int i, changed = 0;

    if (db == NULL || db->backend == NULL || db->backend->ops->stat == NULL) {
        return 0;
    }
    for (i = db->tray_count - 1; i >= 0; i--) {
        tiku_trk_replicant_t *r = &db->tray[i];
        tiku_model_t m;
        char moved[TIKU_PATH_MAX];

        if (db->backend->ops->stat(db->backend, r->path, &m) == 0) {
            continue;                   /* still where it was */
        }
        if (r->node_id != 0 && db->backend->ops->path_of_id != NULL &&
            db->backend->ops->path_of_id(db->backend, r->node_id, moved,
                                         sizeof moved) == 0) {
            /* Moved: re-pointed, keeping its place and its label
             * (DB-040). */
            snprintf(r->path, sizeof r->path, "%s", moved);
            changed = 1;
            continue;
        }
        (void)tiku_trk_deskbar_drop_replicant(db, r->id);
        changed = 1;
    }
    if (changed) {
        save_replicants(db);
    }
    return changed;
}

int
tiku_trk_deskbar_replicant_by_id(const tiku_trk_deskbar_t *db, int id)
{
    int i;

    if (db == NULL) {
        return -1;
    }
    for (i = 0; i < db->tray_count; i++) {
        if (db->tray[i].id == id) {
            return i;
        }
    }
    return -1;
}

int
tiku_trk_deskbar_replicant_by_name(const tiku_trk_deskbar_t *db,
                                   const char *name)
{
    int i;

    if (db == NULL || name == NULL) {
        return -1;
    }
    for (i = 0; i < db->tray_count; i++) {
        if (strcmp(db->tray[i].label, name) == 0) {
            return i;
        }
    }
    return -1;
}

int
tiku_trk_deskbar_drop_replicant(tiku_trk_deskbar_t *db, int id)
{
    int at = tiku_trk_deskbar_replicant_by_id(db, id);
    int i;

    if (at < 0) {
        return 0;
    }
    for (i = at; i < db->tray_count - 1; i++) {
        db->tray[i] = db->tray[i + 1];
    }
    db->tray_count--;
    save_replicants(db);
    return 1;
}

int
tiku_trk_deskbar_set_replicant_height(tiku_trk_deskbar_t *db,
                                      const char *path, int height)
{
    int i;

    if (db == NULL || path == NULL || height <= 0 ||
        height > TIKU_TRK_DB_MAX_REPLICANT_H) {
        return 0;
    }
    for (i = 0; i < db->tray_count; i++) {
        if (strcmp(db->tray[i].path, path) == 0) {
            if (db->tray[i].height == height) {
                return 1;
            }
            db->tray[i].height = height;
            /* Every row keeps the TRAY_ROW pitch, so a new height moves
             * nothing on screen; the tray is saved at once. */
            save_replicants(db);
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_deskbar_replicant_height(const tiku_trk_deskbar_t *db,
                                  const char *path)
{
    int i;

    for (i = 0; db != NULL && path != NULL && i < db->tray_count; i++) {
        if (strcmp(db->tray[i].path, path) == 0) {
            return db->tray[i].height;
        }
    }
    return 0;
}

void
tiku_trk_deskbar_set_show_replicants(tiku_trk_deskbar_t *db, int show)
{
    if (db != NULL) {
        db->settings.show_replicants = !!show;
    }
}

int
tiku_trk_deskbar_show_replicants(const tiku_trk_deskbar_t *db)
{
    return db != NULL ? db->settings.show_replicants : 0;
}

void
tiku_trk_deskbar_set_store(tiku_trk_deskbar_t *db, tiku_store_t *store)
{
    if (db != NULL) {
        db->store = store;
    }
}

int
tiku_trk_deskbar_load_replicants(tiku_trk_deskbar_t *db)
{
    char data[TIKU_TRK_DB_TRAY_MAX * (TIKU_PATH_MAX + 20)];
    int n, loaded = 0;
    char *line;

    if (db == NULL || db->store == NULL) {
        return 0;
    }
    n = tiku_state_read(db->store, TIKU_KEY_SESSION,
                            TIKU_ATTR_REPLICANTS, data, sizeof data - 1u);
    if (n <= 0) {
        return 0;
    }
    data[n] = '\0';
    line = data;
    while (line != NULL && *line != '\0') {
        char *next = strchr(line, '\n');
        char *tab;

        if (next != NULL) { *next++ = '\0'; }
        tab = strchr(line, '\t');
        if (tab != NULL) {
            char *height;
            *tab++ = '\0';
            height = strchr(tab, '\t');
            if (height != NULL) *height++ = '\0';
            if (tab[0] != '\0' && db->backend != NULL &&
                db->backend->ops->stat != NULL &&
                db->backend->ops->stat(db->backend, tab,
                                                          &(tiku_model_t){0}) == 0 &&
                tiku_trk_deskbar_add_replicant(db, line, tab) >= 0) {
                if (height != NULL) {
                    (void)tiku_trk_deskbar_set_replicant_height(db, tab,
                                                               atoi(height));
                }
                loaded++;
            }
        }
        line = next;
    }
    save_replicants(db); /* prune stale entries and normalise the record */
    return loaded;
}

int
tiku_trk_deskbar_remove_replicant(tiku_trk_deskbar_t *db, const char *path)
{
    int i;

    if (db == NULL || path == NULL) { return 0; }
    for (i = 0; i < db->tray_count; i++) {
        if (strcmp(db->tray[i].path, path) == 0) {
            memmove(&db->tray[i], &db->tray[i + 1],
                    (size_t)(db->tray_count - i - 1) * sizeof db->tray[0]);
            db->tray_count--;
            save_replicants(db);
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_deskbar_repoint_replicant(tiku_trk_deskbar_t *db,
                                   const char *old_path,
                                   const char *new_path)
{
    int i;

    if (db == NULL || old_path == NULL || new_path == NULL) { return 0; }
    for (i = 0; i < db->tray_count; i++) {
        if (strcmp(db->tray[i].path, old_path) == 0) {
            snprintf(db->tray[i].path, sizeof db->tray[i].path, "%s", new_path);
            save_replicants(db);
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_deskbar_refresh(tiku_trk_deskbar_t *db)
{
    return tiku_trk_deskbar_refresh_except(db, NULL);
}

int
tiku_trk_deskbar_refresh_except(tiku_trk_deskbar_t *db, const char *except)
{
    int i, changed = 0;

    if (db == NULL || db->backend == NULL) {
        return 0;
    }
    for (i = 0; i < db->tray_count; i++) {
        tiku_trk_replicant_t *r = &db->tray[i];
        char buf[64];
        int n;
        char *nl;

        if (except != NULL && except[0] != '\0' &&
            strncmp(r->path, except, strlen(except)) == 0) {
            continue;           /* read by the caller */
        }
        n = db->backend->ops->read(db->backend, r->path, buf,
                                   sizeof buf - 1);
        if (n < 0) {
            changed += tiku_trk_deskbar_remove_replicant(db, r->path);
            i--;
            continue;
        }
        buf[n] = '\0';
        nl = strchr(buf, '\n');
        if (nl != NULL) { *nl = '\0'; }
        buf[sizeof r->value - 1u] = '\0';
        if (strcmp(r->value, buf) != 0) {
            memcpy(r->value, buf, strlen(buf) + 1u);
            changed++;
        }
        r->valid = 1;
    }
    return changed;
}

int
tiku_trk_deskbar_replicant_paths(const tiku_trk_deskbar_t *db,
                                 char out[][TIKU_PATH_MAX], int max)
{
    int i, n = 0;

    if (db == NULL) {
        return 0;
    }
    for (i = 0; i < db->tray_count && n < max; i++) {
        snprintf(out[n++], TIKU_PATH_MAX, "%s", db->tray[i].path);
    }
    return n;
}

int
tiku_trk_deskbar_replicant_set(tiku_trk_deskbar_t *db, const char *path,
                               const char *value)
{
    int i;

    if (db == NULL || path == NULL) {
        return 0;
    }
    if (value == NULL) {
        return tiku_trk_deskbar_remove_replicant(db, path);
    }
    for (i = 0; i < db->tray_count; i++) {
        tiku_trk_replicant_t *r = &db->tray[i];

        if (strcmp(r->path, path) == 0) {
            int changed = strcmp(r->value, value) != 0;

            snprintf(r->value, sizeof r->value, "%s", value);
            r->valid = 1;
            return changed;
        }
    }
    return 0;
}

/* ---------------------------------------------- applications' tray icons */

/** @brief The item @p owner calls @p id, or -1. */
static int
trayicon_find(const tiku_trk_deskbar_t *db, uint64_t owner, const char *id)
{
    for (int i = 0; i < db->trayicon_count; i++) {
        if (db->trayicons[i].owner == owner &&
            strcmp(db->trayicons[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

int
tiku_trk_deskbar_trayicon_set(tiku_trk_deskbar_t *db, uint64_t owner,
                              const char *id, const char *icon,
                              const char *text, const char *tip)
{
    tiku_trk_trayicon_t t;
    int i;

    if (db == NULL || id == NULL || id[0] == '\0' ||
        strlen(id) >= sizeof t.id) {
        return -1;
    }
    if ((icon == NULL || icon[0] == '\0') && (text == NULL ||
                                               text[0] == '\0')) {
        return tiku_trk_deskbar_trayicon_remove(db, owner, id);
    }
    memset(&t, 0, sizeof t);
    t.owner = owner;
    snprintf(t.id, sizeof t.id, "%s", id);
    snprintf(t.icon, sizeof t.icon, "%s", icon != NULL ? icon : "");
    snprintf(t.text, sizeof t.text, "%s", text != NULL ? text : "");
    snprintf(t.tip, sizeof t.tip, "%s", tip != NULL ? tip : "");
    i = trayicon_find(db, owner, id);
    if (i >= 0) {
        if (memcmp(&db->trayicons[i], &t, sizeof t) == 0) {
            return 0;               /* the same item again */
        }
        db->trayicons[i] = t;
        return 1;
    }
    if (db->trayicon_count >= TIKU_TRK_DB_TRAYICONS_MAX) {
        return -1;
    }
    db->trayicons[db->trayicon_count++] = t;
    return 1;
}

int
tiku_trk_deskbar_trayicon_remove(tiku_trk_deskbar_t *db, uint64_t owner,
                                 const char *id)
{
    int i = (db != NULL && id != NULL) ? trayicon_find(db, owner, id) : -1;

    if (i < 0) {
        return 0;
    }
    memmove(&db->trayicons[i], &db->trayicons[i + 1],
            (size_t)(db->trayicon_count - i - 1) * sizeof db->trayicons[0]);
    db->trayicon_count--;
    db->trayicon_hot = -1;
    return 1;
}

int
tiku_trk_deskbar_trayicon_keep(tiku_trk_deskbar_t *db,
                               const uint64_t *owners, int n)
{
    int gone = 0;

    for (int i = (db != NULL) ? db->trayicon_count - 1 : -1; i >= 0; i--) {
        int k;

        for (k = 0; k < n && owners[k] != db->trayicons[i].owner; k++) {}
        if (k == n) {
            gone |= tiku_trk_deskbar_trayicon_remove(db,
                        db->trayicons[i].owner, db->trayicons[i].id);
        }
    }
    return gone;
}

int
tiku_trk_deskbar_trayicons(const tiku_trk_deskbar_t *db, int i,
                           tiku_trk_trayicon_t *out)
{
    if (db == NULL) {
        return 0;
    }
    if (out != NULL && i >= 0 && i < db->trayicon_count) {
        *out = db->trayicons[i];
    }
    return db->trayicon_count;
}
