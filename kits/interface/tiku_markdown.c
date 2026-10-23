/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_markdown.c - documentation Markdown to help HTML, line by line.
 *
 * One renderer counts and emits: with no output buffer it only counts.
 * The scans that can pass over the same bytes again are charged against
 * a work bound of 64 steps a source byte plus 4096; a page that uses it
 * up fails the conversion.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_markdown.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef struct { const char *p; size_t n; } md_line_t;
typedef struct {
    char *out;
    size_t cap, used, work;
    int full, paragraph, quote, table, depth;
    int list[9], indent[9];
    char ids[64][96];
    int headings;
    tiku_markdown_options_t options;
} md_t;

/** @brief Append all @p n bytes or none, keeping room for the NUL. */
static void md_put(md_t *m, const char *s, size_t n)
{
    if (m->options.cancel && m->options.cancel(m->options.context)) m->full = 1;
    if (m->full) return;
    if (n >= m->cap - m->used) { m->full = 1; return; }
    if (m->out) memcpy(m->out + m->used, s, n);
    m->used += n;
    if (m->out) m->out[m->used] = 0;
}

/** @brief Append the string @p s unescaped. */
static void md_tag(md_t *m, const char *s) { md_put(m, s, strlen(s)); }

/** @brief Append @p n bytes with &, <, > and " escaped, for text and
 *         attribute values alike. */
static void md_escape(md_t *m, const char *s, size_t n)
{
    for (size_t i = 0; i < n && !m->full; i++) {
        switch (s[i]) {
        case '&': md_tag(m, "&amp;"); break;
        case '<': md_tag(m, "&lt;"); break;
        case '>': md_tag(m, "&gt;"); break;
        case '"': md_tag(m, "&quot;"); break;
        default: md_put(m, s+i, 1); break;
        }
    }
}

/**
 * @brief Offset of the first unescaped @p end in @p s, or @p n when there
 *        is none; reads nothing past @p n.
 */
static size_t md_delim(md_t *m, const char *s, size_t n,
                       const char *end, size_t len)
{
    for (size_t i = 0; i + len <= n; i++) {
        if (!(i % 4096) && m->options.cancel &&
            m->options.cancel(m->options.context)) { m->full = 1; return n; }
        if (!m->work) { m->full = 1; return n; }
        m->work--;
        if (s[i] == '\\' && i + 1 < n) { i++; continue; }
        if (!memcmp(s+i, end, len)) return i;
    }
    return n;
}

static void md_inline(md_t *m, const char *s, size_t n, int depth, int links);

/**
 * @brief Emit the inline link or image that starts @p s.
 *
 * The destination's parentheses must balance; a title after it is
 * accepted and left out.
 *
 * @return The bytes the link took, or 0 when @p s starts none
 */
static size_t md_link(md_t *m, const char *s, size_t n, int depth)
{
    int image = s[0] == '!';
    size_t start = image ? 2 : 1;
    if (n <= start || (image && s[1] != '[')) return 0;
    size_t label = md_delim(m, s+start, n-start, "](", 2);
    if (label == n-start) return 0;
    size_t begin = start+label+2, end = begin;
    int balance = 1;
    for (; end < n; end++) {
        if (!m->work) { m->full = 1; return 0; }
        m->work--;
        if (s[end] == '\\' && end+1 < n) { end++; continue; }
        if (s[end] == '(') balance++;
        if (s[end] == ')' && !--balance) break;
    }
    if (end == n) return 0;
    size_t a = begin, b = end;
    while (a < b && isspace((unsigned char)s[a])) a++;
    if (a < b && s[a] == '<') {
        a++; b = a;
        while (b < end && s[b] != '>') b++;
        if (b == end) return 0;
    } else {
        b = a;
        while (b < end && !isspace((unsigned char)s[b])) b++;
    }
    md_tag(m, image ? "<img src=\"" : "<a href=\"");
    for (size_t i = a; i < b; i++) {
        if (s[i] == '\\' && i+1 < b && ispunct((unsigned char)s[i+1])) i++;
        md_escape(m, s+i, 1);
    }
    md_tag(m, image ? "\" alt=\"" : "\">");
    if (image) md_escape(m, s+start, label);
    else md_inline(m, s+start, label, depth+1, 0);
    md_tag(m, image ? "\">" : "</a>");
    return end+1;
}

/**
 * @brief Emit inline text: escapes, code, emphasis and, when @p links is
 *        set, links, nested at most 8 deep.  Unmatched syntax stays text.
 */
