/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_find.c - the Find panel: its state, building a query from it
 * and filling it from one, the type menu, drawing and hit-testing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_state.h"
#include "tiku_strings.h"
#include "tiku_trk_find.h"
#include "tiku_ui.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Used before they are defined. */
static int in(tiku_rect_t r, int x, int y);
static void open_type_popup(tiku_trk_find_t *f, tiku_menu_t *m);
/* End of the forward declarations. */

/* The panel's layout, in pixels; BTN_H, BTN_W and TEXT_H are the R5 control
 * metrics. */
#define PAD        8
#define ROW_H     24
#define GAP        4
#define BTN_H     24     /* button height                                 */
#define BTN_W     75     /* minimum button width                          */
#define TEXT_H    20     /* text control height                           */
#define LABEL_W   64
#define FIELD_W  120
#define OP_W     110
#define JOIN_W    52
#define VALUE_W  150

/* The operator sets a field offers, a bit per tiku_qop_t: the five string
 * operators for text, is / greater / less for a measure, is / is not for a
 * yes-no field, and after / before for a date. */
#define OPS_TEXT  0x1Fu       /* contains, is, is not, starts, ends       */
#define OPS_NUM   0x62u       /* is, greater than, less than              */
#define OPS_EXACT 0x06u       /* is, is not -- for a yes/no field         */
#define OPS_DATE  0x60u       /* after, before                            */

/**
 * @brief The fields a row can test, in the order the menu offers them,
 *        each with its operators and the operator a row starts on.
 */
static const struct {
    tiku_qfield_t field;
    const char       *label;
    unsigned          ops;          /* bit per tiku_qop_t              */
    tiku_qop_t    first;
} kFields[] = {
    { TIKU_QF_NAME,     "label.name",       OPS_TEXT,  TIKU_OP_CONTAINS },
    { TIKU_QF_PATH,     "find.path",       OPS_TEXT,  TIKU_OP_CONTAINS },
    { TIKU_QF_KIND,     "label.kind",       OPS_TEXT,  TIKU_OP_IS },
    { TIKU_QF_VALUE,    "label.value",      OPS_TEXT | OPS_NUM,
                                                     TIKU_OP_IS },
    { TIKU_QF_UNIT,     "find.unit",       OPS_TEXT,  TIKU_OP_IS },
    { TIKU_QF_CAP,      "label.capability", OPS_TEXT,  TIKU_OP_IS },
    { TIKU_QF_SIZE,     "label.size",       OPS_NUM,   TIKU_OP_IS },
    { TIKU_QF_MODIFIED, "label.modified",   OPS_DATE,  TIKU_OP_LESS },
    { TIKU_QF_WRITABLE, "find.writable",   OPS_EXACT, TIKU_OP_IS }
};
#define NFIELDS ((int)(sizeof kFields / sizeof kFields[0]))

/** @brief Operator label keys, indexed by tiku_qop_t. */
static const char *kOps[] = {
    "op.contains", "op.is", "op.is_not", "op.starts_with", "op.ends_with",
    "op.greater_than", "op.less_than"
};

/**
 * @brief The word for @p op on @p field: before and after for a date,
 *        the operator's own name otherwise.
 */
static const char *
op_label(tiku_qfield_t field, tiku_qop_t op)
{
    if (field == TIKU_QF_MODIFIED) {
        return (op == TIKU_OP_GREATER) ? tiku_str("op.after")
                                             : tiku_str("op.before");
    }
    return tiku_str(kOps[op]);
}

/** @brief The label for field @p f; the name field's when unknown. */
static const char *
field_label(tiku_qfield_t f)
{
    int i;

    for (i = 0; i < NFIELDS; i++) {
        if (kFields[i].field == f) {
            return tiku_str(kFields[i].label);
        }
    }
    return tiku_str("label.name");
}

/** @brief The operators @p f offers, as a bit per operator. */
static unsigned
field_ops(tiku_qfield_t f)
{
    int i;

    for (i = 0; i < NFIELDS; i++) {
        if (kFields[i].field == f) {
            return kFields[i].ops;
        }
    }
    return OPS_TEXT;
}

