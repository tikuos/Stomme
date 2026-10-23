/*
 * KintsugiDesktop v0.01 Interface Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * tiku_access_linux.c - native AT-SPI endpoint and immutable publications.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_access_linux_private.h"
#include "tiku_access_linux_xml.inl"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/** @brief Accept the accessibility registry's application identifier. */
static gboolean
application_id(GDBusConnection *bus, const char *sender, const char *path,
               const char *interface, const char *property, GVariant *value,
               GError **error, void *data)
{
    (void)bus;
    (void)sender;
    (void)path;
    (void)error;
    ax_object_t *object = data;
    if (strcmp(interface, "org.a11y.atspi.Application") ||
        strcmp(property, "Id") ||
        !g_variant_is_of_type(value, G_VARIANT_TYPE_INT32))
        return FALSE;
    object->native->id = g_variant_get_int32(value);
    return TRUE;
}
/** @brief Release one registered callback's hold on endpoint storage. */
static void
native_release(void *data)
{
    tiku_access_native_t *native = data;
    if (!--native->refs)
        free(native);
}
/** @brief Release a callback's independent object copy after GIO retires it. */
static void
binding_release(void *data)
{
    ax_object_t *object = data;
    native_release(object->native);
    free(object);
}
/** @brief An exported callback may run only during its captured object
 * lifetime. */
static int
binding_live(ax_object_t *object)
{
    return !object->native->closing &&
           (!object->window ||
            object->epoch == (object->index ? object->window->content_epoch
                                            : object->window->epoch));
}
/** @brief Refuse queued method calls after replacement or endpoint closure. */
static void
binding_method(GDBusConnection *bus, const char *sender, const char *path,
               const char *interface, const char *method, GVariant *parameters,
               GDBusMethodInvocation *invocation, void *data)
{
    if (!binding_live(data)) {
        g_dbus_method_invocation_return_dbus_error(
            invocation, "org.a11y.atspi.Error.Defunct",
            "Element was replaced or closed");
        return;
    }
    ax_method(bus, sender, path, interface, method, parameters, invocation,
              data);
}
/** @brief Refuse queued property reads on retired elements. */
static GVariant *
binding_property(GDBusConnection *bus, const char *sender, const char *path,
                 const char *interface, const char *property, GError **error,
                 void *data)
{
    if (!binding_live(data)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_CLOSED,
                            "Accessibility element was replaced or closed");
        return NULL;
    }
    return ax_property(bus, sender, path, interface, property, error, data);
}
static const GDBusInterfaceVTable vtable = {.method_call = binding_method,
                                            .get_property = binding_property,
                                            .set_property = application_id};
/** @brief Give GIO an independent object copy and an explicit retirement
 * destructor. */
static unsigned
export_object(ax_object_t *object, const char *path, GDBusInterfaceInfo *info)
{
    ax_object_t *binding = malloc(sizeof *binding);
    if (!binding)
        return 0;
    *binding = *object;
    binding->native->refs++;
    unsigned id = g_dbus_connection_register_object(object->native->bus, path,
                                                    info, &vtable, binding,
                                                    binding_release, NULL);
    if (!id)
        binding_release(binding);
    return id;
}
const tiku_access_node_t *
ax_node(const ax_object_t *object)
{
    return object && object->window
               ? tiku_access_node(object->window->tree, object->index)
               : NULL;
}
GVariant *
ax_attributes(void)
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{ss}"));
    return g_variant_builder_end(&builder);
}
GVariant *
ax_ref(const ax_object_t *object, long index)
{
    const char *bus = g_dbus_connection_get_unique_name(object->native->bus);
    const char *path = AX_NULL;
    if (index < 0)
        bus = "";
    else if (!object->window)
        path = AX_ROOT;
    else if ((size_t)index < tiku_access_count(object->window->tree))
        path = object->window->objects[index].path;
    else
        bus = "";
    return g_variant_new("(so)", bus, path);
}
GVariant *
ax_hyperlink_ref(const ax_object_t *object, long index)
{
    if (index < 0 || (size_t)index >= tiku_access_count(object->window->tree))
        return ax_ref(object, -1);
    return g_variant_new("(so)",
                         g_dbus_connection_get_unique_name(object->native->bus),
                         object->window->objects[index].link_path);
}
/** @brief Clamp native coordinates before converting to the protocol integer.
 */
