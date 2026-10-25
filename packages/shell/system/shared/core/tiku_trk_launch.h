/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_launch.h - which application opens a document, and running it.
 *
 * A registry of applications, each naming the types it reads, answers the
 * three questions Tracker asks its MIME database: which applications claim
 * a type, which one is preferred, and how well each matches.  This file
 * ranks the answers, takes their union across a selection, runs the chosen
 * application and collects it when it ends.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#ifndef TIKU_TRK_LAUNCH_H_
#define TIKU_TRK_LAUNCH_H_

#include "tiku_model.h"
#include <sys/types.h>

struct tiku_store;
struct tiku_msg;
struct tiku_trk_app;

/**
 * @brief Launch @p app's module with @p message, handed over through an
 *        inherited descriptor named by --message-fd.
 *
 * The message is flattened into an unlinked temporary file of at most
 * 32 KiB.
 *
 * @return a tiku_trk_launch_err_t: TIKU_TRK_LAUNCH_NOTHING when @p app has
 *         no module, TIKU_TRK_LAUNCH_FAILED when the message cannot be
 *         staged or the fork fails.
 */
int tiku_trk_launch_message(const struct tiku_trk_app *app,
                            const struct tiku_msg *message);

/** @brief The registry's limits: applications, types each claims, and the
 *         longest name and command. */
#define TIKU_TRK_APP_MAX        64
#define TIKU_TRK_APP_TYPES_MAX   TIKU_SHELL_APP_TYPES_MAX
#define TIKU_TRK_APP_NAME_MAX   48
#define TIKU_TRK_APP_CMD_MAX   192

/**
 * @brief Why an application matched, which is also how well (MA-017).
 *
 * The order is the ranking: an exact type beats a supertype match, which
 * beats an application that takes anything at all.  A preferred handler is
 * one of these that the user has chosen; tiku_trk_cand_t carries it beside
 * the relation.
 */
typedef enum {
    TIKU_TRK_REL_NONE = 0,
    TIKU_TRK_REL_SUPERHANDLER,   /* claims everything                     */
    TIKU_TRK_REL_SUPERTYPE,      /* claims "text/", document is "text/c"  */
    TIKU_TRK_REL_EXACT
} tiku_trk_relation_t;

/** @brief One application the registry knows about. */
typedef struct tiku_trk_app {
    char category[32],startup[96],help[128]; /* loaded manifest metadata */
    char id[64];                         /* manifest's stable identity */
    char name[TIKU_TRK_APP_NAME_MAX];    /* as the Open With menu says it */
    char cmd[TIKU_TRK_APP_CMD_MAX];      /* argv[0], run with the paths   */
    char type[TIKU_TRK_APP_TYPES_MAX][TIKU_TYPE_MAX]; /* the types claimed */
    int  ntype;                          /* how many of those            */
    int  takes_anything;                 /* a superhandler                */
    /* Where the application itself lives, when it is a file in the
     * namespace.  Its own path is dropped from the documents handed to it,
     * so an application never opens itself (AW-038). */
    char path[TIKU_PATH_MAX];
    int  is_tracker;                     /* this Tracker, not a launch    */
    char icon[64];                       /* manifest artwork identity     */
    char module[TIKU_PATH_MAX];          /* runner's literal first arg    */
} tiku_trk_app_t;

/** @brief One candidate, with what made it one. */
typedef struct {
    const tiku_trk_app_t *app;
    tiku_trk_relation_t   rel;
    int                   preferred;     /* the user's choice for the type */
} tiku_trk_cand_t;

/** @brief The application registry. */
typedef struct tiku_trk_apps {
    tiku_trk_app_t app[TIKU_TRK_APP_MAX];    /* every application registered */
    int            count;                    /* how many slots are used      */
} tiku_trk_apps_t;

/**
 * @brief Order for a candidate list: the user's choice, then the better
 *        claim, then a shipped document application (one with a module)
 *        for an equal claim, then the name.
 *
 * @return non-zero when @p a belongs above @p b.
 */
int tiku_trk_launch_cand_before(const tiku_trk_cand_t *a,
                                const tiku_trk_cand_t *b);

/** @brief Forget every application. */
void tiku_trk_launch_clear(tiku_trk_apps_t *r);

/**
 * @brief Register a document application, launched as
 *        `runner module file`.
 *
 * @return its index, the existing entry's index when @p module is already
 *         registered, or -1.
 */
int tiku_trk_launch_add_document(tiku_trk_apps_t *r, const char *name,
    const char *runner, const char *module, const char *types);

/**
 * @brief Add one application.  @return its index, or -1.
 *
 * @param types Comma-separated; "*" means it takes anything.
 */
int tiku_trk_launch_add(tiku_trk_apps_t *r, const char *name,
                        const char *cmd, const char *types);

/**
 * @brief Read a registry file: one application per line.
 *
 * `name<TAB>command<TAB>type,type,...`, '#' comments, blank lines ignored
 * (AW-013).  An entry whose absolute command no longer exists is skipped
 * (AW-041).
 *
 * @return how many were added, or -1 when the file cannot be read.
 */
int tiku_trk_launch_load(tiku_trk_apps_t *r, const char *path);

/**
 * @brief How @p app relates to @p type (MA-017).
 *
 * Wildcard-aware: a claim ending in '/' is a supertype claim, and "*"
 * claims everything.
 */
tiku_trk_relation_t tiku_trk_launch_relation(const tiku_trk_app_t *app,
                                             const char *type);

