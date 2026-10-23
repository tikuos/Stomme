/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_archive.h - archives read: ZIP, tar, gzip and tar in gzip, listed
 * and extracted.  An archive's data is read as a stream, never held
 * whole.  Extraction writes nothing outside the folder it is given and
 * follows no link on the way; each entry's size, and its CRC where the
 * format keeps one, is checked.  Not thread-safe per archive: one caller
 * at a time.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ARCHIVE_H_
#define TIKU_ARCHIVE_H_

#include <stddef.h>
#include <stdint.h>

/** @brief An open archive: its file, its format and its listed entries. */
typedef struct tiku_archive tiku_archive_t;

/** @brief What went wrong: with the archive, or with one entry of it. */
typedef enum {
    TIKU_ARCHIVE_OK = 0,
    TIKU_ARCHIVE_MISSING,           /* the file could not be opened      */
    TIKU_ARCHIVE_UNKNOWN,           /* not an archive read here          */
    TIKU_ARCHIVE_NOT_YET,           /* recognised, not read: xz, 7z ...  */
    TIKU_ARCHIVE_DAMAGED,           /* its structure does not hold       */
    TIKU_ARCHIVE_UNSUPPORTED,       /* an entry's method or kind         */
    TIKU_ARCHIVE_ENCRYPTED,         /* an entry wants a password         */
    TIKU_ARCHIVE_CHECKSUM,          /* an entry's CRC or size is wrong   */
    TIKU_ARCHIVE_UNSAFE,            /* an entry's name or link refused   */
    TIKU_ARCHIVE_WRITE,             /* the disk refused a file           */
    TIKU_ARCHIVE_REPLACED,          /* made over a file at its path      */
    TIKU_ARCHIVE_CANCELLED,
    TIKU_ARCHIVE_NOMEM,
    TIKU_ARCHIVE_TOO_LARGE          /* an entry past the size asked for  */
} tiku_archive_err_t;

/** @brief An archive's format, found from its first bytes. */
typedef enum {
    TIKU_ARCHIVE_ZIP = 1,
    TIKU_ARCHIVE_TAR,
    TIKU_ARCHIVE_TAR_GZ,            /* tar inside gzip: .tar.gz, .tgz    */
    TIKU_ARCHIVE_GZ                 /* one file, gzip-compressed         */
} tiku_archive_format_t;

/** @brief An entry's kind. */
enum { TIKU_ARC_FILE = 0, TIKU_ARC_DIR, TIKU_ARC_SYMLINK, TIKU_ARC_HARDLINK,
       TIKU_ARC_OTHER };

#define TIKU_ARC_ENCRYPTED  0x01u   /* wants a password                  */
#define TIKU_ARC_REFUSED    0x02u   /* its name will not be made on disk */
#define TIKU_ARC_UNREADABLE 0x04u   /* a method or kind not read here    */
#define TIKU_ARC_METADATA   0x08u   /* a Mac's __MACOSX/ or ._ companion */

/** @brief One entry, as listed. */
typedef struct {
    const char *path;               /* UTF-8, '/' between parts, cleaned */
    const char *link;               /* a link's target, or NULL          */
    int         type;               /* TIKU_ARC_FILE ...                 */
    unsigned    flags;              /* TIKU_ARC_ENCRYPTED ...            */
    int64_t     size;               /* bytes extracted, -1 unknown       */
    int64_t     packed;             /* bytes in the archive, -1 unknown  */
    int64_t     mtime;              /* seconds since 1970, INT64_MIN none */
    unsigned    mode;               /* rwx bits; 0 when none were given  */
    const char *method;             /* "stored", "deflate" ... for show  */
} tiku_archive_entry_t;

/** @brief How far a long read is: @p done of @p total bytes.  A nonzero
 *         return cancels it. */
typedef int (*tiku_archive_progress_t)(void *ctx, int64_t done,
                                       int64_t total);

/**
 * @brief Open and list the archive at @p path.  A ZIP is listed from its
 *        central directory.  A tar is read a header at a time, its data
 *        skipped, or decoded through when it is in gzip, telling
 *        @p progress (which may be NULL) how far.
 * @return OK with @p out set; else why not, @p out NULL.  A tar that
 *         breaks part way is opened with the entries before the break,
 *         and tiku_archive_damaged() returns 1 for it.
 */
tiku_archive_err_t tiku_archive_open(const char *path,
                                     tiku_archive_progress_t progress,
                                     void *ctx, tiku_archive_t **out);

