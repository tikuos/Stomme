/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_archive_private.h - what the archive readers share: streams that
 * stack (a file, gzip over it, DEFLATE over a ZIP entry), an archive's
 * entries and where each one's data is, stored names cleaned into paths,
 * and the ZIP and tar readers' entry points.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ARCHIVE_PRIVATE_H_
#define TIKU_ARCHIVE_PRIVATE_H_

#include "tiku_archive.h"

#define ARC_NAME_MAX   4096         /* a name as stored, the longest read */
#define ARC_PATH_MAX   1024         /* a path made on disk                */
#define ARC_PART_MAX   255          /* one part of it                     */
#define ARC_LINK_MAX   4096         /* a link's target                    */

/** @brief Bytes read in order from something below. */
typedef struct arc_stream arc_stream_t;
struct arc_stream {
    /** Up to @p max bytes: how many, 0 at the end, -1 with @c err set. */
    long (*read)(arc_stream_t *s, unsigned char *buf, long max);
    /** Past @p n bytes without handing them out; when NULL, arc_skip()
     *  reads and drops them. */
    int  (*skip)(arc_stream_t *s, int64_t n);
    /** Free the stream and the streams it owns. */
    void (*close)(arc_stream_t *s);
    tiku_archive_err_t err;         /**< why read gave -1                */
    int64_t *consumed;              /**< the file's bytes read, for progress */
};

/** @brief Up to @p max bytes.  @return how many, 0, or -1. */
long arc_read(arc_stream_t *s, unsigned char *buf, long max);
/** @brief Exactly @p n bytes.  @return 0, or -1 when fewer came. */
int arc_read_all(arc_stream_t *s, unsigned char *buf, long n);
/** @brief Past @p n bytes.  @return 0, or -1 when fewer were there. */
int arc_skip(arc_stream_t *s, int64_t n);
/** @brief Free @p s and the streams it owns; NULL is ignored. */
void arc_close(arc_stream_t *s);

/** @brief The bytes of @p fd from @p start, @p len of them (-1: to the
 *         end); @p consumed counts them as they are read, may be NULL. */
arc_stream_t *arc_file_stream(int fd, int64_t start, int64_t len,
                              int64_t *consumed);
/** @brief DEFLATE (Deflate64 when asked) over @p under, which it owns. */
arc_stream_t *arc_inflate_stream(arc_stream_t *under, int deflate64);
/** @brief gzip over @p under, which it owns: every member, each checked
 *         against its CRC and size.  @p name, when not NULL, is given the
 *         first member's stored name ("" for none). */
arc_stream_t *arc_gzip_stream(arc_stream_t *under, char *name, size_t max);

/** @brief One entry, and where its data is. */
typedef struct {
    tiku_archive_entry_t pub;       /**< what the listing shows          */
    int64_t  offset;                /**< ZIP: data start; tar: entry no. */
    uint32_t crc;                   /**< ZIP's, checked when has_crc     */
    int      method;                /**< ZIP's method number             */
    unsigned zip_flags;             /**< ZIP general-purpose header flags */
    int      has_crc;               /**< whether crc is to be checked    */
} arc_entry_t;

/** @brief An open archive. */
struct tiku_archive {
    size_t directory_limit;        /**< zero for unrestricted ZIP listing */
    size_t entry_limit;            /**< zero for unrestricted ZIP listing */
    int strict_zip;               /**< require canonical package headers */
    tiku_archive_format_t format;   /**< found by its bytes              */
    int          fd;                /**< open until tiku_archive_close() */
    int64_t      file_size;         /**< its size when opened            */
    char        *path;              /**< as it was opened                */
    arc_entry_t *entry;             /**< listed, in the archive's order  */
    long         count, cap;        /**< entries listed, and room for    */
    int          damaged;           /**< listing stopped at a break      */
    int64_t      zip_base;          /**< where the ZIP's offsets count from */
    int64_t      zip_cd;            /**< its central directory's start   */
    int64_t     *consumed;          /**< an extraction's bytes read      */
};

/** @brief Room for one more entry, cleared, its sizes and time unknown.
 *  @return it, or NULL. */
