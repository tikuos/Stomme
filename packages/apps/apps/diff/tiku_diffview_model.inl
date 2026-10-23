/* tiku_diffview_model.inl -- part of tiku_diffview.c: the comparing.
 * Files are read and diffed, folders walked and archives extracted into a
 * private folder on a worker, one job at a time per window; a job touches
 * only what it was given, and the loop applies what it made.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */

#define DV_TEXT_MAX  (64L << 20)        /* a bigger file compares as bytes */
#define DV_DEPTH     64                 /* folders deep a walk goes        */
#define DV_CHUNK     65536

/* ------------------------------------------------------------ models  */

/** @brief Free two files' comparison. */
static void dv_files_free(dv_files_t *f)
{
    if (!f) return;
    for (int s = 0; s < 2; s++) {
        free(f->text[s]);
        free(f->line[s]);
    }
    tiku_diff_free(&f->d);
    free(f->row);
    free(f->change);
    free(f->end);
    free(f);
}

/** @brief Free two folders' comparison. */
static void dv_folders_free(dv_folders_t *d)
{
    if (!d) return;
    for (long i = 0; i < d->n; i++) free(d->e[i].rel);
    free(d->e);
    free(d);
}

/** @brief The last part of @p path; "empty input" for NULL. */
static const char *dv_leaf(const char *path)
{
    const char *slash = path ? strrchr(path, '/') : NULL;

    return slash && slash[1] ? slash + 1 : path ? path : "empty input";
}

/** @brief Whether the job has been asked to stop. */
static int dv_stopped(const dv_progress_t *p)
{
    return p && atomic_load(&p->stop);
}

/* ------------------------------------------------------------- files  */

/** @brief @p path read whole into @p out, when it is no bigger than
 *         DV_TEXT_MAX; NULL reads as nothing.  @return 0, 1 when it is
 *         bigger and was left unread, -1 when it could not be read. */
static int dv_read(const char *path, char **out, size_t *len, int64_t *size)
{
    FILE *f;
    struct stat st;
    size_t got;

    *out = NULL;
    *len = 0;
    *size = 0;
    if (!path) return (*out = calloc(1, 1)) ? 0 : -1;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    *size = (int64_t)st.st_size;
    if (st.st_size > DV_TEXT_MAX) return 1;
    if (!(f = fopen(path, "rb"))) return -1;
    *out = malloc((size_t)st.st_size + 1);
    got = *out ? fread(*out, 1, (size_t)st.st_size, f) : 0;
    int bad = ferror(f) || fgetc(f) != EOF;
    if (fclose(f) != 0) bad = 1;
    if (!*out || got != (size_t)st.st_size || bad) {
        free(*out);
        *out = NULL;
        return -1;
    }
    (*out)[got] = '\0';
    *len = got;
    return 0;
}

/** @brief Whether @p x and @p y hold the same bytes, or are two links
 *         to the same target; their sizes into @p sx and @p sy.
 *  @return 1 the same, 0 not, -1 when either could not be read. */
static int dv_same_file(const char *x, const char *y, int64_t *sx,
                        int64_t *sy)
{
    struct stat a, b;
    char *buf;
    FILE *fa = NULL, *fb = NULL;
    int same = -1;
    int64_t read_bytes = 0;

    if (lstat(x, &a) != 0 || lstat(y, &b) != 0) return -1;
    *sx = (int64_t)a.st_size;
    *sy = (int64_t)b.st_size;
    if (S_ISLNK(a.st_mode) || S_ISLNK(b.st_mode)) {
        char ta[DV_PATH], tb[DV_PATH];
        ssize_t na = readlink(x, ta, sizeof ta);
        ssize_t nb = readlink(y, tb, sizeof tb);

        if (S_ISLNK(a.st_mode) != S_ISLNK(b.st_mode)) return 0;
        if (na < 0 || nb < 0 || na == sizeof ta || nb == sizeof tb) return -1;
        return na == nb && !memcmp(ta, tb, (size_t)na);
    }
    if (!S_ISREG(a.st_mode) || !S_ISREG(b.st_mode)) return -1;
    if (a.st_size != b.st_size) return 0;
    if (!(buf = malloc(2 * DV_CHUNK)) || !(fa = fopen(x, "rb")) ||
        !(fb = fopen(y, "rb"))) goto done;
    for (same = 1; same == 1;) {
        size_t n = fread(buf, 1, DV_CHUNK, fa);

        if (fread(buf + DV_CHUNK, 1, DV_CHUNK, fb) != n ||
            memcmp(buf, buf + DV_CHUNK, n)) same = 0;
        if (ferror(fa) || ferror(fb)) { same = -1; break; }
        read_bytes += (int64_t)n;
        if (n < DV_CHUNK) break;
    }
    if (same == 1 && read_bytes != (int64_t)a.st_size) same = -1;
done:
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    free(buf);
    return same;
}

