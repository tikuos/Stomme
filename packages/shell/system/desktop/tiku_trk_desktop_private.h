/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_private.h - what the desktop shell's files share.
 *
 * The shell is one composition split over the files beside this one:
 * the state they all hold a pointer to, the small accessors that read
 * it, and the functions one file defines for another.  Only the
 * desktop's own files and its tests include it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#ifndef TIKU_TRK_DESKTOP_PRIVATE_H_
#define TIKU_TRK_DESKTOP_PRIVATE_H_

#include "tiku_attr.h"

#if TIKU_SHELL_BOARD_SERVICES
#include "tiku_channel.h"

#endif
#include "tiku_clipboard.h"
#include "tiku_conduct.h"
#include "tiku_field.h"
#include "tiku_form.h"
#include "tiku_fs.h"
#include "tiku_host.h"
#include "tiku_iconpaint.h"
#include "tiku_icons.h"
#include "tiku_logo.h"
#include "tiku_model.h"
#include "tiku_motion.h"
#include "tiku_mount.h"
#include "tiku_msg.h"

#if TIKU_SHELL_REMOTE_SERVICES
#include "tiku_print_rpc.h"

#endif
#include "tiku_query.h"
#include "tiku_remote.h"
#include "tiku_sound.h"
#include "tiku_state.h"
#include "tiku_strings.h"
#include "tiku_text_input.h"
#include "tiku_thumb.h"
#include "tiku_trk_about_window.h"
#include "tiku_trk_addon.h"
#include "tiku_trk_alert.h"
#include "tiku_trk_application.h"
#include "tiku_trk_band_controller.h"
#include "tiku_trk_clip.h"
#include "tiku_trk_cwmenu.h"
#include "tiku_trk_deskbar.h"
#include "tiku_trk_deskbar_prefs_window.h"
#include "tiku_trk_deskbar_roster.h"
#include "tiku_trk_desktop_prefs_window.h"
#include "tiku_trk_desktop_view.h"
#include "tiku_trk_drag.h"
#include "tiku_trk_drag_target.h"
#include "tiku_trk_drop_controller.h"
#include "tiku_trk_find.h"
#include "tiku_trk_find_window.h"
#include "tiku_trk_folder_actions.h"
#include "tiku_trk_folder_edit.h"
#include "tiku_trk_folder_file_actions.h"
#include "tiku_trk_folder_lifecycle.h"
#include "tiku_trk_folder_menu.h"
#include "tiku_trk_folder_window.h"
#include "tiku_trk_fonts_window.h"
#include "tiku_trk_frame.h"
#include "tiku_trk_gfx_dep.h"
#include "tiku_trk_info.h"
#include "tiku_trk_info_window.h"
#include "tiku_trk_launch.h"
#include "tiku_trk_launch_controller.h"
#include "tiku_trk_navmenu.h"
#include "tiku_trk_nodeicon.h"
#include "tiku_trk_open_with_window.h"
#include "tiku_trk_panel.h"
#include "tiku_trk_panel_window.h"
#include "tiku_trk_poseview.h"
#include "tiku_trk_prefs.h"
#include "tiku_trk_prefs_window.h"
#include "tiku_trk_responder.h"
#include "tiku_trk_script.h"
#include "tiku_trk_script_adapter.h"
#include "tiku_trk_script_api.h"
#include "tiku_trk_select.h"
#include "tiku_trk_select_window.h"
#include "tiku_trk_session_controller.h"
#include "tiku_trk_sounds_window.h"
#include "tiku_trk_splash.h"
#include "tiku_trk_status_overlay.h"
#include "tiku_trk_switcher.h"
#include "tiku_trk_switcher_window.h"
#include "tiku_trk_task.h"
#include "tiku_trk_titleview.h"
#include "tiku_trk_types.h"
#include "tiku_tx.h"
#include "tiku_volume.h"
#include "tiku_window.h"
#include <dirent.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* How many applications this desktop hosts in its own process. */
#define TIKU_TRK_EMBED_MAX 8
/* The most windows one hosted application may hold at once; each is told
 * apart by the window id the services carry. */
#define TIKU_TRK_EMBED_WINDOWS 8

#define DESK_W 1000
#define DESK_H  680
#define COUNT_H  16

/* How many entries a turn asks whether they carry a picture of their own;
 * each ask opens the file. */
#define HINTS_PER_TURN 24

/** @brief How much of the count view's text a script can read. */
#define TIKU_TRK_COUNT_TEXT_MAX 64
/* The icon name of a menu row that shows the TikuOS mark; paint_menu_icon
 * draws the mark with tiku_logo_paint. */
#define MARK_ICON "tikuos"

typedef tiku_trk_folder_window_t folder_t;

/** @brief One asker of a look: a remote session, or a hosted application. */
typedef struct {
    int                   session;  /**< the remote session, or -1 */
    struct desktop_embed *slot;     /**< or the hosted application */
} desktop_asker_t;
/* How many distinct askers a look keeps: every remote session and every
 * hosted application. */
#define DESKTOP_ASKERS (TIKU_REMOTE_SESSIONS + TIKU_TRK_EMBED_MAX)

/** @brief How many described panels may be open at once. */
#define TIKU_TRK_PANELS_MAX 4

/**
 * @brief A panel a device described, open or free (window NULL).
 *
 * root is the board's mount prefix, which a bind that starts with /gui/ is
 * relative to (see bind_path()); "" for a .panel file, whose binds are
 * namespace paths.
 */
typedef struct {
    struct desktop  *app;                 /**< the desktop, for the
                                           *   window's callbacks */
    tiku_form_t      form;                /**< the rows, as the device
                                           *   described them */
    tiku_window_t   *window;              /**< the window showing it, or NULL */
    char             path[TIKU_PATH_MAX]; /**< the described node */
    char             root[TIKU_PATH_MAX]; /**< the prefix its /gui/ binds
                                           *   are relative to, or "" */
    char             status[96];          /**< the last refusal, "not
                                           *   answering", or "" */
    int              unknown;             /**< rows this build did not know */
    int              pending;             /**< the row waiting to be
                                           *   written, or -1 */
    int              tracking;            /**< a slider's knob is
                                           *   being dragged */
    int              quiet;               /**< no bound path answered */
} tiku_trk_described_t;

/**
 * @brief What the Deskbar responder keeps between events: its own
 *        double-click memory (DB-019), a Cmd press still ambiguous
 *        between a re-dock and a chord (DB-049, DB-021), a resize of
 *        the bar (DB-050), a primary press held for the long press, and
 *        a program's row waiting for its release.
 */
typedef struct {
    int64_t  click_at;                  /**< the last click, in microseconds */
    int      click_x, click_y;          /**< where the last click was */
    int      dock_armed;                /**< a Cmd press is down */
    int      dock_x, dock_y;            /**< where the Cmd press went down */
    unsigned dock_mods;                 /**< the Cmd press's modifiers */
    unsigned dock_button;               /**< the Cmd press's button */
    int      dock_drag;                 /**< the Cmd press moved: the bar is
                                         *   being re-docked */
    int      resize;                    /**< a resize drag is under way */
    int      resize_x;                  /**< the pointer's x where the
                                         *   resize began */
    int      resize_w;                  /**< the bar's width when the
                                         *   resize began */
    int      held;                      /**< the primary button is still down */
    int64_t  press_at;                  /**< when it went down, in
                                         *   microseconds */
    int      press_x, press_y;          /**< where it went down */
    /** The program row pressed, launched at the release unless it was dragged
     * away; "" for none. */
    char     launch[TIKU_PATH_MAX];
} desktop_bar_t;

