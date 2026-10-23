/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_shapegeom.c - DrawingML geometry evaluated: names, formulas,
 * guides, text rectangles, handles, sites, and the built-in presets.
 *
 * Names live in an open-addressed table sized to the definition, so a
 * preset of a few hundred guides costs a hash per lookup.  Formulas are
 * read from the definition's strings at each evaluation.  The paths are
 * tiku_shapegeom_path.c's; dragging a handle is tiku_shapegeom_drag.c's.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shapegeom_private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_shapegeom_presets.inl"

void
sg_refuse(env_t *e, tiku_shapegeom_status_t status)
{
    if (e->status == TIKU_SHAPEGEOM_OK) {
        e->status = status;
    }
}

int
sg_fault(env_t *e)
{
    if (e->flags & TIKU_SHAPEGEOM_GEOM_LENIENT) {
        return 1;
    }
    sg_refuse(e, TIKU_SHAPEGEOM_INVALID_GEOMETRY);
    return 0;
}

float
sg_scaled(env_t *e, double v, double unit)
{
    double s = v / unit;

    if (!isfinite(s) || fabs(s) > MAX_COORD) {
        sg_fault(e);
        return 0;
    }
    return (float)s;
}

/** @brief Whether @p t can be a name: 1 to 31 letters, digits or '_'. */
static int
is_name(tok_t t)
{
    size_t i;

    if (t.n == 0 || t.n >= NAME_BYTES) {
        return 0;
    }
    for (i = 0; i < t.n; i++) {
        unsigned char c = (unsigned char)t.s[i];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) {
            return 0;
        }
    }
    return 1;
}

/** @brief The slot holding name @p t, or the free slot it would take. */
static slot_t *
lookup(const env_t *e, tok_t t)
{
    uint64_t h = 1469598103934665603u;
    size_t i, k;

    for (i = 0; i < t.n; i++) {
        h = (h ^ (unsigned char)t.s[i]) * 1099511628211u;
    }
    for (k = 0; k < e->cap; k++) {
        slot_t *s = &e->slot[(h + k) & (e->cap - 1)];

        if (s->name[0] == 0 ||
            (strncmp(s->name, t.s, t.n) == 0 && s->name[t.n] == 0)) {
            return s;
        }
    }
    return NULL; /* full: the table is sized to never be */
}

/** @brief Name @p t set to @p v.  @return the slot, or NULL refused. */
static slot_t *
set(env_t *e, tok_t t, double v)
{
    slot_t *s;

    if (!is_name(t)) {
        sg_fault(e);
        return NULL;
    }
    s = lookup(e, t);
    if (s == NULL) {
        sg_refuse(e, TIKU_SHAPEGEOM_LIMIT);
        return NULL;
    }
    if (s->name[0] == 0) {
        memcpy(s->name, t.s, t.n);
        s->name[t.n] = 0;
    }
    s->v = v;
    return s;
}

/** @brief Name @p name set from a C string. */
static void
set_c(env_t *e, const char *name, double v)
{
    tok_t t = {name, strlen(name)};

    (void)set(e, t, v);
}

int
sg_next(const char **p, const char *end, tok_t *t)
{
    while (*p < end && **p == ' ') {
        (*p)++;
    }
    t->s = *p;
    while (*p < end && **p != ' ') {
        (*p)++;
    }
    t->n = (size_t)(*p - t->s);
    return t->n > 0;
}

/** @brief Whether @p t is written as a decimal number. */
static int
is_number(tok_t t)
{
    size_t i = 0, digits = 0;

    if (i < t.n && (t.s[i] == '-' || t.s[i] == '+')) {
        i++;
    }
    for (; i < t.n && t.s[i] >= '0' && t.s[i] <= '9'; i++) {
        digits++;
    }
    if (i < t.n && t.s[i] == '.') {
        for (i++; i < t.n && t.s[i] >= '0' && t.s[i] <= '9'; i++) {
            digits++;
        }
    }
    return digits > 0 && i == t.n && t.n < 64;
}

/** @brief A token's value: a number, or a name set before.  An unknown
 *         name is a fault and reads 0. */
