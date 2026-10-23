/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_backend_posix.c - local files as a backend.
 *
 * Permissions as the current user has them, inode numbers as identities
 * and per-node state in extended attributes.  A file's type comes from its
 * name, and an executable that carries the KintsugiOS stamp is an application.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifdef __linux__
#define _GNU_SOURCE 1 /* statx() and its STATX_BTIME */
#endif

#include "tiku_model.h"
#include "tiku_state.h"
#include "tiku_volume.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    tiku_store_t *store;
} posix_impl_t;

static tiku_store_t *px_store(tiku_backend_t *b);

/**
 * @brief Whether @p path is a volume's mount point; if so, @p m takes the
 *        volume's capacity, flags and name.
 *
 * The mount table is read at most once a second; the rows of one listing
 * share a read.
 */
static int
volume_here(const char *path, tiku_model_t *m)
{
    static tiku_volumes_t vs;
    static time_t seen;
    time_t now = time(NULL);
    int i;

    if (now != seen) {
        (void)tiku_volumes_scan(&vs);
        seen = now;
    }
    for (i = 0; i < vs.n; i++) {
        if (strcmp(vs.v[i].mount, path) == 0) {
            m->facts.total = vs.v[i].total;
            m->facts.avail = vs.v[i].avail;
            m->facts.vol_flags = tiku_volume_flags(&vs.v[i]);
            /* The entry takes the volume's name: the boot volume's directory
             * entry is "/" (MA-021). */
            if (vs.v[i].name[0] != '\0') {
                snprintf(m->name, sizeof m->name, "%s", vs.v[i].name);
            }
            return 1;
        }
    }
    return 0;
}

/** @brief Whether @p path holds anything at all, "." and ".." aside. */
static int
dir_has_entries(const char *path)
{
    DIR *d = opendir(path);
    struct dirent *e;
    int any = 0;

    if (d == NULL) {
        return 0;
    }
    while (!any && (e = readdir(d)) != NULL) {
        any = (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0);
    }
    (void)closedir(d);
    return any;
}

/** @brief Whether @p path is the Desktop directory, tiku_desktop_dir(). */
static int
desktop_here(const char *path)
{
    char desk[TIKU_PATH_MAX];

    return strcmp(path, tiku_desktop_dir(desk, sizeof desk)) == 0;
}

/**
 * @brief The type a file's name says it is.
 *
 * A host filesystem carries no type attribute, so the type comes from the
 * name's extension, case ignored; an unknown extension gives
 * application/octet-stream (MA-002).
 */
