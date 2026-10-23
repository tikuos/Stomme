/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_script_api.h - the scripting interface onto a pose view.
 *
 * Tracker's scripting suite, a BMessage suite there, as verb, property and
 * specifier here: which specifiers each property takes, what an
 * out-of-range index does, and every failure reported in the reply.  A
 * transport can be a message, a socket, or the shell.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SCRIPT_API_H_
#define TIKU_TRK_SCRIPT_API_H_

#include "tiku_trk_poseview.h"

/** @brief The suite this speaks, as GetSupportedSuites would report it. */
#define TIKU_TRK_SUITE "suite/vnd.Tiku-TrackerPoses"

/** @brief The verbs. */
typedef enum {
    TIKU_TRK_VERB_GET = 0,
    TIKU_TRK_VERB_SET,
    TIKU_TRK_VERB_COUNT,
    TIKU_TRK_VERB_CREATE,
    TIKU_TRK_VERB_DELETE,
    TIKU_TRK_VERB_EXECUTE
} tiku_trk_verb_t;

/** @brief The properties. */
typedef enum {
    TIKU_TRK_PROP_PATH = 0,
    TIKU_TRK_PROP_ENTRY,
    TIKU_TRK_PROP_SELECTION
} tiku_trk_prop_t;

/** @brief How the request names what it means. */
typedef enum {
    TIKU_TRK_SPEC_DIRECT = 0,   /* the property as a whole              */
    TIKU_TRK_SPEC_INDEX,        /* by position in the current order     */
    TIKU_TRK_SPEC_RANGE,        /* from index a to index b, b exclusive */
    TIKU_TRK_SPEC_PATH,         /* by path                              */
    TIKU_TRK_SPEC_PREVIOUS,
    TIKU_TRK_SPEC_NEXT
} tiku_trk_spec_t;

/** @brief Errors, reported in the reply (PVK-054). */
#define TIKU_TRK_ERR_OK              0
#define TIKU_TRK_ERR_BAD_INDEX      -1
#define TIKU_TRK_ERR_NOT_FOUND      -2
#define TIKU_TRK_ERR_NOT_DIRECTORY  -3
#define TIKU_TRK_ERR_UNHANDLED      -4  /* not this suite's; the caller
                                         * may try something else        */

/** @brief One request. */
typedef struct {
    tiku_trk_verb_t verb;
    tiku_trk_prop_t prop;
    tiku_trk_spec_t spec;
    int             a, b;                       /* index or range        */
    const char     *path;                       /* SPEC_PATH, or the walk
                                                 * start for PREVIOUS and
                                                 * NEXT                  */
    const char    **paths;                      /* the npath paths named */
    int             npath;
    const char     *pathv[8];                   /* what parse points
                                                 * paths at              */
} tiku_trk_req_t;

/** @brief What came back. */
typedef struct {
    int   error;                                /* TIKU_TRK_ERR_*        */
    int   count;                                /* COUNT's answer, or how
                                                 * many a set or create
                                                 * selected             */
    char  result[64][TIKU_PATH_MAX];            /* the paths answered    */
    int   nresult;                              /* how many of result[]  */
} tiku_trk_rep_t;

/**
 * @brief Parse "get Entry 3" and the like.
 *
 * @param store Eight slots for the paths the line names.
 * @return 0 on success, -1 for a line that is not a request.
 */
int tiku_trk_script_parse(const char *line, tiku_trk_req_t *req,
                          char (*store)[TIKU_PATH_MAX]);

/** @brief Run one request against @p v; a failure is set in @p rep's error. */
void tiku_trk_script_run(tiku_trk_poseview_t *v, const tiku_trk_req_t *req,
                         tiku_trk_rep_t *rep);

/** @brief Line @p i of the suite's properties, as a discovery request lists
 *         them; NULL past the last. */
const char *tiku_trk_script_suite(int i);

#endif /* TIKU_TRK_SCRIPT_API_H_ */
