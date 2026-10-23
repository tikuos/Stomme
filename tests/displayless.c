/*
 * Shared desktop model tests.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * displayless.c - document layout with caller-supplied metrics and no renderer.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include <string.h>
#include "tiku_doc.h"
static tiku_doc_t page;

/** @brief Supply fixed-width character metrics. */
int tiku_measure_width(int face, const char *text)
{ (void)face; return (int)strlen(text) * 7; }
/** @brief Supply the line height. */
int tiku_measure_height(int face)
{ (void)face; return 14; }
/** @brief Supply the baseline ascent. */
int tiku_measure_ascent(int face)
{ (void)face; return 10; }
/** @brief Map headings to the available bold face. */
int tiku_measure_heading(int level)
{ (void)level; return TIKU_MEASURE_BOLD; }
/** @brief Supply control row height. */
int tiku_measure_row_h(void)
{ return 20; }
/** @brief Supply control horizontal spacing. */
int tiku_measure_step_w(void)
{ return 8; }

/** @brief Check inline styles, flow and boxes with zero stylesheet-rule capacity. */
int main(void)
{
    tiku_doc_init(&page);
    assert(tiku_doc_read(&page,
        "<style>p{color:blue}</style><p style='color:red'>Inline</p>"));
    assert(tiku_doc_notices(&page) & TIKU_DOC_CSS_LIMIT);
    assert(page.nrun == 1 && page.run[0].color == 0xff0000);
    assert(page.run[0].flags & TIKU_DOC_RUN_COLOR);
    tiku_doc_layout(&page, 160);
    assert(tiku_doc_check(&page));
    assert(tiku_doc_read(&page,
        "<p style='text-align:right;line-height:40px;white-space:pre-wrap;"
        "font-size:20px'>A  B\nC</p>"));
    tiku_doc_layout(&page, 160);
    assert(tiku_doc_check(&page));
    assert(page.nline == 2 && page.line[0].h == 40 && page.line[1].h == 40);
    assert(page.line[0].x == 160 - page.line[0].w);
    assert(page.run[0].flow.whitespace == TIKU_DOC_WHITE_PRE_WRAP);
    assert(tiku_doc_notices(&page) & TIKU_DOC_FONT_FALLBACK);
    assert(tiku_doc_read(&page,
        "<div style='padding:3px;background:blue'><p>Box</p></div>"
        "<span style='display:none'>Hidden</span>"));
    tiku_doc_layout(&page, 160);
    assert(tiku_doc_check(&page));
    assert(page.nline == 1 && page.line[0].x == 3);
    return 0;
}
