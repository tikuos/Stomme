/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_cocoa.m - the native window backend on a Mac.
 *
 * AppKit, used the way tiku_x11.c uses Xlib: the interface layer owns
 * every pixel, so the window is one view that shows a buffer, and the
 * shell's own loop pumps AppKit's events from inside tiku_host_wait()
 * and tiku_host_poll().  Nothing above this file knows which of the two
 * is underneath.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tiku_access_native.h"
#include "tiku_clipboard.h"
#include "tiku_gfx.h"
#include "tiku_host.h"
#include "tiku_ime.h"
#include "tiku_present.h"
#include "tiku_text_input.h"

#define QUEUE_MAX 256           /* events between two polls              */
#define REFRESH_US 250000       /* a click re-reads the pasteboard this  */
                                /* often at most, an activation or a     */
                                /* paste always                          */
#define NOTCH_PX 12.0           /* precise wheel travel that is one notch */

@class TikuView;
@class TikuHostDelegate;

struct tiku_host {
    tiku_print_native_t *printing; /**< owned native print sheet service */
    tiku_access_native_t *access;  /**< native semantic publications */
    NSWindow *window;
    TikuView *view;
    TikuHostDelegate *delegate;
    NSCursor *cursor;
    int cursor_kind;            /* TIKU_CURSOR_*, as last set            */
    tiku_rgb_t *pixels;         /* the native frame, last presented      */
    int scale;
    int logical_width, logical_height;
    int native_width, native_height;
    int mapped;                 /* first presentation reveals the window */
    int fullscreen;             /* the window is the screen, or wants it */
    int force_present;
    tiku_event_t queue[QUEUE_MAX];
    int head, tail;
    tiku_text_input_v2_t commit;
    tiku_ime_t ime;
    unsigned commit_token;
    tiku_rect_t text_rect;
    unsigned stamp;             /* the clipboard store's, as published   */
    NSInteger change_count;     /* the pasteboard's, as last imported    */
    int64_t refreshed;
    double wheel;               /* precise travel not yet a notch        */
    NSEventModifierFlags flags; /* the modifier keys as last seen        */
    uint64_t presented_frames, presented_bytes, presented_rects;
};

/** @brief The monotonic clock in microseconds: clipboard deadlines use
 *         elapsed time, not event timestamps. */
static int64_t
monotonic_us(void)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}

/** @brief Queue one event for the next poll; a full queue drops the oldest. */
static void
push(tiku_host_t *host, const tiku_event_t *event)
{
    int next = (host->tail + 1) % QUEUE_MAX;

    if (next == host->head) {
        host->head = (host->head + 1) % QUEUE_MAX;
    }
    host->queue[host->tail] = *event;
    host->tail = next;
}

/** @brief The event queued last, or NULL when there is none. */
static tiku_event_t *
last_queued(tiku_host_t *host)
{
    if (host->head == host->tail) {
        return NULL;
    }
    return &host->queue[(host->tail + QUEUE_MAX - 1) % QUEUE_MAX];
}

/** @brief Translate native modifiers once, at the host boundary. */
static unsigned
event_modifiers(NSEventModifierFlags flags)
{
    unsigned modifiers = 0u;

    if (flags & NSEventModifierFlagShift) { modifiers |= TIKU_MOD_SHIFT; }
    /* Command and Control both give TIKU_MOD_CMD, so a shortcut works
     * with either key. */
    if (flags & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) {
        modifiers |= TIKU_MOD_CMD;
    }
    if (flags & NSEventModifierFlagOption) { modifiers |= TIKU_MOD_OPTION; }
    if (flags & NSEventModifierFlagCapsLock) { modifiers |= TIKU_MOD_CAPS; }
    return modifiers;
}

/** @brief Translate navigation and modifier keys; printable ASCII stays
 *         a character, capitals folded to lower case, and anything else
 *         is 0. */
static unsigned
event_key(unsigned short code, NSString *plain)
{
    unichar c;

    switch (code) {
    case 126: return TIKU_KEY_UP;
    case 125: return TIKU_KEY_DOWN;
    case 123: return TIKU_KEY_LEFT;
    case 124: return TIKU_KEY_RIGHT;
    case 115: return TIKU_KEY_HOME;
    case 119: return TIKU_KEY_END;
    case 116: return TIKU_KEY_PAGE_UP;
    case 121: return TIKU_KEY_PAGE_DOWN;
    case 51:  return TIKU_KEY_BACKSPACE;
    case 117: return TIKU_KEY_DELETE;
    case 53:  return TIKU_KEY_ESCAPE;
    case 36:
    case 76:  return TIKU_KEY_RETURN;
    case 48:  return TIKU_KEY_TAB;
    case 110: return TIKU_KEY_MENU;
    case 122: return TIKU_KEY_F1;
    case 96: return TIKU_KEY_F5;
    case 100: return TIKU_KEY_F8;
    case 120: return TIKU_KEY_F2;
    case 56:
    case 60:  return TIKU_KEY_SHIFT;
    case 54:
    case 55:
    case 59:
    case 62:  return TIKU_KEY_CMD;
    case 58:
    case 61:  return TIKU_KEY_OPTION;
    default:
        break;
    }
    if (plain == nil || [plain length] == 0) {
        return 0u;
    }
    c = [plain characterAtIndex:0];
    if (c >= 'A' && c <= 'Z') {
        return (unsigned)(c - 'A' + 'a');
    }
    return (c >= 0x20 && c < 0x7f) ? (unsigned)c : 0u;
}

/** @brief Map one coordinate in the view onto the logical framebuffer:
 *         -1 before it, @p logical_extent past it. */
static int
logical_coord(double value, int logical_extent)
{
    if (value < 0.0) { return -1; }
    if (value >= (double)logical_extent) { return logical_extent; }
    return (int)value;
}