static int
coordinate(int64_t value)
{
    return value < INT_MIN ? INT_MIN : value > INT_MAX ? INT_MAX : (int)value;
}
tiku_rect_t
ax_bounds(const ax_object_t *object, tiku_rect_t bounds, unsigned coordinates)
{
    int64_t scale = object->native->scale, x = bounds.x, y = bounds.y;
    if (object->window) {
        x += object->window->x;
        y += object->window->y;
    }
    x *= scale;
    y *= scale;
    if (!coordinates) {
        x += object->native->x;
        y += object->native->y;
    } else if (coordinates == 2 && object->window) {
        const tiku_access_node_t *node = ax_node(object);
        if (node && node->parent != TIKU_ACCESS_NO_PARENT) {
            tiku_rect_t parent =
                tiku_access_node(object->window->tree, node->parent)->bounds;
            x -= ((int64_t)parent.x + object->window->x) * scale;
            y -= ((int64_t)parent.y + object->window->y) * scale;
        }
    }
    return (tiku_rect_t){coordinate(x), coordinate(y),
                         coordinate((int64_t)bounds.w * scale),
                         coordinate((int64_t)bounds.h * scale)};
}

int
ax_action(ax_object_t *object, unsigned action, int first, int last,
          const char *text)
{
    ax_window_t *window = object->window;
    const tiku_access_node_t *node = ax_node(object);
    if (!window || !node || !window->action ||
        (node->state & TIKU_ACCESS_DISABLED))
        return 0;
    if ((action == TIKU_ACCESS_REPLACE || action == TIKU_ACCESS_INSERT ||
         action == TIKU_ACCESS_DELETE) &&
        !(node->state & TIKU_ACCESS_EDITABLE))
        return 0;
    return window->action(window->context, (uint32_t)object->index,
                          tiku_access_document(window->tree),
                          tiku_access_generation(window->tree), action, first,
                          last, text);
}
GVariant *
ax_interfaces(const ax_object_t *object)
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
    g_variant_builder_add(&builder, "s", "org.a11y.atspi.Accessible");
    g_variant_builder_add(&builder, "s", "org.a11y.atspi.Component");
    const tiku_access_node_t *node = ax_node(object);
    if (!node)
        g_variant_builder_add(&builder, "s", "org.a11y.atspi.Application");
    else {
        if (node->role == TIKU_ACCESS_PARAGRAPH ||
            node->role == TIKU_ACCESS_HEADING || *node->text ||
            (node->role == TIKU_ACCESS_BODY && node->paragraph)) {
            g_variant_builder_add(&builder, "s", "org.a11y.atspi.Text");
            g_variant_builder_add(&builder, "s", "org.a11y.atspi.Hypertext");
        }
        if (node->state & TIKU_ACCESS_EDITABLE)
            g_variant_builder_add(&builder, "s", "org.a11y.atspi.EditableText");

        if (node->role == TIKU_ACCESS_BUTTON ||
            (node->state & TIKU_ACCESS_ACTIONABLE))
            g_variant_builder_add(&builder, "s", "org.a11y.atspi.Action");
        if (node->role == TIKU_ACCESS_IMAGE)
            g_variant_builder_add(&builder, "s", "org.a11y.atspi.Image");
        if (node->role == TIKU_ACCESS_CELL)
            g_variant_builder_add(&builder, "s", "org.a11y.atspi.TableCell");
        if (node->role == TIKU_ACCESS_TABLE)
            g_variant_builder_add(&builder, "s", "org.a11y.atspi.Table");
        if (node->role == TIKU_ACCESS_DOCUMENT ||
            node->role == TIKU_ACCESS_SLIDE)
            g_variant_builder_add(&builder, "s", "org.a11y.atspi.Document");
    }
    return g_variant_builder_end(&builder);
}
/** @brief Register every advertised native interface at an object path. */
static int
register_object(ax_object_t *object)
{
    GVariant *interfaces = g_variant_ref_sink(ax_interfaces(object));
    GVariantIter iter;
    const char *name;
    g_variant_iter_init(&iter, interfaces);
    while (g_variant_iter_next(&iter, "&s", &name)) {
        GDBusInterfaceInfo *info =
            g_dbus_node_info_lookup_interface(object->native->xml, name);
        unsigned id = export_object(object, object->path, info);
        if (!id) {
            g_variant_unref(interfaces);
            return 0;
        }
        object->registrations[object->count++] = id;
    }
    g_variant_unref(interfaces);
    const tiku_access_node_t *node = ax_node(object);
    if (node &&
        (node->role == TIKU_ACCESS_LINK || node->role == TIKU_ACCESS_IMAGE)) {
        snprintf(object->link_path, sizeof object->link_path, "%s_link",
                 object->path);
        unsigned id =
            export_object(object, object->link_path,
                          g_dbus_node_info_lookup_interface(
                              object->native->xml, "org.a11y.atspi.Hyperlink"));
        if (!id)
            return 0;
        object->registrations[object->count++] = id;
    }
    return 1;
}
/** @brief Remove exported paths before releasing their object storage. */
static void
unregister_object(ax_object_t *object)
{
    for (unsigned i = 0; i < object->count; i++)
        g_dbus_connection_unregister_object(object->native->bus,
                                            object->registrations[i]);
    object->count = 0;
}
/** @brief Release one native window publication and dispatch context. */
static void
window_clear(ax_window_t *window)
{
    if (!window->tree)
        return;
    for (size_t i = 0; i < tiku_access_count(window->tree); i++)
        unregister_object(window->objects + i);
    free(window->objects);
    tiku_access_release(window->tree);
    memset(window, 0, sizeof *window);
}
/** @brief Broadcast one AT-SPI object event using the documented signature. */
static void
event(ax_object_t *object, const char *kind, const char *detail, int first,
      int second, GVariant *value)
{
    GVariantBuilder properties;
    g_variant_builder_init(&properties, G_VARIANT_TYPE("a{sv}"));
    g_dbus_connection_emit_signal(
        object->native->bus, NULL, object->path, "org.a11y.atspi.Event.Object",
        kind,
        g_variant_new("(siiv@a{sv})", detail, first, second, value,
                      g_variant_builder_end(&properties)),
        NULL);
}
/** @brief Return the current native hierarchy in one bounded bulk reply. */
static void
cache_method(GDBusConnection *bus, const char *sender, const char *path,
             const char *interface, const char *method, GVariant *parameters,
             GDBusMethodInvocation *invocation, void *data)
{
    (void)bus;
    (void)sender;
    (void)path;
    (void)interface;
    (void)parameters;
    tiku_access_native_t *native = data;
    if (native->closing) {
        g_dbus_method_invocation_return_dbus_error(
            invocation, "org.a11y.atspi.Error.Defunct", "Endpoint closed");
        return;
    }
    if (strcmp(method, "GetItems")) {
        g_dbus_method_invocation_return_dbus_error(
            invocation, "org.a11y.atspi.Error.NotSupported",
            "Unknown cache query");
        return;
    }
    GVariantBuilder items;
    g_variant_builder_init(&items, G_VARIANT_TYPE("a((so)(so)(so)iiassusau)"));
    g_variant_builder_add_value(&items, ax_cache_item(&native->root));
    for (unsigned i = 0; i < AX_WINDOWS; i++)
        if (native->windows[i].tree) {
            ax_window_t *window = native->windows + i;
            for (size_t j = 0; j < tiku_access_count(window->tree); j++)
                g_variant_builder_add_value(&items,
                                            ax_cache_item(window->objects + j));
        }
    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(@a((so)(so)(so)iiassusau))",
                                  g_variant_builder_end(&items)));
}
/** @brief Cache protocol version one permits immediate bulk queries. */
static GVariant *
cache_property(GDBusConnection *bus, const char *sender, const char *path,
               const char *interface, const char *property, GError **error,
               void *data)
{
    (void)bus;
    (void)sender;
    (void)path;
    (void)interface;
    (void)error;
    (void)data;
    return !strcmp(property, "version") ? g_variant_new_uint32(1) : NULL;
}
static const GDBusInterfaceVTable cache_vtable = {
    .method_call = cache_method, .get_property = cache_property};
