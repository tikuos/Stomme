/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_posix_changes.c - the posix backend over a platform that records the
 * directory each change touches: a change under that directory for each
 * record, a partial answer when records were lost, and ids turned into
 * paths by the platform.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_model.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test) do { if(!(test)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#test);exit(1);} } while(0)

tiku_backend_t *tiku_backend_posix_open(void);

/* The platform: records waiting from position 0 on, and a lost flag. */
static uint64_t records[8], first_next;
static int waiting, lost, asks;

int tiku_posix_changes(uint64_t *next, uint64_t *dirs, int max, int *complete)
{
    int n = 0;

    if (asks++ == 0) first_next = *next;
    if (*next == UINT64_MAX) *next = 0;
    while ((int)*next < waiting && n < max) dirs[n++] = records[(*next)++];
    *complete = !lost;
    return n;
}

int tiku_posix_path_of(uint64_t id, char *out, size_t max)
{
    if (id == 7u) {
        snprintf(out, max, "/home/Documents");
        return 0;
    }
    return -1;
}

int main(void)
{
    tiku_backend_t *b = tiku_backend_posix_open();
    tiku_model_change_t ch[4];
    char path[64];
    int complete = -1;

    CHECK(b != NULL && b->ops->changes != NULL && b->ops->path_of_id != NULL);
    CHECK(b->ops->changes(b, ch, 4, &complete) == 0 && complete == 1);
    CHECK(first_next == UINT64_MAX);
    records[0] = 7u;
    records[1] = 9u;
    waiting = 2;
    CHECK(b->ops->changes(b, ch, 4, &complete) == 2 && complete == 1);
    CHECK(ch[0].op == TIKU_CH_SUBTREE && ch[0].id == 7u && ch[1].op == TIKU_CH_SUBTREE &&
          ch[1].id == 9u);
    CHECK(b->ops->changes(b, ch, 4, &complete) == 0 && complete == 1);
    records[2] = 7u;
    waiting = 3;
    lost = 1;
    CHECK(b->ops->changes(b, ch, 4, &complete) == 1 && complete == 0);
    CHECK(b->ops->path_of_id(b, 7u, path, sizeof path) == 0 && !strcmp(path, "/home/Documents"));
    CHECK(b->ops->path_of_id(b, 9u, path, sizeof path) == -1);
    tiku_backend_close(b);
    puts("PASS posix changes: a change under each recorded directory, partial when records "
         "were lost, ids through the platform");
    return 0;
}