static void md_inline(md_t *m, const char *s, size_t n, int depth, int links)
{
    if (depth >= 8) { md_escape(m, s, n); return; }
    for (size_t i = 0; i < n && !m->full;) {
        if (s[i] == '\\' && i+1 < n && ispunct((unsigned char)s[i+1])) {
            md_escape(m, s+i+1, 1); i += 2; continue;
        }
        if (links && (s[i] == '[' || s[i] == '!')) {
            size_t used = md_link(m, s+i, n-i, depth);
            if (used) { i += used; continue; }
        }
        char ch = s[i];
        if (ch == '`' || ch == '*' || ch == '_') {
            size_t count = 1;
            /* Charged to the work bound: a long run is counted again
             * from each of its positions. */
            while (i+count < n && s[i+count] == ch) {
                if (!m->work) { m->full = 1; return; }
                m->work--;
                count++;
            }
            if (ch != '`' && count > 2) count = 2;
            size_t tail = n-i-count;
            size_t end = md_delim(m, s+i+count, tail, s+i, count);
            int word = ch == '_' && i && isalnum((unsigned char)s[i-1]);
            if (!word && end && end < tail) {
                const char *open = ch == '`' ? "<code>" : count == 2 ? "<b>" : "<em>";
                const char *close = ch == '`' ? "</code>" : count == 2 ? "</b>" : "</em>";
                md_tag(m, open);
                if (ch == '`') md_escape(m, s+i+count, end);
                else md_inline(m, s+i+count, end, depth+1, links);
                md_tag(m, close); i += count+end+count; continue;
            }
        }
        md_escape(m, s+i, 1); i++;
    }
}

/** @brief Take one line ending in LF or CRLF from @p source, without its
 *         CR, all other whitespace kept. */
static md_line_t md_line(const char **source)
{
    const char *p = *source, *end = p;
    while (*end && *end != '\n') end++;
    *source = *end ? end+1 : end;
    size_t n = (size_t)(end-p);
    if (n && p[n-1] == '\r') n--;
    return (md_line_t){p, n};
}

/** @brief @p l without its leading and trailing whitespace. */
static md_line_t md_trim(md_line_t l)
{
    while (l.n && isspace((unsigned char)*l.p)) { l.p++; l.n--; }
    while (l.n && isspace((unsigned char)l.p[l.n-1])) l.n--;
    return l;
}

/** @brief End a paragraph before another block. */
static void md_paragraph_end(md_t *m)
{
    if (m->paragraph) md_tag(m, "</p>");
    m->paragraph = 0;
}

/** @brief Close the open lists, and their open items, down to @p depth. */
static void md_lists_end(md_t *m, int depth)
{
    while (m->depth > depth) {
        md_tag(m, m->list[--m->depth] == 2 ? "</li></ol>" : "</li></ul>");
    }
}

/** @brief Close the open paragraph, lists, quote and table. */
static void md_blocks_end(md_t *m)
{
    md_paragraph_end(m); md_lists_end(m, 0);
    if (m->quote) md_tag(m, "</blockquote>");
    if (m->table) md_tag(m, "</table>");
    m->quote = m->table = 0;
}

/**
 * @brief Whether @p l is at least @p min of @p marker, with spaces and tabs
 *        between: a rule, or a setext underline.
 */
static int md_rule(md_line_t l, char marker, int min)
{
    int count = 0;
    for (size_t i = 0; i < l.n; i++) {
        if (l.p[i] == marker) count++;
        else if (l.p[i] != ' ' && l.p[i] != '\t') return 0;
    }
    return count >= min;
}

/**
 * @brief Emit six document heading levels; help headings use text-derived
 *        unique IDs and combine levels three through six.
 */
static void md_heading(md_t *m, md_line_t l, int level)
{
    char slug[72], id[96], tag[128]; size_t used = 0;
    if (m->options.document) {
        snprintf(tag, sizeof tag, "<h%d>", level);
        md_tag(m, tag);
        md_inline(m, l.p, l.n, 0, 1);
        snprintf(tag, sizeof tag, "</h%d>", level);
        md_tag(m, tag);
        return;
    }
    for (size_t i = 0; i < l.n && used+1 < sizeof slug; i++) {
        unsigned char c = (unsigned char)l.p[i];
        if (c >= 128 || isalnum(c) || c == '_' || c == '-') slug[used++] = (char)tolower(c);
        else if (isspace(c) && used && slug[used-1] != '-') slug[used++] = '-';
    }
    while (used && slug[used-1] == '-') used--;
    slug[used] = 0; if (!used) strcpy(slug, "section");
    snprintf(id, sizeof id, "%s", slug);
    /* Charged to the work bound: a page of identical headings walks the
     * table again for each suffix tried. */
    for (int suffix = 1, i = 0; i < m->headings && i < 64;) {
        if (!m->work) { m->full = 1; return; }
        m->work--;
        if (!strcmp(id, m->ids[i])) {
            snprintf(id, sizeof id, "%s-%d", slug, suffix++); i = 0;
        } else i++;
    }
    if (m->headings < 64) strcpy(m->ids[m->headings], id);
    m->headings++;
    if (level > 3) level = 3;
    snprintf(tag, sizeof tag, "<h%d id=\"%s\">", level, id); md_tag(m, tag);
    md_inline(m, l.p, l.n, 0, 1);
    snprintf(tag, sizeof tag, "</h%d>", level); md_tag(m, tag);
}

