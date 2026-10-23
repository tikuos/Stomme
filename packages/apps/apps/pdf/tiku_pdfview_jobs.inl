/* tiku_pdfview_jobs.inl -- part of tiku_pdfview.c: the worker jobs that
 * open a document, draw its pages and thumbnails, and search it.  Jobs on
 * an open document share its key and run one at a time; only their done
 * and free callbacks, which run on the loop, touch the viewer.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */

/** @brief Page @p p's size as drawn at scale 1: rotation applied. */
static void pv_page_size(tiku_pdf_t *pdf, int p, float *w, float *h)
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

/** @brief Opening a file: the document and all a view needs up front. */
typedef struct {
    tiku_pdfview_t *owner;
    int generation, npage, noutline, may_copy, tried, nsig;
    tiku_pdf_err_t err;
    char path[TIKU_PATH_MAX], *title, password[TIKU_FIELD_MAX];
    char keyfile[TIKU_PATH_MAX];    /* a sealed document's key, or ""    */
    tiku_pdf_t *pdf;
    float *pw, *ph;
    tiku_pdf_outline_t *outline;
    tiku_pdf_signature_t *sig;      /* its signatures, checked here      */
} pv_load_t;

/** @brief On a worker: open (with the key file, for a sealed document),
 *         measure every page, read the outline, check the signatures. */
static void pv_load_run(void *ctx)
{
    pv_load_t *j = ctx;

    j->err = j->keyfile[0]
             ? tiku_pdf_open_key(j->path, j->keyfile, j->password, &j->pdf)
             : tiku_pdf_open_password(j->path, j->password, &j->pdf);
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
        pv_page_size(j->pdf, p, &j->pw[p], &j->ph[p]);
    j->noutline = tiku_pdf_outline(j->pdf, &j->outline);
    if (j->noutline < 0) j->noutline = 0;
    j->title = tiku_pdf_title(j->pdf);
    j->nsig = tiku_pdf_signatures(j->pdf, &j->sig);
}

/** @brief Why a file did not open, as a sentence for the status line. */
static const char *pv_load_error(tiku_pdf_err_t err)
{
    if (err == TIKU_PDF_NOT_PDF) return "That file is not a PDF.";
    if (err == TIKU_PDF_UNSUPPORTED)
        return TIKU_PRODUCT_PDF_ENCRYPTION_MESSAGE;
    return TIKU_PRODUCT_PDF_DAMAGE_MESSAGE;
}

/** @brief A document that would not open for want of a password or a
 *         key: the password panel asks for it, and a sealed one's key
 *         file is asked for first.  @return 0 when @p j is not one. */
static int pv_load_locked(tiku_pdfview_t *c, pv_load_t *j)
{
    int sealed = j->err == TIKU_PDF_CERTIFICATE ||
                 j->err == TIKU_PDF_KEY_FILE || j->keyfile[0];

    if (j->err != TIKU_PDF_PASSWORD && j->err != TIKU_PDF_CERTIFICATE &&
        j->err != TIKU_PDF_KEY_FILE) return 0;
    c->locked = 1;
    c->sealed = sealed;
    snprintf(c->locked_path, sizeof c->locked_path, "%s", j->path);
    pv_forget_password(c);
    c->password_focus = 1;
    if (!sealed) {
        pv_say(c, j->tried ? "That password does not open it. Try again, "
                             "or press Escape." :
                             "This PDF is locked. Enter its password in the "
                             "password panel.");
    } else if (j->err == TIKU_PDF_PASSWORD) {
        pv_say(c, "That password does not open the key file. Try again, "
                  "or press Escape.");
    } else {
        c->keyfile[0] = 0;
        pv_say(c, j->err == TIKU_PDF_KEY_FILE ?
               TIKU_PRODUCT_PDF_THAT_IS_NOT_A_KEY_TIKUPDF_READS_AN_RSA_KEY_IN_A_P12_PFX
               "or .pem file. Choose another with File > Open Key..." :
               j->keyfile[0] ? "That key is not one this PDF was sealed to. "
               "Choose another with File > Open Key..." :
               "This PDF is sealed to a certificate. Choose your key file "
               "(.p12, .pfx or .pem), then type its password.");
        if (!j->keyfile[0] && c->choose) c->choose(c->ctx, 1);
    }
    pv_menus(c);
    return 1;
}

