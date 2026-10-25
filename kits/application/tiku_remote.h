/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_remote.h - the window session between the desktop and an
 * application running as its own process.
 *
 * A window is a surface plus an event route, menus travel as the
 * tiku_menuset_t they are, and picks come back as commands.  The wire is
 * [u32 type][u32 payload length][payload], little-endian, over any stream
 * fd: a local socket, a serial line or a pty.  When both ends share a
 * host, a window's pixels may travel through POSIX shared memory.  A
 * version in HELLO guards the rest.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_REMOTE_H_
#define TIKU_REMOTE_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_access.h"
#include "tiku_dl.h"
#include "tiku_event.h"
#include "tiku_msg.h"
#include "tiku_window.h"

/** @brief The session version; a HELLO naming another ends the session. */
#define TIKU_REMOTE_VERSION   1
/** @brief Sessions the desktop holds at once. */
#define TIKU_REMOTE_SESSIONS  8
/** @brief Windows one out-of-process application may hold at once. */
#define TIKU_REMOTE_WINDOWS   8
/** @brief A window title's bytes on the wire, NUL included. */
#define TIKU_REMOTE_TITLE     64
/** @brief The largest window side, in pixels. */
#define TIKU_REMOTE_MAX_DIM   8192

/**
 * @brief Feature bits beyond version 1: a client claims its own in HELLO
 *        and the desktop answers with its own in WELCOME.
 *
 * SHARED_SURFACE: frames travel through shared memory, which needs both
 * ends on one machine. A client claims it on a Unix-domain socket;
 * network sockets and serial lines carry copied frames.
 */
#define TIKU_FEAT_SHARED_SURFACE 0x1u
/**
 * @brief Shared publications carry a nonzero u64 token after the buffer index.
 * The desktop copies pixels before echoing READY as RELEASE. A client keeps
 * that buffer unchanged until the matching token returns; busy buffers use
 * copied FRAME messages. Both peers must claim this bit for client sharing.
 */
#define TIKU_FEAT_FRAME_SNAPSHOT 0x10u
/** @brief PREEDIT events and atomic UTF-8 commits up to 65535 bytes. */
#define TIKU_FEAT_TEXT_V2 0x20u
/*
 * The end plays a stream of drawing commands (tiku_dl.h) in place of a
 * frame of pixels.  The desktop claims it in WELCOME, and a client sends
 * lists only to a desktop that claimed it.
 */
#define TIKU_FEAT_COMMAND_STREAM 0x2u
/*
 * The end plays the stream's picture commands, small bitmaps with their
 * own masks.  A list holding pictures goes as pixels to an end that has
 * not claimed this.
 */
#define TIKU_FEAT_PICTURE_STREAM 0x8u
/*
 * The end plays the stream's icon commands: art defined once per list as
 * HVIF bytes, placed by reference and rasterised at this end.  A list
 * holding icons goes as pixels to an end that has not claimed this.
 */
#define TIKU_FEAT_ICON_STREAM    0x4u
/** @brief Shared transfer buffers; publication reserves one until RELEASE. */
#define TIKU_REMOTE_BUFFERS      2
/** @brief A shared surface's name on the wire, NUL included. */
#define TIKU_REMOTE_SHM_NAME     64

/**
 * @brief Messages a session may have said that the shell has not yet
 *        taken.
 *
 * The shell takes them every loop.  Past this the newest is dropped and
 * counted (tiku_remote_lost()).
 */
#define TIKU_REMOTE_SAID         8

