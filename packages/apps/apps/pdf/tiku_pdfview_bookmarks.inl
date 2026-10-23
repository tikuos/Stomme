/* tiku_pdfview_bookmarks.inl -- part of tiku_pdfview.c: the pages the
 * person bookmarks, kept in a small file in the settings folder, which is
 * changed under a lock and replaced whole; and the Bookmarks menu.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */

/** @brief Where the bookmarks are kept: $TIKU_PDF_BOOKMARKS (empty keeps
 *         none), else TikuPDFBookmarks in $XDG_CONFIG_HOME or ~/.config,
 *         which @p make creates when it is missing.  @return 0 for nowhere. */
static int pv_bookmark_file(char *out, size_t cap, int make)
{
    const char *custom = getenv(TIKU_PRODUCT_PDF_TIKU_PDF_BOOKMARKS);
    if (custom) return *custom && snprintf(out, cap, "%s", custom) < (int)cap;
    const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    char dir[TIKU_PATH_MAX];
    int n = xdg && *xdg ? snprintf(dir, sizeof dir, "%s", xdg) :
        home && *home ? snprintf(dir, sizeof dir, "%s/.config", home) : -1;
    if (n < 0 || n >= (int)sizeof dir) return 0;
    if (make) (void)mkdir(dir, 0700);
    return snprintf(out, cap, TIKU_PRODUCT_PDF_BOOKMARK_PATH, dir) < (int)cap;
}

/** @brief Replace the list with the file's; a missing file is an empty
 *         list.  @return 0, keeping the list, when the file is unreadable. */
static int pv_bookmarks_read(tiku_pdfview_t *c)
{
    char path[TIKU_PATH_MAX], line[TIKU_PATH_MAX + 128];
    if (!pv_bookmark_file(path, sizeof path, 0)) return 1;
    FILE *f = fopen(path, "r");
    if (!f) {
        if (errno != ENOENT) return 0;
        c->nbookmarks = 0; return 1;
    }
    pv_place_t marks[PV_BOOKMARK_KEEP]; int count = 0;
    for (int i = 0; i < PV_BOOKMARK_KEEP && fgets(line, sizeof line, f); i++)
        count += pv_place_read(line, &marks[count]);
    int ok = !ferror(f);
    fclose(f);
    if (ok) {
        memcpy(c->bookmarks, marks, (size_t)count * sizeof *marks);
        c->nbookmarks = count;
    }
    return ok;
}

/** @brief Replace the file atomically; a failed write leaves the previous
 *         file intact.  @return 1 when written. */
static int pv_bookmarks_write(tiku_pdfview_t *c, const char *path)
{
    char temp[TIKU_PATH_MAX + 16], zoom[16];
    snprintf(temp, sizeof temp, "%s.XXXXXX", path);
    int fd = mkstemp(temp);
    if (fd < 0) return 0;
    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); unlink(temp); return 0; }
    int ok = 1;
    for (int i = 0; i < c->nbookmarks; i++) {
        pv_place_t *r = &c->bookmarks[i];
        if (r->fit == PV_FIXED)
            snprintf(zoom, sizeof zoom, "%d", (int)(r->scale*100+0.5));
        else snprintf(zoom, sizeof zoom, "%s",
                      r->fit == PV_BY_PAGE ? "page" : "width");
        if (fprintf(f, "%d\t%.1f\t%.1f\t%s\t%d\t%s\n", r->page+1,
                    r->down, r->across, zoom, r->turn, r->path) < 0) ok = 0;
    }
    if (fflush(f) || fsync(fd)) ok = 0;
    if (fclose(f)) ok = 0;
    if (!ok || rename(temp, path)) { unlink(temp); return 0; }
    return 1;
}

/**
 * @brief Take the lock beside @p path if it is free: it is tried once, not
 *        waited for.  The lock is a sibling file, @p path ".lock", which
 *        the rename that replaces the bookmark file leaves in place.
 * @return the lock's descriptor; -1 when it cannot be opened or taken,
 *         so the file cannot be written; -2 when another window holds it.
 */
static int pv_bookmark_lock(const char *path)
{
    char lockpath[TIKU_PATH_MAX + 8];
    snprintf(lockpath, sizeof lockpath, "%s.lock", path);
    int lock = open(lockpath, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock < 0) return -1;
    if (flock(lock, LOCK_EX | LOCK_NB)) {
        int busy = errno == EWOULDBLOCK;
        close(lock);
        return busy ? -2 : -1;
    }
    return lock;
}

/** @brief The bookmark on @p place's page of the same file, or -1. */
static int pv_bookmark_at(tiku_pdfview_t *c, const pv_place_t *place)
{
    for (int i = 0; i < c->nbookmarks; i++)
        if (c->bookmarks[i].page == place->page &&
            !strcmp(c->bookmarks[i].path, place->path)) return i;
    return -1;
}

