/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_view_jobs.inl - the PDF view's jobs: opening a document,
 * drawing a page and reading its links and text, and searching a stretch
 * of pages.  The jobs of one document share its key, so they run one at a
 * time; only their done callbacks touch the view, and only while the
 * program still holds it.  Included by tiku_pdf_view.c alone.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @brief Page @p p's size as drawn at scale 1: its rotation applied. */
static void vw_page_size(tiku_pdf_t *pdf, int p, float *w, float *h)
{
    double pw = 612, ph = 792;
    int rot = 0;

    (void)tiku_pdf_page_size(pdf, p, &pw, &ph, &rot);
    if (rot == 90 || rot == 270) {
        double t = pw;
        pw = ph;
        ph = t;
    }
    *w = (float)pw;
    *h = (float)ph;
}

/* ------------------------------------------------------------ opening  */

/** @brief Opening a file: the document and what a view needs up front. */
typedef struct {
    tiku_pdf_view_t *owner;
    int generation, npage, noutline, may_copy, tried;
    tiku_pdf_err_t err;
    char path[VW_PATH], keyfile[VW_PATH], password[256], *title;
    tiku_pdf_view_translate_fn translate;   /* NULL: the file is a PDF */
    char why[256];                          /* the translator's refusal */
    tiku_pdf_t *pdf;
    float *pw, *ph;
    tiku_pdf_outline_t *outline;
} vw_load_t;

/** @brief On a worker: the file made a PDF by the translator, or opened
 *         as one (with the key file, for a sealed document). */
static void vw_load_open(vw_load_t *j)
{
    unsigned char *b;
    size_t len = 0;

    if (!j->translate) {
        j->err = j->keyfile[0]
                 ? tiku_pdf_open_key(j->path, j->keyfile, j->password,
                                     &j->pdf)
                 : tiku_pdf_open_password(j->path, j->password, &j->pdf);
        return;
    }
    b = j->translate(j->path, &len, j->why, sizeof j->why);
    j->err = b ? tiku_pdf_decode(b, len, &j->pdf) : TIKU_PDF_BAD;
    free(b);
}

/** @brief On a worker: open, measure every page, read the outline and
 *         the title. */
static void vw_load_run(void *ctx)
{
    vw_load_t *j = ctx;

    vw_load_open(j);
    memset(j->password, 0, sizeof j->password);   /* used; not kept */
    if (j->err != TIKU_PDF_OK) return;
    j->may_copy = tiku_pdf_may_copy(j->pdf);
    j->npage = tiku_pdf_pages(j->pdf);
    if (j->npage <= 0) {
        j->err = TIKU_PDF_BAD;
        return;
    }
    j->pw = malloc((size_t)j->npage * sizeof *j->pw);
    j->ph = malloc((size_t)j->npage * sizeof *j->ph);
    if (!j->pw || !j->ph) {
        j->err = TIKU_PDF_BAD;
        return;
    }
    for (int p = 0; p < j->npage; p++)
        vw_page_size(j->pdf, p, &j->pw[p], &j->ph[p]);
    j->noutline = tiku_pdf_outline(j->pdf, &j->outline);
    if (j->noutline < 0) j->noutline = 0;
    j->title = tiku_pdf_title(j->pdf);
}

/** @brief Why a file did not open, in a person's words. */
static const char *vw_load_error(const vw_load_t *j)
{
    if (j->why[0]) return j->why;
    switch (j->err) {
    case TIKU_PDF_NOT_PDF: return "That file is not a PDF.";
    case TIKU_PDF_UNSUPPORTED:
        return "That PDF is encrypted in a way that is not read here.";
    case TIKU_PDF_PASSWORD:
        return j->keyfile[0] ? "That password does not open the key file." :
               j->tried ? "That password does not open it." :
                          "This PDF is locked: it needs its password.";
    case TIKU_PDF_CERTIFICATE:
        return j->keyfile[0] ? "That key is not one this PDF was sealed to." :
               "This PDF is sealed to a certificate: it needs a key file.";
    case TIKU_PDF_KEY_FILE:
        return "That is not a key file read here: an RSA key in a .p12, "
               ".pfx or .pem file.";
    default: break;
    }
    return "That PDF is damaged beyond what can be repaired.";
}

/** @brief The new document takes the old one's place: shown from its
 *         first page, or from where the program asked it to start.
 *  @return 0 when there was no memory for it. */
