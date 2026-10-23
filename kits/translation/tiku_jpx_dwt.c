/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_jpx_dwt.c - JPEG 2000's inverse transforms: the wavelet that puts
 * each resolution back from its four subbands (15444-1 Annex F), the
 * 5/3 in integers or the 9/7 in reals, and the component transform that
 * turns YCbCr-like components back to RGB (Annex G).
 *
 * A row or a column is lifted in place over an extension of itself,
 * mirrored at both ends; where it starts on the grid, odd or even, says
 * which of its samples came from the low-pass band.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_jpx_private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define EXT 4                       /* samples mirrored on each side     */

/* The 9/7 lifting constants (F.3.8.2). */
#define ALPHA (-1.586134342059924)
#define BETA  (-0.052980118572961)
#define GAMMA 0.882911075530934
#define DELTA 0.443506852043971
#define KAPPA 1.230174104914001

/** @brief Where sample @p k of a line of @p n lands after periodic
 *         symmetric extension (F-4). */
static int
mirror(int k, int n)
{
    int p = 2 * (n - 1);

    k %= p;
    if (k < 0) {
        k += p;
    }
    return k >= n ? p - k : k;
}

/** @brief One lifting step: each sample of parity @p odd (by its place
 *         on the grid, @p i0 the first's) less @p c times the two beside
 *         it, over the whole extended line @p e of @p m samples. */
static void
lift(float *e, int m, int i0, int odd, double c)
{
    int j;

    for (j = 1; j < m - 1; j++) {
        if (((i0 + j) & 1) == odd) {
            e[j] -= (float)(c * ((double)e[j - 1] + e[j + 1]));
        }
    }
}

/**
 * @brief One line's inverse transform (F.3.6, F.3.7): @p n samples, the
 *        first @p nlow low-pass and the rest high-pass, interleaved by
 *        their place from @p i0, mirrored, lifted and written back in
 *        order.
 * @note @p e holds n + 2 * EXT samples.
 */
static void
line_idwt(float *line, int n, int nlow, int i0, int reversible, float *e)
{
    int e0 = i0 + (i0 & 1), o0 = i0 + !(i0 & 1), k, m = n + 2 * EXT;

    if (n == 1) {
        if (i0 & 1) {               /* a lone high-pass sample, halved */
            line[0] = reversible ? floorf(line[0] / 2) : line[0] / 2;
        }
        return;
    }
    for (k = 0; k < n; k++) {
        int i = i0 + k;

        e[EXT + k] = (i & 1) ? line[nlow + (i - o0) / 2] : line[(i - e0) / 2];
    }
    for (k = 1; k <= EXT; k++) {
        e[EXT - k] = e[EXT + mirror(-k, n)];
        e[EXT + n - 1 + k] = e[EXT + mirror(n - 1 + k, n)];
    }
    if (reversible) {               /* 5/3 (F.3.8.1): integers, floored */
        for (k = 1; k < m - 1; k++) {
            if (((i0 + k) & 1) == 0) {
                e[k] -= floorf((e[k - 1] + e[k + 1] + 2) / 4);
            }
        }
        for (k = 1; k < m - 1; k++) {
            if ((i0 + k) & 1) {
                e[k] += floorf((e[k - 1] + e[k + 1]) / 2);
            }
        }
    } else {                        /* 9/7 (F.3.8.2) */
        for (k = 0; k < m; k++) {
            e[k] *= ((i0 + k) & 1) ? (float)(1 / KAPPA) : (float)KAPPA;
        }
        lift(e, m, i0, 0, DELTA);
        lift(e, m, i0, 1, GAMMA);
        lift(e, m, i0, 0, BETA);
        lift(e, m, i0, 1, ALPHA);
    }
    memcpy(line, e + EXT, (size_t)n * sizeof *line);
}

int
jpx_idwt(jpx_tcomp_t *tc, int reversible)
{
    long stride = tc->stride;
    int r, x, y, most = 0;
    float *line, *e;

    for (r = 0; r <= tc->top; r++) {
        int w = tc->res[r].x1 - tc->res[r].x0, h = tc->res[r].y1 -
                                                    tc->res[r].y0;

        most = w > most ? w : most;
        most = h > most ? h : most;
    }
    line = malloc((size_t)(most + 1) * sizeof *line);
    e = malloc((size_t)(most + 2 * EXT + 1) * sizeof *e);
    if (line == NULL || e == NULL) {
        free(line);
        free(e);
        return -1;
    }
    for (r = 1; r <= tc->top; r++) {
        const jpx_res_t *rs = &tc->res[r], *lo = &tc->res[r - 1];
        int w = rs->x1 - rs->x0, h = rs->y1 - rs->y0;
        int lw = lo->x1 - lo->x0, lh = lo->y1 - lo->y0;

        for (y = 0; w > 0 && y < h; y++) {
            line_idwt(tc->coef + y * stride, w, lw, rs->x0, reversible, e);
        }
        for (x = 0; h > 0 && x < w; x++) {
            for (y = 0; y < h; y++) {
                line[y] = tc->coef[y * stride + x];
            }
            line_idwt(line, h, lh, rs->y0, reversible, e);
            for (y = 0; y < h; y++) {
                tc->coef[y * stride + x] = line[y];
            }
        }
    }
    free(line);
    free(e);
    return 0;
}

void
jpx_mct(float *a, float *b, float *c, long n, int reversible)
{
    long i;

    for (i = 0; i < n; i++) {
        float y = a[i], u = b[i], v = c[i];

        if (reversible) {           /* RCT (G.2): G first, R and B off it */
            float g = y - floorf((u + v) / 4);

            a[i] = v + g;
            b[i] = g;
            c[i] = u + g;
        } else {                    /* ICT (G.3) */
            a[i] = y + 1.402f * v;
            b[i] = y - 0.34413f * u - 0.71414f * v;
            c[i] = y + 1.772f * u;
        }
    }
}
