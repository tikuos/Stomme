#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tiku_trk_addon.h"
static int invoked;
static int invoke(const tiku_trk_addon_t *a, void *ctx)
{ invoked = (a->key == 'x' && ctx != NULL); return invoked; }
int main(void) {
    tiku_trk_addons_t a; tiku_menu_t m; const char *types[] = {"text/plain"};
    tiku_trk_addons_init(&a);
    tiku_trk_addons_register(&a, "Generic", "/bin/true", "*", 'g');
    tiku_trk_addons_register(&a, "Writer", "/bin/true", "text/plain", 'w');
    if (tiku_trk_addons_build_menu(&a, types, 1, &m, 1000) != 2 ||
        m.item[0].label[0] != 'W') return 1;
    /* Shown as Option and Command, the chord the key handler runs it on,
     * with no Shift mark. */
    if (m.item[0].mods != (TIKU_MENU_MOD_CMD | TIKU_MENU_MOD_OPTION) ||
        (m.item[0].mods & TIKU_MENU_MOD_SHIFT) != 0u) {
        puts("[FAIL] the Add-ons menu shows Option and Command, not Shift");
        return 1;
    }
    if (tiku_trk_addons_rebind(&a, "Writer=x\n") != 1 || m.item[0].shortcut != 'w') return 1;
    tiku_trk_addons_build_menu(&a, types, 1, &m, 1000);
    if (m.item[0].shortcut != 'x') return 1;
    tiku_menu_set_icon(&m, m.item[0].command, "text-file");
    if (strcmp(m.item[0].icon, "text-file") != 0) return 1;
    if (!tiku_trk_addons_invoke(&a, 1001, 1000, invoke, &a) || !invoked) return 1;
    if (tiku_trk_addons_shortcut(&a, 'x', 0x3u, 1000) != 1001) return 1;
    if (tiku_trk_addons_shortcut(&a, 'x', 0x2u, 1000) >= 0) return 1;
    {   /* A list of types: the desktop's Open with Editor takes plain
         * text and Markdown, and only those, whole. */
        tiku_trk_addons_t l;
        const char *md[] = {"text/markdown"}, *html[] = {"text/html"};
        const char *part[] = {"text/mark"};
        const char *both[] = {"text/html", "text/plain"};
        tiku_trk_addons_init(&l);
        tiku_trk_addons_register(&l, "Open with Editor", "",
                                 "text/plain,text/markdown", 'e');
        if (!tiku_trk_addons_accepts(&l.item[0], types, 1) ||
            !tiku_trk_addons_accepts(&l.item[0], md, 1) ||
            !tiku_trk_addons_accepts(&l.item[0], both, 2)) return 1;
        if (tiku_trk_addons_accepts(&l.item[0], html, 1) ||
            tiku_trk_addons_accepts(&l.item[0], part, 1)) return 1;
    }
    {   /* Discovery: an add-ons directory of executables (CW-038).  A
         * fixture of its own, so every gate can be asked. */
        char dir[] = "/tmp/trk_addons_test";
        FILE *f;

        (void)system("rm -rf /tmp/trk_addons_test && mkdir -p /tmp/trk_addons_test");
        f = fopen("/tmp/trk_addons_test/stamp", "w");
        if (f == NULL) return 1;
        fputs("#!/bin/sh\nexit 0\n", f); fclose(f);
        (void)system("chmod +x /tmp/trk_addons_test/stamp");
        f = fopen("/tmp/trk_addons_test/stamp.types", "w");
        if (f == NULL) return 1;
        fputs("text/plain\ns\n", f); fclose(f);
        f = fopen("/tmp/trk_addons_test/notes.txt", "w");   /* not executable */
        if (f == NULL) return 1;
        fputs("hi\n", f); fclose(f);
        if (tiku_trk_addons_discover(&a, dir) != 1) return 1;
        if (a.count != 3) return 1;              /* two static + one found */
        if (!a.item[2].external) return 1;
        if (strcmp(a.item[2].name, "stamp") != 0) return 1;
        if (a.item[2].command[0] != '/') return 1;
        if (strcmp(a.item[2].type, "text/plain") != 0 || a.item[2].generic)
            return 1;
        /* The sidecar's second line is the built-in default key, bound as
         * Cmd+Opt+<key> (AW-022). */
        if (a.item[2].key != 's' || a.item[2].mods != 0x3u) return 1;
        if (tiku_trk_addons_shortcut(&a, 's', 0x3u, 1000) != 1002) return 1;
        /* Re-discovery replaces what was found, not what was compiled in;
         * a removed add-on leaves the menu (AW-023). */
        (void)system("rm /tmp/trk_addons_test/stamp");
        if (tiku_trk_addons_discover(&a, dir) != 0) return 1;
        if (a.count != 2) return 1;
        (void)system("rm -rf /tmp/trk_addons_test");
    }
    puts("[PASS] add-on inventory, MIME ordering and live shortcut rebinding"); return 0;
}
