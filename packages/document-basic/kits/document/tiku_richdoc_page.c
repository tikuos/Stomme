/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_page.c - checked single-section paper and margin properties.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include <math.h>

tiku_rich_status_t
tiku_rich_page_setup(const tiku_rich_page_setup_t *input,
                     tiku_rich_page_setup_t *out)
{
    tiku_rich_page_setup_t page;

    if (!input || !out) return TIKU_RICH_INVALID;
    page = *input;
    if (!page.width && !page.height && !page.top && !page.right &&
        !page.bottom && !page.left)
        page = (tiku_rich_page_setup_t){595.2756f, 841.8898f, 54, 54, 54, 54};
    if (!isfinite(page.width) || !isfinite(page.height) ||
        !isfinite(page.top) || !isfinite(page.right) ||
        !isfinite(page.bottom) || !isfinite(page.left) ||
        page.width < 72 || page.width > 14400 || page.height < 72 ||
        page.height > 14400 || page.top < 0 || page.right < 0 ||
        page.bottom < 0 || page.left < 0 ||
        (double)page.top + page.bottom > page.height - 36 ||
        (double)page.left + page.right > page.width - 36)
        return TIKU_RICH_INVALID;
    *out = page;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richsnapshot_page(const tiku_richsnapshot_t *snapshot,
                       tiku_rich_page_setup_t *out)
{
    if (!snapshot || !out) return TIKU_RICH_INVALID;
    *out = snapshot->root->page;
    return TIKU_RICH_OK;
}
