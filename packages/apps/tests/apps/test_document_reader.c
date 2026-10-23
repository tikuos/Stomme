/* TikuReader: local navigation and input without weakening bundled Help. */
#include <assert.h>
#include <limits.h>
#include "tiku_icons.h"
#include "../../apps/reader/tiku_reader_private.h"
#include "../fixtures/reader_wait.h"

static int requests;

/** @brief Test the Open gesture without opening a platform file picker. */
static void choose(void *ctx) { assert(ctx == &requests); requests++; }

/** @brief Write a document fixture under a unique temporary test directory. */
static void fixture(const char *root, const char *name, const char *text)
{
    char path[1024]; snprintf(path, sizeof path, "%s/%s", root, name);
    FILE *f = fopen(path, "w"); assert(f); fputs(text, f); assert(!fclose(f));
}

/** @brief Deliver real window keyboard events, including command modifiers. */
static void press(tiku_reader_help_window_t *c, unsigned key, unsigned mods)
{
    tiku_event_t ev = {.type=TIKU_EVENT_KEY_DOWN, .key=key, .modifiers=mods};
    tiku_window_send(c->window, &ev);
}

/** @brief Rejections must retain both the current page and forward history. */
static void rejected(tiku_reader_help_window_t *c, const char *href)
{
    char before[TIKU_PATH_MAX]; strcpy(before, c->page);
    int back = c->nback, forward = c->nforward;
    assert(tiku_reader_help_window_go(c, href) == TIKU_READER_HELP_NOWHERE);
    assert(!strcmp(before, c->page) && back == c->nback && forward == c->nforward);
    assert(c->status[0]);
}

/** @brief Reload preserves the paragraph and reads past the old source limit. */
static void long_document(tiku_reader_help_window_t *c, const char *base)
{
    char path[1024]; snprintf(path, sizeof path, "%s/long.html", base);
    FILE *f = fopen(path, "w"); assert(f);
    fputs("<h1>Long document</h1>", f);
    for (int i = 0; i < 60; i++) fprintf(f, "<p>Paragraph %d for reading.</p>", i);
    assert(!fclose(f));
    assert(tiku_reader_document_window_file(c, path));
    tiku_workspace_draw(c->workspace);
    tiku_scroll_to(&c->bar, 600);
    tiku_doc_place_t at = tiku_doc_place(c->doc, c->bar.value);
    assert(at.block > 0);
    int back = c->nback, forward = c->nforward;
    assert(tiku_reader_document_window_reload(c));
    tiku_workspace_draw(c->workspace);
    assert(tiku_doc_place(c->doc, c->bar.value).block == at.block);
    assert(c->nback == back && c->nforward == forward);
    f = fopen(path, "a"); assert(f);
    for (int i = 0; i < TIKU_READER_HELP_PAGE_MAX; i++) fputc('x', f);
    assert(!fclose(f));
    assert(tiku_reader_document_window_reload(c) && !c->oversize);
    assert(c->reader->length > TIKU_READER_HELP_PAGE_MAX);
}

/** @brief Exercise the same atomic Unicode commits supplied by the desktop. */
static void type_text(tiku_reader_help_window_t *c, const char *text)
{
    tiku_text_input_t input = {0}; tiku_event_t ev;
    assert(tiku_text_input_set(&input, text, strlen(text)));
    while (tiku_text_input_next(&input, &ev)) tiku_window_send(c->window, &ev);
}

/** @brief Hit-testing uses the browser chrome's drawing rectangles. */
static void click_control(tiku_reader_help_window_t *c, tiku_rect_t rect)
{
    tiku_event_t ev = {.type=TIKU_EVENT_POINTER_DOWN, .button=1,
        .x=rect.x+rect.w/2, .y=rect.y+rect.h/2};
    tiku_window_send(c->window, &ev);
    ev.type = TIKU_EVENT_POINTER_UP; tiku_window_send(c->window, &ev);
}

