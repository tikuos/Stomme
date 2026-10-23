/* tiku_hex_model.c -- TikuHex's document: loading a file, byte changes
 * with undo and redo, find, and saves.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_hex_model.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** @brief Put @p text in m->error.  @return 0. */
static int hex_error(hex_model_t *m, const char *text)
{ snprintf(m->error, sizeof m->error, "%s", text); return 0; }

/** @brief Read @p n bytes from @p fd, retrying an interrupted read.
 *  @return 1 when all were read. */
static int hex_read(int fd, unsigned char *bytes, size_t n)
{
    while (n) {
        ssize_t got = read(fd, bytes, n);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return 0;
        bytes += got; n -= (size_t)got;
    }
    return 1;
}

void hex_free(hex_model_t *m)
{ free(m->bytes); free(m->original); memset(m, 0, sizeof *m); }

int hex_load(hex_model_t *m, const char *path)
{
    struct stat before, after;
    if (!path || !*path || strlen(path) >= sizeof m->path)
        return hex_error(m, "Invalid or overlong local filename.");
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
    if (fd < 0) return hex_error(m, "Cannot read file. Use a regular local file, not a symbolic link.");
    if (fstat(fd, &before) || !S_ISREG(before.st_mode) || before.st_size < 0 ||
        (uint64_t)before.st_size > HEX_LIMIT) {
        close(fd); return hex_error(m, "Only regular files up to 16 MiB can be opened.");
    }
    size_t n = (size_t)before.st_size;
    unsigned char *bytes = malloc(n ? n : 1), *original = malloc(n ? n : 1);
    if (!bytes || !original) {
        free(bytes); free(original); close(fd);
        return hex_error(m, "Not enough memory; the current document was kept.");
    }
    int ok = hex_read(fd, bytes, n) && !fstat(fd, &after) &&
        before.st_size == after.st_size &&
        before.st_mtim.tv_sec == after.st_mtim.tv_sec &&
        before.st_mtim.tv_nsec == after.st_mtim.tv_nsec &&
        before.st_ctim.tv_sec == after.st_ctim.tv_sec &&
        before.st_ctim.tv_nsec == after.st_ctim.tv_nsec;
    close(fd);
    if (!ok) { free(bytes); free(original); return hex_error(m, "File changed or could not be read completely. Try again."); }
    memcpy(original, bytes, n);
    char name[512]; snprintf(name, sizeof name, "%s", path);
    hex_free(m);
    m->bytes = bytes; m->original = original; m->size = n; m->identity = after;
    snprintf(m->path, sizeof m->path, "%s", name);
    return 1;
}

/** @brief Set the byte at @p at, keep the count of bytes that differ from
 *         the file's, and move the cursor there. */
static void hex_set(hex_model_t *m, size_t at, unsigned char byte)
{
    m->changed -= m->bytes[at] != m->original[at];
    m->bytes[at] = byte;
    m->changed += m->bytes[at] != m->original[at];
    m->cursor = at;
}

int hex_replace(hex_model_t *m, size_t at, unsigned char byte)
{
    if (at >= m->size || m->bytes[at] == byte) return 0;
    m->used = m->position;
    if (m->used == HEX_HISTORY) {
        memmove(m->history, m->history + 1, (HEX_HISTORY - 1) * sizeof m->history[0]);
        m->used--; m->position--;
    }
    m->history[m->used++] = (hex_change_t){at, m->bytes[at], byte};
    m->position++;
    hex_set(m, at, byte); return 1;
}

int hex_undo(hex_model_t *m, int redo)
{
    if (redo ? m->position >= m->used : !m->position) return 0;
    hex_change_t c = m->history[redo ? m->position++ : --m->position];
    hex_set(m, c.at, redo ? c.after : c.before); return 1;
}

int hex_offset(const char *text, size_t size, size_t *offset)
{
    if (!text || !*text || *text == '-' || *text == '+' || isspace((unsigned char)*text)) return 0;
    char *end; errno = 0;
    unsigned long long n = strtoull(text, &end,
        text[0] == '0' && (text[1] == 'x' || text[1] == 'X') ? 16 : 10);
    if (errno || *end || end == text || n >= size) return 0;
    *offset = (size_t)n; return 1;
}

