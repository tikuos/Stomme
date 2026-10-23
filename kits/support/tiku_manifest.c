/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_manifest.c - reading an application bundle's manifest.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_manifest.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/** @brief Whether @p s can name a file in the bundle without leaving it:
 *         letters, digits, '.', '_' and '-', and neither "." nor "..". */
static int identifier(const char *s)
{
    if (!*s || !strcmp(s, ".") || !strcmp(s, "..")) return 0;
    for (; *s; s++)
        if (!isalnum((unsigned char)*s) && !strchr("._-", *s)) return 0;
    return 1;
}

int tiku_manifest_read(const char *path, tiku_manifest_t *out,
                       char *error, size_t max)
{
    /* Read into a copy, so a refused file leaves @p out as it was. */
    tiku_manifest_t m = {0};
    struct field { const char *key; char *dst; size_t cap; } fields[] = {
        {"id", m.id, sizeof m.id}, {"name", m.name, sizeof m.name},
        {"module", m.module, sizeof m.module},
        {"category", m.category, sizeof m.category},
        {"icon", m.icon, sizeof m.icon}, {"types", m.types, sizeof m.types},
        {"startup", m.startup, sizeof m.startup},
        {"help", m.help, sizeof m.help}
    };
    FILE *f = fopen(path, "r");
    char line[512];
    unsigned seen = 0;
    int version = 0, ok = f != NULL;
    if (error && max) snprintf(error, max, "Invalid application manifest");
    while (ok && fgets(line, sizeof line, f)) {
        size_t n = strlen(line), i;
        if (!n || (n == sizeof line - 1 && line[n - 1] != '\n')) {
            ok = 0; break;
        }
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (!n || line[0] == '#') continue;
        char *value = strchr(line, '=');
        if (!value) { ok = 0; break; }
        *value++ = 0;
        for (const unsigned char *p = (void *)value; *p; p++)
            if (*p < 32 || *p == 127) ok = 0;
        if (!strcmp(line, "version")) {
            if (version || strcmp(value, "1")) ok = 0;
            version = 1; continue;
        }
        for (i = 0; i < sizeof fields / sizeof fields[0]; i++) {
            if (strcmp(line, fields[i].key)) continue;
            if ((seen & (1u << i)) || strlen(value) >= fields[i].cap) ok = 0;
            else strcpy(fields[i].dst, value);
            seen |= 1u << i; break;
        }
        if (i == sizeof fields / sizeof fields[0]) ok = 0;
    }
    if (f) { if (ferror(f)) ok = 0; fclose(f); }
    size_t n = strlen(m.module);
    if (!version || !identifier(m.id) || !m.name[0] ||
        !identifier(m.module) || n < 4 || strcmp(m.module + n - 3, ".so") ||
        (strcmp(m.category, "applications") && strcmp(m.category, "programming") &&
         strcmp(m.category, "documents") && strcmp(m.category, "graphics") &&
         strcmp(m.category, "utilities") && strcmp(m.category, "development") &&
         strcmp(m.category, "device-tools") && strcmp(m.category, "demos")) ||
        (m.icon[0] && !identifier(m.icon))) ok = 0;
    if (!ok) return -1;
    if (!m.icon[0]) strcpy(m.icon, "application");
    if (error && max) *error = 0;
    *out = m;
    return 0;
}
