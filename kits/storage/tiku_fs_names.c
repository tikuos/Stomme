/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fs_names.c - names and paths: what a name may hold, paths joined
 * and split, whether a name is taken, the first free name for a duplicate
 * or a link, and renaming an entry.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_fs_private.h"

/** @brief The characters a name may not contain. */
#define NAME_DENY "/"

const char *
tiku_fs_name_deny(void)
{
    return NAME_DENY;
}

int
tiku_fs_name_ok(const char *name, char *err, size_t errmax)
{
    if (name == NULL || name[0] == '\0') {
        if (err != NULL) { snprintf(err, errmax, "a name cannot be empty"); }
        return 0;
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        if (err != NULL) { snprintf(err, errmax, "'%s' is not a usable name",
                                    name); }
        return 0;
    }
    if (strpbrk(name, NAME_DENY) != NULL) {
        if (err != NULL) {
            snprintf(err, errmax, "a name cannot contain '/'");
        }
        return 0;
    }
    return 1;
}

/** @brief Join a directory and a leaf name; -1 rather than truncate. */
int
fop_join(char *out, size_t max, const char *dir, const char *name)
{
    size_t dl = strlen(dir), nl = strlen(name);
    int root = (dir[0] == '/' && dir[1] == '\0');

    if (dl + nl + 2u > max) {
        return -1;
    }
    if (root) {
        out[0] = '/';
        memcpy(out + 1, name, nl + 1u);
    } else {
        memcpy(out, dir, dl);
        out[dl] = '/';
        memcpy(out + dl + 1u, name, nl + 1u);
    }
    return 0;
}

/** @brief Whether an entry exists at @p dir / @p name; a name too long to
 *         join counts as taken. */
int
fop_exists(tiku_fs_t *fs, const char *dir, const char *name)
{
    char path[TIKU_PATH_MAX];
    tiku_model_t m;

    if (fop_join(path, sizeof path, dir, name) != 0) {
        return 1;                    /* unusable: treat as taken */
    }
    return (fs->backend->ops->stat(fs->backend, path, &m) == 0);
}

/** @brief The directory part of a path, into @p out. */
void
fop_parent_of(const char *path, char *out, size_t max)
{
    const char *slash = strrchr(path, '/');
    size_t n;

    if (slash == NULL || slash == path) {
        snprintf(out, max, "/");
        return;
    }
    n = (size_t)(slash - path);
    if (n >= max) { n = max - 1u; }
    memcpy(out, path, n);
    out[n] = '\0';
}

/** @brief The last component of @p path, or @p path when it has none. */
const char *
fop_leaf_of(const char *path)
{
    const char *slash = strrchr(path, '/');

    return (slash != NULL && slash[1] != '\0') ? slash + 1 : path;
}

/** @brief Whether a whole component of @p path is TIKU_TRASH_NAME: the
 *         path is a Trash or lies inside one. */
