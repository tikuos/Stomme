/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_volume.c - the mount table, the partition list, mounting and
 * unmounting, and the volume rules.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_volume.h"

#include <pthread.h>
#include <errno.h>
#include <sys/mount.h>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <DiskArbitration/DiskArbitration.h>
#include <dirent.h>
#include <sys/param.h>
#include <sys/ucred.h>
/* Darwin's call is unmount(2), with a flags word that is always 0 here. */
#define umount(path) unmount((path), 0)
#endif

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

/** @brief The leaf of a mount point, which is the name a volume shows;
 *         "/" is "Boot". */
static void
name_of(const char *mount, char *out, size_t max)
{
    const char *slash = strrchr(mount, '/');

    if (strcmp(mount, "/") == 0) {
        snprintf(out, max, "Boot");
        return;
    }
    snprintf(out, max, "%.*s", (int)max - 1,
             (slash != NULL && slash[1] != '\0') ? slash + 1 : mount);
}

#ifdef __APPLE__
/*
 * On Darwin the mount table comes from getmntinfo(), already parsed: the
 * read-only and local flags and the block counts come with it.
 */
/** @brief What a mount attempt came back with, filled by the callback. */
struct mount_answer {
    int done;
    int refused;                /* the dissenter's status, 0 for none */
};

/** @brief The arbitration service's answer, on the run loop below. */
static void
mounted(DADiskRef disk, DADissenterRef refusal, void *ctx)
{
    struct mount_answer *answer = ctx;

    (void)disk;
    answer->refused = (refusal != NULL)
                          ? (int)DADissenterGetStatus(refusal) : 0;
    answer->done = 1;
}

/**
 * @brief Mount @p devname through the arbitration service.
 *
 * The system chooses the filesystem and the mount point, as it does for a
 * disk plugged in.  The answer comes back on a run loop, which is run for
 * at most five seconds (then ETIMEDOUT), so a disk that does not answer
 * does not hang the shell.
 *
 * @return 0 when it mounted, -1 with errno otherwise.
 */
static int
arbitrate_mount(const char *devname)
{
    struct mount_answer answer = { 0, 0 };
    DASessionRef session;
    DADiskRef disk;
    int spin;

    session = DASessionCreate(kCFAllocatorDefault);
    if (session == NULL) {
        errno = ENOMEM;
        return -1;
    }
    disk = DADiskCreateFromBSDName(kCFAllocatorDefault, session, devname);
    if (disk == NULL) {
        CFRelease(session);
        errno = ENOENT;
        return -1;
    }
    DASessionScheduleWithRunLoop(session, CFRunLoopGetCurrent(),
                                 kCFRunLoopDefaultMode);
    DADiskMount(disk, NULL, kDADiskMountOptionDefault, mounted, &answer);
    for (spin = 0; spin < 50 && !answer.done; spin++) {
        (void)CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, true);
    }
    DASessionUnscheduleFromRunLoop(session, CFRunLoopGetCurrent(),
                                   kCFRunLoopDefaultMode);
    CFRelease(disk);
    CFRelease(session);
    if (!answer.done) {
        errno = ETIMEDOUT;
        return -1;
    }
    if (answer.refused != 0) {
        /* A refusal is reported as EPERM: the service's own reasons do not
         * map onto errno. */
        errno = EPERM;
        return -1;
    }
    return 0;
}

/**
 * @brief Whether the device behind @p from is one a person may take away.
 *
 * Asked of the arbitration service: a medium that says it is removable is,
 * and so is a device that says it is not internal.  A device that says
 * neither, such as a second internal volume, is not removable.
 *
 * @param from  the f_mntfromname of a mount, "/dev/disk2s5".
 */
