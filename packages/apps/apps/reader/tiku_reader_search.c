/* tiku_reader_search.c -- folder search on workers, and the outline.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_reader_private.h"

/** @brief A folder search, shared by its jobs on the workers.  Each job
 *         parses its own pages and uses no font or layout state; results
 *         reach the window only through the done callbacks, on the loop. */
typedef struct reader_search {
    /* The window, read on the loop only; NULL once cancelled or finished. */
    tiku_reader_help_window_t *owner;
    int refs;                       /* the window's and each queued job's */
    int mode, discovered, nfiles, scanned, nrows, limited;
    size_t bytes;
    char root[TIKU_PATH_MAX], query[TIKU_FIELD_MAX], key[TIKU_WORK_KEY];
    char files[HELP_ROWS][TIKU_PATH_MAX];
    help_row_t rows[HELP_ROWS];
} reader_search_t;

static int search_post(reader_search_t *j);

/** @brief How a search queues work: tiku_work_post unless a test set one. */
static int (*search_post_fn)(const tiku_work_job_t *job) = tiku_work_post;

void tiku_reader_test_post(int (*post)(const tiku_work_job_t *job))
{
    search_post_fn = post ? post : tiku_work_post;
}

/** @brief Drop one reference; the last one frees the search. */
static void search_unref(reader_search_t *j)
{
    if (!--j->refs) {
        free(j);
    }
}

/** @brief Collect the Markdown and HTML files under @p prefix of the
 *         search's folder, skipping hidden names, links and Help's locale
 *         folders: at most HELP_ROWS files, four folders deep and 8192
 *         entries, past which the search is marked limited. */
static void search_dir(reader_search_t *j, const char *prefix, int depth,
                       int *entries)
{
    char path[TIKU_PATH_MAX];
    if (depth > 4 || j->nfiles >= HELP_ROWS) { j->limited = 1; return; }
    if (snprintf(path, sizeof path, "%s/%s", j->root, prefix) >=
        (int)sizeof path) {
        j->limited = 1; return;
    }
    DIR *dir = opendir(path);
    if (!dir) { j->limited = 1; return; }
    struct dirent *entry;
    while (!tiku_work_cancelled(j) && (entry = readdir(dir))) {
        if (++*entries > 8192 || j->nfiles >= HELP_ROWS) {
            j->limited = 1; break;
        }
        if (entry->d_name[0] == '.') continue;
        char name[TIKU_PATH_MAX]; struct stat st;
        if (snprintf(name, sizeof name, "%s%s%s", prefix, *prefix ? "/" : "",
                     entry->d_name) >= (int)sizeof name ||
            snprintf(path, sizeof path, "%s/%s", j->root, name) >=
            (int)sizeof path) {
            j->limited = 1; continue;
        }
        if (lstat(path, &st) || S_ISLNK(st.st_mode)) continue;
        if (S_ISDIR(st.st_mode)) {
            /* Two-letter folders at Help's top are its locale bundles. */
            if (!j->mode && !*prefix && strlen(entry->d_name) == 2) continue;
            search_dir(j, name, depth + 1, entries);
        } else if (S_ISREG(st.st_mode) &&
                   tiku_reader_document_file_supported(name)) {
            snprintf(j->files[j->nfiles++], TIKU_PATH_MAX, "%s", name);
        }
    }
    closedir(dir);
}

/** @brief Search one file: read it within the search's 32 MiB budget,
 *         parse it without laying out, and add a row for its first match. */