/** @brief What the document's signatures say, in a sentence. */
static void pv_sig_said(const tiku_pdfview_t *c, char *out, size_t cap)
{
    int bad = 0, i;
    const tiku_pdf_signature_t *last = &c->sig[c->nsig - 1];

    for (i = 0; i < c->nsig; i++)
        bad += c->sig[i].status != TIKU_PDF_SIG_GOOD;
    if (bad && c->nsig == 1)
        snprintf(out, cap, " Its signature does not hold: see Signatures.");
    else if (bad)
        snprintf(out, cap, " %d of its %d signatures do%s not hold: see "
                 "Signatures.", bad, c->nsig, bad == 1 ? "es" : "");
    else if (c->nsig == 1)
        snprintf(out, cap, " Signed by %.60s%s.", last->signer ?
                 last->signer : "an unnamed party", last->whole ? "" :
                 ", and added to since");
    else
        snprintf(out, cap, " %d signatures, each holding%s.", c->nsig,
                 last->whole ? "" : "; added to since the last");
}

/** @brief On the loop: the old document's place is noted and it goes;
 *         the new one is shown where it was last left, or from its first
 *         page fitted to the width, with Signatures open when signed. */
static void pv_load_done(void *ctx)
{
    pv_load_t *j = ctx;
    tiku_pdfview_t *c = j->owner;
    pv_doc_t *d;
    const char *name = strrchr(j->path, '/');

    if (j->generation != c->generation || !c->workspace) return;
    if (pv_load_locked(c, j)) return;
    if (j->err != TIKU_PDF_OK) {
        pv_say(c, pv_load_error(j->err));
        return;
    }
    d = calloc(1, sizeof *d);
    if (!d) {
        pv_say(c, "Not enough memory to open that PDF.");
        return;
    }
    /* Every allocation that can fail is made before the current document
     * is released. */
    pv_page_t *pages = calloc((size_t)j->npage, sizeof *pages);
    long *top = calloc((size_t)j->npage, sizeof *top);
    unsigned char *folded = calloc((size_t)j->noutline + 1, 1);
    int *rows = calloc((size_t)j->noutline + 1, sizeof *rows);
    if (!pages || !top || !folded || !rows) {
        free(pages); free(top); free(folded); free(rows); free(d);
        pv_say(c, "Not enough memory to show that PDF; "
               "the current document is unchanged.");
        return;
    }
    pv_recent_note(c);              /* where the one shown was left */
    pv_forget(c);
    c->locked = c->sealed = 0;
    c->keyfile[0] = 0;
    d->pdf = j->pdf;
    j->pdf = NULL;
    d->refs = 1;
    snprintf(d->key, sizeof d->key, "pdf:%p", (void *)d);
    atomic_init(&d->search, c->search_gen);
    c->doc = d;
    c->npage = j->npage;
    c->pw = j->pw;
    c->ph = j->ph;
    c->outline = j->outline;
    c->noutline = j->noutline;
    j->pw = j->ph = NULL;
    j->outline = NULL;
    c->pages = pages;
    c->top = top;
    c->folded = folded;
    c->rows = rows;
    pv_find_widest(c);
    pv_outline_start(c);
    snprintf(c->path, sizeof c->path, "%s", j->path);
    snprintf(c->title, sizeof c->title, "%s",
             j->title && j->title[0] ? j->title : name ? name + 1 : j->path);
    if (c->window)
        snprintf(c->window->title, sizeof c->window->title, TIKU_PRODUCT_PDF_TITLE_FORMAT,
                 name ? name + 1 : j->path);
    c->fit = PV_BY_WIDTH;
    c->may_copy = j->may_copy;
    c->sig = j->sig;
    c->nsig = j->nsig;
    j->sig = NULL;
    j->nsig = 0;
    if (c->nsig) {                  /* who signed it, shown at once */
        c->side = c->sigs = 1;
        c->thumbs = 0;
    }
    pv_recent_restore(c);
    pv_recent_note(c);
    char said[320], sigs[160] = "";
    if (c->nsig) pv_sig_said(c, sigs, sizeof sigs);
    snprintf(said, sizeof said, "%d page%s%s.%s", c->npage,
             c->npage == 1 ? "" : "s", c->noutline ? ", with an outline" : "",
             c->nsig ? sigs : " Drag across text to select it; Ctrl+F finds.");
    pv_say(c, said);
    tiku_dialog_focus(&c->dialog, PV_CANVAS);
}

