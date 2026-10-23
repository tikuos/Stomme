/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_doc_css_inline.inl - streamed HTML attribute decoding for inline CSS.
 * SPDX-License-Identifier: Apache-2.0
 */

/** @brief Merge one decoded declaration without overriding important values. */
static void css_inline_piece(doc_css_t *css, doc_css_decl_t *result,
                              const char *text, size_t n, int overflow)
{
    if (overflow) { css->notices |= TIKU_DOC_CSS_LIMIT; return; }
    doc_css_decl_t piece = css_declarations(css,text,text+n);
    for (int property = 0; property < DOC_CSS_PROPERTIES; property++) {
        unsigned mask = 1u << property;
        if (!(piece.mask & mask)) continue;
        if ((result->important & mask) && !(piece.important & mask)) continue;
        css_copy_property(result,&piece,mask);
    }
}

/** @brief Decode inline references with a 512-byte declaration scratch buffer. */
static doc_css_decl_t css_inline_declarations(doc_css_t *css,
                                              const char *p, size_t length)
{
    doc_css_decl_t result = {0};
    if (length > TIKU_DOC_CSS_BYTES_MAX-css->bytes) {
        css->notices |= TIKU_DOC_CSS_LIMIT; return result;
    }
    size_t after_bytes = css->bytes+length;
    const char *end = p+length;
    char buffer[512], quote = 0, previous = 0;
    size_t n = 0;
    int comment = 0, escaped = 0, overflow = 0;
    while (p < end) {
        char decoded[8]; const char *after = p+1;
        int count;
        if (*p == '&') count = html_value_entity(p,end,decoded,&after);
        else { decoded[0] = *p; count = 1; }
        p = after;
        for (int i = 0; i < count; i++) {
            char c = decoded[i];
            if (n < sizeof buffer) buffer[n++] = c;
            else overflow = 1;
            if (comment) {
                if (previous == '*' && c == '/') { comment = 0; previous = 0; continue; }
            } else if (quote) {
                if (escaped) escaped = 0;
                else if (c == '\\') escaped = 1;
                else if (c == quote) quote = 0;
            } else if (previous == '/' && c == '*') comment = 1;
            else if (c == '\'' || c == '"') quote = c;
            else if (c == ';') {
                css_inline_piece(css,&result,buffer,n,overflow);
                n = 0; overflow = 0; previous = 0; continue;
            }
            previous = c;
        }
    }
    if (n || overflow) css_inline_piece(css,&result,buffer,n,overflow);
    css->bytes = after_bytes;
    return result;
}
