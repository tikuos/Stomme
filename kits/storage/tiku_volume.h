/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_volume.h - what a volume is, and what that means for an operation.
 *
 * A volume's mount point, device, type and capacity as the mount table
 * gives them, and the rules that depend on them: whether a volume takes a
 * write, which one the system booted from, and what a whole volume dragged
 * to the Trash means.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_VOLUME_H_
#define TIKU_VOLUME_H_

#include "tiku_model.h"

#include <stdint.h>
#include <sys/types.h>

/** @brief How many mounted volumes are tracked. */
#define TIKU_VOLUME_MAX 32

/** @brief One mounted volume. */
typedef struct {
    char     mount[TIKU_PATH_MAX];
    char     name[TIKU_NAME_MAX];
    dev_t    dev;
    int      read_only;
    int      is_boot;               /* the volume "/" lives on           */
    int      removable;             /* can be unmounted at all           */
    /* The filesystem's type; shared is decided from it
     * (tiku_volume_shared_type()). */
    char     fstype[24];
    int      shared;
    /* Backed by a device or a network, not by RAM as tmpfs is; only a
     * persistent volume gets a Desktop icon (PVN-040). */
    int      persistent;
    char     source[64];            /* the mount table's device column   */
    uint64_t total, avail;
} tiku_volume_t;

/** @brief Every volume the system has mounted. */
typedef struct tiku_volumes {
    tiku_volume_t v[TIKU_VOLUME_MAX];
    int               n;
} tiku_volumes_t;

/**
 * @brief Read the mount table, or the copy tiku_volumes_cache_set()
 *        installed.
 *
 * @return how many volumes were found, or -1.
 */
int tiku_volumes_scan(tiku_volumes_t *vs);



/** @brief The volume @p path is on, or NULL. */
const tiku_volume_t *tiku_volume_of(const tiku_volumes_t *vs,
                                            const char *path);

/** @brief Whether @p path is the mount point of a volume. */
int tiku_volume_is_root(const tiku_volumes_t *vs, const char *path);

/**
 * @brief Whether a whole volume dragged to the Trash may be unmounted.
 *
 * The boot volume never can: the system is running from it.  The gesture
 * means unmount, not delete (FS-041).
 *
 * @param why Receives the refusal, when there is one.
 * @return 1 when the unmount may proceed.
 */
int tiku_volume_may_unmount(const tiku_volumes_t *vs,
                                const char *path, char *why, size_t max);

/** @brief Why an unmount did not happen. */
typedef enum {
    TIKU_UNMOUNT_OK = 0,
    TIKU_UNMOUNT_REFUSED,    /* the rules say no (the boot volume)   */
    TIKU_UNMOUNT_BUSY,       /* something is still using it          */
    TIKU_UNMOUNT_FAILED      /* the system call failed               */
} tiku_unmount_t;

/** @brief One block-device partition, mounted or waiting to be. */
typedef struct {
    char     name[32];              /* kernel name: nvme0n1p5, sda1, ...  */
    uint64_t kb;                    /* size in 1K blocks                  */
    int      mounted;
    char     mount[TIKU_PATH_MAX];  /* where, when mounted            */
} tiku_partition_t;

/**
 * @brief Every partition the kernel offers, mounted ones marked.
 *
 * Whole disks carved into partitions are skipped, as are the kernel's
 * ram, loop and zram devices.
 *
 * @return Count written.
 */
int tiku_partitions_scan(tiku_partition_t *out, int max);

/**
 * @brief Whether @p part names a partition of the disk @p disk.
 *
 * A partition's name is its disk's name and a number, with "p" (Linux) or
 * "s" (Darwin) between them when the disk's name ends in a digit: sda1,
 * nvme0n1p2, disk2s5.  sdaa, dm-10 and md10 are not partitions of sda,
 * dm-1 and md1.
 */
int tiku_partition_carves(const char *disk, const char *part);

/**
 * @brief Try to mount @p devname (a kernel partition name).
 *
 * On Linux it is mounted at /mnt/<devname>, tried as each common
 * filesystem in turn; on macOS the arbitration service picks the
 * filesystem and the place.  Mounting needs the system's leave; a refusal
 * or failure comes back in @p why, worded for a person (CW-034).
 *
 * @return 0 on success.
 */
int tiku_volume_mount(const char *devname, char *why, size_t max);

/**
 * @brief Called before the volume goes (AW-096): the caller saves the icon
 *        positions of every window showing something on it, while the
 *        volume can still be written.
 *
 * @param dev The volume's device, by which the caller finds its windows.
 */
typedef void (*tiku_before_unmount_fn)(dev_t dev, void *ctx);

/**
 * @brief Unmount the volume at @p path (AW-096, Q-046).
 *
 * The rules are checked first, then @p before runs, and only then is the
 * system asked: once the volume has gone there is nowhere to save the
 * arrangement.
 *
 * @param before Called once, after the rules pass and before the unmount.
 * @param why    Receives the refusal or the failure, when there is one.
 * @return TIKU_UNMOUNT_OK, or why the unmount did not happen.
 */
tiku_unmount_t tiku_volume_unmount(const tiku_volumes_t *vs,
                                           const char *path,
                                           tiku_before_unmount_fn before,
                                           void *ctx, char *why, size_t max);