static const char *
type_by_name(const char *name)
{
    static const struct {
        const char *ext, *type;
    } kByExt[] = {{".txt", "text/plain"},
                  {".rtf", "application/rtf"},
                  {".tiku", "application/x-tiku-project"},
                  {".sdrlog", "application/x-tikusdr"},
                  {".md", "text/markdown"},
                  {".markdown", "text/markdown"},
                  {".tex", "text/x-tex"},
                  {".tkrd", "application/x-tiku-richdoc"},
                  {".bas", "text/x-basic"},
                  {".kicad_sch", "application/x-kicad-schematic"},
                  {".drl", "application/x-excellon"},
                  {".xnc", "application/x-excellon"},
                  {".xln", "application/x-excellon"},
                  {".gbr", "application/x-gerber"},
                  {".gbrjob", "application/x-gerber-job"},
                  {".sch", "application/x-schematic"},
                  {".ger", "application/x-gerber"},
                  {".gbrx", "application/x-gerber"},
                  {".gtl", "application/x-gerber"},
                  {".gbl", "application/x-gerber"},
                  {".gts", "application/x-gerber"},
                  {".gbs", "application/x-gerber"},
                  {".gtp", "application/x-gerber"},
                  {".gbp", "application/x-gerber"},
                  {".gto", "application/x-gerber"},
                  {".gbo", "application/x-gerber"},
                  {".gko", "application/x-gerber"},
                  {".c", "text/x-source"},
                  {".h", "text/x-source"},
                  {".sh", "text/x-script"},
                  {".py", "text/x-script"},
                  {".png", "image/png"},
                  {".bmp", "image/bmp"},
                  {".jpg", "image/jpeg"},
                  {".jpeg", "image/jpeg"},
                  {".gif", "image/gif"},
                  {".html", "text/html"},
                  {".htm", "text/html"},
                  {".json", "text/json"},
                  {".pdf", "application/pdf"},
                  {".zip", "application/zip"},
                  {".tar", "application/x-tar"},
                  {".tgz", "application/x-compressed-tar"},
                  {".gz", "application/gzip"},
                  {".doc", "application/msword"},
                  {".docx", "application/x-docx"},
                  {".odt", "application/vnd.oasis.opendocument.text"},
                  {".pptx", "application/x-pptx"},
                  {".tkdk", "application/x-tiku-deck"}};
    const char *dot;
    size_t i, n;

    if (name == NULL) {
        return "application/octet-stream";
    }
    /* The one double extension read: ".tar.gz" is a compressed tar. */
    n = strlen(name);
    if (n > 7 && strcasecmp(name + n - 7, ".tar.gz") == 0) {
        return "application/x-compressed-tar";
    }
    dot = strrchr(name, '.');
    if (dot == NULL || dot == name) {
        return "application/octet-stream";
    }
    for (i = 0; i < sizeof kByExt / sizeof kByExt[0]; i++) {
        if (strcasecmp(dot, kByExt[i].ext) == 0) {
            return kByExt[i].type;
        }
    }
    return "application/octet-stream";
}

#ifdef __APPLE__
/**
 * @brief Whether the Mach-O image at offset @p at carries the KintsugiOS
 *        stamp, the __TIKUOS segment.
 *
 * The application kit asks for that segment, so it exists in a file only
 * because something linked that kit.  Only the load commands, at the head
 * of the image, are read.
 */
static int
macho_has_stamp(FILE *f, unsigned long at)
{
    unsigned char h[32];
    unsigned long ncmds, sizeofcmds, off, i;

    if (fseek(f, (long)at, SEEK_SET) != 0 ||
        fread(h, 1u, sizeof h, f) != sizeof h) {
        return 0;
    }
    /* Only a 64-bit image in host (little-endian) order is read. */
    if (!(h[0] == 0xcf && h[1] == 0xfa && h[2] == 0xed && h[3] == 0xfe)) {
        return 0;
    }
    ncmds = (unsigned long)h[16] | ((unsigned long)h[17] << 8) |
            ((unsigned long)h[18] << 16) | ((unsigned long)h[19] << 24);
    sizeofcmds = (unsigned long)h[20] | ((unsigned long)h[21] << 8) |
                 ((unsigned long)h[22] << 16) | ((unsigned long)h[23] << 24);
    if (ncmds == 0u || ncmds > 4096u || sizeofcmds > (1u << 20)) {
        return 0;
    }
    off = at + sizeof h;
    for (i = 0u; i < ncmds; i++) {
        unsigned char c[24];
        unsigned long cmd, cmdsize;

        if (fseek(f, (long)off, SEEK_SET) != 0 ||
            fread(c, 1u, sizeof c, f) != sizeof c) {
            return 0;
        }
        cmd = (unsigned long)c[0] | ((unsigned long)c[1] << 8) |
              ((unsigned long)c[2] << 16) | ((unsigned long)c[3] << 24);
        cmdsize = (unsigned long)c[4] | ((unsigned long)c[5] << 8) |
                  ((unsigned long)c[6] << 16) | ((unsigned long)c[7] << 24);
        if (cmdsize < sizeof c) {
            return 0;
        }
        if (cmd == 0x19u /* LC_SEGMENT_64 */ &&
            memcmp(c + 8, "__TIKUOS", 9) == 0) {
            return 1;
        }
        off += cmdsize;
    }
    return 0;
}
#endif

/**
 * @brief Whether @p path is an executable built as a KintsugiOS application.
 *
 * Every KintsugiOS application carries a ".tikuos" section (on macOS a
 * __TIKUOS segment), put there by the application kit it links.  Finding
 * it reads the ELF header, the section headers and their names, never the
 * sections themselves.
 */
