/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_about_window.c - the About box.
 *
 * The identity column on the left holds the logo mark in its colour cycle
 * and the wordmark.  Beside it are the release, the processor count, free
 * memory over a gauge and a button naming the newest report; the footer
 * under both gives the licence and the build stamp.  The connected boards
 * follow, one at a time, and the reports section opens under them.  The
 * box is an application descriptor: it paints its own surface, asks the
 * host for the PANEL placement role, and refits itself when the face or the
 * boards change.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_font.h"
#include "tiku_gfx.h"
#include "tiku_iconpaint.h"
#include "tiku_list.h"
#include "tiku_logo.h"
#include "tiku_press.h"
#include "tiku_report.h"
#include "tiku_sys.h"
#include "tiku_trk_about_window.h"
#include "tiku_trk_task.h"
#include "tiku_ui.h"
#include "tiku_window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The narrowest body width; it holds the identity column's margins. */
#define ABOUT_W 430

/* The release the box shows. */
#define ABOUT_VERSION TIKU_SHELL_ABOUT_VERSION

/*
 * The colour cycle.  Each set of colours is held for HOLD_US, then faded
 * into the next over FADE_US; after the last set the cycle starts again
 * at the first.  A fade is drawn in FADE_STEPS distinct frames, which is
 * the number of repaints it costs.
 */
#define HOLD_US     3000000
#define FADE_US     1200000
#define CYCLE_US    (HOLD_US + FADE_US)
#define FADE_STEPS  16

typedef struct {
    const tiku_app_services_t *services;
    tiku_surface_t            *surface;
    uint32_t                        id;
    /* Counted once at start; the memory is read again on every paint. */
    int                        ncpu;
    int64_t                    opened_us;      /* when the cycle started */
    /*
     * The colour-cycle step last painted and the second the gauge was
     * last read on; the tick repaints only when one of them changes.  An
     * event sets step to -1 to force a repaint.
     */
    int                        step;
        int64_t                    gauge_s;
    /* The newest report's path, named on the reports button. */
            char                       report[512];
    tiku_rect_t                reports_btn;    /* opens the reports section */
    /* The reports section, opened from the reports button: every report
     * newest first, and the chosen one's account. */
    int                        reports_open;
    char                       paths[64][512];
    int                        nreports;
    tiku_list_t                list;
    int                        chosen;         /* which path, or -1 */
    struct {
        char stopped[120];
        char last[120];
        char frames[4][120];
        int  nframes;
    }                          detail;
        tiku_rect_t                list_box;
    tiku_rect_t                open_btn;       /* "Open the file" */
    /* The boards connected to the desktop, as the shell last kept them,
     * for the devices section under the facts. */
    struct {
        char          name[64];     /* what it is mounted as       */
        char          mcu[32];      /* the processor on it         */
        unsigned long freq;         /* its clock, in hertz         */
        /* One per kind of memory the part has, from a board that sends
         * no maps: its SRAM, its non-volatile memory under the device's
         * name for it, and the extra tiers some parts carry. */
        struct {
            char          label[16];
            unsigned long freeb, total;
        }             mem[4];
        int           nmem;
        /* Each memory's map as the board reports it: its estates in
         * order, drawn as one bar with a legend. */
        struct {
            char          name[16];     /* SRAM, or the NVM's kind */
            unsigned long total;        /* the bank, or the part */
            struct {
                char          label[12];
                unsigned long size, used;   /* in use; stack: headroom */
                int           live;         /* used is meaningful */
            }             est[8];
            int           nest;
            unsigned long bank2;        /* a second bank, or 0 */
        }             lay[2];
        int           nlay;
        char          has[112];     /* what else is on it          */
        unsigned long part_sram;    /* the SRAM the part is fitted with */
        unsigned long part_nvm;
        char          kind[16];     /* what its NVM is called      */
        char          up[24];       /* seconds it has been running */
        char          boots[16];
        int           known;        /* the shell has an answer for it */
    }                          dev[6];
    int                        ndev;
    /* The board on show, and where its stepper's arrows were painted,
     * for the hit test. */
    int                        page;
    tiku_rect_t                prev_btn, next_btn;
    /* The button a press is holding: 1 reports, 2 prev, 3 next, 4 open. */
    tiku_press_t               press;
} about_state_t;

#define ABOUT_DEVS  6

#define REPORT_ROWS    5
#define ACCOUNT_LINES  7

/** @brief Which set, and how far into the fade, at @p now. */
static void
cycle_at(const about_state_t *st, int64_t now, int *set, float *toward)
{
    int64_t span = now - st->opened_us;
    int64_t into;

    if (span < 0) {
        span = 0;
    }
    into = span % CYCLE_US;
    *set = (int)(span / CYCLE_US);
    *toward = (into < HOLD_US)
                  ? 0.0f
                  : (float)(into - HOLD_US) / (float)FADE_US;
}

/** @brief The whole visible state of the cycle, as one number. */
static int
cycle_step(const about_state_t *st, int64_t now)
{
    int set;
    float t;

    cycle_at(st, now, &set, &t);
    return set * (FADE_STEPS + 1) + (int)(t * (float)FADE_STEPS);
}

/**
 * @brief The body's width: room for the widest memory line, in the face.
 *
 * Measured on a template of the widest numbers a host can report, so the
 * width does not change with the memory.  The facts column starts 150 in;
 * the result is at least ABOUT_W.
 */
static int about_body_w(void)
{
    int longest = tiku_text_width(tiku_font_plain(),
                                  "Memory: 000000 MB of 000000 MB free");
    int need = 150 + longest + 18;

    return (need > ABOUT_W) ? need : ABOUT_W;
}

/** @brief What the reports section adds to the box's height when open. */
static int reports_h(void)
{
    int h = tiku_font_plain()->height;

    /* A heading, the list, the account, a button row, and the margins
     * between, as paint_reports() lays them out. */
    return 8 + h + 5 + REPORT_ROWS * (h + 3) + 8 +
           ACCOUNT_LINES * (h + 2) + 8 + 6 + tiku_ui_button_h() + 12;
}

static void about_dev_facts(about_state_t *st, int at, const char *text);
static int legend_lines(about_state_t *st, tiku_surface_t *s, int x, int y,
                        int i, int m);
static void about_dev_estate(about_state_t *st, int at, const char *mem,
                             char *rest);

/**
 * @brief Re-read what the shell keeps about each connected board.
 *
 * Both services answer from the shell's copy, which starts a worker when
 * the copy is stale; the tick does not wait on a board, and the box shows
 * what each board last reported.
 */
