/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_textview.h - many lines of editable text, with a caret in them.
 *
 * tiku_field is one line, a name being typed over; this is a document:
 * Return makes a new line, the caret is a line and a column, and the
 * screen is a window onto something taller.  It
 * owns the text, the caret, the selection, the undo journal and the
 * scroll; the pixels are the caller's.  Every edit leaves the caret on a
 * real line at a real column.  Columns are byte offsets.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TEXTVIEW_H_
#define TIKU_TEXTVIEW_H_

#include <stddef.h>

#define TIKU_TEXTVIEW_LINES_MAX 4000  /**< lines in a document              */
#define TIKU_TEXTVIEW_LINE_MAX  1024  /**< bytes of a line, NUL included    */
#define TIKU_TEXTVIEW_UNDO      256   /**< journal entries kept             */

/** @brief One line of the document. */
typedef struct {
    char *text;                 /**< NUL-terminated, owned; NULL is empty */
    int   len;
} tiku_textview_line_t;

/** @brief A document: its lines, caret, selection, scroll and journal. */
typedef struct {
    tiku_textview_line_t line[TIKU_TEXTVIEW_LINES_MAX];
    int nline;
    int cy, cx;                 /**< the caret: line, then column        */
    int ay, ax;                 /**< the selection's anchor              */
    int selecting;              /**< a selection is held from the anchor */
    int top;                    /**< first line shown                    */
    /* The journal: every edit as an insert or a delete of text at a
     * place, with a run of typing folded into one entry, so a word undoes
     * in one step.  The view is modified while the present is away from
     * the mark the last save left. */
    struct tiku_textview_edit {
        int   kind;             /**< 0 empty, 1 inserted, 2 deleted      */
        int   line, col;        /**< where the run began                 */
        char *text;             /**< the characters, '\n' between lines  */
        int   len;
        int   coalesce;         /**< a like edit beside it may fold in   */
        int   group;            /**< 0 alone, else a batch undone as one */
    } undo[TIKU_TEXTVIEW_UNDO];
    int undo_n;                 /**< entries held, redo history included */
    int undo_at;                /**< the present: undo below, redo above */
    int saved_at;               /**< undo_at at the last save, or -1     */
    int recording;              /**< 0 while undo or redo replays        */
    int group_open;             /**< the batch new edits join, 0 none    */
    int group_seq;              /**< the last batch id handed out        */
} tiku_textview_t;

/** @brief Start on one empty line, which is what an empty document is. */
void tiku_textview_init(tiku_textview_t *tv);

/**
 * @brief Free every line and the journal, leaving the view as
 *        tiku_textview_init() leaves it.
 */
void tiku_textview_free(tiku_textview_t *tv);

/**
 * @brief Replace the whole text, splitting @p text on newlines, with the
 *        caret at the top and the journal empty.
 *
 * A trailing newline does not make a final empty line, so a file does not
 * grow a blank line each time it is opened and saved.  A line too long
 * for TIKU_TEXTVIEW_LINE_MAX is cut, and lines past
 * TIKU_TEXTVIEW_LINES_MAX are dropped.
 */
void tiku_textview_set(tiku_textview_t *tv, const char *text);

/** @brief How many lines there are (never fewer than one). */
int tiku_textview_lines(const tiku_textview_t *tv);

/** @brief Line @p i, or "" for one that does not exist. */
const char *tiku_textview_line(const tiku_textview_t *tv, int i);

/** @brief Length of line @p i in bytes, or 0 for one that does not exist. */
int tiku_textview_line_len(const tiku_textview_t *tv, int i);

/**
 * @brief Whether it has been edited since the last set or save; undoing
 *        back to the save reads as unmodified.
 */
int tiku_textview_modified(const tiku_textview_t *tv);

/** @brief Say it has been saved: the modified flag clears. */
void tiku_textview_saved(tiku_textview_t *tv);

/** @brief Where the caret is: its line and byte column. */
void tiku_textview_caret(const tiku_textview_t *tv, int *line, int *col);

/** @brief The first line on screen. */
int tiku_textview_top(const tiku_textview_t *tv);

/**
 * @brief Put the caret on @p line, @p col, clamped into the text and back
 *        to the start of a codepoint.
 */
void tiku_textview_place(tiku_textview_t *tv, int line, int col);

/**
 * @brief Begin a selection at the caret, if one is not already held.
 *
 * Idempotent while a selection stands: the anchor is set once, at the
 * caret it was called on, and every extending move after grows the span
 * from there.  A shift+move calls this before moving; a plain move calls
 * tiku_textview_select_none() instead.
 */
void tiku_textview_select_begin(tiku_textview_t *tv);

/** @brief Let a selection go; the caret stays where it is. */
void tiku_textview_select_none(tiku_textview_t *tv);

/** @brief Take the whole document: anchor at the top, caret at the end. */
void tiku_textview_select_all(tiku_textview_t *tv);