static int
is_tiku_app(const char *path)
{
    unsigned char h[64];
    FILE *f = fopen(path, "rb");
#ifdef __APPLE__
    unsigned char fat[8];
#endif
    unsigned long shoff, stroff = 0;
    unsigned shentsize, shnum, shstrndx, i;
    int found = 0;

    if (f == NULL) {
        return 0;
    }
    if (fread(h, 1u, sizeof h, f) != sizeof h) {
        (void)fclose(f);
        return 0;
    }
#ifdef __APPLE__
    /* A universal file is a directory of Mach-O files; the stamp is in
     * every slice or in none, so the first one answers for all. */
    if (h[0] == 0xca && h[1] == 0xfe && h[2] == 0xba && h[3] == 0xbe) {
        unsigned long slice = 0u, i;

        if (fseek(f, 8L + 8L, SEEK_SET) == 0 &&
            fread(fat, 1u, sizeof fat, f) == sizeof fat) {
            /* Big-endian, as everything in a fat header is. */
            for (i = 0u; i < 4u; i++) {
                slice = (slice << 8) | (unsigned long)fat[i];
            }
        }
        found = slice != 0u ? macho_has_stamp(f, slice) : 0;
        (void)fclose(f);
        return found;
    }
    if (h[0] == 0xcf && h[1] == 0xfa) {
        found = macho_has_stamp(f, 0u);
        (void)fclose(f);
        return found;
    }
#endif
    if (h[0] != 0x7f || h[1] != 'E' || h[2] != 'L' || h[3] != 'F' ||
        h[4] != 2 /* 64-bit only */ || h[5] != 1 /* little-endian */) {
        (void)fclose(f);
        return 0;
    }
    shoff = 0;
    for (i = 0; i < 8u; i++) {
        shoff |= (unsigned long)h[40 + i] << (8u * i);
    }
    shentsize = (unsigned)h[58] | ((unsigned)h[59] << 8);
    shnum = (unsigned)h[60] | ((unsigned)h[61] << 8);
    shstrndx = (unsigned)h[62] | ((unsigned)h[63] << 8);
    if (shoff == 0 || shentsize < 64u || shnum == 0u || shstrndx >= shnum ||
        shnum > 4096u) {
        (void)fclose(f);
        return 0;
    }
    {
        /* The section-name table's own header, to find the names. */
        unsigned char sh[64];

        if (fseek(f, (long)(shoff + (unsigned long)shstrndx * shentsize),
                  SEEK_SET) != 0 ||
            fread(sh, 1u, sizeof sh, f) != sizeof sh) {
            (void)fclose(f);
            return 0;
        }
        for (i = 0; i < 8u; i++) {
            stroff |= (unsigned long)sh[24 + i] << (8u * i);
        }
    }
    for (i = 0; i < shnum && !found; i++) {
        unsigned char sh[64];
        unsigned long name_at = 0;
        char name[16];
        size_t got;

        if (fseek(f, (long)(shoff + (unsigned long)i * shentsize), SEEK_SET) !=
                0 ||
            fread(sh, 1u, sizeof sh, f) != sizeof sh) {
            break;
        }
        name_at = (unsigned long)sh[0] | ((unsigned long)sh[1] << 8) |
                  ((unsigned long)sh[2] << 16) | ((unsigned long)sh[3] << 24);
        if (fseek(f, (long)(stroff + name_at), SEEK_SET) != 0) {
            break;
        }
        got = fread(name, 1u, sizeof name - 1u, f);
        name[got] = '\0';
        if (strcmp(name, ".tikuos") == 0) {
            found = 1;
        }
    }
    (void)fclose(f);
    return found;
}

/**
 * @brief Set @p m's birth time for @p path when the filesystem reports one:
 *        statx() on Linux, st_birthtimespec on macOS.  The time is the
 *        link target's when @p path resolves, else the link's own.
 */