/** @brief Whether @p l is a pipe table's delimiter row, alignment colons
 *         allowed. */
static int md_table_separator(md_line_t l)
{
    l = md_trim(l); int cells = 0;
    if (l.n && l.p[0] == '|') { l.p++; l.n--; }
    while (l.n) {
        size_t end = 0; while (end < l.n && l.p[end] != '|') end++;
        md_line_t cell = md_trim((md_line_t){l.p, end});
        if (cell.n && cell.p[0] == ':') { cell.p++; cell.n--; }
        if (cell.n && cell.p[cell.n-1] == ':') cell.n--;
        if (!md_rule(cell, '-', 3)) return 0;
        cells++;
        if (end == l.n) break;
        l.p += end+1; l.n -= end+1;
    }
    return cells > 0;
}

/** @brief Emit one table row; escaped pipes stay inside their cell. */
static void md_table_row(md_t *m, md_line_t l, int header)
{
    l = md_trim(l);
    if (l.n && l.p[0] == '|') { l.p++; l.n--; }
    md_tag(m, "<tr>");
    while (l.n && !m->full) {
        size_t end = md_delim(m, l.p, l.n, "|", 1);
        md_line_t cell = md_trim((md_line_t){l.p, end});
        md_tag(m, header ? "<th>" : "<td>");
        md_inline(m, cell.p, cell.n, 0, 1); md_tag(m, header ? "</th>" : "</td>");
        if (end == l.n) break;
        l.p += end+1; l.n -= end+1;
    }
    md_tag(m, "</tr>");
}

/**
 * @brief Length of the list marker that starts @p l, or 0 for none;
 *        @p kind is set to 1 for a bullet and 2 for a number.
 */
static size_t md_list_marker(md_line_t l, int *kind)
{
    size_t n = 0;
    if (l.n > 1 && strchr("-*+", l.p[0]) && isspace((unsigned char)l.p[1])) {
        *kind = 1; return 2;
    }
    while (n < l.n && n < 9 && isdigit((unsigned char)l.p[n])) n++;
    if (n && n+1 < l.n && (l.p[n] == '.' || l.p[n] == ')') && isspace((unsigned char)l.p[n+1])) {
        *kind = 2; return n+2;
    }
    return 0;
}

/**
 * @brief Emit a list item, opening and closing lists by its indentation,
 *        at most eight deep in help mode or nine deep in document mode.
 */
static void md_list_item(md_t *m, md_line_t l, size_t marker, int kind, int indent)
{
    md_paragraph_end(m);
    while (m->depth && indent < m->indent[m->depth-1]) md_lists_end(m, m->depth-1);
    if (m->depth && indent == m->indent[m->depth-1] && kind != m->list[m->depth-1])
        md_lists_end(m, m->depth-1);
    if (!m->depth || (indent > m->indent[m->depth-1] &&
        m->depth < (m->options.document ? 9 : 8))) {
        m->list[m->depth] = kind; m->indent[m->depth++] = indent;
        md_tag(m, kind == 2 ? "<ol>" : "<ul>");
    } else md_tag(m, "</li>");
    if (kind == 2 && m->options.document) {
        unsigned value = 0;
        char tag[48];

        for (size_t i = 0; i < marker && l.p[i] >= '0' && l.p[i] <= '9'; i++)
            value = value * 10 + (unsigned)(l.p[i] - '0');
        snprintf(tag, sizeof tag, "<li value=\"%u\">", value);
        md_tag(m, tag);
    } else md_tag(m, "<li>");
    md_inline(m, l.p+marker, l.n-marker, 0, 1);
}

/**
 * @brief When @p l opens a code fence, emit every line up to its closing
 *        fence as literal text and return 1; otherwise return 0.
 */