/** @brief How many of @p f's lines on @p side end CR LF. */
static long dv_crlf(const dv_files_t *f, int side)
{
    long k = 0;

    for (long i = 0; i < f->nline[side]; i++) {
        const tiku_diff_line_t *l = &f->line[side][i];
        if (l->len >= 2 && l->text[l->len - 2] == '\r') k++;
    }
    return k;
}

/** @brief One row onto @p f. */
static void dv_row(dv_files_t *f, long a, long b, int kind)
{
    f->row[f->nrow].a = a;
    f->row[f->nrow].b = b;
    f->row[f->nrow++].kind = kind;
}

/** @brief @p f's rows from its runs: kept lines face each other, a
 *         removal and the addition after it pair up line by line, and
 *         what is left of either faces an empty band.  @return 0, -1 out
 *         of memory. */
static int dv_rows(dv_files_t *f)
{
    const tiku_diff_t *d = &f->d;

    f->row = malloc((size_t)(f->nline[0] + f->nline[1] + 1) * sizeof *f->row);
    f->change = malloc((size_t)(d->nrun + 1) * sizeof *f->change);
    f->end = malloc((size_t)(d->nrun + 1) * sizeof *f->end);
    if (!f->row || !f->change || !f->end) return -1;
    for (long k = 0; k < d->nrun; k++) {
        const tiku_diff_run_t *r = &d->run[k];
        long na = 0, nb = 0, a0 = r->a, b0 = r->b;

        if (r->kind == TIKU_DIFF_SAME) {
            for (long i = 0; i < r->n; i++) dv_row(f, r->a + i, r->b + i, 0);
            continue;
        }
        if (r->kind == TIKU_DIFF_DEL) {
            na = r->n;
            if (k + 1 < d->nrun && d->run[k + 1].kind == TIKU_DIFF_ADD) {
                nb = d->run[++k].n;
                b0 = d->run[k].b;
            }
        } else {
            nb = r->n;
        }
        f->change[f->nchange] = f->nrow;
        for (long i = 0; i < na || i < nb; i++)
            dv_row(f, i < na ? a0 + i : -1, i < nb ? b0 + i : -1,
                   i < na && i < nb ? DV_ROW_CHANGE
                   : i < na ? DV_ROW_DEL : DV_ROW_ADD);
        f->end[f->nchange++] = f->nrow;
    }
    return 0;
}

/**
 * @brief Files @p pa and @p pb compared, either NULL for a file that is
 *        not there: as lines when both are text, else as bytes.
 * @return the comparison, or NULL with why in @p said.
 */
