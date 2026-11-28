/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_about_window.c - the About box.
 *
 * The identity column on the left holds the logo mark in its colour cycle
 * and the wordmark.  Beside it are the release, the processor as a chip
 * with its name and its clock, free memory over a gauge and a button naming
 * the newest report; the footer
 * under both gives the licence and the build stamp.  When the platform
 * maps this computer's memories, they follow as bars; then the connected
 * boards, one at a time, and the reports section opens under them.  The
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
/* The side of the processor's chip, beside its name and clock. */
#define CHIP 40

/* The release the box shows. */
#define ABOUT_VERSION TIKU_SHELL_ABOUT_VERSION

/* The boards the devices section keeps; dev[ABOUT_HOST], after them, holds
 * this computer's own memory maps. */
#define ABOUT_DEVS  6
#define ABOUT_HOST  ABOUT_DEVS
/* The memories an entry maps: SRAM, the NVM and PSRAM, and the areas one
 * map holds. */
#define ABOUT_MAPS  3
#define ABOUT_AREAS 12
/* The room for a memory's name, bold, left of its bar. */
#define LABEL_W     70
/* This computer's memory section: the legend's columns, each this wide. */
#define HOST_COLS   3
#define HOST_CELL   215

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
    /* What paint() draws on: the box's own surface when the host takes
     * frames; the host's view of the window while it draws a window from
     * open_drawn(), and NULL between. */
    tiku_surface_t            *surface;
    int                        width, height;  /* the content's size */
    int                        drawn;          /* the host draws the window */
    uint32_t                        id;
    /* Counted once at start; the memory is read again on every paint. */
    int                        ncpu;
    char                       system_release[64];
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
         * order, drawn as one bar with a legend; lay[0] is the SRAM,
         * lay[1] the NVM and lay[2] the PSRAM. */
        struct {
            char          name[16];     /* SRAM, PSRAM or the NVM's kind */
            unsigned long total;        /* the bank, or the part */
            struct {
                char          label[12];
                unsigned long size, used;   /* in use; stack: headroom */
                int           live;         /* used is meaningful */
            }             est[ABOUT_AREAS];
            int           nest;
            unsigned long bank2;        /* a second bank, or 0 */
        }             lay[ABOUT_MAPS];
        int           nlay;
        char          has[112];     /* what else is on it          */
        unsigned long part_sram;    /* the SRAM the part is fitted with */
        unsigned long part_nvm;
        char          kind[16];     /* what its NVM is called      */
        char          up[24];       /* seconds it has been running */
        char          boots[16];
        int           known;        /* the shell has an answer for it */
    }                          dev[ABOUT_DEVS + 1];
    int                        ndev;
    /* The board on show, and where its stepper's arrows were painted,
     * for the hit test. */
    int                        page;
    tiku_rect_t                prev_btn, next_btn;
    /* The button a press is holding: 1 reports, 2 prev, 3 next, 4 open. */
    tiku_press_t               press;
} about_state_t;

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

/** @brief The box's width: the body's, or wider for this computer's
 *         memory legend's columns when the platform maps them. */