static double
value(env_t *e, tok_t t)
{
    char buf[64];
    slot_t *s;

    if (is_number(t)) {
        memcpy(buf, t.s, t.n);
        buf[t.n] = 0;
        return strtod(buf, NULL);
    }
    s = is_name(t) ? lookup(e, t) : NULL;
    if (s == NULL || s->name[0] == 0) {
        sg_fault(e);
        return 0;
    }
    return s->v;
}

double
sg_next_value(env_t *e, const char **p, const char *end)
{
    tok_t t;

    if (!sg_next(p, end, &t)) {
        sg_fault(e);
        return 0;
    }
    return value(e, t);
}

/** @brief The operators of ECMA-376 Part 1, 20.1.10.24, with their
 *         operand counts. */
static const struct {
    const char *op;
    int n;
} OPS[] = {{"*/", 3},  {"+-", 3},   {"+/", 3},   {"?:", 3},  {"abs", 1},
           {"at2", 2}, {"cat2", 3}, {"cos", 2},  {"max", 2}, {"min", 2},
           {"mod", 3}, {"pin", 3},  {"sat2", 3}, {"sin", 2}, {"sqrt", 1},
           {"tan", 2}, {"val", 1}};

/** @brief Operator @p op applied to @p a.  A division by zero and the
 *         square root of a negative number are 0, as ECMA-376's readers
 *         take them. */
static double
apply(int op, const double *a)
{
    switch (op) {
    case 0:
        return a[2] != 0 ? a[0] * a[1] / a[2] : 0;
    case 1:
        return a[0] + a[1] - a[2];
    case 2:
        return a[2] != 0 ? (a[0] + a[1]) / a[2] : 0;
    case 3:
        return a[0] > 0 ? a[1] : a[2];
    case 4:
        return fabs(a[0]);
    case 5:
        return atan2(a[1], a[0]) / DEG * ANGLE;
    case 6:
        return a[0] * cos(atan2(a[2], a[1]));
    case 7:
        return a[0] * cos(a[1] / ANGLE * DEG);
    case 8:
        return fmax(a[0], a[1]);
    case 9:
        return fmin(a[0], a[1]);
    case 10:
        return sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    case 11:
        return a[1] < a[0] ? a[0] : a[1] > a[2] ? a[2] : a[1];
    case 12:
        return a[0] * sin(atan2(a[2], a[1]));
    case 13:
        return a[0] * sin(a[1] / ANGLE * DEG);
    case 14:
        return a[0] > 0 ? sqrt(a[0]) : 0;
    case 15:
        return a[0] * tan(a[1] / ANGLE * DEG);
    default:
        return a[0];
    }
}

/** @brief Formula [@p p, @p end) worked out. */
static double
formula(env_t *e, const char *p, const char *end)
{
    double a[3] = {0, 0, 0}, v;
    tok_t op, t;
    int which = -1, k = 0;
    size_t i;

    if (!sg_next(&p, end, &op)) {
        sg_fault(e);
        return 0;
    }
    for (i = 0; i < sizeof OPS / sizeof OPS[0]; i++) {
        if (strlen(OPS[i].op) == op.n && memcmp(OPS[i].op, op.s, op.n) == 0) {
            which = (int)i;
        }
    }
    if (which < 0 && !sg_fault(e)) {
        return 0;
    }
    while (sg_next(&p, end, &t)) {
        if (k == 3 || (which >= 0 && k == OPS[which].n)) {
            sg_fault(e); /* more operands than the operator has */
            break;
        }
        a[k++] = value(e, t);
    }
    if (which >= 0 && k < OPS[which].n) {
        sg_fault(e);
    }
    v = apply(which, a);
    if (!isfinite(v)) {
        sg_fault(e);
        v = 0;
    }
    return v;
}

/** @brief @p prefix and @p n into @p out ("wd" 4: "wd4"); @p out holds
 *         16 bytes. */
static void
numbered(char *out, const char *prefix, int n)
{
    size_t k = strlen(prefix);

    memcpy(out, prefix, k);
    if (n >= 10) {
        out[k++] = (char)('0' + n / 10);
    }
    out[k++] = (char)('0' + n % 10);
    out[k] = 0;
}

/** @brief The names every formula may use, for a @p w by @p h EMU box
 *         (ECMA-376 Part 1, 20.1.9.11, and their kin). */
