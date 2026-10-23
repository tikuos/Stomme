/* tiku_arcview_jobs.inl -- part of tiku_arcview.c: the worker jobs that
 * open an archive and extract from it.  Jobs on one archive share its key
 * and run one at a time; a done callback applies its result only while
 * the viewer still shows what the job was asked about.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */

/** @brief Drop a hold on the shared archive; the last one closes it. */
static void av_arc_unref(av_arc_t *d)
{
    if (d && --d->refs == 0) {
        tiku_archive_close(d->a);
        free(d);
    }
}

/** @brief Drop a hold on a job's progress; the last one frees it. */
static void av_progress_unref(av_progress_t *p)
{
    if (p && --p->refs == 0) free(p);
}

/** @brief A new progress, held by the viewer and its job, in place of
 *         the viewer's last one; @p doing names the job. */
static av_progress_t *av_progress_new(tiku_arcview_t *c, const char *doing)
{
    av_progress_t *p = calloc(1, sizeof *p);

    if (!p) return NULL;
    p->refs = 2;
    av_progress_unref(c->progress);
    c->progress = p;
    c->drawn = -1;
    snprintf(c->doing, sizeof c->doing, "%s", doing);
    return p;
}

/** @brief Let go of the shown archive and all that was built from it;
 *         a running job is asked to stop. */
static void av_forget(tiku_arcview_t *c)
{
    if (c->progress) atomic_store(&c->progress->stop, 1);
    av_arc_unref(c->arc);
    c->arc = NULL;
    av_tree_free(c->node, c->nnode);
    c->node = NULL;
    c->nnode = c->nrows = c->all_rows = c->cwd = 0;
    free(c->rows);
    c->rows = NULL;
    c->path[0] = c->made[0] = '\0';
    free(c->report);
    c->report = NULL;
    c->busy = 0;
    av_progress_unref(c->progress);
    c->progress = NULL;
    (void)tiku_list_set_count(&c->list, 0);
}

/** @brief Drop a hold on the viewer; the last one frees it. */
static void av_unref(tiku_arcview_t *c)
{
    if (--c->refs == 0) {
        av_forget(c);
        free(c);
    }
}

/** @brief Whether a job should stop: Stop pressed, or the job
 *         cancelled. */
static int av_stopped(const void *job, av_progress_t *p)
{
    return tiku_work_cancelled(job) || atomic_load(&p->stop);
}

/* ------------------------------------------------------------ opening  */

/** @brief The folder tree being built: nodes, and a hash to find a
 *         folder's child by its name. */
typedef struct {
    av_node_t *node;
    int n, cap;
    int *hash, hcap;                /* node + 1, 0 empty                 */
} av_build_t;

/** @brief Where a child named @p name (@p len bytes) of @p parent, a
 *         folder or not, hashes to. */
static unsigned av_hash(int parent, const char *name, size_t len, int dir)
{
    unsigned h = 2166136261u ^ (unsigned)parent ^ (unsigned)dir << 31;

    while (len--) h = (h ^ (unsigned char)*name++) * 16777619u;
    return h;
}

/** @brief @p parent's child called @p name, folder or file as @p dir
 *         says.  @return its node, or -1. */
static int av_child(const av_build_t *b, int parent, const char *name,
                    size_t len, int dir)
{
    unsigned at = av_hash(parent, name, len, dir) & (unsigned)(b->hcap - 1);

    for (; b->hash[at]; at = (at + 1) & (unsigned)(b->hcap - 1)) {
        const av_node_t *n = &b->node[b->hash[at] - 1];
        if (n->parent == parent && n->dir == dir &&
            strlen(n->name) == len && !memcmp(n->name, name, len))
            return b->hash[at] - 1;
    }
    return -1;
}

/** @brief Node @p i entered in the hash, which grows to stay half full.
 *  @return 0, or -1 out of memory. */