/** @brief After done, or instead of it: give back what was not taken. */
static void pv_load_free(void *ctx)
{
    pv_load_t *j = ctx;
    tiku_pdfview_t *c = j->owner;

    tiku_pdf_close(j->pdf);
    free(j->pw);
    free(j->ph);
    tiku_pdf_outline_free(j->outline, j->noutline);
    tiku_pdf_signatures_free(j->sig, j->nsig);
    free(j->title);
    c->loading = 0;
    if (c->workspace) {
        pv_menus(c);
        pv_changed(c);
    }
    pv_unref(c);
    free(j);
}

/** @brief Ask a worker to open @p path, with @p password when it is
 *         locked ("" for none); for a document sealed to certificates,
 *         @p keyfile and the key file's password.  One opening at a time. */
static int pv_load_as(tiku_pdfview_t *c, const char *path,
                      const char *keyfile, const char *password)
{
    pv_load_t *j;
    tiku_work_job_t work;

    if (!path || !*path || strlen(path) >= TIKU_PATH_MAX) return 0;
    if (c->loading) {
        pv_say(c, "A PDF is still opening. Try again in a moment.");
        return 0;
    }
    j = calloc(1, sizeof *j);
    if (!j) return 0;
    j->owner = c;
    j->generation = c->generation;
    snprintf(j->path, sizeof j->path, "%s", path);
    snprintf(j->keyfile, sizeof j->keyfile, "%s", keyfile);
    snprintf(j->password, sizeof j->password, "%s", password);
    j->tried = password[0] != '\0' || keyfile[0] != '\0';
    memset(&work, 0, sizeof work);
    work.run = pv_load_run;
    work.done = pv_load_done;
    work.free = pv_load_free;
    work.ctx = j;
    c->refs++;
    c->loading = 1;
    if (tiku_work_post(&work) != 0) {
        pv_load_free(j);
        pv_say(c, "The workers are busy. Try again in a moment.");
        return 0;
    }
    pv_say(c, "Opening...");
    pv_menus(c);
    return 1;
}

/** @brief Ask a worker to open @p path, with @p password when it is
 *         locked ("" for none).  One opening at a time. */
static int pv_load(tiku_pdfview_t *c, const char *path, const char *password)
{
    return pv_load_as(c, path, "", password);
}

/* ------------------------------------------------------------ drawing  */

/** @brief Drawing one page, and reading its links and text the first
 *         time it is drawn. */
typedef struct {
    tiku_pdfview_t *owner;
    pv_doc_t *doc;
    int slot, page, text, nlink, failed;
    int turn;                       /* the view's turn it is drawn at   */
    double scale;                   /* what the page is drawn at        */
    long nch;
    tiku_pdf_pixmap_t pm;
    tiku_pdf_link_t *link;
    tiku_pdf_char_t *ch;
} pv_render_t;

/** @brief On a worker: the page's pixels, then what it says. */
static void pv_render_run(void *ctx)
{
    pv_render_t *j = ctx;

    if (tiku_pdf_render(j->doc->pdf, j->page, j->scale, j->turn,
                        &j->pm) != 0)
        j->failed = 1;
    if (!j->text) return;
    j->nlink = tiku_pdf_links(j->doc->pdf, j->page, &j->link);
    if (j->nlink < 0) j->nlink = 0;
    j->nch = tiku_pdf_page_chars(j->doc->pdf, j->page, &j->ch);
    if (j->nch < 0) j->nch = 0;
}

/** @brief Keep the text of the pages used last, @c PV_TEXT_KEEP of them,
 *         never a page the selection or the view is on. */
static void pv_trim_text(tiku_pdfview_t *c)
{
    while (c->ntext > PV_TEXT_KEEP) {
        int old = -1;

        for (int p = 0; p < c->npage; p++) {
            pv_page_t *pg = &c->pages[p];
            if (!pg->text || pg->used == c->frame ||
                (c->selected && p >= c->anchor.page && p <= c->focus.page) ||
                (c->selected && p <= c->anchor.page && p >= c->focus.page))
                continue;
            if (old < 0 || pg->used < c->pages[old].used) old = p;
        }
        if (old < 0) return;
        tiku_pdf_links_free(c->pages[old].link, c->pages[old].nlink);
        free(c->pages[old].ch);
        c->pages[old].link = NULL;
        c->pages[old].ch = NULL;
        c->pages[old].nlink = 0;
        c->pages[old].nch = 0;
        c->pages[old].text = 0;
        c->ntext--;
    }
}

