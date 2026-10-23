/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_tx.h - transport seam: bytes to and from one device.
 *
 * Serial, TCP, and a custom one over a caller's own descriptor and hooks.
 * Nothing above this header knows which one it is talking to.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TX_H_
#define TIKU_TX_H_

#include <stddef.h>

/** @brief Transport handle.  Opaque; created by one of the openers below. */
typedef struct tiku_tx tiku_tx_t;

/**
 * @brief Open a serial device.
 *
 * @param path  Device node, e.g. "/dev/ttyACM0" or a /dev/serial/by-id link.
 * @param baud  Line rate (9600 on MSP430, 115200 elsewhere).
 * @return Handle, or NULL (errno set).
 */
tiku_tx_t *tiku_tx_open_serial(const char *path, int baud);

/**
 * @brief Open a TCP connection (the telnet-shell channel).
 *
 * @param host  Dotted-quad or hostname.
 * @param port  TCP port.
 * @return Handle, or NULL (errno set).
 * @note Resolving the name and connecting may each take seconds: never
 *       call this on a thread that draws.
 */
tiku_tx_t *tiku_tx_open_tcp(const char *host, int port);

/** @brief A custom transport's write: @p len bytes to the device;
 *         @return len, or -1 when the way is gone. */
typedef int (*tiku_tx_write_fn)(const void *buf, size_t len, void *ctx);

/** @brief A custom transport's pump: whatever moves bytes onto @p fd. */
typedef void (*tiku_tx_pump_fn)(void *ctx);

/**
 * @brief A transport over what a caller already carries: bytes back
 *        arrive on @p fd, which stays the caller's and is not closed with
 *        the transport; bytes out go through @p write; @p pump runs
 *        while a read waits, so the medium is serviced by whoever is
 *        blocked on it.  The shell of a board over a link is one.
 *
 * @param name  What to call it, for window titles and logs.
 * @return Handle, or NULL (errno set).
 */
tiku_tx_t *tiku_tx_open_custom(int fd, tiku_tx_write_fn write,
                               tiku_tx_pump_fn pump, void *ctx,
                               const char *name);

/** @brief A custom transport's release: it is closing, and what @p ctx
 *         holds for it may go. */
typedef void (*tiku_tx_release_fn)(void *ctx);

/** @brief Run @p release with a custom transport's ctx as it closes, so
 *         what it reads from outlives every read; no effect on another
 *         kind. */
void tiku_tx_on_close(tiku_tx_t *tx, tiku_tx_release_fn release);

/**
 * @brief Read whatever has arrived, waiting at most @p timeout_ms.
 *
 * @return Bytes read (0 on timeout), or -1 on a broken link.
 */
int tiku_tx_read(tiku_tx_t *tx, void *buf, size_t max,
                      int timeout_ms);

/** @brief Write @p len bytes; -1 on a broken link. */
int tiku_tx_write(tiku_tx_t *tx, const void *buf, size_t len);

/** @brief Reopen the same endpoint after a replug.  0 on success; a
 *         custom transport has no endpoint of its own and stays down. */
int tiku_tx_reopen(tiku_tx_t *tx);

/** @brief Human-readable endpoint name, for window titles and logs. */
const char *tiku_tx_name(const tiku_tx_t *tx);

/** @brief Close the transport; a custom one's descriptor stays open, as
 *         the caller's. */
void tiku_tx_close(tiku_tx_t *tx);

/**
 * @brief The serial ports a board could be on, as tiku_sys_serial_ports()
 *        lists them for this host.
 *
 * Fills @p out with up to @p max NUL-terminated paths.
 * @return Count found, or -1 when this host has no such list.
 */
int tiku_tx_scan_serial(char out[][256], int max);

#endif /* TIKU_TX_H_ */
