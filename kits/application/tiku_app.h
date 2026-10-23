/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_app.h - an application's descriptor, the services a host gives it,
 * and the registry and loader that hold descriptors.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_APP_H_
#define TIKU_APP_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_event.h"
struct tiku_access_tree;

#ifndef TIKU_SDK_IDENTITY
#define TIKU_SDK_IDENTITY "unversioned"
#endif

#ifdef TIKU_APP_SO
/* The loader checks this record before opening the module. */
__attribute__((used)) static const char tiku_sdk_identity_record[] =
    "TIKU-SDK-IDENTITY:" TIKU_SDK_IDENTITY;
#endif

#define TIKU_APP_MAX 16

struct tiku_dl;
struct tiku_msg;

#include "tiku_window.h"

/**
 * @brief The services a host gives an application: windows, menus, the
 *        clipboard, boards and help, each called with @p ctx.
 *
 * Two hosts fill it: the out-of-process runner (tiku_client.h), which
 * carries each call over the window session, and the desktop, for the
 * descriptors it runs in its own process.  A service a host does not
 * offer is NULL.
 */
typedef struct tiku_app_services {
    void *ctx;                  /**< passed back to every service */
    /** @brief Ask for a window.  @return its id, or 0. */
    uint32_t (*open)(void *ctx, const char *title, int w, int h);
    /** @brief The window's next frame, whole. */
    int (*frame)(void *ctx, uint32_t id, const uint32_t *px, int w, int h);
    /**
     * @brief The window's next frame as what was drawn, when it can be.
     *
     * Hand over both the list recorded while painting and the pixels it
     * produced; which one travels depends on the link and on whether the
     * list described the whole window, and can differ per frame.
     *
     * @return 1 when the window was sent, as a list or as pixels.
     * @note May be NULL: the application then sends frame().
     */
    int (*present)(void *ctx, uint32_t id, const struct tiku_dl *dl,
                   const uint32_t *px, int w, int h);
    /** @brief Publish the window's menu set; the host draws the bar. */
    int (*menus)(void *ctx, uint32_t id, const tiku_menuset_t *set);
    /** @brief Give the window back. */
    void (*close)(void *ctx, uint32_t id);
    /**
     * @brief Say what kind of window @p id is (TIKU_APP_PLACE_*); the host
     *        decides where such a window goes.
     *
     * ANNOUNCE is centred in the upper part of the screen at a fixed size.
     * NOTE retains its frame and hides its tab while unfocused.
     * @note May be NULL: the host then places the window itself.
     */
    int (*place)(void *ctx, uint32_t id, int role);
    /**
     * @brief The window's content is now @p w by @p h.
     *
     * For a window whose size follows something that can change, as the
     * About box refits to another face.  The host keeps the window's
     * top-left corner where it is, moving it only to keep it on screen.
     * @note May be NULL: the window then keeps the size it opened at.
     */
    int (*resize)(void *ctx, uint32_t id, int w, int h);
    /**
     * @brief Ask the shell to let the user pick a file; the answer comes
     *        later, at picked().
     *
     * @p mode is a TIKU_APP_PICK_* question, @p start where the panel
     * opens and @p name, when saving, the name it offers.  The shell's
     * panel shows its own namespace, devices included; the LOCAL modes
     * take host files alone and answer with a local path.  A cancelled
     * panel never answers.
     *
     * @return nonzero when the question was put, or sent out of process.
     * @note May be NULL.
     */
    int (*pick)(void *ctx, uint32_t id, int mode, const char *start,
                const char *name);
    /**
     * @brief Bring window @p id to the front of the desktop and give it
     *        the focus.
     *
     * For when the person asks the application for a window it already
     * has open, by picking a file already open or a row of a Window menu.
     * @note May be NULL.
     */
    int (*raise_window)(void *ctx, uint32_t id);
    /**
     * @brief Let the person resize window @p id.
     *
     * Windows open at a fixed size.  A sizeable window has a corner the
     * person can drag; the application then hears TIKU_EVENT_RESIZE with
     * the content's new size and repaints at it.
     * @note May be NULL: the window then keeps its opening size.
     */
    int (*sizeable)(void *ctx, uint32_t id);
    /**
     * @brief Put @p text on the desk's clipboard.
     *
     * One clipboard for the desk: what is cut here can be pasted into
     * another application or the shell's rename and Save-panel fields,
     * and the other way round.  Text only; the file clipboard is separate.
     * @note May be NULL: the application then has no clipboard.
     */
    int (*clip_set)(void *ctx, const char *text);
    /**
     * @brief What the desk's clipboard holds: the host's own copy, never
     *        NULL, good until the application next returns to its loop.
     *
     * Answered from the host's copy, which follows the desk's clipboard,
     * without waiting on the wire.  @p stamp, when not NULL, gets a number
     * that changes whenever the text does.
     */
    const char *(*clip_text)(void *ctx, unsigned *stamp);

    /**
     * @brief The boards the shell has mounted, one name a line.
     *
     * Answered from a copy the shell keeps up to date, like the
     * clipboard, without waiting on the wire.
     * @return the bytes written, or -1 (none written) out of process
     *         until the shell first sends them, a turn after start().
     */
    int (*board_names)(void *ctx, char *out, size_t max);

    /**
     * @brief The current device the desktop is on, empty for this
     *        machine.  An application with no board of its own defaults
     *        its work to this; a change arrives as
     *        board_said(TIKU_APP_BOARD_CURRENT).
     * @return the bytes written.
     */
    int (*current_device)(void *ctx, char *out, size_t max);

    /**
     * @brief What @p board says about itself: one "label<TAB>value" a
     *        line -- its processor, its speed, what memory it has left.
     *
     * Answered from what the shell kept, without waiting on the wire; a
     * stale answer sets the shell asking again, and the new one arrives
     * on a later call.  Empty until the first answer.
     * @return the bytes written.
     * @note May be NULL.
     */
    int (*device_stats)(void *ctx, const char *board, char *out,
                        size_t max);

    /**
     * @brief Run @p program on the board called @p board.
     *
     * The shell hands it to that board's interpreter and tells the
     * application what it prints, as board_said(); nothing is returned
     * here but whether the asking was taken.
     * @return nonzero when the run was started.
     */
    int (*board_run)(void *ctx, const char *board, const char *program);

    /** @brief Answer the running program's INPUT with @p text. */
    int (*board_input)(void *ctx, const char *text);

    /** @brief Break the running program, as Ctrl-C at its console does. */
    int (*board_stop)(void *ctx);

    /**
     * @brief Keep @p program on the board called @p board, under @p name.
     *
     * The board's interpreter saves it in a slot of that name (at most
     * seven characters) and shows it as /data/<name>.bas.  Told as
     * board_said(TIKU_APP_BOARD_SAVED, name), or FAILED.
     * @return nonzero when the saving was started.
     */
        int (*board_save)(void *ctx, const char *board, const char *name,
                      const char *program);

    /**
     * @brief The port @p board is on, into @p out ("/dev/ttyACM0").
     *
     * Answered from the shell's copy, like board_names(), without
     * waiting on the wire.
     * @return the bytes written.
     */
        int (*board_port)(void *ctx, const char *board, char *out, size_t max);

    /**
     * @brief The processor @p board names itself as ("nRF54LM20B"), into
     *        @p out, from the shell's copy like the port.
     * @return the bytes written.
     */
    int (*board_mcu)(void *ctx, const char *board, char *out, size_t max);

    /**
     * @brief Open @p path as a double click on it would: a folder as a
     *        window, a document in its program.
     *
     * Paths are interpreted in the desktop's namespace, also for an
     * out-of-process application.
     * @return nonzero when opened, or the asynchronous request was sent.
     * @note May be NULL.
     */
    int (*open_path)(void *ctx, const char *path);

    /**
     * @brief Ask the shell to let @p board go, port and all.
     *
     * For a tool that needs the port to itself, such as one that
     * reflashes the board: two owners of one port corrupt each other's
     * traffic.  Told as board_said(TIKU_APP_BOARD_RELEASED).
     * @return nonzero when the asking was taken.
     * @note Ask for the port first: a released board has none.
     */
    int (*board_release)(void *ctx, const char *board);

    /**
     * @brief Ask for @p board back after board_release(), on whatever
     *        port it is on now.
     *
     * A reflashed board may come back on another node, so this goes by
     * name and the shell finds the port.  Told as
     * board_said(TIKU_APP_BOARD_RECLAIMED), or FAILED when it has not
     * come back yet; it may be asked again.
     */
        int (*board_reclaim)(void *ctx, const char *board);

    /**
     * @brief Ask the shell to look for boards on this machine's ports.
     *
     * The shell does not look unasked: opening a serial port writes to
     * whatever is on it.  What is found joins board_names(); told as
     * board_said(TIKU_APP_BOARD_LOOKED) with how many are mounted now.
     * @return nonzero when the asking was taken.
     */
    int (*board_find)(void *ctx);

    /**
     * @brief Name this application's own page of the desktop's help.
     *
     * Said once, at start; it opens nothing.  While one of this
     * application's windows is in front, the system menu has a Help row
     * that opens the page.  @p page is a name inside the help folder,
     * such as "apps/editor.html".
     * @return nonzero when there is a help folder to read the page from.
     * @note May be NULL.
     */
    int (*help_page)(void *ctx, const char *page);
    /** @brief Open a contextual help topic, optionally with a #section. */
    int (*help_open)(void *ctx, const char *topic);
    /**
     * @brief Show a generated report in the HTML reader.
     *
     * Opens no file, script or browser.  The host copies @p html, at most
     * 15000 bytes, before returning.
     * @note Call only in answer to a user action.
     */
    int (*help_html)(void *ctx, const char *title, const char *html);
    /**
     * @brief The local document named at launch, or NULL; never a
     *        remembered session path.
     * @note Valid during start() only: copy it to keep it.  Opening it
     *       must not execute its contents.
     */
    const char *startup_document;
    /**
     * @brief Update a window's title, requested content size and size
     *        controls; a zero width and height keep its current size.
     */
    int (*configure)(void *ctx, uint32_t id, const char *title,
                     int width, int height, int resizable, int zoomable);
    /** @brief Structured launch data, or NULL; valid during start(). */
    const struct tiku_msg *startup_message;
    /** @brief Send a structured request to a desktop service. */
    int (*send_message)(void *ctx, const struct tiku_msg *message);
} tiku_app_services_t;

