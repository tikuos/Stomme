/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_xml.c - the XML reader: elements, attributes and text as a file
 * gives them, namespaces given their fixed prefixes whatever a file
 * calls them, the five entities and character references decoded and
 * nothing else expanded, UTF-16 and Latin-1 read as UTF-8, broken files
 * refused with the line they broke on, and thousands of mangled copies
 * of a real part read without harm.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tiku_xml.h"

static int fails;

/** @brief One row of the report, PASS or FAIL; a FAIL is counted. */
static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-7s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) {
        fails++;
    }
}

/** @brief @p s parsed, or NULL with why in @p why. */
static tiku_xml_t *
parse(const char *s, char *why, size_t max)
{
    return tiku_xml_parse(s, strlen(s), why, max);
}

/** @brief Whether @p n is text reading @p s. */
static int
text_is(const tiku_xml_node_t *n, const char *s)
{
    return n != NULL && n->name == NULL && strcmp(n->text, s) == 0;
}

/** @brief Elements, attributes, text and the namespaces' prefixes. */
static void
shape_tests(void)
{
    static const char doc[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<ns0:document xmlns:ns0=\"http://schemas.openxmlformats.org/"
        "wordprocessingml/2006/main\" xmlns:rr=\"http://schemas."
        "openxmlformats.org/officeDocument/2006/relationships\">"
        "<ns0:body><ns0:p><ns0:r><ns0:t xml:space=\"preserve\"> Hi </ns0:t>"
        "</ns0:r></ns0:p><ns0:sectPr><ns0:pgSz ns0:w=\"12240\" "
        "ns0:h='15840'/><ns0:x rr:id=\"rId7\"/></ns0:sectPr></ns0:body>"
        "</ns0:document>";
    char why[128];
    tiku_xml_t *x = parse(doc, why, sizeof why);
    tiku_xml_node_t *root = tiku_xml_root(x), *t, *sz;

    check(x != NULL && tiku_xml_is(root, "w:document"), "XM-001",
          "a WordprocessingML root is w:document whatever it was called");
    t = tiku_xml_path(root, "w:body/w:p/w:r/w:t");
    check(t != NULL && text_is(t->child, " Hi ") &&
          strcmp(tiku_xml_attr(t, "xml:space"), "preserve") == 0,
          "XM-002", "text keeps its spaces; xml: is always known");
    sz = tiku_xml_path(root, "w:body/w:sectPr/w:pgSz");
    check(sz != NULL && strcmp(tiku_xml_attr(sz, "w:w"), "12240") == 0 &&
          strcmp(tiku_xml_attr(sz, "w:h"), "15840") == 0 &&
          tiku_xml_attr(sz, "w:x") == NULL, "XM-003",
          "attributes by their qualified names, either quote");
    check(tiku_xml_attr(tiku_xml_next(sz, NULL), "r:id") != NULL &&
          sz->nattr == 2 && root->nattr == 0, "XM-004",
          "relationships are r:, and declarations are not attributes");
    check(tiku_xml_path(root, "w:body/w:nothing") == NULL &&
          tiku_xml_child(sz, NULL) == NULL && sz->parent != NULL &&
          tiku_xml_is(sz->parent, "w:sectPr"), "XM-005",
          "a missing step is NULL; parents lead back up");
    tiku_xml_free(x);
}

/** @brief Default namespaces, unknown ones, and one rebound. */
static void
namespace_tests(void)
{
    static const char types[] =
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/"
        "content-types\"><Default Extension=\"xml\" ContentType=\"a/b\"/>"
        "<Override PartName=\"/word/document.xml\"/></Types>";
    static const char odf[] =
        "<office:document-content xmlns:office=\"urn:oasis:names:tc:"
        "opendocument:xmlns:office:1.0\" xmlns:t=\"urn:oasis:names:tc:"
        "opendocument:xmlns:text:1.0\" xmlns:q=\"urn:example:unknown\">"
        "<office:body><t:p q:x=\"1\">One<q:y/></t:p>"
        "<t:p xmlns:t=\"urn:example:other\"/></office:body>"
        "</office:document-content>";
    tiku_xml_t *x = parse(types, NULL, 0);
    tiku_xml_node_t *r = tiku_xml_root(x), *p;

    check(x != NULL && tiku_xml_is(r, "ct:Types") &&
          tiku_xml_is(tiku_xml_child(r, NULL), "ct:Default") &&
          strcmp(tiku_xml_attr(tiku_xml_child(r, NULL), "Extension"),
                 "xml") == 0, "XM-010",
          "a default namespace names elements, never attributes");
    tiku_xml_free(x);
    x = parse(odf, NULL, 0);
    p = tiku_xml_path(tiku_xml_root(x), "office:body/text:p");
    check(p != NULL && tiku_xml_attr(p, "q:x") != NULL &&
          tiku_xml_is(tiku_xml_child(p, NULL), "q:y"), "XM-011",
          "OpenDocument's prefixes fixed; an unknown one kept as written");
    check(p != NULL && tiku_xml_is(tiku_xml_next(p, NULL), "t:p") &&
          tiku_xml_next(p, "text:p") == NULL, "XM-012",
          "a prefix rebound to an unknown namespace is not the known one");
    tiku_xml_free(x);
}

/** @brief References, CDATA, comments, instructions and a hostile DTD. */
static void
entity_tests(void)
{
    static const char refs[] =
        "<a>&lt;&gt;&amp;&quot;&apos;&#65;&#x42;&#x1F600;&#0;&nbsp;&#xZZ;"
        "&amp</a>";
    static const char laughs[] =
        "<?xml version=\"1.0\"?>\n<!DOCTYPE lolz [\n"
        " <!ENTITY lol \"lol\">\n"
        " <!ENTITY lol2 \"&lol;&lol;&lol;&lol;&lol;&lol;&lol;&lol;\">\n"
        " <!ENTITY ext SYSTEM \"file:///etc/passwd\">\n]>\n"
        "<lolz>&lol2;&ext;</lolz>";
    static const char mixed[] =
        "<r><!-- a > comment --><?pi a=\"b\"?><![CDATA[<b>&amp;</b>]]>"
        "x\r\ny\rz<e v=\"&#x3c;&amp;\"/></r>";
    char out[128];
    tiku_xml_t *x = parse(refs, NULL, 0);

    check(x != NULL && text_is(tiku_xml_root(x)->child,
          "<>&\"'AB\xf0\x9f\x98\x80\xef\xbf\xbd&nbsp;&#xZZ;&amp"),
          "XM-020", "the five and character references; others kept");
    tiku_xml_free(x);
    x = parse(laughs, NULL, 0);
    check(x != NULL && text_is(tiku_xml_root(x)->child, "&lol2;&ext;"),
          "XM-021", "a DTD is passed over: no entity of its expanded");
    tiku_xml_free(x);
    x = parse(mixed, NULL, 0);
    check(x != NULL && tiku_xml_text(tiku_xml_root(x), out, sizeof out) ==
          strlen("<b>&amp;</b>x\ny\nz") &&
          strcmp(out, "<b>&amp;</b>x\ny\nz") == 0, "XM-022",
          "CDATA as it stands; comments and instructions skipped; CR LF");
    check(x != NULL && strcmp(tiku_xml_attr(tiku_xml_child(tiku_xml_root(x),
          "e"), "v"), "<&") == 0, "XM-023",
          "attribute values decoded");
    tiku_xml_free(x);
}

/** @brief Mixed content, text gathered, and cut on a character's edge. */
static void
text_tests(void)
{
    static const char para[] =
        "<text:p xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:"
        "text:1.0\">Hello <text:span>big</text:span> w\xc3\xb6rld</text:p>";
    char out[64];
    tiku_xml_t *x = parse(para, NULL, 0);
    tiku_xml_node_t *p = tiku_xml_root(x);
    size_t n;

    check(x != NULL && text_is(p->child, "Hello ") &&
          tiku_xml_is(p->child->next, "text:span") &&
          text_is(p->child->next->next, " w\xc3\xb6rld"), "XM-030",
          "text and elements in the order written");
    n = tiku_xml_text(p, out, sizeof out);
    check(n == 16 && strcmp(out, "Hello big w\xc3\xb6rld") == 0, "XM-031",
          "the text of everything under an element, joined");
    n = tiku_xml_text(p, out, 13);
    check(n == 16 && strcmp(out, "Hello big w") == 0, "XM-032",
          "cut short, never half a character, whole length returned");
    tiku_xml_free(x);
}

/** @brief UTF-16 either way round, Latin-1, and a byte-order mark. */
static void
encoding_tests(void)
{
    static const char src[] = "<a b=\"\xc3\xa9\">\xe2\x82\xac\xf0\x9f\x98"
                              "\x80</a>";
    static const char latin[] = "<?xml version=\"1.0\" encoding=\"ISO-8859-1"
                                "\"?><a>caf\xe9</a>";
    unsigned char le[64], be[64];
    static const unsigned short u16[] = {'<', 'a', ' ', 'b', '=', '"', 0xe9,
        '"', '>', 0x20ac, 0xd83d, 0xde00, '<', '/', 'a', '>'};
    size_t i, n = sizeof u16 / sizeof u16[0];
    tiku_xml_t *x, *y;

    le[0] = 0xff; le[1] = 0xfe; be[0] = 0xfe; be[1] = 0xff;
    for (i = 0; i < n; i++) {
        le[2 + 2 * i] = (unsigned char)(u16[i] & 0xff);
        le[3 + 2 * i] = (unsigned char)(u16[i] >> 8);
        be[2 + 2 * i] = (unsigned char)(u16[i] >> 8);
        be[3 + 2 * i] = (unsigned char)(u16[i] & 0xff);
    }
    x = tiku_xml_parse(le, 2 + 2 * n, NULL, 0);
    y = tiku_xml_parse(be, 2 + 2 * n, NULL, 0);
    check(x != NULL && y != NULL &&
          strcmp(tiku_xml_attr(tiku_xml_root(x), "b"), "\xc3\xa9") == 0 &&
          text_is(tiku_xml_root(x)->child, "\xe2\x82\xac\xf0\x9f\x98\x80") &&
          text_is(tiku_xml_root(y)->child, "\xe2\x82\xac\xf0\x9f\x98\x80"),
          "XM-040", "UTF-16, little and big endian, pairs joined");
    tiku_xml_free(x);
    tiku_xml_free(y);
    x = parse(latin, NULL, 0);
    y = tiku_xml_parse("\xef\xbb\xbf<a/>", 7, NULL, 0);
    check(x != NULL && text_is(tiku_xml_root(x)->child, "caf\xc3\xa9") &&
          y != NULL && tiku_xml_is(tiku_xml_root(y), "a"), "XM-041",
          "Latin-1 as declared; a UTF-8 byte-order mark passed over");
    tiku_xml_free(x);
    tiku_xml_free(y);
    x = parse(src, NULL, 0);
    check(x != NULL, "XM-042", "UTF-8 as it is");
    tiku_xml_free(x);
}

/** @brief Windows-1252 declarations decode text and attribute punctuation. */
static void
windows_encoding_tests(void)
{
    static const char *enc[] = {"windows-1252", "cp1252", "WINDOWS-1252"};
    static const char bytes[] = "\x80\x82\x83\x84\x85\x86\x87\x88\x89"
        "\x8a\x8b\x8c\x8e\x91\x92\x93\x94\x95\x96\x97\x98\x99"
        "\x9a\x9b\x9c\x9e\x9f\xa0\xe9";
    static const char expected[] =
        "\xe2\x82\xac\xe2\x80\x9a\xc6\x92\xe2\x80\x9e\xe2\x80\xa6"
        "\xe2\x80\xa0\xe2\x80\xa1\xcb\x86\xe2\x80\xb0\xc5\xa0"
        "\xe2\x80\xb9\xc5\x92\xc5\xbd\xe2\x80\x98\xe2\x80\x99"
        "\xe2\x80\x9c\xe2\x80\x9d\xe2\x80\xa2\xe2\x80\x93\xe2\x80\x94"
        "\xcb\x9c\xe2\x84\xa2\xc5\xa1\xe2\x80\xba\xc5\x93\xc5\xbe"
        "\xc5\xb8\xc2\xa0\xc3\xa9";
    char doc[512];
    tiku_xml_t *x;

    for (size_t i = 0; i < sizeof enc / sizeof enc[0]; i++) {
        snprintf(doc, sizeof doc, "<?xml encoding='%s'?><a v='%s'>%s</a>",
                 enc[i], bytes, bytes);
        x = parse(doc, NULL, 0);
        check(x && text_is(tiku_xml_root(x)->child, expected) &&
              !strcmp(tiku_xml_attr(tiku_xml_root(x), "v"), expected),
              "D-094", "Windows-1252 characters decode in text and attributes");
        tiku_xml_free(x);
    }
    x = parse("<?xml encoding='ISO-8859-1'?><a>\x80\x93\x9f</a>", NULL, 0);
    check(x && text_is(tiku_xml_root(x)->child, "\xc2\x80\xc2\x93\xc2\x9f"),
          "D-094", "Latin-1 retains its C1 code points");
    tiku_xml_free(x);
    x = parse("<?xml encoding='cp1252'?><a>\x81\x8d\x8f\x90\x9d</a>", NULL, 0);
    check(x && text_is(tiku_xml_root(x)->child,
                       "\xc2\x81\xc2\x8d\xc2\x8f\xc2\x90\xc2\x9d"),
          "D-094", "undefined Windows-1252 slots retain their C1 code points");
    tiku_xml_free(x);
    {
        char many[1200], expected_many[3073];
        size_t head = strlen("<?xml encoding='cp1252'?><a>");

        memcpy(many, "<?xml encoding='cp1252'?><a>", head);
        memset(many + head, 0x80, 1024);
        memcpy(many + head + 1024, "</a>", 5);
        for (int i = 0; i < 1024; i++)
            memcpy(expected_many + 3 * i, "\xe2\x82\xac", 3);
        expected_many[3072] = 0;
        x = parse(many, NULL, 0);
        check(x && text_is(tiku_xml_root(x)->child, expected_many),
              "D-094", "Windows-1252 input can expand to three UTF-8 bytes each");
        tiku_xml_free(x);
    }
}

/** @brief Broken files: refused, with the line they broke on. */
static void
broken_tests(void)
{
    static const struct { const char *doc, *why; } bad[] = {
        {"<a>\n<b></a>", "line 2: an end tag closes another element"},
        {"<a><b>", "line 1: the file ends inside an element"},
        {"<a/><b/>", "line 1: a second element outside the first"},
        {"", "line 1: there is no element"},
        {"<a b=c/>", "line 1: an attribute's value is not quoted"},
        {"<a b/>", "line 1: an attribute has no value"},
        {"</a>", "line 1: an end tag closes nothing"},
        {"<a><!-- never", "line 1: a comment or instruction does not end"},
        {"<a b=\"x/>", "line 1: an attribute's value does not end"},
        {"<a><![CDATA[x", "line 1: a CDATA section does not end"},
        {"<", "line 1: the file ends inside a tag"},
    };
    char why[128], deep[TIKU_XML_DEPTH * 8 + 64];
    size_t i;
    int ok = 1;
    tiku_xml_t *x;

    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        x = parse(bad[i].doc, why, sizeof why);
        if (x != NULL || strcmp(why, bad[i].why) != 0) {
            printf("    %s -> %s\n", bad[i].doc, x ? "parsed" : why);
            ok = 0;
        }
        tiku_xml_free(x);
    }
    check(ok, "XM-050", "each broken file refused, saying where and why");
    deep[0] = '\0';
    for (i = 0; i <= TIKU_XML_DEPTH; i++) {
        strcat(deep, "<d>");
    }
    x = parse(deep, why, sizeof why);
    check(x == NULL && strstr(why, "nest too deeply") != NULL, "XM-051",
          "nesting past the bound is refused");
    x = tiku_xml_parse(NULL, 0, why, sizeof why);
    check(x == NULL && tiku_xml_root(NULL) == NULL &&
          tiku_xml_attr(NULL, "a") == NULL && tiku_xml_child(NULL, NULL)
          == NULL, "XM-052", "no bytes, and NULL nodes, answered");
}

