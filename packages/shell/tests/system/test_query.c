/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_query.c - predicates, the glob, scope and matching.
 *
 * The worked examples come from the source that specified them, so a change
 * in the matcher shows up as a disagreement with Tracker rather than only as
 * a disagreement with an earlier version of this file.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "tiku_query.h"
#include "tiku_state.h"
#include "tiku_trk_find.h"

static int fails;

typedef struct {
    int trash_listed;
    uint64_t scope_dev;
    int scope_kind;
    int scope_exists;
} trash_walk_t;

static int
trash_walk_stat(tiku_backend_t *b, const char *path,
                tiku_model_t *out)
{
    trash_walk_t *walk = b->impl;

    if (strcmp(path, "/scope") != 0 || !walk->scope_exists) {
        return -1;
    }
    memset(out, 0, sizeof *out);
    out->kind = walk->scope_kind ? TIKU_KIND_FILE
                                 : TIKU_KIND_DIRECTORY;
    out->facts.dev = walk->scope_dev;
    snprintf(out->name, sizeof out->name, "scope");
    snprintf(out->path, sizeof out->path, "/scope");
    return 0;
}

/** @brief A tiny namespace that proves excluded containers are not walked. */
static int
trash_walk_list(tiku_backend_t *b, const char *path,
                tiku_entry_fn fn, void *ctx)
{
    trash_walk_t *walk = b->impl;
    tiku_model_t m;
    int n = 0;

    memset(&m, 0, sizeof m);
    if (strcmp(path, "/") == 0) {
        m.kind = TIKU_KIND_TRASH;
        snprintf(m.name, sizeof m.name, ".Trash");
        snprintf(m.path, sizeof m.path, "/.Trash");
        n += fn(&m, ctx) >= 0;
        m.kind = TIKU_KIND_FILE;
        snprintf(m.name, sizeof m.name, "visible.txt");
        snprintf(m.path, sizeof m.path, "/visible.txt");
        n += fn(&m, ctx) >= 0;
    } else if (strcmp(path, "/.Trash") == 0) {
        walk->trash_listed++;
        m.kind = TIKU_KIND_FILE;
        snprintf(m.name, sizeof m.name, "deleted.txt");
        snprintf(m.path, sizeof m.path, "/.Trash/deleted.txt");
        n += fn(&m, ctx) >= 0;
    } else if (strcmp(path, "/scope") == 0) {
        m.kind = TIKU_KIND_FILE;
        m.facts.dev = walk->scope_dev;
        snprintf(m.name, sizeof m.name, "inside.txt");
        snprintf(m.path, sizeof m.path, "/scope/inside.txt");
        n += fn(&m, ctx) >= 0;
    }
    return n;
}

static int
depth_walk_stat(tiku_backend_t *b, const char *path,
                tiku_model_t *out)
{
    (void)b;
    memset(out, 0, sizeof *out);
    out->kind = TIKU_KIND_DIRECTORY;
    snprintf(out->path, sizeof out->path, "%s", path);
    snprintf(out->name, sizeof out->name, "scope");
    return strcmp(path, "/scope") == 0 ? 0 : -1;
}

/** @brief /scope holds dirs a then b; each holds one matching file. */
static int
depth_walk_list(tiku_backend_t *b, const char *path,
                tiku_entry_fn fn, void *ctx)
{
    tiku_model_t m;
    int n = 0;

    (void)b;
    memset(&m, 0, sizeof m);
    if (strcmp(path, "/scope") == 0) {
        m.kind = TIKU_KIND_DIRECTORY;
        snprintf(m.name, sizeof m.name, "a");
        snprintf(m.path, sizeof m.path, "/scope/a");
        n += fn(&m, ctx) >= 0;
        snprintf(m.name, sizeof m.name, "b");
        snprintf(m.path, sizeof m.path, "/scope/b");
        n += fn(&m, ctx) >= 0;
    } else if (strcmp(path, "/scope/a") == 0 ||
               strcmp(path, "/scope/b") == 0) {
        m.kind = TIKU_KIND_FILE;
        snprintf(m.name, sizeof m.name, "%c-hit", path[7]);
        snprintf(m.path, sizeof m.path, "%s/%c-hit", path, path[7]);
        n += fn(&m, ctx) >= 0;
    }
    return n;
}

/** @brief /scope holds one matching symlink whose target is /scope. */
static int
cycle_walk_list(tiku_backend_t *b, const char *path,
                tiku_entry_fn fn, void *ctx)
{
    tiku_model_t m;
    int n = 0;

    (void)b;
    memset(&m, 0, sizeof m);
    m.kind = TIKU_KIND_SYMLINK;
    snprintf(m.type, sizeof m.type, "application/x-vnd.Be-directory");
    snprintf(m.name, sizeof m.name, "loop-hit");
    snprintf(m.path, sizeof m.path, "%s/loop-hit", path);
    n += fn(&m, ctx) >= 0;
    return n;
}

static const tiku_backend_ops_t CYCLE_WALK_OPS = {
    .stat = depth_walk_stat,
    .list = cycle_walk_list
};

static const tiku_backend_ops_t DEPTH_WALK_OPS = {
    .stat = depth_walk_stat,
    .list = depth_walk_list
};

static const tiku_backend_ops_t TRASH_WALK_OPS = {
    .stat = trash_walk_stat,
    .list = trash_walk_list
};

static void
check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) { fails++; }
}

static tiku_model_t
node(const char *path, const char *name, tiku_kind_t kind,
     const char *meta, unsigned perm, long size)
{
    tiku_model_t m;

    memset(&m, 0, sizeof m);
    snprintf(m.path, sizeof m.path, "%s", path);
    snprintf(m.name, sizeof m.name, "%s", name);
    snprintf(m.facts.meta, sizeof m.facts.meta, "%s", meta);
    m.kind = kind;
    m.facts.perm = perm;
    m.facts.size = size;
    return m;
}

static int
name_finds(const char *typed, const char *name)
{
    tiku_query_t q;
    tiku_model_t m = node("/x", name, TIKU_KIND_FILE, "-", 1u, 0);

    tiku_query_init(&q);
    tiku_query_set_name(&q, typed);
    return tiku_query_matches(&q, &m, NULL, "");
}