/* Message types, client to desktop... */
#define TIKU_RMSG_HELLO   1u   /* u32 version, name[32], [u32 features] */
#define TIKU_RMSG_OPEN    2u   /* u32 id, i32 w, i32 h, title[64]   */
#define TIKU_RMSG_FRAME   3u   /* u32 id, i32 w, i32 h, u32 px[w*h] */
#define TIKU_RMSG_MENUS   4u   /* u32 id, tiku_menuset_t       */
#define TIKU_RMSG_CLOSE   5u   /* u32 id; none: the client leaves   */
#define TIKU_RMSG_SURFACE 6u   /* u32 id, i32 w, i32 h, name[64]    */
#define TIKU_RMSG_READY   7u   /* u32 id, u32 buffer, [u64 token]   */
#define TIKU_RMSG_SAY     8u   /* a flattened tiku_msg_t       */
#define TIKU_RMSG_DRAW    9u   /* u32 id, i32 w, i32 h, a tiku_dl_t */
#define TIKU_RMSG_RAISE   10u  /* u32 id: bring that window forward  */
#define TIKU_RMSG_SIZEABLE 11u /* u32 id: the person may resize it   */
#define TIKU_RMSG_CONFIGURE 26u /* id,w,h,resize,zoom,title[64] */
#define TIKU_RMSG_DESCRIBE 27u /* id, UTF-8 semantic text for pixel frames */
#define TIKU_RMSG_PLACE   12u  /* u32 id, i32 role: what kind it is  */
/*
 * Menus as text: `menu File`, `item <command> <shortcut|-> <label>`,
 * `sep`, one per line, expanded into a menu set by the desktop.
 */
#define TIKU_RMSG_MENUS_TEXT 13u /* u32 id, then the lines           */
/* ...and desktop to client. */
#define TIKU_RMSG_EVENT   16u  /* u32 id, tiku_event_t         */
#define TIKU_RMSG_PICK    17u  /* u32 id, i32 command               */
#define TIKU_RMSG_TELL    19u  /* a flattened tiku_msg_t       */
/*
 * The desktop's answer to a HELLO: its version and the features it plays.
 * A client that has had no WELCOME assumes nothing beyond version 1, and
 * sends pixels.  A reader steps over a type it does not know by its
 * length.
 */
#define TIKU_RMSG_WELCOME 20u  /* u32 version, u32 features         */
/* Shell and interpreter text to and from a board over a link
 * (tiku_channel.h).  A board's shell answers whoever spoke last, so a
 * desk on a link and a person on the cable each see their own commands
 * and replies. */
#define TIKU_RMSG_SHELL_IN  21u  /* text for the board's shell            */
#define TIKU_RMSG_SHELL_OUT 22u  /* what its shell wrote back             */
#define TIKU_RMSG_BASIC_IN  23u  /* text for the board's interpreter      */
#define TIKU_RMSG_BASIC_OUT 24u  /* what a program on it printed          */
#define TIKU_RMSG_CLOSED  18u  /* u32 id: the desktop closed it     */
#define TIKU_RMSG_SHUTDOWN 25u /* the desktop is ending the session */
#define TIKU_RMSG_RELEASE 28u  /* u32 id, u32 buffer, u64 token     */

/** @brief Where the desktop listens, under this user's HOME.
 *  @return 0, or -1 when HOME is unset or the path does not fit. */
int tiku_remote_path(char *out, size_t max);

/* THE DESKTOP'S SIDE                                                        */

/**
 * @brief One window of a session, named by the id the application gave
 *        it; a session holds up to TIKU_REMOTE_WINDOWS.
 */
