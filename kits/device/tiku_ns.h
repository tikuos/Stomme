/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ns.h - the device's namespace, mirrored host-side.
 *
 * Static structure comes from /sys/vfs/manifest, read a page of lines at a
 * time (path, kind, permissions, descriptor, capability, id); values come
 * from `cat`, on demand and when a `~` line rings a subscribed node.  This
 * is the model every window renders.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_NS_H_
#define TIKU_NS_H_

#include <stddef.h>
#include "tiku_session.h"

/** @brief A node's path, NUL included; a longer one is not mirrored. */
#define TIKU_NS_PATH_MAX  128
/** @brief The descriptor column, as wide as the kernel's own: a cut
 *         descriptor mis-renders a value without saying so. */
#define TIKU_NS_META_MAX   72
/** @brief A node's value as read, NUL included. */
#define TIKU_VAL_MAX   512

/** @brief Permission bits parsed from the manifest's "rw" column. */
#define TIKU_NS_P_READ   0x1u
#define TIKU_NS_P_WRITE  0x2u

/** @brief One namespace node as the manifest describes it. */
typedef struct {
    char     path[TIKU_NS_PATH_MAX];
    char     name[64];                    /* last component, for display   */
    char     meta[TIKU_NS_META_MAX];    /* descriptor: "vtype,unit,..."  */
    /* The capability a writer must hold: "-", "hw", "sys", "fs", "net",
     * or "cap" for a combination; "?" for a node found by `ls`.  Room for
     * a comma-separated list such as "hw,sys,fs,net". */
    char     cap[24];
    /* The node's identity as the device knows it.  Zero when the manifest
     * has no id column (before revision 3) or the node was found by `ls`:
     * a reader then falls back to the path, and cannot tell a rename
     * from a different node. */
    unsigned id;
    unsigned perm;                        /* TIKU_NS_P_*                 */
    int      is_dir;
    int      depth;                       /* '/' components, root = 0      */
    int      subscribed;
    char     value[TIKU_VAL_MAX];    /* last read; empty until read   */
    int      value_valid;
    unsigned generation;                  /* bumped on every value change  */
} tiku_node_t;

/** @brief The mirror of one device's namespace. */
typedef struct tiku_ns tiku_ns_t;

/** @brief Create a mirror over an established session (not owned). */
tiku_ns_t *tiku_ns_new(tiku_session_t *s);

/** @brief Free the mirror; the session stays open. */
void tiku_ns_free(tiku_ns_t *ns);

/**
 * @brief Fetch and parse the manifest, replacing any previous structure.
 *
 * @return Node count, or -1 if the device has no manifest node or did not
 *         answer.
 */
int tiku_ns_load(tiku_ns_t *ns);

/** @brief Node count in the mirror. */
int tiku_ns_count(const tiku_ns_t *ns);

/** @brief Node by index (0..count-1), or NULL. */
const tiku_node_t *tiku_ns_at(const tiku_ns_t *ns, int i);

/**
 * @brief The node carrying @p id, or NULL.
 *
 * Change records (tiku_ns_events()) name a node by its identity; this
 * finds the node it names.
 */
const tiku_node_t *tiku_ns_find_id(const tiku_ns_t *ns,
                                             unsigned id);

/** @brief Node by exact path, or NULL. */
const tiku_node_t *tiku_ns_find(const tiku_ns_t *ns,
                                          const char *path);

/**
 * @brief Immediate children of @p path, in manifest order.
 *
 * @param out  Receives child pointers.
 * @return Count written, capped at @p max.
 */
int tiku_ns_children(const tiku_ns_t *ns, const char *path,
                          const tiku_node_t **out, int max);

/**
 * @brief Runtime children of a dynamic directory (/data), via `ls`.
 *
 * The manifest lists such directories but not their contents.
 *
 * @return Count written, or -1 on a link error.
 */
int tiku_ns_ls(tiku_ns_t *ns, const char *path,
                    char out[][64], int max);

/**
 * @brief As tiku_ns_ls(), also reporting which entries are directories.
 *
 * @param is_dir  Optional parallel array, 1 per directory entry.
 */
int tiku_ns_ls_kinds(tiku_ns_t *ns, const char *path,
                          char out[][64], char *is_dir, int max);

/** @brief One change the device recorded. */
typedef struct {
    char     op[12];     /* changed / created / removed / moved            */
    unsigned id;         /* the node's identity, as the manifest reports   */
    unsigned seq;
} tiku_ns_change_t;

