/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_project.c - a KiCad hierarchy imported as one transaction:
 * each linked file asked of the application's resolver, parsed once and
 * decoded per sheet instance under a shared byte budget; and the sheet
 * list of a self-contained schematic format.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_circuit_kicad_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NAME_BYTES 256          /* a sheet's name; the project bucket    */
#define PATH_BYTES 4096         /* a sheet's file                        */
#define KEY_BYTES  128          /* a physical file's key                 */
#define PAGE_BYTES 64           /* a page number                         */

/* A sheet's UUID path: a '/' and a UUID for each level, the root's too. */
#define INSTANCE_BYTES ((TIKU_CIRCUIT_DEPTH_MAX + 1) * KC_UUID_BYTES + 1)

/** @brief One sheet instance and the strings its descriptor borrows. */
typedef struct {
    tiku_circuit_sheet_t info;      /* the public descriptor             */
    char name[NAME_BYTES];          /* the sheet's name                  */
    char path[PATH_BYTES];          /* its file, as the resolver gave it */
    char instance[INSTANCE_BYTES];  /* its UUID path from the root       */
    char page[PAGE_BYTES];          /* its page number                   */
    tiku_circuit_diagnostic_t diag; /* why it is unavailable, if it is   */
    tiku_circuit_t *scene;          /* its scene, or NULL                */
} sheet_t;

struct tiku_circuit_project {
    sheet_t sheet[TIKU_CIRCUIT_SHEETS_MAX];  /* the root, then by link   */
    size_t count;                   /* sheets in use                     */
    int incomplete;                 /* some sheet has no scene           */
};

/** @brief A file's bytes and syntax, kept while the import runs. */
typedef struct {
    char key[KEY_BYTES];            /* the physical file                 */
    unsigned char *data;            /* a copy of its bytes               */
    size_t bytes;                   /* their length                      */
    circuit_sx_t tree;              /* its parsed syntax                 */
} source_t;

/** @brief An import under way. */
typedef struct {
    tiku_circuit_project_t *project;            /* being built           */
    const tiku_circuit_import_t *options;       /* font, cancellation    */
    const tiku_circuit_resolver_t *resolver;    /* the application's     */
    source_t cache[TIKU_CIRCUIT_SHEETS_MAX];    /* files parsed so far   */
    size_t cached;                  /* entries of cache in use           */
    size_t bytes;                   /* bytes charged to the budget       */
    const char *active[TIKU_CIRCUIT_DEPTH_MAX + 1]; /* files being read, */
                                                    /* root first        */
    char bucket[NAME_BYTES];        /* the root's name, its project      */
} loader_t;

/** @brief Fill diagnostic @p d, which may be NULL, with @p e and
 *         @p message.  @return @p e */
static tiku_circuit_err_t
fail(tiku_circuit_diagnostic_t *d, tiku_circuit_err_t e, const char *message)
{
    if (d) {
        d->code = e;
        snprintf(d->message, sizeof d->message, "%s", message);
    }
    return e;
}

/** @brief Whether @p bytes more fit within the import's budget. */
static int
room(loader_t *l, size_t bytes)
{
    return bytes <= TIKU_CIRCUIT_PROJECT_BYTES - l->bytes;
}

/** @brief Whether the caller has asked the import to stop. */
static int
cancelled(loader_t *l)
{
    return l->options && l->options->cancel &&
           l->options->cancel(l->options->ctx);
}

/** @brief Whether a child sheet's failure @p e ends the whole import;
 *         any other failure leaves that sheet unavailable. */
static int
fatal(tiku_circuit_err_t e)
{
    return e == TIKU_CIRCUIT_NOMEM || e == TIKU_CIRCUIT_LIMIT ||
           e == TIKU_CIRCUIT_CANCELLED;
}

/** @brief The root's own page number from its sheet_instances table, into
 *         @p page; KiCad lets it be other than "1".
 *  @return OK, or the failure noted in @p k */
