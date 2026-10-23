/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_x11_clipboard.c - the X11 CLIPBOARD selection, in and out, up to
 * TIKU_CLIPBOARD_LIMIT: a property read a piece at a time whatever its
 * size, INCR taken in as it comes, and text too large for one request
 * sent INCR too (ICCCM 2.7.2), a piece each time the requestor deletes
 * the last.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_x11_clipboard.h"
#include <X11/Xatom.h>
#include <stdlib.h>
#include <string.h>

#define READ_UNITS (64L * 1024L) /* 32-bit units one property read takes */
#define SINGLE_MAX 256000u       /* bytes one request carries anywhere */
#define PIECE (64u * 1024u)      /* bytes of each INCR piece sent */
#define WAIT_US 1000000          /* an import silent this long times out */
#define IMPORT_US 5000000        /* total time allowed across all targets */
#define SEND_US 5000000          /* a requestor quiet this long is let go */

/* A requestor may disappear between SelectionRequest and ChangeProperty,
 * so those calls run inside a trap that catches X errors on this display
 * and restores the previous handler after. */
static Display *trapped;
static int error_code;
static XErrorHandler previous_handler;
/** @brief Note an error on the trapped display; pass others on. */
static int
on_error(Display *d, XErrorEvent *e)
{
    if (d == trapped) {
        error_code = e->error_code;
        return 0;
    }
    return previous_handler ? previous_handler(d, e) : 0;
}
/** @brief Sync, then catch X errors on @p d until untrap(). */
static void
trap(Display *d)
{
    XSync(d, False);
    trapped = d;
    error_code = 0;
    previous_handler = XSetErrorHandler(on_error);
}
/** @brief Sync, restore the previous handler, and return the error code
 *         caught, 0 for none. */
static int
untrap(Display *d)
{
    XSync(d, False);
    XSetErrorHandler(previous_handler);
    trapped = NULL;
    return error_code;
}

/** @brief End an import: destroy its window and free its bytes. */
static void
cancel(tiku_x11_clipboard_t *c)
{
    if (c->requestor)
        XDestroyWindow(c->display, c->requestor);
    c->requestor = None;
    c->incremental = 0;
    c->used = 0;
    free(c->incoming);
    c->incoming = NULL;
    c->room = 0;
    free(c->plain);
    c->plain = NULL;
    c->plainlen = 0;
    c->rich_target = None;
}

/** @brief End the transfer out in pieces: stop watching its requestor
 *         and free its copy. */
static void
send_stop(tiku_x11_clipboard_t *c)
{
    if (c->send.requestor) {
        trap(c->display);
        XSelectInput(c->display, c->send.requestor, NoEventMask);
        (void)untrap(c->display);
    }
    free(c->send.data);
    memset(&c->send, 0, sizeof c->send);
}

/**
 * @brief End an import with @p result.
 *
 * A failed import that a paste waits on empties the local clipboard and
 * reports its result; a failed refresh nobody waits on keeps the local
 * text and reports nothing.
 */
static void
finish(tiku_x11_clipboard_t *c, int result, int64_t now)
{
    if (result != TIKU_CLIPBOARD_OK && !c->asked) {
        c->cached_owner = c->source;
        c->refreshed = now;
        c->asked = 0;
        cancel(c);
        XFlush(c->display);
        return;
    }
    if (result != TIKU_CLIPBOARD_OK)
        tiku_clipboard_set("");
    c->stamp = tiku_clipboard_stamp(); /* not a local change to publish */
    c->cached_owner = c->source;
    c->refreshed = now;
    c->result = result;
    c->asked = 0;
    cancel(c);
    XFlush(c->display);
}