int
fop_under_trash(const char *path)
{
    const char *p = path;
    size_t n = strlen(TIKU_TRASH_NAME);

    while (p != NULL && (p = strstr(p, "/" TIKU_TRASH_NAME)) != NULL) {
        p += 1u + n;
        if (*p == '\0' || *p == '/') {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Split "<base> <word> N" into its base and the number.
 *
 * A name that already ends in the suffix is recognised, so "x copy"
 * duplicates to "x copy 2", not "x copy copy" (FS-006).
 *
 * @return N, 1 for a bare suffix, or 0 when @p name has no suffix.
 */
static int
split_suffix(const char *name, const char *word, char *base, size_t max)
{
    size_t nl = strlen(name), wl = strlen(word);
    const char *p;
    int n = 0;

    snprintf(base, max, "%s", name);
    if (nl <= wl + 1u) {
        return 0;
    }
    /* "<base> <word>" or "<base> <word> <n>" */
    p = name + nl;
    while (p > name && p[-1] >= '0' && p[-1] <= '9') {
        p--;
    }
    if (p < name + nl) {
        n = atoi(p);
        if (p > name && p[-1] == ' ') {
            p--;
        }
    }
    if ((size_t)(p - name) > wl && strncmp(p - wl, word, wl) == 0 &&
        p - wl - 1 >= name && p[-wl - 1] == ' ') {
        size_t bl = (size_t)(p - wl - 1 - name);
        if (bl >= max) { bl = max - 1u; }
        memcpy(base, name, bl);
        base[bl] = '\0';
        return (n > 0) ? n : 1;
    }
    return 0;
}

/** @brief First free "<base> <word> [n]" in @p dir. */
static int
free_suffixed_name(tiku_fs_t *fs, const char *dir, const char *name,
                   const char *word, int plain_first, char *out, size_t max)
{
    char base[TIKU_NAME_MAX];
    int n, i;

    n = split_suffix(name, word, base, sizeof base);
    if (plain_first && n == 0 && !fop_exists(fs, dir, name)) {
        snprintf(out, max, "%s", name);   /* the plain name is free */
        return 0;
    }
    if (n == 0) {
        snprintf(out, max, "%s %s", base, word);
        if (!fop_exists(fs, dir, out)) {
            return 0;
        }
        n = 1;
    }
    for (i = (n > 1) ? n : 2; i < 10000; i++) {
        snprintf(out, max, "%s %s %d", base, word, i);
        if (!fop_exists(fs, dir, out)) {
            return 0;
        }
    }
    return -1;
}

/** @brief The first free "<name> copy [n]" in @p dir. */
int
fop_fs_duplicate_name(tiku_fs_t *fs, const char *dir,
                           const char *name, char *out, size_t max)
{
    /* Duplicate never reuses the plain name: a duplicate is always "copy". */
    return free_suffixed_name(fs, dir, name, "copy", 0, out, max);
}

/** @brief @p name when it is free in @p dir, else the first free
 *         "<name> link [n]". */
int
fop_fs_link_name(tiku_fs_t *fs, const char *dir, const char *name,
                      char *out, size_t max)
{
    /* A link keeps the original name when the destination allows it. */
    return free_suffixed_name(fs, dir, name, "link", 1, out, max);
}

/** @brief Rename one entry; see tiku_fs_rename(). */
int
fop_fs_rename(tiku_fs_t *fs, const char *path, const char *new_name,
                   char *err, size_t errmax)
{
    char dir[TIKU_PATH_MAX], dst[TIKU_PATH_MAX];

    if (fs == NULL || path == NULL || new_name == NULL) {
        return -1;
    }
    if (!tiku_fs_name_ok(new_name, err, errmax)) {
        return -1;
    }
    if (!fop_on_device(fs, path)) {
        tiku_volumes_t vs;
        char why[200];

        if (tiku_volumes_scan(&vs) > 0) {
            /* A volume's name is its filesystem's label, which nothing
             * here can write; renaming the mount point's entry would leave
             * the label as it was.  Refused (MA-052). */
            if (tiku_volume_is_root(&vs, path)) {
                if (err != NULL) {
                    snprintf(err, errmax,
                             "\"%.40s\" is a disk: its name is the disk's "
                             "own, which cannot be changed from here.",
                             fop_leaf_of(path));
                }
                return -1;
            }
            /* The volume must take writes; its own refusal is the
             * message. */
            if (!tiku_volume_may_write(&vs, path, 1, why, sizeof why)) {
                if (err != NULL) {
                    snprintf(err, errmax, "%s", why);
                }
                return -1;
            }
        }
    }
    {
        /* The Trash and the Desktop are found by their names: renaming
         * either strands every rule that looks there (MA-051). */
        tiku_model_t m;

        if (fs->backend->ops->stat(fs->backend, path, &m) == 0 &&
            (m.kind == TIKU_KIND_TRASH ||
             m.kind == TIKU_KIND_DESKTOP)) {
            if (err != NULL) {
                snprintf(err, errmax,
                         "The %s cannot be renamed: it is found by its "
                         "name.", (m.kind == TIKU_KIND_TRASH)
                                      ? "Trash" : "Desktop");
            }
            return -1;
        }
    }
    if (strlen(new_name) > 255u) {
        /* 255 is NAME_MAX on every host this builds on (FS-080). */
        if (err != NULL) {
            snprintf(err, errmax, "The entered name is too long.");
        }
        return -1;
    }
    fop_parent_of(path, dir, sizeof dir);
    if (fop_exists(fs, dir, new_name)) {
        if (err != NULL) {
            snprintf(err, errmax, "'%s' already exists here", new_name);
        }
        return -1;
    }
    if (fop_join(dst, sizeof dst, dir, new_name) != 0) {
        return -1;
    }
    if (rename(path, dst) != 0) {
        if (err != NULL) {
            snprintf(err, errmax, "cannot rename (%s)", strerror(errno));
        }
        return -1;
    }
    fop_undo_reset(fs, TIKU_OP_RENAME);
    fop_undo_push(fs, dst, path, 0);
    fop_landed(fs, path, dst);
    return 0;
}
