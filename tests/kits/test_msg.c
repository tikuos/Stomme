/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_msg.c - a message that describes itself.
 *
 * Two halves.  First that it keeps what it is given: every type back out
 * as it went in, lists in order, a message inside a message, and the flat
 * bytes exactly as the wire says, because the other end may not be this
 * build and a wire format nobody has written the bytes of is one that
 * drifts.  Then that it survives what it is not given: a message arrives
 * from a cable, so every prefix of a good one and every single-byte
 * change to one must be turned away or read whole, and neither may be a
 * way in.  A field of a type this build has never met comes back out
 * unharmed, the version tolerance the fixed structs cannot offer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_msg.h"

static int fails;

static void
check(int cond, const char *what)
{
    printf("  [%s] TS-MSG  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        fails++;
    }
}

/** @brief Walk everything in @p m, so a reader with ASan has to touch it. */
static void
touch_all(const tiku_msg_t *m)
{
    int i, n = tiku_msg_field_count(m);

    for (i = 0; i < n; i++) {
        tiku_msg_type_t type;
        int count, j;
        const char *name = tiku_msg_field_at(m, i, &type, &count);

        if (name == NULL) {
            continue;
        }
        (void)tiku_msg_type_name(type);
        for (j = 0; j < count; j++) {
            int32_t i32;
            size_t blen;
            const char *s;
            tiku_msg_t *sub;

            (void)tiku_msg_find_int32(m, name, j, &i32);
            (void)tiku_msg_find_blob(m, name, j, &blen);
            s = tiku_msg_find_string(m, name, j);
            if (s != NULL) {
                (void)strlen(s);
            }
            sub = tiku_msg_find_message(m, name, j);
            if (sub != NULL) {
                touch_all(sub);
                tiku_msg_free(sub);
            }
        }
    }
}

