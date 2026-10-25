/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_resource_literals.c - typed JSON round trips and rejected partial inputs.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_literal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/** @brief Require complete rejection without a returned partial allocation. */
static void reject(const char *text)
{
    tiku_msg_t *value = (void *)1;
    assert(tiku_resource_literal_parse("json", text, &value) && !value);
}
int main(void)
{
    const char *valid[] = {
        "null",
        "true",
        "false",
        "-9223372036854775808",
        "9223372036854775807",
        "1.25e-3",
        "\"A\\u00e9\\ud83d\\ude00\"",
        "{\"value\":\"Sensor\",\"context\":\"abcd\"}",
        "[true,\"a\",3]",
        "{\"generation\":{\"$type\":\"counter\",\"$value\":\"18446744073709551615\"},\"mode\":{\"$type\":\"enum\",\"$value\":\"light\"},\"ink\":{\"$type\":\"color\",\"$value\":4294967295}}"};
    for (size_t i = 0; i < sizeof valid / sizeof valid[0]; i++) {
        tiku_msg_t *value = NULL, *copy = NULL;
        assert(!tiku_resource_literal_parse("json", valid[i], &value));
        char text[TIKU_RESOURCE_FRAME_MAX];
        int n = tiku_resource_literal_format(value, text, sizeof text - 1);
        assert(n > 0);
        text[n] = 0;
        assert(!tiku_resource_literal_parse("json", text, &copy) &&
               tiku_resource_value_equal(value, copy));
        tiku_msg_free(copy);
        char untouched[4] = "old";
        assert(tiku_resource_literal_format(value, untouched, 1) < 0 &&
               !memcmp(untouched, "old", 4));
        tiku_msg_free(value);
        size_t length = strlen(valid[i]);
        char *prefix = malloc(length + 1);
        assert(prefix);
        for (size_t at = 0; at < length; at++) {
            memcpy(prefix, valid[i], at);
            prefix[at] = 0;
            tiku_msg_t *partial = NULL;
            (void)tiku_resource_literal_parse("json", prefix, &partial);
            tiku_msg_free(partial);
        }
        free(prefix);
    }
    const char *invalid[] = {"",
                             "{",
                             "{\"a\"",
                             "{\"a\":",
                             "{\"a\":1,}",
                             "[1,]",
                             "[1",
                             "01",
                             "+1",
                             "1.",
                             "1e",
                             "NaN",
                             "1e999",
                             "9223372036854775808",
                             "true false",
                             "{\"a\":1,\"a\":2}",
                             "\"\\u0000\"",
                             "\"\\ud800\"",
                             "\"\\udc00\"",
                             "\"\\ud800x\"",
                             "\"\\q\"",
                             "\"\\",
                             "\"\x01\"",
                             "{\"$type\":\"counter\",\"$value\":\"-1\"}",
                             "{\"$type\":\"color\",\"$value\":4294967296}",
                             "{\"$type\":\"enum\"}"};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++)
        reject(invalid[i]);
    tiku_msg_t *value = NULL;
    assert(!tiku_resource_literal_parse("text", "a\nb", &value));
    tiku_msg_free(value);
    assert(!tiku_resource_literal_parse("color", "0xff00cc", &value));
    tiku_msg_free(value);
    assert(tiku_resource_literal_parse("integer", "1.5", &value) && !value);
    assert(!tiku_resource_literal_parse("number", "3", &value));
    assert(!strcmp(tiku_msg_find_string(value, "type", 0), "number"));
    tiku_msg_free(value);
    char deep[65];
    memset(deep, '[', 32);
    memset(deep + 32, ']', 32);
    deep[64] = 0;
    reject(deep);
    puts(
        "PASS resource literals: lossless custom types, Unicode, bounds and incomplete input");
    return 0;
}
