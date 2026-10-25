/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_x11.c - the X11 host: one window, its pixels and its events.
 *
 * Plain Xlib.  The interface layer draws every pixel; this file scales and
 * presents the frame, turns X events into tiku events, composes text
 * through the X input method, and serves the clipboard through
 * tiku_x11_clipboard.c.  macOS builds tiku_cocoa.m in its place.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_present.h"
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <limits.h>
#include <locale.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tiku_access_native.h"
#include "tiku_gfx.h"
#include "tiku_host.h"
#include "tiku_ime.h"
#include "tiku_text_input.h"
#include "tiku_x11_clipboard.h"
#include "tiku_x11_command.h"
#include <stdio.h>
#include <wchar.h>

struct tiku_host {
    tiku_print_native_t *printing; /**< owned native print service */
    tiku_access_native_t *access;  /**< native logical accessibility endpoint */
    Display *display;
    Window window;
    GC gc;
    XImage *image;
    Pixmap backing; /* last complete frame, and the background */
    int mapped;     /* the first presentation maps the window */
    Atom delete_window;
    Cursor resize_cursor;
    Cursor cursors[6]; /* by TIKU_CURSOR_*, created on first use */
    int cursor_kind;
    int scale;
    int logical_width;
    int logical_height;
    int native_width;
    int native_height;
    tiku_x11_clipboard_t clipboard;
    tiku_event_t deferred;
    int has_deferred, clipboard_ready;
    XIM input_method;
    XIC input_context;
    XIMCallback input_destroy;
    tiku_text_input_v2_t commit;
    tiku_ime_t ime;
    unsigned commit_token; /**< marked episode attached to staged commit */
    int marked_active;     /**< composition awaits its terminating commit */
    tiku_rect_t text_rect;
    XIMCallback preedit_start, preedit_done, preedit_draw, preedit_caret;
    unsigned command_mask; /* which modifiers mean "this is a command" */
    int fullscreen;        /* fullscreen was asked for */
    int force_present;
    uint64_t presented_frames, presented_bytes, presented_rects;
};

/**
 * @brief Xlib's callback when the input method goes away: forget the method
 *        and its context, which Xlib has destroyed, and any commit.
 */
static void
input_destroyed(XIM im, XPointer ctx, XPointer data)
{
    tiku_host_t *host = (tiku_host_t *)ctx;
    (void)im;
    (void)data;
    host->input_method = NULL;
    host->input_context = NULL;
    tiku_text_input_reset_v2(&host->commit);
    host->marked_active = 0;
    host->commit_token = 0;
    tiku_ime_cancel(&host->ime);
}

/** @brief Begin native marked-text collection with an unbounded IM length hint.
 */
static int
preedit_start(XIC ic, XPointer context, XPointer data)
{
    (void)ic;
    (void)data;
    ((tiku_host_t *)context)->marked_active =
        tiku_ime_begin(&((tiku_host_t *)context)->ime);
    return -1;
}
/** @brief End native marked-text collection; a pending commit owns completion.
 */
