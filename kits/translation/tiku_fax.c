/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fax.c - CCITT Group 3 and Group 4 decoding.
 *
 * A one-dimensional row is runs of white and black, alternating from
 * white, each a Huffman code; a two-dimensional row gives where its
 * colour changes against the row above (T.4 4.2: the vertical, pass and
 * horizontal modes).  So a row is kept as the columns where its colour
 * changes, which is also how the next row reads it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_fax.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define COLUMNS_MAX 65536           /* the widest row read, in pixels   */
#define PEEK        13              /* the longest run code, in bits    */
#define EOL         0x001           /* eleven 0s and a 1                */
#define EOFB        0x001001        /* Group 4's end: two EOLs          */

/* The two-dimensional modes; a vertical one is its offset, -3 to 3. */
enum { M_P = 10, M_H, M_EOL, M_BAD };

/* T.4's terminating codes: white runs of 0 to 63. */
static const char *const white_end[64] = {
    "00110101", "000111", "0111", "1000", "1011", "1100", "1110", "1111",
    "10011", "10100", "00111", "01000", "001000", "000011", "110100",
    "110101", "101010", "101011", "0100111", "0001100", "0001000", "0010111",
    "0000011", "0000100", "0101000", "0101011", "0010011", "0100100",
    "0011000", "00000010", "00000011", "00011010", "00011011", "00010010",
    "00010011", "00010100", "00010101", "00010110", "00010111", "00101000",
    "00101001", "00101010", "00101011", "00101100", "00101101", "00000100",
    "00000101", "00001010", "00001011", "01010010", "01010011", "01010100",
    "01010101", "00100100", "00100101", "01011000", "01011001", "01011010",
    "01011011", "01001010", "01001011", "00110010", "00110011", "00110100",
};
/* Its make-up codes: white runs of 64 to 1728, by 64. */
static const char *const white_make[27] = {
    "11011", "10010", "010111", "0110111", "00110110", "00110111", "01100100",
    "01100101", "01101000", "01100111", "011001100", "011001101", "011010010",
    "011010011", "011010100", "011010101", "011010110", "011010111",
    "011011000", "011011001", "011011010", "011011011", "010011000",
    "010011001", "010011010", "011000", "010011011",
};
/* Black runs of 0 to 63. */
static const char *const black_end[64] = {
    "0000110111", "010", "11", "10", "011", "0011", "0010", "00011", "000101",
    "000100", "0000100", "0000101", "0000111", "00000100", "00000111",
    "000011000", "0000010111", "0000011000", "0000001000", "00001100111",
    "00001101000", "00001101100", "00000110111", "00000101000", "00000010111",
    "00000011000", "000011001010", "000011001011", "000011001100",
    "000011001101", "000001101000", "000001101001", "000001101010",
    "000001101011", "000011010010", "000011010011", "000011010100",
    "000011010101", "000011010110", "000011010111", "000001101100",
    "000001101101", "000011011010", "000011011011", "000001010100",
    "000001010101", "000001010110", "000001010111", "000001100100",
    "000001100101", "000001010010", "000001010011", "000000100100",
    "000000110111", "000000111000", "000000100111", "000000101000",
    "000001011000", "000001011001", "000000101011", "000000101100",
    "000001011010", "000001100110", "000001100111",
};
/* Black runs of 64 to 1728, by 64. */
static const char *const black_make[27] = {
    "0000001111", "000011001000", "000011001001", "000001011011",
    "000000110011", "000000110100", "000000110101", "0000001101100",
    "0000001101101", "0000001001010", "0000001001011", "0000001001100",
    "0000001001101", "0000001110010", "0000001110011", "0000001110100",
    "0000001110101", "0000001110110", "0000001110111", "0000001010010",
    "0000001010011", "0000001010100", "0000001010101", "0000001011010",
    "0000001011011", "0000001100100", "0000001100101",
};
/* The extended make-up codes, either colour: 1792 to 2560, by 64. */
static const char *const both_make[13] = {
    "00000001000", "00000001100", "00000001101", "000000010010",
    "000000010011", "000000010100", "000000010101", "000000010110",
    "000000010111", "000000011100", "000000011101", "000000011110",
    "000000011111",
};

