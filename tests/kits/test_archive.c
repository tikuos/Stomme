/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_archive.c - the archive kit against archives Python's zipfile,
 * tarfile and gzip made (tests/fixtures/archive.inc): each listed as its
 * manifest says and extracted to the same bytes; the hostile ones kept
 * inside their folder; the streaming inflater on its own, Deflate64 and
 * broken streams included.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "tiku_archive.h"
#include "tiku_crypto.h"
#include "tiku_inflate.h"

#include "../fixtures/archive.inc"

#define T0 1714566896               /* the fixtures' mtime */

static int fails;
static char scratch[PATH_MAX];      /* this run's folder, removed at the end */

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-7s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) {
        fails++;
    }
}

/** @brief A fixture's bytes, decoded from its base64, to free(). */
static unsigned char *
unbase64(const char *b64, size_t *n)
{
    static const char abc[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned char *p = malloc(strlen(b64) + 1);
    unsigned v = 0;
    int bits = 0;

    for (*n = 0; *b64 && *b64 != '='; b64++) {
        v = v << 6 | (unsigned)(strchr(abc, *b64) - abc);
        if ((bits += 6) >= 8) {
            bits -= 8;
            p[(*n)++] = (unsigned char)(v >> bits);
        }
    }
    return p;
}

/** @brief Which fixture is called @p name.  @return its index. */
static int
fixture(const char *name)
{
    int k;

    for (k = 0; k < (int)(sizeof fixtures / sizeof fixtures[0]); k++) {
        if (strcmp(fixtures[k].name, name) == 0) {
            return k;
        }
    }
    fprintf(stderr, "no fixture %s\n", name);
    exit(2);
}

/** @brief Fixture @p name written into the scratch folder under its own
 *         name and extension; its path into @p path. */
static void
write_fixture(const char *name, char *path, size_t max)
{
    int k = fixture(name);
    size_t n;
    unsigned char *b = unbase64(fixtures[k].data, &n);
    FILE *f;

    snprintf(path, max, "%s/%s%s", scratch, name, fixtures[k].ext);
    f = fopen(path, "wb");
    if (f == NULL || fwrite(b, 1, n, f) != n || fclose(f) != 0) {
        fprintf(stderr, "cannot write %s\n", path);
        exit(2);
    }
    free(b);
}

/** @brief A new empty folder under the scratch one; its path into @p out. */
static void
fresh(const char *name, char *out, size_t max)
{
    snprintf(out, max, "%s/%s", scratch, name);
    (void)mkdir(out, 0755);
}

/** @brief A file's bytes' CRC-32 and size.  @return the size, -1 when
 *         the file cannot be read. */
static long
file_crc(const char *path, uint32_t *crc)
{
    unsigned char buf[4096];
    FILE *f = fopen(path, "rb");
    long total = 0;
    size_t n;

    *crc = 0;
    if (f == NULL) {
        return -1;
    }
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        *crc = tiku_crc32(*crc, buf, n);
        total += (long)n;
    }
    fclose(f);
    return total;
}

/* ----------------------------------------------------------- manifest */

/** @brief One line of a fixture's manifest. */
typedef struct {
    char     kind, flags;
    unsigned mode;                  /* 0: none given */
    long long size;
    uint32_t crc;
    char     path[600], link[600];
} mline_t;

/** @brief The next manifest line at @p *at.  @return 1, or 0 at the end. */
static int
next_line(const char **at, mline_t *m)
{
    const char *p = *at, *end = strchr(p, '\n'), *tab;
    char mode[8], flags[4];
    int used = 0;

    if (end == NULL) {
        return 0;
    }
    memset(m, 0, sizeof *m);
    if (sscanf(p, "%c %7s %lld %x %3s %n", &m->kind, mode, &m->size,
               &m->crc, flags, &used) < 5) {
        return 0;
    }
    m->mode = mode[0] == '-' ? 0 : (unsigned)strtoul(mode, NULL, 8);
    m->flags = flags[0];
    p += used;
    tab = memchr(p, '\t', (size_t)(end - p));
    snprintf(m->path, sizeof m->path, "%.*s",
             (int)((tab != NULL ? tab : end) - p), p);
    if (tab != NULL) {
        snprintf(m->link, sizeof m->link, "%.*s", (int)(end - tab - 1),
                 tab + 1);
    }
    *at = end + 1;
    return 1;
}

/** @brief The kit's kind as the manifest's letter. */
static char
kind_letter(int type)
{
    return "fdlho"[type];
}

