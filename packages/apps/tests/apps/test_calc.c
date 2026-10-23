/* The plain calculator's arithmetic, keys and window without a device. */
#include <assert.h>
#include "../../apps/calculator/tiku_calc.c"

static uint32_t opened(void *ctx, const char *title, int w, int h)
{ (void)ctx; assert(!strcmp(title, "Calculator")); assert(w == CALC_W && h == CALC_H); return 1; }
static int frames;
static int frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{ (void)ctx; (void)id; (void)px; (void)w; (void)h; frames++; return 1; }
static int menus(void *ctx, uint32_t id, const tiku_menuset_t *m)
{ (void)ctx; (void)id; assert(m->nmenu == 1 && m->menu[0].nitem == 2); return 1; }
static char copied[64];
static int clip_set(void *ctx, const char *text)
{ (void)ctx; snprintf(copied, sizeof copied, "%s", text); return 1; }

/** @brief Press keys as a string and read the display. */
static const char *run(calc_model_t *m, const char *keys)
{
    calc_clear(m);
    for (; *keys; keys++) calc_key(m, *keys);
    return m->entry;
}

int main(void)
{
    calc_model_t m;
    assert(!strcmp(run(&m, ""), "0"));
    assert(!strcmp(run(&m, "12+3="), "15"));
    assert(!strcmp(run(&m, "0.1+0.2="), "0.3"));
    assert(!strcmp(run(&m, "9-4="), "5"));
    assert(!strcmp(run(&m, "6*7="), "42"));
    assert(!strcmp(run(&m, "1/4="), "0.25"));
    assert(!strcmp(run(&m, "2+3*4="), "20"));        /* left to right */
    assert(!strcmp(run(&m, "5+*2="), "10"));         /* the last operator wins */
    assert(!strcmp(run(&m, "007"), "7"));
    assert(!strcmp(run(&m, "1.2.3"), "1.23"));
    assert(!strcmp(run(&m, "123b"), "12"));
    assert(!strcmp(run(&m, "1b"), "0"));
    assert(!strcmp(run(&m, "5n"), "-5"));
    assert(!strcmp(run(&m, "5nn"), "5"));
    assert(!strcmp(run(&m, "50%"), "0.5"));
    assert(!strcmp(run(&m, "200*50%="), "100"));
    assert(!strcmp(run(&m, "5/0="), "Error"));
    assert(!strcmp(run(&m, "5/0=7"), "Error"));      /* only C answers */
    assert(!strcmp(run(&m, "5/0=c7"), "7"));
    assert(!strcmp(run(&m, "8="), "8"));
    assert(!strcmp(run(&m, "8=+1="), "9"));
    assert(!strcmp(run(&m, "12345678901234567890"), "123456789012345"));

    tiku_app_services_t services = {.open = opened, .frame = frame,
                                    .menus = menus, .clip_set = clip_set};
    void *state = NULL;
    assert(tiku_calc_app.start(&state, &services) == 0 && state != NULL);
    calc_t *p = state;
    assert(frames >= 1);
    /* Clicks on the keypad and typed keys reach the same model. */
    tiku_rect_t seven = calc_key_rect('7');
    tiku_event_t e = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                      .x = seven.x + 8, .y = seven.y + 8};
    assert(!tiku_calc_app.event(p, &e));
    e.type = TIKU_EVENT_POINTER_UP; assert(!tiku_calc_app.event(p, &e));
    assert(!strcmp(p->model.entry, "7"));
    tiku_event_t k = {.type = TIKU_EVENT_KEY_DOWN, .text = "+"};
    assert(!tiku_calc_app.event(p, &k));
    snprintf(k.text, sizeof k.text, "5"); assert(!tiku_calc_app.event(p, &k));
    k.text[0] = 0; k.key = TIKU_KEY_RETURN; assert(!tiku_calc_app.event(p, &k));
    assert(!strcmp(p->model.entry, "12"));
    assert(!tiku_surface_png(p->surface, "/tmp/tiku-calc.png"));
    assert(!tiku_calc_app.pick(p, 1, C_COPY) && !strcmp(copied, "12"));
    k.key = TIKU_KEY_ESCAPE; assert(!tiku_calc_app.event(p, &k));
    assert(!strcmp(p->model.entry, "0"));
    assert(calc_key_rect('0').w > calc_key_rect('1').w);
    assert(tiku_calc_app.pick(p, 1, C_CLOSE) == 1);
    tiku_calc_app.stop(p);
    puts("PASS: Calculator arithmetic, keys, clipboard and window");
    return 0;
}
