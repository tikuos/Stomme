/* Exact binary operations and native-editor input regressions. */
#include <assert.h>
#include <fcntl.h>
#include "../../apps/hex/tiku_hex.c"

static unsigned opened;
static char copied[16];
static uint32_t window_open(void *ctx, const char *title, int w, int h)
{ (void)ctx; (void)title; (void)w; (void)h; return ++opened; }
static int window_frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{ (void)ctx; (void)id; (void)px; (void)w; (void)h; return 1; }
static int window_menu(void *ctx, uint32_t id, const tiku_menuset_t *menus)
{
    (void)ctx; (void)id;
    for (int i = 0; i < menus->menu[0].nitem; i++) {
        if (menus->menu[0].item[i].sc) {
            assert(menus->menu[0].item[i].mods == TIKU_MOD_CMD);
        }
    }
    return 1;
}
static int copy(void *ctx, const char *text)
{ (void)ctx; snprintf(copied, sizeof copied, "%s", text); return 1; }

/** Write the fixture literally, including all zero and high-bit bytes. */
static void fixture(const char *path, const unsigned char *bytes, size_t size)
{
    FILE *f = fopen(path, "wb"); assert(f);
    assert(fwrite(bytes, 1, size, f) == size && !fclose(f));
}

/** Inspect disk independently rather than trusting the model's saved flag. */
static void disk_is(const char *path, const unsigned char *bytes, size_t size)
{
    unsigned char actual[512]; assert(size <= sizeof actual);
    FILE *f = fopen(path, "rb"); assert(f);
    assert(fread(actual, 1, sizeof actual, f) == size && !memcmp(actual, bytes, size));
    assert(!fclose(f));
}

/** Exercise real key events with explicit modifiers. */
static void key(hex_app_t *p, unsigned code, unsigned modifiers)
{
    tiku_event_t e = {.type = TIKU_EVENT_KEY_DOWN, .key = code, .modifiers = modifiers};
    assert(!hex_event(p, &e));
}

