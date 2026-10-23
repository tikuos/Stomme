/* tiku_calc.c -- Calculator: a pocket calculator's keypad in one window,
 * with the four operations, percent, negate and backspace.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_app_product.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tiku_app.h"
#include "tiku_dialog.h"
#include "tiku_font.h"
#include "tiku_ui.h"

#define CALC_W 240
#define CALC_H 342
#define CALC_KEY 100            /* a keypad button's id is CALC_KEY + its key */
enum { C_CLOSE = 1, C_COPY };
/** @brief The arithmetic's state, as the keys leave it. */
typedef struct {
    double acc;                 /* the running result                  */
    char   entry[24];           /* what the display shows              */
    int    op;                  /* the pending operator, 0 for none    */
    int    fresh;               /* the next digit starts a new entry   */
    int    error;               /* a division by zero or an overflow:  */
                                /* only C answers                      */
} calc_model_t;
#include "tiku_calc_model.inl"

/** @brief The running application: its window, keypad and arithmetic. */
typedef struct {
    const tiku_app_services_t *services;
    tiku_surface_t *surface;
    uint32_t id;
    tiku_dialog_t dialog;
    calc_model_t model;
} calc_t;

/* The keypad, row by row; a space is the wide zero's second cell. */
static const char calc_pad[5][4] = {
    {'c', 'n', '%', '/'}, {'7', '8', '9', '*'}, {'4', '5', '6', '-'},
    {'1', '2', '3', '+'}, {'0', ' ', '.', '='}
};

/** @brief Where a keypad key sits; the zero spans two cells. */
static tiku_rect_t calc_key_rect(char key)
{
    for (int row = 0; row < 5; row++)
        for (int col = 0; col < 4; col++)
            if (calc_pad[row][col] == key)
                return (tiku_rect_t){ 12 + col * 56, 70 + row * 52,
                                      key == '0' ? 104 : 48, 44 };
    return (tiku_rect_t){ 0, 0, 0, 0 };
}

/** @brief Rebuild the keypad's controls; equals is the default button. */
static void calc_controls(calc_t *p)
{
    tiku_dialog_begin(&p->dialog, CALC_KEY + '=', TIKU_DIALOG_NONE);
    for (int row = 0; row < 5; row++)
        for (int col = 0; col < 4; col++) {
            char key = calc_pad[row][col];
            if (key != ' ')
                tiku_dialog_add(&p->dialog, CALC_KEY + key, calc_key_rect(key),
                                TIKU_DIALOG_BUTTON, 1);
        }
    tiku_dialog_end(&p->dialog);
}

/** @brief The display and the keypad, then the frame to the desktop. */
static void calc_paint(calc_t *p)
{
    const tiku_font_t *font = tiku_font_bold();
    tiku_rect_t well = { 12, 12, CALC_W - 24, 44 };

    calc_controls(p);
    tiku_fill(p->surface, (tiku_rect_t){ 0, 0, CALC_W, CALC_H }, TIKU_C_PANEL);
    tiku_ui_sunken(p->surface, well, TIKU_RGB(236, 240, 232));
    tiku_text(p->surface, font,
              well.x + well.w - 10 - tiku_text_width(font, p->model.entry),
              well.y + (well.h - font->height) / 2 + font->ascent,
              p->model.entry, TIKU_C_TEXT);
    for (int i = 0; i < p->dialog.count; i++) {
        const tiku_dialog_control_t *c = &p->dialog.control[i];
        char key = (char)(c->id - CALC_KEY), label[8];
        unsigned flags = 0;

        if (tiku_dialog_focused(&p->dialog, c->id)) flags |= TIKU_S_FOCUS;
        if (tiku_dialog_pressed(&p->dialog, c->id)) flags |= TIKU_S_PRESSED;
        if (key == '=') flags |= TIKU_S_DEFAULT;
        snprintf(label, sizeof label, "%s",
                 key == 'c' ? "C" : key == 'n' ? "\xc2\xb1" :
                 key == '/' ? "\xc3\xb7" : key == '*' ? "\xc3\x97" :
                 (char[]){ key, 0 });
        tiku_ui_button(p->surface, c->rect, label, flags);
    }
    p->services->frame(p->services->ctx, p->id, p->surface->px, CALC_W, CALC_H);
}

