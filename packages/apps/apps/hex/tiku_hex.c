/* tiku_hex.c -- TikuHex: an overwrite-only binary editor, a hex and ASCII
 * grid with offset and byte-pattern fields; a file's length never changes.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_app_product.h"
#include "tiku_hex_model.h"
#include "tiku_app.h"
#include "tiku_dialog.h"
#include "tiku_field_event.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HEX_ROWS 16
enum { H_OPEN = 1, H_SAVE, H_SAVEAS, H_MODE, H_UNDO, H_REDO, H_INPUT,
       H_COPY, H_GO, H_FIND, H_PREV, H_NEXT, H_CLOSE, H_CANCEL, H_DISCARD,
       H_OFFSET = 30, H_PATTERN, H_GRID };
/** @brief The running editor: its window, document, controls and modes. */
typedef struct {
    const tiku_app_services_t *services;
    tiku_surface_t *surface;
    uint32_t id;
    hex_model_t model;
    tiku_dialog_t dialog;
    tiku_field_t field[2];
    tiku_field_input_t input[2];
    int width, height, row, cell, readonly, ascii, high, pending, pick_save;
    int save_failed;                  /* a Save refused under the question */
    size_t top;
    char status[192];
} hex_app_t;

/** @brief The byte grid's rectangle: HEX_ROWS rows of the mono face. */
static tiku_rect_t hex_grid(hex_app_t *p)
{ return (tiku_rect_t){12, 116, p->width - 24, HEX_ROWS * p->row + 8}; }

/** @brief The rectangle of the offset field (0) or the find field (1). */
static tiku_rect_t hex_field(int i)
{ return i ? (tiku_rect_t){375, 58, 270, 28} : (tiku_rect_t){72, 58, 140, 28}; }

/** @brief Scroll so the cursor's row is in view. */
static void hex_reveal(hex_app_t *p)
{
    size_t row = p->model.cursor / 16;
    if (row < p->top) p->top = row;
    if (row >= p->top + HEX_ROWS) p->top = row - HEX_ROWS + 1;
}

/** @brief Rebuild the window's controls, each enabled as the document
 *         stands; while the unsaved-edits question is open only Save and
 *         the question's two buttons are. */
static void hex_controls(hex_app_t *p)
{
    tiku_dialog_begin(&p->dialog, TIKU_DIALOG_NONE, H_CANCEL);
    const int xs[] = {16, 102, 188, 292, 430, 508, 586, 724};
    const int ws[] = {78, 78, 96, 130, 70, 70, 130, 80};
    for (int i = 0; i < 8; i++) {
        int enabled = !p->pending;
        if (i == 1) enabled = p->model.changed != 0;
        if (i == 2 || i == 3) enabled &= p->model.bytes != NULL;
        if (i == 4) enabled &= !p->readonly && p->model.position > 0;
        if (i == 5) enabled &= !p->readonly && p->model.position < p->model.used;
        if (i == 7) enabled &= p->model.size > 0;
        tiku_dialog_add(&p->dialog, H_OPEN + i, (tiku_rect_t){xs[i], 14, ws[i], 28},
                        TIKU_DIALOG_BUTTON, enabled);
    }
    for (int i = 0; i < 2; i++) tiku_dialog_add(&p->dialog, H_OFFSET + i,
        hex_field(i), TIKU_DIALOG_INPUT, !p->pending);
    tiku_dialog_add(&p->dialog, H_GO, (tiku_rect_t){220, 58, 58, 28}, TIKU_DIALOG_BUTTON, !p->pending);
    tiku_dialog_add(&p->dialog, H_FIND, (tiku_rect_t){653, 58, 150, 28}, TIKU_DIALOG_BUTTON, !p->pending);
    tiku_dialog_add(&p->dialog, H_GRID, hex_grid(p), TIKU_DIALOG_INPUT, !p->pending);
    int y = p->height - 42;
    tiku_dialog_add(&p->dialog, H_PREV, (tiku_rect_t){16, y, 92, 28}, TIKU_DIALOG_BUTTON, !p->pending && p->top > 0);
    tiku_dialog_add(&p->dialog, H_NEXT, (tiku_rect_t){116, y, 92, 28}, TIKU_DIALOG_BUTTON,
                    !p->pending && (p->top + HEX_ROWS) * 16 < p->model.size);
    if (p->pending) {
        tiku_dialog_add(&p->dialog, H_CANCEL, (tiku_rect_t){p->width - 266, y, 118, 28}, TIKU_DIALOG_BUTTON, 1);
        tiku_dialog_add(&p->dialog, H_DISCARD, (tiku_rect_t){p->width - 140, y, 124, 28}, TIKU_DIALOG_BUTTON, 1);
    }
    tiku_dialog_end(&p->dialog);
}