/** @brief Which question pick() asks. */
#define TIKU_APP_PICK_OPEN 0         /**< a file in the shell's namespace */
#define TIKU_APP_PICK_SAVE 1         /**< a name to save under */
/** @brief A host file, answered as a host path, not a namespace path. */
#define TIKU_APP_PICK_LOCAL_OPEN 2
/** @brief A host folder: the one chosen, or the one the panel shows, as a
 *         host path.  A host that does not offer it never answers. */
#define TIKU_APP_PICK_LOCAL_FOLDER 3

/**
 * @brief Window roles for place().
 *
 * ANNOUNCE and PANEL sit centred across the screen, a third of the way
 * down, at a fixed size. An announcement has no tab; a panel has its tab
 * and close box. NOTE retains its position and content size; its tab
 * appears while focused and is hidden when focus leaves the window.
 */
enum {
    TIKU_APP_PLACE_ANNOUNCE = 1,  /**< a card: no tab, no close */
    TIKU_APP_PLACE_PANEL    = 2,  /**< managed: with its tab and close */
    TIKU_APP_PLACE_NOTE     = 3   /**< note: tab visible while focused */
};

/**
 * @brief An application: its identity and the calls a host makes on it.
 *
 * id and name are required, with start() and stop() or run()
 * (tiku_app_registry_add()); a host checks every other call for NULL.
 */