static void search_page(reader_search_t *j, const char *name)
{
    char path[TIKU_PATH_MAX];
    /* A stand-in window, so tiku_reader_path() applies Help's confinement
     * and locale rules. */
    tiku_reader_help_window_t source = {.document_mode = j->mode};
    snprintf(source.root, sizeof source.root, "%s", j->root);
    if (j->mode) {
        char candidate[TIKU_PATH_MAX];
        if (snprintf(candidate, sizeof candidate, "%s/%s", j->root, name) >=
            (int)sizeof candidate ||
            !tiku_reader_document_path(candidate, path, sizeof path)) {
            j->limited = 1; return;
        }
    } else if (!tiku_reader_path(&source, name, path, sizeof path)) {
        j->limited = 1; return;
    }
    struct stat st;
    if (stat(path, &st) || st.st_size < 0 ||
        (uint64_t)st.st_size > 32u * 1024u * 1024u - j->bytes) {
        j->limited = 1; return;
    }
    const char *why;
    char *text = tiku_reader_source(path, &why);
    if (!text) { j->limited = 1; return; }
    size_t bytes = strlen(text);
    if (bytes > 32u * 1024u * 1024u - j->bytes) {
        free(text); j->limited = 1; return;
    }
    j->bytes += bytes;
    if (tiku_work_cancelled(j)) { free(text); return; }
    tiku_doc_t *doc = tiku_reader_document_text(name, text);
    free(text);
    if (!doc) { j->limited = 1; return; }
    size_t cap = (size_t)doc->ntext + doc->nblock + 1;
    char *plain = malloc(cap);
    if (!plain) { j->limited = 1; tiku_reader_doc_free(doc); return; }
    tiku_reader_text(doc, plain, cap, NULL);
    for (int i = 0; plain[i]; i++) {
        if (!(i % 4096) && tiku_work_cancelled(j)) break;
        if (!tiku_reader_match(plain + i, j->query)) continue;
        help_row_t *row = &j->rows[j->nrows++];
        snprintf(row->page, sizeof row->page, "%s", j->mode ? path : name);
        snprintf(row->title, sizeof row->title, "%.127s", tiku_doc_title(doc) ? tiku_doc_title(doc) : name);
        snprintf(row->snippet, sizeof row->snippet, "%.150s", plain + i);
        for (char *p = row->snippet; *p; p++) if (*p == '\n' || *p == '\t') *p = ' ';
        break;
    }
    free(plain); tiku_reader_doc_free(doc);
}

/** @brief A worker's turn: walk the folder first, then four files a turn. */
static void search_run(void *ctx)
{
    reader_search_t *j = ctx;
    if (!j->discovered) {
        int entries = 0;
        search_dir(j, "", 0, &entries);
        j->discovered = 1;
        return; /* Publish discovery before scanning the first batch. */
    }
    int end = j->scanned + 4;
    if (end > j->nfiles) end = j->nfiles;
    while (j->scanned < end && !tiku_work_cancelled(j)) {
        search_page(j, j->files[j->scanned]);
        j->scanned++;
    }
}

/** @brief Show the latest search's results in the sidebar. */
void tiku_reader_search_results(tiku_reader_help_window_t *c)
{
    struct help_reader *r = c->reader;
    if (!r) return;
    r->side = 2;
    r->nrows = r->nresults;
    memcpy(r->rows, r->results, (size_t)r->nrows * sizeof *r->rows);
    tiku_list_set_count(&r->list, r->nrows);
    if (c->dirty) *c->dirty = 1;
}

/** @brief On the loop: publish a turn's results, then queue the next turn,
 *         or release the window's reference when the search is over. */
static void search_done(void *ctx)
{
    reader_search_t *j = ctx;
    tiku_reader_help_window_t *c = j->owner;
    if (!c) return;
    struct help_reader *r = c->reader;
    r->nresults = j->nrows; r->limited = j->limited;
    r->search_bytes = j->bytes;
    memcpy(r->results, j->rows, (size_t)j->nrows * sizeof *j->rows);
    if (r->side == 2) tiku_reader_search_results(c);
    int more = j->scanned < j->nfiles;
    snprintf(c->status, sizeof c->status,
             "%s%d/%d documents; %d matching pages%s.",
             more ? "Searching: " : "Searched ", j->scanned, j->nfiles,
             j->nrows,
             j->limited ? " (incomplete: search limit or read failure)" : "");
    /* Once posted, the worker may change the result fields immediately. */
    if (more && !search_post(j)) {
        more = 0; r->limited = 1;
        tiku_reader_say(c, "Search incomplete: workers are busy. "
                        "Completed results remain available.");
    }
    if (!more) {
        r->search = NULL;
        j->owner = NULL;
        /* The window's reference goes; this job's goes in search_free. */
        search_unref(j);
    }
    if (c->dirty) *c->dirty = 1;
}

