/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_doc.c - reading a help page, and fitting it to a width.
 *
 * Reading cooks the page into one text arena and the tables of blocks,
 * runs, links, anchors and images; laying out walks the blocks for one
 * width and fills the lines and pieces.  Words are measured one at a time
 * in their own run's face, so a paragraph mixes plain, bold and code, and
 * a word too wide for its box is broken between code points.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_doc.h"

#include <string.h>

/* The default caps fit the tables' int32_t indices; caller-sized storage
 * is checked when it is bound. */
_Static_assert(TIKU_DOC_BLOCKS_MAX  < INT32_MAX, "blocks index is int32_t");
_Static_assert(TIKU_DOC_RUNS_MAX    < INT32_MAX, "runs index is int32_t");
_Static_assert(TIKU_DOC_LINES_MAX   < INT32_MAX, "lines index is int32_t");
_Static_assert(TIKU_DOC_PIECES_MAX  < INT32_MAX, "pieces index is int32_t");
_Static_assert(TIKU_DOC_LINKS_MAX   < INT32_MAX, "links index is int32_t");
_Static_assert(TIKU_DOC_BLOCKS_MAX  > 1, "a page needs room for a notice");
_Static_assert(TIKU_DOC_TEXT_MAX    <= INT32_MAX / 4, "text range is bounded");
_Static_assert(TIKU_DOC_COLS_MAX >= 1 && TIKU_DOC_COLS_MAX <= 255,
               "column capacity fits cell indices");

/* Scratch for measuring a word, a stretch at a time. */
#define DOC_WORD_MAX 256

/* The bits that mean a table filled and earn a notice; FACE_FLAT, NARROW
 * and COLS_FOLDED report how the page was fitted and are not among them. */
#define DOC_FULL_CAPS (TIKU_DOC_FULL_TEXT | TIKU_DOC_FULL_BLOCKS | \
                       TIKU_DOC_FULL_RUNS | TIKU_DOC_FULL_LINES | \
                       TIKU_DOC_FULL_PIECES | TIKU_DOC_FULL_LINKS | \
                       TIKU_DOC_FULL_ANCHORS | TIKU_DOC_FULL_IMAGES | \
                       TIKU_DOC_FULL_DEPTH | TIKU_DOC_FULL_HEIGHT)

/*---------------------------------------------------------------------------*/
/* THE TEXT ARENA                                                            */
/*---------------------------------------------------------------------------*/

/** @brief How many bytes this codepoint takes, valid or not. */
static int utf8_len(const char *s)
{
    unsigned char c = (unsigned char)*s;
    int n = c < 0x80 ? 1 : c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc2 ? 2 : 1;
    for (int i = 1; i < n; i++) {
        if (((unsigned char)s[i] & 0xc0) != 0x80) return 1;
    }
    return n;
}

/** @brief Room for @p n more bytes and a terminator. */
static int arena_room(tiku_doc_t *d, int n)
{
    if (n >= 0 && n < d->limits.text - d->ntext) return 1;
    d->full |= TIKU_DOC_FULL_TEXT;
    return 0;
}

/** @brief Append byte @p c, when there is room. */
static void arena_byte(tiku_doc_t *d, char c)
{
    if (arena_room(d, 1)) d->text[d->ntext++] = c;
}

/*---------------------------------------------------------------------------*/
/* ENTITIES                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Write @p cp as UTF-8 into @p out.  @return bytes written. */
static int utf8_put(unsigned cp, char *out)
{
    if (cp < 0x80u) { out[0] = (char)cp; return 1; }
    if (cp < 0x800u) {
        out[0] = (char)(0xc0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 63u));
        return 2;
    }
    if (cp < 0x10000u) {
        out[0] = (char)(0xe0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 63u));
        out[2] = (char)(0x80u | (cp & 63u));
        return 3;
    }
    out[0] = (char)(0xf0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 63u));
    out[2] = (char)(0x80u | ((cp >> 6) & 63u));
    out[3] = (char)(0x80u | (cp & 63u));
    return 4;
}

/**
 * @brief One entity, starting just past the '&'.
 *
 * An entity this dialect does not know keeps its '&' and reads as
 * itself.  &nbsp; becomes an ordinary space: U+00A0 is two bytes, a board
 * advances a space for each, and the two ends would lay the page out
 * differently.
 *
 * @return bytes written into @p out; @p after lands past the entity.
 */
static int entity(const char *p, char *out, const char **after)
{
    const char *semi = p;
    unsigned cp = 0;
    int n;

    while (*semi && *semi != ';' && semi - p < 10) semi++;
    if (*semi != ';') { *after = p; out[0] = '&'; return 1; }
    n = (int)(semi - p);
    if (p[0] == '#') {
        const char *at = p + 1;
        int hex = (*at == 'x' || *at == 'X');

        if (hex) at++;
        if (at == semi) { *after = p; out[0] = '&'; return 1; }
        for (; at < semi; at++) {
            int v = *at >= '0' && *at <= '9' ? *at - '0' :
                    hex && (*at | 32) >= 'a' && (*at | 32) <= 'f'
                        ? (*at | 32) - 'a' + 10 : -1;
            if (v < 0) { *after = p; out[0] = '&'; return 1; }
            unsigned base = hex ? 16u : 10u;
            if (cp > (0x10ffffu - (unsigned)v) / base) {
                *after = p; out[0] = '&'; return 1;
            }
            cp = cp * base + (unsigned)v;
        }
        if (!cp || (cp >= 0xd800u && cp <= 0xdfffu)) {
            *after = p; out[0] = '&'; return 1;
        }
    } else if (n == 3 && !memcmp(p, "amp", 3)) {
        cp = '&';
    } else if (n == 2 && !memcmp(p, "lt", 2)) {
        cp = '<';
    } else if (n == 2 && !memcmp(p, "gt", 2)) {
        cp = '>';
    } else if (n == 4 && !memcmp(p, "quot", 4)) {
        cp = '"';
    } else if (n == 4 && !memcmp(p, "apos", 4)) {
        cp = '\'';
    } else if (n == 4 && !memcmp(p, "nbsp", 4)) {
        cp = ' ';
    } else {
        *after = p;
        out[0] = '&';
        return 1;
    }
    *after = semi + 1;
    return utf8_put(cp, out);
}

/*---------------------------------------------------------------------------*/
/* READING                                                                   */
/*---------------------------------------------------------------------------*/

enum {
    T_UNKNOWN = 0, T_P, T_H1, T_H2, T_H3, T_UL, T_OL, T_LI, T_PRE,
    T_QUOTE, T_HR, T_IMG, T_A, T_B, T_I, T_CODE, T_BR, T_SKIP,
    T_TABLE, T_TR, T_TH, T_TD, T_H4, T_H5, T_H6, T_CONTAINER, T_DL, T_DT, T_DD,
    T_U, T_S, T_FIGURE, T_CAPTION
};

#include "tiku_doc_html.inl"
#include "tiku_doc_css.inl"

/** @brief The element @p name is, or T_UNKNOWN. */
static int tag_of(const char *name, int n)
{
    static const struct { const char *name; int n, tag; } k[] = {
        {"p", 1, T_P}, {"h1", 2, T_H1}, {"h2", 2, T_H2}, {"h3", 2, T_H3},
        {"ul", 2, T_UL}, {"ol", 2, T_OL}, {"li", 2, T_LI},
        {"pre", 3, T_PRE}, {"blockquote", 10, T_QUOTE}, {"hr", 2, T_HR},
        {"img", 3, T_IMG}, {"a", 1, T_A}, {"b", 1, T_B}, {"strong", 6, T_B},
        {"i", 1, T_I}, {"em", 2, T_I}, {"code", 4, T_CODE}, {"br", 2, T_BR},
        {"script", 6, T_SKIP}, {"style", 5, T_SKIP}, {"head", 4, T_SKIP},
        {"title", 5, T_SKIP},
        {"h4", 2, T_H4}, {"h5", 2, T_H5}, {"h6", 2, T_H6},
        {"div", 3, T_CONTAINER}, {"section", 7, T_CONTAINER},
        {"article", 7, T_CONTAINER}, {"header", 6, T_CONTAINER},
        {"footer", 6, T_CONTAINER}, {"main", 4, T_CONTAINER},
        {"nav", 3, T_CONTAINER}, {"aside", 5, T_CONTAINER},
        {"figure", 6, T_FIGURE}, {"figcaption", 10, T_CAPTION}, {"caption", 7, T_CAPTION},
        {"dl", 2, T_DL}, {"dt", 2, T_DT}, {"dd", 2, T_DD},
        {"u", 1, T_U}, {"s", 1, T_S}, {"strike", 6, T_S}, {"del", 3, T_S},
        {"table", 5, T_TABLE}, {"tr", 2, T_TR}, {"th", 2, T_TH},
        {"td", 2, T_TD}
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        if (html_name(name, (size_t)n, k[i].name)) return k[i].tag;
    }
    return T_UNKNOWN;
}

/** @brief Everything the reader carries while it walks the page. */
typedef struct {
    tiku_doc_t *d;
    int face, ink, link;        /* the style a run would take now       */
    int flags;                  /* TIKU_DOC_RUN_* for the next run      */
    int block;                  /* the block being filled, or NONE      */
    int run;                    /* the run being filled, or NONE        */
    int pending;                /* a space is owed before the next word */
    int quote;                  /* inside a <blockquote>                */
    int definition;             /* nested definition-list containers    */
    int definition_item[TIKU_DOC_DEPTH_MAX];
    int table;                  /* inside a <table>                     */
    int row, col;               /* the cell the next <td> would be      */
    int occupied[TIKU_DOC_COLS_MAX]; /**< rows reserved in each table column */
    int occupied_row[TIKU_DOC_COLS_MAX]; /**< source row that reserved each column */
    int depth;                  /* how many lists are open              */
    int ordered[TIKU_DOC_DEPTH_MAX];
    int number[TIKU_DOC_DEPTH_MAX];
    struct { int tag, face, ink, link; } stack[TIKU_DOC_DEPTH_MAX];
    int nstack;
    uint32_t color;
    uint32_t background;
    tiku_doc_font_t font;
    tiku_doc_flow_t flow;
    tiku_doc_box_t box;
    unsigned box_mask;
    int hidden, display;
    doc_css_t css;
    struct {
        const char *name;
        size_t length;
        uint32_t color;
        unsigned flags;
        tiku_doc_font_t font;
        tiku_doc_flow_t flow;
        tiku_doc_box_t box;
        uint32_t background;
        unsigned box_mask;
        int hidden, display, element_flags, box_id;
    } css_stack[TIKU_DOC_DEPTH_MAX];
    int css_depth;
    int css_overflow;
} read_t;

