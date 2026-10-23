/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_odt_obj.c - objects embedded in OpenDocument text: a
 * draw:frame's draw:object, a folder of the package with a document of
 * its own.  A chart's is drawn from the data it keeps, where the frame
 * is laid out; a formula's MathML is set as the document's equations
 * are, in the folder or kept in a flat document; anything else is shown
 * by the picture of it kept beside (the frame's draw:image).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_chart.h"
#include "tiku_office_math.h"
#include "tiku_office_odt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Object folder @p dir's part @p name ("content.xml") read.
 *  @return it, to free, or NULL. */
static tiku_xml_t *
object_part(of_pkg_t *pkg, const char *dir, const char *name)
{
    char part[512];

    snprintf(part, sizeof part, "%s/%s", dir, name);
    return of_pkg_xml(pkg, part);
}

/** @brief A chart object drawn in @p box (x y w h): of_doc_t's art_draw
 *         for OpenDocument text. */
static int
object_draw(of_doc_t *d, of_faces_t *fs, const of_art_t *a,
            const float *box, tiku_pdf_writer_t *w)
{
    tiku_xml_t *doc = object_part(d->pkg, a->part, "content.xml");
    tiku_xml_t *st = object_part(d->pkg, a->part, "styles.xml");
    dml_mat_t m = {1, 0, 0, 1, box[0], box[1]};
    dml_ctx_t c;
    int drawn;

    memset(&c, 0, sizeof c);
    c.pkg = d->pkg;
    c.part = a->part;
    c.w = w;
    c.fs = fs;
    c.d = d;
    c.text_pt = 10;
    dml_map_read(&c, NULL);
    drawn = ch_draw_odf(&c, tiku_xml_root(doc), tiku_xml_root(st), &m,
                        box[2], box[3]);
    dml_done(&c);
    tiku_xml_free(doc);
    tiku_xml_free(st);
    return drawn < 0 ? -1 : 0;
}

/** @brief Whether @p e is MathML's math element, its prefix any. */
static int
is_math(const tiku_xml_node_t *e)
{
    size_t n = e != NULL && e->name != NULL ? strlen(e->name) : 0;

    return n >= 4 && strcmp(e->name + n - 4, "math") == 0 &&
           (n == 4 || e->name[n - 5] == ':');
}

/** @brief Config item @p name's value anywhere under @p e into @p out
 *         (@p max bytes), left alone when there is none. */
static void
config(const tiku_xml_node_t *e, const char *name, char *out, size_t max,
       int depth)
{
    const tiku_xml_node_t *c;
    const char *n;

    for (c = tiku_xml_child(e, NULL); c != NULL && depth < 8;
         c = tiku_xml_next(c, NULL)) {
        n = tiku_xml_attr(c, "config:name");
        if (tiku_xml_is(c, "config:config-item") && n != NULL &&
            strcmp(n, name) == 0) {
            (void)tiku_xml_text(c, out, max);
            return;
        }
        config(c, name, out, max, depth + 1);
    }
}

/** @brief Formula @p math, of object folder @p dir (NULL: kept in the
 *         document itself), read as the folder's settings say: at its
 *         base font height, in a line's style when it is set as text.
 *  @return the drawing showing it, or NULL. */
static of_art_t *
formula(od_t *x, const tiku_xml_node_t *math, const char *dir)
{
    tiku_xml_t *set = dir != NULL ? object_part(x->pkg, dir, "settings.xml")
                                  : NULL;
    of_art_t *a = of_alloc(&x->d->arena, sizeof *a);
    char high[32] = "", text[16] = "";
    float pt;

    config(tiku_xml_root(set), "BaseFontHeight", high, sizeof high, 0);
    config(tiku_xml_root(set), "IsTextMode", text, sizeof text, 0);
    tiku_xml_free(set);
    pt = (float)atof(high);
    if (a == NULL || of_mathml(x->d, math, strcmp(text, "true") != 0,
                               pt >= 2 && pt <= 400 ? pt : 12, 0,
                               &a->math) != 0 || a->math == NULL) {
        return NULL;
    }
    return a;
}

of_art_t *
od_object_art(od_t *x, const tiku_xml_node_t *obj)
{
    const char *href = tiku_xml_attr(obj, "xlink:href");
    const tiku_xml_node_t *c;
    char dir[256];
    tiku_xml_t *doc;
    of_art_t *a;
    int chart, n;

    for (c = tiku_xml_child(obj, NULL); c; c = tiku_xml_next(c, NULL)) {
        if (is_math(c)) {           /* a flat document's, within it */
            return formula(x, c, NULL);
        }
    }

    while (href != NULL && href[0] == '.' && href[1] == '/') {
        href += 2;
    }
    if (href == NULL || strstr(href, "://") != NULL) {
        return NULL;
    }
    n = snprintf(dir, sizeof dir, "%s", href);
    while (n > 0 && dir[n - 1] == '/') {   /* "Object 1/": the folder */
        dir[--n] = '\0';
    }
    if (n <= 0 || (size_t)n >= sizeof dir ||
        (doc = object_part(x->pkg, dir, "content.xml")) == NULL) {
        return NULL;
    }
    if (is_math(tiku_xml_root(doc))) {
        a = formula(x, tiku_xml_root(doc), dir);
        tiku_xml_free(doc);
        return a;
    }
    chart = tiku_xml_path(tiku_xml_root(doc), "office:body/office:chart")
            != NULL;
    tiku_xml_free(doc);
    if (!chart || (a = of_alloc(&x->d->arena, sizeof *a)) == NULL) {
        return NULL;
    }
    a->part = of_strdup(&x->d->arena, dir);
    x->d->art_draw = object_draw;
    return a->part != NULL ? a : NULL;
}