static void
about_devices_refresh(about_state_t *st)
{
    char names[512];
    const char *p;

    st->ndev = 0;
    if (st->services == NULL || st->services->board_names == NULL ||
        st->services->device_stats == NULL) {
        return;                 /* no such services: no devices section */
    }
    if (st->services->board_names(st->services->ctx, names,
                                  sizeof names) <= 0) {
        return;
    }
    for (p = names; *p != '\0' && st->ndev < ABOUT_DEVS; ) {
        const char *nl = strchr(p, '\n');
        size_t len = (nl != NULL) ? (size_t)(nl - p) : strlen(p);
        char stats[1024];       /* two memory maps and the rest */
        int at = st->ndev;

        if (len == 0u) {
            break;
        }
        if (len >= sizeof st->dev[at].name) {
            len = sizeof st->dev[at].name - 1u;
        }
        memcpy(st->dev[at].name, p, len);
        st->dev[at].name[len] = '\0';
        st->dev[at].mcu[0] = '\0';
        st->dev[at].freq = 0UL;
        st->dev[at].nmem = 0;
        st->dev[at].nlay = 0;
        memset(st->dev[at].lay, 0, sizeof st->dev[at].lay);
        st->dev[at].part_sram = st->dev[at].part_nvm = 0UL;
        st->dev[at].kind[0] = '\0';
        st->dev[at].has[0] = '\0';
        st->dev[at].up[0] = '\0';
        st->dev[at].boots[0] = '\0';
        stats[0] = '\0';
        (void)st->services->device_stats(st->services->ctx,
                                         st->dev[at].name, stats,
                                         sizeof stats);
        st->dev[at].known = (stats[0] != '\0');
        about_dev_facts(st, at, stats);
        st->ndev++;
        p = (nl != NULL) ? nl + 1 : p + strlen(p);
    }
}

/** @brief A count of bytes as a person reads it. */
static void
fmt_bytes(unsigned long n, char *out, size_t max)
{
    if (n >= 1024UL * 1024UL) {
        /* Rounded to the nearest tenth of a megabyte. */
        unsigned long tenths = (n * 10UL + 512UL * 1024UL) /
                               (1024UL * 1024UL);

        snprintf(out, max, "%lu.%lu MB", tenths / 10UL, tenths % 10UL);
    } else if (n >= 1024UL) {
        snprintf(out, max, "%lu KB", n / 1024UL);
    } else {
        snprintf(out, max, "%lu bytes", n);
    }
}

/** @brief A clock as a person reads it. */
static void
fmt_hz(unsigned long hz, char *out, size_t max)
{
    if (hz >= 1000000UL) {
        snprintf(out, max, "%lu MHz", hz / 1000000UL);
    } else if (hz >= 1000UL) {
        snprintf(out, max, "%lu kHz", hz / 1000UL);
    } else {
        snprintf(out, max, "%lu Hz", hz);
    }
}

/** @brief Store one "mem<TAB>label<TAB>free<TAB>total" row. */
static void
about_dev_mem(about_state_t *st, int at, char *rest)
{
    char *freeb = strchr(rest, '\t');
    char *total;
    int k = st->dev[at].nmem;

    if (freeb == NULL || k >= 4) {
        return;
    }
    *freeb++ = '\0';
    total = strchr(freeb, '\t');
    if (total == NULL) {
        return;
    }
    *total++ = '\0';
    snprintf(st->dev[at].mem[k].label, sizeof st->dev[at].mem[k].label,
             "%s", rest);
    st->dev[at].mem[k].freeb = strtoul(freeb, NULL, 10);
    st->dev[at].mem[k].total = strtoul(total, NULL, 10);
    st->dev[at].nmem = k + 1;
}

/** @brief Store one "<mem>\t<key>\t<a>[\t<b>[\t<c>]]" map line. */
static void
about_dev_estate(about_state_t *st, int at, const char *mem, char *rest)
{
    char *f[4] = { rest, NULL, NULL, NULL };
    unsigned long v[3] = { 0UL, 0UL, 0UL };
    int nf = 1, i, k, which = (strcmp(mem, "nvm") == 0) ? 1 : 0;

    for (i = 1; i < 4; i++) {
        char *tab = strchr(f[i - 1], '\t');

        if (tab == NULL) {
            break;
        }
        *tab = '\0';
        f[i] = tab + 1;
        nf++;
    }
    for (i = 1; i < nf && i < 4; i++) {
        v[i - 1] = strtoul(f[i], NULL, 10);
    }
    if (which + 1 > st->dev[at].nlay) {
        st->dev[at].nlay = which + 1;
    }
    /* kind names the NVM, and total, bank and bank2 size the memory;
     * every other key is an estate. */
    if (which == 1 && strcmp(f[0], "kind") == 0 && nf > 1) {
        snprintf(st->dev[at].lay[1].name, sizeof st->dev[at].lay[1].name,
                 "%s", f[1]);
        return;
    }
    if (strcmp(f[0], "total") == 0 || strcmp(f[0], "bank") == 0) {
        st->dev[at].lay[which].total = v[0];
        if (which == 0) {
            snprintf(st->dev[at].lay[0].name,
                     sizeof st->dev[at].lay[0].name, "SRAM");
        }
        return;
    }
    if (strcmp(f[0], "bank2") == 0) {
        st->dev[at].lay[0].bank2 = v[0];
        return;
    }
    k = st->dev[at].lay[which].nest;
    if (k >= 8) {
        return;
    }
    snprintf(st->dev[at].lay[which].est[k].label,
             sizeof st->dev[at].lay[which].est[k].label, "%s", f[0]);
    /* stack: pool then headroom; tier: size, used, bank; code: window
     * then in use; store: capacity, then in use when the board says it;
     * the rest are a size alone. */
    st->dev[at].lay[which].est[k].size = v[0];
    if (strcmp(f[0], "stack") == 0) {
        /* For the stack, used holds the lowest headroom the board has
         * seen, which the legend prints as headroom; live stays 0. */
        st->dev[at].lay[which].est[k].used = v[1];
        st->dev[at].lay[which].est[k].live = 0;
    } else if (strcmp(f[0], "tier") == 0 || strcmp(f[0], "code") == 0 ||
               (strcmp(f[0], "store") == 0 && nf >= 3)) {
        st->dev[at].lay[which].est[k].used = v[1];
        st->dev[at].lay[which].est[k].live = 1;
        /* A tier in bank 2 is relabelled "tier2": the memory bar and its
         * legend leave it out, and the bank 2 line reports it. */
        if (which == 0 && strcmp(f[0], "tier") == 0 && v[2] == 2UL) {
            snprintf(st->dev[at].lay[which].est[k].label,
                     sizeof st->dev[at].lay[which].est[k].label, "tier2");
        }
    }
    st->dev[at].lay[which].nest = k + 1;
}

