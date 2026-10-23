/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_thumb.h - thumbnails: when to make one, where it lives, and the
 * two queues that make them one job at a time.
 *
 * A stored thumbnail is trusted only while its source file is unchanged.
 * Big files wait on a queue of their own, taken only when the small one
 * is empty, and the same file is not worked twice at once.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_THUMB_H_
#define TIKU_THUMB_H_

#include "tiku_image.h"
#include "tiku_model.h"
#include "tiku_state.h"



#include <stdint.h>

/** @brief The edge of a stored thumbnail, in pixels (Tracker's B_XXL_ICON). */
#define TIKU_THUMB_SIZE 128

/**
 * @brief A file of this many bytes or more goes on the big queue, which
 *        is taken only when the small queue is empty (Tracker's 128 KB).
 */
#define TIKU_THUMB_BIG 131072

/** @brief How many jobs may be waiting on each queue. */
#define TIKU_THUMB_QUEUE 32

/** @brief The attribute name for a thumbnail's pixels.  This kit writes
 *         nothing under it: the pixels are in a cache file under
 *         $HOME/.cache/tiku-tracker. */
#define TIKU_ATTR_THUMB      "_trk/thumb"
/** @brief The attribute that holds a thumbnail's freshness stamp. */
#define TIKU_ATTR_THUMB_TIME "_trk/thumbtime"
/** @brief The attribute that holds the image's own width and height. */
#define TIKU_ATTR_THUMB_DIM  "_trk/thumbdim"

/** @brief One queued piece of work. */
typedef struct {
    char    path[TIKU_PATH_MAX];
    int     size;                   /* the icon size asked for          */
    int64_t bytes;                  /* the file's size, which picks the
                                     * queue                            */
} tiku_thumb_job_t;

/**
 * @brief A file's identity, size and modification time to the nanosecond,
 *        read before decoding and stored as the thumbnail's stamp.
 */
typedef struct {
    uint64_t device, inode, bytes;
    int64_t mtime, mtime_ns;
} tiku_thumb_stamp_t;

/** @brief The two queues and what is in flight. */
typedef struct {
    tiku_thumb_job_t small[TIKU_THUMB_QUEUE];
    int                  nsmall;
    tiku_thumb_job_t big[TIKU_THUMB_QUEUE];
    int                  nbig;
    char                 active[TIKU_PATH_MAX];  /* "" when idle    */
    tiku_thumb_stamp_t    source;
    int                  source_valid;
    int                  made;      /* thumbnails produced this session */
    int                  enabled;   /* the "Generate image thumbnails"
                                     * setting; on by default           */
} tiku_thumb_q_t;

/** @brief Empty both queues, with thumbnails switched on. */
void tiku_thumb_init(tiku_thumb_q_t *q);

/** @brief Whether @p path gets a thumbnail: thumbnails are switched on
 *         and the file is an image. */
int tiku_thumb_wanted(const tiku_thumb_q_t *q, const char *path);

/**
 * @brief Whether the stored thumbnail may still be trusted.
 *
 * The stamp tiku_thumb_landed() recorded holds the modification time of
 * the file as it was decoded; the thumbnail is fresh while that time is
 * not before @p mtime, the file's modification time as the caller's model
 * has it.  Reads the stamp only, not the file.
 */
int tiku_thumb_fresh(tiku_store_t *store, const char *path,
                         int64_t mtime);

/**
 * @brief Queue @p path on the small or the big queue by @p bytes.
 *
 * @return 1 when it was queued, 0 when it was already in flight or queued
 *         (the same file is not worked twice at once), -1 when thumbnails
 *         are switched off or the queue is full.
 */
int tiku_thumb_queue(tiku_thumb_q_t *q, const char *path,
                         int64_t bytes, int size);

/**
 * @brief Do at most one queued job on the calling thread:
 *        tiku_thumb_take(), tiku_thumb_make() and tiku_thumb_landed() in
 *        a row.
 *
 * @return 1 when a job ran, whether or not it made a thumbnail.
 */
int tiku_thumb_step(tiku_thumb_q_t *q, tiku_store_t *store);

/**
 * @brief Take the next job, small files first; its path becomes the
 *        active one until tiku_thumb_landed().
 *
 * @return 1 with @p path set, 0 when a job is active or none waits.
 */
int  tiku_thumb_take(tiku_thumb_q_t *q, char *path, size_t max);
/**
 * @brief Decode @p path and fit it into a TIKU_THUMB_SIZE square.
 *
 * Touches no queue or store, so it may run on any thread.  @p w and @p h
 * get the image's own size.
 *
 * @return 0, or -1 when the image cannot be read or scaled.
 */
int  tiku_thumb_make(const char *path, int *w, int *h, tiku_image_t *thumb);
/**
 * @brief Finish the active job: store @p thumb's pixels in the cache and
 *        the stamp tiku_thumb_take() read and the image's size as
 *        attributes, count it, free it, and clear the active job.
 *
 * Nothing is stored when the file's identity, size or modification time
 * differs from what tiku_thumb_take() read, or when the cache write
 * fails.  A NULL or empty @p thumb, a failed make, only clears the
 * active job.
 *
 * @note Call on the thread that owns @p q and @p store.
 */
void tiku_thumb_landed(tiku_thumb_q_t *q, tiku_store_t *store,
                       const char *path, int w, int h, tiku_image_t *thumb);

/**
 * @brief Read a stored thumbnail back into @p out, TIKU_THUMB_SIZE
 *        square.
 *
 * @return 0, or -1 when no whole thumbnail is stored or memory is short.
 * @note The caller frees @p out with tiku_image_free().
 */
int tiku_thumb_load(tiku_store_t *store, const char *path,
                        tiku_image_t *out);

#endif /* TIKU_THUMB_H_ */