static int av_hash_put(av_build_t *b, int i)
{
    unsigned at;

    if (2 * (b->n + 1) > b->hcap) {
        int cap = b->hcap ? 2 * b->hcap : 1024, *old = b->hash;
        int oldcap = b->hcap;

        if (!(b->hash = calloc((size_t)cap, sizeof *b->hash))) {
            b->hash = old;
            return -1;
        }
        b->hcap = cap;
        for (int k = 0; k < oldcap; k++)
            if (old[k]) {
                const av_node_t *n = &b->node[old[k] - 1];
                at = av_hash(n->parent, n->name, strlen(n->name), n->dir) &
                     (unsigned)(cap - 1);
                while (b->hash[at]) at = (at + 1) & (unsigned)(cap - 1);
                b->hash[at] = old[k];
            }
        free(old);
    }
    at = av_hash(b->node[i].parent, b->node[i].name, strlen(b->node[i].name),
                 b->node[i].dir) & (unsigned)(b->hcap - 1);
    while (b->hash[at]) at = (at + 1) & (unsigned)(b->hcap - 1);
    b->hash[at] = i + 1;
    return 0;
}

/** @brief A new node under @p parent.  @return it, or -1. */
static int av_node_add(av_build_t *b, int parent, const char *name,
                       size_t len, int dir)
{
    av_node_t *n;

    if (b->n == b->cap) {
        int cap = b->cap ? 2 * b->cap : 64;
        av_node_t *more = realloc(b->node, (size_t)cap * sizeof *more);
        if (!more) return -1;
        b->node = more;
        b->cap = cap;
    }
    n = &b->node[b->n];
    memset(n, 0, sizeof *n);
    if (!(n->name = malloc(len + 1))) return -1;
    memcpy(n->name, name, len);
    n->name[len] = '\0';
    n->parent = parent;
    n->entry = -1;
    n->dir = dir;
    n->first = -1;
    n->mtime = INT64_MIN;
    n->next = parent >= 0 ? b->node[parent].first : -1;
    if (parent >= 0) {
        b->node[parent].first = b->n;
        b->node[parent].count++;
    }
    b->n++;
    return av_hash_put(b, b->n - 1) == 0 ? b->n - 1 : -1;
}

/** @brief Entry @p i placed in the tree: its folders made on the way;
 *         a refused name kept whole at the top, never as folders.
 *  @return 0, or -1 out of memory. */
static int av_place(av_build_t *b, const tiku_archive_entry_t *e, long i)
{
    const char *p = e->path, *slash;
    int at = 0, dir = e->type == TIKU_ARC_DIR, n;

    if (e->flags & TIKU_ARC_REFUSED) {
        slash = NULL;
        dir = 0;
    }
    while (!(e->flags & TIKU_ARC_REFUSED) && (slash = strchr(p, '/'))) {
        n = av_child(b, at, p, (size_t)(slash - p), 1);
        if (n < 0 && (n = av_node_add(b, at, p, (size_t)(slash - p), 1)) < 0)
            return -1;
        at = n;
        p = slash + 1;
    }
    n = av_child(b, at, p, strlen(p), dir);
    if (n < 0 && (n = av_node_add(b, at, p, strlen(p), dir)) < 0) return -1;
    b->node[n].entry = i;           /* a name twice: the later one wins */
    b->node[n].flags = e->flags;
    b->node[n].mtime = e->mtime;
    if (!dir) {
        b->node[n].size = e->size;
        b->node[n].packed = e->packed;
    }
    return 0;
}

/** @brief The folder tree @p a's paths make, its root named @p root;
 *         folders' sizes summed from their contents.  @return the nodes,
 *         @p n of them, or NULL. */
