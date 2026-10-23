/* The Markdown app uses real Reader controllers and the public app lifecycle.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include "../../apps/marker/tiku_marker.h"
#include "../../apps/reader/tiku_reader_private.h"
#include "tiku_icons.h"
#include "tiku_manifest.h"
#include "../fixtures/reader_wait.h"

extern const tiku_app_export_t tiku_app_v1;

static void fixture(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    assert(f && fputs(text, f) >= 0 && !fclose(f));
}

static void icon_check(void)
{
    const int sizes[] = {14, 16, 24, 32, 64};
    assert(tiku_icons_find("marker"));
    for (size_t i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        int n = sizes[i], teal = 0, paper = 0;
        const uint32_t *px = tiku_icons_bitmap("marker", n);
        assert(px && !(px[0] >> 24));
        for (int j = 0; j < n*n; j++) {
            unsigned r = (px[j] >> 16) & 255, g = (px[j] >> 8) & 255;
            unsigned b = px[j] & 255;
            if ((px[j] >> 24) < 240) continue;
            teal += r < 70 && g > 90 && b > 90;
            paper += r > 240 && g > 230 && b > 200;
        }
        assert(teal > n && paper > n);
    }
}

typedef struct { int opens, frames, closes, picks; } host_t;

static uint32_t host_open(void *ctx, const char *title, int w, int h)
{
    host_t *harness = ctx;
    assert(!strcmp(title, "TikuMarker") && w > 0 && h > 0);
    harness->opens++;
    return 7;
}
static int host_frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{
    host_t *harness = ctx;
    assert(id == 7 && px && w > 0 && h > 0);
    harness->frames++;
    return 1;
}
static void host_close(void *ctx, uint32_t id)
{
    assert(id == 7);
    ((host_t *)ctx)->closes++;
}
static int host_pick(void *ctx, uint32_t id, int mode, const char *dir,
                     const char *name)
{
    assert(id == 7 && mode == TIKU_APP_PICK_LOCAL_OPEN && dir && !name);
    ((host_t *)ctx)->picks++;
    return 1;
}
static int host_help(void *ctx, const char *path)
{
    assert(ctx && !strcmp(path, "apps/marker.html"));
    return 1;
}

static void app_lifecycle(const char *file, const char *missing)
{
    const tiku_app_descriptor_t *app = tiku_app_v1.app;
    assert(app && !strcmp(app->id, "marker") &&
           !strcmp(app->name, "TikuMarker"));
    host_t host = {0};
    tiku_app_services_t services = {.ctx=&host, .open=host_open,
        .frame=host_frame, .close=host_close, .pick=host_pick,
        .help_page=host_help, .startup_document=missing};
    void *state = NULL;
    /* A bad launch path must still show a usable window, not exit silently. */
    assert(!app->start(&state, &services) && state);
    assert(host.opens == 1 && host.frames > 0);
    tiku_event_t open = {.type=TIKU_EVENT_KEY_DOWN, .key='o',
                         .modifiers=TIKU_MOD_CMD};
    assert(!app->event_in(state, 7, &open) && host.picks == 1);
    app->picked(state, 7, NULL); /* a cancelled pick: no path, no window */
    assert(host.opens == 1 && !host.closes);
    app->picked(state, 7, file);
    app->tick(state, 0);
    assert(host.opens == 1 && host.frames > 1);
    assert(app->pick(state, 7, 100)); /* File > Close terminates the app */
    app->tick(state, 0);
    assert(host.closes == 1);
    app->stop(state);
    services.startup_document = file;
    assert(!app->start(&state, &services));
    app->closed(state, 7); /* native close follows the same cleanup path */
    assert(app->event_in(state, 7, &open));
    app->stop(state);
}