typedef struct tiku_remote_window {
    uint64_t generation; /**< connection-local window incarnation */
    struct tiku_remote_session *session; /* the session it is in      */
    int                  wants_front;   /* a RAISE the shell owes it   */
    int                  wants_place;   /* a PLACE role owed, 0 none   */
    /* A window the shell has still to close.  A CLOSE frees the row at
     * once and an OPEN may reuse it, so the window waits here for the
     * shell. */
    struct tiku_window *reap;
    int                  wants_size;    /* a SIZEABLE the shell owes  */
    int                  configured;    /* pending app window properties */
    tiku_access_tree_t *access;         /**< retained semantic publication */
    void *access_context; /**< host-owned native dispatch context */
    int access_fresh;     /**< native endpoint needs refresh */
    int access_x, access_y, access_focus; /**< last published placement */
    int64_t link_gesture; /**< one-use host monotonic gesture time */
    int                  focused;       /* last keyboard-focus notice sent */
    int                  config_w, config_h, resizable, zoomable;
    uint32_t             win_id;        /* what the application calls it */
    struct tiku_window *window;    /* not owned                       */
    uint32_t            *frame;         /* latest pixels, w*h              */
    int                  fw, fh;
    tiku_menuset_t  menus;
    int                  has_menus;
    int                  menus_fresh;   /* set when MENUS arrives          */
    int                  opened;        /* OPEN seen, window wanted        */
    int                  open_w, open_h;
    char                 title[TIKU_REMOTE_TITLE];
    char                 description[4096]; /* accessibility for pixel frames */
    /* Incoming shared pixels are copied into frame on READY. */
    uint32_t            *shared;        /* mapped, or NULL            */
    size_t               shared_bytes;
    int                  shown;         /* last READY buffer index    */
    /*
     * Nonzero for a FRAME or DRAW publication. READY clears it and frees
     * the retained display list. Each path stores its pixels in frame.
     */
    int                  list_fresh;
    /* The list itself, kept from the last DRAW: what the window is made
     * of, from which its content can be described in words. */
    struct tiku_dl      *list;
    /* The size the shared mapping was made at.  fw/fh follow the newest
     * arrival, so the mapping's stride is kept here. */
    int                  sw, sh;
} tiku_remote_window_t;

/** @brief One connected application: its windows, what it said, and its
 *         partly read and unsent bytes. */
typedef struct tiku_remote_session {
    int                  fd;
    uint64_t             generation; /* never reused for another connection */
    uint64_t next_window_generation; /**< monotonic OPEN incarnation source */
    int                  used;
    char                 name[32];
    struct tiku_remote_listener *owner;
    /* The windows this application holds.  A row carries a menu set and
     * a 4 KB description by value, about 14 KB. */
    tiku_remote_window_t win[TIKU_REMOTE_WINDOWS];
    unsigned             features;      /* what the peer said it can  */
    char                 help_page[128]; /* the page it named, or empty */
    /* Partial-read state: a message, its header included, may arrive in
     * pieces. */
    unsigned char        hbuf[8];
    size_t               hgot;
    unsigned char       *buf;
    size_t               got, want;
    uint32_t             cur_type;
    /* What this session has said and nobody has taken yet. */
    tiku_msg_t     *said[TIKU_REMOTE_SAID];
    int                  said_head, said_count, said_lost;
    /* Desktop-to-application output, bounded and written without
     * blocking, so a stopped application does not stop the desktop's
     * loop or its Force Quit. */
    unsigned char       *out;
    size_t               out_size, out_sent;
    /* The one large message waiting in it (a clipboard): its bytes not
     * yet written, and how far past the unsent start it begins.  The
     * bound on the rest is kept apart, so input keeps flowing behind it. */
    size_t               out_large, large_at;
} tiku_remote_session_t;

/** @brief The desktop's listening socket and its sessions. */
typedef struct tiku_remote_listener {
    uint64_t                   next_generation;
    /* Shared surfaces mapped and not yet given back.  The count lives
     * here, so a mapping a session leaked still counts after the
     * session's fields are cleared. */
    int                        mapped;
    int                        fd;      /* listening socket, or -1        */
    char                       path[108];
    tiku_remote_session_t session[TIKU_REMOTE_SESSIONS];
} tiku_remote_listener_t;

/** @brief Listen on the desk socket.  @return 0, or -1. */
int tiku_remote_listen(tiku_remote_listener_t *listener);

/** @brief Send SHUTDOWN to every session, free them, and close and remove
 *         the listening socket. */
void tiku_remote_shutdown(tiku_remote_listener_t *listener);

/** @brief Adopt an fd (a serial link, a pty) as a session directly.
 *  @return 0, or -1 when every session is in use. */
int tiku_remote_adopt(tiku_remote_listener_t *listener, int fd);

/**
 * @brief Accept and read everything that is ready, without blocking.
 *
 * @return nonzero when a repaint is owed: a session died, a message
 *         changed a window, or a window waits on the shell to be made,
 *         raised, closed, made sizeable or given new menus.
 */
int tiku_remote_poll(tiku_remote_listener_t *listener);