/** @brief One board's raw facts, as the shell reported them. */
static void
about_dev_facts(about_state_t *st, int at, const char *text)
{
    const char *p;

    for (p = text; *p != '\0'; ) {
        const char *nl = strchr(p, '\n');
        size_t len = (nl != NULL) ? (size_t)(nl - p) : strlen(p);
        char row[128], *tab;

        if (len == 0u) {
            break;
        }
        if (len >= sizeof row) {
            len = sizeof row - 1u;
        }
        memcpy(row, p, len);
        row[len] = '\0';
        tab = strchr(row, '\t');
        if (tab != NULL) {
            *tab++ = '\0';
            if (strcmp(row, "mcu") == 0) {
                snprintf(st->dev[at].mcu, sizeof st->dev[at].mcu, "%s",
                         tab);
            } else if (strcmp(row, "freq") == 0) {
                st->dev[at].freq = strtoul(tab, NULL, 10);
            } else if (strcmp(row, "up") == 0) {
                snprintf(st->dev[at].up, sizeof st->dev[at].up, "%s", tab);
            } else if (strcmp(row, "boots") == 0) {
                snprintf(st->dev[at].boots, sizeof st->dev[at].boots, "%s",
                         tab);
            } else if (strcmp(row, "part") == 0) {
                char *nvm = strchr(tab, '\t');

                if (nvm != NULL) {
                    char *kind;

                    *nvm++ = '\0';
                    st->dev[at].part_sram = strtoul(tab, NULL, 10);
                    kind = strchr(nvm, '\t');
                    if (kind != NULL) {
                        *kind++ = '\0';
                        snprintf(st->dev[at].kind,
                                 sizeof st->dev[at].kind, "%s", kind);
                    }
                    st->dev[at].part_nvm = strtoul(nvm, NULL, 10);
                }
            } else if (strcmp(row, "mem") == 0) {
                about_dev_mem(st, at, tab);
            } else if (strcmp(row, "sram") == 0 || strcmp(row, "nvm") == 0) {
                about_dev_estate(st, at, row, tab);
            } else if (strcmp(row, "has") == 0) {
                /* Joined into one comma-separated line. */
                size_t used = strlen(st->dev[at].has);

                snprintf(st->dev[at].has + used,
                         sizeof st->dev[at].has - used, "%s%s",
                         (used > 0u) ? ", " : "", tab);
            }
        }
        p = (nl != NULL) ? nl + 1 : p + strlen(p);
    }
}

/** @brief How many lines one board's block takes. */
static int
dev_rows(const about_state_t *st, int i)
{
    int rows = 2;               /* its name, and what it is */

    if (!st->dev[i].known) {
        return 2;               /* its name, and "asking..." */
    }
    if (st->dev[i].nlay > 0) {
        int m;

        for (m = 0; m < st->dev[i].nlay; m++) {
            rows += 1 + legend_lines((about_state_t *)st, NULL, 58 + 62, 0,
                                     i, m)
                    + (st->dev[i].lay[m].bank2 > 0UL ? 1 : 0);
        }
    } else {
        rows += st->dev[i].nmem;
    }
    if (st->dev[i].nlay == 0 &&
        (st->dev[i].part_sram > 0UL || st->dev[i].part_nvm > 0UL)) {
        rows++;
    }
    if (st->dev[i].has[0] != '\0' || st->dev[i].up[0] != '\0' ||
        st->dev[i].boots[0] != '\0') {
        rows++;                 /* the extras, uptime and boots: one line */
    }
    return rows;
}

/* A name and a hue per estate key, shared by the bar and the legend's
 * swatches; the legend's labels use the theme's text colour. */
static const struct { const char *key; const char *say; tiku_rgb_t hue; }
about_estates[] = {
    { "code",    "Code",    0x3B6EA5u },   /* the desktop's blue     */
    { "module",  "Modules", 0x6A4C93u },   /* violet                 */
    { "static",  "Static",  0x3B6EA5u },
    { "stack",   "Stack pool", 0xC0703Cu }, /* amber: the pool it grows in */
    { "guard",   "Guard",   0xB03A48u },
    { "unassigned", "Unassigned", 0x7A7A7Au },
    { "tier",    "Tier",    0x2E8B57u },   /* green                  */
    { "tier2",   "Tier",    0x2E8B57u },
    { "store",   "Store",   0x8A8A2Eu },   /* olive                  */
    { "persist", "Persist", 0xB03A48u },   /* carmine                */
};

/** @brief How @p key is named and coloured, or a plain grey default. */
static const char *
estate_look(const char *key, tiku_rgb_t *hue)
{
    size_t i;

    for (i = 0; i < sizeof about_estates / sizeof about_estates[0]; i++) {
        if (strcmp(about_estates[i].key, key) == 0) {
            *hue = about_estates[i].hue;
            return about_estates[i].say;
        }
    }
    *hue = 0x7A7A7Au;
    return key;
}

/**
 * @brief The legend table under one memory's bar: a header row, then each
 *        estate's swatch, name, size and usage.
 *
 * With @p s NULL nothing is drawn and the lines are only counted, which is
 * how dev_rows() measures what is painted.  The count includes the header
 * and the blank line that separates one memory from the next.
 * @return how many lines it took.
 */
static int
legend_lines(about_state_t *st, tiku_surface_t *s, int x, int y, int i,
             int m)
{
    const tiku_font_t *plain = tiku_font_plain();
    int room = st->surface->w - x - 14, k, lines = 1;
    int step = plain->height + 2;
    int size_right = x + room * 48 / 100;
    int usage_right = x + room - 6;
    tiku_rgb_t quiet = tiku_dim(TIKU_C_TEXT,
                                tiku_dim(TIKU_C_TEXT, TIKU_C_PANEL));

    if (s != NULL) {
        tiku_fill(s, (tiku_rect_t){ x, y, room, step },
                  tiku_tint(TIKU_C_PANEL, 0.95f));
        tiku_text(s, plain, x + 16, y + plain->ascent, "Region", quiet);
        tiku_text(s, plain, size_right - tiku_text_width(plain, "Size"),
                  y + plain->ascent, "Size", quiet);
        tiku_text(s, plain, usage_right - tiku_text_width(plain, "Usage"),
                  y + plain->ascent, "Usage", quiet);
    }

    for (k = 0; k < st->dev[i].lay[m].nest; k++) {
        char size[24], used[24], usage[48];
        tiku_rgb_t hue;
        const char *name;

        if (strcmp(st->dev[i].lay[m].est[k].label, "tier2") == 0) {
            continue;           /* in the second bank: its own line */
        }
        name = estate_look(st->dev[i].lay[m].est[k].label, &hue);
        fmt_bytes(st->dev[i].lay[m].est[k].size, size, sizeof size);
        fmt_bytes(st->dev[i].lay[m].est[k].used, used, sizeof used);
        if (strcmp(st->dev[i].lay[m].est[k].label, "stack") == 0) {
            snprintf(usage, sizeof usage, "%s headroom", used);
        } else if (st->dev[i].lay[m].est[k].live) {
            snprintf(usage, sizeof usage, "%s used", used);
        } else if (strcmp(st->dev[i].lay[m].est[k].label, "store") == 0) {
            /* The map reports this region's capacity, not file usage. */
            snprintf(usage, sizeof usage, "Files & user data");
        } else {
            snprintf(usage, sizeof usage, "Reserved");
        }
        y += step;
        if (s != NULL) {
            tiku_fill(s, (tiku_rect_t){ x, y, room, step },
                      tiku_tint(TIKU_C_PANEL, (lines % 2) ? 1.04f : 1.0f));
            tiku_fill(s, (tiku_rect_t){ x + 4, y + (plain->height - 6) / 2,
                                       6, 6 }, hue);
            tiku_text(s, plain, x + 16, y + plain->ascent, name, TIKU_C_TEXT);
            tiku_text(s, plain, size_right - tiku_text_width(plain, size),
                      y + plain->ascent, size, TIKU_C_TEXT);
            tiku_text(s, plain, usage_right - tiku_text_width(plain, usage),
                      y + plain->ascent, usage, TIKU_C_TEXT);
        }
        lines++;
        if (strcmp(st->dev[i].lay[m].est[k].label, "store") == 0 &&
            st->dev[i].lay[m].est[k].live) {
            unsigned long total = st->dev[i].lay[m].est[k].size;
            unsigned long taken = st->dev[i].lay[m].est[k].used;

            fmt_bytes(total > taken ? total - taken : 0UL, used, sizeof used);
            snprintf(usage, sizeof usage, "%s free", used);
            y += step;
            if (s != NULL) {
                tiku_text(s, plain, x + 16, y + plain->ascent,
                          "Files & data", quiet);
                tiku_text(s, plain,
                          usage_right - tiku_text_width(plain, usage),
                          y + plain->ascent, usage, TIKU_C_TEXT);
            }
            lines++;
        }
    }
    return lines + 1;
}