/** @brief Notify native caches when an object's lifetime ends. */
static void
cache_remove(ax_object_t *object)
{
    g_dbus_connection_emit_signal(
        object->native->bus, NULL, "/org/a11y/atspi/cache",
        "org.a11y.atspi.Cache", "RemoveAccessible",
        g_variant_new("(@(so))", ax_ref(object, (long)object->index)), NULL);
}
/** @brief Notify native caches of a complete current object. */
static void
cache_add(ax_object_t *object)
{
    g_dbus_connection_emit_signal(
        object->native->bus, NULL, "/org/a11y/atspi/cache",
        "org.a11y.atspi.Cache", "AddAccessible",
        g_variant_new("(@((so)(so)(so)iiassusau))", ax_cache_item(object)),
        NULL);
}
/** @brief Test whether object identities and native interfaces remain stable.
 */
static int
compatible(const tiku_access_tree_t *a, const tiku_access_tree_t *b)
{
    if (tiku_access_document(a) != tiku_access_document(b) ||
        tiku_access_count(a) != tiku_access_count(b))
        return 0;
    for (size_t i = 0; i < tiku_access_count(a); i++) {
        const tiku_access_node_t *x = tiku_access_node(a, i),
                                 *y = tiku_access_node(b, i);
        if (x->role != y->role || x->parent != y->parent ||
            x->body != y->body || x->owner != y->owner ||
            x->paragraph != y->paragraph ||
            ((x->state ^ y->state) & TIKU_ACCESS_EDITABLE) ||
            (!!*x->text != !!*y->text))
            return 0;
    }
    return 1;
}
/** @brief Emit text, caret, selection, bounds and focus changes on stable
 * paths. */