void
tiku_x11_clipboard_init(tiku_x11_clipboard_t *c, Display *d, Window w)
{
    memset(c, 0, sizeof *c);
    c->display = d;
    c->window = w;
    c->clipboard = XInternAtom(d, "CLIPBOARD", False);
    c->utf8 = XInternAtom(d, "UTF8_STRING", False);
    c->targets = XInternAtom(d, "TARGETS", False);
    c->text = XInternAtom(d, "TEXT", False);
    c->html = XInternAtom(d, "text/html", False);
    c->rtf = XInternAtom(d, "text/rtf", False);
    c->rtf_application = XInternAtom(d, "application/rtf", False);
    c->timestamp = XInternAtom(d, "TIMESTAMP", False);
    c->incr = XInternAtom(d, "INCR", False);
    c->property = XInternAtom(d, "_TIKU_CLIPBOARD", False);
    c->clock = XInternAtom(d, "_TIKU_CLIPBOARD_TIME", False);
    c->stamp = tiku_clipboard_stamp();
}
void
tiku_x11_clipboard_close(tiku_x11_clipboard_t *c)
{
    cancel(c);
    send_stop(c);
    free(c->outgoing);
    c->outgoing = NULL;
    c->outlen = 0;
    free(c->out_html);
    c->out_html = NULL;
    c->out_html_len = 0;
    free(c->out_rtf);
    c->out_rtf = NULL;
    c->out_rtf_len = 0;
}

/** @brief Prepare a complete immutable offer before replacing previous bytes.
 */
static int
prepare_offer(tiku_x11_clipboard_t *c)
{
    size_t n = tiku_clipboard_length(), html_bytes = 0, rtf_bytes = 0;
    const void *html = tiku_clipboard_find("text/html", &html_bytes);
    const void *rtf = tiku_clipboard_find("text/rtf", &rtf_bytes);
    if (!rtf)
        rtf = tiku_clipboard_find("application/rtf", &rtf_bytes);
    char *rtf_copy = rtf_bytes ? malloc(rtf_bytes) : NULL;
    char *text_copy = n ? malloc(n + 1) : NULL;
    char *html_copy = html_bytes ? malloc(html_bytes) : NULL;

    if ((n && !text_copy) || (html_bytes && !html_copy) ||
        (rtf_bytes && !rtf_copy)) {
        free(text_copy);
        free(html_copy);
        free(rtf_copy);
        return 0;
    }
    if (text_copy)
        memcpy(text_copy, tiku_clipboard_text(), n + 1);
    if (html_copy)
        memcpy(html_copy, html, html_bytes);
    if (rtf_copy)
        memcpy(rtf_copy, rtf, rtf_bytes);
    free(c->outgoing);
    free(c->out_html);
    free(c->out_rtf);
    c->outgoing = text_copy;
    c->outlen = n;
    c->out_html = html_copy;
    c->out_html_len = html_bytes;
    c->out_rtf = rtf_copy;
    c->out_rtf_len = rtf_bytes;
    return 1;
}

void
tiku_x11_clipboard_tick(tiku_x11_clipboard_t *c, int64_t now)
{
    if (c->stamp != tiku_clipboard_stamp()) {
        cancel(c);
        c->result = 0;
        c->stamp = tiku_clipboard_stamp();
        if (!prepare_offer(c)) {
            c->result = TIKU_CLIPBOARD_UNAVAILABLE;
            return;
        }
        /* Ownership is taken at a server timestamp, which arrives with
         * the property change made here. */
        if (!c->publishing) {
            c->publishing = 1;
            XChangeProperty(c->display, c->window, c->clock, XA_INTEGER, 8,
                            PropModeReplace, (unsigned char *)"", 0);
            XFlush(c->display);
        }
    }
    if (c->requestor && (now >= c->deadline || now >= c->expires))
        finish(c, TIKU_CLIPBOARD_TIMEOUT, now);
    if (c->send.requestor && now >= c->send.deadline)
        send_stop(c);
}

/** @brief Start one target conversion while retaining a staged text fallback.
 */
static void
request_target(tiku_x11_clipboard_t *c, Atom target, int64_t now)
{
    free(c->incoming);
    c->incoming = NULL;
    c->room = c->used = 0;
    c->incremental = 0;
    c->requested = target;
    c->received = None;
    c->deadline = now + WAIT_US;
    XConvertSelection(c->display, c->clipboard, target, c->property,
                      c->requestor, CurrentTime);
    XFlush(c->display);
}