typedef struct desktop {
    tiku_host_t    *host;               /**< the display; NULL when headless */
    tiku_surface_t *surf;               /**< the frame windows paint into */
    tiku_trk_splash_t     splash;       /**< the boot and closing screens */
    tiku_workspace_t     *desk;         /**< the windows and their stacking */
    tiku_trk_deskbar_t  *deskbar;       /**< the bar, drawn last */
    tiku_backend_t  *backend;           /**< the namespace: files and mounts */
    tiku_fs_t       *fs;                /**< file operations over it */
    folder_t             folders[TIKU_WINDOW_MAX]; /**< the folder windows */
    tiku_trk_services_t  services;      /**< what the views ask the desktop */
    tiku_trk_folder_render_t folder_render; /**< how folder windows are drawn */
    tiku_trk_folder_lifecycle_t folder_lifecycle; /**< opens and closes
                                                   *   folder windows */
    int                  folder_count;  /**< folders[] in use */
    int                  row_h;         /**< a list row's height in pixels: the
                                         *   plain font's height plus 5 */
    int                  team;          /**< the desktop's own Deskbar row */
    /** The applications with a Deskbar row of their own, by name: one per
     * application whose session holds a window, removed with the last of its
     * windows. */
    char            app_teams[TIKU_TRK_DB_TEAMS_MAX][TIKU_TRK_DB_LABEL_MAX];
    int                  napp_teams;    /**< app_teams[] in use */
    int                  running;       /**< the loop goes on while set */
    /** Being taken down: the loop runs on for the closing screen, and input is
     * read and dropped. */
    int                  closing;
    int                  dirty;         /**< 1 full repaint, 2
                                         *   bounded repaint */
    tiku_rect_t          damage;        /**< the bounded repaint's area */
    int                  frame_partial; /**< the last frame was bounded */
    tiku_rect_t          frame_damage;  /**< the last frame's clip, which
                                         *   blit() presents when the
                                         *   frame was bounded */
    int64_t              last_present;  /**< the last present's start, in
                                         *   monotonic microseconds */
    /** Frames drawn whole and bounded, and pixels drawn. */
    uint64_t             full_frames, partial_frames, frame_pixels;
    /** Microseconds spent drawing and presenting, and frames that took over
     * 16.7 ms to draw. */
    uint64_t             draw_us, present_us, slow_frames;
    /** The last 128 frames' draw times in microseconds, a ring. */
    int64_t              frame_samples[128];
    unsigned             frame_sample_count; /**< samples taken; the next goes
                                              *   at this modulo 128 */
    /** The folder windows' menus: one set, rebuilt from the focused folder
     * window each time a menu opens. */
    tiku_trk_cwmenu_t    menus;
    tiku_trk_launch_controller_t launcher; /**< opens files and applications */
    tiku_trk_drop_controller_t drop_controller; /**< what a drop does */
    tiku_trk_folder_file_actions_t file_actions; /**< delete, duplicate and the
                                                  *   other file commands */
    tiku_trk_folder_edit_t folder_edit; /**< the inline rename */
    tiku_trk_band_controller_t band_controller; /**< the rubber-band
                                                 *   selection */
    tiku_trk_drag_target_t drag_target; /**< where a drag can drop */
    tiku_trk_session_controller_t session; /**< the open windows, saved and
                                            *   restored across runs */
    tiku_trk_script_adapter_t script_adapter; /**< the -script driver */
    tiku_trk_folder_menu_t folder_menu; /**< a folder window's menus */
    tiku_menu_t     *popup;             /**< the menu showing, or NULL */
    tiku_rect_t     popup_frame;        /**< where it is drawn */
    tiku_menu_t     *popup_sub;         /**< its open submenu, or NULL */
    tiku_rect_t     popup_sub_frame;    /**< where that is drawn */
    folder_t            *bar_fl;        /**< the folder window whose menu bar
                                         *   opened the popup */
    int                  bar_open;      /**< the open menu title, or -1 */
    char                 status[200];   /**< the status line's text */
    uint32_t             backdrop_color; /**< the desktop's background
                                          *   colour, as RGB */
    int                  finding;       /**< the one-line Find entry is up */
    char                 find_buf[64];  /**< what has been typed into it */
    int                  find_len;      /**< find_buf[] in use */
    int                  find_serial;   /**< numbers the result windows */
    tiku_trk_find_window_t find_window; /**< the Find panel */
    /** The sessions of applications in processes of their own, and of board
     * channels. */
    tiku_remote_listener_t remote;
#if TIKU_SHELL_REMOTE_SERVICES

    tiku_print_rpc_t printing; /**< exact ownership of native print requests */
    uint64_t access_owners[TIKU_REMOTE_SESSIONS][TIKU_REMOTE_WINDOWS]
                          [2];
#endif

    tiku_conduct_t         conduct;     /**< the conductor channel: an outside
                                         *   driver's events and questions */
    int                         conduct_on; /**< the channel was asked
                                             *   for and opened */
    /** Injected events, a ring: taken in arrival order, before the
     * display's own events, and handled as those are. */
    tiku_event_t           cq[64];
    int                         cq_head, cq_tail; /**< the ring's read and
                                                   *   write positions */
    /* One application hosted in the desktop's own process, given services
     * backed by the workspace.  Installed applications are not hosted
     * here: the runner starts each in a process of its own. */
    struct desktop_embed {
        struct desktop      *app;       /**< the desktop, for the callbacks */
        const tiku_app_descriptor_t *descriptor; /**< the application's
                                                  *   descriptor */
        tiku_app_services_t services;   /**< the services the
                                         *   application is given */
        void                *state;     /**< the application's own state,
                                         *   from its start */
        /* One window the application holds.  Its workspace tag is this
         * entry, so tiku_workspace_find() by tag returns this window and
         * not another of the application's. */
        struct embed_win {
            struct desktop_embed *slot;     /**< the application it
                                             *   belongs to */
            uint32_t             id;        /**< the window id the
                                             *   application was given */
            tiku_window_t       *window;    /**< the workspace window */
            uint32_t            *frame;     /**< the application's pixels,
                                             *   RGB, fw by fh */
            int                  fw, fh;    /**< the frame's width and height */
        } win[TIKU_TRK_EMBED_WINDOWS]; /**< the application's windows */
        uint32_t             next_id;   /**< the next window id to hand out */
        int                  running;   /**< the slot holds an application */
        /** The help page the application named for itself, opened by the system
         * menu's Help row while one of its windows is in front; "" for
         * none. */
        char                 help_page[128];
    } embeds[TIKU_TRK_EMBED_MAX]; /**< the hosted applications */
    /** The page the system menu's Help row opens: the focused window's, from
     * desktop_help_page_of(); "" when the row is not shown. */
    char                     help_row[128];
#if TIKU_SHELL_BOARD_SERVICES

    /** The board the desktop is current on, by its mount name; "" for this
     * machine. Applications default to it, it is kept across runs, and it can
     * name a board that is not mounted. */
    char                     current_device[64];
    /** The current board's port, kept across runs and connected again at
     * startup. */
    char                     current_device_port[256];
    /* Ports not mounted that a scan found or a look's probe had an answer
     * on.  The Devices menu offers to connect each one a look's probe has
     * named. */
    struct {
        char port[256];         /**< the device node to open */
        char where[32];         /**< the port's short name, such as ttyACM3 */
        char name[64];          /**< the processor name a probe gave, or "" */
    }                        candidates[TIKU_MOUNT_MAX]; /**< ports on offer */
    int                      ncandidates; /**< candidates[] in use */
    /* What a connected board says about itself, for the About box: read
     * on a worker and kept, so drawing never waits on the wire. */
    struct {
        char    name[64];       /**< the board it describes */
        char    text[1024];     /**< its answer, a "label\tvalue" line each */
        int64_t at;             /**< when it last answered, in monotonic
                                 *   microseconds */
        int     asking;         /**< a read is out for it */
    }                        dev_stats[TIKU_MOUNT_MAX]; /**< one per board */
    /** The program run or save in progress on a board, one at a time, or NULL.
     * A run goes over the board's window channel when it has one, else through
     * the board's held shell. */
    struct tiku_board_basic *board_run;
    struct desktop_embed    *board_run_slot; /**< the hosted application that
                                              *   asked, or NULL */
    char                     board_run_name[64]; /**< the board a run is on, or
                                                  *   the name a save writes */
    char                     board_run_prefix[TIKU_PATH_MAX]; /**< the mount
                                                  *   prefix of the board the
                                                  *   run or save is on */
    int                      board_run_session; /**< the remote session that
                                                 *   asked, or -1 */
    int                      board_run_saving; /**< board_run is a save */
    /** The run's state as last written to the trail; -1 at the start. */
    int                      board_run_state;
    int                      board_run_asked; /**< the asker was told the
                                               *   program waits at an INPUT */
    /** The board names last told to the remote sessions. */
    char                     board_names_told[512];
    /** The current board told with them. */
    char                     board_current_told[64];
    /** How many sessions there were then; a new session is told again. */
    int                      board_names_sessions;
    /** The baud the boards were opened at, for later looks, connects and
     * reclaims. */
    int                      board_baud;
    /* A board's last answer to "what changed", fetched on a worker; one
     * asking in flight per mount slot. */
    struct desktop_wire {
        tiku_model_change_t ch[32];     /**< the change records it brought */
        int                 n;          /**< how many, or -1 when the
                                         *   board cannot tell */
        int                 complete;   /**< 0 when the board knows the
                                         *   answer is partial */
        int                 answered;   /**< an answer arrived since this
                                         *   pump's asking */
        int                 in_flight;  /**< an asking is out */
    }                        wire[TIKU_MOUNT_MAX]; /**< one per mount slot */
    int                      replicant_reads;
#endif
 /**< Deskbar replicant reads
                                               *   out on workers */
    int                      fs_job;    /**< the file engine is
                                         *   stepping on a worker */
    int                      fs_was_busy; /**< the file engine was busy at the
                                           *   last progress tick */
    int                      thumb_job; /**< a picture is decoding */
    int                      volumes_job; /**< a volume scan is out */
    int                      unmount_job;
#if TIKU_SHELL_BOARD_SERVICES
 /**< an unmount is out */
    /* A look or a reclaim in progress.  A look probes every port at once
     * on workers, records each board that answers as a candidate, lets the
     * port go, and tells the asker when the last port has answered.  A
     * reclaim probes every port for the released board with uid and mounts
     * it.  At startup, the connect of a port named with -p, and of the
     * current board's port, counts in look.pending.  Each look's askers are
     * told once, when it ends; one asking during a look is told when the
     * look after it ends. */
    struct desktop_ask {
        int   going;                    /**< an asking is under way */
        int   pending;                  /**< ports still to answer */
        int   count;                    /**< a look: ports asked */
        int   landed;                   /**< a look: ports answered so far */
        int   baud;                     /**< the baud the ports are opened at */
        int   session;                  /**< the remote session asking, or -1 */
        struct desktop_embed *slot;     /**< or the hosted application asking */
        int   found;                    /**< a reclaim: the board came back */
        unsigned gen;                   /**< a look: changed as each
                                         *   look's counting starts */
        int   again;                    /**< a look: asked again during it */
        int   nasked;                   /**< a look: askers in asked[] */
        int   nagain;                   /**< a look: askers in again_by[] */
        desktop_asker_t asked[DESKTOP_ASKERS];    /**< a look: told when it
                                                   *   ends */
        desktop_asker_t again_by[DESKTOP_ASKERS]; /**< a look: told when the
                                                   *   look after it ends */
        char  name[64];                 /**< a reclaim: the board's name */
        char  uid[64];                  /**< a reclaim: the board's unique id */
    }                        look, reclaim; /**< the look, and the reclaim */
    /* Boards released so another program could use their ports.  A board
     * is kept by its name and unique id: a reclaim finds it on whatever
     * port the host gives it next. */
    struct {
        char name[64];                  /**< the mount name it had, and
                                         *   is given back */
        char uid[40];                   /**< the chip's unique id */
        int  used;                      /**< the entry holds a board */
    }                        released[TIKU_MOUNT_MAX]; /**< released boards */
    /* The device tray on the global bar: a cell per board being connected,
     * connected, lent or lost, in the order they came, cell 0 beside the
     * camera. */
    struct desktop_devtray {
        int           state;            /**< a DESKTOP_DEVTRAY_ value */
        char          port[256];        /**< its port, or "" */
        char          name[64];         /**< its leaf under /devices, or ""
                                         *   while it connects */
        char          why[64];          /**< what went wrong, while red */
        int64_t       since;            /**< when it came, microseconds */
        int64_t       red_until;        /**< red until then, or 0 */
        tiku_motion_t landing;          /**< the ring closing as it lands */
    }                        devtray[TIKU_MOUNT_MAX];
    /** Each cell as drawn last, or empty when it was not drawn. */
    tiku_rect_t              devtray_box[TIKU_MOUNT_MAX];
    char                     devtray_menu_of[64]; /**< the board the cell
                                                   *   menu dropped from */
    tiku_menu_t              devtraymenu;
#endif
 /**< a cell's menu */
    /** The remote window that took the current press, whose session gets the
     * moves and the release; NULL for none. */
    tiku_window_t  *remote_press;
    /** The hosted application's window that took the current press, or NULL. */
    struct embed_win     *embed_press;
    /** The window a resize is reshaping, whose owner is told the new size at
     * the release; NULL for none. */
    tiku_window_t  *resize_track;
    tiku_window_t  *box_press;          /**< the window whose close or zoom box
                                         *   is held down, or NULL */
    int             box_hit;            /**< TIKU_HIT_CLOSE or TIKU_HIT_ZOOM */
    /** The modifiers at the press, which decide what the box does even when a
     * key is released before the button. */
    unsigned        box_mods;
    /* The application the file panel is open for, if any.  The panel
     * carries only a (target, command) pair; the rest of the application's
     * request is kept here. */
    struct {
        int                   kind;     /**< 0 none, 1 hosted, 2 remote */
        struct desktop_embed *slot;     /**< hosted: which one */
        int                   session;  /**< remote: which session */
        uint32_t              window;   /**< the window that asked */
        /** The session's generation, which tells a reused session slot
         * apart. */
        uint64_t              generation;
        int                   local;    /**< return host filenames only */
    } pending_pick; /**< the application's pick request */
    /** The panels devices described. Each form names its controls and the paths
     * they show; the desktop reads and writes those paths and draws the
     * controls. */
    tiku_trk_described_t panels[TIKU_TRK_PANELS_MAX];
#if TIKU_SHELL_BOARD_SERVICES

    /** Each board's window session: beside the shell of a board on a cable, or
     * on the link a board dialled in on; NULL for a free slot. */
    tiku_channel_t      *channels[TIKU_MOUNT_MAX];
    /** For a channel a board dialled in on, the mount made over its link,
     * removed when the link is lost; "" otherwise. */
    char                 channel_mount[TIKU_MOUNT_MAX][TIKU_PATH_MAX];
    /** For a channel beside a cabled board's shell, that board's mount; ""
     * otherwise. */
    char                 channel_board[TIKU_MOUNT_MAX][TIKU_PATH_MAX];
    int                  board_listen;  /**< the -listen socket; 0 or
                                         *   less for none */
    /* One per mount slot: the identity of the board's gui/ node, which
     * tells the board's change records apart, and whether its forms were
     * opened unasked.  They are opened unasked once: a panel a person
     * closed is not put back. */
    struct {
        char     root[TIKU_PATH_MAX];   /**< the mount's prefix */
        uint64_t gui_id;                /**< the gui/ node's identity: 0 not
                                         *   looked up, ~0 none */
        int      announced;             /**< its forms were opened unasked */
    } boards[TIKU_MOUNT_MAX];
#endif
 /**< one per mount slot */
    /** Where the runner is, for starting an installed application in a process
     * of its own. */
    char                 runner[TIKU_PATH_MAX];
    tiku_query_t     pending_query;     /**< the query the save panel is asking
                                         *   a destination for */
    int                  theme_mode;    /**< the theme in use; -1 until
                                         *   first applied */
    /** The global menu bar: 1 on, 0 off, -1 until the first sync. It is one
     * strip at the top of the screen: the system menu at its left and the
     * menus the focused window published, rebuilt on every draw. */
    int                  gbar_on;
    tiku_menu_t      sysmenu;          /**< the system menu at the left */
#if TIKU_SHELL_BOARD_SERVICES
    tiku_menu_t      devmenu;           /**< the Devices menu beside it */
    tiku_menu_t      devsub;            /**< its Default device submenu */
    struct desktop_deviceprefs *deviceprefs; /**< the Devices Preferences
                                              *   window's state, or NULL */
    struct desktop_vfs_job *vfs_jobs;
#endif
   /**< device service requests
                                         *   in progress */
    struct desktop_startup *startup;    /**< the Startup Preferences window's
                                         *   state, or NULL */
    struct desktop_associations *associations; /**< the File Associations
                                                *   window's state, or NULL */
    struct desktop_forcequit *forcequit; /**< the Force Quit window's
                                          *   state, or NULL */
    /** The window that took the current primary press and gets its moves and
     * release, or NULL. */
    tiku_window_t *prefs_track;
    /** The focused window at the last turn, compared and never dereferenced; a
     * change cancels a composition. */
    tiku_window_t *input_focus;
    tiku_text_input_t legacy_commit;    /**< the text being composed for the
                                         *   desktop's own fields */
    tiku_window_t *legacy_target;       /**< the window the
                                         *   composition began in */
    tiku_menu_t      gmenu[TIKU_MENUSET_MENUS]; /**< the published menus */
    tiku_menu_t      gsub[TIKU_MENUSET_MENUS][4]; /**< their submenus */
    tiku_menubar_t   gbar;              /**< the strip they hang from */
    tiku_window_t  *gbar_from;          /**< the window whose menus it shows */
    /** The focused program's About command, shown in the system menu; 0 for
     * none. */
    int                  about_hoist;
    char                 about_label[64]; /**< the label it was
                                           *   published under */
    int                  gbar_open;     /**< the strip menu open, or -1 */
    int                  qdrag_on;      /**< the Find window's query icon is
                                         *   being dragged (Q-034) */
    /** The folder the query icon was dropped on. */
    char                 qdrop_dest[TIKU_PATH_MAX];
    int                  cursor_kind;   /**< TIKU_CURSOR_*, as last asked */
    /** The popup and submenu last drawn, for their fade-in. */
    const tiku_menu_t *popup_seen, *popup_sub_seen;
    /** When each was first drawn, in microseconds. */
    int64_t              popup_since, popup_sub_since;
    tiku_motion_t        activation_motion; /**< the outline growing out of
                                             *   an opened icon */
    tiku_rect_t          activation_rect; /**< the icon it grows out of */
    /** Where the next folder window without a saved or inherited frame opens;
     * advanced on every folder window opened, and reset when the next window
     * would run off the screen. */
    int                  cascade_x, cascade_y;
    struct tiku_store *store;           /**< settings and state, on disk */
    /** The desktop's own icons: a pose view with its union source and
     * watcher. */
    tiku_trk_desktop_view_t desktop_view;
    int                  desk_row;      /**< the desktop pose under the
                                         *   pointer, or -1 */
    tiku_trk_clip_t      clip;          /**< the desktop's one clipboard, shared
                                         *   by every window */
    /** The Select by pattern window: one for the desktop, hidden when closed,
     * so its expression is kept for the next use (CW-063, AW-105). */
    tiku_trk_select_window_t select_window;
    tiku_trk_open_with_window_t open_with_window; /**< the Open With window, one
                                                   *   for the desktop */
    tiku_trk_loop_t     *loop;          /**< deferred and periodic work, run
                                         *   from the loop's tick (AW-083) */
    char                 clock_stamp[64]; /**< what the clock last showed */
    /** The last press on a pose: for the long press, and to tell a slow second
     * click from a drag. */
    tiku_trk_press_t     press;
    int64_t              info_icon_at;  /**< when an Info window's icon was
                                         *   last pressed */
    tiku_window_t       *info_icon_win; /**< and that Info window */
    tiku_trk_drag_t      drag;          /**< the gesture between
                                         *   press and drop */
    unsigned             drag_live_mods; /**< the modifiers as of
                                          *   the last move */
    /** The column title strip's gesture, one at a time, in the folder window
     * the press was in (TS-063). */
    tiku_trk_titleview_t tstrip;
    folder_t            *tstrip_fl;     /**< that folder window, or NULL */
    folder_t            *sbar_fl;       /**< the folder window whose scroll bar
                                         *   was last pressed */
    int                  strip_ptr_x;   /**< the pointer's x at the last move
                                         *   over a title strip */
    int                  strip_ptr_y;   /**< and its y */
    tiku_thumb_q_t   thumbs;            /**< image previews, decoded
                                         *   on a worker */
    /** The desktop's node-icon cache: a file shown in two windows is one entry,
     * and an icon edited on disk is dropped from it once (IV-038). */
    tiku_trk_nodeicon_t  nicons;
    /** The installed applications and what each reads; the Open with menu, a
     * double click and a drop on an application all use it (AW-040). */
    tiku_trk_apps_t      apps;
    tiku_trk_addons_t    addons;        /**< the add-ons and their keys */
    /** The add-ons directory, watched by modification time so an add-on
     * installed while the desktop runs is loaded without a restart
     * (AW-022). */
    char                 addons_dir[TIKU_PATH_MAX];
    /** The user shortcut file, watched the same way (AW-023). */
    char                 addons_keys[TIKU_PATH_MAX];
    time_t               addons_mtime;  /**< the directory's modification time,
                                         *   as last seen */
    time_t               keys_mtime;    /**< the file's, as last seen */
    int64_t              addons_checked; /**< when they were last looked at, in
                                          *   microseconds */
    /** What the system knows about each type: its description, its icon, and
     * the columns worth showing (AW-013, CW-026). */
    tiku_trk_types_t     types;
    /** What the count view last drew, for a script to read. */
    char                 count_text[TIKU_TRK_COUNT_TEXT_MAX];
    /** The folder the running job writes into, for the progress window's "To:"
     * line (FS-069). */
    char                 prog_dest[TIKU_PATH_MAX];
    /** What is mounted, and what was mounted at the last poll: the source of
     * mount and unmount notices (PVN-008). */
    tiku_volwatch_t  volwatch;
    /** The record the last search wrote, for Edit query to reopen. */
    char                 last_query[TIKU_NAME_MAX];
    /** The Open/Save panel, one for the desktop, asking one question at a
     * time. */
    tiku_trk_panel_window_t file_panel;
    tiku_trk_info_windows_t info_windows; /**< every Get Info window */
    /** The broken link an open offered Get Info for (AW-031). */
    char                 broken_open_path[TIKU_PATH_MAX];
    /** The link a picker was opened to re-point, matched by path to its
     *  Info window when the answer comes; empty when none. */
    char                 info_relink_path[TIKU_PATH_MAX];
    tiku_trk_status_overlay_t status_overlay; /**< the alert and the
                                               *   progress window */
    tiku_rect_t              shot_btn;  /**< the camera button at the global
                                         *   bar's right, or empty */
    int                      shot_pending; /**< a screenshot is owed */
    int                      shot_pressed; /**< the camera button is held */
    tiku_trk_prefs_window_t prefs_window; /**< Tracker preferences */
    /** Deskbar preferences. */
    tiku_trk_deskbar_prefs_window_t deskbar_prefs_window;
    /** Desktop preferences. */
    tiku_trk_desktop_prefs_window_t desktop_prefs_window;
    tiku_trk_fonts_window_t         fonts_window; /**< Fonts preferences */
    char help_root[TIKU_PATH_MAX];      /**< the installed documentation
                                         *   directory, or "" */
    tiku_trk_sounds_window_t        sounds_window; /**< Sounds preferences */
    tiku_sound_event_t sound_pick;      /**< the sound event the file panel
                                         *   picks a sound for */
    tiku_window_t *sound_focused;       /**< the front window at
                                         *   the last turn */
    int            sound_windows;       /**< how many windows were open at
                                         *   the last turn */
    /** Hide dotfiles as last applied to the windows; -1 before the first. */
    int                  applied_hide_dotfiles;
    int                  applied_full_path; /**< full path in
                                             *   titles, likewise */
    int                  applied_folders_first; /**< folders sort
                                                 *   first, likewise */
    tiku_trk_switcher_window_t switcher_window; /**< the window switcher */
    tiku_trk_nav_t       nav;           /**< the context menu's
                                         *   navigation walk */
    int                  nav_on;        /**< the walk is showing */
    /** Spring navigation is showing: the same walk, anchored to a sprung pose
     * (PVS-051). */
    int                  spring_nav_on;
    int                  spring_nav_x;  /**< where its root row is drawn */
    int                  spring_nav_y;  /**< and its y */
    /** The window in front when a file job began, focused again when it ends
     * (FS-073); NULL for none. */
    tiku_window_t      *prog_was_active;
    int                  ptr_x, ptr_y;  /**< where the pointer last was */
    /** The path the context menu was raised on when it is the Info window's
     * icon, "" otherwise (AW-076). */
    char                 popup_path[TIKU_PATH_MAX];
    /** The folder window the context menu was raised on: the shared menu model
     * is rebuilt for it, and the chosen command runs there. */
    folder_t            *popup_fl;
    tiku_trk_responders_t responders;   /**< the surfaces that answer events for
                                         *   themselves, in z-order */
    tiku_trk_responder_t  bar_responder; /**< the Deskbar's entry */
    desktop_bar_t         bar;          /**< the Deskbar responder's state
                                         *   between events */
    tiku_trk_responder_t  info_hover_responder; /**< the field pop-up */
    tiku_trk_responder_t  spring_responder; /**< the spring navigator */
    tiku_trk_responder_t  popup_responder; /**< the popup menus */
    tiku_trk_responder_t  status_responder; /**< the alert and the
                                             *   progress window */
    tiku_trk_responder_t  forcequit_responder; /**< the Force Quit window */
    tiku_trk_responder_t  windows_responder; /**< the window frames */
    tiku_trk_responder_t  folders_responder; /**< the folders' content */
    tiku_trk_responder_t  infowin_responder; /**< the Info windows */
    tiku_trk_responder_t  gbar_responder; /**< the global menu bar */
    tiku_trk_responder_t  keys_responder; /**< the desk's keyboard */
    tiku_trk_responder_t  help_responder; /**< F1 */
    tiku_trk_click_t      click;        /**< the folders responder's last click,
                                         *   for double clicks */
} desktop_t;

