/* tiku_reader_window.h -- the viewer window TikuReader, Help and
 * TikuMarker share: a tiku_doc page put on the screen, and where its links
 * go among files, help pages and the desktop's namespace.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_READER_WINDOW_H_
#define TIKU_READER_WINDOW_H_

#include "tiku_model.h"
#include "tiku_doc.h"
#include "tiku_reader_document.h"
#include "tiku_dialog.h"
#include "tiku_scroll.h"
#include "tiku_window.h"

/**
 * @brief The page's control id: a caller focuses it to give the page, and
 *        its reading keys, the keyboard.
 */
#define TIKU_READER_HELP_CONTROL_PAGE 5

/** @brief How many pages the back and forward histories each hold. */
#define TIKU_READER_HELP_HISTORY 16
/** @brief The largest report tiku_reader_help_window_html() takes. */
#define TIKU_READER_HELP_PAGE_MAX TIKU_DOC_TEXT_MAX
/** @brief The largest document file read, apart from the kit's own
 *         limits. */
#define TIKU_READER_READER_FILE_MAX TIKU_READER_FILE_MAX
/** @brief The most memory one document's tables may take. */
#define TIKU_READER_READER_MEMORY_MAX TIKU_READER_MEMORY_MAX

/** @brief Where a link went. */
typedef enum {
    TIKU_READER_HELP_NOWHERE = 0,  /* refused, and the status says why   */
    TIKU_READER_HELP_ANCHOR,       /* a place in the page now shown      */
    TIKU_READER_HELP_PAGE,         /* another page: of the help folder,  */
                                   /* or a document opened in its place  */
    TIKU_READER_HELP_NAMESPACE     /* a path, handed to the opener       */
} tiku_reader_help_where_t;

/**
 * @brief Open a path that is not a page, through the desktop's services.
 *
 * @return nonzero when the place was there and was opened; zero, and the
 *         window says it could not be opened.
 */
typedef int (*tiku_reader_help_open_fn)(void *ctx, const char *path);

/** @brief A reading position: the scroll offset, the size the page was
 *         laid out at, and the place in the page, for when the size has
 *         changed by the time it is returned to. */
typedef struct {
    int pixel, width, height;
    tiku_doc_place_t place;
} tiku_reader_help_position_t;

/**
 * @brief The identity an application gives the shared document viewer.
 *
 * A NULL profile selects TikuReader's.  A profile changes neither Help's
 * root confinement nor the document safety limits.
 * @note The caller keeps the profile and its strings for as long as any
 *       window uses them.
 */
typedef struct {
    const char *name, *icon;        /* window title; status-line icon    */
    const char *bookmark_env;       /* variable naming the bookmark file */
    const char *bookmark_file;      /* its name in the settings folder   */
    const char *welcome_html;       /* shown before a document is open   */
    const char *help_page;          /* the app's page in Help            */
} tiku_reader_profile_t;

/** @brief A viewer window, Help or a document: its page, history, chrome
 *         and reading state. */
typedef struct {
    tiku_workspace_t *workspace;
    tiku_window_t    *window;
    int              *dirty;
    /* Nonempty only when the last event changed hover chrome alone. */
    tiku_rect_t       hover_damage;
    int               screen_w, screen_h;
    /*
     * The page and its tables are on the heap, outside this structure,
     * which the app holds by value.
     */
    tiku_doc_t       *doc;
    tiku_scroll_t     bar;
    tiku_dialog_t     dialog;
    int               focus_link;
    int               laid_for;         /* the width the page was laid for */
    int               page_h;           /* and the height it is read in    */
    char              root[TIKU_PATH_MAX];
    char              page[TIKU_PATH_MAX];  /* under the root, or a        */
                                            /* document's absolute path    */
    char              status[256];
    char              wanted[TIKU_PATH_MAX];/* the last path handed over    */
    char              anchor[128];      /* a place to reach once laid out  */
    int               oversize;        /* the page had more than fitted   */
    struct {
        char page[TIKU_PATH_MAX];
        tiku_reader_help_position_t at;
    } back[TIKU_READER_HELP_HISTORY], forward[TIKU_READER_HELP_HISTORY];
    int nback, nforward;
    tiku_reader_help_open_fn opener;
    void *opener_ctx;
    struct help_reader *reader;
    int document_mode;             /* local documents, separate from Help */
    const tiku_reader_profile_t *profile;
    void (*choose_file)(void *ctx); /* explicit native Open-file request */
    void *choose_ctx;
} tiku_reader_help_window_t;

/** @brief Make @p c a TikuReader document window with no file open; its
 *         bookmarks are kept apart from Help's. */
void tiku_reader_document_window_init(tiku_reader_help_window_t *c,
    tiku_workspace_t *workspace, int screen_w, int screen_h, int *dirty,
    void (*choose_file)(void *ctx), void *ctx);
/** @brief Make @p c a document window under an application's identity;
 *         a NULL @p profile is TikuReader's. */
