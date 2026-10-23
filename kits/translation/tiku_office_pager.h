/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_pager.h - placing a story's blocks: on pages, a column at
 * a time, breaking where a line or a row does not fit; or down one
 * column that never ends, for a cell, a text box, a header or a note.
 * Paragraphs are placed in tiku_office_page.c, tables in
 * tiku_office_table.c; both share the pager they are placed with.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_OFFICE_PAGER_H_
#define TIKU_OFFICE_PAGER_H_

#include "tiku_office_private.h"

/** @brief A float's box, kept clear of text on the page it is on. */
typedef struct {
    float         x0, y0, x1, y1;   /* on the page, its gap included    */
    unsigned char wrap;             /* OF_WRAP_SQUARE or OF_WRAP_BLOCK  */
} pg_clear_t;

/** @brief What a page shows besides its body: which section it is in,
 *         the number it shows, whether it is its section's first. */
typedef struct {
    int sect;                       /* index into the document's        */
    int num;                        /* as shown                         */
    int first;                      /* the section's first page         */
} pg_info_t;

/** @brief Blocks being placed: on pages, or down one unending column. */
typedef struct {
    of_doc_t           *d;          /* the document                     */
    of_faces_t         *fs;         /* its faces                        */
    of_layout_t        *lay;        /* pages being made; NULL: a column */
    of_arena_t         *a;          /* glyphs and fragments kept in     */
    of_vec_t           *frags;      /* the page's or column's fragments */
    of_vec_t            info;       /* pg_info_t, a page each           */
    const of_pageno_t  *no;         /* a column's page numbers          */
    const of_section_t *sec;        /* the section being placed         */
    int                 nsect;      /* its index                        */
    float               x, width;   /* the column: its left and width   */
    float               page_top;   /* where a new page's body begins   */
    float               top;        /* where columns begin on this page */
    float               bottom;     /* where the body ends; 0: never    */
    float               y;          /* where the next line goes         */
    int                 col, ncol;  /* this column, of how many         */
    float               gap;        /* between columns                  */
    int                 page;       /* the page, -1 before the first    */
    int                 pagecap;    /* room for pages in the layout     */
    int                 num;        /* its number as shown              */
    int                 content;    /* something is on the page         */
    float               after;      /* space the last paragraph wants   */
    const char         *style;      /* the last paragraph's style       */
    int                 contextual; /* the last paragraph's contextual  */
    of_vec_t            notes;      /* int: footnotes on this page      */
    float               notes_h;    /* their height, the rule included  */
    float              *note_h;     /* each note's height, once known   */
    of_vec_t            clear;      /* pg_clear_t: floats on this page  */
    of_vec_t            places;     /* of_place_t: bookmarks, headings  */
    int                 depth;      /* cells within cells               */
    int                 failed;     /* out of memory, or too many pages */
} of_pager_t;

/** @brief A pager for one column @p width wide, never ending: fragments
 *         go to @p frags, x from 0, y from 0. */
void pg_column(of_pager_t *pg, of_doc_t *d, of_faces_t *fs, float width,
               const of_pageno_t *no, of_vec_t *frags, of_arena_t *a);
/** @brief The pager's own memory given back. */
void pg_done(of_pager_t *pg);
/** @brief Every block of @p s placed.  @return 0, or -1. */
int pg_story(of_pager_t *pg, const of_story_t *s);
/** @brief A paragraph placed.  @return 0, or -1. */
int pg_para(of_pager_t *pg, const of_para_t *p);
/** @brief A table placed.  @return 0, or -1. */
int pg_table(of_pager_t *pg, const of_table_t *t);
/** @brief The next column, or the next page after the last.  @return 0,
 *         or -1 out of memory or past the page ceiling. */
int pg_next_column(of_pager_t *pg);
/** @brief A new page of the section's setup, the open one closed with
 *         its notes placed.  @return 0, or -1 out of memory or past the
 *         page ceiling. */
int pg_new_page(of_pager_t *pg);
/** @brief Fragments @p from on moved by (@p dx, @p dy). */
void pg_move(of_vec_t *frags, int from, float dx, float dy);
/** @brief The frags from @p from on, moved to come before those from
 *         @p at: drawn under them. */
void pg_under(of_vec_t *frags, int at, int from);
/** @brief A rule from (@p x, @p y), @p w across and @p h down, as border
 *         @p b draws it.  @return 0, or -1. */
int pg_rule(of_vec_t *frags, float x, float y, float w, float h,
            const of_border_t *b);
/** @brief A filled box.  @return 0, or -1. */
int pg_box(of_vec_t *frags, float x, float y, float w, float h,
           uint32_t color);
/** @brief Where the page's body ends for text: above its footnotes. */
float pg_limit(const of_pager_t *pg);

#endif /* TIKU_OFFICE_PAGER_H_ */