static int vw_load_take(tiku_pdf_view_t *v, vw_load_t *j)
{
    vw_doc_t *d = calloc(1, sizeof *d);
    const char *name = strrchr(j->path, '/');

    if (!d) return 0;
    vw_forget(v);
    d->pdf = j->pdf;
    j->pdf = NULL;
    d->refs = 1;
    snprintf(d->key, sizeof d->key, "pdfview:%p", (void *)d);
    atomic_init(&d->search, v->search_gen);
    v->doc = d;
    v->npage = j->npage;
    v->pw = j->pw;
    v->ph = j->ph;
    v->outline = j->outline;
    v->noutline = j->noutline;
    v->may_copy = j->may_copy;
    j->pw = j->ph = NULL;
    j->outline = NULL;
    j->noutline = 0;
    v->pages = calloc((size_t)v->npage, sizeof *v->pages);
    v->top = calloc((size_t)v->npage, sizeof *v->top);
    if (!v->pages || !v->top) {
        vw_forget(v);
        return 0;
    }
    vw_find_widest(v);
    snprintf(v->path, sizeof v->path, "%s", j->path);
    snprintf(v->title, sizeof v->title, "%.255s", j->title && j->title[0]
             ? j->title : name ? name + 1 : j->path);
    return 1;
}

/** @brief On the loop: the program is told how the opening went, and a
 *         document that opened is shown. */
static void vw_load_done(void *ctx)
{
    vw_load_t *j = ctx;
    tiku_pdf_view_t *v = j->owner;
    tiku_pdf_err_t err = j->err;
    char said[96];

    if (j->generation != v->generation || v->gone) return;
    if (err == TIKU_PDF_OK && !vw_load_take(v, j)) err = TIKU_PDF_BAD;
    if (err != TIKU_PDF_OK) {
        vw_say(v, err == j->err ? vw_load_error(j)
                                : "Not enough memory to show that PDF.");
    } else {
        snprintf(said, sizeof said, "%d page%s%s.", v->npage,
                 v->npage == 1 ? "" : "s",
                 v->noutline ? ", with an outline" : "");
        vw_say(v, said);
    }
    if (v->host.opened) v->host.opened(v->host.ctx, err);
}

/** @brief After done, or alone when the job is cancelled: what was not
 *         taken given back. */
static void vw_load_free(void *ctx)
{
    vw_load_t *j = ctx;
    tiku_pdf_view_t *v = j->owner;

    tiku_pdf_close(j->pdf);
    free(j->pw);
    free(j->ph);
    tiku_pdf_outline_free(j->outline, j->noutline);
    free(j->title);
    v->loading = 0;
    vw_changed(v);
    vw_unref(v);
    free(j);
}

/** @brief Ask a worker to open @p path with @p password ("" for none):
 *         the password of @p keyfile when that is given; made a PDF by
 *         @p translate when that is given.  One at a time. */
static int vw_load_as(tiku_pdf_view_t *v, const char *path,
                      const char *keyfile, const char *password,
                      tiku_pdf_view_translate_fn translate)
{
    vw_load_t *j;
    tiku_work_job_t work;

    if (!path || !*path || strlen(path) >= VW_PATH ||
        strlen(keyfile) >= VW_PATH || strlen(password) >= 256) return 0;
    if (v->loading) {
        vw_say(v, "A PDF is still opening. Try again in a moment.");
        return 0;
    }
    if (!(j = calloc(1, sizeof *j))) return 0;
    j->owner = v;
    j->generation = v->generation;
    snprintf(j->path, sizeof j->path, "%s", path);
    snprintf(j->keyfile, sizeof j->keyfile, "%s", keyfile);
    snprintf(j->password, sizeof j->password, "%s", password);
    j->tried = password[0] != '\0';
    j->translate = translate;
    memset(&work, 0, sizeof work);
    work.run = vw_load_run;
    work.done = vw_load_done;
    work.free = vw_load_free;
    work.ctx = j;
    v->refs++;
    v->loading = 1;
    if (tiku_work_post(&work) != 0) {
        vw_load_free(j);
        vw_say(v, "The workers are busy. Try again in a moment.");
        return 0;
    }
    vw_say(v, translate ? "Laying out its pages..." : "Opening...");
    return 1;
}

