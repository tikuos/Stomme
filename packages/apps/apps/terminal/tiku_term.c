/* tiku_term.c -- Terminal: a shell on a pty of its own, written into an
 * 80x24 grid of cells by a VT100-subset parser, with a scrollback.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE 1
#define _GNU_SOURCE 1

#include "tiku_app_product.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "tiku_app.h"
#include "tiku_text_input.h"
#include "tiku_client.h"
#include "tiku_font.h"
#include "tiku_gfx.h"

#define COLS      80
#define ROWS      24
#define SCROLLBACK 500
#define STRIP_H   20
#define MARGIN    4
#define PARAMS_MAX 8

#define ATTR_BOLD    0x01u
#define ATTR_REVERSE 0x02u

#define CMD_CLEAR 1
#define CMD_RESET 2
#define CMD_QUIT  3

/** @brief One cell of the grid: its character, inks and attributes. */
typedef struct {
    char          ch;
    unsigned char fg;           /* 0-7 ANSI, 8 = the default ink   */
    unsigned char bg;           /* 0-7 ANSI, 8 = the default paper */
    unsigned char attr;
} cell_t;

/** @brief Where the parser stands in the shell's output. */
typedef enum {
    SCAN_TEXT = 0,
    SCAN_ESC,
    SCAN_CSI,
    SCAN_OSC,
    SCAN_SKIP                   /* the one byte left of a sequence */
} scan_t;

/** @brief The running terminal: its window, shell, grid and parser. */
typedef struct {
    const tiku_app_services_t *services;
    tiku_surface_t            *surface;
    uint32_t                        id;
    int                             master;
    pid_t                           child;

    cell_t                          cell[ROWS][COLS];
    cell_t                          back[SCROLLBACK][COLS];
    int                             back_count;
    int                             back_head;  /* oldest row       */
    int                             view;       /* rows scrolled up */

    int                             row, col;
    int                             save_row, save_col;
    int                             top, bottom;    /* scroll region */
    unsigned char                   fg, bg, attr;
    int                             wrap_next;
    int                             cursor_on;
    int                             app_cursor;     /* CSI ?1h      */

    scan_t                          scan;
    int                             param[PARAMS_MAX];
    int                             nparam;
    int                             question;       /* CSI ? ...    */

    int                             alive;
    int                             dirty;
    int                             cell_w, cell_h;
    tiku_text_input_t                commit;
} term_state_t;

/* The ANSI eight colours. */
static const tiku_rgb_t kAnsi[8] = {
    TIKU_RGB(0, 0, 0),       TIKU_RGB(178, 24, 24),
    TIKU_RGB(24, 154, 24),   TIKU_RGB(160, 132, 0),
    TIKU_RGB(40, 90, 190),   TIKU_RGB(160, 60, 170),
    TIKU_RGB(0, 150, 160),   TIKU_RGB(190, 190, 190)
};
/* Their bright forms, which a bold ink is drawn in. */
static const tiku_rgb_t kBright[8] = {
    TIKU_RGB(110, 110, 110), TIKU_RGB(230, 60, 60),
    TIKU_RGB(50, 200, 50),   TIKU_RGB(210, 180, 20),
    TIKU_RGB(80, 140, 240),  TIKU_RGB(210, 100, 220),
    TIKU_RGB(40, 200, 210),  TIKU_RGB(255, 255, 255)
};

/** @brief The window's width: the grid's columns and the margins. */
static int
width_px(const term_state_t *st)
{
    return COLS * st->cell_w + 2 * MARGIN;
}

/** @brief The window's height: the grid's rows, the margins and the
 *         status strip. */
static int
height_px(const term_state_t *st)
{
    return ROWS * st->cell_h + 2 * MARGIN + STRIP_H;
}

/** @brief An empty cell in the current colours. */
static cell_t
blank_of(const term_state_t *st)
{
    cell_t c;

    c.ch = ' ';
    c.fg = st->fg;
    c.bg = st->bg;
    c.attr = 0u;
    return c;
}

