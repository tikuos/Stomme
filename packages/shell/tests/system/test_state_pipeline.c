#include <stdio.h>
#include <string.h>
#include "tiku_trk_state_pipeline.h"

static int accept_layout(const char *attr, void *ctx)
{ (void)ctx; return strcmp(attr, "_trk/columns") == 0; }

static int prefix(const char *attr, const void *in, size_t len, void *out,
                  size_t out_capacity, size_t *out_len, void *ctx)
{
    const char *p = ctx;
    size_t n = strlen(p);
    if (n + len > out_capacity) return -1;
    memcpy(out, p, n); memcpy((char *)out + n, in, len);
    *out_len = n + len; (void)attr; return 0;
}

int main(void)
{
    tiku_store_t *s = tiku_store_memory_open();
    char out[32] = "";
    if (s == NULL) return 1;
    if (tiku_state_write(s, "/source", "_trk/columns", "abc", 3) != 0 ||
        tiku_state_write(s, "/source", "_trk/frame", "xyz", 3) != 0 ||
        tiku_trk_state_pipeline_copy(s, "/source", "/dest", accept_layout,
                                      prefix, "v:") != 1 ||
        tiku_state_read(s, "/dest", "_trk/columns", out, sizeof out) != 5 ||
        memcmp(out, "v:abc", 5) != 0) return 1;
    tiku_store_free(s);
    puts("[PASS] state pipeline filters, transforms and reports writes");
    return 0;
}