arc_entry_t *arc_add(tiku_archive_t *a);

/**
 * @brief @p n bytes of a stored name made a path: separators '/', a
 *        leading '/' or drive taken off, "." parts dropped.  Refused --
 *        TIKU_ARC_REFUSED in @p flags, the name kept printable -- when a
 *        part is "..", holds a control character or is longer than
 *        ARC_PART_MAX, or the path is longer than ARC_PATH_MAX.
 *        @p backslash: '\' separates too (names from DOS and Windows).
 * @return the path, malloc'd; NULL out of memory.  @p dir is set when it
 *         ended in a separator.
 */
char *arc_clean_name(const char *raw, size_t n, int backslash,
                     unsigned *flags, int *dir);

/** @brief Whether @p path is a Mac's metadata: under __MACOSX/, or a
 *         "._" companion file. */
int arc_is_metadata(const char *path);

/** @brief @p s duplicated.  @return it, or NULL. */
char *arc_strdup(const char *s);

/** @brief @p n bytes of a name as UTF-8: kept when they already are, else
 *         read as code page 437 (@p cp) or Latin-1.  @return it, malloc'd. */
char *arc_utf8(const char *raw, size_t n, int cp);

/** @brief Whether @p n spans, @c span[2i] to @c span[2i+1], overlap; the
 *         array is sorted in place.  @return 1 when two do. */
int arc_spans_overlap(int64_t *span, long n);

/** @brief Whether a 512-byte block is a tar header: its checksum holds. */
int arc_tar_header_ok(const unsigned char *block);

/**
 * @brief List a ZIP from its central directory, ZIP64's included, then
 *        find each entry's data from its local header and read the links'
 *        targets.
 * @return OK; NOMEM when the directory cannot be held; else DAMAGED when
 *         the directory does not hold, a local header is not where it
 *         says, or entries' data overlap or run into the directory.
 */
tiku_archive_err_t arc_zip_list(tiku_archive_t *a);
/** @brief The symbolic links' targets, read from their data; a link whose
 *         data cannot be read or is longer than ARC_LINK_MAX keeps none.
 *  @return OK. */
tiku_archive_err_t arc_zip_links(tiku_archive_t *a);
/**
 * @brief List a tar, or a tar in gzip, a header at a time, telling
 *        @p progress (which may be NULL) the file's bytes read.  The
 *        archive's own "." folder is left out.
 * @return OK, NOMEM or CANCELLED.  A break marks @p a damaged and keeps
 *         what came before, or is DAMAGED when nothing did.
 */
tiku_archive_err_t arc_tar_list(tiku_archive_t *a,
                                tiku_archive_progress_t progress, void *ctx);

/** @brief A ZIP entry's data, decoded.  @return it, or NULL with @p why
 *         set: ENCRYPTED, UNSUPPORTED for a method other than stored,
 *         deflate and deflate64, or NOMEM. */
arc_stream_t *arc_zip_data(tiku_archive_t *a, const arc_entry_t *e,
                           tiku_archive_err_t *why);

/** @brief The whole archive as tar: the file, or gzip over it.
 *  @p consumed counts the file's bytes read.  @return it, or NULL. */
arc_stream_t *arc_tar_stream(tiku_archive_t *a, int64_t *consumed);

/** @brief A tar walked a header at a time. */
typedef struct {
    arc_stream_t *s;                /**< the tar's bytes, in order       */
    int64_t       left;             /**< the current entry's bytes unread */
    int64_t       pad;              /**< then padding to 512             */
    long          index;            /**< entries read so far             */
} arc_tar_t;

/**
 * @brief The next listed entry's header, into @p e (its path and link
 *        malloc'd, the caller's).  Its data follows, @c left bytes of it.
 * @return 1 for an entry, 0 at the archive's end, -1 for damage.
 */
int arc_tar_next(arc_tar_t *t, arc_entry_t *e);
/** @brief Up to @p max of the current entry's bytes.  @return how many,
 *         0 at its end, or -1 when the tar's bytes fail. */
long arc_tar_read(arc_tar_t *t, unsigned char *buf, long max);

#endif /* TIKU_ARCHIVE_PRIVATE_H_ */