static av_node_t *av_tree_build(const tiku_archive_t *a, const char *root,
                                int *n)
{
    av_build_t b = { NULL, 0, 0, NULL, 0 };
    long count = tiku_archive_count(a), i;

    if (av_node_add(&b, -1, root, strlen(root), 1) < 0) goto fail;
    for (i = 0; i < count; i++)
        if (av_place(&b, tiku_archive_entry(a, i), i) != 0) goto fail;
    for (int k = b.n - 1; k > 0; k--) {     /* children after parents */
        av_node_t *p = &b.node[b.node[k].parent];
        p->size = p->size < 0 || b.node[k].size < 0 ? -1
                                                    : p->size + b.node[k].size;
        p->packed = p->packed < 0 || b.node[k].packed < 0
                    ? -1 : p->packed + b.node[k].packed;
    }
    free(b.hash);
    *n = b.n;
    return b.node;
fail:
    free(b.hash);
    av_tree_free(b.node, b.n);
    return NULL;
}

/** @brief Opening an archive: listed, and its tree built, on a worker. */
typedef struct {
    tiku_arcview_t *owner;
    int generation, nnode;
    tiku_archive_err_t err;
    char path[TIKU_PATH_MAX];
    tiku_archive_t *a;
    av_node_t *node;
    av_progress_t *progress;
} av_load_t;

/** @brief Listing's progress; nonzero asks the kit to stop. */
static int av_load_progress(void *ctx, int64_t done, int64_t total)
{
    av_load_t *j = ctx;

    atomic_store(&j->progress->done, done);
    atomic_store(&j->progress->total, total);
    return av_stopped(j, j->progress);
}

/** @brief On a worker: open and list, then build the folder tree. */
static void av_load_run(void *ctx)
{
    av_load_t *j = ctx;
    const char *leaf = strrchr(j->path, '/');

    j->err = tiku_archive_open(j->path, av_load_progress, j, &j->a);
    if (j->err != TIKU_ARCHIVE_OK) return;
    j->node = av_tree_build(j->a, leaf ? leaf + 1 : j->path, &j->nnode);
    if (!j->node) j->err = TIKU_ARCHIVE_NOMEM;
}

/** @brief Why an archive did not open, as a sentence. */
static void av_load_said(const av_load_t *j, char *out, size_t max)
{
    const char *kind = j->err == TIKU_ARCHIVE_NOT_YET
                       ? tiku_archive_identify(j->path) : NULL;

    if (kind)
        snprintf(out, max, TIKU_PRODUCT_ARCHIVE_FORMAT_MESSAGE
                 "yet.", kind);
    else if (j->err == TIKU_ARCHIVE_CANCELLED)
        snprintf(out, max, "Opening was stopped.");
    else
        snprintf(out, max, "Could not open that: %s.",
                 tiku_archive_strerror(j->err));
}

/** @brief How wide @p text is in the list's face, for fitting sizes. */
static int av_measure(const char *text, void *ctx)
{
    (void)ctx;
    return tiku_measure_width(TIKU_MEASURE_PLAIN, text);
}

/** @brief A size as Tracker writes one in a column @p width wide: bytes
 *         below a kibibyte, then KiB, MiB ...; "-" when unknown. */
static void av_bytes_fit(int64_t n, int width, char *out, size_t max)
{
    (void)tiku_attr_size_fit(n, width, av_measure, NULL, out, max);
}

/** @brief A size as Tracker writes one, with no width limit. */
static void av_bytes(int64_t n, char *out, size_t max)
{
    av_bytes_fit(n, 10000, out, max);
}

