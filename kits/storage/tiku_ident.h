/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ident.h - the one device identity, keyed on by everything durable.
 *
 * A device is recognised by a scored match over the identity nodes it
 * publishes under /sys/device.  Every store keys on the string
 * tiku_ident_key() produces, so there is one spelling of "which board is
 * this".
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IDENT_H_
#define TIKU_IDENT_H_

#include <stddef.h>

#define TIKU_DEVID_MAX 40

/** @brief The identity facts read from a device, any of them possibly empty. */
typedef struct {
    char uid[40];      /* /sys/device/uid  -- a die identifier where present */
    char id[40];       /* /sys/device/id                                     */
    char mcu[32];      /* /sys/device/mcu                                    */
    char name[40];     /* /sys/device/name -- user-settable, so weak         */
} tiku_identity_t;

/** @brief Weights: a uid alone binds; without one, id+mcu+name together do. */
#define TIKU_W_UID   8
#define TIKU_W_ID    4
#define TIKU_W_MCU   2
#define TIKU_W_NAME  1
#define TIKU_BIND    7

/**
 * @brief Canonical device key: "uid-<uid>" when the device has a uid, else
 *        "prov-<hash>" over id, mcu and name.
 *
 * @note A provisional key cannot tell two identical boards apart; the
 *       caller says so where it matters (tiku_ident_provisional()).
 * @return The key's length as snprintf() counts it; 0 for a NULL argument.
 */
int tiku_ident_key(const tiku_identity_t *id, char *out, size_t max);

/** @brief Whether the device has no uid, so its key is the provisional
 *         hash. */
int tiku_ident_provisional(const tiku_identity_t *id);

/**
 * @brief Score @p seen against a remembered @p known identity.
 *
 * Two identities whose uids differ score -1, however much else agrees.
 *
 * @return 0..15, or -1 for the veto.
 */
int tiku_ident_score(const tiku_identity_t *known,
                         const tiku_identity_t *seen);

/** @brief Whether @p seen should be treated as @p known. */
int tiku_ident_matches(const tiku_identity_t *known,
                           const tiku_identity_t *seen);

#endif /* TIKU_IDENT_H_ */