void tiku_reader_document_window_init_profile(tiku_reader_help_window_t *c,
    tiku_workspace_t *workspace, int screen_w, int screen_h, int *dirty,
    void (*choose_file)(void *ctx), void *ctx,
    const tiku_reader_profile_t *profile);
/** @brief Read the local file @p path into the window and open it; a
 *         failed read keeps the page shown and the history as they were,
 *         and says why.  @return 1 when it was read. */
int tiku_reader_document_window_file(tiku_reader_help_window_t *c, const char *path);
/** @brief Read the shown document again, back at the place it was read
 *         to and without a history entry; a failed read keeps what is
 *         shown.  @return 1 when reloaded. */
int tiku_reader_document_window_reload(tiku_reader_help_window_t *c);
/** @brief Maximize the window within @p area, the desk's usable area, or
 *         restore the frame it had before.  @return 1 when it changed. */
int tiku_reader_document_window_zoom(tiku_reader_help_window_t *c, tiku_rect_t area);
/** @brief Whether @p path's suffix, in any case, is one Open and document
 *         links accept: .md, .markdown, .html or .htm. */
int tiku_reader_document_file_supported(const char *path);

/** @brief Show generated HTML under @p title as a page of its own, kept
 *         among the last few reports and never written to a file.
 *  @return 1 when it is shown. */
int tiku_reader_help_window_html(tiku_reader_help_window_t *c,
                            const char *title, const char *html);
/** @brief Find @p query in the page's text, the next match or with
 *         @p backwards the one before.  @return 1 when there is one. */
int tiku_reader_help_window_find(tiku_reader_help_window_t *c,
                            const char *query, int backwards);
/** @brief Copy the selection, or with @p code the code block selected or
 *         in view; a range too large for the clipboard is refused whole.
 *  @return 1 when copied. */
int tiku_reader_help_window_copy(tiku_reader_help_window_t *c, int code);

/** @brief Empty, with @p root the folder the pages are read from. */
void tiku_reader_help_window_init(tiku_reader_help_window_t *c,
                               tiku_workspace_t *workspace,
                               const char *root, int screen_w, int screen_h,
                               int *dirty);

/** @brief Who opens a path that is not a page.  Both may be NULL. */
void tiku_reader_help_window_opener(tiku_reader_help_window_t *c,
                                 tiku_reader_help_open_fn fn, void *ctx);

/** @brief Open the window, or raise it when open: Help shows the folder's
 *         first page, or says there is none; a document window with nothing
 *         read shows its welcome page. */
void tiku_reader_help_window_open(tiku_reader_help_window_t *c);

/**
 * @brief Show @p page of the help folder, opening the window for it.
 *
 * The window opens on the folder's contents page first, then goes to
 * @p page as a link would; a page that is not there is reported in the
 * status line.
 */
void tiku_reader_help_window_open_page(tiku_reader_help_window_t *c,
                                    const char *page);

/** @brief Whether there is a help folder at all.  @return nonzero. */
int tiku_reader_help_window_ready(const tiku_reader_help_window_t *c);

/**
 * @brief Follow @p href from the page now shown.
 *
 * '#name' is a place in this page.  In Help, a plain file name is a page
 * beside it, refused if it climbs out of the help folder; a path beginning
 * '/' or "tiku:" goes to the opener; a web or mail address is refused.  In
 * a document window, a relative or absolute path to a local Markdown or
 * HTML file opens it here; anything else is refused.  A refusal is said in
 * the status line.
 */
tiku_reader_help_where_t tiku_reader_help_window_go(tiku_reader_help_window_t *c,
                                              const char *href);

/** @brief Step back to the page before, at the place it was left.
 *  @return 1 when it moved. */
int tiku_reader_help_window_back(tiku_reader_help_window_t *c);
/** @brief Step forward again after a step back.  @return 1 when it moved. */
int tiku_reader_help_window_forward(tiku_reader_help_window_t *c);

/** @brief Help: back to the folder's first page; a document window asks
 *         for a file to open instead.  @return 1 when it did. */
int tiku_reader_help_window_home(tiku_reader_help_window_t *c);

/** @brief What the window last said in its status line. */
const char *tiku_reader_help_window_status(const tiku_reader_help_window_t *c);
/** @brief The page shown: its name under the root, or a document's path. */
const char *tiku_reader_help_window_page(const tiku_reader_help_window_t *c);
/** @brief The path last handed to the opener. */
const char *tiku_reader_help_window_wanted(const tiku_reader_help_window_t *c);

/** @brief Close the window, stopping a folder search; the page is kept. */
void tiku_reader_help_window_close(tiku_reader_help_window_t *c);
/** @brief Close the window and free the page and the reading state. */
void tiku_reader_help_window_destroy(tiku_reader_help_window_t *c);
/** @brief The window, or NULL while it is closed. */
tiku_window_t *tiku_reader_help_window_handle(tiku_reader_help_window_t *c);

#endif /* TIKU_READER_WINDOW_H_ */