static void
birth_time(tiku_model_t *m, const char *path, const struct stat *st)
{
#if defined(__linux__) && defined(STATX_BTIME)
    struct statx sx;

    (void)st;
    if ((statx(AT_FDCWD, path, 0, STATX_BTIME, &sx) == 0 ||
         statx(AT_FDCWD, path, AT_SYMLINK_NOFOLLOW, STATX_BTIME, &sx) == 0) &&
        (sx.stx_mask & STATX_BTIME) != 0u) {
        m->facts.btime = (int64_t)sx.stx_btime.tv_sec;
        m->facts.btime_known = 1;
    }
#elif defined(__APPLE__)
    (void)path;
    if (st->st_birthtimespec.tv_sec > 0) {
        m->facts.btime = (int64_t)st->st_birthtimespec.tv_sec;
        m->facts.btime_known = 1;
    }
#else
    (void)m;
    (void)path;
    (void)st;
#endif
}

/** @brief Fill a Model for @p path from a stat buffer; @p is_link makes it
 *         a SYMLINK whatever @p st describes. */
static void
from_stat(tiku_model_t *m, const char *path, const struct stat *st, int is_link)
{
    const char *slash = strrchr(path, '/');

    memset(m, 0, sizeof *m);
    snprintf(m->path, sizeof m->path, "%s", path);
    snprintf(m->name, sizeof m->name, "%s",
             (slash != NULL && slash[1] != '\0') ? slash + 1 : path);
    m->node_id = (uint64_t)st->st_ino;
    /* Which filesystem it is on: a departing volume's windows are found
     * by it (PVN-043). */
    m->facts.dev = (uint64_t)st->st_dev;
    m->facts.size = (int64_t)st->st_size;
    m->facts.mode = (unsigned)st->st_mode;
    m->facts.mode_known = 1;
    m->facts.mtime = (int64_t)st->st_mtime;
    birth_time(m, path, st);
    m->facts.cap_known = 1;
    snprintf(m->facts.req_cap, sizeof m->facts.req_cap, "-");
    snprintf(m->facts.meta, sizeof m->facts.meta, "-");

    if (is_link) {
        m->kind = TIKU_KIND_SYMLINK;
    } else if (S_ISDIR(st->st_mode) && volume_here(path, m)) {
        /* A mount point is a VOLUME, and "/" the Disks ROOT: it cannot be
         * renamed, dragging it to the Trash unmounts it, and it has a
         * capacity. */
        m->kind = (strcmp(path, "/") == 0) ? TIKU_KIND_ROOT : TIKU_KIND_VOLUME;
    } else if (S_ISDIR(st->st_mode)) {
        /* A directory called ".Trash" is the Trash, recognised by its name:
         * the Trash is a convention about a location, not a property of
         * the inode.  The Desktop is recognised by its whole path. */
        m->kind = (strcmp(m->name, ".Trash") == 0) ? TIKU_KIND_TRASH
                  : desktop_here(path)             ? TIKU_KIND_DESKTOP
                                                   : TIKU_KIND_DIRECTORY;
        if (m->kind == TIKU_KIND_TRASH) {
            m->facts.trash_full = dir_has_entries(path);
        }
    } else {
        m->kind = TIKU_KIND_FILE;
    }
    /* Permission as the current user has it, from access(). */
    if (access(path, R_OK) == 0) {
        m->facts.perm |= TIKU_P_READ;
    }
    if (access(path, W_OK) == 0) {
        m->facts.perm |= TIKU_P_WRITE;
    }
    if (access(path, X_OK) == 0) {
        m->facts.perm |= TIKU_P_EXEC;
    }
    snprintf(m->type, sizeof m->type, "%s",
             m->kind == TIKU_KIND_ROOT ? "application/x-vnd.Be-root"
             : S_ISDIR(st->st_mode)    ? "application/x-vnd.Be-directory"
             /* An executable carrying the KintsugiOS stamp gets the
              * application type; any other file gets its name's type. */
             : ((m->facts.perm & TIKU_P_EXEC) && is_tiku_app(path))
                 ? "application/x-vnd.be-app"
                 : type_by_name(m->name));
}