/** @brief Prepare one text/HTML/RTF item before replacing a named pasteboard. */
static int
clipboard_write(NSPasteboard *board)
{
    NSPasteboardItem *item;
    NSString *text = [NSString stringWithUTF8String:tiku_clipboard_text()];
    size_t bytes = 0;
    const void *html;
    int result = 0;

    if (!text)
        return 0;
    item = [[NSPasteboardItem alloc] init];
    if (!item || ![item setString:text forType:NSPasteboardTypeString])
        goto done;
    html = tiku_clipboard_find("text/html", &bytes);
    if (html) {
        NSData *data = [NSData dataWithBytes:html length:bytes];

        if (!data || ![item setData:data forType:NSPasteboardTypeHTML])
            goto done;
    }
    const void *rtf = tiku_clipboard_find("text/rtf", &bytes);
    if (!rtf) rtf = tiku_clipboard_find("application/rtf", &bytes);
    if (rtf) {
        NSData *data = [NSData dataWithBytes:rtf length:bytes];
        if (!data || ![item setData:data forType:NSPasteboardTypeRTF])
            goto done;
    }
    [board clearContents];
    result = [board writeObjects:@[ item ]];
done:
    [item release];
    return result;
}

/** @brief Publish a changed local store without reimporting the same item. */
static void
clipboard_publish(tiku_host_t *host)
{
    NSPasteboard *board;

    if (host->stamp == tiku_clipboard_stamp())
        return;
    host->stamp = tiku_clipboard_stamp();
    board = [NSPasteboard generalPasteboard];
    (void)clipboard_write(board);
    /* The desktop's own writing is not imported back. */
    host->change_count = [board changeCount];
}

/** @brief Import one unchanged pasteboard item set with optional HTML bytes. */
static int
clipboard_read(NSPasteboard *board, NSInteger count)
{
    NSPasteboardItem *item = [[board pasteboardItems] firstObject];
    NSString *text = [item stringForType:NSPasteboardTypeString];
    NSData *plain = text ? [text dataUsingEncoding:NSUTF8StringEncoding
                               allowLossyConversion:NO]
                         : nil;
    NSData *html = [item dataForType:NSPasteboardTypeHTML];
    NSData *rtf = html ? nil : [item dataForType:NSPasteboardTypeRTF];
    NSData *rich = html ? html : rtf;
    size_t length = plain ? [plain length] : 0;
    size_t bytes = rich ? [rich length] : 0;

    if ([board changeCount] != count)
        return TIKU_CLIPBOARD_UNAVAILABLE;
    if (text && !plain)
        return TIKU_CLIPBOARD_INVALID;
    if (length > TIKU_CLIPBOARD_LIMIT || bytes > TIKU_CLIPBOARD_LIMIT - length)
        return TIKU_CLIPBOARD_TOO_LARGE;
    if (rich && !bytes)
        return TIKU_CLIPBOARD_INVALID;
    return tiku_clipboard_import_typed(plain ? [plain bytes] : "", length,
                                       html ? "text/html"
                                            : rtf ? "text/rtf" : NULL,
                                       rich ? [rich bytes] : NULL, bytes)
               ? TIKU_CLIPBOARD_OK
               : TIKU_CLIPBOARD_INVALID;
}

/**
 * @brief Bring what the pasteboard holds into the store, when it changed.
 *
 * Forced for an activation and a paste, since either may follow a copy
 * in another application; a click looks at most every REFRESH_US.  An
 * unforced failure changes nothing and queues nothing; a forced one
 * empties the store and queues the failure.
 *
 * @return 1 when a TIKU_EVENT_CLIPBOARD was queued.
 */
static int
clipboard_refresh_from(tiku_host_t *host, NSPasteboard *board, int force)
{
    NSInteger count = [board changeCount];
    int64_t now = monotonic_us();
    tiku_event_t event;
    int result;

    if (count == host->change_count) {
        return 0;
    }
    if (!force && host->refreshed && now - host->refreshed < REFRESH_US) {
        return 0;
    }
    host->refreshed = now;
    result = clipboard_read(board, count);
    if (result != TIKU_CLIPBOARD_OK && !force) {
        return 0;
    }
    host->change_count = count;
    if (result != TIKU_CLIPBOARD_OK) {
        tiku_clipboard_set("");
    }
    host->stamp = tiku_clipboard_stamp();  /* not published back */
    memset(&event, 0, sizeof event);
    event.type = TIKU_EVENT_CLIPBOARD;
    event.key = (unsigned)result;
    push(host, &event);
    return 1;
}

/** @brief Refresh native contents through the host's general pasteboard. */
static int
clipboard_refresh(tiku_host_t *host, int force)
{
    return clipboard_refresh_from(host, [NSPasteboard generalPasteboard],
                                  force);
}

@interface TikuView : NSView <NSTextInputClient> {
  @public
    tiku_host_t *host;
    NSTrackingArea *tracking;
    NSString *inserted;         /* what a key turned into, this key       */
    NSString *markedString;
    NSRange markedSelection;
    int interpreting;
    int marked;                 /* a composition is under way             */
}
@end

@implementation TikuView

- (BOOL)isFlipped { return YES; }
- (BOOL)isAccessibilityElement
{
    return NO;
}
- (NSArray *)accessibilityChildren
{
    return host ? (NSArray *)tiku_access_native_children(host->access) : @[];
}
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)wantsUpdateLayer { return NO; }

- (void)dealloc
{
    [inserted release];
    [markedString release];
    [tracking release];
    [super dealloc];
}

/** @brief Every pointer move, over the whole view, whether or not in front. */
- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    if (tracking != nil) {
        [self removeTrackingArea:tracking];
        [tracking release];
    }
    tracking = [[NSTrackingArea alloc]
        initWithRect:[self bounds]
             options:(NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
                      NSTrackingActiveAlways | NSTrackingInVisibleRect)
               owner:self
            userInfo:nil];
    [self addTrackingArea:tracking];
}