/** @brief Find and Location preserve command, focus and history behavior. */
static void browser_controls(tiku_workspace_t *workspace, const char *base)
{
    tiku_reader_help_window_t c; int dirty = 0;
    tiku_reader_document_window_init(&c, workspace, 1000, 760,
                                  &dirty, choose, &requests);
    tiku_reader_help_window_open(&c); tiku_workspace_draw(workspace);
    help_layout_t l = tiku_reader_layout(&c, tiku_window_content(c.window));
    assert(!l.find.w && !l.side.w && !l.copy.w && !l.bookmark.w);
    int height = l.page.h;
    click_control(&c, l.find_toggle);
    assert(c.reader->find_visible && c.dialog.focus == HELP_FIND);
    l = tiku_reader_layout(&c, tiku_window_content(c.window));
    assert(l.find.w > 0 && l.find.y > l.page.y+l.page.h);
    assert(l.page.h < height);
    click_control(&c, l.find_close);
    assert(!c.reader->find_visible && c.dialog.focus == HELP_PAGE);
    tiku_reader_document_menu_pick(c.window, HELP_FIND, &c);
    assert(c.reader->find_visible && c.dialog.focus == HELP_FIND);
    press(&c, TIKU_KEY_ESCAPE, 0);
    press(&c, 'f', TIKU_MOD_CMD); type_text(&c, "quiet");
    press(&c, TIKU_KEY_RETURN, 0); assert(c.reader->matches == 1);
    press(&c, TIKU_KEY_ESCAPE, 0); assert(!c.reader->find_visible);
    char path[TIKU_PATH_MAX];
    snprintf(path, sizeof path, "%s/index.html", base);
    press(&c, 'l', TIKU_MOD_CMD); assert(c.dialog.focus == HELP_LOCATION);
    type_text(&c, path); press(&c, TIKU_KEY_RETURN, 0);
    assert(!strcmp(c.page, path) && c.dialog.focus == HELP_PAGE);
    assert(!strcmp(c.reader->location.text, path));
    press(&c, 'l', TIKU_MOD_CMD); type_text(&c, "https://example.com/test.md");
    press(&c, TIKU_KEY_RETURN, 0);
    assert(!strcmp(c.page, path) && !c.nback && !c.nforward);
    assert(strstr(c.status, "does not open web"));
    assert(c.dialog.focus == HELP_LOCATION);
    press(&c, TIKU_KEY_ESCAPE, 0);
    assert(!strcmp(c.reader->location.text, path));
    fixture(base, "café #100%.md", "# Unicode address\n\nAn offline page.\n");
    press(&c, 'l', TIKU_MOD_CMD); type_text(&c, "café #100%.md");
    click_control(&c, tiku_reader_layout(&c, tiku_window_content(c.window)).go);
    assert(!strcmp(tiku_doc_title(c.doc), "Unicode address") && c.nback == 1);
    press(&c, TIKU_KEY_LEFT, TIKU_MOD_OPTION);
    assert(!strcmp(c.page, path) && c.nforward == 1);
    assert(!strcmp(c.reader->location.text, path));
    press(&c, 'l', TIKU_MOD_CMD); type_text(&c, "missing.md");
    press(&c, TIKU_KEY_RETURN, 0);
    assert(!strcmp(c.page, path) && c.nforward == 1);
    press(&c, TIKU_KEY_ESCAPE, 0);
    click_control(&c, tiku_reader_layout(&c, tiku_window_content(c.window)).location);
    assert(c.dialog.focus == HELP_LOCATION);
    press(&c, 'a', TIKU_MOD_CMD); press(&c, 'c', TIKU_MOD_CMD);
    assert(!strcmp(tiku_clipboard_text(), path));
    assert(tiku_clipboard_import("not the address", 15));
    tiku_reader_document_menu_pick(c.window, HELP_COPY, &c);
    assert(!strcmp(tiku_clipboard_text(), path));
    press(&c, TIKU_KEY_ESCAPE, 0);
    c.window->frame.w = 410; press(&c, 'f', TIKU_MOD_CMD);
    tiku_workspace_draw(workspace);
    l = tiku_reader_layout(&c, tiku_window_content(c.window));
    assert(l.location.w > 100 && l.find.w > 70);
    assert(l.find.x+l.find.w <= l.prev.x);
    c.window->frame.w = 180; tiku_workspace_draw(workspace);
    l = tiku_reader_layout(&c, tiku_window_content(c.window));
    assert(l.location.w > 0 && l.find.w > 0 && !l.prev.w);
    tiku_reader_help_window_destroy(&c);
}