/** @brief Blank screen row @p r. */
static void
row_blank(term_state_t *st, int r)
{
    int c;

    for (c = 0; c < COLS; c++) {
        st->cell[r][c] = blank_of(st);
    }
}

/** @brief Blank the screen and put the cursor home. */
static void
grid_clear(term_state_t *st)
{
    int r;

    for (r = 0; r < ROWS; r++) {
        row_blank(st, r);
    }
    st->row = 0;
    st->col = 0;
    st->wrap_next = 0;
    st->dirty = 1;
}

/** @brief Keep the row leaving the top, so it can be scrolled back to. */
static void
remember(term_state_t *st, const cell_t *row)
{
    int slot = (st->back_head + st->back_count) % SCROLLBACK;

    memcpy(st->back[slot], row, sizeof st->back[0]);
    if (st->back_count < SCROLLBACK) {
        st->back_count++;
    } else {
        st->back_head = (st->back_head + 1) % SCROLLBACK;
    }
}

/** @brief Scroll the scroll region up a row, blanking its last. */
static void
scroll_region_up(term_state_t *st)
{
    int r;

    /* Only a scroll of the whole screen enters the scrollback; a scroll
     * inside a region belongs to the program that drew the region. */
    if (st->top == 0 && st->bottom == ROWS - 1) {
        remember(st, st->cell[0]);
    }
    for (r = st->top; r < st->bottom; r++) {
        memcpy(st->cell[r], st->cell[r + 1], sizeof st->cell[0]);
    }
    row_blank(st, st->bottom);
}

/** @brief Scroll the scroll region down a row, blanking its first. */
static void
scroll_region_down(term_state_t *st)
{
    int r;

    for (r = st->bottom; r > st->top; r--) {
        memcpy(st->cell[r], st->cell[r - 1], sizeof st->cell[0]);
    }
    row_blank(st, st->top);
}

/** @brief Move the cursor down a row; at the region's bottom the region
 *         scrolls instead. */
static void
newline(term_state_t *st)
{
    st->wrap_next = 0;
    if (st->row == st->bottom) {
        scroll_region_up(st);
    } else if (st->row + 1 < ROWS) {
        st->row++;
    }
}

/** @brief Put @p ch at the cursor in the current colours and move on; a
 *         wrap held at the last column happens first. */
static void
put_printable(term_state_t *st, char ch)
{
    cell_t *c;

    if (st->wrap_next) {
        st->col = 0;
        newline(st);
    }
    c = &st->cell[st->row][st->col];
    c->ch = ch;
    c->fg = st->fg;
    c->bg = st->bg;
    c->attr = st->attr;
    if (st->col + 1 >= COLS) {
        /* Held at the last column: a character there wraps only when the
         * next one arrives, so a line that exactly fills the width leaves
         * no blank row behind it. */
        st->wrap_next = 1;
    } else {
        st->col++;
    }
}

/** @brief Parameter @p i of the sequence, or @p fallback when it was not
 *         given. */
static int
param_or(const term_state_t *st, int i, int fallback)
{
    if (i >= st->nparam || st->param[i] < 0) {
        return fallback;
    }
    return st->param[i];
}

/** @brief Blank row @p r's cells @p from to @p to, inclusive. */
static void
erase_cells(term_state_t *st, int r, int from, int to)
{
    int c;

    for (c = from; c <= to && c < COLS; c++) {
        st->cell[r][c] = blank_of(st);
    }
}

/** @brief SGR: set the colours and attributes the parameters name; no
 *         parameter resets them. */
static void
select_graphics(term_state_t *st)
{
    int i;

    if (st->nparam == 0) {
        st->fg = 8u;
        st->bg = 8u;
        st->attr = 0u;
        return;
    }
    for (i = 0; i < st->nparam; i++) {
        int p = param_or(st, i, 0);

        if (p == 0) {
            st->fg = 8u;
            st->bg = 8u;
            st->attr = 0u;
        } else if (p == 1) {
            st->attr |= ATTR_BOLD;
        } else if (p == 7) {
            st->attr |= ATTR_REVERSE;
        } else if (p == 22) {
            st->attr &= ~ATTR_BOLD;
        } else if (p == 27) {
            st->attr &= ~ATTR_REVERSE;
        } else if (p >= 30 && p <= 37) {
            st->fg = (unsigned char)(p - 30);
        } else if (p == 39) {
            st->fg = 8u;
        } else if (p >= 40 && p <= 47) {
            st->bg = (unsigned char)(p - 40);
        } else if (p == 49) {
            st->bg = 8u;
        } else if (p >= 90 && p <= 97) {
            st->fg = (unsigned char)(p - 90);
            st->attr |= ATTR_BOLD;
        } else if (p >= 100 && p <= 107) {
            st->bg = (unsigned char)(p - 100);
        }
    }
}