/** @brief Whether one type record declares the model fact behind a field. */
static int
record_has_field(const tiku_trk_typerec_t *r, tiku_qfield_t field)
{
    int want = -1, i;

    switch (field) {
    case TIKU_QF_KIND:  want = TIKU_TRK_COL_KIND; break;
    case TIKU_QF_VALUE: want = TIKU_TRK_COL_VALUE; break;
    case TIKU_QF_CAP:   want = TIKU_TRK_COL_CAP; break;
    case TIKU_QF_PATH:  want = TIKU_TRK_COL_PATH; break;
    /* Unit describes a typed value, so a type declaring Value declares the
     * unit paired with it as searchable too. */
    case TIKU_QF_UNIT:  want = TIKU_TRK_COL_VALUE; break;
    default: break;
    }
    for (i = 0; r != NULL && want >= 0 && i < r->ncol; i++) {
        if (r->col[i] == want) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Whether @p field is in a row's attribute menu: name, size and
 *        modified always, and what the chosen type's record, or its
 *        supertype's, declares.
 */
static int
field_in_menu(const tiku_trk_find_t *f, tiku_qfield_t field)
{
    const tiku_trk_typerec_t *super, *exact;

    if (field == TIKU_QF_NAME || field == TIKU_QF_SIZE ||
        field == TIKU_QF_MODIFIED) {
        return 1;
    }
    if (f == NULL || f->types == NULL || f->type[0] == '\0') {
        return 0;
    }
    super = tiku_trk_types_super(f->types, f->type);
    exact = tiku_trk_types_exact(f->types, f->type);
    return record_has_field(super, field) || record_has_field(exact, field);
}

void
tiku_trk_find_init(tiku_trk_find_t *f)
{
    if (f == NULL) {
        return;
    }
    memset(f, 0, sizeof *f);
    f->mode = TIKU_QMODE_NAME;
    f->everywhere = 1;
    f->row_count = 1;
    f->row[0].field = TIKU_QF_NAME;
    f->row[0].op = TIKU_OP_CONTAINS;
    f->row[0].join = TIKU_JOIN_AND;
    f->focus = TIKU_TRK_FIND_FOCUS_NAME;
    f->popup_for = TIKU_TRK_FIND_HIT_NONE;
}

int
tiku_trk_find_add_place(tiku_trk_find_t *f, const char *name)
{
    int i;

    if (f == NULL || name == NULL ||
        f->place_count >= TIKU_TRK_FIND_PLACES_MAX) {
        return 0;
    }
    for (i = 0; i < f->place_count; i++) {
        if (strcmp(f->place[i], name) == 0) {
            return 0;
        }
    }
    snprintf(f->place[f->place_count], TIKU_NAME_MAX, "%s", name);
    f->place_count++;
    return 1;
}

int
tiku_trk_find_take_folder_request(tiku_trk_find_t *f)
{
    int was = (f != NULL) ? f->wants_folder : 0;

    if (f != NULL) {
        f->wants_folder = 0;
    }
    return was;
}

int
tiku_trk_find_scope_to_folder(tiku_trk_find_t *f, const char *path)
{
    int at;

    if (f == NULL || path == NULL || path[0] != '/' || path[1] == '\0') {
        return 0;
    }
    /* Stored without its leading slash, as every place is; the label and
     * the scope path both put it back. */
    if (!tiku_trk_find_add_place(f, path + 1)) {
        /* Already offered: the search is scoped to it. */
        int i;

        for (i = 0; i < f->place_count; i++) {
            if (strcmp(f->place[i], path + 1) == 0) {
                f->everywhere = 0;
                f->place_mask |= (1u << i);
                return 1;
            }
        }
        return 0;
    }
    at = f->place_count - 1;
    f->everywhere = 0;
    f->place_mask |= (1u << at);
    return 1;
}

int
tiku_trk_find_add_place_identity(tiku_trk_find_t *f, const char *name,
                                 uint64_t dev, const char *volume)
{
    int at;

    if (!tiku_trk_find_add_place(f, name)) {
        return 0;
    }
    at = f->place_count - 1;
    f->place_dev[at] = dev;
    snprintf(f->place_volume[at], sizeof f->place_volume[at], "%s",
             volume != NULL ? volume : "");
    return 1;
}

void
tiku_trk_find_set_mode(tiku_trk_find_t *f, tiku_qmode_t mode)
{
    if (f == NULL || f->mode == mode) {
        return;
    }
    if (mode == TIKU_QMODE_FORMULA) {
        /* Entering formula mode fills the field with the predicate the
         * controls describe. */
        tiku_query_t q;

        tiku_trk_find_build(f, &q);
        (void)tiku_query_predicate(&q, f->formula, sizeof f->formula);
        f->focus = TIKU_TRK_FIND_FOCUS_FORMULA;
    } else {
        /* Leaving formula mode keeps the formula and the attribute
         * rows. */
        f->focus = (mode == TIKU_QMODE_NAME)
                       ? TIKU_TRK_FIND_FOCUS_NAME
                       : TIKU_TRK_FIND_FOCUS_ROW;
        /* Attribute mode focuses the last row. */
        f->focus_row = (mode == TIKU_QMODE_ATTRIBUTE)
                           ? f->row_count - 1 : 0;
    }
    f->mode = mode;
    f->popup_for = TIKU_TRK_FIND_HIT_NONE;
}

int
tiku_trk_find_add_row(tiku_trk_find_t *f)
{
    tiku_trk_find_row_t *r;

    if (f == NULL || f->row_count >= TIKU_TRK_FIND_ROWS_MAX) {
        return 0;
    }
    r = &f->row[f->row_count];
    memset(r, 0, sizeof *r);
    r->field = kFields[0].field;
    r->op = kFields[0].first;
    r->join = TIKU_JOIN_AND;
    f->focus = TIKU_TRK_FIND_FOCUS_ROW;
    f->focus_row = f->row_count;
    f->row_count++;
    return 1;
}

int
tiku_trk_find_remove_row(tiku_trk_find_t *f)
{
    /* At least one row stays. */
    if (f == NULL || f->row_count <= 1) {
        return 0;
    }
    f->row_count--;
    if (f->focus_row >= f->row_count) {
        f->focus_row = f->row_count - 1;
    }
    return 1;
}

int
tiku_trk_find_from_query(tiku_trk_find_t *f, const tiku_query_t *q)
{
    int i, k, all = 1;

    if (f == NULL || q == NULL) {
        return 0;
    }
    {
        /* The places survive the reset: the shell supplies them from what
         * is mounted, and they are not part of the query. */
        char kept[TIKU_TRK_FIND_PLACES_MAX][TIKU_NAME_MAX];
        uint64_t kept_dev[TIKU_TRK_FIND_PLACES_MAX];
        char kept_volume[TIKU_TRK_FIND_PLACES_MAX][TIKU_NAME_MAX];
        int nkept = f->place_count, k;
        /* The store and the type database survive too: they belong to the
         * panel, not to the query. */
        struct tiku_store *store = f->store;
        const tiku_trk_types_t *types = f->types;

        for (k = 0; k < nkept; k++) {
            memcpy(kept[k], f->place[k], TIKU_NAME_MAX);
            kept_dev[k] = f->place_dev[k];
            memcpy(kept_volume[k], f->place_volume[k], TIKU_NAME_MAX);
        }
        tiku_trk_find_init(f);
        for (k = 0; k < nkept; k++) {
            memcpy(f->place[k], kept[k], TIKU_NAME_MAX);
            f->place_dev[k] = kept_dev[k];
            memcpy(f->place_volume[k], kept_volume[k], TIKU_NAME_MAX);
        }
        f->place_count = nkept;
        f->store = store;
        f->types = types;
    }
    f->mode = q->mode;
    f->row_count = 0;
    for (i = 0; i < q->term_count && i < TIKU_TRK_FIND_ROWS_MAX; i++) {
        f->row[i].field = q->term[i].field;
        f->row[i].op = q->term[i].op;
        snprintf(f->row[i].value, sizeof f->row[i].value, "%s",
                 q->term[i].value);
        /* A term's join is stored on the row before it, where its control
         * sits, as tiku_trk_find_build() reads it. */
        if (i > 0) {
            f->row[i - 1].join = q->term[i].join;
        }
        f->row_count++;
    }
    if (f->row_count == 0) {
        f->row_count = 1;       /* the panel always shows one row */
    }
    snprintf(f->type, sizeof f->type, "%s", q->type);
    if (q->type[0] != '\0' && f->row_count > 0) {
        /* The type term, which tiku_trk_find_build() appends last, belongs
         * to the type control, so it is not restored as a row. */
        int last = f->row_count - 1;

        if (f->row[last].field == TIKU_QF_TYPE) {
            f->row_count = (last > 0) ? last : 1;
            if (last == 0) {
                memset(&f->row[0], 0, sizeof f->row[0]);
                f->row[0].field = TIKU_QF_NAME;
                f->row[0].op = TIKU_OP_CONTAINS;
            }
        }
    }
    if (q->mode == TIKU_QMODE_NAME && q->term_count > 0) {
        /* A by-name query's first term holds the typed text, which the name
         * field shows. */
        snprintf(f->name_text, sizeof f->name_text, "%s", q->term[0].value);
    }
    if (q->mode == TIKU_QMODE_FORMULA) {
        (void)tiku_query_predicate(q, f->formula, sizeof f->formula);
    }
    f->include_trash = q->include_trash;
    f->everywhere = (q->scope_count == 0);
    f->place_mask = 0u;
    for (i = 0; i < q->scope_count; i++) {
        int found = 0;

        for (k = 0; k < f->place_count; k++) {
            char path[TIKU_PATH_MAX];

            /* The scope list stores each path with a trailing '/', so the
             * place is compared in that form. */
            snprintf(path, sizeof path, "/%s/", f->place[k]);
            if (strcmp(path, q->scope[i]) == 0) {
                f->place_mask |= (1u << k);
                f->place_dev[k] = q->scope_was[i].dev;
                snprintf(f->place_volume[k], sizeof f->place_volume[k],
                         "%s", q->scope_was[i].volume);
                found = 1;
                break;
            }
        }
        if (!found) {
            /* A scope this panel does not offer -- a disk that has gone, or
             * a place another session knew -- is reported in the return
             * value. */
            all = 0;
        }
    }
    if (f->place_mask == 0u && q->scope_count > 0 && !all) {
        f->everywhere = 1;
    }
    return all;
}

void
tiku_trk_find_build(const tiku_trk_find_t *f, tiku_query_t *q)
{
    int i;

    if (f == NULL || q == NULL) {
        return;
    }
    tiku_query_init(q);
    q->mode = f->mode;
    switch (f->mode) {
    case TIKU_QMODE_NAME:
        tiku_query_set_name(q, f->name_text);
        break;
    case TIKU_QMODE_ATTRIBUTE:
        for (i = 0; i < f->row_count; i++) {
            /* The join that reaches a row is stored on the row before it,
             * where its control sits. */
            (void)tiku_query_add_term(q, f->row[i].field, f->row[i].op,
                                          f->row[i].value,
                                          (i == 0) ? TIKU_JOIN_AND
                                                   : f->row[i - 1].join);
        }
        break;
    default:
        /* Formula mode runs the formula field.  A malformed predicate
         * marks the query invalid, and the shell refuses to run it. */
        (void)tiku_query_set_formula(q, f->formula);
        break;
    }
    q->include_trash = f->include_trash;
    if (f->type[0] != '\0' && f->mode != TIKU_QMODE_FORMULA) {
        /* ANDed onto everything the rows ask, and recorded on the query, so
         * a saved query comes back with its type (Q-004). */
        snprintf(q->type, sizeof q->type, "%s", f->type);
        (void)tiku_query_add_term(q, TIKU_QF_TYPE, TIKU_OP_IS,
                                      f->type, TIKU_JOIN_AND);
    }
    if (!f->everywhere) {
        for (i = 0; i < f->place_count; i++) {
            if (f->place_mask & (1u << i)) {
                char path[TIKU_PATH_MAX];

                snprintf(path, sizeof path, "/%s", f->place[i]);
                if (tiku_query_add_scope(q, path)) {
                    int at = q->scope_count - 1;

                    q->scope_was[at].dev = f->place_dev[i];
                    memcpy(q->scope_was[at].path, q->scope[at],
                           sizeof q->scope_was[at].path);
                    snprintf(q->scope_was[at].volume,
                             sizeof q->scope_was[at].volume, "%s",
                             f->place_volume[i]);
                }
            }
        }
    }
}

int
tiku_trk_find_scope_label(const tiku_trk_find_t *f, char *out, size_t max)
{
    int i, n = 0, last = -1;

    if (out == NULL || max == 0u) {
        return 0;
    }
    if (f == NULL || f->everywhere) {
        snprintf(out, max, "%s", tiku_str("find.everywhere"));
        return (int)strlen(out);
    }
    for (i = 0; i < f->place_count; i++) {
        if (f->place_mask & (1u << i)) {
            n++;
            last = i;
        }
    }
    /* One place is named; several are counted. */
    if (n == 0) {
        snprintf(out, max, "%s", tiku_str("find.everywhere"));
    } else if (n == 1) {
        snprintf(out, max, "/%.*s", (int)max - 2, f->place[last]);
    } else {
        snprintf(out, max, "%d places", n);
    }
    return (int)strlen(out);
}

int
tiku_trk_find_default_name(const tiku_trk_find_t *f, long stamp, char *out,
                           size_t max)
{
    char desc[96];
    char when[40];
    size_t i;
    time_t t = (time_t)stamp;
    struct tm tmv;

    if (out == NULL || max == 0u) {
        return 0;
    }
    switch (f != NULL ? f->mode : TIKU_QMODE_NAME) {
    case TIKU_QMODE_NAME:
        snprintf(desc, sizeof desc, "name contains %.40s", f->name_text);
        break;
    case TIKU_QMODE_ATTRIBUTE: {
        /* Every row is described, joined by the join that reaches it, so two
         * saved queries differing only in their second row do not end up
         * with the same name. */
        size_t o = 0;
        int k;

        desc[0] = '\0';
        for (k = 0; k < f->row_count && o + 1u < sizeof desc; k++) {
            o += (size_t)snprintf(desc + o, sizeof desc - o, "%s%s %s %.20s",
                                  (k == 0) ? ""
                                  : (f->row[k - 1].join == TIKU_JOIN_OR)
                                      ? " or " : " and ",
                                  field_label(f->row[k].field),
                                  op_label(f->row[k].field, f->row[k].op),
                                  f->row[k].value);
        }
        break;
    }
    default:
        snprintf(desc, sizeof desc, "formula");
        break;
    }
    (void)gmtime_r(&t, &tmv);
    (void)strftime(when, sizeof when, " - %b %d, %H:%M:%S", &tmv);
    snprintf(out, max, "%s%s", desc, when);
    /* The name becomes a path component, so separators and control
     * whitespace become '_'. */
    for (i = 0; out[i] != '\0'; i++) {
        if (out[i] == '/' || out[i] == '\\' || out[i] == '\n' ||
            out[i] == '\r' || out[i] == '\t') {
            out[i] = '_';
        }
    }
    return (int)strlen(out);
}

/*---------------------------------------------------------------------------*/
/* The type menu (Q-004, Q-012, Q-015, Q-016)                                */
/*---------------------------------------------------------------------------*/

/** @brief The state-store node and attribute the most-used list is kept
 *         under. */
#define TYPE_SLOT "\x01types"
#define TYPE_ATTR "_trk/types_used"

void
tiku_trk_find_set_store(tiku_trk_find_t *f, struct tiku_store *s)
{
    if (f != NULL) {
        f->store = s;
    }
}

void
tiku_trk_find_set_types(tiku_trk_find_t *f, const tiku_trk_types_t *types)
{
    if (f != NULL) {
        f->types = types;
    }
}

int
tiku_trk_find_type_shown(const tiku_trk_find_t *f)
{
    return (f != NULL && f->mode != TIKU_QMODE_FORMULA);
}

const char *
tiku_trk_find_type_label(const tiku_trk_find_t *f)
{
    const tiku_trk_typerec_t *record;
    int i;
    const char *t, *label;

    if (f == NULL || f->type[0] == '\0') {
        return tiku_str("find.all_files");
    }
    record = tiku_trk_types_exact(f->types, f->type);
    if (record != NULL && record->desc[0] != '\0') {
        return record->desc;
    }
    for (i = 0; (t = tiku_model_type_at(i, &label)) != NULL; i++) {
        if (strcmp(t, f->type) == 0) {
            return label;
        }
    }
    return f->type;             /* one the table does not name */
}

/** @brief Read the most-used list, newest first. */
static int
used_read(struct tiku_store *store, char out[][TIKU_TYPE_MAX],
          int max)
{
    char buf[TIKU_TYPE_MAX * TIKU_TRK_FIND_MOST_USED * 2];
    int n = 0, len;
    char *line, *save;

    if (store == NULL || max <= 0) {
        return 0;
    }
    len = tiku_state_read(store, TYPE_SLOT, TYPE_ATTR, buf,
                              sizeof buf - 1u);
    if (len <= 0) {
        return 0;
    }
    buf[len] = '\0';
    for (line = strtok_r(buf, "\n", &save); line != NULL && n < max;
         line = strtok_r(NULL, "\n", &save)) {
        if (line[0] != '\0') {
            snprintf(out[n++], TIKU_TYPE_MAX, "%.*s",
                     (int)(TIKU_TYPE_MAX - 1), line);
        }
    }
    return n;
}

void
tiku_trk_find_set_type(tiku_trk_find_t *f, const char *type,
                       struct tiku_store *store)
{
    char used[TIKU_TRK_FIND_MOST_USED][TIKU_TYPE_MAX];
    char keep[TIKU_TRK_FIND_MOST_USED][TIKU_TYPE_MAX];
    int n, i, k = 0;

    if (f == NULL) {
        return;
    }
    snprintf(f->type, sizeof f->type, "%s", (type != NULL) ? type : "");
    /* A type change keeps the attribute rows (Q-013). */
    if (store == NULL || f->type[0] == '\0') {
        return;
    }
    n = used_read(store, used, TIKU_TRK_FIND_MOST_USED);
    snprintf(keep[k++], TIKU_TYPE_MAX, "%.*s",
             (int)(TIKU_TYPE_MAX - 1), f->type);
    for (i = 0; i < n && k < TIKU_TRK_FIND_MOST_USED; i++) {
        /* The chosen type moves to the head of the list; the rest keep
         * their order. */
        if (strcmp(used[i], f->type) != 0) {
            memcpy(keep[k++], used[i], TIKU_TYPE_MAX);
        }
    }
    {
        char buf[TIKU_TYPE_MAX * TIKU_TRK_FIND_MOST_USED * 2];
        size_t at = 0u;

        for (i = 0; i < k && at + 1u < sizeof buf; i++) {
            int w = snprintf(buf + at, sizeof buf - at, "%s\n", keep[i]);

            if (w <= 0 || (size_t)w >= sizeof buf - at) {
                break;
            }
            at += (size_t)w;
        }
        (void)tiku_state_write(store, TYPE_SLOT, TYPE_ATTR, buf, at);
    }
}

int
tiku_trk_find_type_menu(struct tiku_store *store, const char **out,
                        int max)
{
    static char used[TIKU_TRK_FIND_MOST_USED][TIKU_TYPE_MAX];
    int n = 0, nu, i, k;
    const char *t;

    if (out == NULL || max <= 0) {
        return 0;
    }
    nu = used_read(store, used, TIKU_TRK_FIND_MOST_USED);
    for (i = 0; i < nu && n < max; i++) {
        out[n++] = used[i];
    }
    /* Then every type in the model's table, minus the ones already at the
     * top. */
    for (i = 0; (t = tiku_model_type_at(i, NULL)) != NULL && n < max;
         i++) {
        int seen = 0;

        for (k = 0; k < nu; k++) {
            if (strcmp(used[k], t) == 0) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            out[n++] = t;
        }
    }
    return n;
}

/** @brief Human label for one type, preferring the installed database. */
static const char *
type_label(const tiku_trk_find_t *f, const char *type)
{
    const tiku_trk_typerec_t *r;
    const char *known, *label;
    int i;

    r = (f != NULL) ? tiku_trk_types_exact(f->types, type) : NULL;
    if (r != NULL && r->desc[0] != '\0') {
        return r->desc;
    }
    for (i = 0; (known = tiku_model_type_at(i, &label)) != NULL; i++) {
        if (strcmp(known, type) == 0) {
            return label;
        }
    }
    return type;
}

/**
 * @brief The menu command for @p type: 100 plus its index among this
 *        opening's choices, the same in a submenu.
 * @return the command, or -1 when the choice table is full.
 */
static int
type_command(tiku_trk_find_t *f, const char *type)
{
    int i;

    for (i = 0; i < f->type_choice_count; i++) {
        if (strcmp(f->type_choice[i], type) == 0) {
            return 100 + i;
        }
    }
    if (f->type_choice_count >= TIKU_TRK_FIND_TYPE_CHOICES) {
        return -1;
    }
    i = f->type_choice_count++;
    snprintf(f->type_choice[i], sizeof f->type_choice[i], "%s", type);
    return 100 + i;
}

/** @brief Whether @p type is among the @p n already listed. */
static int
type_is_used(char used[][TIKU_TYPE_MAX], int n, const char *type)
{
    int i;

    for (i = 0; i < n; i++) {
        if (strcmp(used[i], type) == 0) {
            return 1;
        }
    }
    return 0;
}

/** @brief Whether an exact record belongs below a registered supertype. */
static int
type_has_parent(const tiku_trk_types_t *db, const char *type)
{
    int i;

    for (i = 0; db != NULL && i < db->n; i++) {
        size_t n = strlen(db->rec[i].type);

        if (n > 0u && db->rec[i].type[n - 1u] == '/' &&
            strlen(type) > n && strncmp(type, db->rec[i].type, n) == 0) {
            return 1;
        }
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* layout                                                                    */
/*---------------------------------------------------------------------------*/

/** @brief Rows of controls the current mode needs. */
static int
control_rows(const tiku_trk_find_t *f)
{
    int n;

    switch (f->mode) {
    case TIKU_QMODE_ATTRIBUTE: n = f->row_count; break;
    default:                       n = 1; break;
    }
    /* The type control takes a row of its own when it is shown. */
    return n + (tiku_trk_find_type_shown(f) ? 1 : 0);
}

int
tiku_trk_find_height(const tiku_trk_find_t *f)
{
    if (f == NULL) {
        return 0;
    }
    /* mode row + controls + scope row + buttons */
    return PAD + ROW_H + GAP + control_rows(f) * (ROW_H + GAP) +
           ROW_H + GAP + BTN_H + 8 + PAD;
}

/** @brief Rectangle of the mode selector. */
static tiku_rect_t
mode_rect(tiku_rect_t f)
{
    tiku_rect_t r = { f.x + PAD + LABEL_W, f.y + PAD, FIELD_W, ROW_H };

    return r;
}

/** @brief Rectangle of control row @p i. */
static tiku_rect_t
row_rect(tiku_rect_t f, int i)
{
    tiku_rect_t r;

    r.x = f.x + PAD;
    r.y = f.y + PAD + ROW_H + GAP + i * (ROW_H + GAP);
    r.w = f.w - 2 * PAD;
    r.h = ROW_H;
    return r;
}

/** @brief Rectangle of the type control, the row above the scope. */
static tiku_rect_t
type_rect(const tiku_trk_find_t *f, tiku_rect_t fr)
{
    tiku_rect_t r;
    int rows = control_rows(f) - 1;     /* its own row is the last one */

    r.x = fr.x + PAD + LABEL_W;
    r.y = fr.y + PAD + ROW_H + GAP + rows * (ROW_H + GAP);
    r.w = FIELD_W + OP_W;
    r.h = ROW_H;
    return r;
}

/** @brief Where the scope field sits, under the last control row. */
static tiku_rect_t
scope_rect(const tiku_trk_find_t *f, tiku_rect_t fr)
{
    tiku_rect_t r;

    r.x = fr.x + PAD + LABEL_W;
    r.y = fr.y + PAD + ROW_H + GAP + control_rows(f) * (ROW_H + GAP);
    r.w = FIELD_W + OP_W;
    r.h = ROW_H;
    return r;
}

/** @brief The Include Trash option shares the scope row. */
static tiku_rect_t
trash_rect(const tiku_trk_find_t *f, tiku_rect_t fr)
{
    tiku_rect_t r = scope_rect(f, fr);

    r.x = fr.x + fr.w - PAD - 116;
    r.w = 116;
    return r;
}

/**
 * @brief Where button @p which sits on the bottom row; the default button,
 *        0, is 8 pixels wider and taller.
 */
static tiku_rect_t
button_rect(const tiku_trk_find_t *f, tiku_rect_t fr, int which)
{
    tiku_rect_t r;
    int w = BTN_W + ((which == 0) ? 8 : 0);

    r.y = fr.y + PAD + ROW_H + GAP + control_rows(f) * (ROW_H + GAP) +
          ROW_H + GAP;
    r.h = BTN_H + ((which == 0) ? 8 : 0);
    r.w = w;
    /* The default button sits at the right. */
    if (which == 0) {
        r.x = fr.x + fr.w - PAD - w;
    } else {
        /* The others go leftward from it, in the order they are
         * numbered. */
        r.x = fr.x + fr.w - PAD - (BTN_W + 8) - which * (GAP + w);
        r.y += 4;                 /* centred against the taller default   */
    }
    return r;
}

/*---------------------------------------------------------------------------*/
/* drawing                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief A menu field, from the shared control look. */
static void
draw_popup_button(tiku_surface_t *s, tiku_rect_t r,
                  const char *label)
{
    tiku_ui_menufield(s, r, label, TIKU_S_NORMAL);
}

/** @brief A text field, with the caret when it has the focus. */
static void
draw_field(tiku_surface_t *s, tiku_rect_t r, const char *text,
           int focused)
{
    /* The shared text control draws the caret. */
    tiku_ui_textfield(s, r, (text != NULL) ? text : "",
                           focused ? (int)strlen((text != NULL) ? text : "")
                                   : -1,
                           focused ? TIKU_S_FOCUS : TIKU_S_NORMAL);
}

/** @brief A button, from the shared control look. */
static void
draw_button(tiku_surface_t *s, tiku_rect_t r, const char *label,
            unsigned state)
{
    tiku_ui_button(s, r, label, state);
}

/** @brief @p text at @p x, centred on row @p r's height. */
static void
label_at(tiku_surface_t *s, int x, tiku_rect_t r, const char *text)
{
    const tiku_font_t *font = tiku_font_plain();

    tiku_text(s, font, x, r.y + (r.h - font->height) / 2 + font->ascent,
                   text, TIKU_C_TEXT);
}

tiku_rect_t
tiku_trk_find_icon_rect(const tiku_trk_find_t *f, tiku_rect_t frame)
{
    tiku_rect_t none = { 0, 0, 0, 0 };
    tiku_rect_t r = { frame.x + frame.w - PAD - 32, frame.y + PAD,
                           32, 32 };

    if (f == NULL || f->edit_template) {
        return none;
    }
    return r;
}

void
tiku_trk_find_draw(tiku_trk_find_t *f, tiku_surface_t *s,
                   tiku_rect_t frame)
{
    static const char *kModes[] = { "by name", "by attribute", "by formula" };
    tiku_rect_t r;
    char buf[TIKU_PATH_MAX];
    int i;

    if (f == NULL || s == NULL) {
        return;
    }
    tiku_fill(s, frame, TIKU_C_PANEL);

    r = mode_rect(frame);
    label_at(s, frame.x + PAD, r, tiku_str("find.find"));
    draw_popup_button(s, r, kModes[f->mode]);

    if (f->mode == TIKU_QMODE_NAME) {
        r = row_rect(frame, 0);
        label_at(s, r.x, r, tiku_str("find.name_contains"));
        r.x += LABEL_W + 40;
        r.w = VALUE_W + OP_W;
        r.y += (ROW_H - TEXT_H) / 2;
        r.h = TEXT_H;
        draw_field(s, r, f->name_text,
                   f->has_keyboard &&
                   f->focus == TIKU_TRK_FIND_FOCUS_NAME);
    } else if (f->mode == TIKU_QMODE_FORMULA) {
        r = row_rect(frame, 0);
        label_at(s, r.x, r, tiku_str("find.formula"));
        r.x += LABEL_W;
        r.w = frame.w - 2 * PAD - LABEL_W;
        r.y += (ROW_H - TEXT_H) / 2;
        r.h = TEXT_H;
        draw_field(s, r, f->formula,
                   f->has_keyboard &&
                   f->focus == TIKU_TRK_FIND_FOCUS_FORMULA);
    } else {
        for (i = 0; i < f->row_count; i++) {
            tiku_rect_t rr = row_rect(frame, i);
            tiku_rect_t c = rr;

            c.w = FIELD_W;
            draw_popup_button(s, c, field_label(f->row[i].field));
            c.x += FIELD_W + GAP;
            c.w = OP_W;
            draw_popup_button(s, c,
                              op_label(f->row[i].field, f->row[i].op));
            c.x += OP_W + GAP;
            c.w = VALUE_W;
            c.y += (ROW_H - TEXT_H) / 2;
            c.h = TEXT_H;
            /* The caret is gated at draw time only: f->focus survives the
             * window losing the keyboard, and names the field keys go into
             * after re-activation. */
            draw_field(s, c, f->row[i].value,
                       f->has_keyboard &&
                       f->focus == TIKU_TRK_FIND_FOCUS_ROW &&
                       f->focus_row == i);
            /* The and/or belongs to the gap between this row and the next,
             * so the last row does not have one. */
            if (i + 1 < f->row_count) {
                c.x += VALUE_W + GAP;
                c.w = JOIN_W;
                c.y = rr.y;
                c.h = ROW_H;
                draw_popup_button(s, c,
                                  (f->row[i].join == TIKU_JOIN_OR)
                                      ? "or" : "and");
            }
        }
    }

    if (tiku_trk_find_type_shown(f)) {
        r = type_rect(f, frame);
        label_at(s, frame.x + PAD, r, tiku_str("find.of_type"));
        draw_popup_button(s, r, tiku_trk_find_type_label(f));
    }
    r = scope_rect(f, frame);
    label_at(s, frame.x + PAD, r, tiku_str("find.search_in"));
    (void)tiku_trk_find_scope_label(f, buf, sizeof buf);
    draw_popup_button(s, r, buf);
    tiku_ui_checkbox(s, trash_rect(f, frame), tiku_str("find.include_trash"),
                          f->include_trash ? TIKU_S_ON : 0u);

    if (f->mode == TIKU_QMODE_ATTRIBUTE) {
        tiku_rect_t a = scope_rect(f, frame);

        a.x += a.w + GAP * 2;
        a.w = 26;
        a.h = ROW_H;
        draw_button(s, a, "+",
                    (f->row_count >= TIKU_TRK_FIND_ROWS_MAX)
                        ? TIKU_S_DISABLED : TIKU_S_NORMAL);
        a.x += a.w + GAP;
        /* Remove is disabled, not absent, while one row remains, so the
         * controls do not move. */
        draw_button(s, a, "-",
                    (f->row_count <= 1) ? TIKU_S_DISABLED
                                        : TIKU_S_NORMAL);
    }

    draw_button(s, button_rect(f, frame, 0),
                tiku_trk_find_primary_is_save(f) ? tiku_str("find.save")
                                                 : tiku_str("find.search"),
                f->has_keyboard ? TIKU_S_DEFAULT
                                : TIKU_S_NORMAL);
    /* Always enabled: it saves back to the attached record, or asks where
     * through the save panel (AW-052). */
    draw_button(s, button_rect(f, frame, 1), "Save", TIKU_S_NORMAL);
    draw_button(s, button_rect(f, frame, 2), "Queries", TIKU_S_NORMAL);

    if (f->popup_for != TIKU_TRK_FIND_HIT_NONE) {
        tiku_menu_draw(&f->popup, s, f->popup_frame);
        if (f->popup_sub != NULL) {
            tiku_menu_draw(f->popup_sub, s, f->popup_sub_frame);
        }
    }
}

/*---------------------------------------------------------------------------*/
/* hit-testing and clicks                                                    */
/*---------------------------------------------------------------------------*/

/** @brief Whether the point is inside the rectangle. */
static int
in(tiku_rect_t r, int x, int y)
{
    return (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h);
}

tiku_trk_find_hit_t
tiku_trk_find_hit(const tiku_trk_find_t *f, tiku_rect_t frame, int x,
                  int y, int *row)
{
    tiku_rect_t r;
    int i;

    if (row != NULL) {
        *row = -1;
    }
    if (f == NULL) {
        return TIKU_TRK_FIND_HIT_NONE;
    }
    if (in(mode_rect(frame), x, y)) {
        return TIKU_TRK_FIND_HIT_MODE;
    }
    if (in(button_rect(f, frame, 0), x, y)) {
        return TIKU_TRK_FIND_HIT_SEARCH;
    }
    if (in(button_rect(f, frame, 1), x, y)) {
        return TIKU_TRK_FIND_HIT_SAVE;
    }
    if (in(button_rect(f, frame, 2), x, y)) {
        return TIKU_TRK_FIND_HIT_QUERIES;
    }
    if (tiku_trk_find_type_shown(f) &&
        in(type_rect(f, frame), x, y)) {
        return TIKU_TRK_FIND_HIT_TYPE;
    }
    if (in(trash_rect(f, frame), x, y)) {
        return TIKU_TRK_FIND_HIT_TRASH;
    }
    r = scope_rect(f, frame);
    if (in(r, x, y)) {
        return TIKU_TRK_FIND_HIT_SCOPE;
    }
    if (f->mode == TIKU_QMODE_ATTRIBUTE) {
        tiku_rect_t a = r;

        a.x += a.w + GAP * 2;
        a.w = 26;
        if (in(a, x, y)) {
            return TIKU_TRK_FIND_HIT_ADD;
        }
        a.x += a.w + GAP;
        if (in(a, x, y)) {
            return TIKU_TRK_FIND_HIT_REMOVE;
        }
    }
    if (f->mode == TIKU_QMODE_NAME) {
        tiku_rect_t v = row_rect(frame, 0);

        v.x += LABEL_W + 40;
        v.w = VALUE_W + OP_W;
        return in(v, x, y) ? TIKU_TRK_FIND_HIT_VALUE : TIKU_TRK_FIND_HIT_NONE;
    }
    if (f->mode == TIKU_QMODE_FORMULA) {
        tiku_rect_t v = row_rect(frame, 0);

        v.x += LABEL_W;
        v.w = frame.w - 2 * PAD - LABEL_W;
        return in(v, x, y) ? TIKU_TRK_FIND_HIT_VALUE : TIKU_TRK_FIND_HIT_NONE;
    }
    for (i = 0; i < f->row_count; i++) {
        tiku_rect_t rr = row_rect(frame, i);
        tiku_rect_t c = rr;

        c.w = FIELD_W;
        if (in(c, x, y)) {
            if (row != NULL) { *row = i; }
            return TIKU_TRK_FIND_HIT_FIELD;
        }
        c.x += FIELD_W + GAP;
        c.w = OP_W;
        if (in(c, x, y)) {
            if (row != NULL) { *row = i; }
            return TIKU_TRK_FIND_HIT_OP;
        }
        c.x += OP_W + GAP;
        c.w = VALUE_W;
        if (in(c, x, y)) {
            if (row != NULL) { *row = i; }
            return TIKU_TRK_FIND_HIT_VALUE;
        }
        if (i + 1 < f->row_count) {
            c.x += VALUE_W + GAP;
            c.w = JOIN_W;
            if (in(c, x, y)) {
                if (row != NULL) { *row = i; }
                return TIKU_TRK_FIND_HIT_JOIN;
            }
        }
    }
    return TIKU_TRK_FIND_HIT_NONE;
}

/** @brief Build the popup a control owns and place it under that control. */
static void
open_popup(tiku_trk_find_t *f, tiku_trk_find_hit_t what, int row,
           tiku_rect_t under, tiku_rect_t screen)
{
    tiku_menu_t *m = &f->popup;
    int i;

    tiku_menu_clear(m);
    f->popup_sub = NULL;
    f->type_sub_count = 0;
    f->type_choice_count = 0;
    switch (what) {
    case TIKU_TRK_FIND_HIT_MODE:
        (void)tiku_menu_add(m, "by name", 0, 0, 0u, 1);
        (void)tiku_menu_add(m, "by attribute", 1, 0, 0u, 1);
        (void)tiku_menu_add(m, "by formula", 2, 0, 0u, 1);
        m->item[f->mode].marked = 1;
        break;
    case TIKU_TRK_FIND_HIT_FIELD:
        for (i = 0; i < NFIELDS; i++) {
            int at;

            if (!field_in_menu(f, kFields[i].field)) {
                continue;
            }
            at = tiku_menu_add(m, tiku_str(kFields[i].label), i, 0,
                                   0u, 1);
            if (kFields[i].field == f->row[row].field) {
                m->item[at].marked = 1;
            }
        }
        break;
    case TIKU_TRK_FIND_HIT_OP: {
        /* Only the operators the field offers are listed. */
        unsigned ops = field_ops(f->row[row].field);

        for (i = 0; i < 7; i++) {
            if ((ops & (1u << i)) == 0u) {
                continue;
            }
            (void)tiku_menu_add(m,
                                    op_label(f->row[row].field,
                                             (tiku_qop_t)i),
                                    i, 0, 0u, 1);
            if ((tiku_qop_t)i == f->row[row].op) {
                m->item[m->count - 1].marked = 1;
            }
        }
        break;
    }
    case TIKU_TRK_FIND_HIT_JOIN:
        (void)tiku_menu_add(m, "and", 0, 0, 0u, 1);
        (void)tiku_menu_add(m, "or", 1, 0, 0u, 1);
        m->item[f->row[row].join].marked = 1;
        break;
    case TIKU_TRK_FIND_HIT_QUERIES: {
        char names[TIKU_QUERY_LIST_MAX][128];
        int n;

        (void)tiku_menu_add(m, tiku_str("find.save_as_template"), 0, 0,
                                0u, 1);
        /* The templates first, then the recent queries (Q-032). */
        n = tiku_query_list_kind(f->store, TIKU_QREC_TEMPLATE,
                                     names, TIKU_QUERY_LIST_MAX);
        if (n > 0) {
            (void)tiku_menu_add_separator(m);
            for (i = 0; i < n; i++) {
                (void)tiku_menu_add(m, names[i], 100 + i, 0, 0u, 1);
            }
        }
        n = tiku_query_list_kind(f->store, TIKU_QREC_QUERY, names,
                                     TIKU_TRK_FIND_RECENT_MAX);
        if (n > 0) {
            (void)tiku_menu_add_separator(m);
            for (i = 0; i < n; i++) {
                (void)tiku_menu_add(m, names[i], 200 + i, 0, 0u, 1);
            }
        }
        /* Last, so offering it does not move the entries above. */
        if (f->loaded_kind == TIKU_QREC_TEMPLATE &&
            f->loaded_name[0] != '\0' && !f->edit_template) {
            (void)tiku_menu_add_separator(m);
            (void)tiku_menu_add(m, tiku_str("find.edit_loaded_template"), 1,
                                    0, 0u,
                                    1);
        }
        break;
    }
    case TIKU_TRK_FIND_HIT_SCOPE:
        (void)tiku_menu_add(m, "Everywhere", 0, 0, 0u, 1);
        m->item[0].marked = f->everywhere;
        (void)tiku_menu_add_separator(m);
        for (i = 0; i < f->place_count; i++) {
            char lbl[TIKU_NAME_MAX + 2];

            snprintf(lbl, sizeof lbl, "/%s", f->place[i]);
            (void)tiku_menu_add(m, lbl, 100 + i, 0, 0u, 1);
            if (!f->everywhere && (f->place_mask & (1u << i))) {
                m->item[m->count - 1].marked = 1;
            }
        }
        (void)tiku_menu_add_separator(m);
        /* Any folder at all, not only the roots the startup scan found:
         * the chooser is the file panel (Q-019). */
        (void)tiku_menu_add(m, tiku_str("find.select_folders"), 99, 0,
                                0u, 1);
        break;
    case TIKU_TRK_FIND_HIT_TYPE:
        open_type_popup(f, m);
        break;
    default:
        return;
    }
    f->popup_for = what;
    f->popup_row = row;
    f->popup_frame = tiku_menu_place(m, under.x, under.y + under.h,
                                         screen);
}

int
tiku_trk_find_load_named(tiku_trk_find_t *f, const char *name)
{
    tiku_query_t q;
    int whole;

    if (f == NULL || f->store == NULL || name == NULL) {
        return -1;
    }
    if (tiku_query_load(&q, name, f->store) != 0) {
        snprintf(f->note, sizeof f->note, "no query called %.40s", name);
        return -1;
    }
    whole = tiku_trk_find_from_query(f, &q);
    snprintf(f->loaded_name, sizeof f->loaded_name, "%s", name);
    f->loaded_kind = q.kind;
    f->edit_template = 0;
    snprintf(f->note, sizeof f->note, "%s %.30s%s",
             (q.kind == TIKU_QREC_TEMPLATE) ? "template" : "query",
             name, whole ? "" : " -- a place is no longer here");
    return whole ? 1 : 0;
}

int
tiku_trk_find_save_template(tiku_trk_find_t *f, const char *name,
                            int64_t now)
{
    tiku_query_t q;

    if (f == NULL || f->store == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }
    tiku_trk_find_build(f, &q);
    q.kind = TIKU_QREC_TEMPLATE;
    /* A template is never temporary, so the sweep that reaps unnamed
     * searches leaves it (AW-090). */
    q.temporary = 0;
    q.saved_at = now;
    if (tiku_query_save(&q, name, f->store) != 0) {
        snprintf(f->note, sizeof f->note, "that template cannot be saved");
        return -1;
    }
    snprintf(f->note, sizeof f->note, "template %.50s", name);
    return 0;
}

int
tiku_trk_find_edit_template(tiku_trk_find_t *f, const char *name)
{
    tiku_query_t q;
    char attached[128];
    int whole;

    if (f == NULL || f->store == NULL || name == NULL ||
        tiku_query_load(&q, name, f->store) != 0 ||
        q.kind != TIKU_QREC_TEMPLATE) {
        return -1;
    }
    /* The menu passes f->loaded_name itself, which
     * tiku_trk_find_from_query() clears, so the name is copied first. */
    snprintf(attached, sizeof attached, "%s", name);
    whole = tiku_trk_find_from_query(f, &q);
    snprintf(f->loaded_name, sizeof f->loaded_name, "%s", attached);
    f->loaded_kind = q.kind;
    f->edit_template = 1;
    snprintf(f->note, sizeof f->note, "editing template %.40s", attached);
    return whole ? 1 : 0;
}

int
tiku_trk_find_save_current(tiku_trk_find_t *f, int64_t now)
{
    tiku_query_t q;

    if (f == NULL || f->store == NULL || f->loaded_name[0] == '\0' ||
        (f->loaded_kind == TIKU_QREC_TEMPLATE && !f->edit_template)) {
        return -1;
    }
    tiku_trk_find_build(f, &q);
    q.kind = f->loaded_kind;
    q.temporary = 0;
    q.saved_at = now;
    if (tiku_query_save(&q, f->loaded_name, f->store) != 0) {
        snprintf(f->note, sizeof f->note, "that record cannot be saved");
        return -1;
    }
    snprintf(f->note, sizeof f->note, "saved %.50s", f->loaded_name);
    return 0;
}

int
tiku_trk_find_editing_template(const tiku_trk_find_t *f)
{
    return f != NULL && f->edit_template;
}

int
tiku_trk_find_open_default(tiku_trk_find_t *f, int64_t now)
{
    tiku_query_t q;
    int whole;

    if (f == NULL || f->store == NULL) {
        return -1;
    }
    if (tiku_query_load(&q, "default", f->store) != 0) {
        tiku_trk_find_build(f, &q);
        q.kind = TIKU_QREC_TEMPLATE;
        q.temporary = 0;
        q.saved_at = now;
        if (tiku_query_save(&q, "default", f->store) != 0) {
            return -1;
        }
    }
    whole = tiku_trk_find_from_query(f, &q);
    /* The default is a starting shape, not an opened record: nothing is
     * attached, so Save asks where to save. */
    f->loaded_name[0] = '\0';
    f->loaded_kind = TIKU_QREC_QUERY;
    f->edit_template = 0;
    snprintf(f->note, sizeof f->note, "default template%s",
             whole ? "" : " -- a place is no longer here");
    return whole ? 1 : 0;
}

int
tiku_trk_find_can_save_current(const tiku_trk_find_t *f)
{
    return f != NULL && f->loaded_name[0] != '\0';
}

int
tiku_trk_find_primary_is_save(const tiku_trk_find_t *f)
{
    return tiku_trk_find_editing_template(f);
}

int
tiku_trk_find_popup_open(const tiku_trk_find_t *f)
{
    return (f != NULL && f->popup_for != TIKU_TRK_FIND_HIT_NONE) ? 1 : 0;
}

const char *
tiku_trk_find_note(const tiku_trk_find_t *f)
{
    return (f != NULL) ? f->note : "";
}

void
tiku_trk_find_clear_note(tiku_trk_find_t *f)
{
    if (f != NULL) {
        f->note[0] = '\0';
    }
}

tiku_trk_find_hit_t
tiku_trk_find_click(tiku_trk_find_t *f, tiku_rect_t frame, int x, int y)
{
    tiku_rect_t screen = { 0, 0, 4096, 4096 };
    tiku_trk_find_hit_t what;
    int row = -1;

    if (f == NULL) {
        return TIKU_TRK_FIND_HIT_NONE;
    }
    /* A showing popup eats the click, whether it lands on an item or not. */
    if (f->popup_for != TIKU_TRK_FIND_HIT_NONE) {
        tiku_menu_t *picked_menu = &f->popup;
        int pick = -1;

        if (f->popup_sub != NULL) {
            pick = tiku_menu_item_at(f->popup_sub, f->popup_sub_frame,
                                         x, y);
            if (pick >= 0) {
                picked_menu = f->popup_sub;
            }
        }
        if (pick < 0) {
            picked_menu = &f->popup;
            pick = tiku_menu_item_at(&f->popup, f->popup_frame, x, y);
        }

        /* Only enabled items act: a supertype whose members are all in
         * the most-used list is added greyed, with an empty submenu. */
        if (pick >= 0 && picked_menu->item[pick].enabled) {
            int cmd = picked_menu->item[pick].command;

            if (picked_menu == &f->popup &&
                f->popup.item[pick].submenu != NULL) {
                f->popup_sub = f->popup.item[pick].submenu;
                f->popup.open_index = pick;
                f->popup_sub_frame = tiku_menu_place_sub(
                    &f->popup, pick, f->popup_frame, screen);
                return TIKU_TRK_FIND_HIT_NONE;
            }

            switch (f->popup_for) {
            case TIKU_TRK_FIND_HIT_MODE:
                tiku_trk_find_set_mode(f, (tiku_qmode_t)cmd);
                break;
            case TIKU_TRK_FIND_HIT_FIELD:
                f->row[f->popup_row].field = kFields[cmd].field;
                /* An operator the new field does not offer falls back to
                 * the field's starting operator. */
                if ((kFields[cmd].ops &
                     (1u << (unsigned)f->row[f->popup_row].op)) == 0u) {
                    f->row[f->popup_row].op = kFields[cmd].first;
                }
                break;
            case TIKU_TRK_FIND_HIT_OP:
                f->row[f->popup_row].op = (tiku_qop_t)cmd;
                break;
            case TIKU_TRK_FIND_HIT_JOIN:
                f->row[f->popup_row].join = (tiku_qjoin_t)cmd;
                break;
            case TIKU_TRK_FIND_HIT_SCOPE:
                if (cmd == 99) {
                    /* The shell opens the file panel and calls back with
                     * the folder (Q-019). */
                    f->wants_folder = 1;
                } else if (cmd == 0) {
                    /* Choosing everywhere clears every individual place, as
                     * "All disks" clears every volume mark. */
                    f->everywhere = 1;
                    f->place_mask = 0u;
                } else {
                    f->everywhere = 0;
                    f->place_mask ^= 1u << (unsigned)(cmd - 100);
                    if (f->place_mask == 0u) {
                        f->everywhere = 1;
                    }
                }
                break;
            case TIKU_TRK_FIND_HIT_QUERIES: {
                char names[TIKU_QUERY_LIST_MAX][128];

                if (cmd == 0) {
                    char nm[128];

                    (void)tiku_trk_find_default_name(f, (long)time(NULL),
                                                     nm, sizeof nm);
                    (void)tiku_trk_find_save_template(f, nm,
                                                      (int64_t)time(NULL));
                } else if (cmd == 1 &&
                           f->loaded_kind == TIKU_QREC_TEMPLATE) {
                    (void)tiku_trk_find_edit_template(f, f->loaded_name);
                } else if (cmd >= 200) {
                    int n = tiku_query_list_kind(f->store,
                                TIKU_QREC_QUERY, names,
                                TIKU_QUERY_LIST_MAX);

                    if (cmd - 200 < n) {
                        (void)tiku_trk_find_load_named(f, names[cmd - 200]);
                    }
                } else if (cmd >= 100) {
                    int n = tiku_query_list_kind(f->store,
                                TIKU_QREC_TEMPLATE, names,
                                TIKU_QUERY_LIST_MAX);

                    if (cmd - 100 < n) {
                        (void)tiku_trk_find_load_named(f, names[cmd - 100]);
                    }
                }
                break;
            }
            case TIKU_TRK_FIND_HIT_TYPE:
                if (cmd == 0) {
                    tiku_trk_find_set_type(f, "", f->store);
                } else {
                    int at = cmd - 100;

                    if (at >= 0 && at < f->type_choice_count) {
                        tiku_trk_find_set_type(f, f->type_choice[at],
                                               f->store);
                    }
                }
                break;
            default:
                break;
            }
        }
        f->popup_for = TIKU_TRK_FIND_HIT_NONE;
        f->popup_sub = NULL;
        return TIKU_TRK_FIND_HIT_NONE;
    }

    what = tiku_trk_find_hit(f, frame, x, y, &row);
    switch (what) {
    case TIKU_TRK_FIND_HIT_QUERIES:
        open_popup(f, what, 0, button_rect(f, frame, 2), screen);
        break;
    case TIKU_TRK_FIND_HIT_MODE:
        open_popup(f, what, 0, mode_rect(frame), screen);
        break;
    case TIKU_TRK_FIND_HIT_SCOPE:
        open_popup(f, what, 0, scope_rect(f, frame), screen);
        break;
    case TIKU_TRK_FIND_HIT_TYPE:
        open_popup(f, what, 0, type_rect(f, frame), screen);
        break;
    case TIKU_TRK_FIND_HIT_FIELD: {
        tiku_rect_t c = row_rect(frame, row);

        c.w = FIELD_W;
        open_popup(f, what, row, c, screen);
        break;
    }
    case TIKU_TRK_FIND_HIT_OP: {
        tiku_rect_t c = row_rect(frame, row);

        c.x += FIELD_W + GAP;
        c.w = OP_W;
        open_popup(f, what, row, c, screen);
        break;
    }
    case TIKU_TRK_FIND_HIT_JOIN: {
        tiku_rect_t c = row_rect(frame, row);

        c.x += FIELD_W + GAP + OP_W + GAP + VALUE_W + GAP;
        c.w = JOIN_W;
        open_popup(f, what, row, c, screen);
        break;
    }
    case TIKU_TRK_FIND_HIT_VALUE:
        if (f->mode == TIKU_QMODE_NAME) {
            f->focus = TIKU_TRK_FIND_FOCUS_NAME;
        } else if (f->mode == TIKU_QMODE_FORMULA) {
            f->focus = TIKU_TRK_FIND_FOCUS_FORMULA;
        } else {
            f->focus = TIKU_TRK_FIND_FOCUS_ROW;
            f->focus_row = row;
        }
        break;
    case TIKU_TRK_FIND_HIT_ADD:
        (void)tiku_trk_find_add_row(f);
        break;
    case TIKU_TRK_FIND_HIT_REMOVE:
        (void)tiku_trk_find_remove_row(f);
        break;
    case TIKU_TRK_FIND_HIT_TRASH:
        f->include_trash = !f->include_trash;
        break;
    default:
        break;
    }
    return what;
}

/** @brief The field the caret is in, or NULL. */
static char *
focused_field(tiku_trk_find_t *f, size_t *max)
{
    switch (f->focus) {
    case TIKU_TRK_FIND_FOCUS_NAME:
        *max = sizeof f->name_text;
        return f->name_text;
    case TIKU_TRK_FIND_FOCUS_FORMULA:
        *max = sizeof f->formula;
        return f->formula;
    case TIKU_TRK_FIND_FOCUS_ROW:
        if (f->focus_row >= 0 && f->focus_row < f->row_count) {
            *max = sizeof f->row[0].value;
            return f->row[f->focus_row].value;
        }
        return NULL;
    default:
        return NULL;
    }
}

void
tiku_trk_find_key(tiku_trk_find_t *f, char ch)
{
    size_t max = 0;
    char *field;
    size_t n;

    if (f == NULL || ch < 0x20 || ch >= 0x7F) {
        return;
    }
    field = focused_field(f, &max);
    if (field == NULL) {
        return;
    }
    n = strlen(field);
    if (n + 1u < max) {
        field[n] = ch;
        field[n + 1u] = '\0';
    }
}

void
tiku_trk_find_backspace(tiku_trk_find_t *f)
{
    size_t max = 0;
    char *field;

    if (f == NULL) {
        return;
    }
    field = focused_field(f, &max);
    if (field == NULL) {
        return;
    }
    tiku_text_input_backspace(field, max);
}

int tiku_trk_find_text(tiku_trk_find_t *f, const char *text)
{
    size_t max = 0;
    if (!f || tiku_trk_find_popup_open(f)) return 0;
    char *field = focused_field(f, &max);
    return field && tiku_text_input_append(field, max, text);
}

/**
 * @brief The type popup: All files and folders, the most-used types, then
 *        the type database's supertypes, each with a submenu of its
 *        members, then its other types and the model's own; without a
 *        database, the model's table.
 */
static void
open_type_popup(tiku_trk_find_t *f, tiku_menu_t *m)
{
    int i;

    char used[TIKU_TRK_FIND_MOST_USED][TIKU_TYPE_MAX];
    int nu = used_read(f->store, used, TIKU_TRK_FIND_MOST_USED);

    (void)tiku_menu_add(m, tiku_str("find.all_files"), 0, 0, 0u, 1);
    m->item[0].marked = (f->type[0] == '\0');
    (void)tiku_menu_add_separator(m);
    for (i = 0; i < nu; i++) {
        int cmd = type_command(f, used[i]);
        int at = tiku_menu_add(m, type_label(f, used[i]), cmd,
                                   0, 0u, cmd >= 0);

        if (at >= 0 && strcmp(f->type, used[i]) == 0) {
            m->item[at].marked = 1;
        }
    }
    if (nu > 0) {
        (void)tiku_menu_add_separator(m);
    }
    if (f->types != NULL) {
        /* A supertype with no member is omitted, and the most-used types
         * are not repeated in the tree. */
        for (i = 0; i < f->types->n; i++) {
            const tiku_trk_typerec_t *super = &f->types->rec[i];
            size_t sn = strlen(super->type);
            tiku_menu_t *sub;
            int k, children = 0;

            if (sn == 0u || super->type[sn - 1u] != '/') {
                continue;
            }
            for (k = 0; k < f->types->n; k++) {
                const char *child = f->types->rec[k].type;

                if (strlen(child) > sn &&
                    strncmp(child, super->type, sn) == 0) {
                    children++;
                }
            }
            if (children == 0 ||
                f->type_sub_count >= TIKU_TRK_FIND_TYPE_SUPERS) {
                continue;
            }
            sub = &f->type_sub[f->type_sub_count++];
            tiku_menu_clear(sub);
            if (!type_is_used(used, nu, super->type)) {
                char all[TIKU_MENU_LABEL_MAX];
                int cmd = type_command(f, super->type);
                int at;

                snprintf(all, sizeof all, "All %s",
                         type_label(f, super->type));
                at = tiku_menu_add(sub, all, cmd, 0, 0u, cmd >= 0);
                if (at >= 0 && strcmp(f->type, super->type) == 0) {
                    sub->item[at].marked = 1;
                }
            }
            for (k = 0; k < f->types->n; k++) {
                const tiku_trk_typerec_t *child = &f->types->rec[k];
                int cmd, at;

                if (strlen(child->type) <= sn ||
                    strncmp(child->type, super->type, sn) != 0 ||
                    type_is_used(used, nu, child->type)) {
                    continue;
                }
                cmd = type_command(f, child->type);
                at = tiku_menu_add(sub, type_label(f, child->type),
                                       cmd, 0, 0u, cmd >= 0);
                if (at >= 0 && strcmp(f->type, child->type) == 0) {
                    sub->item[at].marked = 1;
                }
            }
            (void)tiku_menu_add_submenu(m,
                                            type_label(f, super->type),
                                            sub, sub->count > 0);
        }
        for (i = 0; i < f->types->n; i++) {
            const char *type = f->types->rec[i].type;
            size_t n = strlen(type);
            int cmd, at;

            if ((n > 0u && type[n - 1u] == '/') ||
                type_has_parent(f->types, type) ||
                type_is_used(used, nu, type)) {
                continue;
            }
            cmd = type_command(f, type);
            at = tiku_menu_add(m, type_label(f, type), cmd, 0, 0u,
                                   cmd >= 0);
            if (at >= 0 && strcmp(f->type, type) == 0) {
                m->item[at].marked = 1;
            }
        }
        /* The model's own types, such as Folder and Volume, are listed
         * when the type database has no record for them. */
        for (i = 0; tiku_model_type_at(i, NULL) != NULL; i++) {
            const char *type = tiku_model_type_at(i, NULL);
            int cmd, at;

            if (tiku_trk_types_exact(f->types, type) != NULL ||
                type_is_used(used, nu, type)) {
                continue;
            }
            cmd = type_command(f, type);
            at = tiku_menu_add(m, type_label(f, type), cmd, 0, 0u,
                                   cmd >= 0);
            if (at >= 0 && strcmp(f->type, type) == 0) {
                m->item[at].marked = 1;
            }
        }
    } else {
        const char *types[24];
        int n = tiku_trk_find_type_menu(f->store, types, 24);

        for (i = nu; i < n; i++) {
            int cmd = type_command(f, types[i]);
            int at = tiku_menu_add(m, type_label(f, types[i]), cmd,
                                       0, 0u, cmd >= 0);

            if (at >= 0 && strcmp(f->type, types[i]) == 0) {
                m->item[at].marked = 1;
            }
        }
    }
}
