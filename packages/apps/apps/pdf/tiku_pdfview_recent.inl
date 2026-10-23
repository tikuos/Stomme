/* tiku_pdfview_recent.inl -- part of tiku_pdfview.c: recent documents and
 * where each was left (a page, how far down and across it, the zoom and the
 * turn), kept most recent first in a small file in the settings folder; the
 * first few are the File menu's Open Recent.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */

#define PV_RECENT_MAGIC TIKU_PRODUCT_PDF_RECENT_MAGIC

/** @brief Where the list is kept: $TIKU_PDF_RECENT (empty keeps none),
 *         else TikuPDFRecent in $XDG_CONFIG_HOME or ~/.config, which
 *         @p make creates when it is missing.  @return 0 for nowhere. */
static int pv_recent_file(char *out, size_t cap, int make)
{
    const char *custom = getenv(TIKU_PRODUCT_PDF_TIKU_PDF_RECENT);
    const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    char dir[TIKU_PATH_MAX];
    int n;

    if (custom) return *custom && snprintf(out, cap, "%s", custom) < (int)cap;
    if (xdg && *xdg) n = snprintf(dir, sizeof dir, "%s", xdg);
    else if (home && *home) n = snprintf(dir, sizeof dir, "%s/.config", home);
    else return 0;
    if (n >= (int)sizeof dir) return 0;
    if (make) (void)mkdir(dir, 0700);
    return snprintf(out, cap, TIKU_PRODUCT_PDF_RECENT_PATH, dir) < (int)cap;
}

/** @brief @p text as a zoom: Fit Width, Fit Page, or a fixed percentage.
 *         @return 0 when it is none of them. */
static int pv_zoom_read(const char *text, pv_place_t *r)
{
    char *end;
    long pct;

    if (!strcmp(text, "width")) r->fit = PV_BY_WIDTH;
    else if (!strcmp(text, "page")) r->fit = PV_BY_PAGE;
    else {
        pct = strtol(text, &end, 10);
        if (*end || end == text || pct < 25 || pct > 400) return 0;
        r->fit = PV_FIXED;
        r->scale = pct / 100.0;
    }
    return 1;
}

/** @brief One line of the file as a place, or 0 when it does not parse:
 *         a page from 1, down, across, zoom, turn, then the path. */
static int pv_place_read(char *line, pv_place_t *r)
{
    char zoom[16];
    int used = 0;

    line[strcspn(line, "\r\n")] = 0;
    memset(r, 0, sizeof *r);
    if (sscanf(line, "%d\t%lf\t%lf\t%15[^\t]\t%d\t%n", &r->page, &r->down,
               &r->across, zoom, &r->turn, &used) != 5 || !used)
        return 0;
    if (r->page < 1 || r->turn % 90 || r->turn < 0 || r->turn > 270 ||
        !(fabs(r->down) < 1e6) || !(fabs(r->across) < 1e6) ||
        !pv_zoom_read(zoom, r) || line[used] != '/' ||
        strlen(line + used) >= sizeof r->path)
        return 0;
    for (const char *p = line + used; *p; p++)
        if ((unsigned char)*p < 32) return 0;
    snprintf(r->path, sizeof r->path, "%s", line + used);
    r->page--;
    r->gone = access(r->path, F_OK) != 0;
    return 1;
}

/** @brief Read the list from the file; lines that do not parse are
 *         skipped. */
static void pv_recent_read(tiku_pdfview_t *c)
{
    char file[TIKU_PATH_MAX], line[TIKU_PATH_MAX + 128];
    FILE *f;

    c->nrecent = 0;
    if (!pv_recent_file(file, sizeof file, 0) || !(f = fopen(file, "r")))
        return;
    if (fgets(line, sizeof line, f) &&
        !strncmp(line, PV_RECENT_MAGIC, strlen(PV_RECENT_MAGIC))) {
        while (c->nrecent < PV_RECENT_KEEP && fgets(line, sizeof line, f))
            c->nrecent += pv_place_read(line, &c->recent[c->nrecent]);
    }
    fclose(f);
}

/** @brief Replace the file with the list through a temporary file renamed
 *         over it; a failed write leaves the old file. */
