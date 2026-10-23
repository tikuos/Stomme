/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_config_outbox.c - the configuration request outbox.
 *
 * A record, <uid>.pending, holds "TIKU-CONFIG-OUTBOX-1", the board's UID,
 * "enroll" or "write", the path, the request and a CRC-32 line.  It is
 * written under a temporary name, synced and linked into place, never
 * rewritten, and removed once the device's receipt confirms it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_config_outbox.h"
#include "tiku_sys.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef TIKU_CONFIG_OUTBOX_TEST_HOOKS
extern int tiku_config_outbox_fail(const char *step);
#define OB_FAIL(step) tiku_config_outbox_fail(step)
#else
#define OB_FAIL(step) 0
#endif

/** @brief Set @p p's state and summary.  @return -1 for ERROR, else 0. */
static int ob_result(tiku_config_pending_t *p, int state, const char *message)
{
    p->state = state;
    snprintf(p->summary, sizeof p->summary, "%s", message);
    return state == TIKU_CONFIG_PENDING_ERROR ? -1 : 0;
}
/** @brief Whether @p s is lower-case hex of even length from @p min to
 *         @p max, not all zeros. */
static int ob_hex(const char *s, size_t min, size_t max)
{
    size_t n = strlen(s);
    unsigned any = 0;
    if (n < min || n > max || (n & 1)) return 0;
    for (size_t i = 0; i < n; i++) {
        if (!((s[i] >= '0' && s[i] <= '9') ||
              (s[i] >= 'a' && s[i] <= 'f'))) return 0;
        any |= s[i] != '0';
    }
    return any != 0;
}
/** @brief The CRC-32 (reflected, polynomial 0xedb88320) of @p n bytes. */
static uint32_t ob_crc(const char *s, size_t n)
{
    uint32_t crc = UINT32_MAX;
    while (n--) {
        crc ^= (unsigned char)*s++;
        for (unsigned i = 0; i < 8; i++)
            crc = (crc >> 1) ^ ((0u - (crc & 1u)) & 0xedb88320u);
    }
    return ~crc;
}
/** @brief Read @p path into @p out as a string.  @return Its length, or
 *         -1. */
static int ob_read(tiku_backend_t *b, const char *path, char *out, size_t max)
{
    int n;
    if (!b || !b->ops || !b->ops->read) return -1;
    n = b->ops->read(b, path, out, max);
    if (n < 0 || (size_t)n >= max || memchr(out, 0, (size_t)n)) return -1;
    out[n] = 0;
    return n;
}
/** @brief The board's hardware UID from /sys/device/uid, 8 to 32 hex
 *         digits.  @return 0, or -1 when it is absent, malformed or not the
 *         UID the backend's key names. */