/** @brief Describe @p path, a link as the link itself; the Desktop's icon
 *         descriptor is seeded the first time it is seen.  0, or -1. */
static int
px_stat(tiku_backend_t *b, const char *path, tiku_model_t *out)
{
    struct stat st, lst;
    posix_impl_t *impl = (b != NULL) ? b->impl : NULL;

    if (path == NULL || out == NULL) {
        return -1;
    }
    if (lstat(path, &lst) != 0) {
        return -1;
    }
    if (S_ISLNK(lst.st_mode)) {
        /* A link reports its target's size and type when it resolves. */
        if (stat(path, &st) == 0) {
            from_stat(out, path, &st, 1);
            /* The link's identity is its own inode, so a link beside its
             * target has an id of its own in the listing diff. */
            out->node_id = (uint64_t)lst.st_ino;
        } else {
            /* The target has gone: the link is described from lstat() and
             * marked broken. */
            from_stat(out, path, &lst, 1);
            out->facts.link_broken = 1;
        }
    } else {
        from_stat(out, path, &lst, 0);
    }
    /* A Desktop directory with no icon attribute is given "_trk/icon" =
     * "desktop" the first time it is stat'ed, so a Desktop on any
     * filesystem shows the same art (FS-082).  A store that cannot be
     * written is left alone, and the stat still succeeds. */
    if (out->kind == TIKU_KIND_DESKTOP && impl != NULL) {
        tiku_store_t *store = px_store(b);
        char existing[16];

        if (store == NULL || !tiku_store_writable(store)) {
            out->backend = b;
            return 0;
        }
        if (tiku_state_read(store, path, "_trk/icon", existing,
                            sizeof existing) >= 0) {
            out->backend = b;
            return 0;
        }
        static const char desk_icon[] = "desktop";

        (void)tiku_state_write(store, path, "_trk/icon", desk_icon,
                               sizeof desk_icon);
    }
    out->backend = b;
    return 0;
}

/** @brief List @p path's entries, "." and ".." aside.  Entries, or -1 when
 *         the directory cannot be opened. */
static int
px_list(tiku_backend_t *b, const char *path, tiku_entry_fn fn, void *ctx)
{
    DIR *d = opendir(path);
    struct dirent *e;
    int n = 0;

    if (d == NULL) {
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        char child[TIKU_PATH_MAX];
        tiku_model_t m;
        size_t pl = strlen(path);

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        if (pl + strlen(e->d_name) + 2u > sizeof child) {
            continue;
        }
        memcpy(child, path, pl);
        if (pl > 0u && path[pl - 1u] != '/') {
            child[pl++] = '/';
        }
        memcpy(child + pl, e->d_name, strlen(e->d_name) + 1u);

        if (px_stat(b, child, &m) != 0) {
            /* An entry that cannot be stat'ed now -- busy, mid-write, a
             * transient error -- is still listed, with kind UNKNOWN and
             * what is known; the view holds it until a listing describes
             * it. */
            memset(&m, 0, sizeof m);
            (void)snprintf(m.path, sizeof m.path, "%s", child);
            (void)snprintf(m.name, sizeof m.name, "%s", e->d_name);
            (void)snprintf(m.facts.meta, sizeof m.facts.meta, "-");
            (void)snprintf(m.facts.req_cap, sizeof m.facts.req_cap, "?");
            m.kind = TIKU_KIND_UNKNOWN;
            m.facts.cap_known = 0;
            m.backend = b;
        }
        n++;
        if (fn != NULL && fn(&m, ctx) != 0) {
            break;
        }
    }
    (void)closedir(d);
    return n;
}

/** @brief Read up to @p max bytes from the start of @p path.  Bytes, or -1
 *         when it cannot be opened. */
static int
px_read(tiku_backend_t *b, const char *path, void *buf, size_t max)
{
    FILE *f = fopen(path, "rb");
    size_t n;

    (void)b;
    if (f == NULL) {
        return -1;
    }
    n = fread(buf, 1, max, f);
    (void)fclose(f);
    return (int)n;
}