/** @brief On the loop: the listed archive shown, the old one let go. */
static void av_load_done(void *ctx)
{
    av_load_t *j = ctx;
    tiku_arcview_t *c = j->owner;
    const char *name = strrchr(j->path, '/');
    char said[320], size[32], packed[32];

    if (j->generation != c->generation || !c->workspace) return;
    if (j->err != TIKU_ARCHIVE_OK) {
        av_load_said(j, said, sizeof said);
        av_say(c, said);
        return;
    }
    av_forget(c);
    c->arc = calloc(1, sizeof *c->arc);
    c->rows = malloc((size_t)j->nnode * sizeof *c->rows);
    if (!c->arc || !c->rows) {
        free(c->arc);
        c->arc = NULL;
        av_say(c, "Not enough memory to show that archive.");
        return;
    }
    c->arc->a = j->a;
    c->arc->refs = 1;
    snprintf(c->arc->key, sizeof c->arc->key, "arc:%p", (void *)c->arc);
    c->node = j->node;
    c->nnode = j->nnode;
    j->a = NULL;
    j->node = NULL;
    j->nnode = 0;
    c->damaged = tiku_archive_damaged(c->arc->a);
    c->gz = tiku_archive_format(c->arc->a) == TIKU_ARCHIVE_GZ;
    snprintf(c->format, sizeof c->format, "%s",
             tiku_archive_format_name(c->arc->a));
    snprintf(c->path, sizeof c->path, "%s", j->path);
    if (c->window)
        snprintf(c->window->title, sizeof c->window->title,
                 TIKU_PRODUCT_ARCHIVE_TITLE_FORMAT, name ? name + 1 : j->path);
    av_enter(c, 0);
    av_bytes(c->node[0].size, size, sizeof size);
    av_bytes(c->node[0].packed, packed, sizeof packed);
    snprintf(said, sizeof said, "A %s: %ld entr%s, %s%s%s%s.%s", c->format,
             tiku_archive_count(c->arc->a),
             tiku_archive_count(c->arc->a) == 1 ? "y" : "ies", size,
             c->node[0].packed >= 0 ? " (" : "",
             c->node[0].packed >= 0 ? packed : "",
             c->node[0].packed >= 0 ? " packed)" : "",
             c->damaged ? " It is damaged: what came before the damage is "
                          "shown." : "");
    av_say(c, said);
    tiku_dialog_focus(&c->dialog, AV_LIST);
}

/** @brief After done, or instead of it: give back what was not taken. */
static void av_load_free(void *ctx)
{
    av_load_t *j = ctx;
    tiku_arcview_t *c = j->owner;

    tiku_archive_close(j->a);
    av_tree_free(j->node, j->nnode);
    c->loading = 0;
    if (c->progress == j->progress) {
        av_progress_unref(c->progress);
        c->progress = NULL;
    }
    av_progress_unref(j->progress);
    if (c->workspace) {
        av_menus(c);
        av_changed(c);
    }
    av_unref(c);
    free(j);
}

/** @brief Ask a worker to open @p path.  One at a time. */
static int av_load(tiku_arcview_t *c, const char *path)
{
    av_load_t *j;
    tiku_work_job_t work;

    if (!path || !*path || strlen(path) >= TIKU_PATH_MAX) return 0;
    if (c->loading || c->busy) {
        av_say(c, "An archive operation is still running. "
                  "Stop it or wait before opening another.");
        return 0;
    }
    if (!(j = calloc(1, sizeof *j))) return 0;
    j->owner = c;
    j->generation = c->generation;
    snprintf(j->path, sizeof j->path, "%s", path);
    if (!(j->progress = av_progress_new(c, "Reading"))) {
        free(j);
        return 0;
    }
    memset(&work, 0, sizeof work);
    work.run = av_load_run;
    work.done = av_load_done;
    work.free = av_load_free;
    work.ctx = j;
    c->refs++;
    c->loading = 1;
    if (tiku_work_post(&work) != 0) {
        av_load_free(j);
        av_say(c, "The workers are busy. Try again in a moment.");
        return 0;
    }
    av_say(c, "Opening...");
    av_menus(c);
    return 1;
}

/* --------------------------------------------------------- extracting  */

/** @brief Extracting: into a new folder, or opening one file from it. */
typedef struct {
    tiku_arcview_t *owner;
    int generation, metadata, open_after;
    int landed;                     /* done ran: it was not cancelled    */
    av_arc_t *arc;
    av_progress_t *progress;
    unsigned char *which;
    char strip[TIKU_PATH_MAX];
    char parent[TIKU_PATH_MAX], name[256];  /* the new folder, or "" to  */
                                            /* write into parent itself  */
    char dest[TIKU_PATH_MAX], leaf[256];    /* where it went; Open's file */
    tiku_archive_err_t err;
    long counts[TIKU_ARCHIVE_NOMEM + 1];    /* entries not made, by why  */
    char first_bad[256];                    /* the first of them          */
    tiku_archive_err_t first_why;
    char *report;
    size_t report_len;
    int report_incomplete;
} av_extract_t;

