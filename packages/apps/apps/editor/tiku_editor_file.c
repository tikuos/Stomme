/* tiku_editor_file.c -- reading a document whole, every byte kept and
 * within its size limit, and saving it atomically through a file in the
 * same directory.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_app_product.h"
#include "tiku_editor_private.h"

static int same_disk(const struct stat *a, const struct stat *b);

/** Whether @p a and @p b are the same file unchanged: device, inode, size,
 *  and modification and change times. */
static int same_disk(const struct stat *a, const struct stat *b)
{
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino &&
        a->st_size == b->st_size && a->st_mtim.tv_sec == b->st_mtim.tv_sec &&
        a->st_mtim.tv_nsec == b->st_mtim.tv_nsec &&
        a->st_ctim.tv_sec == b->st_ctim.tv_sec &&
        a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
}

/** The line break a file uses most, ties going to the one it uses first;
 *  "\n" for a file with none. */
static const char *main_break(const char *bytes, size_t n)
{
    static const char *const kinds[] = {"\n", "\r\n", "\r"};
    size_t count[3] = {0, 0, 0}, first[3] = {SIZE_MAX, SIZE_MAX, SIZE_MAX};
    int best = 0;
    for (size_t i = 0; i < n; i++) {
        int kind;
        if (bytes[i] == '\n') kind = 0;
        else if (bytes[i] != '\r') continue;
        else kind = i + 1 < n && bytes[i + 1] == '\n' ? 1 : 2;
        if (!count[kind]++) first[kind] = i;
        i += kind == 1;
    }
    for (int k = 1; k < 3; k++)
        if (count[k] > count[best] || (count[k] == count[best] && first[k] < first[best]))
            best = k;
    return kinds[best];
}

/** Load the file's bytes after any BOM as they are; the document is
 *  replaced only when the whole file is read and accepted.  @return 1
 *  when read; 0 with the reason in the note and the document kept. */
int tiku_editor_read_document(edit_state_t *st, const char *path)
{
    int fd = -1, ok = 0, bom = 0;
    struct stat info, after;
    char *bytes = NULL;
    const char *error = "Cannot read file; the current document was kept.";
    if (strlen(path) >= sizeof st->path) goto done;
    fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0 || fstat(fd, &info) || !S_ISREG(info.st_mode)) goto done;
    error = "File exceeds this editor's addressable range. Nothing was truncated.";
    if (info.st_size < 0 || (uintmax_t)info.st_size > INT_MAX - 1u) goto done;
    size_t n = (size_t)info.st_size, used = 0;
    bytes = malloc(n + 1);
    error = "Not enough memory; the current document was kept.";
    if (!bytes) goto done;
    error = "Read failed or file changed while reading. Please reopen it.";
    while (used < n) {
        ssize_t got = read(fd, bytes + used, n - used);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) goto done;
        used += (size_t)got;
    }
    if (fstat(fd, &after) || !same_disk(&info, &after)) goto done;
    bytes[n] = 0;
    error = TIKU_PRODUCT_EDITOR_BINARY_MESSAGE;
    if (memchr(bytes, 0, n)) goto done;
    bom = n >= 3 && !memcmp(bytes, "\xef\xbb\xbf", 3);
    size_t skip = bom ? 3 : 0;
    tiku_edit_status_t loaded = tiku_editdoc_load(st->doc, bytes + skip, n - skip);
    if (loaded != TIKU_EDIT_OK) {
        error = loaded == TIKU_EDIT_NO_MEMORY
            ? "Not enough memory; the current document was kept."
            : loaded == TIKU_EDIT_LIMIT
            ? "File exceeds this editor's addressable range. Nothing was truncated."
            : "Invalid UTF-8; the current document was kept.";
        goto done;
    }
    snprintf(st->newline, sizeof st->newline, "%s", main_break(bytes + skip, n - skip));
    (void)tiku_codeview_set_newline(st->view, st->newline);
    st->bom = bom;
    st->disk = info; st->disk_known = 1; st->load_failed = 0;
    /* Another file goes back to Automatic; the same one read again, as
     * Revert does, keeps the Language menu's choice. */
    if (strcmp(st->path, path)) {
        snprintf(st->path, sizeof st->path, "%s", path);
        st->language_mode = EDIT_LANG_AUTO;
    }
    if (st->language_mode == EDIT_LANG_AUTO)
        st->lang = tiku_syntax_of_path(path);
    tiku_codeview_set_language(st->view, st->lang);
    snprintf(st->note, sizeof st->note, "Opened %zu bytes", n);
    ok = 1;
