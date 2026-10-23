/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_inflate_stream.c - DEFLATE (RFC 1951) decoded as it is read, and
 * Deflate64 beside it: compressed bytes are asked for as they are needed,
 * decoded bytes handed out as asked for, so an archive entry of any size
 * passes through a window of 32 KB, 64 KB for Deflate64.  Codes are
 * looked up ten bits at a time, longer ones finished down a small tree.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_inflate.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FAST_BITS  10               /* bits a code is looked up by        */
#define FAST_SIZE  (1 << FAST_BITS)
#define MAX_BITS   15               /* the longest code DEFLATE has       */
#define LIT_SYMS   288              /* literal/length symbols, fixed table */
#define DIST_SYMS  32               /* distance symbols, Deflate64's      */
#define IN_SIZE    65536            /* compressed bytes read at a time    */

/**
 * @brief A Huffman code as looked up: @c fast by a code's first ten bits,
 *        then @c tree a bit at a time for the codes longer than that.
 *        An entry > 0 is a symbol with its length from bit 9 up; < 0 is
 *        ~node; 0 is no code at all.
 */
typedef struct {
    int16_t fast[FAST_SIZE];
    int16_t tree[2 * LIT_SYMS];
} huff_t;

enum { ST_HEADER, ST_STORED, ST_CODES, ST_DONE, ST_ERROR };

struct tiku_inflater {
    tiku_inflate_fill_t fill;       /* where compressed bytes come from   */
    void          *ctx;
    unsigned char *in;              /* compressed bytes from fill         */
    long           in_pos, in_len;  /* the next one to take; how many     */
    int            in_end;          /* fill said there is no more         */
    int            broken;          /* fill failed: decoding ends, -1     */
    uint64_t       bitbuf;          /* bits taken from in, lowest first   */
    int            bitcnt;
    unsigned char *win;             /* the output's last wsize bytes      */
    long           wsize, wpos;     /* its size; where the next byte goes */
    long           have;            /* bytes of history in it, <= wsize   */
    long           pend;            /* of those, not yet handed out       */
    int            state, last, deflate64;
    long           stored;          /* a stored block's bytes to come     */
    long           copy_len, copy_dist;   /* a match partly copied        */
    huff_t         lit, dist;       /* the current block's codes          */
};

/* ------------------------------------------------------------ bits  */

/** @brief Read more compressed bytes once those held are used.
 *  @return nonzero while there are bytes to take. */
static int
more(tiku_inflater_t *z)
{
    long n;

    if (z->in_pos < z->in_len) {
        return 1;
    }
    if (z->in_end) {
        return 0;
    }
    n = z->fill(z->ctx, z->in, IN_SIZE);
    if (n <= 0) {
        z->in_end = 1;
        z->broken = n < 0;
        return 0;
    }
    z->in_pos = 0;
    z->in_len = n;
    return 1;
}

/** @brief Top the bit buffer up past 56 bits, or to the input's end. */
static void
refill(tiku_inflater_t *z)
{
    while (z->bitcnt <= 56 && more(z)) {
        z->bitbuf |= (uint64_t)z->in[z->in_pos++] << z->bitcnt;
        z->bitcnt += 8;
    }
}

/** @brief Take @p n bits, at most 16.  @return them, or -1 past the end. */
static long
bits(tiku_inflater_t *z, int n)
{
    long v;

    if (z->bitcnt < n) {
        refill(z);
        if (z->bitcnt < n) {
            return -1;
        }
    }
    v = (long)(z->bitbuf & ((1u << n) - 1u));
    z->bitbuf >>= n;
    z->bitcnt -= n;
    return v;
}

/* --------------------------------------------------------- Huffman  */

/** @brief The low @p len bits of @p code, reversed: DEFLATE sends a
 *         code's first bit lowest. */
static unsigned
reverse(unsigned code, int len)
{
    unsigned r = 0;

    while (len-- > 0) {
        r = (r << 1) | (code & 1u);
        code >>= 1;
    }
    return r;
}

/** @brief Hang a code longer than FAST_BITS off @p h's tree.
 *  @return 0, or -1 when two codes collide or the tree is full. */
static int
hang(huff_t *h, unsigned rev, int len, int sym, int *nodes)
{
    int16_t *slot = &h->fast[rev & (FAST_SIZE - 1)];
    int b;

    for (b = FAST_BITS; b < len; b++) {
        if (*slot > 0) {
            return -1;
        }
        if (*slot == 0) {
            if (*nodes >= LIT_SYMS) {
                return -1;
            }
            *slot = (int16_t)~(*nodes)++;
        }
        slot = &h->tree[2 * ~*slot + ((rev >> b) & 1u)];
    }
    if (*slot != 0) {
        return -1;
    }
    *slot = (int16_t)(sym | (len << 9));
    return 0;
}