/** @brief Pixels @p *px, @p *w by @p *h, turned @p by degrees clockwise
 *         in their own place.  @return 0, or -1 with them let go when
 *         there is no memory for the turned copy. */
static int pv_turn_pixels(uint32_t **px, int *w, int *h, int by)
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
static void pv_render_done(void *ctx)
{
    pv_render_t *j = ctx;
    tiku_pdfview_t *c = j->owner;
    pv_tile_t *t = &c->tile[j->slot];

    if (!c->workspace || j->doc != c->doc) return;
    if (t->pending && t->page == j->page) {
        t->pending = 0;
        if (j->failed) {
            t->page = -1;
            c->pages[j->page].failed = 1;
        } else if (pv_turn_pixels(&j->pm.px, &j->pm.w, &j->pm.h,
                                  c->turn - j->turn) != 0) {
            t->page = -1;           /* no room to turn: asked for again */
        } else {
            t->px = j->pm.px;
            t->w = j->pm.w;
            t->h = j->pm.h;
            j->pm.px = NULL;
        }
    }
    if (j->text && !c->pages[j->page].text) {
        pv_page_t *pg = &c->pages[j->page];
        pg->link = j->link;
        pg->nlink = j->nlink;
        pg->ch = j->ch;
        pg->nch = j->nch;
        pg->text = 1;
        pg->used = c->frame;
        j->link = NULL;
        j->ch = NULL;
        c->ntext++;
        pv_trim_text(c);
    }
    pv_changed(c);
}

/** @brief After done or a cancel: the slot is let go if it still waits. */
static void pv_render_free(void *ctx)
{
    pv_render_t *j = ctx;
    tiku_pdfview_t *c = j->owner;

    if (j->doc == c->doc) {
        pv_tile_t *t = &c->tile[j->slot];
        c->inflight--;
        if (t->pending && t->page == j->page) {
            t->pending = 0;
            t->page = -1;
        }
        if (c->workspace) pv_changed(c);
    }
    tiku_pdf_pixmap_free(&j->pm);
    tiku_pdf_links_free(j->link, j->nlink);
    free(j->ch);
    pv_doc_unref(j->doc);
    pv_unref(c);
    free(j);
}

/** @brief The scale page @p p is drawn at: the view's, unless that would
 *         take more than @c PV_PAGE_PIXELS, when it is shown enlarged. */
static double pv_draw_scale(tiku_pdfview_t *c, int p)
{
    double area = (double)c->pw[p] * c->ph[p] * c->scale * c->scale;

    if (area <= (double)PV_PAGE_PIXELS) return c->scale;
    return sqrt((double)PV_PAGE_PIXELS / ((double)c->pw[p] * c->ph[p]));
}

/** @brief A slot for @p pixels more, giving back the pages shown least
 *         recently, never one on screen now.  @return it, or -1. */
static int pv_slot(tiku_pdfview_t *c, long pixels)
{
    for (;;) {
        long held = 0;
        int free_slot = -1, old = -1;

        for (int i = 0; i < PV_TILES; i++) {
            pv_tile_t *t = &c->tile[i];
            if (t->px) held += (long)t->w * t->h;
            if (t->pending) held += pixels;
            if (t->page < 0 && !t->pending) {
                if (free_slot < 0) free_slot = i;
            } else if (!t->pending && t->used != c->frame &&
                       (old < 0 || t->used < c->tile[old].used)) {
                old = i;
            }
        }
        if (free_slot >= 0 && held + pixels <= PV_PIXELS) return free_slot;
        if (old < 0) return -1;
        free(c->tile[old].px);
        memset(&c->tile[old], 0, sizeof c->tile[old]);
        c->tile[old].page = -1;
    }
}