/** @brief Publish the File menu: Open..., Save, Save As... and Close. */
static void hex_menus(hex_app_t *p)
{
    tiku_menuset_t menus = {0}; menus.nmenu = 1;
    snprintf(menus.menu[0].title, sizeof menus.menu[0].title, "File");
    const char *labels[] = {"Open…", "Save", "Save As…", "Close"};
    const int commands[] = {H_OPEN, H_SAVE, H_SAVEAS, H_CLOSE};
    const unsigned keys[] = {'o', 's', 0, 'w'};
    menus.menu[0].nitem = 4;
    for (int i = 0; i < 4; i++) {
        snprintf(menus.menu[0].item[i].label, sizeof menus.menu[0].item[i].label, "%s", labels[i]);
        menus.menu[0].item[i].command = commands[i];
        menus.menu[0].item[i].sc = (char)keys[i];
        menus.menu[0].item[i].mods = keys[i] ? TIKU_MOD_CMD : 0;
        menus.menu[0].item[i].enabled = i == 1 ? p->model.changed != 0 : 1;
    }
    p->services->menus(p->services->ctx, p->id, &menus);
}

/** @brief Draw @p text in the plain face, clipped to @p r. */
static void hex_line(hex_app_t *p, tiku_rect_t r, const char *text)
{ tiku_text_line_draw(p->surface, r, TIKU_MEASURE_PLAIN, text, TIKU_C_TEXT); }

/** @brief The bytes at the cursor read as values: its bits, and 16- and
 *         32-bit little- and big-endian integers, built a byte at a time;
 *         nothing when the window is too narrow. */
static void hex_inspect(hex_app_t *p, int x, int y)
{
    if (p->width - x < 148 || !p->model.size) return;
    char text[64];
    hex_line(p, (tiku_rect_t){x, y, p->width - x - 20, 26}, "At the cursor");
    unsigned char *b = p->model.bytes + p->model.cursor;
    size_t left = p->model.size - p->model.cursor;
    for (int i = 0; i < 8; i++) text[i] = (char)('0' + ((b[0] >> (7 - i)) & 1));
    text[8] = 0;
    hex_line(p, (tiku_rect_t){x, y + 30, p->width - x - 20, 26}, text);
    for (int n = 2; n <= 4; n += 2) {
        uint32_t little = 0, big = 0;
        if (left >= (size_t)n) for (int i = 0; i < n; i++) {
            little |= (uint32_t)b[i] << (8 * i); big = (big << 8) | b[i];
        }
        int row = y + 66 + (n - 2) * 40;
        if (left >= (size_t)n) snprintf(text, sizeof text, "LE%d: %u", n * 8, little);
        else snprintf(text, sizeof text, "LE%d: —", n * 8);
        hex_line(p, (tiku_rect_t){x, row, p->width - x - 20, 26}, text);
        if (left >= (size_t)n) snprintf(text, sizeof text, "BE%d: %u", n * 8, big);
        else snprintf(text, sizeof text, "BE%d: —", n * 8);
        hex_line(p, (tiku_rect_t){x, row + 26, p->width - x - 20, 26}, text);
    }
}

/** @brief Draw the grid: sixteen bytes a row in hex and ASCII, the
 *         cursor's byte selected and changed bytes underlined; then the
 *         values at the cursor. */
