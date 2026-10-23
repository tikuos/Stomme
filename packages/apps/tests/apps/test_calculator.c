/* Exact arithmetic, CRC vectors, native input and rendering without a device. */
#include <assert.h>
#include "../../apps/developer-calculator/tiku_calculator.c"

static uint32_t opened(void *ctx, const char *title, int w, int h)
{ (void)ctx; assert(!strcmp(title, "Developer Calculator")); assert(w == CALC_W && h == CALC_H); return 1; }
static int frames;
static int frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{ (void)ctx; (void)id; (void)px; (void)w; (void)h; frames++; return 1; }
static int menus(void *ctx, uint32_t id, const tiku_menuset_t *m)
{ (void)ctx; (void)id; assert(m->nmenu == 1); return 1; }
static void click(calculator_t *p, tiku_rect_t r)
{
    tiku_event_t e = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1, .x = r.x + 8, .y = r.y + 8};
    calc_event(p, &e); e.type = TIKU_EVENT_POINTER_UP; calc_event(p, &e);
}
int main(void)
{
    uint64_t n;
    assert(calc_parse("FFFFFFFFFFFFFFFF", 16, 64, &n) && n == UINT64_MAX);
    assert(calc_parse("18446744073709551615", 10, 64, &n) && n == UINT64_MAX);
    assert(!calc_parse("18446744073709551616", 10, 64, &n));
    assert(!calc_parse("100", 16, 8, &n)); assert(!calc_parse("-1", 10, 64, &n));
    assert(!calc_parse("0x", 16, 64, &n)); assert(!calc_parse("1junk", 10, 64, &n));
    char text[65];
    for (int width = 8; width <= 64; width *= 2) {
        for (unsigned base = 2; base <= 16; base += 2) {
            if (base != 2 && base != 8 && base != 10 && base != 16) continue;
            calc_format(calc_mask(width), base, text);
            assert(calc_parse(text, base, width, &n) && n == calc_mask(width));
        }
    }
    assert(calc_swap(0x12345678, 32) == 0x78563412);
    assert(calc_swap(UINT64_C(0x0123456789ABCDEF), 64) == UINT64_C(0xEFCDAB8967452301));
    assert(calc_extract(UINT64_MAX, 63, 0) == UINT64_MAX);
    assert(calc_extract(0xF0, 7, 4) == 15);
    assert(calc_crc32((const unsigned char *)"123456789", 9) == 0xCBF43926);
    assert(calc_crc16((const unsigned char *)"123456789", 9) == 0x29B1);
    assert(calc_crc32(NULL, 0) == 0 && calc_crc16(NULL, 0) == 0xFFFF);
    unsigned char data[256]; size_t length;
    assert(calc_bytes("00 FF 1234", data, &length) && length == 4 && data[1] == 255);
    assert(!calc_bytes("0 F", data, &length)); assert(!calc_bytes("GG", data, &length));
    tiku_app_services_t services = {.open=opened, .frame=frame, .menus=menus};
    void *state = NULL; assert(calc_start(&state, &services) == 0);
    calculator_t *p = state;
    assert(strstr(p->checksum, "CBF43926"));
    click(p, calc_bit_rect(0)); assert(p->value == 1);
    click(p, calc_bit_rect(31)); assert(p->value == UINT64_C(0x80000001));
    click(p, (tiku_rect_t){186,12,124,30}); assert(p->width == 64);
    click(p, calc_bit_rect(63)); assert(p->value == UINT64_C(0x8000000080000001));
    click(p, calc_bit_rect(0)); assert(p->value == UINT64_C(0x8000000080000000));
    assert(p->dialog.count == 75);
    click(p, (tiku_rect_t){186,12,124,30}); assert(p->width == 64);
    tiku_field_start(&p->fields[0], "FFFFFFFFFFFFFFFF"); calc_paint(p);
    assert(p->valid && p->value == UINT64_MAX);
    assert(!tiku_surface_png(p->surface, "/tmp/tiku-calculator.png"));
    tiku_font_set_size(16); calc_paint(p);
    assert(!tiku_surface_png(p->surface, "/tmp/tiku-calculator-large.png"));
    tiku_font_set_size(12);
    tiku_field_start(&p->fields[0], "XYZ"); calc_paint(p); assert(!p->valid);
    int old_base = p->base; calc_action(p, C_BASE); assert(p->base != old_base);
    /* Tab carries a tab character; the field it lands in refuses one. */
    p->status[0] = 0;
    tiku_event_t tab = {.type = TIKU_EVENT_KEY_DOWN, .key = TIKU_KEY_TAB};
    tab.text[0] = '\t';
    tiku_dialog_focus(&p->dialog, C_FIELD);
    assert(calc_event(p, &tab) == 0);
    assert(p->dialog.focus != C_FIELD && !p->status[0]);
    /* A width the value fits leaves no refusal standing behind it. */
    tiku_field_start(&p->fields[0], "FFFFFFFFFFFFFFFF");
    p->base = 16; p->width = 64; calc_paint(p);
    calc_action(p, C_WIDTH); assert(p->width == 64 && p->status[0]);
    tiku_field_start(&p->fields[0], "FF"); calc_paint(p);
    calc_action(p, C_WIDTH); assert(p->width == 8 && !p->status[0]);
    /* A bare hover answers nothing here, so it publishes nothing; a
     * move with a button held is the press tracking under it. */
    tiku_event_t hover = {.type = TIKU_EVENT_POINTER_MOVE, .x = 10, .y = 10};
    int before = frames;
    for (int i = 0; i < 10; i++) assert(calc_event(p, &hover) == 0);
    assert(frames == before);
    tiku_rect_t swap = {322, 12, 180, 30};
    tiku_event_t down = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                         .x = swap.x + 8, .y = swap.y + 8};
    calc_event(p, &down); before = frames;
    calc_event(p, &hover); assert(frames > before);
    down.type = TIKU_EVENT_POINTER_UP; calc_event(p, &down);
    tiku_event_t ev = {.type=TIKU_EVENT_KEY_DOWN, .key=TIKU_KEY_ESCAPE};
    assert(calc_event(p, &ev) == 1);
    calc_stop(p);
    puts("PASS: unsigned 8/16/32/64-bit conversion, overflow, byte order, bit ranges, CRC vectors and native controls");
    return 0;
}