static int
device_removable(const char *from)
{
    CFDictionaryRef about;
    DASessionRef session;
    DADiskRef disk;
    CFBooleanRef says;
    int removable = 0;

    if (strncmp(from, "/dev/", 5) != 0) {
        return 0;               /* not a device: nothing to take away */
    }
    session = DASessionCreate(kCFAllocatorDefault);
    if (session == NULL) {
        return 0;
    }
    disk = DADiskCreateFromBSDName(kCFAllocatorDefault, session, from + 5);
    if (disk != NULL) {
        about = DADiskCopyDescription(disk);
        if (about != NULL) {
            says = CFDictionaryGetValue(about,
                                        kDADiskDescriptionMediaRemovableKey);
            if (says != NULL && CFBooleanGetValue(says)) {
                removable = 1;
            }
            says = CFDictionaryGetValue(about,
                                        kDADiskDescriptionDeviceInternalKey);
            if (says != NULL && !CFBooleanGetValue(says)) {
                removable = 1;
            }
            CFRelease(about);
        }
        CFRelease(disk);
    }
    CFRelease(session);
    return removable;
}

int
tiku_volumes_scan_now(tiku_volumes_t *vs)
{
    struct statfs *mounts = NULL;
    int count, i, data_rw = 0;

    if (vs == NULL) {
        return -1;
    }
    memset(vs, 0, sizeof *vs);
    count = getmntinfo(&mounts, MNT_NOWAIT);
    if (count <= 0) {
        return -1;
    }
    /*
     * The boot volume is two volumes joined under "/": a sealed read-only
     * system and a writable data volume.  "/" reports the system half's
     * read-only flag, and both halves share one st_dev, so "/" takes its
     * read-only flag from /System/Volumes/Data.
     */
    for (i = 0; i < count; i++) {
        if (strcmp(mounts[i].f_mntonname, "/System/Volumes/Data") == 0) {
            data_rw = (mounts[i].f_flags & MNT_RDONLY) == 0;
            break;
        }
    }
    for (i = 0; i < count && vs->n < TIKU_VOLUME_MAX; i++) {
        const struct statfs *m = &mounts[i];
        const char *type = m->f_fstypename;
        int is_root = strcmp(m->f_mntonname, "/") == 0;
        tiku_volume_t *v;
        struct stat st;

        /* Only volumes a person could browse: MNT_DONTBROWSE, which the
         * system sets on the boot group's halves and on the recovery and
         * preboot volumes, hides a mount, "/" aside. */
        if ((m->f_flags & MNT_DONTBROWSE) != 0 && !is_root) {
            continue;
        }
        if (strcmp(type, "devfs") == 0 || strcmp(type, "autofs") == 0 ||
            strcmp(type, "nullfs") == 0 || strcmp(type, "fdesc") == 0) {
            continue;
        }
        if (stat(m->f_mntonname, &st) != 0) {
            continue;
        }
        v = &vs->v[vs->n];
        memset(v, 0, sizeof *v);
        snprintf(v->mount, sizeof v->mount, "%s", m->f_mntonname);
        name_of(m->f_mntonname, v->name, sizeof v->name);
        v->dev = st.st_dev;
        snprintf(v->source, sizeof v->source, "%s", m->f_mntfromname);
        snprintf(v->fstype, sizeof v->fstype, "%.*s",
                 (int)(sizeof v->fstype - 1u), type);
        v->read_only = (m->f_flags & MNT_RDONLY) ? 1 : 0;
        if (is_root && data_rw) {
            v->read_only = 0;
        }
        v->shared = tiku_volume_shared_type(type) ||
                    !(m->f_flags & MNT_LOCAL);
        v->persistent = !(strcmp(type, "tmpfs") == 0 ||
                          strcmp(type, "devfs") == 0);
        v->is_boot = is_root;
        /* The boot volume is never removable; another volume is when the
         * arbitration service says so. */
        v->removable = !v->is_boot && device_removable(m->f_mntfromname);
        v->total = (uint64_t)m->f_blocks * (uint64_t)m->f_bsize;
        v->avail = (uint64_t)m->f_bavail * (uint64_t)m->f_bsize;
        vs->n++;
    }
    return vs->n;
}
#else
int
tiku_volumes_scan_now(tiku_volumes_t *vs)
{
    FILE *f;
    char line[1024];

    if (vs == NULL) {
        return -1;
    }
    memset(vs, 0, sizeof *vs);
    /* The kernel's own table, which /etc/mtab can disagree with. */
    f = fopen("/proc/self/mounts", "r");
    if (f == NULL) {
        return -1;
    }
    while (fgets(line, sizeof line, f) != NULL &&
           vs->n < TIKU_VOLUME_MAX) {
        char dev[256], mount[TIKU_PATH_MAX], type[64], opts[256];
        tiku_volume_t *v;
        struct stat st;
        struct statvfs sv;

        if (sscanf(line, "%255s %511s %63s %255s", dev, mount, type,
                   opts) != 4) {
            continue;
        }
        /* Only volumes a person could browse: the kernel's own
         * bookkeeping filesystems are not places files live. */
        if (strcmp(type, "proc") == 0 || strcmp(type, "sysfs") == 0 ||
            strcmp(type, "cgroup") == 0 || strcmp(type, "cgroup2") == 0 ||
            strcmp(type, "devpts") == 0 || strcmp(type, "securityfs") == 0 ||
            strcmp(type, "debugfs") == 0 || strcmp(type, "tracefs") == 0 ||
            strcmp(type, "bpf") == 0 || strcmp(type, "pstore") == 0 ||
            strcmp(type, "mqueue") == 0 || strcmp(type, "hugetlbfs") == 0 ||
            strcmp(type, "configfs") == 0 || strcmp(type, "fusectl") == 0 ||
            strcmp(type, "binfmt_misc") == 0 ||
            strcmp(type, "autofs") == 0 || strcmp(type, "nsfs") == 0) {
            continue;
        }
        if (stat(mount, &st) != 0) {
            continue;
        }
        v = &vs->v[vs->n];
        memset(v, 0, sizeof *v);
        snprintf(v->mount, sizeof v->mount, "%s", mount);
        name_of(mount, v->name, sizeof v->name);
        v->dev = st.st_dev;
        snprintf(v->source, sizeof v->source, "%s", dev);
        /* "ro" as a whole option, not a substring: "rootcontext=" and
         * "errors=remount-ro" both contain it. */
        {
            const char *o = opts;

            while (o != NULL && *o != '\0') {
                if (strncmp(o, "ro", 2) == 0 &&
                    (o[2] == ',' || o[2] == '\0')) {
                    v->read_only = 1;
                    break;
                }
                o = strchr(o, ',');
                if (o != NULL) { o++; }
            }
        }
        snprintf(v->fstype, sizeof v->fstype, "%.*s",
                 (int)(sizeof v->fstype - 1u), type);
        /* Served over a network: such a volume wears the share icon, and
         * the setting that puts disks on the desktop treats shares
         * separately (IV-006, PVN-040). */
        v->shared = tiku_volume_shared_type(type);
        v->persistent = !(strcmp(type, "tmpfs") == 0 ||
                          strcmp(type, "devtmpfs") == 0 ||
                          strcmp(type, "ramfs") == 0 ||
                          strcmp(type, "overlay") == 0 ||
                          strcmp(type, "squashfs") == 0 ||
                          strncmp(type, "fuse.portal", 11) == 0);
        v->is_boot = (strcmp(mount, "/") == 0);
        v->removable = !v->is_boot;
        if (statvfs(mount, &sv) == 0) {
            v->total = (uint64_t)sv.f_blocks * (uint64_t)sv.f_frsize;
            v->avail = (uint64_t)sv.f_bavail * (uint64_t)sv.f_frsize;
        }
        vs->n++;
    }
    (void)fclose(f);
    return vs->n;
}

