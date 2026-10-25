/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_types.c - the type database, as a file.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_trk_types.h"
#include <stdio.h>
#include <string.h>

void
tiku_trk_types_init(tiku_trk_types_t *db)
{
    if (db != NULL) {
        memset(db, 0, sizeof *db);
    }
}

int
tiku_trk_types_column(const char *name)
{
    static const struct { const char *name; int col; } kCols[] = {
        { "name",        TIKU_TRK_COL_NAME },
        { "kind",        TIKU_TRK_COL_KIND },
        { "value",       TIKU_TRK_COL_VALUE },
        { "size",        TIKU_TRK_COL_SIZE },
        { "cap",         TIKU_TRK_COL_CAP },
        { "modified",    TIKU_TRK_COL_DATE },
        { "location",    TIKU_TRK_COL_PATH },
        { "permissions", TIKU_TRK_COL_PERM },
        { "original_path", TIKU_TRK_COL_ORIGINAL }
    };
    size_t i;

    for (i = 0; name != NULL && i < sizeof kCols / sizeof kCols[0]; i++) {
        if (strcmp(kCols[i].name, name) == 0) {
            return kCols[i].col;
        }
    }
    return -1;
}

int
tiku_trk_types_add(tiku_trk_types_t *db, const char *type, const char *desc,
                   const char *icon, const char *cols)
{
    tiku_trk_typerec_t *r = NULL;
    const char *p;
    int i;

    if (db == NULL || type == NULL || type[0] == '\0') {
        return -1;
    }
    for (i = 0; i < db->n; i++) {
        if (strcmp(db->rec[i].type, type) == 0) {
            /* Replaced, not added twice: a database read over one already
             * in memory keeps one record per type. */
            r = &db->rec[i];
            break;
        }
    }
    if (r == NULL) {
        if (db->n >= TIKU_TRK_TYPES_MAX) {
            return -1;
        }
        r = &db->rec[db->n++];
        i = db->n - 1;
    }
    memset(r, 0, sizeof *r);
    snprintf(r->type, sizeof r->type, "%s", type);
    snprintf(r->desc, sizeof r->desc, "%s", (desc != NULL) ? desc : "");
    snprintf(r->icon, sizeof r->icon, "%s", (icon != NULL) ? icon : "");
    for (p = (cols != NULL) ? cols : ""; *p != '\0'; ) {
        const char *comma = strchr(p, ',');
        size_t len = (comma != NULL) ? (size_t)(comma - p) : strlen(p);
        char word[24];
        int col;

        while (len > 0u && (*p == ' ' || *p == '\t')) { p++; len--; }
        while (len > 0u && (p[len - 1u] == ' ' || p[len - 1u] == '\t')) {
            len--;
        }
        snprintf(word, sizeof word, "%.*s", (int)len, p);
        if (strcmp(word, "document") == 0) {
            r->document = 1;
        } else if (strncmp(word, "attr:", 5) == 0 && word[5] != '\0' &&
            r->nattr < TIKU_TRK_TYPE_COLS_MAX) {
            /* Kept by name: a view numbers it when it gives it a column
             * slot. */
            snprintf(r->attr[r->nattr], sizeof r->attr[0], "%s", word + 5);
            r->nattr++;
        } else {
            col = tiku_trk_types_column(word);
            if (col >= 0 && r->ncol < TIKU_TRK_TYPE_COLS_MAX) {
                r->col[r->ncol++] = col;
            }
        }
        if (comma == NULL) {
            break;
        }
        p = comma + 1;
    }
    return i;
}

const tiku_trk_typerec_t *
tiku_trk_types_exact(const tiku_trk_types_t *db, const char *type)
{
    int i;

    if (db == NULL || type == NULL || type[0] == '\0') {
        return NULL;
    }
    for (i = 0; i < db->n; i++) {
        if (strcmp(db->rec[i].type, type) == 0) {
            return &db->rec[i];
        }
    }
    return NULL;
}