static folder_t *folder_of(desktop_t *app, tiku_window_t *w);
static folder_t *desktop_folder(desktop_t *app);

/** @brief One created entry waiting to be selected where it lives. */
typedef struct {
    desktop_t *app;                 /**< the desktop it was created in */
    char       path[TIKU_PATH_MAX]; /**< the entry, selected once listed */
} select_created_t;

/** @brief What the command line asked for. */
typedef struct {
#if TIKU_SHELL_BOARD_SERVICES

    /** -devices: the -p board is mounted under /devices and the local files
     * stay at the root; with no -p, the ports are listed at boot. */
    int         want_devices;
    int         listen_port;        /**< -listen: the port boards
                                     *   dial in on, or 0 */
    const char *port;               /**< -p: one board's port, else a scan */
    int         baud;               /**< -b: the port's baud, 115200
                                     *   when not given */
    int         use_device;         /**< -d, or -p alone: the
                                     *   board is the root */
    const char *mount_at;
#endif
           /**< -mount: where the -p
                                     *   board is mounted */
    const char *shot;               /**< -shot: write a frame here two
                                     *   seconds in; -seconds ends the run */
    /** -shotmenu, -shotpanel, -shotlive, -shotshift and -shotzoom: what the
     * frame shows. */
    int         shot_menu, shot_panel, shot_live, shot_shift, shot_zoom;
    /** -shotcascade, -shotnotify and -shotselect, likewise. */
    int         shot_cascade, shot_notify, shot_select;
    const char *shot_edit, *shot_find; /**< the arguments of -shotedit
                                        *   and -shotfind */
    int         safe_mode;          /**< -safe: restore no session (AW-007) */
    const char *script_path;        /**< -script: the file driving this run */
    int         mute;               /**< -mute: every sound silent */
    int         fullscreen;         /**< -fullscreen: the window
                                     *   is the screen */
    /** -conduct: the conductor channel's socket path; -conduct-tty: a serial
     * line to speak it over. The channel is off unless one is given. */
    const char *conduct_path, *conduct_tty;
    const char *after;              /**< -after: the exit report of the desk
                                     *   this one was started after */
    /** -conduct-token and -conduct-query-token: the names a peer gives at HELLO
     * to drive, or only to ask and watch; when either is set, an unnamed peer
     * is closed at HELLO. */
    const char *conduct_token, *conduct_query_token;
    const char *openme[TIKU_WINDOW_MAX]; /**< paths to open at start */
    const char *argv0;              /**< how this shell was started */
    int         nopen;              /**< openme[] in use */
    int64_t     deadline;           /**< -seconds: stop at this time, in
                                     *   microseconds; 0 never */
} desktop_options_t;