/**
 * @brief Wait up to @p timeout_us for the listening socket or any session
 *        to have something.
 *
 * For a caller between turns that is waiting on a child to start: the
 * wait gives the child the processor and returns as soon as it connects
 * or writes.  Nothing is read; tiku_remote_poll() takes what arrived.
 *
 * @return nonzero when something is ready, 0 when the time passed.
 */
int tiku_remote_wait(tiku_remote_listener_t *listener, int timeout_us);

/**
 * @brief How many sessions are connected right now.
 *
 * A child that has started but is not counted here has not connected.
 */
int tiku_remote_sessions(const tiku_remote_listener_t *listener);

/** @brief Send an input event to the session owning @p window. */
void tiku_remote_event(tiku_remote_listener_t *listener,
                            struct tiku_window *window,
                            const tiku_event_t *event);

/** @brief Queue a Cmd-W close request without issuing an input-gesture grant.
 *  @return 1 when queued, 0 when the owner is disconnected or its queue refuses.
 *  @note The application retains its close decision and dirty-document prompts. */
int tiku_remote_request_close(tiku_remote_listener_t *, struct tiku_window *);
/** @brief Queue Cmd-Q through the current app owner without an input-gesture grant.
 *  @return 1 when queued; the application retains its quit decision. */
int tiku_remote_request_quit(tiku_remote_listener_t *, struct tiku_window *);


/** @brief Send a menu pick to the session owning @p window. */
void tiku_remote_pick(tiku_remote_listener_t *listener,
                           struct tiku_window *window, int command);

/** @brief Tell the session its window was closed, and forget it. */
void tiku_remote_window_closed(tiku_remote_listener_t *listener,
                                    struct tiku_window *window);

/** @brief Shared surfaces this listener still holds. */
int tiku_remote_mapped(const tiku_remote_listener_t *listener);

/** @brief The window @p w belongs to, or NULL. */
tiku_remote_window_t *tiku_remote_window_of(
    tiku_remote_listener_t *listener, const struct tiku_window *w);

/**
 * @brief The pixels a session's window should show, or NULL.
 *
 * The desktop owns this snapshot. Its pixels and dimensions stay fixed
 * until a later publication or window/session closure is processed.
 */
const uint32_t *tiku_remote_pixels(
    const tiku_remote_window_t *win);

/*
 * SAY and TELL carry a flattened tiku_msg_t: named, typed fields, which a
 * reader that does not know one steps over.  A message's "what" names
 * it.
 *
 * Getting a file picked:
 *
 * ASK    what = TIKU_MSG_PICK    int32 "window", int32 "mode",
 *                                string "start", string "name"
 * ANSWER what = TIKU_MSG_PICKED  int32 "window", string "path"
 */
#define TIKU_MSG_PICK   0x7069636bu   /* 'pick' */
#define TIKU_MSG_PICKED 0x70696b64u   /* 'pikd' */
/* The desk's clipboard, both ways: an application says it with "text" to
 * put text on the desk, and the shell tells it to every session whenever
 * the clipboard changes, so clip_text() answers from the session's copy
 * and never waits on the wire. */
#define TIKU_MSG_CLIP   0x636c6970u   /* 'clip' */
/* Ask the desktop to open a namespace path with its normal file handler. */
#define TIKU_MSG_OPENPATH 0x6f706174u /* 'opat', string "path" */
/* A screenshot, asked of the desktop by an application that cannot see
 * the desk. ASK int32 "what" (TIKU_GRAB_*), int64 "id" for a window,
 * and int64 "request" echoed in every capture success/error response.
 * ANSWER int32 "list" and a string "title" and int64 "id" a window; or
 * string "path", int32 "w" and "h", int64 "size"; or string "error". */
#define TIKU_MSG_GRAB    0x67726162u  /* 'grab' */
#define TIKU_MSG_GRABBED 0x67726264u  /* 'grbd' */
enum { TIKU_GRAB_LIST = 0, TIKU_GRAB_DESK = 1, TIKU_GRAB_WINDOW = 2 };
/* An application's own icon beside the Deskbar's clock.  ASK string "id"
 * (its name for the item, at most 15
 * bytes), string "icon" (a name from the desk's icon set), string "text"
 * (a few characters after it) and "tip"; no icon and no text takes it
 * away.  A click on it comes back as TIKU_MSG_TRAYHIT with its "id". */