/** @brief Act on a complete control sequence. */
static void
csi_dispatch(term_state_t *st, char final)
{
    int n = param_or(st, 0, 1);
    int r, c;

    switch (final) {
    case 'A':
        st->row -= (n < 1) ? 1 : n;
        break;
    case 'B':
        st->row += (n < 1) ? 1 : n;
        break;
    case 'C':
        st->col += (n < 1) ? 1 : n;
        break;
    case 'D':
        st->col -= (n < 1) ? 1 : n;
        break;
    case 'E':
        st->row += (n < 1) ? 1 : n;
        st->col = 0;
        break;
    case 'F':
        st->row -= (n < 1) ? 1 : n;
        st->col = 0;
        break;
    case 'G':
        st->col = n - 1;
        break;
    case 'd':
        st->row = n - 1;
        break;
    case 'H':
    case 'f':
        st->row = param_or(st, 0, 1) - 1;
        st->col = param_or(st, 1, 1) - 1;
        break;
    case 'J':
        n = param_or(st, 0, 0);
        if (n == 0) {
            erase_cells(st, st->row, st->col, COLS - 1);
            for (r = st->row + 1; r < ROWS; r++) {
                row_blank(st, r);
            }
        } else if (n == 1) {
            erase_cells(st, st->row, 0, st->col);
            for (r = 0; r < st->row; r++) {
                row_blank(st, r);
            }
        } else {
            for (r = 0; r < ROWS; r++) {
                row_blank(st, r);
            }
        }
        break;
    case 'K':
        n = param_or(st, 0, 0);
        if (n == 0) {
            erase_cells(st, st->row, st->col, COLS - 1);
        } else if (n == 1) {
            erase_cells(st, st->row, 0, st->col);
        } else {
            row_blank(st, st->row);
        }
        break;
    case 'L':
        for (r = 0; r < n; r++) {
            int keep = st->top;

            st->top = st->row;
            scroll_region_down(st);
            st->top = keep;
        }
        break;
    case 'M':
        for (r = 0; r < n; r++) {
            int keep = st->top;

            st->top = st->row;
            scroll_region_up(st);
            st->top = keep;
        }
        break;
    case 'P':
        for (c = st->col; c < COLS; c++) {
            st->cell[st->row][c] = (c + n < COLS)
                                       ? st->cell[st->row][c + n]
                                       : blank_of(st);
        }
        break;
    case '@':
        for (c = COLS - 1; c >= st->col; c--) {
            st->cell[st->row][c] = (c - n >= st->col)
                                       ? st->cell[st->row][c - n]
                                       : blank_of(st);
        }
        break;
    case 'S':
        for (r = 0; r < n; r++) {
            scroll_region_up(st);
        }
        break;
    case 'T':
        for (r = 0; r < n; r++) {
            scroll_region_down(st);
        }
        break;
    case 'm':
        select_graphics(st);
        break;
    case 'r':
        st->top = param_or(st, 0, 1) - 1;
        st->bottom = param_or(st, 1, ROWS) - 1;
        if (st->top < 0) {
            st->top = 0;
        }
        if (st->bottom >= ROWS) {
            st->bottom = ROWS - 1;
        }
        if (st->top >= st->bottom) {
            st->top = 0;
            st->bottom = ROWS - 1;
        }
        st->row = st->top;
        st->col = 0;
        break;
    case 'h':
    case 'l':
        for (c = 0; st->question && c < st->nparam; c++) {
            if (param_or(st, c, 0) == 25) {
                st->cursor_on = (final == 'h');
            } else if (param_or(st, c, 0) == 1) {
                st->app_cursor = (final == 'h');
            }
        }
        break;
    case 's':
        st->save_row = st->row;
        st->save_col = st->col;
        break;
    case 'u':
        st->row = st->save_row;
        st->col = st->save_col;
        break;
    default:
        break;                  /* a sequence this build does not know */
    }
    if (st->row < 0) {
        st->row = 0;
    }
    if (st->row >= ROWS) {
        st->row = ROWS - 1;
    }
    if (st->col < 0) {
        st->col = 0;
    }
    if (st->col >= COLS) {
        st->col = COLS - 1;
    }
    st->wrap_next = 0;
}