/** @brief Replace @p path's contents with @p len bytes, in place.  0, or -1
 *         with @p err filled. */
static int
px_write(tiku_backend_t *b, const char *path, const void *buf, size_t len,
         char *err, size_t errmax)
{
    FILE *f;

    (void)b;
    f = fopen(path, "wb");
    if (f == NULL) {
        if (err != NULL && errmax > 0u) {
            snprintf(err, errmax, "cannot write '%s' (%s)", path,
                     strerror(errno));
        }
        return -1;
    }
    if (len > 0u && fwrite(buf, 1, len, f) != len) {
        (void)fclose(f);
        if (err != NULL && errmax > 0u) {
            snprintf(err, errmax, "short write to '%s'", path);
        }
        return -1;
    }
    if (fclose(f) != 0) {
        /* Buffered bytes are written at close, so a full disk can fail
         * here. */
        if (err != NULL && errmax > 0u) {
            snprintf(err, errmax, "cannot write '%s' (%s)", path,
                     strerror(errno));
        }
        return -1;
    }
    return 0;
}

/** @brief Nothing to service: local files raise no notices, and a view
 *         over them polls.  Always 0. */
static int
px_pump(tiku_backend_t *b, int timeout_ms)
{
    (void)b;
    (void)timeout_ms;
    return 0;
}

/** @brief The extended-attribute state store, opened on first use. */
static tiku_store_t *
px_store(tiku_backend_t *b)
{
    posix_impl_t *impl = b->impl;

    if (impl->store == NULL) {
        impl->store = tiku_store_xattr_open();
    }
    return impl->store;
}

/** @brief Free the state store and the backend. */
static void
px_close(tiku_backend_t *b)
{
    posix_impl_t *impl = b->impl;

    if (impl != NULL) {
        tiku_store_free(impl->store);
        free(impl);
    }
    free(b);
}

/** @brief Apply read/write/execute to a local file. */
static int
px_setperm(tiku_backend_t *b, const char *path, unsigned perm, char *err,
           size_t errmax)
{
    struct stat st;
    mode_t mode;

    (void)b;
    if (path == NULL || stat(path, &st) != 0) {
        snprintf(err, errmax, "%s", strerror(errno));
        return -1;
    }
    /* Only the owner's three bits change, which is what the grid edits;
     * group and other keep theirs. */
    mode = st.st_mode & (mode_t) ~(S_IRUSR | S_IWUSR | S_IXUSR);
    if (perm & TIKU_P_READ) {
        mode |= S_IRUSR;
    }
    if (perm & TIKU_P_WRITE) {
        mode |= S_IWUSR;
    }
    if (perm & TIKU_P_EXEC) {
        mode |= S_IXUSR;
    }
    if (chmod(path, mode) != 0) {
        snprintf(err, errmax, "%s", strerror(errno));
        return -1;
    }
    return 0;
}

/** @brief Make the folder @p path.  0, or -1 with @p err filled. */
static int
px_mkdir(tiku_backend_t *b, const char *path, char *err, size_t errmax)
{
    (void)b;
    if (mkdir(path, 0755) != 0) {
        if (err != NULL && errmax > 0u) {
            snprintf(err, errmax, "%s", strerror(errno));
        }
        return -1;
    }
    return 0;
}

static const tiku_backend_ops_t POSIX_OPS = {
    /* A local filesystem has no change ring: a NULL changes makes the view
     * poll, and with no change records there is no identity to resolve. */
    "posix", px_stat, px_list,    px_read,  px_write, px_pump,
    NULL,    NULL,    px_setperm, px_store, px_close, px_mkdir};

tiku_backend_t *
tiku_backend_posix_open(void)
{
    tiku_backend_t *b = calloc(1, sizeof *b);
    posix_impl_t *impl = calloc(1, sizeof *impl);

    if (b == NULL || impl == NULL) {
        free(b);
        free(impl);
        return NULL;
    }
    b->ops = &POSIX_OPS;
    b->impl = impl;
    return b;
}