static dv_files_t *dv_files(const char *pa, const char *pb, int space,
                            char *said, size_t max)
{
    dv_files_t *f = calloc(1, sizeof *f);
    int r[2];

    if (!f) {
        snprintf(said, max, "Not enough memory to compare files.");
        return NULL;
    }
    r[0] = dv_read(pa, &f->text[0], &f->len[0], &f->size[0]);
    r[1] = dv_read(pb, &f->text[1], &f->len[1], &f->size[1]);
    if (r[0] < 0 || r[1] < 0) {
        snprintf(said, max, "Could not read %s.",
                 dv_leaf(r[0] < 0 ? pa : pb));
        dv_files_free(f);
        return NULL;
    }
    f->binary = r[0] > 0 || r[1] > 0 ||
                memchr(f->text[0], 0, f->len[0]) ||
                memchr(f->text[1], 0, f->len[1]);
    if (f->binary) {
        int64_t sa, sb;
        f->same = r[0] || r[1] ? (pa && pb ? dv_same_file(pa, pb, &sa, &sb) : 0)
                               : f->len[0] == f->len[1] &&
                                 !memcmp(f->text[0], f->text[1], f->len[0]);
        if (f->same < 0) {
            snprintf(said, max, "Could not read both files completely.");
            dv_files_free(f);
            return NULL;
        }
        return f;
    }
    f->nline[0] = tiku_diff_split(f->text[0], f->len[0], &f->line[0]);
    f->nline[1] = tiku_diff_split(f->text[1], f->len[1], &f->line[1]);
    if (f->nline[0] < 0 || f->nline[1] < 0 ||
        tiku_diff_lines(f->line[0], f->nline[0], f->line[1], f->nline[1],
                        TIKU_DIFF_CR | (space ? TIKU_DIFF_SPACE : 0),
                        &f->d) != 0 || dv_rows(f) != 0) {
        snprintf(said, max, "Not enough memory to compare them.");
        dv_files_free(f);
        return NULL;
    }
    f->same = f->d.removed + f->d.added == 0;
    f->endings = (dv_crlf(f, 0) > 0) != (dv_crlf(f, 1) > 0);
    return f;
}

/* ----------------------------------------------------------- folders  */

/** @brief A growing list of paths. */
typedef struct {
    char **p;
    long n, cap;
} dv_list_t;

/** @brief @p s added to @p l.  @return 0, -1 out of memory. */
static int dv_list_add(dv_list_t *l, const char *s)
{
    if (l->n == l->cap) {
        long cap = l->cap ? l->cap * 2 : 64;
        char **p = realloc(l->p, (size_t)cap * sizeof *p);

        if (!p) return -1;
        l->p = p;
        l->cap = cap;
    }
    if (!(l->p[l->n] = strdup(s))) return -1;
    l->n++;
    return 0;
}

/** @brief Free @p l's paths. */
static void dv_list_free(dv_list_t *l)
{
    for (long i = 0; i < l->n; i++) free(l->p[i]);
    free(l->p);
    memset(l, 0, sizeof *l);
}

/** @brief Two paths in order, for qsort. */
static int dv_cmp(const void *x, const void *y)
{
    return strcmp(*(char *const *)x, *(char *const *)y);
}

/** @brief How deep @p rel lies, in folders. */
static int dv_depth(const char *rel)
{
    int n = 0;

    for (; *rel; rel++) n += *rel == '/';
    return n;
}

/**
 * @brief Every file and link under @p root, as paths from it, sorted;
 *        links are not followed.  Folders wait in a heap list, so the
 *        walk's stack use does not grow with the tree's depth.
 * @return 0, or -1 when stopped, out of memory, deeper than DV_DEPTH,
 *         or when a folder or a path could not be read.
 */
static int dv_walk(const char *root, dv_list_t *out, const dv_progress_t *p)
{
    dv_list_t todo = { NULL, 0, 0 };
    char *path = malloc(2 * DV_PATH), *sub;
    int bad = !path || dv_list_add(&todo, "") != 0;

    while (!bad && todo.n > 0 && !dv_stopped(p)) {
        DIR *d;
        struct dirent *e;

        sub = todo.p[--todo.n];
        if (snprintf(path, DV_PATH, "%s%s%s", root, *sub ? "/" : "", sub)
            >= DV_PATH) {
            free(sub); bad = 1; break;
        }
        d = opendir(path);
        if (!d) bad = 1;
        while (d && !bad) {
            struct stat st;
            char *rel = path + DV_PATH;

            errno = 0;
            e = readdir(d);
            if (!e) { if (errno) bad = 1; break; }
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if (snprintf(rel, DV_PATH, "%s%s%s", sub, *sub ? "/" : "",
                         e->d_name) >= DV_PATH ||
                snprintf(path, DV_PATH, "%s/%s", root, rel) >= DV_PATH ||
                lstat(path, &st) != 0) { bad = 1; break; }
            if (S_ISDIR(st.st_mode))
                bad = dv_depth(rel) >= DV_DEPTH || dv_list_add(&todo, rel) != 0;
            else if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))
                bad = dv_list_add(out, rel) != 0;
        }
        if (d) closedir(d);
        free(sub);
    }
    free(path);
    bad = bad || dv_stopped(p);
    dv_list_free(&todo);
    if (!bad && out->n > 1)
        qsort(out->p, (size_t)out->n, sizeof *out->p, dv_cmp);
    return bad ? -1 : 0;
}