/** @brief The kit's flags as the manifest's letter. */
static char
flag_letter(unsigned flags)
{
    return flags & TIKU_ARC_REFUSED ? 'R' : flags & TIKU_ARC_UNREADABLE ? 'U'
         : flags & TIKU_ARC_METADATA ? 'M' : '-';
}

/** @brief Whether the listing is the manifest, entry for entry. */
static int
listing_is(tiku_archive_t *a, const char *manifest)
{
    const char *at = manifest;
    mline_t m;
    long i = 0;

    for (; next_line(&at, &m); i++) {
        const tiku_archive_entry_t *e = tiku_archive_entry(a, i);

        if (e == NULL || kind_letter(e->type) != m.kind ||
            e->mode != m.mode || e->size != m.size ||
            flag_letter(e->flags) != m.flags || strcmp(e->path, m.path) ||
            strcmp(e->link != NULL ? e->link : "", m.link)) {
            printf("    entry %ld: %s, not %c %o %lld %c %s\n", i,
                   e != NULL ? e->path : "(none)", m.kind, m.mode, m.size,
                   m.flags, m.path);
            return 0;
        }
    }
    return i == tiku_archive_count(a);
}

/** @brief Whether @p dest holds what the manifest's plain entries say:
 *         files with their bytes, folders, links to their targets; and
 *         nothing of the refused, unreadable or metadata ones. */
