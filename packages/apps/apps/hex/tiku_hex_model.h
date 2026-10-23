/* tiku_hex_model.h -- TikuHex's document: a file's bytes edited in place
 * without changing its length, with undo, find, and saves that replace
 * the file only while it is unchanged on disk.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_HEX_MODEL_H_
#define TIKU_HEX_MODEL_H_
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
/** @brief The largest file opened, in bytes. */
#define HEX_LIMIT (16u * 1024u * 1024u)
/** @brief How many byte changes undo keeps. */
#define HEX_HISTORY 512
/** @brief One undoable change: the byte at @c at, before and after. */
typedef struct { size_t at; unsigned char before, after; } hex_change_t;
/** @brief A document: its bytes and the file's, the bytes that differ, the
 *         cursor, the undo history, its path, the last error, and the
 *         file's identity when it was read. */
typedef struct {
    unsigned char *bytes, *original;
    size_t size, changed, cursor;
    hex_change_t history[HEX_HISTORY];
    int used, position;
    char path[512], error[192];
    struct stat identity;
} hex_model_t;
/** @brief Free the document's bytes and empty the model. */
void hex_free(hex_model_t *m);
/** @brief Read regular file @p path, not a link, of at most HEX_LIMIT
 *         bytes; a file that changes while it is read is refused, and on
 *         any failure the document shown stays.
 *  @return 1, or 0 with the reason in m->error. */
int hex_load(hex_model_t *m, const char *path);
/** @brief Write the bytes to @p path through a temporary file in its
 *         folder: over the document's own file only while it is unchanged
 *         on disk since it was read, else to a name that does not exist.
 *  @return 1, or 0 with the reason in m->error and the edits kept. */
int hex_save(hex_model_t *m, const char *path);
/** @brief Replace the byte at @p at with @p byte as one undoable change;
 *         the redo history is dropped.
 *  @return 1, or 0 when @p at is out of range or the byte is the same. */
int hex_replace(hex_model_t *m, size_t at, unsigned char byte);
/** @brief Undo the last change, or with @p redo redo the next one.
 *  @return 1 when there was one. */
int hex_undo(hex_model_t *m, int redo);
/** @brief Parse an offset into a file of @p size bytes: 0x hexadecimal or
 *         unsigned decimal, below @p size.  @return 1, or 0. */
int hex_offset(const char *text, size_t size, size_t *offset);
/** @brief Parse hex byte pairs into at most 64 bytes, spaces allowed only
 *         between bytes.  @return 1, or 0 for a lone digit, a character
 *         that is not hex, or no bytes. */
int hex_pattern(const char *text, unsigned char *bytes, size_t *size);
/** @brief Move the cursor to the next match after it, wrapping round
 *         once; a match does not run past the end of the file.
 *  @return 1 when found. */
int hex_find(hex_model_t *m, const unsigned char *pattern, size_t size);
#endif