static int md_fence(md_t *m, md_line_t l, const char **source)
{
    if (!l.n || (l.p[0] != '`' && l.p[0] != '~')) return 0;
    size_t count = 0; while (count < l.n && l.p[count] == l.p[0]) count++;
    if (count < 3) return 0;
    md_blocks_end(m); md_tag(m, "<pre>");
    while (**source && !m->full) {
        md_line_t code = md_line(source), trim = md_trim(code);
        size_t n = 0; while (n < trim.n && trim.p[n] == l.p[0]) n++;
        if (n >= count && n == trim.n) break;
        md_escape(m, code.p, code.n);
        if (code.p[code.n] == '\n' || code.p[code.n] == '\r') md_tag(m, "\n");
    }
    md_tag(m, "</pre>"); return 1;
}

/**
 * @brief Render one line, looking one line ahead for a setext underline
 *        or a table's delimiter row.
 */
static void md_block(md_t *m, md_line_t raw, const char **source)
{
    md_line_t l = md_trim(raw);
    if (!l.n) { md_blocks_end(m); return; }
    if (md_fence(m, l, source)) return;
    size_t hashes = 0; while (hashes < l.n && l.p[hashes] == '#') hashes++;
    if (hashes && hashes <= 6 && (hashes == l.n || l.p[hashes] == ' ')) {
        md_blocks_end(m); l.p += hashes; l.n -= hashes; l = md_trim(l);
        size_t end = l.n; while (end && l.p[end-1] == '#') end--;
        if (end < l.n && end && l.p[end-1] == ' ') l.n = end-1;
        md_heading(m, md_trim(l), (int)hashes); return;
    }
    const char *next = *source; md_line_t below = md_trim(md_line(&next));
    if (md_rule(below, '=', 1) || md_rule(below, '-', 1)) {
        md_blocks_end(m); md_heading(m, l, below.p[0] == '=' ? 1 : 2); *source = next; return;
    }
    if (md_rule(l, '-', 3) || md_rule(l, '*', 3) || md_rule(l, '_', 3)) {
        md_blocks_end(m); md_tag(m, "<hr>"); return;
    }
    if (memchr(l.p, '|', l.n) && md_table_separator(below)) {
        md_blocks_end(m); md_tag(m, "<table>"); m->table = 1;
        md_table_row(m, l, 1); *source = next; return;
    }
    if (m->table && memchr(l.p, '|', l.n)) { md_table_row(m, l, 0); return; }
    if (m->table) { md_tag(m, "</table>"); m->table = 0; }
    int quote = l.p[0] == '>';
    if (quote != m->quote) {
        md_blocks_end(m); m->quote = quote;
        if (quote) md_tag(m, "<blockquote>");
    }
    if (quote) { l.p++; l.n--; l = md_trim(l); }
    int kind = 0; size_t marker = md_list_marker(l, &kind);
    int indent = (int)(l.p-raw.p);
    if (marker) { md_list_item(m, l, marker, kind, indent); return; }
    if (m->depth && indent > m->indent[m->depth-1]) {
        md_tag(m, " "); md_inline(m, l.p, l.n, 0, 1); return;
    }
    md_lists_end(m, 0);
    if (!m->paragraph) { md_tag(m, "<p>"); m->paragraph = 1; }
    else md_tag(m, " ");
    int hard = raw.n >= 2 && raw.p[raw.n-1] == ' ' && raw.p[raw.n-2] == ' ';
    if (l.n && l.p[l.n-1] == '\\') { l.n--; hard = 1; }
    md_inline(m, l.p, l.n, 0, 1); if (hard) md_tag(m, "<br>");
}

/**
 * @brief Render @p markdown into @p out, or only count when @p out is NULL.
 *
 * @return The bytes written, NUL included, or 0 on failure
 */
static size_t md_render(char *out, size_t capacity, const char *markdown,
                        const tiku_markdown_options_t *options)
{
    if (out && capacity) out[0] = 0;
    if (!capacity || !markdown) return 0;
    size_t n = strlen(markdown);
    if (n > (SIZE_MAX - 4096) / 64) return 0;
    md_t m = {.out = out, .cap = capacity, .work = n * 64 + 4096};
    if (options) m.options = *options;
    const char *source = markdown;
    while (*source && !m.full) {
        md_line_t line = md_line(&source); md_block(&m, line, &source);
    }
    md_blocks_end(&m);
    if (m.full && out) out[0] = 0;
    return m.full ? 0 : m.used + 1;
}

size_t tiku_markdown_html_size(const char *markdown)
{
    return md_render(NULL, SIZE_MAX, markdown, NULL);
}

int tiku_markdown_html(char *out, size_t capacity, const char *markdown)
{
    return out && md_render(out, capacity, markdown, NULL) != 0;
}

size_t
tiku_markdown_convert(char *out, size_t capacity, const char *markdown,
                        const tiku_markdown_options_t *options)
{
    return md_render(out, capacity, markdown, options);
}
