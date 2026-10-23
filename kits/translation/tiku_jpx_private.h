/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_jpx_private.h - what the JPEG 2000 decoder's files share: the
 * codestream's coding and quantization styles, and a tile taken apart
 * as the standard does -- components, resolutions, subbands, precincts
 * and code-blocks -- which tier 2 fills from packets (tiku_jpx_t2.c)
 * and tier 1 turns into coefficients (tiku_jpx_t1.c).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_JPX_PRIVATE_H_
#define TIKU_JPX_PRIVATE_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_jpx.h"

#define JPX_LEVELS  32              /* decomposition levels, at most     */
#define JPX_SEGS    110             /* codeword segments a code-block    */
#define JPX_POCS    256             /* progression changes one list keeps */
#define JPX_COMPS   16384           /* components a codestream may have  */

/* Code-block styles (A.6.1, Table A.19). */
#define JPX_BYPASS  0x01            /* first two passes raw after plane 4 */
#define JPX_RESET   0x02            /* contexts reset each pass          */
#define JPX_TERMALL 0x04            /* each pass its own segment         */
#define JPX_VSC     0x08            /* vertically causal contexts        */
#define JPX_SEGSYM  0x20            /* segmentation symbols              */

enum { JPX_LL, JPX_HL, JPX_LH, JPX_HH };
enum { JPX_LRCP, JPX_RLCP, JPX_RPCL, JPX_PCRP, JPX_CPRL };

/** @brief A component as SIZ gives it (A.5.1). */
typedef struct {
    int prec, sgnd;                 /**< its bits; whether signed       */
    int dx, dy;                     /**< its sampling of the reference grid */
} jpx_comp_t;

/** @brief How one component is coded (COD, COC: A.6.1, A.6.2). */
typedef struct {
    int           nl;               /**< decomposition levels           */
    int           cbw, cbh;         /**< code-block size exponents      */
    int           style;            /**< JPX_BYPASS ...                 */
    int           reversible;       /**< 5/3 (1) or 9/7 (0)             */
    unsigned char ppx[JPX_LEVELS + 1], ppy[JPX_LEVELS + 1];
                                    /**< precinct exponents by resolution */
} jpx_ccod_t;

/** @brief How one component is quantized (QCD, QCC: A.6.4, A.6.5). */
typedef struct {
    int      style;                 /**< 0 none, 1 derived, 2 expounded */
    int      guard;                 /**< guard bits                     */
    int      n;                     /**< step sizes given               */
    uint16_t step[3 * JPX_LEVELS + 1];  /**< exponent and mantissa each */
} jpx_quant_t;

/** @brief A progression and the range it runs over (COD, POC). */
typedef struct {
    int order;                      /**< JPX_LRCP ...                   */
    int rs, re, cs, ce, le;         /**< resolutions, components, layers */
} jpx_prog_t;

/** @brief The styles a tile is coded by: the main header's, overridden
 *         by its own tile-part headers. */
typedef struct {
    int          order, layers, mct; /**< progression, layers, transform */
    int          sop, eph;          /**< SOP and EPH markers used       */
    jpx_ccod_t  *ccod;              /**< each component's coding        */
    jpx_quant_t *quant;             /**< each component's quantization  */
    int         *roi;               /**< each component's ROI shift     */
    jpx_prog_t  *poc;               /**< progression changes, in order  */
    int          npoc, cappoc;      /**< how many; room for             */
} jpx_style_t;

/** @brief A codeword segment of a code-block (B.10.7). */
typedef struct {
    long len, newlen;               /**< its bytes; this packet's       */
    int  npasses, maxpasses;        /**< coding passes it holds, can hold */
} jpx_seg_t;

/** @brief A code-block and what the packets brought for it. */
typedef struct {
    int            x0, y0, x1, y1;  /**< in its subband                 */
    int            incl;            /**< already included in a layer    */
    int            numbps;          /**< magnitude bitplanes it codes   */
    int            lblock;          /**< Lblock (B.10.7.1)              */
    int            npasses;         /**< coding passes so far           */
    int            nseg, capseg;    /**< codeword segments; room for    */
    int            nnew, pfirst;    /**< in this packet; its first segment */
    jpx_seg_t     *seg;             /**< its codeword segments          */
    unsigned char *data;            /**< every segment's bytes, in order */
    long           len, cap;        /**< data's bytes; room for         */
} jpx_cblk_t;

/** @brief A tag tree over a precinct's code-blocks in one subband. */
typedef struct {
    int  w, h, nnodes;              /**< leaves across and down; all    */
    int *value, *low, *parent;      /**< each node's                    */
} jpx_tag_t;

/** @brief A precinct's part of one subband: its code-blocks. */
typedef struct {
    int         cw, ch;             /**< code-blocks across and down    */
    jpx_cblk_t *cblk;               /**< cw * ch of them, row by row    */
    jpx_tag_t   incl, zbp;          /**< inclusion and zero bitplanes   */
} jpx_pband_t;

