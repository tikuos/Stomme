/* Large desktop files must remain navigable, searchable and transactional. */
#include <assert.h>
#include <dirent.h>
#include "../../apps/reader/tiku_reader_private.h"
#include "../fixtures/reader_wait.h"

/** @brief Generate over a megabyte of Markdown with a unique tail and anchor. */
static void large_markdown(const char *path)
{
    FILE *f = fopen(path, "w"); assert(f);
    fputs("# Large guide\n\n", f);
    for (int i = 0; i < 12000; i++)
        fprintf(f, "Paragraph %d: this ordinary documentation contains enough "
                   "words to exercise a full desktop-sized reader.\n\n", i);
    fputs("## Tail section\n\nFINAL_NEEDLE beyond the old limits.\n\n"
          "```basic\nPRINT 123\n```\n", f);
    assert(!fclose(f));
}

/** @brief The declared 8 MiB source boundary is inclusive for ordinary HTML. */
static void at_source_limit(tiku_reader_help_window_t *c, const char *base)
{
    char path[1024]; snprintf(path, sizeof path, "%s/limit.html", base);
    FILE *f = fopen(path, "w"); assert(f);
    const char *tail = " LAST_AT_EIGHT_MIB";
    size_t bytes = TIKU_READER_READER_FILE_MAX - strlen(tail);
    for (size_t i = 0; i < bytes; i++) fputc(i % 5 == 4 ? ' ' : 'x', f);
    fputs(tail, f); assert(!fclose(f));
    assert(tiku_reader_document_window_file(c, path));
    tiku_workspace_draw(c->workspace);
    assert(tiku_reader_help_window_find(c, "LAST_AT_EIGHT_MIB", 0));
    assert(c->reader->match > 7 * 1024 * 1024);
    assert(!c->oversize && tiku_doc_check(c->doc));
}

/** @brief Leave nothing behind: these fixtures are megabytes of /tmp. */
static void sweep(const char *base)
{
    DIR *d = opendir(base);
    struct dirent *e;
    char path[1024];

    assert(d != NULL);
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(path, sizeof path, "%s/%s", base, e->d_name);
        assert(!unlink(path));
    }
    assert(!closedir(d) && !rmdir(base));
}

int main(void)
{
    char base[] = "/tmp/tiku-reader-large.XXXXXX"; assert(mkdtemp(base));
    char path[1024], huge[1024], bookmark[1024];
    snprintf(path, sizeof path, "%s/guide.md", base); large_markdown(path);
    snprintf(bookmark, sizeof bookmark, "%s/bookmarks", base);
    setenv("TIKU_READER_BOOKMARKS", bookmark, 1);
    setenv("TIKU_HELP_BOOKMARKS", "", 1);
    struct stat st; assert(!stat(path, &st) && st.st_size > 1024 * 1024);
    tiku_surface_t *surface = tiku_surface_new(1000, 760, TIKU_C_PANEL);
    tiku_workspace_t *workspace = tiku_workspace_new(surface); int dirty = 0;
    tiku_reader_help_window_t c;
    tiku_reader_document_window_init(&c, workspace, 1000, 760, &dirty, NULL, NULL);
    assert(tiku_reader_document_window_file(&c, path));
    tiku_workspace_draw(workspace);
    assert(!c.oversize && c.doc->nblock > 10000 && c.reader->length > 1024 * 1024);
    assert(c.doc->storage && c.doc->storage_bytes <= TIKU_READER_READER_MEMORY_MAX);
    assert(tiku_doc_check(c.doc) && !(c.doc->full &
        ~(TIKU_DOC_FACE_FLAT | TIKU_DOC_NARROW | TIKU_DOC_COLS_FOLDED)));
    assert(tiku_reader_help_window_find(&c, "FINAL_NEEDLE", 0));
    assert(c.reader->match > 1024 * 1024 && c.bar.value > 32767);
    assert(tiku_reader_help_window_go(&c, "#tail-section") == TIKU_READER_HELP_ANCHOR);
    assert(tiku_reader_help_window_copy(&c, 1));
    assert(strstr(tiku_clipboard_text(), "PRINT 123"));
    tiku_reader_bookmark(&c); assert(c.reader->nmarks == 1 && c.reader->marks[0].block > 10000);
    int back = c.nback;
    tiku_doc_place_t at = tiku_doc_place(c.doc, c.bar.value);
    assert(tiku_reader_document_window_reload(&c)); tiku_workspace_draw(workspace);
    assert(c.nback == back && tiku_doc_place(c.doc, c.bar.value).block == at.block);
    tiku_field_start(&c.reader->query, "FINAL_NEEDLE"); tiku_reader_search(&c);
    reader_wait(&c);
    assert(c.reader->nrows == 1 && !c.reader->limited);
    assert(strstr(c.reader->rows[0].snippet, "FINAL_NEEDLE"));
    snprintf(huge, sizeof huge, "%s/oversize.md", base);
    FILE *f = fopen(huge, "w"); assert(f);
    assert(!ftruncate(fileno(f), TIKU_READER_READER_FILE_MAX + 1)); assert(!fclose(f));
    tiku_doc_t *before = c.doc;
    assert(!tiku_reader_document_window_file(&c, huge));
    assert(c.doc == before && c.nback == back && strstr(c.status, "8 MiB"));
    f = fopen(huge, "w"); assert(f); fputs("# Before", f); fputc(0, f);
    fputs("hidden suffix", f); assert(!fclose(f));
    assert(!tiku_reader_document_window_file(&c, huge) && c.doc == before);
    assert(strstr(c.status, "binary"));
    tiku_reader_help_window_destroy(&c);
    tiku_reader_document_window_init(&c, workspace, 1000, 760, &dirty, NULL, NULL);
    assert(c.reader->nmarks == 1 && c.reader->marks[0].block > 10000);
    assert(tiku_reader_document_window_file(&c, path));
    tiku_workspace_draw(workspace);
    tiku_reader_action(&c, HELP_BOOKMARKS); tiku_workspace_draw(workspace);
    tiku_list_select_only(&c.reader->list, 0); tiku_reader_side_open(&c);
    assert(tiku_doc_place(c.doc, c.bar.value).block == at.block);
    at_source_limit(&c, base);
    tiku_reader_help_window_destroy(&c);
    tiku_workspace_free(workspace); tiku_surface_free(surface);
    sweep(base);
    puts("PASS: >1 MiB Markdown, tail search/anchors, copying, reload, bookmarks and safe refusal");
    return 0;
}