#endif

int
tiku_volume_shared_type(const char *fstype)
{
    if (fstype == NULL) {
        return 0;
    }
    return (strcmp(fstype, "nfs") == 0 || strcmp(fstype, "nfs4") == 0 ||
            strcmp(fstype, "cifs") == 0 || strcmp(fstype, "smb3") == 0 ||
            strcmp(fstype, "smbfs") == 0 || strcmp(fstype, "9p") == 0 ||
            strncmp(fstype, "fuse.sshfs", 10) == 0);
}

unsigned
tiku_volume_flags(const tiku_volume_t *v)
{
    unsigned f = 0u;

    if (v == NULL) {
        return 0u;
    }
    if (v->read_only) { f |= TIKU_VOL_READ_ONLY; }
    if (v->is_boot)   { f |= TIKU_VOL_BOOT; }
    if (v->removable) { f |= TIKU_VOL_REMOVABLE; }
    if (v->shared)    { f |= TIKU_VOL_SHARED; }
    return f;
}

const tiku_volume_t *
tiku_volume_of(const tiku_volumes_t *vs, const char *path)
{
    const tiku_volume_t *best = NULL;
    struct stat st;
    int i;

    if (vs == NULL || path == NULL || stat(path, &st) != 0) {
        return NULL;
    }
    /* Matched by device: a bind mount or a symlink can put a path under a
     * mount point that is not its volume's. */
    for (i = 0; i < vs->n; i++) {
        if (vs->v[i].dev == st.st_dev) {
            /* The shortest mount point on that device is the volume; a
             * deeper one is a mount of the same device elsewhere. */
            if (best == NULL ||
                strlen(vs->v[i].mount) < strlen(best->mount)) {
                best = &vs->v[i];
            }
        }
    }
    return best;
}