static int about_box_w(const about_state_t *st)
{
    int host = 18 + HOST_COLS * HOST_CELL + 14;

    return (st->dev[ABOUT_HOST].nlay > 0 && host > about_body_w()) ? host
                                                                  : about_body_w();
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
    int nf = 1, i, k, which = (strcmp(mem, "nvm") == 0) ? 1 :
                              (strcmp(mem, "psram") == 0) ? 2 : 0;

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
        if (which != 1) {
            snprintf(st->dev[at].lay[which].name,
                     sizeof st->dev[at].lay[which].name, "%s",
                     which == 2 ? "PSRAM" : "SRAM");
        }
        return;
    }
    if (strcmp(f[0], "bank2") == 0) {
        st->dev[at].lay[0].bank2 = v[0];
        return;
    }
    k = st->dev[at].lay[which].nest;
    if (k >= ABOUT_AREAS) {
        return;
    }
    snprintf(st->dev[at].lay[which].est[k].label,
             sizeof st->dev[at].lay[which].est[k].label, "%s", f[0]);
    /* stack and jobs: pool then headroom; tier: size, used, bank; code:
     * window then in use; store: capacity, then in use when the board says
     * it; heap: size, then in use; free: a size none of which is in use;
     * the rest are a size alone. */
    st->dev[at].lay[which].est[k].size = v[0];
    if (strcmp(f[0], "stack") == 0 || strcmp(f[0], "jobs") == 0) {
        /* For the stack, used holds the lowest headroom the board has
         * seen, which the legend prints as headroom; live stays 0. */
        st->dev[at].lay[which].est[k].used = v[1];
        st->dev[at].lay[which].est[k].live = 0;
    } else if (strcmp(f[0], "free") == 0) {
        st->dev[at].lay[which].est[k].used = 0;
        st->dev[at].lay[which].est[k].live = 1;
    } else if (strcmp(f[0], "tier") == 0 || strcmp(f[0], "code") == 0 ||
               strcmp(f[0], "heap") == 0 ||
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
            } else if (strcmp(row, "sram") == 0 || strcmp(row, "nvm") == 0 ||
                       strcmp(row, "psram") == 0) {
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
            rows += 1 + legend_lines((about_state_t *)st, NULL, 58 + LABEL_W, 0,
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
    { "jobs",    "Job stack", 0xD89A5Cu },  /* a lighter amber        */
    { "guard",   "Guard",   0xB03A48u },
    { "unassigned", "Unassigned", 0x7A7A7Au },
    { "tier",    "Tier",    0x2E8B57u },   /* green                  */
    { "tier2",   "Tier",    0x2E8B57u },
    { "store",   "Store",   0x8A8A2Eu },   /* olive                  */
    { "persist", "Persist", 0xB03A48u },   /* carmine                */
    { "heap",    "Heap",    0x2E8B8Bu },   /* teal                   */
    { "frames",  "Frames",  0x5A86C0u },   /* light blue             */
    { "drivers", "Drivers", 0x8C6D46u },   /* brown                  */
    { "shadow",  "Store shadow", 0x9C8F3Au },
    { "settings", "Settings", 0xA05C8Cu }, /* plum                   */
    { "free",    "Free",    0x9A9A9Au },   /* grey, drawn lightened  */
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
    int room = st->width - x - 14, k, lines = 1;
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
        if (strcmp(st->dev[i].lay[m].est[k].label, "stack") == 0 ||
            strcmp(st->dev[i].lay[m].est[k].label, "jobs") == 0) {
            snprintf(usage, sizeof usage, "%s headroom", used);
        } else if (strcmp(st->dev[i].lay[m].est[k].label, "free") == 0) {
            snprintf(usage, sizeof usage, "Free");
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
    int lines = legend_lines(st, st->surface, x + LABEL_W, y, i, m);

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
        tiku_text(st->surface, plain, x + LABEL_W,
                  y + lines * (plain->height + 2) + plain->ascent, said,
                  TIKU_C_TEXT);
        lines++;
    }
    return lines;
}

/**
 * @brief One memory's name and its bar: its estates side by side in the
 *        order they lie, each as wide as its share, the live ones showing
 *        their use inside, and the total at the right.
 */
static void
paint_bar(about_state_t *st, int x, int y, int i, int m)
{
    const tiku_font_t *plain = tiku_font_plain();
    int h = plain->height, k, px;
    unsigned long total = st->dev[i].lay[m].total, laid = 0UL;
    char a[24];
    /* The bar takes the box's width, less room for the total at its
     * right. */
    tiku_rect_t bar = { x + LABEL_W, y + 1, st->surface->w - (x + LABEL_W) - 78,
                        h - 2 };

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
}

/**
 * @brief One memory as one bar, with the legend's table underneath.
 * @return how many lines it took, or 0 when the memory has no total.
 */
static int
paint_layout(about_state_t *st, int x, int y, int i, int m)
{
    if (st->dev[i].lay[m].total == 0UL) {
        return 0;
    }
    paint_bar(st, x, y, i, m);
    return 1 + paint_legend(st, x, y + tiku_font_plain()->height + 2, i, m);
}

/* The order this computer's memories are drawn in: SRAM, PSRAM, NVM. */
static const int host_order[ABOUT_MAPS] = { 0, 2, 1 };

/**
 * @brief Read this computer's memory maps into dev[ABOUT_HOST], none when
 *        the platform has no maps.
 */
static void
about_host_refresh(about_state_t *st)
{
    char text[2048];

    memset(&st->dev[ABOUT_HOST], 0, sizeof st->dev[ABOUT_HOST]);
    if (tiku_sys_memory_map(text, sizeof text) > 0) {
        text[sizeof text - 1u] = '\0';
        about_dev_facts(st, ABOUT_HOST, text);
    }
}

/** @brief How many legend rows memory @p m of this computer takes: its
 *         areas with a size, HOST_COLS to a row. */
static int
host_rows(const about_state_t *st, int m)
{
    int k, n = 0;

    for (k = 0; k < st->dev[ABOUT_HOST].lay[m].nest; k++) {
        n += st->dev[ABOUT_HOST].lay[m].est[k].size > 0UL;
    }
    return (n + HOST_COLS - 1) / HOST_COLS;
}

/** @brief The height of this computer's memory section, 0 without maps:
 *         per memory a bar, its legend rows and a 3-pixel gap. */
static int
host_h(const about_state_t *st)
{
    int step = tiku_font_plain()->height + 2, h = 0, m;

    for (m = 0; m < ABOUT_MAPS; m++) {
        if (st->dev[ABOUT_HOST].lay[m].total > 0UL) {
            h += (1 + host_rows(st, m)) * step + 3;
        }
    }
    return h > 0 ? h + 4 : 0;
}

/**
 * @brief An area's figure in the compact legend: what is in use over its
 *        size when it reports use (a stack's is its size less its
 *        headroom), the unit written once when both share it, otherwise
 *        its size.
 */
static void
host_value(const about_state_t *st, int m, int k, char *out, size_t max)
{
    unsigned long size = st->dev[ABOUT_HOST].lay[m].est[k].size;
    unsigned long used = st->dev[ABOUT_HOST].lay[m].est[k].used;
    const char *label = st->dev[ABOUT_HOST].lay[m].est[k].label;
    char a[24], b[24];

    fmt_bytes(size, b, sizeof b);
    if (strcmp(label, "stack") == 0 || strcmp(label, "jobs") == 0) {
        used = size > used ? size - used : 0UL;
    } else if (!st->dev[ABOUT_HOST].lay[m].est[k].live ||
               strcmp(label, "free") == 0) {
        snprintf(out, max, "%s", b);
        return;
    }
    fmt_bytes(used, a, sizeof a);
    {
        const char *ua = strrchr(a, ' '), *ub = strrchr(b, ' ');

        if (used == 0UL) {
            snprintf(out, max, "0/%s", b);
        } else if (ua != NULL && ub != NULL && strcmp(ua, ub) == 0) {
            snprintf(out, max, "%.*s/%s", (int)(ua - a), a, b);
        } else {
            snprintf(out, max, "%s/%s", a, b);
        }
    }
}

/**
 * @brief This computer's memories from @p y down: each as a bar, then its
 *        areas HOST_COLS to a row, a swatch, the name and the figure.
 */
static void
paint_host(about_state_t *st, int y)
{
    const tiku_font_t *plain = tiku_font_plain();
    int step = plain->height + 2, x = 18, o, k, col;

    for (o = 0; o < ABOUT_MAPS; o++) {
        int m = host_order[o];

        if (st->dev[ABOUT_HOST].lay[m].total == 0UL) {
            continue;
        }
        paint_bar(st, x, y, ABOUT_HOST, m);
        y += step;
        for (k = 0, col = 0; k < st->dev[ABOUT_HOST].lay[m].nest; k++) {
            int cx = x + col * HOST_CELL;
            char value[64];
            tiku_rgb_t hue;
            const char *name;

            if (st->dev[ABOUT_HOST].lay[m].est[k].size == 0UL) {
                continue;
            }
            name = estate_look(st->dev[ABOUT_HOST].lay[m].est[k].label, &hue);
            host_value(st, m, k, value, sizeof value);
            tiku_fill(st->surface, (tiku_rect_t){ cx, y + (plain->height - 6) / 2,
                                                  6, 6 }, hue);
            tiku_text(st->surface, plain, cx + 10, y + plain->ascent, name,
                      TIKU_C_TEXT);
            tiku_text(st->surface, plain,
                      cx + HOST_CELL - 12 - tiku_text_width(plain, value),
                      y + plain->ascent, value, TIKU_C_TEXT);
            if (++col == HOST_COLS) {
                col = 0;
                y += step;
            }
        }
        y += (col > 0 ? step : 0) + 3;
    }
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
    bar.x = x + LABEL_W;
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
/** @brief The processor row's height: the chip, or its two lines of text
 *  when the face makes them taller. */
static int processor_row_h(void)
{
    int text = 2 * tiku_font_plain()->height + 2;

    return text > CHIP ? text : CHIP;
}

static int about_body_h(const about_state_t *st)
{
    /* The facts column: 171 px at a 15 px line, growing by three line
     * heights with the face, with the processor row in place of one. */
    int facts = 171 + 3 * (tiku_font_plain()->height - 15) -
                tiku_font_plain()->height + processor_row_h();
    /* The identity column: 16 above, the 80 px mark, the wordmark, its
     * rule, and the line under the rule. */
    int identity = 16 + 80 + 14 + tiku_font_at(28)->height + 6 + 3 + 14 +
                   tiku_font_plain()->height + 20;

    if (st->system_release[0]) facts += tiku_font_plain()->height + 4;

    /* The two-line footer runs under both columns: its first line sits in
     * the taller column's bottom margin, and the second is added here. */
        return ((identity > facts) ? identity : facts)
           + tiku_font_plain()->height + 4;
}

/**
 * @brief The whole box: the facts, this computer's memories, the
 *        connected boards, and the reports when they are open.
 */
static int
about_h(const about_state_t *st)
{
    return about_body_h(st) + host_h(st) + devices_h(st) +
           (st->reports_open ? reports_h() : 0);
}

/**
 * @brief The content width: the box less two window borders, so the
 *        window's outer width is about_box_w().
 *
 * tests/integration/shell/about.script checks pixels at this geometry.
 */
static int about_content_w(const about_state_t *st)
{
    return about_box_w(st) - 2 * TIKU_WINDOW_BORDER;
}

/** @brief How many processors are online, at least 1. */
static int
count_cpus(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);

    return (n > 0) ? (int)n : 1;
}

/** @brief A clock rate as the box writes it: "320 MHz", or "2.80 GHz"
 *  from 1 GHz; empty for 0. */
static void
rate_text(unsigned long hz, char *out, size_t max)
{
    if (hz == 0UL) {
        out[0] = '\0';
    } else if (hz >= 1000000000UL) {
        unsigned long hundredths = (hz + 5000000UL) / 10000000UL;

        snprintf(out, max, "%lu.%02lu GHz", hundredths / 100UL, hundredths % 100UL);
    } else {
        snprintf(out, max, "%lu MHz", (hz + 500000UL) / 1000000UL);
    }
}

/** @brief The processor's line: how many there are and its name, or the
 *  count alone where the host names none. */
static void
processor_text(int ncpu, const char *name, char *out, size_t max)
{
    if (name[0] == '\0') {
        snprintf(out, max, "%d processor%s", ncpu, ncpu == 1 ? "" : "s");
    } else if (ncpu > 1) {
        snprintf(out, max, "%d x %s", ncpu, name);
    } else {
        snprintf(out, max, "%s", name);
    }
}

/** @brief Cut @p text, ending it in "...", until it is at most @p w
 *  pixels wide in @p font. */
static void
fit_text(const tiku_font_t *font, char *text, int w)
{
    size_t n = strlen(text);

    while (n > 3u && tiku_text_width(font, text) > w) {
        n--;
        memcpy(text + n - 3u, "...", 4u);
    }
}

/**
 * @brief A processor chip, @p size pixels square at (@p x, @p y): a
 *        graphite package with five pins on each side, a gold die and the
 *        pin-one dot.  The same colours under every theme, as an icon.
 */
static void
paint_chip(tiku_surface_t *s, int x, int y, int size)
{
    int inset = size / 6, body = size - 2 * inset;
    int pitch = body / 6, pw = size >= 32 ? size / 16 : 2, plen = inset - 1;
    int die = body / 2, dx = x + inset + (body - die) / 2;
    int dy = y + inset + (body - die) / 2;
    tiku_rgb_t pin = TIKU_RGB(0xb8, 0xbe, 0xc8);

    for (int i = 1; i <= 5; i++) {
        int at = inset + i * pitch - pw / 2;

        tiku_fill(s, (tiku_rect_t){ x + at, y + 1, pw, plen }, pin);
        tiku_fill(s, (tiku_rect_t){ x + at, y + size - 1 - plen, pw, plen }, pin);
        tiku_fill(s, (tiku_rect_t){ x + 1, y + at, plen, pw }, pin);
        tiku_fill(s, (tiku_rect_t){ x + size - 1 - plen, y + at, plen, pw }, pin);
    }
    tiku_fill_round(s, (tiku_rect_t){ x + inset, y + inset, body, body }, 3,
                    TIKU_CORNER_ALL, TIKU_RGB(0x2a, 0x2e, 0x35));
    tiku_frame_round(s, (tiku_rect_t){ x + inset, y + inset, body, body }, 3,
                     TIKU_CORNER_ALL, TIKU_RGB(0x55, 0x5c, 0x68));
    tiku_fill_round(s, (tiku_rect_t){ dx, dy, die, die }, 2, TIKU_CORNER_ALL,
                    TIKU_RGB(0xc9, 0xa2, 0x3f));
    tiku_frame_round(s, (tiku_rect_t){ dx, dy, die, die }, 2, TIKU_CORNER_ALL,
                     TIKU_RGB(0x8a, 0x6a, 0x1e));
    tiku_hline(s, dx + 2, dy + 2, die - 4, TIKU_RGB(0xec, 0xd5, 0x86));
    tiku_fill_round(s, (tiku_rect_t){ x + inset + 3, y + inset + 3, 3, 3 }, 1,
                    TIKU_CORNER_ALL, TIKU_RGB(0x78, 0x80, 0x8c));
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
    if (st->system_release[0]) {
        y += plain->height + 4;
        snprintf(line, sizeof line, "%s", st->system_release);
        fit_text(plain, line, surface->w - facts_x - 14);
        tiku_text(surface, plain, facts_x, y + plain->ascent, line, TIKU_C_TEXT);
    }
    y += plain->height + 10;
    {   /* The processor: its chip, then its name over its clock, read on
         * every paint so a rate changed while the box is open shows. */
        char name[96] = "", rate[24];
        unsigned long hz = 0;
        int row = processor_row_h(), tx = facts_x + CHIP + 10;
        int top = y + (row - (2 * plain->height + 2)) / 2;

        if (tiku_sys_processor(name, sizeof name, &hz) != 0) {
            name[0] = '\0';
        }
        paint_chip(surface, facts_x, y + (row - CHIP) / 2, CHIP);
        processor_text(st->ncpu, name, line, sizeof line);
        fit_text(plain, line, surface->w - 14 - tx);
        rate_text(hz, rate, sizeof rate);
        tiku_text(surface, rate[0] != '\0' ? plain : bold, tx,
                  rate[0] != '\0' ? top + plain->ascent
                                  : y + (row - plain->height) / 2 + plain->ascent,
                  line, TIKU_C_TEXT);
        if (rate[0] != '\0') {
            tiku_text(surface, bold, tx, top + plain->height + 2 + plain->ascent, rate,
                      TIKU_C_TEXT);
        }
        y += row + 10;
    }
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
        int foot = about_body_h(st) - 14;

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
    if (host_h(st) > 0) {
        paint_host(st, about_body_h(st));
    }
    if (st->ndev > 0) {
        paint_devices(st, about_body_h(st) + host_h(st));
    }
    if (st->reports_open) {
        paint_reports(st, about_body_h(st) + host_h(st) + devices_h(st));
    }
}

/** @brief Have the window shown afresh: drawn again by the host, or
 *         painted and handed over as a frame. */
static void
show(about_state_t *st)
{
    if (st->drawn) {
        st->services->redraw(st->services->ctx, st->id);
        return;
    }
    paint(st);
    (void)st->services->frame(st->services->ctx, st->id,
                              st->surface->px, st->surface->w,
                              st->surface->h);
}

/** @brief The host drawing the window: paint on its view @p s. */
static void
about_draw(void *app, uint32_t id, tiku_surface_t *s)
{
    about_state_t *st = app;

    (void)id;
    st->surface = s;
    paint(st);
    st->surface = NULL;
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
#ifdef TIKU_SHELL_ABOUT_SYSTEM_RELEASE
    TIKU_SHELL_ABOUT_SYSTEM_RELEASE(st->system_release, sizeof st->system_release);
#endif
    st->opened_us = tiku_trk_now();
    st->step = -1;
    st->chosen = -1;
    about_host_refresh(st);
    w = st->width = about_content_w(st);
    h = st->height = about_body_h(st) + host_h(st) + devices_h(st);
    /* A host that draws the window keeps no pixels for it; otherwise the
     * box paints its own surface and sends frames. */
    st->drawn = services->open_drawn != NULL && services->redraw != NULL;
    if (st->drawn) {
        st->id = services->open_drawn(services->ctx, "About this system", w, h,
                                      about_draw, st);
    } else {
        st->surface = tiku_surface_new(w, h, TIKU_C_PANEL);
        st->id = st->surface != NULL
                     ? services->open(services->ctx, "About this system", w, h)
                     : 0;
    }
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
    /* Repainted once a second, so the memory gauge and this computer's
     * maps follow the machine; the maps are read again then. */
    sec = now_us / 1000000;
    if (sec != st->gauge_s) {
        st->gauge_s = sec;
        about_host_refresh(st);
        owe = 1;
    }
    /* The face and the boards change outside this window, so the size is
     * checked on every tick, after the boards are re-read.  The host keeps
     * the window's corner in place on a resize. */
    about_devices_refresh(st);
    want_w = about_content_w(st);
    want_h = about_h(st);
    if (want_w != st->width || want_h != st->height) {
        tiku_surface_t *grown = st->drawn ? NULL
                                          : tiku_surface_new(want_w, want_h,
                                                             TIKU_C_PANEL);

        if (st->drawn || grown != NULL) {
            tiku_surface_free(st->surface);
            st->surface = grown;
            st->width = want_w;
            st->height = want_h;
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