static void
update(ax_window_t *window, tiku_access_tree_t *tree, int x, int y, int focused,
       tiku_access_action_fn action, void *context)
{
    tiku_access_tree_t *old = window->tree;
    int old_focus = window->focused, moved = window->x != x || window->y != y;
    tiku_access_retain(tree);
    window->tree = tree;
    window->x = x;
    window->y = y;
    window->focused = focused;
    window->action = action;
    window->context = context;
    for (size_t i = 0; i < tiku_access_count(tree); i++) {
        ax_object_t *object = window->objects + i;
        const tiku_access_node_t *a = tiku_access_node(old, i),
                                 *b = tiku_access_node(tree, i);
        if ((b->role == TIKU_ACCESS_PARAGRAPH ||
             b->role == TIKU_ACCESS_HEADING) &&
            strcmp(a->text, b->text)) {
            event(object, "TextChanged", "delete", 0,
                  tiku_access_characters(a->text, strlen(a->text)),
                  g_variant_new_string(a->text));
            event(object, "TextChanged", "insert", 0,
                  tiku_access_characters(b->text, strlen(b->text)),
                  g_variant_new_string(b->text));
        }
        if (a->caret != b->caret)
            event(object, "TextCaretMoved", "", b->caret, 0,
                  g_variant_new_int32(0));
        if (a->selection_start != b->selection_start ||
            a->selection_end != b->selection_end)
            event(object, "TextSelectionChanged", "", 0, 0,
                  g_variant_new_int32(0));
        int was_showing = tiku_access_showing(old, i),
            now_showing = tiku_access_showing(tree, i);
        if ((was_showing || now_showing) &&
            (moved || memcmp(&a->bounds, &b->bounds, sizeof a->bounds))) {
            tiku_rect_t r = ax_bounds(object, b->bounds, 0);
            event(object, "BoundsChanged", "", 0, 0,
                  g_variant_new("(iiii)", r.x, r.y, r.w, r.h));
        }
        if ((old_focus && !!(a->state & TIKU_ACCESS_FOCUSED)) !=
            (focused && !!(b->state & TIKU_ACCESS_FOCUSED)))
            event(object, "StateChanged", "focused",
                  focused && !!(b->state & TIKU_ACCESS_FOCUSED), 0,
                  g_variant_new_int32(0));
        if (was_showing != now_showing)
            event(object, "StateChanged", "showing", now_showing, 0,
                  g_variant_new_int32(0));
        if (a->state != b->state || old_focus != focused ||
            strcmp(a->name, b->name) ||
            strcmp(a->description, b->description) ||
            was_showing != now_showing)
            cache_add(object);
    }
    tiku_access_release(old);
}
tiku_access_native_t *
tiku_access_native_open(const char *name, void *native_window)
{
    (void)native_window;
    GDBusConnection *session = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    if (!session)
        return NULL;
    GVariant *reply = g_dbus_connection_call_sync(
        session, "org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress",
        NULL, G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL);
    g_object_unref(session);
    if (!reply)
        return NULL;
    const char *address;
    g_variant_get(reply, "(&s)", &address);
    GDBusConnection *bus = g_dbus_connection_new_for_address_sync(
        address,
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
            G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
        NULL, NULL, NULL);
    g_variant_unref(reply);
    if (!bus)
        return NULL;
    tiku_access_native_t *native = calloc(1, sizeof *native);
    if (!native) {
        g_object_unref(bus);
        return NULL;
    }
    native->refs = 1;
    native->bus = bus;
    native->scale = 1;
    snprintf(native->name, sizeof native->name, "%s",
             name ? name : "KintsugiDesktop");
    native->xml = g_dbus_node_info_new_for_xml(ax_xml, NULL);
    native->root.native = native;
    snprintf(native->root.path, sizeof native->root.path, "%s", AX_ROOT);
    if (!native->xml || !register_object(&native->root)) {
        tiku_access_native_close(native);
        return NULL;
    }
    native->refs++;
    native->cache_registration = g_dbus_connection_register_object(
        bus, "/org/a11y/atspi/cache",
        g_dbus_node_info_lookup_interface(native->xml, "org.a11y.atspi.Cache"),
        &cache_vtable, native, native_release, NULL);
    if (!native->cache_registration) {
        native_release(native);
        tiku_access_native_close(native);
        return NULL;
    }
    /* Registry embedding is asynchronous because it may query the new root. */
    g_dbus_connection_call(
        bus, "org.a11y.atspi.Registry", AX_ROOT, "org.a11y.atspi.Socket",
        "Embed",
        g_variant_new("((so))", g_dbus_connection_get_unique_name(bus),
                      AX_ROOT),
        NULL, G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL, NULL);
    return native;
}
void
tiku_access_native_close(tiku_access_native_t *native)
{
    if (!native || native->closing)
        return;
    native->closing = 1;
    for (unsigned i = 0; i < AX_WINDOWS; i++)
        window_clear(native->windows + i);
    unregister_object(&native->root);
    if (native->xml)
        g_dbus_node_info_unref(native->xml);
    if (native->cache_registration)
        g_dbus_connection_unregister_object(native->bus,
                                            native->cache_registration);
    g_dbus_connection_close_sync(native->bus, NULL, NULL);
    g_object_unref(native->bus);
    native_release(native);
}
/** @brief Logical index of one live virtual window among native roots. */
static int
window_index(tiku_access_native_t *native, ax_window_t *window)
{
    int index = 0;
    for (ax_window_t *at = native->windows; at < window; at++)
        index += at->tree != NULL;
    return index;
}
/** @brief Replace descendants while retaining the live native window root. */
static int
replace_window(ax_window_t *window, tiku_access_tree_t *tree, int x, int y,
               int focused, tiku_access_action_fn action, void *context)
{
    tiku_access_native_t *native = window->native;
    size_t count = tiku_access_count(tree);
    ax_object_t *objects = calloc(count, sizeof *objects);
    if (!objects)
        return 0;
    ax_window_t old = *window;
    tiku_access_retain(tree);
    window->tree = tree;
    window->objects = objects;
    window->x = x;
    window->y = y;
    window->focused = focused;
    window->action = action;
    window->context = context;
    window->content_epoch = ++native->serial;
    objects[0] = old.objects[0];
    objects[0].children = 0;
    for (size_t i = 1; i < count; i++) {
        ax_object_t *object = objects + i;
        object->native = native;
        object->window = window;
        object->index = i;
        object->epoch = window->content_epoch;
        snprintf(object->path, sizeof object->path,
                 "/org/a11y/atspi/accessible/w%u_e%llu_n%zu", window->id,
                 (unsigned long long)window->content_epoch, i);
        const tiku_access_node_t *node = tiku_access_node(tree, i);
        object->sibling = objects[node->parent].children++;
        if (!register_object(object)) {
            for (size_t j = 1; j <= i; j++)
                unregister_object(objects + j);
            *window = old;
            free(objects);
            tiku_access_release(tree);
            return 0;
        }
    }
    ax_window_t replacement = *window;
    *window = old;
    for (size_t i = 1; i < tiku_access_count(old.tree); i++) {
        if (tiku_access_node(old.tree, i)->parent == 0)
            event(old.objects, "ChildrenChanged", "remove",
                  old.objects[i].sibling, 0, ax_ref(old.objects, (long)i));
        cache_remove(old.objects + i);
        unregister_object(old.objects + i);
    }
    *window = replacement;
    free(old.objects);
    tiku_access_release(old.tree);
    for (size_t i = 0; i < count; i++)
        cache_add(objects + i);
    for (size_t i = 1; i < count; i++)
        if (tiku_access_node(tree, i)->parent == 0)
            event(objects, "ChildrenChanged", "add", objects[i].sibling, 0,
                  ax_ref(objects, (long)i));
    if (focused)
        for (size_t i = 0; i < count; i++)
            if (tiku_access_node(tree, i)->state & TIKU_ACCESS_FOCUSED)
                event(objects + i, "StateChanged", "focused", 1, 0,
                      g_variant_new_int32(0));
    return 1;
}
int
tiku_access_native_set(tiku_access_native_t *native, uint32_t id,
                       tiku_access_tree_t *tree, int x, int y, int focused,
                       tiku_access_action_fn action, void *context)
{
    if (!native || native->closing || !id ||
        (tree && tiku_access_node(tree, 0)->role != TIKU_ACCESS_WINDOW))
        return 0;
    ax_window_t *window = NULL;
    for (unsigned i = 0; i < AX_WINDOWS; i++)
        if (native->windows[i].id == id) {
            window = native->windows + i;
            break;
        }
    if (window && window->tree && tree &&
        tiku_access_document(window->tree) == tiku_access_document(tree) &&
        tiku_access_generation(tree) < tiku_access_generation(window->tree))
        return 0;
    if (window && tree && compatible(window->tree, tree)) {
        update(window, tree, x, y, focused, action, context);
        return 1;
    }
    if (window && tree)
        return replace_window(window, tree, x, y, focused, action, context);
    if (window) {
        for (size_t j = 0; j < tiku_access_count(window->tree); j++)
            cache_remove(window->objects + j);
        event(&native->root, "ChildrenChanged", "remove",
              window_index(native, window), 0, ax_ref(window->objects, 0));
        window_clear(window);
    }
    if (!tree)
        return 1;
    if (!window)
        for (unsigned i = 0; i < AX_WINDOWS; i++)
            if (!native->windows[i].tree) {
                window = native->windows + i;
                break;
            }
    if (!window)
        return 0;
    window->native = native;
    window->id = id;
    window->x = x;
    window->y = y;
    window->focused = focused;
    window->action = action;
    window->context = context;
    tiku_access_retain(tree);
    window->tree = tree;
    window->objects = calloc(tiku_access_count(tree), sizeof *window->objects);
    if (!window->objects) {
        tiku_access_release(tree);
        memset(window, 0, sizeof *window);
        return 0;
    }
    native->serial++;
    window->epoch = window->content_epoch = native->serial;
    for (size_t i = 0; i < tiku_access_count(tree); i++) {
        ax_object_t *object = window->objects + i;
        object->native = native;
        object->epoch = window->epoch;
        object->window = window;
        object->index = i;
        snprintf(object->path, sizeof object->path,
                 "/org/a11y/atspi/accessible/w%u_e%llu_n%zu", id,
                 (unsigned long long)native->serial, i);
        const tiku_access_node_t *node = tiku_access_node(tree, i);
        if (node->parent != TIKU_ACCESS_NO_PARENT) {
            ax_object_t *parent = window->objects + node->parent;
            object->sibling = parent->children++;
        }
        if (!register_object(object)) {
            window_clear(window);
            return 0;
        }
    }
    for (size_t i = 0; i < tiku_access_count(tree); i++)
        cache_add(window->objects + i);
    event(&native->root, "ChildrenChanged", "add", window_index(native, window),
          0, ax_ref(window->objects, 0));
    if (focused)
        event(window->objects, "StateChanged", "active", 1, 0,
              g_variant_new_int32(0));
    if (focused)
        for (size_t i = 0; i < tiku_access_count(tree); i++)
            if (tiku_access_node(tree, i)->state & TIKU_ACCESS_FOCUSED)
                event(window->objects + i, "StateChanged", "focused", 1, 0,
                      g_variant_new_int32(0));
    return 1;
}
void
tiku_access_native_origin(tiku_access_native_t *native, int x, int y, int scale)
{
    if (native && scale >= 1 && scale <= 4) {
        native->x = x;
        native->y = y;
        native->scale = scale;
    }
}
void
tiku_access_native_poll(tiku_access_native_t *native)
{
    if (native)
        for (unsigned i = 0; i < 64 && g_main_context_pending(NULL); i++)
            g_main_context_iteration(NULL, FALSE);
}

void *
tiku_access_native_children(tiku_access_native_t *native)
{
    (void)native;
    return NULL;
}
