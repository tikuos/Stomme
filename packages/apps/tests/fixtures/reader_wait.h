/* Headless equivalent of Reader's event-loop worker collection. */
#include "tiku_work.h"
#include <unistd.h>
static void reader_wait(tiku_reader_help_window_t *c)
{
    for (int i = 0; i < 10000; i++) {
        tiku_work_collect();
        if (!c->reader->search) return;
        usleep(1000);
    }
    assert(!"Reader search did not finish");
}