int main(void)
{
    char root[] = "/tmp/tiku-marker-XXXXXX";
    assert(mkdtemp(root));
    char path[1024], next[1024], missing[1024], config[1024];
    char marks[1100], readers[1100], custom[1100];
    snprintf(path, sizeof path, "%s/README.MD", root);
    snprintf(next, sizeof next, "%s/details.MARKDOWN", root);
    snprintf(missing, sizeof missing, "%s/missing.md", root);
    /* Marker's own variable is unset, so its file goes to a settings
     * folder it makes; Reader's variable keeps Reader's file away. */
    snprintf(config, sizeof config, "%s/config", root);
    snprintf(marks, sizeof marks, "%s/TikuMarkerBookmarks", config);
    snprintf(readers, sizeof readers, "%s/TikuReaderBookmarks", config);
    snprintf(custom, sizeof custom, "%s/custom-marks", root);
    setenv("XDG_CONFIG_HOME", config, 1);
    unsetenv("TIKU_MARKER_BOOKMARKS");
    setenv("TIKU_READER_BOOKMARKS", "", 1);
    setenv("TIKU_HELP_BOOKMARKS", "", 1);
    fixture(path, "# Sensor notes\n\nSome **readable** text.\n\n"
        "## Example\n\n```basic\nPRINT 42\n```\n\n"
        "| Item | Value |\n| --- | --- |\n| Clock | 32 |\n\n"
        "[Details](details.MARKDOWN#details)\n");
    fixture(next, "# Details\n\nSome readable text too.\n");
    tiku_manifest_t manifest;
    assert(!tiku_manifest_read("apps/marker/marker.app", &manifest, NULL, 0));
    assert(!strcmp(manifest.id, "marker") && !strcmp(manifest.icon, "marker"));
    assert(!strcmp(manifest.types, "text/markdown"));
    tiku_surface_t *surface = tiku_surface_new(1000, 760, TIKU_C_PANEL);
    assert(surface);
    tiku_workspace_t *workspace = tiku_workspace_new(surface);
    assert(workspace);
    tiku_reader_help_window_t marker, other, reader;
    int dirty = 0;
    tiku_reader_document_window_init_profile(&marker, workspace, 1000, 760,
        &dirty, NULL, NULL, &tiku_marker_profile);
    tiku_reader_help_window_open(&marker);
    tiku_workspace_draw(workspace);
    assert(!strcmp(marker.window->title, "TikuMarker"));
    assert(!strcmp(tiku_doc_title(marker.doc), "TikuMarker") &&
           !marker.page[0]);
    assert(marker.window->has_menus && marker.window->menus.nmenu == 3);
    icon_check();
    assert(tiku_reader_document_window_file(&marker, path));
    tiku_workspace_draw(workspace);
    assert(!strcmp(tiku_doc_title(marker.doc), "Sensor notes"));
    assert(tiku_reader_help_window_find(&marker, "readable", 0));
    assert(tiku_reader_help_window_copy(&marker, 1));
    assert(strstr(tiku_clipboard_text(), "PRINT 42"));
    tiku_reader_action(&marker, HELP_OUTLINE);
    tiku_workspace_draw(workspace);
    assert(marker.reader->nrows == 2);
    tiku_reader_bookmark(&marker);
    assert(marker.reader->nmarks == 1 && !marker.reader->marks_session);
    assert(!access(marks, F_OK) && access(readers, F_OK));
    tiku_reader_document_window_init_profile(&other, workspace, 1000, 760,
        &dirty, NULL, NULL, &tiku_marker_profile);
    assert(other.reader->nmarks == 1);
    tiku_reader_document_window_init(&reader, workspace, 1000, 760, &dirty,
                                     NULL, NULL);
    assert(reader.reader->nmarks == 0 &&
           !strcmp(reader.profile->name, "TikuReader"));
    /* TIKU_MARKER_BOOKMARKS names the file Marker keeps them in. */
    setenv("TIKU_MARKER_BOOKMARKS", custom, 1);
    tiku_reader_bookmark(&marker);
    assert(marker.reader->nmarks == 0 && !access(custom, F_OK));
    unsetenv("TIKU_MARKER_BOOKMARKS");
    assert(tiku_reader_help_window_go(&marker, "details.MARKDOWN#details") ==
           TIKU_READER_HELP_PAGE);
    assert(!strcmp(tiku_doc_title(marker.doc), "Details"));
    assert(tiku_reader_help_window_back(&marker));
    assert(!tiku_reader_document_window_file(&marker, missing) &&
           marker.status[0]);
    assert(!strcmp(tiku_doc_title(marker.doc), "Sensor notes"));
    assert(tiku_reader_help_window_go(&marker,
                                      "https://example.com/file.md") ==
           TIKU_READER_HELP_NOWHERE);
    fixture(path, "# Updated notes\n\nA readable update.\n");
    assert(tiku_reader_document_window_reload(&marker));
    assert(!strcmp(tiku_doc_title(marker.doc), "Updated notes"));
    tiku_field_start(&marker.reader->query, "readable");
    tiku_reader_action(&marker, HELP_SEARCH);
    reader_wait(&marker);
    assert(marker.reader->nresults == 2);
    tiku_reader_document_menu_pick(marker.window, HELP_TEXT_LARGER, &marker);
    tiku_reader_document_menu_pick(marker.window, HELP_READING_WIDTH, &marker);
    assert(marker.reader->text_size && marker.reader->reading_width == 720);
    tiku_workspace_draw(workspace);
    assert(!tiku_surface_png(surface, "/tmp/tiku-marker-test.png"));
    tiku_reader_help_window_destroy(&marker);
    tiku_reader_help_window_destroy(&other);
    tiku_reader_help_window_destroy(&reader);
    tiku_workspace_free(workspace);
    tiku_surface_free(surface);
    app_lifecycle(path, missing);
    assert(!unlink(path) && !unlink(next) && !unlink(marks));
    snprintf(marks, sizeof marks, "%s/TikuMarkerBookmarks.lock", config);
    assert(!unlink(marks) && !rmdir(config) && !unlink(custom));
    snprintf(custom, sizeof custom, "%s/custom-marks.lock", root);
    assert(!unlink(custom) && !rmdir(root));
    puts("PASS: Marker identity, Markdown, navigation, find, bookmarks, "
         "reload, search, icons and lifecycle");
    return 0;
}