/** @brief The canonical codes for @p n symbols' @p length[] into @p h.
 *  @return 0 for a complete or empty code, 1 for a lone code, -1 for any
 *          other incomplete code or an over-subscribed or broken one. */
static int
build(huff_t *h, const unsigned char *length, int n)
{
    int count[MAX_BITS + 1] = {0}, next[MAX_BITS + 2];
    int len, sym, left = 1, nodes = 0, used = 0;

    memset(h, 0, sizeof *h);
    for (sym = 0; sym < n; sym++) {
        count[length[sym]]++;
    }
    for (len = 1; len <= MAX_BITS; len++) {
        left = (left << 1) - count[len];
        used += count[len];
        if (left < 0) {
            return -1;
        }
    }
    next[1] = 0;
    for (len = 1; len <= MAX_BITS; len++) {
        next[len + 1] = (next[len] + count[len]) << 1;
    }
    for (sym = 0; sym < n; sym++) {
        unsigned rev;

        if ((len = length[sym]) == 0) {
            continue;
        }
        rev = reverse((unsigned)next[len]++, len);
        if (len <= FAST_BITS) {
            for (; rev < FAST_SIZE; rev += 1u << len) {
                h->fast[rev] = (int16_t)(sym | (len << 9));
            }
        } else if (hang(h, rev, len, sym, &nodes) != 0) {
            return -1;
        }
    }
    return left == 0 || used == 0 ? 0 : (used == 1 ? 1 : -1);
}

/** @brief The next symbol under @p h.  @return it, or -1 for a code the
 *         table does not have or a stream cut short. */
static int
decode(tiku_inflater_t *z, const huff_t *h)
{
    int e, len, b = FAST_BITS;

    if (z->bitcnt < MAX_BITS) {
        refill(z);
    }
    e = h->fast[z->bitbuf & (FAST_SIZE - 1)];
    while (e < 0) {
        if (b >= MAX_BITS) {
            return -1;
        }
        e = h->tree[2 * ~e + (int)((z->bitbuf >> b++) & 1u)];
    }
    len = e >> 9;
    if (e == 0 || len > z->bitcnt) {
        return -1;
    }
    z->bitbuf >>= len;
    z->bitcnt -= len;
    return e & 511;
}

/* ----------------------------------------------------------- blocks  */

/** @brief The fixed codes of RFC 1951 section 3.2.6. */
static void
fixed_codes(tiku_inflater_t *z)
{
    unsigned char length[LIT_SYMS];

    memset(length, 8, 144);
    memset(length + 144, 9, 112);
    memset(length + 256, 7, 24);
    memset(length + 280, 8, 8);
    (void)build(&z->lit, length, LIT_SYMS);
    memset(length, 5, DIST_SYMS);
    (void)build(&z->dist, length, DIST_SYMS);
}

/** @brief The code lengths a dynamic block's code-length code spells:
 *         @p total of them into @p length.  @return 0, or -1. */
static int
code_lengths(tiku_inflater_t *z, const huff_t *clen, unsigned char *length,
             int total)
{
    int at = 0;

    while (at < total) {
        int sym = decode(z, clen), fill = 0;
        long rep;

        if (sym < 0) {
            return -1;
        }
        if (sym < 16) {
            length[at++] = (unsigned char)sym;
            continue;
        }
        if (sym == 16) {
            if (at == 0) {
                return -1;          /* nothing to repeat */
            }
            fill = length[at - 1];
        }
        rep = bits(z, sym == 16 ? 2 : sym == 17 ? 3 : 7);
        if (rep < 0) {
            return -1;
        }
        rep += sym == 18 ? 11 : 3;
        if (at + rep > total) {
            return -1;
        }
        memset(length + at, fill, (size_t)rep);
        at += (int)rep;
    }
    return 0;
}

/** @brief A dynamic block's tables (RFC 1951 section 3.2.7).
 *  @return 0, or -1 for a malformed header. */