/** @brief The paragraph kind within the current quote or definition item. */
static int flow_kind(const read_t *r)
{
    if (r->definition && r->definition_item[r->definition - 1] == T_DD)
        return TIKU_DOC_BLOCK_DEFINITION;
    return r->quote ? TIKU_DOC_BLOCK_QUOTE : TIKU_DOC_BLOCK_P;
}

/** @brief End the run being filled; an empty one is taken back. */
static void run_close(read_t *r)
{
    tiku_doc_t *d = r->d;

    if (r->run == TIKU_DOC_NONE) return;
    d->run[r->run].len = (int32_t)(d->ntext - d->run[r->run].off);
    if (d->run[r->run].len == 0) {
        /* Nothing was put in it, so it is not a run. */
        d->nrun--;
        d->ntext = d->run[r->run].off;
    } else {
        arena_byte(d, 0);
    }
    r->run = TIKU_DOC_NONE;
}

#include "tiku_doc_font.inl"
#include "tiku_doc_flow.inl"

static void block_end(read_t *r);
/** @brief Close a container's descendant range. */
static void box_end(read_t *r, int block);

#include "tiku_doc_box_style.inl"
#include "tiku_doc_style.inl"

/** @brief Start a run in the style now in force.
 *  @return 1 when there is one. */
static int run_open(read_t *r)
{
    tiku_doc_t *d = r->d;

    if (r->run != TIKU_DOC_NONE) return 1;
    if (r->block == TIKU_DOC_NONE) return 0;
    if (d->nrun == d->limits.runs) { d->full |= TIKU_DOC_FULL_RUNS; return 0; }
    if (!arena_room(d, 1)) return 0;
    tiku_doc_run_t *run = &d->run[d->nrun];
    memset(run, 0, sizeof *run);
    run->off = d->ntext;
    run->face = (uint16_t)r->face;
    run->ink = (uint8_t)r->ink;
    run->link = (int32_t)r->link;
    run->flags = (uint8_t)r->flags;
    run->color = r->color;
    run->background = r->background;
    run->font = r->font;
    run->flow = r->flow;
    r->run = d->nrun++;
    return 1;
}

/** @brief A change of face, ink or link ends the run it changed. */
static void style_changed(read_t *r)
{
    tiku_doc_t *d = r->d;

    if (r->run == TIKU_DOC_NONE) return;
    if (d->run[r->run].face == (uint16_t)r->face &&
        d->run[r->run].ink == (uint8_t)r->ink &&
        d->run[r->run].link == (int32_t)r->link) return;
    run_close(r);
}

/** @brief End the block being filled; one with no runs is taken back,
 *         unless it is a rule, an image or a cell. */
static void block_end(read_t *r)
{
    tiku_doc_t *d = r->d;

    run_close(r);
    if (r->block == TIKU_DOC_NONE) return;
    tiku_doc_block_t *b = &d->block[r->block];
    b->nrun = (int32_t)(d->nrun - b->run0);
    if (!b->nrun && b->kind != TIKU_DOC_BLOCK_RULE &&
        b->kind != TIKU_DOC_BLOCK_IMAGE &&
        b->kind != TIKU_DOC_BLOCK_BOX &&
        b->kind != TIKU_DOC_BLOCK_CELL) {
        d->nblock--;            /* an empty block is taken back */
    }
    r->block = TIKU_DOC_NONE;
    r->pending = 0;
}

/** @brief Start a block.  @return 1 when there was room for one. */
static int block_begin(read_t *r, int kind, int depth, int index)
{
    tiku_doc_t *d = r->d;

    block_end(r);
    /* The last slot is kept for the notice, so a page that fills the
     * table can still say that it did. */
    if (d->nblock >= d->limits.blocks - 1) {
        d->full |= TIKU_DOC_FULL_BLOCKS;
        return 0;
    }
    tiku_doc_block_t *b = &d->block[d->nblock];
    memset(b, 0, sizeof *b);
    b->kind = (uint8_t)kind;
    b->depth = (uint8_t)depth;
    b->index = (int32_t)index;
    b->image = TIKU_DOC_NONE;
    b->run0 = (int32_t)d->nrun;
    b->font = r->font;
    b->flow = r->flow;
    b->box = r->box;
    b->parent = TIKU_DOC_NONE;
    for (int i = r->css_depth-1; i >= 0; i--)
        if (r->css_stack[i].box_id >= 0) { b->parent = r->css_stack[i].box_id; break; }
    if (kind != TIKU_DOC_BLOCK_BOX && r->css_depth &&
        r->css_stack[r->css_depth-1].box_id >= 0) b->flags |= TIKU_DOC_BLOCK_BOX_CHILD;
    r->block = d->nblock++;
    r->pending = 0;
    return 1;
}

/** @brief Start a container node and leave its text to descendant blocks. */
static void box_begin(read_t *r, int tag)
{
    if (!block_begin(r,TIKU_DOC_BLOCK_BOX,0,0)) return;
    int id = r->block;
    if (tag == T_FIGURE) r->d->block[id].flags |= TIKU_DOC_BLOCK_FIGURE;
    if (tag == T_TABLE) r->d->block[id].flags |= TIKU_DOC_BLOCK_TABLE;
    r->d->block[id].end = id+1;
    r->css_stack[r->css_depth-1].box_id = id;
    r->block = TIKU_DOC_NONE;
}

static void box_end(read_t *r, int block)
{
    block_end(r);
    r->d->block[block].end = r->d->nblock;
    if (r->d->block[block].flags & TIKU_DOC_BLOCK_TABLE)
        r->d->block[block].index = r->row+1;
}

/** @brief Put one word's worth of bytes in, opening a run if need be. */
static void text_add(read_t *r, const char *s, int n)
{
    tiku_doc_t *d = r->d;

    if (n <= 0) return;
    if (r->block == TIKU_DOC_NONE && !block_begin(r, flow_kind(r), r->definition, 0)) return;
    if (!run_open(r)) return;
    if (r->pending) {
        tiku_doc_run_t *run = &d->run[r->run];

        r->pending = 0;
        if (d->ntext > run->off) {
            arena_byte(d, ' ');
        } else if (r->run > d->block[r->block].run0 &&
                   (run - 1)->link == TIKU_DOC_NONE && arena_room(d, 1)) {
            /* The space goes to the words before it, unless they are a
             * link, so a link's underline stops at its last word. */
            tiku_doc_run_t *prev = run - 1;

            d->text[prev->off + prev->len] = ' ';
            prev->len++;
            d->text[d->ntext++] = 0;
            run->off = d->ntext;
        } else if (r->run > d->block[r->block].run0) {
            arena_byte(d, ' ');
        }
    }
    if (!arena_room(d, n)) return;
    memcpy(d->text + d->ntext, s, (size_t)n);
    d->ntext += n;
}

/** @brief Attribute @p name of the tag at @p p, entities decoded,
 *         copied into the arena.
 *  @return its offset in the text, or TIKU_DOC_NONE. */
static int attr(const char *p, const char *name, tiku_doc_t *d)
{
    const char *value;
    size_t length;
    if (!html_attribute(p, name, &value, &length)) return TIKU_DOC_NONE;
    const char *end = value + length;
    int bytes = 0;
    for (const char *c = value; c < end;) {
        char out[8];
        const char *after = c + 1;
        int n = *c == '&' ? html_value_entity(c, end, out, &after) : 1;
        if (n >= d->limits.text - bytes) {
            d->full |= TIKU_DOC_FULL_TEXT;
            return TIKU_DOC_NONE;
        }
        bytes += n; c = after;
    }
    if (!arena_room(d, bytes)) return TIKU_DOC_NONE;
    int at = d->ntext;
    for (const char *c = value; c < end;) {
        if (*c == '&') {
            char out[8]; const char *after;
            int n = html_value_entity(c, end, out, &after);
            memcpy(d->text + d->ntext, out, (size_t)n);
            d->ntext += n; c = after;
        } else {
            d->text[d->ntext++] = *c++;
        }
    }
    d->text[d->ntext++] = 0;
    return at;
}

/** @brief Read a signed decimal attribute, or its fallback on invalid input. */
static int attr_num(const char *p, const char *name, int fallback)
{
    const char *value;
    size_t length;
    if (!html_attribute(p, name, &value, &length)) return fallback;
    const char *end = value + length;
    while (value < end && html_space(*value)) value++;
    int negative = value < end && *value == '-';
    if (value < end && (*value == '-' || *value == '+')) value++;
    const char *first = value;
    uint32_t v = 0, limit = negative ? 2147483648u : 2147483647u;
    while (value < end && *value >= '0' && *value <= '9') {
        unsigned digit = (unsigned)(*value++ - '0');
        if (v > (limit - digit) / 10u) return fallback;
        v = v * 10u + digit;
    }
    if (value == first) return fallback;
    while (value < end && html_space(*value)) value++;
    if (value != end) return fallback;
    return negative ? (int)(-(int64_t)v) : (int)v;
}

