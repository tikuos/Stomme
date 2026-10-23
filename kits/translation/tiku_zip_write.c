/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_zip_write.c - checked stored ZIP32 headers and central directory.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_zip_write.h"
#include "tiku_crypto.h"
#include <string.h>

/** @brief Store a little-endian field of two or four bytes. */
static void
put(unsigned char *p, uint32_t value, unsigned count)
{
    while (count--) { *p++ = (unsigned char)value; value >>= 8; }
}

/** @brief Recognize the portable relative-path subset without allocation. */
static int
name_valid(const char *name)
{
    size_t i, start = 0;

    if (!name || !*name) return 0;
    if (!strcmp(name, "[Content_Types].xml"))
        return 1;
    for (i = 0; i <= 240; i++) {
        unsigned c = (unsigned char)name[i];

        if (!c || c == '/') {
            if (i == start || (i - start == 1 && name[start] == '.') ||
                (i - start == 2 && !memcmp(name + start, "..", 2)))
                return 0;
            if (!c) return 1;
            start = i + 1;
        } else if (!((c >= 'a' && c <= 'z') ||
                     (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') ||
                     c == '.' || c == '_' || c == '-')) return 0;
    }
    return 0;
}

/** @brief Validate entry names and sizes before any bytes are written. */
static tiku_archive_err_t
check(const tiku_zip_entry_t *entries, size_t count)
{
    uint64_t bytes = 22;
    size_t i, j;

    if (count > 1024) return TIKU_ARCHIVE_TOO_LARGE;
    if (count && !entries) return TIKU_ARCHIVE_DAMAGED;
    for (i = 0; i < count; i++) {
        if (!name_valid(entries[i].name)) return TIKU_ARCHIVE_UNSAFE;
        if (entries[i].length && !entries[i].data)
            return TIKU_ARCHIVE_DAMAGED;
        if (entries[i].length >= UINT32_MAX) return TIKU_ARCHIVE_TOO_LARGE;
        for (j = 0; j < i; j++)
            if (!strcmp(entries[i].name, entries[j].name))
                return TIKU_ARCHIVE_REPLACED;
        bytes += 76 + strlen(entries[i].name) * 2 + entries[i].length;
        if (bytes >= UINT32_MAX) return TIKU_ARCHIVE_TOO_LARGE;
    }
    return TIKU_ARCHIVE_OK;
}

/** @brief Write one local or central header, including its name. */
static int
header(FILE *out, const tiku_zip_entry_t *e, uint32_t crc,
       uint32_t offset, int central)
{
    unsigned char h[46] = {0};
    size_t n = strlen(e->name), base = central ? 6 : 4;

    put(h, central ? 0x02014b50u : 0x04034b50u, 4);
    if (central) put(h + 4, 20, 2);
    put(h + base, 20, 2);
    put(h + base + 2, 0x800, 2);
    put(h + base + 8, 33, 2); /* DOS date: 1980-01-01. */
    put(h + base + 10, crc, 4);
    put(h + base + 14, (uint32_t)e->length, 4);
    put(h + base + 18, (uint32_t)e->length, 4);
    put(h + base + 22, (uint32_t)n, 2);
    if (central) put(h + 42, offset, 4);
    return fwrite(h, 1, central ? 46 : 30, out) ==
           (size_t)(central ? 46 : 30) && fwrite(e->name, 1, n, out) == n;
}

tiku_archive_err_t
tiku_zip_write(FILE *out, const tiku_zip_entry_t *entries, size_t count)
{
    uint32_t crc[1024], offset = 0, directory, size = 0;
    unsigned char end[22] = {0};
    size_t i;
    tiku_archive_err_t status = check(entries, count);

    if (status != TIKU_ARCHIVE_OK) return status;
    if (!out) return TIKU_ARCHIVE_WRITE;
    for (i = 0; i < count; i++) {
        crc[i] = tiku_crc32(0, entries[i].data, entries[i].length);
        if (!header(out, &entries[i], crc[i], 0, 0) ||
            (entries[i].length && fwrite(entries[i].data, 1,
                entries[i].length, out) != entries[i].length))
            return TIKU_ARCHIVE_WRITE;
        offset += 30 + (uint32_t)strlen(entries[i].name) +
                  (uint32_t)entries[i].length;
    }
    directory = offset;
    offset = 0;
    for (i = 0; i < count; i++) {
        if (!header(out, &entries[i], crc[i], offset, 1))
            return TIKU_ARCHIVE_WRITE;
        offset += 30 + (uint32_t)strlen(entries[i].name) +
                  (uint32_t)entries[i].length;
        size += 46 + (uint32_t)strlen(entries[i].name);
    }
    put(end, 0x06054b50u, 4);
    put(end + 8, (uint32_t)count, 2);
    put(end + 10, (uint32_t)count, 2);
    put(end + 12, size, 4);
    put(end + 16, directory, 4);
    return fwrite(end, 1, sizeof end, out) == sizeof end && !ferror(out)
        ? TIKU_ARCHIVE_OK : TIKU_ARCHIVE_WRITE;
}