static void
preedit_done(XIC ic, XPointer context, XPointer data)
{
    (void)ic;
    (void)data;
    tiku_host_t *host = (tiku_host_t *)context;
    if (!host->commit.active)
        tiku_ime_cancel(&host->ime);
}
/** @brief Encode one valid native wide character as UTF-8. */
static size_t
wide_utf8(uint32_t cp, char *out)
{
    if (!cp || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
        return 0;
    if (cp < 128) {
        out[0] = cp;
        return 1;
    }
    if (cp < 2048) {
        out[0] = 0xc0 | (cp >> 6);
        out[1] = 0x80 | (cp & 63);
        return 2;
    }
    if (cp < 65536) {
        out[0] = 0xe0 | (cp >> 12);
        out[1] = 0x80 | ((cp >> 6) & 63);
        out[2] = 0x80 | (cp & 63);
        return 3;
    }
    out[0] = 0xf0 | (cp >> 18);
    out[1] = 0x80 | ((cp >> 12) & 63);
    out[2] = 0x80 | ((cp >> 6) & 63);
    out[3] = 0x80 | (cp & 63);
    return 4;
}
/** @brief Apply XIM scalar-indexed replacement text without publishing an edit.
 */
static void
preedit_draw(XIC ic, XPointer context, XPointer call)
{
    tiku_host_t *host = (tiku_host_t *)context;
    XIMPreeditDrawCallbackStruct *draw = (void *)call;
    char text[TIKU_TEXT_INPUT_V2_MAX];
    size_t bytes = 0;
    (void)ic;
    if (!draw || draw->chg_first < 0 || draw->chg_length < 0 || draw->caret < 0)
        goto reject;
    size_t characters = 0;
    for (size_t i = 0; host->ime.text[i]; i++)
        characters += ((unsigned char)host->ime.text[i] & 0xc0) != 0x80;
    if ((size_t)draw->chg_first > characters ||
        (size_t)draw->chg_length > characters - (size_t)draw->chg_first)
        goto reject;
    if (draw->text) {
        XIMText *native = draw->text;
        if (native->length &&
            (native->encoding_is_wchar ? native->string.wide_char == NULL
                                       : native->string.multi_byte == NULL)) {
            (void)tiku_ime_replace(&host->ime, 0, 0, "", 0, draw->caret);
            return;
        }
        if (native->encoding_is_wchar) {
            for (size_t i = 0; i < native->length; i++) {
                if (bytes > sizeof text - 5)
                    goto reject;
                size_t n = wide_utf8(native->string.wide_char[i], text + bytes);
                if (!n)
                    goto reject;
                bytes += n;
            }
        } else {
            mbstate_t state = {0};
            const char *at = native->string.multi_byte;
            for (size_t i = 0; i < native->length; i++) {
                wchar_t cp;
                size_t n = mbrtowc(&cp, at, MB_CUR_MAX, &state);
                if (!n || n == (size_t)-1 || n == (size_t)-2 ||
                    bytes > sizeof text - 5)
                    goto reject;
                size_t encoded = wide_utf8(cp, text + bytes);
                if (!encoded)
                    goto reject;
                bytes += encoded;
                at += n;
            }
        }
    }
    (void)tiku_ime_replace(&host->ime, draw->chg_first, draw->chg_length, text,
                           bytes, draw->caret);
    return;
reject:
    tiku_ime_cancel(&host->ime);
}
/** @brief Honor an absolute XIM marked-text caret request. */
static void
preedit_caret(XIC ic, XPointer context, XPointer call)
{
    tiku_host_t *host = (tiku_host_t *)context;
    XIMPreeditCaretCallbackStruct *caret = (void *)call;
    (void)ic;
    if (!caret || caret->direction != XIMAbsolutePosition ||
        caret->position < 0)
        return;
    (void)tiku_ime_replace(&host->ime, 0, 0, "", 0, caret->position);
}

/**
 * @brief Open the input method and a context on the window, the server's
 *        input method first and local Compose (@im=none) failing that.
 */
static void
input_open(tiku_host_t *host)
{
    XIMStyles *styles = NULL;
    XIMStyle chosen = 0;
    if (host->input_method)
        return;
    (void)XSetLocaleModifiers("");
    host->input_method = XOpenIM(host->display, NULL, NULL, NULL);
    if (!host->input_method) {
        (void)XSetLocaleModifiers("@im=none");
        host->input_method = XOpenIM(host->display, NULL, NULL, NULL);
        (void)XSetLocaleModifiers("");
    }
    if (!host->input_method)
        return;
    if (!XGetIMValues(host->input_method, XNQueryInputStyle, &styles, NULL) &&
        styles) {
        XIMStyle choices[] = {XIMPreeditCallbacks | XIMStatusNothing,
                              XIMPreeditCallbacks | XIMStatusNone,
                              XIMPreeditNothing | XIMStatusNothing,
                              XIMPreeditNone | XIMStatusNone};
        for (int j = 0; !chosen && j < 4; j++)
            for (unsigned i = 0; i < styles->count_styles; i++)
                if (styles->supported_styles[i] == choices[j])
                    chosen = choices[j];
    }
    if (styles)
        XFree(styles);
    if (chosen & XIMPreeditCallbacks) {
        union {
            int (*start)(XIC, XPointer, XPointer);
            XIMProc callback;
        } begin;
        begin.start = preedit_start;
        host->preedit_start = (XIMCallback){(XPointer)host, begin.callback};
        host->preedit_done =
            (XIMCallback){(XPointer)host, (XIMProc)preedit_done};
        host->preedit_draw =
            (XIMCallback){(XPointer)host, (XIMProc)preedit_draw};
        host->preedit_caret =
            (XIMCallback){(XPointer)host, (XIMProc)preedit_caret};
        XVaNestedList attrs = XVaCreateNestedList(
            0, XNPreeditStartCallback, &host->preedit_start,
            XNPreeditDoneCallback, &host->preedit_done, XNPreeditDrawCallback,
            &host->preedit_draw, XNPreeditCaretCallback, &host->preedit_caret,
            NULL);
        if (attrs) {
            host->input_context =
                XCreateIC(host->input_method, XNInputStyle, chosen,
                          XNClientWindow, host->window, XNFocusWindow,
                          host->window, XNPreeditAttributes, attrs, NULL);
            XFree(attrs);
        }
    } else if (chosen)
        host->input_context =
            XCreateIC(host->input_method, XNInputStyle, chosen, XNClientWindow,
                      host->window, XNFocusWindow, host->window, NULL);
    if (!host->input_context) {
        XCloseIM(host->input_method);
        host->input_method = NULL;
        return;
    }
    host->input_destroy = (XIMCallback){(XPointer)host, input_destroyed};
    (void)XSetIMValues(host->input_method, XNDestroyCallback,
                       &host->input_destroy, NULL);
    long mask = 0;
    XWindowAttributes attr;
    if (!XGetICValues(host->input_context, XNFilterEvents, &mask, NULL) &&
        XGetWindowAttributes(host->display, host->window, &attr))
        XSelectInput(host->display, host->window, attr.your_event_mask | mask);
}

void
tiku_host_text_rect(tiku_host_t *host, tiku_rect_t rect)
{
    if (!host || rect.w < 0 || rect.h < 0)
        return;
    host->text_rect = rect;
    if (host->input_context) {
        XPoint spot = {(short)rect.x, (short)(rect.y + rect.h)};
        XVaNestedList attrs =
            XVaCreateNestedList(0, XNSpotLocation, &spot, NULL);
        if (attrs) {
            (void)XSetICValues(host->input_context, XNPreeditAttributes, attrs,
                               NULL);
            XFree(attrs);
        }
    }
}

void
tiku_host_cancel_composition(tiku_host_t *host)
{
    if (!host)
        return;
    if (host->input_context) {
        char *discard = Xutf8ResetIC(host->input_context);
        if (discard)
            XFree(discard);
    }
    tiku_text_input_reset_v2(&host->commit);
    host->marked_active = 0;
    host->commit_token = 0;
    tiku_ime_cancel(&host->ime);
}

/** @brief Encode a keysym's Unicode value when no XIM context is available. */
static int
input_utf8(unsigned cp, char *out)
{
    if (cp < 128) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 2048) {
        out[0] = (char)(0xc0 | cp >> 6);
        out[1] = (char)(0x80 | (cp & 63));
        return 2;
    }
    if (cp >= 0xd800 && cp <= 0xdfff)
        return 0;
    if (cp < 65536) {
        out[0] = (char)(0xe0 | cp >> 12);
        out[1] = (char)(0x80 | ((cp >> 6) & 63));
        out[2] = (char)(0x80 | (cp & 63));
        return 3;
    }
    if (cp > 0x10ffff)
        return 0;
    out[0] = (char)(0xf0 | cp >> 18);
    out[1] = (char)(0x80 | ((cp >> 12) & 63));
    out[2] = (char)(0x80 | ((cp >> 6) & 63));
    out[3] = (char)(0x80 | (cp & 63));
    return 4;
}

