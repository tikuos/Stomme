/* tiku_reader_typography.c -- a window's text size and reading width.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_reader_private.h"

/** @brief Switch the font to this window's text size for the page.
 *  @return the size before, for tiku_reader_font_end().
 *  @note Loop thread only.  The caller restores the size before drawing
 *        anything else, so the toolbar and the desktop keep theirs. */
int tiku_reader_font_begin(tiku_reader_help_window_t *c)
{
    int previous = tiku_font_size();
    if (c->reader && c->reader->text_size)
        tiku_font_set_size(c->reader->text_size);
    return previous;
}

/** @brief Restore the font size tiku_reader_font_begin() returned. */
void tiku_reader_font_end(int previous)
{
    if (tiku_font_size() != previous) tiku_font_set_size(previous);
}

/** @brief Text size and reading width commands; they apply to this window
 *         only and are not saved.  @return 1 when @p command was one. */
int tiku_reader_typography(tiku_reader_help_window_t *c, int command)
{
    struct help_reader *r = c->reader;
    if (!r) return 0;
    int size = r->text_size ? r->text_size : tiku_font_size();
    switch (command) {
    case HELP_TEXT_LARGER:
        if (size < 16) size += 2;
        r->text_size = size;
        break;
    case HELP_TEXT_SMALLER:
        if (size > 10) size -= 2;
        r->text_size = size;
        break;
    case HELP_TEXT_RESET:
        r->text_size = 0;
        size = tiku_font_size();
        break;
    case HELP_READING_WIDTH:
        r->reading_width = r->reading_width ? 0 : 720;
        break;
    default: return 0;
    }
    r->dragging = 0;
    if (c->window) {
        tiku_rect_t content = tiku_window_content(c->window);
        tiku_reader_refit(c, tiku_reader_layout(c, content));
    }
    snprintf(c->status, sizeof c->status, "Text: %d px%s. Reading width: %s.",
             size, r->text_size ? "" : " (desktop default)",
             r->reading_width ? "comfortable" : "full window");
    if (c->dirty) *c->dirty = 1;
    return 1;
}
