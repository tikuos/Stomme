/*
 * TikuOS Interface Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_doc_html.inl - HTML names, tag boundaries and attribute spans.
 * SPDX-License-Identifier: Apache-2.0
 */

/** @brief Whether a byte separates HTML tokens. */
static int html_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

/** @brief Whether a byte is an ASCII letter. */
static int html_letter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/** @brief Compare a bounded HTML name with a lowercase ASCII literal. */
static int html_name(const char *name, size_t length, const char *literal)
{
    if (strlen(literal) != length) return 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (unsigned char)literal[i]) return 0;
    }
    return 1;
}

/** @brief Compare two bounded HTML names, ignoring ASCII case. */
static int html_same(const char *a, size_t an, const char *b, size_t bn)
{
    if (an != bn) return 0;
    for (size_t i = 0; i < an; i++) {
        unsigned char ac = (unsigned char)a[i], bc = (unsigned char)b[i];
        if (ac >= 'A' && ac <= 'Z') ac += 'a' - 'A';
        if (bc >= 'A' && bc <= 'Z') bc += 'a' - 'A';
        if (ac != bc) return 0;
    }
    return 1;
}

/** @brief End after an unquoted '>', at a new '<', or at the terminator. */
static const char *html_tag_end(const char *p)
{
    char quote = 0;
    while (*p) {
        char c = *p++;
        if (quote) {
            if (c == quote) quote = 0;
        } else if (c == '\'' || c == '"') {
            quote = c;
        } else if (c == '>') {
            return p;
        } else if (c == '<') {
            return p - 1;
        }
    }
    return p;
}

/** @brief Find the first matching attribute, without copying its value. */
static int html_attribute(const char *p, const char *name,
                          const char **value, size_t *length)
{
    while (*p && *p != '>' && *p != '<') {
        while (html_space(*p) || *p == '/') p++;
        if (!*p || *p == '>' || *p == '<') break;
        const char *key = p;
        while (*p && !html_space(*p) && *p != '=' && *p != '>' &&
               *p != '<' && *p != '/') p++;
        size_t n = (size_t)(p - key);
        while (html_space(*p)) p++;
        if (*p != '=') continue;
        p++;
        while (html_space(*p)) p++;
        char quote = (*p == '\'' || *p == '"') ? *p++ : 0;
        const char *val = p;
        while (*p && (quote ? *p != quote :
               !html_space(*p) && *p != '>' && *p != '<')) p++;
        if (html_name(key, n, name)) {
            *value = val; *length = (size_t)(p - val);
            return 1;
        }
        if (quote && *p) p++;
    }
    return 0;
}

/** @brief Decode an entity within an attribute's value boundaries. */
static int html_value_entity(const char *p, const char *end, char *out,
                              const char **after)
{
    int n = entity(p + 1, out, after);
    if (*after > end || *after == p + 1) {
        out[0] = '&'; *after = p + 1; return 1;
    }
    return n;
}
