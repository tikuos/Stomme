/* Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_term_stream.h - nonblocking shell stream supplied by the application host.
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_TERM_STREAM_H
#define TIKU_TERM_STREAM_H
#include <stddef.h>

/** Claim a shell stream. Return its handle or -1 when unavailable. */
int tiku_term_stream_open(void);
/** Read up to size bytes. Return zero on hangup or -1 with errno on refusal. */
int tiku_term_stream_read(int handle, char *bytes, size_t size);
/** Submit one key or committed text. The host reports refused input to the user. */
void tiku_term_stream_write(int handle, const char *bytes, size_t size);
/** Cancel this stream's input and release its shell. */
void tiku_term_stream_close(int handle);
#endif