/**
 * @brief The table under a memory's bar, then a line for its second bank
 *        when there is one.
 * @return how many lines it took.
 */
static int
paint_legend(about_state_t *st, int x, int y, int i, int m)
{
    const tiku_font_t *plain = tiku_font_plain();
    int lines = legend_lines(st, st->surface, x + 62, y, i, m);

    if (st->dev[i].lay[m].bank2 > 0UL) {
        unsigned long tsz = 0UL, tused = 0UL;
        char said[128], b[24], t[24], u[24];
        int k;

        for (k = 0; k < st->dev[i].lay[m].nest; k++) {
            if (strcmp(st->dev[i].lay[m].est[k].label, "tier2") == 0) {
                tsz = st->dev[i].lay[m].est[k].size;
                tused = st->dev[i].lay[m].est[k].used;
            }
        }
        fmt_bytes(st->dev[i].lay[m].bank2, b, sizeof b);
        if (tsz > 0UL) {
            fmt_bytes(tsz, t, sizeof t);
            fmt_bytes(tused, u, sizeof u);
            snprintf(said, sizeof said, "bank 2 %s: tier %s, %s used",
                     b, t, u);
        } else {
            snprintf(said, sizeof said, "bank 2 %s", b);
        }
        tiku_text(st->surface, plain, x + 62,
                  y + lines * (plain->height + 2) + plain->ascent, said,
                  TIKU_C_TEXT);
        lines++;
    }
    return lines;
}

/**
 * @brief One memory as one bar: its estates side by side in the order
 *        they lie, each as wide as its share, the live ones showing their
 *        use inside, the total at the right and the legend underneath.
 * @return how many lines it took, or 0 when the memory has no total.
 */
static int
paint_layout(about_state_t *st, int x, int y, int i, int m)
{
    const tiku_font_t *plain = tiku_font_plain();
    int h = plain->height, k, px;
    unsigned long total = st->dev[i].lay[m].total, laid = 0UL;
    char a[24];
    /* The bar takes the box's width, less room for the total at its
     * right. */
    tiku_rect_t bar = { x + 62, y + 1, st->surface->w - (x + 62) - 78,
                        h - 2 };

    if (total == 0UL) {
        return 0;
    }
    tiku_text(st->surface, tiku_font_bold(), x, y + tiku_font_bold()->ascent,
              st->dev[i].lay[m].name, TIKU_C_TEXT);
    tiku_fill(st->surface, bar, tiku_tint(TIKU_C_PANEL, 1.12f));
    px = bar.x;
    for (k = 0; k < st->dev[i].lay[m].nest; k++) {
        unsigned long sz = st->dev[i].lay[m].est[k].size;
        tiku_rgb_t hue;
        int w;

        (void)estate_look(st->dev[i].lay[m].est[k].label, &hue);
        if (strcmp(st->dev[i].lay[m].est[k].label, "tier2") == 0) {
            continue;           /* lives in the second bank, drawn below */
        }
        if (sz > total - laid) {
            sz = total - laid;
        }
        w = (int)((unsigned long long)sz * (unsigned long long)bar.w / total);
        if (w < 1 && sz > 0UL) {
            w = 1;
        }
        /* The whole estate in its hue, lightened; the used part over it
         * in the hue itself. */
        tiku_fill(st->surface, (tiku_rect_t){ px, bar.y, w, bar.h },
                  tiku_tint(hue, 0.45f));
        if (!st->dev[i].lay[m].est[k].live && sz > 0UL) {
            tiku_fill(st->surface, (tiku_rect_t){ px, bar.y, w, bar.h },
                      hue);         /* region size; occupancy not reported */
        }
        if (st->dev[i].lay[m].est[k].live && sz > 0UL) {
            /* The used part, in the hue itself, from the left edge. */
            unsigned long u = st->dev[i].lay[m].est[k].used;
            int uw = (int)((unsigned long long)((u > sz) ? sz : u) *
                           (unsigned long long)w / sz);

            tiku_fill(st->surface, (tiku_rect_t){ px, bar.y, uw, bar.h },
                      hue);
        }
        tiku_vline(st->surface, px + w, bar.y, bar.h,
                   tiku_tint(TIKU_C_PANEL, 0.7f));
        px += w;
        laid += sz;
    }
    /* A sunken edge: shadow above and left, light below and right. */
    tiku_bevel(st->surface, bar, tiku_tint(TIKU_C_PANEL, 1.18f),
               tiku_tint(TIKU_C_PANEL, 0.72f));
    fmt_bytes(total, a, sizeof a);
    tiku_text(st->surface, plain, bar.x + bar.w + 8, y + plain->ascent, a,
              TIKU_C_TEXT);
    return 1 + paint_legend(st, x, y + h + 2, i, m);
}

/** @brief What the devices section takes, 0 when nothing is connected. */
static int
devices_h(const about_state_t *st)
{
    int h = tiku_font_plain()->height;
    int rows = 0, i;

    if (st->ndev <= 0) {
        return 0;
    }
    /* Only the board on show, plus the stepper's row when more than one
     * is connected. */
    i = (st->page >= 0 && st->page < st->ndev) ? st->page : 0;
    rows = dev_rows(st, i);
    return 10 + h + 8 + rows * (h + 2) + 10 + 6 +
           ((st->ndev > 1) ? tiku_ui_button_h() + 8 : 0);
}

/**
 * @brief A labelled gauge filled with the used part, and "free of total"
 *        beside it.
 */
static void
paint_room(about_state_t *st, int x, int y, const char *label,
           unsigned long freeb, unsigned long total)
{
    const tiku_font_t *plain = tiku_font_plain();
    char a[24], b[24], said[64];
    tiku_rect_t bar;

    tiku_text(st->surface, plain, x, y + plain->ascent, label,
              TIKU_C_TEXT);
    bar.x = x + 62;
    bar.y = y + 2;
    bar.w = 92;
    bar.h = plain->height - 4;
    if (bar.h < 6) {
        bar.h = 6;
    }
    tiku_ui_gauge(st->surface, bar,
                  (total > 0UL && total >= freeb)
                      ? (float)(total - freeb) / (float)total
                      : 0.0f);
    fmt_bytes(freeb, a, sizeof a);
    fmt_bytes(total, b, sizeof b);
    snprintf(said, sizeof said, "%s of %s free", a, b);
    tiku_text(st->surface, plain, bar.x + bar.w + 10, y + plain->ascent,
              said, TIKU_C_TEXT);
}

