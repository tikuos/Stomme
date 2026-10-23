/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_leaf.c - the leaf menu, built from the namespace every
 * time it opens; its folder and recent lists; the calendar's date sums.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_deskbar_private.h"

/** @brief The last component of @p path; the path itself when it ends in
 *         a slash or has none. */
const char *
db_leaf_name(const char *path)
{
    const char *slash = strrchr(path, '/');

    return (slash != NULL && slash[1] != '\0') ? slash + 1 : path;
}

/** @brief Days in @p month of @p year, leap years counted. */
int
db_calendar_days(int year, int month)
{
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31,
                                 30, 31 };
    int leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));

    if (month == 2) {
        return days[1] + leap;
    }
    return (month >= 1 && month <= 12) ? days[month - 1] : 31;
}

/** @brief Move the calendar by months and years, the day clamped to fit. */
void
db_calendar_shift(tiku_trk_deskbar_t *db, int months, int years)
{
    int month;

    if (db == NULL) {
        return;
    }
    month = db->calendar_month + months + years * 12;
    while (month < 1) { month += 12; db->calendar_year--; }
    while (month > 12) { month -= 12; db->calendar_year++; }
    db->calendar_month = month;
    if (db->calendar_day > db_calendar_days(db->calendar_year, month)) {
        db->calendar_day = db_calendar_days(db->calendar_year, month);
    }
}

/** @brief Append a leaf-menu row; a full menu drops it and returns NULL. */
static tiku_trk_db_item_t *
add_item(tiku_trk_deskbar_t *db, tiku_trk_db_item_kind_t kind,
         const char *label, const char *path, const char *icon)
{
    tiku_trk_db_item_t *it;

    if (db->leaf_count >= TIKU_TRK_DB_LEAF_MAX) {
        return NULL;
    }
    it = &db->leaf[db->leaf_count++];
    memset(it, 0, sizeof *it);
    it->kind = kind;
    snprintf(it->label, sizeof it->label, "%.*s", (int)sizeof it->label - 1,
             (label != NULL) ? label : "");
    snprintf(it->path, sizeof it->path, "%s", (path != NULL) ? path : "");
    snprintf(it->icon, sizeof it->icon, "%s", (icon != NULL) ? icon : "");
    return it;
}

/** @brief Append a leaf-menu row that acts by @p command, with the icon
 *         named @p icon; a full menu drops it. */
static void
add_row(tiku_trk_deskbar_t *db, int command, const char *label,
        const char *path, const char *icon)
{
    tiku_trk_db_item_t *it = add_item(db, TIKU_TRK_DB_ITEM_ROW, label, path,
                                      icon);

    if (it != NULL) {
        it->command = command;
    }
}

/** @brief A row that opens the submenu @p expand builds for @p which. */
static void
add_expand(tiku_trk_deskbar_t *db, tiku_trk_db_expand_t expand, int which,
           const char *label, const char *path, const char *icon)
{
    tiku_trk_db_item_t *it = add_item(db, TIKU_TRK_DB_ITEM_EXPAND, label,
                                      path, icon);

    if (it != NULL) {
        it->expand = expand;
        it->which = which;
    }
}

/** @brief Make @p it the menu row for @p m: a folder expands to its
 *         entries, a file opens. */
void
db_fill_entry(tiku_trk_db_item_t *it, const tiku_model_t *m)
{
    memset(it, 0, sizeof *it);
    if (tiku_model_is_container(m)) {
        it->kind = TIKU_TRK_DB_ITEM_EXPAND;
        it->expand = TIKU_TRK_DB_EXPAND_DIR;
        snprintf(it->icon, sizeof it->icon, "folder");
    } else {
        it->kind = TIKU_TRK_DB_ITEM_ROW;
        it->command = TIKU_TRK_DB_OPEN_PATH;
        snprintf(it->icon, sizeof it->icon, "file");
    }
    snprintf(it->label, sizeof it->label, "%.*s", (int)sizeof it->label - 1,
             m->name);
    snprintf(it->path, sizeof it->path, "%s", m->path);
}

/** @brief One form under a board's gui/: a row that opens its panel. */
static int
db_form_entry(const tiku_model_t *m, void *ctx)
{
    tiku_trk_deskbar_t *db = ctx;
    char path[TIKU_PATH_MAX];

    if (m == NULL || m->name[0] == '.' || !tiku_model_is_container(m)) {
        return 0;
    }
    snprintf(path, sizeof path, "%.*s/panel", (int)sizeof path - 8, m->path);
    add_row(db, TIKU_TRK_DB_OPEN_PANEL, m->name, path, "");
    return 0;
}

/**
 * @brief The forms a board at @p prefix publishes, as rows after its
 *        own: a board with none adds nothing.
 */