static void hex_bytes(hex_app_t *p)
{
    tiku_rect_t g = hex_grid(p);
    const tiku_font_t *f = tiku_font_mono(0);
    int hx = 16 + 10 * p->cell, ax = hx + 50 * p->cell;
    tiku_ui_sunken(p->surface, g, TIKU_C_DOC);
    tiku_text(p->surface, f, 16, 108, "Offset", TIKU_C_TEXT);
    for (int col = 0; col < 16; col++) {
        char text[4]; snprintf(text, sizeof text, "%02X", col);
        tiku_text(p->surface, f, hx + col * 3 * p->cell, 108, text, TIKU_C_TEXT);
    }
    tiku_text(p->surface, f, ax, 108, "ASCII", TIKU_C_TEXT);
    for (int row = 0; row < HEX_ROWS; row++) {
        size_t start = (p->top + (size_t)row) * 16;
        if (start >= p->model.size) break;
        int y = g.y + 4 + row * p->row;
        char offset[24]; snprintf(offset, sizeof offset, "%08zX", start);
        tiku_text(p->surface, f, 16, y + f->ascent, offset, TIKU_C_TEXT);
        for (int col = 0; col < 16 && start + col < p->model.size; col++) {
            size_t at = start + col;
            unsigned char b = p->model.bytes[at];
            tiku_rect_t hex = {hx + col * 3 * p->cell, y, 2 * p->cell, p->row};
            tiku_rect_t ascii = {ax + col * p->cell, y, p->cell, p->row};
            int selected = at == p->model.cursor;
            if (selected) {
                tiku_fill(p->surface, hex, TIKU_C_SELECT);
                tiku_fill(p->surface, ascii, TIKU_C_SELECT);
            }
            tiku_rgb_t color = selected ? TIKU_C_SELTEXT : TIKU_C_TEXT;
            char text[4]; snprintf(text, sizeof text, "%02X", b);
            tiku_text(p->surface, f, hex.x, y + f->ascent, text, color);
            text[0] = b >= 32 && b < 127 ? (char)b : '.'; text[1] = 0;
            tiku_text(p->surface, f, ascii.x, y + f->ascent, text, color);
            if (b != p->model.original[at])
                tiku_fill(p->surface, (tiku_rect_t){hex.x, y + p->row - 3, hex.w, 2}, color);
        }
    }
    if (!p->model.size) hex_line(p, (tiku_rect_t){24, g.y + 20, g.w - 32, 30},
        p->model.bytes ? "Empty file (0 bytes). This editor preserves file length." :
        "Open a local binary file or device dump. Files start read-only.");
    hex_inspect(p, ax + 16 * p->cell + 22, g.y + 8);
    if (tiku_dialog_focused(&p->dialog, H_GRID))
        tiku_frame(p->surface, tiku_inset(g, 2), TIKU_C_FOCUS);
}

/** @brief The foot line: while the unsaved-edits question stands, the
 *         question, or why a Save it led to failed; else the status or the
 *         path. */
static const char *hex_foot(hex_app_t *p)
{
    if (p->pending && p->save_failed) return p->status;
    if (p->pending) return "Unsaved edits: Save to continue, Keep editing, "
                           "or Discard edits.";
    return p->status[0] ? p->status : p->model.path;
}

/** @brief Draw the window: toolbar, fields, grid and status lines; then
 *         hand the frame to the desktop and publish the menu. */