/** @brief A subband of a resolution. */
typedef struct {
    int    orient;                  /**< JPX_LL ...                     */
    int    x0, y0, x1, y1;          /**< its extent                     */
    int    mb;                      /**< magnitude bitplanes, Mb        */
    double delta;                   /**< its quantization step          */
    int    xcb, ycb;                /**< code-block exponents here      */
} jpx_band_t;

/** @brief A resolution of a tile-component. */
typedef struct {
    int          x0, y0, x1, y1;    /**< its extent                     */
    int          nbands;            /**< 1 at resolution 0, else 3      */
    jpx_band_t   band[3];           /**< LL alone, or HL, LH and HH     */
    int          ppx, ppy;          /**< precinct exponents             */
    int          pw, ph;            /**< precincts across and down      */
    jpx_pband_t *prec;              /**< pw * ph * nbands               */
    int         *layers;            /**< each precinct's layers read    */
} jpx_res_t;

/** @brief A tile's share of one component, and its coefficients. */
typedef struct {
    int        x0, y0, x1, y1;      /**< in the component's own samples */
    int        nl;                  /**< its decomposition levels       */
    int        top;                 /**< the resolution decoded up to   */
    jpx_res_t *res;                 /**< nl + 1 resolutions             */
    float     *coef;                /**< the top resolution's, its subbands
                                         laid as the transform leaves them */
    long       stride;              /**< coef's samples a row           */
} jpx_tcomp_t;

/** @brief A tile: where it is, its styles and the bytes of its parts. */
typedef struct {
    int            x0, y0, x1, y1;  /**< on the reference grid          */
    int            ownst, ownpoc;   /**< its styles, its progression
                                         changes its own, not the main's */
    jpx_style_t    st;              /**< its styles                     */
    jpx_tcomp_t   *comp;            /**< each component's share         */
    unsigned char *data;            /**< its tile-parts' bodies, joined */
    long           len, cap;        /**< data's bytes; room for         */
    unsigned char *hdr;             /**< packet headers PPT/PPM carry   */
    long           hlen, hcap;      /**< hdr's bytes; room for          */
} jpx_tile_t;

/** @brief A decode: the image's geometry, its styles, its tiles. */
typedef struct {
    uint32_t     xsiz, ysiz, xosiz, yosiz; /**< the image on the grid  */
    uint32_t     xtsiz, ytsiz, xtosiz, ytosiz; /**< the tiles on it    */
    int          ncomp;             /**< components                     */
    jpx_comp_t  *cm;                /**< each one's                     */
    int          reduce;            /**< resolutions left out, to fit   */
    long         max_pixels;        /**< samples a component may come to */
    int          ntx, nty;          /**< tiles across and down          */
    jpx_style_t  main;              /**< the main header's styles       */
    jpx_tile_t **tile;              /**< each, once a part of it came   */
    unsigned char *ppm;             /**< PPM's headers, for the tile-parts */
    long         ppmlen;            /**< their bytes                    */
    int64_t      work;              /**< coefficient bitplanes decoded  */
    long         layout;            /**< bytes the tile's layout took   */
} jpx_t;

/* tiku_jpx_t2.c */

/** @brief The tile's packets, in its progressions, into its code-blocks.
 *  @return 0, or -1 when the data will not read (what did is kept). */
int jpx_t2_decode(jpx_t *j, jpx_tile_t *t);
/** @brief A tag tree of @p w by @p h leaves.  @return 0, or -1. */
int jpx_tag_init(jpx_tag_t *tt, int w, int h);
/** @brief Tag tree @p tt's nodes given back, and @p tt left empty. */
void jpx_tag_free(jpx_tag_t *tt);

/* tiku_jpx_dwt.c */

/** @brief Tile-component @p tc's coefficients transformed back to
 *         samples, resolution by resolution up to its top one.
 *  @return 0, or -1. */
int jpx_idwt(jpx_tcomp_t *tc, int reversible);
/** @brief The inverse component transform over the first three
 *         components, @p n samples each: RCT or ICT. */
void jpx_mct(float *a, float *b, float *c, long n, int reversible);

/* tiku_jpx.c */

/** @brief The codestream at @p p decoded into @p planes: each of the
 *         first TIKU_JPX_COMPS components' samples, at its own sampling
 *         and the reduction that fits @p max_pixels, as the coder's
 *         unsigned values.  @return 0, or -1. */
int jpx_codestream(jpx_t *j, const unsigned char *p, long n, long max_pixels,
                   uint16_t **planes);
/** @brief What jpx_codestream() made, given back (not the planes). */
void jpx_done(jpx_t *j);

/* tiku_jpx_t1.c */

/** @brief Code-block @p cb of band @p b decoded into @p out (its
 *         coefficients dequantized, @p stride apart).  @return 0, or -1
 *         when it is past what can be decoded. */
int jpx_t1_decode(const jpx_cblk_t *cb, const jpx_band_t *b, int style,
                  int reversible, int roi, float *out, long stride);

#endif /* TIKU_JPX_PRIVATE_H_ */