/** @brief Remember a name a link may reach. */
static void anchor_add(read_t *r, const char *p, int block)
{
    tiku_doc_t *d = r->d;
    const char *value;
    size_t length;
    if (!html_attribute(p, "id", &value, &length) || !length) return;
    run_close(r);
    int id = attr(p, "id", d);

    if (id == TIKU_DOC_NONE || !d->text[id]) return;
    if (d->nanchor == d->limits.anchors) {
        d->full |= TIKU_DOC_FULL_ANCHORS;
        return;
    }
    d->anchor[d->nanchor].id = id;
    d->anchor[d->nanchor].block = (int32_t)block;
    d->anchor[d->nanchor].spare = 0;
    d->nanchor++;
}

/** @brief A list item's marker as a run of its own: its number and a
 *         dot when @p ordered, else a dash. */
static void marker_add(read_t *r, int ordered, int number)
{
    char text[16];
    int n;

    if (ordered) {
        unsigned v = number < 0 ? (unsigned)(-(int64_t)number) : (unsigned)number;
        int digits = 0, i, sign = number < 0;
        char tmp[10];

        do { tmp[digits++] = (char)('0' + v % 10); v /= 10; }
        while (v && digits < (int)sizeof tmp);
        if (sign) text[0] = '-';
        for (i = 0; i < digits; i++) text[sign + i] = tmp[digits - 1 - i];
        text[sign + digits] = '.';
        n = sign + digits + 1;
    } else {
        text[0] = '-';
        n = 1;
    }
    int face = r->face, ink = r->ink, link = r->link, flags = r->flags;
    r->face = TIKU_MEASURE_PLAIN;
    r->ink = TIKU_DOC_INK_PLAIN;
    r->link = TIKU_DOC_NONE;
    r->flags = TIKU_DOC_RUN_MARKER | (flags & TIKU_DOC_RUN_COLOR);
    run_close(r);
    if (run_open(r)) {
        if (arena_room(r->d, n)) {
            memcpy(r->d->text + r->d->ntext, text, (size_t)n);
            r->d->ntext += n;
        }
    }
    run_close(r);
    r->face = face; r->ink = ink; r->link = link; r->flags = flags;
}

/** @brief Close the innermost open @p tag: restore the style it saved
 *         and take it off the stack. */
static void stack_pop(read_t *r, int tag)
{
    int at = r->nstack - 1;

    while (at >= 0 && r->stack[at].tag != tag) at--;
    if (at < 0) return;
    style_changed(r);
    r->face = r->stack[at].face;
    r->ink = r->stack[at].ink;
    r->link = r->stack[at].link;
    run_close(r);
    for (int i = at; i < r->nstack - 1; i++) r->stack[i] = r->stack[i + 1];
    r->nstack--;
}

/** @brief Open @p tag, saving the style in force; past
 *         TIKU_DOC_DEPTH_MAX it is not kept and the depth cap is
 *         marked. */
static void stack_push(read_t *r, int tag)
{
    if (r->nstack == TIKU_DOC_DEPTH_MAX) {
        r->d->full |= TIKU_DOC_FULL_DEPTH;
        return;
    }
    r->stack[r->nstack].tag = tag;
    r->stack[r->nstack].face = r->face;
    r->stack[r->nstack].ink = r->ink;
    r->stack[r->nstack].link = r->link;
    r->nstack++;
}

/** @brief An <img>: an image block keeping its source and alt text, the
 *         alt text as its words in the faint ink. */
static void image_add(read_t *r, const char *p)
{
    tiku_doc_t *d = r->d;
    int src, alt;

    if (!block_begin(r, TIKU_DOC_BLOCK_IMAGE, 0, 0)) return;
    anchor_add(r, p, r->block);
    src = attr(p, "src", d);
    alt = attr(p, "alt", d);
    if (d->nimage == d->limits.images) {
        d->full |= TIKU_DOC_FULL_IMAGES;
    } else {
        d->image[d->nimage].src = src;
        d->image[d->nimage].alt = alt;
        d->image[d->nimage].w = 0;
        d->image[d->nimage].h = 0;
        d->image[d->nimage].block = (int32_t)r->block;
        d->image[d->nimage].spare = 0;
        tiku_doc_image_t *im = &d->image[d->nimage];
        im->width = r->box.width; im->height = r->box.height; im->max_width = r->box.max_width;
        int w = attr_num(p,"width",0), h = attr_num(p,"height",0);
        if (!(r->box_mask & DOC_CSS_WIDTH) && w > 0 && w <= 4096)
            im->width = (tiku_doc_length_t){w*DOC_FONT_ONE,TIKU_DOC_PX};
        if (!(r->box_mask & DOC_CSS_IMAGE_HEIGHT) && h > 0 && h <= 4096)
            im->height = (tiku_doc_length_t){h*DOC_FONT_ONE,TIKU_DOC_PX};
        d->block[r->block].image = (int32_t)d->nimage++;
    }
    if (alt != TIKU_DOC_NONE) {
        int ink = r->ink;

        r->ink = TIKU_DOC_INK_FAINT;
        text_add(r, d->text + alt, (int)strlen(d->text + alt));
        r->ink = ink;
    }
    block_end(r);
}

/** @brief Apply structural containers, definitions and headings when matched. */
static int tag_structure(read_t *r, int tag, int close)
{
    switch (tag) {
    case T_CONTAINER:
    case T_FIGURE:
        block_end(r);
        break;
    case T_CAPTION:
        if (close) block_end(r);
        else (void)block_begin(r,TIKU_DOC_BLOCK_CAPTION,0,0);
        break;
    case T_DL:
        block_end(r);
        if (close) {
            stack_pop(r, T_DT);
            stack_pop(r, T_DD);
            if (r->definition) r->definition--;
        }
        else if (r->definition < TIKU_DOC_DEPTH_MAX)
            r->definition_item[r->definition++] = 0;
        else r->d->full |= TIKU_DOC_FULL_DEPTH;
        break;
    case T_DT: case T_DD:
        if (close) {
            block_end(r);
            stack_pop(r, tag);
            if (r->definition) r->definition_item[r->definition - 1] = 0;
        } else {
            block_end(r);
            stack_pop(r, T_DT);
            stack_pop(r, T_DD);
            if (block_begin(r, tag == T_DT ? TIKU_DOC_BLOCK_P : TIKU_DOC_BLOCK_DEFINITION,
                            r->definition ? r->definition : 1, 0)) {
                stack_push(r, tag);
                if (tag == T_DT) r->face = TIKU_MEASURE_BOLD;
                if (r->definition) r->definition_item[r->definition - 1] = tag;
            }
        }
        break;
    case T_H1: case T_H2: case T_H3: case T_H4: case T_H5: case T_H6: {
        int kind = tag <= T_H3 ? TIKU_DOC_BLOCK_H1 + (tag - T_H1) :
                                TIKU_DOC_BLOCK_H4 + (tag - T_H4);

        if (close) {
            block_end(r);
            r->face = TIKU_MEASURE_PLAIN;
            break;
        }
        if (!block_begin(r, kind, 0, 0)) break;
        /* A heading's words are the bold face, which is the face they
         * are measured in as well as drawn in. */
        r->face = TIKU_MEASURE_BOLD;
        break;
    }
    default: return 0;
    }
    return 1;
}