/** @brief What the shell's loop keeps between one event and the next. */
typedef struct {
    tiku_trk_click_t    click;      /**< the shell's last click */
    tiku_trk_script_t  *sc;         /**< the script driving this run, or NULL */
    int64_t             shot_at;    /**< when the -shot frame is due, 0 never */
    /** When the notice pump, the live-query re-run and the sweep last ran,
     * and when the loop started; in microseconds. */
    int64_t             last_pump, last_requery, last_sweep, started_at;
} desktop_loop_t;

/**
 * @brief Open the described panel at @p path, a .panel file or a board's
 *        gui/<form>/panel node.  @return 1 when a window opened.
 */
int open_panel_path(desktop_t *app, const char *path);
/** @brief The panel shown in @p w, or NULL. */
tiku_trk_described_t *panel_of_window(desktop_t *app, const tiku_window_t *w);
/**
 * @brief A key for the focused described panel's entry, if one has the
 *        caret: typed, entered (and written) or reverted.  @return 1 when
 *        the panel took it.
 */
int panel_key(desktop_t *app, unsigned key, char ch);
/** @brief Write what the panels have queued, one row per turn. */
void panels_pump(desktop_t *app);
/** @brief The panel whose slider is being dragged, or NULL. */
tiku_trk_described_t *panel_tracking(desktop_t *app);
/**
 * @brief The notices of a turn: a board's gui/ that changed refreshes
 *        its open panels, and a board seen for the first time has its
 *        forms opened.  @return how many panels changed.
 */