static int ob_uid(tiku_backend_t *b, char uid[33])
{
    char raw[64];
    size_t n;
    if (ob_read(b, "/sys/device/uid", raw, sizeof raw) <= 0) return -1;
    n = strcspn(raw, "\r\n");
    if (raw[n] && strcmp(raw+n, "\n") && strcmp(raw+n, "\r\n")) return -1;
    raw[n] = 0;
    if (!ob_hex(raw, 8, 32)) return -1;
    if (!strncmp(b->devid, "uid-", 4) && strcmp(b->devid+4, raw)) return -1;
    memcpy(uid, raw, n+1);
    return 0;
}
int tiku_config_outbox_dir(char *out, size_t max)
{
    const char *override = getenv("TIKU_CONFIG_OUTBOX"), *home = getenv("HOME");
    int n;
    if (override && *override) n = snprintf(out, max, "%s", override);
    else if (home && *home)
        n = snprintf(out, max, "%s/.config/tracker/config-requests", home);
    else return -1;
    if (n <= 1 || (size_t)n >= max || out[0] != '/') return -1;
    /* Trailing slashes are dropped here: ob_directory() refuses them. */
    while (n > 1 && out[n-1] == '/') out[--n] = 0;
    return n > 1 ? 0 : -1;
}
/** @brief fsync() the directory that holds @p path.  @return 0, or -1. */
static int ob_parent_sync(const char *path)
{
    char parent[1024], *slash;
    int fd, rc;
    if (snprintf(parent, sizeof parent, "%s", path) >= (int)sizeof parent) return -1;
    slash = strrchr(parent, '/');
    if (!slash) return -1;
    if (slash == parent) slash[1] = 0; else *slash = 0;
    fd = open(parent, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return -1;
    rc = fsync(fd); close(fd); return rc;
}
/**
 * @brief Open the outbox directory, which must be this user's and closed to
 *        others; with @p create, make it (syncing each new level) and take
 *        its lock.
 * @return The descriptor, or -1 with errno set: EACCES for a directory
 *         that is not private, EBUSY when another process holds it.
 */
static int ob_directory(const char *dir, int create)
{
    char path[1024];
    struct stat st;
    int fd;
    size_t n;
    if (!dir || (n = strlen(dir)) < 2 || n >= sizeof path ||
        dir[0] != '/' || dir[n-1] == '/') { errno = EINVAL; return -1; }
    if (create) {
        memcpy(path, dir, n+1);
        for (size_t i = 1; i <= n; i++) {
            if (path[i] && path[i] != '/') continue;
            char saved = path[i]; path[i] = 0;
            if (mkdir(path, 0700) == 0) {
                if (ob_parent_sync(path)) return -1;
            } else if (errno != EEXIST) return -1;
            path[i] = saved;
        }
    }
    fd = open(dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) return -1;
    if (fstat(fd, &st) || st.st_uid != getuid() || (st.st_mode & 077)) {
        close(fd); errno = EACCES; return -1;
    }
    if (create && flock(fd, LOCK_EX | LOCK_NB)) {
        close(fd); errno = EBUSY; return -1;
    }
    return fd;
}
/** @brief The record's file name for @p uid: "<uid>.pending". */
static void ob_filename(const char *uid, char name[48])
{ snprintf(name, 48, "%s.pending", uid); }

/**
 * @brief Whether a record is well formed: an enrollment of 32 hex digits to
 *        /sys/config/provision, or a request that decodes for the device
 *        name (at most 31 bytes) or the CPU target frequency (a decimal,
 *        no leading zero, up to UINT32_MAX).
 */
static int ob_valid(const tiku_config_pending_t *p)
{
    tiku_config_snapshot_t s;
    char token[33], value[33];
    if (!ob_hex(p->uid, 8, 32)) return 0;
    if (p->enrollment)
        return !strcmp(p->path, "/sys/config/provision") &&
               ob_hex(p->request, 32, 32);
    if (tiku_config_decode_request(p->request, &s, token, value)) return 0;
    if (!strcmp(p->path, "/sys/device/name")) return strlen(value) <= 31;
    if (strcmp(p->path, "/sys/cpu/freq_target")) return 0;
    /* The firmware normalises a decimal clock target, so only the canonical
     * form is taken: a receipt's value then compares exactly. */
    if (value[0] < '1' || value[0] > '9' || strlen(value) > 10) return 0;
    for (size_t i = 1; value[i]; i++) if (value[i] < '0' || value[i] > '9') return 0;
    return strtoull(value, NULL, 10) <= UINT32_MAX;
}
/** @brief Encode a record, its CRC-32 line last.  @return Its length, or
 *         -1. */
static int ob_encode(const tiku_config_pending_t *p, char out[384])
{
    int n = snprintf(out, 384, "TIKU-CONFIG-OUTBOX-1\n%s\n%s\n%s\n%s\n",
                     p->uid, p->enrollment ? "enroll" : "write", p->path, p->request);
    if (n < 0 || n+9 >= 384) return -1;
    snprintf(out+n, 384-(size_t)n, "%08lx\n", (unsigned long)ob_crc(out, (size_t)n));
    return n+9;
}
/** @brief Load the record kept for @p uid.  @return 1 with @p out filled,
 *         0 when there is none, -1 for one that is unsafe, corrupt or
 *         unreadable. */
static int ob_load(int dir, const char *uid, tiku_config_pending_t *out)
{
    char name[48], buf[384], expected[10], *lines[5], *next;
    struct stat st;
    size_t used = 0;
    ssize_t n;
    int fd;
    tiku_config_pending_t p = {0};
    ob_filename(uid, name);
    fd = openat(dir, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) ||
        st.st_uid != getuid() || (st.st_mode & 077) ||
        st.st_size <= 9 || st.st_size >= (off_t)sizeof buf) { close(fd); return -1; }
    do {
        n = read(fd, buf+used, sizeof buf-1-used);
        if (n > 0) used += (size_t)n;
    } while ((n > 0 && used < sizeof buf-1) || (n < 0 && errno == EINTR));
    close(fd);
    if (n < 0 || used != (size_t)st.st_size || memchr(buf, 0, used)) return -1;
    buf[used] = 0;
    snprintf(expected, sizeof expected, "%08lx\n",
             (unsigned long)ob_crc(buf, used-9));
    if (strcmp(buf+used-9, expected)) return -1;
    buf[used-9] = 0; next = buf;
    for (int i = 0; i < 5; i++) {
        lines[i] = next; next = strchr(next, '\n');
        if (!next) return -1;
        *next++ = 0;
    }
    if (*next || strcmp(lines[0], "TIKU-CONFIG-OUTBOX-1") ||
        strcmp(lines[1], uid) ||
        (strcmp(lines[2], "enroll") && strcmp(lines[2], "write")) ||
        strlen(lines[3]) >= sizeof p.path || strlen(lines[4]) >= sizeof p.request)
        return -1;
    snprintf(p.uid, sizeof p.uid, "%s", uid);
    p.enrollment = !strcmp(lines[2], "enroll");
    snprintf(p.path, sizeof p.path, "%s", lines[3]);
    snprintf(p.request, sizeof p.request, "%s", lines[4]);
    if (!ob_valid(&p)) return -1;
    *out = p;
    return 1;
}
/** @brief Whether two records hold the same request. */
static int ob_same(const tiku_config_pending_t *a, const tiku_config_pending_t *b)
{
    return a && b && a->enrollment == b->enrollment &&
        !strcmp(a->uid, b->uid) && !strcmp(a->path, b->path) &&
        !strcmp(a->request, b->request);
}
/** @brief Write a record under a temporary name, sync it, link it into
 *         place and sync the directory.  @return 0, or -1. */
