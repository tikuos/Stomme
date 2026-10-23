/* tiku_marker_app.c -- TikuMarker, a read-only Markdown viewer: the
 * reader's document window started under TikuMarker's profile.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_app_product.h"
#include "tiku_marker.h"

const tiku_reader_profile_t tiku_marker_profile = {
    .name = TIKU_PRODUCT_MARKER_NAME, .icon = TIKU_PRODUCT_MARKER_ICON,
    .bookmark_env = TIKU_PRODUCT_MARKER_TIKU_MARKER_BOOKMARKS,
    .bookmark_file = TIKU_PRODUCT_MARKER_BOOKMARK_FILE,
    .help_page = TIKU_PRODUCT_MARKER_HELP_PAGE,
    .welcome_html = TIKU_PRODUCT_MARKER_WELCOME_TITLE
        "<p>Read Markdown documents, notes and project guides.</p>"
        "<h2>Open a Markdown file</h2>"
        "<p>Choose File &gt; Open or press Ctrl+O to open a .md or "
        ".markdown file. Headings, lists, tables and code blocks appear "
        "as a formatted document.</p>"
        "<h2>Read and navigate</h2>"
        "<p>Use View &gt; Outline to jump to a heading, Ctrl+F to find "
        "text, or View &gt; Toggle bookmark to save a reading position. "
        "View also offers larger text and a comfortable reading width.</p>"
        "<h2>Your files stay unchanged</h2>"
        TIKU_PRODUCT_MARKER_P_TIKUMARKER_IS_A_VIEWER_NOT_AN_EDITOR_TO_MAKE_CHANGES_OPEN
        "the file with Editor, save it, then press Ctrl+R here to reload. "
        "Local Markdown and HTML links are supported; scripts and web "
        "requests are not run.</p>"
};

/** @brief Start the shared document viewer as TikuMarker. */
static int start(void **state, const tiku_app_services_t *services)
{
    return tiku_reader_app_start(state, services, &tiku_marker_profile);
}

static const tiku_app_descriptor_t descriptor = {
    .id = TIKU_PRODUCT_MARKER_ID, .name = TIKU_PRODUCT_MARKER_NAME, .start = start,
    .stop = tiku_reader_app_stop, .event_in = tiku_reader_app_event,
    .tick = tiku_reader_app_tick, .pick = tiku_reader_app_pick,
    .picked = tiku_reader_app_picked, .closed = tiku_reader_app_closed
};
/** @brief The one symbol a loader looks for; see tiku_app.h. */
__attribute__((visibility("default"))) const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, sizeof(tiku_app_descriptor_t), &descriptor
};