int
tiku_x11_clipboard_request(tiku_x11_clipboard_t *c, int64_t now, int force)
{
    if (c->requestor) {
        if (force)
            c->asked = 1;
        return 1;
    }
    if (c->publishing)
        return 0;
    Window owner = XGetSelectionOwner(c->display, c->clipboard);
    if (owner == c->window)
        return 0;
    if (!force && owner == c->cached_owner && c->refreshed &&
        now - c->refreshed < 250000)
        return 0;
    c->source = owner;
    if (!owner) {
        /* With no owner the clipboard is empty. */
        tiku_clipboard_set("");
        finish(c, TIKU_CLIPBOARD_OK, now);
        return 0;
    }
    c->requestor = XCreateSimpleWindow(
        c->display, DefaultRootWindow(c->display), 0, 0, 1, 1, 0, 0, 0);
    XSelectInput(c->display, c->requestor, PropertyChangeMask);
    c->asked = force != 0;
    c->expires = now + IMPORT_US;
    c->rich_target = None;
    request_target(c, c->targets, now);
    return 1;
}

/** @brief Make room for @p more bytes past what came in, and a NUL;
 *         0 when out of memory. */
static int
grow(tiku_x11_clipboard_t *c, size_t more)
{
    size_t need = c->used + more + 1, room = c->room ? c->room : 4096;
    char *bigger;
    if (need <= c->room)
        return 1;
    while (room < need)
        room *= 2;
    if (!(bigger = realloc(c->incoming, room)))
        return 0;
    c->incoming = bigger;
    c->room = room;
    return 1;
}

/**
 * @brief Take the bytes read into the local clipboard, Latin-1 converted
 *        to UTF-8, unless the selection changed owner meanwhile.
 */
static void
import(tiku_x11_clipboard_t *c, int64_t now)
{
    char *converted = NULL;
    const char *text = c->incoming ? c->incoming : "";
    size_t length = c->used;
    int ok;
    if (XGetSelectionOwner(c->display, c->clipboard) != c->source) {
        finish(c, TIKU_CLIPBOARD_UNAVAILABLE, now);
        return;
    }
    if (c->requested == c->html || c->requested == c->rtf ||
        c->requested == c->rtf_application) {
        ok = length && tiku_clipboard_import_typed(
                           c->plain ? c->plain : "", c->plainlen,
                           c->requested == c->html ? "text/html" : "text/rtf",
                           text, length);
        finish(c, ok ? TIKU_CLIPBOARD_OK : TIKU_CLIPBOARD_INVALID, now);
        return;
    }
    if (c->received == XA_STRING) { /* Latin-1: two bytes at most each */
        if (!(converted = malloc(2 * c->used + 1))) {
            finish(c, TIKU_CLIPBOARD_TOO_LARGE, now);
            return;
        }
        length = 0;
        for (size_t i = 0; i < c->used; i++) {
            unsigned char ch = (unsigned char)c->incoming[i];
            if (ch >= 128)
                converted[length++] = (char)(0xc0 | (ch >> 6));
            converted[length++] = (char)(ch < 128 ? ch : 0x80 | (ch & 63));
        }
        if (length > TIKU_CLIPBOARD_LIMIT) {
            free(converted);
            finish(c, TIKU_CLIPBOARD_TOO_LARGE, now);
            return;
        }
        text = converted;
    }
    if (c->rich_target) {
        c->plain = converted ? converted : c->incoming;
        if (!converted)
            c->incoming = NULL;
        c->plainlen = length;
        request_target(c, c->rich_target, now);
        return;
    }
    ok = tiku_clipboard_import(text, length);
    free(converted);
    finish(c, ok ? TIKU_CLIPBOARD_OK : TIKU_CLIPBOARD_INVALID, now);
}

/** @brief Read a bounded TARGETS list; unsupported negotiation uses plain text.
 */