/* ------------------------------------------------------------ drawing  */

/** @brief Drawing one page, and reading its links and text the first
 *         time it is drawn. */
typedef struct {
    tiku_pdf_view_t *owner;
    vw_doc_t *doc;
    int slot, page, text, nlink, failed;
    int turn;                       /* the view's turn it is drawn at   */
    double scale;                   /* what the page is drawn at        */
    long nch;
    tiku_pdf_pixmap_t pm;
    tiku_pdf_link_t *link;
    tiku_pdf_char_t *ch;
} vw_render_t;

/** @brief On a worker: the page's pixels, then what it says. */
static void vw_render_run(void *ctx)
{
    vw_render_t *j = ctx;

    if (tiku_pdf_render(j->doc->pdf, j->page, j->scale, j->turn,
                        &j->pm) != 0)
        j->failed = 1;
    if (!j->text) return;
    j->nlink = tiku_pdf_links(j->doc->pdf, j->page, &j->link);
    if (j->nlink < 0) j->nlink = 0;
    j->nch = tiku_pdf_page_chars(j->doc->pdf, j->page, &j->ch);
    if (j->nch < 0) j->nch = 0;
}

/** @brief Keep the text of the pages used last, @c VW_TEXT_KEEP of them,
 *         never a page the selection or the view is on. */
static void vw_trim_text(tiku_pdf_view_t *v)
{
    while (v->ntext > VW_TEXT_KEEP) {
        int old = -1;

        for (int p = 0; p < v->npage; p++) {
            vw_page_t *pg = &v->pages[p];
            if (!pg->text || pg->used == v->frame ||
                (v->selected && p >= v->anchor.page && p <= v->focus.page) ||
                (v->selected && p <= v->anchor.page && p >= v->focus.page))
                continue;
            if (old < 0 || pg->used < v->pages[old].used) old = p;
        }
        if (old < 0) return;
        tiku_pdf_links_free(v->pages[old].link, v->pages[old].nlink);
        free(v->pages[old].ch);
        v->pages[old].link = NULL;      /* its failure, if any, is kept */
        v->pages[old].ch = NULL;
        v->pages[old].nlink = 0;
        v->pages[old].nch = 0;
        v->pages[old].text = 0;
        v->ntext--;
    }
}

/** @brief Pixels @p *px, @p *w by @p *h, turned @p by degrees clockwise
 *         in their own place.  @return 0, or -1 with them let go when
 *         there is no memory for the turned copy. */
static int vw_turn_pixels(uint32_t **px, int *w, int *h, int by)
{
    int q = (by % 360 + 360) % 360 / 90, wide = *w, high = *h;
    uint32_t *out;

    if (!q || !*px) return 0;
    out = malloc((size_t)wide * high * sizeof *out);
    if (!out) {
        free(*px);
        *px = NULL;
        return -1;
    }
    for (int y = 0; y < high; y++) {
        const uint32_t *row = *px + (size_t)y * wide;
        for (int x = 0; x < wide; x++) {
            size_t at = q == 1 ? (size_t)x * high + (high - 1 - y) :
                        q == 2 ? (size_t)(high - 1 - y) * wide + (wide - 1 - x)
                               : (size_t)(wide - 1 - x) * high + y;
            out[at] = row[x];
        }
    }
    free(*px);
    *px = out;
    if (q % 2) {
        *w = high;
        *h = wide;
    }
    return 0;
}

/** @brief On the loop: the pixels go to their slot, turned to the view
 *         when it turned while they were drawn; the text to the page. */
static void vw_render_done(void *ctx)
{
    vw_render_t *j = ctx;
    tiku_pdf_view_t *v = j->owner;
    vw_tile_t *t = &v->tile[j->slot];

    if (v->gone || j->doc != v->doc) return;
    if (t->pending && t->page == j->page) {
        t->pending = 0;
        if (j->failed) {
            t->page = -1;
            v->pages[j->page].failed = 1;
        } else if (vw_turn_pixels(&j->pm.px, &j->pm.w, &j->pm.h,
                                  v->turn - j->turn) != 0) {
            t->page = -1;           /* no room to turn: asked for again */
        } else {
            t->px = j->pm.px;
            t->w = j->pm.w;
            t->h = j->pm.h;
            j->pm.px = NULL;
        }
    }
    if (j->text && !v->pages[j->page].text) {
        vw_page_t *pg = &v->pages[j->page];
        pg->link = j->link;
        pg->nlink = j->nlink;
        pg->ch = j->ch;
        pg->nch = j->nch;
        pg->text = 1;
        pg->used = v->frame;
        j->link = NULL;
        j->ch = NULL;
        v->ntext++;
        vw_trim_text(v);
    }
    vw_changed(v);
}

