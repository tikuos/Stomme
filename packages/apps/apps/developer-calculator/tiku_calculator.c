/* tiku_calculator.c -- Developer Calculator: an unsigned 8- to 64-bit
 * word in four bases with a grid of its bits, a bit-range extraction, and
 * CRC-32 and CRC-16 checksums of text or hex bytes.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_app_product.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tiku_app.h"
#include "tiku_app_resources.h"
#include "tiku_dialog.h"
#include "tiku_field_event.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"

#define CALC_W 820
#define CALC_H 650
enum { C_BASE = 1, C_WIDTH, C_SWAP, C_COPY, C_CRCMODE, C_CRCCOPY, C_CLOSE,
       C_FIELD = 20, C_BIT = 100 };
/** @brief The running application: its window, the four fields, and the
 *         value, the word width and the results they give. */
typedef struct {
    const tiku_app_services_t *services;
    tiku_surface_t *surface;
    uint32_t id;
    tiku_dialog_t dialog;
    tiku_field_t fields[4];
    tiku_field_input_t inputs[4];
    int base, width, valid, crc_hex;
    uint64_t value;
    char results[4][65], extraction[160], checksum[160], status[192];
} calculator_t;
#include "tiku_calculator_model.inl"

/** @brief The rectangle of field @p i: the value, the high and low bit of
 *         the extraction, and the CRC input. */
static tiku_rect_t calc_field_rect(int i)
{
    const tiku_rect_t rects[] = {{16, 54, 784, 30}, {114, 386, 64, 30},
        {246, 386, 64, 30}, {16, 476, 784, 30}};
    return rects[i];
}

/** @brief The button of bit @p bit: sixteen to a row, the highest bits
 *         first. */
static tiku_rect_t calc_bit_rect(int bit)
{
    return (tiku_rect_t){16 + (15 - bit % 16) * 49, 206 + (3 - bit / 16) * 40,
                         45, 32};
}

/** @brief Recompute the value's four forms, the bit extraction and the
 *         checksums from the fields. */
static void calc_update(calculator_t *p)
{
    p->valid =
        calc_parse(p->fields[0].text, (unsigned)p->base, p->width, &p->value);
    const unsigned bases[] = {16, 10, 8, 2};
    for (int i = 0; i < 4; i++) {
        if (p->valid) calc_format(p->value, bases[i], p->results[i]);
        else
            snprintf(p->results[i], sizeof p->results[i],
                     "Invalid or outside selected unsigned width");
    }
    uint64_t high, low;
    if (p->valid && calc_parse(p->fields[1].text, 10, 8, &high) &&
        calc_parse(p->fields[2].text, 10, 8, &low) &&
        high < (unsigned)p->width && low <= high)
        snprintf(p->extraction, sizeof p->extraction,
                 "Bits %u:%u = 0x%" PRIX64 " (%" PRIu64 ")", (unsigned)high,
                 (unsigned)low,
                 calc_extract(p->value, (unsigned)high, (unsigned)low),
                 calc_extract(p->value, (unsigned)high, (unsigned)low));
    else
        snprintf(p->extraction, sizeof p->extraction,
                 "Use 0 <= low <= high < word width.");
    unsigned char bytes[256]; size_t n = strlen(p->fields[3].text);
    if (p->crc_hex && !calc_bytes(p->fields[3].text, bytes, &n)) {
        snprintf(p->checksum, sizeof p->checksum,
                 "Invalid hex bytes: use pairs such as 00 FF 12.");
        return;
    }
    if (!p->crc_hex) memcpy(bytes, p->fields[3].text, n);
    snprintf(p->checksum, sizeof p->checksum,
             "%zu bytes | CRC-32/ISO-HDLC: %08" PRIX32
             " | CRC-16/CCITT-FALSE: %04X",
             n, calc_crc32(bytes, n), (unsigned)calc_crc16(bytes, n));
}

/** @brief Rebuild the window's controls: Swap, Copy and the bit buttons
 *         are enabled only for a valid value, and a bit only inside the
 *         word. */