static void
read_targets(tiku_x11_clipboard_t *c, int64_t now)
{
    Atom type;
    int format;
    unsigned long count, after;
    unsigned char *data = NULL;

    if (XGetWindowProperty(c->display, c->requestor, c->property, 0, 1024, True,
                           XA_ATOM, &type, &format, &count, &after,
                           &data) != Success ||
        type != XA_ATOM || format != 32 || after) {
        if (data)
            XFree(data);
        finish(c, TIKU_CLIPBOARD_INVALID, now);
        return;
    }
    for (unsigned long i = 0; i < count; i++) {
        Atom offered = ((Atom *)data)[i];
        if (offered == c->html)
            c->rich_target = c->html;
        else if (c->rich_target != c->html &&
                 (offered == c->rtf || offered == c->rtf_application))
            c->rich_target = offered;
    }
    XFree(data);
    request_target(c, c->utf8, now);
}

/**
 * @brief Read the requestor's property whole, a piece at a time however
 *        large, then delete it, which in an INCR transfer asks for the
 *        next piece.
 */
static void
read_property(tiku_x11_clipboard_t *c, int initial, int64_t now)
{
    Atom type = None;
    int format = 0, result = 0;
    unsigned long count = 0, after = 0, got = 0;
    long offset = 0;
    if (c->requested == c->targets) {
        read_targets(c, now);
        return;
    }
    do {
        unsigned char *data = NULL;
        if (XGetWindowProperty(c->display, c->requestor, c->property, offset,
                               READ_UNITS, False, AnyPropertyType, &type,
                               &format, &count, &after, &data) != Success ||
            type == None) {
            if (data)
                XFree(data);
            if (offset == 0)
                return; /* nothing there yet */
            break;
        }
        if (initial && offset == 0 && type == c->incr) {
            int valid = format == 32 && count == 1 && !after;
            unsigned long lower = valid ? *(unsigned long *)data : 0;
            XFree(data);
            XDeleteProperty(c->display, c->requestor, c->property);
            if (!valid)
                finish(c, TIKU_CLIPBOARD_INVALID, now);
            else if (lower > TIKU_CLIPBOARD_LIMIT - c->plainlen)
                finish(c, TIKU_CLIPBOARD_TOO_LARGE, now);
            else {
                c->incremental = 1;
                c->deadline = now + WAIT_US;
            }
            XFlush(c->display);
            return;
        }
        if (format != 8 ||
            ((c->requested == c->html || c->requested == c->rtf ||
              c->requested == c->rtf_application)
                 ? type != c->requested
                 : (type != c->utf8 && type != XA_STRING)) ||
            (c->received != None && type != c->received))
            result = TIKU_CLIPBOARD_INVALID;
        else if (count > TIKU_CLIPBOARD_LIMIT - c->plainlen - c->used ||
                 !grow(c, count))
            result = TIKU_CLIPBOARD_TOO_LARGE;
        else {
            c->received = type;
            if (count)
                memcpy(c->incoming + c->used, data, count);
            c->used += count;
            got += count;
            offset += (long)(count / 4);
        }
        if (data)
            XFree(data);
    } while (!result && after);
    XDeleteProperty(c->display, c->requestor, c->property);
    c->deadline = now + WAIT_US; /* progress: the clock starts again */
    if (result)
        finish(c, result, now);
    else if (!c->incremental || !got)
        import(c, now);
    XFlush(c->display);
}

/**
 * @brief Convert UTF-8 @p s to Latin-1, the encoding of XA_STRING.
 *
 * @return the bytes made into *out, which the caller frees, or -1 for a
 *         character Latin-1 lacks.
 */
static long
latin1(const char *s, size_t n, unsigned char **out)
{
    unsigned char *o = malloc(n + 1);
    long k = 0;
    if (!o)
        return -1;
    for (size_t i = 0; i < n;) {
        unsigned char ch = (unsigned char)s[i++];
        if (ch < 128)
            o[k++] = ch;
        else if ((ch == 0xc2 || ch == 0xc3) && i < n &&
                 ((unsigned char)s[i] & 0xc0) == 0x80)
            o[k++] = (unsigned char)(((ch & 3) << 6) | (s[i++] & 63));
        else {
            free(o);
            return -1;
        }
    }
    *out = o;
    return k;
}