/**
 * @brief The selected span, ordered so its start is not past its end.
 *
 * An insert, a newline, a backspace or a delete first removes the span,
 * so typing replaces what is selected.  When the line the removal would
 * leave is past the line limit, the span stays and the edit does nothing.
 *
 * @return 1, filling the four coordinates, when a non-empty selection is
 *         held; 0, touching nothing, when there is none.
 */
int tiku_textview_selection(const tiku_textview_t *tv,
                            int *y0, int *x0, int *y1, int *x1);

/**
 * @brief The selected text, newlines between its lines, NUL-closed.
 *
 * Each line's part is copied whole or not at all, and copying stops at
 * the first that does not fit.
 *
 * @return the length written, NUL apart; 0 when nothing is selected or
 *         the first line's part does not fit.
 */
size_t tiku_textview_selected(const tiku_textview_t *tv, char *out,
                              size_t max);

/** @brief The bytes tiku_textview_selected() needs for the whole
 *         selection, its NUL apart; 0 when nothing is selected. */
size_t tiku_textview_selected_size(const tiku_textview_t *tv);

/**
 * @brief Type @p text at the caret, newlines breaking lines.
 *
 * A held selection is removed first, as a typed key removes it; when it
 * cannot be, nothing lands.  Tabs, CR and UTF-8 bytes are kept and other
 * control bytes dropped.  A character that would pass the line limits is
 * skipped whole, and the journal records exactly what landed.
 */
void tiku_textview_insert_text(tiku_textview_t *tv, const char *text);
/**
 * @brief Insert @p text, a UTF-8 commit, in place of the selection or at
 *        the caret, as one undo group.
 *
 * Text that is not valid UTF-8, or would pass a line or the line count
 * limit, is refused before the selection changes.  Caret columns stay
 * byte offsets on codepoint boundaries.
 *
 * @return 1, or 0 when refused
 */
int tiku_textview_commit(tiku_textview_t *tv, const char *text);

/** @brief Take back the last edit; @return 1 if there was one. */
int tiku_textview_undo(tiku_textview_t *tv);

/** @brief Do again the last undone edit; @return 1 if there was one. */
int tiku_textview_redo(tiku_textview_t *tv);

/** @brief Whether there is an edit to undo. */
int tiku_textview_can_undo(const tiku_textview_t *tv);

/** @brief Whether there is an undone edit to redo. */
int tiku_textview_can_redo(const tiku_textview_t *tv);

/**
 * @brief Open a batch of edits that undo and redo take as one.
 *
 * A renumber, which rewrites every line, is one undo this way.  Until
 * tiku_textview_group_end() the edits still happen one by one, and
 * coalesce as usual, but each is stamped with the batch's id.
 */
void tiku_textview_group_begin(tiku_textview_t *tv);

/** @brief Close the batch tiku_textview_group_begin() opened. */
void tiku_textview_group_end(tiku_textview_t *tv);

/**
 * @brief Type one byte at the caret, in place of a held selection; a full
 *        line takes nothing.
 */
void tiku_textview_insert(tiku_textview_t *tv, char ch);

/** @brief Break the line at the caret; the tail becomes the next line. */
void tiku_textview_newline(tiku_textview_t *tv);

/**
 * @brief Rub out the codepoint before the caret, or the held selection.
 *
 * At the head of a line this joins it to the one above, with the caret
 * at the seam, not at the start.
 */
void tiku_textview_backspace(tiku_textview_t *tv);

/**
 * @brief Rub out the codepoint after the caret, or the held selection,
 *        joining the next line at a line's end.
 */
void tiku_textview_delete(tiku_textview_t *tv);

/** @brief Which way the caret is being moved. */
typedef enum {
    TIKU_TEXTVIEW_LEFT = 0,
    TIKU_TEXTVIEW_RIGHT,
    TIKU_TEXTVIEW_UP,
    TIKU_TEXTVIEW_DOWN,
    TIKU_TEXTVIEW_HOME,
    TIKU_TEXTVIEW_END
} tiku_textview_move_t;

/**
 * @brief Move the caret, a codepoint at a time.
 *
 * Left at the head of a line goes to the end of the one above, and right
 * at the end goes to the head of the one below, so the caret walks the
 * text as one run.  Up and down keep the column, clamped to a shorter
 * line.
 */
void tiku_textview_move(tiku_textview_t *tv, tiku_textview_move_t how);

/**
 * @brief Scroll so the caret is among the @p rows lines on screen.
 *
 * @return nonzero when the first line shown changed.
 */
int tiku_textview_reveal(tiku_textview_t *tv, int rows);

/**
 * @brief Scroll to @p top directly, clamped so the last of @p rows lines
 *        on screen is at most the document's last.
 *
 * @return nonzero when the first line shown changed.
 */
int tiku_textview_scroll_to(tiku_textview_t *tv, int top, int rows);

/**
 * @brief Write the whole text into @p out, each line ended by a newline.
 *
 * @return the whole text's length, NUL apart, whether or not it fitted,
 *         so a caller can size a buffer by asking twice.
 */
size_t tiku_textview_text(const tiku_textview_t *tv, char *out, size_t max);

#endif /* TIKU_TEXTVIEW_H_ */
