/* Corrected memory-map fixtures use the existing About parser unchanged. */
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
    return 0;
}