/** @brief Ask a worker to draw page @p p at the view's scale. */
static int pv_request(tiku_pdfview_t *c, int p)
{
    pv_render_t *j;
    tiku_work_job_t work;
    double scale;
    int slot;

    if (!c->doc || c->inflight >= PV_INFLIGHT || c->pages[p].failed)
        return 0;
    scale = pv_draw_scale(c, p);
    slot = pv_slot(c, (long)(c->pw[p] * scale + 1) * (long)(c->ph[p] * scale
                                                             + 1));
    if (slot < 0) return 0;
    j = calloc(1, sizeof *j);
    if (!j) return 0;
    j->owner = c;
    j->doc = c->doc;
    j->slot = slot;
    j->page = p;
    j->scale = scale;
    j->turn = c->turn;
    j->text = !c->pages[p].text;
    memset(&work, 0, sizeof work);
    work.run = pv_render_run;
    work.done = pv_render_done;
    work.free = pv_render_free;
    work.ctx = j;
    snprintf(work.key, sizeof work.key, "%s", c->doc->key);
    c->tile[slot].page = p;
    c->tile[slot].scale = c->scale;
    c->tile[slot].pending = 1;
    c->tile[slot].used = c->frame;
    c->refs++;
    c->doc->refs++;
    c->inflight++;
    if (tiku_work_post(&work) != 0) {
        c->tile[slot].page = -1;
        c->tile[slot].pending = 0;
        c->refs--;
        c->doc->refs--;
        c->inflight--;
        free(j);
        return 0;
    }
    return 1;
}

/* --------------------------------------------------------- thumbnails  */

/** @brief The scale page @p p fits the thumbnail box at, turned. */
static double pv_thumb_scale(tiku_pdfview_t *c, int p)
{
    double sx = PV_THUMB_W / fmax(pv_pw(c, p), 1);
    double sy = PV_THUMB_H / fmax(pv_ph(c, p), 1);

    return sx < sy ? sx : sy;
}

/** @brief Page @p p's thumbnail, drawn or on its way; NULL for neither. */
static pv_tile_t *pv_thumb(tiku_pdfview_t *c, int p)
{
    for (int i = 0; i < PV_THUMB_KEEP; i++)
        if (c->thumb[i].page == p) return &c->thumb[i];
    return NULL;
}

/** @brief On the loop: the small page goes to its slot, unless the view
 *         turned while it was drawn: then it is asked for again. */
static void pv_thumb_done(void *ctx)
{
    pv_render_t *j = ctx;
    tiku_pdfview_t *c = j->owner;
    pv_tile_t *t = &c->thumb[j->slot];

    if (!c->workspace || j->doc != c->doc) return;
    if (j->turn != c->turn && t->pending && t->page == j->page) {
        t->pending = 0;
        t->page = -1;
    }
    if (t->pending && t->page == j->page) {
        t->pending = 0;
        if (j->failed) {
            t->page = -1;
            c->pages[j->page].failed = 1;
        } else {
            t->px = j->pm.px;
            t->w = j->pm.w;
            t->h = j->pm.h;
            j->pm.px = NULL;
        }
    }
    pv_changed(c);
}

/** @brief After done or a cancel: the slot is let go if it still waits. */
static void pv_thumb_free(void *ctx)
{
    pv_render_t *j = ctx;
    tiku_pdfview_t *c = j->owner;

    if (j->doc == c->doc) {
        pv_tile_t *t = &c->thumb[j->slot];
        c->thumb_busy--;
        if (t->pending && t->page == j->page) {
            t->pending = 0;
            t->page = -1;
        }
        if (c->workspace) pv_changed(c);
    }
    tiku_pdf_pixmap_free(&j->pm);
    pv_doc_unref(j->doc);
    pv_unref(c);
    free(j);
}

/**
 * @brief Ask a worker to draw page @p p small, when the pages in view
 *        wait for nothing: thumbnails come one at a time, behind them.
 *        The slot is the free one, or the one shown least lately.
 */