/** @brief Long absolute paths remain complete, never truncated into another file. */
static void browser_long_address(tiku_workspace_t *workspace, const char *base)
{
    char folder[TIKU_PATH_MAX], part[96];
    snprintf(folder, sizeof folder, "%s", base);
    memset(part, 'a', sizeof part-1); part[sizeof part-1] = 0;
    for (int i = 0; i < 3; i++) {
        strcat(folder, "/"); strcat(folder, part);
        assert(!mkdir(folder, 0700));
    }
    fixture(folder, "deep.md", "# Deep document\n");
    char path[TIKU_PATH_MAX];
    int n = snprintf(path, sizeof path, "%s/deep.md", folder);
    assert(n >= TIKU_FIELD_MAX && n < TIKU_PATH_MAX);
    tiku_reader_help_window_t c; int dirty = 0;
    tiku_reader_document_window_init(&c, workspace, 1000, 760,
                                  &dirty, choose, &requests);
    assert(tiku_reader_document_window_file(&c, path));
    assert(!strcmp(c.page, path) && !strcmp(c.reader->location.text, "deep.md"));
    press(&c, 'l', TIKU_MOD_CMD); type_text(&c, path);
    assert(!strcmp(c.reader->location.text, "deep.md"));
    assert(strstr(c.status, "too long"));
    press(&c, TIKU_KEY_RETURN, 0);
    assert(!strcmp(c.page, path) && !c.nback);
    tiku_reader_help_window_destroy(&c);
}

/** @brief Frame toggling preserves the exact original rectangle and paragraph. */
static void browser_zoom(tiku_workspace_t *workspace, const char *base)
{
    tiku_reader_help_window_t c; int dirty = 0;
    char path[TIKU_PATH_MAX]; snprintf(path, sizeof path, "%s/long.html", base);
    tiku_reader_document_window_init(&c, workspace, 1000, 760,
                                  &dirty, choose, &requests);
    assert(tiku_reader_document_window_file(&c, path));
    tiku_workspace_draw(workspace); tiku_scroll_to(&c.bar, 600);
    tiku_rect_t original = c.window->frame, area = {12,24,900,710};
    tiku_doc_place_t place = tiku_doc_place(c.doc, c.bar.value);
    assert(tiku_reader_document_window_zoom(&c, area));
    assert(c.window->frame.x == 14 && c.window->frame.y == 26);
    assert(c.window->frame.w == 896 && c.window->frame.h == 706);
    tiku_workspace_draw(workspace);
    assert(tiku_doc_place(c.doc, c.bar.value).block == place.block);
    assert(tiku_reader_document_window_zoom(&c, area));
    assert(!memcmp(&c.window->frame, &original, sizeof original));
    assert(!c.window->zoom_valid);
    assert(!tiku_reader_document_window_zoom(&c, (tiku_rect_t){0,0,0,0}));
    assert(!memcmp(&c.window->frame, &original, sizeof original));
    /* A manual resize establishes a new restore frame on the next zoom. */
    assert(tiku_reader_document_window_zoom(&c, area));
    c.window->frame.w -= 140; original = c.window->frame;
    assert(tiku_reader_document_window_zoom(&c, area));
    assert(tiku_reader_document_window_zoom(&c, area));
    assert(!memcmp(&c.window->frame, &original, sizeof original));
    tiku_reader_help_window_destroy(&c);
}