done:
    if (!ok) snprintf(st->note, sizeof st->note, "%s", error);
    if (fd >= 0) close(fd);
    free(bytes);
    return ok;
}

/** Read the launch document, when one is named; when it cannot be read,
 *  the load is marked failed, so a save to its own path is refused. */
void tiku_editor_load(edit_state_t *st)
{
    if (!st->path[0]) return;
    char path[sizeof st->path];
    snprintf(path, sizeof path, "%s", st->path);
    if (!tiku_editor_read_document(st, path)) st->load_failed = 1;
}

/** Hand one run of the document's bytes to the file; nonzero stops. */
static int write_run(void *ctx, const char *bytes, size_t n)
{ return fwrite(bytes, 1, n, ctx) != n; }

/** Write the document to @p f: the BOM when the file had one, then every
 *  byte.  @return 0 when a write failed. */
int tiku_editor_write_document(edit_state_t *st, FILE *f)
{
    tiku_edit_range_t all = {0, tiku_editdoc_length(st->doc)};
    if (st->bom && fwrite("\xef\xbb\xbf", 1, 3, f) != 3) return 0;
    if (all.end > 0 && tiku_editdoc_each(st->doc, all, write_run, f) != TIKU_EDIT_OK)
        return 0;
    return !ferror(f);
}

/** Save to @p path through a temporary file in its folder, renamed over
 *  it once written and synced.  A symbolic or hard link, a read-only
 *  file, a file changed on disk since it was read, and a source that
 *  failed to load are refused.  @return 1 when saved; 0 with the reason
 *  in the note. */
int tiku_editor_save_document(edit_state_t *st, const char *path)
{
    struct stat before, after;
    char temporary[560];
    int exists, fd, ok;
    if (!path[0] || strlen(path) >= sizeof st->path) return 0;
    int original = !strcmp(path, st->path);
    if (original && st->load_failed) {
        snprintf(st->note, sizeof st->note, "Source was not loaded. Use Save As to a different file.");
        return 0;
    }
    exists = lstat(path, &before) == 0;
    if ((!exists && errno != ENOENT) ||
        (exists && (!S_ISREG(before.st_mode) || before.st_nlink != 1))) {
        snprintf(st->note, sizeof st->note, "Save refused: use a regular file, not a symbolic/hard link.");
        return 0;
    }
    if (exists && (!(before.st_mode & 0222) || access(path, W_OK))) {
        snprintf(st->note, sizeof st->note, "File is read-only. Use Save As to save a copy.");
        return 0;
    }
    if (original && st->disk_known &&
        (!exists || !same_disk(&st->disk, &before))) {
        snprintf(st->note, sizeof st->note, "File changed on disk. Reopen it or Save As another file.");
        return 0;
    }
    uint64_t state = tiku_editdoc_state(st->doc);
    snprintf(temporary, sizeof temporary, "%s.edit-XXXXXX", path);
    fd = mkstemp(temporary);
    if (fd < 0) goto failed;
    FILE *f = fdopen(fd, "wb");
    if (!f) { close(fd); unlink(temporary); goto failed; }
    ok = (!exists || !fchmod(fd, before.st_mode & 0777)) &&
         tiku_editor_write_document(st, f) && !fflush(f) && !fsync(fd);
    if (fclose(f)) ok = 0;
    int present = lstat(path, &after) == 0;
    if (present != exists || (present && !same_disk(&before, &after)) ||
        (!present && errno != ENOENT)) ok = 0;
    if (!ok || rename(temporary, path)) {
        unlink(temporary); goto failed;
    }
    if (!original) {
        snprintf(st->path, sizeof st->path, "%s", path);
        if (st->language_mode == EDIT_LANG_AUTO)
            st->lang = tiku_syntax_of_path(path);
    }
    st->disk_known = !stat(path, &st->disk);
    st->load_failed = 0;
    tiku_editdoc_saved(st->doc, state);
    snprintf(st->note, sizeof st->note, "Saved");
    return 1;
failed:
    snprintf(st->note, sizeof st->note, "Save failed; the existing file was not replaced.");
    return 0;
}

/** Save to the current document's name. */
int tiku_editor_save(edit_state_t *st)
{ return tiku_editor_save_document(st, st->path); }