static void
builtins(env_t *e, double w, double h)
{
    static const int div[] = {2, 3, 4, 5, 6, 8, 10, 12, 16, 32};
    static const struct {
        const char *name;
        double v;
    } angle[] = {{"cd2", 10800000},  {"cd4", 5400000},  {"cd8", 2700000},
                 {"3cd4", 16200000}, {"3cd8", 8100000}, {"5cd8", 13500000},
                 {"7cd8", 18900000}};
    double ss = fmin(w, h);
    char name[16];
    size_t i;

    set_c(e, "w", w);
    set_c(e, "h", h);
    set_c(e, "l", 0);
    set_c(e, "t", 0);
    set_c(e, "r", w);
    set_c(e, "b", h);
    set_c(e, "hc", w / 2);
    set_c(e, "vc", h / 2);
    set_c(e, "ss", ss);
    set_c(e, "ls", fmax(w, h));
    for (i = 0; i < sizeof div / sizeof div[0]; i++) {
        numbered(name, "wd", div[i]);
        set_c(e, name, w / div[i]);
        numbered(name, "hd", div[i]);
        set_c(e, name, h / div[i]);
        numbered(name, "ssd", div[i]);
        set_c(e, name, ss / div[i]);
    }
    for (i = 0; i < sizeof angle / sizeof angle[0]; i++) {
        set_c(e, angle[i].name, angle[i].v);
    }
}

/** @brief The next ';'-separated item of the list at @p p into
 *         [@p *start, @p *stop), @p p moved past it.  @return 0 at the
 *         end of the string. */
static int
item(const char **p, const char **start, const char **stop)
{
    while (**p == ' ' || **p == ';') {
        (*p)++;
    }
    if (**p == 0) {
        return 0;
    }
    *start = *p;
    while (**p != 0 && **p != ';') {
        (*p)++;
    }
    *stop = *p;
    return 1;
}

/** @brief How many items list @p s holds, at most. */
static size_t
items(const char *s)
{
    size_t n = 1;

    if (s == NULL || *s == 0) {
        return 0;
    }
    for (; *s; s++) {
        n += *s == ';';
    }
    return n;
}

/** @brief Each "name formula" of list @p list set, in order; @p adjust
 *         marks them as values a handle may move. */
static void
guides(env_t *e, const char *list, int adjust)
{
    const char *start, *stop;
    tok_t name;

    while (list != NULL && e->status == TIKU_SHAPEGEOM_OK &&
           item(&list, &start, &stop)) {
        slot_t *s;
        double v;

        if (!sg_next(&start, stop, &name)) {
            sg_fault(e);
            continue;
        }
        v = formula(e, start, stop);
        s = set(e, name, v);
        if (s != NULL && adjust) {
            s->adjust = 1;
        }
    }
}

/** @brief The shape's own adjust values over the defaults.  In strict
 *         mode each must name an adjust value the definition has. */
static void
overrides(env_t *e, const tiku_shapegeom_adjust_t *adjust, size_t count)
{
    size_t i;

    for (i = 0; i < count && e->status == TIKU_SHAPEGEOM_OK; i++) {
        tok_t name = {adjust[i].name,
                      adjust[i].name ? strlen(adjust[i].name) : 0};
        slot_t *s = is_name(name) ? lookup(e, name) : NULL;
        double v = adjust[i].value;

        if ((s == NULL || !s->adjust) && !sg_fault(e)) {
            return;
        }
        if (adjust[i].formula != NULL) {
            v = formula(e, adjust[i].formula,
                        adjust[i].formula + strlen(adjust[i].formula));
        } else if (!isfinite(v)) {
            sg_refuse(e, TIKU_SHAPEGEOM_INVALID);
            return;
        }
        (void)set(e, name, v);
    }
}

/** @brief The text rectangle, in points; an empty one is the shape. */
static void
text_rect(env_t *e, const char *spec, float *box)
{
    const char *end;
    int i;

    if (spec == NULL || *spec == 0) {
        spec = "l t r b";
    }
    end = spec + strlen(spec);
    for (i = 0; i < 4; i++) {
        box[i] = sg_scaled(e, sg_next_value(e, &spec, end), EMU);
    }
}

/** @brief One reference and range of a handle: "ref min max", '-' for
 *         a direction it does not move. */
