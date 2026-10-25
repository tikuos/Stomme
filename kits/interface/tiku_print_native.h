/*
 * KintsugiDesktop v0.01 Interface Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_PRINT_NATIVE_H_
#define TIKU_PRINT_NATIVE_H_
#include <stddef.h>
#include <stdint.h>
#define TIKU_PRINT_PDF_MAX (128u * 1024u * 1024u)
typedef struct tiku_print_native tiku_print_native_t;
/** @brief Native completion: submitted=1, cancelled=0, failed=-1. */
typedef void (*tiku_print_done_fn)(void *context, uint64_t request, int status,
                                   const char *reason);
/** @brief Open a native print service for the supplied native parent window.
 * Linux uses a portal parent identifier; Cocoa uses the NSWindow pointer.
 * The parent remains valid until close. Returns NULL on allocation refusal. */
tiku_print_native_t *tiku_print_native_open(const char *parent,
                                            void *native_window);
/** @brief Queue an owned PDF copy and native print dialog without blocking.
 * Requires a unique nonzero request, valid PDF prefix and UTF-8 title <=255
 * bytes. At most four jobs and 256 MiB of copied PDF are retained. Completion
 * runs from poll, never the worker. Returns one on queue acceptance. */
int tiku_print_native_begin(tiku_print_native_t *native, uint64_t request,
                            const void *pdf, size_t bytes, const char *title,
                            tiku_print_done_fn done, void *context);
/** @brief Cancel a queued or displayed request; completion reports
 * cancellation. */
void tiku_print_native_cancel(tiku_print_native_t *native, uint64_t request);
/** @brief Deliver completed jobs and release their copied PDF storage. */
void tiku_print_native_poll(tiku_print_native_t *native);
/** @brief Cancel requests and invalidate callbacks; captured resources survive
 * until native work stops. */
void tiku_print_native_close(tiku_print_native_t *native);
/** @brief Copied native job metadata; status is known after completion. */
typedef struct {
    uint64_t request;
    size_t bytes;
    int started, cancellation_requested, finished, status;
    char title[256], reason[256];
} tiku_print_job_observation_t;
/** @brief Copy current native jobs on the owner thread; return required count. */
size_t tiku_print_native_observe(tiku_print_native_t *,tiku_print_job_observation_t *,size_t capacity);

#endif