const tiku_trk_typerec_t *
tiku_trk_types_super(const tiku_trk_types_t *db, const char *type)
{
    const tiku_trk_typerec_t *best = NULL;
    size_t best_len = 0u;
    int i;

    if (db == NULL || type == NULL || type[0] == '\0') {
        return NULL;
    }
    for (i = 0; i < db->n; i++) {
        size_t len = strlen(db->rec[i].type);

        /* A supertype is a proper prefix ending at the namespace boundary.
         * The proper-prefix rule keeps asking about "text/" itself from
         * returning the same record as both exact and super. */
        if (len > best_len && strlen(type) > len &&
            (db->rec[i].type[len - 1u] == '/' ||
             db->rec[i].type[len - 1u] == '-') &&
            strncmp(db->rec[i].type, type, len) == 0) {
            best = &db->rec[i];
            best_len = len;
        }
    }
    return best;
}

const tiku_trk_typerec_t *
tiku_trk_types_find(const tiku_trk_types_t *db, const char *type)
{
    const tiku_trk_typerec_t *exact = tiku_trk_types_exact(db, type);

    return (exact != NULL) ? exact : tiku_trk_types_super(db, type);
}

int
tiku_trk_types_load(tiku_trk_types_t *db, const char *path)
{
    FILE *f;
    char line[512];
    int n = 0;

    if (db == NULL || path == NULL) {
        return -1;
    }
    f = fopen(path, "r");
    if (f == NULL) {
        return -1;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        char *type = line, *desc, *icon, *cols, *nl;

        nl = strchr(line, '\n');
        if (nl != NULL) { *nl = '\0'; }
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }
        desc = strchr(type, '\t');
        if (desc == NULL) {
            continue;           /* a type with no description is not one */
        }
        *desc++ = '\0';
        icon = strchr(desc, '\t');
        if (icon != NULL) { *icon++ = '\0'; }
        cols = (icon != NULL) ? strchr(icon, '\t') : NULL;
        if (cols != NULL) { *cols++ = '\0'; }
        if (tiku_trk_types_add(db, type, desc, icon, cols) >= 0) {
            n++;
        }
    }
    (void)fclose(f);
    return n;
}

const char *tiku_trk_types_column_name(int column)
{
    static const char *const names[] = {
        "name", "kind", "value", "size", "cap", "modified", "location",
        "permissions", "original_path"
    };
    return column >= 0 && (size_t)column < sizeof names / sizeof names[0]
        ? names[column] : NULL;
}

int tiku_trk_types_write_record(FILE *file, const tiku_trk_typerec_t *record)
{
    int i, count = 0;
    if (!file || !record) return -1;
    if (fprintf(file, "%s\t%s\t%s\t", record->type, record->desc,
                record->icon) < 0)
        return -1;
    for (i = 0; i < record->ncol; i++) {
        const char *name = tiku_trk_types_column_name(record->col[i]);
        if (!name || fprintf(file, "%s%s", count++ ? "," : "", name) < 0)
            return -1;
    }
    for (i = 0; i < record->nattr; i++)
        if (fprintf(file, "%sattr:%s", count++ ? "," : "", record->attr[i]) <
            0)
            return -1;
    if (record->document && fprintf(file, "%sdocument", count ? "," : "") < 0)
        return -1;
    return ferror(file) ? -1 : 0;
}

int tiku_trk_types_save(const tiku_trk_types_t *db, const char *path)
{
    FILE *file;
    int i, error = 0;
    if (!db || !path || !(file = fopen(path, "w"))) return -1;
    if (fputs("# type\tdescription\ticon\tcolumns\n", file) < 0) error = 1;
    for (i = 0; i < db->n && !error; i++)
        if (tiku_trk_types_write_record(file, &db->rec[i]) ||
            fputc('\n', file) == EOF)
            error = 1;
    if (fclose(file)) error = 1;
    return error ? -1 : 0;
}

void tiku_trk_types_defaults(tiku_trk_types_t *db)
{
    static const struct {
        const char *type, *description, *icon, *columns;
    } defaults[] = TIKU_SHELL_TYPE_DEFAULTS;
    if (!db) return;
    tiku_trk_types_init(db);
    for (size_t i = 0; i < sizeof defaults / sizeof defaults[0]; i++)
        (void)tiku_trk_types_add(db, defaults[i].type, defaults[i].description,
                               defaults[i].icon, defaults[i].columns);
}

int
tiku_trk_types_install(tiku_trk_types_t *db, const char *path)
{
    if (db == NULL || path == NULL) {
        return 0;
    }
    if (tiku_trk_types_load(db, path) >= 0) {
        /* One is already there, and it is left alone (AW-013). */
        return 0;
    }
    tiku_trk_types_defaults(db);
    (void)tiku_trk_types_save(db, path);
    return 1;
}
