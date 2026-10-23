#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "tiku_trk_drop_protocol.h"
int main(void) {
    tiku_trk_foreign_offer_t o; tiku_trk_drop_result_t r;
    char path[256];
    const char text[] = "hello from a foreign offer\n";
    tiku_trk_foreign_offer_parse(&o, "copy move", "text/uri-list", 1);
    r = tiku_trk_foreign_drop(&o, 1, 0, 0, TIKU_TRK_DROP_ACTION_MOVE);
    if (!r.accepted || r.reply != TIKU_TRK_DROP_REPLY_MOVE_TARGET || !r.create_clipping) return 1;
    tiku_trk_foreign_offer_parse(&o, "trash", "text/uri-list", 0);
    r = tiku_trk_foreign_drop(&o, 0, 1, 0, 0);
    if (r.reply != TIKU_TRK_DROP_REPLY_TRASH_TARGET) return 1;
    r = tiku_trk_foreign_drop(&o, 1, 0, 1, 0);
    if (r.accepted) return 1;
    if (tiku_trk_foreign_clip_create("/tmp", "trk clipping", text,
            sizeof text - 1u, 0, path, sizeof path) != 0) return 1;
    if (access(path, F_OK) != 0) return 1;
    {
        FILE *f = fopen(path, "r");
        char got[64] = {0};
        if (f == NULL || fgets(got, sizeof got, f) == NULL ||
            strcmp(got, text) != 0) return 1;
        fclose(f);
    }
    unlink(path);
    puts("[PASS] foreign drop negotiation and promise replies"); return 0;
}
