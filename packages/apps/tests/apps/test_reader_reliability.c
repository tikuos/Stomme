/* Regression coverage for concurrent preferences, reflow and worker
 * lifetime. */
#include <assert.h>
#include <sys/file.h>
#include "../../apps/reader/tiku_reader_private.h"
#include "../fixtures/reader_wait.h"

static char base[128];
static void path_for(char *out, size_t cap, const char *name)
{ assert(snprintf(out, cap, "%s/%s", base, name) < (int)cap); }

static void file(const char *name, const char *text)
{
    char path[512]; path_for(path, sizeof path, name);
    FILE *f = fopen(path, "w"); assert(f); fputs(text, f); assert(!fclose(f));
}

static void open_file(tiku_reader_help_window_t *c, const char *name)
{
    char path[512]; path_for(path, sizeof path, name);
    assert(tiku_reader_document_window_file(c, path));
    tiku_workspace_draw(c->workspace);
}

static void bookmarks(tiku_workspace_t *ws)
{
    tiku_reader_help_window_t a, b, fresh; int dirty = 0;
    tiku_reader_document_window_init(&a, ws, 1000, 760, &dirty, NULL, NULL);
    tiku_reader_document_window_init(&b, ws, 1000, 760, &dirty, NULL, NULL);
    file("a.md", "# A\n\nneedle alpha\n"); file("b.md", "# B\n\nneedle beta\n");
    open_file(&a, "a.md"); open_file(&b, "b.md");
    tiku_reader_bookmark(&a); tiku_reader_bookmark(&b);
    tiku_reader_document_window_init(&fresh, ws, 1000, 760, &dirty, NULL, NULL);
    assert(fresh.reader->nmarks == 2);
    tiku_reader_bookmarks_load(&fresh); assert(fresh.reader->nmarks == 2);
    /* Removing from the stale first window must retain the second bookmark. */
    tiku_reader_bookmark(&a); tiku_reader_bookmarks_load(&fresh);
    assert(fresh.reader->nmarks == 1 &&
           strstr(fresh.reader->marks[0].page, "/b.md"));
    char lockpath[512]; path_for(lockpath, sizeof lockpath, "bookmarks.lock");
    int lock = open(lockpath, O_RDWR);
    assert(lock >= 0 && !flock(lock, LOCK_EX));
    tiku_reader_bookmark(&b); assert(strstr(b.status, "busy"));
    tiku_reader_bookmarks_load(&fresh); assert(fresh.reader->nmarks == 1);
    close(lock);
    tiku_reader_bookmark(&b); tiku_reader_bookmarks_load(&fresh);
    assert(!fresh.reader->nmarks);
    tiku_reader_help_window_destroy(&a); tiku_reader_help_window_destroy(&b);
    tiku_reader_help_window_destroy(&fresh);
}

/** @brief Bookmarks the file cannot take stay in the window for the rest
 *         of the session, and the saved list keeps what it had. */
static void bookmark_fallback(tiku_workspace_t *ws)
{
    char missing[512], folder[512], kept[512], lock[600];
    tiku_reader_help_window_t w, fresh; int dirty = 0;
    file("c.md", "# C\n\ngamma\n");
    /* No folder for the file: kept here, across later changes. */
    path_for(missing, sizeof missing, "missing/bookmarks");
    setenv("TIKU_READER_BOOKMARKS", missing, 1);
    tiku_reader_document_window_init(&w, ws, 1000, 760, &dirty, NULL, NULL);
    open_file(&w, "a.md"); tiku_reader_bookmark(&w);
    assert(w.reader->nmarks == 1 && strstr(w.status, "session only"));
    open_file(&w, "b.md"); tiku_reader_bookmark(&w);
    assert(w.reader->nmarks == 2 && strstr(w.status, "session only"));
    open_file(&w, "a.md"); tiku_reader_bookmark(&w);
    assert(w.reader->nmarks == 1 && strstr(w.reader->marks[0].page, "/b.md"));
    assert(access(missing, F_OK));
    tiku_reader_help_window_destroy(&w);
    /* The lock is taken but saving fails: the next change keeps it. */
    path_for(folder, sizeof folder, "readonly"); assert(!mkdir(folder, 0700));
    path_for(kept, sizeof kept, "readonly/bookmarks");
    snprintf(lock, sizeof lock, "%s.lock", kept);
    setenv("TIKU_READER_BOOKMARKS", kept, 1);
    tiku_reader_document_window_init(&w, ws, 1000, 760, &dirty, NULL, NULL);
    open_file(&w, "a.md"); tiku_reader_bookmark(&w);
    assert(w.reader->nmarks == 1 && !w.reader->marks_session);
    assert(!chmod(folder, 0500));
    open_file(&w, "b.md"); tiku_reader_bookmark(&w);
    assert(w.reader->nmarks == 2 &&
           strstr(w.status, "saving bookmarks failed"));
    open_file(&w, "c.md"); tiku_reader_bookmark(&w);
    assert(w.reader->nmarks == 3 && strstr(w.status, "session only"));
    assert(!chmod(folder, 0700));
    tiku_reader_document_window_init(&fresh, ws, 1000, 760, &dirty, NULL, NULL);
    assert(fresh.reader->nmarks == 1); /* the file kept its one bookmark */
    tiku_reader_help_window_destroy(&w);
    tiku_reader_help_window_destroy(&fresh);
    assert(!unlink(kept) && !unlink(lock) && !rmdir(folder));
}