typedef struct {
    const char *id;                 /**< stable machine-readable identity */
    const char *name;               /**< label shown to the user          */
    /** @brief Make the application's state.  @return 0 on success. */
    int  (*start)(void **state, const tiku_app_services_t *services);
    /** @brief Free the state start() made. */
    void (*stop)(void *state);
    /** @brief An event.  @return nonzero when the application is done. */
    int  (*event)(void *state, const tiku_event_t *event);
    /** @brief Called regularly with the time, in microseconds. */
    void (*tick)(void *state, int64_t now_us);
    /** @brief A pick from the window's published menus, wherever the bar
     *         is.  @return nonzero when the application is done. */
    int  (*pick)(void *state, uint32_t window, int command);
    int  (*run)(int argc, char **argv); /**< a blocking command entry */
    /**
     * @brief The path the person chose, for a pick() window @p window
     *        asked.
     *
     * Called from the host's loop, at any time after the pick() that
     * asked, whatever the application has done since.  A cancelled panel
     * never calls it.
     * @note May be NULL for an application that never asks.
     */
    void (*picked)(void *state, uint32_t window, const char *path);
    /**
     * @brief An event, and the window of this application it landed in.
     *
     * A host calls this when it is set and event() when it is not
     * (tiku_app_deliver_event()), so an application with one window may
     * set event() alone.
     *
     * @return nonzero when the application is done, as event() does.
     */
    int  (*event_in)(void *state, uint32_t window,
                     const tiku_event_t *event);
    /**
     * @brief The host closed window @p window: by its close box, Cmd-W or
     *        the shell shutting down.
     *
     * An application holding several windows must hear this, or it paints
     * into a window that is gone.
     * @note May be NULL for an application with one window.
     */
    void (*closed)(void *state, uint32_t window);

    /**
     * @brief What the program running on a board printed, a line at a
     *        time, and then how it ended.
     *
     * @param what TIKU_APP_BOARD_*; @p text is the line for LINE, and
     *             the reason (or empty) for the others.
     */
    void (*board_said)(void *state, int what, const char *text);
    /** @brief A message the shell sent, lent for the call only. */
    void (*message)(void *state, const struct tiku_msg *message);
} tiku_app_descriptor_t;