/** @brief After done or a cancel: the slot is let go if it still waits. */
static void vw_render_free(void *ctx)
{
    vw_render_t *j = ctx;
    tiku_pdf_view_t *v = j->owner;

    if (j->doc == v->doc) {
        vw_tile_t *t = &v->tile[j->slot];
        v->inflight--;
        if (t->pending && t->page == j->page) {
            t->pending = 0;
            t->page = -1;
        }
        vw_changed(v);
    }
    tiku_pdf_pixmap_free(&j->pm);
    tiku_pdf_links_free(j->link, j->nlink);
    free(j->ch);
    vw_doc_unref(j->doc);
    vw_unref(v);
    free(j);
}

/** @brief The scale page @p p is drawn at: the view's, unless that would
 *         take more than @c VW_PAGE_PIXELS, when it is shown enlarged. */
static double vw_draw_scale(const tiku_pdf_view_t *v, int p)
{
    double area = (double)v->pw[p] * v->ph[p] * v->scale * v->scale;

    if (area <= (double)VW_PAGE_PIXELS) return v->scale;
    return sqrt((double)VW_PAGE_PIXELS / ((double)v->pw[p] * v->ph[p]));
}

/** @brief A slot for @p pixels more, giving back the pages shown least
 *         recently, never one on screen now.  @return it, or -1. */
static int vw_slot(tiku_pdf_view_t *v, long pixels)
{
    for (;;) {
        long held = 0;
        int free_slot = -1, old = -1;

        for (int i = 0; i < VW_TILES; i++) {
            vw_tile_t *t = &v->tile[i];
            if (t->px) held += (long)t->w * t->h;
            if (t->pending) held += pixels;
            if (t->page < 0 && !t->pending) {
                if (free_slot < 0) free_slot = i;
            } else if (!t->pending && t->used != v->frame &&
                       (old < 0 || t->used < v->tile[old].used)) {
                old = i;
            }
        }
        if (free_slot >= 0 && held + pixels <= VW_PIXELS) return free_slot;
        if (old < 0) return -1;
        free(v->tile[old].px);
        memset(&v->tile[old], 0, sizeof v->tile[old]);
        v->tile[old].page = -1;
    }
}

/** @brief Ask a worker to draw page @p p at vw_draw_scale(), and to read
 *         its links and text the first time.  @return 1 when asked. */
static int vw_request(tiku_pdf_view_t *v, int p)
{
    vw_render_t *j;
    tiku_work_job_t work;
    double scale;
    int slot;

    if (!v->doc || v->inflight >= VW_INFLIGHT || v->pages[p].failed)
        return 0;
    scale = vw_draw_scale(v, p);
    slot = vw_slot(v, (long)(v->pw[p] * scale + 1) *
                      (long)(v->ph[p] * scale + 1));
    if (slot < 0 || !(j = calloc(1, sizeof *j))) return 0;
    j->owner = v;
    j->doc = v->doc;
    j->slot = slot;
    j->page = p;
    j->scale = scale;
    j->turn = v->turn;
    j->text = !v->pages[p].text;
    memset(&work, 0, sizeof work);
    work.run = vw_render_run;
    work.done = vw_render_done;
    work.free = vw_render_free;
    work.ctx = j;
    snprintf(work.key, sizeof work.key, "%s", v->doc->key);
    v->tile[slot] = (vw_tile_t){ p, 0, 0, 1, v->scale, NULL, v->frame };
    v->refs++;
    v->doc->refs++;
    v->inflight++;
    if (tiku_work_post(&work) != 0) {
        v->tile[slot].page = -1;
        v->tile[slot].pending = 0;
        v->refs--;
        v->doc->refs--;
        v->inflight--;
        free(j);
        return 0;
    }
    return 1;
}

/* ---------------------------------------------------------- searching  */

