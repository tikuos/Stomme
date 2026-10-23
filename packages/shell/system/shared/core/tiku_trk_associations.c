/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_associations.c - the File Associations model.  Loading and
 * cancelling never write; only a save touches the stored preferences.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_trk_associations.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/** @brief The name the list shows for @p type, or NULL for one not named. */
static const char *association_label(const char *type)
{
    static const struct { const char *type, *label; } names[] =
        TIKU_SHELL_ASSOCIATION_LABELS;
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (!strcmp(names[i].type, type)) return names[i].label;
    return NULL;
}

/**
 * @brief Add a row for @p type when some application opens it.
 *
 * Folder types (inode/), supertypes ("text/"), Be types and a type already
 * listed are skipped.  The label is association_label()'s, else @p label,
 * else the type itself.
 */
static void association_add(tiku_trk_associations_t *m,
    const tiku_trk_apps_t *apps, const char *type, const char *label)
{
    tiku_trk_cand_t candidates[TIKU_TRK_APP_MAX];
    size_t n = strlen(type);
    if (!n || n >= TIKU_TYPE_MAX || !strchr(type, '/') ||
        type[n - 1] == '/' || !strncmp(type, "inode/", 6) ||
        !strncasecmp(type, "application/x-vnd.be-", 21) ||
        m->count == TIKU_TRK_ASSOC_MAX) return;
    for (int i = 0; i < m->count; i++)
        if (!strcmp(m->row[i].type, type)) return;
    if (!tiku_trk_launch_candidates(apps, &type, 1, NULL, candidates,
                                    TIKU_TRK_APP_MAX)) return;
    tiku_trk_association_t *r = &m->row[m->count++];
    snprintf(r->type, sizeof r->type, "%s", type);
    const char *friendly = association_label(type);
    snprintf(r->label, sizeof r->label, "%s", friendly ? friendly :
             label && *label ? label : type);
}

/** @brief Order rows by label, whatever order the registries list them in. */
static int association_compare(const void *a, const void *b)
{
    const tiku_trk_association_t *x = a, *y = b;
    return strcmp(x->label, y->label);
}

void tiku_trk_associations_load(tiku_trk_associations_t *m,
    const tiku_trk_apps_t *apps, const tiku_trk_types_t *types,
    tiku_store_t *store)
{
    memset(m, 0, sizeof *m);
    if (types) for (int i = 0; i < types->n; i++)
        association_add(m, apps, types->rec[i].type, types->rec[i].desc);
    for (int i = 0; ; i++) {
        const char *label = NULL;
        const char *type = tiku_model_type_at(i, &label);
        if (!type) break;
        association_add(m, apps, type, label);
    }
    if (apps) for (int i = 0; i < apps->count; i++)
        for (int j = 0; j < apps->app[i].ntype; j++)
            association_add(m, apps, apps->app[i].type[j], NULL);
    qsort(m->row, (size_t)m->count, sizeof m->row[0], association_compare);
    for (int i = 0; i < m->count; i++) {
        tiku_trk_association_t *r = &m->row[i];
        tiku_trk_launch_preferred(store, r->type, r->saved, sizeof r->saved);
        snprintf(r->chosen, sizeof r->chosen, "%s", r->saved);
    }
}

int tiku_trk_associations_changed(const tiku_trk_associations_t *m)
{
    for (int i = 0; i < m->count; i++)
        if (strcmp(m->row[i].saved, m->row[i].chosen)) return 1;
    return 0;
}

int tiku_trk_associations_save(tiku_trk_associations_t *m, tiku_store_t *s)
{
    char before[TIKU_TRK_ASSOC_MAX][TIKU_TRK_APP_NAME_MAX];
    int failed = 0;
    for (int i = 0; i < m->count; i++)
        tiku_trk_launch_preferred(s, m->row[i].type, before[i],
                                  sizeof before[i]);
    for (int i = 0; i < m->count; i++) {
        tiku_trk_association_t *r = &m->row[i];
        if (strcmp(r->saved, r->chosen) &&
            tiku_trk_launch_set_preferred(s, r->type, r->chosen) < 0)
            failed = 1;
    }
    if (!failed && tiku_store_flush(s) >= 0) {
        for (int i = 0; i < m->count; i++)
            snprintf(m->row[i].saved, sizeof m->row[i].saved, "%s",
                     m->row[i].chosen);
        return 1;
    }
    for (int i = 0; i < m->count; i++)
        if (strcmp(m->row[i].saved, m->row[i].chosen))
            (void)tiku_trk_launch_set_preferred(s, m->row[i].type, before[i]);
    (void)tiku_store_flush(s);
    return 0;
}