static void calc_controls(calculator_t *p)
{
    tiku_dialog_begin(&p->dialog, TIKU_DIALOG_NONE, C_CLOSE);
    tiku_dialog_add(&p->dialog, C_BASE, (tiku_rect_t){16, 12, 158, 30},
                    TIKU_DIALOG_BUTTON, 1);
    tiku_dialog_add(&p->dialog, C_WIDTH, (tiku_rect_t){186, 12, 124, 30},
                    TIKU_DIALOG_BUTTON, 1);
    tiku_dialog_add(&p->dialog, C_SWAP, (tiku_rect_t){322, 12, 180, 30},
                    TIKU_DIALOG_BUTTON, p->valid);
    tiku_dialog_add(&p->dialog, C_COPY, (tiku_rect_t){514, 12, 164, 30},
                    TIKU_DIALOG_BUTTON, p->valid);
    tiku_dialog_add(&p->dialog, C_CLOSE, (tiku_rect_t){690, 12, 110, 30},
                    TIKU_DIALOG_BUTTON, 1);
    for (int i = 0; i < 4; i++)
        tiku_dialog_add(&p->dialog, C_FIELD + i, calc_field_rect(i),
                        TIKU_DIALOG_INPUT, 1);
    tiku_dialog_add(&p->dialog, C_CRCMODE, (tiku_rect_t){16, 434, 240, 30},
                    TIKU_DIALOG_BUTTON, 1);
    tiku_dialog_add(&p->dialog, C_CRCCOPY, (tiku_rect_t){268, 434, 170, 30},
                    TIKU_DIALOG_BUTTON, strncmp(p->checksum, "Invalid", 7));
    for (int bit = 63; bit >= 0; bit--)
        tiku_dialog_add(&p->dialog, C_BIT + bit, calc_bit_rect(bit),
                        TIKU_DIALOG_BUTTON, p->valid && bit < p->width);
    tiku_dialog_end(&p->dialog);
}

/** @brief Draw @p text in the plain face, clipped to @p width. */
static void calc_line(calculator_t *p, int x, int y, int width,
                      const char *text)
{
    tiku_text_line_draw(p->surface, (tiku_rect_t){x, y, width, 26},
                        TIKU_MEASURE_PLAIN, text, TIKU_C_TEXT);
}

/** @brief Recompute and paint the window: the value's forms, the bit
 *         grid, the extraction, the checksums and the status line, then
 *         hand the frame to the desktop. */
static void calc_paint(calculator_t *p)
{
    calc_update(p); calc_controls(p);
    tiku_fill(p->surface, (tiku_rect_t){0, 0, CALC_W, CALC_H}, TIKU_C_PANEL);
    const char *names[] = {"Hex", "Decimal", "Octal", "Binary"};
    for (int i = 0; i < 3; i++) {
        calc_line(p, 16 + i * 268, 90, 250, names[i]);
        calc_line(p, 16 + i * 268, 116, 250, p->results[i]);
    }
    calc_line(p, 16, 148, 784, p->results[3]);
    calc_line(
        p, 16, 176, 784,
        "Bit grid: high bits first; click to toggle. Labels show bit index : value.");
    calc_line(p, 16, 386, 96, "High bit:"); calc_line(p, 186, 386, 60, "Low:");
    calc_line(p, 326, 386, 474, p->extraction);
    calc_line(p, 16, 518, 784, p->checksum);
    calc_line(
        p, 16, 552, 784,
        "CRC input: literal UTF-8 bytes or hex byte pairs. No implicit newline is added.");
    calc_line(
        p, 16, 578, 784,
        "Unsigned 8 / 16 / 32 / 64-bit words. Overflow is rejected; byte swap is explicit.");
    calc_line(p, 16, 614, 784, p->status);
    for (int i = 0; i < p->dialog.count; i++) {
        const tiku_dialog_control_t *c = &p->dialog.control[i];
        unsigned flags = c->enabled ? 0 : TIKU_S_DISABLED; char label[64];
        if (tiku_dialog_focused(&p->dialog, c->id)) flags |= TIKU_S_FOCUS;
        if (tiku_dialog_pressed(&p->dialog, c->id)) flags |= TIKU_S_PRESSED;
        if (c->id >= C_FIELD && c->id < C_FIELD + 4) {
            int k = c->id - C_FIELD; tiku_field_t *f = &p->fields[k];
            tiku_ui_textfield_scroll(p->surface, c->rect, f->text, f->caret,
                f->sel_a, f->sel_b, flags, p->inputs[k].scroll); continue;
        }
        if (c->id >= C_BIT) {
            int bit = c->id - C_BIT;
            snprintf(label, sizeof label, "%d:%d", bit,
                     (int)((p->value >> bit) & 1));
            if (!p->valid) snprintf(label, sizeof label, "%d:-", bit);
            if (p->valid && ((p->value >> bit) & 1)) flags |= TIKU_S_DEFAULT;
        } else if (c->id == C_BASE)
            snprintf(label, sizeof label, "Input: base %d", p->base);
        else if (c->id == C_WIDTH)
            snprintf(label, sizeof label, "%d-bit word", p->width);
        else if (c->id == C_SWAP)
            snprintf(label, sizeof label, "Swap byte order");
        else if (c->id == C_COPY)
            snprintf(label, sizeof label, "Copy conversions");
        else if (c->id == C_CRCCOPY)
            snprintf(label, sizeof label, "Copy checksums");
        else if (c->id == C_CLOSE) snprintf(label, sizeof label, "Done");
        else
            snprintf(label, sizeof label, "CRC input: %s",
                     p->crc_hex ? "Hex bytes" : "UTF-8 text");
        tiku_ui_button(p->surface, c->rect, label, flags);
    }
    p->services->frame(p->services->ctx, p->id, p->surface->px, CALC_W, CALC_H);
}