static void
handle_axis(env_t *e, tiku_shapegeom_handle_t *hd, int k, const char **p,
            const char *end)
{
    tok_t ref, lo, hi;
    slot_t *s;

    if (!sg_next(p, end, &ref) || !sg_next(p, end, &lo) ||
        !sg_next(p, end, &hi)) {
        sg_fault(e);
        return;
    }
    if (ref.n == 1 && ref.s[0] == '-') {
        return;
    }
    s = is_name(ref) ? lookup(e, ref) : NULL;
    if ((s == NULL || !s->adjust) && !sg_fault(e)) {
        return;
    }
    if (is_name(ref)) {
        memcpy(hd->ref[k], ref.s, ref.n);
        hd->ref[k][ref.n] = 0;
    }
    hd->value[k] = s != NULL && s->name[0] ? s->v : 0;
    hd->min[k] = lo.n == 1 && lo.s[0] == '-' ? -EXTENT : value(e, lo);
    hd->max[k] = hi.n == 1 && hi.s[0] == '-' ? EXTENT : value(e, hi);
    if (hd->min[k] > hd->max[k]) {
        double t = hd->min[k];

        hd->min[k] = hd->max[k];
        hd->max[k] = t;
    }
}

/** @brief The handles of list @p list, evaluated into @p out. */
static void
handles(env_t *e, tiku_shapegeom_outline_t *out, const char *list)
{
    const char *start, *stop;
    tok_t kind;

    if (list == NULL || items(list) == 0) {
        return;
    }
    if (items(list) > MAX_ITEMS) {
        sg_refuse(e, TIKU_SHAPEGEOM_LIMIT);
        return;
    }
    out->handles = calloc(items(list), sizeof *out->handles);
    if (out->handles == NULL) {
        sg_refuse(e, TIKU_SHAPEGEOM_NO_MEMORY);
        return;
    }
    while (e->status == TIKU_SHAPEGEOM_OK && item(&list, &start, &stop)) {
        tiku_shapegeom_handle_t *hd = &out->handles[out->handle_count];

        if (!sg_next(&start, stop, &kind) || kind.n != 1 ||
            (kind.s[0] != 'X' && kind.s[0] != 'R')) {
            sg_fault(e);
            continue;
        }
        hd->kind = kind.s[0] == 'X' ? TIKU_SHAPEGEOM_HANDLE_XY
                                    : TIKU_SHAPEGEOM_HANDLE_POLAR;
        handle_axis(e, hd, 0, &start, stop);
        handle_axis(e, hd, 1, &start, stop);
        hd->x = sg_scaled(e, sg_next_value(e, &start, stop), EMU);
        hd->y = sg_scaled(e, sg_next_value(e, &start, stop), EMU);
        out->handle_count++;
    }
}

/** @brief The connection sites of list @p list, evaluated into @p out. */
static void
sites(env_t *e, tiku_shapegeom_outline_t *out, const char *list)
{
    const char *start, *stop;

    if (list == NULL || items(list) == 0) {
        return;
    }
    if (items(list) > MAX_ITEMS) {
        sg_refuse(e, TIKU_SHAPEGEOM_LIMIT);
        return;
    }
    out->sites = calloc(items(list), sizeof *out->sites);
    if (out->sites == NULL) {
        sg_refuse(e, TIKU_SHAPEGEOM_NO_MEMORY);
        return;
    }
    while (e->status == TIKU_SHAPEGEOM_OK && item(&list, &start, &stop)) {
        tiku_shapegeom_site_t *s = &out->sites[out->site_count++];

        s->angle = sg_scaled(e, sg_next_value(e, &start, stop), ANGLE);
        s->x = sg_scaled(e, sg_next_value(e, &start, stop), EMU);
        s->y = sg_scaled(e, sg_next_value(e, &start, stop), EMU);
    }
}

void
tiku_shapegeom_outline_free(tiku_shapegeom_outline_t *outline)
{
    size_t i;

    if (outline == NULL) {
        return;
    }
    for (i = 0; i < outline->path_count; i++) {
        free(outline->paths[i].ops);
        free(outline->paths[i].points);
    }
    free(outline->paths);
    free(outline->handles);
    free(outline->sites);
    memset(outline, 0, sizeof *outline);
}

/** @brief A name table with room for every name @p def and @p count
 *         overrides can set, kept under half full.
 *  @return 0, -1 out of memory, -2 past MAX_ITEMS names. */
