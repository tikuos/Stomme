/* Corrected memory-map fixtures use the existing About parser unchanged;
 * this computer's own maps; the processor row's clock and name text and
 * its chip. */
#include <assert.h>
#include "../../system/shared/ui/windows/tiku_trk_about_window.c"

static void row(about_state_t *st, const char *kind, const char *text)
{
    char line[128];
    snprintf(line, sizeof line, "%s", text);
    about_dev_estate(st, 0, kind, line);
}

int main(void)
{
    about_state_t st = {0};
    row(&st, "nvm", "kind\tFRAM");
    row(&st, "nvm", "total\t262144");
    row(&st, "nvm", "tier\t1024\t64");
    row(&st, "nvm", "store\t9316");
    assert(!strcmp(st.dev[0].lay[1].name, "FRAM"));
    assert(st.dev[0].lay[1].est[0].size == 1024);
    assert(st.dev[0].lay[1].est[0].used == 64);
    assert(st.dev[0].lay[1].est[1].size == 9316);
    assert(!st.dev[0].lay[1].est[1].live);
    row(&st, "nvm", "store\t9316\t4096");
    assert(st.dev[0].lay[1].est[2].live);
    assert(st.dev[0].lay[1].est[2].used == 4096);
    row(&st, "sram", "bank\t524288");
    row(&st, "sram", "static\t65536");
    row(&st, "sram", "stack\t36864\t8192");
    row(&st, "sram", "tier\t421888\t4096\t1");
    assert(st.dev[0].lay[0].est[0].size + st.dev[0].lay[0].est[1].size +
           st.dev[0].lay[0].est[2].size == st.dev[0].lay[0].total);
    assert(!strcmp(st.dev[0].lay[0].est[2].label, "tier"));
    row(&st, "sram", "tier\t65536\t128\t2");
    assert(!strcmp(st.dev[0].lay[0].est[3].label, "tier2"));
    memset(&st, 0, sizeof st);
    row(&st, "sram", "bank\t245760");
    row(&st, "sram", "static\t109616");
    row(&st, "sram", "tier\t261120\t64\t2");
    row(&st, "sram", "tier\t99264\t32\t1");
    row(&st, "sram", "guard\t4096");
    row(&st, "sram", "unassigned\t16");
    row(&st, "sram", "stack\t32768\t8192");
    row(&st, "sram", "bank2\t261120");
    assert(st.dev[0].lay[0].nest == 6);
    unsigned long primary = 0;
    for (int i = 0; i < st.dev[0].lay[0].nest; i++) {
        if (strcmp(st.dev[0].lay[0].est[i].label, "tier2"))
            primary += st.dev[0].lay[0].est[i].size;
    }
    assert(primary == st.dev[0].lay[0].total);
    assert(st.dev[0].lay[0].bank2 == 261120);
    assert(st.dev[0].lay[0].est[1].used == 64);
    assert(st.dev[0].lay[0].est[2].used == 32);
    tiku_rgb_t hue;
    assert(!strcmp(estate_look("guard", &hue), "Guard"));
    assert(!strcmp(estate_look("unassigned", &hue), "Unassigned"));
    /* Apollo: multi-MiB shared tier is separate from the TCM bank bar. */
    memset(&st, 0, sizeof st);
    row(&st, "sram", "bank\t524288");
    row(&st, "sram", "static\t280480");
    row(&st, "sram", "tier\t3112960\t4096\t2");
    row(&st, "sram", "tier\t206944\t32\t1");
    row(&st, "sram", "guard\t4096");
    row(&st, "sram", "unassigned\t0");
    row(&st, "sram", "stack\t32768\t8192");
    primary = 0;
    for (int i = 0; i < st.dev[0].lay[0].nest; i++) {
        if (strcmp(st.dev[0].lay[0].est[i].label, "tier2"))
            primary += st.dev[0].lay[0].est[i].size;
    }
    assert(primary == 524288);
    assert(st.dev[0].lay[0].est[1].size == 3112960);
    assert(st.dev[0].lay[0].est[1].used == 4096);
    puts("about_memory: map fixtures PASS");
    memset(&st, 0, sizeof st);
    row(&st, "sram", "bank\t100");
    row(&st, "sram", "tier\t100\t50");
    st.surface = tiku_surface_new(500, 120, 0);
    paint_layout(&st, 0, 0, 0, 0);
    estate_look("tier", &hue);
    int bad_edge = st.surface->px[1 * 500 + 100] !=
                   tiku_tint(TIKU_C_PANEL, 1.18f);
    int bad_unused = st.surface->px[5 * 500 + 350] != tiku_tint(hue, 0.45f);
    tiku_surface_free(st.surface);
    if (bad_edge) puts("FAIL D-042: bar edge must be sunken");
    if (bad_unused) puts("FAIL D-043: unused capacity must be lighter");
    if (bad_edge || bad_unused) return 1;

    /* This computer's own maps: SRAM, PSRAM and the NVM, drawn in that
     * order as bars with a compact legend; none on this host. */
    memset(&st, 0, sizeof st);
    about_host_refresh(&st);
    assert(host_h(&st) == 0 && about_box_w(&st) == about_body_w());
    {
        static const char host_map[] =
            "sram\tbank\t393216\nsram\tstatic\t196224\nsram\ttier\t180608\t0\t1\n"
            "sram\tguard\t0\nsram\tunassigned\t0\nsram\tstack\t16384\t11812\n"
            "psram\ttotal\t16777216\npsram\tshadow\t1802240\npsram\tfree\t121216\n"
            "psram\tdrivers\t829056\npsram\tframes\t1536000\n"
            "psram\tstack\t2883584\t167164\npsram\tjobs\t393216\t376028\n"
            "psram\tsettings\t49152\n"
            "psram\theap\t8507392\t3600000\npsram\tstatic\t655360\n"
            "nvm\tkind\tFlash\nnvm\ttotal\t16777216\nnvm\ttier\t32768\t0\n"
            "nvm\tstore\t1736704\t49152\n";
        int step = tiku_font_plain()->height + 2, heap = 7;
        char v[64];

        about_dev_facts(&st, ABOUT_HOST, host_map);
        assert(!strcmp(st.dev[ABOUT_HOST].lay[2].name, "PSRAM") &&
               st.dev[ABOUT_HOST].lay[2].total == 16777216UL &&
               st.dev[ABOUT_HOST].lay[2].nest == 9);
        assert(!strcmp(st.dev[ABOUT_HOST].lay[2].est[heap].label, "heap") &&
               st.dev[ABOUT_HOST].lay[2].est[heap].live &&
               st.dev[ABOUT_HOST].lay[2].est[heap].used == 3600000UL);
        assert(st.dev[ABOUT_HOST].lay[2].est[1].live && st.dev[ABOUT_HOST].lay[2].est[1].used == 0);
        assert(!strcmp(st.dev[ABOUT_HOST].lay[1].name, "Flash") &&
               !strcmp(st.dev[ABOUT_HOST].lay[0].name, "SRAM"));
        /* Areas without a size take no cell: SRAM's three fill one row. */
        assert(host_rows(&st, 0) == 1 && host_rows(&st, 2) == 3 && host_rows(&st, 1) == 1);
        assert(host_h(&st) == (3 + 5) * step + 3 * 3 + 4);
        assert(about_box_w(&st) == 18 + HOST_COLS * HOST_CELL + 14);
        host_value(&st, 2, heap, v, sizeof v);
        assert(!strcmp(v, "3.4/8.1 MB"));
        host_value(&st, 2, 4, v, sizeof v);       /* the stack: size less headroom */
        assert(!strcmp(v, "2.6/2.8 MB"));
        host_value(&st, 2, 5, v, sizeof v);
        assert(!strcmp(v, "16/384 KB"));
        host_value(&st, 2, 1, v, sizeof v);
        assert(!strcmp(v, "118 KB"));
        host_value(&st, 2, 3, v, sizeof v);
        assert(!strcmp(v, "1.5 MB"));
        host_value(&st, 1, 1, v, sizeof v);
        assert(!strcmp(v, "48 KB/1.7 MB"));
        host_value(&st, 0, 1, v, sizeof v);       /* none of the SRAM tier in use */
        assert(!strcmp(v, "0/176 KB"));

        /* The PSRAM bar is the second: in its heap, the part in use in the
         * hue and the rest lightened; the heap's swatch is in its legend's
         * third row. */
        st.surface = tiku_surface_new(about_box_w(&st), host_h(&st), TIKU_C_PANEL);
        paint_host(&st, 0);
        {
            int y = (1 + host_rows(&st, 0)) * step + 3;
            int bx = 18 + LABEL_W, bw = st.surface->w - bx - 78, px = bx, k, w, uw;
            unsigned long total = 16777216UL;
            tiku_rgb_t heap_hue;

            for (k = 0; k < heap; k++) {
                unsigned long sz = st.dev[ABOUT_HOST].lay[2].est[k].size;
                w = (int)((unsigned long long)sz * (unsigned long long)bw / total);
                px += (w < 1 && sz > 0UL) ? 1 : w;
            }
            w = (int)(8507392ULL * (unsigned long long)bw / total);
            uw = (int)(3600000ULL * (unsigned long long)w / 8507392ULL);
            (void)estate_look("heap", &heap_hue);
            int row = y + 1 + (tiku_font_plain()->height - 2) / 2;
            assert(st.surface->px[row * st.surface->w + px + uw / 2] == heap_hue);
            assert(st.surface->px[row * st.surface->w + px + uw + (w - uw) / 2] ==
                   tiku_tint(heap_hue, 0.45f));
            /* The eighth area with a size: third row, second column. */
            int sy = y + step + 2 * step + (tiku_font_plain()->height - 6) / 2 + 3;
            assert(st.surface->px[sy * st.surface->w + 18 + HOST_CELL + 3] == heap_hue);
        }
        tiku_surface_free(st.surface);
    }
    puts("about_memory: this computer's maps PASS");

    /* The clock in MHz, from 1 GHz in GHz to the hundredth, none for 0. */
    char text[64];
    rate_text(320000000UL, text, sizeof text);
    assert(!strcmp(text, "320 MHz"));
    rate_text(159999800UL, text, sizeof text);
    assert(!strcmp(text, "160 MHz"));
    rate_text(2795000000UL, text, sizeof text);
    assert(!strcmp(text, "2.80 GHz"));
    rate_text(0UL, text, sizeof text);
    assert(text[0] == '\0');
    /* The name, with the count when there is more than one, or the count
     * alone. */
    processor_text(1, "ESP32-S31 rev 0.0", text, sizeof text);
    assert(!strcmp(text, "ESP32-S31 rev 0.0"));
    processor_text(2, "ESP32-S31 rev 0.0", text, sizeof text);
    assert(!strcmp(text, "2 x ESP32-S31 rev 0.0"));
    processor_text(1, "", text, sizeof text);
    assert(!strcmp(text, "1 processor"));
    processor_text(4, "", text, sizeof text);
    assert(!strcmp(text, "4 processors"));
    /* A name wider than the column ends in an ellipsis within it. */
    snprintf(text, sizeof text, "13th Gen Intel(R) Core(TM) i7-1360P with a long tail");
    fit_text(tiku_font_plain(), text, 120);
    assert(tiku_text_width(tiku_font_plain(), text) <= 120 &&
           !strcmp(text + strlen(text) - 3, "..."));
    snprintf(text, sizeof text, "short");
    fit_text(tiku_font_plain(), text, 400);
    assert(!strcmp(text, "short"));
    /* The chip: pins on each side, the graphite body, the gold die, and
     * nothing drawn outside its square. */
    {
        tiku_surface_t *s = tiku_surface_new(48, 48, TIKU_RGB(1, 2, 3));
        int inset = CHIP / 6, pitch = (CHIP - 2 * inset) / 6;

        paint_chip(s, 4, 4, CHIP);
        assert(s->px[(4 + CHIP / 2) * 48 + 4 + CHIP / 2] == TIKU_RGB(0xc9, 0xa2, 0x3f));
        assert(s->px[(4 + inset + 2) * 48 + 4 + CHIP - inset - 4] == TIKU_RGB(0x2a, 0x2e, 0x35));
        assert(s->px[(4 + 2) * 48 + 4 + inset + pitch] == TIKU_RGB(0xb8, 0xbe, 0xc8));
        assert(s->px[(4 + 2) * 48 + 4 + inset + 5 * pitch] == TIKU_RGB(0xb8, 0xbe, 0xc8));
        assert(s->px[(4 + CHIP - 3) * 48 + 4 + inset + 3 * pitch] == TIKU_RGB(0xb8, 0xbe, 0xc8));
        assert(s->px[(4 + inset + 2 * pitch) * 48 + 4 + 2] == TIKU_RGB(0xb8, 0xbe, 0xc8));
        assert(s->px[(4 + inset + 2 * pitch) * 48 + 4 + CHIP - 3] == TIKU_RGB(0xb8, 0xbe, 0xc8));
        for (int i = 0; i < 48; i++) {
            assert(s->px[1 * 48 + i] == TIKU_RGB(1, 2, 3) && s->px[i * 48 + 46] == TIKU_RGB(1, 2, 3));
        }
        tiku_surface_free(s);
    }
    return 0;
}
