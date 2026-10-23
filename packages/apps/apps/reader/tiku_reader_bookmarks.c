/* tiku_reader_bookmarks.c -- bookmarks: a page and a block each, kept in
 * a small text file for each profile, or each help folder, and written only
 * when one is added or removed.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_app_product.h"
#include "tiku_reader_private.h"
#include <errno.h>
#include <sys/file.h>

static const char *reader_bookmark_env(const tiku_reader_help_window_t *c);
static int reader_bookmark_path(tiku_reader_help_window_t *c, char *out, size_t cap);
static int reader_bookmark_lock(tiku_reader_help_window_t *c, const char *path);
static int reader_bookmark_find(const struct help_reader *r, const char *page,
                                int block);
static int reader_bookmarks_read(tiku_reader_help_window_t *c,
                                 const char *path);
static int reader_bookmarks_save(tiku_reader_help_window_t *c,
                                 const char *path);

/** @brief The variable that names this window's bookmark file. */
static const char *reader_bookmark_env(const tiku_reader_help_window_t *c)
{
    return c->document_mode ? c->profile->bookmark_env : TIKU_PRODUCT_READER_TIKU_HELP_BOOKMARKS;
}

/** @brief The bookmark file: the one the variable names when it is set;
 *         else, under XDG_CONFIG_HOME or ~/.config, the profile's file for
 *         a document window and one file per help folder for Help.
 *  @return 1, or 0 when there is none. */
static int reader_bookmark_path(tiku_reader_help_window_t *c, char *out, size_t cap)
{
    const char *custom = getenv(reader_bookmark_env(c));
    if (custom) return *custom && snprintf(out, cap, "%s", custom) < (int)cap;
    char config[TIKU_PATH_MAX], root[TIKU_PATH_MAX];
    const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    int n = xdg && *xdg ? snprintf(config, sizeof config, "%s", xdg) :
        snprintf(config, sizeof config, "%s/.config", home ? home : "");
    if (n >= (int)sizeof config || ((!xdg || !*xdg) && (!home || !*home))) return 0;
    if (c->document_mode)
        return snprintf(out, cap, "%s/%s", config,
                        c->profile->bookmark_file) < (int)cap;
    if (!tiku_reader_realpath(c->root, root, sizeof root)) snprintf(root, sizeof root, "%s", c->root);
    uint32_t hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)root; *p; p++) hash = (hash ^ *p) * 16777619u;
    return snprintf(out, cap, TIKU_PRODUCT_READER_S_TIKUHELPBOOKMARKS_08X, config, hash) < (int)cap;
}

/**
 * @brief Take the lock beside @p path if it is free: it is tried once, not
 *        waited for.  The lock is a sibling file, @p path ".lock", which
 *        the rename that replaces the bookmark file leaves in place; the
 *        default location's settings folder is made first.
 * @return the lock's descriptor; -1 when it cannot be opened or taken,
 *         so the file cannot be written; -2 when another window holds it.
 */