static int
env_open(env_t *e, const tiku_shapegeom_def_t *def, size_t count,
         const tiku_shapegeom_opt_t *options)
{
    size_t need = 64 + items(def->av) + items(def->gd) + count;

    memset(e, 0, sizeof *e);
    if (need > 64 + MAX_ITEMS) {
        return -2;
    }
    e->cap = 128;
    while (e->cap < need * 2) {
        e->cap *= 2;
    }
    e->slot = calloc(e->cap, sizeof *e->slot);
    e->flags = options ? options->flags : 0;
    e->max_points = options && options->max_points
                        ? options->max_points
                        : TIKU_SHAPEGEOM_GEOM_MAX_POINTS;
    return e->slot != NULL ? 0 : -1;
}

tiku_shapegeom_status_t
tiku_shapegeom_eval(const tiku_shapegeom_def_t *def,
                    const tiku_shapegeom_adjust_t *adjust, size_t adjust_count,
                    float width, float height,
                    const tiku_shapegeom_opt_t *options,
                    tiku_shapegeom_outline_t *out)
{
    double w = (double)width * EMU, h = (double)height * EMU;
    env_t e;

    if (out == NULL) {
        return TIKU_SHAPEGEOM_INVALID;
    }
    memset(out, 0, sizeof *out);
    if (!isfinite(width) || !isfinite(height) || width < 0 || height < 0 ||
        (adjust == NULL && adjust_count > 0)) {
        return TIKU_SHAPEGEOM_INVALID;
    }
    if (def == NULL) {
        def = tiku_shapegeom_preset("rect");
    }
    switch (env_open(&e, def, adjust_count, options)) {
    case -1:
        return TIKU_SHAPEGEOM_NO_MEMORY;
    case -2:
        return TIKU_SHAPEGEOM_LIMIT;
    default:
        break;
    }
    builtins(&e, w, h);
    guides(&e, def->av, 1);
    overrides(&e, adjust, adjust_count);
    guides(&e, def->gd, 0);
    if (e.status == TIKU_SHAPEGEOM_OK &&
        !(e.flags & TIKU_SHAPEGEOM_GEOM_NO_PATHS) && def->path != NULL) {
        sg_run_paths(&e, out, def->path, w, h);
    }
    text_rect(&e, def->rect, out->text);
    handles(&e, out, def->ah);
    sites(&e, out, def->cxn);
    free(e.slot);
    if (e.status != TIKU_SHAPEGEOM_OK) {
        tiku_shapegeom_outline_free(out);
    }
    return e.status;
}

const tiku_shapegeom_def_t *
tiku_shapegeom_preset(const char *name)
{
    size_t lo = 0, hi = TIKU_SHAPEGEOM_PRESET_COUNT;

    while (name != NULL && lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = strcmp(name, TIKU_SHAPEGEOM_PRESETS[mid].name);

        if (c == 0) {
            return &TIKU_SHAPEGEOM_PRESETS[mid];
        }
        if (c < 0) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return NULL;
}

size_t
tiku_shapegeom_preset_count(void)
{
    return TIKU_SHAPEGEOM_PRESET_COUNT;
}

const tiku_shapegeom_def_t *
tiku_shapegeom_preset_at(size_t index)
{
    return index < TIKU_SHAPEGEOM_PRESET_COUNT ? &TIKU_SHAPEGEOM_PRESETS[index]
                                               : NULL;
}

tiku_shapegeom_status_t
tiku_shapegeom_check(const tiku_shapegeom_def_t *def)
{
    static const float size[][2] = {
        {100, 100}, {300, 80}, {60, 240}, {0, 0}, {1, 1000}};
    tiku_shapegeom_outline_t o;
    size_t i;

    if (def == NULL) {
        return TIKU_SHAPEGEOM_INVALID;
    }
    for (i = 0; i < sizeof size / sizeof size[0]; i++) {
        tiku_shapegeom_status_t st =
            tiku_shapegeom_eval(def, NULL, 0, size[i][0], size[i][1], NULL, &o);

        tiku_shapegeom_outline_free(&o);
        if (st != TIKU_SHAPEGEOM_OK) {
            return st;
        }
    }
    return TIKU_SHAPEGEOM_OK;
}