int panels_poll(desktop_t *app, const tiku_model_change_t *ch, int nch);
#if TIKU_SHELL_BOARD_SERVICES

/**
 * @brief Adopt every board's channel into the remote listener and greet
 *        it.
 * @note Called once the listener exists, after the mounts.
 */
void channels_attach(desktop_t *app);

/**
 * @brief Adopt one board's channel into the listener and greet it, for a
 *        board taken in after startup.
 * @return nonzero when the channel is now listened to.
 */
int  channel_attach(desktop_t *app, int slot);
#endif


/**
 * @brief Whether a remote client such as TikuTracker may have the desktop
 *        open @p path, judged by its name: a text, Markdown, HTML, BASIC
 *        or project document.
 */
int desktop_remote_document(const char *path);

/** @brief The Tracker preferences, kept by the preferences window. */
static inline tiku_trk_prefs_t *
prefs_model(desktop_t *app)
{
    return tiku_trk_prefs_window_model(&app->prefs_window);
}

/** @brief The Select by pattern state, kept by its window. */
static inline tiku_trk_select_t *
select_model(desktop_t *app)
{
    return tiku_trk_select_window_model(&app->select_window);
}

/** @brief The Find state, kept by the Find window. */
static inline tiku_trk_find_t *
find_model(desktop_t *app)
{
    return tiku_trk_find_window_model(&app->find_window);
}

