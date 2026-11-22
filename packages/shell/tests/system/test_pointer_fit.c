/* The line a pointer calibration fits through its taps.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include "tiku_trk_pointer_fit.h"

int main(void)
{
    /* Nine taps on a three by three grid, as the calibration page asks,
     * on a panel that reports 17 pixels far at pixel 0 and 2 near at
     * pixel 799: reported = 0.976 * target + 17. */
    int target[9] = { 100, 400, 700, 100, 400, 700, 100, 400, 700 };
    int exact[9], noisy[9], lo = -1, hi = -1, i;
    const int jitter[9] = { 3, -4, 2, -3, 5, -2, 4, -5, 1 };

    for (i = 0; i < 9; i++) {
        exact[i] = (int)(0.976 * target[i] + 17.0 + 0.5);
        noisy[i] = exact[i] + jitter[i];
    }
    assert(tiku_trk_pointer_fit(target, exact, 9, 799, &lo, &hi) == 0);
    assert(lo >= 16 && lo <= 18 && hi >= 796 && hi <= 798);
    /* Taps scatter a few pixels: the line stays within three. */
    assert(tiku_trk_pointer_fit(target, noisy, 9, 799, &lo, &hi) == 0);
    assert(lo >= 14 && lo <= 20 && hi >= 794 && hi <= 800);
    /* One tap 40 pixels off is dropped, and the rest give the same line. */
    noisy[4] += 40;
    assert(tiku_trk_pointer_fit(target, noisy, 9, 799, &lo, &hi) == 0);
    assert(lo >= 14 && lo <= 20 && hi >= 794 && hi <= 800);
    /* Two taps that far off leave too much scatter: refused, and the
     * results are not touched. */
    noisy[0] -= 60;
    lo = hi = -7;
    assert(tiku_trk_pointer_fit(target, noisy, 9, 799, &lo, &hi) == -1);
    assert(lo == -7 && hi == -7);
    /* A panel a quarter too wide is no calibration; nor are two taps, or
     * targets all at one place. */
    for (i = 0; i < 9; i++) {
        noisy[i] = target[i] * 5 / 4;
    }
    assert(tiku_trk_pointer_fit(target, noisy, 9, 799, &lo, &hi) == -1);
    assert(tiku_trk_pointer_fit(target, exact, 2, 799, &lo, &hi) == -1);
    for (i = 0; i < 9; i++) {
        noisy[i] = 400;
    }
    assert(tiku_trk_pointer_fit(noisy, exact, 9, 799, &lo, &hi) == -1);
    assert(lo == -7 && hi == -7);
    puts("PASS: a pointer calibration's line drops one stray tap and "
         "refuses a bad fit");
    return 0;
}