static int
tree_is(const char *dest, const char *manifest)
{
    const char *at = manifest;
    mline_t m;
    char path[PATH_MAX], target[600];
    struct stat st;
    uint32_t crc;

    while (next_line(&at, &m)) {
        int there;

        snprintf(path, sizeof path, "%s/%s", dest, m.path);
        there = lstat(path, &st) == 0;
        if (m.flags != '-' || m.kind == 'o') {
            if (there && m.flags == 'R') {
                printf("    %s was made\n", m.path);
                return 0;
            }
            continue;
        }
        if (!there || (m.kind == 'd' && !S_ISDIR(st.st_mode)) ||
            (m.kind == 'f' && (!S_ISREG(st.st_mode) ||
                               file_crc(path, &crc) != (m.size < 0
                                   ? (long)st.st_size : (long)m.size) ||
                               crc != m.crc)) ||
            (m.kind == 'l' && (readlink(path, target, sizeof target - 1) !=
                               (ssize_t)strlen(m.link) ||
                               memcmp(target, m.link, strlen(m.link))))) {
            printf("    %s is not as listed\n", m.path);
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------ inflater */

/** @brief Compressed bytes handed out a few at a time. */
typedef struct {
    const unsigned char *p;
    long n, at, step;
} feed_t;

/** @brief The inflater's input: at most @c step bytes a fill. */
static long
feed(void *ctx, unsigned char *buf, long max)
{
    feed_t *f = ctx;
    long n = f->n - f->at;

    n = n < max ? n : max;
    n = n < f->step ? n : f->step;
    memcpy(buf, f->p + f->at, (size_t)n);
    f->at += n;
    return n;
}

/** @brief @p n compressed bytes decoded @p out_step at a time from fills
 *         of @p in_step.  @return the output's CRC; @p len its length or
 *         -1 when the stream was refused. */
static uint32_t
inflate_all(const unsigned char *p, long n, long in_step, long out_step,
            int flags, long *len)
{
    feed_t f = { p, n, 0, in_step };
    tiku_inflater_t *z = tiku_inflater_new(feed, &f, flags);
    unsigned char *buf = malloc((size_t)out_step);
    uint32_t crc = 0;
    long got;

    *len = 0;
    while ((got = tiku_inflater_read(z, buf, out_step)) > 0) {
        crc = tiku_crc32(crc, buf, (size_t)got);
        *len += got;
    }
    if (got < 0 || !tiku_inflater_done(z)) {
        *len = -1;
    }
    tiku_inflater_free(z);
    free(buf);
    return crc;
}

/** @brief Bits written lowest first, as DEFLATE packs them. */
typedef struct {
    unsigned char *out;
    long n;
    uint32_t acc;
    int bits;
} bitw_t;

/** @brief @p n bits of @p v, lowest first. */
static void
put_bits(bitw_t *w, uint32_t v, int n)
{
    w->acc |= v << w->bits;
    w->bits += n;
    while (w->bits >= 8) {
        w->out[w->n++] = (unsigned char)w->acc;
        w->acc >>= 8;
        w->bits -= 8;
    }
}

/** @brief A Huffman code, sent highest bit first. */
static void
put_code(bitw_t *w, uint32_t code, int n)
{
    while (n-- > 0) {
        put_bits(w, (code >> n) & 1u, 1);
    }
}

/** @brief A literal in the fixed code (RFC 1951 3.2.6). */
static void
put_literal(bitw_t *w, unsigned c)
{
    if (c < 144) {
        put_code(w, 0x30 + c, 8);
    } else {
        put_code(w, 0x190 + c - 144, 9);
    }
}

/** @brief Deflate64 by hand: 40,000 bytes as literals, then a match of
 *         30,000 from 40,000 back -- a length and a distance only it has. */
static void
inflate64_tests(void)
{
    enum { HEAD = 40000, COPY = 30000 };
    unsigned char *raw = malloc(HEAD + COPY), *z = malloc(80000);
    bitw_t w = { z, 0, 0, 0 };
    uint32_t x = 7, want;
    long len, i;

    for (i = 0; i < HEAD; i++) {
        x = x * 1103515245u + 12345u;
        raw[i] = (unsigned char)(x >> 16);
    }
    memcpy(raw + HEAD, raw, COPY);
    want = tiku_crc32(0, raw, HEAD + COPY);
    put_bits(&w, 1, 1);             /* final */
    put_bits(&w, 1, 2);             /* fixed codes */
    for (i = 0; i < HEAD; i++) {
        put_literal(&w, raw[i]);
    }
    put_code(&w, 0xc5, 8);          /* length 285: 3 + sixteen bits */
    put_bits(&w, COPY - 3, 16);
    put_code(&w, 30, 5);            /* distance 30: 32769 + fourteen bits */
    put_bits(&w, HEAD - 32769, 14);
    put_code(&w, 0, 7);             /* end of block */
    put_bits(&w, 0, 7);
    check(inflate_all(z, w.n, 4096, 5000, TIKU_INFLATE_DEFLATE64, &len)
          == want && len == HEAD + COPY, "AR-002",
          "Deflate64: a 30,000-byte match from 40,000 back, a 64 KB window");
    (void)inflate_all(z, w.n, 4096, 5000, 0, &len);
    check(len == -1, "AR-003", "the same stream read as DEFLATE is refused");
    free(raw);
    free(z);
}

/** @brief Streams that are not DEFLATE, each refused. */
static void
inflate_broken_tests(void)
{
    static const unsigned char reserved[] = { 0x07 };           /* type 3 */
    static const unsigned char nlen[] = { 0x01, 0x05, 0x00, 0x00, 0x00 };
    /* 'a', then a match 2 back: zlib says "distance too far back". */
    static const unsigned char far[] = { 0x4b, 0x04, 0x42, 0x00 };
    /* Nineteen code-length codes of length 1: "invalid code lengths". */
    static const unsigned char over[] = { 0x05, 0xe0, 0x93, 0x24, 0x49,
                                          0x92, 0x24, 0x49, 0x92, 0x00 };
    long len;

    (void)inflate_all(reserved, 1, 64, 64, 0, &len);
    check(len == -1, "AR-004", "a reserved block type is refused");
    (void)inflate_all(nlen, 5, 64, 64, 0, &len);
    check(len == -1, "AR-005", "a stored length that is not its NOT's");
    (void)inflate_all(far, 4, 64, 64, 0, &len);
    check(len == -1, "AR-006", "a distance before the first byte written");
    (void)inflate_all(over, 10, 64, 64, 0, &len);
    check(len == -1, "AR-007", "an over-subscribed code is refused");
}

/** @brief The gzip fixture's DEFLATE body, decoded in every chunking,
 *         and the bytes after it handed back. */
static void
inflate_tests(void)
{
    int k = fixture("gz_fields");
    size_t n;
    unsigned char *g = unbase64(fixtures[k].data, &n), tail[16];
    const char *at = fixtures[k].manifest;
    long body = 10 + 2 + 4 + 10 + 10 + 2, len, ok = 1, steps[][2] = {
        { 65536, 65536 }, { 1, 7 }, { 13, 4096 }, { 4096, 1 } };
    feed_t f;
    tiku_inflater_t *z;
    mline_t m;
    size_t i;

    (void)next_line(&at, &m);
    for (i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        ok &= inflate_all(g + body, (long)n - body, steps[i][0], steps[i][1],
                          0, &len) == m.crc && len > 0;
    }
    check(ok, "AR-001", "DEFLATE decoded whole from fills of 1 to 64 KB, "
          "read out 1 byte to 64 KB at a time");
    f = (feed_t){ g + body, (long)n - body, 0, 65536 };
    z = tiku_inflater_new(feed, &f, 0);
    while (tiku_inflater_read(z, tail, sizeof tail) > 0) {
    }
    check(tiku_inflater_tail(z, tail, sizeof tail) == 8 &&
          memcmp(tail, g + n - 8, 8) == 0, "AR-008",
          "the 8 bytes after the stream -- gzip's trailer -- handed back");
    tiku_inflater_free(z);
    free(g);
    inflate64_tests();
    inflate_broken_tests();
}

/* ------------------------------------------------------------ archives */

/** @brief Each good fixture listed as its manifest says and extracted to
 *         the same bytes, and what its format is called. */
static void
read_all_tests(void)
{
    static const struct { const char *name, *format; } good[] = {
        { "zip_features", "ZIP archive" }, { "zip_windows", "ZIP archive" },
        { "zip_descriptor", "ZIP archive" }, { "zip64", "ZIP archive" },
        { "zip_sfx", "ZIP archive" }, { "tar_pax", "tar archive" },
        { "tar_gnu", "tar archive" }, { "tar_v7", "tar archive" },
        { "tgz", "gzip-compressed tar archive" },
        { "gz_fields", "gzip-compressed file" },
        { "gz_members", "gzip-compressed file" }
    };
    char path[PATH_MAX], dest[PATH_MAX], row[16], what[160];
    size_t i;

    for (i = 0; i < sizeof good / sizeof good[0]; i++) {
        int k = fixture(good[i].name);
        tiku_archive_t *a = NULL;
        tiku_archive_err_t err;

        write_fixture(good[i].name, path, sizeof path);
        err = tiku_archive_open(path, NULL, NULL, &a);
        snprintf(row, sizeof row, "AR-%03d", 10 + (int)i * 2);
        snprintf(what, sizeof what, "%s: a %s, listed as made", good[i].name,
                 good[i].format);
        check(err == TIKU_ARCHIVE_OK && a != NULL &&
              strcmp(tiku_archive_format_name(a), good[i].format) == 0 &&
              !tiku_archive_damaged(a) && listing_is(a, fixtures[k].manifest),
              row, what);
        fresh(good[i].name, dest, sizeof dest);
        snprintf(row, sizeof row, "AR-%03d", 11 + (int)i * 2);
        snprintf(what, sizeof what, "%s: extracted, every byte as made",
                 good[i].name);
        check(a != NULL && tiku_archive_extract(a, dest, NULL) ==
              TIKU_ARCHIVE_OK && tree_is(dest, fixtures[k].manifest), row,
              what);
        tiku_archive_close(a);
    }
}

/** @brief Times, modes and the Mac's metadata, from the extracted trees. */
static void
detail_tests(void)
{
    char path[PATH_MAX];
    struct stat st;
    struct tm tm = { 0 };

    snprintf(path, sizeof path, "%s/zip_features/stamped.txt", scratch);
    check(stat(path, &st) == 0 && st.st_mtime == T0 + 3600, "AR-040",
          "an extended timestamp's mtime, over DOS's");
    tm.tm_year = 124;
    tm.tm_mon = 4;
    tm.tm_mday = 1;
    tm.tm_hour = 12;
    tm.tm_min = 34;
    tm.tm_sec = 56;
    tm.tm_isdst = -1;
    snprintf(path, sizeof path, "%s/zip_features/docs/readme.txt", scratch);
    check(stat(path, &st) == 0 && st.st_mtime == mktime(&tm), "AR-041",
          "a DOS time, read as the local time it is");
    snprintf(path, sizeof path, "%s/zip_features/bin/tool", scratch);
    check(stat(path, &st) == 0 && (st.st_mode & 0777) == 0755, "AR-042",
          "a Unix mode kept: the tool runs");
    snprintf(path, sizeof path, "%s/tar_pax/top", scratch);
    check(stat(path, &st) == 0 && (st.st_mode & 0777) == 0750 &&
          st.st_mtime == T0, "AR-043",
          "a folder's mode and time, set after its contents");
    snprintf(path, sizeof path, "%s/zip_features/__MACOSX", scratch);
    check(lstat(path, &st) != 0, "AR-044",
          "a Mac's __MACOSX companions are listed, not extracted");
    snprintf(path, sizeof path, "%s/tar_pax/top/hard", scratch);
    {
        struct stat t;
        char data[PATH_MAX];

        snprintf(data, sizeof data, "%s/tar_pax/top/data.txt", scratch);
        check(stat(path, &st) == 0 && stat(data, &t) == 0 &&
              st.st_ino == t.st_ino, "AR-045",
              "a hard link made to the file it names");
    }
}

/** @brief What an extraction reported, entry by entry. */
typedef struct {
    tiku_archive_err_t why[32];
    int calls;
    int cancel_after;               /* progress calls before stopping */
} seen_t;

/** @brief An entry's trouble, kept by its index. */
static void
seen_report(void *ctx, long entry, tiku_archive_err_t why)
{
    seen_t *s = ctx;

    if (entry >= 0 && entry < 32) {
        s->why[entry] = why;
    }
}

/** @brief Progress, cancelled after @c cancel_after calls when set. */
static int
seen_progress(void *ctx, int64_t done, int64_t total)
{
    seen_t *s = ctx;

    (void)done;
    (void)total;
    return s->cancel_after > 0 && ++s->calls >= s->cancel_after;
}

/** @brief @p name opened and extracted into a fresh folder of its own,
 *         inside a jail folder, reporting into @p s.  @return the result. */
static tiku_archive_err_t
extract_seen(const char *name, seen_t *s, char *dest, size_t max)
{
    tiku_archive_extract_t opt;
    tiku_archive_t *a = NULL;
    tiku_archive_err_t err;
    char path[PATH_MAX], jail[PATH_MAX], leaf[128];

    memset(&opt, 0, sizeof opt);
    opt.report = seen_report;
    opt.progress = seen_progress;
    opt.ctx = s;
    write_fixture(name, path, sizeof path);
    snprintf(leaf, sizeof leaf, "jail-%s", name);
    fresh(leaf, jail, sizeof jail);
    snprintf(dest, max, "%s/in", jail);
    (void)mkdir(dest, 0755);
    err = tiku_archive_open(path, NULL, NULL, &a);
    if (err == TIKU_ARCHIVE_OK) {
        err = tiku_archive_extract(a, dest, &opt);
        tiku_archive_close(a);
    }
    return err;
}

/** @brief Whether the jail around @p dest holds nothing but it. */
static int
jail_clean(const char *dest)
{
    char cmd[PATH_MAX + 64];

    snprintf(cmd, sizeof cmd, "test \"$(ls -A '%s/..')\" = in", dest);
    return system(cmd) == 0;
}

/** @brief Whether @p dest holds @p name and nothing else. */
static int
only(const char *dest, const char *name)
{
    char cmd[PATH_MAX + 64];

    snprintf(cmd, sizeof cmd, "test \"$(ls -A '%s')\" = '%s'", dest, name);
    return system(cmd) == 0;
}

/** @brief Names that climb out are refused; the rest made inside. */
static void
hostile_zip_tests(void)
{
    seen_t s;
    char dest[PATH_MAX], p[PATH_MAX];
    struct stat st;

    memset(&s, 0, sizeof s);
    check(extract_seen("hostile_zip", &s, dest, sizeof dest) ==
          TIKU_ARCHIVE_UNSAFE && s.why[0] == TIKU_ARCHIVE_UNSAFE &&
          s.why[1] == TIKU_ARCHIVE_UNSAFE && s.why[3] == TIKU_ARCHIVE_UNSAFE,
          "AR-050", "'../', 'a/../../' and a control character refused");
    snprintf(p, sizeof p, "%s/abs/inside.txt", dest);
    check(stat(p, &st) == 0 && jail_clean(dest), "AR-051",
          "an absolute path made inside; nothing beside the folder");
}

/** @brief Links out, links on the way, hard links out, FIFOs, devices,
 *         one name twice, set-uid: none gets past the folder. */
static void
hostile_tar_tests(void)
{
    seen_t s;
    char dest[PATH_MAX], p[PATH_MAX], q[PATH_MAX];
    struct stat st, t;

    memset(&s, 0, sizeof s);
    (void)extract_seen("hostile_tar", &s, dest, sizeof dest);
    snprintf(p, sizeof p, "%s/out", dest);
    check(s.why[0] == TIKU_ARCHIVE_UNSAFE && lstat(p, &st) == 0 &&
          S_ISDIR(st.st_mode), "AR-052",
          "a link out refused; a file under its name lands in a folder");
    snprintf(p, sizeof p, "%s/dir/via-link.txt", dest);
    check(s.why[2] == TIKU_ARCHIVE_UNSAFE &&
          s.why[5] == TIKU_ARCHIVE_UNSAFE && lstat(p, &st) != 0, "AR-053",
          "an absolute link refused; a path through a link never followed");
    snprintf(p, sizeof p, "%s/hard-in", dest);
    snprintf(q, sizeof q, "%s/ok.txt", dest);
    check(s.why[7] == TIKU_ARCHIVE_UNSAFE && stat(p, &st) == 0 &&
          stat(q, &t) == 0 && st.st_ino == t.st_ino, "AR-054",
          "a hard link out refused, one to a file made here made");
    check(s.why[9] == TIKU_ARCHIVE_UNSUPPORTED &&
          s.why[10] == TIKU_ARCHIVE_UNSUPPORTED, "AR-055",
          "a FIFO and a device are not made");
    snprintf(p, sizeof p, "%s/twice.txt", dest);
    {
        uint32_t crc;

        check(s.why[12] == TIKU_ARCHIVE_REPLACED &&
              file_crc(p, &crc) == 7 && crc == tiku_crc32(0, "second\n", 7),
              "AR-056", "a name twice: the later one wins, and says so");
    }
    snprintf(p, sizeof p, "%s/setuid", dest);
    check(stat(p, &st) == 0 && (st.st_mode & 07777) == 0755 &&
          jail_clean(dest), "AR-057",
          "set-uid dropped; nothing beside the folder");
}

/** @brief A link whose target climbs back up after passing through a
 *         link made earlier is refused; one that climbs, then descends,
 *         is made. */
static void
climb_tests(void)
{
    seen_t s;
    char dest[PATH_MAX], p[PATH_MAX + 8];
    struct stat st;

    memset(&s, 0, sizeof s);
    (void)extract_seen("climb_tar", &s, dest, sizeof dest);
    snprintf(p, sizeof p, "%s/b/l", dest);
    check(s.why[3] == TIKU_ARCHIVE_UNSAFE && lstat(p, &st) != 0, "AR-058",
          "'../a/up/..' through the link a/up -> '..' refused");
    snprintf(p, sizeof p, "%s/b/ok", dest);
    check(s.why[1] == TIKU_ARCHIVE_OK && s.why[4] == TIKU_ARCHIVE_OK &&
          lstat(p, &st) == 0 && S_ISLNK(st.st_mode) && jail_clean(dest),
          "AR-059", "'..' and '../a/up/b' made; nothing beside the folder");
}

/** @brief Archives that lie, or are cut short. */
static void
broken_tests(void)
{
    char path[PATH_MAX], dest[PATH_MAX], p[PATH_MAX];
    tiku_archive_t *a = NULL;
    struct stat st;
    seen_t s;

    write_fixture("bomb_zip", path, sizeof path);
    check(tiku_archive_open(path, NULL, NULL, &a) == TIKU_ARCHIVE_DAMAGED &&
          a == NULL, "AR-060", "entries sharing their data: not opened");
    memset(&s, 0, sizeof s);
    check(extract_seen("liar_zip", &s, dest, sizeof dest) ==
          TIKU_ARCHIVE_CHECKSUM, "AR-061",
          "an entry inflating past its size is stopped there");
    snprintf(p, sizeof p, "%s/liar.txt", dest);
    check(lstat(p, &st) != 0, "AR-062", "and its half-made file taken away");
    memset(&s, 0, sizeof s);
    check(extract_seen("badcrc_zip", &s, dest, sizeof dest) ==
          TIKU_ARCHIVE_CHECKSUM, "AR-063", "a ZIP entry's wrong CRC found");
    memset(&s, 0, sizeof s);
    check(extract_seen("badgz", &s, dest, sizeof dest) ==
          TIKU_ARCHIVE_CHECKSUM, "AR-064", "a gzip trailer's wrong CRC found");
    write_fixture("cut_tgz", path, sizeof path);
    check(tiku_archive_open(path, NULL, NULL, &a) == TIKU_ARCHIVE_OK &&
          tiku_archive_damaged(a) && tiku_archive_count(a) > 0, "AR-065",
          "a tar.gz cut short: what came before listed, marked damaged");
    fresh("cut", dest, sizeof dest);
    check(a != NULL && tiku_archive_extract(a, dest, NULL) ==
          TIKU_ARCHIVE_DAMAGED, "AR-066", "and extracting it says damaged");
    tiku_archive_close(a);
}

/** @brief Whether every file the manifest of @p name lists reads into
 *         memory as made: found by its path, its size and CRC. */
static int
parts_read(const char *name)
{
    char path[PATH_MAX];
    const char *at = fixtures[fixture(name)].manifest;
    tiku_archive_t *a = NULL;
    mline_t m;
    int ok;

    write_fixture(name, path, sizeof path);
    ok = tiku_archive_open(path, NULL, NULL, &a) == TIKU_ARCHIVE_OK;
    while (ok && next_line(&at, &m)) {
        unsigned char *b = NULL;
        size_t n = 0;
        long i = tiku_archive_find(a, m.path);

        if (m.kind != 'f') {
            continue;
        }
        ok = i >= 0 && tiku_archive_read(a, i, 1u << 20, &b, &n) ==
             TIKU_ARCHIVE_OK && (m.size < 0 || (long long)n == m.size) &&
             tiku_crc32(0, b, n) == m.crc && b[n] == '\0';
        if (!ok) {
            printf("    %s: %s did not read\n", name, m.path);
        }
        free(b);
    }
    tiku_archive_close(a);
    return ok;
}

/** @brief Entries read whole into memory, as a package's parts are:
 *         found by name, bounded, and checked. */
static void
part_tests(void)
{
    char path[PATH_MAX];
    tiku_archive_t *a = NULL;
    unsigned char *b = (unsigned char *)"";
    size_t n = 1;

    check(parts_read("zip_features") && parts_read("zip64") &&
          parts_read("zip_descriptor") && parts_read("tar_pax") &&
          parts_read("tgz") && parts_read("gz_fields"), "AR-090",
          "each file read into memory, ZIP, tar and gzip, as made");
    write_fixture("zip_features", path, sizeof path);
    (void)tiku_archive_open(path, NULL, NULL, &a);
    check(tiku_archive_find(a, "DOCS/README.TXT") ==
          tiku_archive_find(a, "docs/readme.txt") &&
          tiku_archive_find(a, "docs/readme.txt") >= 0 &&
          tiku_archive_find(a, "docs/missing.txt") < 0 &&
          tiku_archive_find(NULL, "x") < 0, "AR-091",
          "found by path, or by path whatever its case; else -1");
    check(tiku_archive_read(a, tiku_archive_find(a, "data/big.bin"), 1000,
                            &b, &n) == TIKU_ARCHIVE_TOO_LARGE &&
          b == NULL && n == 0, "AR-092",
          "an entry past the bound is not read at all");
    check(tiku_archive_read(a, tiku_archive_find(a, "docs"), 1000, &b, &n)
          == TIKU_ARCHIVE_UNSUPPORTED &&
          tiku_archive_read(a, 999, 1000, &b, &n) == TIKU_ARCHIVE_MISSING,
          "AR-093", "a folder, or no such entry, reads nothing");
    tiku_archive_close(a);
    write_fixture("badcrc_zip", path, sizeof path);
    (void)tiku_archive_open(path, NULL, NULL, &a);
    check(a != NULL && tiku_archive_read(a, 0, 1u << 20, &b, &n) ==
          TIKU_ARCHIVE_CHECKSUM && b == NULL, "AR-094",
          "a wrong CRC gives nothing back");
    tiku_archive_close(a);
    write_fixture("liar_zip", path, sizeof path);
    a = NULL;
    (void)tiku_archive_open(path, NULL, NULL, &a);
    check(a != NULL && tiku_archive_read(a, 0, 1u << 30, &b, &n) ==
          TIKU_ARCHIVE_CHECKSUM && b == NULL, "AR-095",
          "an entry inflating past its stated size is stopped there");
    tiku_archive_close(a);
    check(strcmp(tiku_archive_strerror(TIKU_ARCHIVE_TOO_LARGE),
                 "it is larger than can be read here") == 0, "AR-096",
          "and too large is said in words");
}

/** @brief A folder taken off, one entry chosen, a cancel, metadata, and
 *         a gzip's file made beside one of its name. */
static void
option_tests(void)
{
    char path[PATH_MAX], dest[PATH_MAX], p[PATH_MAX];
    unsigned char which[32] = { 0 };
    tiku_archive_extract_t opt;
    tiku_archive_t *a = NULL;
    struct stat st;
    seen_t s;

    write_fixture("zip_features", path, sizeof path);
    (void)tiku_archive_open(path, NULL, NULL, &a);
    memset(&opt, 0, sizeof opt);
    which[1] = 1;                   /* docs/readme.txt */
    opt.which = which;
    opt.strip = "docs";
    fresh("strip", dest, sizeof dest);
    snprintf(p, sizeof p, "%s/readme.txt", dest);
    check(tiku_archive_extract(a, dest, &opt) == TIKU_ARCHIVE_OK &&
          stat(p, &st) == 0 && st.st_size == 5000 &&
          only(dest, "readme.txt"), "AR-070",
          "one entry chosen, its folder taken off: readme.txt alone");
    memset(&opt, 0, sizeof opt);
    opt.metadata = 1;
    fresh("meta", dest, sizeof dest);
    snprintf(p, sizeof p, "%s/__MACOSX/._readme.txt", dest);
    check(tiku_archive_extract(a, dest, &opt) == TIKU_ARCHIVE_OK &&
          stat(p, &st) == 0, "AR-071", "metadata made when asked for");
    tiku_archive_close(a);
    memset(&s, 0, sizeof s);
    s.cancel_after = 1;
    check(extract_seen("tgz", &s, dest, sizeof dest) ==
          TIKU_ARCHIVE_CANCELLED, "AR-072", "a cancel stops it");
    snprintf(p, sizeof p, "%s/pkg/src.c", dest);
    check(lstat(p, &st) != 0, "AR-073", "and the file it was making goes");
    write_fixture("gz_fields", path, sizeof path);
    (void)tiku_archive_open(path, NULL, NULL, &a);
    fresh("beside", dest, sizeof dest);
    snprintf(p, sizeof p, "%s/notes.txt", dest);
    fclose(fopen(p, "w"));
    snprintf(p, sizeof p, "%s/notes 2.txt", dest);
    check(tiku_archive_extract(a, dest, NULL) == TIKU_ARCHIVE_OK &&
          stat(p, &st) == 0 && st.st_size > 0, "AR-074",
          "a gzip's file beside one of its name: notes 2.txt, never over it");
    tiku_archive_close(a);
}

/** @brief Names for folders, new folders, and files not read here. */
static void
helper_tests(void)
{
    static const struct { const char *path, *want; } names[] = {
        { "/x/pkg-1.2.tar.gz", "pkg-1.2" }, { "a.TGZ", "a" },
        { "b.zip", "b" }, { "c.tar.xz", "c" }, { "notes.txt.gz", "notes.txt" },
        { ".zip", "Archive" }, { "plain", "plain" }
    };
    static const struct { const char *magic; size_t n; const char *what; }
        kinds[] = {
        { "\xfd" "7zXZ\0", 6, "an xz-compressed file" },
        { "BZh91AY&SY", 10, "a bzip2-compressed file" },
        { "7z\xbc\xaf\x27\x1c", 6, "a 7-Zip archive" }
    };
    char out[PATH_MAX], again[PATH_MAX], path[PATH_MAX];
    tiku_archive_t *a = NULL;
    size_t i;
    int ok = 1;

    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        tiku_archive_folder_name(names[i].path, out, sizeof out);
        ok &= strcmp(out, names[i].want) == 0;
    }
    check(ok, "AR-080", "folder names: the archive's suffix taken off");
    check(tiku_archive_new_folder(scratch, "made", out, sizeof out) == 0 &&
          tiku_archive_new_folder(scratch, "made", again, sizeof again) == 0 &&
          strstr(again, "made 2") != NULL, "AR-081",
          "a new folder is always new: 'made', then 'made 2'");
    for (i = 0, ok = 1; i < sizeof kinds / sizeof kinds[0]; i++) {
        FILE *f;

        snprintf(path, sizeof path, "%s/kind%zu", scratch, i);
        f = fopen(path, "wb");
        fwrite(kinds[i].magic, 1, kinds[i].n, f);
        fwrite("0123456789", 1, 10, f);
        fclose(f);
        ok &= tiku_archive_open(path, NULL, NULL, &a) == TIKU_ARCHIVE_NOT_YET
              && tiku_archive_identify(path) != NULL &&
              strcmp(tiku_archive_identify(path), kinds[i].what) == 0;
    }
    check(ok, "AR-082", "xz, bzip2 and 7-Zip: not read yet, and named");
    snprintf(path, sizeof path, "%s/kind0", scratch);
    snprintf(out, sizeof out, "%s/nothing-here", scratch);
    check(tiku_archive_open(out, NULL, NULL, &a) == TIKU_ARCHIVE_MISSING &&
          tiku_archive_open(scratch, NULL, NULL, &a) == TIKU_ARCHIVE_MISSING,
          "AR-083", "no file, or a folder: missing");
}

/** @brief Every group, in a scratch folder removed at the end. */
int
main(void)
{
    const char *tmp = getenv("TMPDIR");
    char cmd[PATH_MAX + 16];

    snprintf(scratch, sizeof scratch, "%s/tiku-archive-XXXXXX",
             tmp != NULL ? tmp : "/tmp");
    if (mkdtemp(scratch) == NULL) {
        perror("mkdtemp");
        return 2;
    }
    printf("Archives: ZIP, tar and gzip read, listed and extracted\n");
    inflate_tests();
    read_all_tests();
    detail_tests();
    hostile_zip_tests();
    hostile_tar_tests();
    climb_tests();
    broken_tests();
    option_tests();
    helper_tests();
    part_tests();
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", scratch);
    (void)system(cmd);
    printf("%s\n", fails ? "FAIL" : "PASS: ZIP, tar and gzip listed and "
           "extracted, the hostile ones kept inside their folder");
    return fails ? 1 : 0;
}
