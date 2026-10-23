/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_sys_darwin.c - the machine underneath, as Darwin describes it.
 *
 * The Darwin definitions of tiku_sys.h; mk/kits.mk compiles this file
 * or tiku_sys_linux.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_sys.h"
#include <sys/stat.h>

#include "tiku_sys_pinned.h"

#include <dirent.h>
#include <fcntl.h>
#include <xlocale.h>
#include <errno.h>
#include <libproc.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/event.h>
#include <sys/ioctl.h>
#include <sys/proc_info.h>
#include <sys/socket.h>
#include <sys/ucred.h>
#include <IOKit/serial/ioss.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/ucontext.h>
#include <time.h>
#include <unistd.h>

int
tiku_sys_stat_same(const struct stat *a, const struct stat *b)
{
    return a && b && a->st_dev == b->st_dev && a->st_ino == b->st_ino &&
        a->st_size == b->st_size && a->st_mode == b->st_mode &&
        a->st_mtimespec.tv_sec == b->st_mtimespec.tv_sec &&
        a->st_mtimespec.tv_nsec == b->st_mtimespec.tv_nsec &&
        a->st_ctimespec.tv_sec == b->st_ctimespec.tv_sec &&
        a->st_ctimespec.tv_nsec == b->st_ctimespec.tv_nsec;
}

/* The UTF-8 locales tried for reading text, in order: C.UTF-8 orders
 * characters by code point, and en_US.UTF-8 stands in where the host has
 * no C.UTF-8. */
static const char *const utf8_locales[] = { "C.UTF-8", "en_US.UTF-8" };

void *
tiku_sys_locale_utf8(void)
{
    locale_t locale = (locale_t)0;
    size_t i;

    for (i = 0; locale == (locale_t)0 &&
                i < sizeof utf8_locales / sizeof utf8_locales[0]; i++) {
        locale = newlocale(LC_CTYPE_MASK | LC_COLLATE_MASK, utf8_locales[i],
                           (locale_t)0);
    }
    return locale;
}

void *
tiku_sys_locale_use(void *locale)
{
    return uselocale((locale_t)locale);
}

void
tiku_sys_locale_free(void *locale)
{
    if (locale != NULL) {
        freelocale((locale_t)locale);
    }
}

int tiku_sys_sync_file(int fd)
{
    if (fsync(fd)) return -1;
    return fcntl(fd, F_FULLFSYNC);
}

void *
tiku_sys_fault_pc(const void *context)
{
    const ucontext_t *uc = context;

    if (uc == NULL || uc->uc_mcontext == NULL) {
        return NULL;
    }
#if defined(__arm64__) || defined(__aarch64__)
    /* The accessor strips the pointer-authentication code an arm64e pc
     * carries in its top bits. */
    return (void *)(uintptr_t)
        __darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss);
#elif defined(__x86_64__)
    return (void *)(uintptr_t)uc->uc_mcontext->__ss.__rip;
#else
    /* Another architecture: no program counter. */
    return NULL;
#endif
}

int
tiku_sys_processes(void (*fn)(const tiku_sys_process_t *process, void *ctx),
                   void *ctx)
{
    uid_t me = getuid();
    pid_t *pids;
    int bytes, n, i, offered = 0;

    if (fn == NULL) {
        return -1;
    }
    /* proc_listpids() counts in bytes: the first call gives the size,
     * the second fills a buffer of it. */
    bytes = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
    if (bytes <= 0) {
        return -1;
    }
    pids = malloc((size_t)bytes);
    if (pids == NULL) {
        return -1;
    }
    bytes = proc_listpids(PROC_ALL_PIDS, 0, pids, bytes);
    if (bytes <= 0) {
        free(pids);
        return -1;
    }
    n = bytes / (int)sizeof *pids;
    for (i = 0; i < n; i++) {
        struct proc_bsdshortinfo brief;
        char command[PROC_PIDPATHINFO_MAXSIZE];
        char name[64];
        tiku_sys_process_t process;

        if (pids[i] <= 0) {
            continue;
        }
        /* The short form gives the owner and the name in one call. */
        if (proc_pidinfo(pids[i], PROC_PIDT_SHORTBSDINFO, 0, &brief,
                         sizeof brief) != (int)sizeof brief ||
            brief.pbsi_uid != me) {
            continue;
        }
        /* A process with no executable path is a kernel thread here. */
        if (proc_pidpath(pids[i], command, sizeof command) <= 0) {
            continue;
        }
        snprintf(name, sizeof name, "%.*s", (int)sizeof brief.pbsi_comm,
                 brief.pbsi_comm);
        if (name[0] == '\0') {
            continue;
        }
        process.pid = (long)pids[i];
        process.name = name;
        process.command = command;
        fn(&process, ctx);
        offered++;
    }
    free(pids);
    return offered;
}

