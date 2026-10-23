/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_odt.h - what the OpenDocument reader's parts share: the
 * reader, the named styles of styles.xml and content.xml, and the lists
 * being counted.  Styles, lists and pages are resolved in
 * tiku_office_odt_style.c; the body is read in tiku_office_odt.c, its
 * tables in tiku_office_odt_table.c and its embedded objects in
 * tiku_office_odt_obj.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_OFFICE_ODT_H_
#define TIKU_OFFICE_ODT_H_

#include "tiku_office_private.h"

#define OD_LEVELS 10                /* list and outline levels           */

/** @brief The families styles come in, as the reader needs them. */
enum { OD_PARA, OD_TEXT, OD_TABLE, OD_COLUMN, OD_ROW, OD_CELL, OD_GRAPHIC,
       OD_FAMILIES };

/** @brief A named style: its element, and what it is based on. */
typedef struct {
    const char            *name;    /* style:name                        */
    const char            *parent;  /* style:parent-style-name, or NULL  */
    int                    family;  /* OD_*                              */
    int                    automatic;  /* content.xml's own, not common  */
    const tiku_xml_node_t *e;       /* its style:style                   */
} od_style_t;

/** @brief A list being counted: its style, and each level's count. */
typedef struct {
    const tiku_xml_node_t *style;   /* its text:list-style               */
    int                    count[OD_LEVELS];
} od_list_t;

/** @brief The reader: the document built and where it comes from. */
typedef struct {
    of_doc_t              *d;
    of_pkg_t              *pkg;
    tiku_xml_t            *styles;  /* styles.xml                        */
    tiku_xml_t            *content; /* content.xml                       */
    od_style_t            *style;   /* every named style, sorted         */
    int                    nstyle;
    const tiku_xml_node_t *dflt[OD_FAMILIES];  /* style:default-style    */
    const tiku_xml_node_t *faces[2];  /* office:font-face-decls, both parts */
    const tiku_xml_node_t *lstyles[3];  /* where text:list-style are    */
    const tiku_xml_node_t *outline; /* text:outline-style                */
    const tiku_xml_node_t *masters; /* office:master-styles              */
    const tiku_xml_node_t *layouts; /* styles.xml's automatic styles     */
    od_list_t              list;    /* the list read last                */
    od_list_t              heading; /* headings, counted                 */
    const char            *master;  /* the master page the pages are of  */
    int                    depth;   /* tables in cells, boxes in boxes   */
    of_vec_t               notes;   /* of_story_t *: footnotes in order  */
    of_vec_t               endnotes;  /* and endnotes                    */
    of_vec_t               waiting; /* of_run_t: frames for a paragraph  */
} od_t;

/* Styles, lists and pages: tiku_office_odt_style.c */

/** @brief The styles of both parts found and sorted.  @return 0, or -1 out
 *         of memory. */
int od_styles_load(od_t *x);
/** @brief The style of @p family called @p name, or NULL. */
const od_style_t *od_style(const od_t *x, int family, const char *name);
/** @brief A length ("2cm", "0.5in", "12pt") in points; @p dflt when it is
 *         none. */
float od_length(const char *v, float dflt);
/** @brief Attribute @p attr of @p props ("style:paragraph-properties") in
 *         style @p name of @p family, else its parents', else the family's
 *         default.  NULL when none says. */
const char *od_attr(const od_t *x, int family, const char *name,
                    const char *props, const char *attr);
/** @brief A paragraph style's look: its text properties over its parents'
 *         and the defaults', into @p st. */
void od_para_look(od_t *x, const char *pstyle, of_style_t *st);
/** @brief Text style @p tstyle's properties, its parents' first, over
 *         @p st: a span inside a paragraph. */
void od_span_look(od_t *x, const char *tstyle, of_style_t *st);
/** @brief Paragraph style @p pstyle's layout into @p p.
 *  @return 0, or -1 out of memory. */
int od_layout(od_t *x, const char *pstyle, of_pstyle_t *p);
/** @brief A border ("0.5pt solid #000000") into @p b; none for "none". */
void od_border(const char *v, of_border_t *b);
/** @brief A count or a number to start from (@p v): 0 to 32767, as the
 *         office suites allow, so counting on cannot overflow; @p dflt
 *         when it says none. */
int od_count(const char *v, int dflt);
/** @brief A colour ("#rrggbb") as ARGB; 0 for "transparent" or none. */
uint32_t od_color(const char *v);
/** @brief The text:list-style called @p name, or NULL. */
const tiku_xml_node_t *od_list_style(const od_t *x, const char *name);
/**
 * @brief The next label of level @p lvl (0 first) of list style @p ls,
 *        counted in @p list (@p start overriding when not -1), into @p p:
 *        its text, its look over @p base, what follows it, and the
 *        level's indents -- an aligned label's only unless @p own_indent.
 *        A heading's number, with the outline style for @p ls.
 *        @return 0, or -1 out of memory.
 */
int od_label(od_t *x, const tiku_xml_node_t *ls, od_list_t *list, int lvl,
             int start, const of_style_t *base, int own_indent,
             of_para_t *p);
/** @brief Section @p s's page, margins, headers and footers, from the
 *         master page called @p master (the first for NULL or a name not
 *         found); @p read reads a header's contents. */
void od_page(od_t *x, const char *master, of_section_t *s,
             of_story_t *(*read)(od_t *x, const tiku_xml_node_t *e));

/* Tables: tiku_office_odt_table.c */

/** @brief A table:table read into @p t.  @return 0, or -1 out of memory. */
int od_table(od_t *x, const tiku_xml_node_t *e, of_table_t *t);

/* The body: tiku_office_odt.c */

/** @brief The blocks of @p e (a cell, a text box, a note) as a story.
 *  @return it, or NULL out of memory. */
of_story_t *od_story(od_t *x, const tiku_xml_node_t *e);

/* Embedded objects: tiku_office_odt_obj.c */

/** @brief The drawing that shows draw:object @p obj: a chart the reader
 *         draws from its own data, or a formula set from its MathML; NULL
 *         for any other object, shown by its picture instead. */
of_art_t *od_object_art(od_t *x, const tiku_xml_node_t *obj);

#endif /* TIKU_OFFICE_ODT_H_ */
