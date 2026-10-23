/* Terminal's parser and keys without a shell: byte sequences go straight
 * to feed(), and the bytes a key sends are read back from a pipe. */
#define TIKU_APP_EMBED
#include "../../apps/terminal/tiku_term.c"
#include <assert.h>

static int failures;

static int
frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{
    (void)ctx; (void)id; (void)px; (void)w; (void)h;
    return 1;
}

static tiku_app_services_t services = { .frame = frame };

static void
check(int cond, const char *what)
{
    printf("%s: %s\n", cond ? "ok" : "FAIL", what);
    if (!cond) {
        failures++;
    }
}

/** @brief A terminal state as term_start() leaves it, without a window. */
static term_state_t *
fresh(void)
{
    term_state_t *st = calloc(1, sizeof *st);

    assert(st != NULL);
    st->services = &services;
    st->master = -1;
    st->fg = 8u;
    st->bg = 8u;
    st->bottom = ROWS - 1;
    st->cursor_on = 1;
    st->cell_w = tiku_font_mono_cell(0);
    st->cell_h = tiku_font_mono(0)->height + 1;
    st->surface = tiku_surface_new(width_px(st), height_px(st), TIKU_C_DOC);
    assert(st->surface != NULL);
    grid_clear(st);
    return st;
}

static void
feed_str(term_state_t *st, const char *s)
{
    for (; *s != '\0'; s++) {
        feed(st, (unsigned char)*s);
    }
}

/** @brief Whether screen row @p r starts with @p text. */
static int
row_has(const term_state_t *st, int r, const char *text)
{
    int c;

    for (c = 0; text[c] != '\0'; c++) {
        if (st->cell[r][c].ch != text[c]) {
            return 0;
        }
    }
    return 1;
}

/** @brief Save at "ab", move away, print, restore, print: the last text
 *         lands right after "ab" on the first row. */
static void
save_restore(const char *save, const char *restore, const char *what)
{
    term_state_t *st = fresh();

    feed_str(st, "ab");
    feed_str(st, save);
    feed_str(st, "\r\nline two\x1b[10;20H");
    feed_str(st, restore);
    feed_str(st, "XY");
    check(row_has(st, 0, "abXY") && st->row == 0 && st->col == 4, what);
    tiku_surface_free(st->surface);
    free(st);
}

/** @brief The bytes the up and left arrows send, read from a pipe. */
static void
arrows(term_state_t *st, char *out, size_t size)
{
    int fds[2];
    tiku_event_t e;
    ssize_t n;

    assert(pipe(fds) == 0);
    st->master = fds[1];
    st->alive = 1;
    memset(&e, 0, sizeof e);
    e.type = TIKU_EVENT_KEY_DOWN;
    e.key = TIKU_KEY_UP;
    assert(term_event(st, &e) == 0);
    e.key = TIKU_KEY_LEFT;
    assert(term_event(st, &e) == 0);
    (void)close(fds[1]);
    n = read(fds[0], out, size - 1u);
    out[n > 0 ? n : 0] = '\0';
    (void)close(fds[0]);
    st->master = -1;
    st->alive = 0;
}

int
main(void)
{
    term_state_t *st;
    char sent[32];

    save_restore("\x1b" "7", "\x1b" "8", "ESC 7 and ESC 8 save and restore "
                 "the cursor");
    save_restore("\x1b[s", "\x1b[u", "CSI s and CSI u save and restore "
                 "the cursor");

    st = fresh();
    arrows(st, sent, sizeof sent);
    check(strcmp(sent, "\x1b[A\x1b[D") == 0,
          "arrows send ESC [ A and ESC [ D by default");
    feed_str(st, "\x1b[?1h\x1b=");
    arrows(st, sent, sizeof sent);
    check(strcmp(sent, "\x1bOA\x1bOD") == 0,
          "arrows send ESC O A and ESC O D after the vt100 smkx");
    feed_str(st, "\x1b[?1l\x1b>");
    arrows(st, sent, sizeof sent);
    check(strcmp(sent, "\x1b[A\x1b[D") == 0,
          "arrows send ESC [ A again after the vt100 rmkx");
    feed_str(st, "\x1b[?1h");
    (void)term_pick(st, 0u, CMD_RESET);
    arrows(st, sent, sizeof sent);
    check(strcmp(sent, "\x1b[A\x1b[D") == 0,
          "Reset returns the arrows to ESC [ A");
    feed_str(st, "\x1b[?1h\x1b" "c");
    arrows(st, sent, sizeof sent);
    check(strcmp(sent, "\x1b[A\x1b[D") == 0,
          "ESC c returns the arrows to ESC [ A");
    tiku_surface_free(st->surface);
    free(st);

    if (failures != 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all terminal checks passed\n");
    return 0;
}