static void
add_forms(tiku_trk_deskbar_t *db, const char *prefix)
{
    char dir[TIKU_PATH_MAX];

    if (db->backend == NULL || db->backend->ops->list == NULL) {
        return;
    }
    snprintf(dir, sizeof dir, "%s/gui", prefix);
    (void)db->backend->ops->list(db->backend, dir, db_form_entry, db);
}

/**
 * @brief The listing callback that makes a leaf-menu row of an entry: into
 *        the walk's own table when it has one, else onto the bar's leaf.
 *        A dot file is not a row.
 */
int
db_menu_entry(const tiku_model_t *m, void *ctx)
{
    struct menu_walk *w = ctx;

    if (m == NULL || w->n >= w->max || m->name[0] == '.') {
        return 0;               /* a dot file is not a menu entry */
    }
    if (w->into != NULL) {
        db_fill_entry(&w->into[w->n++], m);
    } else if (w->db->leaf_count < TIKU_TRK_DB_LEAF_MAX) {
        db_fill_entry(&w->db->leaf[w->db->leaf_count++], m);
        w->n++;
    }
    return 0;
}

/** @brief The Deskbar folder to read: the user's, or the system copy. */
static const char *
menu_dir_of(const tiku_trk_deskbar_t *db)
{
    tiku_model_t m;

    if (db->backend == NULL || db->backend->ops->stat == NULL) {
        return "";
    }
    if (db->menu_dir[0] != '\0' &&
        db->backend->ops->stat(db->backend, db->menu_dir, &m) == 0 &&
        tiku_model_is_container(&m)) {
        return db->menu_dir;
    }
    if (db->menu_dir_fallback[0] != '\0' &&
        db->backend->ops->stat(db->backend, db->menu_dir_fallback, &m) == 0 &&
        tiku_model_is_container(&m)) {
        /* No per-user folder: the menu shows the system copy (DB-003). */
        return db->menu_dir_fallback;
    }
    return "";
}

/** @brief Build the leaf menu from what is there now (DB-002, DB-005). */
void
db_build_leaf(tiku_trk_deskbar_t *db)
{
    static const char *const kRecentKey[TIKU_TRK_DB_RECENT_KINDS] = {
        "label.recent_documents", "label.recent_folders",
        "label.recent_applications"
    };
    const char *dir;
    int i;

    db->leaf_count = 0;
    db->sub_of = -1;
    db->sub_count = 0;
    db_reset_menu_hots(db);
    add_row(db, TIKU_TRK_DB_FIND, tiku_str("menu.find"), NULL, "query");
    add_row(db, TIKU_TRK_DB_FORCE_QUIT, tiku_str("menu.force_quit"), NULL,
            "force_quit");
    /* One Settings row, expanding to every preferences window. */
    add_expand(db, TIKU_TRK_DB_EXPAND_SETTINGS, 0, tiku_str("menu.settings"),
               NULL, "folder_system");
    {
        const struct { int group; const char *key; } groups[] = {
            {TIKU_TRK_DB_GROUP_DOCUMENTS, "menu.documents"},
            {TIKU_TRK_DB_GROUP_GRAPHICS, "menu.graphics"},
            {TIKU_TRK_DB_GROUP_UTILITIES, "menu.utilities"},
            {TIKU_TRK_DB_GROUP_DEVELOPMENT, "menu.development"},
            {TIKU_TRK_DB_GROUP_DEVICE_TOOLS, "menu.device_tools"},
            {TIKU_TRK_DB_GROUP_DEMOS, "menu.demos"}
        };
        /* The groups in the order the menu lists them, each with its
         * label key; the assertion fails when this table and the enum
         * differ in length. */
        _Static_assert(sizeof groups / sizeof groups[0] ==
                       TIKU_TRK_DB_GROUP_COUNT, "a group has no name");
        int counts[TIKU_TRK_DB_GROUP_COUNT] = {0};
        for (i = 0; i < db->app_count; i++) {
            int group = db->app[i].group;
            if (group >= 0 && group < TIKU_TRK_DB_GROUP_COUNT) counts[group]++;
        }
        for (i = 0; i < TIKU_TRK_DB_GROUP_COUNT; i++) {
            int group = groups[i].group;
            if (counts[group]) add_expand(db, TIKU_TRK_DB_EXPAND_APPS, group,
                tiku_str(groups[i].key), NULL,
                group == TIKU_TRK_DB_GROUP_DEVELOPMENT ? "folder_develop"
                                                   : "folder_apps");
        }
    }
    {
        /*
         * The stores that are attached, one row each, opening the place
         * they are mounted at, each followed by the forms its board
         * publishes.  The namespace is asked each time the menu is built,
         * so a mount that came or went shows in the next menu.
         */
        int slots = tiku_mount_slots(db->backend), any = 0;

        for (i = 0; i < slots; i++) {
            const char *prefix = tiku_mount_prefix_at(db->backend, i);
            const char *leaf;

            if (prefix == NULL ||
                (prefix[0] == '/' && prefix[1] == '\0')) {
                continue;       /* the root has no row of its own */
            }
            leaf = strrchr(prefix, '/');
            leaf = (leaf != NULL && leaf[1] != '\0') ? leaf + 1 : prefix;
            if (!any) {
                (void)add_item(db, TIKU_TRK_DB_ITEM_SEP, "-", NULL, NULL);
                any = 1;
            }
            add_row(db, TIKU_TRK_DB_OPEN_PATH, leaf, prefix, "folder");
            add_forms(db, prefix);
        }
    }
    {
        int any = 0;

        for (i = 0; i < TIKU_TRK_DB_RECENT_KINDS; i++) {
            if (db->recent_count[i] > 0 &&
                db->settings.recent_enabled[i] &&
                db->settings.recent_count[i] > 0) {
                if (!any) {
                    (void)add_item(db, TIKU_TRK_DB_ITEM_SEP, "-", NULL, NULL);
                    any = 1;
                }
                /* A list with nothing in it is not shown at all
                 * (DB-074). */
                add_expand(db, TIKU_TRK_DB_EXPAND_RECENT, i,
                           tiku_str(kRecentKey[i]), NULL,
                           (i == 1) ? "folder" : (i == 2) ? "application"
                                                          : "file");
            }
        }
    }
    {
        /*
         * The Deskbar folder's own items, under a separator that stays only
         * when something follows it: an empty folder adds no rows.  The
         * separator goes in before the walk adds the items, and rolling
         * leaf_count back takes it out again when the walk added none.
         */
        int before = db->leaf_count;

        (void)add_item(db, TIKU_TRK_DB_ITEM_SEP, "-", NULL, NULL);
        dir = menu_dir_of(db);
        if (dir[0] != '\0' && db->backend->ops->list != NULL) {
            struct menu_walk w;

            w.db = db;
            w.n = 0;
            w.into = NULL;
            /* The walk stops two rows short of TIKU_TRK_DB_LEAF_MAX, for
             * the separator and Shut down added after it. */
            w.max = TIKU_TRK_DB_LEAF_MAX - 2 - db->leaf_count;
            if (w.max < 0) {
                w.max = 0;
            }
            (void)db->backend->ops->list(db->backend, dir, db_menu_entry, &w);
        }
        if (db->leaf_count == before + 1) {
            db->leaf_count = before;
        }
    }
    (void)add_item(db, TIKU_TRK_DB_ITEM_SEP, "-", NULL, NULL);
    add_row(db, TIKU_TRK_DB_QUIT, tiku_str("menu.shut_down"), NULL,
            "shutdown");
}