static int pv_thumb_request(tiku_pdfview_t *c, int p)
{
    pv_render_t *j;
    tiku_work_job_t work;
    int slot = -1;

    if (!c->doc || c->inflight || c->thumb_busy || c->pages[p].failed)
        return 0;
    for (int i = 0; i < PV_THUMB_KEEP; i++) {
        pv_tile_t *t = &c->thumb[i];
        if (t->pending) continue;
        if (t->page < 0) { slot = i; break; }
        if (t->used != c->frame &&
            (slot < 0 || t->used < c->thumb[slot].used)) slot = i;
    }
    if (slot < 0 || !(j = calloc(1, sizeof *j))) return 0;
    free(c->thumb[slot].px);
    memset(&c->thumb[slot], 0, sizeof c->thumb[slot]);
    j->owner = c;
    j->doc = c->doc;
    j->slot = slot;
    j->page = p;
    j->scale = pv_thumb_scale(c, p);
    j->turn = c->turn;
    memset(&work, 0, sizeof work);
    work.run = pv_render_run;       /* the page's pixels, no text */
    work.done = pv_thumb_done;
    work.free = pv_thumb_free;
    work.ctx = j;
    snprintf(work.key, sizeof work.key, "%s", c->doc->key);
    c->thumb[slot].page = p;
    c->thumb[slot].pending = 1;
    c->thumb[slot].used = c->frame;
    c->refs++;
    c->doc->refs++;
    c->thumb_busy++;
    if (tiku_work_post(&work) != 0) {
        c->thumb[slot].page = -1;
        c->thumb[slot].pending = 0;
        c->refs--;
        c->doc->refs--;
        c->thumb_busy--;
        free(j);
        return 0;
    }
    return 1;
}

/* ---------------------------------------------------------- searching  */

/** @brief One stretch of pages searched for the folded query. */
typedef struct {
    tiku_pdfview_t *owner;
    pv_doc_t *doc;
    int gen, from, to, nq, nmatch, cap;
    int scanned, failed, limited;
    int keep_case, whole;           /* Match Case, Whole Words          */
    uint32_t q[TIKU_FIELD_MAX];
    pv_match_t *match;
} pv_search_t;

/** @brief A character as a search compares it: the kinds of space,
 *         hyphen and quote a typeset page uses folded together, and case
 *         too unless @p keep_case. */