static void reflow(tiku_reader_help_window_t *c)
{
    char path[512]; path_for(path, sizeof path, "long.html");
    FILE *f = fopen(path, "w"); assert(f); fputs("<h1>Long</h1><p>", f);
    for (int i = 0; i < 10000; i++) fputs("word ", f);
    fputs("TAIL_MARKER</p>", f); assert(!fclose(f));
    open_file(c, "long.html");
    int capacity = c->doc->limits.lines;
    c->reader->a = 2; c->reader->b = 6;
    c->window->frame.w = 180; tiku_workspace_draw(c->workspace);
    assert(c->doc->limits.lines > capacity);
    assert(!(c->doc->full & (TIKU_DOC_FULL_LINES | TIKU_DOC_FULL_PIECES)));
    assert(tiku_doc_check(c->doc));
    assert(c->reader->a == 2 && c->reader->b == 6);
    assert(tiku_reader_help_window_find(c, "TAIL_MARKER", 0));
    int visible = 0;
    for (int i = 0; i < c->doc->npiece; i++) {
        tiku_doc_piece_t *pc = &c->doc->piece[i];
        int start = c->reader->starts[pc->run] + pc->off -
                    c->doc->run[pc->run].off;
        if (c->reader->match >= start && c->reader->match < start + pc->len)
            visible = 1;
    }
    assert(visible && c->bar.value > 0);

    int ui_size = tiku_font_size();
    tiku_reader_document_menu_pick(c->window, HELP_TEXT_LARGER, c);
    tiku_workspace_draw(c->workspace);
    assert(c->reader->text_size > ui_size && tiku_font_size() == ui_size);
    assert(c->doc->laid_height == tiku_font_at(c->reader->text_size)->height);
    assert(!(c->doc->full & (TIKU_DOC_FULL_LINES | TIKU_DOC_FULL_PIECES)));
    tiku_reader_document_menu_pick(c->window, HELP_TEXT_RESET, c);
    assert(!c->reader->text_size && tiku_font_size() == ui_size);
    tiku_dialog_focus(&c->dialog, HELP_LOCATION);
    tiku_event_t zoom = {.type=TIKU_EVENT_KEY_DOWN, .key='+',
                         .modifiers=TIKU_MOD_CMD};
    tiku_window_send(c->window, &zoom);
    assert(c->reader->text_size > ui_size);
    zoom.key = '0'; tiku_window_send(c->window, &zoom);
    assert(!c->reader->text_size);
    c->window->frame.w = 1000;
    tiku_reader_document_menu_pick(c->window, HELP_READING_WIDTH, c);
    tiku_workspace_draw(c->workspace);
    assert(c->laid_for == 720);
    tiku_reader_document_menu_pick(c->window, HELP_READING_WIDTH, c);
    tiku_workspace_draw(c->workspace); assert(c->laid_for > 720);
}

static void images(tiku_reader_help_window_t *c)
{
    char path[512], name[48];
    tiku_surface_t *small = tiku_surface_new(20, 8, TIKU_C_NOTE);
    tiku_surface_t *large = tiku_surface_new(1024, 1024, TIKU_C_NOTE);
    assert(small && large);
    path_for(path, sizeof path, "images.html");
    FILE *f = fopen(path, "w"); assert(f);
    for (int i = 0; i < 8; i++)
        fprintf(f, "<img src=\"missing%d.png\" alt=\"missing\">", i);
    for (int i = 0; i < 16; i++) {
        snprintf(name, sizeof name, "picture%d.png", i);
        path_for(path, sizeof path, name);
        assert(!tiku_surface_png(i < 10 ? small : large, path));
        fprintf(f, "<img src=\"%s\" alt=\"picture\">", name);
    }
    assert(!fclose(f)); tiku_surface_free(small); tiku_surface_free(large);
    open_file(c, "images.html");
    assert(c->reader->nimages == 24);
    int w, h;
    assert(tiku_reader_image_size(c, "picture9.png", &w, &h) &&
           w == 20 && h == 8);
    for (int i = 10; i < 16; i++) {
        snprintf(name, sizeof name, "picture%d.png", i);
        tiku_reader_image_draw(c, name, (tiku_rect_t){0,0,4,4});
        assert(c->reader->image_bytes <= HELP_IMAGE_BYTES);
    }
    assert(!c->reader->images[18].image.px); /* first large image was evicted */
    tiku_reader_image_draw(c, "picture10.png", (tiku_rect_t){0,0,4,4});
    assert(c->reader->images[18].image.px &&
           c->reader->image_bytes <= HELP_IMAGE_BYTES);
}