int
tiku_volume_is_root(const tiku_volumes_t *vs, const char *path)
{
    int i;

    if (vs == NULL || path == NULL) {
        return 0;
    }
    for (i = 0; i < vs->n; i++) {
        if (strcmp(vs->v[i].mount, path) == 0) {
            return 1;
        }
    }
    return 0;
}

int
tiku_volume_may_unmount(const tiku_volumes_t *vs, const char *path,
                            char *why, size_t max)
{
    const tiku_volume_t *v;

    if (!tiku_volume_is_root(vs, path)) {
        return 0;                   /* not a mount point */
    }
    v = tiku_volume_of(vs, path);
    if (v == NULL) {
        return 0;
    }
    if (v->is_boot) {
        if (why != NULL) {
            snprintf(why, max, "You cannot unmount the boot volume "
                               "\"%.40s\".", v->name);
        }
        return 0;
    }
    return 1;
}

int
tiku_volume_may_write(const tiku_volumes_t *vs, const char *path,
                          int write, char *why, size_t max)
{
    const tiku_volume_t *v;

    if (!write) {
        return 1;                   /* reading is always allowed */
    }
    v = tiku_volume_of(vs, path);
    if (v == NULL || !v->read_only) {
        return 1;
    }
    if (why != NULL) {
        /* The message names the volume that refused. */
        snprintf(why, max, "Files cannot be moved or deleted from a "
                           "read-only volume (\"%.40s\").", v->name);
    }
    return 0;
}

int
tiku_volume_shows_icon(const tiku_volumes_t *vs,
                           const tiku_volume_t *v)
{
    size_t n;
    int i;

    if (vs == NULL || v == NULL) {
        return 0;
    }
    if (v->is_boot) {
        return 1;                   /* everything is inside it */
    }
    n = strlen(v->mount);
    for (i = 0; i < vs->n; i++) {
        const char *o = vs->v[i].mount;
        size_t on = strlen(o);

        if (&vs->v[i] == v || vs->v[i].is_boot) {
            continue;
        }
        /* Mounted under another volume, on a whole path component: no
         * icon of its own. */
        if (on < n && strncmp(v->mount, o, on) == 0 &&
            (o[on - 1u] == '/' || v->mount[on] == '/')) {
            return 0;
        }
    }
    return 1;
}

tiku_space_t
tiku_volume_space(const tiku_volume_t *v, int *used_pct)
{
    if (v == NULL) {
        if (used_pct != NULL) { *used_pct = 0; }
        return TIKU_SPACE_FREE;
    }
    return tiku_volume_space_facts(v->total, v->avail, used_pct);
}