/** @brief A menu row or a keypad button; @return 1 to close. */
static int calc_action(calc_t *p, int command)
{
    if (command == C_CLOSE) return 1;
    if (command == C_COPY) {
        if (p->services->clip_set != NULL)
            (void)p->services->clip_set(p->services->ctx, p->model.entry);
    } else if (command >= CALC_KEY) {
        calc_key(&p->model, (char)(command - CALC_KEY));
    }
    calc_paint(p);
    return 0;
}

/** @brief Typed keys reach the keypad: digits, operators, Return,
 *         Backspace and Escape. */
static int calc_typed(calc_t *p, const tiku_event_t *ev)
{
    char key = 0;

    if (ev->key == TIKU_KEY_RETURN) key = '=';
    else if (ev->key == TIKU_KEY_BACKSPACE) key = 'b';
    else if (ev->key == TIKU_KEY_ESCAPE) key = 'c';
    else if (ev->text[0] != '\0' && ev->text[1] == '\0' &&
             strchr("0123456789.+-*/=%", ev->text[0]) != NULL)
        key = ev->text[0];
    else if (strcmp(ev->text, "\xc3\xb7") == 0) key = '/';
    else if (strcmp(ev->text, "\xc3\x97") == 0) key = '*';
    if (key == 0) return 0;
    calc_key(&p->model, key);
    return 1;
}

/** @brief Pointer and keys go to the keypad; a bare hover draws nothing. */
static int calc_event(void *state, const tiku_event_t *ev)
{
    calc_t *p = state;

    if (ev->type == TIKU_EVENT_CLOSE) return 1;
    if (ev->type == TIKU_EVENT_POINTER_MOVE &&
        p->dialog.capture == TIKU_DIALOG_NONE) return 0;
    calc_controls(p);
    if (ev->type == TIKU_EVENT_KEY_DOWN && ev->key != TIKU_KEY_TAB &&
        calc_typed(p, ev)) {
        calc_paint(p);
        return 0;
    }
    tiku_dialog_result_t r = tiku_dialog_event(&p->dialog, ev);
    if (r.activated != TIKU_DIALOG_NONE && calc_action(p, r.activated)) return 1;
    calc_paint(p);
    return 0;
}

/** @brief Open the one window and publish its menu. */
static int calc_start(void **state, const tiku_app_services_t *services)
{
    calc_t *p = calloc(1, sizeof *p);
    tiku_menuset_t menu;

    if (p == NULL) return -1;
    p->services = services;
    calc_clear(&p->model);
    p->surface = tiku_surface_new(CALC_W, CALC_H, TIKU_C_PANEL);
    if (p->surface == NULL) { free(p); return -1; }
    p->id = services->open(services->ctx, TIKU_PRODUCT_CALCULATOR_NAME, CALC_W, CALC_H);
    if (!p->id) { tiku_surface_free(p->surface); free(p); return -1; }
    if (services->help_page != NULL)
        (void)services->help_page(services->ctx, "apps/calc.html");
    tiku_dialog_init(&p->dialog);
    memset(&menu, 0, sizeof menu);
    menu.nmenu = 1;
    snprintf(menu.menu[0].title, sizeof menu.menu[0].title, TIKU_PRODUCT_CALCULATOR_NAME);
    menu.menu[0].nitem = 2;
    menu.menu[0].item[0].command = C_COPY;
    menu.menu[0].item[0].enabled = 1;
    snprintf(menu.menu[0].item[0].label, sizeof menu.menu[0].item[0].label,
             "Copy result");
    menu.menu[0].item[1].command = C_CLOSE;
    menu.menu[0].item[1].enabled = 1;
    snprintf(menu.menu[0].item[1].label, sizeof menu.menu[0].item[1].label,
             "Close");
    services->menus(services->ctx, p->id, &menu);
    calc_paint(p);
    *state = p;
    return 0;
}

/** @brief Free the window's surface and the state. */
static void calc_stop(void *state)
{
    calc_t *p = state;

    tiku_surface_free(p->surface);
    free(p);
}

/** @brief A menu pick is an action like any button's. */
static int calc_pick(void *state, uint32_t window, int command)
{
    (void)window;
    return calc_action(state, command);
}

/** @brief Calculator's descriptor, which the export below hands on. */
const tiku_app_descriptor_t tiku_calc_app = {
    .id = TIKU_PRODUCT_CALCULATOR_ID, .name = TIKU_PRODUCT_CALCULATOR_NAME, .start = calc_start,
    .stop = calc_stop, .event = calc_event, .pick = calc_pick
};
#ifdef TIKU_APP_SO
/** @brief The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, (uint32_t)sizeof(tiku_app_descriptor_t), &tiku_calc_app
};
#endif
