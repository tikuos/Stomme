#include "tiku_shell_product.h"
/* The desktop process is not added to its own application roster. */
#include "../../system/deskbar/tiku_trk_deskbar_roster.c"
int main(void)
{
    struct roster_pass pass = {0};
    pass.deskbar = tiku_trk_deskbar_new(NULL);
    tiku_sys_process_t process = {.pid = 42, .name = TIKU_SHELL_DESKTOP_NAME,
                                  .command = TIKU_SHELL_DESKTOP_NAME};
    roster_note(&process, &pass);
    int failed = tiku_trk_deskbar_find_team(pass.deskbar, TIKU_SHELL_DESKTOP_NAME) >= 0;
    tiku_trk_deskbar_free(pass.deskbar);
    puts(failed ? "FAIL D-077" : "PASS D-077"); return failed;
}