/** @brief A decode under way: the bits, the rows as changes, the codes. */
typedef struct {
    const unsigned char *p;         /* the coded bytes                  */
    long                 n, bit;    /* how many, and the next bit       */
    int                  w;         /* columns                          */
    int                 *ref, *cur; /* the row above and this one       */
    uint16_t            *tab;       /* runs by PEEK bits, white, black  */
} fax_t;

/* --------------------------------------------------------------- bits  */

/** @brief The next @p k bits (up to 24), zeros past the end. */
static unsigned
peek(const fax_t *f, int k)
{
    long byte = f->bit >> 3;
    uint32_t v = 0;
    int i;

    for (i = 0; i < 4; i++) {
        v = v << 8 | (byte + i < f->n ? f->p[byte + i] : 0u);
    }
    return (unsigned)(v >> (32 - (int)(f->bit & 7) - k)) & ((1u << k) - 1);
}

/** @brief Whether the bits have run out. */
static int
spent(const fax_t *f)
{
    return f->bit >= f->n * 8;
}

/* -------------------------------------------------------------- codes  */

/** @brief Code @p code for a run of @p run into @p tab: every PEEK-bit
 *         value it begins holds the run and the code's length. */
static void
put(uint16_t *tab, const char *code, int run)
{
    int len = (int)strlen(code), i;
    unsigned v = 0;

    for (i = 0; i < len; i++) {
        v = v << 1 | (unsigned)(code[i] == '1');
    }
    for (i = 0; i < 1 << (PEEK - len); i++) {
        tab[v << (PEEK - len) | (unsigned)i] = (uint16_t)(run << 4 | len);
    }
}

/** @brief The white table then the black, each 1 << PEEK entries. */
static uint16_t *
tables(void)
{
    uint16_t *tab = calloc((size_t)2 << PEEK, sizeof *tab);
    int i;

    if (tab == NULL) {
        return NULL;
    }
    for (i = 0; i < 64; i++) {
        put(tab, white_end[i], i);
        put(tab + (1 << PEEK), black_end[i], i);
    }
    for (i = 0; i < 27; i++) {
        put(tab, white_make[i], 64 * (i + 1));
        put(tab + (1 << PEEK), black_make[i], 64 * (i + 1));
    }
    for (i = 0; i < 13; i++) {
        put(tab, both_make[i], 1792 + 64 * i);
        put(tab + (1 << PEEK), both_make[i], 1792 + 64 * i);
    }
    return tab;
}

/** @brief One run of @p black or white: make-up codes, then the one that
 *         ends it.  @return its length, or -1 at a code that is none. */
static int
read_run(fax_t *f, int black)
{
    int total = 0;

    for (;;) {
        unsigned e = f->tab[(black ? 1 << PEEK : 0) + peek(f, PEEK)];

        if (e == 0 || total > COLUMNS_MAX) {
            return -1;
        }
        f->bit += e & 15;
        total += (int)(e >> 4);
        if ((e >> 4) < 64) {
            return total;
        }
    }
}

/** @brief A two-dimensional mode's code: M_P, M_H, a vertical offset,
 *         M_EOL (left unread) or M_BAD. */
static int
read_mode(fax_t *f)
{
    static const signed char vert[4] = {0, 0, -1, 1};  /* 010, 011 */
    unsigned v = peek(f, 7);

    if (v >> 6 == 1) {
        f->bit += 1;                /* 1: V0 */
        return 0;
    }
    if (v >> 4 >= 2) {
        f->bit += 3;                /* 010 VL1, 011 VR1 */
        return vert[v >> 4];
    }
    if (v >> 4 == 1) {
        f->bit += 3;                /* 001 */
        return M_H;
    }
    if (v >> 3 == 1) {
        f->bit += 4;                /* 0001 */
        return M_P;
    }
    if (v >> 1 == 2 || v >> 1 == 3) {
        f->bit += 6;                /* 000010 VL2, 000011 VR2 */
        return v >> 1 == 2 ? -2 : 2;
    }
    if (v == 2 || v == 3) {
        f->bit += 7;                /* 0000010 VL3, 0000011 VR3 */
        return v == 2 ? -3 : 3;
    }
    return peek(f, 12) == EOL ? M_EOL : M_BAD;
}

/* --------------------------------------------------------------- rows  */

/** @brief A one-dimensional row: runs from white, alternating.
 *  @return how many changes it put in @p cur, or -1 at a bad code. */