static int
dynamic_codes(tiku_inflater_t *z)
{
    static const unsigned char order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };
    unsigned char length[LIT_SYMS + DIST_SYMS];
    long nlit = bits(z, 5), ndist = bits(z, 5), ncode = bits(z, 4);
    int i, err;

    if (nlit < 0 || ndist < 0 || ncode < 0) {
        return -1;
    }
    nlit += 257;
    ndist += 1;
    if (nlit > 286 || ndist > (z->deflate64 ? 32 : 30)) {
        return -1;
    }
    memset(length, 0, 19);
    for (i = 0; i < ncode + 4; i++) {
        long v = bits(z, 3);

        if (v < 0) {
            return -1;
        }
        length[order[i]] = (unsigned char)v;
    }
    if (build(&z->lit, length, 19) != 0 ||
        code_lengths(z, &z->lit, length, (int)(nlit + ndist)) != 0 ||
        length[256] == 0) {
        return -1;          /* a bad length code, or no end-of-block code */
    }
    err = build(&z->lit, length, (int)nlit);
    if (err < 0) {
        return -1;
    }
    err = build(&z->dist, length + nlit, (int)ndist);
    return err < 0 ? -1 : 0;
}

/** @brief A block's header: its kind, and its codes or its length.
 *  @return 0, or -1 for a malformed one. */
static int
header(tiku_inflater_t *z)
{
    long final = bits(z, 1), type = bits(z, 2), len, nlen;

    if (final < 0 || type < 0) {
        return -1;
    }
    z->last = (int)final;
    if (type == 1) {
        fixed_codes(z);
        z->state = ST_CODES;
        return 0;
    }
    if (type == 2) {
        z->state = ST_CODES;
        return dynamic_codes(z);
    }
    if (type != 0) {
        return -1;                  /* type 3 is reserved */
    }
    (void)bits(z, z->bitcnt & 7);   /* stored blocks start on a byte */
    len = bits(z, 16);
    nlen = bits(z, 16);
    if (len < 0 || nlen < 0 || len != (~nlen & 0xffff)) {
        return -1;                  /* LEN and NLEN disagree */
    }
    z->stored = len;
    z->state = ST_STORED;
    return 0;
}

/** @brief Put @p c in the window, to be handed out. */
static void
put(tiku_inflater_t *z, unsigned char c)
{
    z->win[z->wpos] = c;
    z->wpos = (z->wpos + 1) & (z->wsize - 1);
    z->pend++;
    if (z->have < z->wsize) {
        z->have++;
    }
}

/** @brief Up to @p room bytes of a stored block: first those already in
 *         the bit buffer, then runs straight from the input.
 *  @return how many. */
static long
stored(tiku_inflater_t *z, long room)
{
    long done = 0;

    while (done < room && z->stored > 0 && z->bitcnt >= 8) {
        put(z, (unsigned char)z->bitbuf);
        z->bitbuf >>= 8;
        z->bitcnt -= 8;
        z->stored--;
        done++;
    }
    while (done < room && z->stored > 0 && more(z)) {
        long n = z->in_len - z->in_pos, space = z->wsize - z->wpos;

        n = n < room - done ? n : room - done;
        n = n < z->stored ? n : z->stored;
        n = n < space ? n : space;
        memcpy(z->win + z->wpos, z->in + z->in_pos, (size_t)n);
        z->in_pos += n;
        z->wpos = (z->wpos + n) & (z->wsize - 1);
        z->pend += n;
        z->have = z->have + n < z->wsize ? z->have + n : z->wsize;
        z->stored -= n;
        done += n;
    }
    if (z->stored == 0) {
        z->state = ST_HEADER;
    } else if (done < room) {
        z->state = ST_ERROR;        /* the input ended inside the block */
    }
    return done;
}

/** @brief The length and distance after length symbol @p sym, into the
 *         match being copied.  @return 0, or -1 for a bad pair. */
static int
match(tiku_inflater_t *z, int sym)
{
    static const unsigned short lbase[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51,
        59, 67, 83, 99, 115, 131, 163, 195, 227, 258
    };
    static const unsigned char lext[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4,
        4, 5, 5, 5, 5, 0
    };
    static const unsigned dbase[32] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385,
        513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385,
        24577, 32769, 49153
    };
    int dsym;
    long e;

    sym -= 257;
    if (sym >= 29) {
        return -1;
    }
    /* Deflate64's last length code carries sixteen bits from 3. */
    e = bits(z, z->deflate64 && sym == 28 ? 16 : lext[sym]);
    if (e < 0) {
        return -1;
    }
    z->copy_len = z->deflate64 && sym == 28 ? e + 3 : lbase[sym] + e;
    dsym = decode(z, &z->dist);
    if (dsym < 0 || dsym >= (z->deflate64 ? 32 : 30)) {
        return -1;
    }
    e = bits(z, dsym < 4 ? 0 : (dsym - 2) >> 1);
    if (e < 0) {
        return -1;
    }
    z->copy_dist = (long)dbase[dsym] + e;
    /* A distance past the history the window holds is refused: those
     * window bytes were never written. */
    return z->copy_dist > z->have ? -1 : 0;
}