/**
 * @brief The applications that can open @p types, best first (AW-040).
 *
 * The union over the selection: an application appears when it can open
 * any of the types.  Sorted as tiku_trk_launch_cand_before() orders them
 * (AW-042).  The registry holds applications only (AW-041).
 *
 * @param store Consulted per type for the user's choice; may be NULL.
 * @return how many candidates were written.
 */
int tiku_trk_launch_candidates(const tiku_trk_apps_t *r,
                               const char *const *types, int ntypes,
                               struct tiku_store *store,
                               tiku_trk_cand_t *out, int max);

/**
 * @brief Whether every one of @p types agrees on one preferred handler.
 *
 * The Open With list preselects a handler only when the selection agrees
 * (AW-043).
 *
 * @return the application, or NULL.
 */
const tiku_trk_app_t *tiku_trk_launch_agreed(const tiku_trk_apps_t *r,
                                             const char *const *types,
                                             int ntypes,
                                             struct tiku_store *store);

/**
 * @brief Remember the application named @p app_name as the handler for
 *        @p type (AW-044).
 *
 * @return 0, or -1 when it could not be stored.
 */
int tiku_trk_launch_set_preferred(struct tiku_store *store,
                                  const char *type, const char *app_name);

/** @brief The remembered handler's name for @p type, or "". */
const char *tiku_trk_launch_preferred(struct tiku_store *store,
                                      const char *type, char *buf,
                                      size_t max);

/** @brief Read the last saved preferred handler without filesystem I/O.
 *  @return 0 for a copied handler or no preference; -1 when unavailable. */
int tiku_trk_launch_preferred_persisted(struct tiku_store *store, const char *type,
    char *buffer, size_t max);


/** @brief The word a Relation column shows for @p rel (AW-042, MA-081). */
const char *tiku_trk_launch_relation_label(tiku_trk_relation_t rel);

/** @brief What went wrong, so the diagnosis can be specific (AW-039). */
typedef enum {
    TIKU_TRK_LAUNCH_OK = 0,
    TIKU_TRK_LAUNCH_NO_APP,      /* nothing claims the type               */
    TIKU_TRK_LAUNCH_NOTHING,     /* no documents to open                  */
    TIKU_TRK_LAUNCH_IS_TRACKER,  /* handled here instead (AW-035)         */
    TIKU_TRK_LAUNCH_FAILED,      /* the command could not be run          */
} tiku_trk_launch_err_t;

/**
 * @brief Make @p m into the application it already is.
 *
 * An executable file opens itself; the registry is not asked about its
 * type.
 *
 * @return 1 when @p m is a runnable file and @p out was filled.
 */
int tiku_trk_launch_self(const tiku_model_t *m, tiku_trk_app_t *out);

/**
 * @brief Run @p app over @p paths (AW-002, AW-038).
 *
 * The application's own file is dropped from what it is handed, so it never
 * opens itself.  The child starts in the user's home directory.  The parent
 * does not wait for it.
 *
 * @return TIKU_TRK_LAUNCH_OK once the child is forked, or why not.  A
 *         command that then fails to run shows when it is reaped.
 * @note An application with a module takes one document per launch; more
 *       than one is refused as TIKU_TRK_LAUNCH_NOTHING.
 */
tiku_trk_launch_err_t tiku_trk_launch_run(const tiku_trk_app_t *app,
                                          const char *const *paths, int n);

/** @brief The sentence a failed launch should say (AW-039). */
const char *tiku_trk_launch_reason(tiku_trk_launch_err_t err);

/**
 * @brief Remember that @p pid is @p name, until it is collected.
 *
 * A program that ends is reaped by its pid, so no other code's child is
 * collected, and its name goes into the trail with how it ended.
 */
void tiku_trk_launch_note(pid_t pid, const char *name);
/** @brief Mark @p pid's coming SIGKILL as a requested Force Quit, so its end
 *         is not reported as a crash. */
void tiku_trk_launch_expect_stop(pid_t pid);

/**
 * @brief How many launched programs have not yet been collected.
 *
 * A caller about to wait asks it: while a child it started runs, the next
 * event may come from that process.
 */
int tiku_trk_launch_running(void);

/** @brief How a launched program ended. */
typedef struct {
    char name[64];
    long pid;                   /* the exact child whose status was reaped */
    char report[512];           /* where its exit report went, or empty */
    int  signal;                /* what killed it, or 0 */
    int  status;                /* what it exited with, when it exited */
    int  expected;              /* a requested Force Quit, not a crash */
} tiku_trk_death_t;

/**
 * @brief Collect any launched program that has ended.
 *
 * @param out up to @p max of them, or NULL when only the trail is
 *            wanted; then every ended program is collected.
 * @return how many, each written into the trail with how it went.
 */
int tiku_trk_launch_reap(tiku_trk_death_t *out, int max);

/** @brief Copied child identity and retained exit outcome. */
typedef struct {
    unsigned long long generation;
    long pid;
    char name[64], app_id[64], report[512];
    int running, signal, status, expected;
} tiku_trk_launch_observation_t;
/** @brief Copy one current child or one of the latest 64 exits; -1 out of range.
 *  @note Loop thread only; index zero begins a current native snapshot. */
int tiku_trk_launch_observe(int index,tiku_trk_launch_observation_t *);
/** @brief Associate a tracked child with its loaded manifest identity. */
void tiku_trk_launch_identify(pid_t pid,const char *app_id);

#endif /* TIKU_TRK_LAUNCH_H_ */