static int
row_1d(fax_t *f, int *cur)
{
    int a0 = 0, black = 0, n = 0;

    while (a0 < f->w && peek(f, 12) != EOL) {
        int run = read_run(f, black);

        if (run < 0 || n > f->w + 1) {
            return -1;
        }
        a0 = a0 + run > f->w ? f->w : a0 + run;
        cur[n++] = a0;
        black = !black;
    }
    return n;
}

/** @brief Horizontal mode: a run of the colour at @p *a0, then one of
 *         the other, both noted.  @return 0, or -1 at a bad code. */
static int
horizontal(fax_t *f, int *cur, int *n, int *a0, int black)
{
    int r1 = read_run(f, black), r2 = r1 < 0 ? -1 : read_run(f, !black);
    int a1 = *a0 < 0 ? 0 : *a0;

    if (r2 < 0) {
        return -1;
    }
    a1 = a1 + r1 > f->w ? f->w : a1 + r1;
    *a0 = a1 + r2 > f->w ? f->w : a1 + r2;
    cur[(*n)++] = a1;
    cur[(*n)++] = *a0;
    return 0;
}

/**
 * @brief A two-dimensional row, read against @p ref, the row above.
 *
 * b1 is the first change above, right of a0, to the colour a0 is not;
 * its index parity is that colour, since rows start white.
 * @return how many changes it put in @p cur, or -1 at a bad code.
 */
static int
row_2d(fax_t *f, const int *ref, int *cur)
{
    int a0 = -1, black = 0, n = 0, k = 0;

    while (a0 < f->w) {
        int mode = read_mode(f), a1;

        if (mode == M_EOL) {
            break;                  /* a row cut short */
        }
        if (mode == M_BAD || n > f->w + 1) {
            return -1;
        }
        while (k > 0 && ref[k - 1] > a0) {
            k--;
        }
        while (ref[k] <= a0 || (k & 1) != black) {
            k++;
        }
        if (mode == M_P) {
            a0 = ref[k + 1];        /* on to b2, the colour kept */
        } else if (mode == M_H) {
            if (horizontal(f, cur, &n, &a0, black) != 0) {
                return -1;
            }
        } else {
            a1 = ref[k] + mode;     /* b1, moved by the offset */
            a1 = a1 < (a0 < 0 ? 0 : a0) ? (a0 < 0 ? 0 : a0) : a1;
            a0 = cur[n++] = a1 > f->w ? f->w : a1;
            black = !black;
        }
    }
    return n;
}

/** @brief Row @p cur's @p n changes settled: two at one column undo each
 *         other, those past the row go, three sentinels follow.
 *  @return how many remain. */
static int
settle(int *cur, int n, int w)
{
    int i, m = 0;

    for (i = 0; i < n && cur[i] < w; i++) {
        if (m > 0 && cur[m - 1] == cur[i]) {
            m--;
        } else {
            cur[m++] = cur[i];
        }
    }
    cur[m] = cur[m + 1] = cur[m + 2] = w;
    return m;
}

/** @brief The row's changes as bits: white, then each change flipping. */
static void
emit(unsigned char *row, const int *cur, int m, int w, int black_is_1)
{
    int i, x;

    memset(row, black_is_1 ? 0x00 : 0xff, (size_t)(w + 7) / 8);
    for (i = 0; i < m; i += 2) {
        for (x = cur[i]; x < cur[i + 1]; x++) {
            if (black_is_1) {
                row[x >> 3] |= (unsigned char)(0x80 >> (x & 7));
            } else {
                row[x >> 3] &= (unsigned char)~(0x80 >> (x & 7));
            }
        }
    }
}

/**
 * @brief Past what comes before a row: Group 4's alignment and its end
 *        (EOFB); Group 3's fill, EOL and one-or-two-dimensional tag.
 *
 * Two EOLs in a row are Group 3's end (RTC), with or without the tags
 * of mixed rows between them.
 * @return 0 at the end of the block or of the bits, else 1.
 */