void
tiku_sys_memory(long *total_kb, long *avail_kb)
{
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_statistics64_data_t vm;
    uint64_t total = 0;
    size_t room = sizeof total;

    *total_kb = 0;
    *avail_kb = 0;
    if (sysctlbyname("hw.memsize", &total, &room, NULL, 0) == 0) {
        *total_kb = (long)(total / 1024u);
    }
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          (host_info64_t)&vm, &count) == KERN_SUCCESS) {
        /* Free, inactive and purgeable pages, the nearest to Linux's
         * MemAvailable. */
        uint64_t pages = (uint64_t)vm.free_count +
                         (uint64_t)vm.inactive_count +
                         (uint64_t)vm.purgeable_count;

        *avail_kb = (long)(pages * (uint64_t)vm_kernel_page_size / 1024u);
    }
}

long
tiku_sys_uptime(void)
{
    struct timeval boot;
    size_t room = sizeof boot;
    time_t now;

    if (sysctlbyname("kern.boottime", &boot, &room, NULL, 0) != 0 ||
        boot.tv_sec == 0) {
        return 0;
    }
    now = time(NULL);
    return (now > boot.tv_sec) ? (long)(now - boot.tv_sec) : 0;
}

int
tiku_sys_self_path(char *out, size_t max)
{
    char raw[PROC_PIDPATHINFO_MAXSIZE];
    uint32_t room = (uint32_t)sizeof raw;
    char real[PROC_PIDPATHINFO_MAXSIZE];

    if (_NSGetExecutablePath(raw, &room) != 0) {
        out[0] = '\0';
        return -1;
    }
    /* _NSGetExecutablePath() gives the path as it was invoked; realpath()
     * resolves it to the file on the disk. */
    if (realpath(raw, real) != NULL) {
        snprintf(out, max, "%s", real);
    } else {
        snprintf(out, max, "%s", raw);
    }
    return 0;
}

void
tiku_sys_frame_name(const char *raw, char *out, size_t max)
{
    static char self[PROC_PIDPATHINFO_MAXSIZE];
    char image[64] = "", address[32] = "", symbol[160] = "";
    const char *leaf;
    int index = 0;

    /*
     * backtrace_symbols_fd() writes the frame's number, image and address,
     * then the symbol and its offset.  The image is printed only when the
     * frame is not this program's own.
     */
    if (sscanf(raw, "%d %63s %31s %159[^\n]", &index, image, address,
               symbol) != 4) {
        snprintf(out, max, "%.100s", raw);
        return;
    }
    /* The program's own path, asked for once and kept. */
    if (self[0] == '\0' && tiku_sys_self_path(self, sizeof self) != 0) {
        self[0] = '\1';        /* asked, and it would not say */
    }
    leaf = strrchr(self, '/');
    leaf = (leaf != NULL) ? leaf + 1 : self;
    if (leaf[0] != '\0' && strcmp(leaf, image) == 0) {
        snprintf(out, max, "%.119s", symbol);
    } else {
        snprintf(out, max, "%s: %.100s", image, symbol);
    }
}

/*
 * The pinned processes.  A handle is a kqueue watching the process's
 * exit, close-on-exec and the caller's to close.  The process is known by
 * its pid and its start time in microseconds, which a later process given
 * the same pid does not share.
 */
static tiku_sys_pinned_t pinned[TIKU_SYS_PINNED_MAX];

/** @brief When @p pid began, or -1 when there is no such process now. */
static long long
began(pid_t pid)
{
    struct proc_bsdinfo info;

    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof info) !=
        (int)sizeof info) {
        return -1;
    }
    return (long long)info.pbi_start_tvsec * 1000000 +
           (long long)info.pbi_start_tvusec;
}