/** @brief One tag, already bounded by its angles.  @return past it. */
static const char *tag_do(read_t *r, const char *p, const char **skip_to)
{
    const char *body = p + 1;
    int close = *body == '/';
    const char *name;
    int n, tag;

    if (close) body++;
    name = body;
    while (*body && !html_space(*body) && *body != '>' && *body != '/' &&
           *body != '<') body++;
    n = (int)(body - name);
    tag = n ? tag_of(name, n) : T_UNKNOWN;
    const char *end = html_tag_end(body);
    if (end == body || end[-1] != '>') return end;
    int element = css_element(r,tag,close,name,(size_t)n,body);
    if (element & DOC_ELEMENT_IGNORED) return end;
    if (r->hidden || (element & DOC_ELEMENT_HIDDEN)) {
        if (tag == T_SKIP && !close) *skip_to = name;
        if (element & DOC_ELEMENT_TRANSIENT) css_restore(r,r->css_depth-1);
        return end;
    }
    int inline_block = (element & DOC_ELEMENT_INLINE) && css_native_block(tag) &&
        tag != T_TABLE && tag != T_TR && tag != T_TH && tag != T_TD &&
        tag != T_IMG && tag != T_HR;
    if (!close && !inline_block && tag != T_TH && tag != T_TD && tag != T_TR && tag != T_IMG &&
        ((element & DOC_ELEMENT_BLOCK) || tag == T_FIGURE || tag == T_TABLE ||
         (css_native_block(tag) && r->box_mask))) box_begin(r,tag);

    if (!inline_block && !tag_structure(r, tag, close)) switch (tag) {
    case T_SKIP:
        if (!close) *skip_to = name;
        break;
    case T_P:
        if (close) block_end(r);
        else (void)block_begin(r, flow_kind(r), r->definition, 0);
        break;
    case T_UL: case T_OL:
        block_end(r);
        if (close) {
            if (r->depth) r->depth--;
        } else if (r->depth < TIKU_DOC_DEPTH_MAX) {
            r->ordered[r->depth] = tag == T_OL;
            r->number[r->depth] = tag == T_OL ? attr_num(body, "start", 1) : 1;
            r->depth++;
        } else {
            r->d->full |= TIKU_DOC_FULL_DEPTH;
        }
        break;
    case T_LI: {
        int at = r->depth ? r->depth - 1 : 0;

        if (close) { block_end(r); break; }
        int number = r->ordered[at] ? attr_num(body, "value", r->number[at]) : 0;
        if (!block_begin(r, TIKU_DOC_BLOCK_ITEM, r->depth ? r->depth : 1, number)) break;
        if (r->ordered[at]) r->d->block[r->block].flags |= TIKU_DOC_BLOCK_ORDERED;
        marker_add(r, r->ordered[at], number);
        if (r->ordered[at]) r->number[at] = number < INT32_MAX ? number + 1 : number;
        break;
    }
    case T_PRE:
        if (close) {
            r->face = TIKU_MEASURE_PLAIN;
            block_end(r);
        } else if (block_begin(r, TIKU_DOC_BLOCK_PRE, 0, 0)) {
            r->face = TIKU_MEASURE_MONO;
        }
        break;
    case T_QUOTE:
        block_end(r);
        if (close) { if (r->quote) r->quote--; }
        else if (r->quote < TIKU_DOC_DEPTH_MAX) r->quote++;
        else r->d->full |= TIKU_DOC_FULL_DEPTH;
        break;
    case T_TABLE:
        block_end(r);
        if (!close && r->table) r->css.notices |= TIKU_DOC_TABLE_FALLBACK;
        r->table = !close;
        r->row = -1;
        r->col = 0;
        memset(r->occupied,0,sizeof r->occupied);
        break;
    case T_TR:
        block_end(r);
        if (!close) {
            if (!r->table) { r->table = 1; r->row = -1; }
            if (r->row < INT32_MAX-1) r->row++;
            for (int c = 0; c < TIKU_DOC_COLS_MAX; c++)
                if (r->occupied[c]) r->occupied[c]--;
            r->col = 0;
        }
        break;
    case T_TH: case T_TD: {
        int span, rows;

        if (close) {
            block_end(r);
            r->face = TIKU_MEASURE_PLAIN;
            break;
        }
        /* A cell met outside a table opens one, so that its words are
         * kept. */
        if (!r->table) { r->table = 1; r->row = 0; r->col = 0; }
        if (r->row < 0) r->row = 0;
        while (r->col < TIKU_DOC_COLS_MAX && r->occupied[r->col]) r->col++;
        span = attr_num(body, "colspan", 1);
        if (span < 1) span = 1;
        if (r->col >= TIKU_DOC_COLS_MAX) {
            /* A cell past the last column is put in the last one. */
            r->d->full |= TIKU_DOC_COLS_FOLDED;
            span = 1;
            for (;;) {
                r->col = TIKU_DOC_COLS_MAX-1;
                while (r->col >= 0 && r->occupied[r->col] &&
                       !(r->occupied[r->col] == 1 && r->occupied_row[r->col] == r->row)) r->col--;
                if (r->col >= 0) break;
                if (r->row < INT32_MAX-1) r->row++;
                for (int c = 0; c < TIKU_DOC_COLS_MAX; c++)
                    if (r->occupied[c]) r->occupied[c]--;
                r->css.notices |= TIKU_DOC_TABLE_FALLBACK;
            }
        }
        if (span > TIKU_DOC_COLS_MAX - r->col) {
            r->d->full |= TIKU_DOC_COLS_FOLDED;
            span = TIKU_DOC_COLS_MAX - r->col;
        }
        for (int c = 1; c < span; c++) if (r->occupied[r->col+c]) {
            span = c; r->css.notices |= TIKU_DOC_TABLE_FALLBACK; break;
        }
        rows = attr_num(body,"rowspan",1);
        if (rows < 1 || rows > TIKU_DOC_ROWSPAN_MAX) {
            rows = rows < 1 ? 1 : TIKU_DOC_ROWSPAN_MAX;
            r->css.notices |= TIKU_DOC_TABLE_FALLBACK;
        }
        if (!block_begin(r, TIKU_DOC_BLOCK_CELL, 0, r->row)) break;
        r->d->block[r->block].col = (uint8_t)r->col;
        r->d->block[r->block].span = (uint8_t)span;
        r->d->block[r->block].rowspan = (uint8_t)rows;
        for (int c = 0; c < span; c++) {
            r->occupied[r->col+c] = rows; r->occupied_row[r->col+c] = r->row;
        }
        r->col += span;
        if (tag == T_TH) {
            r->d->block[r->block].flags |= TIKU_DOC_BLOCK_HEADER;
            r->face = TIKU_MEASURE_BOLD;
        }
        break;
    }
    case T_HR:
        if (!close && block_begin(r, TIKU_DOC_BLOCK_RULE, 0, 0)) {
            anchor_add(r, body, r->block);
            block_end(r);
        }
        break;
    case T_IMG:
        if (!close) image_add(r, body);
        break;
    case T_A:
        if (close) { stack_pop(r, T_A); break; }
        /* An attribute is written into the same arena as the words, so
         * the run has to be closed before one is read or the address
         * lands inside the sentence. */
        run_close(r);
        stack_push(r, T_A);
        int href = attr(body, "href", r->d);
        if (href == TIKU_DOC_NONE) break;
        if (r->d->nlink == r->d->limits.links) {
            r->d->full |= TIKU_DOC_FULL_LINKS;
        } else {
            r->d->link[r->d->nlink].href = href;
            r->d->link[r->d->nlink].run0 = (int32_t)r->d->nrun;
            r->d->link[r->d->nlink].nrun = 0;
            r->d->link[r->d->nlink].piece0 = TIKU_DOC_NONE;
            r->d->link[r->d->nlink].npiece = 0;
            r->link = r->d->nlink++;
            r->ink = TIKU_DOC_INK_LINK;
        }
        run_close(r);
        break;
    case T_B:
        if (close) { stack_pop(r, T_B); break; }
        style_changed(r);
        stack_push(r, T_B);
        r->face = TIKU_MEASURE_BOLD;
        run_close(r);
        break;
    case T_I:
        if (close) { stack_pop(r, T_I); break; }
        style_changed(r);
        stack_push(r, T_I);
        r->ink = TIKU_DOC_INK_EMPHASIS;
        run_close(r);
        break;
    case T_CODE:
        if (close) { stack_pop(r, T_CODE); break; }
        style_changed(r);
        stack_push(r, T_CODE);
        r->face = TIKU_MEASURE_MONO;
        if (r->ink == TIKU_DOC_INK_PLAIN) r->ink = TIKU_DOC_INK_CODE;
        run_close(r);
        break;
    case T_BR:
        if (!close) {
            if (run_open(r)) r->d->run[r->run].flags |= TIKU_DOC_RUN_BREAK;
            run_close(r);
            r->pending = 0;
        }
        break;
    default:
        break;                  /* an unknown tag keeps its text */
    }
    if (!close && tag != T_SKIP && tag != T_IMG && tag != T_HR) {
        anchor_add(r, body, r->block != TIKU_DOC_NONE ? r->block : r->d->nblock);
    }
    if (element & DOC_ELEMENT_TRANSIENT) css_restore(r,r->css_depth-1);
    return end;
}

/** @brief Count each link's runs: those from its first that carry it. */
static void links_close(tiku_doc_t *d)
{
    for (int i = 0; i < d->nlink; i++) {
        int last = d->link[i].run0;

        while (last < d->nrun && d->run[last].link == i) last++;
        d->link[i].nrun = (int32_t)(last - d->link[i].run0);
    }
}

void tiku_doc_init(tiku_doc_t *d)
{
    memset(d, 0, sizeof *d);
    d->text = d->small_text; d->block = d->small_block;
    d->run = d->small_run; d->line = d->small_line;
    d->piece = d->small_piece; d->link = d->small_link;
    d->anchor = d->small_anchor; d->image = d->small_image;
    d->limits = (tiku_doc_limits_t){TIKU_DOC_TEXT_MAX, TIKU_DOC_BLOCKS_MAX,
        TIKU_DOC_RUNS_MAX, TIKU_DOC_LINES_MAX, TIKU_DOC_PIECES_MAX,
        TIKU_DOC_LINKS_MAX, TIKU_DOC_ANCHORS_MAX, TIKU_DOC_IMAGES_MAX};
    d->title = TIKU_DOC_NONE;
    d->laid_width = TIKU_DOC_NONE;
}

#include "tiku_doc_storage.inl"

void tiku_doc_images(tiku_doc_t *d, tiku_doc_image_fn fn, void *ctx)
{
    d->image_fn = fn;
    d->image_ctx = ctx;
}

int tiku_doc_read(tiku_doc_t *d, const char *html)
{
    tiku_doc_image_fn fn = d->image_fn;
    void *ctx = d->image_ctx;
    read_t r;
    const char *p = html ? html : "";
    const char *skip_to = NULL;

    if (d->storage) {
        tiku_doc_limits_t limits = d->limits;
        (void)tiku_doc_init_storage(d, &limits, d->storage, d->storage_bytes);
    } else tiku_doc_init(d);
    d->image_fn = fn;
    d->image_ctx = ctx;
    memset(&r, 0, sizeof r);
    r.d = d;
    r.face = TIKU_MEASURE_PLAIN;
    r.ink = TIKU_DOC_INK_PLAIN;
    r.link = TIKU_DOC_NONE;
    r.block = TIKU_DOC_NONE;
    r.run = TIKU_DOC_NONE;
    r.font.scale = DOC_FONT_ONE;
    r.box = css_box_default();
    css_collect(&r.css,p);

    while (*p) {
        if (skip_to) {
            /* Everything up to this element's own close tag is gone. */
            if (p[0] == '<' && p[1] == '/') {
                const char *name = p + 2;
                int n = 0;

                while (name[n] && !html_space(name[n]) && name[n] != '>' &&
                       name[n] != '/' && name[n] != '<') n++;
                size_t expected = 0;
                while (skip_to[expected] && !html_space(skip_to[expected]) &&
                       skip_to[expected] != '/' && skip_to[expected] != '>') expected++;
                if (html_same(name, (size_t)n, skip_to, expected)) {
                    skip_to = NULL;
                    p = html_tag_end(name + n);
                    continue;
                }
            }
            p++;
            continue;
        }
        if (p[0] == '<' && p[1] == '!') {
            if (p[2] == '-' && p[3] == '-') {
                const char *end = strstr(p + 4, "-->");

                p = end ? end + 3 : p + strlen(p);
            } else {
                p = html_tag_end(p + 2);
            }
            continue;
        }
        if (p[0] == '<' && (html_letter(p[1]) ||
                            (p[1] == '/' && html_letter(p[2])))) {
            p = tag_do(&r, p, &skip_to);
            continue;
        }
        if (r.hidden) { p++; continue; }
        if ((r.flags & TIKU_DOC_RUN_PRE) == 0 &&
            html_space(*p)) {
            /* A closing tag ends its run, and the blank after it is
             * still the space between two words. */
            if (r.block != TIKU_DOC_NONE &&
                (r.run != TIKU_DOC_NONE ||
                 d->nrun > d->block[r.block].run0)) {
                r.pending = 1;
            }
            p++;
            continue;
        }
        if (*p == '&') {
            char out[8];
            const char *after;
            int n = entity(p + 1, out, &after);

            text_add(&r, out, n);
            p = after == p + 1 ? p + 1 : after;
            continue;
        }
        if ((r.flags & TIKU_DOC_RUN_PRE) && *p == '\t') {
            text_add(&r, "    ", 4);
            p++;
            continue;
        }
        if ((r.flags & TIKU_DOC_RUN_PRE) && *p == '\r') {
            text_add(&r,"\n",1);
            p += p[1] == '\n' ? 2 : 1;
            continue;
        }
        {
            int n = utf8_len(p);

            if ((unsigned char)*p < 32 && *p != '\n') { p += n; continue; }
            text_add(&r, p, n);
            p += n;
        }
    }
    block_end(&r);
    if (r.css_depth) css_restore(&r,0);
    int anchors = 0;
    for (int i = 0; i < d->nanchor; i++) {
        if (d->anchor[i].block < d->nblock) d->anchor[anchors++] = d->anchor[i];
    }
    d->nanchor = anchors;
    d->notices = r.css.notices;
    links_close(d);
    for (int i = 0; i < d->nblock && d->title == TIKU_DOC_NONE; i++) {
        if (tiku_doc_heading_level(&d->block[i]) && d->block[i].nrun) {
            d->title = d->run[d->block[i].run0].off;
        }
    }
    return d->full == 0;
}