static void hex_paint(hex_app_t *p)
{
    hex_controls(p);
    tiku_fill(p->surface, (tiku_rect_t){0, 0, p->width, p->height}, TIKU_C_PANEL);
    hex_bytes(p);
    hex_line(p, (tiku_rect_t){16, 58, 56, 28}, "Offset:");
    hex_line(p, (tiku_rect_t){302, 58, 70, 28}, "Hex find:");
    const char *labels[] = {"Open…", "Save", "Save As…", p->readonly ? "Read-only" : "Editing bytes",
        "Undo", "Redo", p->ascii ? "Input: ASCII" : "Input: Hex", "Copy"};
    for (int i = 0; i < p->dialog.count; i++) {
        tiku_dialog_control_t *c = &p->dialog.control[i];
        unsigned flags = (c->enabled ? 0 : TIKU_S_DISABLED) |
            (tiku_dialog_focused(&p->dialog, c->id) ? TIKU_S_FOCUS : 0) |
            (tiku_dialog_pressed(&p->dialog, c->id) ? TIKU_S_PRESSED : 0);
        if (c->id == H_OFFSET || c->id == H_PATTERN) {
            int k = c->id - H_OFFSET;
            tiku_ui_textfield_scroll(p->surface, c->rect, p->field[k].text,
                p->field[k].caret, p->field[k].sel_a, p->field[k].sel_b,
                flags, p->input[k].scroll);
        } else if (c->id != H_GRID) {
            const char *label = c->id <= H_COPY ? labels[c->id - 1] :
                c->id == H_GO ? "Go" : c->id == H_FIND ? "Find next" :
                c->id == H_PREV ? "Page up" : c->id == H_NEXT ? "Page down" :
                c->id == H_CANCEL ? "Keep editing" : "Discard edits";
            tiku_ui_button(p->surface, c->rect, label, flags);
        }
    }
    char text[256];
    if (p->model.size) snprintf(text, sizeof text,
        "0x%08zX (%zu)  |  %02X = %u  |  %zu bytes  |  %zu changed%s",
        p->model.cursor, p->model.cursor, p->model.bytes[p->model.cursor],
        p->model.bytes[p->model.cursor], p->model.size, p->model.changed,
        p->high >= 0 ? "  |  First hex digit entered; type the second." : "");
    else snprintf(text, sizeof text, "0 bytes | Read-only until you enable editing");
    hex_line(p, (tiku_rect_t){16, p->height - 102, p->width - 32, 26}, text);
    hex_line(p, (tiku_rect_t){16, p->height - 73, p->width - 32, 26},
        hex_foot(p));
    if (!p->pending) hex_line(p, (tiku_rect_t){224, p->height - 42,
        p->width - 240, 28}, p->model.path);
    p->services->frame(p->services->ctx, p->id, p->surface->px, p->width, p->height);
    hex_menus(p);
}

/** @brief Open @p path read-only; a failed load keeps the document shown
 *         and puts the reason in the status line. */
static void hex_open_file(hex_app_t *p, const char *path)
{
    if (hex_load(&p->model, path)) {
        p->readonly = 1; p->top = 0; p->high = -1;
        snprintf(p->status, sizeof p->status, "Opened read-only. Enable Editing bytes to modify the buffer.");
    } else snprintf(p->status, sizeof p->status, "%s", p->model.error);
}

/** @brief Carry out @p command.  Open and Close with edits unsaved ask
 *         first: Save, Keep editing or Discard edits; a failed Save keeps
 *         the edits and the question.  @return 1 to close the app. */