/** @brief Feed the parser one byte of the shell's output. */
static void
feed(term_state_t *st, unsigned char ch)
{
    switch (st->scan) {
    case SCAN_ESC:
        if (ch == '[') {
            st->scan = SCAN_CSI;
            st->nparam = 0;
            st->question = 0;
            memset(st->param, 0, sizeof st->param);
            st->param[0] = -1;
            return;
        }
        if (ch == ']') {
            st->scan = SCAN_OSC;
            return;
        }
        if (ch == '(' || ch == ')' || ch == '#' || ch == '%') {
            st->scan = SCAN_SKIP;   /* a charset or line size: skipped */
            return;
        }
        if (ch == 'M') {
            /* Reverse index, which a pager scrolling backwards uses.
             * ESC 7 and ESC 8 save and restore the cursor, ESC c resets;
             * any other escape without a bracket is ignored. */
            if (st->row == st->top) {
                scroll_region_down(st);
            } else if (st->row > 0) {
                st->row--;
            }
        } else if (ch == '7') {
            st->save_row = st->row;
            st->save_col = st->col;
        } else if (ch == '8') {
            st->row = st->save_row;
            st->col = st->save_col;
            st->wrap_next = 0;
        } else if (ch == 'c') {
            grid_clear(st);
            st->fg = 8u;
            st->bg = 8u;
            st->attr = 0u;
            st->app_cursor = 0;
        }
        st->scan = SCAN_TEXT;
        return;
    case SCAN_SKIP:
        st->scan = SCAN_TEXT;
        return;
    case SCAN_OSC:
        /* A window title, usually.  Ends at BEL, or at ESC and one more. */
        if (ch == 0x07) {
            st->scan = SCAN_TEXT;
        } else if (ch == 0x1b) {
            st->scan = SCAN_SKIP;
        }
        return;
    case SCAN_CSI:
        if (ch == '?') {
            st->question = 1;
            return;
        }
        if (ch >= '0' && ch <= '9') {
            if (st->nparam == 0) {
                st->nparam = 1;
                st->param[0] = 0;
            }
            if (st->param[st->nparam - 1] < 0) {
                st->param[st->nparam - 1] = 0;
            }
            st->param[st->nparam - 1] =
                st->param[st->nparam - 1] * 10 + (ch - '0');
            return;
        }
        if (ch == ';') {
            if (st->nparam == 0) {
                st->nparam = 1;
                st->param[0] = -1;
            }
            if (st->nparam < PARAMS_MAX) {
                st->param[st->nparam++] = -1;
            }
            return;
        }
        if (ch >= '@' && ch <= '~') {
            csi_dispatch(st, (char)ch);
            st->scan = SCAN_TEXT;
            return;
        }
        return;                 /* an intermediate byte: ignored */
    case SCAN_TEXT:
    default:
        break;
    }

    switch (ch) {
    case 0x1b:
        st->scan = SCAN_ESC;
        return;
    case '\n':
        newline(st);
        return;
    case '\r':
        st->col = 0;
        st->wrap_next = 0;
        return;
    case '\b':
        if (st->col > 0) {
            st->col--;
        }
        st->wrap_next = 0;
        return;
    case '\t':
        st->col = (st->col + 8) & ~7;
        if (st->col >= COLS) {
            st->col = COLS - 1;
        }
        return;
    case 0x07:
        return;                 /* the bell is not sounded */
    default:
        break;
    }
    if (ch >= 32u && ch < 127u) {
        put_printable(st, (char)ch);
    }
}