static int ob_store(int dir, const tiku_config_pending_t *p)
{
    char buf[384], name[48], temp[48], token[33];
    uint8_t random[16];
    int fd, rc = -1, len = ob_encode(p, buf);
    size_t pos = 0;
    if (len < 0 || tiku_config_token(random)) return -1;
    for (int i = 0; i < 16; i++) snprintf(token+2*i, 3, "%02x", random[i]);
    snprintf(temp, sizeof temp, ".request-%s", token);
    ob_filename(p->uid, name);
    fd = openat(dir, temp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) return -1;
    while (pos < (size_t)len) {
        ssize_t n = write(fd, buf+pos, (size_t)len-pos);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) goto done;
        pos += (size_t)n;
    }
    if (OB_FAIL("file-sync") || tiku_sys_sync_file(fd) || OB_FAIL("publish") ||
        linkat(dir, temp, dir, name, 0) || OB_FAIL("directory-sync") || fsync(dir) ||
        tiku_sys_sync_file(fd))
        goto done;
    rc = 0;
done:
    close(fd);
    (void)unlinkat(dir, temp, 0);
    return rc;
}
/** @brief Remove the kept record, synced, when it still matches @p p.
 *  @return 0, or -1. */
static int ob_remove(int dir, const tiku_config_pending_t *p)
{
    tiku_config_pending_t current;
    char name[48];
    int fd, rc;
    if (ob_load(dir, p->uid, &current) != 1 || !ob_same(p, &current) ||
        OB_FAIL("remove")) return -1;
    ob_filename(p->uid, name);
    fd = openat(dir, name, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return -1;
    rc = unlinkat(dir, name, 0) || fsync(dir) || tiku_sys_sync_file(fd) ? -1 : 0;
    close(fd); return rc;
}
/** @brief Sync the kept record and its directory again, when it still
 *         matches @p p.  @return 0, or -1. */
static int ob_durable(int dir, const tiku_config_pending_t *p)
{
    tiku_config_pending_t current;
    char name[48];
    int fd, rc;
    if (ob_load(dir, p->uid, &current) != 1 || !ob_same(p, &current)) return -1;
    ob_filename(p->uid, name);
    fd = openat(dir, name, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return -1;
    /* A record whose publish failed at the directory sync is visible on
     * this boot but may not survive a crash, so it is synced again before
     * a send. */
    rc = OB_FAIL("retry-sync") || fsync(dir) || tiku_sys_sync_file(fd) ? -1 : 0;
    close(fd); return rc;
}
/**
 * @brief Set @p p's state from the device: an enrollment by the store's
 *        incarnation, a write by the receipts in /sys/config/history.
 * @return -1 for ERROR, else 0.
 */
static int ob_resolve(tiku_backend_t *b, tiku_config_pending_t *p)
{
    tiku_config_snapshot_t probe, wanted, current;
    char token[33], value[33], history[768], *line;
    int match = 0, conflict = 0, count = 0;
    if (tiku_config_probe(b, &probe) < 0)
        return ob_result(p, TIKU_CONFIG_PENDING_ERROR, "Recovery state unavailable. Local request retained.");
    if (p->enrollment) {
        if (probe.mode == 1 && !strcmp(probe.incarnation, p->request))
            return ob_result(p, TIKU_CONFIG_PENDING_CONFIRMED, "Device confirms recovery enrollment. Dismiss the local record.");
        return ob_result(p, TIKU_CONFIG_PENDING_UNKNOWN,
            "Enrollment is unconfirmed. Forget this record before a new enrollment; the old token will not be replayed.");
    }
    if (tiku_config_decode_request(p->request, &wanted, token, value))
        return ob_result(p, TIKU_CONFIG_PENDING_ERROR, "Local request is invalid. It has not been sent.");
    if (probe.mode != 1 || strcmp(probe.incarnation, wanted.incarnation))
        return ob_result(p, TIKU_CONFIG_PENDING_OBSOLETE, "Device recovery identity changed. This old request cannot be retried.");
    if (tiku_config_read(b, p->path, &current) != 1 ||
        ob_read(b, "/sys/config/history", history, sizeof history) < 0 ||
        tiku_config_probe(b, &probe) != 1 ||
        strcmp(probe.incarnation, wanted.incarnation) ||
        strcmp(current.incarnation, wanted.incarnation))
        return ob_result(p, TIKU_CONFIG_PENDING_ERROR, "Could not check device receipts. Local request retained.");
    line = history;
    while (*line) {
        char wire[160], receipt_token[33];
        tiku_config_snapshot_t receipt;
        char *end = strchr(line, '\n');
        /* The session trims the reply's trailing newline, so the last
         * line ends at the terminator. */
        if (++count > 4) goto malformed;
        if (end) *end = 0;
        size_t len = strlen(line);
        if (len && line[len-1] == '\r') line[--len] = 0;
        if (len < 46 || (line[0] != '1' && line[0] != '2') ||
            line[1] != ':' || line[10] != ':' || line[43] != ':') goto malformed;
        memcpy(receipt_token, line+11, 32); receipt_token[32] = 0;
        if (!ob_hex(receipt_token, 32, 32)) goto malformed;
        int n = snprintf(wire, sizeof wire, "v1:%s:%.8s:%s",
                         wanted.incarnation, line+2, line+44);
        if (n < 0 || (size_t)n >= sizeof wire || tiku_config_parse(wire, &receipt) ||
            !receipt.revision) goto malformed;
        int resource = !strcmp(p->path, "/sys/device/name") ? '1' : '2';
        if (line[0] == resource && receipt.revision == wanted.revision+1) {
            if (!strcmp(receipt_token, token) && !strcmp(receipt.desired, value)) match++;
            else conflict++;
        }
        line = end ? end + 1 : line + strlen(line);
    }
    if (match > 1 || (match && conflict)) goto malformed;
    if (match)
        return ob_result(p, TIKU_CONFIG_PENDING_CONFIRMED,
            "Device confirms this save was accepted. Current settings may be newer.");
    if (conflict)
        return ob_result(p, TIKU_CONFIG_PENDING_CONFLICT,
            "Another request changed this setting. This request cannot replace it.");
    if (current.revision == wanted.revision)
        return ob_result(p, TIKU_CONFIG_PENDING_RETRY,
            "A saved request is unconfirmed. Retry sends the exact original request.");
    return ob_result(p, TIKU_CONFIG_PENDING_UNKNOWN,
        "The setting advanced, but its receipt is unavailable. Acceptance cannot be determined.");
malformed:
    return ob_result(p, TIKU_CONFIG_PENDING_ERROR, "Device receipts are invalid. Local request retained.");
}
int tiku_config_outbox_inspect(const char *dir, tiku_backend_t *b,
                              tiku_config_pending_t *out)
{
    char uid[33];
    int fd, found;
    if (!out) return -1;
    memset(out, 0, sizeof *out);
    fd = ob_directory(dir, 0);
    if (fd < 0) return errno == ENOENT ? 0 :
        ob_result(out, TIKU_CONFIG_PENDING_ERROR, "Local recovery directory is unavailable or unsafe.");
    if (ob_uid(b, uid)) { close(fd); return ob_result(out, TIKU_CONFIG_PENDING_ERROR,
        "Could not verify the board hardware UID. Local recovery requests were not read."); }
    found = ob_load(fd, uid, out); close(fd);
    if (found < 0) return ob_result(out, TIKU_CONFIG_PENDING_ERROR,
        "Local request is unreadable or corrupt. Inspect the recovery directory; nothing was sent.");
    return found ? ob_resolve(b, out) : 0;
}
/**
 * @brief Send a kept record, once the board's UID is checked and the record
 *        synced, and resolve the outcome; a confirmed record is removed.
 * @return 0 when confirmed and removed, -1 otherwise with @p error filled.
 */
static int ob_transmit(int fd, tiku_backend_t *b, tiku_config_pending_t *p,
                       char *error, size_t max)
{
    char uid[33], primed[128];
    tiku_config_snapshot_t probe;
    if (OB_FAIL("before-send") || ob_uid(b, uid) || strcmp(uid, p->uid)) {
        snprintf(error, max, "Request saved locally; board identity could not be confirmed.");
        return -1;
    }
    if (ob_durable(fd, p)) {
        snprintf(error, max, "Could not sync the saved request. Nothing sent.");
        return -1;
    }
    if (p->enrollment) {
        if (tiku_config_probe(b, &probe) != 0 || !probe.enrollable ||
            ob_read(b, p->path, primed, sizeof primed) <= 0) {
            snprintf(error, max, "Enrollment state changed. Local request retained.");
            return -1;
        }
        (void)b->ops->write(b, p->path, p->request, strlen(p->request), error, max);
    } else (void)tiku_config_send(b, p->path, p->request, error, max);
    /* Acceptance is read from the receipts, not from the write's answer. */
    (void)ob_resolve(b, p);
    if (p->state == TIKU_CONFIG_PENDING_CONFIRMED) {
        if (!ob_remove(fd, p)) { snprintf(error, max, "Device confirmed the request."); return 0; }
        snprintf(error, max, "Device confirmed the request; local record cleanup failed.");
    } else snprintf(error, max, "%s", p->summary);
    return -1;
}
/** @brief Keep @p p for its board and send it.  @return As ob_transmit(),
 *         or -1 when the UID cannot be read, a request is already kept or
 *         the record cannot be stored. */
static int ob_start(const char *dir, tiku_backend_t *b, tiku_config_pending_t *p,
                     char *error, size_t max)
{
    tiku_config_pending_t old;
    int fd, rc;
    if (!b || !b->ops || !b->ops->write || ob_uid(b, p->uid) || !ob_valid(p)) {
        snprintf(error, max, "A valid request and verified hardware UID are required.");
        return -1;
    }
    fd = ob_directory(dir, 1);
    if (fd < 0) { snprintf(error, max, "Private recovery directory unavailable or busy. Nothing sent."); return -1; }
    if (ob_load(fd, p->uid, &old) != 0) {
        snprintf(error, max, "Resolve the existing local request before saving another setting.");
        close(fd); return -1;
    }
    if (ob_store(fd, p)) {
        snprintf(error, max, "Could not durably save the local request. Nothing sent; refresh recovery.");
        close(fd); return -1;
    }
    rc = ob_transmit(fd, b, p, error, max); close(fd); return rc;
}
int tiku_config_outbox_save(const char *dir, tiku_backend_t *b, const char *path,
    const tiku_config_snapshot_t *snapshot, const char *value, size_t length,
    char *error, size_t max)
{
    tiku_config_pending_t p = {0};
    uint8_t token[16];
    if (!path || strlen(path) >= sizeof p.path || tiku_config_token(token) ||
        tiku_config_request(snapshot, token, value, length, p.request, sizeof p.request) < 0) {
        snprintf(error, max, "Could not prepare a safe configuration request."); return -1;
    }
    snprintf(p.path, sizeof p.path, "%s", path);
    return ob_start(dir, b, &p, error, max);
}
int tiku_config_outbox_enroll(const char *dir, tiku_backend_t *b, char *error, size_t max)
{
    tiku_config_pending_t p = {.enrollment = 1};
    tiku_config_snapshot_t probe;
    tiku_model_t model;
    uint8_t token[16];
    if (tiku_config_probe(b, &probe) != 0 || !probe.enrollable ||
        b->ops->stat(b, "/sys/config/provision", &model) ||
        !tiku_model_is_writable(&model) || tiku_config_token(token)) {
        snprintf(error, max, "This device is not available for recovery enrollment."); return -1;
    }
    snprintf(p.path, sizeof p.path, "/sys/config/provision");
    for (int i = 0; i < 16; i++) snprintf(p.request+2*i, 3, "%02x", token[i]);
    return ob_start(dir, b, &p, error, max);
}
/** @brief Retry, or with @p forget remove, the kept request
 *         @p displayed, which must still match the kept record. */
static int ob_existing(const char *dir, tiku_backend_t *b,
    const tiku_config_pending_t *displayed, int forget, char *error, size_t max)
{
    char uid[33];
    tiku_config_pending_t p;
    int fd, rc = -1;
    if (!displayed || !ob_valid(displayed) || ob_uid(b, uid) || strcmp(uid, displayed->uid)) {
        snprintf(error, max, "The selected board or request changed. Refresh recovery."); return -1;
    }
    fd = ob_directory(dir, 1);
    if (fd < 0) { snprintf(error, max, "Local recovery directory unavailable or busy."); return -1; }
    if (ob_load(fd, uid, &p) != 1 || !ob_same(&p, displayed))
        snprintf(error, max, "The local request changed. Refresh before continuing.");
    else if (forget) {
        rc = ob_remove(fd, &p);
        snprintf(error, max, "%s", rc ? "Could not remove the local record." :
            "Local record removed. This does not undo or cancel any device operation.");
    } else {
        (void)ob_resolve(b, &p);
        if (p.state == TIKU_CONFIG_PENDING_CONFIRMED) {
            rc = ob_remove(fd, &p);
            snprintf(error, max, "%s", rc ? "Confirmed; local record cleanup failed." : "Device confirmed this request. Local record removed.");
        } else if (!p.enrollment && p.state == TIKU_CONFIG_PENDING_RETRY)
            rc = ob_transmit(fd, b, &p, error, max);
        else snprintf(error, max, "%s", p.summary);
    }
    close(fd); return rc;
}
int tiku_config_outbox_retry(const char *dir, tiku_backend_t *b,
    const tiku_config_pending_t *displayed, char *error, size_t max)
{ return ob_existing(dir, b, displayed, 0, error, max); }
int tiku_config_outbox_forget(const char *dir, tiku_backend_t *b,
    const tiku_config_pending_t *displayed, char *error, size_t max)
{ return ob_existing(dir, b, displayed, 1, error, max); }