int hex_pattern(const char *text, unsigned char *bytes, size_t *size)
{
    size_t n = 0; int high = -1;
    for (; *text; text++) {
        unsigned char ch = (unsigned char)*text;
        if (isspace(ch)) { if (high >= 0) return 0; continue; }
        int digit = ch >= '0' && ch <= '9' ? ch - '0' :
            ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 :
            ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
        if (digit < 0) return 0;
        if (high < 0) high = digit;
        else { if (n >= 64) return 0; bytes[n++] = (unsigned char)(high * 16 + digit); high = -1; }
    }
    if (high >= 0 || !n) return 0;
    *size = n; return 1;
}

int hex_find(hex_model_t *m, const unsigned char *pattern, size_t size)
{
    if (!size || size > m->size) return 0;
    for (size_t step = 1; step <= m->size; step++) {
        size_t at = (m->cursor + step) % m->size;
        if (at <= m->size - size && m->bytes[at] == pattern[0] &&
            !memcmp(m->bytes + at, pattern, size)) { m->cursor = at; return 1; }
    }
    return 0;
}

/** @brief Whether @p path is still the file that was read: one link, the
 *         same device, inode, size and times, and the same bytes. */
static int hex_unchanged(hex_model_t *m, const char *path)
{
    struct stat st;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
    if (fd < 0) return 0;
    int ok = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_nlink == 1 &&
        st.st_dev == m->identity.st_dev && st.st_ino == m->identity.st_ino &&
        st.st_size == (off_t)m->size &&
        st.st_mtim.tv_sec == m->identity.st_mtim.tv_sec &&
        st.st_mtim.tv_nsec == m->identity.st_mtim.tv_nsec &&
        st.st_ctim.tv_sec == m->identity.st_ctim.tv_sec &&
        st.st_ctim.tv_nsec == m->identity.st_ctim.tv_nsec;
    unsigned char block[8192];
    for (size_t at = 0; ok && at < m->size; ) {
        size_t n = m->size - at; if (n > sizeof block) n = sizeof block;
        ok = hex_read(fd, block, n) && !memcmp(block, m->original + at, n);
        at += n;
    }
    close(fd); return ok;
}

int hex_save(hex_model_t *m, const char *path)
{
    char temp[560]; struct stat st;
    if (!m->bytes || !path || !*path || strlen(path) >= sizeof m->path)
        return hex_error(m, "No document, or invalid destination.");
    int same = !strcmp(path, m->path);
    if (same && !hex_unchanged(m, path))
        return hex_error(m, "Original changed, disappeared, or has multiple links. Use Save As with a new name.");
    if (!same && (lstat(path, &st) == 0 || errno != ENOENT))
        return hex_error(m, "Save As needs a new filename; existing files are never replaced.");
    if (same && access(path, W_OK)) return hex_error(m, "Original is read-only. Use Save As.");
    snprintf(temp, sizeof temp, "%s.tikuhex-XXXXXX", path);
    int fd = mkstemp(temp);
    if (fd < 0) return hex_error(m, "Cannot create a temporary file in that folder.");
    int ok = 1;
    for (size_t at = 0; at < m->size; ) {
        ssize_t n = write(fd, m->bytes + at, m->size - at);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ok = 0; break; }
        at += (size_t)n;
    }
    if (same && fchmod(fd, m->identity.st_mode & 0777)) ok = 0;
    if (fsync(fd)) ok = 0;
    if (close(fd)) ok = 0;
    if (same && ok) ok = hex_unchanged(m, path);
    if (ok) ok = same ? !rename(temp, path) : !link(temp, path);
    if (!same || !ok) (void)unlink(temp);
    if (!ok) return hex_error(m, "Save failed or original changed. Your edits remain in memory.");
    if (path != m->path) snprintf(m->path, sizeof m->path, "%s", path);
    if (stat(path, &m->identity)) memset(&m->identity, 0, sizeof m->identity);
    memcpy(m->original, m->bytes, m->size); m->changed = 0; m->error[0] = 0;
    return 1;
}