/** @brief Release a finished or cancelled job's reference. */
static void search_free(void *ctx)
{
    search_unref(ctx);
}

/** @brief Queue the search's next turn; the job holds a reference until
 *         its free callback.  @return 1 when queued, 0 when refused. */
static int search_post(reader_search_t *j)
{
    tiku_work_job_t work = {.run = search_run, .done = search_done,
                            .free = search_free, .ctx = j};
    snprintf(work.key, sizeof work.key, "%s", j->key);
    j->refs++;
    if (!search_post_fn(&work)) {
        return 1;
    }
    search_unref(j);
    return 0;
}

/** @brief Stop the running search; the results found so far stay. */
void tiku_reader_search_cancel(tiku_reader_help_window_t *c)
{
    struct help_reader *r = c->reader;
    if (!r || !r->search) return;
    reader_search_t *j = r->search;
    r->search = NULL; j->owner = NULL;
    tiku_work_cancel(j->key);
    search_unref(j);
    tiku_reader_say(c, "Search cancelled; completed results remain available.");
    if (c->dirty) *c->dirty = 1;
}

/** @brief Start a folder search on the workers, replacing a running one.
 *         Result rows keep the source page and a short matching context. */
void tiku_reader_search(tiku_reader_help_window_t *c)
{
    struct help_reader *r = c->reader;
    if (!r || !r->query.text[0]) {
        tiku_reader_say(c, "Enter words to search for.");
        return;
    }
    if (!c->root[0]) {
        tiku_reader_say(c, "Open a document before searching its folder.");
        return;
    }
    tiku_reader_search_cancel(c);
    reader_search_t *j = tiku_reader_calloc(1, sizeof *j);
    if (!j) {
        tiku_reader_say(c, "Not enough memory to start a search.");
        return;
    }
    j->owner = c; j->refs = 1; j->mode = c->document_mode;
    snprintf(j->root, sizeof j->root, "%s", c->root);
    snprintf(j->query, sizeof j->query, "%s", r->query.text);
    snprintf(j->key, sizeof j->key, "reader-search:%p", (void *)j);
    if (!search_post(j)) {
        search_unref(j);
        tiku_reader_say(c, "Search workers are busy. Try again.");
        return;
    }
    r->search = j;
    r->nresults = r->limited = 0; r->search_bytes = 0;
    snprintf(r->search_query, sizeof r->search_query, "%s", j->query);
    r->list.top = 0;
    tiku_reader_search_results(c);
    tiku_reader_say(c, "Looking for documents... "
                    "Choose Stop Search to cancel.");
}

/** @brief Build the outline from the page's H1 to H6 headings, leaving out
 *         those under a folded heading. */
void tiku_reader_outline(tiku_reader_help_window_t *c)
{
    struct help_reader *r = c->reader; if (!r || !c->doc) return;
    r->nrows = 0; int hidden = 0;
    for (int i = 0; i < c->doc->nblock && r->nrows < HELP_ROWS; i++) {
        const tiku_doc_block_t *b = &c->doc->block[i];
        int level = tiku_doc_heading_level(b);
        if (!level) continue;
        if (hidden && level > hidden) continue;
        hidden = r->folded[i] ? level : 0;
        help_row_t *row = &r->rows[r->nrows++]; memset(row, 0, sizeof *row); row->block = i;
        row->level = level;
        for (int j = i+1; j < c->doc->nblock; j++) {
            int child_level = tiku_doc_heading_level(&c->doc->block[j]);
            if (!child_level) continue;
            row->children = child_level > level; break;
        }
        int used = 0;
        for (int k = b->run0; k < b->run0 + b->nrun && used < 126; k++) {
            int n = c->doc->run[k].len; if (n > 126-used) n = 126-used;
            memcpy(row->title+used, c->doc->text+c->doc->run[k].off, n); used += n;
        }
        row->title[used] = 0;
    }
    tiku_list_set_count(&r->list, r->nrows);
}