void
tiku_trk_deskbar_set_menu_dir(tiku_trk_deskbar_t *db, const char *user,
                              const char *fallback)
{
    char u[TIKU_PATH_MAX], fb[TIKU_PATH_MAX];

    if (db == NULL) {
        return;
    }
    /* Copied through the stack: a caller may hand back the strings this
     * bar reported, and an snprintf onto its own source is undefined. */
    snprintf(u, sizeof u, "%s", (user != NULL) ? user : "");
    snprintf(fb, sizeof fb, "%s", (fallback != NULL) ? fallback : "");
    memcpy(db->menu_dir, u, sizeof db->menu_dir);
    memcpy(db->menu_dir_fallback, fb, sizeof db->menu_dir_fallback);
}

const char *
tiku_trk_deskbar_menu_dir(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? menu_dir_of(db) : "";
}

const char *
tiku_trk_deskbar_user_menu_dir(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->menu_dir : "";
}

void
tiku_trk_deskbar_set_recent(tiku_trk_deskbar_t *db,
                            tiku_trk_db_recent_t which,
                            const char paths[][TIKU_PATH_MAX], int n)
{
    int i;

    if (db == NULL || (int)which < 0 ||
        (int)which >= TIKU_TRK_DB_RECENT_KINDS) {
        return;
    }
    if (n > TIKU_TRK_DB_RECENT_MAX) {
        n = TIKU_TRK_DB_RECENT_MAX;
    }
    for (i = 0; i < n && paths != NULL; i++) {
        snprintf(db->recent[which][i], TIKU_PATH_MAX, "%s", paths[i]);
    }
    db->recent_count[which] = (paths != NULL && n > 0) ? n : 0;
}

int
tiku_trk_deskbar_leaf_count(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->leaf_count : 0;
}

const tiku_trk_db_item_t *
tiku_trk_deskbar_leaf_item(const tiku_trk_deskbar_t *db, int i)
{
    if (db == NULL || i < 0 || i >= db->leaf_count) {
        return NULL;
    }
    return &db->leaf[i];
}