/**
 * @brief Bookmark the page shown, or remove its bookmark.
 *
 * Whether it adds or removes is decided from this window's list, then
 * applied to the file as it is now.  A window whose bookmarks could not
 * be saved keeps them to itself and leaves the file alone.
 */
static void pv_bookmark_toggle(tiku_pdfview_t *c)
{
    pv_place_t place;
    if (!pv_place_now(c, &place)) return;
    place.page = pv_current(c);
    place.down = fmax(0, (c->vbar.value - c->top[place.page]) / c->scale);
    for (const char *p = place.path; *p; p++)
        if ((unsigned char)*p < 32) {
            pv_say(c, "This filename cannot be stored as a bookmark.");
            return;
        }
    int remove = pv_bookmark_at(c, &place) >= 0, lock = -1;
    const char *unsaved = c->bookmarks_session ?
        "bookmarks in this window are not being saved" : NULL;
    char path[TIKU_PATH_MAX], said[sizeof c->status];
    if (!unsaved && pv_bookmark_file(path, sizeof path, 1)) {
        lock = pv_bookmark_lock(path);
        if (lock == -2) {
            pv_say(c, "Bookmarks are busy in another window; "
                   "nothing was changed. Try again.");
            return;
        }
        if (lock < 0) {
            unsaved = "the bookmarks file cannot be written";
        } else if (!pv_bookmarks_read(c)) {
            close(lock);
            pv_say(c, "Could not read saved bookmarks; nothing was changed.");
            return;
        }
    }
    int at = pv_bookmark_at(c, &place);
    if (remove && at >= 0)
        memmove(c->bookmarks+at, c->bookmarks+at+1,
                (size_t)(--c->nbookmarks-at) * sizeof place);
    if (!remove && at < 0) {
        if (c->nbookmarks == PV_BOOKMARK_KEEP) {
            if (lock >= 0) close(lock);
            snprintf(said, sizeof said, "Bookmark limit reached (%d). "
                     "Remove a bookmark to add another.", PV_BOOKMARK_KEEP);
            pv_say(c, said);
            return;
        }
        c->bookmarks[c->nbookmarks++] = place;
    }
    if (lock >= 0) {
        if (!pv_bookmarks_write(c, path))
            unsaved = "saving bookmarks failed";
        close(lock);
    }
    if (unsaved) {
        c->bookmarks_session = 1;
        snprintf(said, sizeof said, "%s for this session only: %s.",
                 remove ? "Page bookmark removed" : "Page bookmarked",
                 unsaved);
        pv_say(c, said);
    } else {
        pv_say(c, remove ? "Page bookmark removed." :
               "Page bookmarked. Choose it from Bookmarks to return.");
    }
}

/** @brief Go to bookmark @p index: its page, place, zoom and turn. */
static void pv_bookmark_go(tiku_pdfview_t *c, int index)
{
    pv_place_t current;
    if (index < 0 || index >= c->nbookmarks || !pv_place_now(c, &current))
        return;
    pv_place_t mark = c->bookmarks[index];
    if (strcmp(mark.path, current.path) || mark.page >= c->npage) return;
    pv_go(c, mark.page, mark.down, 1);
    pv_turn(c, mark.turn - c->turn);
    pv_set_scale(c, mark.fit, mark.scale);
    c->restore = mark; c->restoring = 1;
}

/** @brief The Bookmarks menu: the toggle, then this document's pages. */
static void pv_bookmark_menu(tiku_pdfview_t *c, tiku_menu_list_t *menu)
{
    pv_place_t current;
    int loaded = pv_place_now(c, &current);
    if (loaded) current.page = pv_current(c);
    int marked = loaded && pv_bookmark_at(c, &current) >= 0;
    pv_menu_add(c, menu, PV_BOOKMARK, 'd', TIKU_MOD_CMD, marked);
    if (marked)
        snprintf(menu->item[0].label, sizeof menu->item[0].label,
                 "Remove Page Bookmark");
    for (int i = 0; loaded && i < c->nbookmarks &&
                    menu->nitem < TIKU_MENUSET_ITEMS; i++) {
        pv_place_t *r = &c->bookmarks[i];
        if (strcmp(r->path, current.path)) continue;
        tiku_menu_entry_t *item = &menu->item[menu->nitem++];
        snprintf(item->label, sizeof item->label, "Page %d%s", r->page+1,
                 r->page >= c->npage ? " (no longer present)" : "");
        item->command = PV_BOOKMARK_GO+i;
        item->enabled = r->page < c->npage;
        item->marked = current.page == r->page;
    }
}