- (void)resetCursorRects
{
    if (host != NULL && host->cursor != nil) {
        [self addCursorRect:[self bounds] cursor:host->cursor];
    }
}

/**
 * @brief Blit the damaged tiles out of the native buffer.
 *
 * Only the rectangles present() marked are asked for, and only those
 * are copied: a sub-image over the buffer, drawn at its place.  The
 * view is flipped so everything above it can keep its origin at the
 * top left, and a flipped context draws an image upside down, so the
 * flip is undone around each draw.
 */
- (void)drawRect:(NSRect)dirty
{
    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    CGColorSpaceRef space;
    CGDataProviderRef provider;
    CGImageRef frame;
    const NSRect *rects = NULL;
    NSInteger count = 0, i;
    int sc;

    (void)dirty;
    if (host == NULL || host->pixels == NULL) {
        return;
    }
    sc = host->scale;
    space = CGColorSpaceCreateDeviceRGB();
    provider = CGDataProviderCreateWithData(NULL, host->pixels,
        (size_t)host->native_width * (size_t)host->native_height * 4u, NULL);
    /* 0x00RRGGBB in a little-endian word: B, G, R, pad in memory. */
    frame = CGImageCreate((size_t)host->native_width,
                          (size_t)host->native_height, 8, 32,
                          (size_t)host->native_width * 4u, space,
                          kCGBitmapByteOrder32Little |
                              kCGImageAlphaNoneSkipFirst,
                          provider, NULL, false, kCGRenderingIntentDefault);
    CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
    [self getRectsBeingDrawn:&rects count:&count];
    for (i = 0; i < count && frame != NULL; i++) {
        NSRect r = rects[i];
        CGRect native = CGRectMake(floor(r.origin.x) * sc,
                                   floor(r.origin.y) * sc,
                                   ceil(r.size.width) * sc,
                                   ceil(r.size.height) * sc);
        CGImageRef part = CGImageCreateWithImageInRect(frame, native);
        CGRect at = CGRectMake(native.origin.x / sc, native.origin.y / sc,
                               native.size.width / sc,
                               native.size.height / sc);

        if (part == NULL) {
            continue;
        }
        CGContextSaveGState(ctx);
        CGContextTranslateCTM(ctx, at.origin.x, at.origin.y + at.size.height);
        CGContextScaleCTM(ctx, 1.0, -1.0);
        CGContextDrawImage(ctx, CGRectMake(0, 0, at.size.width,
                                           at.size.height), part);
        CGContextRestoreGState(ctx);
        CGImageRelease(part);
    }
    if (frame != NULL) { CGImageRelease(frame); }
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(space);
}

/** @brief Where the pointer is, in the logical frame. */
- (void)locate:(NSEvent *)e into:(tiku_event_t *)out
{
    NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];

    out->x = logical_coord(p.x, host->logical_width);
    out->y = logical_coord(p.y, host->logical_height);
}

/** @brief The button in X's numbering, which the shells use: 1 left,
 *         2 middle, 3 right. */
- (unsigned)buttonOf:(NSEvent *)e
{
    switch ([e buttonNumber]) {
    case 0:  return 1u;
    case 1:  return 3u;
    case 2:  return 2u;
    default: return (unsigned)[e buttonNumber] + 1u;
    }
}

- (void)pointerDown:(NSEvent *)e
{
    tiku_event_t ev;

    memset(&ev, 0, sizeof ev);
    ev.type = TIKU_EVENT_POINTER_DOWN;
    ev.button = [self buttonOf:e];
    ev.modifiers = event_modifiers([e modifierFlags]);
    ev.time_us = (int64_t)([e timestamp] * 1000000.0);
    [self locate:e into:&ev];
    if (ev.button == 1u) {
        tiku_host_cancel_composition(host);
        (void)clipboard_refresh(host, 0);
    }
    push(host, &ev);
}

- (void)pointerUp:(NSEvent *)e
{
    tiku_event_t ev;

    memset(&ev, 0, sizeof ev);
    ev.type = TIKU_EVENT_POINTER_UP;
    ev.button = [self buttonOf:e];
    ev.modifiers = event_modifiers([e modifierFlags]);
    ev.time_us = (int64_t)([e timestamp] * 1000000.0);
    [self locate:e into:&ev];
    push(host, &ev);
}

/** @brief Consecutive moves with the same modifiers collapse into the
 *         latest; never across a press. */
- (void)pointerMove:(NSEvent *)e
{
    tiku_event_t ev, *tail;

    memset(&ev, 0, sizeof ev);
    ev.type = TIKU_EVENT_POINTER_MOVE;
    ev.modifiers = event_modifiers([e modifierFlags]);
    ev.time_us = (int64_t)([e timestamp] * 1000000.0);
    [self locate:e into:&ev];
    tail = last_queued(host);
    if (tail != NULL && tail->type == TIKU_EVENT_POINTER_MOVE &&
        tail->modifiers == ev.modifiers) {
        *tail = ev;
        return;
    }
    push(host, &ev);
}

- (void)mouseDown:(NSEvent *)e       { [self pointerDown:e]; }
- (void)rightMouseDown:(NSEvent *)e  { [self pointerDown:e]; }
- (void)otherMouseDown:(NSEvent *)e  { [self pointerDown:e]; }
- (void)mouseUp:(NSEvent *)e         { [self pointerUp:e]; }
- (void)rightMouseUp:(NSEvent *)e    { [self pointerUp:e]; }
- (void)otherMouseUp:(NSEvent *)e    { [self pointerUp:e]; }
- (void)mouseMoved:(NSEvent *)e      { [self pointerMove:e]; }
- (void)mouseDragged:(NSEvent *)e    { [self pointerMove:e]; }
- (void)rightMouseDragged:(NSEvent *)e { [self pointerMove:e]; }
- (void)otherMouseDragged:(NSEvent *)e { [self pointerMove:e]; }