/**
 * @brief A bounded parse that hits a bound fails TIKU_XML_LIMIT and says
 *        which bound in why; one given no bytes fails TIKU_XML_BAD.
 */
static void
limit_tests(void)
{
    static const struct {
        size_t bytes, nodes;        /* the bounds                       */
        const char *why;            /* what why must then say           */
    } hit[] = {
        {1u << 20, 1, "line 1: XML node limit exceeded"},
        {1u << 20, 64, "line 1: XML name length limit exceeded"},
        {20000, 64, "line 1: XML allocation budget exceeded"},
    };
    char doc[700], why[128];
    tiku_xml_limits_t limits = {0, 0, NULL, NULL};
    tiku_xml_status_t status;
    size_t i;
    int ok = 1;

    for (i = 0; i < sizeof hit / sizeof hit[0]; i++) {
        tiku_xml_t *x;

        /* Two elements, the second's name 600 bytes long. */
        snprintf(doc, sizeof doc, "<a><b%0599d/></a>", 0);
        limits.bytes = hit[i].bytes;
        limits.nodes = hit[i].nodes;
        x = tiku_xml_parse_limited(doc, strlen(doc), &limits, &status, why,
                                   sizeof why);
        if (x != NULL || status != TIKU_XML_LIMIT ||
            strcmp(why, hit[i].why) != 0) {
            printf("    bound %zu -> %s\n", i, x ? "parsed" : why);
            ok = 0;
        }
        tiku_xml_free(x);
    }
    check(ok, "XM-070", "a bound hit is named in why, not called a "
          "shortage of memory");
    check(tiku_xml_parse_limited(NULL, 0, NULL, &status, why, sizeof why)
          == NULL && status == TIKU_XML_BAD && strcmp(why, "no bytes") == 0,
          "XM-071", "no bytes is a bad document, not a shortage of memory");
}

