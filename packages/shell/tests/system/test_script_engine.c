/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_script_engine.c - the scripted driver's reader and tally: a script
 * longer than the step table fails, and the Desktop icon checks look at the
 * Desktop alone.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_trk_script.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) { fails++; }
}

/** @brief The focused window's poses then the Desktop's: names notes.txt. */
static int
window_pose_at(void *ctx, const char *name, int *x, int *y)
{
    (void)ctx;
    *x = 10;
    *y = 10;
    return strcmp(name, "notes.txt") == 0;
}

/** @brief The Desktop's poses: names Boot only. */
static int
desk_pose_at(void *ctx, const char *name, int *x, int *y)
{
    (void)ctx;
    *x = 20;
    *y = 20;
    return strcmp(name, "Boot") == 0;
}

/** @brief Run @p s to its end against @p st.  @return the report's code. */
static int
run_to_end(tiku_trk_script_t *s, const tiku_trk_script_state_t *st)
{
    tiku_trk_script_step_t out;
    int turns = 0;

    while (turns++ < 100000 && tiku_trk_script_pump(s, st, &out)) {
        if (out.what == TIKU_TRK_SC_QUIT) {
            break;
        }
    }
    return tiku_trk_script_report(s, "test");
}

int
main(void)
{
    const char *path = "/tmp/trk_script_engine_test.script";
    tiku_trk_script_state_t st;
    tiku_trk_script_t *s;
    FILE *f;
    int i, rc;

    f = fopen(path, "w");
    if (!f) return 1;
    fputs("key numbersign # a literal hash without starting a comment\n", f);
    (void)fclose(f);
    memset(&st, 0, sizeof st);
    s = tiku_trk_script_open(path);
    check(s != NULL, "the numbersign script opens");
    if (s) {
        tiku_trk_script_step_t out;

        check(tiku_trk_script_pump(s, &st, &out) &&
              out.what == TIKU_TRK_SC_KEY && out.key == '#',
              "numbersign produces a literal hash key");
        tiku_trk_script_free(s);
    }

    printf("a script longer than the step table\n");
    f = fopen(path, "w");
    if (f == NULL) {
        return 1;
    }
    for (i = 0; i < 512; i++) {
        fputs("expect-file /\n", f);
    }
    fputs("# a comment past the table is not a step\n\n", f);
    fputs("expect-no-file /\n", f);
    (void)fclose(f);
    memset(&st, 0, sizeof st);
    s = tiku_trk_script_open(path);
    check(s != NULL, "the script opens");
    if (s != NULL) {
        rc = run_to_end(s, &st);
        check(rc != 0, "a step past the table fails the run");
        tiku_trk_script_free(s);
    }

    printf("\na script that fits the table\n");
    f = fopen(path, "w");
    if (f == NULL) {
        return 1;
    }
    for (i = 0; i < 512; i++) {
        fputs("expect-file /\n", f);
    }
    fputs("# a comment past the table is not a step\n\n", f);
    (void)fclose(f);
    s = tiku_trk_script_open(path);
    if (s != NULL) {
        rc = run_to_end(s, &st);
        check(rc == 0, "512 passing steps and trailing comments pass");
        tiku_trk_script_free(s);
    }

    printf("\nthe Desktop icon checks\n");
    f = fopen(path, "w");
    if (f == NULL) {
        return 1;
    }
    fputs("expect-no-desk-icon notes.txt\n", f);
    (void)fclose(f);
    memset(&st, 0, sizeof st);
    st.pose_at = window_pose_at;
    st.pose_desk_at = desk_pose_at;
    s = tiku_trk_script_open(path);
    if (s != NULL) {
        (void)run_to_end(s, &st);
        check(tiku_trk_script_failures(s) == 0,
              "a window's notes.txt is not on the Desktop");
        tiku_trk_script_free(s);
    }
    f = fopen(path, "w");
    if (f == NULL) {
        return 1;
    }
    fputs("expect-desk-icon notes.txt\n", f);
    (void)fclose(f);
    s = tiku_trk_script_open(path);
    if (s != NULL) {
        (void)run_to_end(s, &st);
        check(tiku_trk_script_failures(s) == 1,
              "expect-desk-icon fails for a window's notes.txt");
        tiku_trk_script_free(s);
    }
    f = fopen(path, "w");
    if (f == NULL) {
        return 1;
    }
    fputs("expect-desk-icon Boot\n", f);
    (void)fclose(f);
    s = tiku_trk_script_open(path);
    if (s != NULL) {
        (void)run_to_end(s, &st);
        check(tiku_trk_script_failures(s) == 0,
              "expect-desk-icon finds the Desktop's Boot");
        tiku_trk_script_free(s);
    }
    st.pose_desk_at = NULL;
    s = tiku_trk_script_open(path);
    if (s != NULL) {
        (void)run_to_end(s, &st);
        check(tiku_trk_script_failures(s) == 1,
              "without a Desktop lookup, pose_at answers");
        tiku_trk_script_free(s);
    }

    (void)remove(path);
    printf("\n%s\n", fails ? "FAILURES" : "the script engine holds");
    return fails ? 1 : 0;
}