/**
 * @brief Put @p n bytes of @p data, of @p type, onto a requestor's
 *        property: in one request up to SINGLE_MAX, else as the one INCR
 *        transfer out, from a copy, announced by its size.
 *
 * @return 1 when the requestor will get them; 0 while another transfer
 *         out runs, or when out of memory.
 */
static int
put_text(tiku_x11_clipboard_t *c, XSelectionRequestEvent *q, Atom prop,
         Atom type, const char *data, size_t n, int64_t now)
{
    long size = (long)n;
    char *copy;
    if (n <= SINGLE_MAX) {
        XChangeProperty(c->display, q->requestor, prop, type, 8,
                        PropModeReplace,
                        (const unsigned char *)(data ? data : ""), (int)n);
        return 1;
    }
    if (c->send.requestor || !(copy = malloc(n)))
        return 0; /* one at a time */
    memcpy(copy, data, n);
    XSelectInput(c->display, q->requestor, PropertyChangeMask);
    XChangeProperty(c->display, q->requestor, prop, c->incr, 32,
                    PropModeReplace, (unsigned char *)&size, 1);
    c->send = (tiku_x11_send_t){.requestor = q->requestor,
                                .property = prop,
                                .type = type,
                                .data = copy,
                                .len = n,
                                .deadline = now + SEND_US};
    return 1;
}

/** @brief Send the next piece to a requestor that deleted the last; the
 *         empty piece after the text ends the transfer (ICCCM 2.7.2). */
static void
send_next(tiku_x11_clipboard_t *c, int64_t now)
{
    tiku_x11_send_t *s = &c->send;
    size_t n = s->len - s->sent < PIECE ? s->len - s->sent : PIECE;
    trap(c->display);
    XChangeProperty(c->display, s->requestor, s->property, s->type, 8,
                    PropModeReplace, (unsigned char *)s->data + s->sent,
                    (int)n);
    if (untrap(c->display) || n == 0) {
        send_stop(c);
        return;
    }
    s->sent += n;
    s->deadline = now + SEND_US;
}

/**
 * @brief Answer a SelectionRequest for TARGETS, TIMESTAMP, UTF8_STRING,
 *        TEXT, STRING or published HTML/RTF while this window owns CLIPBOARD.
 */
static void
serve(tiku_x11_clipboard_t *c, XSelectionRequestEvent *q, int64_t now)
{
    XEvent reply = {0};
    Atom prop = q->property ? q->property : q->target;
    reply.xselection = (XSelectionEvent){.type = SelectionNotify,
                                         .display = q->display,
                                         .requestor = q->requestor,
                                         .selection = q->selection,
                                         .target = q->target,
                                         .time = q->time,
                                         .property = None};
    int owned =
        q->selection == c->clipboard &&
        XGetSelectionOwner(c->display, c->clipboard) == c->window &&
        (q->time == CurrentTime || (int32_t)(q->time - c->acquired) >= 0);
    trap(c->display);
    if (owned) {
        if (q->target == c->targets) {
            Atom targets[8] = {c->targets, c->timestamp, c->utf8, c->text,
                               XA_STRING};
            int count = 5;
            if (c->out_html)
                targets[count++] = c->html;
            if (c->out_rtf) {
                targets[count++] = c->rtf;
                targets[count++] = c->rtf_application;
            }
            XChangeProperty(c->display, q->requestor, prop, XA_ATOM, 32,
                            PropModeReplace, (unsigned char *)targets, count);
            reply.xselection.property = prop;
        } else if (q->target == c->timestamp) {
            XChangeProperty(c->display, q->requestor, prop, XA_INTEGER, 32,
                            PropModeReplace, (unsigned char *)&c->acquired, 1);
            reply.xselection.property = prop;
        } else if (q->target == c->utf8 || q->target == c->text) {
            if (put_text(c, q, prop, c->utf8, c->outgoing, c->outlen, now))
                reply.xselection.property = prop;
        } else if ((q->target == c->rtf || q->target == c->rtf_application) &&
                   c->out_rtf) {
            if (put_text(c, q, prop, q->target, c->out_rtf, c->out_rtf_len,
                         now))
                reply.xselection.property = prop;
        } else if (q->target == c->html && c->out_html) {
            if (put_text(c, q, prop, c->html, c->out_html, c->out_html_len,
                         now))
                reply.xselection.property = prop;
        } else if (q->target == XA_STRING) {
            unsigned char *converted = NULL;
            long n =
                latin1(c->outgoing ? c->outgoing : "", c->outlen, &converted);
            if (n >= 0 && put_text(c, q, prop, XA_STRING, (char *)converted,
                                   (size_t)n, now))
                reply.xselection.property = prop;
            free(converted);
        }
    }
    if (untrap(c->display))
        reply.xselection.property = None;
    trap(c->display);
    XSendEvent(c->display, q->requestor, False, 0, &reply);
    (void)untrap(c->display);
}