static uint32_t pv_fold(uint32_t cp, int keep_case)
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
static void pv_match_boxes(pv_match_t *m, const tiku_pdf_char_t *ch, long a,
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

/** @brief Whether @p cp belongs to a word: an ASCII letter, digit or '_',
 *         or a character from U+00C0 on other than the multiplication and
 *         division signs. */
static int pv_wordy(uint32_t cp)
{
    return cp < 0x80 ? isalnum((int)cp) || cp == '_'
                     : cp >= 0xc0 && cp != 0xd7 && cp != 0xf7;
}

/** @brief Every place on page @p p where the query stands, spaces folded
 *         to one so a phrase broken over two lines is still found; with
 *         Whole Words, only where no letter touches either end. */
static void pv_search_page(pv_search_t *j, int p, const tiku_pdf_char_t *ch,
                           long n)
{
    uint32_t *f = malloc((size_t)n * sizeof *f);
    long *at = malloc((size_t)n * sizeof *at), m = 0;

    if (!f || !at) j->failed = 1;

    for (long i = 0; f && at && i < n; i++) {
        uint32_t cp = pv_fold(ch[i].cp, j->keep_case);
        if (cp == ' ' && (m == 0 || f[m - 1] == ' ')) continue;
        f[m] = cp;
        at[m++] = i;
    }
    for (long i = 0; f && at && i + j->nq <= m; i++) {
        int k = 0;
        while (k < j->nq && f[i + k] == j->q[k]) k++;
        if (k < j->nq) continue;
        if (j->whole && ((i > 0 && pv_wordy(f[i - 1])) ||
                         (i + j->nq < m && pv_wordy(f[i + j->nq]))))
            continue;
        if (j->nmatch == j->cap) {
            int cap = j->cap ? j->cap * 2 : 32;
            pv_match_t *grown = realloc(j->match, (size_t)cap * sizeof *grown);
            if (!grown) { j->failed = 1; break; }
            j->match = grown;
            j->cap = cap;
        }
        j->match[j->nmatch].page = p;
        pv_match_boxes(&j->match[j->nmatch], ch, at[i], at[i + j->nq - 1]);
        j->nmatch++;
        i += j->nq - 1;
        if (j->nmatch >= PV_MATCHES) { j->limited = 1; break; }
    }
    free(f);
    free(at);
}

/** @brief On a worker: read each page's text and look for the query;
 *         a newer search, or a closed document, stops it between pages. */
static void pv_search_run(void *ctx)
{
    pv_search_t *j = ctx;

    for (int p = j->from; p < j->to && j->nmatch < PV_MATCHES; p++) {
        tiku_pdf_char_t *ch = NULL;
        long n;

        if (atomic_load(&j->doc->search) != j->gen) return;
        n = tiku_pdf_page_chars(j->doc->pdf, p, &ch);
        if (n < 0) j->failed = 1;
        if (n > 0) pv_search_page(j, p, ch, n);
        free(ch);
        if (j->failed || j->limited) break;
        j->scanned++;
    }
}

static int pv_search_post(tiku_pdfview_t *c, int from);
static void pv_find_step(tiku_pdfview_t *c, int dir);

/** @brief Put how the search stands in the status line; a search that
 *         failed or reached the match limit is called incomplete. */
static void pv_search_status(tiku_pdfview_t *c)
{
    char said[320];
    if (c->search_failed)
        snprintf(said, sizeof said, "Search incomplete: could not read or "
                 "search all pages. %d matches retained. "
                 "Press Enter to retry.", c->nmatch);
    else if (c->search_limited)
        snprintf(said, sizeof said, "Showing the first %d matches; "
                 "result limit reached. Narrow your search.", c->nmatch);
    else if (c->cur >= 0 && c->cur < c->nmatch)
        snprintf(said, sizeof said, "Match %d of %d%s.", c->cur + 1,
                 c->nmatch, c->searching ? " so far" : "");
    else if (c->searching)
        snprintf(said, sizeof said, "Searching... %d of %d pages read.",
                 c->searched, c->npage);
    else if (c->nmatch)
        snprintf(said, sizeof said, "%d matches.", c->nmatch);
    else
        snprintf(said, sizeof said, "Not found in this document.");
    pv_say(c, said);
}

/** @brief On the loop: the stretch's matches join the rest, and the next
 *         stretch is asked for behind any pages waiting to be drawn. */
static void pv_search_done(void *ctx)
{
    pv_search_t *j = ctx;
    tiku_pdfview_t *c = j->owner;

    if (!c->workspace || j->doc != c->doc || j->gen != c->search_gen) return;
    if (j->nmatch > 0) {
        int room = PV_MATCHES - c->nmatch;
        int take = j->nmatch < room ? j->nmatch : room;
        pv_match_t *all = realloc(c->match, (size_t)(c->nmatch + take) *
                                            sizeof *all);
        if (all) {
            memcpy(all + c->nmatch, j->match, (size_t)take * sizeof *all);
            c->match = all;
            c->nmatch += take;
        } else c->search_failed = 1;
        if (take < j->nmatch) c->search_limited = 1;
    }
    c->search_failed |= j->failed;
    c->search_limited |= j->limited || c->nmatch >= PV_MATCHES;
    c->searched = j->from + j->scanned;
    c->searching = 0;
    if (!c->search_failed && !c->search_limited && c->searched < c->npage) {
        c->searching = pv_search_post(c, c->searched);
        if (!c->searching) c->search_failed = 1;
    }
    if (c->want_next) pv_find_step(c, c->want_next);
    pv_search_status(c);
    pv_changed(c);
}

/** @brief After done, or instead of it: free the job. */
static void pv_search_free(void *ctx)
{
    pv_search_t *j = ctx;
    tiku_pdfview_t *c = j->owner;

    free(j->match);
    pv_doc_unref(j->doc);
    pv_unref(c);
    free(j);
}

/** @brief Ask a worker to search the next stretch from page @p from. */
static int pv_search_post(tiku_pdfview_t *c, int from)
{
    pv_search_t *j = calloc(1, sizeof *j);
    tiku_work_job_t work;

    if (!j) return 0;
    j->owner = c;
    j->doc = c->doc;
    j->gen = c->search_gen;
    j->from = from;
    j->to = from + PV_SEARCH_STEP < c->npage ? from + PV_SEARCH_STEP
                                             : c->npage;
    j->keep_case = c->match_case;
    j->whole = c->whole_words;
    j->nq = pv_query(c->query, j->q, TIKU_FIELD_MAX, j->keep_case);
    memset(&work, 0, sizeof work);
    work.run = pv_search_run;
    work.done = pv_search_done;
    work.free = pv_search_free;
    work.ctx = j;
    snprintf(work.key, sizeof work.key, "%s", c->doc->key);
    c->refs++;
    c->doc->refs++;
    if (tiku_work_post(&work) != 0) {
        c->refs--;
        c->doc->refs--;
        free(j);
        return 0;
    }
    return 1;
}