static void search(tiku_reader_help_window_t *c)
{
    for (int i = 0; i < 12; i++) {
        char name[32]; snprintf(name, sizeof name, "search%d.md", i);
        file(name, "# Search document\n\nneedle match\n");
    }
    open_file(c, "a.md"); tiku_field_start(&c->reader->query, "needle");
    tiku_reader_search(c); assert(c->reader->search);
    for (int i = 0; i < 10000 && !c->reader->nresults; i++) {
        tiku_work_collect(); usleep(1000);
    }
    assert(c->reader->nresults > 0 && c->reader->search); /* incremental */
    tiku_list_select_only(&c->reader->list, 0); tiku_reader_side_open(c);
    assert(c->reader->side == 2 && c->reader->matches > 0);
    reader_wait(c); assert(c->reader->nresults == 14);
    tiku_reader_action(c, HELP_OUTLINE); tiku_workspace_draw(c->workspace);
    tiku_reader_action(c, HELP_RESULTS); assert(c->reader->nrows == 14);
    tiku_field_start(&c->reader->query, "needle"); tiku_reader_search(c);
    tiku_field_start(&c->reader->query, "TAIL_MARKER"); tiku_reader_search(c);
    reader_wait(c); assert(c->reader->nresults == 1);
    assert(strstr(c->reader->results[0].page, "long.html"));
    /* Edit > Search folder during a search starts one for the new words. */
    tiku_field_start(&c->reader->query, "needle"); tiku_reader_search(c);
    tiku_field_start(&c->reader->query, "TAIL_MARKER");
    tiku_reader_document_menu_pick(c->window, HELP_SEARCH, c);
    assert(c->reader->search &&
           !strcmp(c->reader->search_query, "TAIL_MARKER"));
    reader_wait(c); assert(c->reader->nresults == 1);
    /* The Find bar's search button reads Stop Search then, and stops it. */
    tiku_reader_document_find(c, 1); tiku_workspace_draw(c->workspace);
    tiku_dialog_focus(&c->dialog, HELP_PAGE);
    tiku_reader_search(c); assert(c->reader->search);
    help_layout_t l = tiku_reader_layout(c, tiku_window_content(c->window));
    tiku_event_t click = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                          .x = l.search.x + l.search.w / 2,
                          .y = l.search.y + l.search.h / 2};
    assert(l.search.w);
    tiku_window_send(c->window, &click);
    click.type = TIKU_EVENT_POINTER_UP; tiku_window_send(c->window, &click);
    assert(!c->reader->search && strstr(c->status, "cancelled"));
    tiku_reader_search(c); tiku_reader_action(c, HELP_CANCEL_SEARCH);
    assert(!c->reader->search && strstr(c->status, "cancelled"));
    tiku_reader_search(c); /* destroy while a job is queued or running */
}

int main(void)
{
    snprintf(base, sizeof base, "/tmp/tiku-reader-reliability.XXXXXX");
    assert(mkdtemp(base));
    char path[512]; path_for(path, sizeof path, "bookmarks");
    setenv("TIKU_READER_BOOKMARKS", path, 1);
    setenv("TIKU_HELP_BOOKMARKS", "", 1);
    tiku_surface_t *surface = tiku_surface_new(1100, 800, TIKU_C_PANEL);
    tiku_workspace_t *ws = tiku_workspace_new(surface); assert(ws);
    bookmarks(ws);
    bookmark_fallback(ws);
    path_for(path, sizeof path, "bookmarks");
    setenv("TIKU_READER_BOOKMARKS", path, 1);
    tiku_reader_help_window_t c; int dirty = 0;
    tiku_reader_document_window_init(&c, ws, 1000, 760, &dirty, NULL, NULL);
    reflow(&c); images(&c); search(&c);
    tiku_workspace_close(ws, c.window);
    assert(!c.window && !c.reader->search);
    tiku_reader_help_window_destroy(&c);
    for (int i = 0; i < 10000 && tiku_work_busy(); i++) {
        tiku_work_collect();
        usleep(1000);
    }
    assert(!tiku_work_busy());
    tiku_workspace_free(ws); tiku_surface_free(surface);
    DIR *dir = opendir(base); assert(dir); struct dirent *e;
    while ((e = readdir(dir))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        path_for(path, sizeof path, e->d_name); assert(!unlink(path));
    }
    closedir(dir); assert(!rmdir(base));
    puts("PASS: Reader bookmark merging, locking and session fallback, "
         "reflow, text zoom, reading width, image cache, asynchronous "
         "search, restart and cancellation");
    return 0;
}