/** @brief Scalable icon controls stay labelled, compact and hover-discoverable. */
static void browser_toolbar(tiku_workspace_t *workspace)
{
    tiku_reader_help_window_t c; int dirty = 0;
    tiku_reader_document_window_init(&c, workspace, 1000, 760,
                                  &dirty, choose, &requests);
    tiku_reader_help_window_open(&c); tiku_workspace_draw(workspace);
    help_layout_t l = tiku_reader_layout(&c, tiku_window_content(c.window));
    assert(l.back.w <= 34 && l.back.h <= 34);
    assert(l.location.x > l.home.x+l.home.w && l.location.w > 300);
    assert(l.location.y < l.back.y+l.back.h);
    assert(l.location.x+l.location.w <= l.go.x);
    assert(l.go.x+l.go.w <= l.find_toggle.x);
    const char *icons[] = {"nav_back", "nav_forward", "nav_reload",
                          "folder_open", "magnifier", "reader"};
    for (int i = 0; i < 6; i++) {
        assert(tiku_icons_bitmap(icons[i], 16));
        assert(tiku_icons_bitmap(icons[i], 24));
        assert(tiku_icons_bitmap(icons[i], 32));
    }
    tiku_event_t ev = {.type=TIKU_EVENT_POINTER_MOVE,
        .x=l.home.x+l.home.w/2, .y=l.home.y+l.home.h/2};
    tiku_window_send(c.window, &ev);
    assert(c.reader->hover_tool == HELP_HOME);
    char facts[8192];
    tiku_workspace_controls(workspace, c.window, facts, sizeof facts);
    assert(strstr(facts, "Open") && strstr(facts, "Forward"));
    ev.x = -1; ev.y = -1; tiku_window_send(c.window, &ev);
    assert(!c.reader->hover_tool);
    const int sizes[] = {10,12,14,16}, widths[] = {180,410,820};
    c.reader->find_visible = 1;
    for (int f = 0; f < 4; f++) {
        tiku_font_set_size(sizes[f]);
        for (int w = 0; w < 3; w++) {
            c.window->frame.w = widths[w];
            tiku_workspace_draw(workspace);
            l = tiku_reader_layout(&c, tiku_window_content(c.window));
            assert(l.home.x+l.home.w <= l.find_toggle.x);
            assert(l.location.w > 0 && l.find.w > 0 && l.page.h > 0);
            assert(l.location.x+l.location.w <= l.go.x);
        }
    }
    tiku_font_set_size(12);
    tiku_reader_help_window_destroy(&c);
}

/** @brief Preview the same book artwork at menu, list and application sizes. */
static void reader_icon_preview(void)
{
    const tiku_icon_t *art = tiku_icons_find("reader");
    assert(art && art->len > 4 && !memcmp(art->data, "ncif", 4));
    tiku_surface_t *s = tiku_surface_new(256, 112, TIKU_C_PANEL);
    assert(s);
    const int sizes[] = {16,24,32,64}, positions[] = {16,58,108,164};
    for (int i = 0; i < 4; i++) {
        char label[16]; snprintf(label, sizeof label, "%d px", sizes[i]);
        assert(tiku_icon_paint(s, "reader", positions[i],
                              (80-sizes[i])/2+4, sizes[i]));
        const uint32_t *pixels = tiku_icons_bitmap("reader", sizes[i]);
        int opaque = 0;
        for (int p = 0; p < sizes[i]*sizes[i]; p++)
            if ((pixels[p] >> 24) > 200) opaque++;
        assert(opaque > sizes[i]*sizes[i]/4 && !(pixels[0] >> 24));
        tiku_text_line_draw(s, (tiku_rect_t){positions[i]-4,88,60,18},
                            TIKU_MEASURE_PLAIN, label, TIKU_C_TEXT);
    }
    assert(!tiku_surface_png(s, "/tmp/tiku-reader-icon.png"));
    tiku_surface_free(s);
}