/** @brief Add "path: why" to the job's report.  Past 1 MiB, or out of
 *         memory, the report is marked incomplete and takes no more. */
static void av_report_append(av_extract_t *j, const char *path, const char *why)
{
    size_t n = strlen(path) + strlen(why) + 4;
    if (j->report_incomplete) return;
    if (n > (1u << 20) - j->report_len) { j->report_incomplete = 1; return; }
    char *more = realloc(j->report, j->report_len + n + 1);
    if (!more) { j->report_incomplete = 1; return; }
    j->report = more;
    j->report_len += (size_t)snprintf(more + j->report_len, n + 1,
                                     "%s: %s\n", path, why);
}

/** @brief Extraction's progress; nonzero asks the kit to stop. */
static int av_extract_progress(void *ctx, int64_t done, int64_t total)
{
    av_extract_t *j = ctx;

    atomic_store(&j->progress->done, done);
    atomic_store(&j->progress->total, total);
    return av_stopped(j, j->progress);
}

/** @brief An entry not made: counted by why and added to the report;
 *         the first that is not a replaced duplicate is kept by name. */
static void av_extract_report(void *ctx, long entry, tiku_archive_err_t why)
{
    av_extract_t *j = ctx;
    const tiku_archive_entry_t *e = tiku_archive_entry(j->arc->a, entry);

    if ((unsigned)why > TIKU_ARCHIVE_NOMEM) return;
    j->counts[why]++;
    if (e) av_report_append(j, e->path, tiku_archive_strerror(why));
    if (!j->first_bad[0] && why != TIKU_ARCHIVE_REPLACED && e) {
        snprintf(j->first_bad, sizeof j->first_bad, "%s", e->path);
        j->first_why = why;
    }
}

/** @brief On a worker: the folder made, the chosen entries put in it. */
static void av_extract_run(void *ctx)
{
    av_extract_t *j = ctx;
    tiku_archive_extract_t opt;

    if (av_stopped(j, j->progress)) {
        j->err = TIKU_ARCHIVE_CANCELLED;
        return;
    }

    if (j->name[0]) {
        if (tiku_archive_new_folder(j->parent, j->name, j->dest,
                                    sizeof j->dest) != 0) {
            j->err = TIKU_ARCHIVE_WRITE;
            return;
        }
    } else {
        snprintf(j->dest, sizeof j->dest, "%s", j->parent);
    }
    memset(&opt, 0, sizeof opt);
    opt.which = j->which;
    opt.strip = j->strip[0] ? j->strip : NULL;
    opt.metadata = j->metadata;
    opt.progress = av_extract_progress;
    opt.report = av_extract_report;
    opt.ctx = j;
    j->err = tiku_archive_extract(j->arc->a, j->dest, &opt);
}

/** @brief What an extraction came to, as a sentence. */
static void av_extract_said(const av_extract_t *j, char *out, size_t max)
{
    const char *folder = strrchr(j->dest, '/');
    long bad = 0;

    folder = folder ? folder + 1 : j->dest;
    for (int k = 1; k <= TIKU_ARCHIVE_NOMEM; k++)
        if (k != TIKU_ARCHIVE_REPLACED) bad += j->counts[k];
    if (j->err == TIKU_ARCHIVE_CANCELLED && !j->dest[0])
        snprintf(out, max, "Stopped before anything was extracted.");
    else if (j->err == TIKU_ARCHIVE_CANCELLED)
        snprintf(out, max, "Stopped. What was extracted so far is in "
                 "\"%.120s\".", folder);
    else if (j->err == TIKU_ARCHIVE_WRITE && !j->first_bad[0])
        snprintf(out, max, "Could not make the folder to extract into.");
    else if (bad > 0)
        snprintf(out, max, "Extracted into \"%.80s\", but %ld not: %.80s -- "
                 "%s.", folder, bad, j->first_bad,
                 tiku_archive_strerror(j->first_why));
    else if (j->counts[TIKU_ARCHIVE_REPLACED])
        snprintf(out, max, "Extracted into \"%.120s\". %ld name%s came "
                 "twice; the later of each was kept.", folder,
                 j->counts[TIKU_ARCHIVE_REPLACED],
                 j->counts[TIKU_ARCHIVE_REPLACED] == 1 ? "" : "s");
    else if (j->err != TIKU_ARCHIVE_OK)
        snprintf(out, max, "Extraction stopped: %s.",
                 tiku_archive_strerror(j->err));
    else
        snprintf(out, max, "Extracted into \"%.160s\".", folder);
}

