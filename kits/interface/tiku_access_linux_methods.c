/*
 * KintsugiDesktop v0.01 Interface Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * tiku_access_linux_methods.c - native logical hierarchy, geometry and actions.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_access_linux_private.h"
#include <stdio.h>
#include <string.h>
/** @brief Native role number for the portable semantic role. */
static unsigned
role(const ax_object_t *object)
{
    static const unsigned roles[] = {23, 94, 85, 55, 56, 73,
                                     83, 88, 27, 37, 43};
    const tiku_access_node_t *node = ax_node(object);
    return node ? roles[node->role] : 75;
}
/** @brief Native role name matching the logical content. */
static const char *
role_name(const ax_object_t *object)
{
    static const char *names[] = {"frame",    "document text", "section",
                                  "table",    "table cell",    "paragraph",
                                  "heading",  "link",          "image",
                                  "page tab", "push button"};
    const tiku_access_node_t *node = ax_node(object);
    return node ? names[node->role] : "application";
}
/** @brief Number of immediate logical children of this object. */
static int
children(const ax_object_t *object)
{
    if (object->window)
        return object->window->objects[object->index].children;
    int count = 0;
    for (unsigned i = 0; i < AX_WINDOWS; i++)
        count += object->native->windows[i].tree != NULL;
    return count;
}
/** @brief Child reference in logical order. */
static GVariant *
child(const ax_object_t *object, int which)
{
    if (which < 0)
        return ax_ref(object, -1);
    if (!object->window) {
        for (unsigned i = 0; i < AX_WINDOWS; i++)
            if (object->native->windows[i].tree && which-- == 0)
                return ax_ref(object->native->windows[i].objects, 0);
    } else
        for (size_t i = object->index + 1;
             i < tiku_access_count(object->window->tree); i++)
            if (tiku_access_node(object->window->tree, i)->parent ==
                    object->index &&
                which-- == 0)
                return ax_ref(object, (long)i);
    return ax_ref(object, -1);
}
GVariant *
ax_property(GDBusConnection *connection, const char *sender, const char *path,
            const char *interface, const char *property, GError **error,
            void *data)
{
    (void)connection;
    (void)sender;
    (void)path;
    (void)interface;
    (void)error;
    ax_object_t *object = data;
    const tiku_access_node_t *node = ax_node(object);
    if (!strcmp(property, "Name"))
        return g_variant_new_string(node ? node->name : object->native->name);
    if (!strcmp(property, "Description"))
        return g_variant_new_string(node ? node->description : "");
    if (!strcmp(property, "Parent")) {
        if (!node)
            return g_variant_new("(so)", "", AX_NULL);
        if (node->parent == TIKU_ACCESS_NO_PARENT)
            return ax_ref(&object->native->root, 0);
        return ax_ref(object, node->parent);
    }
    if (!strcmp(property, "ChildCount"))
        return g_variant_new_int32(children(object));
    if (!strcmp(property, "Locale") || !strcmp(property, "ImageLocale"))
        return g_variant_new_string("en");
    if (!strcmp(property, "ToolkitName"))
        return g_variant_new_string("Tiku");
    if (!strcmp(property, "Version"))
        return g_variant_new_string("0.06");
    if (!strcmp(property, "version"))
        return g_variant_new_uint32(1);
    if (!strcmp(property, "Id"))
        return g_variant_new_int32(object->native->id);
    if (!strcmp(property, "AccessibleId"))
        return g_variant_new_string(object->path);
    if (!strcmp(property, "HelpText"))
        return g_variant_new_string("");
    if (!strcmp(property, "CharacterCount"))
        return g_variant_new_int32(
            node ? tiku_access_characters(node->text, strlen(node->text)) : 0);
    if (!strcmp(property, "CaretOffset"))
        return g_variant_new_int32(node ? node->caret : -1);
    if (!strcmp(property, "NActions"))
        return g_variant_new_int32(1);
    if (!strcmp(property, "NAnchors"))
        return g_variant_new_int16(1);
    if (!strcmp(property, "StartIndex") || !strcmp(property, "EndIndex"))
        return g_variant_new_int32(
            node ? ax_link_offset(object, !strcmp(property, "EndIndex")) : 0);
    if (node && !strcmp(property, "ColumnSpan"))
        return g_variant_new_int32(node->columns);
    if (node && !strcmp(property, "RowSpan"))
        return g_variant_new_int32(node->rows);
    if (node && !strcmp(property, "Position"))
        return g_variant_new("(ii)", node->row, node->column);
    if (node && !strcmp(property, "Table"))
        return ax_ref(object, node->parent);
    if (!strcmp(property, "NRows"))
        return g_variant_new_int32(node ? node->rows : 0);
    if (!strcmp(property, "NColumns"))
        return g_variant_new_int32(node ? node->columns : 0);
    if (!strcmp(property, "Caption") || !strcmp(property, "Summary"))
        return ax_ref(object, -1);
    if (!strcmp(property, "NSelectedRows") ||
        !strcmp(property, "NSelectedColumns"))
        return g_variant_new_int32(0);
    if (!strcmp(property, "ImageDescription"))
        return g_variant_new_string(node ? node->description : "");
    if (!strcmp(property, "PageCount"))
        return g_variant_new_int32(node ? node->rows : 0);
    if (!strcmp(property, "CurrentPageNumber"))
        return g_variant_new_int32(node ? node->row + 1 : 0);
    return NULL;
}
/** @brief Return the native accessible state bit array. */
static GVariant *
states(const ax_object_t *object)
{
    const tiku_access_node_t *node = ax_node(object);
    uint32_t bits = (1u << 8) | (1u << 24) | (1u << 11);
    if (!node || node->state & TIKU_ACCESS_VISIBLE) {
        bits |= 1u << 30;
        if (!node || tiku_access_showing(object->window->tree, object->index))
            bits |= 1u << 25;
    }
    if (node) {
        if (node->role == TIKU_ACCESS_WINDOW && object->window->focused)
            bits |= 1u << 1;
        if (node->state & TIKU_ACCESS_DISABLED)
            bits &= ~((1u << 8) | (1u << 24));
        if (node->state & TIKU_ACCESS_EDITABLE)
            bits |= 1u << 7;
        if (node->state & TIKU_ACCESS_SELECTED)
            bits |= (1u << 22) | (1u << 23);
        if (object->window->focused && node->state & TIKU_ACCESS_FOCUSED)
            bits |= 1u << 12;
        if (*node->text)
            bits |= 1u << 17;
    }
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("au"));
    g_variant_builder_add(&builder, "u", bits);
    g_variant_builder_add(&builder, "u", 0);
    return g_variant_builder_end(&builder);
}
/** @brief Component and image geometry methods from retained display bounds. */
static GVariant *
geometry(ax_object_t *object, const char *method, GVariant *parameters)
{
    const tiku_access_node_t *node = ax_node(object);
    tiku_rect_t bounds = node ? node->bounds : (tiku_rect_t){0};
    unsigned coordinates = 0;
    if (g_variant_n_children(parameters)) {
        GVariant *last = g_variant_get_child_value(
            parameters, g_variant_n_children(parameters) - 1);
        if (g_variant_is_of_type(last, G_VARIANT_TYPE_UINT32))
            coordinates = g_variant_get_uint32(last);
        g_variant_unref(last);
    }
    bounds = ax_bounds(object, bounds, coordinates);
    if (!strcmp(method, "GetExtents") || !strcmp(method, "GetImageExtents"))
        return g_variant_new("((iiii))", bounds.x, bounds.y, bounds.w,
                             bounds.h);
    if (!strcmp(method, "GetPosition") || !strcmp(method, "GetImagePosition"))
        return g_variant_new("(ii)", bounds.x, bounds.y);
    if (!strcmp(method, "GetSize") || !strcmp(method, "GetImageSize"))
        return g_variant_new("(ii)", bounds.w, bounds.h);
    if (!strcmp(method, "GrabFocus"))
        return g_variant_new("(b)",
                             ax_action(object, TIKU_ACCESS_FOCUS, 0, 0, NULL));
    if (!strcmp(method, "GetLayer"))
        return g_variant_new("(u)", 3);
    if (!strcmp(method, "GetMDIZOrder"))
        return g_variant_new("(n)", 0);
    if (!strcmp(method, "GetAlpha"))
        return g_variant_new("(d)", 1.0);
    if (!strcmp(method, "Contains") ||
        !strcmp(method, "GetAccessibleAtPoint")) {
        int x, y;
        unsigned type;
        g_variant_get(parameters, "(iiu)", &x, &y, &type);
        int inside = x >= bounds.x && y >= bounds.y &&
                     (int64_t)x < (int64_t)bounds.x + bounds.w &&
                     (int64_t)y < (int64_t)bounds.y + bounds.h;
        if (!strcmp(method, "Contains"))
            return g_variant_new("(b)", inside);
        long found = inside ? (long)object->index : -1;
        if (inside && object->window)
            for (size_t i = object->index + 1;
                 i < tiku_access_count(object->window->tree); i++) {
                const tiku_access_node_t *child =
                    tiku_access_node(object->window->tree, i);
                if (child->parent != object->index ||
                    !(child->state & TIKU_ACCESS_VISIBLE))
                    continue;
                tiku_rect_t box = ax_bounds(object, child->bounds, type);
                if (x >= box.x && y >= box.y &&
                    (int64_t)x < (int64_t)box.x + box.w &&
                    (int64_t)y < (int64_t)box.y + box.h)
                    found = (long)i;
            }
        return g_variant_new("(@(so))", ax_ref(object, found));
    }
    return g_variant_new("(b)", FALSE);
}
void
ax_method(GDBusConnection *connection, const char *sender, const char *path,
          const char *interface, const char *method, GVariant *parameters,
          GDBusMethodInvocation *invocation, void *data)
{
    (void)connection;
    (void)sender;
    (void)path;
    ax_object_t *object = data;
    GVariant *result = NULL;
    if (ax_text(object, interface, method, parameters, invocation) ||
        ax_table(object, interface, method, parameters, invocation))
        return;
    if (strstr(interface, "Component") || strstr(interface, "Image"))
        result = geometry(object, method, parameters);
    else if (!strcmp(method, "GetRole"))
        result = g_variant_new("(u)", role(object));
    else if (!strcmp(method, "GetRoleName") ||
             !strcmp(method, "GetLocalizedRoleName"))
        result = g_variant_new("(s)", role_name(object));
    else if (!strcmp(method, "GetState"))
        result = g_variant_new("(@au)", states(object));
    else if (!strcmp(method, "GetInterfaces"))
        result = g_variant_new("(@as)", ax_interfaces(object));
    else if (!strcmp(method, "GetAttributes")) {
        const tiku_access_node_t *node = ax_node(object);
        GVariantBuilder attributes;
        g_variant_builder_init(&attributes, G_VARIANT_TYPE("a{ss}"));
        if (node && node->role == TIKU_ACCESS_HEADING) {
            char level[16];
            snprintf(level, sizeof level, "%u", node->rows);
            g_variant_builder_add(&attributes, "{ss}", "level", level);
        }
        result = g_variant_new("(@a{ss})", g_variant_builder_end(&attributes));
    } else if (!strcmp(method, "GetLocale"))
        result = g_variant_new("(s)", "en");
    else if (!strcmp(method, "GetAttributeValue"))
        result = g_variant_new("(s)", "");
    else if (!strcmp(method, "GetApplication"))
        result = g_variant_new("(@(so))", ax_ref(&object->native->root, 0));
    else if (!strcmp(method, "GetChildAtIndex")) {
        int index;
        g_variant_get(parameters, "(i)", &index);
        result = g_variant_new("(@(so))", child(object, index));
    } else if (!strcmp(method, "GetChildren")) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("a(so)"));
        for (int i = 0; i < children(object); i++)
            g_variant_builder_add_value(&builder, child(object, i));
        result = g_variant_new("(@a(so))", g_variant_builder_end(&builder));
    } else if (!strcmp(method, "GetIndexInParent")) {
        const tiku_access_node_t *node = ax_node(object);
        int index = -1;
        if (node && node->parent != TIKU_ACCESS_NO_PARENT) {
            index = 0;
            for (size_t i = 0; i < object->index; i++)
                index += tiku_access_node(object->window->tree, i)->parent ==
                         node->parent;
        } else if (node) {
            index = 0;
            for (ax_window_t *w = object->native->windows; w < object->window;
                 w++)
                index += w->tree != NULL;
        }
        result = g_variant_new("(i)", index);
    } else if (!strcmp(method, "GetRelationSet")) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ua(so))"));
        result = g_variant_new("(@a(ua(so)))", g_variant_builder_end(&builder));
    } else if (!strcmp(method, "DoAction")) {
        int index;
        g_variant_get(parameters, "(i)", &index);
        const tiku_access_node_t *node = ax_node(object);
        result = g_variant_new(
            "(b)",
            index == 0 && ax_action(object,
                                    node && node->role == TIKU_ACCESS_CELL
                                        ? TIKU_ACCESS_FOCUS
                                        : TIKU_ACCESS_ACTIVATE,
                                    0, 0, NULL));
    } else if (!strcmp(method, "GetActions")) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("a(sss)"));
        g_variant_builder_add(&builder, "(sss)", "activate",
                              "Activate logical content", "");
        result = g_variant_new("(@a(sss))", g_variant_builder_end(&builder));
    } else if (strstr(interface, "Action"))
        result = g_variant_new(
            "(s)", !strcmp(method, "GetKeyBinding") ? "" : "activate");
    if (result)
        g_dbus_method_invocation_return_value(invocation, result);
    else
        g_dbus_method_invocation_return_dbus_error(
            invocation, "org.a11y.atspi.Error.NotSupported",
            "Method outside the declared profile");
}