static tiku_circuit_err_t
root_page(circuit_kc_t *k, char page[PAGE_BYTES])
{
    size_t table = kc_child(k, 0, "sheet_instances");

    if (table == SX_NONE) {
        return TIKU_CIRCUIT_OK;
    }
    if (!kc_fields(k, table, "path", "path", 0, "")) {
        return k->error;
    }
    int found = 0;

    for (size_t n = table + 2; n < k->sx.node[table].end;
         n = k->sx.node[n].end) {
        if (!kc_fields(k, n, "page", "", 1, "")) {
            return k->error;
        }
        if (strcmp(kc_arg(k, n, 0), "/")) {
            continue;
        }
        size_t p = kc_child(k, n, "page");

        if (!kc_fields(k, p, "", "", 1, "")) {
            return k->error;
        }
        const char *text = kc_arg(k, p, 0);

        if (found++ || !*text) {
            kc_fail(k, p, TIKU_CIRCUIT_BAD,
                    "Invalid or duplicate root page number");
            return k->error;
        }
        if (strlen(text) >= PAGE_BYTES) {
            kc_fail(k, p, TIKU_CIRCUIT_LIMIT,
                    "Root page number is too long");
            return k->error;
        }
        memcpy(page, text, strlen(text) + 1);
    }
    return TIKU_CIRCUIT_OK;
}

/**
 * @brief The cached syntax of resolver result @p s, parsed on the file's
 *        first read.
 *
 * Syntax is kept once per physical file and geometry decoded per
 * instance.  A later read must match the first byte for byte: a file
 * changed during the import fails with TIKU_CIRCUIT_IO.
 *
 * @return OK with @p out set, or why not
 */
static tiku_circuit_err_t
source(loader_t *l, const tiku_circuit_source_t *s, unsigned depth,
       source_t **out, tiku_circuit_diagnostic_t *diag)
{
    if (!s->data || !s->bytes || !*s->path || !*s->key ||
        !memchr(s->path, 0, sizeof s->path) ||
        !memchr(s->key, 0, sizeof s->key)) {
        return fail(diag, TIKU_CIRCUIT_BAD, "Invalid resolver result");
    }
    if (s->bytes > CIRCUIT_INPUT_MAX) {
        return fail(diag, TIKU_CIRCUIT_LIMIT, "Schematic exceeds 8 MiB");
    }
    for (unsigned i = 0; i < depth; i++) {
        if (!strcmp(l->active[i], s->key)) {
            return fail(diag, TIKU_CIRCUIT_BAD, "Cyclic sheet dependency");
        }
    }
    for (size_t i = 0; i < l->cached; i++) {
        if (!strcmp(l->cache[i].key, s->key)) {
            source_t *c = &l->cache[i];

            if (c->bytes != s->bytes ||
                memcmp(c->data, s->data, s->bytes)) {
                return fail(diag, TIKU_CIRCUIT_IO,
                            "Linked file changed during import; try Reload");
            }
            *out = c;
            return TIKU_CIRCUIT_OK;
        }
    }
    if (l->cached == TIKU_CIRCUIT_SHEETS_MAX) {
        return fail(diag, TIKU_CIRCUIT_LIMIT,
                    "Too many schematic source files");
    }
    /* Reserve a conservative bound before parsing or copying: there are
     * no more nodes than input bytes, and the node array doubles from its
     * first size. */
    size_t nodes = CIRCUIT_NODES_FIRST;

    while (nodes < s->bytes && nodes < CIRCUIT_TOKEN_MAX) {
        nodes *= 2;
    }
    if (!room(l, 3 * s->bytes + 2 + nodes * sizeof(circuit_sx_node_t))) {
        return fail(diag, TIKU_CIRCUIT_LIMIT,
                    "Project import exceeds the 64 MiB budget");
    }
    source_t *c = &l->cache[l->cached++];

    c->data = malloc(s->bytes);
    if (!c->data) {
        return fail(diag, TIKU_CIRCUIT_NOMEM,
                    "Cannot cache schematic source");
    }
    memcpy(c->data, s->data, s->bytes);
    c->bytes = s->bytes;
    snprintf(c->key, sizeof c->key, "%s", s->key);
    tiku_circuit_err_t e = circuit_sx_parse(s->data, s->bytes, l->options,
                                            &c->tree, diag);

    if (e) {
        free(c->data);
        memset(c, 0, sizeof *c);
        l->cached--;
        return e;
    }
    l->bytes += 2 * s->bytes + 1 +
                c->tree.capacity * sizeof(circuit_sx_node_t);
    *out = c;
    return TIKU_CIRCUIT_OK;
}

/**
 * @brief Read, parse and decode sheet @p index from @p reference, then
 *        the sheets it links, depth first.
 *
 * A child's failure leaves that child unavailable and the project
 * incomplete, unless it is fatal(), which ends the import.
 *
 * @return OK, the sheet's own failure, or a child's fatal one
 */