/** @brief On the loop: said, and a file asked to be opened handed on. */
static void av_extract_done(void *ctx)
{
    av_extract_t *j = ctx;
    tiku_arcview_t *c = j->owner;
    char said[sizeof c->status], file[TIKU_PATH_MAX];
    int n;

    j->landed = 1;
    if (j->generation != c->generation || !c->workspace) return;
    av_extract_said(j, said, sizeof said);
    free(c->report);
    size_t cap = strlen(said) + strlen(j->dest) + j->report_len + 200;
    c->report = malloc(cap);
    if (c->report)
        snprintf(c->report, cap, "%s\nDestination: %s\n%s%s", said,
                 j->dest[0] ? j->dest : "(none)", j->report ? j->report : "",
                 j->report_incomplete ? "\nReport incomplete: memory or "
                                        "1 MiB report limit reached.\n" : "");
    if (j->dest[0]) snprintf(c->made, sizeof c->made, "%s", j->dest);
    if (j->open_after && j->err == TIKU_ARCHIVE_OK) {
        n = snprintf(file, sizeof file, "%s/%s", j->dest, j->leaf);
        if (n > 0 && (size_t)n < sizeof file &&  /* not a cut-off path */
            c->host.open_path && c->host.open_path(c->host.ctx, file))
            snprintf(said, sizeof said, "Opened %.100s. Copy kept in %.100s; "
                     "edits do not update the archive.", j->leaf, j->dest);
        else
            snprintf(said, sizeof said, "Extracted %.200s, but nothing "
                     "opened it.", j->leaf);
        av_say(c, said);
        return;
    }
    av_extract_said(j, said, sizeof said);
    av_say(c, said);
}

/** @brief After done, or instead of it: the job's holds let go. */
static void av_extract_free(void *ctx)
{
    av_extract_t *j = ctx;
    tiku_arcview_t *c = j->owner;

    if (j->generation == c->generation) {
        c->busy = 0;
        if (!j->landed && c->workspace)     /* stopped before it began */
            av_say(c, "Stopped before anything was extracted.");
        if (c->progress == j->progress) {
            av_progress_unref(c->progress);
            c->progress = NULL;
        }
    }
    av_progress_unref(j->progress);
    av_arc_unref(j->arc);
    free(j->which);
    free(j->report);
    if (c->workspace) {
        av_menus(c);
        av_changed(c);
    }
    av_unref(c);
    free(j);
}

/** @brief Post @p j under the archive's key, holding the archive and the
 *         viewer.  @return 1 when it was queued. */
static int av_extract_post(tiku_arcview_t *c, av_extract_t *j,
                           const char *doing)
{
    tiku_work_job_t work;

    j->owner = c;
    j->generation = c->generation;
    j->metadata = c->meta;
    if (!(j->progress = av_progress_new(c, doing))) {
        free(j->which);
        free(j);
        return 0;
    }
    j->arc = c->arc;
    c->arc->refs++;
    c->refs++;
    memset(&work, 0, sizeof work);
    work.run = av_extract_run;
    work.done = av_extract_done;
    work.free = av_extract_free;
    work.ctx = j;
    snprintf(work.key, sizeof work.key, "%s", c->arc->key);
    c->busy = 1;
    if (tiku_work_post(&work) != 0) {
        av_extract_free(j);
        av_say(c, "The workers are busy. Try again in a moment.");
        return 0;
    }
    av_menus(c);
    av_changed(c);
    return 1;
}