tiku_space_t
tiku_volume_space_facts(uint64_t total, uint64_t avail, int *used_pct)
{
    struct { uint64_t total, avail; } vv;
    const void *vp = &vv;
    uint64_t used;
    int pct = 0;

    vv.total = total;
    vv.avail = avail;
    (void)vp;
    if (used_pct != NULL) {
        *used_pct = 0;
    }
    if (total == 0u) {
        /* A volume that does not report its size shows an empty bar and
         * no warning. */
        return TIKU_SPACE_FREE;
    }
    used = (total > avail) ? (total - avail) : 0u;
    pct = (int)((used * 100u + total / 2u) / total);
    if (pct > 100) { pct = 100; }
    if (used_pct != NULL) {
        *used_pct = pct;
    }
    /* The warning tests what is left, not the rounded used share: under
     * TIKU_VOLUME_WARN_PCT percent free. */
    if (avail * 100u < total * (uint64_t)TIKU_VOLUME_WARN_PCT) {
        return TIKU_SPACE_WARNING;
    }
    return TIKU_SPACE_USED;
}

/*---------------------------------------------------------------------------*/
/* Watching                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Whether @p a names the same mounted volume as @p b. */
static int
same_volume(const tiku_volume_t *a, const tiku_volume_t *b)
{
    /* Both must match: a mount point reused by another device, or a
     * device mounted somewhere else, is one volume leaving and another
     * arriving. */
    return strcmp(a->mount, b->mount) == 0 && a->dev == b->dev;
}

void
tiku_volwatch_init(tiku_volwatch_t *w)
{
    if (w == NULL) {
        return;
    }
    memset(w, 0, sizeof *w);
    if (tiku_volumes_scan(&w->was) >= 0) {
        w->primed = 1;
    }
}

int
tiku_volwatch_diff(const tiku_volumes_t *was,
                       const tiku_volumes_t *now,
                       tiku_volchange_t *out, int max)
{
    int i, j, n = 0;

    if (was == NULL || now == NULL || out == NULL || max <= 0) {
        return 0;
    }
    for (i = 0; i < was->n && n < max; i++) {
        for (j = 0; j < now->n; j++) {
            if (same_volume(&was->v[i], &now->v[j])) {
                if (strcmp(was->v[i].name, now->v[j].name) != 0) {
                    /* The volume stayed and its label did not: a rename,
                     * not a departure (MA-021). */
                    memset(&out[n], 0, sizeof out[n]);
                    out[n].v = now->v[j];
                    out[n].renamed = 1;
                    n++;
                }
                break;
            }
        }
        if (j == now->n && n < max) {
            memset(&out[n], 0, sizeof out[n]);
            out[n].v = was->v[i];
            out[n].mounted = 0;
            n++;
        }
    }
    for (j = 0; j < now->n && n < max; j++) {
        for (i = 0; i < was->n; i++) {
            if (same_volume(&was->v[i], &now->v[j])) {
                break;
            }
        }
        if (i == was->n) {
            memset(&out[n], 0, sizeof out[n]);
            out[n].v = now->v[j];
            out[n].mounted = 1;
            n++;
        }
    }
    return n;
}

int
tiku_volwatch_poll(tiku_volwatch_t *w, tiku_volchange_t *out,
                       int max)
{
    tiku_volumes_t now;
    int n;

    if (w == NULL || out == NULL || max <= 0) {
        return 0;
    }
    if (!w->primed) {
        tiku_volwatch_init(w);
        return 0;               /* the first look is the baseline */
    }
    if (tiku_volumes_scan(&now) < 0) {
        return 0;
    }
    n = tiku_volwatch_diff(&w->was, &now, out, max);
    /* The baseline moves even when @p out had no room for every change:
     * a change that did not fit is not reported again. */
    w->was = now;
    return n;
}