/** @brief One entry onto @p d: @p rel, taken over, and how it stands. */
static void dv_entry(dv_folders_t *d, char *rel, int status, int64_t sa,
                     int64_t sb)
{
    dv_entry_t *e = &d->e[d->n++];

    e->rel = rel;
    e->status = status;
    e->size[0] = sa;
    e->size[1] = sb;
    d->count[status]++;
}

/** @brief The size of @p root/@p rel, -1 when it cannot be seen. */
static int64_t dv_size(const char *root, const char *rel)
{
    char path[DV_PATH];
    struct stat st;

    snprintf(path, sizeof path, "%s/%s", root, rel);
    return lstat(path, &st) == 0 ? (int64_t)st.st_size : -1;
}

/**
 * @brief The paths of @p la and @p lb, both sorted, joined in order:
 *        under one side only, or under both and then compared byte for
 *        byte.  The lists' strings pass to the entries.
 * @return 0, or -1 when stopped, out of memory, or a file could not be
 *         read.
 */
static int dv_join(dv_folders_t *d, const char *ra, const char *rb,
                   dv_list_t *la, dv_list_t *lb, dv_progress_t *p)
{
    long i = 0, j = 0;
    char *pa = malloc(2 * DV_PATH);

    if (!pa) return -1;
    while ((i < la->n || j < lb->n) && !dv_stopped(p)) {
        int cmp = i >= la->n ? 1 : j >= lb->n ? -1 : strcmp(la->p[i], lb->p[j]);

        if (cmp < 0) {
            if (dv_size(ra, la->p[i]) < 0) { free(pa); return -1; }
            dv_entry(d, la->p[i], DV_ENT_LEFT, dv_size(ra, la->p[i]), -1);
            la->p[i++] = NULL;
        } else if (cmp > 0) {
            if (dv_size(rb, lb->p[j]) < 0) { free(pa); return -1; }
            dv_entry(d, lb->p[j], DV_ENT_RIGHT, -1, dv_size(rb, lb->p[j]));
            lb->p[j++] = NULL;
        } else {
            int64_t sa = -1, sb = -1;
            int same;

            snprintf(pa, DV_PATH, "%s/%s", ra, la->p[i]);
            snprintf(pa + DV_PATH, DV_PATH, "%s/%s", rb, lb->p[j]);
            same = dv_same_file(pa, pa + DV_PATH, &sa, &sb);
            if (same < 0) { free(pa); return -1; }
            dv_entry(d, la->p[i], same == 1 ? DV_ENT_SAME : DV_ENT_CHANGED,
                     sa, sb);
            la->p[i++] = NULL;
            j++;
            atomic_fetch_add(&p->done, 1);
        }
        atomic_fetch_add(&p->done, 1);
    }
    free(pa);
    return dv_stopped(p) ? -1 : 0;
}

/** @brief Folders @p ra and @p rb compared, every file under either.
 *  @return the comparison, or NULL with why in @p said. */
static dv_folders_t *dv_folders(const char *ra, const char *rb,
                                dv_progress_t *p, char *said, size_t max)
{
    dv_list_t la = { NULL, 0, 0 }, lb = { NULL, 0, 0 };
    dv_folders_t *d = calloc(1, sizeof *d);
    int ok = d && dv_walk(ra, &la, p) == 0 && dv_walk(rb, &lb, p) == 0 &&
             (d->e = malloc((size_t)(la.n + lb.n + 1) * sizeof *d->e));

    if (ok) {
        atomic_store(&p->total, la.n + lb.n);
        ok = dv_join(d, ra, rb, &la, &lb, p) == 0;
    }
    dv_list_free(&la);
    dv_list_free(&lb);
    if (!ok) {
        snprintf(said, max, dv_stopped(p) ? "Stopped." :
                 "Comparison incomplete: a folder or file could not be read, "
                 "or a path, depth, or memory limit was reached.");
        dv_folders_free(d);
        return NULL;
    }
    return d;
}