/*---------------------------------------------------------------------------*/
/* LAYING OUT                                                                */
/*---------------------------------------------------------------------------*/

/** @brief Everything the layout carries down a block. */
typedef struct {
    tiku_doc_t *d;
    int width;
    int y;
    int line;                   /* the line being filled, or NONE       */
    int x;                      /* the pen, from the line's own x       */
    int piece;                  /* the piece being grown, or NONE       */
    int block;
    int xc;                     /* where a continuation line begins     */
    int left;                   /**< containing block's content origin */
} lay_t;

#include "tiku_doc_length.inl"

/** @brief How tall a line in @p face is. */
static int face_h(int face) { return tiku_measure_height(face); }

/** @brief What room the line being filled has, its indent taken off. */
static int line_room(const lay_t *L)
{
    int x = L->line != TIKU_DOC_NONE ? L->d->line[L->line].x : L->xc;
    int room = L->width - x;

    return room > 0 ? room : 1;
}

/** @brief Measure @p n bytes of @p s in @p face, copied a stretch at a
 *         time into terminated scratch. */
static int word_w(int face, const char *s, int n)
{
    char buf[DOC_WORD_MAX];
    int total = 0;

    while (n > 0) {
        int take = n < (int)sizeof buf - 1 ? n : (int)sizeof buf - 1;

        /* Never split a codepoint at the scratch edge. */
        while (take > 1 && ((unsigned char)s[take] & 0xc0) == 0x80) take--;
        memcpy(buf, s, (size_t)take);
        buf[take] = 0;
        total += tiku_measure_width(face, buf);
        s += take;
        n -= take;
    }
    return total;
}

/** @brief Include a font's ascent, descent and requested leading in a line. */
static void line_metrics(lay_t *L, const tiku_doc_font_t *font,
                           const tiku_doc_flow_t *flow, int face)
{
    if (L->line == TIKU_DOC_NONE) return;
    tiku_doc_line_t *ln = &L->d->line[L->line];
    int height = face_h(face), ascent = tiku_measure_ascent(face);
    int64_t logical = (int64_t)font->scale*tiku_measure_font_size()+font->offset;
    int64_t wanted = height;
    if (flow->height == TIKU_DOC_HEIGHT_FACTOR)
        wanted = (logical*flow->scale/DOC_FONT_ONE+DOC_FONT_ONE/2)/DOC_FONT_ONE;
    else if (flow->height == TIKU_DOC_HEIGHT_LENGTH)
        wanted = ((int64_t)flow->scale*tiku_measure_font_size()+flow->offset+
                  DOC_FONT_ONE/2)/DOC_FONT_ONE;
    if (wanted < height) {
        L->d->notices |= TIKU_DOC_FLOW_FALLBACK; wanted = height;
    }
    if (wanted > 4096) {
        L->d->notices |= TIKU_DOC_FLOW_FALLBACK; wanted = 4096;
    }
    int extra = (int)wanted-height;
    int above = ascent+extra/2, below = height-ascent+extra-extra/2;
    int descent = ln->h-ln->ascent;
    if (above > ln->ascent) ln->ascent = above;
    if (below > descent) descent = below;
    ln->h = ln->ascent+descent;
}

/** @brief Trim collapsed trailing spaces, align the line and advance its y. */
static void line_close(lay_t *L)
{
    tiku_doc_t *d = L->d;

    if (L->line == TIKU_DOC_NONE) return;
    tiku_doc_line_t *ln = &d->line[L->line];
    if (ln->npiece) {
        tiku_doc_piece_t *last = &d->piece[ln->piece0 + ln->npiece - 1];
        int face = d->run[last->run].face;
        int before = last->w;

        while (!(d->run[last->run].flags & TIKU_DOC_RUN_PRE) &&
               last->len > 0 && d->text[last->off + last->len - 1] == ' ') {
            last->len--;
            last->w = (int32_t)word_w(face, d->text + last->off, last->len);
        }
        /* The pen goes back with it: the line's width is where the pen
         * ends. */
        L->x -= before - last->w;
        if (!last->len) ln->npiece--, d->npiece--;
    }
    if (!ln->h) {
        ln->h = (int32_t)face_h(TIKU_MEASURE_PLAIN);
        ln->ascent = (int32_t)tiku_measure_ascent(TIKU_MEASURE_PLAIN);
    }
    ln->w = (int32_t)L->x;
    int remaining = L->width-ln->x-ln->w;
    unsigned align = d->block[L->block].flow.align;
    if (remaining > 0) {
        if (align == TIKU_DOC_ALIGN_CENTER) ln->x += remaining/2;
        else if (align == TIKU_DOC_ALIGN_RIGHT) ln->x += remaining;
    }
    L->y += ln->h;
    L->line = TIKU_DOC_NONE;
    L->piece = TIKU_DOC_NONE;
}

/** @brief Begin a line at @p x.  @return 1 when there was room. */
static int line_open(lay_t *L, int x)
{
    tiku_doc_t *d = L->d;

    if (L->line != TIKU_DOC_NONE) return 1;
    if (L->y > TIKU_DOC_HEIGHT_MAX-16384 &&
        d->block[L->block].kind != TIKU_DOC_BLOCK_NOTICE) {
        d->full |= TIKU_DOC_FULL_HEIGHT; return 0;
    }
    if (d->nline == d->limits.lines) { d->full |= TIKU_DOC_FULL_LINES; return 0; }
    tiku_doc_line_t *ln = &d->line[d->nline];
    memset(ln, 0, sizeof *ln);
    ln->y = L->y;
    ln->block = (int32_t)L->block;
    ln->piece0 = (int32_t)d->npiece;
    ln->x = (int32_t)x;
    L->line = d->nline++;
    L->x = 0;
    L->piece = TIKU_DOC_NONE;
    if (d->block[L->block].nline == 0) d->block[L->block].line0 = (int32_t)L->line;
    d->block[L->block].nline++;
    const tiku_doc_block_t *b = &d->block[L->block];
    if (b->flow.height != TIKU_DOC_HEIGHT_NORMAL) {
        int level = tiku_doc_heading_level(b);
        int face = b->font.specified ? doc_font_face(d,&b->font) :
            b->font.family == TIKU_MEASURE_FAMILY_MONO ? TIKU_MEASURE_MONO :
            level ? tiku_measure_heading(level) :
            b->font.weight ? TIKU_MEASURE_BOLD : TIKU_MEASURE_PLAIN;
        line_metrics(L,&b->font,&b->flow,face);
    }
    return 1;
}

/** @brief Give an unlaid block an empty range at the current layout boundary. */
static void block_unlaid(lay_t *L, int block)
{
    tiku_doc_block_t *b = &L->d->block[block];
    b->y = L->y; b->h = 0;
    b->line0 = L->d->nline; b->nline = 0;
    b->x = b->w = b->border_px = 0;
}

/** @brief Put @p n bytes on the line at the pen, growing the piece. */
static int piece_add(lay_t *L, int run, int off, int n, int w)
{
    tiku_doc_t *d = L->d;

    if (L->line == TIKU_DOC_NONE) return 0;
    tiku_doc_line_t *ln = &d->line[L->line];
    if (L->piece != TIKU_DOC_NONE && d->piece[L->piece].run == run &&
        d->piece[L->piece].off + d->piece[L->piece].len == off) {
        d->piece[L->piece].len = (int32_t)(d->piece[L->piece].len + n);
        d->piece[L->piece].w = (int32_t)(d->piece[L->piece].w + w);
    } else {
        if (d->npiece == d->limits.pieces) {
            d->full |= TIKU_DOC_FULL_PIECES;
            return 0;
        }
        tiku_doc_piece_t *pc = &d->piece[d->npiece];
        pc->off = off;
        pc->len = (int32_t)n;
        pc->x = (int32_t)L->x;
        pc->w = (int32_t)w;
        pc->run = (int32_t)run;
        L->piece = d->npiece++;
        ln->npiece++;
    }
    L->x += w;
    line_metrics(L,&d->run[run].font,&d->run[run].flow,d->run[run].face);
    return 1;
}

/**
 * @brief Place a word too wide for the line, a code point at a time.
 *
 * At least one code point goes on every line, however narrow the box.
 */