static void pv_recent_write(tiku_pdfview_t *c)
{
    char file[TIKU_PATH_MAX], temp[TIKU_PATH_MAX + 16], zoom[16];
    int fd, ok = 1;
    FILE *f;

    if (!pv_recent_file(file, sizeof file, 1)) return;
    snprintf(temp, sizeof temp, "%s.XXXXXX", file);
    if ((fd = mkstemp(temp)) < 0) return;
    if (!(f = fdopen(fd, "w"))) {
        close(fd);
        unlink(temp);
        return;
    }
    ok = fprintf(f, "%s\n", PV_RECENT_MAGIC) > 0;
    for (int i = 0; i < c->nrecent; i++) {
        const pv_place_t *r = &c->recent[i];
        if (r->fit == PV_FIXED)
            snprintf(zoom, sizeof zoom, "%d", (int)(r->scale * 100 + 0.5));
        else snprintf(zoom, sizeof zoom, r->fit == PV_BY_PAGE ? "page"
                                                             : "width");
        if (fprintf(f, "%d\t%.1f\t%.1f\t%s\t%d\t%s\n", r->page + 1, r->down,
                    r->across, zoom, r->turn, r->path) < 0) ok = 0;
    }
    if (fflush(f) || fsync(fd)) ok = 0;
    if (fclose(f)) ok = 0;
    if (!ok || rename(temp, file)) unlink(temp);
}

/** @brief The shown document's place as the view stands, or as it is
 *         about to be taken up.  @return 0 when nothing is shown. */
static int pv_place_now(tiku_pdfview_t *c, pv_place_t *r)
{
    char real[PATH_MAX];

    if (!c->doc || !c->npage) return 0;
    if (c->restoring) *r = c->restore;
    else {
        memset(r, 0, sizeof *r);
        r->fit = c->fit;
        r->scale = c->scale;
        r->turn = c->turn;
        if (c->laid_scale) {
            r->page = pv_page_at(c, c->vbar.value);
            r->down = (c->vbar.value - c->top[r->page]) / c->scale;
            r->across = c->hbar.value / c->scale;
        }
    }
    if (!realpath(c->path, real) || strlen(real) >= sizeof r->path)
        snprintf(real, sizeof real, "%s", c->path);
    snprintf(r->path, sizeof r->path, "%s", real);
    r->gone = 0;
    return 1;
}

/** @brief Whether places @p a and @p b are one, as the file keeps them. */
static int pv_place_same(const pv_place_t *a, const pv_place_t *b)
{
    return !strcmp(a->path, b->path) && a->page == b->page &&
           fabs(a->down - b->down) < 0.05 &&
           fabs(a->across - b->across) < 0.05 && a->fit == b->fit &&
           (a->fit != PV_FIXED || fabs(a->scale - b->scale) < 0.005) &&
           a->turn == b->turn;
}

/** @brief The shown document to the top of the list with its place; the
 *         file written when that changed anything.  A path the file could
 *         not hold on one line is not kept. */
static void pv_recent_note(tiku_pdfview_t *c)
{
    pv_place_t now;
    int at = 0;

    if (!pv_place_now(c, &now) || now.path[0] != '/') return;
    for (const char *p = now.path; *p; p++)
        if ((unsigned char)*p < 32) return;
    while (at < c->nrecent && strcmp(c->recent[at].path, now.path)) at++;
    if (at == 0 && c->nrecent && pv_place_same(&c->recent[0], &now)) return;
    if (at == c->nrecent && at == PV_RECENT_KEEP) at--;   /* oldest goes */
    memmove(&c->recent[1], &c->recent[0], (size_t)at * sizeof now);
    c->recent[0] = now;
    if (at == c->nrecent) c->nrecent++;
    pv_recent_write(c);
}

/** @brief Where the document just opened was left, taken up again: its
 *         zoom and turn now, its page and scroll at the first layout. */
static void pv_recent_restore(tiku_pdfview_t *c)
{
    pv_place_t now;

    if (!pv_place_now(c, &now)) return;
    for (int i = 0; i < c->nrecent; i++) {
        const pv_place_t *r = &c->recent[i];
        if (strcmp(r->path, now.path)) continue;
        c->fit = r->fit;
        if (r->fit == PV_FIXED) c->scale = r->scale;
        c->turn = r->turn;
        pv_find_widest(c);
        c->restore = *r;
        c->restore.page = r->page < c->npage ? r->page : c->npage - 1;
        c->restoring = 1;
        return;
    }
}