#define TIKU_MSG_TRAY    0x74726179u  /* 'tray' */
#define TIKU_MSG_TRAYHIT 0x74726869u  /* 'trhi' */
/* The pointer shape a window's content asks for, as the window's own
 * hover finds it.  ASK int32 "window" and int32 "shape" (TIKU_CURSOR_*);
 * the desktop shows it while the pointer is over that content and keeps
 * it until asked again or the window goes. */
#define TIKU_MSG_TEXT_CARET                                                    \
    0x696d6563u                       /**< focused native input caret, imec    \
                                       */
/** Explicit native URI dispatch: window, target, request; reply accepted. */
#define TIKU_MSG_ACCESS_TREE 0x61787472u
#define TIKU_MSG_ACCESS_ACTION 0x61786163u
#define TIKU_MSG_LINK_OPEN 0x6c6f706eu
#define TIKU_MSG_LINK_OPENED 0x6c6f7064u
#define TIKU_MSG_CURSOR  0x63757273u  /* 'curs' */
/* Local image dispatch; the reply acknowledges opening, not decoding. */
#define TIKU_MSG_IMAGEOPEN 0x696f706eu /* 'iopn', string "path" */
#define TIKU_MSG_IMAGEOPENED 0x696f7064u /* 'iopd', int32 "accepted" */
/* Open a host document by its local path (OPENPATH takes a path in the
 * desktop's namespace).  "accepted" acknowledges the dispatch, not that
 * the application parsed the document. */
#define TIKU_MSG_DOCUMENTOPEN 0x646f706eu /* 'dopn', string "path" */
#define TIKU_MSG_DOCUMENTOPENED 0x646f7064u /* 'dopd', int32 "accepted" */
/* Name the application's own page of the desktop's help folder.  Said
 * once, at start; it opens nothing, and the system menu's Help row opens
 * it while one of this session's windows is in front. */
#define TIKU_MSG_HELPPAGE 0x68656c70u /* 'help', string "page" */
#define TIKU_MSG_HELPOPEN 0x686f706eu /* 'hopn', string "page" */
#define TIKU_MSG_HELPHTML 0x6868746du /* 'hhtm', strings "title", "html" */

/* A program on a board the shell has mounted.  The shell owns the board
 * and the run: an application asks over SAY and hears over TELL.  The
 * board list is told whenever it changes, so the services that return
 * board data answer from a local copy.
 *
 * TELL  TIKU_MSG_BOARDS    string "names" (a "name<TAB>port<TAB>mcu"
 *                          row a line), string "current"
 * ASK   TIKU_MSG_BOARDRUN  string "board", string "program"
 * ASK   TIKU_MSG_BOARDIN   string "text"
 * ASK   TIKU_MSG_BOARDSTOP
 * ASK   TIKU_MSG_BOARDSAVE string "board", string "name", string "program"
 * ASK   TIKU_MSG_BOARDFREE string "board": let it go, port and all
 * ASK   TIKU_MSG_BOARDBACK string "board": take it back
 * ASK   TIKU_MSG_BOARDLOOK               look for boards on the ports
 * TELL  TIKU_MSG_BOARDOUT  int32 "what" (TIKU_APP_BOARD_*), string "text"
 */
#define TIKU_MSG_BOARDS    0x62726473u   /* 'brds' */
#define TIKU_MSG_BOARDRUN  0x6272756eu   /* 'brun' */
#define TIKU_MSG_BOARDIN   0x6269696eu   /* 'biin' */
#define TIKU_MSG_BOARDSTOP 0x62737470u   /* 'bstp' */
#define TIKU_MSG_BOARDOUT  0x626f7574u   /* 'bout' */
#define TIKU_MSG_BOARDSAVE 0x62736176u   /* 'bsav' */
#define TIKU_MSG_BOARDFREE 0x62667265u   /* 'bfre' */
#define TIKU_MSG_BOARDBACK 0x6262616bu   /* 'bbak' */
#define TIKU_MSG_BOARDLOOK 0x626c6f6bu   /* 'blok' */

