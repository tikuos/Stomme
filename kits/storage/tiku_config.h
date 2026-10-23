/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_config.h - configuration v1: snapshots of a device's curated
 * settings, and the requests that change them.
 *
 * A curated setting (the device name, the CPU target frequency) is read as a
 * snapshot carrying the device's incarnation and the setting's revision.  A
 * change is a request against that revision, which the device refuses when
 * the setting has moved on; the request goes through any backend.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CONFIG_H_
#define TIKU_CONFIG_H_
#include "tiku_model.h"

/**
 * @brief One curated setting as the device reported it.
 *
 * @note With mode -1 the caller does not fall back to a plain write of the
 *       setting.
 */
typedef struct {
    /* 1 managed, 0 absent, unsupported or unenrolled, -1 advertised but
     * unreadable. */
    int mode;
    int enrollable;                  /* the device is ready to enroll    */
    uint32_t revision;               /* the setting's revision, 0 unset   */
    /* The configuration store's identity (32 hex digits), the setting's
     * state ("unset", "accepted", "applied", "restart" or "blocked") and
     * its desired value as text. */
    char incarnation[33], state[12], desired[33];
} tiku_config_snapshot_t;

/**
 * @brief Ask the device whether it serves configuration v1, and its
 *        incarnation, without reading a setting or enrolling.
 * @return 1 when ready (mode 1); 0 when absent, unsupported or unenrolled
 *         (mode 0, enrollable set for unenrolled); -1 when the status
 *         cannot be read (mode -1).
 */
int tiku_config_probe(tiku_backend_t *, tiku_config_snapshot_t *);

/**
 * @brief Snapshot a curated setting: /sys/device/name or
 *        /sys/cpu/freq_target, read through /sys/config.  Nothing is
 *        enrolled or written.
 * @note Take the snapshot before showing the value.
 * @return 1 with the snapshot; 0 for a path that is not curated or a device
 *         without configuration; -1 on error.
 */
int tiku_config_read(tiku_backend_t *, const char *resource_path,
                     tiku_config_snapshot_t *);

/** @brief Parse a setting's wire form,
 *         "v1:<incarnation>:<revision>:<state>:<hex value>".
 *  @return 0 with the snapshot (mode 1), or -1 for anything malformed. */
int tiku_config_parse(const char *, tiku_config_snapshot_t *);

/**
 * @brief Build the request that sets a value of 1 to 32 printable bytes
 *        against a snapshot, under @p token:
 *        "<incarnation>:<revision>:<token>:<hex value>:<crc32>".
 * @return Its length, or -1.
 */
int tiku_config_request(const tiku_config_snapshot_t *, const uint8_t token[16],
                        const char *, size_t, char *out, size_t max);

/** @brief Decode and verify a request tiku_config_request() built: the
 *         snapshot takes its incarnation, revision and value, @p token and
 *         @p value its token in hex and the value as text.
 *  @return 0, or -1 for a malformed request or a bad checksum. */
int tiku_config_decode_request(const char *, tiku_config_snapshot_t *,
                               char token[33], char value[33]);

/** @brief Sixteen bytes from /dev/urandom, not all zero.
 *  @return 0, or -1 when they cannot be read; no weaker source is used. */
int tiku_config_token(uint8_t out[16]);

/** @brief Send @p request as it is, its revision unchanged, once the
 *         device's incarnation is checked against it.
 *  @return 0, or -1 with @p error filled for the user. */
int tiku_config_send(tiku_backend_t *, const char *resource_path,
                     const char *request, char *error, size_t max);

/**
 * @brief Build a request from a snapshot under a fresh token and send it,
 *        without keeping it.
 * @note An interactive client uses tiku_config_outbox_save() instead, which
 *       keeps the request across a host crash and checks the device's
 *       receipts.
 * @return 0, or -1 with @p error filled.
 */
int tiku_config_write(tiku_backend_t *, const char *resource_path,
                      const tiku_config_snapshot_t *, const char *, size_t,
                      char *error, size_t max);
#endif
