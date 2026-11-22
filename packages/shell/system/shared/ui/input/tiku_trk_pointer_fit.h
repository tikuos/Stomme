/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_pointer_fit.h - a line through the taps of a pointer calibration.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_POINTER_FIT_H_
#define TIKU_TRK_POINTER_FIT_H_

#include <math.h>
#include <stddef.h>

/** @brief A tap farther than this many pixels from the line is dropped,
 *         one tap at most. */
#define TIKU_TRK_FIT_OUTLIER 12
/** @brief The scatter about the line, in pixels rms, above which a fit is
 *         refused. */
#define TIKU_TRK_FIT_SCATTER 12

/**
 * @brief The least-squares line through the taps, leaving out tap @p skip
 *        (-1 for none).
 * @return 0 with the slope in @p a and the offset in @p b; -1 when fewer
 *         than three taps are counted or their targets are all at one
 *         place.
 */
static inline int
tiku_trk_pointer_fit_line(const int *target, const int *reported, int n,
                          int skip, double *a, double *b)
{
    double st = 0, sr = 0, stt = 0, str = 0, spread;
    int i, used = 0;

    for (i = 0; i < n; i++) {
        if (i == skip) {
            continue;
        }
        st += target[i];
        sr += reported[i];
        stt += (double)target[i] * target[i];
        str += (double)target[i] * reported[i];
        used++;
    }
    spread = used * stt - st * st;
    if (used < 3 || spread <= 0) {
        return -1;
    }
    *a = (used * str - st * sr) / spread;
    *b = (sr - *a * st) / used;
    return 0;
}

/**
 * @brief Fit reported = a * target + b through @p n taps on one axis, by
 *        least squares.
 * @param target   Where each target was on the axis, in pixels.
 * @param reported Where the pointer was reported for it.
 * @param n        How many taps, 64 at most.
 * @param last     The axis's last pixel: its length less one.
 * @param[out] lo  The position the line gives for pixel 0.
 * @param[out] hi  The position the line gives for pixel @p last.
 * @return 0; or -1, with @p lo and @p hi unchanged, for fewer than three
 *         taps or more than 64, targets all at one place, a slope outside
 *         0.85 to 1.15, or taps that scatter more than
 *         TIKU_TRK_FIT_SCATTER pixels rms about the line.
 * @note The tap farthest from the first line is dropped when it is more
 *       than TIKU_TRK_FIT_OUTLIER pixels off and three taps remain, and
 *       the line is fitted again through the rest.
 */
static inline int
tiku_trk_pointer_fit(const int *target, const int *reported, int n, int last,
                     int *lo, int *hi)
{
    double a, b, worst = 0, sum = 0;
    int i, far = -1, skip = -1, used;

    if (target == NULL || reported == NULL || lo == NULL || hi == NULL ||
        n < 3 || n > 64 ||
        tiku_trk_pointer_fit_line(target, reported, n, -1, &a, &b) != 0) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        double off = fabs(reported[i] - (a * target[i] + b));

        if (off > worst) {
            worst = off;
            far = i;
        }
    }
    if (worst > TIKU_TRK_FIT_OUTLIER && n > 3 &&
        tiku_trk_pointer_fit_line(target, reported, n, far, &a, &b) == 0) {
        skip = far;
    } else if (tiku_trk_pointer_fit_line(target, reported, n, -1, &a,
                                         &b) != 0) {
        return -1;
    }
    used = (skip >= 0) ? n - 1 : n;
    for (i = 0; i < n; i++) {
        if (i != skip) {
            double off = reported[i] - (a * target[i] + b);

            sum += off * off;
        }
    }
    if (a < 0.85 || a > 1.15 || sqrt(sum / used) > TIKU_TRK_FIT_SCATTER) {
        return -1;
    }
    *lo = (int)lround(b);
    *hi = (int)lround(a * last + b);
    return 0;
}

#endif
