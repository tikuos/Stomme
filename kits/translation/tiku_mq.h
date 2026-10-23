/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mq.h - the MQ arithmetic decoder of JBIG2 and JPEG 2000: binary
 * decisions, each in an adaptive context, from a stream of bytes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MQ_H_
#define TIKU_MQ_H_

#include <stdint.h>

/** @brief One adaptive context: its probability state (0 to 46) and more
 *         probable symbol.  Zeroed, it is the usual starting state. */
typedef struct {
    unsigned char i, mps;
} tiku_mq_cx_t;

/** @brief The decoder over @p n bytes. */
typedef struct {
    const unsigned char *p;         /* the coded bytes                   */
    long                 n, pos;    /* how many; the one in C's low end  */
    uint32_t             c, a;      /* the code and interval registers   */
    int                  ct;        /* bits left before the next byte    */
    long                 past;      /* bytes of ones fed past the data   */
} tiku_mq_t;

/** @brief The decoder begun on @p n bytes (INITDEC). */
void tiku_mq_init(tiku_mq_t *d, const unsigned char *p, long n);
/** @brief One decision in context @p cx. */
int tiku_mq_bit(tiku_mq_t *d, tiku_mq_cx_t *cx);
/** @brief Whether the decoder ran far past its data: a stream that asks
 *         for more than it holds.
 *  @note A caller decodes no further once this is nonzero. */
int tiku_mq_spent(const tiku_mq_t *d);

#endif /* TIKU_MQ_H_ */