/**
 * @brief Take the next message session @p index said, or NULL; taking it
 *        makes room for another.
 * @note The caller frees the message.
 */
tiku_msg_t *tiku_remote_said(tiku_remote_listener_t *listener,
                                       int index);

/** @brief How many messages session @p index said that did not fit, and
 *         were dropped. */
int tiku_remote_lost(const tiku_remote_listener_t *listener,
                          int index);

/** @brief Send @p m down to session @p index.  @return 1 when queued, 0
 *         when refused. */
int tiku_remote_tell(tiku_remote_listener_t *listener, int index,
                          const tiku_msg_t *m);

/** @brief The session owning @p window, or NULL. */
tiku_remote_session_t *tiku_remote_owner(
    tiku_remote_listener_t *listener, struct tiku_window *window);

/**
 * @brief Pin the process at the other end of @p session's socket, as
 *        tiku_sys_process_pin() does: a client-supplied pid is never
 *        trusted.
 *
 * Only a peer of this user other than this process qualifies; the handle
 * is close-on-exec.
 * @param pid  filled with the peer's process id when it is pinned.
 * @return the handle, or -1 for a peer that cannot be pinned.
 * @note The caller closes the handle.
 */
int tiku_remote_process_handle(const tiku_remote_session_t *session, long *pid);
/** @brief Kill the process @p handle pinned; a pid reused since the pin
 *         is not touched.  @return 0, or -1. */
int tiku_remote_process_kill(int handle);

/* THE APPLICATION'S SIDE                                                    */

/** @brief An application's end of the session. */
typedef struct {
    int           fd;
    uint32_t      next_id;      /* the id the next OPEN takes */
    /* Shared transfer storage when both peers claim FRAME_SNAPSHOT. */
    uint32_t     *shared;
    size_t        shared_bytes;
    int           shared_w, shared_h;
    uint32_t      shared_id;    /* the window this mapping was sent to */
    uint32_t      shared_serial; /* unique names for queued replacements */
    int           next_buffer;
    uint64_t      frame_serial; /* publication tokens, not reused */
    uint64_t      busy[TIKU_REMOTE_BUFFERS]; /* outstanding token, or zero */
    char          shm_name[TIKU_REMOTE_SHM_NAME];
    unsigned      features;     /* what this end claimed in HELLO */
    /* The features the desktop claimed in WELCOME: 0 until it has, which
     * means assume nothing beyond version 1. */
    unsigned      peer_features;
    int           peer_version;
    /* Header accumulation: the line has no peek. */
    unsigned char hbuf[8];
    size_t        hgot;
    /* The message the last read brought up, until it is taken. */
    tiku_msg_t *heard;
} tiku_remote_client_t;

/**
 * @brief Connect to the desktop's socket and say HELLO as @p name.
 *
 * The connect is retried every 100 ms for up to @p wait_ms, so a client
 * started just before the desktop finds it.
 *
 * @return 0, or -1 when no desktop answered within @p wait_ms.
 */
int tiku_remote_connect(tiku_remote_client_t *client,
                             const char *name, int wait_ms);

/**
 * @brief The features the desktop claimed in WELCOME, or 0 before it has.
 * @note Check it before sending anything the desktop might not play; 0
 *       means no features.
 */
unsigned tiku_remote_peer_features(const tiku_remote_client_t *client);

/**
 * @brief Start the session over @p fd, already connected to a desktop
 *        (a socket, a serial line or a pty), saying HELLO as @p name.
 *
 * Shared surfaces are claimed only when @p fd is a Unix-domain socket.
 * @return 0, or -1 for a bad @p fd.
 */
int tiku_remote_connect_fd(tiku_remote_client_t *client,
                                const char *name, int fd);

/** @brief Send a CLOSE that names no window, which ends the session, and
 *         close the connection and any shared surface. */
void tiku_remote_disconnect(tiku_remote_client_t *client);