/** @brief The folder the archive is in. */
static void av_archive_dir(const tiku_arcview_t *c, char *out, size_t max)
{
    const char *slash = strrchr(c->path, '/');

    if (slash == NULL) snprintf(out, max, ".");
    else if (slash == c->path) snprintf(out, max, "/");
    else snprintf(out, max, "%.*s", (int)(slash - c->path), c->path);
}

/** @brief Extract Selected To... or Extract All To...: the Save panel
 *         asks where the new folder goes and what it is called. */
static void av_ask_where(tiku_arcview_t *c)
{
    char dir[TIKU_PATH_MAX], name[256];

    if (!c->host.pick) return;
    av_archive_dir(c, dir, sizeof dir);
    tiku_archive_folder_name(c->path, name, sizeof name);
    c->host.pick(c->host.ctx, 1, dir, name);
}

/**
 * @brief Extract everything (@p all) or the rows chosen into a new folder:
 *        at @p to, the Save panel's answer, or beside the archive under
 *        its name, asking where when that folder cannot be written.  A
 *        gzip's one file goes beside it, under a new name when its own is
 *        taken.  @return 1 when it started.
 */
static int av_extract(tiku_arcview_t *c, int all, const char *to)
{
    av_extract_t *j;
    long count = tiku_archive_count(c->arc->a);
    int any = 0;

    if (c->busy) {
        av_say(c, "An extraction is under way.");
        return 0;
    }
    if (!(j = calloc(1, sizeof *j))) return 0;
    j->which = av_which(c, all, count);
    for (long i = 0; j->which && i < count && !any; i++) any = j->which[i];
    if (!any) {
        free(j->which);
        free(j);
        av_say(c, "Nothing chosen to extract.");
        return 0;
    }
    if (!all) av_node_path(c->node, c->cwd, j->strip, sizeof j->strip);
    if (to) {
        const char *slash = strrchr(to, '/');
        snprintf(j->parent, sizeof j->parent, "%.*s",
                 slash ? (int)(slash - to) : 1, slash ? to : ".");
        snprintf(j->name, sizeof j->name, "%s", slash ? slash + 1 : to);
    } else {
        av_archive_dir(c, j->parent, sizeof j->parent);
        if (!c->gz) tiku_archive_folder_name(c->path, j->name,
                                             sizeof j->name);
        if (access(j->parent, W_OK) != 0) {
            free(j->which);
            free(j);
            av_say(c, "The archive's folder cannot be written to; choose "
                   "where to extract.");
            av_ask_where(c);
            return 0;
        }
    }
    av_say(c, all ? "Extracting everything..." : "Extracting...");
    return av_extract_post(c, j, "Extracting");
}

/** @brief Extract node @p node's entry into a new folder beside the
 *         archive and open it in its program.  The copy is kept: the
 *         program it was handed to may still have it open. */
static int av_open_entry(tiku_arcview_t *c, int node)
{
    av_extract_t *j;

    if (c->busy) {
        av_say(c, "An extraction is under way.");
        return 0;
    }
    if (!(j = calloc(1, sizeof *j)) ||
        !(j->which = calloc((size_t)tiku_archive_count(c->arc->a), 1))) {
        free(j);
        return 0;
    }
    j->which[c->node[node].entry] = 1;
    j->open_after = 1;
    av_node_path(c->node, c->node[node].parent, j->strip, sizeof j->strip);
    av_archive_dir(c, j->parent, sizeof j->parent);
    if (access(j->parent, W_OK) != 0) {
        free(j->which);
        free(j);
        av_say(c, "This folder is read-only. Use Extract Selected To..., "
                  "then open the extracted file.");
        return 0;
    }
    tiku_archive_folder_name(c->path, j->name, sizeof j->name);
    snprintf(j->leaf, sizeof j->leaf, "%s", c->node[node].name);
    av_say(c, "Opening...");
    return av_extract_post(c, j, "Opening");
}