/** @brief The alert, kept by the status overlay. */
static inline tiku_alert_t *
alert_model(desktop_t *app)
{
    return tiku_trk_status_overlay_alert(&app->status_overlay);
}

/** @brief The progress window's state, kept by the status overlay. */
static inline tiku_trk_progress_win_t *
progress_model(desktop_t *app)
{
    return tiku_trk_status_overlay_progress(&app->status_overlay);
}

/** @brief What the action table needs to know about the focused window. */
static inline tiku_trk_sc_ctx_t
menu_ctx(desktop_t *app, folder_t *fl, unsigned mods)
{
    return tiku_trk_folder_menu_context(&app->folder_menu, fl, mods);
}

/** @brief The folder behind a window, or NULL. */
static inline folder_t *
folder_of(desktop_t *app, tiku_window_t *w)
{
    return tiku_trk_folder_lifecycle_find(&app->folder_lifecycle, w);
}

/** @brief Whether the file panel is up with a question the desktop asked,
 *         not an application's. */
static inline int
panel_asks_desktop(desktop_t *app)
{
    return app->file_panel.window != NULL &&
           app->file_panel.target != NULL &&
           app->file_panel.target != (void *)&app->pending_pick;
}

/** @brief The folder behind the desktop's own icons. */
static inline folder_t *
desktop_folder(desktop_t *app)
{
    return tiku_trk_desktop_view_folder(&app->desktop_view);
}

/** @brief A mount slot's last change records; see desktop_t.wire. */
typedef struct desktop_wire desktop_wire_t;
#if TIKU_SHELL_BOARD_SERVICES

/* What a probe of a port is for.  LOOK records a board that answers as a
 * candidate and lets the port go.  RECLAIM mounts the released board it
 * finds by uid.  CONNECT mounts what answered.  BOOT is handled as LOOK,
 * except that its answer does not end a look. */
enum { DESKTOP_ASK_LOOK, DESKTOP_ASK_BOOT, DESKTOP_ASK_RECLAIM,
       DESKTOP_ASK_CONNECT };
/* What a device tray cell shows. */
enum {
    DESKTOP_DEVTRAY_NONE = 0,   /**< a free cell */
    DESKTOP_DEVTRAY_CONNECTING, /**< a connect probe is out on its port */
    DESKTOP_DEVTRAY_CONNECTED,  /**< mounted under /devices */
    DESKTOP_DEVTRAY_LENT,       /**< released for another program */
    DESKTOP_DEVTRAY_GONE        /**< failed or lost: red, then freed */
};
#endif


/* Shared across the family: defined in one file, used in another. */

/* tiku_desktop_automation.c */
int desktop_invoke_addon(const tiku_trk_addon_t *addon, void *context);
void desktop_addons_register(desktop_t *app);
void script_command_file(desktop_t *app, const char *path);
void sync_addons(desktop_t *app, int force);

/* tiku_desktop_commands.c */
int desktop_cmd_layout(desktop_t *app, folder_t *fl, int command);
int desktop_cmd_open_with(desktop_t *app, folder_t *fl, int command);
int desktop_cmd_query(desktop_t *app, folder_t *fl, int command);
int desktop_cmd_system(desktop_t *app, folder_t *fl, int command);
void build_per_type(desktop_t *app, folder_t *fl);
void desktop_press_hold(desktop_t *app);
void menu_invoke(desktop_t *app, folder_t *fl, int command);
#if TIKU_SHELL_BOARD_SERVICES


/* tiku_desktop_device_jobs.c */
int desktop_wire_changed(desktop_t *app, int slot);
int desktop_wire_changes(desktop_t *app, tiku_model_change_t *out, int max,
                         int *complete);
#endif

int desktop_wire_panel_write(desktop_t *app, tiku_trk_described_t *p, int row);
#if TIKU_SHELL_BOARD_SERVICES

int desktop_wire_probe(desktop_t *app, int kind, const char *port, int baud,
                       int probe, const char *at, int counted);
void desktop_wire_ask(desktop_t *app);
void desktop_wire_replicants(desktop_t *app);
void desktop_wire_run_over(desktop_t *app, struct tiku_board_basic *run,
                           int what, int session, struct desktop_embed *slot,
                           int saving, const char *name, const char *prefix);
#endif

void desktop_wire_wait(desktop_t *app, int *pending, long ms);
#if TIKU_SHELL_BOARD_SERVICES


/* tiku_desktop_device_stats.c */
int desktop_device_stats(desktop_t *app, const char *board, char *out,
                         size_t max);
#endif


/* tiku_desktop_drop.c */
int drop_launch(void *context, const struct tiku_trk_app *application,
                const char *const *paths, int count);
int drop_on_info_icon(desktop_t *app, int x, int y, unsigned modifiers);
int drop_on_panel(desktop_t *app, int x, int y);
int info_icon_drop_slot(desktop_t *app, int x, int y);
void do_drop(desktop_t *app, int x, int y, unsigned state, unsigned button);

/* tiku_desktop_file_actions.c */
int desktop_cmd_destination(desktop_t *app, folder_t *fl, int command,
                            const char **template_path);
int desktop_cmd_new_folder(desktop_t *app, folder_t *fl, int command,
                           int seeded, int seed_x, int seed_y);
int desktop_cmd_template(desktop_t *app, folder_t *fl, int command,
                         const char *template_path,
                         const char **template_parent, int *seeded, int *seed_x,
                         int *seed_y);
int folder_selection(desktop_t *app, folder_t *fl, char paths[][TIKU_PATH_MAX],
                     const char **out, int max);
void begin_edit(desktop_t *app, folder_t *fl);
void begin_rename(desktop_t *app, folder_t *fl, int row);
void commit_edit(desktop_t *app, folder_t *fl);
void commit_rename(desktop_t *app, int commit);
void desktop_progress_tick(desktop_t *app);
void desktop_rename_pulse(desktop_t *app);

/* tiku_desktop_filesystem_jobs.c */
int desktop_wire_unmount(desktop_t *app, const char *mount, const char *name);
void desktop_wire_fs_run(desktop_t *app);
void desktop_wire_install(desktop_t *app);
void desktop_wire_volumes(desktop_t *app);

/* tiku_desktop_folder_windows.c */
folder_t *open_folder(desktop_t *app, const char *path);
int trash_union_refresh(desktop_t *app, folder_t *fl);
void close_window(desktop_t *app, tiku_window_t *w);
void close_windows_under(desktop_t *app, const char *path);
void deskbar_edit_path(void *context, const char *path);
void file_actions_close_under(void *context, const char *path);
void launch_open_path(void *context, const char *path);
void offer_broken_link(desktop_t *app, const char *path);
void parent_path(const char *path, char *out, size_t size);

/* tiku_desktop_media_jobs.c */
int desktop_wire_thumb(desktop_t *app);

/* tiku_desktop_navigation.c */
int spring_nav_hit(desktop_t *app, int x, int y, char *dest, size_t max);
void desktop_spring_tick(desktop_t *app);
void spring_nav_close(desktop_t *app);
void spring_nav_track(desktop_t *app, int x, int y);
void spring_open_nav(void *context, const char *path);

/* tiku_desktop_volume_commands.c */
int desktop_cmd_mount(desktop_t *app, folder_t *fl, int command);
int desktop_cmd_unmount(desktop_t *app, folder_t *fl, int command);