/* ------------------------------------------------------------- sides  */

/** @brief The one file @p dir holds when it holds nothing else, into
 *         @p out.  @return 1 when it does. */
static int dv_only_file(const char *dir, char *out, size_t max)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int n = 0;
    struct stat st;

    while (d && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(out, max, "%s/%s", dir, e->d_name);
        n++;
    }
    if (d) closedir(d);
    return n == 1 && lstat(out, &st) == 0 && S_ISREG(st.st_mode);
}

/** @brief An extraction's bytes, as the job's progress. */
static int dv_extracting(void *ctx, int64_t done, int64_t total)
{
    dv_progress_t *p = ctx;

    atomic_store(&p->done, done);
    atomic_store(&p->total, total);
    return dv_stopped(p);
}

/**
 * @brief Side @p s for @p path: a folder, a file, or an archive
 *        extracted under @p under and taken as the folder it made, or as
 *        its one file when it holds only that.
 * @return 0, or -1 with why in @p said.
 */
static int dv_side(dv_side_t *s, const char *path, const char *under,
                   dv_progress_t *p, char *said, size_t max)
{
    struct stat st;
    tiku_archive_t *a = NULL;
    tiku_archive_extract_t opt;
    char name[DV_PATH];
    tiku_archive_err_t err;

    snprintf(s->path, sizeof s->path, "%s", path);
    snprintf(s->root, sizeof s->root, "%s", path);
    snprintf(s->name, sizeof s->name, "%s", dv_leaf(path));
    if (stat(path, &st) != 0) {
        snprintf(said, max, "%s is not there.", dv_leaf(path));
        return -1;
    }
    s->dir = S_ISDIR(st.st_mode);
    if (s->dir) return 0;
    err = tiku_archive_open(path, dv_extracting, p, &a);
    if (err == TIKU_ARCHIVE_UNKNOWN) return 0;
    if (err != TIKU_ARCHIVE_OK || tiku_archive_damaged(a)) {
        snprintf(said, max, "Could not read archive %s: %s.", dv_leaf(path),
                 err == TIKU_ARCHIVE_OK ? "damaged listing"
                                        : tiku_archive_strerror(err));
        tiku_archive_close(a);
        return -1;
    }
    memset(&opt, 0, sizeof opt);
    opt.progress = dv_extracting;
    opt.ctx = p;
    tiku_archive_folder_name(path, name, sizeof name);
    err = mkdir(under, 0700) != 0 && errno != EEXIST ? TIKU_ARCHIVE_WRITE
        : tiku_archive_new_folder(under, name, s->root, sizeof s->root) != 0
        ? TIKU_ARCHIVE_WRITE : tiku_archive_extract(a, s->root, &opt);
    tiku_archive_close(a);
    if (err != TIKU_ARCHIVE_OK) {
        snprintf(said, max, "Could not extract %s: %s.", dv_leaf(path),
                 tiku_archive_strerror(err));
        return -1;
    }
    s->archive = 1;
    s->dir = 1;
    if (dv_only_file(s->root, name, sizeof name)) {  /* as a gzip holds */
        snprintf(s->root, sizeof s->root, "%s", name);
        s->dir = 0;
    }
    return 0;
}

/* ------------------------------------------------------------ patch  */

/** @brief @p n bytes of @p s onto the patch being written.
 *  @return 0, -1 out of memory. */
static int dv_patch_put(dv_job_t *j, const char *s, size_t n)
{
    if (j->patch_len + n + 1 > j->patch_cap) {
        size_t cap = j->patch_cap ? j->patch_cap : 65536;
        char *p;

        while (j->patch_len + n + 1 > cap) cap *= 2;
        if (!(p = realloc(j->patch_text, cap))) return -1;
        j->patch_text = p;
        j->patch_cap = cap;
    }
    memcpy(j->patch_text + j->patch_len, s, n);
    j->patch_len += n;
    j->patch_text[j->patch_len] = '\0';
    return 0;
}