/** @brief The application interface version; tiku_app_load() refuses an
 *         export built for another. */
#define TIKU_APP_ABI    5u

/** @brief What board_said() is carrying. */
#define TIKU_APP_BOARD_LINE     0   /**< one line the program printed   */
#define TIKU_APP_BOARD_ENDED    1   /**< it finished by itself          */
#define TIKU_APP_BOARD_STOPPED  2   /**< a person stopped it            */
#define TIKU_APP_BOARD_FAILED   3   /**< the board would not take it    */
#define TIKU_APP_BOARD_ASKS     4   /**< at an INPUT; text is its prompt */
#define TIKU_APP_BOARD_SAVED    5   /**< kept on the board; text = name */
#define TIKU_APP_BOARD_RELEASED 6   /**< let go; text = the board       */
#define TIKU_APP_BOARD_RECLAIMED 7  /**< taken back; text = the board   */
#define TIKU_APP_BOARD_LOOKED   8   /**< looked; text = how many there are */
#define TIKU_APP_BOARD_RUNNING  9   /**< sent, and now running          */
#define TIKU_APP_BOARD_CURRENT  10  /**< the current device changed;
                                     *   text = its name, empty for host */
/**
 * @brief The one symbol a loader looks for in a loadable application.
 *
 * The version is in the symbol's name, so an incompatible export is
 * refused by its absence; the export's abi and size catch a file built
 * against another toolkit.
 */
#define TIKU_APP_EXPORT "tiku_app_v1"

/** @brief What a loadable application exports as TIKU_APP_EXPORT. */
typedef struct {
    uint32_t                          abi;   /**< TIKU_APP_ABI      */
    uint32_t                          size;  /**< of the descriptor      */
    const tiku_app_descriptor_t *app;
} tiku_app_export_t;

/**
 * @brief Load the application in the shared object at @p path.
 *
 * @param err Why not, when the answer is NULL; never left unwritten.
 * @return the descriptor, owned by the loaded object, or NULL.
 */
const tiku_app_descriptor_t *tiku_app_load(const char *path,
                                                     char *err,
                                                     size_t max);

/** @brief Up to TIKU_APP_MAX descriptors, each id at most once. */
typedef struct {
    const tiku_app_descriptor_t *app[TIKU_APP_MAX];
    int                               count;
} tiku_app_registry_t;

/** @brief One started descriptor, its services and the state start()
 *         made. */
typedef struct {
    const tiku_app_descriptor_t *descriptor;
    const tiku_app_services_t   *services;
    void                             *state;
    int                               running;
} tiku_app_instance_t;

/** @brief Empty @p registry. */
void tiku_app_registry_init(tiku_app_registry_t *registry);

/**
 * @brief Add @p descriptor to @p registry.
 *
 * @return its index, or -1 for a full registry or an id already there,
 *         or a descriptor that lacks an id or a name, has only one of
 *         start() and stop(), or has neither start() nor run().
 */
int tiku_app_registry_add(tiku_app_registry_t *registry,
                               const tiku_app_descriptor_t *descriptor);

/** @brief The descriptor whose id is @p id, or NULL. */
const tiku_app_descriptor_t *tiku_app_registry_find(
    const tiku_app_registry_t *registry, const char *id);

