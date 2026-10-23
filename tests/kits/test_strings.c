/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_strings.c - the words the interface is made of.
 *
 * That a key finds its line; that a translation read over the top wins;
 * that a line nobody has is its own name rather than a blank; and that a
 * catalogue built to hurt is read without harm, since these files are
 * dropped in a folder like the fonts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_strings.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] TS-STR  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

/** @brief Write @p body to a fresh temp file, and return its path. */
static const char *
plant(const char *body)
{
    static char path[] = "/tmp/trk_strXXXXXX";
    int fd;
    FILE *f;

    strcpy(path, "/tmp/trk_strXXXXXX");
    fd = mkstemp(path);
    if (fd < 0) {
        return NULL;
    }
    f = fdopen(fd, "wb");
    if (f == NULL) {
        return NULL;
    }
    (void)fwrite(body, 1u, strlen(body), f);
    (void)fclose(f);
    return path;
}

int
main(void)
{
    const char *p;

    printf("the words the interface is made of (TS-STR)\n");

    /* The baked English is there with no file loaded at all. */
    check(tiku_strings_builtin_count() > 20, "the English is baked in");
    check(strcmp(tiku_str("button.defaults"), "Defaults") == 0,
          "a key finds its baked line");
    check(strstr(tiku_str("menu.find"), "Find") != NULL &&
          (unsigned char)tiku_str("menu.find")[
              strlen(tiku_str("menu.find")) - 1] == 0xA6u,
          "and keeps its UTF-8: Find… ends in an ellipsis, three bytes");
    check(strcmp(tiku_str("no.such.key.at.all"), "no.such.key.at.all")
          == 0, "a line nobody has is its own name, never a blank");
    check(strcmp(tiku_str(NULL), "") == 0, "and NULL is the empty line");

    /* A translation read over the top: its lines win, the rest stay. */
    p = plant("<?xml version=\"1.0\"?>\n<strings lang=\"fr\">\n"
              "  <string name=\"button.defaults\">Par d\xc3\xa9""faut</string>\n"
              "  <string name=\"button.revert\">R\xc3\xa9tablir</string>\n"
              "</strings>\n");
    check(p != NULL && tiku_strings_load_file(p) == 2,
          "a translation loads the lines it carries");
    check(strcmp(tiku_str("button.defaults"), "Par d\xc3\xa9""faut") == 0,
          "and they win over the baked English");
    check(strstr(tiku_str("menu.about"), "About") != NULL,
          "while a line it did NOT carry is still the English");
    if (p != NULL) {
        (void)remove(p);
    }
    tiku_strings_reset();
    check(strcmp(tiku_str("button.defaults"), "Defaults") == 0,
          "and reset puts the English back");

    /* Entities: a translator writes an ampersand, an accent, a code point. */
    p = plant("<strings>\n"
              "  <string name=\"a\">Fichiers &amp; dossiers</string>\n"
              "  <string name=\"b\">caf&#233;</string>\n"
              "  <string name=\"c\">&#x3b1;&#x3b2;</string>\n"
              "  <string name=\"d\">&lt;tag&gt; &quot;q&quot;</string>\n"
              "</strings>\n");
    (void)(p != NULL && tiku_strings_load_file(p));
    check(strcmp(tiku_str("a"), "Fichiers & dossiers") == 0,
          "&amp; becomes an ampersand");
    check(strcmp(tiku_str("b"), "caf\xc3\xa9") == 0,
          "&#233; becomes the letter, as UTF-8");
    check(strcmp(tiku_str("c"), "\xce\xb1\xce\xb2") == 0,
          "&#x3b1; becomes Greek, which the device can now draw");
    check(strcmp(tiku_str("d"), "<tag> \"q\"") == 0,
          "and the angle brackets and quotes come back as themselves");
    if (p != NULL) {
        (void)remove(p);
    }
    tiku_strings_reset();

    /*
     * The name attribute, found among others.  A reader that matched the
     * letters "name" anywhere would take a key out of a neighbour's value
     * or lose the entry; an unterminated quote must be refused, not kept.
     */
    p = plant("<strings>\n"
              "  <string tip=\"pick a name here\" name=\"greet\">Hi</string>\n"
              "  <string other=\"name='trap'\" name=\"real\">Yes</string>\n"
              "  <string namely=\"trap\" name=\"right\">Yes</string>\n  <string name=\"open\">Close</string>\n"
              "</strings>\n");
    (void)(p != NULL && tiku_strings_load_file(p));
    check(strcmp(tiku_str("greet"), "Hi") == 0,
          "the name= attribute is found past a decoy in another value");
    check(strcmp(tiku_str("real"), "Yes") == 0 &&
          strcmp(tiku_str("trap"), "trap") == 0,
          "and a name='...' inside another value is not taken for the key");
    check(strcmp(tiku_str("open"), "Close") == 0,
          "a plain entry beside the decoys still reads");
    check(strcmp(tiku_str("right"), "Yes") == 0 &&
          strcmp(tiku_str("namely"), "namely") == 0,
          "and an attribute merely BEGINNING with name is not it either");
    if (p != NULL) {
        (void)remove(p);
    }
    tiku_strings_reset();
    {
        const char *bad = plant("<string name=\"ab>text</string>");

        (void)(bad != NULL && tiku_strings_load_file(bad));
        check(strcmp(tiku_str("ab"), "ab") == 0,
              "an attribute quote that never closes is refused, not stored");
        if (bad != NULL) {
            (void)remove(bad);
        }
        tiku_strings_reset();
    }

    /*
     * A translation is passed to snprintf as a format where the English
     * was, so one whose %-specifiers do not match must be refused -- else
     * "Dock: %d" could become "Dock: %s %s" and read arguments not there.
     */
    p = plant("<strings>\n"
              "  <string name=\"prefs.deskbar.dock_n\">Quai : %s %s</string>\n"
              "  <string name=\"button.defaults\">D\xc3\xa9""fauts</string>\n"
              "</strings>\n");
    (void)(p != NULL && tiku_strings_load_file(p));
    check(strcmp(tiku_str("prefs.deskbar.dock_n"), "Dock: %d") == 0,
          "a translation whose format does not match is refused");
    check(strcmp(tiku_str("button.defaults"), "D\xc3\xa9""fauts") == 0,
          "while a plain translation beside it is still taken");
    if (p != NULL) {
        (void)remove(p);
    }
    tiku_strings_reset();

    /*
     * A '*' width or precision takes an int argument, and a length
     * modifier changes the argument's type, so a line that adds either
     * reads arguments the English line does not pass.
     */
    p = plant("<strings>\n"
              "  <string name=\"prefs.deskbar.dock_n\">Quai : %lld</string>\n"
              "  <string name=\"status.selected\">%*d/%d choisis</string>\n"
              "  <string name=\"alert.msg.exists_here\">%ls existe</string>\n"
              "  <string name=\"alert.msg.exists_replace\">"
              "%.*s existe</string>\n"
              "</strings>\n");
    (void)(p != NULL && tiku_strings_load_file(p));
    check(strcmp(tiku_str("prefs.deskbar.dock_n"), "Dock: %d") == 0,
          "a translation that turns %d into %lld is refused");
    check(strcmp(tiku_str("status.selected"), "%d/%d selected") == 0,
          "a translation that adds a * width is refused");
    check(strstr(tiku_str("alert.msg.exists_here"), "%.80s") != NULL,
          "a translation that turns %s into %ls is refused");
    check(strstr(tiku_str("alert.msg.exists_replace"), "%.64s") != NULL,
          "a translation that adds a * precision is refused");
    if (p != NULL) {
        (void)remove(p);
    }
    tiku_strings_reset();

    /* A specifier longer than the signature's buffer is cut short and
     * refused, without writing past the buffer. */
    p = plant("<strings>\n"
              "  <string name=\"prefs.deskbar.dock_n\">Quai : "
              "%****************************************d</string>\n"
              "</strings>\n");
    (void)(p != NULL && tiku_strings_load_file(p));
    check(strcmp(tiku_str("prefs.deskbar.dock_n"), "Dock: %d") == 0,
          "a specifier longer than the signature is refused");
    if (p != NULL) {
        (void)remove(p);
    }
    tiku_strings_reset();

    /* Flags, a fixed width and a fixed precision take no argument. */
    p = plant("<strings>\n"
              "  <string name=\"alert.msg.exists_here\">"
              "&quot;%-20s&quot; existe ici.</string>\n"
              "  <string name=\"prefs.deskbar.dock_n\">Quai : %03d</string>\n"
              "</strings>\n");
    (void)(p != NULL && tiku_strings_load_file(p));
    check(strcmp(tiku_str("alert.msg.exists_here"),
                 "\"%-20s\" existe ici.") == 0,
          "a translation that changes only flags and widths is taken");
    check(strcmp(tiku_str("prefs.deskbar.dock_n"), "Quai : %03d") == 0,
          "and so is one that pads a number");
    if (p != NULL) {
        (void)remove(p);
    }
    tiku_strings_reset();

    /*
     * A catalogue built to hurt.  These files arrive by being dropped in
     * a folder; an unterminated tag, an entity with no semicolon, a value
     * that never closes -- each must be read without running off the end,
     * and must not leave a half-line behind.
     */
    {
        static const char *const nasty[] = {
            "<string name=\"x\">",
            "<string name=\"x\">unclosed value with no end tag",
            "<string name=",
            "<string name=\"x\">a &",
            "<string name=\"x\">b &#;",
            "<string name=\"x\">c &#xZZZZ;",
            "<!-- a comment that never ends",
            "<string name=\"\">empty key</string>",
            "<string name=\"x\">&#x110000;</string>",
            "<string",
            "&&&&&&&&<<<<<<<>>>>>>",
        };
        size_t i;
        int survived = 1;

        for (i = 0; i < sizeof nasty / sizeof nasty[0]; i++) {
            const char *bad = plant(nasty[i]);

            if (bad != NULL) {
                (void)tiku_strings_load_file(bad);
                (void)remove(bad);
            }
            tiku_strings_reset();
        }
        check(survived && strcmp(tiku_str("button.revert"), "Revert")
              == 0, "a catalogue built to hurt is read without harm");
    }

    if (fails > 0) {
        printf("\nFAILURES\n");
        return 1;
    }
    printf("\nthe interface's words hold\n");
    return 0;
}