/** @brief The colour of palette entry @p index: above 7, the default ink
 *         or paper; a bold ink is the bright form. */
static tiku_rgb_t
ink_of(unsigned char index, unsigned char attr, int is_bg)
{
    if (index > 7u) {
        return is_bg ? TIKU_C_DOC : TIKU_C_TEXT;
    }
    return ((attr & ATTR_BOLD) != 0u && !is_bg) ? kBright[index]
                                                : kAnsi[index];
}

/** @brief The row shown at screen line @p r, live or scrolled back. */
static const cell_t *
row_at(const term_state_t *st, int r)
{
    int behind = st->view - r;  /* how far into history this line is */

    if (behind > 0) {
        int slot = (st->back_head + st->back_count - behind) % SCROLLBACK;

        return st->back[slot];
    }
    return st->cell[r - st->view];
}

/** @brief Draw the grid, live or scrolled back, the cursor and the status
 *         strip, and hand the frame to the desktop. */
static void
paint(term_state_t *st)
{
    const tiku_font_t *plain = tiku_font_mono(0);
    const tiku_font_t *bold = tiku_font_mono(1);
    const tiku_font_t *small = tiku_font_at(11);
    int w = width_px(st);
    int h = height_px(st);
    tiku_rect_t page = { 0, 0, w, h - STRIP_H };
    tiku_rect_t strip = { 0, h - STRIP_H, w, STRIP_H };
    char note[96];
    int r, c;

    tiku_fill(st->surface, page, TIKU_C_DOC);
    for (r = 0; r < ROWS; r++) {
        const cell_t *line = row_at(st, r);
        int y = MARGIN + r * st->cell_h;

        for (c = 0; c < COLS; c++) {
            const cell_t *cell = &line[c];
            tiku_rgb_t fg = ink_of(cell->fg, cell->attr, 0);
            tiku_rgb_t bg = ink_of(cell->bg, cell->attr, 1);
            char one[2];

            if ((cell->attr & ATTR_REVERSE) != 0u) {
                tiku_rgb_t swap = fg;

                fg = bg;
                bg = swap;
            }
            if (cell->bg <= 7u || (cell->attr & ATTR_REVERSE) != 0u) {
                tiku_fill(st->surface,
                               (tiku_rect_t){ MARGIN + c * st->cell_w,
                                                   y, st->cell_w,
                                                   st->cell_h }, bg);
            }
            if (cell->ch == ' ' || cell->ch == '\0') {
                continue;
            }
            one[0] = cell->ch;
            one[1] = '\0';
            tiku_text(st->surface,
                           ((cell->attr & ATTR_BOLD) != 0u) ? bold : plain,
                           MARGIN + c * st->cell_w, y + plain->ascent, one,
                           fg);
        }
    }
    if (st->alive && st->cursor_on && st->view == 0) {
        /* A block where the next character will land, with whatever is
         * already there drawn back over it. */
        tiku_rect_t at = { MARGIN + st->col * st->cell_w,
                                MARGIN + st->row * st->cell_h,
                                st->cell_w, st->cell_h };

        tiku_fill(st->surface, at, TIKU_C_SELECT);
        if (st->cell[st->row][st->col].ch > ' ') {
            char one[2];

            one[0] = st->cell[st->row][st->col].ch;
            one[1] = '\0';
            tiku_text(st->surface, plain, at.x, at.y + plain->ascent,
                           one, TIKU_C_SELTEXT);
        }
    }
    tiku_fill(st->surface, strip, TIKU_C_PANEL);
    tiku_hline(st->surface, 0, strip.y, w,
                    tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    if (!st->alive) {
        snprintf(note, sizeof note, "the shell has exited");
    } else if (st->view > 0) {
        snprintf(note, sizeof note, "%d line%s back  --  shift+page down "
                 "to return", st->view, st->view == 1 ? "" : "s");
    } else {
        snprintf(note, sizeof note, "%dx%d  vt100", COLS, ROWS);
    }
    tiku_text(st->surface, small, MARGIN,
                   strip.y + (STRIP_H - small->height) / 2 + small->ascent,
                   note, TIKU_C_TEXT);
    (void)st->services->frame(st->services->ctx, st->id, st->surface->px,
                              w, h);
    st->dirty = 0;
}

/** @brief Publish the Terminal menu: Clear, Reset and Quit. */
static void
publish(term_state_t *st)
{
    tiku_menuset_t menus;

    memset(&menus, 0, sizeof menus);
    menus.nmenu = 1;
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, TIKU_PRODUCT_TERMINAL_NAME);
    menus.menu[0].nitem = 3;
    snprintf(menus.menu[0].item[0].label,
             sizeof menus.menu[0].item[0].label, "Clear");
    menus.menu[0].item[0].command = CMD_CLEAR;
    menus.menu[0].item[0].enabled = 1;
    snprintf(menus.menu[0].item[1].label,
             sizeof menus.menu[0].item[1].label, "Reset");
    menus.menu[0].item[1].command = CMD_RESET;
    menus.menu[0].item[1].enabled = 1;
    snprintf(menus.menu[0].item[2].label,
             sizeof menus.menu[0].item[2].label, "Quit");
    menus.menu[0].item[2].command = CMD_QUIT;
    menus.menu[0].item[2].enabled = 1;
    (void)st->services->menus(st->services->ctx, st->id, &menus);
}