/** @brief How many descriptors @p registry holds. */
int tiku_app_registry_count(const tiku_app_registry_t *registry);
/** @brief The descriptor at @p index, or NULL out of range. */
const tiku_app_descriptor_t *tiku_app_registry_at(
    const tiku_app_registry_t *registry, int index);

/**
 * @brief Start @p descriptor into @p instance with @p services.
 *
 * @return 0, or -1 when the descriptor lacks start() or stop(), the
 *         instance is already running, or start() failed.
 */
int tiku_app_instance_start(tiku_app_instance_t *instance,
                                 const tiku_app_descriptor_t *descriptor,
                                 const tiku_app_services_t *services);
/**
 * @brief Hand @p event to @p descriptor, saying which window it landed in.
 *
 * Calls event_in() when the descriptor has it, else event().  The
 * desktop and the out-of-process runner both deliver through here.
 *
 * @return nonzero when the application says it is done.
 */
int tiku_app_deliver_event(const tiku_app_descriptor_t *descriptor,
                                void *state, uint32_t window,
                                const tiku_event_t *event);

/** @brief Hand @p event to a running instance's event(), which names no
 *         window.  @return nonzero when the application is done. */
int tiku_app_instance_event(tiku_app_instance_t *instance,
                                 const tiku_event_t *event);
/** @brief Call a running instance's tick() with @p now_us. */
void tiku_app_instance_tick(tiku_app_instance_t *instance,
                                 int64_t now_us);
/** @brief Stop a running instance and clear it. */
void tiku_app_instance_stop(tiku_app_instance_t *instance);

/** @brief Run @p descriptor's blocking run() with @p argc and @p argv.
 *  @return its result, or -1 when it has no run() or the arguments are
 *          bad. */
int tiku_app_run(const tiku_app_descriptor_t *descriptor,
                      int argc, char **argv);

/**
 * @brief The request that sets tray item @p id beside the Deskbar's clock:
 *        @p icon from the desk's icon set, a few characters of @p text after
 *        it, and @p tip; no icon and no text take the item away.  A click
 *        on it comes back to the descriptor's message() as TIKU_MSG_TRAYHIT.
 * @return the message for the caller to send and free, or NULL.
 */
struct tiku_msg *tiku_app_tray_message(const char *id, const char *icon,
                                       const char *text, const char *tip);

/** @brief The same request, sent through @p services' send_message.
 *  @return 1 when it went; 0 from a host that takes no such requests. */
int tiku_app_tray_set(const tiku_app_services_t *services, const char *id,
                      const char *icon, const char *text, const char *tip);

/** @brief Send the focused text caret in application-content coordinates. */
int tiku_app_text_rect(const tiku_app_services_t *services, uint32_t window,
                       int x, int y, int width, int height);
/**
 * @brief The request that shows the pointer as @p shape (TIKU_CURSOR_*)
 *        over window @p window's content, until asked again.
 * @return the message for the caller to send and free, or NULL.
 */
struct tiku_msg *tiku_app_cursor_message(uint32_t window, int shape);

/** @brief The same request, sent through @p services' send_message.
 *  @return 1 when it went; 0 from a host that takes no such requests. */
int tiku_app_cursor_set(const tiku_app_services_t *services, uint32_t window,
                        int shape);

/** @brief Queue explicit external URI dispatch; request is echoed in the host
 * reply. A recent focused host gesture is required. Queueing is not acceptance.
 */
int tiku_app_open_link(const tiku_app_services_t *services, uint32_t window,
                       const char *target, int64_t request);
/** @brief Publish an owned logical tree through the host message service.
 * Zero queues a clear publication; returns one when queued, zero otherwise. */
int tiku_app_access(const tiku_app_services_t *services, uint32_t window,
                    const struct tiku_access_tree *tree);
/** @brief Request a sixty-second print grant from a recent focused user action.
 */
int tiku_app_print_prepare(const tiku_app_services_t *services, uint32_t window,
                           int64_t request, const char *title);
/** @brief Queue the captured final PDF after PRINT_READY; returns queue
 * acceptance. */
int tiku_app_print_pdf(const tiku_app_services_t *services, uint32_t window,
                       int64_t request, const void *pdf, size_t bytes);
/** @brief Cancel PDF preparation or an active native print request. */
void tiku_app_print_cancel(const tiku_app_services_t *services, uint32_t window,
                           int64_t request);
#endif /* TIKU_APP_H_ */
