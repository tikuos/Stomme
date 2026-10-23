/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_lexer.c - an application's lexer colouring a view: its
 * runs checked before the view uses them, its state carried from line to
 * line, and the JSON lexer the kit supplies.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"

#include <limits.h>
#include <string.h>

void
tiku_codeview_lexer_changed(tiku_codeview_t *view)
{
    if (view == NULL || view->lexing)
        return;
    tiku_cv_style_reset(view);
    tiku_cv_hot_forget(view);
    view->lexer_status = TIKU_EDIT_OK;
}

tiku_edit_status_t
tiku_codeview_set_lexer(tiku_codeview_t *view,
                        const tiku_codeview_lexer_t *lexer)
{
    tiku_codeview_lexer_t none;

    if (view == NULL || (lexer != NULL && lexer->line == NULL))
        return TIKU_EDIT_INVALID;
    if (view->lexing)
        return TIKU_EDIT_BUSY;
    memset(&none, 0, sizeof none);
    if (lexer == NULL)
        lexer = &none;
    if (view->lexer.line == lexer->line && view->lexer.ctx == lexer->ctx &&
        view->lexer.initial_state == lexer->initial_state &&
        view->lexer.stateful == lexer->stateful)
        return TIKU_EDIT_OK;
    view->lexer = *lexer;
    if (view->fold_levels != NULL)
        (void)tiku_codeview_set_fold_levels(view, NULL, 0, 0);
    tiku_codeview_lexer_changed(view);
    return TIKU_EDIT_OK;
}

tiku_edit_status_t
tiku_codeview_lexer_status(const tiku_codeview_t *view)
{
    return view != NULL ? view->lexer_status : TIKU_EDIT_INVALID;
}

/** @brief A line's runs on their way from the application's lexer. */
typedef struct {
    tiku_codeview_t *view;      /* the view coloured                    */
    const char *text;           /* the line                             */
    size_t length;              /* the line's bytes                     */
    size_t at;                  /* the bytes the runs cover so far      */
    int invalid;                /* a run broke the rules                */
    tiku_syntax_run_fn emit;    /* where the runs go, and its context   */
    void *ctx;
} checked_runs_t;

/** @brief Pass on a run of @p len bytes in @p ink, which must cover bytes
 *         of the line not yet covered, end at a character boundary and be
 *         a built-in ink or a defined style; the first that does not
 *         stops the line's runs. */
static void
checked_run(void *ctx, tiku_ink_t ink, int len)
{
    checked_runs_t *r = ctx;
    size_t end;

    if (r->invalid)
        return;
    if (len <= 0 || (unsigned)ink >= TIKU_CODEVIEW_STYLE_COUNT ||
        ((unsigned)ink >= TIKU_CV_INKS && !r->view->style_defined[ink]) ||
        (size_t)len > r->length - r->at) {
        r->invalid = 1;
        return;
    }
    end = r->at + (size_t)len;
    if (end < r->length && ((unsigned char)r->text[end] & 0xc0) == 0x80) {
        r->invalid = 1;
        return;
    }
    r->at = end;
    if (r->emit != NULL)
        r->emit(r->ctx, ink, len);
}

int
tiku_cv_lex(tiku_codeview_t *v, const char *text, uint32_t in,
            uint32_t *out, tiku_syntax_run_fn emit, void *ctx)
{
    checked_runs_t r;
    uint32_t next = in;
    tiku_edit_status_t st;

    if (v->lexer.line == NULL) {
        tiku_syntax_state_t state = in;

        tiku_syntax_runs(v->lang, text, in, &state, emit, ctx);
        if (out != NULL)
            *out = state;
        v->lexer_status = TIKU_EDIT_OK;
        return 1;
    }
    if (v->lexing) {
        v->lexer_status = TIKU_EDIT_BUSY;
        return 0;
    }
    memset(&r, 0, sizeof r);
    r.view = v;
    r.text = text;
    r.length = strlen(text);
    r.emit = emit;
    r.ctx = ctx;
    v->lexing = 1;
    v->doc->busy++;
    st = v->lexer.line(v->lexer.ctx, text, r.length, in, &next,
                       emit != NULL ? checked_run : NULL, &r);
    v->doc->busy--;
    v->lexing = 0;
    if (st == TIKU_EDIT_OK && emit != NULL && (r.invalid || r.at != r.length))
        st = TIKU_EDIT_INVALID_RANGE;
    v->lexer_status = st;
    if (st != TIKU_EDIT_OK)
        return 0;
    if (out != NULL)
        *out = next;
    return 1;
}

/** @brief Whether @p c may be part of a JSON word: an ASCII letter, a
 *         digit, '_' or any byte past ASCII. */
static int
json_word(unsigned char c)
{
    return c >= 0x80 || c == '_' || (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/** @brief Whether @p c may continue a JSON number. */
static int
json_digit(char c)
{
    return (c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
           c == '+' || c == '-';
}

/** @brief The end of the token of @p s starting at @p i, @p n bytes in
 *         all, and its ink in @p ink: a string to its closing quote, a
 *         number, a word, which is a keyword when it is true, false or
 *         null, or else one byte. */
static size_t
json_token(const char *s, size_t n, size_t i, tiku_ink_t *ink)
{
    unsigned char c = (unsigned char)s[i++];
    size_t start = i - 1;

    *ink = TIKU_INK_PLAIN;
    if (c == '"') {
        *ink = TIKU_INK_STR;
        while (i < n) {
            if (s[i] == '\\' && i + 1 < n) {
                i += 2;
                continue;
            }
            if (s[i++] == '"')
                break;
        }
    } else if ((c >= '0' && c <= '9') || c == '-') {
        *ink = TIKU_INK_NUM;
        while (i < n && json_digit(s[i]))
            i++;
    } else if (json_word(c)) {
        while (i < n && json_word((unsigned char)s[i]))
            i++;
        if ((i - start == 4 && (memcmp(s + start, "true", 4) == 0 ||
                                memcmp(s + start, "null", 4) == 0)) ||
            (i - start == 5 && memcmp(s + start, "false", 5) == 0))
            *ink = TIKU_INK_WORD;
    }
    return i;
}

/** @brief Colour one line of JSON, @p n bytes of @p s, by its tokens,
 *         without parsing it: any text is coloured, valid or not. */
static tiku_edit_status_t
json_line(void *ctx, const char *s, size_t n, uint32_t in, uint32_t *out,
          tiku_syntax_run_fn emit, void *emit_ctx)
{
    size_t i = 0;

    (void)ctx;
    (void)in;
    *out = 0;
    if (n > INT_MAX)
        return TIKU_EDIT_LIMIT;
    if (emit == NULL)
        return TIKU_EDIT_OK;
    while (i < n) {
        tiku_ink_t ink;
        size_t end = json_token(s, n, i, &ink);

        emit(emit_ctx, ink, (int)(end - i));
        i = end;
    }
    return TIKU_EDIT_OK;
}

const tiku_codeview_lexer_t *
tiku_codeview_json_lexer(void)
{
    static const tiku_codeview_lexer_t lexer = { NULL, 0, 0, json_line };

    return &lexer;
}
