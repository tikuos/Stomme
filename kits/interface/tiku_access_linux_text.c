/*
 * KintsugiDesktop v0.01 Interface Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * tiku_access_linux_text.c - scalar text, selected ranges and explicit editing.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_access_linux_private.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
/** @brief Extract a checked scalar range without splitting UTF-8. */
static char *
range(const tiku_access_node_t *node, int first, int last)
{
    int count = tiku_access_characters(node->text, strlen(node->text));
    if (last == -1)
        last = count;
    if (first < 0 || last < first || last > count)
        return NULL;
    long a = tiku_access_byte(node->text, (size_t)first),
         b = tiku_access_byte(node->text, (size_t)last);
    return a < 0 || b < a ? NULL : g_strndup(node->text + a, (size_t)(b - a));
}
/** @brief Identify logical token boundaries, using retained lines for wrapping.
 */
static int
boundary_at(const tiku_access_node_t *node, const char *at, int offset,
            unsigned kind)
{
    if (!offset || !*at)
        return 1;
    const char *previous = g_utf8_find_prev_char(node->text, at);
    gunichar before = g_utf8_get_char(previous), after = g_utf8_get_char(at);
    if (kind == 1)
        return g_unichar_isspace(before) && !g_unichar_isspace(after);
    if (kind == 2)
        return !g_unichar_isspace(before) && g_unichar_isspace(after);
    if (kind == 3 || kind == 4) {
        const char *scan = kind == 3 ? previous : at;
        while (scan > node->text && g_unichar_isspace(g_utf8_get_char(scan)))
            scan = g_utf8_find_prev_char(node->text, scan);
        gunichar mark = g_utf8_get_char(scan);
        return (mark == '.' || mark == '!' || mark == '?' || mark == '\n') &&
               (kind == 3 ? !g_unichar_isspace(after)
                          : g_unichar_isspace(after));
    }
    if (kind == 5 || kind == 6) {
        if (node->character_lines && (size_t)offset < node->character_count)
            return node->character_lines[offset] !=
                   node->character_lines[offset - 1];
        return kind == 5 ? before == '\n' : after == '\n';
    }
    return 0;
}
/** @brief Return a scalar range between two native boundary points. */
static void
boundary(const tiku_access_node_t *node, int offset, unsigned kind, int *first,
         int *last)
{
    int count = tiku_access_characters(node->text, strlen(node->text));
    offset = offset < 0 ? 0 : offset > count ? count : offset;
    *first = offset;
    *last = offset < count ? offset + 1 : offset;
    if (!kind || offset == count)
        return;
    if (kind > 6) {
        *first = 0;
        *last = count;
        return;
    }
    *first = 0;
    *last = count;
    const char *at = node->text;
    for (int i = 0; i <= count; i++) {
        if (boundary_at(node, at, i, kind)) {
            if (i <= offset)
                *first = i;
            else {
                *last = i;
                break;
            }
        }
        if (*at)
            at = g_utf8_next_char(at);
    }
}
/** @brief Native hyperlink child by logical order. */
static long
text_link(const ax_object_t *object, int index)
{
    for (size_t i = object->index + 1;
         i < tiku_access_count(object->window->tree); i++) {
        const tiku_access_node_t *node =
            tiku_access_node(object->window->tree, i);
        if (node->parent == object->index &&
            (node->role == TIKU_ACCESS_LINK ||
             node->role == TIKU_ACCESS_IMAGE) &&
            index-- == 0)
            return (long)i;
    }
    return -1;
}
/** @brief Apply validated editing payloads only to an editable logical node. */
static GVariant *
edit(ax_object_t *object, const char *method, GVariant *parameters)
{
    const tiku_access_node_t *node = ax_node(object);
    int count = tiku_access_characters(node->text, strlen(node->text)),
        first = 0, last = count;
    const char *text = "";
    char *prefix = NULL;
    unsigned action = TIKU_ACCESS_REPLACE;
    if (!strcmp(method, "SetTextContents"))
        g_variant_get(parameters, "(&s)", &text);
    else if (!strcmp(method, "InsertText")) {
        int length;
        g_variant_get(parameters, "(i&si)", &first, &text, &length);
        last = first;
        action = TIKU_ACCESS_INSERT;
        int chars = tiku_access_characters(text, strlen(text));
        if (length < 0 || chars < 0)
            return g_variant_new("(b)", FALSE);
        size_t bytes = strlen(text);
        if ((size_t)length < bytes) {
            if (tiku_access_characters(text, (size_t)length) < 0)
                return g_variant_new("(b)", FALSE);
            prefix = g_strndup(text, (size_t)length);
            text = prefix;
        }
    } else if (!strcmp(method, "DeleteText")) {
        g_variant_get(parameters, "(ii)", &first, &last);
        action = TIKU_ACCESS_DELETE;
    } else
        return g_variant_new("(b)", FALSE);
    int accepted = first >= 0 && last >= first && last <= count &&
                   strlen(text) <= 262144u &&
                   tiku_access_characters(text, strlen(text)) >= 0 &&
                   ax_action(object, action, first, last, text);
    g_free(prefix);
    return g_variant_new("(b)", accepted);
}
int
ax_text(ax_object_t *object, const char *interface, const char *method,
        GVariant *parameters, GDBusMethodInvocation *invocation)
{
    const tiku_access_node_t *node = ax_node(object);
    if (!node)
        return 0;
    GVariant *result = NULL;
    int first = 0, last = 0;
    if (strstr(interface, "EditableText")) {
        if (!strcmp(method, "CopyText")) {
            g_dbus_method_invocation_return_dbus_error(
                invocation, "org.a11y.atspi.Error.NotSupported",
                "Use the application Copy command for clipboard publication");
            return 1;
        }
        result = edit(object, method, parameters);
    } else if (strstr(interface, "Hypertext")) {
        if (!strcmp(method, "GetNLinks")) {
            int n = 0;
            while (text_link(object, n) >= 0)
                n++;
            result = g_variant_new("(i)", n);
        } else if (!strcmp(method, "GetLink")) {
            int index;
            g_variant_get(parameters, "(i)", &index);
            result = g_variant_new(
                "(@(so))", ax_hyperlink_ref(object, text_link(object, index)));
        } else {
            int offset, index = -1;
            g_variant_get(parameters, "(i)", &offset);
            for (int i = 0;; i++) {
                long id = text_link(object, i);
                if (id < 0)
                    break;
                if (offset >= ax_link_offset(object->window->objects + id, 0) &&
                    offset < ax_link_offset(object->window->objects + id, 1)) {
                    index = i;
                    break;
                }
            }
            result = g_variant_new("(i)", index);
        }
    } else if (strstr(interface, "Hyperlink")) {
        if (!strcmp(method, "GetURI"))
            result = g_variant_new("(s)", node->target);
        else if (!strcmp(method, "GetObject"))
            result =
                g_variant_new("(@(so))", ax_ref(object, (long)object->index));
        else
            result = g_variant_new("(b)", TRUE);
    } else if (!strstr(interface, ".Text"))
        return 0;
    else if (!strcmp(method, "GetText")) {
        g_variant_get(parameters, "(ii)", &first, &last);
        char *text = range(node, first, last);
        if (text) {
            result = g_variant_new("(s)", text);
            g_free(text);
        }
    } else if (!strcmp(method, "GetCharacterAtOffset")) {
        int offset;
        g_variant_get(parameters, "(i)", &offset);
        long byte =
            offset >= 0 ? tiku_access_byte(node->text, (size_t)offset) : -1;
        result =
            g_variant_new("(i)", byte < 0 || !node->text[byte]
                                     ? 0
                                     : (int)g_utf8_get_char(node->text + byte));
    } else if (!strcmp(method, "GetNSelections"))
        result = g_variant_new("(i)",
                               node->selection_start >= 0 &&
                                   node->selection_end > node->selection_start);
    else if (!strcmp(method, "GetSelection")) {
        int index;
        g_variant_get(parameters, "(i)", &index);
        result = g_variant_new("(ii)", index == 0 ? node->selection_start : -1,
                               index == 0 ? node->selection_end : -1);
    } else if (!strcmp(method, "SetCaretOffset")) {
        g_variant_get(parameters, "(i)", &first);
        int count = tiku_access_characters(node->text, strlen(node->text));
        result = g_variant_new("(b)", first >= 0 && first <= count &&
                                          ax_action(object, TIKU_ACCESS_SELECT,
                                                    first, first, NULL));
    } else if (!strcmp(method, "SetSelection") ||
               !strcmp(method, "AddSelection") ||
               !strcmp(method, "RemoveSelection")) {
        int index = 0;
        if (!strcmp(method, "SetSelection"))
            g_variant_get(parameters, "(iii)", &index, &first, &last);
        else if (!strcmp(method, "AddSelection"))
            g_variant_get(parameters, "(ii)", &first, &last);
        else {
            g_variant_get(parameters, "(i)", &index);
            first = last = node->caret;
        }
        int count = tiku_access_characters(node->text, strlen(node->text));
        result = g_variant_new("(b)", index == 0 && first >= 0 &&
                                          last >= first && last <= count &&
                                          ax_action(object, TIKU_ACCESS_SELECT,
                                                    first, last, NULL));
    } else if (strstr(method, "Extents")) {
        unsigned coordinates;
        int offset;
        if (!strcmp(method, "GetCharacterExtents")) {
            g_variant_get(parameters, "(iu)", &offset, &coordinates);
            first = offset;
            last = offset >= 0 && offset < INT_MAX ? offset + 1 : -1;
        } else
            g_variant_get(parameters, "(iiu)", &first, &last, &coordinates);
        tiku_rect_t bounds = {0};
        if (first >= 0 && last >= first &&
            (uint32_t)last <= node->character_count) {
            for (int i = first; i < last; i++) {
                tiku_rect_t b = node->character_bounds[i];
                if (i == first)
                    bounds = b;
                else {
                    int right = MAX(bounds.x + bounds.w, b.x + b.w),
                        bottom = MAX(bounds.y + bounds.h, b.y + b.h);
                    bounds.x = MIN(bounds.x, b.x);
                    bounds.y = MIN(bounds.y, b.y);
                    bounds.w = right - bounds.x;
                    bounds.h = bottom - bounds.y;
                }
            }
        }
        bounds = ax_bounds(object, bounds, coordinates);
        result = !strcmp(method, "GetCharacterExtents")
                     ? g_variant_new("(iiii)", bounds.x, bounds.y, bounds.w,
                                     bounds.h)
                     : g_variant_new("((iiii))", bounds.x, bounds.y, bounds.w,
                                     bounds.h);
    } else if (!strcmp(method, "GetOffsetAtPoint")) {
        int x, y, found = -1;
        unsigned coordinates;
        g_variant_get(parameters, "(iiu)", &x, &y, &coordinates);
        for (uint32_t i = 0; i < node->character_count; i++) {
            tiku_rect_t b =
                ax_bounds(object, node->character_bounds[i], coordinates);
            if (x >= b.x && y >= b.y && (int64_t)x < (int64_t)b.x + b.w &&
                (int64_t)y < (int64_t)b.y + b.h) {
                found = (int)i;
                break;
            }
        }
        result = g_variant_new("(i)", found);
    } else if (!strcmp(method, "GetStringAtOffset") ||
               !strcmp(method, "GetTextAtOffset") ||
               !strcmp(method, "GetTextBeforeOffset") ||
               !strcmp(method, "GetTextAfterOffset")) {
        int offset;
        unsigned kind;
        g_variant_get(parameters, "(iu)", &offset, &kind);
        if (!strcmp(method, "GetStringAtOffset")) {
            static const unsigned granularities[] = {0, 1, 3, 5, 7};
            kind = kind < 5 ? granularities[kind] : 7;
        }
        boundary(node, offset, kind, &first, &last);
        if (!strcmp(method, "GetTextBeforeOffset"))
            if (first > 0)
                boundary(node, first - 1, kind, &first, &last);
            else
                first = last = 0;
        if (!strcmp(method, "GetTextAfterOffset"))
            boundary(node, last, kind, &first, &last);
        char *text = range(node, first, last);
        result = g_variant_new("(sii)", text ? text : "", first, last);
        g_free(text);
    } else if (!strcmp(method, "GetAttributes") ||
               !strcmp(method, "GetAttributeRun"))
        result = g_variant_new(
            "(@a{ss}ii)", ax_attributes(), 0,
            tiku_access_characters(node->text, strlen(node->text)));
    else if (!strcmp(method, "GetDefaultAttributes") ||
             !strcmp(method, "GetDefaultAttributeSet"))
        result = g_variant_new("(@a{ss})", ax_attributes());
    else if (!strcmp(method, "GetAttributeValue"))
        result = g_variant_new("(s)", "");
    if (result)
        g_dbus_method_invocation_return_value(invocation, result);
    else
        g_dbus_method_invocation_return_dbus_error(
            invocation, "org.a11y.atspi.Error.InvalidArgument",
            "Invalid text range");
    return 1;
}