/** @brief Put @p value in the value field, written in the current base. */
static void calc_set(calculator_t *p, uint64_t value)
{
    char text[65]; calc_format(value, (unsigned)p->base, text);
    tiku_field_start(&p->fields[0], text); tiku_field_input_init(&p->inputs[0]);
}

/** @brief Carry out a button or menu command; the word is not narrowed
 *         while the value does not fit it.  @return 1 to close. */
static int calc_action(calculator_t *p, int command)
{
    if (command == C_CLOSE) return 1;
    if (command == C_CRCMODE) p->crc_hex = !p->crc_hex;
    else if (command == C_CRCCOPY || command == C_COPY) {
        char text[512];
        if (command == C_COPY && !p->valid) return 0;
        if (command == C_CRCCOPY && !strncmp(p->checksum, "Invalid", 7))
            return 0;
        if (command == C_COPY)
            snprintf(text, sizeof text,
                     "Hex: %s\nDecimal: %s\nOctal: %s\nBinary: %s",
                     p->results[0], p->results[1], p->results[2],
                     p->results[3]);
        else snprintf(text, sizeof text, "%s", p->checksum);
        snprintf(p->status, sizeof p->status, "%s",
                 p->services->clip_set &&
                         p->services->clip_set(p->services->ctx, text)
                     ? "Copied to desktop clipboard."
                     : "Clipboard unavailable.");
    } else if (command == C_BASE) {
        p->base = p->base == 16   ? 10
                  : p->base == 10 ? 2
                  : p->base == 2  ? 8
                                  : 16;
        if (p->valid)
            calc_set(p, p->value);
        } else if (command == C_WIDTH) {
            int width = p->width == 64 ? 8 : p->width * 2;
            if (p->valid && p->value > calc_mask(width))
                snprintf(
                    p->status, sizeof p->status,
                    "Value does not fit %d bits. Change the value before narrowing.",
                    width);
            else { p->width = width; p->status[0] = 0; }
    } else if (p->valid) {
        if (command == C_SWAP) calc_set(p, calc_swap(p->value, p->width));
        else if (command >= C_BIT && command < C_BIT + p->width)
            calc_set(p, p->value ^ (UINT64_C(1) << (command - C_BIT)));
    }
    calc_paint(p); return 0;
}

/** @brief Whether a field is extending a selection under the held
 *         pointer. */
static int calc_dragging(const calculator_t *p)
{
    for (int i = 0; i < 4; i++) if (p->inputs[i].dragging) return 1;
    return 0;
}

/** @brief Every window event: the dialog's controls first, then the four
 *         fields; a bare hover draws nothing.  @return 1 to close. */
static int calc_event(void *state, const tiku_event_t *ev)
{
    calculator_t *p = state;
    if (ev->type == TIKU_EVENT_CLOSE) return 1;
    if (ev->type == TIKU_EVENT_POINTER_MOVE &&
        p->dialog.capture == TIKU_DIALOG_NONE && !calc_dragging(p)) {
        return 0;               /* nothing here answers a bare hover */
    }
    calc_controls(p);
    tiku_dialog_result_t r = tiku_dialog_event(&p->dialog, ev);
    if (r.activated != TIKU_DIALOG_NONE && calc_action(p, r.activated))
        return 1;
    /* A key the dialog took, other than Return, moved the focus and does
     * not reach the fields: Tab carries a character, which the field it
     * lands on refuses. */
    int steered = r.taken && ev->type == TIKU_EVENT_KEY_DOWN &&
                  ev->key != TIKU_KEY_RETURN;
    for (int i = 0; !steered && i < 4; i++)
        if (tiku_field_event(
                &p->fields[i], &p->inputs[i], ev, calc_field_rect(i),
                p->dialog.focus == C_FIELD + i) == TIKU_FIELD_REJECTED)
            snprintf(p->status, sizeof p->status,
                     "Input rejected: invalid text or more than 255 bytes.");
    calc_paint(p); return 0;
}