/**
 * @brief One board, from its mark down to its uptime line, at @p y.
 * @return the y below it.
 */
static int
paint_device(about_state_t *st, int i, int y)
{
    const tiku_font_t *plain = tiku_font_plain();
    const tiku_font_t *bold = tiku_font_bold();
    tiku_rgb_t quiet = tiku_dim(TIKU_C_TEXT,
                                tiku_dim(TIKU_C_TEXT, TIKU_C_PANEL));
    int h = plain->height, k;
    char said[128];
    int x = 58;

    (void)tiku_icon_paint(st->surface, "microcontroller", 18, y + 2,
                          32);
    tiku_text(st->surface, bold, x, y + bold->ascent,
              st->dev[i].name, TIKU_C_TEXT);
    y += h + 2;
    if (!st->dev[i].known) {
        /* The shell has no answer for this board: "asking..." in place
         * of its facts. */
        tiku_text(st->surface, plain, x, y + plain->ascent,
                  "asking...", quiet);
        return y + h + 2 + 10;
    }
    {
        char speed[24];

        fmt_hz(st->dev[i].freq, speed, sizeof speed);
        snprintf(said, sizeof said, "%s at %s",
                 st->dev[i].mcu[0] ? st->dev[i].mcu : "?", speed);
    }
    tiku_text(st->surface, plain, x, y + plain->ascent, said, quiet);
    y += h + 2;
    /* A board without maps: what the part is fitted with, on one line;
     * the gauges below show what is free to allocate. */
    if (st->dev[i].nlay == 0 &&
        (st->dev[i].part_sram > 0UL || st->dev[i].part_nvm > 0UL)) {
        char a[24], b[24];

        fmt_bytes(st->dev[i].part_sram, a, sizeof a);
        fmt_bytes(st->dev[i].part_nvm, b, sizeof b);
        snprintf(said, sizeof said, "Fitted with %s SRAM and %s %s",
                 a, b, st->dev[i].kind[0] ? st->dev[i].kind : "NVM");
        tiku_text(st->surface, plain, x, y + plain->ascent, said,
                  quiet);
        y += h + 2;
    }
    if (st->dev[i].nlay > 0) {
        int m;

        for (m = 0; m < st->dev[i].nlay; m++) {
            y += paint_layout(st, x, y, i, m) * (h + 2);
        }
    } else {
        for (k = 0; k < st->dev[i].nmem; k++) {
            paint_room(st, x, y, st->dev[i].mem[k].label,
                       st->dev[i].mem[k].freeb,
                       st->dev[i].mem[k].total);
            y += h + 2;
        }
    }
    if (st->dev[i].has[0] != '\0' || st->dev[i].up[0] != '\0' ||
        st->dev[i].boots[0] != '\0') {
        char up[48] = "";

        if (st->dev[i].up[0] != '\0' || st->dev[i].boots[0] != '\0') {
            snprintf(up, sizeof up, "up %s s, %s boots",
                     st->dev[i].up[0] ? st->dev[i].up : "?",
                     st->dev[i].boots[0] ? st->dev[i].boots : "?");
        }
        snprintf(said, sizeof said, "%s%s%s%s",
                 st->dev[i].has[0] ? "also " : "", st->dev[i].has,
                 (st->dev[i].has[0] && up[0]) ? " \xC2\xB7 " : "", up);
        tiku_text(st->surface, plain, x, y + plain->ascent, said,
                  quiet);
        y += h + 2;
    }
    y += 10;
    return y;
}

/**
 * @brief The devices section under the machine's own facts: a heading,
 *        a stepper when several boards are connected, and the board on
 *        show with its mark, name, part, memory and extras.
 *
 * A board that reports maps of its memory is drawn as layout bars with a
 * legend; one that does not, as a gauge per kind of memory.
 */
static void
paint_devices(about_state_t *st, int top)
{
    const tiku_font_t *plain = tiku_font_plain();
    const tiku_font_t *bold = tiku_font_bold();
    int h = plain->height;
    int y = top + 10;

    tiku_hline(st->surface, 14, y, st->surface->w - 28,
               tiku_tint(TIKU_C_PANEL, 0.80f));
    y += 8;
    tiku_text(st->surface, bold, 14, y + bold->ascent,
              "Connected devices", TIKU_C_TEXT);
    if (st->ndev > 1) {
        /* A stepper at the heading's right, the count between its
         * arrows. */
        char count[24];
        int bh = tiku_ui_button_h(), cw, bx = st->surface->w - 14;

        snprintf(count, sizeof count, "%d of %d", st->page + 1, st->ndev);
        cw = tiku_text_width(plain, count);
        st->next_btn = (tiku_rect_t){ bx - bh, y - 2, bh, bh };
        st->prev_btn = (tiku_rect_t){ bx - bh - 8 - cw - 8 - bh, y - 2,
                                       bh, bh };
        tiku_ui_button(st->surface, st->prev_btn, "",
                       ((st->page > 0) ? 0u : TIKU_S_DISABLED) |
                       (tiku_press_held(&st->press, 2) ? TIKU_S_PRESSED : 0u));
        tiku_ui_arrow(st->surface, st->prev_btn, 2, st->page > 0);
        tiku_text(st->surface, plain, st->prev_btn.x + bh + 8,
                  y - 2 + (bh - h) / 2 + plain->ascent, count, TIKU_C_TEXT);
        tiku_ui_button(st->surface, st->next_btn, "",
                       ((st->page + 1 < st->ndev) ? 0u : TIKU_S_DISABLED) |
                       (tiku_press_held(&st->press, 3) ? TIKU_S_PRESSED : 0u));
        tiku_ui_arrow(st->surface, st->next_btn, 3,
                      st->page + 1 < st->ndev);
    } else {
        st->prev_btn = st->next_btn = (tiku_rect_t){ 0, 0, 0, 0 };
    }
    y += h + 8;
    if (st->page < 0 || st->page >= st->ndev) {
        st->page = 0;
    }
    (void)paint_device(st, st->page, y);
}

/**
 * @brief The body's height: the taller of the two columns, plus the
 *        two-line footer.
 */
static int about_body_h(void)
{
    /* The facts column: 171 px at a 15 px line, growing by three line
     * heights with the face. */
    int facts = 171 + 3 * (tiku_font_plain()->height - 15);
    /* The identity column: 16 above, the 80 px mark, the wordmark, its
     * rule, and the line under the rule. */
    int identity = 16 + 80 + 14 + tiku_font_at(28)->height + 6 + 3 + 14 +
                   tiku_font_plain()->height + 20;

    /* The two-line footer runs under both columns: its first line sits in
     * the taller column's bottom margin, and the second is added here. */
        return ((identity > facts) ? identity : facts)
           + tiku_font_plain()->height + 4;
}