int
tiku_partition_carves(const char *disk, const char *part)
{
    size_t ld = strlen(disk);
    const char *tail;

    if (ld == 0u || strncmp(part, disk, ld) != 0) {
        return 0;
    }
    tail = part + ld;
    if (disk[ld - 1] >= '0' && disk[ld - 1] <= '9') {
        if (*tail != 'p' && *tail != 's') {
            return 0;
        }
        tail++;
    }
    if (*tail == '\0') {
        return 0;
    }
    for (; *tail != '\0'; tail++) {
        if (*tail < '0' || *tail > '9') {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Drop any entry another entry is a partition of, by
 *        tiku_partition_carves(): a disk carved into partitions is not
 *        itself mountable.
 *
 * @return how many are left.
 */
static int
drop_carved(tiku_partition_t *out, int n)
{
    int i, j;

    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            if (i != j && tiku_partition_carves(out[i].name, out[j].name)) {
                out[i] = out[--n];
                i--;
                break;
            }
        }
    }
    return n;
}

#ifdef __APPLE__
/** @brief A number out of a disk's description, 0 when it has none. */
static long long
disk_number(CFDictionaryRef about, CFStringRef key)
{
    CFNumberRef value = CFDictionaryGetValue(about, key);
    long long held = 0;

    if (value == NULL ||
        !CFNumberGetValue(value, kCFNumberLongLongType, &held)) {
        return 0;
    }
    return held;
}

/*
 * Darwin has no partition table to read: each disk named under /dev is
 * asked about through the arbitration service, a mach round trip and a
 * plist per disk.  The Mount menu asks on every frame a folder window
 * draws, so the answer is kept while the names under /dev and the mount
 * table stay the same.  The names are read, not stat'ed: devfs does not
 * date the directory when a disk appears.
 */
/** @brief FNV-1a over a string, folded into a running hash. */
static uint64_t
fold_text(uint64_t h, const char *text)
{
    for (; *text != '\0'; text++) {
        h = (h ^ (unsigned char)*text) * 1099511628211ull;
    }
    return h;
}

/** @brief The mount table folded: where each mount is and what it is. */
static uint64_t
fold_mounts(uint64_t h)
{
    struct statfs *table = NULL;
    int n = getmntinfo(&table, MNT_NOWAIT);
    int i;

    for (i = 0; i < n; i++) {
        h = fold_text(h, table[i].f_mntonname);
        h = fold_text(h, table[i].f_mntfromname);
    }
    return h;
}

/* How many partitions the kept answer holds. */
#define PARTITIONS_HELD 64

int
tiku_partitions_scan(tiku_partition_t *out, int max)
{
    static tiku_partition_t held[PARTITIONS_HELD];
    static int held_n = -1;     /* -1: nothing kept yet                 */
    static uint64_t held_key;
    DASessionRef session;
    struct dirent *entry;
    DIR *dev;
    uint64_t key = 1469598103934665603ull;
    int n = 0, i;

    if (out == NULL || max <= 0) {
        return 0;
    }
    dev = opendir("/dev");
    while (dev != NULL && (entry = readdir(dev)) != NULL) {
        if (strncmp(entry->d_name, "disk", 4) == 0) {
            key = fold_text(key, entry->d_name);
        }
    }
    key = fold_mounts(key);
    if (held_n >= 0 && key == held_key) {
        if (dev != NULL) {
            (void)closedir(dev);
        }
        for (i = 0; i < held_n && i < max; i++) {
            out[i] = held[i];
        }
        return i;
    }
    session = DASessionCreate(kCFAllocatorDefault);
    if (session == NULL) {
        if (dev != NULL) {
            (void)closedir(dev);
        }
        return 0;
    }
    if (dev != NULL) {
        rewinddir(dev);
    }
    while (dev != NULL && (entry = readdir(dev)) != NULL &&
           n < PARTITIONS_HELD) {
        CFDictionaryRef about;
        DADiskRef disk;

        if (strncmp(entry->d_name, "disk", 4) != 0) {
            continue;
        }
        disk = DADiskCreateFromBSDName(kCFAllocatorDefault, session,
                                       entry->d_name);
        if (disk == NULL) {
            continue;
        }
        about = DADiskCopyDescription(disk);
        if (about != NULL) {
            CFURLRef at = CFDictionaryGetValue(
                about, kDADiskDescriptionVolumePathKey);

            memset(&held[n], 0, sizeof held[n]);
            snprintf(held[n].name, sizeof held[n].name, "%s",
                     entry->d_name);
            held[n].kb = (uint64_t)(disk_number(
                about, kDADiskDescriptionMediaSizeKey) / 1024);
            if (at != NULL &&
                CFURLGetFileSystemRepresentation(
                    at, true, (UInt8 *)held[n].mount,
                    (CFIndex)sizeof held[n].mount)) {
                held[n].mounted = 1;
            }
            n++;
            CFRelease(about);
        }
        CFRelease(disk);
    }
    if (dev != NULL) {
        (void)closedir(dev);
    }
    CFRelease(session);
    held_n = drop_carved(held, n);
    held_key = key;
    for (i = 0; i < held_n && i < max; i++) {
        out[i] = held[i];
    }
    return i;
}
#else
int
tiku_partitions_scan(tiku_partition_t *out, int max)
{
    tiku_volumes_t vs;
    FILE *f;
    char line[256];
    int n = 0, have_vols, i;

    if (out == NULL || max <= 0) {
        return 0;
    }
    have_vols = tiku_volumes_scan(&vs) > 0;
    f = fopen("/proc/partitions", "r");
    if (f == NULL) {
        return 0;
    }
    while (fgets(line, sizeof line, f) != NULL && n < max) {
        unsigned major, minor;
        unsigned long long kb;
        char name[32];

        if (sscanf(line, "%u %u %llu %31s", &major, &minor, &kb,
                   name) != 4) {
            continue;
        }
        /* The kernel's own ram, loop and zram devices are not disks a
         * person mounts. */
        if (strncmp(name, "ram", 3) == 0 || strncmp(name, "loop", 4) == 0 ||
            strncmp(name, "zram", 4) == 0) {
            continue;
        }
        memset(&out[n], 0, sizeof out[n]);
        snprintf(out[n].name, sizeof out[n].name, "%s", name);
        out[n].kb = kb;
        if (have_vols) {
            char devpath[80];

            snprintf(devpath, sizeof devpath, "/dev/%s", name);
            for (i = 0; i < vs.n; i++) {
                if (strcmp(vs.v[i].source, devpath) == 0) {
                    out[n].mounted = 1;
                    snprintf(out[n].mount, sizeof out[n].mount, "%s",
                             vs.v[i].mount);
                    break;
                }
            }
        }
        n++;
    }
    (void)fclose(f);
    return drop_carved(out, n);
}
#endif


#ifndef __APPLE__
/*
 * The filesystems a partition is tried as, in turn.  mount(2) loads a
 * filesystem's module when asked for it by name, and refuses a superblock
 * that is not its own with EINVAL.
 */
static const char *const FS_TYPES[] = {
    "ext4", "btrfs", "xfs", "exfat", "ntfs3", "ntfs", "vfat", "f2fs",
    "iso9660", "udf", "hfsplus"
};

/** @brief Mount @p dev at @p at as each of FS_TYPES until one takes it.
 *  @return 0, or -1 with errno from the last attempt. */
static int
mount_any_type(const char *dev, const char *at)
{
    size_t i;

    for (i = 0; i < sizeof FS_TYPES / sizeof FS_TYPES[0]; i++) {
        if (mount(dev, at, FS_TYPES[i], 0, NULL) == 0) {
            return 0;
        }
        if (errno != EINVAL && errno != ENODEV) {
            return -1;          /* not a question of the type */
        }
    }
    return -1;
}
#endif

int
tiku_volume_mount(const char *devname, char *why, size_t max)
{
    char dev[80];

    if (why != NULL && max > 0u) {
        why[0] = '\0';
    }
    if (devname == NULL || devname[0] == '\0') {
        return -1;
    }
    snprintf(dev, sizeof dev, "/dev/%s", devname);
    /* An unprivileged caller gets EPERM from mount(2) or EACCES from
     * mkdir(2) under /mnt, which @p why words as needing the system's
     * leave. */
#ifndef __APPLE__
    {
        char at[96];
        int made, err;

        /* Each partition has its own directory under /mnt. */
        snprintf(at, sizeof at, "/mnt/%s", devname);
        made = mkdir(at, 0755) == 0;
        if (made || errno == EEXIST) {
            if (mount_any_type(dev, at) == 0) {
                return 0;
            }
            err = errno;
            if (made) {
                (void)rmdir(at);
            }
            errno = err;
        }
    }
#else
    (void)dev;
    if (arbitrate_mount(devname) == 0) {
        return 0;
    }
#endif
    if (why != NULL) {
        snprintf(why, max, (errno == EPERM || errno == EACCES)
                     ? "Mounting \"%.40s\" needs the system's leave."
                     : "\"%.40s\" could not be mounted.", devname);
    }
    return -1;
}

tiku_unmount_t
tiku_volume_unmount(const tiku_volumes_t *vs, const char *path,
                        tiku_before_unmount_fn before, void *ctx,
                        char *why, size_t max)
{
    const tiku_volume_t *v;

    if (why != NULL && max > 0u) {
        why[0] = '\0';
    }
    if (!tiku_volume_may_unmount(vs, path, why, max)) {
        return TIKU_UNMOUNT_REFUSED;
    }
    v = tiku_volume_of(vs, path);
    if (v == NULL) {
        return TIKU_UNMOUNT_REFUSED;
    }
    if (before != NULL) {
        /* Before the unmount, while the volume can still be written
         * (AW-096). */
        before(v->dev, ctx);
    }
    if (umount(v->mount) != 0) {
        if (why != NULL) {
            snprintf(why, max, (errno == EBUSY)
                         ? "Something is still using \"%.40s\"."
                         : "\"%.40s\" could not be unmounted.", v->name);
        }
        return (errno == EBUSY) ? TIKU_UNMOUNT_BUSY
                                : TIKU_UNMOUNT_FAILED;
    }
    return TIKU_UNMOUNT_OK;
}

/*
 * The table tiku_volumes_cache_set() installs and tiku_volumes_scan()
 * reads.  cache_count is -1 until one is installed, and until then
 * tiku_volumes_scan() scans.
 */
static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;
static tiku_volumes_t  cache;
static int             cache_count = -1;

void
tiku_volumes_cache_set(const tiku_volumes_t *vs, int count)
{
    pthread_mutex_lock(&cache_lock);
    if (vs != NULL) {
        cache = *vs;
    }
    cache_count = count;
    pthread_mutex_unlock(&cache_lock);
}

int
tiku_volumes_scan(tiku_volumes_t *vs)
{
    int count;

    if (vs == NULL) {
        return -1;
    }
    pthread_mutex_lock(&cache_lock);
    count = cache_count;
    if (count >= 0) {
        *vs = cache;
    }
    pthread_mutex_unlock(&cache_lock);
    if (count >= 0) {
        return count;
    }
    return tiku_volumes_scan_now(vs);
}

int
tiku_volume_unmount_prepare(const tiku_volumes_t *vs, const char *path,
                            tiku_before_unmount_fn before, void *ctx,
                            char *mount, size_t mmax, char *name,
                            size_t nmax, char *why, size_t max)
{
    const tiku_volume_t *v;

    if (why != NULL && max > 0u) {
        why[0] = '\0';
    }
    if (!tiku_volume_may_unmount(vs, path, why, max)) {
        return -1;
    }
    v = tiku_volume_of(vs, path);
    if (v == NULL) {
        return -1;
    }
    if (before != NULL) {
        before(v->dev, ctx);
    }
    snprintf(mount, mmax, "%s", v->mount);
    snprintf(name, nmax, "%s", v->name);
    return 0;
}

tiku_unmount_t
tiku_volume_umount_now(const char *mount, const char *name, char *why,
                       size_t max)
{
    if (umount(mount) != 0) {
        if (why != NULL) {
            snprintf(why, max, (errno == EBUSY)
                         ? "Something is still using \"%.40s\"."
                         : "\"%.40s\" could not be unmounted.", name);
        }
        return (errno == EBUSY) ? TIKU_UNMOUNT_BUSY
                                : TIKU_UNMOUNT_FAILED;
    }
    return TIKU_UNMOUNT_OK;
}
