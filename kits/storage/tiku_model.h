/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_model.h - the Model and the backend seam.
 *
 * The Model is the one place that knows what kind of thing a pose wraps.
 * A view asks it questions -- is it a container, is it writable, what kind
 * is it -- and browses local files and a device namespace with the same
 * code.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MODEL_H_
#define TIKU_MODEL_H_

#include <stddef.h>

#include "tiku_ident.h"
#include <stdint.h>

#define TIKU_NAME_MAX  128
#define TIKU_PATH_MAX  512
#define TIKU_TYPE_MAX   48
#define TIKU_META_MAX   72

/**
 * @brief What a Model is.
 *
 * Directories on a device are VIRTUAL_DIR: nothing can be created, renamed
 * or pasted in them, so /sys, /dev and /proc are read-only in the UI.  A
 * dynamic store such as /data is a DIRECTORY.
 */
typedef enum {
    TIKU_KIND_UNKNOWN = 0,
    TIKU_KIND_FILE,          /* a local file                            */
    TIKU_KIND_DIRECTORY,     /* a local dir, or a device dynamic store  */
    TIKU_KIND_SYMLINK,
    TIKU_KIND_VOLUME,        /* a mount root, or a device root          */
    TIKU_KIND_ROOT,          /* the synthetic Disks root                */
    TIKU_KIND_VIRTUAL_DIR,   /* a device namespace directory            */
    TIKU_KIND_DEVICE_NODE,   /* a device namespace leaf: a live value   */
    TIKU_KIND_QUERY,
    /* A print destination, which wears the printer icon. */
    TIKU_KIND_PRINTER,
    TIKU_KIND_TRASH,
    TIKU_KIND_DESKTOP,
} tiku_kind_t;

/** @brief Permission bits, from stat() or from the manifest's perm column. */
#define TIKU_P_READ   0x1u
#define TIKU_P_WRITE  0x2u
#define TIKU_P_EXEC   0x4u

/**
 * @brief What the backend said about a node: permission bits, the
 *        manifest's columns, sizes and times.
 *
 * Permission answers such as tiku_model_is_writable() are derived from
 * these facts each time they are asked.
 */
typedef struct {
    unsigned perm;                        /* TIKU_P_*                   */
    char     req_cap[24];                 /* "-", "hw", "sys", ...          */
    char     meta[TIKU_META_MAX];     /* typed descriptor, or "-"       */
    int      cap_known;                   /* 0 when the manifest was cut    */
    /* Set on a link whose target is gone; the link is still listed, and its
     * row says it is broken. */
    int      link_broken;
    /* The raw mode word, when the store has one (MA-031): the Permissions
     * column renders the classic ten-character string from it.  A store
     * with no mode bits leaves mode_known 0 and the column falls back to
     * the three access answers this user has. */
    unsigned mode;
    int      mode_known;
    /* Set on a Trash that holds something, when the entry is taken in;
     * the icon, the menu item's enablement and the delete question read it
     * from here. */
    int      trash_full;
    /* A volume's capacity and free space in bytes, read when the entry is
     * taken in; the space bar, the Kind column and the free-space question
     * read them from here.  Both zero on anything that is not a volume. */
    uint64_t total, avail;
    /* Which filesystem the entry is on (st_dev) and what that filesystem
     * allows (TIKU_VOL_*).  When a volume leaves, the windows showing it
     * are found by dev: their paths no longer stat. */
    uint64_t dev;
    unsigned vol_flags;
    int64_t  size;
    int64_t  mtime;
    /* When the entry was made, in seconds since the epoch, valid when
     * btime_known is set.  A store that does not report a birth time
     * leaves both zero. */
    int64_t  btime;
    int      btime_known;
    /* Whether the node carries a picture of its own (TIKU_ICONHINT_*),
     * asked once when the entry is taken in.  A zeroed model is UNASKED:
     * it has not been probed, and reading it as NONE hides the node's own
     * art. */
    int      icon_hint;
} tiku_facts_t;