int
main(void)
{
    tiku_msg_t *m, *back, *sub;
    unsigned char *flat;
    size_t n, wrote, got;

    /*
     * Line-buffered, because this test is run under a sanitizer that ends
     * the process itself when it finds something.  Buffered output would
     * be thrown away at exactly the moment it was worth reading, and the
     * run would report only that something went wrong, not where.
     */
    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("a message that describes itself (TS-MSG)\n");

    /*----------------------------------------------------------------*/
    /* What goes in comes out                                          */
    /*----------------------------------------------------------------*/
    {
        tiku_msg_rect_t r;
        int b = 0;
        int8_t i8 = 0;
        int16_t i16 = 0;
        int32_t i32 = 0;
        int64_t i64 = 0;
        float f = 0.0f, x = 0.0f, y = 0.0f;
        double d = 0.0;
        int ok;

        r.x = 1.5f; r.y = -2.5f; r.w = 640.0f; r.h = 480.25f;

        m = tiku_msg_new(0x4B494B55u);
        check(m != NULL && tiku_msg_what(m) == 0x4B494B55u,
              "a message remembers what it is");

        ok = tiku_msg_add_bool(m, "on", 1) &&
             tiku_msg_add_int8(m, "i8", -128) &&
             tiku_msg_add_int16(m, "i16", -32768) &&
             tiku_msg_add_int32(m, "i32", -2147483647 - 1) &&
             tiku_msg_add_int64(m, "i64", (int64_t)-9007199254740993LL) &&
             tiku_msg_add_float(m, "f", 0.1f) &&
             tiku_msg_add_double(m, "d", 0.1) &&
             tiku_msg_add_string(m, "s", "caf\xc3\xa9 \xce\xb1\xce\xb2") &&
             tiku_msg_add_blob(m, "bytes", "\x00\x01\xff\x00", 4u) &&
             tiku_msg_add_point(m, "pt", 3.25f, -4.75f) &&
             tiku_msg_add_rect(m, "r", r) &&
             tiku_msg_add_ref(m, "path", "/Desktop/notes.txt");
        check(ok, "every kind of value goes in");

        check(tiku_msg_find_bool(m, "on", 0, &b) && b == 1 &&
              tiku_msg_find_int8(m, "i8", 0, &i8) && i8 == -128 &&
              tiku_msg_find_int16(m, "i16", 0, &i16) && i16 == -32768 &&
              tiku_msg_find_int32(m, "i32", 0, &i32) &&
              i32 == -2147483647 - 1 &&
              tiku_msg_find_int64(m, "i64", 0, &i64) &&
              i64 == (int64_t)-9007199254740993LL,
              "and every whole number comes back to the bit, edges included");

        check(tiku_msg_find_float(m, "f", 0, &f) && f == 0.1f &&
              tiku_msg_find_double(m, "d", 0, &d) && d == 0.1,
              "a float and a double come back exactly, not nearly");

        check(strcmp(tiku_msg_find_string(m, "s", 0),
                     "caf\xc3\xa9 \xce\xb1\xce\xb2") == 0,
              "a string keeps its UTF-8");
        check(strcmp(tiku_msg_find_ref(m, "path", 0),
                     "/Desktop/notes.txt") == 0, "a ref keeps its path");
        {
            const unsigned char *p = (const unsigned char *)
                tiku_msg_find_blob(m, "bytes", 0, &got);

            check(p != NULL && got == 4u && p[0] == 0u && p[2] == 0xFFu,
                  "and a blob keeps the zero bytes inside it");
        }
        check(tiku_msg_find_point(m, "pt", 0, &x, &y) &&
              x == 3.25f && y == -4.75f, "a point comes back");
        {
            tiku_msg_rect_t o;

            check(tiku_msg_find_rect(m, "r", 0, &o) && o.x == 1.5f &&
                  o.y == -2.5f && o.w == 640.0f && o.h == 480.25f,
                  "and a rect comes back whole");
        }
    }

    /*----------------------------------------------------------------*/
    /* Asking for what is not there                                    */
    /*----------------------------------------------------------------*/
    {
        int32_t i32 = 4242;

        check(!tiku_msg_find_int32(m, "nothing", 0, &i32) && i32 == 4242,
              "a name nobody has leaves the answer alone");
        check(!tiku_msg_find_int32(m, "s", 0, &i32) && i32 == 4242,
              "and so does a name that is there but is another type");
        check(!tiku_msg_find_int32(m, "i32", 1, &i32) && i32 == 4242,
              "and so does an index past the end");
        check(tiku_msg_find_string(m, "i32", 0) == NULL,
              "a number is not handed back as a string");
        check(!tiku_msg_add_int32(m, "s", 1),
              "and a name already meaning a string will not take a number");
    }

    /*----------------------------------------------------------------*/
    /* A field is a list whenever it is asked to be                    */
    /*----------------------------------------------------------------*/
    {
        tiku_msg_t *l = tiku_msg_new(1u);
        int i, ok = 1;

        for (i = 0; i < 300; i++) {
            char buf[32];

            sprintf(buf, "/Desktop/item-%d", i);
            ok = ok && tiku_msg_add_ref(l, "refs", buf);
        }
        check(ok && tiku_msg_count(l, "refs") == 300,
              "three hundred refs go under one name");
        check(strcmp(tiku_msg_find_ref(l, "refs", 0),
                     "/Desktop/item-0") == 0 &&
              strcmp(tiku_msg_find_ref(l, "refs", 299),
                     "/Desktop/item-299") == 0,
              "and they keep their order -- which is what a drag is");
        check(tiku_msg_count(l, "refs") == 300 &&
              tiku_msg_field_count(l) == 1,
              "a repeated name is one field, not three hundred");
        check(tiku_msg_count(l, "nothing") == 0,
              "and a name nobody has counts zero");
        tiku_msg_free(l);
    }

    /*----------------------------------------------------------------*/
    /* Looking at what is there without being told                     */
    /*----------------------------------------------------------------*/
    {
        tiku_msg_type_t type = TIKU_MSG_T_BOOL;
        int count = 0;
        const char *name = tiku_msg_field_at(m, 0, &type, &count);

        check(name != NULL && strcmp(name, "on") == 0 &&
              type == TIKU_MSG_T_BOOL && count == 1,
              "a message can be asked what its first field is");
        check(tiku_msg_field_count(m) == 12,
              "and how many it holds");
        check(tiku_msg_field_at(m, 12, NULL, NULL) == NULL &&
              tiku_msg_field_at(m, -1, NULL, NULL) == NULL,
              "past either end there is nothing");
        check(strcmp(tiku_msg_type_name(TIKU_MSG_T_RECT), "rect") == 0 &&
              strcmp(tiku_msg_type_name((tiku_msg_type_t)999),
                     "unknown") == 0,
              "and a type can say its name, even one it does not know");
        check(tiku_msg_has(m, "s", TIKU_MSG_T_STRING) &&
              !tiku_msg_has(m, "s", TIKU_MSG_T_INT32) &&
              !tiku_msg_has(m, "gone", TIKU_MSG_T_STRING),
              "has() answers about the name AND the type together");
    }

    /*----------------------------------------------------------------*/
    /* The flat form, byte for byte                                    */
    /*----------------------------------------------------------------*/
    {
        static const unsigned char want[] = {
            0x54, 0x4B, 0x4D, 0x53,             /* 'TKMS'            */
            0x44, 0x33, 0x22, 0x11,             /* what, LE          */
            0x01, 0x00, 0x00, 0x00,             /* one field         */
            0x01, 0x00, 'n',                    /* name, u16 length  */
            0x04, 0x00, 0x00, 0x00,             /* type: int32       */
            0x01, 0x00, 0x00, 0x00,             /* one value         */
            0x04, 0x00, 0x00, 0x00,             /* four bytes of it  */
            0x07, 0x00, 0x00, 0x00              /* seven, LE         */
        };
        unsigned char buf[64];
        tiku_msg_t *one = tiku_msg_new(0x11223344u);

        (void)tiku_msg_add_int32(one, "n", 7);
        check(tiku_msg_flat_size(one) == sizeof want,
              "the flat size is known before a byte is written");
        check(tiku_msg_flatten(one, buf, sizeof buf, &wrote) &&
              wrote == sizeof want &&
              memcmp(buf, want, sizeof want) == 0,
              "and the bytes are the ones written down, little-endian");
        check(!tiku_msg_flatten(one, buf, sizeof want - 1u, NULL),
              "a buffer one byte short is refused, not half-filled");
        tiku_msg_free(one);
    }

    /*----------------------------------------------------------------*/
    /* Out and back                                                    */
    /*----------------------------------------------------------------*/
    n = tiku_msg_flat_size(m);
    flat = (unsigned char *)malloc(n);
    check(flat != NULL && tiku_msg_flatten(m, flat, n, &wrote) &&
          wrote == n, "the whole message flattens");
    back = tiku_msg_unflatten(flat, n, &got);
    check(back != NULL && got == n &&
          tiku_msg_what(back) == 0x4B494B55u &&
          tiku_msg_field_count(back) == 12,
          "and comes back with its code and all its fields");
    {
        tiku_msg_rect_t o;
        int64_t i64 = 0;
        double d = 0.0;

        check(tiku_msg_find_int64(back, "i64", 0, &i64) &&
              i64 == (int64_t)-9007199254740993LL &&
              tiku_msg_find_double(back, "d", 0, &d) && d == 0.1 &&
              tiku_msg_find_rect(back, "r", 0, &o) && o.h == 480.25f &&
              strcmp(tiku_msg_find_string(back, "s", 0),
                     "caf\xc3\xa9 \xce\xb1\xce\xb2") == 0,
              "with every value the same on the other side");
    }
    tiku_msg_free(back);

    /*----------------------------------------------------------------*/
    /* A copy that owes the original nothing                           */
    /*----------------------------------------------------------------*/
    {
        tiku_msg_t *c = tiku_msg_copy(m);
        const char *s;

        check(c != NULL && tiku_msg_field_count(c) == 12,
              "a copy has everything the original had");
        tiku_msg_free(m);          /* the original goes away first */
        m = NULL;
        s = tiku_msg_find_string(c, "s", 0);
        check(s != NULL && strcmp(s, "caf\xc3\xa9 \xce\xb1\xce\xb2") == 0,
              "and still reads after the original is gone");
        check(tiku_msg_remove(c, "s") && !tiku_msg_remove(c, "s") &&
              tiku_msg_field_count(c) == 11 &&
              tiku_msg_find_string(c, "s", 0) == NULL,
              "a field can be taken out, once");
        {
            int32_t i32 = 0;

            check(tiku_msg_find_int32(c, "i32", 0, &i32) &&
                  i32 == -2147483647 - 1,
                  "and what was after it in the list is still reachable");
        }
        tiku_msg_clear(c);
        check(tiku_msg_field_count(c) == 0 &&
              tiku_msg_what(c) == 0x4B494B55u,
              "clearing empties it and leaves it the message it was");
        tiku_msg_free(c);
    }
    tiku_msg_free(NULL);
    check(tiku_msg_what(NULL) == 0u &&
          tiku_msg_flat_size(NULL) == 0u &&
          tiku_msg_copy(NULL) == NULL,
          "and nothing at all is handled as nothing at all");

    /*----------------------------------------------------------------*/
    /* A message inside a message                                      */
    /*----------------------------------------------------------------*/
    {
        tiku_msg_t *outer = tiku_msg_new(0xAAu);
        tiku_msg_t *item;
        int i;

        for (i = 0; i < 3; i++) {
            item = tiku_msg_new(0xBBu);
            (void)tiku_msg_add_int32(item, "index", i);
            (void)tiku_msg_add_string(item, "name",
                                           i == 1 ? "second" : "other");
            (void)tiku_msg_add_message(outer, "item", item);
            tiku_msg_free(item);   /* the sub was COPIED in */
        }
        check(tiku_msg_count(outer, "item") == 3,
              "three messages go under one name -- a drag with its own"
              " metadata per item");

        n = tiku_msg_flat_size(outer);
        free(flat);
        flat = (unsigned char *)malloc(n);
        (void)tiku_msg_flatten(outer, flat, n, NULL);
        back = tiku_msg_unflatten(flat, n, NULL);
        sub = (back != NULL)
                  ? tiku_msg_find_message(back, "item", 1) : NULL;
        {
            int32_t idx = -1;

            check(sub != NULL && tiku_msg_what(sub) == 0xBBu &&
                  tiku_msg_find_int32(sub, "index", 0, &idx) &&
                  idx == 1 &&
                  strcmp(tiku_msg_find_string(sub, "name", 0),
                         "second") == 0,
                  "and the middle one survives the wire whole");
        }
        tiku_msg_free(sub);
        tiku_msg_free(back);
        tiku_msg_free(outer);
    }

    /*
     * Nesting deep.  A container that read nested messages by recursing
     * would meet its stack here; this one holds a nested message
     * flattened and opens one level per ask, so depth costs the CALLER
     * a loop and costs the parser nothing.
     */
    {
        tiku_msg_t *cur = tiku_msg_new(0xDEEDu);
        int i, depth = 0;
        int built = 1;

        (void)tiku_msg_add_string(cur, "leaf", "bottom");
        for (i = 0; i < 400; i++) {
            tiku_msg_t *up = tiku_msg_new((uint32_t)i);

            if (up == NULL || !tiku_msg_add_message(up, "in", cur)) {
                built = 0;
                tiku_msg_free(up);
                break;
            }
            tiku_msg_free(cur);
            cur = up;
        }
        n = tiku_msg_flat_size(cur);
        free(flat);
        flat = (unsigned char *)malloc(n);
        (void)tiku_msg_flatten(cur, flat, n, NULL);
        tiku_msg_free(cur);

        cur = tiku_msg_unflatten(flat, n, NULL);
        check(built && cur != NULL, "four hundred messages deep flattens"
              " and is read back without a deep stack");
        while (cur != NULL) {
            tiku_msg_t *inner = tiku_msg_find_message(cur, "in", 0);

            if (inner == NULL) {
                break;
            }
            tiku_msg_free(cur);
            cur = inner;
            depth++;
        }
        check(depth == 400 && cur != NULL &&
              strcmp(tiku_msg_find_string(cur, "leaf", 0), "bottom") == 0,
              "and the bottom of it still says what it said");
        tiku_msg_free(cur);
    }

    /*----------------------------------------------------------------*/
    /* A type this build has never met                                 */
    /*                                                                 */
    /* The reason for the length on every value.  An older reader must  */
    /* step over a field it does not understand and carry it across     */
    /* untouched, so a message can gain a field without every end       */
    /* having to be rebuilt at once.                                    */
    /*----------------------------------------------------------------*/
    {
        static const unsigned char future[] = {
            0x54, 0x4B, 0x4D, 0x53,             /* 'TKMS'              */
            0x99, 0x00, 0x00, 0x00,             /* what                */
            0x02, 0x00, 0x00, 0x00,             /* two fields          */

            0x06, 0x00, 'f', 'u', 't', 'u', 'r', 'e',
            0x92, 0x24, 0x00, 0x00,             /* type 9362: not ours */
            0x01, 0x00, 0x00, 0x00,             /* one value           */
            0x03, 0x00, 0x00, 0x00,             /* three bytes of it   */
            0xDE, 0xAD, 0xBE,                   /* which mean nothing  */

            0x03, 0x00, 'o', 'l', 'd',          /* and one it does know */
            0x04, 0x00, 0x00, 0x00,             /* type: int32         */
            0x01, 0x00, 0x00, 0x00,
            0x04, 0x00, 0x00, 0x00,
            0x2A, 0x00, 0x00, 0x00              /* forty-two           */
        };
        tiku_msg_t *fm;
        tiku_msg_type_t type = TIKU_MSG_T_BOOL;
        int count = 0;

        fm = tiku_msg_unflatten(future, sizeof future, &got);
        check(fm != NULL && got == sizeof future &&
              tiku_msg_field_count(fm) == 2,
              "a message with a type we have never met still reads");
        {
            int32_t i32 = 0;

            check(fm != NULL &&
                  tiku_msg_find_int32(fm, "old", 0, &i32) && i32 == 42,
                  "and the field beyond it, which we DO know, is reached");
        }
        check(fm != NULL &&
              tiku_msg_field_at(fm, 0, &type, &count) != NULL &&
              (int)type == 9362 && count == 1,
              "the stranger is still described: its type and its count");
        {
            unsigned char out[sizeof future];

            check(fm != NULL && tiku_msg_flatten(fm, out, sizeof out,
                                                      &wrote) &&
                  wrote == sizeof future &&
                  memcmp(out, future, sizeof future) == 0,
                  "and it is handed on byte for byte, not dropped");
        }
        tiku_msg_free(fm);
    }

    /*----------------------------------------------------------------*/
    /* Bytes that arrived from somewhere else                          */
    /*----------------------------------------------------------------*/
    {
        tiku_msg_t *good = tiku_msg_new(7u);
        size_t i;
        int refused = 0;

        (void)tiku_msg_add_string(good, "s", "hello");
        (void)tiku_msg_add_int32(good, "n", 3);
        (void)tiku_msg_add_blob(good, "b", "\x01\x02", 2u);
        sub = tiku_msg_new(8u);
        (void)tiku_msg_add_double(sub, "d", 2.5);
        (void)tiku_msg_add_message(good, "sub", sub);
        tiku_msg_free(sub);

        n = tiku_msg_flat_size(good);
        free(flat);
        flat = (unsigned char *)malloc(n);
        (void)tiku_msg_flatten(good, flat, n, NULL);
        tiku_msg_free(good);

        /* Every prefix short of the whole is not a message. */
        for (i = 0; i < n; i++) {
            tiku_msg_t *t = tiku_msg_unflatten(flat, i, NULL);

            if (t == NULL) {
                refused++;
            } else {
                touch_all(t);
                tiku_msg_free(t);
            }
        }
        check(refused == (int)n,
              "every prefix of a good message is refused, not half-read");
        check(tiku_msg_unflatten(NULL, n, NULL) == NULL,
              "and no buffer at all is refused too");

        /* And no single change to it is a way in. */
        {
            static const unsigned char paint[] = {
                0x00u, 0x01u, 0x7Fu, 0x80u, 0xFFu
            };
            size_t j, k;
            int read_back = 0, unstable = 0;

            for (j = 0; j < n; j++) {
                unsigned char keep = flat[j];

                for (k = 0; k < sizeof paint; k++) {
                    tiku_msg_t *t;

                    flat[j] = paint[k];
                    t = tiku_msg_unflatten(flat, n, NULL);
                    if (t == NULL) {
                        continue;
                    }
                    read_back++;
                    touch_all(t);

                    /*
                     * Whatever it is, it must be the same thing said
                     * twice: a changed byte may make another message,
                     * but not one that reads differently the second
                     * time round.
                     */
                    {
                        size_t sz = tiku_msg_flat_size(t);
                        unsigned char *again = (unsigned char *)malloc(sz);
                        tiku_msg_t *twice = NULL;

                        if (again != NULL &&
                            tiku_msg_flatten(t, again, sz, NULL)) {
                            twice = tiku_msg_unflatten(again, sz, NULL);
                        }
                        if (twice == NULL ||
                            tiku_msg_what(twice) !=
                                tiku_msg_what(t) ||
                            tiku_msg_field_count(twice) !=
                                tiku_msg_field_count(t)) {
                            unstable++;
                        }
                        tiku_msg_free(twice);
                        free(again);
                    }
                    tiku_msg_free(t);
                }
                flat[j] = keep;
            }
            check(read_back > 0 && unstable == 0,
                  "and a one-byte change either fails to read or reads the"
                  " same thing twice -- never something in between");
        }

        /*
         * The two that must be turned away by name, because getting them
         * wrong is a read past the end rather than a wrong answer: a
         * string whose bytes do not end, and a number the wrong size.
         */
        {
            unsigned char bad[] = {
                0x54, 0x4B, 0x4D, 0x53, 0x01, 0x00, 0x00, 0x00,
                0x01, 0x00, 0x00, 0x00,
                0x01, 0x00, 's',
                0x08, 0x00, 0x00, 0x00,             /* string */
                0x01, 0x00, 0x00, 0x00,
                0x02, 0x00, 0x00, 0x00, 'h', 'i'    /* no terminator */
            };

            check(tiku_msg_unflatten(bad, sizeof bad, NULL) == NULL,
                  "a string whose bytes do not end is refused");
            bad[27] = 'h'; bad[28] = '\0';     /* the same two, ending */
            {
                tiku_msg_t *t = tiku_msg_unflatten(bad, sizeof bad,
                                                             NULL);
                const char *v = (t != NULL)
                                    ? tiku_msg_find_string(t, "s", 0)
                                    : NULL;

                check(v != NULL && strcmp(v, "h") == 0,
                      "while the same bytes ending are taken, and read");
                tiku_msg_free(t);
            }
            bad[15] = 0x04;                     /* say int32 instead */
            check(tiku_msg_unflatten(bad, sizeof bad, NULL) == NULL,
                  "and an int32 carrying two bytes is refused");
        }
        {
            unsigned char noname[] = {
                0x54, 0x4B, 0x4D, 0x53, 0x01, 0x00, 0x00, 0x00,
                0x01, 0x00, 0x00, 0x00,
                0x00, 0x00,                         /* a name of nothing */
                0x04, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00
            };

            check(tiku_msg_unflatten(noname, sizeof noname, NULL) == NULL,
                  "a field with no name is not a field");
            noname[12] = 0xFF; noname[13] = 0xFF;
            check(tiku_msg_unflatten(noname, sizeof noname, NULL) == NULL,
                  "and a name running past the end is refused");
        }
        {
            unsigned char holed[] = {
                0x54, 0x4B, 0x4D, 0x53, 0x01, 0x00, 0x00, 0x00,
                0x01, 0x00, 0x00, 0x00,
                0x03, 0x00, 'a', 0x00, 'b',         /* a name with a hole */
                0x04, 0x00, 0x00, 0x00,             /* int32              */
                0x01, 0x00, 0x00, 0x00,
                0x04, 0x00, 0x00, 0x00,
                0x2A, 0x00, 0x00, 0x00
            };

            check(tiku_msg_unflatten(holed, sizeof holed, NULL) == NULL,
                  "and a name with a zero byte inside it, which would read"
                  " as a shorter name than it was written as");
            holed[15] = 'x';
            {
                tiku_msg_t *t = tiku_msg_unflatten(holed,
                                                             sizeof holed,
                                                             NULL);
                int32_t i32 = 0;

                check(t != NULL &&
                      tiku_msg_find_int32(t, "axb", 0, &i32) &&
                      i32 == 42,
                      "while the same field with a whole name is taken");
                tiku_msg_free(t);
            }
        }
        {
            unsigned char huge[] = {
                0x54, 0x4B, 0x4D, 0x53, 0x01, 0x00, 0x00, 0x00,
                0xFF, 0xFF, 0xFF, 0xFF              /* four billion fields */
            };

            check(tiku_msg_unflatten(huge, sizeof huge, NULL) == NULL,
                  "and a header claiming more fields than there are bytes"
                  " for is not believed");
        }
        {
            unsigned char wrong[] = { 'N', 'O', 'P', 'E', 0, 0, 0, 0,
                                      0, 0, 0, 0 };

            check(tiku_msg_unflatten(wrong, sizeof wrong, NULL) == NULL,
                  "and bytes that are not a message at all are refused");
        }

        /*
         * And then rubbish, at length.  The sweep above changes one byte
         * of something already right, which finds the checks that are off
         * by one; this finds the shapes nothing sane would build.  The
         * numbers come from a seed written down here, so a failure is the
         * same failure tomorrow.
         */
        {
            uint32_t seed = 0x544B4D53u;
            unsigned char noise[256];
            int round, read_back = 0;

            for (round = 0; round < 40000; round++) {
                size_t len, b;
                tiku_msg_t *t;

                seed = seed * 1664525u + 1013904223u;
                len = 4u + (size_t)(seed >> 20) % (sizeof noise - 4u);
                for (b = 0; b < len; b++) {
                    seed = seed * 1664525u + 1013904223u;
                    noise[b] = (unsigned char)(seed >> 16);
                }
                if ((round & 1) == 0 && len >= 12u) {
                    noise[0] = 0x54; noise[1] = 0x4B;    /* say 'TKMS', and */
                    noise[2] = 0x4D; noise[3] = 0x53;
                    noise[8] &= 0x03u;                   /* a field count   */
                    noise[9] = 0u;                       /* small enough to */
                    noise[10] = 0u;                      /* be believed, so */
                    noise[11] = 0u;                      /* it gets inside  */
                }
                t = tiku_msg_unflatten(noise, len, NULL);
                if (t != NULL) {
                    read_back++;
                    touch_all(t);
                    tiku_msg_free(t);
                }
            }
            check(read_back > 0, "forty thousand buffers of noise are read"
                  " or refused, and some of them do get inside");
        }
    }
    free(flat);

    if (fails > 0) {
        printf("\nFAILURES\n");
        return 1;
    }
    printf("\nthe message holds what it is given, and holds against what"
           " it is not\n");
    return 0;
}