/** @brief Hover does no work when unchanged; bounded paints equal full ones. */
static void browser_hover(tiku_workspace_t *workspace, tiku_surface_t *s)
{
    tiku_reader_help_window_t c;
    int dirty = 0;
    tiku_reader_document_window_init(&c, workspace, 1000, 760,
                                  &dirty, choose, &requests);
    tiku_reader_help_window_open(&c);
    tiku_reader_document_find(&c, 1);
    tiku_workspace_draw(workspace);
    size_t bytes = (size_t)s->w*s->h*sizeof *s->px;
    tiku_rgb_t *copy = malloc(bytes); assert(copy);
    int ids[] = {HELP_HOME, HELP_DONE, HELP_FIND_CLOSE, 0};
    for (int j = 0; j < 4; j++) {
        tiku_event_t ev = {.type=TIKU_EVENT_POINTER_MOVE, .x=-1, .y=-1};
        for (int i = 0; i < c.dialog.count; i++)
            if (c.dialog.control[i].id == ids[j]) {
                ev.x = c.dialog.control[i].rect.x+3;
                ev.y = c.dialog.control[i].rect.y+3;
            }
        dirty = 0;
        tiku_window_send(c.window, &ev);
        assert(dirty && c.hover_damage.w > 0);
        tiku_clip_damage(s, &c.hover_damage);
        tiku_workspace_draw(workspace);
        tiku_clip_damage(s, NULL);
        memcpy(copy, s->px, bytes);
        tiku_workspace_draw(workspace);
        assert(!memcmp(copy, s->px, bytes));
        dirty = 0;
        for (int i = 0; i < 1000; i++) tiku_window_send(c.window, &ev);
        assert(!dirty && !c.hover_damage.w);
    }
    free(copy);
    help_layout_t l = tiku_reader_layout(&c, tiku_window_content(c.window));
    tiku_event_t drag = {.type=TIKU_EVENT_POINTER_DOWN, .button=1,
        .x=l.home.x+3, .y=l.home.y+3};
    int before = requests;
    tiku_window_send(c.window, &drag);
    assert(tiku_dialog_pressed(&c.dialog, HELP_HOME));
    drag.type = TIKU_EVENT_POINTER_MOVE; drag.x = -1; drag.y = -1;
    tiku_window_send(c.window, &drag);
    assert(!tiku_dialog_pressed(&c.dialog, HELP_HOME) && !c.hover_damage.w);
    drag.type = TIKU_EVENT_POINTER_UP; tiku_window_send(c.window, &drag);
    assert(requests == before); /* Dragging away still cancels the button. */
    tiku_reader_help_window_destroy(&c);
    puts("PASS: 4,000 unchanged hovers skip repaint; tip damage matches redraw");
}