/** @brief Open the window on a 32-bit word in base 16, publish its
 *         Calculator menu, and focus the value field. */
static int calc_start(void **state, const tiku_app_services_t *services)
{
    calculator_t *p = calloc(1, sizeof *p); if (!p) return -1;
    p->services = services; p->base = 16; p->width = 32;
    p->surface = tiku_surface_new(CALC_W, CALC_H, TIKU_C_PANEL);
    if (!p->surface) { free(p); return -1; }
    p->id = services->open(
        services->ctx, TIKU_PRODUCT_DEVELOPER_CALCULATOR_NAME, CALC_W, CALC_H);
    if (services->help_page != NULL) {
        (void)services->help_page(services->ctx, "apps/calculator.html");
    }
    if (!p->id) { tiku_surface_free(p->surface); free(p); return -1; }
    const char *initial[] = {"0", "7", "0", "123456789"};
    for (int i = 0; i < 4; i++) {
        tiku_field_start(&p->fields[i], initial[i]);
        tiku_field_input_init(&p->inputs[i]);
    }
    tiku_dialog_init(&p->dialog);
    tiku_menuset_t menu = {0}; menu.nmenu = 1;
    snprintf(menu.menu[0].title, sizeof menu.menu[0].title, "Calculator");
    menu.menu[0].nitem = 1; menu.menu[0].item[0].command = C_CLOSE;
    menu.menu[0].item[0].enabled = 1;
    snprintf(menu.menu[0].item[0].label, sizeof menu.menu[0].item[0].label,
             "Close");
    services->menus(services->ctx, p->id, &menu);
    calc_paint(p); tiku_dialog_focus(&p->dialog, C_FIELD); calc_paint(p);
    *state = p; return 0;
}

/** @brief Free the window's surface and the state. */
static void calc_stop(void *state)
{ calculator_t *p = state; tiku_surface_free(p->surface); free(p); }

/** @brief A menu pick, carried out as a button's command. */
static int calc_pick(void *state, uint32_t window, int command)
{ (void)window; return calc_action(state, command); }

/** @brief Developer Calculator's descriptor, which the export below
 *         hands on. */
const tiku_app_descriptor_t tiku_calculator_app = {
    .id = TIKU_PRODUCT_DEVELOPER_CALCULATOR_ID,
    .name = TIKU_PRODUCT_DEVELOPER_CALCULATOR_NAME,
    .start = calc_start,
    .stop = calc_stop,
    .event = calc_event,
    .pick = calc_pick};