/* tiku_trk_desktop.c */
void sync_folder_preferences(desktop_t *app);

/* tiku_trk_desktop_apps.c */
const char *desktop_app_art(desktop_t *app, const char *path);
const char *desktop_help_page_of(desktop_t *app, tiku_window_t *window);
const tiku_trk_app_t *desktop_app_at(desktop_t *app, const char *path);
#if TIKU_SHELL_BOARD_SERVICES

int desktop_bench_firmware(desktop_t *app, const char *board);
#endif

int desktop_help_html(desktop_t *app, const char *title, const char *html);
int desktop_help_open(desktop_t *app, const char *page);
int desktop_help_ready(desktop_t *app);
int desktop_launch_app(desktop_t *app, const char *id, const char *document);
int find_help_dir(const char *argv0, char *out, size_t max);
void build_pickers(desktop_t *app, folder_t *fl);
void offer_installed(desktop_t *app, const char *argv0);

/* tiku_trk_desktop_associations.c */
tiku_window_t *desktop_associations_handle(desktop_t *app);
void desktop_associations_close(desktop_t *app);
void desktop_associations_destroy(desktop_t *app);
void desktop_associations_open(desktop_t *app);

/* tiku_trk_desktop_bar.c */
void desktop_bar_attach(desktop_t *app);
void desktop_take_damage(desktop_t *app, tiku_rect_t damage);
#if TIKU_SHELL_REMOTE_SERVICES


/* tiku_trk_desktop_cursor.c */
/** @brief Apply native input geometry from the focused session window. */
void desktop_text_rect(desktop_t *app, int session, const tiku_msg_t *message);
#else


/* tiku_trk_desktop_boardrun.c */
int desktop_board_input(desktop_t *app, const char *text);
int desktop_board_list(desktop_t *app, char *out, size_t max);
int desktop_board_mcu(desktop_t *app, const char *name, char *out, size_t max);
int desktop_board_names(desktop_t *app, char *out, size_t max);
int desktop_board_port(desktop_t *app, const char *name, char *out, size_t max);
int desktop_board_reclaim(desktop_t *app, const char *name, int session,
                          struct desktop_embed *slot);
int desktop_board_release(desktop_t *app, const char *name);
int desktop_board_run(desktop_t *app, struct desktop_embed *slot,
                      const char *board, const char *program);
int desktop_board_save(desktop_t *app, struct desktop_embed *slot,
                       const char *board, const char *name,
                       const char *program);
int desktop_board_stop(desktop_t *app);
int device_connect(desktop_t *app, const char *port);
int device_disconnect(desktop_t *app, const char *name);
void desktop_board_look_begin(desktop_t *app, int session,
                              struct desktop_embed *slot);
void desktop_board_look_done(desktop_t *app);
void desktop_board_pump(desktop_t *app);
void desktop_board_reclaim_done(desktop_t *app);
void desktop_board_reclaim_landed(desktop_t *app, tiku_backend_t *dev);
void desktop_board_say(desktop_t *app, int session, struct desktop_embed *slot,
                       int what, const char *text);
void device_current_broadcast(desktop_t *app);
void device_set_current(desktop_t *app, const char *name);
#endif


/* tiku_trk_desktop_cursor.c */
void desktop_cursor(desktop_t *app, int session, const tiku_msg_t *msg);
void desktop_pointer_says(desktop_t *app, int x, int y);

/* tiku_trk_desktop_deskbar.c */
int deskbar_menu_icon(void *context, tiku_surface_t *s,
                      const tiku_trk_db_item_t *item, int x, int y, int size);
int paint_menu_icon(tiku_surface_t *s, const char *icon, int x, int y, int size,
                    int dimmed);
void deskbar_pick_dispatch(desktop_t *app, tiku_trk_db_pick_t *pick, int x,
                           int y);
void open_preferences(void *context);
#if TIKU_SHELL_BOARD_SERVICES


/* tiku_trk_desktop_deviceprefs.c */
int desktop_deviceprefs_popup(desktop_t *app);
tiku_window_t *desktop_deviceprefs_handle(desktop_t *app);
void desktop_deviceprefs_close(desktop_t *app);
void desktop_deviceprefs_destroy(desktop_t *app);
void desktop_deviceprefs_open(desktop_t *app);
void desktop_deviceprefs_open_on(desktop_t *app, const char *name);

/* tiku_trk_desktop_devtray.c */
int desktop_devtray_again(desktop_t *app, const tiku_event_t *ev,
                          int64_t now);
int desktop_devtray_at(desktop_t *app, int x, int y);
void desktop_devtray_connecting(desktop_t *app, const char *port);
void desktop_devtray_draw(desktop_t *app, tiku_rect_t strip, int left,
                          int right, int64_t now);
void desktop_devtray_draw_tip(desktop_t *app);
void desktop_devtray_failed(desktop_t *app, const char *port,
                            const char *name, const char *why);
void desktop_devtray_forget(desktop_t *app, const char *port);
void desktop_devtray_hover(desktop_t *app, int was_x, int was_y);
int desktop_devtray_invoke(desktop_t *app, int command);
void desktop_devtray_landed(desktop_t *app, const char *port,
                            const char *prefix);
int desktop_devtray_press(desktop_t *app, const tiku_event_t *ev,
                          int64_t now);
int desktop_devtray_tick(desktop_t *app, int64_t now, tiku_rect_t *damage);
int desktop_devtray_tip(desktop_t *app, char *out, size_t max);
#endif


/* tiku_trk_desktop_draw.c */
int resize_desktop(desktop_t *app, int width, int height);
int tick_clock(void *ctx);
void blit(desktop_t *app);
void desktop_damage(desktop_t *app, tiku_rect_t rect);
void desktop_damage_all(desktop_t *app);
void desktop_focus_paint(desktop_t *app);
void desktop_idle_work(desktop_t *app);
void desktop_screenshot_tick(desktop_t *app);
void draw_all(desktop_t *app);
void set_backdrop_color(void *context, const tiku_trk_color_t *color);

/* tiku_trk_desktop_drivers.c */
int conduct_text(void *context, const char *name, char *out, size_t max);
long script_redraw_diff(void *ctx);
unsigned conduct_pixel(void *context, int x, int y);
void conduct_inject(void *context, const tiku_event_t *event);

/* tiku_trk_desktop_embed.c */
int desktop_app_open_path(desktop_t *app, const char *path);
int embed_deliver(struct embed_win *ew, const tiku_event_t *ev);
int embed_open_or_raise(desktop_t *app,
                        const tiku_app_descriptor_t *descriptor);
int embed_start(desktop_t *app, const tiku_app_descriptor_t *descriptor);
int tick_embeds(void *ctx);
struct embed_win *embed_win_of_window(desktop_t *app, const tiku_window_t *w);
void embed_drop_window(struct embed_win *ew, int tell);
void embed_stop(struct desktop_embed *slot);

/* tiku_trk_desktop_events.c */
void desktop_key_down(desktop_t *app, const tiku_event_t *ev,
                      desktop_loop_t *lp);
void desktop_pointer_down(desktop_t *app, const tiku_event_t *ev,
                          desktop_loop_t *lp);
void desktop_pointer_move(desktop_t *app, const tiku_event_t *ev,
                          desktop_loop_t *lp);
void desktop_pointer_up(desktop_t *app, const tiku_event_t *ev,
                        desktop_loop_t *lp);
void desktop_text_input(desktop_t *app, const tiku_event_t *ev);
void desktop_turn_begin(desktop_t *app, const desktop_options_t *o,
                        desktop_loop_t *lp);
void desktop_turn_end(desktop_t *app, const desktop_options_t *o,
                      desktop_loop_t *lp);

/* tiku_trk_desktop_folders.c */
void desktop_folders_attach(desktop_t *app);

