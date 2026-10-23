/*
 * KintsugiDesktop v0.01 Interface Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * tiku_access_linux_table.c - logical table coordinates and spans.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_access_linux_private.h"
#include <string.h>
/** @brief Find the logical cell covering a coordinate, including merged spans.
 */
static long
cell(ax_object_t *object, int row, int column)
{
    if (row < 0 || column < 0)
        return -1;
    for (size_t i = object->index + 1;
         i < tiku_access_count(object->window->tree); i++) {
        const tiku_access_node_t *node =
            tiku_access_node(object->window->tree, i);
        if (node->parent == object->index && node->role == TIKU_ACCESS_CELL &&
            (node->state & TIKU_ACCESS_VISIBLE) && (uint32_t)row >= node->row &&
            (uint32_t)column >= node->column &&
            (uint32_t)row - node->row < node->rows &&
            (uint32_t)column - node->column < node->columns)
            return (long)i;
    }
    return -1;
}
int
ax_table(ax_object_t *object, const char *interface, const char *method,
         GVariant *parameters, GDBusMethodInvocation *invocation)
{
    if (!strstr(interface, ".Table"))
        return 0;
    const tiku_access_node_t *node = ax_node(object);
    GVariant *result = NULL;
    if (strstr(interface, ".TableCell")) {
        if (!strcmp(method, "GetRowColumnSpan"))
            result = g_variant_new("(iiii)", node->row, node->column,
                                   node->rows, node->columns);
        else {
            GVariantBuilder headers;
            g_variant_builder_init(&headers, G_VARIANT_TYPE("a(so)"));
            result = g_variant_new("(@a(so))", g_variant_builder_end(&headers));
        }
        g_dbus_method_invocation_return_value(invocation, result);
        return 1;
    }
    int row = 0, column = 0, index = 0;
    if (!strcmp(method, "GetAccessibleAt") || !strcmp(method, "GetIndexAt") ||
        strstr(method, "ExtentAt") || !strcmp(method, "IsSelected")) {
        g_variant_get(parameters, "(ii)", &row, &column);
        long at = cell(object, row, column);
        const tiku_access_node_t *which =
            at < 0 ? NULL : tiku_access_node(object->window->tree, (size_t)at);
        if (!strcmp(method, "GetAccessibleAt"))
            result = g_variant_new("(@(so))", ax_ref(object, at));
        else if (!strcmp(method, "GetIndexAt"))
            result = g_variant_new(
                "(i)", at < 0 ? -1 : row * (int)node->columns + column);
        else if (!strcmp(method, "GetRowExtentAt"))
            result = g_variant_new("(i)", which ? (int)which->rows : 0);
        else if (!strcmp(method, "GetColumnExtentAt"))
            result = g_variant_new("(i)", which ? (int)which->columns : 0);
        else
            result = g_variant_new("(b)", which && which->state &
                                                       TIKU_ACCESS_SELECTED);
    } else if (!strcmp(method, "GetRowAtIndex") ||
               !strcmp(method, "GetColumnAtIndex")) {
        g_variant_get(parameters, "(i)", &index);
        int valid = index >= 0 && node->columns &&
                    (uint64_t)index < (uint64_t)node->rows * node->columns;
        result = g_variant_new("(i)", !valid ? -1
                                      : !strcmp(method, "GetRowAtIndex")
                                          ? index / (int)node->columns
                                          : index % (int)node->columns);
    } else if (strstr(method, "Description"))
        result = g_variant_new("(s)", "");
    else if (strstr(method, "Header"))
        result = g_variant_new("(@(so))", ax_ref(object, -1));
    else if (!strcmp(method, "GetSelectedRows") ||
             !strcmp(method, "GetSelectedColumns")) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("ai"));
        result = g_variant_new("(@ai)", g_variant_builder_end(&builder));
    } else
        result = g_variant_new("(b)", FALSE);
    g_dbus_method_invocation_return_value(invocation, result);
    return 1;
}