/**
 * @brief How many device pixels make one drawn pixel at @p dpi: dpi / 96
 *        rounded, from 1 to 4.
 *
 * A whole number, so every drawn pixel is a square of device pixels.
 */
static int
scale_for_dpi(double dpi)
{
    int scale = (int)(dpi / 96.0 + 0.5);

    if (scale < 1) {
        scale = 1;
    }
    if (scale > 4) {
        scale = 4;
    }
    return scale;
}

/**
 * @brief The dpi a screen's width in pixels and in millimetres give.
 *
 * Both come from one server, so the ratio holds whichever pixels it
 * counts: device pixels give a high dpi and a large scale, logical pixels
 * a low dpi and a small one.  A server that does not know its size
 * reports about 96, which is scale 1.
 *
 * @return the dpi, or 0.0 when either number is not positive.
 */
static double
dpi_from_size(int pixels, int millimetres)
{
    if (pixels <= 0 || millimetres <= 0) {
        return 0.0;
    }
    return (double)pixels * 25.4 / (double)millimetres;
}

/**
 * @brief The integer UI scale: TIKU_SCALE when set, else from Xft.dpi,
 *        else from the screen's size, else 1.
 */
static int
display_scale(Display *display)
{
    XrmDatabase database;
    XrmValue value;
    char *manager, *type = NULL;
    double dpi = 0.0;
    int screen;

    const char *forced = getenv("TIKU_SCALE");

    if (forced != NULL && forced[0] != '\0') {
        return scale_for_dpi(atoi(forced) * 96.0);
    }
    manager = XResourceManagerString(display);
    if (manager != NULL) {
        XrmInitialize();
        database = XrmGetStringDatabase(manager);
        if (database != NULL &&
            XrmGetResource(database, "Xft.dpi", "Xft.Dpi", &type, &value) &&
            value.addr != NULL) {
            dpi = strtod(value.addr, NULL);
        }
        if (database != NULL) {
            XrmDestroyDatabase(database);
        }
    }
    /*
     * A desktop session sets Xft.dpi; an X server started under Aqua on
     * a Mac has no resource database, so the screen's own measurements
     * are asked next.
     */
    if (dpi <= 0.0) {
        screen = DefaultScreen(display);
        dpi = dpi_from_size(DisplayWidth(display, screen),
                            DisplayWidthMM(display, screen));
    }
    if (dpi <= 0.0) {
        dpi = 96.0;
    }
    return scale_for_dpi(dpi);
}

/** @brief Map a physical pointer position into the logical framebuffer. */
static int
logical_coord(int value, int native_extent, int logical_extent)
{
    if (value < 0) {
        return -1;
    }
    if (value >= native_extent) {
        return logical_extent;
    }
    return (int)((int64_t)value * logical_extent / native_extent);
}

/** @brief Replace the XImage after the window manager changes its size. */
static int
resize_image(tiku_host_t *host, int width, int height)
{
    XImage *image;
    char *pixels;
    int screen;

    if (host == NULL || width <= 0 || height <= 0 ||
        (size_t)width > SIZE_MAX / 4u / (size_t)height) {
        return -1;
    }
    pixels = malloc((size_t)width * (size_t)height * 4u);
    if (pixels == NULL) {
        return -1;
    }
    screen = DefaultScreen(host->display);
    image = XCreateImage(host->display, DefaultVisual(host->display, screen),
                         (unsigned)DefaultDepth(host->display, screen), ZPixmap,
                         0, pixels, (unsigned)width, (unsigned)height, 32, 0);
    if (image == NULL) {
        free(pixels);
        return -1;
    }
    Pixmap backing = XCreatePixmap(
        host->display, host->window, (unsigned)width, (unsigned)height,
        (unsigned)DefaultDepth(host->display, screen));
    if (backing == None) {
        XDestroyImage(image);
        return -1;
    }
    /* The old background remains server-owned until the replacement is
     * fully painted and installed by present(), even across a resize. */
    if (host->backing != None)
        XFreePixmap(host->display, host->backing);
    host->backing = backing;
    if (host->image != NULL) {
        XDestroyImage(host->image);
    }
    host->image = image;
    host->force_present = 1;
    host->native_width = width;
    host->native_height = height;
    host->logical_width = (width + host->scale - 1) / host->scale;
    host->logical_height = (height + host->scale - 1) / host->scale;
    return 0;
}

/**
 * @brief Which modifier bits the keymap puts Meta and Super on, the
 *        Command key; the bits differ between servers.
 *
 * Shift, Lock, Control and Mod1 are left out: Mod1 is Option here, and
 * Meta and Alt share a key on most keymaps.
 *
 * @param map   The server's modifier map.
 * @param syms  Its keycode-to-keysym table.
 * @param first The keycode @p syms starts at, @p count its length in
 *              keycodes and @p per its stride.
 * @return the mask, or 0 when this keymap has no key free to mean it.
 */