/** @brief What facts.vol_flags says about the volume the entry is on. */
#define TIKU_VOL_READ_ONLY 0x1u
#define TIKU_VOL_BOOT      0x2u
#define TIKU_VOL_REMOVABLE 0x4u
#define TIKU_VOL_SHARED    0x8u   /* a network share, not a disk      */

/** @brief The answers facts.icon_hint can hold. */
#define TIKU_ICONHINT_UNASKED 0
#define TIKU_ICONHINT_NONE    1
#define TIKU_ICONHINT_ATTR    2  /* an icon attribute of its own       */
#define TIKU_ICONHINT_THUMB   3  /* its own picture is its icon        */

typedef struct tiku_backend tiku_backend_t;

/**
 * @brief One change the backend reports: what happened, and to which node.
 *
 * @note The caller acts on op: a removal takes the row out, a value change
 *       refreshes it.  A caller that reads only the id leaves deleted rows
 *       on screen.
 */
typedef struct {
    uint64_t id;
    int      op;      /* TIKU_CH_*                                         */
} tiku_model_change_t;

/** @brief What a change record says happened. */
#define TIKU_CH_CHANGED 0
#define TIKU_CH_CREATED 1
#define TIKU_CH_REMOVED 2
#define TIKU_CH_MOVED   3

/** @brief One entry, whatever backend it came from. */
typedef struct {
    char                 name[TIKU_NAME_MAX];
    char                 path[TIKU_PATH_MAX];
    char                 type[TIKU_TYPE_MAX];   /* MIME or tiku-* type  */
    tiku_kind_t      kind;
    tiku_facts_t     facts;
    tiku_backend_t  *backend;                   /* not owned            */
    uint64_t             node_id;                   /* inode, or a hash     */
    unsigned             generation;                /* device value changes */
} tiku_model_t;

/*---------------------------------------------------------------------------*/
/* Questions the view asks                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Whether it holds other entries: any kind of directory or volume,
 *         a query, the Trash, the Desktop, or a link to a folder. */
int tiku_model_is_container(const tiku_model_t *m);

/** @brief Whether entries can be created, renamed or pasted inside it. */
int tiku_model_is_mutable_container(const tiku_model_t *m);

/** @brief Whether its facts carry the write bit. */
int tiku_model_is_writable(const tiku_model_t *m);

/**
 * @brief The name shown to people: the string table's name for the Disks
 *        root, the Trash or the Desktop, and the entry's own name otherwise.
 *
 * Model.name stays the real entry name, for file operations.
 */
const char *tiku_model_display_name(const tiku_model_t *m);

/**
 * @brief Walk the type table (Q-015).
 *
 * The compiled-in table is every type the system knows, so a menu built
 * from it offers only types something recognises.
 *
 * @param i     Row index, from zero.
 * @param label Receives the human name ("Text file"), or NULL.
 * @return the type string, or NULL past the end.
 */
const char *tiku_model_type_at(int i, const char **label);

/**
 * @brief Whether @p type is the type @p want asks for (Q-004).
 *
 * A supertype admits its members: "text/" admits "text/x-source-code".
 * The match is case-folded, and an empty @p want admits every type.
 */
int tiku_model_type_is(const char *type, const char *want);

/** @brief Human "Kind" column text. */
const char *tiku_model_kind_string(const tiku_model_t *m);

/**
 * @brief Which icon of the set represents it.
 *
 * The icon comes from the entry's kind and type; a node's own picture
 * (facts.icon_hint) is not consulted here.
 *
 * @return A name for tiku_icons_bitmap(); never NULL.
 */
const char *tiku_model_icon_name(const tiku_model_t *m);

/*---------------------------------------------------------------------------*/
/* The backend seam                                                          */
/*---------------------------------------------------------------------------*/