GVariant *
ax_cache_item(const ax_object_t *object)
{
    const tiku_access_node_t *node = ax_node(object);
    int index = node ? object->sibling : -1;
    if (node && node->parent == TIKU_ACCESS_NO_PARENT) {
        index = 0;
        for (ax_window_t *window = object->native->windows;
             window < object->window; window++)
            index += window->tree != NULL;
    }
    GVariant *parent = !node ? ax_ref(object, -1)
                       : node->parent == TIKU_ACCESS_NO_PARENT
                           ? ax_ref(&object->native->root, 0)
                           : ax_ref(object, node->parent);
    return g_variant_new(
        "(@(so)@(so)@(so)ii@assus@au)", ax_ref(object, (long)object->index),
        ax_ref(&object->native->root, 0), parent, index, children(object),
        ax_interfaces(object), node ? node->name : object->native->name,
        role(object), node ? node->description : "", states(object));
}
int
ax_link_offset(const ax_object_t *object, int end)
{
    const tiku_access_node_t *node = ax_node(object);
    if (!node || node->parent == TIKU_ACCESS_NO_PARENT)
        return -1;
    const tiku_access_node_t *parent =
        tiku_access_node(object->window->tree, node->parent);
    size_t byte = end ? node->last_byte : node->first_byte;
    if (byte > strlen(parent->text))
        return -1;
    return tiku_access_characters(parent->text, byte);
}
