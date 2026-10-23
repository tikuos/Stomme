#include "tiku_shell_product.h"
/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_script_stub.c - the shipping build's refusal of -script: it opens
 * nothing, and the build and the script it names exist.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tiku_trk_script.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) { fails++; }
}

int
main(void)
{
    char said[1024];
    char *cut;
    size_t got;
    FILE *f;
    int keep;

    /* The refusal goes to stderr: catch it in a file. */
    f = tmpfile();
    if (f == NULL) {
        return 1;
    }
    fflush(stderr);
    keep = dup(fileno(stderr));
    (void)dup2(fileno(f), fileno(stderr));
    check(tiku_trk_script_open("x.script") == NULL, "-script is refused");
    fflush(stderr);
    (void)dup2(keep, fileno(stderr));
    (void)close(keep);
    rewind(f);
    got = fread(said, 1u, sizeof said - 1u, f);
    said[got] = '\0';
    (void)fclose(f);
    printf("%s", said);

    check(strstr(said, "TRK_SCRIPT_ENABLE") == NULL,
          "no build variable that no makefile reads");
    check(strstr(said, TIKU_SHELL_TEST_DESKTOP_BINARY) != NULL,
          "it names the development flavour's binary");
    cut = strstr(said, "tests/");
    if (cut != NULL) {
        cut[strcspn(cut, " \n")] = '\0';
        if (cut[0] != '\0' && cut[strlen(cut) - 1u] == '.') {
            cut[strlen(cut) - 1u] = '\0';
        }
    }
    check(cut != NULL && access(cut, R_OK) == 0,
          "the script it names exists in the tree");

    printf("\n%s\n", fails ? "FAILURES" : "the stub's refusal holds");
    return fails ? 1 : 0;
}