/** @brief Receives one entry during a listing; return non-zero to stop. */
typedef int (*tiku_entry_fn)(const tiku_model_t *m, void *ctx);

/**
 * @brief What every backend implements.
 *
 * A listing hands each entry to a callback as it is read.
 */
typedef struct {
    const char *name;

    /** @brief Fill @p out for one path.  0, or -1. */
    int (*stat)(tiku_backend_t *b, const char *path,
                tiku_model_t *out);

    /** @brief Enumerate the children of @p path.  Entries or -1. */
    int (*list)(tiku_backend_t *b, const char *path,
                tiku_entry_fn fn, void *ctx);

    /** @brief Read a node's value.  Bytes, or -1. */
    int (*read)(tiku_backend_t *b, const char *path, void *buf,
                size_t max);

    /** @brief Write a node's value.  0, or -1 with @p err filled. */
    int (*write)(tiku_backend_t *b, const char *path, const void *buf,
                 size_t len, char *err, size_t errmax);

    /** @brief Service the link and dispatch change notices.  Notices
     *         handled, or -1 on a link error. */
    int (*pump)(tiku_backend_t *b, int timeout_ms);

    /**
     * @brief Ask what changed since the last ask: one question for the
     *        whole backend.
     *
     * A backend with no change records leaves this NULL, and the view
     * polls by listing again.
     *
     * @param out      Receives the changes
     * @param max      Capacity of @p out
     * @param complete Set to 0 when the answer is known to be partial; the
     *                 caller then lists again
     * @return Changes written, or -1 when the backend cannot tell (an
     *         older device): the caller polls.
     */
    int (*changes)(tiku_backend_t *b, tiku_model_change_t *out, int max,
                   int *complete);

    /**
     * @brief The path of the node carrying @p id, or -1.
     *
     * A change record names an identity; only the backend can say what that
     * identity is currently called.
     */
    int (*path_of_id)(tiku_backend_t *b, uint64_t id, char *out,
                      size_t max);

    /**
     * @brief Change what an entry permits.  0, or -1 with @p err filled.
     *
     * NULL where the idea does not apply, as for a device node's capability
     * bits; the caller then shows the permissions grid read-only.
     */
    int (*setperm)(tiku_backend_t *b, const char *path, unsigned perm,
                   char *err, size_t errmax);

    /** @brief The state store for entries of this backend. */
    struct tiku_store *(*state_store)(tiku_backend_t *b);

    /** @brief Release the backend and everything it holds. */
    void (*close)(tiku_backend_t *b);

    /**
     * @brief Make the folder @p path.  0, or -1 with @p err filled.
     *
     * NULL where the backend cannot make folders; tiku_backend_mkdir() then
     * refuses.
     */
    int (*mkdir)(tiku_backend_t *b, const char *path, char *err,
                 size_t errmax);
} tiku_backend_ops_t;

/** @brief A backend instance; concrete ones extend this. */
struct tiku_backend {
    const tiku_backend_ops_t *ops;
    char                          devid[40];   /* empty for local files     */
    void                         *impl;
};

/** @brief Local files: permissions as this user has them, state in
 *         extended attributes. */
tiku_backend_t *tiku_backend_posix_open(void);

/**
 * @brief A TikuOS device, over the session and namespace client in
 *        kits/device.
 *
 * @param port  Serial device, or NULL to take the first port that looks
 *              like a board.
 * @param baud  Line rate.
 */
tiku_backend_t *tiku_backend_tiku_open(const char *port, int baud);

/**
 * @brief The same, with the wait and the recovery chosen.
 *
 * @p sync_ms is how long to wait for the shell's prompt after a newline.
 * With @p recover set, a port that does not answer is sent a break and BYE,
 * which bring a board back out of its interpreter, and asked again.  A
 * scan of every port passes a short @p sync_ms and @p recover 0, so a port
 * that is not a board is sent only the newline.
 */