/** @brief One stretch of pages searched for the folded query. */
typedef struct {
    tiku_pdf_view_t *owner;
    vw_doc_t *doc;
    int gen, from, to, nq, nmatch, cap;
    int keep_case, whole;           /* TIKU_PDF_FIND_CASE, _WORDS       */
    uint32_t q[VW_QUERY];
    vw_match_t *match;
} vw_search_t;

/** @brief A character as a search compares it: the kinds of space,
 *         hyphen and quote a typeset page uses folded together, and case
 *         too unless @p keep_case. */
static uint32_t vw_fold(uint32_t cp, int keep_case)
{
    if (cp == '\n' || cp == '\t' || cp == '\r' || cp == 0xa0) return ' ';
    if (keep_case && cp < 0x80) return cp;
    if (cp < 0x80) return (uint32_t)tolower((int)cp);
    if (!keep_case && cp >= 0xc0 && cp <= 0xde && cp != 0xd7) return cp + 0x20;
    if (cp == 0x2010 || cp == 0x2011 || cp == 0x2013 || cp == 0x2212)
        return '-';
    if (cp == 0x2018 || cp == 0x2019) return '\'';
    if (cp == 0x201c || cp == 0x201d) return '"';
    return cp;
}

/** @brief The boxes of characters @p a to @p b (inclusive): one a line. */
static void vw_match_boxes(vw_match_t *m, const tiku_pdf_char_t *ch, long a,
                           long b)
{
    m->nr = 0;
    for (long i = a; i <= b; i++) {
        const tiku_pdf_char_t *k = &ch[i];
        float *r;

        if (k->x1 <= k->x0) continue;
        r = m->nr ? m->r[m->nr - 1] : NULL;
        if (r && k->y0 < r[3] - 1 && k->y1 > r[1] + 1) {
            if (k->x0 < r[0]) r[0] = k->x0;
            if (k->y0 < r[1]) r[1] = k->y0;
            if (k->x1 > r[2]) r[2] = k->x1;
            if (k->y1 > r[3]) r[3] = k->y1;
        } else if (m->nr < 4) {
            r = m->r[m->nr++];
            r[0] = k->x0; r[1] = k->y0; r[2] = k->x1; r[3] = k->y1;
        } else {
            r = m->r[3];
            if (k->x0 < r[0]) r[0] = k->x0;
            if (k->x1 > r[2]) r[2] = k->x1;
            if (k->y1 > r[3]) r[3] = k->y1;
        }
    }
}

/** @brief Whether @p cp belongs to a word: a letter or digit (or _). */
static int vw_wordy(uint32_t cp)
{
    return cp < 0x80 ? isalnum((int)cp) || cp == '_'
                     : cp >= 0xc0 && cp != 0xd7 && cp != 0xf7;
}

/** @brief Room for one match more.  @return 0 when there is none. */
static int vw_search_room(vw_search_t *j)
{
    int cap = j->cap ? j->cap * 2 : 32;
    vw_match_t *grown;

    if (j->nmatch < j->cap) return 1;
    if (!(grown = realloc(j->match, (size_t)cap * sizeof *grown))) return 0;
    j->match = grown;
    j->cap = cap;
    return 1;
}

/** @brief Every place on page @p p where the query stands, spaces folded
 *         to one, so a phrase broken over two lines is found; with
 *         TIKU_PDF_FIND_WORDS, only where no letter or digit touches
 *         either end. */
static void vw_search_page(vw_search_t *j, int p, const tiku_pdf_char_t *ch,
                           long n)
{
    uint32_t *f = malloc((size_t)n * sizeof *f);
    long *at = malloc((size_t)n * sizeof *at), m = 0;

    for (long i = 0; f && at && i < n; i++) {
        uint32_t cp = vw_fold(ch[i].cp, j->keep_case);
        if (cp == ' ' && (m == 0 || f[m - 1] == ' ')) continue;
        f[m] = cp;
        at[m++] = i;
    }
    for (long i = 0; f && at && i + j->nq <= m; i++) {
        int k = 0;
        while (k < j->nq && f[i + k] == j->q[k]) k++;
        if (k < j->nq) continue;
        if (j->whole && ((i > 0 && vw_wordy(f[i - 1])) ||
                         (i + j->nq < m && vw_wordy(f[i + j->nq]))))
            continue;
        if (!vw_search_room(j)) break;
        j->match[j->nmatch].page = p;
        vw_match_boxes(&j->match[j->nmatch], ch, at[i], at[i + j->nq - 1]);
        j->nmatch++;
        i += j->nq - 1;
        if (j->nmatch >= VW_MATCHES) break;
    }
    free(f);
    free(at);
}

