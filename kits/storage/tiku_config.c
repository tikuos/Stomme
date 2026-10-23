/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_config.c - the configuration v1 client: the wire codec, probing and
 * reading a setting, and sending a request.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_config.h"
#include <stdio.h>
#include <string.h>

/** @brief The /sys/config node behind a curated setting's path, or
 *         NULL. */
static const char *endpoint(const char *path)
{
    if (!path) return NULL;
    if (!strcmp(path, "/sys/device/name")) return "/sys/config/name";
    if (!strcmp(path, "/sys/cpu/freq_target")) return "/sys/config/frequency";
    return NULL;
}
/** @brief The value of a lower-case hex digit, or -1. */
static int digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
/** @brief Whether the first @p n characters of @p s are lower-case hex. */
static int hex(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) if (digit(s[i]) < 0) return 0;
    return 1;
}
/** @brief The CRC-32 (reflected, polynomial 0xedb88320) of @p n bytes. */
static uint32_t checksum(const char *s, size_t n)
{
    uint32_t crc = UINT32_MAX;
    while (n--) {
        crc ^= (unsigned char)*s++;
        for (unsigned i = 0; i < 8; i++) crc = (crc >> 1) ^
            ((0u - (crc & 1u)) & 0xedb88320u);
    }
    return ~crc;
}
int tiku_config_parse(const char *s, tiku_config_snapshot_t *out)
{
    const char *end, *value;
    size_t len, state_len;
    if (!out) return -1;
    memset(out, 0, sizeof *out); out->mode = -1;
    if (!s || strlen(s) < 47 || strncmp(s, "v1:", 3) || s[35] != ':' ||
        s[44] != ':' || !hex(s+3, 32) || !hex(s+36, 8)) return -1;
    end = strchr(s+45, ':');
    if (!end) return -1;
    state_len = (size_t)(end - (s+45));
    if (!state_len || state_len >= sizeof out->state) return -1;
    memcpy(out->state, s+45, state_len);
    if (strcmp(out->state, "unset") && strcmp(out->state, "accepted") &&
        strcmp(out->state, "applied") && strcmp(out->state, "restart") &&
        strcmp(out->state, "blocked")) return -1;
    value = end+1; len = strcspn(value, "\r\n");
    if (len > 64 || (len & 1u) || !hex(value, len) ||
        (value[len] && strcmp(value+len, "\n") && strcmp(value+len, "\r\n"))) return -1;
    for (size_t i = 0; i < len/2; i++) {
        out->desired[i] = (char)(digit(value[2*i])*16 + digit(value[2*i+1]));
        if ((unsigned char)out->desired[i] < 32 || out->desired[i] == 127) return -1;
    }
    memcpy(out->incarnation, s+3, 32);
    if (!strcmp(out->incarnation, "00000000000000000000000000000000")) return -1;
    for (size_t i = 36; i < 44; i++) out->revision =
        out->revision * 16u + (unsigned)digit(s[i]);
    if ((!out->revision) != !strcmp(out->state, "unset") ||
        (!out->revision && len) || (out->revision && !len)) return -1;
    out->mode = 1; return 0;
}
int tiku_config_request(const tiku_config_snapshot_t *s, const uint8_t token[16],
                        const char *value, size_t n, char *out, size_t max)
{
    static const char digits[] = "0123456789abcdef";
    char t[33], v[65];
    unsigned any = 0;
    int rc;
    if (!s || s->mode != 1 || !token || !value || !n || n > 32 || !out ||
        strlen(s->incarnation) != 32 || !hex(s->incarnation, 32)) return -1;
    for (size_t i = 0; i < 16; i++) {
        any |= token[i]; t[2*i] = digits[token[i] >> 4]; t[2*i+1] = digits[token[i] & 15];
    }
    if (!any) return -1;
    t[32] = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned c = (unsigned char)value[i];
        if (c < 32 || c == 127) return -1;
        v[2*i] = digits[c >> 4]; v[2*i+1] = digits[c & 15];
    }
    v[2*n] = 0;
    rc = snprintf(out, max, "%s:%08lx:%s:%s", s->incarnation,
                   (unsigned long)s->revision, t, v);
    if (rc < 0 || (size_t)rc >= max) return -1;
    int tail = snprintf(out + rc, max - (size_t)rc, ":%08lx",
        (unsigned long)checksum(out, (size_t)rc));
    return tail == 9 && (size_t)rc + 9 < max ? rc + 9 : -1;
}
/** @brief Read @p path into @p out as a string.  @return Its length, or -1
 *         when it is empty, too long or holds a NUL. */