/**
 * @brief Start $SHELL (else /bin/sh) as an interactive shell on a pty of
 *        its own, in a new session with the pty as its controlling
 *        terminal, which job control needs.
 * @return 0, or -1 when the pty or the fork fails.
 */
static int
start_shell(term_state_t *st)
{
    char slave_name[128];
    struct winsize ws;
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    pid_t kid;

    if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0 ||
        ptsname_r(master, slave_name, sizeof slave_name) != 0) {
        return -1;
    }
    memset(&ws, 0, sizeof ws);
    ws.ws_col = COLS;
    ws.ws_row = ROWS;
    (void)ioctl(master, TIOCSWINSZ, &ws);

    kid = fork();
    if (kid < 0) {
        (void)close(master);
        return -1;
    }
    if (kid == 0) {
        const char *shell = getenv("SHELL");
        int slave;

        (void)close(master);
        (void)setsid();
        slave = open(slave_name, O_RDWR);
        if (slave < 0) {
            _exit(127);
        }
        (void)ioctl(slave, TIOCSCTTY, 0);
        (void)dup2(slave, 0);
        (void)dup2(slave, 1);
        (void)dup2(slave, 2);
        if (slave > 2) {
            (void)close(slave);
        }
        /* Programs run here are told TERM=vt100 and send what that
         * terminfo entry names; feed() is to handle all of it. */
        (void)setenv("TERM", "vt100", 1);
        if (shell == NULL || shell[0] == '\0') {
            shell = "/bin/sh";
        }
        (void)execl(shell, shell, "-i", (char *)NULL);
        (void)execl("/bin/sh", "sh", "-i", (char *)NULL);
        _exit(127);
    }
    (void)fcntl(master, F_SETFL, O_NONBLOCK);
    st->master = master;
    st->child = kid;
    st->alive = 1;
    return 0;
}

/** @brief Write @p n bytes to the shell while it is alive. */
static void
send_bytes(term_state_t *st, const char *bytes, size_t n)
{
    if (st->alive) {
        (void)write(st->master, bytes, n);
        /* Typing returns the view to the live screen, where what is
         * typed shows. */
        if (st->view != 0) {
            st->view = 0;
            st->dirty = 1;
        }
    }
}

/** @brief Open the window and start the shell; when none starts, the
 *         screen says so. */