Bool
tiku_x11_clipboard_match(Display *d, XEvent *e, XPointer ctx)
{
    tiku_x11_clipboard_t *c = (tiku_x11_clipboard_t *)ctx;
    (void)d;
    return (e->type == SelectionRequest &&
            e->xselectionrequest.selection == c->clipboard) ||
           (e->type == SelectionClear &&
            e->xselectionclear.selection == c->clipboard) ||
           (e->type == SelectionNotify &&
            e->xselection.selection == c->clipboard) ||
           (e->type == PropertyNotify &&
            (e->xproperty.window == c->requestor ||
             (c->send.requestor && e->xproperty.window == c->send.requestor) ||
             (e->xproperty.window == c->window &&
              e->xproperty.atom == c->clock)));
}
int
tiku_x11_clipboard_event(tiku_x11_clipboard_t *c, XEvent *e, int64_t now)
{
    if (!tiku_x11_clipboard_match(c->display, e, (XPointer)c))
        return 0;
    if (e->type == SelectionRequest)
        serve(c, &e->xselectionrequest, now);
    else if (e->type == SelectionClear &&
             e->xselectionclear.selection == c->clipboard)
        c->refreshed = 0;
    else if (e->type == PropertyNotify && c->send.requestor &&
             e->xproperty.window == c->send.requestor) {
        if (e->xproperty.atom == c->send.property &&
            e->xproperty.state == PropertyDelete)
            send_next(c, now);
    } else if (e->type == PropertyNotify && e->xproperty.window == c->window &&
               e->xproperty.atom == c->clock &&
               e->xproperty.state == PropertyNewValue && c->publishing) {
        c->publishing = 0;
        c->acquired = e->xproperty.time;
        c->cached_owner = c->window;
        c->refreshed = 0;
        XSetSelectionOwner(c->display, c->clipboard, c->window, c->acquired);
        XFlush(c->display);
    } else if (e->type == SelectionNotify && c->requestor &&
               e->xselection.requestor == c->requestor &&
               e->xselection.selection == c->clipboard &&
               e->xselection.target == c->requested) {
        if (e->xselection.property == c->property)
            read_property(c, 1, now);
        else if (e->xselection.property == None && c->requested == c->targets)
            request_target(c, c->utf8, now);
        else if (e->xselection.property == None && c->requested == c->utf8)
            request_target(c, XA_STRING, now);
        else if (e->xselection.property == None && c->requested == XA_STRING &&
                 c->rich_target)
            request_target(c, c->rich_target, now);
        else
            finish(c, TIKU_CLIPBOARD_UNAVAILABLE, now);
    } else if (e->type == PropertyNotify && c->requestor && c->incremental &&
               e->xproperty.window == c->requestor &&
               e->xproperty.atom == c->property &&
               e->xproperty.state == PropertyNewValue)
        read_property(c, 0, now);
    return 1;
}