/**
 * @brief The whole box: the facts, the connected boards, and the reports
 *        when they are open.
 */
static int
about_h(const about_state_t *st)
{
    return about_body_h() + devices_h(st) +
           (st->reports_open ? reports_h() : 0);
}

/**
 * @brief The content width: the body less two window borders, so the
 *        window's outer width is about_body_w().
 *
 * tests/integration/shell/about.script checks pixels at this geometry.
 */
static int about_content_w(void)
{
    return about_body_w() - 2 * TIKU_WINDOW_BORDER;
}

/** @brief How many processors are online, at least 1. */
static int
count_cpus(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);

    return (n > 0) ? (int)n : 1;
}

/** @brief Total and available memory, in kilobytes. */
static void
read_memory(long *total_kb, long *avail_kb)
{
    tiku_sys_memory(total_kb, avail_kb);
}

/** @brief Kind and moment from a report's name, for its row. */
static void
report_label(const char *path, char *out, size_t max)
{
    const char *leaf = strrchr(path, '/');
    char kind[16] = "", stamp[16] = "";

    leaf = (leaf != NULL) ? leaf + 1 : path;
    if (sscanf(leaf, "%15[a-z]-%15[0-9-]", kind, stamp) == 2 &&
        strlen(stamp) >= 15u) {
        snprintf(out, max, "%-7s %.4s-%.2s-%.2s %.2s:%.2s:%.2s", kind,
                 stamp, stamp + 4, stamp + 6, stamp + 9, stamp + 11,
                 stamp + 13);
    } else {
        snprintf(out, max, "%.60s", leaf);
    }
}

/**
 * @brief A stack frame line from a report, made readable.
 *
 * The line's format depends on the host that wrote the report, so
 * tiku_sys_frame_name() reads it.
 */
static void
frame_name(const char *raw, char *out, size_t max)
{
    tiku_sys_frame_name(raw, out, max);
}

/** @brief Read the chosen report's account into the detail. */
static void
load_detail(about_state_t *st)
{
    FILE *f;
    char line[256];
    int in_frames = 0, want_last = 0;

    memset(&st->detail, 0, sizeof st->detail);
    if (st->chosen < 0 || st->chosen >= st->nreports) {
        return;
    }
    f = fopen(st->paths[st->chosen], "r");
    if (f == NULL) {
        snprintf(st->detail.stopped, sizeof st->detail.stopped,
                 "the file could not be read");
        return;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        line[strcspn(line, "\n")] = '\0';
        if (want_last) {
            const char *text = line;

            while (*text == ' ' || (*text >= '0' && *text <= '9')) {
                text++;         /* past the trail's own time stamp */
            }
            snprintf(st->detail.last, sizeof st->detail.last, "%.119s",
                     text);
            want_last = 0;
            continue;
        }
        if (strncmp(line, "stopped by: ", 12) == 0 ||
            strncmp(line, "asked for: ", 11) == 0) {
            snprintf(st->detail.stopped, sizeof st->detail.stopped,
                     "%.119s", line);
        } else if (strcmp(line, "the last thing it did:") == 0) {
            want_last = 1;
        } else if (strcmp(line, "where it was:") == 0 ||
                   strcmp(line, "symbolized stack (best effort):") == 0) {
            in_frames = 1;
        } else if (in_frames) {
            if (line[0] == '\0') {
                in_frames = 0;
            } else if (st->detail.nframes < 4 &&
                       strncmp(line, "instruction:", 12) != 0 &&
                       strncmp(line, "fault address:", 14) != 0 &&
                       strncmp(line, "signal code:", 12) != 0 &&
                       strstr(line, "tiku_report") == NULL &&
                       strstr(line, "asked_to_say") == NULL &&
                       strstr(line, "took_signal") == NULL) {
                frame_name(line, st->detail.frames[st->detail.nframes],
                           sizeof st->detail.frames[0]);
                st->detail.nframes++;
            }
        }
    }
    (void)fclose(f);
}

/**
 * @brief Open the reports section, or refresh it: list the reports and
 *        choose the newest.
 */
static void
reports_open(about_state_t *st)
{
    st->nreports = tiku_report_list(st->paths, 64);
    tiku_list_init(&st->list, 1);
    (void)tiku_list_set_count(&st->list, st->nreports);
    st->chosen = (st->nreports > 0) ? 0 : -1;
    if (st->chosen >= 0) {
        tiku_list_select_only(&st->list, 0);
    }
    load_detail(st);
    st->reports_open = 1;
}

/** @brief @p text, cut to @p width pixels of @p font with an ellipsis. */
static void
elide(const tiku_font_t *font, const char *text, int width, char *out,
      size_t max)
{
    size_t n = strlen(text);

    snprintf(out, max, "%s", text);
    while (n > 3u && tiku_text_width(font, out) > width) {
        n--;
        snprintf(out, max, "%.*s...", (int)n, text);
    }
}

/** @brief One line of the account, kept inside its box. */
static void
account_line(tiku_surface_t *surface, const tiku_font_t *font,
             tiku_rect_t box, int y, const char *text)
{
    char fit[160];

    elide(font, text, box.w - 16, fit, sizeof fit);
    tiku_text(surface, font, box.x + 8, y + font->ascent, fit, TIKU_C_TEXT);
}

/** @brief Paint the reports section from @p y down. */
static void
paint_reports(about_state_t *st, int y)
{
    tiku_surface_t *surface = st->surface;
    const tiku_font_t *plain = tiku_font_plain();
    int left = 18, i;
    int h = plain->height;
    tiku_rect_t body, account, btn;

    y += 8;
    tiku_text(surface, tiku_font_bold(), left, y + plain->ascent, "Reports",
              TIKU_C_TEXT);
    y += h + 5;

    /* The list: newest first, one row each by kind and moment. */
    body.x = left;
    body.y = y;
    body.w = surface->w - 2 * left;
    body.h = REPORT_ROWS * (h + 3);
    st->list_box = body;
    tiku_ui_sunken(surface, body, TIKU_C_DOC);
    tiku_list_set_row_h(&st->list, h + 3);
    if (st->nreports == 0) {
        tiku_text(surface, plain, body.x + 8, body.y + 4 + plain->ascent,
                  "none", TIKU_C_NOTE);
    }
    for (i = st->list.top; i < st->nreports; i++) {
        tiku_rect_t r = tiku_list_row_rect(&st->list, body, i);
        char label[96];

        if (r.y + r.h > body.y + body.h) {
            break;
        }
        r.x += 2;
        r.w -= 4;
        report_label(st->paths[i], label, sizeof label);
        if (tiku_list_selected(&st->list, i)) {
            tiku_fill(surface, r, TIKU_C_SELECT);
        }
        tiku_text(surface, plain, r.x + 6, r.y + plain->ascent + 1, label,
                  tiku_list_selected(&st->list, i) ? TIKU_C_SELTEXT
                                                   : TIKU_C_TEXT);
    }
    y += body.h + 8;

    /* The account of the chosen one, in a box of its own. */
    account.x = left;
    account.y = y;
    account.w = surface->w - 2 * left;
    account.h = ACCOUNT_LINES * (h + 2) + 8;
    tiku_ui_sunken(surface, account, TIKU_C_DOC);
    if (st->chosen >= 0) {
        int ty = account.y + 4;
        char text[160];
        account_line(surface, plain, account, ty, st->detail.stopped);
        ty += h + 2;
        if (st->detail.last[0] != '\0') {
            snprintf(text, sizeof text, "the last thing it did: %.100s",
                     st->detail.last);
            account_line(surface, plain, account, ty, text);
        }
        ty += h + 2;
        if (st->detail.nframes > 0) {
            account_line(surface, plain, account, ty,
                         "where it was, as this build reads it:");
            ty += h + 2;
            for (i = 0; i < st->detail.nframes && i < ACCOUNT_LINES - 3;
                 i++) {
                snprintf(text, sizeof text, "    %.90s",
                         st->detail.frames[i]);
                account_line(surface, plain, account, ty, text);
                ty += h + 2;
            }
        }
    } else {
        tiku_text(surface, plain, account.x + 8,
                  account.y + 4 + plain->ascent,
                  "nothing to show", TIKU_C_NOTE);
    }
    y += account.h + 8;

    /* The button row: Open the file opens the whole report. */
    btn.w = tiku_ui_button_w("Open the file", 0);
    btn.h = tiku_ui_button_h();
    btn.x = surface->w - left - btn.w;
    btn.y = y + 6;
    tiku_ui_button(surface, btn, "Open the file",
                   ((st->chosen >= 0) ? 0u : TIKU_S_DISABLED) |
                   (tiku_press_held(&st->press, 4) ? TIKU_S_PRESSED : 0u));
    st->open_btn = btn;
}