static int
term_start(void **state, const tiku_app_services_t *services)
{
    term_state_t *st = calloc(1, sizeof *st);

    if (st == NULL) {
        return -1;
    }
    st->services = services;
    st->master = -1;
    st->fg = 8u;
    st->bg = 8u;
    st->top = 0;
    st->bottom = ROWS - 1;
    st->cursor_on = 1;
    /* Every glyph of the mono face advances the same width, so a column
     * is that width. */
    st->cell_w = tiku_font_mono_cell(0);
    st->cell_h = tiku_font_mono(0)->height + 1;
    st->surface = tiku_surface_new(width_px(st), height_px(st),
                                        TIKU_C_DOC);
    if (st->surface == NULL) {
        free(st);
        return -1;
    }
    grid_clear(st);
    if (start_shell(st) != 0) {
        const char *sorry = "no shell could be started";
        int i;

        for (i = 0; sorry[i] != '\0'; i++) {
            feed(st, (unsigned char)sorry[i]);
        }
    }
    st->id = services->open(services->ctx, TIKU_PRODUCT_TERMINAL_NAME, width_px(st),
                            height_px(st));
    if (services->help_page != NULL) {
        (void)services->help_page(services->ctx, "apps/terminal.html");
    }
    paint(st);
    publish(st);
    *state = st;
    return 0;
}

/** @brief Hang up the shell and free the window. */
static void
term_stop(void *state)
{
    term_state_t *st = state;

    if (st == NULL) {
        return;
    }
    if (st->child > 0) {
        (void)kill(st->child, SIGHUP);
        (void)waitpid(st->child, NULL, WNOHANG);
    }
    if (st->master >= 0) {
        (void)close(st->master);
    }
    tiku_surface_free(st->surface);
    free(st);
}

/** @brief Each turn: feed the shell's output to the parser, notice when
 *         the shell has ended, and repaint when anything changed. */
static void
term_tick(void *state, int64_t now_us)
{
    term_state_t *st = state;
    char buf[8192];
    ssize_t n;

    (void)now_us;
    if (st->master < 0) {
        return;
    }
    while ((n = read(st->master, buf, sizeof buf)) > 0) {
        ssize_t i;

        for (i = 0; i < n; i++) {
            feed(st, (unsigned char)buf[i]);
        }
        st->dirty = 1;
    }
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
                   errno != EINTR)) {
        if (st->alive) {
            st->alive = 0;
            st->dirty = 1;
        }
    }
    if (st->child > 0 && waitpid(st->child, NULL, WNOHANG) == st->child) {
        st->child = -1;
        st->alive = 0;
        st->dirty = 1;
    }
    if (st->dirty) {
        paint(st);
    }
}

/** @brief Move the view @p by rows into what has gone past. */
static void
scroll_view(term_state_t *st, int by)
{
    int limit = st->back_count;

    st->view += by;
    if (st->view > limit) {
        st->view = limit;
    }
    if (st->view < 0) {
        st->view = 0;
    }
    st->dirty = 1;
    paint(st);
}

/** @brief Typed text and keys go to the shell as the bytes a terminal
 *         sends; Shift with Page Up or Page Down scrolls the history. */
