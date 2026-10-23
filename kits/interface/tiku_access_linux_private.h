/*
 * KintsugiDesktop v0.01 Interface Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * tiku_access_linux_private.h - AT-SPI service and retained window
 * publications. SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ACCESS_LINUX_PRIVATE_H_
#define TIKU_ACCESS_LINUX_PRIVATE_H_
#include "tiku_access_native.h"
#include <gio/gio.h>
#define AX_WINDOWS 64
#define AX_ROOT "/org/a11y/atspi/accessible/root"
#define AX_NULL "/org/a11y/atspi/null"
typedef struct ax_object ax_object_t;
typedef struct {
    struct tiku_access_native *native; /**< containing endpoint */
    uint64_t epoch, content_epoch; /**< window and descendant path lifetimes */
    uint32_t id;                   /**< host-owned window key */
    int x, y, focused;             /**< content position and native focus */
    tiku_access_tree_t *tree;      /**< retained semantic snapshot */
    tiku_access_action_fn action;  /**< explicit user action dispatch */
    void *context;                 /**< borrowed dispatch context */
    ax_object_t *objects;          /**< registered per-node paths */
} ax_window_t;
struct ax_object {
    struct tiku_access_native *native; /**< containing endpoint */
    ax_window_t *window;               /**< NULL for the application root */
    uint64_t epoch;                    /**< captured exported object lifetime */
    int children, sibling;             /**< cached immediate hierarchy counts */
    size_t index;                      /**< current publication node index */
    char link_path[144];        /**< distinct native hyperlink proxy path */
    char path[128];             /**< native object path */
    unsigned registrations[12]; /**< interface registration handles */
    unsigned count;             /**< handles in use */
};
struct tiku_access_native {
    unsigned refs,
        closing;          /**< caller and registered callback lifetime holds */
    GDBusConnection *bus; /**< dedicated accessibility bus connection */
    GDBusNodeInfo *xml;   /**< owned implemented-interface schema */
    ax_object_t root;     /**< one application root */
    ax_window_t windows[AX_WINDOWS]; /**< bounded virtual window roots */
    char name[128];                  /**< application label */
    unsigned cache_registration;     /**< bulk query endpoint */
    uint64_t serial;                 /**< path lifetime token */
    int id;                          /**< registry-assigned application ID */
    int x, y, scale;                 /**< host canvas screen transform */
};
/** @brief Borrow current node data for an exported object. */
const tiku_access_node_t *ax_node(const ax_object_t *object);
/** @brief Native reference for a window node, or the null object. */
GVariant *ax_ref(const ax_object_t *object, long index);
/** @brief Distinct hyperlink reference, avoiding accessible proxy cache
 * aliases. */
GVariant *ax_hyperlink_ref(const ax_object_t *object, long index);
/** @brief Node bounds in the requested native coordinate space. */
tiku_rect_t ax_bounds(const ax_object_t *object, tiku_rect_t bounds,
                      unsigned coordinates);
/** @brief Dispatch one checked semantic action against the current snapshot. */
int ax_action(ax_object_t *object, unsigned action, int first, int last,
              const char *text);
/** @brief Read one native interface property. */
GVariant *ax_property(GDBusConnection *connection, const char *sender,
                      const char *path, const char *interface,
                      const char *property, GError **error, void *data);
/** @brief Handle base, component, action and semantic container methods. */
void ax_method(GDBusConnection *connection, const char *sender,
               const char *path, const char *interface, const char *method,
               GVariant *parameters, GDBusMethodInvocation *invocation,
               void *data);
/** @brief Handle scalar text, editing and hyperlink methods; zero if unrelated.
 */
int ax_text(ax_object_t *object, const char *interface, const char *method,
            GVariant *parameters, GDBusMethodInvocation *invocation);
/** @brief Handle logical table methods; zero if unrelated. */
int ax_table(ax_object_t *object, const char *interface, const char *method,
             GVariant *parameters, GDBusMethodInvocation *invocation);
/** @brief Interfaces implemented by a current node. */
GVariant *ax_interfaces(const ax_object_t *object);
/** @brief Return an immutable empty attribute dictionary. */
GVariant *ax_attributes(void);
/** @brief Cache tuple for one exported logical node. */
GVariant *ax_cache_item(const ax_object_t *object);
/** @brief Parent scalar range of a hyperlink child; -1 for invalid source. */
int ax_link_offset(const ax_object_t *object, int end);
#endif
