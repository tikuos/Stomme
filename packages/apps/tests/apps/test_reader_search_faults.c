/* Search admission failures must not erase completed results or leak jobs. */
#include <assert.h>
#include "../../apps/reader/tiku_reader_private.h"
#include "../fixtures/reader_wait.h"

static int fail_allocation, fail_post;

/** @brief calloc, failing once when a check arms it. */
static void *search_calloc(size_t n, size_t size)
{
    if (fail_allocation) { fail_allocation = 0; return NULL; }
    return calloc(n, size);
}

/** @brief tiku_work_post, refusing once when a check arms it. */
static int search_work_post(const tiku_work_job_t *job)
{
    if (fail_post) { fail_post = 0; return -1; }
    return tiku_work_post(job);
}

int main(void)
{
    static const tiku_reader_allocator_t failing = {malloc, search_calloc};
    tiku_reader_test_allocator(&failing);
    tiku_reader_test_post(search_work_post);
    char base[] = "/tmp/tiku-reader-search-faults.XXXXXX";
    assert(mkdtemp(base));
    char path[512]; snprintf(path, sizeof path, "%s/doc.md", base);
    FILE *f = fopen(path, "w"); assert(f);
    fputs("# Document\n\nneedle\n", f); fclose(f);
    setenv("TIKU_READER_BOOKMARKS", "", 1);
    setenv("TIKU_HELP_BOOKMARKS", "", 1);
    tiku_reader_help_window_t c; int dirty = 0;
    tiku_surface_t *s = tiku_surface_new(1000, 760, TIKU_C_PANEL);
    tiku_workspace_t *ws = tiku_workspace_new(s);
    tiku_reader_document_window_init(&c, ws, 1000, 760, &dirty, NULL, NULL);
    assert(tiku_reader_document_window_file(&c, path));
    tiku_field_start(&c.reader->query, "needle");
    tiku_reader_search(&c); reader_wait(&c);
    assert(c.reader->nresults == 1);
    fail_allocation = 1; tiku_reader_search(&c);
    assert(!fail_allocation); /* the search's own allocation took the fault */
    assert(!c.reader->search && c.reader->nresults == 1 &&
           strstr(c.status, "memory"));
    fail_post = 1; tiku_reader_search(&c);
    assert(!c.reader->search && c.reader->nresults == 1 &&
           strstr(c.status, "busy"));
    tiku_reader_search(&c); assert(c.reader->search);
    /* The folder walk succeeds; queueing the first files does not. */
    fail_post = 1; reader_wait(&c);
    assert(!fail_post && c.reader->limited && strstr(c.status, "incomplete"));
    tiku_reader_search(&c); reader_wait(&c);
    assert(!c.reader->limited && c.reader->nresults == 1);
    tiku_reader_help_window_destroy(&c); tiku_work_collect();
    assert(!tiku_work_busy());
    tiku_workspace_free(ws); tiku_surface_free(s);
    tiku_reader_test_post(NULL);
    tiku_reader_test_allocator(NULL);
    unlink(path); rmdir(base);
    puts("PASS: Reader search allocation/queue failures, preserved "
         "results, incomplete reporting and retry");
    return 0;
}