/**
 * @brief The wheel as X reports it: a press and release of button 4 or 5.
 *
 * A mouse wheel clicks in notches; a trackpad slides in pixels, and
 * those are gathered until they amount to one.
 */
- (void)scrollWheel:(NSEvent *)e
{
    double travel = [e scrollingDeltaY];
    int notches = 0;

    if ([e hasPreciseScrollingDeltas]) {
        host->wheel += travel;
        notches = (int)(host->wheel / NOTCH_PX);
        host->wheel -= notches * NOTCH_PX;
    } else {
        notches = travel > 0.0 ? (int)ceil(travel) : (int)floor(travel);
    }
    while (notches != 0) {
        tiku_event_t ev;

        memset(&ev, 0, sizeof ev);
        ev.button = notches > 0 ? 4u : 5u;
        ev.modifiers = event_modifiers([e modifierFlags]);
        ev.time_us = (int64_t)([e timestamp] * 1000000.0);
        [self locate:e into:&ev];
        ev.type = TIKU_EVENT_POINTER_DOWN;
        push(host, &ev);
        ev.type = TIKU_EVENT_POINTER_UP;
        push(host, &ev);
        notches += notches > 0 ? -1 : 1;
    }
}

/**
 * @brief A key down: a command, a navigation key, or text.
 *
 * With Command or Option held the key is an instruction and its letter
 * rides along in text.  Otherwise AppKit is
 * asked what the key means on this keyboard: a dead key starts a
 * composition and says nothing yet; a character lands in text when it
 * is ASCII and as a text commit when it is not, so an accent or an
 * ideograph reaches a field whole.
 */
- (void)keyDown:(NSEvent *)e
{
    tiku_event_t ev;
    NSString *plain = [e charactersIgnoringModifiers];

    memset(&ev, 0, sizeof ev);
    ev.type = TIKU_EVENT_KEY_DOWN;
    ev.modifiers = event_modifiers([e modifierFlags]);
    ev.key = event_key([e keyCode], plain);
    ev.repeat = [e isARepeat] ? 1u : 0u;
    ev.time_us = (int64_t)([e timestamp] * 1000000.0);
    if (ev.modifiers & (TIKU_MOD_CMD | TIKU_MOD_OPTION)) {
        tiku_host_cancel_composition(host);
        if ([plain length] == 1 && [plain characterAtIndex:0] < 0x80) {
            ev.text[0] = (char)[plain characterAtIndex:0];
        }
        if (ev.key == 'v' && (ev.modifiers & TIKU_MOD_CMD)) {
            (void)clipboard_refresh(host, 1);
        }
        push(host, &ev);
        return;
    }
    if (!marked && ev.key != 0u && ev.key < 0x20u) {
        push(host, &ev);            /* navigation, no text to ask about */
        return;
    }
    [inserted release];
    inserted = nil;
    BOOL composing = marked;
    interpreting = 1;
    [self interpretKeyEvents:[NSArray arrayWithObject:e]];
    interpreting = 0;
    if (inserted != nil) {
        const char *utf8 = [inserted UTF8String];
        size_t n = [inserted lengthOfBytesUsingEncoding:NSUTF8StringEncoding];

        if (utf8 && !composing && !host->ime.active && n == 1 &&
            (unsigned char)utf8[0] < 0x80) {
            ev.text[0] = utf8[0];
            push(host, &ev);
        } else if (n > 0 && tiku_text_input_set_v2(&host->commit, utf8, n)) {
            host->commit_token =
                (composing || host->ime.active) ? host->ime.token : 0;
            tiku_ime_committed(&host->ime);
        } else if (n > 0) {
            ev.type = TIKU_EVENT_TEXT_INPUT;
            ev.button = TIKU_TEXT_REJECT;
            push(host, &ev);
        }
        [inserted release];
        inserted = nil;
        return;
    }
    if (marked) {
        return;                     /* a composition sends its text when done */
    }
    push(host, &ev);
}

- (void)keyUp:(NSEvent *)e
{
    tiku_event_t ev;

    memset(&ev, 0, sizeof ev);
    ev.type = TIKU_EVENT_KEY_UP;
    ev.modifiers = event_modifiers([e modifierFlags]);
    ev.key = event_key([e keyCode], [e charactersIgnoringModifiers]);
    ev.time_us = (int64_t)([e timestamp] * 1000000.0);
    push(host, &ev);
}

/** @brief Modifier keys come as flag changes; each becomes a key event. */
- (void)flagsChanged:(NSEvent *)e
{
    NSEventModifierFlags now = [e modifierFlags], was = host->flags;
    NSEventModifierFlags pressed = (now & ~was), released = (was & ~now);
    NSEventModifierFlags bits = NSEventModifierFlagShift |
                                NSEventModifierFlagCommand |
                                NSEventModifierFlagControl |
                                NSEventModifierFlagOption;
    tiku_event_t ev;

    host->flags = now;
    if (((pressed | released) & bits) == 0) {
        return;
    }
    memset(&ev, 0, sizeof ev);
    ev.type = (pressed & bits) ? TIKU_EVENT_KEY_DOWN : TIKU_EVENT_KEY_UP;
    ev.modifiers = event_modifiers(now);
    ev.key = event_key([e keyCode], nil);
    ev.time_us = (int64_t)([e timestamp] * 1000000.0);
    if (ev.key == 0u) {
        return;
    }
    push(host, &ev);
}

/* NSTextInputClient: what a key means, answered by the keyboard layout
 * and the input method, is delivered through these. */

