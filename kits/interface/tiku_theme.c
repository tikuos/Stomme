/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_theme.c - the one table every semantic colour reads through.
 *
 * The default is the BeOS R5 look.  A table handed to tiku_theme_set()
 * recolours every widget, since every semantic colour is read through
 * here; layout does not change with the theme.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_gfx.h"

static const tiku_theme_t tiku_theme_r5 = {
    /* panel    */ TIKU_RGB(216, 216, 216),
    /* doc      */ TIKU_RGB(255, 255, 255),
    /* text     */ TIKU_RGB(0, 0, 0),
    /* tab      */ TIKU_RGB(255, 203, 0),
    /* tab_idle */ TIKU_RGB(232, 232, 232),
    /* select   */ TIKU_RGB(51, 102, 152),
    /* seltext  */ TIKU_RGB(255, 255, 255),
    /* focus    */ TIKU_RGB(0, 0, 229),
    /* backdrop */ TIKU_RGB(51, 102, 152),
    /* danger   */ TIKU_RGB(203, 48, 48),
    /* note     */ TIKU_RGB(255, 255, 200),
    /* tab_text */ TIKU_RGB(0, 0, 0),
    /* code_word*/ TIKU_RGB(0, 0, 176),
    /* code_num */ TIKU_RGB(0, 112, 96),
    /* code_str */ TIKU_RGB(160, 32, 32),
    /* code_rem */ TIKU_RGB(96, 112, 96),
    /* success  */ TIKU_RGB(46, 125, 50),
    /* warn     */ TIKU_RGB(178, 106, 0),
};

/*
 * The dark table: the surfaces darken and the ink lightens.  The tab keeps
 * its amber, deepened so a dark screen has no bright bar, the selection
 * keeps its blue, and the bevels follow because they are tints of the
 * panel.
 */
static const tiku_theme_t tiku_theme_dusk = {
    /* panel    */ TIKU_RGB(58, 58, 58),
    /* doc      */ TIKU_RGB(30, 30, 30),
    /* text     */ TIKU_RGB(222, 222, 222),
    /* tab      */ TIKU_RGB(110, 88, 12),
    /* tab_idle */ TIKU_RGB(70, 70, 70),
    /* select   */ TIKU_RGB(66, 113, 166),
    /* seltext  */ TIKU_RGB(255, 255, 255),
    /* focus    */ TIKU_RGB(96, 96, 255),
    /* backdrop */ TIKU_RGB(28, 42, 58),
    /* danger   */ TIKU_RGB(224, 82, 82),
    /* note     */ TIKU_RGB(72, 72, 46),
    /* tab_text */ TIKU_RGB(235, 221, 170),
    /* code_word*/ TIKU_RGB(140, 170, 255),
    /* code_num */ TIKU_RGB(120, 208, 184),
    /* code_str */ TIKU_RGB(236, 150, 140),
    /* code_rem */ TIKU_RGB(136, 156, 136),
    /* success  */ TIKU_RGB(88, 168, 92),
    /* warn     */ TIKU_RGB(214, 150, 48),
};

const tiku_theme_t *
tiku_theme_dark(void)
{
    return &tiku_theme_dusk;
}

/*
 * No hue at all.  Each role's grey is chosen by its job, so roles that
 * differ only in hue in the coloured tables stay apart here.  Every bevel
 * is a tint of the panel, so the table keeps greys between black and
 * white.
 */
static const tiku_theme_t tiku_theme_grey = {
    /* panel    */ TIKU_RGB(216, 216, 216),
    /* doc      */ TIKU_RGB(255, 255, 255),
    /* text     */ TIKU_RGB(0, 0, 0),
    /* tab      */ TIKU_RGB(140, 140, 140),
    /* tab_idle */ TIKU_RGB(232, 232, 232),
    /* select   */ TIKU_RGB(40, 40, 40),
    /* seltext  */ TIKU_RGB(255, 255, 255),
    /* focus    */ TIKU_RGB(64, 64, 64),
    /* backdrop */ TIKU_RGB(112, 112, 112),
    /* danger   */ TIKU_RGB(64, 64, 64),
    /* note     */ TIKU_RGB(240, 240, 240),
    /* tab_text */ TIKU_RGB(0, 0, 0),
    /* Without hue the four code inks part by luminance alone: the text
     * stays black, a reserved word sits just off it and a remark stands
     * furthest back. */
    /* code_word*/ TIKU_RGB(32, 32, 32),
    /* code_num */ TIKU_RGB(96, 96, 96),
    /* code_str */ TIKU_RGB(64, 64, 64),
    /* code_rem */ TIKU_RGB(144, 144, 144),
    /* Success sits darker than danger and warning lighter, so the three
     * still differ. */
    /* success  */ TIKU_RGB(40, 40, 40),
    /* warn     */ TIKU_RGB(128, 128, 128),
};

const tiku_theme_t *
tiku_theme_mono(void)
{
    return &tiku_theme_grey;
}

static const tiku_theme_t *tiku_theme_live = &tiku_theme_r5;
static int tiku_theme_live_achromatic;

/**
 * @brief Whether every role in table @p t is a grey.
 *
 * Worked out from the table, so a table added later needs no flag of its
 * own.
 */
static int
achromatic(const tiku_theme_t *t)
{
    const tiku_rgb_t *role = (const tiku_rgb_t *)t;
    size_t n = sizeof *t / sizeof *role;
    size_t i;

    for (i = 0; i < n; i++) {
        unsigned r = (role[i] >> 16) & 0xFFu;
        unsigned g = (role[i] >> 8) & 0xFFu;
        unsigned b = role[i] & 0xFFu;

        if (r != g || g != b) {
            return 0;
        }
    }
    return 1;
}

int
tiku_theme_achromatic(void)
{
    return tiku_theme_live_achromatic;
}

tiku_rgb_t
tiku_grey(tiku_rgb_t c)
{
    /* Rec. 601 luminance, weighted as the eye sees: green counts most and
     * blue least. */
    unsigned r = (c >> 16) & 0xFFu;
    unsigned g = (c >> 8) & 0xFFu;
    unsigned b = c & 0xFFu;
    unsigned y = (299u * r + 587u * g + 114u * b) / 1000u;

    if (y > 255u) {
        y = 255u;
    }
    return TIKU_RGB(y, y, y);
}

const tiku_theme_t *
tiku_theme(void)
{
    return tiku_theme_live;
}

void
tiku_theme_set(const tiku_theme_t *theme)
{
    tiku_theme_live = (theme != NULL) ? theme : &tiku_theme_r5;
    tiku_theme_live_achromatic = achromatic(tiku_theme_live);
}
