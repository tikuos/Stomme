/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * test_zip_write.c - stored ZIP output and bounded package validation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_zip_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL zip write:%d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

/** @brief Read a little-endian field from a generated directory. */
static unsigned
get32(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8 |
           (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

/** @brief Write bounded test-owned bytes into an existing temporary path. */
static void
write_bytes(const char *path, const unsigned char *data, size_t n)
{
    FILE *f = fopen(path, "wb");

    CHECK(f && fwrite(data, 1, n, f) == n && !fclose(f));
}

/** @brief Invalid names, duplicates and oversized inputs emit no bytes. */
static void
refusals(void)
{
    const char *names[] = {"", "/abs", "../file", "a/../b", "./a", "a//b",
        "a/", "a\\b", "x:y", "a\nb", "space name", "[Other].xml", "a/[Content_Types].xml"};
    tiku_zip_entry_t entries[2] = {{"a", "a", 1}, {"a", "b", 1}};
    FILE *f = tmpfile();
    size_t i;

    CHECK(f);
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        entries[0].name = names[i];
        CHECK(tiku_zip_write(f, entries, 1) == TIKU_ARCHIVE_UNSAFE);
        CHECK(ftell(f) == 0);
    }
    entries[0].name = "a";
    CHECK(tiku_zip_write(f, entries, 2) == TIKU_ARCHIVE_REPLACED);
    entries[0].length = UINT32_MAX;
    CHECK(tiku_zip_write(f, entries, 1) == TIKU_ARCHIVE_TOO_LARGE);
    CHECK(tiku_zip_write(f, entries, 1025) == TIKU_ARCHIVE_TOO_LARGE);
    CHECK(ftell(f) == 0);
    CHECK(!fclose(f));
}

/** @brief Central-directory limits and strict local-header matching hold. */
static void
strict_packages(const char *path, unsigned char *valid, size_t n)
{
    tiku_archive_t *a = (void *)1;
    unsigned cd = get32(valid + n - 6);
    unsigned char *changed = malloc(n + 1);
    size_t i;
    const size_t fields[] = {6, 8, 14, 18, 22, 26, 30};

    CHECK(changed && cd < n);
    CHECK(tiku_archive_open_package(path, n - 1, 4096, 3, &a) ==
          TIKU_ARCHIVE_TOO_LARGE && !a);
    CHECK(tiku_archive_open_package(path, n, 4, 3, &a) ==
          TIKU_ARCHIVE_TOO_LARGE && !a);
    CHECK(tiku_archive_open_package(path, n, 4096, 1, &a) ==
          TIKU_ARCHIVE_TOO_LARGE && !a);
    for (i = 0; i < sizeof fields / sizeof fields[0]; i++) {
        memcpy(changed, valid, n);
        changed[fields[i]] ^= 1;
        write_bytes(path, changed, n);
        CHECK(tiku_archive_open_package(path, n, 4096, 3, &a) ==
              TIKU_ARCHIVE_DAMAGED && !a);
    }
    memcpy(changed, valid, n);
    changed[cd + 46] = '/';
    write_bytes(path, changed, n);
    CHECK(tiku_archive_open_package(path, n, 4096, 3, &a) ==
          TIKU_ARCHIVE_DAMAGED && !a);
    memcpy(changed, valid, n);
    changed[n] = '!';
    write_bytes(path, changed, n + 1);
    CHECK(tiku_archive_open_package(path, n + 1, 4096, 3, &a) ==
          TIKU_ARCHIVE_DAMAGED && !a);
    for (i = 0; i < n; i++) {
        write_bytes(path, valid, i);
        CHECK(tiku_archive_open_package(path, n, 4096, 3, &a) !=
              TIKU_ARCHIVE_OK && !a);
    }
    free(changed);
}

/** @brief Read stored entries using both the package and general readers. */
static void
roundtrip(void)
{
    char path[] = "/tmp/tiku-zip-write-XXXXXX";
    unsigned char bytes[] = {0, 1, 2, 0xff, 0, 17}, *read, *valid;
    tiku_zip_entry_t entries[] = {{"document.xml", "<test/>", 7},
        {"media/a.bin", bytes, sizeof bytes}, {"empty", NULL, 0}};
    int fd = mkstemp(path);
    FILE *f;
    long size;
    tiku_archive_t *a;
    size_t i, n;

    CHECK(fd >= 0);
    f = fdopen(fd, "w+b");
    CHECK(f && tiku_zip_write(f, entries, 3) == TIKU_ARCHIVE_OK);
    CHECK(!fflush(f) && (size = ftell(f)) > 0);
    valid = malloc((size_t)size);
    CHECK(valid);
    rewind(f);
    CHECK(fread(valid, 1, (size_t)size, f) == (size_t)size && !fclose(f));
    CHECK(tiku_archive_open_package(path, (size_t)size, 4096, 3, &a) ==
          TIKU_ARCHIVE_OK);
    CHECK(tiku_archive_count(a) == 3);
    for (i = 0; i < 3; i++) {
        CHECK(!strcmp(tiku_archive_entry(a, (long)i)->path, entries[i].name));
        CHECK(tiku_archive_read(a, (long)i, 32, &read, &n) == TIKU_ARCHIVE_OK);
        CHECK(n == entries[i].length);
        CHECK(!n || !memcmp(read, entries[i].data, n));
        free(read);
    }
    tiku_archive_close(a);
    CHECK(tiku_archive_open(path, NULL, NULL, &a) == TIKU_ARCHIVE_OK);
    tiku_archive_close(a);
    strict_packages(path, valid, (size_t)size);
    free(valid);
    CHECK(!unlink(path));
}

/** @brief Exercise stored ZIP serialization and strict package boundaries. */
int
main(void)
{
    refusals();
    roundtrip();
    puts("ZIP writer: CRC round trips, empty files, names, sizes, headers, "
         "truncation and pre-allocation limits PASS");
    return 0;
}