int
tiku_sys_process_pin(int fd, long *pid)
{
    struct xucred cred;
    socklen_t size = sizeof cred;
    pid_t peer = 0;
    socklen_t psize = sizeof peer;
    long long start;
    struct kevent watch;
    int kq;

    if (getsockopt(fd, SOL_LOCAL, LOCAL_PEERCRED, &cred, &size) != 0) {
        return -1;
    }
    if (getsockopt(fd, SOL_LOCAL, LOCAL_PEERPID, &peer, &psize) != 0) {
        return -1;
    }
    /* Refuse init, this process, and another user's process. */
    if (cred.cr_version != XUCRED_VERSION || cred.cr_uid != geteuid() ||
        peer <= 1 || peer == getpid()) {
        errno = EPERM;
        return -1;
    }
    start = began(peer);
    if (start < 0) {
        errno = ESRCH;
        return -1;
    }
    kq = kqueue();
    if (kq < 0) {
        return -1;
    }
    /* kevent() refuses a process that has already exited. */
    EV_SET(&watch, (uintptr_t)peer, EVFILT_PROC, EV_ADD, NOTE_EXIT, 0, NULL);
    if (kevent(kq, &watch, 1, NULL, 0, NULL) != 0 ||
        fcntl(kq, F_SETFD, FD_CLOEXEC) < 0) {
        int keep = errno;

        (void)close(kq);
        errno = keep;
        return -1;
    }
    if (tiku_sys_pinned_add(pinned, kq, (long)peer, start) != 0) {
        (void)close(kq);
        errno = ENOMEM;
        return -1;
    }
    if (pid != NULL) {
        *pid = (long)peer;
    }
    return kq;
}

int
tiku_sys_process_kill(int handle)
{
    tiku_sys_pinned_t *row = tiku_sys_pinned_find(pinned, handle);

    if (row == NULL) {
        /* A handle this file did not issue, such as a copy of one:
         * nothing is killed. */
        errno = ESRCH;
        return -1;
    }
    if (began((pid_t)row->pid) != row->started) {
        /* The pinned process has exited, and its pid may now name
         * another. */
        row->handle = 0;
        errno = ESRCH;
        return -1;
    }
    if (kill((pid_t)row->pid, SIGKILL) != 0) {
        return -1;
    }
    row->handle = 0;
    return 0;
}

int
tiku_sys_reserve(int fd, long long bytes)
{
    /* Nothing to do: a shared memory object here is given its pages when
     * it is sized, so a mapping that was made can be written. */
    (void)fd;
    (void)bytes;
    return 0;
}

int
tiku_sys_serial_speed(int fd, int baud)
{
    speed_t rate = (speed_t)baud;

    /* IOSSIOSPEED sets a rate termios has no constant for; a later
     * tcsetattr() writes the termios rate back over it. */
    if (fd < 0 || baud <= 0) {
        errno = EINVAL;
        return -1;
    }
    if (ioctl(fd, IOSSIOSPEED, &rate) != 0) {
        return -1;
    }
    return 0;
}

int
tiku_sys_serial_ports(char out[][256], int max)
{
    /* The cu.NAME nodes in /dev whose name holds a driver in WHICH:
     * usbmodem is Apple's CDC driver, the rest are serial bridges.  A
     * tty.NAME node is left out, since opening it waits for a carrier a
     * board never asserts; so is the Bluetooth port, which pairs when
     * opened. */
    static const char *const WHICH[] = {
        "usbmodem", "usbserial", "SLAB", "wch", "UART"
    };
    DIR *d = opendir("/dev");
    struct dirent *e;
    int n = 0;

    if (d == NULL) {
        return -1;
    }
    while (n < max && (e = readdir(d)) != NULL) {
        size_t i;

        if (strncmp(e->d_name, "cu.", 3) != 0) {
            continue;
        }
        for (i = 0; i < sizeof WHICH / sizeof WHICH[0]; i++) {
            if (strstr(e->d_name + 3, WHICH[i]) != NULL) {
                break;
            }
        }
        if (i == sizeof WHICH / sizeof WHICH[0]) {
            continue;
        }
        if (strlen(e->d_name) + 6u > 256u) {
            continue;
        }
        (void)snprintf(out[n], 256, "/dev/%s", e->d_name);
        n++;
    }
    (void)closedir(d);
    return n;
}

const char *
tiku_sys_uri_launcher(void)
{
    return "/usr/bin/open";
}