tiku_backend_t *tiku_backend_tiku_open_within(const char *port, int baud,
                                              int sync_ms, int recover);

/**
 * @brief Ask the board on @p port who it is, without opening its
 *        namespace: the shell's prompt within @p sync_ms, then the four
 *        identity nodes, and the port is closed again.
 *
 * A port that does not answer is sent only the newline.
 * @return 0 with @p out filled when a shell answered, -1 otherwise.
 */
int tiku_backend_tiku_identify(const char *port, int baud, int sync_ms,
                               tiku_identity_t *out);
struct tiku_tx;
/**
 * @brief The same device over a transport already open, such as a board
 *        that reached the desk over a network link.  Takes @p tx: it is
 *        closed with the backend, or on failure.
 */
tiku_backend_t *tiku_backend_tiku_open_tx(struct tiku_tx *tx);
/** @brief A device backend's patience for one write, in ms. */
void tiku_backend_tiku_set_write_timeout(tiku_backend_t *b, int ms);
/** @brief Whether a device backend's link is down, awaiting a replug. */
int tiku_backend_tiku_down(const tiku_backend_t *b);
/** @brief The console session under a device backend, for a channel. */
struct tiku_session *tiku_backend_tiku_session(tiku_backend_t *b);

/**
 * @brief The identity the backend bound to, or NULL when @p b is not a
 *        device backend.
 *
 * Read once when the device was opened, so asking costs no round trip.
 * A shell names the board in its namespace from the name held here.
 *
 * @note The name is user-settable: two boards may answer to it, so
 *       nothing durable may be keyed on it.
 */
const tiku_identity_t *tiku_backend_tiku_identity(const tiku_backend_t *b);

/**
 * @brief The port a board is on, into @p out ("/dev/ttyACM0").
 * @return the bytes written, or 0 when it is not on a port at all.
 */
int tiku_backend_tiku_port(const tiku_backend_t *b, char *out, size_t max);

/**
 * @brief Open the board whose die says @p uid, whatever port it is on.
 *
 * Every serial port that looks like a board is opened in turn and asked
 * for its uid, and the others are closed again.  A reflashed board comes
 * back on whichever node the host gives it, and is found there.
 * @return the board, or NULL when none of them is that one.
 */
tiku_backend_t *tiku_backend_tiku_open_uid(const char *uid, int baud);

/**
 * @brief Take the board's shell for one client: its text comes to
 *        @p on_text as it arrives, and the namespace is closed until it
 *        is given back.
 *
 * While the shell is held, a stat, a list, a read or a write returns -1:
 * on a board running a program every byte sent is the program's input,
 * and its output has no prompt until it ends.  Pumping still works and is
 * what moves the text.
 *
 * @return 0, or -1 when this is not a board backend, @p on_text is NULL or
 *         somebody already holds the shell.
 */
int tiku_backend_tiku_lease(tiku_backend_t *b,
                            void (*on_text)(const unsigned char *bytes,
                                            size_t n, void *ctx),
                            void *ctx);

/** @brief Write bytes to the leased shell as they are: no newline, no wait. */
int tiku_backend_tiku_lease_send(tiku_backend_t *b, const void *bytes,
                                 size_t n);

/** @brief Give the shell back and find its prompt again. */
void tiku_backend_tiku_unlease(tiku_backend_t *b);

/** @brief Whether somebody is holding the shell. */
int tiku_backend_tiku_leased(const tiku_backend_t *b);

/** @brief Close @p b through its ops; NULL, or a backend with no close,
 *         is left alone. */
void tiku_backend_close(tiku_backend_t *b);

/**
 * @brief Make the folder @p path through @p b's mkdir operation.
 * @return 0, or -1 with @p err filled; a backend with no mkdir operation
 *         refuses.
 */
int tiku_backend_mkdir(tiku_backend_t *b, const char *path, char *err,
                       size_t errmax);

#endif /* TIKU_MODEL_H_ */