/** @brief Up to @p room bytes of a coded block.  @return how many. */
static long
coded(tiku_inflater_t *z, long room)
{
    long done = 0;

    while (done < room) {
        int sym;

        if (z->copy_len > 0) {
            unsigned char c = z->win[(z->wpos - z->copy_dist) &
                                     (z->wsize - 1)];

            put(z, c);
            z->copy_len--;
            done++;
            continue;
        }
        sym = decode(z, &z->lit);
        if (sym < 0) {
            z->state = ST_ERROR;
            return done;
        }
        if (sym < 256) {
            put(z, (unsigned char)sym);
            done++;
        } else if (sym == 256) {
            z->state = ST_HEADER;
            return done;
        } else if (match(z, sym) != 0) {
            z->state = ST_ERROR;
            return done;
        }
    }
    return done;
}

/** @brief Decode until the window holds all it may without losing bytes
 *         not handed out, or the stream stops. */
static void
produce(tiku_inflater_t *z)
{
    while (z->pend < z->wsize) {
        long room = z->wsize - z->pend;

        if (z->broken) {
            z->state = ST_ERROR;
        }
        if (z->state == ST_HEADER) {
            if (z->last) {
                (void)bits(z, z->bitcnt & 7);   /* to a byte boundary */
                z->state = ST_DONE;
            } else if (header(z) != 0) {
                z->state = ST_ERROR;
            }
        } else if (z->state == ST_STORED) {
            (void)stored(z, room);
        } else if (z->state == ST_CODES) {
            (void)coded(z, room);
        } else {
            return;
        }
    }
}

/* -------------------------------------------------------------- API  */

tiku_inflater_t *
tiku_inflater_new(tiku_inflate_fill_t fill, void *ctx, int flags)
{
    tiku_inflater_t *z = calloc(1, sizeof *z);

    if (z == NULL) {
        return NULL;
    }
    z->deflate64 = (flags & TIKU_INFLATE_DEFLATE64) != 0;
    z->wsize = z->deflate64 ? 65536 : 32768;
    z->in = malloc(IN_SIZE);
    z->win = malloc((size_t)z->wsize);
    if (z->in == NULL || z->win == NULL || fill == NULL) {
        tiku_inflater_free(z);
        return NULL;
    }
    z->fill = fill;
    z->ctx = ctx;
    z->state = ST_HEADER;
    return z;
}

long
tiku_inflater_read(tiku_inflater_t *z, unsigned char *out, long max)
{
    long got = 0;

    while (got < max) {
        if (z->pend > 0) {
            long n = z->pend < max - got ? z->pend : max - got;
            long at = (z->wpos - z->pend) & (z->wsize - 1);
            long first = n < z->wsize - at ? n : z->wsize - at;

            memcpy(out + got, z->win + at, (size_t)first);
            memcpy(out + got + first, z->win, (size_t)(n - first));
            z->pend -= n;
            got += n;
            continue;
        }
        if (z->state == ST_DONE || z->state == ST_ERROR) {
            break;
        }
        produce(z);
    }
    return got > 0 || z->state != ST_ERROR ? got : -1;
}

int
tiku_inflater_done(const tiku_inflater_t *z)
{
    return z->state == ST_DONE && z->pend == 0;
}

long
tiku_inflater_tail(tiku_inflater_t *z, unsigned char *out, long max)
{
    long got = 0;

    if (z->state != ST_DONE) {
        return 0;
    }
    while (got < max && z->bitcnt >= 8) {
        out[got++] = (unsigned char)z->bitbuf;
        z->bitbuf >>= 8;
        z->bitcnt -= 8;
    }
    if (got < max && z->in_pos < z->in_len) {
        long n = z->in_len - z->in_pos < max - got ? z->in_len - z->in_pos
                                                   : max - got;

        memcpy(out + got, z->in + z->in_pos, (size_t)n);
        z->in_pos += n;
        got += n;
    }
    return got;
}

void
tiku_inflater_free(tiku_inflater_t *z)
{
    if (z != NULL) {
        free(z->in);
        free(z->win);
        free(z);
    }
}