static int scalar(tiku_backend_t *b, const char *path, char *out, size_t max)
{
    int n = b->ops->read(b, path, out, max);
    if (n <= 0 || (size_t)n >= max || memchr(out, 0, (size_t)n)) return -1;
    out[n] = 0;
    return n;
}
int tiku_config_probe(tiku_backend_t *b, tiku_config_snapshot_t *out)
{
    char status[128];
    tiku_model_t model;
    if (!out) return -1;
    memset(out, 0, sizeof *out);
    if (!b || !b->ops || !b->ops->stat || !b->ops->read) { out->mode = -1; return -1; }
    if (b->ops->stat(b, "/sys/config/status", &model)) return 0;
    out->mode = -1;
    if (scalar(b, "/sys/config/status", status, sizeof status) < 0) return -1;
    size_t n = strcspn(status, "\r\n");
    if (status[n] && strcmp(status+n, "\n") && strcmp(status+n, "\r\n")) return -1;
    status[n] = 0;
    if (!strcmp(status, "v1:unsupported:-") || !strcmp(status, "v1:unenrolled:-")) {
        out->mode = 0;
        out->enrollable = !strcmp(status, "v1:unenrolled:-");
        return 0;
    }
    if (strncmp(status, "v1:ready:", 9) || strlen(status) != 41 ||
        !hex(status+9, 32) ||
        !strcmp(status+9, "00000000000000000000000000000000")) return -1;
    memcpy(out->incarnation, status+9, 33); out->mode = 1;
    return 1;
}
int tiku_config_read(tiku_backend_t *b, const char *path,
                     tiku_config_snapshot_t *out)
{
    tiku_config_snapshot_t probe;
    char value[160];
    const char *ep = endpoint(path);
    int rc;
    if (!out) return -1;
    memset(out, 0, sizeof *out);
    if (!ep) return 0;
    rc = tiku_config_probe(b, &probe); *out = probe;
    if (rc != 1) return rc;
    out->mode = -1;
    if (scalar(b, ep, value, sizeof value) < 0 ||
        tiku_config_parse(value, out)) return -1;
    if (strcmp(out->incarnation, probe.incarnation)) {
        out->mode = -1; return -1;
    }
    return 1;
}
int tiku_config_token(uint8_t token[16])
{
    FILE *random;
    size_t n;
    unsigned any = 0;
    if (!token) return -1;
    random = fopen("/dev/urandom", "rb");
    n = random ? fread(token, 1, 16, random) : 0;
    if (random) fclose(random);
    for (unsigned i = 0; i < 16 && i < n; i++) any |= token[i];
    return n == 16 && any ? 0 : -1;
}
int tiku_config_decode_request(const char *wire, tiku_config_snapshot_t *out,
                               char token[33], char value[33])
{
    char state[160];
    size_t n;
    uint32_t crc = 0;
    if (!wire || !out || !token || !value) return -1;
    n = strlen(wire);
    if (n < 86 || n > 148 || wire[32] != ':' || wire[41] != ':' ||
        wire[74] != ':' || wire[n-9] != ':' || !hex(wire, 32) ||
        !hex(wire+33, 8) || !hex(wire+42, 32) || !hex(wire+n-8, 8))
        return -1;
    for (size_t i = n-8; i < n; i++) crc = crc*16u + (unsigned)digit(wire[i]);
    if (crc != checksum(wire, n-9)) return -1;
    /* The value is checked by tiku_config_parse(), under a placeholder
     * revision of 1. */
    snprintf(state, sizeof state, "v1:%.32s:00000001:accepted:%.*s",
             wire, (int)(n-84), wire+75);
    if (tiku_config_parse(state, out)) return -1;
    out->revision = 0;
    for (size_t i = 33; i < 41; i++) out->revision =
        out->revision*16u + (unsigned)digit(wire[i]);
    if (out->revision == UINT32_MAX) return -1;
    memcpy(token, wire+42, 32); token[32] = 0;
    if (!strcmp(token, "00000000000000000000000000000000")) return -1;
    memcpy(value, out->desired, 33);
    return 0;
}
int tiku_config_send(tiku_backend_t *b, const char *path, const char *request,
                     char *error, size_t max)
{
    char transport_value[160], token[33], value[33];
    tiku_config_snapshot_t expected, current;
    const char *ep = endpoint(path);
    if (!ep || !b || !b->ops || !b->ops->write ||
        tiku_config_decode_request(request, &expected, token, value) ||
        tiku_config_read(b, path, &current) != 1 ||
        strcmp(expected.incarnation, current.incarnation)) {
        snprintf(error, max, "Recovery state changed. Refresh before saving.");
        return -1;
    }
    /* The desktop's RPC proxy checks a write against the bytes it last
     * read, so the endpoint is read first.  The request keeps its own
     * revision, which the device checks. */
    if (!b->ops->read || scalar(b, ep, transport_value, sizeof transport_value) < 0) {
        snprintf(error, max, "Recovery state unavailable. Refresh before saving."); return -1;
    }
    if (b->ops->write(b, ep, request, strlen(request), error, max)) {
        /* No retry and no new revision: the outbox keeps the exact request
         * and matches the device's receipts when an acknowledgement is
         * lost. */
        if (strstr(error, "ECONFLICT") || strstr(error, "ESTALE"))
            snprintf(error, max, "Device configuration changed. Refresh before saving.");
        else snprintf(error, max, "Save not confirmed. Refresh to inspect device state.");
        return -1;
    }
    return 0;
}
int tiku_config_write(tiku_backend_t *b, const char *path,
                      const tiku_config_snapshot_t *snapshot,
                      const char *value, size_t length, char *error, size_t max)
{
    uint8_t token[16];
    char request[160];
    if (tiku_config_token(token) ||
        tiku_config_request(snapshot, token, value, length,
                            request, sizeof request) < 0) {
        snprintf(error, max, "Could not prepare a safe configuration request.");
        return -1;
    }
    return tiku_config_send(b, path, request, error, max);
}
