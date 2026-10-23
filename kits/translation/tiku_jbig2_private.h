/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_jbig2_private.h - what the JBIG2 decoder's files share: bitmaps
 * of a byte a pixel, the integer procedures the MQ arithmetic decoder
 * (tiku_mq) carries (T.88 Annex A), Huffman tables (Annex B), and the
 * generic and refinement region procedures (6.2, 6.3) the segments of
 * tiku_jbig2.c build on.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_JBIG2_PRIVATE_H_
#define TIKU_JBIG2_PRIVATE_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_mq.h"

/** @brief Maximum number of bitmap pixels composed by one decode. */
#define JB_WORK_MAX (1LL << 29)

/** @brief A bitmap, one byte a pixel, 1 black; outside it is white. */
typedef struct {
    int            w, h;            /**< its size in pixels             */
    unsigned char *px;              /**< w * h bytes, 0 or 1            */
} jb_bm_t;

/** @brief An integer procedure's contexts (Annex A.2). */
typedef struct {
    tiku_mq_cx_t cx[512];           /**< by the bits decoded so far     */
} jb_iax_t;

/** @brief Bits read most significant first, for the Huffman coded. */
typedef struct {
    const unsigned char *p;         /**< the bytes                       */
    long                 n, bit;    /**< how many; the next bit          */
} jb_bits_t;

enum { JB_LINE, JB_LOWER, JB_UPPER, JB_OOB };

/** @brief A line of a Huffman table: its prefix, and the range it
 *         codes as RANGELOW plus RANGELEN more bits. */
typedef struct {
    /** PREFLEN, RANGELEN, and JB_LINE, _LOWER, _UPPER or _OOB */
    int      prefl, rangel, kind;
    int32_t  low;                   /**< RANGELOW                       */
    uint32_t code;                  /**< assigned by jb_table_codes()   */
} jb_line_t;

/** @brief A Huffman table: its lines, with their codes. */
typedef struct {
    int        n;                   /**< lines                           */
    jb_line_t *line;                /**< in table order, the order in which
                                         codes of one length are given */
} jb_table_t;

/** @brief An integer (A.2).  @return 0 with @p v, 1 for OOB. */
int jb_int(tiku_mq_t *d, jb_iax_t *ia, int32_t *v);
/** @brief A symbol ID of @p len bits (A.3), in 1 << @p len contexts. */
long jb_iaid(tiku_mq_t *d, tiku_mq_cx_t *cx, int len);

/** @brief The next @p k bits (up to 32).  @return -1 past the end. */
int jb_bits(jb_bits_t *b, int k, uint32_t *v);
/** @brief On to the next byte. */
void jb_align(jb_bits_t *b);
/** @brief Standard table B.@p k (1 to 15), built into @p t's lines. */
void jb_table_std(int k, jb_table_t *t, jb_line_t *lines);
/** @brief Prefix codes for @p t's lines by their lengths (B.3). */
void jb_table_codes(jb_table_t *t);
/** @brief A table segment (7.4.13) read into @p t.  @return 0, or -1. */
int jb_table_read(const unsigned char *p, long n, jb_table_t *t);
/** @brief A value from @p t.  @return 0, 1 for OOB, -1 for none. */
int jb_huff(jb_bits_t *b, const jb_table_t *t, int32_t *v);

/** @brief A bitmap @p w by @p h, filled with @p fill; NULL when too big
 *         or memory is short. */
jb_bm_t *jb_bm_new(int w, int h, int fill);
/** @brief Free @p b and its pixels; NULL is ignored. */
void jb_bm_free(jb_bm_t *b);
/** @brief Pixel @p x, @p y; 0 outside. */
int jb_px(const jb_bm_t *b, long x, long y);
/** @brief @p src drawn into @p dst at @p x, @p y by operator @p op: 0
 *         OR, 1 AND, 2 XOR, 3 XNOR, 4 REPLACE. */
void jb_compose(jb_bm_t *dst, const jb_bm_t *src, long x, long y, int op);

/** @brief What a generic region is coded with (6.2). */
typedef struct {
    int         mmr, tmpl, tpgdon;  /**< MMR, GBTEMPLATE, TPGDON        */
    int         at[8];              /**< A1 to A4, x then y each        */
    const jb_bm_t *skip;            /**< USESKIP: pixels not coded      */
} jb_gen_t;

/** @brief What a refinement is coded against (6.3). */
typedef struct {
    int            tmpl, tpgron;    /**< GRTEMPLATE, TPGRON             */
    int            at[4];           /**< A1 in the image, A2 in reference */
    const jb_bm_t *ref;             /**< the reference bitmap           */
    long           dx, dy;          /**< GRREFERENCEDX, GRREFERENCEDY   */
} jb_ref_t;

/** @brief Decode @p out's pixels arithmetically (6.2.5) in contexts
 *         @p gb (65536).  @return 0, or -1 when the data ran out. */
int jb_generic(tiku_mq_t *d, tiku_mq_cx_t *gb, const jb_gen_t *g, jb_bm_t *out);
/** @brief Decode @p out's pixels from MMR data (6.2.6).  @return the
 *         bytes it took, or -1. */