/**
 * @brief Whether an operation on @p path is allowed by its volume.
 *
 * @param write Non-zero when the operation changes the item (a move, a
 *              delete, a rename); zero for a read (a copy from it).
 * @param why   Receives the refusal, when there is one.
 * @return 1 when it may proceed.
 */
int tiku_volume_may_write(const tiku_volumes_t *vs, const char *path,
                              int write, char *why, size_t max);

/**
 * @brief Whether @p v should get an icon of its own (PVN-041).
 *
 * A volume mounted inside another one, other than the boot volume, gets no
 * icon of its own: it is a detail of that volume.  The boot volume always
 * gets one.
 */
int tiku_volume_shows_icon(const tiku_volumes_t *vs,
                               const tiku_volume_t *v);

/**
 * @brief Whether a filesystem type is one served over a network: NFS,
 *        SMB/CIFS, 9p or sshfs.
 */
int tiku_volume_shared_type(const char *fstype);

/** @brief The flags a model on @p v should carry (TIKU_VOL_*). */
unsigned tiku_volume_flags(const tiku_volume_t *v);

/** @brief Below this share free, the bar warns (TS-054). */
#define TIKU_VOLUME_WARN_PCT 5

/** @brief Which of the three space-bar colours a volume calls for. */
typedef enum {
    TIKU_SPACE_USED = 0,
    TIKU_SPACE_FREE,
    TIKU_SPACE_WARNING
} tiku_space_t;

/**
 * @brief How full @p v is, and whether it is low enough to warn.
 *
 * @param used_pct Receives the used share, 0..100.
 * @return the colour of the used part of the bar: TIKU_SPACE_WARNING in
 *         place of TIKU_SPACE_USED when the volume is low, and
 *         TIKU_SPACE_FREE when its size is unknown.
 */
tiku_space_t tiku_volume_space(const tiku_volume_t *v,
                                       int *used_pct);

/**
 * @brief The same question asked of the total and avail a model carries,
 *        for drawing, which has no mount table at hand.
 */
tiku_space_t tiku_volume_space_facts(uint64_t total, uint64_t avail,
                                             int *used_pct);

/*---------------------------------------------------------------------------*/
/* Watching what is mounted (PVN-008, PVN-043)                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief What a poll of the mount table found.
 *
 * The mount table is polled, not watched, and the device has no concept of
 * mounting at all, so the notice is derived by diffing a re-reading against
 * what was there before.
 */
typedef struct {
    tiku_volumes_t was;
    int                primed;
} tiku_volwatch_t;

/** @brief How many changes one poll will report. */
#define TIKU_VOLWATCH_MAX 8

/** @brief One volume that arrived or left. */
typedef struct {
    tiku_volume_t v;
    int               mounted;      /* 1 arrived, 0 left                 */
    /* Set when the volume stayed and its name changed: a label edited
     * while mounted reaches the windows as a rename (MA-021). */
    int               renamed;
} tiku_volchange_t;

/** @brief Take the current table as the baseline; reports nothing. */
void tiku_volwatch_init(tiku_volwatch_t *w);

/**
 * @brief Re-read the table and report what moved.
 *
 * Keyed on the mount point and the device together: the same directory with
 * a different device behind it is one volume leaving and another
 * arriving.
 *
 * @return how many changes were written.
 */
int tiku_volwatch_poll(tiku_volwatch_t *w,
                           tiku_volchange_t *out, int max);

/** @brief The diff the poll runs, over two tables the caller holds. */
int tiku_volwatch_diff(const tiku_volumes_t *was,
                           const tiku_volumes_t *now,
                           tiku_volchange_t *out, int max);

/**
 * @brief The scan itself: the mount table (/proc/self/mounts, or
 *        getmntinfo() on macOS) and each volume's capacity.
 *
 * @note Call it on a worker: a mount gone away on the network never
 *       answers statvfs.  The loop reads tiku_volumes_scan().
 */
int tiku_volumes_scan_now(tiku_volumes_t *vs);

/**
 * @brief Install what a scan found as what tiku_volumes_scan() answers
 *        from now on.
 *
 * A mount gone away on the network never answers statvfs, so a shell with
 * workers scans on a worker and installs the result here.  Until the first
 * call, tiku_volumes_scan() scans directly.
 */
void tiku_volumes_cache_set(const tiku_volumes_t *vs, int count);

/**
 * @brief The half of an unmount that belongs on the loop: the refusal,
 *        the saving, and the names.  @return 0 to go on, with @p mount
 *        and @p name filled for tiku_volume_umount_now().
 *
 * An unmount in two halves is for a shell that will not wait on umount(2):
 * the asking and the saving on the loop, the call itself on a worker.
 */
int tiku_volume_unmount_prepare(const tiku_volumes_t *vs, const char *path,
                                tiku_before_unmount_fn before, void *ctx,
                                char *mount, size_t mmax, char *name,
                                size_t nmax, char *why, size_t max);

/** @brief umount(2) itself, which flushes what is dirty.
 *  @note Call it on a worker. */
tiku_unmount_t tiku_volume_umount_now(const char *mount, const char *name,
                                      char *why, size_t max);

#endif /* TIKU_VOLUME_H_ */