static int
row_start(fax_t *f, const tiku_fax_t *p, int *twod)
{
    int eols = 0;

    if (p->k < 0) {
        if (p->byte_align) {
            f->bit = (f->bit + 7) & ~7L;
        }
        return !spent(f) && peek(f, 24) != EOFB;
    }
    for (;;) {
        while (peek(f, 12) == 0 && !spent(f)) {
            f->bit++;               /* fill before an EOL */
        }
        if (peek(f, 12) != EOL && !(eols > 0 && p->k > 0 &&
                                    peek(f, 13) == (1u << 12 | EOL))) {
            break;
        }
        f->bit += peek(f, 12) == EOL ? 12 : 13;
        if (++eols == 2) {
            return 0;
        }
    }
    if (eols == 0 && p->byte_align) {
        f->bit = (f->bit + 7) & ~7L;
    }
    if (p->k > 0) {
        *twod = peek(f, 1) == 0;    /* 1: one-dimensional */
        f->bit++;
    }
    return !spent(f);
}

/** @brief @p buf grown to hold @p want bytes, within @p omax.
 *  @return 0, or -1 past @p omax or out of memory. */
static int
room(unsigned char **buf, long *cap, long want, long omax)
{
    unsigned char *b;
    long c = *cap > 0 ? *cap : 4096;

    if (want <= *cap) {
        return 0;
    }
    if (want > omax) {
        return -1;
    }
    while (c < want) {
        c = c > omax / 2 ? omax : c * 2;
    }
    c = c > omax ? omax : c;
    b = realloc(*buf, (size_t)c);
    if (b == NULL) {
        return -1;
    }
    *buf = b;
    *cap = c;
    return 0;
}

/** @brief Row after row until the rows asked for, the block or the bits
 *         end, or a row will not read; rows asked for and not read are
 *         white.  @return the bytes, or -1 when they will not fit or, with
 *         no row count, none read. */
static long
rows(fax_t *f, const tiku_fax_t *p, unsigned char **out, long omax)
{
    long rb = (f->w + 7) / 8, cap = 0, row;
    unsigned char *buf = NULL;
    int full = 0;

    for (row = 0; p->rows <= 0 || row < p->rows; row++) {
        int twod = p->k < 0, m, *t;

        if (!row_start(f, p, &twod)) {
            break;
        }
        m = twod ? row_2d(f, f->ref, f->cur) : row_1d(f, f->cur);
        if (m < 0 || (full = room(&buf, &cap, (row + 1) * rb, omax)) != 0) {
            break;
        }
        m = settle(f->cur, m, f->w);
        emit(buf + row * rb, f->cur, m, f->w, p->black_is_1);
        t = f->ref;
        f->ref = f->cur;
        f->cur = t;
    }
    if (full == 0 && row < p->rows &&
        (full = room(&buf, &cap, (long)p->rows * rb, omax)) == 0) {
        memset(buf + row * rb, p->black_is_1 ? 0x00 : 0xff,
               (size_t)((p->rows - row) * rb));
        row = p->rows;
    }
    if (row == 0 || full != 0) {
        free(buf);
        return -1;
    }
    *out = buf;
    return row * rb;
}

long
tiku_fax_decode(const unsigned char *src, long n, const tiku_fax_t *p,
                unsigned char **out, long omax)
{
    return tiku_fax_decode_used(src, n, p, out, omax, NULL);
}

long
tiku_fax_decode_used(const unsigned char *src, long n, const tiku_fax_t *p,
                     unsigned char **out, long omax, long *used)
{
    fax_t f;
    long made = -1;

    if (out != NULL) {
        *out = NULL;
    }
    if (used != NULL) {
        *used = 0;
    }
    if (src == NULL || n < 0 || p == NULL || out == NULL ||
        p->columns <= 0 || p->columns > COLUMNS_MAX) {
        return -1;
    }
    memset(&f, 0, sizeof f);
    f.p = src;
    f.n = n;
    f.w = p->columns;
    f.tab = tables();
    f.ref = malloc(((size_t)f.w + 8) * sizeof *f.ref);
    f.cur = malloc(((size_t)f.w + 8) * sizeof *f.cur);
    if (f.tab != NULL && f.ref != NULL && f.cur != NULL) {
        f.ref[0] = f.ref[1] = f.ref[2] = f.w;   /* above the first: white */
        made = rows(&f, p, out, omax);
        if (p->rows > 0 && peek(&f, 24) == 0x001001u) {
            f.bit += 24;            /* the end of the block, after them */
        }
    }
    if (used != NULL) {
        *used = (f.bit + 7) / 8 < n ? (f.bit + 7) / 8 : n;
    }
    free(f.tab);
    free(f.ref);
    free(f.cur);
    return made;
}