static void place_long(lay_t *L, int run, int off, int n, int face)
{
    tiku_doc_t *d = L->d;
    int at = 0;

    d->full |= TIKU_DOC_NARROW;
    while (at < n) {
        int fit = 0, fitw = 0, i = at, w = 0, room = line_room(L);

        while (i < n) {
            int step = utf8_len(d->text + off + i);
            int add = word_w(face, d->text + off + i, step);

            if (L->x + w + add > room) break;
            w += add;
            i += step;
            fit = i - at;
            fitw = w;
        }
        /* A box with no room for one glyph still takes one. */
        if (!fit) {
            fit = utf8_len(d->text + off + at);
            fitw = word_w(face, d->text + off + at, fit);
        }
        if (!piece_add(L, run, off + at, fit, fitw)) return;
        at += fit;
        if (at < n) {
            line_close(L);
            if (!line_open(L, L->xc)) return;
        }
    }
}

/** @brief Lay one run's words out, breaking lines as they fill. */
static void place_run(lay_t *L, int run)
{
    tiku_doc_t *d = L->d;
    const tiku_doc_run_t *rn = &d->run[run];
    int face = rn->face, off = rn->off, n = rn->len, at = 0;
    int wrap = rn->flow.whitespace != TIKU_DOC_WHITE_PRE &&
               rn->flow.whitespace != TIKU_DOC_WHITE_NOWRAP;
    if (!wrap && d->block[L->block].kind == TIKU_DOC_BLOCK_CELL) {
        wrap = 1; d->notices |= TIKU_DOC_TABLE_FALLBACK;
    }

    while (at < n) {
        int start = at, w, room;

        if ((rn->flags & TIKU_DOC_RUN_PRE) && d->text[off + at] == '\n') {
            line_metrics(L,&rn->font,&rn->flow,face);
            line_close(L);
            if (!line_open(L, L->xc)) return;
            line_metrics(L,&rn->font,&rn->flow,face);
            at++;
            continue;
        }
        /* A word, and the blank space that follows it on the same line. */
        while (at < n && d->text[off + at] != ' ' &&
               !((rn->flags & TIKU_DOC_RUN_PRE) && d->text[off + at] == '\n')) {
            at += utf8_len(d->text + off + at);
        }
        w = word_w(face, d->text + off + start, at - start);
        room = line_room(L);
        if (wrap && L->line != TIKU_DOC_NONE && L->x && L->x + w > room) {
            line_close(L);
        }
        if (L->line == TIKU_DOC_NONE && !line_open(L, L->xc)) return;
        room = line_room(L);
        if (wrap && w > room) {
            place_long(L, run, off + start, at - start, face);
        } else if (!piece_add(L, run, off + start, at - start, w)) {
            return;
        }
        /* Preserved spaces wrap individually; collapsed trailing spaces trim. */
        {
            int sp = at;

            while (at < n && d->text[off + at] == ' ') at++;
            if (wrap && (rn->flags & TIKU_DOC_RUN_PRE)) {
                int sw = word_w(face," ",1);
                while (sp < at) {
                    if (L->x && L->x+sw > line_room(L)) {
                        line_close(L);
                        if (!line_open(L,L->xc)) return;
                    }
                    if (!piece_add(L,run,off+sp,1,sw)) return;
                    sp++;
                }
            } else if (at > sp && !piece_add(L, run, off + sp, at - sp,
                                      word_w(face, d->text + off + sp, at - sp))) {
                return;
            }
        }
    }
    if (rn->flags & TIKU_DOC_RUN_BREAK) line_close(L);
}

/** @brief The space a block wants above and below it, and how far it is
 *         indented. */
static void block_metrics(const tiku_doc_t *d, int i, int *above, int *below,
                          int *indent)
{
    int line = face_h(TIKU_MEASURE_PLAIN);
    const tiku_doc_block_t *b = &d->block[i];

    *above = 0;
    *below = line / 2;
    *indent = 0;
    switch (b->kind) {
    case TIKU_DOC_BLOCK_H1: *above = line; *below = line / 2; break;
    case TIKU_DOC_BLOCK_H2: *above = line; *below = line / 3; break;
    case TIKU_DOC_BLOCK_H3: *above = line / 2; *below = line / 4; break;
    case TIKU_DOC_BLOCK_H4: case TIKU_DOC_BLOCK_H5: case TIKU_DOC_BLOCK_H6:
        *above = line / 2; *below = line / 4; break;
    case TIKU_DOC_BLOCK_DEFINITION:
        *indent = b->depth * line;
        break;
    case TIKU_DOC_BLOCK_ITEM:
        *indent = b->depth * (line + 4);
        *below = line / 5;
        break;
    case TIKU_DOC_BLOCK_PRE:
        *above = line / 2; *below = line / 2; *indent = line / 2;
        break;
    case TIKU_DOC_BLOCK_QUOTE:
        *above = line / 3; *below = line / 3; *indent = line;
        break;
    case TIKU_DOC_BLOCK_RULE: *above = line / 2; *below = line / 2; break;
    /* lay_table() lays cells out without asking here. */
    case TIKU_DOC_BLOCK_CELL: *below = 0; break;
    case TIKU_DOC_BLOCK_NOTICE: *above = line; *below = line / 2; break;
    default: break;
    }
    if (i == 0) *above = 0;
    if (b->flags & TIKU_DOC_BLOCK_BOX_CHILD) *above = *below = 0;
}

/** @brief The columns of one table, and where each of them begins. */
typedef struct {
    int x[TIKU_DOC_COLS_MAX];
    int w[TIKU_DOC_COLS_MAX];
    int n;
} cols_t;

/**
 * @brief The narrowest and the widest one cell can be drawn.
 *
 * The narrowest is its longest word and the widest everything it holds
 * on one line, both measured in the runs' own faces (a header cell's
 * bold among them).
 */
static void cell_extent(const tiku_doc_t *d, int i, int *min, int *max)
{
    const tiku_doc_block_t *b = &d->block[i];

    *min = 0;
    *max = 0;
    for (int k = 0; k < b->nrun; k++) {
        const tiku_doc_run_t *rn = &d->run[b->run0 + k];
        const char *t = d->text + rn->off;
        int at = 0;

        *max += word_w(rn->face, t, rn->len);
        while (at < rn->len) {
            int end = at;
            int w;

            while (end < rn->len && t[end] != ' ') end++;
            w = word_w(rn->face, t + at, end - at);
            if (w > *min) *min = w;
            at = end;
            while (at < rn->len && t[at] == ' ') at++;
        }
    }
}

/**
 * @brief Share @p width out between the columns of one table.
 *
 * Every column is given its widest content when they all fit, and
 * otherwise its longest word plus a share of what is left over in
 * proportion to what it wanted.  The columns always add up to the width
 * they were given, so a rule drawn under a row reaches both edges.
 */
static void cols_measure(lay_t *L, int first, int last, int width,
                         int gap, cols_t *C)
{
    const tiku_doc_t *d = L->d;
    int min[TIKU_DOC_COLS_MAX] = {0};
    int max[TIKU_DOC_COLS_MAX] = {0};
    int summin = 0, summax = 0, room, used = 0;

    C->n = 1;
    for (int i = first; i < last; i++) {
        if (d->block[i].kind != TIKU_DOC_BLOCK_CELL) continue;
        int end = d->block[i].col + (d->block[i].span ? d->block[i].span : 1);

        if (end > C->n) C->n = end;
    }
    if (C->n > TIKU_DOC_COLS_MAX) C->n = TIKU_DOC_COLS_MAX;
    for (int i = first; i < last; i++) {
        const tiku_doc_block_t *b = &d->block[i];
        int lo, hi;

        if (b->kind != TIKU_DOC_BLOCK_CELL) continue;
        if (b->span > 1) continue;      /* a spanning cell: shared below */
        cell_extent(d, i, &lo, &hi);
        int edges = box_length(L,&b->box.padding[1],&b->font,width,4096)+
                    box_length(L,&b->box.padding[3],&b->font,width,4096);
        if (b->box.border_style) edges += 2*box_length(L,&b->box.border,&b->font,width,4096);
        lo += edges; hi += edges;
        if (b->box.width.unit != TIKU_DOC_AUTO) {
            int preferred = box_length(L,&b->box.width,&b->font,width,TIKU_DOC_HEIGHT_MAX)+edges;
            if (preferred > hi) hi = preferred;
        }
        if (lo > min[b->col]) min[b->col] = lo;
        if (hi > max[b->col]) max[b->col] = hi;
    }
    /* A column nothing but a spanning cell reaches still needs a width,
     * so the span hands its own out over the columns it covers. */
    for (int i = first; i < last; i++) {
        const tiku_doc_block_t *b = &d->block[i];
        int span = b->span ? b->span : 1;
        int lo, hi, have = 0, want;

        if (b->kind != TIKU_DOC_BLOCK_CELL || span < 2) continue;
        cell_extent(d, i, &lo, &hi);
        for (int c = b->col; c < b->col + span && c < C->n; c++) have += max[c];
        want = hi - have - gap * (span - 1);
        if (want <= 0) continue;
        for (int c = b->col; c < b->col + span && c < C->n; c++) {
            max[c] += want / span;
            if (min[c] < lo / span) min[c] = lo / span;
        }
    }
    for (int c = 0; c < C->n; c++) {
        if (max[c] < min[c]) max[c] = min[c];
        summin += min[c];
        summax += max[c];
    }
    room = width - gap * (C->n - 1);
    if (room < C->n) room = C->n;
    for (int c = 0; c < C->n; c++) {
        /* In long: a page of very wide cells makes the numerator of the
         * share larger than an int holds. */
        if (summax <= room) {
            C->w[c] = summax > 0
                ? max[c] + (int)((long)(room - summax) * max[c] / summax)
                : room / C->n;
        } else if (summin <= room && summax > summin) {
            C->w[c] = min[c] +
                      (int)((long)(room - summin) * (max[c] - min[c]) /
                            (summax - summin));
        } else {
            C->w[c] = room / C->n;
        }
        if (C->w[c] < 1) C->w[c] = 1;
        used += C->w[c];
    }
    /* What the division left over goes to the last column, so the row
     * ends where the page does. */
    C->w[C->n - 1] += room - used;
    if (C->w[C->n - 1] < 1) C->w[C->n - 1] = 1;
    C->x[0] = 0;
    for (int c = 1; c < C->n; c++) C->x[c] = C->x[c - 1] + C->w[c - 1] + gap;
}