static int hex_action(hex_app_t *p, int command)
{
    if ((command == H_OPEN || command == H_CLOSE) && p->model.changed) {
        p->pending = command; p->save_failed = 0; return 0;
    }
    if (command == H_CLOSE) return 1;
    if (command == H_CANCEL) {
        p->pending = 0; p->save_failed = 0; p->high = -1; return 0;
    }
    if (command == H_DISCARD && p->pending) {
        command = p->pending; p->pending = 0; p->save_failed = 0;
        memcpy(p->model.bytes, p->model.original, p->model.size);
        p->model.changed = 0; p->model.used = p->model.position = 0;
        return hex_action(p, command);
    }
    if (command == H_SAVE && p->model.changed) {
        if (hex_save(&p->model, p->model.path)) {
            snprintf(p->status, sizeof p->status, "Saved %zu bytes.", p->model.size);
            int next = p->pending; p->pending = 0; p->save_failed = 0;
            if (next) return hex_action(p, next);
        } else {
            snprintf(p->status, sizeof p->status, "%s%s", p->model.error,
                     p->pending ? "  Keep editing, or Discard edits." : "");
            p->save_failed = p->pending != 0;
        }
        return 0;
    }
    if (p->pending) return 0;
    if ((command == H_OPEN || command == H_SAVEAS) && p->services->pick) {
        p->pick_save = command == H_SAVEAS;
        p->services->pick(p->services->ctx, p->id, p->pick_save ? TIKU_APP_PICK_SAVE : TIKU_APP_PICK_OPEN,
                          NULL, p->pick_save ? "copy.bin" : NULL);
    } else if (command == H_MODE) { p->readonly = !p->readonly; p->high = -1; }
    else if (command == H_INPUT) { p->ascii = !p->ascii; p->high = -1; }
    else if ((command == H_UNDO || command == H_REDO) && !p->readonly) {
        hex_undo(&p->model, command == H_REDO); hex_reveal(p); p->high = -1;
    } else if (command == H_GO) {
        size_t offset;
        if (hex_offset(p->field[0].text, p->model.size, &offset)) {
            p->model.cursor = offset; hex_reveal(p); p->high = -1;
            tiku_dialog_focus(&p->dialog, H_GRID); p->status[0] = 0;
        } else snprintf(p->status, sizeof p->status, "Enter an offset inside the file: decimal or 0x hexadecimal.");
    } else if (command == H_FIND) {
        unsigned char bytes[64]; size_t n;
        if (!hex_pattern(p->field[1].text, bytes, &n))
            snprintf(p->status, sizeof p->status, "Enter 1–64 complete hex bytes, such as 00 FF 12.");
        else if (!hex_find(&p->model, bytes, n)) snprintf(p->status, sizeof p->status, "Byte sequence not found.");
        else { hex_reveal(p); p->high = -1; snprintf(p->status, sizeof p->status, "Match at 0x%zX (search wraps).", p->model.cursor); }
    } else if (command == H_COPY && p->model.size && p->services->clip_set) {
        char text[4]; snprintf(text, sizeof text, "%02X", p->model.bytes[p->model.cursor]);
        p->services->clip_set(p->services->ctx, text);
    } else if (command == H_PREV) p->top = p->top > HEX_ROWS ? p->top - HEX_ROWS : 0;
    else if (command == H_NEXT && (p->top + HEX_ROWS) * 16 < p->model.size) p->top += HEX_ROWS;
    return 0;
}

/** @brief A key on the grid: the arrows, Page Up, Page Down, Home and End
 *         move the cursor; while editing, a printable character or two hex
 *         digits replace the byte and move on. */
static void hex_key(hex_app_t *p, const tiku_event_t *e)
{
    if (!p->model.size) return;
    size_t at = p->model.cursor;
    if (e->key == TIKU_KEY_LEFT) { if (at) at--; }
    else if (e->key == TIKU_KEY_RIGHT) { if (at + 1 < p->model.size) at++; }
    else if (e->key == TIKU_KEY_UP) at = at >= 16 ? at - 16 : 0;
    else if (e->key == TIKU_KEY_DOWN) at += 16;
    else if (e->key == TIKU_KEY_PAGE_UP) at = at >= 256 ? at - 256 : 0;
    else if (e->key == TIKU_KEY_PAGE_DOWN) at += 256;
    else if (e->key == TIKU_KEY_HOME) at = 0;
    else if (e->key == TIKU_KEY_END) at = p->model.size - 1;
    else {
        if (p->readonly) { snprintf(p->status, sizeof p->status, "Read-only: enable Editing bytes first."); return; }
        unsigned char ch = e->text[0] ? (unsigned char)e->text[0] : (unsigned char)e->key;
        if (p->ascii) {
            if (ch < 32 || ch > 126 || e->text[1]) return;
            hex_replace(&p->model, at, ch);
        } else {
            int digit = ch >= '0' && ch <= '9' ? ch - '0' :
                ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 :
                ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
            if (digit < 0) return;
            if (p->high < 0) { p->high = digit; return; }
            hex_replace(&p->model, at, (unsigned char)(p->high * 16 + digit));
        }
        if (at + 1 < p->model.size) at++;
        p->status[0] = 0;
    }
    p->high = -1;
    p->model.cursor = at < p->model.size ? at : p->model.size - 1;
    hex_reveal(p);
}