static tiku_circuit_err_t
load(loader_t *l, size_t index, const char *reference)
{
    sheet_t *s = &l->project->sheet[index];

    if (cancelled(l)) {
        return fail(&s->diag, TIKU_CIRCUIT_CANCELLED, "Import cancelled");
    }
    if (!room(l, CIRCUIT_INPUT_MAX + 1)) {
        return fail(&s->diag, TIKU_CIRCUIT_LIMIT,
                    "Project read exceeds the 64 MiB budget");
    }
    tiku_circuit_source_t input = {0};
    const char *parent = s->info.parent == SIZE_MAX
                         ? NULL : l->project->sheet[s->info.parent].path;
    tiku_circuit_err_t e = l->resolver->read(l->resolver->ctx, parent,
                                             reference, &input, &s->diag);
    source_t *c = NULL;

    if (!e) {
        e = source(l, &input, s->info.depth, &c, &s->diag);
    }
    if (!e) {
        snprintf(s->path, sizeof s->path, "%s", input.path);
    }
    l->resolver->release(l->resolver->ctx, &input);
    if (e) {
        if (!s->diag.code) {
            fail(&s->diag, e, tiku_circuit_error(e));
        }
        return e;
    }
    l->active[s->info.depth] = c->key;
    if (!index) {
        circuit_kc_t k = {.sx = c->tree, .options = l->options,
                          .diag = &s->diag};
        const char *uuid = kc_arg(&k, kc_child(&k, 0, "uuid"), 0);

        if (!kc_uuid(uuid)) {
            s->diag.line = 1;
            s->diag.column = 1;
            return fail(&s->diag, TIKU_CIRCUIT_BAD,
                        "Root schematic requires a valid UUID");
        }
        snprintf(s->instance, sizeof s->instance, "/%s", uuid);
        const char *base = strrchr(s->path, '/');

        base = base ? base + 1 : s->path;
        size_t base_bytes = strlen(base);

        if (base_bytes >= sizeof l->bucket) {
            return fail(&s->diag, TIKU_CIRCUIT_LIMIT,
                        "Root schematic name is too long");
        }
        memcpy(s->name, base, base_bytes + 1);
        memcpy(l->bucket, base, base_bytes + 1);
        char *extension = strrchr(l->bucket, '.');

        if (extension) {
            *extension = 0;
        }
        e = root_page(&k, s->page);
        if (e) {
            return e;
        }
    }
    size_t font = l->options ? l->options->font_bytes : 0;

    if (font > CIRCUIT_FONT_MAX ||
        !room(l, CIRCUIT_BYTES_MAX + font + sizeof(circuit_kc_links_t))) {
        return fail(&s->diag, TIKU_CIRCUIT_LIMIT,
                    "Project scene exceeds the 64 MiB budget");
    }
    circuit_kc_links_t *links = calloc(1, sizeof *links);

    if (!links) {
        return fail(&s->diag, TIKU_CIRCUIT_NOMEM,
                    "Cannot allocate sheet links");
    }
    l->bytes += sizeof *links;
    e = circuit_kc_decode(&c->tree, l->options, s->instance, l->bucket,
                          index != 0, links, &s->scene, &s->diag);
    if (!e) {
        l->bytes += s->scene->bytes + (s->scene->font ? font : 0);
    }
    for (size_t i = 0; !e && i < links->count; i++) {
        circuit_kc_link_t *link = &links->link[i];

        if (s->info.depth == TIKU_CIRCUIT_DEPTH_MAX ||
            l->project->count == TIKU_CIRCUIT_SHEETS_MAX) {
            e = fail(&s->diag, TIKU_CIRCUIT_LIMIT,
                     "Hierarchy exceeds 64 sheets or 16 child levels");
            break;
        }
        size_t child = l->project->count++;
        sheet_t *d = &l->project->sheet[child];

        d->info = (tiku_circuit_sheet_t){d->name, d->path, d->instance,
                                         d->page, index, s->info.depth + 1,
                                         &d->diag};
        snprintf(d->name, sizeof d->name, "%s", link->name);
        snprintf(d->path, sizeof d->path, "%s", link->file);
        snprintf(d->page, sizeof d->page, "%s", link->page);
        size_t length = strlen(s->instance);

        if (length + 1 + KC_UUID_BYTES > sizeof d->instance) {
            e = fail(&s->diag, TIKU_CIRCUIT_LIMIT,
                     "Sheet instance path is too long");
            break;
        }
        memcpy(d->instance, s->instance, length);
        d->instance[length] = '/';
        memcpy(d->instance + length + 1, link->uuid, KC_UUID_BYTES);
        tiku_circuit_err_t child_error = load(l, child, link->file);

        if (fatal(child_error)) {
            s->diag = d->diag;
            e = child_error;
        } else if (child_error) {
            l->project->incomplete = 1;
        }
    }
    l->bytes -= sizeof *links;
    free(links);
    return e;
}