static int
term_event(void *state, const tiku_event_t *event)
{
    term_state_t *st = state;
    const char *seq = NULL;
    char one;

    int commit = tiku_text_input_event(&st->commit, event);
    if (event->type == TIKU_EVENT_TEXT_INPUT) {
        if (commit == TIKU_TEXT_COMPLETE)
            send_bytes(st, st->commit.text, (size_t)st->commit.length);
        return 0;
    }
    if (event->type != TIKU_EVENT_KEY_DOWN) {
        return 0;
    }
    /* Shift with a paging key scrolls the history and is not sent to the
     * shell. */
    if ((event->modifiers & TIKU_MOD_SHIFT) != 0u) {
        if (event->key == TIKU_KEY_PAGE_UP) {
            scroll_view(st, ROWS - 1);
            return 0;
        }
        if (event->key == TIKU_KEY_PAGE_DOWN) {
            scroll_view(st, -(ROWS - 1));
            return 0;
        }
    }
    if ((event->modifiers & TIKU_MOD_CMD) != 0u) {
        /* Ctrl with a letter sends its control code, the letter's place
         * in the alphabet (Ctrl+C is 3); other Ctrl keys send nothing. */
        unsigned k = event->key;

        if (k >= 'a' && k <= 'z') {
            one = (char)(k - 'a' + 1);
            send_bytes(st, &one, 1u);
        } else if (k >= 'A' && k <= 'Z') {
            one = (char)(k - 'A' + 1);
            send_bytes(st, &one, 1u);
        }
        return 0;
    }
    switch (event->key) {
    case TIKU_KEY_RETURN:
        one = '\r';
        send_bytes(st, &one, 1u);
        return 0;
    case TIKU_KEY_BACKSPACE:
        one = 0x7f;
        send_bytes(st, &one, 1u);
        return 0;
    case TIKU_KEY_TAB:
        one = '\t';
        send_bytes(st, &one, 1u);
        return 0;
    case TIKU_KEY_ESCAPE:
        one = 0x1b;
        send_bytes(st, &one, 1u);
        return 0;
    /* The sequences line editors and full-screen editors read the arrow
     * and editing keys from; after CSI ?1h the arrows send ESC O and a
     * letter, as the vt100 entry's key names give them. */
    case TIKU_KEY_UP:
        seq = st->app_cursor ? "\x1bOA" : "\x1b[A";
        break;
    case TIKU_KEY_DOWN:
        seq = st->app_cursor ? "\x1bOB" : "\x1b[B";
        break;
    case TIKU_KEY_RIGHT:
        seq = st->app_cursor ? "\x1bOC" : "\x1b[C";
        break;
    case TIKU_KEY_LEFT:
        seq = st->app_cursor ? "\x1bOD" : "\x1b[D";
        break;
    case TIKU_KEY_HOME:
        seq = "\x1b[H";
        break;
    case TIKU_KEY_END:
        seq = "\x1b[F";
        break;
    case TIKU_KEY_DELETE:
        seq = "\x1b[3~";
        break;
    case TIKU_KEY_PAGE_UP:
        seq = "\x1b[5~";
        break;
    case TIKU_KEY_PAGE_DOWN:
        seq = "\x1b[6~";
        break;
    default: {
        /* The typed character is in the event's text; its key is the
         * case-folded keysym that shortcuts match, which has lost the
         * case Shift gave it. */
        unsigned ch = (unsigned char)event->text[0];

        if (ch == 0u) {
            ch = event->key;
        }
        if (ch >= 32u && ch < 127u) {
            one = (char)ch;
            send_bytes(st, &one, 1u);
        }
        return 0;
    }
    }
    send_bytes(st, seq, strlen(seq));
    return 0;
}

/** @brief A menu pick: Clear, Reset, or Quit.  @return 1 to quit. */
static int
term_pick(void *state, uint32_t window, int command)
{
    term_state_t *st = state;

    (void)window;
    switch (command) {
    case CMD_QUIT:
        return 1;
    case CMD_CLEAR:
        grid_clear(st);
        paint(st);
        break;
    case CMD_RESET:
        /* Colours, attributes, scroll region, parser, cursor, cursor
         * keys and view go back to where they started, and the screen is
         * cleared; the shell keeps running. */
        st->fg = 8u;
        st->bg = 8u;
        st->attr = 0u;
        st->top = 0;
        st->bottom = ROWS - 1;
        st->scan = SCAN_TEXT;
        st->cursor_on = 1;
        st->app_cursor = 0;
        st->view = 0;
        grid_clear(st);
        paint(st);
        break;
    default:
        break;
    }
    return 0;
}

/** @brief Terminal's descriptor: exported to a loader, or run by main(). */
const tiku_app_descriptor_t tiku_term_app = {
    .id = TIKU_PRODUCT_TERMINAL_ID,
    .name = TIKU_PRODUCT_TERMINAL_NAME,
    .start = term_start,
    .stop = term_stop,
    .event = term_event,
    .tick = term_tick,
    .pick = term_pick
};

#ifdef TIKU_APP_SO
/** @brief The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t),
    &tiku_term_app
};
#endif

#ifndef TIKU_APP_EMBED
/** @brief Run Terminal as a client of the desktop. */
int
main(void)
{
    return tiku_client_run(&tiku_term_app);
}
#endif