/** @brief The next of a fixed stream of numbers, below @p n. */
static unsigned
next(unsigned n)
{
    static unsigned long long s = 0x9e3779b97f4a7c15ull;

    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return (unsigned)(s % n);
}

/** @brief A part the size of a long document, read quickly; then copies
 *         of a real one mangled thousands of ways, read without harm. */
static void
load_tests(void)
{
    static const char unit[] =
        "<w:p><w:pPr><w:pStyle w:val=\"Body\"/></w:pPr><w:r><w:rPr><w:b/>"
        "</w:rPr><w:t xml:space=\"preserve\">A line &amp; more </w:t></w:r>"
        "</w:p>";
    static const char head[] =
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/"
        "wordprocessingml/2006/main\"><w:body>";
    size_t u = strlen(unit), n = strlen(head) + 100000 * u + 32, i, len;
    char *big = malloc(n), *copy;
    clock_t t0;
    tiku_xml_t *x;
    long count = 0;
    tiku_xml_node_t *p;

    strcpy(big, head);
    for (i = 0, len = strlen(head); i < 100000; i++, len += u) {
        memcpy(big + len, unit, u);
    }
    strcpy(big + len, "</w:body></w:document>");
    t0 = clock();
    x = parse(big, NULL, 0);
    for (p = tiku_xml_child(tiku_xml_child(tiku_xml_root(x), "w:body"),
                            "w:p"); p != NULL; p = tiku_xml_next(p, "w:p")) {
        count++;
    }
    check(x != NULL && count == 100000 &&
          (double)(clock() - t0) / CLOCKS_PER_SEC < 2.0, "XM-060",
          "a 12 MB part of 100,000 paragraphs, read in under 2 s");
    tiku_xml_free(x);
    len = strlen(head) + 40 * strlen(unit);
    big[len] = '\0';
    copy = malloc(len + 1);
    for (i = 0; i < 5000; i++) {
        size_t k, cuts = 1 + next(6);

        memcpy(copy, big, len + 1);
        for (k = 0; k < cuts; k++) {
            copy[next((unsigned)len)] = "<>/=\"'&;:x \0#"[next(13)];
        }
        tiku_xml_free(tiku_xml_parse(copy, len - next(40), NULL, 0));
    }
    check(1, "XM-061", "5,000 mangled copies read without harm");
    free(copy);
    free(big);
}

/** @brief Every group. */
int
main(void)
{
    printf("XML: elements, namespaces, entities and hostile files\n");
    shape_tests();
    namespace_tests();
    entity_tests();
    text_tests();
    encoding_tests();
    windows_encoding_tests();
    broken_tests();
    limit_tests();
    load_tests();
    printf("%s\n", fails ? "FAIL" : "PASS: XML read into trees, namespaces "
           "fixed, nothing but the five entities expanded");
    return fails ? 1 : 0;
}