/** @brief One entry of the folder list as a patch's section: its unified
 *         text, a/ and b/ before its path, /dev/null for a side it is not
 *         on; a binary file said to differ. */
static int dv_patch_one(dv_job_t *j, const dv_rel_t *e, char *pa)
{
    char *pb = pa + DV_PATH, *na = pa + 2 * DV_PATH;
    char *nb = pa + 3 * DV_PATH, said[160];
    dv_files_t *f;
    char *u;
    size_t n = 0;
    int bad = 0;

    snprintf(pa, DV_PATH, "%s/%s", j->side[0].root, e->rel);
    snprintf(pb, DV_PATH, "%s/%s", j->side[1].root, e->rel);
    snprintf(na, DV_PATH, e->status == DV_ENT_RIGHT ? "/dev/null"
                                                         : "a/%s", e->rel);
    snprintf(nb, DV_PATH, e->status == DV_ENT_LEFT ? "/dev/null"
                                                        : "b/%s", e->rel);
    f = dv_files(e->status == DV_ENT_RIGHT ? NULL : pa,
                 e->status == DV_ENT_LEFT ? NULL : pb, j->space, said,
                 sizeof said);
    if (!f) {
        snprintf(j->said, sizeof j->said, "Patch not saved: %s", said);
        return -1;
    }
    if (f->same) {
        dv_files_free(f);
        return 0;
    }
    if (f->binary) {
        int k = snprintf(said, sizeof said, "Binary files %.60s and %.60s "
                         "differ\n", na, nb);
        bad = dv_patch_put(j, said, (size_t)k);
    } else if ((u = tiku_diff_unified(&f->d, f->line[0], f->line[1], na, nb,
                                      3, &n)) != NULL) {
        bad = dv_patch_put(j, u, n);
        free(u);
    } else bad = 1;
    dv_files_free(f);
    return bad ? -1 : 0;
}

/** @brief Every difference of the folder list as one patch, in the list's
 *         order, as diff -ruN writes it and patch -p1 takes it. */
static void dv_patch_run(dv_job_t *j)
{
    char *paths = malloc(4 * DV_PATH);
    int bad = !paths;

    atomic_store(&j->progress->total, j->nrel);
    for (long i = 0; paths && i < j->nrel && !dv_stopped(j->progress); i++) {
        if (dv_patch_one(j, &j->rels[i], paths) != 0) { bad = 1; break; }
        atomic_fetch_add(&j->progress->done, 1);
    }
    free(paths);
    if (dv_stopped(j->progress))
        snprintf(j->said, sizeof j->said,
                 "Stopped. No patch was saved or copied.");
    else if (bad && !j->said[0])
        snprintf(j->said, sizeof j->said,
                 "Not enough memory. No patch was saved or copied.");
}

/** @brief Write the patch text to @p out for tiku_atomic_file(), a chunk
 *         at a time.  @return 0, or -1 on a short write or when stopped. */
static int dv_write_patch(FILE *out, void *ctx)
{
    dv_job_t *j = ctx;
    size_t at = 0;
    while (at < j->patch_len) {
        size_t n = j->patch_len - at;
        if (n > DV_CHUNK) n = DV_CHUNK;
        if (dv_stopped(j->progress) ||
            fwrite(j->patch_text + at, 1, n, out) != n)
            return -1;
        at += n;
    }
    return dv_stopped(j->progress) ? -1 : 0;
}

/* -------------------------------------------------------------- jobs  */

/** @brief A new progress, held by the viewer and the job about to run;
 *         @p doing names the job. */
static dv_progress_t *dv_progress_new(tiku_diffview_t *c, const char *doing)
{
    dv_progress_t *p = calloc(1, sizeof *p);

    if (!p) return NULL;
    p->refs = 2;
    c->progress = p;
    snprintf(c->doing, sizeof c->doing, "%s", doing);
    return p;
}

/** @brief One of a progress's holders done with it. */
static void dv_progress_unref(dv_progress_t *p)
{
    if (p && --p->refs == 0) free(p);
}

