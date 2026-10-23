/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_x11_clipboard.h - the X11 host's side of the CLIPBOARD selection,
 * used by tiku_x11.c alone.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_X11_CLIPBOARD_H_
#define TIKU_X11_CLIPBOARD_H_
#include "tiku_clipboard.h"
#include <X11/Xlib.h>
#include <stdint.h>

/** @brief One peer taking the clipboard text a piece at a time (ICCCM
 *         2.7.2, INCR). */
typedef struct {
    Window requestor;    /**< None when no transfer is running */
    Atom property, type; /**< where it reads each piece; as what */
    char *data;          /**< a private copy of what it takes */
    size_t len, sent;    /**< its bytes; sent so far */
    int64_t deadline;    /**< a peer silent past this is let go */
} tiku_x11_send_t;

/** @brief The clipboard's X state: staged text/HTML imports and owned offers.
 */
typedef struct {
    Display *display;
    /* The host window; the window an import reads into (None when no
     * import runs); the owner it reads from; the owner last read. */
    Window window, requestor, source, cached_owner;
    Atom clipboard, utf8, targets, text, timestamp, incr, property, clock;
    Atom html;                 /**< UTF-8 text/html target */
    Atom rtf, rtf_application; /**< RTF target aliases */
    Atom rich_target;          /**< preferred advertised HTML or RTF target */
    Atom requested, received;  /**< the target asked for, and the type */
    Time acquired;             /**< server time ownership was taken at */
    unsigned stamp;            /**< the local clipboard's stamp last seen */
    /* Fetching a timestamp to take ownership with; an import arriving in
     * INCR pieces; a finished import's TIKU_CLIPBOARD_* result, 0 for
     * none. */
    int publishing, incremental, result;
    int asked;                   /**< a paste waits on the import */
    int64_t deadline, refreshed; /**< import give-up time; last read time */
    int64_t expires;      /**< absolute import limit across targets/chunks */
    char *incoming;       /**< text being taken in, held at its size */
    size_t used, room;    /**< its bytes; what is allocated */
    char *outgoing;       /**< the text published; NULL when empty */
    size_t outlen;        /**< its bytes */
    char *plain;          /**< staged fallback while HTML is imported */
    size_t plainlen;      /**< staged UTF-8 bytes, excluding NUL */
    char *out_html;       /**< immutable HTML offered by this owner */
    char *out_rtf;        /**< immutable RTF offered by this owner */
    size_t out_rtf_len;   /**< bytes in the RTF representation */
    size_t out_html_len;  /**< bytes in the HTML representation */
    tiku_x11_send_t send; /**< the one transfer out in pieces */
} tiku_x11_clipboard_t;

/** @brief Set @p c up for window @p w on @p d, with nothing in flight. */
void tiku_x11_clipboard_init(tiku_x11_clipboard_t *c, Display *d, Window w);
/** @brief End any import and transfer, and free the published text. */
void tiku_x11_clipboard_close(tiku_x11_clipboard_t *c);
/**
 * @brief Publish a local clipboard change, and give up an import or a
 *        transfer out that passed its deadline.
 *
 * A change is copied and a server timestamp fetched, through a property
 * change on the window; ownership is taken when it arrives.
 */
void tiku_x11_clipboard_tick(tiku_x11_clipboard_t *c, int64_t now);
/**
 * @brief Negotiate text and preferred HTML/RTF, then atomically import their
 * representations.
 *
 * Nothing starts while this window owns the selection, while a timestamp
 * is fetched or an import runs, or, without @p force, when the same owner
 * was read in the last 250 ms.  With no owner the clipboard is emptied.
 * A forced request joins an existing import and requires its result.
 *
 * @return nonzero while an import is pending; the call does not wait.
 */
int tiku_x11_clipboard_request(tiku_x11_clipboard_t *c, int64_t now, int force);
/**
 * @brief Handle one clipboard protocol event.
 *
 * @return 1 when @p e was one; 0 for any other event, left alone.
 */
int tiku_x11_clipboard_event(tiku_x11_clipboard_t *c, XEvent *e, int64_t now);
/**
 * @brief XCheckIfEvent() predicate: true for the clipboard's selection
 *        events and the property events of its windows.
 */
Bool tiku_x11_clipboard_match(Display *d, XEvent *e, XPointer ctx);
#endif