static unsigned
command_mask_of(const XModifierKeymap *map, const KeySym *syms, int first,
                int count, int per)
{
    unsigned mask = 0u;
    int index, slot, which;

    if (map == NULL || syms == NULL || per <= 0 || count <= 0) {
        return 0u;
    }
    for (index = 0; index < 8; index++) {
        for (slot = 0; slot < map->max_keypermod; slot++) {
            KeyCode code = map->modifiermap[index * map->max_keypermod + slot];

            /* The map and the table are two round trips, and a keymap
             * can change between them: a code outside the range that
             * was fetched names no row of it. */
            if (code == 0 || (int)code < first || (int)code >= first + count) {
                continue;
            }
            for (which = 0; which < per; which++) {
                KeySym sym = syms[(code - first) * per + which];

                if (sym == XK_Meta_L || sym == XK_Meta_R || sym == XK_Super_L ||
                    sym == XK_Super_R) {
                    mask |= 1u << index;
                }
            }
        }
    }
    return mask & ~(unsigned)(ShiftMask | LockMask | ControlMask | Mod1Mask);
}

/**
 * @brief The modifiers that make a key press a command: Control always,
 *        and on a machine whose command key is Command
 *        (tiku_x11_command_is_meta()) the Meta and Super bits as well.
 */
static unsigned
command_modifiers(Display *display)
{
    XModifierKeymap *map;
    KeySym *syms;
    int first = 0, count = 0, per = 0;
    unsigned mask = ControlMask;

    if (!tiku_x11_command_is_meta()) {
        return mask;
    }
    XDisplayKeycodes(display, &first, &count);
    count = count - first + 1;
    if (count <= 0) {
        return mask;
    }
    syms = XGetKeyboardMapping(display, (KeyCode)first, count, &per);
    map = XGetModifierMapping(display);
    mask |= command_mask_of(map, syms, first, count, per);
    if (map != NULL) {
        XFreeModifiermap(map);
    }
    if (syms != NULL) {
        XFree(syms);
    }
    return mask;
}

/** @brief Translate native modifiers once, at the host boundary. */
static unsigned
event_modifiers(const tiku_host_t *host, unsigned state)
{
    unsigned modifiers = 0u;

    if ((state & ShiftMask) != 0u) {
        modifiers |= TIKU_MOD_SHIFT;
    }
    if ((state & host->command_mask) != 0u) {
        modifiers |= TIKU_MOD_CMD;
    }
    if ((state & Mod1Mask) != 0u) {
        modifiers |= TIKU_MOD_OPTION;
    }
    if ((state & LockMask) != 0u) {
        modifiers |= TIKU_MOD_CAPS;
    }
    return modifiers;
}

/** @brief Translate navigation keys; printable values remain characters. */
static unsigned
event_key(KeySym keysym)
{
    switch (keysym) {
    case XK_Up:
        return TIKU_KEY_UP;
    case XK_Down:
        return TIKU_KEY_DOWN;
    case XK_Left:
        return TIKU_KEY_LEFT;
    case XK_Right:
        return TIKU_KEY_RIGHT;
    case XK_Home:
        return TIKU_KEY_HOME;
    case XK_End:
        return TIKU_KEY_END;
    case XK_Prior:
        return TIKU_KEY_PAGE_UP;
    case XK_Next:
        return TIKU_KEY_PAGE_DOWN;
    case XK_BackSpace:
        return TIKU_KEY_BACKSPACE;
    case XK_Delete:
        return TIKU_KEY_DELETE;
    case XK_Escape:
        return TIKU_KEY_ESCAPE;
    case XK_Return:
    case XK_KP_Enter:
        return TIKU_KEY_RETURN;
    case XK_Tab:
        return TIKU_KEY_TAB;
    case XK_Menu:
        return TIKU_KEY_MENU;
    case XK_F2:
        return TIKU_KEY_F2;
    case XK_F1:
        return TIKU_KEY_F1;
    case XK_F5:
        return TIKU_KEY_F5;
    case XK_F8:
        return TIKU_KEY_F8;
    case XK_Shift_L:
    case XK_Shift_R:
        return TIKU_KEY_SHIFT;
    case XK_Control_L:
    case XK_Control_R:
        return TIKU_KEY_CMD;
    case XK_Alt_L:
    case XK_Alt_R:
    case XK_Meta_L:
    case XK_Meta_R:
        return TIKU_KEY_OPTION;
    default:
        if (keysym >= XK_A && keysym <= XK_Z) {
            return (unsigned)(keysym - XK_A + 'a');
        }
        return (keysym >= 0x20 && keysym < 0x7fu) ? (unsigned)keysym : 0u;
    }
}