/**
 * @brief Drain the device's change records.
 *
 * Reading /sys/vfs/events drains the device's records, so one round trip
 * says whether anything changed, and which nodes.
 *
 * @param out      Destination (NULL to discard)
 * @param max      Capacity of @p out
 * @param dropped  Optional: records lost, to the device's full ring or to
 *                 a full @p out.  Nonzero means the drained set is
 *                 incomplete.
 * @return Records written, or -1 when the device has no such node or did
 *         not answer: it cannot say what changed.
 */
int tiku_ns_events(tiku_ns_t *ns, tiku_ns_change_t *out, int max,
                        unsigned *dropped);

/**
 * @brief Read a node's value into the mirror (`cat`).
 *
 * @return 0 on success, -1 if unreadable, longer than TIKU_VAL_MAX - 1
 *         bytes, or the link failed.
 */
int tiku_ns_read(tiku_ns_t *ns, const char *path);

/**
 * @brief @p value as one argument of a line the board's shell parses.
 *
 * The shell splits on spaces, and a token opening with a quote runs to the
 * matching quote with no escapes.  A value that is empty, holds a space or
 * opens with a quote is wrapped in a quote it does not contain.  @return 0,
 * or -1 if it holds a line break, needs wrapping and holds both quotes, or
 * overflows.
 */
int tiku_ns_quote(const char *value, char *out, size_t cap);

/**
 * @brief Write a value (`write`), then re-read it.
 *
 * @param err  Optional buffer for the device's refusal text.
 * @return 0 on success, -1 if refused (capability, range or absent node).
 */
int tiku_ns_write(tiku_ns_t *ns, const char *path, const char *value,
                       char *err, size_t errmax);

/**
 * @brief Subscribe to a node so writes elsewhere refresh it (`sub add`).
 *
 * Falls back to marking the node poll-only when the firmware lacks `sub`.
 * @return 0 when the device confirmed the subscription, 1 when polling
 *         instead, -1 on a link error.
 */
int tiku_ns_subscribe(tiku_ns_t *ns, const char *path);

/** @brief Drop a subscription (`sub del`).  0 on success. */
int tiku_ns_unsubscribe(tiku_ns_t *ns, const char *path);

/**
 * @brief Whether the manifest arrived cut short, by an older board's read
 *        buffer or by filling this side's.
 *
 * True means the mirror covers only the nodes that fit; tiku_ns_complete()
 * fills the rest from `ls`.
 */
int tiku_ns_truncated(const tiku_ns_t *ns);

/**
 * @brief Fill in the directories the manifest lists empty by walking
 *        them with `ls`: a dynamic directory (gui/, proc/, data/) has no
 *        static children to render, and what it holds is only found by
 *        asking.  Nodes found this way carry the metadata
 *        tiku_ns_complete() describes.
 *
 * @param max_depth  How far below each such directory to walk; 2 if 0.
 * @return Nodes added, or -1.
 */
int tiku_ns_complete_dynamic(tiku_ns_t *ns, int max_depth);

/**
 * @brief Ask the device what @p path holds now, adding what is new, to
 *        @p max_depth levels, 1 if 0.
 *
 * For a directory whose contents change after connect -- a program
 * saved into /data -- which tiku_ns_complete_dynamic() passes over once
 * the directory has any child at all.
 *
 * @return Nodes added, or -1 for a NULL @p ns or a relative @p path.
 */
int tiku_ns_complete_dir(tiku_ns_t *ns, const char *path, int max_depth);

/**
 * @brief Complete a truncated mirror by walking the tree with `ls`, to
 *        @p max_depth levels, 4 if 0.
 *
 * Nodes discovered this way carry no descriptor or capability metadata; a
 * file's read and write bits are those of its `ls` perm column, and a
 * directory's are read only.
 *
 * @return Nodes added, or -1 when @p ns is NULL.
 */
int tiku_ns_complete(tiku_ns_t *ns, int max_depth);

/** @brief Whether the device answered `sub` at all (push vs poll UI). */
int tiku_ns_has_push(const tiku_ns_t *ns);

/**
 * @brief Service notifications: re-read every node the device rang, or,
 *        on a device without `sub`, every subscribed node.
 *
 * A link that broke is reopened, and the manifest reloaded, on the next
 * pump.
 *
 * @return Nodes refreshed, or -1 on a link error.
 */
int tiku_ns_pump(tiku_ns_t *ns, int timeout_ms);

/**
 * @brief How long a write waits for the board's answer, in ms; 0 sets the
 *        20 s default, room for a file landing in NVM.
 */
void tiku_ns_set_write_timeout(tiku_ns_t *ns, int ms);

/** @brief Whether the link is down and awaiting a reopen on the pump. */
int tiku_ns_down(const tiku_ns_t *ns);

#endif /* TIKU_NS_H_ */