#ifdef TIKU_APP_SO
/** @brief The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t), &tiku_calculator_app
};
#endif

/** @brief Copy the calculator's current native model without manufacturing input gestures. */
static tiku_msg_t *resource_value(calculator_t *app)
{
 tiku_msg_t *value=tiku_app_resource_object(0);int bad=0;
 bad |= tiku_app_resource_member(value, "value",
                                 tiku_app_resource_counter_value(app->value));
 bad |= tiku_app_resource_member(
     value, "base",
     tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, app->base));
 bad |= tiku_app_resource_member(
     value, "width",
     tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, app->width));
 bad |= tiku_app_resource_member(
     value, "valid",
     tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, app->valid));
 bad |= tiku_app_resource_member(
     value, "checksum",
     tiku_resource_value_string(TIKU_RESOURCE_TEXT, app->checksum));
 tiku_msg_t *fields = tiku_app_resource_object(1);
 for (size_t i = 0; i < 4; i++)
     bad |= tiku_app_resource_item(
         fields,
         tiku_resource_value_string(TIKU_RESOURCE_TEXT, app->fields[i].text));
 bad |= tiku_app_resource_member(value, "fields", fields);
 if(bad){tiku_msg_free(value);return NULL;}return value;
}
/** @brief Capture native calculation state and its optimistic input precondition. */
static tiku_resource_error_t resource_capture(void *state,
                                              tiku_app_resources_t *owner,
                                              const char *path,
                                              tiku_resource_snapshot_t *out)
{
 (void)owner;if(strcmp(path,"calculation"))return TIKU_RESOURCE_UNSUPPORTED;
 tiku_msg_t *value = resource_value(state);
 char token[65];
 if (!value || tiku_app_resource_token(value, token) ||
     tiku_app_resource_member(
         value, "context",
         tiku_resource_value_string(TIKU_RESOURCE_TEXT, token))) {
     tiku_msg_free(value);
     return TIKU_RESOURCE_NO_MEMORY;
 }
 return tiku_app_resource_snapshot(out, value, "observed")
            ? TIKU_RESOURCE_NO_MEMORY
            : 0;
}
/** @brief Enumerate native state and the calculator's existing input controller. */
static int resource_catalog(void *state, tiku_app_resources_t *owner,
                            size_t index, tiku_resource_initial_t *out)
{
    if (index > 2)
        return 0;
    if (!index) {
        out->spec =
            (tiku_resource_spec_t){.path = "calculation",
                                   .kind = TIKU_RESOURCE_STATE,
                                   .schema_version = 1,
                                   .schema = {.type = TIKU_RESOURCE_OBJECT},
                                   .persistence = "none"};
        return resource_capture(state, owner, "calculation", &out->initial)
                   ? -1
                   : 1;
    }
    return tiku_app_resource_initial(
        out, (index == 1 ? "commands/button" : "commands/input"),
        TIKU_RESOURCE_ACTION, TIKU_RESOURCE_OBJECT,
        tiku_app_resource_object(0));
}
/** @brief Recheck native calculation state before forwarding one validated input. */
static tiku_resource_error_t
resource_change(void *state, tiku_app_resources_t *owner, const char *path,
                const tiku_msg_t *value, int invoke,
                tiku_resource_snapshot_t *out, tiku_resource_effect_t *effect)
{
    if (!invoke)
        return TIKU_RESOURCE_UNSUPPORTED;
    calculator_t *app = state;
    char expected[65], actual[65];
    tiku_msg_t *current = resource_value(app);
    int error = !current || tiku_app_resource_token(current, actual);
    tiku_msg_free(current);
    if (error)
        return TIKU_RESOURCE_UNAVAILABLE;
    if (!tiku_app_resource_text(value, "context", expected, sizeof expected))
        return TIKU_RESOURCE_VALUE;
    if (strcmp(expected, actual))
        return TIKU_RESOURCE_STALE;
    if (!strcmp(path, "commands/button")) {
        const char *keys[] = {"context", "command"};
        int64_t command;
        if (!tiku_app_resource_keys(value, keys, 2) ||
            !tiku_app_resource_integer(value, "command", 1, C_BIT + 63,
                                       &command) ||
            command == C_CLOSE || (command > C_CRCCOPY && command < C_BIT))
            return TIKU_RESOURCE_VALUE;
        calc_controls(app);
        int enabled = 0;
        for (int i = 0; i < app->dialog.count; i++)
            if (app->dialog.control[i].id == command)
                enabled = app->dialog.control[i].enabled;
        if (!enabled)
            return TIKU_RESOURCE_UNSUPPORTED;
        calc_action(app, (int)command);
    } else if (!strcmp(path, "commands/input")) {
        const char *keys[] = {"context", "field", "text"};
        int64_t field;
        char text[TIKU_FIELD_MAX];
        if (!tiku_app_resource_keys(value, keys, 3) ||
            !tiku_app_resource_integer(value, "field", 0, 3, &field) ||
            !tiku_app_resource_text(value, "text", text, sizeof text))
            return TIKU_RESOURCE_VALUE;
        tiku_field_t next = app->fields[field];
        tiku_field_key(&next, TIKU_FIELD_SELECT_ALL, 0, 0);
        if (tiku_field_commit(&next, text) != TIKU_FIELD_HANDLED)
            return TIKU_RESOURCE_VALUE;
        app->fields[field] = next;
        tiku_field_input_init(&app->inputs[field]);
        calc_update(app);
        calc_paint(app);
    } else
        return TIKU_RESOURCE_UNSUPPORTED;
    *effect = TIKU_RESOURCE_EFFECT_PARTIAL;
    return resource_capture(state, owner, "calculation", out);
}
/** @brief Optional versioned native application resource callbacks. */
__attribute__((visibility("default")))
const tiku_app_resource_export_t tiku_app_resources_v1 = {
    .version = 1,
    .size = sizeof(tiku_app_resource_export_t),
    .catalog = resource_catalog,
    .capture = resource_capture,
    .change = resource_change};
