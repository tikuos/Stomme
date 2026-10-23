/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_oc.c - optional content: the layers a page shows.
 *
 * A group is on or off as the document's default configuration leaves
 * it (ISO 32000 8.11.4.3): its base state, its ON and OFF lists, then
 * the states its groups ask for when viewed.  A membership dictionary
 * (8.11.2.2) joins groups by a policy or a visibility expression.  What
 * is off is neither drawn nor read as text.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_private.h"

#include <stdlib.h>
#include <string.h>

#define OC_DEPTH 16                 /* expressions within expressions   */

/** @brief Group @p g set on or off: it leaves or joins the off list. */
static void
set_state(tiku_pdf_t *doc, pdf_obj_t *g, int on)
{
    int i;

    if (g == NULL || g->kind != PDF_DICT) {
        return;
    }
    for (i = 0; i < doc->noc_off; i++) {
        if (doc->oc_off[i] == g) {
            if (on) {
                doc->oc_off[i] = doc->oc_off[--doc->noc_off];
            }
            return;
        }
    }
    if (!on) {
        pdf_obj_t **grown = realloc(doc->oc_off, (size_t)(doc->noc_off + 1) *
                                                 sizeof *grown);

        if (grown != NULL) {
            doc->oc_off = grown;
            doc->oc_off[doc->noc_off++] = g;
        }
    }
}

/** @brief Every group in array @p a set on or off. */
static void
set_all(tiku_pdf_t *doc, pdf_obj_t *a, int on)
{
    int i;

    for (i = 0; a != NULL && a->kind == PDF_ARRAY && i < a->u.a.n; i++) {
        set_state(doc, pdf_resolve(doc, a->u.a.item[i]), on);
    }
}

/** @brief The states asked for on viewing (the AS entries whose Event is
 *         View): each group's Usage says, for the View category. */
static void
view_states(tiku_pdf_t *doc, pdf_obj_t *d)
{
    pdf_obj_t *as = pdf_get(doc, d, "AS");
    int i, k;

    for (i = 0; as != NULL && as->kind == PDF_ARRAY && i < as->u.a.n; i++) {
        pdf_obj_t *u = pdf_resolve(doc, as->u.a.item[i]);
        pdf_obj_t *cat = pdf_get(doc, u, "Category");
        pdf_obj_t *groups = pdf_get(doc, u, "OCGs");
        int view = 0;

        for (k = 0; cat != NULL && cat->kind == PDF_ARRAY &&
                    k < cat->u.a.n; k++) {
            view |= pdf_is_name(pdf_resolve(doc, cat->u.a.item[k]), "View");
        }
        if (!pdf_is_name(pdf_get(doc, u, "Event"), "View") || !view) {
            continue;
        }
        for (k = 0; groups != NULL && groups->kind == PDF_ARRAY &&
                    k < groups->u.a.n; k++) {
            pdf_obj_t *g = pdf_resolve(doc, groups->u.a.item[k]);
            pdf_obj_t *vs = pdf_get(doc, pdf_get(doc, pdf_get(doc, g,
                                                              "Usage"),
                                                 "View"), "ViewState");

            if (vs != NULL) {
                set_state(doc, g, !pdf_is_name(vs, "OFF"));
            }
        }
    }
}

/** @brief Whether group @p g is on, the configuration read at first ask. */
static int
group_on(tiku_pdf_t *doc, pdf_obj_t *g)
{
    int i;

    if (!doc->oc_read) {
        pdf_obj_t *props = pdf_get(doc, pdf_root(doc), "OCProperties");
        pdf_obj_t *d = pdf_get(doc, props, "D");

        doc->oc_read = 1;
        if (pdf_is_name(pdf_get(doc, d, "BaseState"), "OFF")) {
            set_all(doc, pdf_get(doc, props, "OCGs"), 0);
        }
        set_all(doc, pdf_get(doc, d, "ON"), 1);
        set_all(doc, pdf_get(doc, d, "OFF"), 0);
        view_states(doc, d);
    }
    for (i = 0; i < doc->noc_off; i++) {
        if (doc->oc_off[i] == g) {
            return 0;
        }
    }
    return 1;
}

static int visible(tiku_pdf_t *doc, pdf_obj_t *oc, int depth);

/** @brief A visibility expression: [/And ...], [/Or ...] or [/Not x],
 *         of groups and expressions. */
static int
expression(tiku_pdf_t *doc, pdf_obj_t *ve, int depth)
{
    pdf_obj_t *op = ve->u.a.n > 0 ? pdf_resolve(doc, ve->u.a.item[0]) : NULL;
    int i, any = 0, all = 1;

    if (depth > OC_DEPTH) {
        return 1;
    }
    for (i = 1; i < ve->u.a.n; i++) {
        pdf_obj_t *t = pdf_resolve(doc, ve->u.a.item[i]);
        int on = t != NULL && t->kind == PDF_ARRAY
                 ? expression(doc, t, depth + 1) : visible(doc, t, depth + 1);

        any |= on;
        all &= on;
        if (pdf_is_name(op, "Not")) {
            return !on;
        }
    }
    return pdf_is_name(op, "Or") ? any : all;
}

/** @brief A membership dictionary's groups under its policy P: AnyOn
 *         unless it says AllOn, AnyOff or AllOff. */
static int
policy(tiku_pdf_t *doc, pdf_obj_t *groups, pdf_obj_t *p)
{
    int i, on = 0, off = 0;

    if (groups != NULL && groups->kind == PDF_DICT) {
        on = group_on(doc, groups);
        off = !on;
    }
    for (i = 0; groups != NULL && groups->kind == PDF_ARRAY &&
                i < groups->u.a.n; i++) {
        pdf_obj_t *g = pdf_resolve(doc, groups->u.a.item[i]);

        if (g != NULL && g->kind == PDF_DICT) {
            if (group_on(doc, g)) {
                on++;
            } else {
                off++;
            }
        }
    }
    if (on + off == 0) {
        return 1;                   /* no groups: no say in it */
    }
    if (pdf_is_name(p, "AllOn")) return off == 0;
    if (pdf_is_name(p, "AnyOff")) return off > 0;
    if (pdf_is_name(p, "AllOff")) return on == 0;
    return on > 0;
}

/** @brief Whether @p oc, a group or a membership dictionary, shows. */
static int
visible(tiku_pdf_t *doc, pdf_obj_t *oc, int depth)
{
    pdf_obj_t *ve;

    if (oc == NULL || oc->kind != PDF_DICT || depth > OC_DEPTH) {
        return 1;
    }
    if (!pdf_is_name(pdf_get(doc, oc, "Type"), "OCMD")) {
        return group_on(doc, oc);
    }
    ve = pdf_get(doc, oc, "VE");
    if (ve != NULL && ve->kind == PDF_ARRAY) {
        return expression(doc, ve, depth + 1);
    }
    return policy(doc, pdf_get(doc, oc, "OCGs"), pdf_get(doc, oc, "P"));
}

int
pdf_oc_visible(tiku_pdf_t *doc, pdf_obj_t *oc)
{
    return visible(doc, oc, 0);
}

pdf_obj_t *
pdf_oc_props(tiku_pdf_t *doc, pdf_obj_t *operand, pdf_obj_t *res)
{
    if (operand != NULL && operand->kind == PDF_NAME) {
        return pdf_get(doc, pdf_get(doc, res, "Properties"),
                       (const char *)operand->u.s.p);
    }
    return pdf_resolve(doc, operand);
}