tiku_circuit_err_t
tiku_circuit_project_open(const char *root,
                          const tiku_circuit_import_t *options,
                          const tiku_circuit_resolver_t *resolver,
                          tiku_circuit_project_t **out,
                          tiku_circuit_diagnostic_t *diag)
{
    if (diag) {
        memset(diag, 0, sizeof *diag);
    }
    if (!root || !*root || !out || !resolver || !resolver->read ||
        !resolver->release) {
        return fail(diag, TIKU_CIRCUIT_BAD,
                    "Missing project source or resolver");
    }
    loader_t *l = calloc(1, sizeof *l);
    tiku_circuit_project_t *p = calloc(1, sizeof *p);

    if (!l || !p) {
        free(l);
        free(p);
        return fail(diag, TIKU_CIRCUIT_NOMEM, "Cannot allocate project");
    }
    l->project = p;
    l->options = options;
    l->resolver = resolver;
    l->bytes = sizeof *l + sizeof *p;
    p->count = 1;
    sheet_t *s = &p->sheet[0];

    s->info = (tiku_circuit_sheet_t){s->name, s->path, s->instance, s->page,
                                     SIZE_MAX, 0, &s->diag};
    snprintf(s->page, sizeof s->page, "1");
    tiku_circuit_err_t e = load(l, 0, root);

    if (diag) {
        *diag = s->diag;
    }
    for (size_t i = 0; i < l->cached; i++) {
        circuit_sx_free(&l->cache[i].tree);
        free(l->cache[i].data);
    }
    free(l);
    if (e) {
        tiku_circuit_project_free(p);
        return e;
    }
    *out = p;
    return TIKU_CIRCUIT_OK;
}

void
tiku_circuit_project_free(tiku_circuit_project_t *p)
{
    if (!p) {
        return;
    }
    for (size_t i = 0; i < p->count; i++) {
        tiku_circuit_release(p->sheet[i].scene);
    }
    free(p);
}

size_t
tiku_circuit_project_count(const tiku_circuit_project_t *p)
{
    return p ? p->count : 0;
}

int
tiku_circuit_project_incomplete(const tiku_circuit_project_t *p)
{
    return p && p->incomplete;
}

const tiku_circuit_sheet_t *
tiku_circuit_project_sheet(const tiku_circuit_project_t *p, size_t i)
{
    return p && i < p->count ? &p->sheet[i].info : NULL;
}

tiku_circuit_t *
tiku_circuit_project_scene(const tiku_circuit_project_t *p, size_t i)
{
    return p && i < p->count ? p->sheet[i].scene : NULL;
}

tiku_circuit_err_t
circuit_project_sheets(tiku_circuit_t *const *scenes,
                       const char *const *names, const char *path,
                       size_t count, tiku_circuit_project_t **out)
{
    if (!scenes || !names || !path || !out || !count) {
        return TIKU_CIRCUIT_BAD;
    }
    if (count > TIKU_CIRCUIT_SHEETS_MAX || strlen(path) >= PATH_BYTES) {
        return TIKU_CIRCUIT_LIMIT;
    }
    tiku_circuit_project_t *p = calloc(1, sizeof *p);

    if (!p) {
        return TIKU_CIRCUIT_NOMEM;
    }
    for (size_t i = 0; i < count; i++) {
        if (!scenes[i] || !names[i]) {
            tiku_circuit_project_free(p);
            return TIKU_CIRCUIT_BAD;
        }
        if (strlen(names[i]) >= NAME_BYTES) {
            tiku_circuit_project_free(p);
            return TIKU_CIRCUIT_LIMIT;
        }
        sheet_t *s = &p->sheet[p->count++];

        s->scene = tiku_circuit_retain(scenes[i]);
        snprintf(s->name, sizeof s->name, "%s", names[i]);
        snprintf(s->path, sizeof s->path, "%s", path);
        snprintf(s->instance, sizeof s->instance, "/eagle/%zu", i + 1);
        snprintf(s->page, sizeof s->page, "%zu", i + 1);
        s->info = (tiku_circuit_sheet_t){s->name, s->path, s->instance,
                                         s->page, SIZE_MAX, 0, &s->diag};
    }
    *out = p;
    return TIKU_CIRCUIT_OK;
}