#include "tiku_doc_table_layout.inl"

#include "tiku_doc_image_layout.inl"

/** @brief One block: its space above, its lines, its space below. */
static void lay_block(lay_t *L, int i)
{
    tiku_doc_t *d = L->d;
    tiku_doc_block_t *b = &d->block[i];
    int above, below, indent, hang = 0;

    block_metrics(d, i, &above, &below, &indent);
    indent += L->left;
    L->y += above;
    L->block = i;
    b->y = L->y;
    b->line0 = (int32_t)d->nline;
    b->nline = 0;

    if (b->kind == TIKU_DOC_BLOCK_RULE) {
        if (line_open(L, indent)) {
            d->line[L->line].h = (int32_t)(face_h(TIKU_MEASURE_PLAIN) / 2);
            d->line[L->line].ascent = d->line[L->line].h;
            line_close(L);
        }
        b->h = (int32_t)(L->y + below - b->y);
        L->y += below;
        return;
    }
    if (b->kind == TIKU_DOC_BLOCK_NOTICE) {
        if (line_open(L, indent)) {
            d->line[L->line].h = (int32_t)face_h(TIKU_MEASURE_PLAIN);
            d->line[L->line].ascent =
                (int32_t)tiku_measure_ascent(TIKU_MEASURE_PLAIN);
            line_close(L);
        }
        b->h = (int32_t)(L->y + below - b->y);
        L->y += below;
        return;
    }
    if (b->kind == TIKU_DOC_BLOCK_IMAGE && b->image != TIKU_DOC_NONE) {
        int w = 0, h = 0;

        if (d->image_fn && d->image[b->image].src != TIKU_DOC_NONE &&
            d->image_fn(d->image_ctx, d->text + d->image[b->image].src, &w, &h)
            && image_dimensions(L,b,&w,&h)) {
            d->image[b->image].w = (int32_t)w;
            d->image[b->image].h = (int32_t)h;
            if (line_open(L, indent)) {
                L->x = w;
                d->line[L->line].h = (int32_t)h;
                d->line[L->line].ascent = (int32_t)h;
                line_close(L);
            }
            b->h = (int32_t)(L->y + below - b->y);
            L->y += below;
            return;
        }
        d->image[b->image].w = 0;
        d->image[b->image].h = 0;
        /* No pixels: the alt runs are laid out as words, in a frame. */
    }

    /* A list item hangs its marker: the first line begins at the indent
     * and the rest line up under the words. */
    if (b->kind == TIKU_DOC_BLOCK_ITEM && b->nrun &&
        (d->run[b->run0].flags & TIKU_DOC_RUN_MARKER)) {
        hang = word_w(d->run[b->run0].face, d->text + d->run[b->run0].off,
                      d->run[b->run0].len) + face_h(d->run[b->run0].face) / 3;
    }
    L->xc = indent + hang;
    if (!line_open(L, indent)) { b->h = 0; return; }
    for (int k = 0; k < b->nrun; k++) {
        int run = b->run0 + k;

        if (d->run[run].flags & TIKU_DOC_RUN_MARKER) {
            int w = word_w(d->run[run].face, d->text + d->run[run].off,
                           d->run[run].len);

            (void)piece_add(L, run, d->run[run].off, d->run[run].len, w);
            L->x = hang;
            continue;
        }
        place_run(L, run);
        if (L->line == TIKU_DOC_NONE && k + 1 < b->nrun &&
            !line_open(L, L->xc)) break;
    }
    line_close(L);
    b->h = (int32_t)(L->y + below - b->y);
    L->y += below;
}

#include "tiku_doc_box_layout.inl"

int tiku_doc_layout(tiku_doc_t *d, int width)
{
    lay_t L;
    int face_height = face_h(TIKU_MEASURE_PLAIN);

    if (width < 1) width = 1;
    unsigned stamp = tiku_measure_font_stamp();
    if (d->laid_width == width && d->laid_height == face_height &&
        d->laid_font_stamp == stamp) {
        return d->height;
    }
    d->nline = 0;
    d->npiece = 0;
    /* A notice from the last width goes with that width's tables. */
    if (d->nblock && d->block[d->nblock - 1].kind == TIKU_DOC_BLOCK_NOTICE) {
        d->nblock--;
    }
    d->full &= ~(TIKU_DOC_FULL_LINES | TIKU_DOC_FULL_PIECES | TIKU_DOC_NARROW |
                 TIKU_DOC_FULL_HEIGHT);
    d->full &= ~TIKU_DOC_FACE_FLAT;
    d->notices &= ~(TIKU_DOC_FONT_FALLBACK | TIKU_DOC_FLOW_FALLBACK);
    /* Headings take their face from tiku_measure_heading(); a fallback
     * to bold sets TIKU_DOC_FACE_FLAT. */
    for (int i = 0; i < d->nblock; i++) {
        int level = tiku_doc_heading_level(&d->block[i]);
        if (level) {
            int face = tiku_measure_heading(level);
            if (level < 3 && face == TIKU_MEASURE_BOLD) d->full |= TIKU_DOC_FACE_FLAT;
            for (int k = d->block[i].run0; k < d->block[i].run0+d->block[i].nrun; k++)
                if (!d->run[k].font.specified) d->run[k].face = (uint16_t)face;
        }
    }
    for (int i = 0; i < d->nrun; i++) {
        if (d->run[i].font.specified)
            d->run[i].face = (uint16_t)doc_font_face(d,&d->run[i].font);
    }
    for (int i = 0; i < d->nlink; i++) {
        d->link[i].piece0 = TIKU_DOC_NONE;
        d->link[i].npiece = 0;
    }
    memset(&L, 0, sizeof L);
    L.d = d;
    L.width = width;
    L.line = TIKU_DOC_NONE;
    L.piece = TIKU_DOC_NONE;
    lay_range(&L,0,d->nblock);
    /* What a cap cost takes room of its own, so nothing can pass over it. */
    if ((d->full & DOC_FULL_CAPS) && d->nblock < d->limits.blocks) {
        tiku_doc_block_t *b = &d->block[d->nblock];

        memset(b, 0, sizeof *b);
        b->kind = TIKU_DOC_BLOCK_NOTICE;
        b->image = TIKU_DOC_NONE;
        b->run0 = (int32_t)d->nrun;
        d->nblock++;
        lay_block(&L, d->nblock - 1);
    }
    for (int i = 0; i < d->npiece; i++) {
        int link = d->run[d->piece[i].run].link;

        if (link < 0 || link >= d->nlink) continue;
        if (d->link[link].piece0 == TIKU_DOC_NONE) {
            d->link[link].piece0 = (int32_t)i;
            d->link[link].npiece = 1;
        } else if (d->link[link].piece0 + d->link[link].npiece == i) {
            /* Only what lies against it: a page that left an anchor open
             * puts another link's words in the middle, and they are not
             * this one's to draw a ring around. */
            d->link[link].npiece++;
        }
    }
    d->height = L.y;
    d->laid_width = width;
    d->laid_height = face_height;
    d->laid_font_stamp = stamp;
    return d->height;
}

/*---------------------------------------------------------------------------*/
/* ASKING                                                                    */
/*---------------------------------------------------------------------------*/

int tiku_doc_height(const tiku_doc_t *d) { return d->height; }
int tiku_doc_width(const tiku_doc_t *d) { return d->laid_width; }
int tiku_doc_laid(const tiku_doc_t *d)
{ return d->laid_width != TIKU_DOC_NONE && d->laid_width > 0; }
unsigned tiku_doc_full(const tiku_doc_t *d) { return d->full; }
unsigned tiku_doc_notices(const tiku_doc_t *d) { return d->notices; }
int tiku_doc_block_count(const tiku_doc_t *d) { return d->nblock; }
int tiku_doc_line_count(const tiku_doc_t *d) { return d->nline; }
int tiku_doc_piece_count(const tiku_doc_t *d) { return d->npiece; }
int tiku_doc_link_count(const tiku_doc_t *d) { return d->nlink; }

const tiku_doc_block_t *tiku_doc_block(const tiku_doc_t *d, int i)
{ return i >= 0 && i < d->nblock ? &d->block[i] : NULL; }
int tiku_doc_heading_level(const tiku_doc_block_t *block)
{
    if (!block) return 0;
    if (block->kind >= TIKU_DOC_BLOCK_H1 && block->kind <= TIKU_DOC_BLOCK_H3)
        return block->kind - TIKU_DOC_BLOCK_H1 + 1;
    if (block->kind >= TIKU_DOC_BLOCK_H4 && block->kind <= TIKU_DOC_BLOCK_H6)
        return block->kind - TIKU_DOC_BLOCK_H4 + 4;
    return 0;
}
const tiku_doc_run_t *tiku_doc_run(const tiku_doc_t *d, int i)
{ return i >= 0 && i < d->nrun ? &d->run[i] : NULL; }
const tiku_doc_line_t *tiku_doc_line(const tiku_doc_t *d, int i)
{ return i >= 0 && i < d->nline ? &d->line[i] : NULL; }
const tiku_doc_piece_t *tiku_doc_piece(const tiku_doc_t *d, int i)
{ return i >= 0 && i < d->npiece ? &d->piece[i] : NULL; }

const char *tiku_doc_text(const tiku_doc_t *d, int off)
{ return off >= 0 && off < d->ntext ? d->text + off : ""; }

const char *tiku_doc_title(const tiku_doc_t *d)
{ return d->title == TIKU_DOC_NONE ? NULL : d->text + d->title; }

