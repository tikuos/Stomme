/* External add-ons leave no unreaped child owned by the desktop. */
#include "tiku_trk_addon.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
int main(void)
{
    tiku_trk_addon_t addon = {0};
    addon.external = 1;
    strcpy(addon.command, "/bin/sleep");
    const char *args[] = {"0.05"};
    assert(tiku_trk_addons_run_external(&addon, args, 1) == 0);
    usleep(100000);
    int status;
    pid_t child = waitpid(-1, &status, WNOHANG);
    int ok = child == -1 && errno == ECHILD;
    puts(ok ? "PASS D-031" : "FAIL D-031");
    return !ok;
}
