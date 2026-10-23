/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_html_view.h - an embeddable offline HTML document view.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_HTML_VIEW_H_
#define TIKU_HTML_VIEW_H_
#include "tiku_html_document.h"
#include "tiku_html_text.h"
#include "tiku_doc_paint.h"
#include "tiku_event.h"

/** @brief Handle a non-anchor link; the borrowed href is valid during the call. */
typedef void (*tiku_html_link_fn)(void *ctx, const char *href);

/** @brief A view's owned or borrowed model and interaction state. */
typedef struct {
    tiku_doc_t *doc;             /**< owned after load, borrowed after bind */
    char *text;                 /**< flattened UTF-8 text */
    int *starts;                /**< flattened byte offset for each run */
    int length, owned, valid_options;
    size_t tools_bytes;         /**< owned flattened text and run mapping allocation */
    int a, b, dragging, block, down_link; /**< selection byte ends and pointer capture */
    int scroll, focus_link;
    int match, match_len, matches;
    char query[256];
    tiku_rect_t viewport;
    tiku_html_options_t options;
    tiku_doc_paint_t paint;
    tiku_doc_image_fn image_size;
    tiku_html_link_fn link;
    void *link_ctx;
} tiku_html_view_t;

/** @brief Initialize an empty view; supplied options are copied. */
void tiku_html_view_init(tiku_html_view_t *view, const tiku_html_options_t *options);
/** @brief Release owned model/text storage; borrowed storage is untouched. */
void tiku_html_view_free(tiku_html_view_t *view);
/** @brief Transactionally load HTML from memory, with an optional default stylesheet.
 *  @return 1, or 0 with the previous page retained; input strings are not retained. */
int tiku_html_view_load(tiku_html_view_t *view, const char *html, const char *default_css);
/** @brief Borrow an existing model and flattened text; these must outlive the binding.
 *  @note starts has at least doc->nrun entries and text has length bytes plus NUL. */
void tiku_html_view_bind(tiku_html_view_t *view, tiku_doc_t *doc,
                           char *text, int length, int *starts);
/** @brief Install optional image callbacks; Interface Kit does not decode or fetch resources. */
void tiku_html_view_resources(tiku_html_view_t *view, tiku_doc_image_fn size,
                                tiku_doc_draw_image_fn draw, void *ctx);
/** @brief Resize/reflow within the viewport while keeping the reading position. */
void tiku_html_view_resize(tiku_html_view_t *view, tiku_rect_t viewport);
/** @brief Set a clamped vertical scroll offset and return its value. */
int tiku_html_view_scroll(tiku_html_view_t *view, int offset);
/** @brief Draw backgrounds, text, find highlights, selection and focused links. */
void tiku_html_view_draw(tiku_html_view_t *view, tiku_surface_t *surface, tiku_rect_t viewport);
/** @brief Handle routed pointer, wheel and keyboard events; return whether consumed.
 *  @note The host routes keyboard input to its focused view. */
int tiku_html_view_event(tiku_html_view_t *view, const tiku_event_t *event);
/** @brief Find the next/previous ASCII-case-insensitive match, wrapping at the document end. */
int tiku_html_view_find(tiku_html_view_t *view, const char *query, int backwards);
/** @brief Copy selected UTF-8 bytes into out; return required capacity including NUL.
 *  @note Insufficient capacity leaves out untouched. NULL out queries the required size. */
size_t tiku_html_view_selection(const tiku_html_view_t *view, char *out, size_t capacity);

#endif