/** @brief On a worker: read each page's text and look for the query;
 *         a newer search, or a closed document, stops it between pages. */
static void vw_search_run(void *ctx)
{
    vw_search_t *j = ctx;

    for (int p = j->from; p < j->to && j->nmatch < VW_MATCHES; p++) {
        tiku_pdf_char_t *ch = NULL;
        long n;

        if (atomic_load(&j->doc->search) != j->gen) return;
        n = tiku_pdf_page_chars(j->doc->pdf, p, &ch);
        if (n > 0) vw_search_page(j, p, ch, n);
        free(ch);
    }
}

static int vw_search_post(tiku_pdf_view_t *v, int from);
static void vw_find_step(tiku_pdf_view_t *v, int dir);

/** @brief The stretch's matches joined to the rest, as far as the
 *         ceiling allows. */
static void vw_search_keep(tiku_pdf_view_t *v, const vw_search_t *j)
{
    int room = VW_MATCHES - v->nmatch;
    int take = j->nmatch < room ? j->nmatch : room;
    vw_match_t *all;

    if (take <= 0) return;
    all = realloc(v->match, (size_t)(v->nmatch + take) * sizeof *all);
    if (!all) return;
    memcpy(all + v->nmatch, j->match, (size_t)take * sizeof *all);
    v->match = all;
    v->nmatch += take;
}

/** @brief On the loop: the stretch's matches join the rest, and the next
 *         stretch is asked for behind any pages waiting to be drawn. */
static void vw_search_done(void *ctx)
{
    vw_search_t *j = ctx;
    tiku_pdf_view_t *v = j->owner;
    char said[160];

    if (v->gone || j->doc != v->doc || j->gen != v->search_gen) return;
    vw_search_keep(v, j);
    v->searched = j->to;
    if (v->searched >= v->npage || v->nmatch >= VW_MATCHES ||
        !vw_search_post(v, v->searched))
        v->searching = 0;
    if (v->want_next) vw_find_step(v, v->want_next);
    else if (v->cur >= 0 && v->cur < v->nmatch) {
        snprintf(said, sizeof said, "Match %d of %d%s.", v->cur + 1,
                 v->nmatch, v->searching ? " so far" : "");
        vw_say(v, said);
    }
    if (v->searching && !v->nmatch) {
        snprintf(said, sizeof said, "Searching... %d of %d pages read.",
                 v->searched, v->npage);
        vw_say(v, said);
    } else if (!v->searching && !v->nmatch) {
        vw_say(v, "Not found in this document.");
    }
    vw_changed(v);
}

/** @brief After done or a cancel. */
static void vw_search_free(void *ctx)
{
    vw_search_t *j = ctx;
    tiku_pdf_view_t *v = j->owner;

    free(j->match);
    vw_doc_unref(j->doc);
    vw_unref(v);
    free(j);
}

static int vw_query(const char *text, uint32_t *q, int max, int keep_case);

/** @brief Ask a worker to search the next stretch from page @p from. */
static int vw_search_post(tiku_pdf_view_t *v, int from)
{
    vw_search_t *j = calloc(1, sizeof *j);
    tiku_work_job_t work;

    if (!j) return 0;
    j->owner = v;
    j->doc = v->doc;
    j->gen = v->search_gen;
    j->from = from;
    j->to = from + VW_SEARCH_STEP < v->npage ? from + VW_SEARCH_STEP
                                             : v->npage;
    j->keep_case = (v->find_flags & TIKU_PDF_FIND_CASE) != 0;
    j->whole = (v->find_flags & TIKU_PDF_FIND_WORDS) != 0;
    j->nq = vw_query(v->query, j->q, VW_QUERY, j->keep_case);
    memset(&work, 0, sizeof work);
    work.run = vw_search_run;
    work.done = vw_search_done;
    work.free = vw_search_free;
    work.ctx = j;
    snprintf(work.key, sizeof work.key, "%s", v->doc->key);
    v->refs++;
    v->doc->refs++;
    if (tiku_work_post(&work) != 0) {
        v->refs--;
        v->doc->refs--;
        free(j);
        return 0;
    }
    return 1;
}