/**
 * @brief Paint the box: the identity column, the facts, the footer, then
 *        the devices and reports sections when they are shown.
 */
static void
paint(about_state_t *st)
{
    tiku_surface_t *surface = st->surface;
    const tiku_font_t *plain = tiku_font_plain();
    const tiku_font_t *big = tiku_font_at(TIKU_SHELL_ABOUT_FONT_SIZE);
    const tiku_font_t *bold = tiku_font_bold();
    int left = 18;
    int facts_x = 150;
    int y;
    char line[160];

    tiku_fill(surface, (tiku_rect_t){ 0, 0, surface->w, surface->h },
                   TIKU_C_PANEL);
    {   /* The identity column: the mark, drawn from its paths in the
         * cycle's current colours, then the wordmark over its accent
         * rule. */
        int mark = 80;
        int base;

        tiku_logo_palette_t p;
        int set;
        float toward;

        cycle_at(st, tiku_trk_now(), &set, &toward);
        tiku_logo_palette(set, toward, &p);
        tiku_logo_paint_with(surface, (tiku_rect_t){ left, 16, mark, mark },
                             TIKU_LOGO_GROUND, &p, 1.0f);
        base = 16 + mark + 14 + big->ascent;

        /* The product name above the rule, and under it which part of
         * TikuOS this is. */
        tiku_text(surface, big, left, base,
                  TIKU_SHELL_PRODUCT_NAME, TIKU_C_TEXT);
        tiku_fill(surface, (tiku_rect_t){ left, base + 6, 96, 3 },
                       TIKU_C_TAB);
        tiku_text(surface, bold, left,
                       base + 14 + plain->ascent, "desktop",
                       TIKU_C_TEXT);
    }
    y = 22;
    tiku_text(surface, bold, facts_x, y + plain->ascent,
                   TIKU_SHELL_DESKTOP_NAME_SPACE ABOUT_VERSION, TIKU_C_TEXT);
    y += plain->height + 10;
    snprintf(line, sizeof line, "Processors: %d", st->ncpu);
    tiku_text(surface, plain, facts_x, y + plain->ascent, line,
                   TIKU_C_TEXT);
    y += plain->height + 10;
    {   /* Memory, with its gauge: what is in use, over what there is. */
        long total = 0, avail = 0;
        tiku_rect_t gauge = { facts_x, y + plain->height + 4,
                                   about_body_w() - 150 - 36, 10 };
        long used;

        read_memory(&total, &avail);
        used = (total > avail) ? total - avail : 0;
        /* The text gives the free memory; the gauge fills with the used
         * part. */
        snprintf(line, sizeof line, "Memory: %ld MB of %ld MB free",
                 avail / 1024, total / 1024);
        tiku_text(surface, plain, facts_x, y + plain->ascent, line,
                       TIKU_C_TEXT);
                tiku_ui_gauge(surface, gauge,
                      (total > 0) ? (float)used / (float)total : 0.0f);
    }
    y += plain->height + 10 + 8;
    {   /* The reports button: it names the newest report and opens the
         * reports section, and reads Hide reports while the section is
         * open. */
        const char *leaf;
        char kind[16] = "", stamp[16] = "";
        tiku_rect_t btn;

        (void)tiku_report_newest(st->report, sizeof st->report);
        leaf = strrchr(st->report, '/');
        leaf = (leaf != NULL) ? leaf + 1 : st->report;
        if (st->reports_open) {
            snprintf(line, sizeof line, "Hide reports");
        } else if (st->report[0] == '\0') {
            snprintf(line, sizeof line, "Reports: none");
        } else if (sscanf(leaf, "%15[a-z]-%15[0-9-]", kind, stamp) == 2 &&
                   strlen(stamp) >= 15u) {
            /* A report is named <kind>-<yyyymmdd>-<hhmmss>-...; the
             * button shows the kind and the moment. */
            snprintf(line, sizeof line,
                     "Last report: %s, %.4s-%.2s-%.2s %.2s:%.2s...",
                     kind, stamp, stamp + 4, stamp + 6, stamp + 9,
                     stamp + 11);
        } else {
            snprintf(line, sizeof line, "Last report: %.40s...", leaf);
        }
        btn.x = facts_x;
        btn.y = y;
        btn.w = surface->w - 14 - facts_x;
        btn.h = tiku_ui_button_h();
        tiku_ui_button(surface, btn, line,
                       ((st->report[0] == '\0') ? TIKU_S_DISABLED : 0u) |
                       (tiku_press_held(&st->press, 1) ? TIKU_S_PRESSED : 0u));
        st->reports_btn = btn;
    }
    {   /* The footer: the copyright and the project page, then the
         * licence on a second line, since the three do not fit on one
         * line between the margins. */
        int foot = about_body_h() - 14;

        tiku_text(surface, plain, left, foot - plain->height - 4,
                       TIKU_SHELL_ABOUT_COPYRIGHT,
                       TIKU_C_TEXT);
        tiku_text(surface, plain, left, foot,
                       "Apache License 2.0", TIKU_C_TEXT);
        /* The build date and time, right-aligned on the licence line. */
        {
            char built[64];
            int w;

            snprintf(built, sizeof built, "built %s %s", __DATE__,
                     __TIME__);
                        w = tiku_text_width(plain, built);
            tiku_text(surface, plain, surface->w - 14 - w, foot,
                           built, TIKU_C_TEXT);
        }
    }
    if (st->ndev > 0) {
        paint_devices(st, about_body_h());
    }
    if (st->reports_open) {
        paint_reports(st, about_body_h() + devices_h(st));
    }
}