/** @brief One of the viewer's holders gone; the last frees it. */
static void dv_unref(tiku_diffview_t *c)
{
    if (--c->refs > 0) return;
    /* An extraction worker may still be writing after the window closes. */
    if (c->temp[0]) dv_rm(AT_FDCWD, c->temp, DV_DEPTH + 8);
    dv_files_free(c->files);
    dv_folders_free(c->folders);
    free(c->shown);
    free(c);
}

/** @brief The job: compare two paths, open one path of the folder list,
 *         or write every difference as one patch. */
static void dv_run(void *ctx)
{
    dv_job_t *j = ctx;

    if (j->patch) {
        if (j->patch == 1) dv_patch_run(j);
        if (!j->said[0] && dv_stopped(j->progress))
            snprintf(j->said, sizeof j->said,
                     "Stopped. No patch was saved or copied.");
        if (!j->said[0] && j->save[0] && j->patch_len &&
            tiku_atomic_file(j->save, dv_write_patch, j) != 0)
            snprintf(j->said, sizeof j->said,
                     "Could not save %s. The destination is unchanged.",
                     dv_leaf(j->save));
        return;
    }
    if (j->rel[0]) {                    /* an entry of the folder list */
        char *pa = malloc(2 * DV_PATH);

        if (!pa) return;
        snprintf(pa, DV_PATH, "%s/%s", j->side[0].root, j->rel);
        snprintf(pa + DV_PATH, DV_PATH, "%s/%s", j->side[1].root,
                 j->rel);
        j->files = dv_files(j->status == DV_ENT_RIGHT ? NULL : pa,
                            j->status == DV_ENT_LEFT ? NULL : pa + DV_PATH,
                            j->space, j->said, sizeof j->said);
        free(pa);
        return;
    }
    for (int s = 0; s < 2; s++)
        if (dv_side(&j->side[s], j->path[s], j->under, j->progress, j->said,
                    sizeof j->said) != 0) return;
    if (j->side[0].dir != j->side[1].dir)
        snprintf(j->said, sizeof j->said, "A folder can only be compared "
                 "with a folder, and a file with a file.");
    else if (j->side[0].dir)
        j->folders = dv_folders(j->side[0].root, j->side[1].root,
                                j->progress, j->said, sizeof j->said);
    else
        j->files = dv_files(j->side[0].root, j->side[1].root, j->space,
                            j->said, sizeof j->said);
}

/** @brief After done, or instead of it: the job's holds let go. */
static void dv_free(void *ctx)
{
    dv_job_t *j = ctx;
    tiku_diffview_t *c = j->owner;

    if (j->generation == c->generation) {
        c->busy = 0;
        if (c->progress == j->progress) {
            dv_progress_unref(c->progress);
            c->progress = NULL;
        }
        if (!j->landed && c->workspace) dv_say(c, "Stopped.");
        dv_menus(c);
        dv_changed(c);
    }
    dv_progress_unref(j->progress);
    dv_files_free(j->files);
    dv_folders_free(j->folders);
    for (long i = 0; i < j->nrel; i++) free(j->rels[i].rel);
    free(j->rels);
    free(j->patch_text);
    free(j);
    dv_unref(c);
}

/** @brief Hand @p j to a worker, keyed to this window so its jobs run one
 *         at a time.  @return 1 when it was taken. */
static int dv_post(tiku_diffview_t *c, dv_job_t *j, const char *doing)
{
    tiku_work_job_t work;

    j->owner = c;
    j->generation = c->generation;
    j->space = c->space;
    if (!(j->progress = dv_progress_new(c, doing))) {
        for (long i = 0; i < j->nrel; i++) free(j->rels[i].rel);
        free(j->rels);
        free(j->patch_text);
        free(j);
        dv_say(c, "Not enough memory to start the operation.");
        return 0;
    }
    c->refs++;
    memset(&work, 0, sizeof work);
    work.run = dv_run;
    work.done = dv_done;
    work.free = dv_free;
    work.ctx = j;
    snprintf(work.key, sizeof work.key, "diff:%p", (void *)c);
    c->busy = 1;
    if (tiku_work_post(&work) != 0) {
        dv_free(j);
        dv_say(c, "The workers are busy. Try again in a moment.");
        return 0;
    }
    dv_say(c, "");
    dv_menus(c);
    dv_changed(c);
    return 1;
}