/** @brief A click on the grid: the byte under it becomes the cursor, and
 *         the side clicked sets the input to hex or ASCII. */
static void hex_pointer(hex_app_t *p, const tiku_event_t *e)
{
    tiku_rect_t g = hex_grid(p);
    int hx = 16 + 10 * p->cell, ax = hx + 50 * p->cell;
    if (e->y < g.y + 4 || e->y >= g.y + 4 + HEX_ROWS * p->row) return;
    int col;
    if (e->x >= ax && e->x < ax + 16 * p->cell) {
        col = (e->x - ax) / p->cell; p->ascii = 1;
    } else if (e->x >= hx && e->x < hx + 48 * p->cell) {
        col = (e->x - hx) / (3 * p->cell); p->ascii = 0;
    } else return;
    size_t at = (p->top + (e->y - g.y - 4) / p->row) * 16 + col;
    if (at < p->model.size) { p->model.cursor = at; p->high = -1; }
}

/** @brief Every window event: Ctrl shortcuts, the controls, the pointer
 *         and wheel on the grid, the two fields, then keys on the grid; a
 *         Ctrl key that is not a shortcut never reaches the grid.
 *  @return 1 to close. */
static int hex_event(void *state, const tiku_event_t *e)
{
    hex_app_t *p = state;
    if (e->type == TIKU_EVENT_POINTER_MOVE && p->dialog.capture < 0 &&
        !p->input[0].dragging && !p->input[1].dragging) return 0;
    hex_controls(p);
    int command = 0;
    if (e->type == TIKU_EVENT_KEY_DOWN && e->key == TIKU_KEY_ESCAPE)
        p->high = -1;
    if (e->type == TIKU_EVENT_CLOSE) command = H_CLOSE;
    else if (e->type == TIKU_EVENT_KEY_DOWN && (e->modifiers & TIKU_MOD_CMD)) {
        command = e->key == 's' ? H_SAVE : e->key == 'o' ? H_OPEN :
            e->key == 'w' || e->key == 'q' ? H_CLOSE :
            e->key == 'z' ? H_UNDO : e->key == 'y' ? H_REDO :
            e->key == 'c' && p->dialog.focus == H_GRID ? H_COPY : 0;
        if (p->dialog.focus == H_GRID && !command) return 0;
    }
    if (command) { int done = hex_action(p, command); hex_paint(p); return done; }
    tiku_dialog_result_t r = tiku_dialog_event(&p->dialog, e);
    if (r.activated >= 0 && hex_action(p, r.activated)) return 1;
    if (!p->pending) {
        if (e->type == TIKU_EVENT_POINTER_DOWN && e->button == 1) hex_pointer(p, e);
        if (e->type == TIKU_EVENT_WHEEL) {
            if (e->dy < 0) p->top = p->top > 3 ? p->top - 3 : 0;
            else if ((p->top + 3) * 16 < p->model.size) p->top += 3;
        }
        for (int i = 0; i < 2; i++) {
            if (r.taken && e->type == TIKU_EVENT_KEY_DOWN && e->key != TIKU_KEY_RETURN) continue;
            tiku_field_result_t result = tiku_field_event(&p->field[i], &p->input[i],
                e, hex_field(i), p->dialog.focus == H_OFFSET + i);
            if (result == TIKU_FIELD_COMMITTED) hex_action(p, i ? H_FIND : H_GO);
        }
        if (!r.taken && e->type == TIKU_EVENT_KEY_DOWN &&
            p->dialog.focus == H_GRID &&
            !(e->modifiers & ~(TIKU_MOD_SHIFT | TIKU_MOD_CAPS))) hex_key(p, e);
    }
    hex_paint(p); return 0;
}

/** @brief The file panel's answer: Save As writes the bytes there; Open
 *         opens it unless edits were made meanwhile; a cancel does
 *         nothing. */