- (void)insertText:(id)string replacementRange:(NSRange)range
{
    NSString *text = [string isKindOfClass:[NSAttributedString class]]
                         ? [string string] : string;

    if (range.location != NSNotFound &&
        (!markedString || range.location > [markedString length] ||
         range.length > [markedString length] - range.location)) {
        tiku_ime_cancel(&host->ime);
        return;
    }
    [inserted release];
    inserted = [text copy];
    marked = 0;
    if (!interpreting) {
        const char *utf8 = [text UTF8String];
        unsigned token = host->ime.active      ? host->ime.token
                         : host->commit.active ? host->commit_token
                                               : 0;
        if (utf8 &&
            tiku_text_input_set_v2(
                &host->commit, utf8,
                [text lengthOfBytesUsingEncoding:NSUTF8StringEncoding])) {
            host->commit_token = token;
            tiku_ime_committed(&host->ime);
        } else {
            tiku_ime_cancel(&host->ime);
            tiku_event_t event = {.type = TIKU_EVENT_TEXT_INPUT,
                                  .button = TIKU_TEXT_REJECT};
            push(host, &event);
        }
    }
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selected
     replacementRange:(NSRange)replaced
{
    NSString *text = [string isKindOfClass:[NSAttributedString class]]
                         ? [string string]
                         : string;
    if (selected.location > [text length] ||
        selected.length > [text length] - selected.location ||
        (replaced.location != NSNotFound &&
         (!markedString || replaced.location > [markedString length] ||
          replaced.length > [markedString length] - replaced.location))) {
        tiku_ime_cancel(&host->ime);
        return;
    }
    if (!marked && !tiku_ime_begin(&host->ime))
        return;
    [markedString release];
    markedString = [text copy];
    markedSelection = selected;
    marked = [text length] != 0;
    const char *utf8 = [text UTF8String];
    NSString *prefix = [text substringToIndex:selected.location];
    const char *caret = [prefix UTF8String];
    if (!utf8 || !caret) {
        tiku_ime_cancel(&host->ime);
        return;
    }
    size_t scalar = 0;
    for (size_t i = 0; caret[i]; i++)
        scalar += ((unsigned char)caret[i] & 0xc0) != 0x80;
    (void)tiku_ime_set(&host->ime, utf8,
                       [text lengthOfBytesUsingEncoding:NSUTF8StringEncoding],
                       scalar);
}

- (void)unmarkText
{
    if (marked && markedString) {
        const char *text = [markedString UTF8String];
        size_t bytes =
            [markedString lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
        if (text && tiku_text_input_set_v2(&host->commit, text, bytes)) {
            host->commit_token = host->ime.token;
            tiku_ime_committed(&host->ime);
        } else
            tiku_ime_cancel(&host->ime);
    }
    marked = 0;
    [markedString release];
    markedString = nil;
}
- (BOOL)hasMarkedText { return marked != 0; }
- (NSRange)markedRange
{
    return marked ? NSMakeRange(0, [markedString length])
                  : NSMakeRange(NSNotFound, 0);
}
- (NSRange)selectedRange
{
    return marked ? markedSelection : NSMakeRange(NSNotFound, 0);
}

- (NSAttributedString *)
    attributedSubstringForProposedRange:(NSRange)range
                            actualRange:(NSRangePointer)actual
{
    (void)range; (void)actual;
    return nil;
}

- (NSArray<NSAttributedStringKey> *)validAttributesForMarkedText
{
    return [NSArray array];
}

- (NSRect)firstRectForCharacterRange:(NSRange)range
                         actualRange:(NSRangePointer)actual
{
    (void)range;
    if (actual)
        *actual = [self selectedRange];
    tiku_rect_t caret = host->text_rect;
    NSRect r = NSMakeRect(caret.x, caret.y, caret.w > 0 ? caret.w : 1,
                          caret.h > 0 ? caret.h : 1);
    r = [self convertRect:r toView:nil];
    r = [[self window] convertRectToScreen:r];
    return r;
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point
{
    (void)point;
    return NSNotFound;
}

- (void)doCommandBySelector:(SEL)selector
{
    (void)selector;             /* arrows and the like are keys already */
}

@end

static int resize_frame(tiku_host_t *host, int width, int height);

@interface TikuHostDelegate : NSObject <NSWindowDelegate,
                                        NSApplicationDelegate> {
@public
    tiku_host_t *host;
}
@end

@implementation TikuHostDelegate

/** @brief Closing is the shell's decision: it is told, and answers. */
- (BOOL)windowShouldClose:(NSWindow *)sender
{
    tiku_event_t ev;

    (void)sender;
    memset(&ev, 0, sizeof ev);
    ev.type = TIKU_EVENT_CLOSE;
    push(host, &ev);
    return NO;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)app
{
    tiku_event_t ev;

    (void)app;
    memset(&ev, 0, sizeof ev);
    ev.type = TIKU_EVENT_CLOSE;
    push(host, &ev);
    return NSTerminateCancel;
}

- (void)askToClose:(id)sender
{
    (void)[self windowShouldClose:nil];
    (void)sender;
}

- (void)windowDidResize:(NSNotification *)note
{
    NSRect content = [host->window contentRectForFrameRect:
                                       [host->window frame]];
    int width = (int)content.size.width, height = (int)content.size.height;
    tiku_event_t ev;

    (void)note;
    if (width <= 0 || height <= 0) {
        return;
    }
    if (width != host->logical_width || height != host->logical_height) {
        (void)resize_frame(host, width * host->scale,
                           height * host->scale);
    }
    memset(&ev, 0, sizeof ev);
    ev.type = TIKU_EVENT_RESIZE;
    ev.width = host->logical_width;
    ev.height = host->logical_height;
    push(host, &ev);
}

- (void)windowDidBecomeKey:(NSNotification *)note
{
    tiku_event_t ev;

    (void)note;
    (void)clipboard_refresh(host, 1);
    memset(&ev, 0, sizeof ev);
    ev.type = TIKU_EVENT_ACTIVATED;
    push(host, &ev);
}

- (void)windowDidResignKey:(NSNotification *)note
{
    tiku_event_t ev;

    (void)note;
    tiku_host_cancel_composition(host);
    memset(&ev, 0, sizeof ev);
    ev.type = TIKU_EVENT_DEACTIVATED;
    push(host, &ev);
}

- (void)windowDidEnterFullScreen:(NSNotification *)note
{
    (void)note;
    host->fullscreen = 1;
}

- (void)windowDidExitFullScreen:(NSNotification *)note
{
    (void)note;
    host->fullscreen = 0;
}

@end

/**
 * @brief Replace the native frame with a blank one of @p width by
 *        @p height device pixels, the logical size following it.
 * @return 0, or -1 when it is too large or memory runs out, the old frame
 *         kept.
 */
static int
resize_frame(tiku_host_t *host, int width, int height)
{
    tiku_rgb_t *pixels;

    if (host == NULL || width <= 0 || height <= 0 ||
        (size_t)width > SIZE_MAX / 4u / (size_t)height) {
        return -1;
    }
    pixels = calloc((size_t)width * (size_t)height, sizeof *pixels);
    if (pixels == NULL) {
        return -1;
    }
    free(host->pixels);
    host->pixels = pixels;
    host->native_width = width;
    host->native_height = height;
    host->logical_width = (width + host->scale - 1) / host->scale;
    host->logical_height = (height + host->scale - 1) / host->scale;
    host->force_present = 1;
    return 0;
}

/**
 * @brief The application, made once: a process that was started from a
 *        terminal has none, and AppKit wants one before any window.
 */
static void
application_prepare(TikuHostDelegate *delegate)
{
    static int prepared;
    NSMenu *bar, *menu;
    NSMenuItem *item;

    if (prepared) {
        return;
    }
    prepared = 1;
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp setDelegate:delegate];
    /* One menu with one item, Quit, which queues TIKU_EVENT_CLOSE for
     * the shell as the window's close box does. */
    bar = [[NSMenu alloc] initWithTitle:@""];
    item = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    menu = [[NSMenu alloc] initWithTitle:@"KintsugiDesktop"];
    [menu addItemWithTitle:@"Quit KintsugiDesktop" action:@selector(askToClose:)
             keyEquivalent:@"q"];
    [item setSubmenu:menu];
    [bar addItem:item];
    [NSApp setMainMenu:bar];
    [menu release];
    [item release];
    [bar release];
    [NSApp finishLaunching];
}

/** @brief How many device pixels make one drawn pixel, whole, 1 to 4:
 *         TIKU_SCALE when it is set, else the screen's backing scale. */
static int
display_scale(NSScreen *screen)
{
    const char *forced = getenv("TIKU_SCALE");
    int scale;

    if (forced != NULL && forced[0] != '\0') {
        scale = atoi(forced);
    } else {
        scale = (int)([screen backingScaleFactor] + 0.5);
    }
    if (scale < 1) { scale = 1; }
    if (scale > 4) { scale = 4; }
    return scale;
}

tiku_host_t *
tiku_host_open(int width, int height, const char *title)
{
    tiku_host_t *host;
    NSScreen *screen;
    NSRect content;

    if (width <= 0 || height <= 0) {
        return NULL;
    }
    @autoreleasepool {
        /* No screen is no window server session: a shell over ssh, a
         * login that has none.  That is "no display" here. */
        screen = [NSScreen mainScreen];
        if (screen == nil) {
            return NULL;
        }
        host = calloc(1u, sizeof *host);
        if (host == NULL) {
            return NULL;
        }
        host->delegate = [[TikuHostDelegate alloc] init];
        host->delegate->host = host;
        application_prepare(host->delegate);
        host->scale = display_scale(screen);
        if (width > INT_MAX / host->scale ||
            height > INT_MAX / host->scale) {
            tiku_host_close(host);
            return NULL;
        }
        content = NSMakeRect(0, 0, width, height);
        host->window = [[NSWindow alloc]
            initWithContentRect:content
                      styleMask:(NSWindowStyleMaskTitled |
                                 NSWindowStyleMaskClosable |
                                 NSWindowStyleMaskMiniaturizable |
                                 NSWindowStyleMaskResizable)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [host->window setReleasedWhenClosed:NO];
        [host->window setTitle:[NSString stringWithUTF8String:
                                  (title != NULL) ? title : "TikuOS"]];
        [host->window setDelegate:host->delegate];
        [host->window setAcceptsMouseMovedEvents:YES];
        [host->window setCollectionBehavior:
            NSWindowCollectionBehaviorFullScreenPrimary];
        [host->window center];
        host->view = [[TikuView alloc] initWithFrame:content];
        host->view->host = host;
        [host->view setWantsLayer:YES];
        [host->view setLayerContentsRedrawPolicy:
            NSViewLayerContentsRedrawOnSetNeedsDisplay];
        [host->window setContentView:host->view];
        [host->window makeFirstResponder:host->view];
        host->cursor = [[NSCursor arrowCursor] retain];
        if (resize_frame(host, width * host->scale,
                         height * host->scale) != 0) {
            tiku_host_close(host);
            return NULL;
        }
        host->stamp = tiku_clipboard_stamp();
        host->change_count = [[NSPasteboard generalPasteboard] changeCount];
        host->flags = [NSEvent modifierFlags];
    }
    return host;
}

void tiku_host_close(tiku_host_t *host)
{
    if (host == NULL) {
        return;
    }
    @autoreleasepool {
        tiku_print_native_close(host->printing);
        host->printing = NULL;
        tiku_access_native_close(host->access);
        host->access = NULL;
        if (host->window != nil) {
            [host->window setDelegate:nil];
            [host->window orderOut:nil];
            [host->window setContentView:nil];
            [host->window release];
        }
        if (host->view != nil) {
            host->view->host = NULL;
            [host->view release];
        }
        if (host->delegate != nil) {
            if ([NSApp delegate] == host->delegate) {
                [NSApp setDelegate:nil];
            }
            [host->delegate release];
        }
        [host->cursor release];
    }
    free(host->pixels);
    free(host);
}

void tiku_host_text_rect(tiku_host_t *host, tiku_rect_t rect)
{
    if (host && rect.w >= 0 && rect.h >= 0)
        host->text_rect = rect;
}

void tiku_host_cancel_composition(tiku_host_t *host)
{
    if (host == NULL) {
        return;
    }
    if (host->view != nil && host->view->marked) {
        [[host->view inputContext] discardMarkedText];
        host->view->marked = 0;
        [host->view->markedString release];
        host->view->markedString = nil;
    }
    tiku_text_input_reset_v2(&host->commit);
    host->commit_token = 0;
    tiku_ime_cancel(&host->ime);
}

int tiku_host_scale(const tiku_host_t *host)
{
    return (host != NULL && host->scale > 1) ? host->scale : 1;
}

int tiku_host_present_in(tiku_host_t *host, const tiku_surface_t *surface,
                     const tiku_rect_t *bounds)
{
    /* The whole surface is compared: the bounds go unused here. */
    (void)bounds;
    return tiku_host_present(host, surface);
}

int tiku_host_present(tiku_host_t *host, const tiku_surface_t *surface)
{
    tiku_rect_t rects[TIKU_PRESENT_RECTS];
    int n, i, sc;

    if (host == NULL || surface == NULL || host->pixels == NULL ||
        surface->w != host->logical_width ||
        surface->h != host->logical_height) {
        return -1;
    }
    n = tiku_present_update(host->pixels, host->native_width,
                            host->native_height, host->native_width,
                            surface, host->force_present, rects,
                            TIKU_PRESENT_RECTS);
    if (n < 0) {
        return -1;
    }
    host->force_present = 0;
    host->presented_frames++;
    sc = host->scale;
    /* The damaged tiles are marked on the view and drawn at once, so the
     * frame is on its way to the screen before this returns.  The first
     * frame reveals the window, and a fullscreen asked for before it is
     * taken then: a window has to be visible to become the screen. */
    @autoreleasepool {
        for (i = 0; i < n; i++) {
            tiku_rect_t r = rects[i];
            NSRect at = NSMakeRect(r.x / sc, r.y / sc,
                                   (r.w + sc - 1) / sc, (r.h + sc - 1) / sc);

            [host->view setNeedsDisplayInRect:at];
            host->presented_bytes +=
                (uint64_t)r.w * r.h * sizeof(tiku_rgb_t);
            host->presented_rects++;
        }
        if (n > 0) {
            if (!host->mapped) {
                [host->window makeKeyAndOrderFront:nil];
                [NSApp activateIgnoringOtherApps:YES];
                host->mapped = 1;
                if (host->fullscreen) {
                    [host->window toggleFullScreen:nil];
                }
            }
            [host->view displayIfNeeded];
            [CATransaction flush];
        }
    }
    return 0;
}

void tiku_host_present_stats(tiku_host_t *host, uint64_t *frames,
                        uint64_t *bytes, uint64_t *rectangles)
{
    if (frames) { *frames = host ? host->presented_frames : 0; }
    if (bytes) { *bytes = host ? host->presented_bytes : 0; }
    if (rectangles) { *rectangles = host ? host->presented_rects : 0; }
}

void tiku_host_wait(tiku_host_t *host, int64_t timeout_us)
{
    if (timeout_us <= 0) {
        return;
    }
    if (timeout_us > 60000000) {
        timeout_us = 60000000;
    }
    if (host == NULL) {
        struct timespec nap = { (time_t)(timeout_us / 1000000),
                                (long)(timeout_us % 1000000) * 1000 };

        (void)nanosleep(&nap, NULL);
        return;
    }
    if (host->commit.offset < host->commit.length ||
        host->head != host->tail) {
        return;
    }
    /* AppKit's events are pumped until one of them queues something,
     * or the time is up. */
    @autoreleasepool {
        NSDate *until = [NSDate dateWithTimeIntervalSinceNow:
                                    (double)timeout_us / 1000000.0];

        while (host->head == host->tail) {
            NSEvent *e = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:until
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];

            if (e == nil) {
                break;
            }
            [NSApp sendEvent:e];
            if (host->commit.active || host->ime.pending.active ||
                host->ime.cancel)
                break;
        }
    }
}

int tiku_host_poll(tiku_host_t *host, tiku_event_t *event)
{
    if (host)
        tiku_print_native_poll(host->printing);
    int i;

    if (host == NULL || event == NULL) {
        return 0;
    }
    if (host->head != host->tail) {
        *event = host->queue[host->head];
        host->head = (host->head + 1) % QUEUE_MAX;
        return 1;
    }
    if (tiku_ime_next(&host->ime, event))
        return 1;
    if (tiku_text_input_next_v2(&host->commit, event)) {
        event->repeat = host->commit_token;
        return 1;
    }
    @autoreleasepool {
        clipboard_publish(host);
        /* What has arrived, without waiting for more; bounded, so a
         * stream of moves cannot hold the loop. */
        for (i = 0; i < 32; i++) {
            NSEvent *e = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:[NSDate distantPast]
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];

            if (e == nil) {
                break;
            }
            [NSApp sendEvent:e];
            if (host->commit.active || host->ime.pending.active ||
                host->ime.cancel)
                break;
        }
    }
    if (host->head == host->tail) {
        if (tiku_ime_next(&host->ime, event))
            return 1;
        if (!tiku_text_input_next_v2(&host->commit, event))
            return 0;
        event->repeat = host->commit_token;
        return 1;
    }
    *event = host->queue[host->head];
    host->head = (host->head + 1) % QUEUE_MAX;
    return 1;
}

void tiku_host_set_title(tiku_host_t *host, const char *title)
{
    if (host == NULL || title == NULL) {
        return;
    }
    @autoreleasepool {
        [host->window setTitle:[NSString stringWithUTF8String:title]];
    }
}

void tiku_host_set_fullscreen(tiku_host_t *host, int on)
{
    if (host == NULL) {
        return;
    }
    @autoreleasepool {
        if (!host->mapped) {
            host->fullscreen = (on != 0);   /* taken at the first frame */
        } else if ((on != 0) != host->fullscreen) {
            [host->window toggleFullScreen:nil];
        }
    }
}

void tiku_host_set_cursor(tiku_host_t *host, int kind)
{
    NSCursor *which;

    if (host == NULL || kind == host->cursor_kind) {
        return;
    }
    @autoreleasepool {
        switch (kind) {
        case TIKU_CURSOR_TEXT:        which = [NSCursor IBeamCursor]; break;
        case TIKU_CURSOR_RESIZE_H:    which = [NSCursor resizeLeftRightCursor]; break;
        case TIKU_CURSOR_RESIZE_V:    which = [NSCursor resizeUpDownCursor]; break;
        case TIKU_CURSOR_RESIZE_DIAG: which = [NSCursor crosshairCursor]; break;
        case TIKU_CURSOR_HAND:        which = [NSCursor pointingHandCursor]; break;
        default:                      which = [NSCursor arrowCursor]; kind = 0; break;
        }
        host->cursor_kind = kind;
        [host->cursor release];
        host->cursor = [which retain];
        [host->cursor set];
        [host->window invalidateCursorRectsForView:host->view];
    }
}

void tiku_host_set_resize_cursor(tiku_host_t *host, int enabled)
{
    tiku_host_set_cursor(host, enabled ? TIKU_CURSOR_RESIZE_H
                                       : TIKU_CURSOR_ARROW);
}

int tiku_host_pointer(tiku_host_t *host, int *x, int *y, unsigned *modifiers)
{
    if (host == NULL) {
        return 0;
    }
    @autoreleasepool {
        NSPoint p = [host->view convertPoint:
                        [host->window mouseLocationOutsideOfEventStream]
                                    fromView:nil];

        if (x != NULL) { *x = logical_coord(p.x, host->logical_width); }
        if (y != NULL) { *y = logical_coord(p.y, host->logical_height); }
        if (modifiers != NULL) {
            *modifiers = event_modifiers([NSEvent modifierFlags]);
        }
    }
    return 1;
}

int tiku_host_show(const tiku_surface_t *s, const char *title)
{
    tiku_host_t *host;
    tiku_event_t ev;
    int running = 1;

    if (s == NULL) {
        return -1;
    }
    host = tiku_host_open(s->w, s->h, title);
    if (host == NULL) {
        return -1;
    }
    (void)tiku_host_present(host, s);
    while (running) {
        tiku_host_wait(host, 100000);
        while (tiku_host_poll(host, &ev)) {
            if (ev.type == TIKU_EVENT_CLOSE ||
                ev.type == TIKU_EVENT_KEY_DOWN ||
                ev.type == TIKU_EVENT_POINTER_DOWN) {
                running = 0;
            } else if (ev.type == TIKU_EVENT_EXPOSE ||
                       ev.type == TIKU_EVENT_RESIZE) {
                (void)tiku_host_present(host, s);
            }
        }
    }
    tiku_host_close(host);
    return 0;
}

int
tiku_host_access(tiku_host_t *host, uint32_t window, tiku_access_tree_t *tree,
                 int x, int y, int focused, tiku_access_action_fn action,
                 void *context)
{
    if (!host)
        return 0;
    @autoreleasepool {
        if (!host->access && tree)
            host->access = tiku_access_native_open("KintsugiDesktop", host->view);
        return tiku_access_native_set(host->access, window, tree, x, y, focused,
                                      action, context);
    }
}

int
tiku_host_print(tiku_host_t *host, uint64_t request, const void *pdf,
                size_t bytes, const char *title, tiku_print_done_fn done,
                void *context)
{
    if (!host)
        return 0;
    @autoreleasepool {
        if (!host->printing)
            host->printing = tiku_print_native_open(NULL, host->window);
        return tiku_print_native_begin(host->printing, request, pdf, bytes,
                                       title, done, context);
    }
}
void
tiku_host_print_cancel(tiku_host_t *host, uint64_t request)
{
    if (host) {
        @autoreleasepool {
            tiku_print_native_cancel(host->printing, request);
        }
    }
}

void tiku_host_observe(const tiku_host_t *host,tiku_host_observation_t *out)
{
    if(!out)return;
    memset(out,0,sizeof *out);snprintf(out->backend,sizeof out->backend,"cocoa");if(!host)return;
    out->available=1;out->scale=host->scale;out->logical_width=host->logical_width;out->logical_height=host->logical_height;
    out->native_width=host->native_width;out->native_height=host->native_height;out->fullscreen=host->fullscreen;out->mapped=host->mapped;
    out->accessibility_endpoint=host->access!=NULL;out->print_endpoint=host->printing!=NULL;
}
size_t tiku_host_print_observe(tiku_host_t *host,tiku_print_job_observation_t *out,size_t capacity)
{
    return host?tiku_print_native_observe(host->printing,out,capacity):0;
}