int
main(void)
{
    printf("the glob, which every operator goes through\n");
    check(tiku_query_glob("*.txt", "a.txt"), "*.txt matches a.txt");
    check(!tiku_query_glob("*.txt", "a.txtx"), "and not a.txtx");
    check(tiku_query_glob("[lL][eE][dD]*", "led0"),
          "a class pattern matches");
    check(tiku_query_glob("[lL][eE][dD]*", "LED9"),
          "and matches the other case");
    check(!tiku_query_glob("[lL][eE][dD]*", "lid0"),
          "but not a near miss");
    check(tiku_query_glob("a*b*c", "axxbyyc"), "two stars backtrack");
    check(!tiku_query_glob("a*b*c", "axxbyy"), "and fail honestly");
    check(tiku_query_glob("*", ""), "a lone star matches nothing at all");
    check(tiku_query_glob("l?d0", "led0"), "? matches one character");
    check(!tiku_query_glob("l?d0", "leed0"), "and only one");
    /* A '[' with nothing closing it is a literal bracket, not a class that
     * happens to swallow whatever follows. */
    check(!tiku_query_glob("[abc", "a"),
          "an unterminated class matches nothing");
    check(tiku_query_glob("[abc]", "a"), "a closed one still matches");
    check(tiku_query_glob("[!abc]", "z"), "and negation works");
    check(!tiku_query_glob("[!abc]", "a"), "both ways");

    printf("\ncase-insensitivity is a rewrite, not a flag\n");
    {
        char pat[128];

        (void)tiku_query_pattern("txt", TIKU_OP_CONTAINS, pat,
                                     sizeof pat);
        check(strcmp(pat, "*[tT][xX][tT]*") == 0,
              "\"txt\" contains becomes *[tT][xX][tT]*");
        (void)tiku_query_pattern("txt", TIKU_OP_BEGINS_WITH, pat,
                                     sizeof pat);
        check(strcmp(pat, "[tT][xX][tT]*") == 0,
              "starts-with anchors the front");
        (void)tiku_query_pattern("txt", TIKU_OP_ENDS_WITH, pat,
                                     sizeof pat);
        check(strcmp(pat, "*[tT][xX][tT]") == 0,
              "ends-with anchors the back");
        (void)tiku_query_pattern("txt", TIKU_OP_IS, pat, sizeof pat);
        check(strcmp(pat, "[tT][xX][tT]") == 0, "is anchors both");
        /* A space becomes a star, which is why the original finds
         * "foo-bar" when you type "foo bar". */
        (void)tiku_query_pattern("foo bar", TIKU_OP_CONTAINS, pat,
                                     sizeof pat);
        check(strcmp(pat, "*[fF][oO][oO]*[bB][aA][rR]*") == 0,
              "\"foo bar\" becomes *[fF][oO][oO]*[bB][aA][rR]*");
        /* Digits and punctuation are not letters and get no class. */
        (void)tiku_query_pattern("led0", TIKU_OP_IS, pat, sizeof pat);
        check(strcmp(pat, "[lL][eE][dD]0") == 0, "a digit stays a digit");
    }

    printf("\nthe star rule: a substring search, unless you type a star\n");
    check(name_finds("led", "led0"), "\"led\" finds led0 as a substring");
    check(name_finds("ED", "led0"), "and does so case-insensitively");
    check(name_finds("d0", "led0"), "a substring can be at the end");
    /* With a star the text stops being a substring and must match outright. */
    check(name_finds("led*", "led0"), "\"led*\" matches led0");
    check(!name_finds("*d", "led0"), "\"*d\" does NOT match led0");
    check(name_finds("*d0", "led0"), "\"*d0\" does");
    check(name_finds("foo bar", "foo-bar"),
          "a typed space spans anything, so \"foo bar\" finds foo-bar");
    check(name_finds("", "anything"),
          "empty text matches everything, as the substring form degenerates");

    printf("\nterms, and how they join\n");
    {
        tiku_query_t q;
        tiku_model_t led = node("/dev/led0", "led0", TIKU_KIND_DEVICE_NODE,
                                    "bool,bool,cached,free", 3u, 1);
        tiku_model_t ram = node("/sys/mem/free", "free", TIKU_KIND_DEVICE_NODE,
                                    "u32,bytes,cached,free", 1u, 5);
        tiku_desc_t dled, dram;

        (void)tiku_desc_parse(led.facts.meta, &dled);
        (void)tiku_desc_parse(ram.facts.meta, &dram);

        tiku_query_init(&q);
        (void)tiku_query_add_term(&q, TIKU_QF_WRITABLE,
                                      TIKU_OP_IS, "yes",
                                      TIKU_JOIN_AND);
        check(tiku_query_matches(&q, &led, &dled, "1"),
              "a writable node matches writable=yes");
        check(!tiku_query_matches(&q, &ram, &dram, "1024"),
              "a read-only one does not");

        /* Two terms ANDed: writable AND named led. */
        (void)tiku_query_add_term(&q, TIKU_QF_NAME,
                                      TIKU_OP_CONTAINS, "led",
                                      TIKU_JOIN_AND);
        check(tiku_query_matches(&q, &led, &dled, "1"),
              "and both terms together still match");

        /* Or: writable OR named free -- the read-only one comes back. */
        q.term[1].join = TIKU_JOIN_OR;
        snprintf(q.term[1].value, sizeof q.term[1].value, "free");
        check(tiku_query_matches(&q, &ram, &dram, "1024"),
              "an OR term readmits what the first term excluded");

        /* A unit term: everything measured in bytes, whatever it is called. */
        tiku_query_init(&q);
        (void)tiku_query_add_term(&q, TIKU_QF_UNIT, TIKU_OP_IS,
                                      "bytes", TIKU_JOIN_AND);
        check(tiku_query_matches(&q, &ram, &dram, "1024") &&
              !tiku_query_matches(&q, &led, &dled, "1"),
              "a unit term selects every node measured in bytes");
    }

    printf("\nordering compares numbers, not their spelling\n");
    {
        tiku_query_t q;
        tiku_model_t n1 = node("/a", "a", TIKU_KIND_DEVICE_NODE,
                                   "u32,count,cached,free", 1u, 0);
        tiku_desc_t d;

        (void)tiku_desc_parse(n1.facts.meta, &d);
        tiku_query_init(&q);
        (void)tiku_query_add_term(&q, TIKU_QF_VALUE,
                                      TIKU_OP_GREATER, "1000",
                                      TIKU_JOIN_AND);
        check(tiku_query_matches(&q, &n1, &d, "9999"),
              "9999 is greater than 1000");
        check(!tiku_query_matches(&q, &n1, &d, "999"),
              "999 is not (as text it would have been)");
        check(strcmp("999", "1000") > 0,
              "(comparing the text would have got this wrong)");

        /* Most namespace nodes declare no type at all, so the ordering
         * operator has to work on them from the values alone. */
        {
            tiku_model_t plain = node("/sys/boot_count", "boot_count",
                                          TIKU_KIND_DEVICE_NODE, "-",
                                          1u, 0);
            tiku_desc_t none;

            (void)tiku_desc_parse("-", &none);
            check(!tiku_query_matches(&q, &plain, &none, "30"),
                  "an UNTYPED node reading 30 is not over 1000");
            check(tiku_query_matches(&q, &plain, &none, "9999"),
                  "an untyped node reading 9999 is");
            /* A name is not a number, so it is neither greater nor less. */
            check(!tiku_query_matches(&q, &plain, &none, "EK-RA8P1"),
                  "a node whose value is a name is not over 1000 either");
        }
    }

    printf("\ntyped values and relative dates (Q-008, Q-009, Q-040)\n");
    {
        const int64_t now = 1723550400;
        tiku_query_t q;
        tiku_model_t m = node("/data/report", "report",
                                  TIKU_KIND_FILE, "-", 3u, 8192);

        tiku_query_init(&q);
        (void)tiku_query_add_term(&q, TIKU_QF_SIZE,
                                      TIKU_OP_GREATER, "4K",
                                      TIKU_JOIN_AND);
        check(tiku_query_matches(&q, &m, NULL, ""),
              "Q-008: scalar K/M/G suffixes are parsed for numeric fields");
        m.facts.size = 4096;
        check(!tiku_query_matches(&q, &m, NULL, ""),
              "the suffix boundary is compared numerically");

        tiku_query_init(&q);
        (void)tiku_query_add_term(&q, TIKU_QF_VALUE,
                                      TIKU_OP_GREATER, "2.5",
                                      TIKU_JOIN_AND);
        check(tiku_query_matches(&q, &m, NULL, "3.25"),
              "Q-008: floating-point values compare as numbers");

        tiku_query_init(&q);
        (void)tiku_query_add_term(&q, TIKU_QF_WRITABLE,
                                      TIKU_OP_IS, "true",
                                      TIKU_JOIN_AND);
        check(tiku_query_matches(&q, &m, NULL, ""),
              "Q-008: boolean spellings are normalized before comparison");

        m.facts.mtime = now - 3600;
        tiku_query_init(&q);
        (void)tiku_query_add_term(&q, TIKU_QF_MODIFIED,
                                      TIKU_OP_GREATER, "today",
                                      TIKU_JOIN_AND);
        check(q.dynamic_date,
              "Q-009: a relative Modified term marks the query dynamic");
        check(tiku_query_matches_at(&q, &m, NULL, "", now),
              "the current relative-date boundary admits the file");
        check(tiku_query_matches_at(&q, &m, NULL, "", now) &&
              !tiku_query_matches_at(&q, &m, NULL, "", now + 86400),
              "Q-040: the same file falls out after the date boundary");
        check(tiku_query_next_refresh(&q, now) > now &&
              tiku_query_next_refresh(&q, now) <= now + 86400,
              "Q-040: a dynamic query schedules its next boundary refresh");
    }

    printf("\nscope is a path prefix, enforced outside the predicate\n");
    {
        tiku_query_t q;

        tiku_query_init(&q);
        check(tiku_query_in_scope(&q, "/anywhere"),
              "with no scope everything is in scope");
        (void)tiku_query_add_scope(&q, "/dev");
        check(tiku_query_in_scope(&q, "/dev/led0"),
              "/dev admits /dev/led0");
        check(!tiku_query_in_scope(&q, "/sys/uptime"),
              "and excludes /sys/uptime");
        /* The trailing separator is why a sibling cannot sneak in. */
        check(!tiku_query_in_scope(&q, "/devices/x"),
              "/dev does NOT admit /devices/x");
        (void)tiku_query_add_scope(&q, "/sys");
        check(tiku_query_in_scope(&q, "/sys/uptime"),
              "a second scope widens it");
        check(tiku_query_add_scope(&q, "/sys") == 0,
              "adding the same scope twice is a no-op");
    }

    printf("\nsaved scopes are revalidated by identity (Q-027, Q-057)\n");
    {
        trash_walk_t walk = { 0, 41u, 0, 1 };
        tiku_backend_t backend = { &TRASH_WALK_OPS, "", &walk };
        tiku_query_t q;
        tiku_qrun_t *run;

        tiku_query_init(&q);
        (void)tiku_query_add_scope(&q, "/scope");
        tiku_query_bind_scopes(&q, &backend);
        check(q.scope_was[0].dev == 41u &&
              strcmp(q.scope_was[0].volume, "scope") == 0,
              "Q-027: saving binds a scope to the volume that owns it");
        run = tiku_query_start(&backend, &q);
        while (run != NULL && !tiku_query_done(run)) {
            (void)tiku_query_step(run, 8);
        }
        check(run != NULL && tiku_query_count(run) == 1 &&
              tiku_query_targets_device(run, 41u),
              "the same mounted identity is searched and remains active");
        tiku_query_stop(run);

        walk.scope_dev = 99u;
        run = tiku_query_start(&backend, &q);
        check(run != NULL && tiku_query_done(run) &&
              tiku_query_count(run) == 0 &&
              tiku_query_targets_device(run, 41u) &&
              !tiku_query_targets_device(run, 99u),
              "Q-027: a reused mount path does not retarget the saved query");
        tiku_query_stop(run);

        q.scope_was[0].dev = 0u;
        walk.scope_kind = 1;
        run = tiku_query_start(&backend, &q);
        while (run != NULL && !tiku_query_done(run)) {
            (void)tiku_query_step(run, 8);
        }
        check(run != NULL && tiku_query_done(run) &&
              tiku_query_count(run) == 1 &&
              strcmp(tiku_query_at(run, 0)->path, "/scope") == 0,
              "PVN-070: a file scope is returned as one result rather than "
              "being sent to the directory lister");
        tiku_query_stop(run);
    }

    printf("\nthe predicate the formula field would show\n");
    {
        tiku_query_t q;
        char pred[TIKU_QUERY_PRED_MAX];

        tiku_query_init(&q);
        tiku_query_set_name(&q, "led");
        (void)tiku_query_predicate(&q, pred, sizeof pred);
        check(strcmp(pred, "(name==\"*[lL][eE][dD]*\")") == 0,
              "a by-name search reads (name==\"*[lL][eE][dD]*\")");

        tiku_query_init(&q);
        (void)tiku_query_add_term(&q, TIKU_QF_UNIT, TIKU_OP_IS,
                                      "bytes", TIKU_JOIN_AND);
        (void)tiku_query_add_term(&q, TIKU_QF_VALUE,
                                      TIKU_OP_GREATER, "1000",
                                      TIKU_JOIN_AND);
        (void)tiku_query_predicate(&q, pred, sizeof pred);
        check(strcmp(pred, "(unit==\"[bB][yY][tT][eE][sS]\")&&(value>1000)")
              == 0, "two terms join with &&, and > keeps its number bare");
        if (strcmp(pred, "(unit==\"[bB][yY][tT][eE][sS]\")&&(value>1000)")) {
            printf("         got %s\n", pred);
        }
    }

    printf("\nformula execution (Q-010)\n");
    {
        tiku_query_t fq;
        tiku_model_t report, notes;
        const char *formula =
            "(name==\"*[rR][eE][pP][oO][rR][tT]*\")&&(size>10)";

        memset(&report, 0, sizeof report);
        memset(&notes, 0, sizeof notes);
        snprintf(report.name, sizeof report.name, "Quarterly Report");
        report.facts.size = 20;
        snprintf(notes.name, sizeof notes.name, "notes");
        notes.facts.size = 20;
        check(tiku_query_set_formula(&fq, formula) == 0 &&
              fq.mode == TIKU_QMODE_FORMULA && fq.term_count == 2,
              "Q-010: a typed formula becomes the predicate that runs");
        check(tiku_query_matches(&fq, &report, NULL, NULL) &&
              !tiku_query_matches(&fq, &notes, NULL, NULL),
              "the quoted pattern and numeric comparison both govern "
              "the result rather than the old attribute rows");
        check(tiku_query_set_formula(&fq, "this is not a predicate")
              != 0 && fq.invalid && fq.term_count == 0,
              "an invalid formula is refused rather than silently running "
              "a different search");
        {
            trash_walk_t walk = { 0 };
            tiku_backend_t backend = { &TRASH_WALK_OPS, "", &walk };

            check(tiku_query_start(&backend, &fq) == NULL,
                  "and cannot start a whole-namespace run");
        }
    }

    printf("\nTrash is excluded unless explicitly included (Q-036)\n");
    {
        tiku_query_t q;
        tiku_model_t trash, deleted, visible;

        memset(&trash, 0, sizeof trash);
        memset(&deleted, 0, sizeof deleted);
        memset(&visible, 0, sizeof visible);
        trash.kind = TIKU_KIND_TRASH;
        snprintf(trash.path, sizeof trash.path, "/home/me/.Trash");
        deleted.kind = TIKU_KIND_FILE;
        snprintf(deleted.path, sizeof deleted.path,
                 "/home/me/.Trash/deleted.txt");
        visible.kind = TIKU_KIND_FILE;
        snprintf(visible.path, sizeof visible.path,
                 "/home/me/deleted.txt");
        tiku_query_init(&q);
        snprintf(q.trash_path, sizeof q.trash_path, "/home/me/.Trash");
        check(!tiku_query_allows_path(&q, &trash) &&
              !tiku_query_allows_path(&q, &deleted),
              "Q-036: both the Trash container and its children are out by "
              "default");
        check(tiku_query_allows_path(&q, &visible),
              "a similarly named item outside the Trash subtree stays in");
        q.include_trash = 1;
        check(tiku_query_allows_path(&q, &trash) &&
              tiku_query_allows_path(&q, &deleted),
              "the Include Trash option admits the entire subtree");

        {
            trash_walk_t walk = { 0 };
            tiku_backend_t backend = { &TRASH_WALK_OPS, "", &walk };
            tiku_qrun_t *run;

            tiku_query_init(&q);
            run = tiku_query_start(&backend, &q);
            while (run != NULL && !tiku_query_done(run)) {
                (void)tiku_query_step(run, 8);
            }
            check(run != NULL && tiku_query_count(run) == 1 &&
                  walk.trash_listed == 0,
                  "the default filter stops before enqueueing the Trash "
                  "container");
            tiku_query_stop(run);

            q.include_trash = 1;
            run = tiku_query_start(&backend, &q);
            while (run != NULL && !tiku_query_done(run)) {
                (void)tiku_query_step(run, 8);
            }
            check(run != NULL && tiku_query_count(run) == 3 &&
                  walk.trash_listed == 1,
                  "including Trash walks it and returns its child");
            tiku_query_stop(run);
        }
    }

    printf("\nsaved queries survive a round trip\n");
    {
        tiku_store_t *st = tiku_store_memory_open();
        tiku_query_t a, b;
        char names[8][128];
        int n;

        tiku_query_init(&a);
        (void)tiku_query_add_term(&a, TIKU_QF_UNIT, TIKU_OP_IS,
                                      "bytes", TIKU_JOIN_AND);
        (void)tiku_query_add_term(&a, TIKU_QF_VALUE,
                                      TIKU_OP_GREATER, "1000",
                                      TIKU_JOIN_OR);
        (void)tiku_query_add_scope(&a, "/sys");
        check(tiku_query_save(&a, "big memory", st) == 0,
              "a query saves");
        check(tiku_query_load(&b, "big memory", st) == 0,
              "and loads back");
        check(b.term_count == a.term_count && b.scope_count == a.scope_count,
              "with its terms and scopes intact");
        check(b.term[1].op == TIKU_OP_GREATER &&
              b.term[1].join == TIKU_JOIN_OR &&
              strcmp(b.term[1].value, "1000") == 0,
              "including the operator, the join and the value");
        check(tiku_query_load(&b, "no such query", st) == -1,
              "an unsaved name does not load");

        n = tiku_query_list(st, names, 8);
        check(n == 1 && strcmp(names[0], "big memory") == 0,
              "a saved query is listed by name");
        (void)tiku_query_save(&a, "second", st);
        n = tiku_query_list(st, names, 8);
        check(n == 2 && strcmp(names[0], "second") == 0,
              "the newest is listed first");
        (void)tiku_query_forget("second", st);
        n = tiku_query_list(st, names, 8);
        check(n == 1 && strcmp(names[0], "big memory") == 0,
              "forgetting removes it from the list");
        (void)tiku_query_save(&a, "Old Logs", st);
        (void)tiku_query_save(&a, "Logs", st);
        n = tiku_query_list(st, names, 8);
        check(n == 3 && strcmp(names[0], "Logs") == 0,
              "D-132: a name inside an indexed name is listed too");
        (void)tiku_query_save(&a, "Logs", st);
        n = tiku_query_list(st, names, 8);
        check(n == 3, "and saving it again does not list it twice");
        (void)tiku_query_forget("Logs", st);
        (void)tiku_query_forget("Old Logs", st);

        /* What a record has to carry to be re-openable and sweepable. */
        a.kind = TIKU_QREC_TEMPLATE;
        a.temporary = 1;
        a.saved_at = 1700000000;
        a.include_trash = 1;
        snprintf(a.trash_path, sizeof a.trash_path, "/home/me/.Trash");
        snprintf(a.type, sizeof a.type, "text/plain");
        (void)tiku_query_add_term(&a, TIKU_QF_MODIFIED,
                                      TIKU_OP_GREATER, "today",
                                      TIKU_JOIN_AND);
        a.scope_was[0].dev = 4242u;
        snprintf(a.scope_was[0].volume, sizeof a.scope_was[0].volume,
                 "Backup");
        (void)tiku_query_save(&a, "record", st);
        check(tiku_query_load(&b, "record", st) == 0 &&
              b.kind == TIKU_QREC_TEMPLATE, "Q-028: a record says "
              "whether opening it RUNS it or opens the panel filled in");
        check(b.temporary == 1 && b.saved_at == 1700000000,
              "MA-053: and whether anyone meant to keep it, and when it "
              "was written -- the two things a sweep of stale by-products "
              "needs and neither of which existed");
        check(strcmp(b.type, "text/plain") == 0,
              "Q-004: the type it was narrowed to comes back with it");
        check(b.include_trash == 1 && b.trash_path[0] == '\0',
              "Q-036: the Include Trash choice survives a saved-query "
              "round trip without pinning it to this machine's Trash path");
        check(b.dynamic_date == 1, "Q-009: the dynamic-date marker survives "
              "a saved-query round trip");
        check(b.scope_was[0].dev == 4242u &&
              strcmp(b.scope_was[0].volume, "Backup") == 0,
              "Q-027: and a scope remembers WHICH volume it meant, not "
              "only the path -- mount points are reused, and a query that "
              "kept only the path searches whatever took its place");
        check(strcmp(b.scope[0], a.scope[0]) == 0,
              "with the path still there beside it");
        {   /* An identity line with no scope before it belongs to nothing. */
            tiku_query_t stray;

            (void)tiku_state_write(st, "stray", "_trk/query",
                                       "v2\nmode 0\nd 77 Ghost\n", 22);
            check(tiku_query_load(&stray, "stray", st) == 0 &&
                  stray.scope_count == 0 && stray.dropped == 1 &&
                  stray.scope_was[0].dev == 0u,
                  "Q-027: an identity line that arrives BEFORE any scope is "
                  "dropped rather than attached to the next one -- a scope "
                  "wearing another volume's identity is worse than a scope "
                  "with none");
        }
        {   /* A v1 record still reads: the new fields take defaults. */
            tiku_query_t old;

            (void)tiku_state_write(st, "ancient", "_trk/query",
                                       "v1\nmode 0\nt 0 0 0 hello\n", 25);
            check(tiku_query_load(&old, "ancient", st) == 0 &&
                  old.term_count == 1 &&
                  strcmp(old.term[0].value, "hello") == 0 &&
                  old.kind == TIKU_QREC_QUERY && old.temporary == 0,
                  "a record written before any of this still loads, with "
                  "the fields it does not carry taking their defaults");
        }
        tiku_store_free(st);
    }

    printf("\nthe Find panel\n");
    {
        tiku_trk_find_t f;
        tiku_query_t q;
        char lbl[64];

        tiku_trk_find_init(&f);
        check(f.row_count == 1, "a panel starts with one row");
        check(!tiku_trk_find_remove_row(&f),
              "and refuses to remove its last one");
        check(tiku_trk_find_add_row(&f) && f.row_count == 2, "rows are added");
        check(tiku_trk_find_remove_row(&f) && f.row_count == 1,
              "and removed once there is more than one");

        /* The join lives on the row ABOVE the one it reaches, and the fold
         * is strictly left to right. */
        tiku_trk_find_init(&f);
        tiku_trk_find_set_mode(&f, TIKU_QMODE_ATTRIBUTE);
        (void)tiku_trk_find_add_row(&f);
        f.row[0].field = TIKU_QF_UNIT;
        f.row[0].op = TIKU_OP_IS;
        snprintf(f.row[0].value, sizeof f.row[0].value, "bytes");
        f.row[0].join = TIKU_JOIN_OR;
        f.row[1].field = TIKU_QF_NAME;
        f.row[1].op = TIKU_OP_CONTAINS;
        snprintf(f.row[1].value, sizeof f.row[1].value, "led");
        tiku_trk_find_build(&f, &q);
        check(q.term_count == 2 && q.term[1].join == TIKU_JOIN_OR,
              "the join recorded on row 0 is the one that reaches row 1");

        /* Switching into formula shows what the controls were asking. */
        tiku_trk_find_set_mode(&f, TIKU_QMODE_FORMULA);
        check(strstr(f.formula, "[bB][yY][tT][eE][sS]") != NULL &&
              strstr(f.formula, "||") != NULL,
              "formula mode is pre-filled with the composed predicate");
        /* And leaving it does not throw the rows away, unlike the original. */
        tiku_trk_find_set_mode(&f, TIKU_QMODE_ATTRIBUTE);
        check(f.row_count == 2 && strcmp(f.row[1].value, "led") == 0,
              "leaving formula mode keeps the rows it came from");

        tiku_trk_find_init(&f);
        (void)tiku_trk_find_add_place(&f, "sys");
        (void)tiku_trk_find_add_place(&f, "dev");
        check(tiku_trk_find_add_place(&f, "sys") == 0,
              "a place is offered once");
        (void)tiku_trk_find_scope_label(&f, lbl, sizeof lbl);
        check(strcmp(lbl, "Everywhere") == 0, "no scope reads Everywhere");
        f.everywhere = 0;
        f.place_mask = 1u;
        (void)tiku_trk_find_scope_label(&f, lbl, sizeof lbl);
        check(strcmp(lbl, "/sys") == 0, "one place is named");
        f.place_mask = 3u;
        (void)tiku_trk_find_scope_label(&f, lbl, sizeof lbl);
        check(strcmp(lbl, "2 places") == 0, "several are counted");
        tiku_trk_find_build(&f, &q);
        check(q.scope_count == 2, "and both reach the query");

        f.include_trash = 1;
        tiku_trk_find_build(&f, &q);
        check(q.include_trash,
              "Q-036: the panel carries Include Trash into the query");
        {
            tiku_trk_find_t back;

            tiku_trk_find_init(&back);
            check(tiku_trk_find_from_query(&back, &q) == 0 &&
                  back.include_trash,
                  "and restores the choice while reopening a query");
        }

        {
            tiku_rect_t fr = { 0, 0, 560, 400 };
            int x, y, row0, found = 0;

            tiku_trk_find_init(&f);
            for (y = 0; y < fr.h && !found; y++) {
                for (x = 0; x < fr.w; x++) {
                    if (tiku_trk_find_hit(&f, fr, x, y, &row0) ==
                        TIKU_TRK_FIND_HIT_TRASH) {
                        found = 1;
                        (void)tiku_trk_find_click(&f, fr, x, y);
                        break;
                    }
                }
            }
            check(found && f.include_trash,
                  "the visible Include Trash checkbox toggles the option");
        }

        {
            char nm[128];

            tiku_trk_find_init(&f);
            snprintf(f.name_text, sizeof f.name_text, "led");
            (void)tiku_trk_find_default_name(&f, 0, nm, sizeof nm);
            check(strstr(nm, "name contains led") != NULL &&
                  strstr(nm, " - ") != NULL,
                  "a saved query is named for its criteria and its time");
            check(strchr(nm, '/') == NULL,
                  "and carries nothing a path cannot");
        }
    }

    printf("\neach field offers only the operators it can answer\n");
    {
        tiku_trk_find_t f;
        char nm[160];

        tiku_trk_find_init(&f);
        tiku_trk_find_set_mode(&f, TIKU_QMODE_ATTRIBUTE);
        /* Size is measured: it starts on "is" and never offers "contains". */
        f.row[0].field = TIKU_QF_SIZE;
        f.row[0].op = TIKU_OP_CONTAINS;
        {
            tiku_rect_t fr = { 0, 0, 560, 400 };
            int picked, hx = -1, hy = -1, sx, sy, row0;

            /* Ask the panel where its own field control is rather than
             * assuming: a test that guesses coordinates tests the guess. */
            for (sy = 0; sy < fr.h && hy < 0; sy++) {
                for (sx = 0; sx < fr.w; sx++) {
                    if (tiku_trk_find_hit(&f, fr, sx, sy, &row0) ==
                        TIKU_TRK_FIND_HIT_FIELD && row0 == 0) {
                        hx = sx;
                        hy = sy;
                        break;
                    }
                }
            }
            check(hx >= 0, "the panel reports where its field control is");
            (void)tiku_trk_find_click(&f, fr, hx + 2, hy + 2);
            picked = -1;
            {
                int k;
                for (k = 0; k < f.popup.count; k++) {
                    if (strcmp(f.popup.item[k].label, "Size") == 0) {
                        picked = k;
                    }
                }
            }
            check(f.popup.count > 0 && picked >= 0,
                  "clicking it opens a menu that offers Size");
            if (picked >= 0) {
                int y = f.popup_frame.y +
                        tiku_menu_item_top(&f.popup, picked) + 2;
                (void)tiku_trk_find_click(&f, fr, f.popup_frame.x + 4, y);
            }
            check(f.row[0].field == TIKU_QF_SIZE,
                  "picking Size sets the field");
            check(f.row[0].op != TIKU_OP_CONTAINS,
                  "and drops \"contains\", which a size cannot answer");
        }

        /* A saved name describes every row, not just the first. */
        tiku_trk_find_init(&f);
        tiku_trk_find_set_mode(&f, TIKU_QMODE_ATTRIBUTE);
        (void)tiku_trk_find_add_row(&f);
        f.row[0].field = TIKU_QF_UNIT;
        f.row[0].op = TIKU_OP_IS;
        snprintf(f.row[0].value, sizeof f.row[0].value, "bytes");
        f.row[0].join = TIKU_JOIN_OR;
        f.row[1].field = TIKU_QF_NAME;
        f.row[1].op = TIKU_OP_CONTAINS;
        snprintf(f.row[1].value, sizeof f.row[1].value, "led");
        (void)tiku_trk_find_default_name(&f, 0, nm, sizeof nm);
        check(strstr(nm, "bytes") != NULL && strstr(nm, "led") != NULL &&
              strstr(nm, " or ") != NULL,
              "the generated name describes every row and its join");

        /* Entering attribute mode puts the caret on the last row. */
        tiku_trk_find_set_mode(&f, TIKU_QMODE_FORMULA);
        tiku_trk_find_set_mode(&f, TIKU_QMODE_ATTRIBUTE);
        check(f.focus_row == f.row_count - 1,
              "the caret lands on the row still being filled in");
    }

    printf("\nre-opening a query in the panel (Q-026)\n");
    {
        tiku_trk_find_t f;
        tiku_query_t q;

        tiku_trk_find_init(&f);
        (void)tiku_trk_find_add_place(&f, "sys");
        (void)tiku_trk_find_add_place(&f, "dev");
        tiku_trk_find_set_mode(&f, TIKU_QMODE_ATTRIBUTE);
        f.row_count = 2;
        f.row[0].field = TIKU_QF_NAME;
        f.row[0].op = TIKU_OP_CONTAINS;
        snprintf(f.row[0].value, sizeof f.row[0].value, "log");
        f.row[0].join = TIKU_JOIN_OR;
        f.row[1].field = TIKU_QF_SIZE;
        f.row[1].op = TIKU_OP_GREATER;
        snprintf(f.row[1].value, sizeof f.row[1].value, "500");
        f.everywhere = 0;
        f.place_mask = 0x2u;            /* the second place */
        tiku_trk_find_build(&f, &q);
        q.scope_was[0].dev = 77u;
        snprintf(q.scope_was[0].volume,
                 sizeof q.scope_was[0].volume, "board-77");
        {
            tiku_trk_find_t back;
            tiku_query_t rebuilt;

            tiku_trk_find_init(&back);
            (void)tiku_trk_find_add_place(&back, "sys");
            (void)tiku_trk_find_add_place(&back, "dev");
            check(tiku_trk_find_from_query(&back, &q) == 1,
                  "Q-026: a query the panel can express restores whole");
            check(back.mode == TIKU_QMODE_ATTRIBUTE &&
                  back.row_count == 2, "with its mode and its rows");
            check(back.row[0].field == TIKU_QF_NAME &&
                  back.row[1].op == TIKU_OP_GREATER &&
                  strcmp(back.row[1].value, "500") == 0,
                  "each row's field, operator and value");
            check(back.row[0].join == TIKU_JOIN_OR,
                  "and the join, which belongs to the row BEFORE the one "
                  "it joins -- the asymmetry the builder writes out");
            check(!back.everywhere && back.place_mask == 0x2u,
                  "the scope ticks the place it named, not the first one");
            tiku_trk_find_build(&back, &rebuilt);
            check(rebuilt.scope_was[0].dev == 77u &&
                  strcmp(rebuilt.scope_was[0].volume, "board-77") == 0,
                  "Q-027: editing and rebuilding preserves the scope's "
                  "volume identity");
        }
        {   /* A scope this panel cannot offer. */
            tiku_trk_find_t back;

            tiku_trk_find_init(&back);
            (void)tiku_trk_find_add_place(&back, "sys");
            check(tiku_trk_find_from_query(&back, &q) == 0,
                  "a query naming somewhere the panel does not offer is "
                  "reported rather than silently narrowed");
            check(back.everywhere,
                  "and falls back to everywhere: a search that quietly "
                  "lost its scope searches somewhere else");
        }
        {   /* By name: what was TYPED comes back. */
            tiku_query_t byname;
            tiku_trk_find_t back;

            tiku_query_init(&byname);
            tiku_query_set_name(&byname, "report");
            tiku_trk_find_init(&back);
            check(tiku_trk_find_from_query(&back, &byname) == 1 &&
                  back.mode == TIKU_QMODE_NAME &&
                  strcmp(back.name_text, "report") == 0, "Q-026: a by-name "
                  "query comes back with the text in the field, not as a "
                  "term the user never typed as one");
        }
    }

    printf("\nwhat a type admits (Q-004)\n");
    check(tiku_model_type_is("text/plain", "text/plain"),
          "a type is itself");
    check(tiku_model_type_is("TEXT/Plain", "text/plain"),
          "case-folded, as every other type lookup here is");
    check(tiku_model_type_is("text/x-source-code", "text/"),
          "a supertype admits what is under it");
    check(!tiku_model_type_is("textual/plain", "text"),
          "a want with no slash is not a supertype: without the slash "
          "\"text\" would claim \"textual/plain\", and a menu entry that "
          "quietly widened would be worse than one that found nothing");
    check(!tiku_model_type_is("text/plain", "text/x"),
          "and a longer want is not admitted by a shorter type");
    check(tiku_model_type_is("anything", ""),
          "asking for no type admits everything -- that is what the "
          "menu's first item means");
    check(!tiku_model_type_is("", "text/"),
          "while an entry with no type of its own is admitted by nothing");

    printf("\nnarrowing to a type (Q-004)\n");
    {
        tiku_trk_find_t f;
        tiku_query_t q;
        tiku_model_t dir, file, src;

        memset(&dir, 0, sizeof dir);
        dir.kind = TIKU_KIND_DIRECTORY;
        snprintf(dir.name, sizeof dir.name, "papers");
        snprintf(dir.type, sizeof dir.type,
                 "application/x-vnd.Be-directory");
        memset(&file, 0, sizeof file);
        file.kind = TIKU_KIND_FILE;
        snprintf(file.name, sizeof file.name, "paper.bin");
        snprintf(file.type, sizeof file.type, "application/octet-stream");
        memset(&src, 0, sizeof src);
        src.kind = TIKU_KIND_FILE;
        snprintf(src.name, sizeof src.name, "paper.c");
        snprintf(src.type, sizeof src.type, "text/x-source-code");

        tiku_trk_find_init(&f);
        tiku_trk_find_set_type(&f, "application/x-vnd.be-directory", NULL);
        tiku_trk_find_build(&f, &q);
        check(tiku_query_matches(&q, &dir, NULL, NULL),
              "Q-004: a type is ANDed onto what the rows ask");
        check(!tiku_query_matches(&q, &file, NULL, NULL),
              "so anything of another type is out, whatever its name says");

        tiku_trk_find_set_type(&f, "text/", NULL);
        tiku_trk_find_build(&f, &q);
        check(tiku_query_matches(&q, &src, NULL, NULL),
              "a SUPERTYPE admits its members: picking text finds "
              "text/x-source-code, which is what makes a table of a dozen "
              "entries able to answer for types it has never heard of");
        check(!tiku_query_matches(&q, &file, NULL, NULL),
              "and still excludes what is not under it");

        {   /* A prefix that is not a supertype must not admit. */
            tiku_model_t near;

            memset(&near, 0, sizeof near);
            near.kind = TIKU_KIND_FILE;
            snprintf(near.name, sizeof near.name, "trap");
            snprintf(near.type, sizeof near.type, "textual/plain");
            check(!tiku_query_matches(&q, &near, NULL, NULL),
                  "a type that merely STARTS the same is not under it: "
                  "the supertype ends at the slash, or \"text\" would "
                  "claim \"textual/plain\"");
        }

        tiku_trk_find_set_type(&f, "", NULL);
        tiku_trk_find_build(&f, &q);
        check(tiku_query_matches(&q, &file, NULL, NULL) &&
              tiku_query_matches(&q, &dir, NULL, NULL) &&
              q.term_count == 1,
              "asking for no type admits everything again, and adds no "
              "term to say so");

        {   /* A formula asks for itself; nothing is ANDed onto it. */
            tiku_trk_find_t g;
            tiku_query_t fq;

            tiku_trk_find_init(&g);
            tiku_trk_find_set_type(&g, "text/", NULL);
            tiku_trk_find_set_mode(&g, TIKU_QMODE_FORMULA);
            snprintf(g.formula, sizeof g.formula,
                     "(name==\"*[fF][iI][lL][eE]*\")");
            tiku_trk_find_build(&g, &fq);
            {
                int i, has_type = 0;

                for (i = 0; i < fq.term_count; i++) {
                    if (fq.term[i].field == TIKU_QF_TYPE) {
                        has_type = 1;
                    }
                }
                check(fq.type[0] == '\0' && !has_type,
                      "Q-012: in "
                      "formula mode the type is not ANDed on -- the "
                      "control is hidden there, and a hidden one that "
                      "still narrowed the search would make the formula "
                      "on screen a lie");
            }
        }
    }

    printf("\nthe type menu itself (Q-012, Q-015, Q-016)\n");
    {
        tiku_trk_find_t f;
        tiku_store_t *st2 = tiku_store_memory_open();
        const char *menu[24];
        int n;

        tiku_trk_find_init(&f);
        check(tiku_trk_find_type_shown(&f), "Q-012: the control shows in "
              "by-name mode");
        tiku_trk_find_set_mode(&f, TIKU_QMODE_ATTRIBUTE);
        check(tiku_trk_find_type_shown(&f), "and in by-attribute mode");
        tiku_trk_find_set_mode(&f, TIKU_QMODE_FORMULA);
        check(!tiku_trk_find_type_shown(&f), "Q-012: and is hidden in "
              "formula mode, where the formula IS the question and a "
              "control quietly ANDing a type onto it would make the text "
              "on screen a lie");
        tiku_trk_find_set_mode(&f, TIKU_QMODE_NAME);

        check(strcmp(tiku_trk_find_type_label(&f),
                     "All files and folders") == 0,
              "Q-015: it starts on the fixed first item, which is not one "
              "of the types");
        n = tiku_trk_find_type_menu(NULL, menu, 24);
        check(n > 0, "Q-015: the menu offers what the port can recognise");
        {
            int i, seen = 0;

            for (i = 0; i < n; i++) {
                if (strcmp(menu[i], "text/") == 0) { seen = 1; }
            }
            check(seen, "including the supertypes, which are what make it "
                  "useful on types the table has never heard of");
        }

        tiku_trk_find_set_type(&f, "image/", st2);
        check(strcmp(tiku_trk_find_type_label(&f), "Image") == 0,
              "Q-015: a chosen type is named by what it MEANS");
        n = tiku_trk_find_type_menu(st2, menu, 24);
        check(n > 0 && strcmp(menu[0], "image/") == 0,
              "Q-016: and picking one promotes it to the head of the menu");
        tiku_trk_find_set_type(&f, "audio/", st2);
        n = tiku_trk_find_type_menu(st2, menu, 24);
        check(strcmp(menu[0], "audio/") == 0 &&
              strcmp(menu[1], "image/") == 0,
              "the most recently picked first, the one before it next");
        tiku_trk_find_set_type(&f, "image/", st2);
        n = tiku_trk_find_type_menu(st2, menu, 24);
        check(strcmp(menu[0], "image/") == 0 &&
              strcmp(menu[1], "audio/") == 0,
              "and picking one again MOVES it rather than listing it "
              "twice -- the head of the menu is the types this user works "
              "in, not a log of every pick");
        {
            int i, dupes = 0;

            for (i = 1; i < n; i++) {
                int k;

                for (k = 0; k < i; k++) {
                    if (strcmp(menu[i], menu[k]) == 0) { dupes++; }
                }
            }
            check(dupes == 0, "Q-015: and a promoted type is not offered "
                  "a second time further down");
        }
        {   /* The panel keeps its rows when the type changes. */
            tiku_trk_find_t g;

            tiku_trk_find_init(&g);
            tiku_trk_find_set_mode(&g, TIKU_QMODE_ATTRIBUTE);
            (void)tiku_trk_find_add_row(&g);
            snprintf(g.row[0].value, sizeof g.row[0].value, "half typed");
            tiku_trk_find_set_type(&g, "text/", NULL);
            check(g.row_count == 2 &&
                  strcmp(g.row[0].value, "half typed") == 0,
                  "Q-013: choosing a type does NOT throw the rows away -- "
                  "this panel already refuses that loss when the mode "
                  "changes, and the same loss with a different trigger is "
                  "still a half-built query lost to a mis-click");
        }
        tiku_store_free(st2);
    }

    printf("\ntype hierarchy and declared query fields (Q-007, Q-015)\n");
    {
        tiku_trk_types_t types;
        tiku_trk_find_t f;
        tiku_rect_t fr = { 0, 0, 560, 420 };
        int x, y, row, type_x = -1, type_y = -1, group = -1, item = -1;

        tiku_trk_types_init(&types);
        (void)tiku_trk_types_add(&types, "sensor/", "Sensor", "device",
                                 "value,cap");
        (void)tiku_trk_types_add(&types, "sensor/temp", "Temperature",
                                 "device", "kind,location");
        (void)tiku_trk_types_add(&types, "empty/", "Empty", "file",
                                 "size");
        tiku_trk_find_init(&f);
        tiku_trk_find_set_types(&f, &types);
        for (y = 0; y < fr.h && type_y < 0; y++) {
            for (x = 0; x < fr.w; x++) {
                if (tiku_trk_find_hit(&f, fr, x, y, &row) ==
                        TIKU_TRK_FIND_HIT_TYPE) {
                    type_x = x; type_y = y; break;
                }
            }
        }
        (void)tiku_trk_find_click(&f, fr, type_x + 2, type_y + 2);
        {
            int empty = 0;

            for (x = 0; x < f.popup.count; x++) {
                empty |= strcmp(f.popup.item[x].label, "Empty") == 0;
            }
            check(!empty, "Q-015: a supertype with no installed members "
                  "is omitted instead of opening an empty submenu");
        }
        for (x = 0; x < f.popup.count; x++) {
            if (strcmp(f.popup.item[x].label, "Sensor") == 0 &&
                f.popup.item[x].submenu != NULL) {
                group = x;
            }
        }
        check(group >= 0, "Q-015: a supertype with members is a submenu, "
              "not another flat type");
        if (group >= 0) {
            y = f.popup_frame.y +
                tiku_menu_item_top(&f.popup, group) + 2;
            (void)tiku_trk_find_click(&f, fr, f.popup_frame.x + 4, y);
            for (x = 0; f.popup_sub != NULL &&
                        x < f.popup_sub->count; x++) {
                if (strcmp(f.popup_sub->item[x].label, "Temperature") == 0) {
                    item = x;
                }
            }
        }
        check(item >= 0, "Q-015: the subtype is nested under its supertype");
        if (item >= 0) {
            y = f.popup_sub_frame.y +
                tiku_menu_item_top(f.popup_sub, item) + 2;
            (void)tiku_trk_find_click(&f, fr, f.popup_sub_frame.x + 4, y);
        }
        check(strcmp(f.type, "sensor/temp") == 0,
              "Q-015: a nested subtype remains selectable");
        check(strcmp(tiku_trk_find_type_label(&f), "Temperature") == 0,
              "Q-015: a nested choice is labelled by its description");

        tiku_trk_find_set_mode(&f, TIKU_QMODE_ATTRIBUTE);
        for (y = 0; y < fr.h; y++) {
            int found = 0;

            for (x = 0; x < fr.w; x++) {
                if (tiku_trk_find_hit(&f, fr, x, y, &row) ==
                        TIKU_TRK_FIND_HIT_FIELD) {
                    (void)tiku_trk_find_click(&f, fr, x + 2, y + 2);
                    found = 1;
                    break;
                }
            }
            if (found) { break; }
        }
        {
            int has_value = 0, has_cap = 0, has_kind = 0, has_path = 0;

            for (x = 0; x < f.popup.count; x++) {
                has_value |= strcmp(f.popup.item[x].label, "Value") == 0;
                has_cap |= strcmp(f.popup.item[x].label, "Capability") == 0;
                has_kind |= strcmp(f.popup.item[x].label, "Kind") == 0;
                has_path |= strcmp(f.popup.item[x].label, "Path") == 0;
            }
            check(has_value && has_cap, "Q-007: the field menu includes "
                  "the supertype's declared fields");
            check(has_kind && has_path, "Q-007: and extends them with the "
                  "selected subtype's own fields");
        }
    }

    printf("\ntemplates, and the sweep of what nobody named "
           "(Q-031, Q-032, Q-037, AW-090)\n");
    {
        tiku_store_t *st3 = tiku_store_memory_open();
        tiku_query_t q;
        char names[8][128];
        const int64_t now = 2000000000;
        const int64_t week = TIKU_QUERY_STALE_SEC;
        int n;

        /* Four records: a template, a query the user named, a stale
         * by-product, and a by-product from this morning. */
        tiku_query_init(&q);
        (void)tiku_query_add_term(&q, TIKU_QF_NAME,
                                      TIKU_OP_CONTAINS, "report",
                                      TIKU_JOIN_AND);
        q.kind = TIKU_QREC_TEMPLATE;
        q.temporary = 1;            /* templates are never by-products   */
        q.saved_at = now - 4 * week;
        (void)tiku_query_save(&q, "reports template", st3);
        q.kind = TIKU_QREC_QUERY;
        q.temporary = 0;
        q.saved_at = now - 4 * week;
        (void)tiku_query_save(&q, "the one I named", st3);
        q.temporary = 1;
        q.saved_at = now - 2 * week;
        (void)tiku_query_save(&q, "find #1", st3);
        q.saved_at = now - 60;
        (void)tiku_query_save(&q, "find #2", st3);

        n = tiku_query_list_kind(st3, TIKU_QREC_TEMPLATE, names, 8);
        check(n == 1 && strcmp(names[0], "reports template") == 0,
              "Q-031: the templates menu is the saved records of that "
              "kind, and only those");
        n = tiku_query_list_kind(st3, TIKU_QREC_QUERY, names, 8);
        check(n == 3 && strcmp(names[0], "find #2") == 0,
              "Q-032: and the recent-queries menu is the others, newest "
              "first -- the order the index is already kept in");

        check(tiku_query_sweep(st3, now, 60, NULL, 0) == -1,
              "AW-090: the sweep does not run in the first half hour: a "
              "machine that has just come up is busy, and this is the "
              "least urgent work there is");
        {
            const char *open[] = { "find #1" };

            check(tiku_query_sweep(st3, now, 3600, open, 1) == 0,
                  "Q-037: a stale by-product that a window is SHOWING is "
                  "left alone -- deleting the record under an open window "
                  "would leave the window unable to say what it is");
        }
        check(tiku_query_sweep(st3, now, 3600, NULL, 0) == 1,
              "Q-037: with nothing showing it, the week-old by-product "
              "goes");
        check(tiku_query_load(&q, "find #1", st3) != 0 &&
              tiku_query_load(&q, "find #2", st3) == 0 &&
              tiku_query_load(&q, "the one I named", st3) == 0 &&
              tiku_query_load(&q, "reports template", st3) == 0,
              "AW-090: and only that one -- this morning's search, the "
              "query with a name and the template are all still there");
        tiku_store_free(st3);
    }

    printf("\ntemplate run and edit identity (Q-029)\n");
    {
        tiku_store_t *st4 = tiku_store_memory_open();
        tiku_trk_find_t f;
        tiku_query_t before, after;

        tiku_trk_find_init(&f);
        tiku_trk_find_set_store(&f, st4);
        snprintf(f.name_text, sizeof f.name_text, "original");
        check(tiku_trk_find_save_template(&f, "daily", 100) == 0,
              "Q-029: the template fixture is saved");
        snprintf(f.name_text, sizeof f.name_text, "changed");
        check(tiku_trk_find_load_named(&f, "daily") >= 0,
              "opening a template fills the panel");
        snprintf(f.name_text, sizeof f.name_text, "run copy");
        check(tiku_query_load(&before, "daily", st4) == 0 &&
              strcmp(before.term[0].value, "original") == 0,
              "running from it has not overwritten the template");
        {
            tiku_rect_t fr = { 0, 0, 560, 400 };
            int x, y, row0, found = 0, edit = -1;

            for (y = 0; y < fr.h && !found; y++) {
                for (x = 0; x < fr.w; x++) {
                    if (tiku_trk_find_hit(&f, fr, x, y, &row0) ==
                        TIKU_TRK_FIND_HIT_QUERIES) {
                        found = 1;
                        (void)tiku_trk_find_click(&f, fr, x, y);
                        break;
                    }
                }
            }
            for (x = 0; x < f.popup.count; x++) {
                if (strcmp(f.popup.item[x].label,
                           "Edit loaded template") == 0) {
                    edit = x;
                }
            }
            if (edit >= 0) {
                y = f.popup_frame.y +
                    tiku_menu_item_top(&f.popup, edit) + 2;
                (void)tiku_trk_find_click(&f, fr,
                                          f.popup_frame.x + 4, y);
            }
            check(found && edit >= 0 &&
                  tiku_trk_find_editing_template(&f),
                  "Edit Query template is an explicit menu action that "
                  "attaches the panel for replacement");
            check(tiku_trk_find_primary_is_save(&f),
                  "Q-022: template-editing mode turns the primary action "
                  "into Save-only");
        }
        snprintf(f.name_text, sizeof f.name_text, "replacement");
        check(tiku_trk_find_save_current(&f, 200) == 0,
              "editing in place saves back to the attached record");
        check(tiku_query_load(&after, "daily", st4) == 0 &&
              strcmp(after.term[0].value, "replacement") == 0 &&
              after.kind == TIKU_QREC_TEMPLATE,
              "editing in place writes over the same template record");
        tiku_store_free(st4);
    }

    printf("\nthe default template (Q-030)\n");
    {
        tiku_store_t *st5 = tiku_store_memory_open();
        tiku_trk_find_t first, reopened;
        tiku_query_t saved;
        tiku_rect_t fr = { 0, 0, 560, 400 };
        int row = -1, x, y, save_hit = 0;

        tiku_trk_find_init(&first);
        tiku_trk_find_set_store(&first, st5);
        snprintf(first.name_text, sizeof first.name_text, "starter");
        check(tiku_trk_find_open_default(&first, 300) >= 0 &&
              tiku_query_load(&saved, "default", st5) == 0 &&
              saved.kind == TIKU_QREC_TEMPLATE,
              "Q-030: first use creates the default template");
        check(!tiku_trk_find_can_save_current(&first),
              "with no explicit record loaded, Save is disabled");

        snprintf(saved.term[0].value, sizeof saved.term[0].value,
                 "remembered default");
        check(tiku_query_save(&saved, "default", st5) == 0,
              "the default fixture can be changed");
        tiku_trk_find_init(&reopened);
        tiku_trk_find_set_store(&reopened, st5);
        check(tiku_trk_find_open_default(&reopened, 400) >= 0 &&
              strcmp(reopened.name_text, "remembered default") == 0,
              "opening Find again starts from the stored default");
        for (y = 0; y < fr.h; y++) {
            for (x = 0; x < fr.w; x++) {
                save_hit |= (tiku_trk_find_hit(&reopened, fr, x, y, &row) ==
                             TIKU_TRK_FIND_HIT_SAVE);
            }
        }
        check(save_hit,
              "Save stays reachable with nothing attached: it asks WHERE "
              "instead of saving into nowhere (AW-052)");

        first.include_trash = 1;
        check(tiku_trk_find_load_named(&first, "default") >= 0 &&
              !first.include_trash &&
              tiku_trk_find_load_named(&first, "default") >= 0 &&
              !first.include_trash,
              "Q-053: opening a query restores Include Trash and never "
              "toggles it as a fall-through side effect");
        tiku_store_free(st5);
    }

    printf("\ndepth-first walk order (PVN-069)\n");
    {
        tiku_backend_t backend = { &DEPTH_WALK_OPS, "", NULL };
        tiku_query_t q;
        tiku_qrun_t *run;

        tiku_query_init(&q);
        tiku_query_set_name(&q, "hit");
        (void)tiku_query_add_scope(&q, "/scope");
        run = tiku_query_start(&backend, &q);
        (void)tiku_query_step(run, 2);   /* lists /scope: queues a, b */
        (void)tiku_query_step(run, 2);   /* pops one directory        */
        check(run != NULL && tiku_query_count(run) == 1 &&
              strcmp(tiku_query_at(run, 0)->name, "b-hit") == 0,
              "the walk descends into the newest directory first -- a "
              "STACK, the source's depth-first order, not a queue");
        while (run != NULL && !tiku_query_done(run)) {
            (void)tiku_query_step(run, 4);
        }
        check(run != NULL && tiku_query_count(run) == 2,
              "and still visits everything in the end");
        tiku_query_stop(run);
    }

    printf("\nsymlinks are entries, not places to descend (PVN-069)\n");
    {
        tiku_backend_t backend = { &CYCLE_WALK_OPS, "", NULL };
        tiku_query_t q;
        tiku_qrun_t *run;
        int guard = 0;

        tiku_query_init(&q);
        tiku_query_set_name(&q, "hit");
        (void)tiku_query_add_scope(&q, "/scope");
        run = tiku_query_start(&backend, &q);
        while (run != NULL && !tiku_query_done(run) && guard++ < 64) {
            (void)tiku_query_step(run, 4);
        }
        check(run != NULL && tiku_query_done(run),
              "a namespace with a symlink cycle still finishes: only real "
              "directories are descended, as the source walks only "
              "B_DIRECTORY_NODE entries");
        check(run != NULL && tiku_query_count(run) == 1,
              "and the link itself was still SEEN as an entry");
        tiku_query_stop(run);
    }


    printf("\n%s\n", fails ? "FAILURES" : "every query rule holds");
    return fails ? 1 : 0;
}