/* tiku_trk_desktop_forcequit.c */
int desktop_forcequit_input(desktop_t *app, const tiku_event_t *ev);
int desktop_forcequit_picking(desktop_t *app);
tiku_window_t *desktop_forcequit_handle(desktop_t *app);
void desktop_forcequit_close(desktop_t *app);
void desktop_forcequit_destroy(desktop_t *app);
void desktop_forcequit_open(desktop_t *app);
void desktop_forcequit_overlay(desktop_t *app);

/* tiku_trk_desktop_globalbar.c */
char key_char(const tiku_event_t *event);
const char *key_name(const tiku_event_t *event, char printable[2]);
tiku_rect_t global_bar_rect(desktop_t *app);
void global_bar_status_draw(desktop_t *app, tiku_rect_t strip);
void global_or_invoke(desktop_t *app, folder_t *ffl, int command);
void publish_folder_menus(desktop_t *app, folder_t *fl);
void rebuild_global_bar(desktop_t *app);

/* tiku_trk_desktop_grab.c */
int desktop_shot_path(const char *what, char *out, size_t max);
int desktop_shot_save(desktop_t *app, int session, const char *what,
                      uint32_t *px, int w, int h, int alpha);
void desktop_grab(desktop_t *app, int session, const tiku_msg_t *msg);
void desktop_shot_desk(desktop_t *app);

/* tiku_trk_desktop_infowin.c */
void desktop_infowin_attach(desktop_t *app);

/* tiku_trk_desktop_keys.c */
void desktop_help_attach(desktop_t *app);
void desktop_keys_attach(desktop_t *app);

/* tiku_trk_desktop_lifecycle.c */
int desktop_boot(desktop_t *app, const desktop_options_t *o,
                 desktop_loop_t *lp);
int desktop_parse_args(int argc, char **argv, desktop_options_t *o);
int desktop_teardown(desktop_t *app, desktop_loop_t *lp);
void begin_shutdown(desktop_t *app);
#if TIKU_SHELL_BOARD_SERVICES


/* tiku_trk_desktop_mounts.c */
int boards_listen(desktop_t *app, int port);
int channel_keep(desktop_t *app, struct tiku_channel *ch);
int device_scan(desktop_t *app);
int mount_devices(desktop_t *app, const char *port, int baud, const char *at);
int mount_landed(desktop_t *app, tiku_backend_t *dev, const char *port,
                 const char *at);
#endif

int port_is_mounted(desktop_t *app, const char *port);
#if TIKU_SHELL_BOARD_SERVICES

void boards_accept(desktop_t *app);
void boards_unlisten(desktop_t *app);
void channels_tend(desktop_t *app);
#endif

void desktop_device_pump(desktop_t *app);
void desktop_notice_pump(desktop_t *app, desktop_loop_t *lp);
void desktop_sweep(desktop_t *app, desktop_loop_t *lp);
#if TIKU_SHELL_BOARD_SERVICES

void device_candidate_record(desktop_t *app, const char *port,
                             const char *label);
void device_mount_name(const tiku_backend_t *dev, const tiku_backend_t *ns,
                       char *out, size_t max);
void device_port_label(const char *where, const char *const *all, int n,
                       char *out, size_t max);
void device_port_where(const char *port, char *out, size_t max);
#endif

void save_before_unmount(dev_t dev, void *context);

/* tiku_trk_desktop_panels.c */
const char *bind_path(const tiku_trk_described_t *p, const tiku_form_row_t *row,
                      char *buf, size_t max);
int desktop_document_local(desktop_t *app, const char *path);
int desktop_document_open(void *ctx, const tiku_trk_app_t *application,
                          const char *path);
int desktop_image_file(desktop_t *app, const char *path);
int desktop_reader_file(desktop_t *app, const char *path);
int launch_paths(desktop_t *app, const tiku_trk_app_t *application,
                 const char *const *paths, int count);
int list_children(tiku_backend_t *b, const char *path, tiku_model_t *out,
                  int max);
int open_one(desktop_t *app, const tiku_model_t *model);
int panel_search(void *context, tiku_trk_find_t *panel);
int refresh_results(desktop_t *app, folder_t *fl);
int select_named(desktop_t *app, folder_t *fl, const char *name);
tiku_window_t *apply_select_model(void *context, void *target,
                                  const tiku_trk_select_t *selection);
void build_open_with(desktop_t *app, folder_t *fl);
void desktop_remote_pick(desktop_t *app, int session, const tiku_msg_t *msg);
void find_save_as(void *context);
void find_select_folder(void *context);
void fpanel_commit(void *context, void *target, int command,
                   const char *destination);
void fpanel_open(desktop_t *app, folder_t *folder, int command);
void info_opens_with_fill(desktop_t *app, tiku_trk_info_t *info);
void info_opens_with_next(desktop_t *app, tiku_trk_info_t *info);
void open_panel(desktop_t *app);
void open_with_pick(desktop_t *app, folder_t *fl, int at);
void open_with_window_launch(void *context, const tiku_trk_app_t *application,
                             const char *const *paths, int count);
void panel_refreshed(tiku_trk_described_t *p, int asked, int answered);
void prepare_places(desktop_t *app);
void run_find(desktop_t *app, folder_t *from);
void sound_choose(void *context, tiku_sound_event_t event);
#if TIKU_SHELL_BOARD_SERVICES


/* tiku_trk_desktop_release.inl */
int desktop_board_unmount(desktop_t *app, const char *prefix);
int desktop_board_unmount_released(desktop_t *app, const char *prefix);
#endif


/* tiku_trk_desktop_remote.c */
void clip_mirror(desktop_t *app);
#if TIKU_SHELL_BOARD_SERVICES

void desktop_board_names_tell(desktop_t *app);
void desktop_board_tell(desktop_t *app, int session, int what,
                        const char *text);
#endif

void pick_sweep(desktop_t *app);
void remote_focus(desktop_t *app, tiku_window_t *focused);
void remote_hear(desktop_t *app);
void remote_reconcile(desktop_t *app);
void remote_sweep(desktop_t *app);

/* tiku_trk_desktop_report.c */
int desktop_report_write(desktop_t *app, const char *why, char *out,
                         size_t max);
void desktop_frame_report(desktop_t *app, FILE *out);
void desktop_report_poll(desktop_t *app);

/* tiku_trk_desktop_startup.c */
tiku_window_t *desktop_startup_handle(desktop_t *app);
void desktop_startup_close(desktop_t *app);
void desktop_startup_destroy(desktop_t *app);
void desktop_startup_offer(desktop_t *app, const char *path, const char *label,
                           const char *group);
void desktop_startup_offer_id(desktop_t *app, const char *path,
                              const char *label, const char *id,
                              const char *legacy);
void desktop_startup_open(desktop_t *app);
void desktop_startup_run(desktop_t *app, int safe_mode, int *restore,
                         int *fallback);

/* tiku_trk_desktop_surfaces.c */
int info_hover_text(desktop_t *app, char *out, size_t max);
void desktop_responders_attach(desktop_t *app);

/* tiku_trk_desktop_teams.c */
const char *desktop_window_team(desktop_t *app, tiku_window_t *w);
int desktop_team_has(desktop_t *app, int team, tiku_window_t *w);
int desktop_team_is_app(desktop_t *app, int team);
int desktop_tray_session(desktop_t *app, uint64_t owner);
void desktop_publish_teams(desktop_t *app);
void desktop_tray(desktop_t *app, int session, const tiku_msg_t *msg);
void desktop_tray_hit(desktop_t *app, uint64_t owner, const char *id);
#if TIKU_SHELL_BOARD_SERVICES


/* tiku_trk_desktop_vfs.c */
int desktop_vfs_destroy(desktop_t *app);
void desktop_vfs_request(desktop_t *app, int session,
                         const tiku_msg_t *request);
#endif


/* tiku_trk_desktop_windows.c */
void desktop_windows_attach(desktop_t *app);

/* End of the shared declarations. */

#endif /* TIKU_TRK_DESKTOP_PRIVATE_H_ */