static void hex_picked(void *state, uint32_t window, const char *path)
{
    hex_app_t *p = state; (void)window;
    if (!path || !*path || p->pending) return;
    if (p->pick_save) {
        int ok = hex_save(&p->model, path);
        snprintf(p->status, sizeof p->status, "%s", ok ? "Saved copy." : p->model.error);
    } else if (!p->model.changed) hex_open_file(p, path);
    else snprintf(p->status, sizeof p->status, "Open cancelled: edits were made while the picker was open.");
    hex_paint(p);
}

/** @brief The desktop closed the window: with edits unsaved, a new window
 *         opens on them, asking the unsaved-edits question. */
static void hex_closed(void *state, uint32_t window)
{
    hex_app_t *p = state;
    if (window != p->id || !p->model.changed) return;
    p->id = p->services->open(p->services->ctx, TIKU_PRODUCT_HEX_UNSAVED_TITLE, p->width, p->height);
    p->pending = H_CLOSE; hex_paint(p);
}

/** @brief Open the window, empty or on the document the app was started
 *         with, read-only. */
static int hex_start(void **state, const tiku_app_services_t *services)
{
    hex_app_t *p = calloc(1, sizeof *p); if (!p) return -1;
    p->services = services; p->readonly = 1; p->high = -1;
    p->cell = tiku_font_mono_cell(0); p->row = tiku_font_mono(0)->height + 6;
    p->width = 76 * p->cell + 32; if (p->width < 820) p->width = 820;
    p->height = 124 + HEX_ROWS * p->row + 114;
    p->surface = tiku_surface_new(p->width, p->height, TIKU_C_PANEL);
    if (!p->surface) { free(p); return -1; }
    p->id = services->open(services->ctx, TIKU_PRODUCT_HEX_NAME, p->width, p->height);
    if (!p->id) { tiku_surface_free(p->surface); free(p); return -1; }
    tiku_dialog_init(&p->dialog);
    for (int i = 0; i < 2; i++) { tiku_field_start(&p->field[i], i ? "" : "0x0"); tiku_field_input_init(&p->input[i]); }
    if (services->startup_document) hex_open_file(p, services->startup_document);
    if (services->help_page) services->help_page(services->ctx, "apps/hex.html");
    hex_controls(p); tiku_dialog_focus(&p->dialog, H_GRID);
    hex_paint(p); *state = p; return 0;
}

/** @brief Free the editor.  Unsaved bytes are first written to a new
 *         /tmp/TikuHex-recovery-* file, named on stderr; the document's own
 *         file is not touched. */
static void hex_stop(void *state)
{
    hex_app_t *p = state;
    if (p->model.changed) {
        char recovery[] = TIKU_PRODUCT_HEX_RECOVERY_PATH;
        int fd = mkstemp(recovery);
        if (fd >= 0) {
            size_t at = 0;
            while (at < p->model.size) {
                ssize_t n = write(fd, p->model.bytes + at, p->model.size - at);
                if (n <= 0) break;
                at += (size_t)n;
            }
            int ok = at == p->model.size && !fsync(fd);
            if (close(fd)) ok = 0;
            if (ok) fprintf(stderr, TIKU_PRODUCT_HEX_TIKUHEX_UNSAVED_BYTES_RECOVERED_TO_S_N, recovery);
            else { unlink(recovery); fprintf(stderr, TIKU_PRODUCT_HEX_TIKUHEX_RECOVERY_WRITE_FAILED_N); }
        }
    }
    hex_free(&p->model); tiku_surface_free(p->surface); free(p);
}

/** @brief A menu pick, carried out as a button's command.
 *  @return 1 to close. */
static int hex_pick(void *state, uint32_t window, int command)
{ (void)window; hex_app_t *p = state; int done = hex_action(p, command); hex_paint(p); return done; }

/** @brief TikuHex's descriptor, which the export below hands on. */
const tiku_app_descriptor_t tiku_hex_app = {
    .id = TIKU_PRODUCT_HEX_ID, .name = TIKU_PRODUCT_HEX_NAME, .start = hex_start,
    .stop = hex_stop, .event = hex_event, .pick = hex_pick,
    .picked = hex_picked, .closed = hex_closed
};
#ifdef TIKU_APP_SO
/** @brief The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t), &tiku_hex_app
};
#endif