/** @brief Open a regular ZIP with bounded file, directory and entry counts.
 *  @note Limits must be nonzero. Names must already be canonical
 *        relative paths; local headers must agree with the directory.
 *        This does not extract entries or accept self-extracting archives.
 *  @return OK with *out set, or an error with *out NULL. */
tiku_archive_err_t tiku_archive_open_package(const char *path,
    size_t file_bytes, size_t directory_bytes, size_t entries,
    tiku_archive_t **out);

/** @brief Close @p a's file and free @p a, its entries and names. */
void tiku_archive_close(tiku_archive_t *a);

/** @brief What @p a was found to be. */
tiku_archive_format_t tiku_archive_format(const tiku_archive_t *a);

/** @brief "ZIP archive", "tar archive", "gzip-compressed tar archive" or
 *         "gzip-compressed file". */
const char *tiku_archive_format_name(const tiku_archive_t *a);

/** @brief Whether listing a tar stopped at a break: the entries are
 *         those before it. */
int tiku_archive_damaged(const tiku_archive_t *a);

/** @brief How many entries were listed. */
long tiku_archive_count(const tiku_archive_t *a);

/** @brief Entry @p i, in the archive's order, or NULL past the end. */
const tiku_archive_entry_t *tiku_archive_entry(const tiku_archive_t *a,
                                               long i);

/** @brief The first entry whose path, as the listing cleaned it, is
 *         @p path; failing that, the first equal but for ASCII case, the
 *         way a package's part names compare.  @return its index, or -1. */
long tiku_archive_find(const tiku_archive_t *a, const char *path);

/**
 * @brief Read entry @p i whole into memory, at most @p max bytes of it,
 *        checking its size and, where the format keeps one, its CRC.
 * @return OK with @p out (to free(), a zero byte after its end) and
 *         @p len; else why not, @p out NULL: MISSING for no entry @p i,
 *         UNSUPPORTED for one that is not a file, TOO_LARGE past @p max.
 */
tiku_archive_err_t tiku_archive_read(tiku_archive_t *a, long i, size_t max,
                                     unsigned char **out, size_t *len);

/** @brief A phrase for @p e: "the archive is damaged" ... */
const char *tiku_archive_strerror(tiku_archive_err_t e);

/** @brief What a file not read here is, by its first bytes: "an xz-
 *         compressed file", "a 7-Zip archive" ...; NULL for nothing
 *         recognised. */
const char *tiku_archive_identify(const char *path);

/** @brief How an extraction goes. */
typedef struct {
    const unsigned char *which;     /* a byte an entry, nonzero to take; */
                                    /* NULL takes every entry            */
    const char *strip;              /* a folder taken off each path, or  */
                                    /* NULL: "src/lib" makes src/lib/a.c */
                                    /* a.c; entries outside it are left  */
    int metadata;                   /* take __MACOSX/ and ._ files too   */
    tiku_archive_progress_t progress;   /* bytes written, of the total;  */
                                        /* the file's bytes read, of its */
                                        /* size, when a size is unknown  */
    void (*report)(void *ctx, long entry, tiku_archive_err_t why);
    void *ctx;                      /* for progress and report           */
} tiku_archive_extract_t;

/**
 * @brief Extract into the folder @p dest, which exists.  Each entry not
 *        made, or made over a file already at its path, is passed to
 *        @c report with why.
 * @return OK when every entry chosen was made; CANCELLED or NOMEM when
 *         the extraction stopped, WRITE when @p dest cannot be opened;
 *         else the first trouble an entry met other than REPLACED, or
 *         DAMAGED for a tar that ends early.
 * @note A file or link already at an entry's path in @p dest is removed
 *       and replaced; a gzip's one file is given a free name, "name 2.ext"
 *       and on.
 */
tiku_archive_err_t tiku_archive_extract(tiku_archive_t *a, const char *dest,
                                        const tiku_archive_extract_t *opt);

/** @brief The folder an archive is extracted into: its file's name, the
 *         archive's suffix (.zip, .tar.gz, .tgz ...) taken off, or
 *         "Archive" when nothing is left. */
void tiku_archive_folder_name(const char *path, char *out, size_t max);

/**
 * @brief Make a new folder @p parent/@p name, or "name 2", "name 3" ...
 *        up to "name 999" when the name is taken.
 * @return 0 with its path in @p out; -1 when none could be made.
 */
int tiku_archive_new_folder(const char *parent, const char *name,
                            char *out, size_t max);

#endif /* TIKU_ARCHIVE_H_ */