int main(void)
{
    char base[TIKU_PATH_MAX] = "/tmp/tiku-document-reader.XXXXXX";
    char real[PATH_MAX];

    assert(mkdtemp(base));
    /* Resolved, because the code under test resolves what it is given:
     * on a host where /tmp is a symlink (macOS points it at
     * /private/tmp) every assertion below would be comparing the name
     * handed in against the name handed back. */
    assert(realpath(base, real));
    snprintf(base, sizeof base, "%s", real);
    char path[1200], sub[1024], marks[1024];
    snprintf(sub, sizeof sub, "%s/sub", base); assert(!mkdir(sub, 0700));
    snprintf(marks, sizeof marks, "%s/bookmarks", base);
    setenv("TIKU_READER_BOOKMARKS", marks, 1);
    setenv("TIKU_HELP_BOOKMARKS", "", 1);
    setenv("TIKU_LANG", "fr", 1); /* ordinary files are never substituted */
    fixture(base, "index.html", "<h1>Root guide</h1><p>Shared words</p>");
    fixture(base, "notes.MD", "# Notes\n\nShared **words** and café.\n\n"
        "## Example\n\n```basic\nPRINT 42\n```\n\n"
        "[Details](sub/detail%20%23one.HTM#details)\n");
    fixture(sub, "detail #one.HTM", "<h1 id=\"details\">Details</h1>"
        "<p>Shared words</p><a href=\"../notes.MD#example\">Notes</a>"
        "<script>hidden script</script><img src=\"pixel.png\" alt=\"pixel\">");
    fixture(base, "empty.md", "");
    fixture(base, "script.sh", "echo NEVER_EXECUTE");
    snprintf(path, sizeof path, "%s/pipe.md", base); assert(!mkfifo(path, 0600));
    snprintf(path, sizeof path, "%s/fr", base); assert(!mkdir(path, 0700));
    fixture(path, "notes.MD", "# Wrong localized substitution\n");
    tiku_surface_t *im = tiku_surface_new(20, 8, TIKU_RGB(200,10,20)); assert(im);
    snprintf(path, sizeof path, "%s/pixel.png", sub);
    assert(!tiku_surface_png(im, path)); tiku_surface_free(im);
    tiku_surface_t *surface = tiku_surface_new(1000, 760, TIKU_C_PANEL);
    tiku_workspace_t *workspace = tiku_workspace_new(surface); int dirty = 0;
    tiku_reader_help_window_t c;
    tiku_reader_document_window_init(&c, workspace, 1000, 760, &dirty, choose, &requests);
    tiku_reader_help_window_open(&c); tiku_workspace_draw(workspace);
    assert(c.window && !strcmp(c.window->title, "TikuReader"));
    assert(!c.page[0] && !c.nback && !c.nforward && !requests);
    assert(c.window->has_menus && c.window->menus.nmenu == 3);
    press(&c, 'o', TIKU_MOD_CMD); assert(requests == 1);
    assert(!tiku_reader_document_window_reload(&c));
    snprintf(path, sizeof path, "%s/notes.MD", base);
    assert(tiku_reader_document_window_file(&c, path)); tiku_workspace_draw(workspace);
    assert(!strcmp(tiku_doc_title(c.doc), "Notes") && !c.nback);
    assert(tiku_reader_help_window_find(&c, "shared words", 0));
    assert(tiku_reader_help_window_copy(&c, 1));
    assert(strstr(tiku_clipboard_text(), "PRINT 42"));
    tiku_reader_action(&c, HELP_OUTLINE); tiku_workspace_draw(workspace);
    assert(c.reader->nrows == 2);
    assert(tiku_reader_help_window_go(&c, tiku_doc_link_href(c.doc, 0)) == TIKU_READER_HELP_PAGE);
    tiku_workspace_draw(workspace);
    assert(!strcmp(tiku_doc_title(c.doc), "Details") && c.nback == 1);
    assert(!strstr(c.reader->text, "hidden script"));
    /* The image was decoded: its pixels are the fixture's red. */
    const tiku_image_t *pixel = &c.reader->images[0].image;
    assert(c.reader->nimages == 1 && pixel->px && pixel->w == 20 &&
           pixel->h == 8 &&
           (pixel->px[0] & 0xFFFFFFu) == TIKU_RGB(200, 10, 20));
    assert(tiku_reader_help_window_go(&c, "../notes.MD#example") == TIKU_READER_HELP_PAGE);
    tiku_workspace_draw(workspace);
    assert(!strcmp(tiku_doc_title(c.doc), "Notes"));
    assert(tiku_reader_help_window_back(&c));
    assert(!strcmp(tiku_doc_title(c.doc), "Details"));
    rejected(&c, "https://example.com/doc.html");
    rejected(&c, "file:///etc/passwd"); rejected(&c, "tiku:/clock");
    rejected(&c, "//server/share.md"); rejected(&c, "../script.sh");
    rejected(&c, "../pipe.md"); rejected(&c, "../missing.md");
    rejected(&c, "javascript:run()"); rejected(&c, "../notes.MD%00.sh");
    rejected(&c, "../notes%xx.MD"); rejected(&c, "../notes.MD#%00");
    assert(!tiku_reader_path(&c, "../notes.MD", path, sizeof path));
    snprintf(path, sizeof path, "%s/escape.png", sub);
    assert(!symlink("../index.html", path));
    assert(!tiku_reader_path(&c, "escape.png", path, sizeof path));
    tiku_reader_bookmark(&c); assert(c.reader->nmarks == 1);
    assert(strstr(c.reader->marks[0].page, "detail #one.HTM"));
    assert(tiku_reader_help_window_forward(&c));
    tiku_reader_action(&c, HELP_BOOKMARKS); tiku_workspace_draw(workspace);
    tiku_list_select_only(&c.reader->list, 0); tiku_reader_side_open(&c);
    assert(!strcmp(tiku_doc_title(c.doc), "Details")); /* literal # in saved path */
    assert(tiku_reader_help_window_go(&c, "../notes.MD") == TIKU_READER_HELP_PAGE);
    tiku_field_start(&c.reader->query, "Shared words"); tiku_reader_search(&c);
    reader_wait(&c);
    assert(c.reader->nrows == 3);
    for (int i = 0; i < c.reader->nrows; i++) assert(c.reader->rows[i].page[0] == '/');
    int back = c.nback;
    fixture(base, "notes.MD", "# Revised notes\n\nReloaded text.\n");
    press(&c, 'r', TIKU_MOD_CMD);
    assert(!strcmp(tiku_doc_title(c.doc), "Revised notes") && c.nback == back);
    snprintf(path, sizeof path, "%s/notes.MD", base); assert(!unlink(path));
    assert(!tiku_reader_document_window_reload(&c));
    assert(!strcmp(tiku_doc_title(c.doc), "Revised notes") && c.nback == back);
    assert(tiku_reader_help_window_go(&c, "empty.md") == TIKU_READER_HELP_PAGE);
    assert(c.reader->length == 0 && !c.oversize);
    assert(tiku_reader_help_window_go(&c, "sub/detail%20%23one.HTM") == TIKU_READER_HELP_PAGE);
    c.reader->side = 1; tiku_workspace_draw(workspace);
    assert(!tiku_surface_png(surface, "/tmp/tiku-document-reader.png"));
    c.window->frame.w = 410; tiku_workspace_draw(workspace);
    tiku_reader_help_window_close(&c); tiku_reader_help_window_open(&c);
    assert(!strcmp(tiku_doc_title(c.doc), "Details")); /* in-session reopen */
    tiku_reader_help_window_destroy(&c);
    tiku_reader_document_window_init(&c, workspace, 1000, 760, &dirty, choose, &requests);
    assert(c.reader->nmarks == 1 && !c.page[0]); /* bookmarks, not automatic reopening */
    tiku_reader_help_window_open(&c); tiku_workspace_draw(workspace);
    tiku_reader_action(&c, HELP_BOOKMARKS); tiku_workspace_draw(workspace);
    tiku_list_select_only(&c.reader->list, 0); tiku_reader_side_open(&c);
    assert(!strcmp(tiku_doc_title(c.doc), "Details"));
    long_document(&c, base);
    tiku_reader_help_window_destroy(&c);
    browser_controls(workspace, base);
    browser_long_address(workspace, base);
    browser_zoom(workspace, base);
    browser_toolbar(workspace);
    browser_hover(workspace, surface);
    reader_icon_preview();
    /* The reader must not turn Help's slash links into local file reads. */
    tiku_reader_help_window_init(&c, workspace, base, 1000, 760, &dirty);
    tiku_reader_help_window_open(&c);
    assert(tiku_reader_help_window_go(&c, "../index.html") == TIKU_READER_HELP_NOWHERE);
    assert(c.reader->nmarks == 0);
    tiku_reader_help_window_destroy(&c);
    tiku_workspace_free(workspace); tiku_surface_free(surface);
    puts("PASS: TikuReader navigation, input, reload, search, bookmarks, images and local-only policy");
    return 0;
}