static int reader_bookmark_lock(tiku_reader_help_window_t *c, const char *path)
{
    char lockpath[TIKU_PATH_MAX + 8];
    if (!getenv(reader_bookmark_env(c))) {
        char dir[TIKU_PATH_MAX];
        snprintf(dir, sizeof dir, "%s", path);
        char *slash = strrchr(dir, '/');
        if (slash) {
            *slash = 0;
            (void)mkdir(dir, 0700);
        }
    }
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

/** @brief The index of the bookmark at @p page and @p block, or -1. */
static int reader_bookmark_find(const struct help_reader *r, const char *page,
                                int block)
{
    for (int i = 0; i < r->nmarks; i++) {
        if (!strcmp(r->marks[i].page, page) && r->marks[i].block == block) {
            return i;
        }
    }
    return -1;
}

/** @brief Read the window's bookmark file into its list, at most 32
 *         rows; nothing is written. */
void tiku_reader_bookmarks_load(tiku_reader_help_window_t *c)
{
    char path[TIKU_PATH_MAX];
    if (c->reader && reader_bookmark_path(c, path, sizeof path))
        (void)reader_bookmarks_read(c, path);
}

/** @brief Replace the list with the file's; a missing file is an empty
 *         list.  @return 0, keeping the list, when the file is unreadable. */
static int reader_bookmarks_read(tiku_reader_help_window_t *c,
                                 const char *path)
{
    char line[TIKU_PATH_MAX + 256];
    FILE *f = fopen(path, "r");
    if (!f) {
        if (errno != ENOENT) return 0;
        c->reader->nmarks = 0;
        return 1;
    }
    help_row_t marks[32]; int count = 0;
    for (int lines = 0; lines < 32 && fgets(line, sizeof line, f); lines++) {
        char *page = strchr(line, '\t'); if (!page) continue;
        *page++ = 0; char *title = strchr(page, '\t'); if (!title) continue;
        *title++ = 0; title[strcspn(title, "\r\n")] = 0;
        char *end; long block = strtol(line, &end, 10);
        if (*end || block < 0 || block >= 32 * 1024 * 1024 ||
            !*page || *page == '@' || strlen(page) >= TIKU_PATH_MAX) continue;
        if (c->document_mode ? (*page != '/' || !tiku_reader_document_file_supported(page)) :
            (*page == '/' || strstr(page, "..") != NULL)) continue;
        help_row_t *row = &marks[count++]; memset(row, 0, sizeof *row);
        strcpy(row->page, page); row->block = (int)block;
        snprintf(row->title, sizeof row->title, "%.127s", title);
    }
    int ok = !ferror(f);
    fclose(f);
    if (ok) {
        memcpy(c->reader->marks, marks, (size_t)count * sizeof *marks);
        c->reader->nmarks = count;
    }
    return ok;
}

/** @brief Write the list to @p path through a temporary file renamed
 *         over it, so a failed save leaves the old file as it was.
 *  @return 1 when saved. */
static int reader_bookmarks_save(tiku_reader_help_window_t *c,
                                 const char *path)
{
    char temp[TIKU_PATH_MAX + 16];
    snprintf(temp, sizeof temp, "%s.XXXXXX", path);
    int fd = mkstemp(temp); if (fd < 0) return 0;
    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); unlink(temp); return 0; }
    int ok = 1;
    for (int i = 0; i < c->reader->nmarks; i++) {
        help_row_t *row = &c->reader->marks[i]; char title[128];
        snprintf(title, sizeof title, "%s", row->title);
        for (char *p = title; *p; p++) if ((unsigned char)*p < 32) *p = ' ';
        if (fprintf(f, "%d\t%s\t%s\n", row->block, row->page, title) < 0) ok = 0;
    }
    if (fflush(f) || fsync(fd)) ok = 0;
    if (fclose(f)) ok = 0;
    if (!ok || rename(temp, path)) {
        unlink(temp); return 0;
    }
    return 1;
}
/** @brief Toggle a bookmark on the block at the top of the view: added
 *         when there is none, removed when there is.  A bookmark names a
 *         block, which stays the same when the text size changes. */
void tiku_reader_bookmark(tiku_reader_help_window_t *c)
{
    struct help_reader *r = c->reader; if (!r || !c->doc) return;
    if (!c->page[0]) { tiku_reader_say(c, "Open a file before adding a bookmark."); return; }
    if (c->page[0] == '@') { tiku_reader_say(c, "Generated reports are temporary; copy the information you need."); return; }
    tiku_doc_place_t at = tiku_doc_place(c->doc, c->bar.value);
    /* Decide add/remove from what this window showed, then apply that
     * operation to the latest file: a second window with a stale list does
     * not toggle off a bookmark already added.  A window whose bookmarks
     * could not be saved keeps them to itself and leaves the file alone. */
    int remove = reader_bookmark_find(r, c->page, at.block) >= 0, lock = -1;
    const char *unsaved = r->marks_session ?
        "bookmarks in this window are not being saved" : NULL;
    char path[TIKU_PATH_MAX];
    if (!unsaved && reader_bookmark_path(c, path, sizeof path)) {
        lock = reader_bookmark_lock(c, path);
        if (lock == -2) {
            tiku_reader_say(c, "Bookmarks are busy in another window; "
                            "nothing was changed. Try again.");
            return;
        }
        if (lock < 0) {
            unsaved = "the bookmarks file cannot be written";
        } else if (!reader_bookmarks_read(c, path)) {
            close(lock);
            tiku_reader_say(c, "Could not read saved bookmarks; "
                            "nothing was changed.");
            return;
        }
    }
    int i = reader_bookmark_find(r, c->page, at.block);
    if (remove && i >= 0)
        memmove(r->marks+i, r->marks+i+1, (size_t)(--r->nmarks-i)*sizeof *r->marks);
    if (!remove && i < 0) {
        if (r->nmarks == 32) {
            if (lock >= 0) close(lock);
            tiku_reader_say(c, "Bookmark limit reached (32)."); return;
        }
        help_row_t *row = &r->marks[r->nmarks++]; memset(row, 0, sizeof *row);
        snprintf(row->page, sizeof row->page, "%s", c->page);
        row->block = at.block;
        snprintf(row->title, sizeof row->title, "%.127s",
                 tiku_doc_title(c->doc) ? tiku_doc_title(c->doc) : c->page);
    }
    if (lock >= 0) {
        if (!reader_bookmarks_save(c, path)) {
            unsaved = "saving bookmarks failed";
        }
        close(lock);
    }
    if (unsaved) {
        char said[sizeof c->status];
        r->marks_session = 1;
        snprintf(said, sizeof said, "%s for this session only: %s.",
                 remove ? "Bookmark removed" : "Bookmark added", unsaved);
        tiku_reader_say(c, said);
    } else {
        tiku_reader_say(c, remove ? "Bookmark removed." : "Bookmark added. "
                        "Use Bookmark again here to remove it.");
    }
}
