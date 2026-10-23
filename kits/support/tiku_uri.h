/*
 * KintsugiDesktop v0.01 Support Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_URI_H_
#define TIKU_URI_H_
/** @brief Validate bounded UTF-8 http, https or mailto targets for native
 * opening. Local fragments, control bytes, malformed escapes and other schemes
 * refuse. Returns one if valid, zero otherwise; input is NUL terminated, at
 * most 2048 bytes. */
int tiku_uri_external(const char *target);
/** @brief Open a validated external URI with the native launcher on a worker.
 * Returns zero after successful dispatch, negative on failure or a 5-second
 * timeout. The target is passed as one argument, without a command interpreter.
 */
int tiku_uri_open(const char *target);
#endif