/** @brief Paint and hand the frame over. */
static void
show(about_state_t *st)
{
    paint(st);
    (void)st->services->frame(st->services->ctx, st->id,
                              st->surface->px, st->surface->w,
                              st->surface->h);
}

/**
 * @brief Open the box through the services and paint its first frame.
 *
 * @return 0 with @p state set, -1 when the services lack open or frame or
 *         the window could not be made.
 */
static int
about_start(void **state, const tiku_app_services_t *services)
{
    about_state_t *st = calloc(1, sizeof *st);
    int w, h;

    if (st == NULL || services == NULL || services->open == NULL ||
        services->frame == NULL) {
        free(st);
        return -1;
    }
    st->services = services;
    st->ncpu = count_cpus();
    st->opened_us = tiku_trk_now();
    st->step = -1;
    st->chosen = -1;
    w = about_content_w();
    h = about_body_h() + devices_h(st);
    st->surface = tiku_surface_new(w, h, TIKU_C_PANEL);
    if (st->surface == NULL) {
        free(st);
        return -1;
    }
    st->id = services->open(services->ctx, "About this system", w, h);
    if (st->id == 0) {
        tiku_surface_free(st->surface);
        free(st);
        return -1;
    }
    /* The host places a PANEL window and gives it a tab and a close box;
     * a host without place() places it as any other window. */
    if (services->place != NULL) {
        (void)services->place(services->ctx, st->id,
                              TIKU_APP_PLACE_PANEL);
    }
    show(st);
    *state = st;
    return 0;
}

/** @brief Close the window and free what the box held. */
static void
about_stop(void *state)
{
    about_state_t *st = state;

    if (st == NULL) {
        return;
    }
    if (st->services->close != NULL) {
        st->services->close(st->services->ctx, st->id);
    }
    tiku_surface_free(st->surface);
    free(st);
}

/** @brief Whether (@p x, @p y) lies in @p r. */
static int
inside_rect(tiku_rect_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/**
 * @brief Which of the About box's buttons is under (@p x, @p y).
 *
 * Only a button that can act counts; a greyed one is not found.
 * @return 1 the reports button, 2 and 3 the board stepper's arrows,
 *         4 Open the file, 0 none.
 */
static int
about_button_at(const about_state_t *st, int x, int y)
{
    if (st->report[0] != '\0' && inside_rect(st->reports_btn, x, y)) {
        return 1;
    }
    if (st->ndev > 1) {
        if (st->page > 0 && inside_rect(st->prev_btn, x, y)) { return 2; }
        if (st->page + 1 < st->ndev &&
            inside_rect(st->next_btn, x, y)) {
            return 3;
        }
    }
    if (st->reports_open && st->chosen >= 0 &&
        st->services->open_path != NULL &&
        inside_rect(st->open_btn, x, y)) {
        return 4;
    }
    return 0;
}

/**
 * @brief Escape asks to close; presses drive the buttons and pick a
 *        report, and the wheel scrolls the report list.
 * @return 1 to close the box, 0 otherwise.
 */
static int
about_event(void *state, const tiku_event_t *event)
{
    about_state_t *st = state;

    if (event != NULL && event->type == TIKU_EVENT_KEY_DOWN &&
        event->key == TIKU_KEY_ESCAPE) {
        return 1;
    }
        if (event == NULL || st == NULL) {
        return 0;
    }
    /* A button shows pressed while a press holds it and acts on a
     * release over it; a press released elsewhere does nothing.  The
     * same hit test serves the press, the move and the release. */
    if (event->type == TIKU_EVENT_POINTER_DOWN) {
        int id = about_button_at(st, event->x, event->y);

        if (id != 0 && tiku_press_arm(&st->press, id)) {
            st->step = -1;
            return 0;
        }
    } else if (event->type == TIKU_EVENT_POINTER_MOVE) {
        if (tiku_press_track(&st->press,
                             about_button_at(st, event->x, event->y))) {
            st->step = -1;
        }
        return 0;
    } else if (event->type == TIKU_EVENT_POINTER_UP) {
        int id = tiku_press_release(&st->press,
                                    about_button_at(st, event->x, event->y));

        st->step = -1;
        if (id == 1) {
            /* The reports button opens the reports section, or closes
             * it. */
            if (st->reports_open) {
                st->reports_open = 0;
            } else {
                reports_open(st);
            }
        } else if (id == 2 || id == 3) {
            st->page += (id == 3) ? 1 : -1;
        } else if (id == 4) {
            (void)st->services->open_path(st->services->ctx,
                                          st->paths[st->chosen]);
        }
        return 0;
    }
    if (st->reports_open && event->type == TIKU_EVENT_POINTER_DOWN) {
        int row = tiku_list_at(&st->list, st->list_box, event->x, event->y);

        if (row >= 0 && row < st->nreports) {
            tiku_list_select_only(&st->list, row);
            st->chosen = row;
            load_detail(st);
            st->step = -1;
            return 0;
        }
    }
    if (st->reports_open && event->type == TIKU_EVENT_WHEEL &&
        st->nreports > REPORT_ROWS) {
        int top = st->list.top + ((event->dy > 0) ? 1 : -1);

        if (top < 0) {
            top = 0;
        }
        if (top > st->nreports - REPORT_ROWS) {
            top = st->nreports - REPORT_ROWS;
        }
        st->list.top = top;
        st->step = -1;
    }
    return 0;
}

/**
 * @brief Refresh the boards, refit the surface to about_h(), and repaint
 *        when the size, the cycle step or the second changes.
 */
static void
about_tick(void *state, int64_t now_us)
{
    about_state_t *st = state;
    int step, want_w, want_h;
    int64_t sec;
    int owe = 0;

    if (st == NULL) {
        return;
    }
    /* The face and the boards change outside this window, so the size is
     * checked on every tick, after the boards are re-read.  The host keeps
     * the window's corner in place on a resize. */
    about_devices_refresh(st);
    want_w = about_content_w();
    want_h = about_h(st);
    if (want_w != st->surface->w || want_h != st->surface->h) {
        tiku_surface_t *grown = tiku_surface_new(want_w, want_h,
                                                 TIKU_C_PANEL);

        if (grown != NULL) {
            tiku_surface_free(st->surface);
            st->surface = grown;
            if (st->services->resize != NULL) {
                (void)st->services->resize(st->services->ctx, st->id,
                                           want_w, want_h);
            }
            owe = 1;
        }
    }
    step = cycle_step(st, now_us);
    if (step != st->step) {
        st->step = step;
        owe = 1;
    }
    /* Repainted once a second, so the memory gauge follows the
     * machine. */
    sec = now_us / 1000000;
    if (sec != st->gauge_s) {
        st->gauge_s = sec;
        owe = 1;
    }
    if (owe) {
        show(st);
    }
}

const tiku_app_descriptor_t tiku_trk_about_app = {
    .id = "org.tikuos.about",
    .name = "About",
    .start = about_start,
    .stop = about_stop,
    .event = about_event,
    .tick = about_tick,
};