tiku_host_t *
tiku_host_open(int width, int height, const char *title)
{
    tiku_host_t *host;
    int screen;

    if (width <= 0 || height <= 0) {
        return NULL;
    }
    /* Only character classification changes; numeric VFS formatting stays C. */
    (void)setlocale(LC_CTYPE, "");
    host = calloc(1u, sizeof *host);
    if (host == NULL) {
        return NULL;
    }
    host->display = XOpenDisplay(NULL);
    if (host->display == NULL) {
        free(host);
        return NULL;
    }
    screen = DefaultScreen(host->display);
    host->scale = display_scale(host->display);
    host->command_mask = command_modifiers(host->display);
    if (width > INT_MAX / host->scale || height > INT_MAX / host->scale) {
        tiku_host_close(host);
        return NULL;
    }
    host->window = XCreateSimpleWindow(
        host->display, RootWindow(host->display, screen), 0, 0,
        (unsigned)(width * host->scale), (unsigned)(height * host->scale), 0,
        BlackPixel(host->display, screen), BlackPixel(host->display, screen));
    XSelectInput(host->display, host->window,
                 ExposureMask | KeyPressMask | KeyReleaseMask |
                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                     StructureNotifyMask | FocusChangeMask |
                     PropertyChangeMask);
    tiku_x11_clipboard_init(&host->clipboard, host->display, host->window);
    host->clipboard_ready = 1;
    input_open(host);
    host->delete_window = XInternAtom(host->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(host->display, host->window, &host->delete_window, 1);
    XStoreName(host->display, host->window, (title != NULL) ? title : "TikuOS");
    host->gc = XCreateGC(host->display, host->window, 0, NULL);
    XSetGraphicsExposures(host->display, host->gc, False);
    if (resize_image(host, width * host->scale, height * host->scale) != 0) {
        tiku_host_close(host);
        return NULL;
    }
    host->resize_cursor =
        XCreateFontCursor(host->display, XC_sb_h_double_arrow);
    return host;
}

void
tiku_host_close(tiku_host_t *host)
{
    if (host == NULL) {
        return;
    }
    tiku_print_native_close(host->printing);
    tiku_access_native_close(host->access);
    if (host->image != NULL) {
        XDestroyImage(host->image);
    }
    if (host->input_context)
        XDestroyIC(host->input_context);
    host->input_context = NULL;
    if (host->input_method)
        XCloseIM(host->input_method);
    host->input_method = NULL;
    if (host->display != NULL && host->resize_cursor != None) {
        XFreeCursor(host->display, host->resize_cursor);
    }
    if (host->display != NULL) {
        int k;

        for (k = 0; k < 6; k++) {
            if (host->cursors[k] != None) {
                XFreeCursor(host->display, host->cursors[k]);
            }
        }
    }
    if (host->display != NULL && host->gc != NULL) {
        XFreeGC(host->display, host->gc);
    }
    if (host->display != NULL && host->backing != None) {
        XFreePixmap(host->display, host->backing);
    }
    if (host->display != NULL && host->window != None) {
        if (host->clipboard_ready)
            tiku_x11_clipboard_close(&host->clipboard);
        XDestroyWindow(host->display, host->window);
    }
    if (host->display != NULL) {
        XCloseDisplay(host->display);
    }
    free(host);
}

int
tiku_host_present(tiku_host_t *host, const tiku_surface_t *surface)
{
    return tiku_host_present_in(host, surface, NULL);
}

int
tiku_host_present_in(tiku_host_t *host, const tiku_surface_t *surface,
                     const tiku_rect_t *bounds)
{
    if (host == NULL || surface == NULL || host->image == NULL ||
        surface->w != host->logical_width ||
        surface->h != host->logical_height) {
        return -1;
    }
    tiku_rect_t rects[TIKU_PRESENT_RECTS];
    int n = tiku_present_update_in(
        (tiku_rgb_t *)host->image->data, host->native_width,
        host->native_height,
        host->image->bytes_per_line / (int)sizeof(tiku_rgb_t), surface,
        host->force_present, bounds, rects, TIKU_PRESENT_RECTS);
    if (n < 0)
        return -1;
    host->force_present = 0;
    host->presented_frames++;
    for (int i = 0; i < n; i++) {
        tiku_rect_t r = rects[i];
        XPutImage(host->display, host->backing, host->gc, host->image, r.x, r.y,
                  r.x, r.y, (unsigned)r.w, (unsigned)r.h);
        host->presented_bytes += (uint64_t)r.w * r.h * sizeof(tiku_rgb_t);
        host->presented_rects++;
    }
    if (n) {
        /* The backing pixmap is the window's background, so the server
         * repaints an exposed area from it while expose handling waits,
         * as it does during boot. */
        XSetWindowBackgroundPixmap(host->display, host->window, host->backing);
        if (!host->mapped) {
            XMapWindow(host->display, host->window);
            host->mapped = 1;
        } else {
            for (int i = 0; i < n; i++) {
                tiku_rect_t r = rects[i];
                XCopyArea(host->display, host->backing, host->window, host->gc,
                          r.x, r.y, (unsigned)r.w, (unsigned)r.h, r.x, r.y);
            }
        }
        XFlush(host->display);
    }
    return 0;
}

void
tiku_host_present_stats(tiku_host_t *host, uint64_t *frames, uint64_t *bytes,
                        uint64_t *rectangles)
{
    if (frames)
        *frames = host ? host->presented_frames : 0;
    if (bytes)
        *bytes = host ? host->presented_bytes : 0;
    if (rectangles)
        *rectangles = host ? host->presented_rects : 0;
}

void
tiku_host_wait(tiku_host_t *host, int64_t timeout_us)
{
    if (timeout_us <= 0)
        return;
    if (timeout_us > 60000000)
        timeout_us = 60000000;
    struct pollfd fd = {.fd = -1, .events = POLLIN};
    if (host) {
        if (host->commit.offset < host->commit.length ||
            host->clipboard.result ||
            (host->has_deferred && !host->clipboard.requestor))
            return;
        /* While input waits behind a clipboard import, queued events do
         * not end the wait; traffic on the connection does. */
        if (!(host->has_deferred && host->clipboard.requestor) &&
            XPending(host->display))
            return;
        fd.fd = ConnectionNumber(host->display);
        XFlush(host->display);
    }
    (void)poll(&fd, 1, (int)((timeout_us + 999) / 1000));
}

int
tiku_host_scale(const tiku_host_t *host)
{
    return (host != NULL && host->scale > 1) ? host->scale : 1;
}

void
tiku_host_set_fullscreen(tiku_host_t *host, int on)
{
    Atom state, full;
    int screen;

    if (host == NULL || host->display == NULL) {
        return;
    }
    host->fullscreen = (on != 0);
    state = XInternAtom(host->display, "_NET_WM_STATE", False);
    full = XInternAtom(host->display, "_NET_WM_STATE_FULLSCREEN", False);
    if (!host->mapped) {
        /* Before the map: a manager reads the state off the window as it
         * takes it, so the first frame is already the whole screen. */
        if (on) {
            XChangeProperty(host->display, host->window, state, XA_ATOM, 32,
                            PropModeReplace, (unsigned char *)&full, 1);
        } else {
            XDeleteProperty(host->display, host->window, state);
        }
    } else {
        /* After it: the manager owns the window's state and is asked
         * through the root, as EWMH says (1 adds, 0 removes). */
        XEvent e;

        memset(&e, 0, sizeof e);
        e.xclient.type = ClientMessage;
        e.xclient.window = host->window;
        e.xclient.message_type = state;
        e.xclient.format = 32;
        e.xclient.data.l[0] = on ? 1 : 0;
        e.xclient.data.l[1] = (long)full;
        e.xclient.data.l[2] = 0;
        e.xclient.data.l[3] = 1;
        XSendEvent(host->display, DefaultRootWindow(host->display), False,
                   SubstructureRedirectMask | SubstructureNotifyMask, &e);
    }
    /* The window is also sized to the screen, for a server with no
     * window manager, such as a bare Xvfb; a manager's own placement
     * overrides it. */
    if (on) {
        screen = DefaultScreen(host->display);
        XMoveResizeWindow(host->display, host->window, 0, 0,
                          (unsigned)DisplayWidth(host->display, screen),
                          (unsigned)DisplayHeight(host->display, screen));
    }
    XFlush(host->display);
}

void
tiku_host_set_title(tiku_host_t *host, const char *title)
{
    if (host != NULL && title != NULL) {
        XStoreName(host->display, host->window, title);
    }
}

/** @brief Monotonic microseconds, the clock clipboard deadlines use. */
static int64_t
monotonic_us(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}

/**
 * @brief Fill @p event with a TIKU_EVENT_CLIPBOARD for a finished import,
 *        which is delivered before the input deferred behind it.
 *
 * @return 1 when there was a result to deliver.
 */
static int
clipboard_result(tiku_host_t *host, tiku_event_t *event)
{
    if (!host->clipboard.result)
        return 0;
    memset(event, 0, sizeof *event);
    event->type = TIKU_EVENT_CLIPBOARD;
    event->key = (unsigned)host->clipboard.result;
    host->clipboard.result = 0;
    return 1;
}

int
tiku_host_poll(tiku_host_t *host, tiku_event_t *event)
{
    XEvent native;

    if (host == NULL || event == NULL) {
        return 0;
    }
    tiku_print_native_poll(host->printing);
    tiku_access_native_poll(host->access);
    int64_t now = monotonic_us();
    if (tiku_ime_next(&host->ime, event))
        return 1;
    if (tiku_text_input_next_v2(&host->commit, event)) {
        event->repeat = host->commit_token;
        return 1;
    }
    tiku_x11_clipboard_tick(&host->clipboard, now);
    /* Only clipboard protocol events are taken here, at most 32 a poll;
     * clicks and keys stay queued behind an import for an earlier event. */
    for (int i = 0; i < 32 && XCheckIfEvent(host->display, &native,
                                            tiku_x11_clipboard_match,
                                            (XPointer)&host->clipboard);
         i++)
        (void)tiku_x11_clipboard_event(&host->clipboard, &native, now);
    if (clipboard_result(host, event))
        return 1;
    /* Input waits only for an import a deferred event waits on; the
     * refresh an ordinary click starts does not hold input back. */
    if (host->clipboard.requestor && host->has_deferred)
        return 0;
    if (host->has_deferred) {
        *event = host->deferred;
        host->has_deferred = 0;
        return 1;
    }
    if (!XPending(host->display))
        return 0;
    XNextEvent(host->display, &native);
    memset(event, 0, sizeof *event);
    if (native.type == KeyPress && (host->ime.active || host->marked_active) &&
        XLookupKeysym(&native.xkey, 0) == XK_Escape) {
        tiku_host_cancel_composition(host);
        return tiku_ime_next(&host->ime, event);
    }
    if (XFilterEvent(&native, None))
        return 1;
    switch (native.type) {
    case Expose:
        host->force_present = 1;
        event->type = TIKU_EVENT_EXPOSE;
        break;
    case KeyPress:
    case KeyRelease: {
        KeySym keysym = NoSymbol;
        char text[TIKU_TEXT_INPUT_V2_MAX] = {0};
        Status status = XLookupNone;
        int n;
        if (native.type == KeyPress && host->input_context) {
            n = Xutf8LookupString(host->input_context, &native.xkey, text,
                                  sizeof text - 1, &keysym, &status);
            if (status == XBufferOverflow) {
                tiku_host_cancel_composition(host);
                event->type = TIKU_EVENT_TEXT_INPUT;
                event->button = TIKU_TEXT_REJECT;
                return 1;
            }
            if (status != XLookupChars && status != XLookupBoth)
                n = 0;
        } else {
            n = XLookupString(&native.xkey, text, sizeof text - 1, &keysym,
                              NULL);
            if (keysym >= 0x01000000 && keysym <= 0x0110ffff)
                n = input_utf8((unsigned)(keysym & 0xffffff), text);
            else if (keysym >= 0xa0 && keysym <= 0xff)
                n = input_utf8((unsigned)keysym, text);
        }

        event->type =
            (native.type == KeyPress) ? TIKU_EVENT_KEY_DOWN : TIKU_EVENT_KEY_UP;
        event->modifiers = event_modifiers(host, native.xkey.state);
        event->key = event_key(keysym);
        event->time_us = (int64_t)native.xkey.time * 1000;
        if (n < 0 || n >= (int)sizeof text)
            n = 0;
        text[n] = 0;
        if (native.type == KeyPress && n &&
            !(event->modifiers & (TIKU_MOD_CMD | TIKU_MOD_OPTION)) &&
            (host->marked_active || host->ime.active || n > 1 ||
             (unsigned char)text[0] >= 128 || status == XLookupChars)) {
            if (tiku_text_input_set_v2(&host->commit, text, (size_t)n)) {
                host->commit_token = (host->marked_active || host->ime.active)
                                         ? host->ime.token
                                         : 0;
                tiku_ime_submitted(&host->ime);
                host->marked_active = 0;
                (void)tiku_text_input_next_v2(&host->commit, event);
                event->repeat = host->commit_token;
            } else {
                memset(event, 0, sizeof *event);
                event->type = TIKU_EVENT_TEXT_INPUT;
                event->button = TIKU_TEXT_REJECT;
            }
        } else if (native.type == KeyPress && n < (int)sizeof event->text)
            memcpy(event->text, text, (size_t)n + 1);
        if (native.type == KeyPress &&
            (event->modifiers & (TIKU_MOD_CMD | TIKU_MOD_OPTION)))
            tiku_host_cancel_composition(host);
        break;
    }
    case ButtonPress:
    case ButtonRelease:
        event->type = (native.type == ButtonPress) ? TIKU_EVENT_POINTER_DOWN
                                                   : TIKU_EVENT_POINTER_UP;
        event->x = logical_coord(native.xbutton.x, host->native_width,
                                 host->logical_width);
        event->y = logical_coord(native.xbutton.y, host->native_height,
                                 host->logical_height);
        event->button = native.xbutton.button;
        event->modifiers = event_modifiers(host, native.xbutton.state);
        event->time_us = (int64_t)native.xbutton.time * 1000;
        break;
    case MotionNotify:
        for (int moves = 0; moves < 64 && XPending(host->display); moves++) {
            XEvent next;
            XPeekEvent(host->display, &next);
            if (next.type != MotionNotify ||
                next.xmotion.window != native.xmotion.window ||
                next.xmotion.state != native.xmotion.state)
                break;
            /* Consecutive moves with the same buttons and modifiers held
             * are collapsed into the last; any other event ends the run. */
            XNextEvent(host->display, &native);
        }
        event->type = TIKU_EVENT_POINTER_MOVE;
        event->x = logical_coord(native.xmotion.x, host->native_width,
                                 host->logical_width);
        event->y = logical_coord(native.xmotion.y, host->native_height,
                                 host->logical_height);
        event->modifiers = event_modifiers(host, native.xmotion.state);
        event->time_us = (int64_t)native.xmotion.time * 1000;
        break;
    case FocusIn:
        input_open(host);
        if (host->input_context)
            XSetICFocus(host->input_context);
        event->type = TIKU_EVENT_ACTIVATED;
        break;
    case FocusOut:
        tiku_host_cancel_composition(host);
        if (host->input_context)
            XUnsetICFocus(host->input_context);
        event->type = TIKU_EVENT_DEACTIVATED;
        break;
    case ConfigureNotify:
        if (host->access) {
            int screen_x = 0, screen_y = 0;
            Window child;
            XTranslateCoordinates(host->display, host->window,
                                  DefaultRootWindow(host->display), 0, 0,
                                  &screen_x, &screen_y, &child);
            tiku_access_native_origin(host->access, screen_x, screen_y,
                                      host->scale);
        }
        event->type = TIKU_EVENT_RESIZE;
        if (native.xconfigure.width != host->native_width ||
            native.xconfigure.height != host->native_height) {
            (void)resize_image(host, native.xconfigure.width,
                               native.xconfigure.height);
        }
        event->width = host->logical_width;
        event->height = host->logical_height;
        break;
    case ClientMessage:
        if ((Atom)native.xclient.data.l[0] == host->delete_window) {
            event->type = TIKU_EVENT_CLOSE;
        }
        break;
    default:
        event->type = TIKU_EVENT_NONE;
        break;
    }
    int paste = event->type == TIKU_EVENT_KEY_DOWN && event->key == 'v' &&
                (event->modifiers & TIKU_MOD_CMD);
    if (event->type == TIKU_EVENT_POINTER_DOWN && event->button == 1)
        tiku_host_cancel_composition(host);
    if (event->type == TIKU_EVENT_ACTIVATED || paste ||
        (event->type == TIKU_EVENT_POINTER_DOWN && event->button == 1)) {
        int forced = paste || event->type == TIKU_EVENT_ACTIVATED;
        int waiting = tiku_x11_clipboard_request(&host->clipboard, now, forced);
        if ((waiting && forced) || host->clipboard.result) {
            host->deferred = *event;
            host->has_deferred = 1;
            return clipboard_result(host, event);
        }
    }
    return 1;
}

void
tiku_host_set_cursor(tiku_host_t *host, int kind)
{
    static const unsigned shapes[6] = {XC_left_ptr,
                                       XC_xterm,
                                       XC_sb_h_double_arrow,
                                       XC_sb_v_double_arrow,
                                       XC_bottom_right_corner,
                                       XC_hand2};

    if (host == NULL || kind < 0 || kind >= 6 || kind == host->cursor_kind) {
        return;
    }
    host->cursor_kind = kind;
    if (kind == TIKU_CURSOR_ARROW) {
        XUndefineCursor(host->display, host->window);
        XFlush(host->display);
        return;
    }
    if (host->cursors[kind] == None) {
        host->cursors[kind] = XCreateFontCursor(host->display, shapes[kind]);
    }
    XDefineCursor(host->display, host->window, host->cursors[kind]);
    XFlush(host->display);
}

void
tiku_host_set_resize_cursor(tiku_host_t *host, int enabled)
{
    tiku_host_set_cursor(host,
                         enabled ? TIKU_CURSOR_RESIZE_H : TIKU_CURSOR_ARROW);
}

int
tiku_host_pointer(tiku_host_t *host, int *x, int *y, unsigned *modifiers)
{
    Window root, child;
    int root_x, root_y, local_x, local_y;
    unsigned state;

    if (host == NULL ||
        !XQueryPointer(host->display, host->window, &root, &child, &root_x,
                       &root_y, &local_x, &local_y, &state)) {
        return 0;
    }
    if (x != NULL) {
        *x = logical_coord(local_x, host->native_width, host->logical_width);
    }
    if (y != NULL) {
        *y = logical_coord(local_y, host->native_height, host->logical_height);
    }
    if (modifiers != NULL) {
        *modifiers = event_modifiers(host, state);
    }
    return 1;
}

int
tiku_host_show(const tiku_surface_t *s, const char *title)
{
    Display *d;
    Window w;
    XImage *img;
    GC gc;
    Atom del;
    char *pixels;
    int screen, running = 1;

    d = XOpenDisplay(NULL);
    if (d == NULL) {
        return -1;
    }
    screen = DefaultScreen(d);
    w = XCreateSimpleWindow(d, RootWindow(d, screen), 0, 0, (unsigned)s->w,
                            (unsigned)s->h, 0, BlackPixel(d, screen),
                            WhitePixel(d, screen));
    XStoreName(d, w, title);
    XSelectInput(d, w,
                 ExposureMask | KeyPressMask | ButtonPressMask |
                     StructureNotifyMask);
    del = XInternAtom(d, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(d, w, &del, 1);
    XMapWindow(d, w);
    gc = XCreateGC(d, w, 0, NULL);

    /* XImage frees this on XDestroyImage, so it must be its own copy. */
    pixels = malloc((size_t)s->w * (size_t)s->h * 4u);
    if (pixels == NULL) {
        XCloseDisplay(d);
        return -1;
    }
    memcpy(pixels, s->px, (size_t)s->w * (size_t)s->h * 4u);
    img = XCreateImage(d, DefaultVisual(d, screen),
                       (unsigned)DefaultDepth(d, screen), ZPixmap, 0, pixels,
                       (unsigned)s->w, (unsigned)s->h, 32, 0);
    if (img == NULL) {
        free(pixels);
        XCloseDisplay(d);
        return -1;
    }
    while (running) {
        XEvent e;
        XNextEvent(d, &e);
        switch (e.type) {
        case Expose:
            XPutImage(d, w, gc, img, 0, 0, 0, 0, (unsigned)s->w,
                      (unsigned)s->h);
            break;
        case KeyPress:
        case ButtonPress:
            running = 0;
            break;
        case ClientMessage:
            if ((Atom)e.xclient.data.l[0] == del) {
                running = 0;
            }
            break;
        default:
            break;
        }
    }
    XDestroyImage(img);
    XFreeGC(d, gc);
    XDestroyWindow(d, w);
    XCloseDisplay(d);
    return 0;
}

int
tiku_host_access(tiku_host_t *host, uint32_t window, tiku_access_tree_t *tree,
                 int x, int y, int focused, tiku_access_action_fn action,
                 void *context)
{
    if (!host)
        return 0;
    if (!host->access && tree)
        host->access = tiku_access_native_open("KintsugiDesktop", NULL);
    if (!host->access)
        return 0;
    int screen_x = 0, screen_y = 0;
    Window child;
    XTranslateCoordinates(host->display, host->window,
                          DefaultRootWindow(host->display), 0, 0, &screen_x,
                          &screen_y, &child);
    tiku_access_native_origin(host->access, screen_x, screen_y, host->scale);
    return tiku_access_native_set(host->access, window, tree, x, y, focused,
                                  action, context);
}

int
tiku_host_print(tiku_host_t *host, uint64_t request, const void *pdf,
                size_t bytes, const char *title, tiku_print_done_fn done,
                void *context)
{
    if (!host)
        return 0;
    if (!host->printing) {
        char parent[64];
        snprintf(parent, sizeof parent, "x11:%lx", (unsigned long)host->window);
        host->printing = tiku_print_native_open(parent, NULL);
    }
    return tiku_print_native_begin(host->printing, request, pdf, bytes, title,
                                   done, context);
}
void
tiku_host_print_cancel(tiku_host_t *host, uint64_t request)
{
    if (host)
        tiku_print_native_cancel(host->printing, request);
}

void tiku_host_observe(const tiku_host_t *host,tiku_host_observation_t *out)
{
    if(!out)return;
    memset(out,0,sizeof *out);snprintf(out->backend,sizeof out->backend,"x11");if(!host)return;
    out->available=1;out->scale=host->scale;out->logical_width=host->logical_width;out->logical_height=host->logical_height;
    out->native_width=host->native_width;out->native_height=host->native_height;out->fullscreen=host->fullscreen;out->mapped=host->mapped;
    out->accessibility_endpoint=host->access!=NULL;out->print_endpoint=host->printing!=NULL;
}
size_t tiku_host_print_observe(tiku_host_t *host,tiku_print_job_observation_t *out,size_t capacity)
{
    return host?tiku_print_native_observe(host->printing,out,capacity):0;
}
