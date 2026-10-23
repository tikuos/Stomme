/*
 * KintsugiDesktop v0.01 Application Kit.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_clip_rpc.h - acknowledged typed clipboard requests.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CLIP_RPC_H_
#define TIKU_CLIP_RPC_H_
#include "tiku_clipboard.h"
#include "tiku_msg.h"

/** @brief Typed clipboard request/reply message codes. */
#define TIKU_MSG_CLIP_REQUEST 0x636c7271u
#define TIKU_MSG_CLIP_REPLY 0x636c7270u
/** @brief Probe capabilities, read one current snapshot, or publish a pair. */
enum { TIKU_CLIP_PROBE = 1, TIKU_CLIP_GET, TIKU_CLIP_SET };

/** @brief Build a version-1 request with a positive correlation ID.
 *  @note SET copies text, MIME and data; other operations ignore them.
 *        NULL MIME publishes text only. Caller frees the message.
 *  @return Owned message, or NULL for invalid arguments or allocation failure.
 */
tiku_msg_t *tiku_clip_request(int64_t id, int operation, const char *text,
                              const char *mime, const void *data, size_t bytes);
/** @brief Build a version-3 GET for an exact held text representation.
 * @note Requires a PROBE reply with read_format=1. Supported MIME values are
 * text/html, text/rtf and application/rtf. text/rtf may return application/rtf
 * when that alias is held instead. A missing representation returns the text
 * fallback without MIME/data; callers must refuse it when exact format is
 * required. Does not fetch additional formats from the host. Returns an owned
 * message, or NULL on invalid arguments/allocation refusal. */
tiku_msg_t *tiku_clip_request_format(int64_t id, const char *mime);
/** @brief Build a version-2 SET carrying ordered MIME representations.
 *  @note Use only after PROBE advertises formats >= the requested count.
 *        NULL on invalid lengths or allocation failure; caller frees result. */
tiku_msg_t *tiku_clip_request_formats(int64_t id, const char *text,
                                      const tiku_clipboard_format_t *formats,
                                      size_t count);
/** @brief Handle one request against the UI-thread clipboard store.
 *  @note Builds an acknowledgment before attempting SET. A successful SET
 *        reply means the store owns both representations. GET copies a
 *        current snapshot. NULL means refusal before publication.
 *        Caller sends/frees the reply; failure to send must not imply Cut. */
tiku_msg_t *tiku_clip_reply(const tiku_msg_t *request);
#endif