int main(void)
{
    char dir[] = "/tmp/tikuhex-test-XXXXXX", path[512], out[512], other[512];
    assert(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/binary; literal.bin", dir);
    snprintf(out, sizeof out, "%s/copy.bin", dir);
    snprintf(other, sizeof other, "%s/link.bin", dir);
    unsigned char bytes[512]; for (int i = 0; i < 512; i++) bytes[i] = (unsigned char)i;
    fixture(path, bytes, sizeof bytes);
    hex_model_t m = {0};
    assert(hex_load(&m, path) && m.size == 512 && !m.changed);
    assert(!memcmp(m.bytes, bytes, sizeof bytes));
    assert(hex_replace(&m, 0, 0xFF) && m.changed == 1);
    assert(!hex_replace(&m, 512, 0) && !hex_replace(&m, 0, 0xFF));
    assert(hex_undo(&m, 0) && !m.changed && hex_undo(&m, 1) && m.changed == 1);
    assert(hex_save(&m, m.path) && !m.changed && !strcmp(m.path, path));
    bytes[0] = 0xFF; disk_is(path, bytes, sizeof bytes);
    assert(hex_undo(&m, 0) && m.changed == 1 && hex_undo(&m, 1) && !m.changed);
    assert(hex_replace(&m, 1, 0x80));
    assert(hex_save(&m, out)); bytes[1] = 0x80; disk_is(out, bytes, sizeof bytes);
    assert(!hex_save(&m, path)); /* Save As never overwrites another file. */
    assert(hex_replace(&m, 2, 0xAA));
    unsigned char changed[512]; memcpy(changed, bytes, sizeof bytes); changed[4] = 123;
    fixture(out, changed, sizeof changed);
    assert(!hex_save(&m, out) && m.changed == 1); disk_is(out, changed, sizeof changed);
    assert(!symlink(path, other));
    assert(!hex_load(&m, other) && m.changed == 1);
    assert(!unlink(other) && !link(out, other));
    assert(hex_load(&m, out) && hex_replace(&m, 0, 5));
    assert(!hex_save(&m, out)); assert(!unlink(other));
    int fd = open(other, O_CREAT | O_RDWR, 0600); assert(fd >= 0);
    assert(!ftruncate(fd, HEX_LIMIT + 1) && !close(fd));
    assert(!hex_load(&m, other) && m.changed == 1); assert(!unlink(other));
    assert(!mkfifo(other, 0600)); assert(!hex_load(&m, other)); assert(!unlink(other));
    fixture(other, bytes, 0); assert(hex_load(&m, other) && !m.size);
    size_t at; assert(!hex_offset("0", 0, &at));
    assert(hex_offset("0xFF", 512, &at) && at == 255);
    assert(hex_offset("010", 512, &at) && at == 10);
    assert(!hex_offset("-1", 512, &at) && !hex_offset("512", 512, &at));
    assert(!hex_offset("0x", 512, &at) && !hex_offset("1garbage", 512, &at));
    unsigned char pattern[64]; size_t n;
    assert(hex_pattern("00 FF80", pattern, &n) && n == 3);
    assert(!hex_pattern("0 F", pattern, &n) && !hex_pattern("0xFF", pattern, &n));
    assert(hex_load(&m, path));
    assert(hex_pattern("FE FF", pattern, &n));
    assert(hex_find(&m, pattern, n) && m.cursor == 254);
    assert(hex_find(&m, pattern, n) && m.cursor == 510);
    assert(hex_find(&m, pattern, n) && m.cursor == 254);
    for (int i = 0; i < 700; i++) hex_replace(&m, 0, (unsigned char)i);
    assert(m.used == HEX_HISTORY); hex_free(&m);
    tiku_app_services_t services = {.open = window_open, .frame = window_frame,
        .menus = window_menu, .clip_set = copy, .startup_document = path};
    void *state; assert(!hex_start(&state, &services));
    hex_app_t *p = state;
    key(p, 'a', 0); assert(!p->model.changed && p->readonly);
    assert(!hex_action(p, H_MODE));
    key(p, 'a', 0); assert(p->high == 10 && !p->model.changed);
    key(p, 'b', 0); assert(p->model.bytes[0] == 0xAB && p->model.changed == 1);
    key(p, 'z', TIKU_MOD_CMD); assert(!p->model.changed);
    key(p, 'y', TIKU_MOD_CMD); assert(p->model.changed == 1);
    key(p, 'c', TIKU_MOD_CMD); assert(!strcmp(copied, "AB"));
    key(p, 'q', TIKU_MOD_CMD); assert(p->pending == H_CLOSE);
    key(p, TIKU_KEY_ESCAPE, 0); assert(!p->pending && p->model.changed == 1);
    unsigned was = opened; hex_closed(p, p->id);
    assert(opened == was + 1 && p->pending == H_CLOSE && p->model.changed);
    {
        /* A Save that fails under the question must say so on the foot
         * line: the question drawn again reads as though nothing
         * happened, and the edits are still unsaved. */
        char gone[600];
        snprintf(gone, sizeof gone, "%s/gone/x.bin", dir);
        char keep[sizeof p->model.path];
        snprintf(keep, sizeof keep, "%s", p->model.path);
        snprintf(p->model.path, sizeof p->model.path, "%s", gone);
        assert(!hex_action(p, H_SAVE));
        assert(p->pending == H_CLOSE && p->model.changed && p->save_failed);
        assert(!strcmp(hex_foot(p), p->status) &&
               strstr(p->status, p->model.error) == p->status &&
               strstr(p->status, "Discard edits"));
        snprintf(p->model.path, sizeof p->model.path, "%s", keep);
    }
    assert(!hex_action(p, H_CANCEL));
    assert(!p->save_failed && !strcmp(hex_foot(p), p->status));
    tiku_field_start(&p->field[0], "0x100");
    hex_controls(p); tiku_dialog_focus(&p->dialog, H_OFFSET);
    key(p, TIKU_KEY_RETURN, 0); assert(p->model.cursor == 256);
    hex_action(p, H_INPUT); tiku_dialog_focus(&p->dialog, H_GRID);
    key(p, 'Z', 0); assert(p->model.bytes[256] == 'Z');
    hex_paint(p);
    assert(!tiku_surface_png(p->surface, "/tmp/tikuhex-editor.png"));
    hex_action(p, H_CLOSE); assert(hex_action(p, H_DISCARD));
    assert(!p->model.changed); hex_stop(p);
    assert(!unlink(path) && !unlink(out) && !unlink(other) && !rmdir(dir));
    puts("PASS: binary IO, bounds, undo/redo, conflict-safe saves, search, input and close guards");
    return 0;
}