long jb_generic_mmr(const unsigned char *p, long n, jb_bm_t *out);
/** @brief Decode @p out against a reference (6.3.5) in contexts @p gr
 *         (8192).  @return 0, or -1 when the data ran out. */
int jb_refine(tiku_mq_t *d, tiku_mq_cx_t *gr, const jb_ref_t *r, jb_bm_t *out);

/* The segments and the page: tiku_jbig2.c and tiku_jbig2_text.c        */

/** @brief A segment (7.2) and what decoding it left for later ones. */
typedef struct {
    uint32_t             num;       /**< its segment number              */
    int                  type;      /**< 0 symbols, 6 text, 48 page, ... */
    uint32_t             page;      /**< the page it belongs to, 0 shared */
    uint32_t            *ref;       /**< the segments it refers to       */
    int                  nref;      /**< how many it refers to           */
    const unsigned char *data;      /**< its data, in the caller's bytes */
    long                 len;       /**< its data's length               */
    jb_bm_t            **sym;       /**< a symbol dictionary's exports   */
    long                 nsym;      /**< how many it exports             */
    tiku_mq_cx_t        *gbkeep;    /**< its generic contexts, kept for
                                         the next dictionary             */
    tiku_mq_cx_t        *grkeep;    /**< its refinement contexts, kept
                                         likewise                        */
    jb_bm_t            **pat;       /**< a pattern dictionary's patterns */
    long                 npat;      /**< how many patterns               */
    jb_bm_t             *region;    /**< an intermediate region          */
    jb_table_t           table;     /**< a table segment's               */
} jb_seg_t;

/** @brief A decode: its segments, every bitmap made, and the page. */
typedef struct {
    jb_seg_t *seg;                  /**< in the order they came          */
    long      nseg, capseg;         /**< segments held, and room         */
    jb_bm_t **pool;                 /**< freed at the end, all together  */
    long      npool, cappool;       /**< bitmaps held, and room          */
    long long pixels;               /**< in the pool, against the budget */
    long long work;                 /**< pixels drawn so far, bounded too */
    long      omax;                 /**< the page's packed bytes, at most */
    jb_bm_t  *page;                 /**< NULL until page information    */
    int       page_known;           /**< its height given up front       */
    /** the page's default pixel, its combination operator, and whether a
     *  region's own operator overrides it (7.4.8) */
    int       defpixel, defop, override;
} jb_dec_t;

/** @brief Region segment information (7.4.1). */
typedef struct {
    uint32_t w, h;                  /**< its size                        */
    int32_t  x, y;                  /**< where on the page               */
    int      op;                    /**< how it combines there: 0 OR ... */
} jb_rinfo_t;

/** @brief Arithmetic decoding shared through a dictionary or region:
 *         the integer procedures, symbol IDs and both context sets. */
typedef struct {
    tiku_mq_t  mq;                  /**< the one decoder they all share  */
    /** the integer procedures, IADH to IARDY (Annex A.2) */
    jb_iax_t dh, dw, ex, ai, dt, fs, ds, it, ri, rdw, rdh, rdx, rdy;
    tiku_mq_cx_t *id;               /**< 1 << the ID's bits             */
    tiku_mq_cx_t *gb, *gr;          /**< 65536 and 8192                 */
} jb_ar_t;

/** @brief A bitmap the decode keeps until its end; NULL past the
 *         budget. */
jb_bm_t *jb_new(jb_dec_t *jd, long w, long h, int fill);
/** @brief Count @p pixels more as drawn.
 *  @return Nonzero once the decode exceeds JB_WORK_MAX pixels. */
int jb_work(jb_dec_t *jd, long long pixels);
/** @brief Referred segment @p i of @p s, decoded before it, or NULL. */
jb_seg_t *jb_referred(jb_dec_t *jd, const jb_seg_t *s, int i);
/** @brief Region information from @p p.  @return 0, or -1. */
int jb_rinfo(const unsigned char *p, long n, jb_rinfo_t *r);
/** @brief @p b onto the page, or kept when the segment is intermediate. */
int jb_place(jb_dec_t *jd, jb_seg_t *s, jb_bm_t *b, const jb_rinfo_t *r);
/** @brief Arithmetic state for @p idlen-bit IDs; NULL when short. */
jb_ar_t *jb_ar_new(int idlen);
/** @brief Free @p ar and its contexts; NULL is ignored. */
void jb_ar_free(jb_ar_t *ar);
/** @brief Big-endian 32 bits. */
uint32_t jb_be32(const unsigned char *p);
/** @brief The bits a count of @p n needs: ceil(log2(n)). */
int jb_log2(uint32_t n);

/** @brief A symbol dictionary segment (7.4.2, 6.5). */
int jb_symbols(jb_dec_t *jd, jb_seg_t *s);
/** @brief A text region segment (7.4.3, 6.4). */
int jb_text(jb_dec_t *jd, jb_seg_t *s);

#endif /* TIKU_JBIG2_PRIVATE_H_ */