int tiku_doc_full_first(const tiku_doc_t *d, const char **what, int *cap)
{
    const struct { unsigned bit; const char *what; int cap; } t[] = {
        {TIKU_DOC_FULL_TEXT,    "text",    d->limits.text},
        {TIKU_DOC_FULL_BLOCKS,  "blocks",  d->limits.blocks - 1},
        {TIKU_DOC_FULL_RUNS,    "runs",    d->limits.runs},
        {TIKU_DOC_FULL_LINES,   "lines",   d->limits.lines},
        {TIKU_DOC_FULL_PIECES,  "pieces",  d->limits.pieces},
        {TIKU_DOC_FULL_LINKS,   "links",   d->limits.links},
        {TIKU_DOC_FULL_ANCHORS, "anchors", d->limits.anchors},
        {TIKU_DOC_FULL_IMAGES,  "images",  d->limits.images},
        {TIKU_DOC_FULL_DEPTH,   "nesting", TIKU_DOC_DEPTH_MAX},
        {TIKU_DOC_FULL_HEIGHT,  "layout height", TIKU_DOC_HEIGHT_MAX}
    };

    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
        if (d->full & t[i].bit) {
            if (what) *what = t[i].what;
            if (cap) *cap = t[i].cap;
            return 1;
        }
    }
    return 0;
}

tiku_rect_t tiku_doc_piece_rect(const tiku_doc_t *d, int piece)
{
    tiku_rect_t r = {0, 0, 0, 0};

    if (piece < 0 || piece >= d->npiece) return r;
    const tiku_doc_piece_t *pc = &d->piece[piece];
    for (int i = 0; i < d->nline; i++) {
        if (piece < d->line[i].piece0 ||
            piece >= d->line[i].piece0 + d->line[i].npiece) continue;
        r.x = d->line[i].x + pc->x;
        r.y = (int)d->line[i].y;
        r.w = pc->w;
        r.h = d->line[i].h;
        return r;
    }
    return r;
}

tiku_rect_t tiku_doc_block_rect(const tiku_doc_t *d, int block)
{
    tiku_rect_t r = {0, 0, 0, 0};

    if (block < 0 || block >= d->nblock) return r;
    const tiku_doc_block_t *b = &d->block[block];
    if (b->kind == TIKU_DOC_BLOCK_BOX || b->kind == TIKU_DOC_BLOCK_CELL)
        return (tiku_rect_t){b->x,b->y,b->w,b->h};
    r.y = (int)b->y;
    r.h = b->h;
    r.x = d->laid_width;
    for (int i = b->line0; i < b->line0 + b->nline && i < d->nline; i++) {
        int right = d->line[i].x + d->line[i].w;

        if (d->line[i].x < r.x) r.x = d->line[i].x;
        if (right - r.x > r.w) r.w = right - r.x;
    }
    if (!b->nline) { r.x = 0; r.w = 0; }
    return r;
}

int tiku_doc_line_at(const tiku_doc_t *d, int y)
{
    if (!tiku_doc_laid(d)) return TIKU_DOC_NONE;
    for (int i = 0; i < d->nline; i++) {
        if (y >= d->line[i].y && y < d->line[i].y + d->line[i].h) return i;
    }
    return TIKU_DOC_NONE;
}

tiku_doc_hit_t tiku_doc_hit(const tiku_doc_t *d, int x, int y)
{
    tiku_doc_hit_t h;
    int line;

    h.kind = TIKU_DOC_HIT_NONE;
    h.link = TIKU_DOC_NONE;
    h.block = TIKU_DOC_NONE;
    h.line = TIKU_DOC_NONE;
    h.piece = TIKU_DOC_NONE;
    if (!tiku_doc_laid(d)) return h;
    line = TIKU_DOC_NONE;
    /* Every line whose band holds the point is searched: the cells of a
     * table share a band and differ by x. */
    for (int k = 0; k < d->nline; k++) {
        const tiku_doc_line_t *ln = &d->line[k];

        if (y < ln->y || y >= ln->y + ln->h) continue;
        const tiku_doc_block_t *b = &d->block[ln->block];
        if (b->kind == TIKU_DOC_BLOCK_CELL &&
            (x < b->content_x || x >= b->content_x+b->content_w)) continue;
        if (line == TIKU_DOC_NONE) line = k;
        for (int i = ln->piece0; i < ln->piece0 + ln->npiece; i++) {
            int left = ln->x + d->piece[i].x;

            if (x < left || x >= left + d->piece[i].w) continue;
            h.line = k;
            h.block = ln->block;
            h.piece = i;
            h.kind = TIKU_DOC_HIT_TEXT;
            if (d->run[d->piece[i].run].link != TIKU_DOC_NONE) {
                h.kind = TIKU_DOC_HIT_LINK;
                h.link = d->run[d->piece[i].run].link;
            }
            return h;
        }
    }
    if (line == TIKU_DOC_NONE) return h;
    h.line = line;
    h.block = d->line[line].block;
    if (h.block != TIKU_DOC_NONE &&
        d->block[h.block].kind == TIKU_DOC_BLOCK_IMAGE &&
        d->block[h.block].image != TIKU_DOC_NONE) {
        const tiku_doc_image_t *im = &d->image[d->block[h.block].image];

        if (im->w > 0 && x >= d->line[line].x &&
            x < d->line[line].x + im->w) {
            h.kind = TIKU_DOC_HIT_IMAGE;
        }
    }
    return h;
}

const char *tiku_doc_link_href(const tiku_doc_t *d, int link)
{
    if (link < 0 || link >= d->nlink) return "";
    return d->link[link].href == TIKU_DOC_NONE ? ""
                                               : d->text + d->link[link].href;
}

int tiku_doc_link_pieces(const tiku_doc_t *d, int link, int *first, int *n)
{
    if (link < 0 || link >= d->nlink ||
        d->link[link].piece0 == TIKU_DOC_NONE) return 0;
    if (first) *first = d->link[link].piece0;
    if (n) *n = d->link[link].npiece;
    return 1;
}

int tiku_doc_link_next(const tiku_doc_t *d, int from, int back)
{
    if (!d->nlink) return TIKU_DOC_NONE;
    if (from < 0 || from >= d->nlink) return back ? d->nlink - 1 : 0;
    if (back) return from == 0 ? d->nlink - 1 : from - 1;
    return from + 1 == d->nlink ? 0 : from + 1;
}

int tiku_doc_anchor_y(const tiku_doc_t *d, const char *id)
{
    if (!id || !*id) return TIKU_DOC_NONE;
    for (int i = 0; i < d->nanchor; i++) {
        if (d->anchor[i].id == TIKU_DOC_NONE) continue;
        if (strcmp(d->text + d->anchor[i].id, id)) continue;
        if (d->anchor[i].block < 0 || d->anchor[i].block >= d->nblock) break;
        return (int)d->block[d->anchor[i].block].y;
    }
    return TIKU_DOC_NONE;
}

tiku_doc_place_t tiku_doc_place(const tiku_doc_t *d, int y)
{
    tiku_doc_place_t at;

    at.block = 0;
    at.into = 0;
    for (int i = 0; i < d->nblock; i++) {
        if (d->block[i].y > y) break;
        at.block = i;
        at.into = 0;
        for (int k = d->block[i].line0;
             k < d->block[i].line0 + d->block[i].nline && k < d->nline; k++) {
            if (d->line[k].y <= y) at.into = k - d->block[i].line0;
        }
    }
    return at;
}

int tiku_doc_place_y(const tiku_doc_t *d, tiku_doc_place_t at)
{
    if (at.block < 0 || at.block >= d->nblock) return 0;
    const tiku_doc_block_t *b = &d->block[at.block];
    int line = b->line0 + at.into;

    if (at.into > 0 && line < b->line0 + b->nline && line < d->nline) {
        return (int)d->line[line].y;
    }
    return (int)b->y;
}

int tiku_doc_check(const tiku_doc_t *d)
{
    int last_y = 0;

    for (int i = 0; i < d->npiece; i++) {
        const tiku_doc_piece_t *pc = &d->piece[i];

        if (pc->run < 0 || pc->run >= d->nrun) return 0;
        if (pc->off < d->run[pc->run].off) return 0;
        if (pc->off + pc->len > d->run[pc->run].off + d->run[pc->run].len) return 0;
        if (pc->len < 0 || pc->w < 0) return 0;
    }
    for (int i = 0; i < d->nline; i++) {
        const tiku_doc_line_t *ln = &d->line[i];

        if (ln->piece0 < 0 || ln->npiece < 0) return 0;
        if (ln->piece0 + ln->npiece > d->npiece) return 0;
        if (ln->block < 0 || ln->block >= d->nblock) return 0;
        /* y only goes down, except back to the top of a row for the
         * next cell of a table.  Nothing else may step backwards. */
        if (ln->y < last_y &&
            d->block[ln->block].kind != TIKU_DOC_BLOCK_CELL) return 0;
        last_y = (int)ln->y;
        if (ln->h <= 0) return 0;
    }
    for (int i = 0; i < d->nblock; i++) {
        const tiku_doc_block_t *b = &d->block[i];

        int down = 0;

        if (b->run0 < 0 || b->run0 + b->nrun > d->nrun) return 0;
        if (b->kind == TIKU_DOC_BLOCK_BOX && (b->end <= i || b->end > d->nblock ||
            b->w < 0 || b->h < 0)) return 0;
        if (b->nline && b->line0 + b->nline > d->nline) return 0;
        /* Inside one block y always goes down, a cell included: the
         * step backwards a table makes is between cells, never within
         * one. */
        for (int k = b->line0; k < b->line0 + b->nline && k < d->nline; k++) {
            if ((int)d->line[k].y < down) return 0;
            down = (int)d->line[k].y;
        }
    }
    for (int i = 0; i < d->nlink; i++) {
        if (d->link[i].piece0 == TIKU_DOC_NONE) continue;
        if (d->link[i].piece0 + d->link[i].npiece > d->npiece) return 0;
    }
    return 1;
}
