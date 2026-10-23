/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fs_bytes.c - one item's copy: a file's bytes, within one store or
 * between two, and the owner, mode, times and icon position that go with
 * it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_fs_private.h"

/**
 * @brief Give @p dst the source's permissions, owner, group and times.
 *
 * Mode and times are part of what is copied (FS-031).  Ownership is
 * attempted and may fail: only root may give a file away.
 *
 * @param link  Non-zero when @p dst is a symlink, which must not be followed.
 */
void
fop_carry_over(const char *src, const char *dst, int link)
{
    struct stat st;
    struct timespec times[2];

    if ((link ? lstat(src, &st) : stat(src, &st)) != 0) {
        return;
    }
    times[0] = st.st_atim;
    times[1] = st.st_mtim;
    if (link) {
        if (lchown(dst, st.st_uid, st.st_gid) != 0) { errno = 0; }
        (void)utimensat(AT_FDCWD, dst, times, AT_SYMLINK_NOFOLLOW);
        return;
    }
    if (chown(dst, st.st_uid, st.st_gid) != 0) { errno = 0; }
    (void)chmod(dst, st.st_mode & 07777u);
    (void)utimensat(AT_FDCWD, dst, times, 0);
}

/**
 * @brief Carry an icon pose record along with a cross-folder copy.
 *
 * The copy takes the source's pose record from the state store.  An
 * explicit drop point, applied when the destination view hears of the new
 * item, still wins (FS-037).
 */
void
fop_copy_pose_info(tiku_fs_t *fs, const char *src, const char *dst)
{
    tiku_store_t *store;
    unsigned char raw[64];
    int n;

    if (fs == NULL || fs->backend == NULL || src == NULL || dst == NULL ||
        fs->backend->ops->state_store == NULL) {
        return;
    }
    store = fs->backend->ops->state_store(fs->backend);
    if (store == NULL || !tiku_store_writable(store)) {
        return;
    }
    n = tiku_state_read(store, src, TIKU_ATTR_POSE_INFO, raw,
                            sizeof raw);
    if (n > 0) {
        (void)tiku_state_write(store, dst, TIKU_ATTR_POSE_INFO, raw,
                                   (size_t)n);
    }
}

/**
 * @brief Copy one node from one store to another, through both of them.
 *
 * Between two stores only the backend ops apply.  Read takes no offset, so
 * a node larger than the 64 KB buffer is refused; and the ops cannot make a
 * directory, so a folder is refused.
 *
 * @return 0, -1 with @p err filled, or -2 when cancelled.
 */
int
fop_copy_across(tiku_fs_t *fs, tiku_backend_t *sb, const char *slocal,
            tiku_backend_t *db, const char *dlocal, const char *src,
            char *err, size_t errmax)
{
    static unsigned char across[64 * 1024];
    tiku_model_t m;
    int len;

    /* Cancel is asked once, before the one read and the one write. */
    if (fop_cancelled(fs)) {
        snprintf(err, errmax, "cancelled");
        return -2;
    }
    if (sb->ops->stat(sb, slocal, &m) != 0) {
        snprintf(err, errmax, "cannot read '%.60s'", fop_leaf_of(src));
        return -1;
    }
    if (tiku_model_is_container(&m)) {
        snprintf(err, errmax,
                 "a folder cannot be copied between stores yet: the "
                 "backend contract has no way to make one");
        return -1;
    }
    if (m.facts.size > (int64_t)sizeof across) {
        snprintf(err, errmax,
                 "'%.40s' is too big to copy between stores: they are "
                 "read whole, and this one is %lld bytes",
                 fop_leaf_of(src), (long long)m.facts.size);
        return -1;
    }
        {
        int held = fop_fs_rest(fs);

        len = sb->ops->read(sb, slocal, across, sizeof across);
        fop_fs_back(fs, held);
    }
    if (len < 0) {
        snprintf(err, errmax, "cannot read '%.60s'", fop_leaf_of(src));
        return -1;
    }
    /* A read shorter than the entry's size is refused: writing it would
     * leave a truncated file. */
    if (m.facts.size > 0 && (int64_t)len < m.facts.size) {
        snprintf(err, errmax,
                 "'%.40s' was read short, %d of %lld bytes", fop_leaf_of(src),
                 len, (long long)m.facts.size);
        return -1;
    }
    if (db->ops->write == NULL) {
        snprintf(err, errmax, "that store does not take writes");
        return -1;
    }
        {
        int held = fop_fs_rest(fs);
        int rc = db->ops->write(db, dlocal, across, (size_t)len, err,
                                errmax);

        fop_fs_back(fs, held);
        return rc;
    }
}

/**
 * @brief Copy one file's bytes within one store: through the backend on a
 *        device, with stdio for local files.
 * @return 0, -1 with @p err filled, or -2 when cancelled, the partial file
 *         removed.
 */
int
fop_copy_bytes(tiku_fs_t *fs, const char *src, const char *dst, char *err,
           size_t errmax)
{
    static unsigned char buf[64 * 1024];
    FILE *in, *out;
    size_t n;

    /* A device file goes through the backend.  Both ends are on one store
     * here (fop_copy_across() takes a crossing), so the source answers for
     * both. */
    if (fop_on_device(fs, src)) {
        int len = fs->backend->ops->read(fs->backend, src, buf, sizeof buf);
        if (len < 0) {
            snprintf(err, errmax, "cannot read '%s'", src);
            return -1;
        }
        return fs->backend->ops->write(fs->backend, dst, buf, (size_t)len,
                                       err, errmax);
    }
    {   /* Space is checked again for each file: another process may have
         * taken it since the job started (FS-016). */
        struct stat st;
        char dir[TIKU_PATH_MAX];

        uint64_t freeb;

        fop_parent_of(dst, dir, sizeof dir);
        freeb = tiku_fs_free_bytes(dir);
        /* One kilobyte of headroom per file. */
        if (freeb != 0u && stat(src, &st) == 0 &&
            (uint64_t)st.st_size + 1024u >= freeb) {
            snprintf(err, errmax, "There is not enough free space on the "
                                  "destination volume.");
            return -1;
        }
    }
    in = fopen(src, "rb");
    if (in == NULL) {
        snprintf(err, errmax, "cannot read '%s' (%s)", src, strerror(errno));
        return -1;
    }
    out = fopen(dst, "wb");
    if (out == NULL) {
        (void)fclose(in);
        snprintf(err, errmax, "cannot create '%s' (%s)", dst,
                 strerror(errno));
        return -1;
    }
    for (;;) {
        /* Cancel is asked before each chunk. */
        if (fop_cancelled(fs)) {
            (void)fclose(in);
            (void)fclose(out);
            /* The half-written destination is removed (FS-033). */
            (void)remove(dst);
            snprintf(err, errmax, "cancelled");
            return -2;
        }
                {
            int held = fop_fs_rest(fs);

            n = fread(buf, 1, sizeof buf, in);
            fop_fs_back(fs, held);
        }
        if (n == 0) {
            break;
        }
        fs->prog.bytes += (int64_t)n;
        {
            int held = fop_fs_rest(fs);
            size_t put = fwrite(buf, 1, n, out);

            fop_fs_back(fs, held);
            if (put == n) {
                continue;
            }
        }
        {
            (void)fclose(in);
            (void)fclose(out);
            snprintf(err, errmax, "write failed on '%s'", dst);
            return -1;
        }
    }
    (void)fclose(in);
    return (fclose(out) == 0) ? 0 : -1;
}