/** @brief Ask for a window.  @return its id, or 0. */
uint32_t tiku_remote_open(tiku_remote_client_t *client,
                               const char *title, int w, int h);

/**
 * @brief Send the whole surface as the window's next frame.
 * Shared transfer requires both peers' FRAME_SNAPSHOT support and a free
 * buffer. Other frames travel in copied messages; no input is read here.
 * @return 0, or -1.
 */
int tiku_remote_frame(tiku_remote_client_t *client, uint32_t id,
                           const uint32_t *px, int w, int h);

/**
 * @brief Send the display list @p dl as window @p id's next frame.
 *
 * The far end plays the list into a surface of @p w by @p h and shows
 * that, as it shows a frame of pixels.  A command is semantic: one of
 * them is a whole button.
 *
 * @return 1 when it went.
 */
int tiku_remote_draw(tiku_remote_client_t *client, uint32_t id,
                          const tiku_dl_t *dl, int w, int h);

/** @brief Give back one window; the session stays. */
void tiku_remote_close_window(tiku_remote_client_t *client, uint32_t id);

/** @brief Ask for one of this session's windows to come to the front. */
void tiku_remote_raise(tiku_remote_client_t *client, uint32_t id);

/** @brief Say what kind of window @p id is (a place role). */
void tiku_remote_place(tiku_remote_client_t *client, uint32_t id,
                            int role);

/** @brief Say the person may resize this window. */
void tiku_remote_sizeable(tiku_remote_client_t *client, uint32_t id);
/**
 * @brief Send window @p id's title, content size and size controls; a
 *        zero @p w and @p h keep its size.
 * @return 1 when sent, 0 for bad arguments or a closed connection.
 */
int tiku_remote_configure(tiku_remote_client_t *client, uint32_t id,
    const char *title, int w, int h, int resize, int zoom);
/** @brief Send a text description of window @p id's content, up to 4095
 *         bytes, for readers of a pixel frame; NULL sends an empty one. */
void tiku_remote_describe(tiku_remote_client_t *client, uint32_t id,
                          const char *text);

/** @brief Publish window @p id's menu set.  @return 0, or -1. */
int tiku_remote_menus(tiku_remote_client_t *client, uint32_t id,
                           const tiku_menuset_t *menus);

/**
 * @brief Read menus written as text into @p set: `menu <title>` opens a
 *        menu, `item <command> <shortcut|-> <label words>` adds an item,
 *        `sep` a separator.  @return how many menus, 0 for none.
 */
int tiku_remote_menus_from_text(const char *text, tiku_menuset_t *set);

/**
 * @brief Read one message if one is ready.
 *
 * @return the message type, 0 when nothing is ready, or -1 when the
 *         desktop went away.
 */
int tiku_remote_read(tiku_remote_client_t *client, uint32_t *id,
                          tiku_event_t *event, int *command);

/** @brief Say @p m to the desktop.  @return 1 when it went. */
int tiku_remote_say(tiku_remote_client_t *client,
                         const tiku_msg_t *m);

/**
 * @brief Take the message the last read brought up, or NULL.
 *
 * tiku_remote_read() returns TIKU_RMSG_TELL when it brought one up.
 * @note The caller frees the message.
 */
tiku_msg_t *tiku_remote_heard(tiku_remote_client_t *client);

/** @brief Monotonic microseconds used for host gesture expiry. */
int64_t tiku_remote_gesture_clock(void);
/** @brief Record a one-use gesture for a focused live window. */
void tiku_remote_gesture(tiku_remote_window_t *window, int64_t now);
/** @brief Validate window ownership, focus, URI and a gesture aged at most 2 s.
 * Consumes a valid gesture once and invokes the supplied dispatch callback. */
int tiku_remote_link(tiku_remote_session_t *session, const tiku_msg_t *message,
                     int64_t now, int (*dispatch)(void *, const char *),
                     void *ctx);
/** @brief Send to one current application session; zero if disconnected. */
int tiku_remote_session_tell(tiku_remote_session_t *session,
                             const tiku_msg_t *message);
#endif /* TIKU_REMOTE_H_ */