/** @brief At the first layout: the view where the document was left. */
static void pv_recent_apply(tiku_pdfview_t *c)
{
    const pv_place_t *r = &c->restore;

    if (!c->restoring || !c->laid_scale) return;
    c->restoring = 0;
    tiku_scroll_to(&c->vbar, (int)(c->top[r->page] + r->down * c->scale +
                                   0.5));
    tiku_scroll_to(&c->hbar, (int)(r->across * c->scale + 0.5));
}

/** @brief Open recent document @p i, unless it has gone from where it was:
 *         then it is said so and left off the menu. */
static void pv_recent_open(tiku_pdfview_t *c, int i)
{
    char said[TIKU_PATH_MAX + 64];

    if (i < 0 || i >= c->nrecent || c->loading) return;
    if (access(c->recent[i].path, R_OK) != 0) {
        c->recent[i].gone = 1;
        snprintf(said, sizeof said, "That PDF is no longer at %s.",
                 c->recent[i].path);
        pv_say(c, said);
        return;
    }
    (void)pv_load(c, c->recent[i].path, "");
}

/** @brief Forget every document and its place, in the file too. */
static void pv_recent_clear(tiku_pdfview_t *c)
{
    c->nrecent = 0;
    pv_recent_write(c);
    pv_say(c, "Recent documents and their places are forgotten.");
}

/** @brief The @p k-th recent document still where it was, or -1. */
static int pv_recent_shown(const tiku_pdfview_t *c, int k)
{
    for (int i = 0; i < c->nrecent; i++)
        if (!c->recent[i].gone && k-- == 0) return i;
    return -1;
}

/** @brief Recent document @p i's name for a menu row, @p cap bytes at
 *         most: its file's name, and its folder's when two share one. */
static void pv_recent_label(const tiku_pdfview_t *c, int i, char *out,
                            size_t cap)
{
    const char *path = c->recent[i].path, *name = strrchr(path, '/') + 1;
    size_t n;
    int twin = 0;

    for (int k = 0, j; k < PV_RECENT_SHOWN && (j = pv_recent_shown(c, k))
                                              >= 0; k++) {
        const char *other = strrchr(c->recent[j].path, '/') + 1;
        if (j != i && !strcmp(other, name)) twin = 1;
    }
    if (twin && name - path > 1) {
        const char *dir = name - 2;
        while (dir > path && *dir != '/') dir--;
        snprintf(out, cap, "%s (%.*s)", name, (int)(name - dir - 2),
                 dir + 1);
    } else {
        snprintf(out, cap, "%s", name);
    }
    n = strlen(out);                /* a character cut in two goes */
    if (n == cap - 1) {
        while (n && ((unsigned char)out[n - 1] & 0xc0) == 0x80) n--;
        if (n && (unsigned char)out[n - 1] >= 0xc0) n--;
        out[n] = 0;
    }
}

/** @brief File > Open Recent: the documents still where they were, and
 *         Clear Menu; a row that opens nothing while there are none. */
static void pv_menu_recent(tiku_pdfview_t *c, tiku_menu_list_t *m)
{
    tiku_menu_entry_t *holder, *e;
    int shown = 0, i;

    if (m->nitem >= TIKU_MENUSET_ITEMS - 2) return;
    holder = &m->item[m->nitem++];
    memset(holder, 0, sizeof *holder);
    snprintf(holder->label, sizeof holder->label, "Open Recent");
    while (shown < PV_RECENT_SHOWN && m->nitem < TIKU_MENUSET_ITEMS - 3 &&
           (i = pv_recent_shown(c, shown)) >= 0) {
        e = &m->item[m->nitem++];
        memset(e, 0, sizeof *e);
        pv_recent_label(c, i, e->label, sizeof e->label);
        e->command = PV_RECENT + i;
        e->enabled = (unsigned char)!c->loading;
        e->level = 1;
        shown++;
    }
    holder->enabled = shown > 0;
    if (!shown) return;
    e = &m->item[m->nitem++];
    memset(e, 0, sizeof *e);
    e->separator = e->level = 1;
    pv_menu_add(c, m, PV_CLEAR_RECENT, 0, 0, 0);
    m->item[m->nitem - 1].level = 1;
}
