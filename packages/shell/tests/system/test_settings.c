/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_settings.c - shared settings persistence, reload and resource ownership.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_settings.h"
#include "tiku_resource_jobs.h"
#include "tiku_work.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); exit(1); } } while (0)

/** @brief Preference paths specified by the desktop resource coverage contract. */
static const struct { const char *name, *path; } paths[] = {
    {"sound.beep", "/settings/sound/beep/enabled"},
    {"sound.beep.clip", "/settings/sound/beep/clip"},
    {"sound.shutdown", "/settings/sound/shutdown/enabled"},
    {"sound.shutdown.clip", "/settings/sound/shutdown/clip"},
    {"sound.startup", "/settings/sound/startup/enabled"},
    {"sound.startup.clip", "/settings/sound/startup/clip"},
    {"sound.volume", "/settings/sound/volume"},
    {"sound.window-activate", "/settings/sound/window-activate/enabled"},
    {"sound.window-activate.clip", "/settings/sound/window-activate/clip"},
    {"sound.window-close", "/settings/sound/window-close/enabled"},
    {"sound.window-close.clip", "/settings/sound/window-close/clip"},
    {"sound.window-minimize", "/settings/sound/window-minimize/enabled"},
    {"sound.window-minimize.clip", "/settings/sound/window-minimize/clip"},
    {"sound.window-open", "/settings/sound/window-open/enabled"},
    {"sound.window-open.clip", "/settings/sound/window-open/clip"},
    {"sound.window-zoom", "/settings/sound/window-zoom/enabled"},
    {"sound.window-zoom.clip", "/settings/sound/window-zoom/clip"},
    {"AutoMount", "/settings/tracker/automount"},
    {"DesktopFilePanelRoot", "/settings/tracker/desktopfilepanelroot"},
    {"EjectWhenUnmounting", "/settings/tracker/ejectwhenunmounting"},
    {"FreeSpaceColor", "/settings/tracker/freespacecolor"},
    {"GenerateImageThumbnails", "/settings/tracker/generateimagethumbnails"},
    {"HideDotFiles", "/settings/tracker/hidedotfiles"},
    {"IntegrateNonBootBeOSDesktops", "/settings/tracker/integratenonbootbeosdesktops"},
    {"MountSharedVolumesOntoDesktop", "/settings/tracker/mountsharedvolumesontodesktop"},
    {"MountVolumesOntoDesktop", "/settings/tracker/mountvolumesontodesktop"},
    {"RecentApplications", "/settings/tracker/recentapplications"},
    {"RecentDocuments", "/settings/tracker/recentdocuments"},
    {"RecentFolders", "/settings/tracker/recentfolders"},
    {"SelectedLabelColor", "/settings/tracker/selectedlabelcolor"},
    {"ShowDisksIcon", "/settings/tracker/showdisksicon"},
    {"ShowFullPathInTitleBar", "/settings/tracker/showfullpathintitlebar"},
    {"ShowNavigator", "/settings/tracker/shownavigator"},
    {"ShowSelectionWhenInactive", "/settings/tracker/showselectionwheninactive"},
    {"ShowVolumeSpaceBar", "/settings/tracker/showvolumespacebar"},
    {"SingleWindowBrowse", "/settings/tracker/singlewindowbrowse"},
    {"SortFolderNamesFirst", "/settings/tracker/sortfoldernamesfirst"},
    {"TransparentSelection", "/settings/tracker/transparentselection"},
    {"TypeAheadFiltering", "/settings/tracker/typeaheadfiltering"},
    {"UsedSpaceColor", "/settings/tracker/usedspacecolor"},
    {"WarningSpaceColor", "/settings/tracker/warningspacecolor"},
    {"GlobalMenuBar", "/settings/ui/global-menu-bar"},
    {"ReduceMotion", "/settings/ui/reduce-motion"},
    {"DarkMode", "/settings/ui/theme"},
    {"CurrentDevice", "/settings/tracker/currentdevice"},
    {"CurrentDevicePort", "/settings/tracker/currentdeviceport"},
};

/** @brief Count owner-thread live model applications. */
static void changed(void *context)
{
    (*(int *)context)++;
}

/** @brief Collect workers with a bounded deadline. */
static void drain(void)
{
    unsigned i;
    for (i = 0; i < 10000 && tiku_work_busy(); i++) { tiku_work_collect(); usleep(1000); }
    CHECK(!tiku_work_busy());
}

/** @brief Replace an isolated settings file with exact test bytes. */
static void write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb"); CHECK(file && fputs(text, file) >= 0 && !fclose(file));
}

/** @brief Read one public snapshot with explicit trusted test authority. */
static tiku_resource_snapshot_t read_value(tiku_resource_registry_t *registry, const char *path)
{
    tiku_resource_caller_t caller = {.authenticated = 1, .scopes = 63};
    tiku_resource_snapshot_t value = {0};
    strcpy(caller.principal, "00000000000000000000000000000001");
    CHECK(!tiku_resource_read(registry, &caller, path, &value)); return value;
}

/** @brief Submit a real setting change and reconcile its terminal worker result. */
static tiku_resource_error_t change(tiku_resource_registry_t *registry, const char *path,
    const tiku_msg_t *value, unsigned serial)
{
    tiku_resource_caller_t caller = {.authenticated = 1, .scopes = 63};
    tiku_resource_snapshot_t before = read_value(registry, path);
    tiku_resource_job_info_t job = {0}, result = {0};
    char id[33], connection[33] = "00000000000000000000000000000002";
    tiku_resource_error_t error;
    strcpy(caller.principal, "00000000000000000000000000000001"); snprintf(id, sizeof id, "%032x", serial);
    error = tiku_resource_job_submit(registry, &caller, connection, path, &before.stamp, id, value, 0, 10000, &job);
    tiku_resource_snapshot_clear(&before);
    if (error) return error;
    CHECK(tiku_resource_jobs_pump(registry) == 1); drain();
    CHECK(!tiku_resource_job_status(registry, &caller, job.id, &result));
    error = result.error;
    CHECK(result.state == TIKU_RESOURCE_JOB_SUCCEEDED || result.state == TIKU_RESOURCE_JOB_FAILED || result.state == TIKU_RESOURCE_JOB_UNKNOWN);
    tiku_resource_job_info_clear(&job); tiku_resource_job_info_clear(&result); return error;
}

int main(void)
{
    char directory[] = "/tmp/stomme-settings-XXXXXX", path[256], original[256], link[256];
    tiku_trk_prefs_t prefs, restarted;
    tiku_resource_registry_t *registry = tiku_resource_registry_new(NULL);
    tiku_trk_settings_t *owner;
    tiku_resource_snapshot_t value;
    tiku_msg_t *request;
    struct stat before; struct timespec times[2];
    int calls = 0, boolean; int64_t number;
    tiku_resource_watch_t watch;
    tiku_msg_t *watch_snapshot;
    tiku_resource_event_t event;
    tiku_resource_caller_t watcher = {.authenticated = 1, .scopes = 63};
    CHECK(registry && mkdtemp(directory)); snprintf(path, sizeof path, "%s/TrackerSettings", directory);
    snprintf(original, sizeof original, "%s/original", directory); snprintf(link, sizeof link, "%s/link", directory);
    write_file(path, "DarkMode 0\nFutureFlag preserved\n");
    tiku_trk_prefs_init(&prefs); CHECK(!tiku_trk_prefs_load(&prefs, path));
    owner = tiku_trk_settings_open(&prefs, path, registry, changed, &calls); CHECK(owner);
    drain(); CHECK(calls == 1 && !tiku_trk_settings_error(owner));
    CHECK(tiku_trk_settings_count(owner) == (size_t)prefs.n + 1);
    CHECK(!tiku_trk_prefs_submit(&prefs, path) && !tiku_work_busy());
    value = read_value(registry, "/settings/ui/theme");
    CHECK(!strcmp(tiku_msg_find_string(value.desired, "value", 0), "light")); tiku_resource_snapshot_clear(&value);
    strcpy(watcher.principal, "00000000000000000000000000000001");
    CHECK(!tiku_resource_watch_open(registry, &watcher, "settings", "/settings/ui/theme", &watch, &watch_snapshot));
    tiku_msg_free(watch_snapshot);
    request = tiku_resource_value_string(TIKU_RESOURCE_ENUM, "dark"); CHECK(request && !change(registry, "/settings/ui/theme", request, 1)); tiku_msg_free(request);
    CHECK(tiku_trk_prefs_get(&prefs, "DarkMode") == 1 && calls == 2);
    CHECK(tiku_resource_watch_next(registry, watch, &event) == 1 && !strcmp(event.path, "/settings/ui/theme") && !event.overflow);
    tiku_resource_watch_close(registry, watch);
    tiku_trk_prefs_init(&restarted); CHECK(!tiku_trk_prefs_load(&restarted, path));
    CHECK(tiku_trk_prefs_get(&restarted, "DarkMode") == 1 && restarted.nunknown == 1 && !strcmp(restarted.unknown[0], "FutureFlag preserved")); tiku_trk_prefs_free(&restarted);
    {
        tiku_resource_snapshot_t profile = read_value(registry, "/settings/profile");
        tiku_msg_t *replacement = tiku_msg_new(0); int i;
        CHECK(replacement && tiku_msg_add_string(replacement, "type", "object"));
        for (i = 0; i < tiku_msg_count(profile.desired, "member"); i++) {
            tiku_msg_t *member = tiku_msg_find_message(profile.desired, "member", i);
            const char *key = tiku_msg_find_string(member, "key", 0);
            if (!strcmp(key, "sound.volume") || !strcmp(key, "HideDotFiles")) {
                tiku_msg_t *next = tiku_msg_new(0), *number = tiku_resource_value_integer(!strcmp(key, "sound.volume") ? TIKU_RESOURCE_INTEGER : TIKU_RESOURCE_BOOLEAN, !strcmp(key, "sound.volume") ? 55 : 1);
                CHECK(next && number && tiku_msg_add_string(next, "key", key) && tiku_msg_add_message(next, "value", number));
                tiku_msg_free(member); member = next; tiku_msg_free(number);
            }
            CHECK(tiku_msg_add_message(replacement, "member", member)); tiku_msg_free(member);
        }
        CHECK(!change(registry, "/settings/profile", replacement, 50));
        CHECK(tiku_trk_prefs_get(&prefs, "sound.volume") == 55 && tiku_trk_prefs_get(&prefs, "HideDotFiles") && tiku_trk_prefs_get(&prefs, "DarkMode") == 1);
        tiku_msg_free(replacement);
        replacement = tiku_msg_new(0); CHECK(replacement && tiku_msg_add_string(replacement, "type", "object"));
        CHECK(change(registry, "/settings/profile", replacement, 51) == TIKU_RESOURCE_VALUE);
        CHECK(tiku_trk_prefs_get(&prefs, "sound.volume") == 55);
        tiku_msg_free(replacement);
        {
            tiku_msg_t *extra = tiku_msg_new(0), *enabled = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 1);
            CHECK(extra && enabled && tiku_msg_add_string(extra, "key", "UnknownPreference") && tiku_msg_add_message(extra, "value", enabled) && tiku_msg_add_message(profile.desired, "member", extra));
            CHECK(change(registry, "/settings/profile", profile.desired, 52) == TIKU_RESOURCE_VALUE);
            CHECK(tiku_trk_prefs_get(&prefs, "sound.volume") == 55);
            tiku_msg_free(extra); tiku_msg_free(enabled);
        }
        tiku_resource_snapshot_clear(&profile);
    }
    request = tiku_resource_value_string(TIKU_RESOURCE_ENUM, "invalid"); CHECK(change(registry, "/settings/ui/theme", request, 2) == TIKU_RESOURCE_VALUE); tiku_msg_free(request);
    request = tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, 101); CHECK(change(registry, "/settings/sound/volume", request, 3) == TIKU_RESOURCE_VALUE); tiku_msg_free(request);
    request = tiku_resource_value_string(TIKU_RESOURCE_TEXT, "bad\nDarkMode 2"); CHECK(change(registry, "/settings/sound/beep/clip", request, 4) == TIKU_RESOURCE_VALUE); tiku_msg_free(request);
    request = tiku_resource_value_integer(TIKU_RESOURCE_COLOR, 0xff00aa11); CHECK(!change(registry, "/settings/tracker/selectedlabelcolor", request, 5)); tiku_msg_free(request);
    CHECK((uint32_t)tiku_trk_prefs_get(&prefs, "SelectedLabelColor") == 0xff00aa11);
    request = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 1); CHECK(!change(registry, "/settings/tracker/mountvolumesontodesktop", request, 6)); tiku_msg_free(request);
    CHECK(!tiku_trk_prefs_get(&prefs, "ShowDisksIcon"));
    value = read_value(registry, "/settings/tracker/showdisksicon"); CHECK(tiku_msg_find_bool(value.desired, "value", 0, &boolean) && !boolean); tiku_resource_snapshot_clear(&value);
    tiku_trk_prefs_set(&prefs, "DarkMode", 2); CHECK(!tiku_trk_prefs_submit(&prefs, path));
    CHECK(tiku_trk_prefs_get(&prefs, "DarkMode") == 1); drain(); CHECK(tiku_trk_prefs_get(&prefs, "DarkMode") == 2);
    value = read_value(registry, "/settings/ui/theme"); CHECK(!strcmp(tiku_msg_find_string(value.desired, "value", 0), "monochrome")); tiku_resource_snapshot_clear(&value);
    CHECK(!rename(path, original) && !mkdir(path, 0700));
    tiku_trk_prefs_set(&prefs, "DarkMode", 0); CHECK(!tiku_trk_prefs_submit(&prefs, path)); drain();
    CHECK(tiku_trk_settings_error(owner) && tiku_trk_prefs_get(&prefs, "DarkMode") == 2);
    CHECK(!rmdir(path) && !rename(original, path));
    write_file(path, "DarkMode 0\nFutureFlag preserved\n"); tiku_trk_settings_poll(owner, 1000); drain(); CHECK(!tiku_trk_prefs_get(&prefs, "DarkMode"));
    CHECK(!stat(path, &before)); times[0] = before.st_atim; times[1] = before.st_mtim;
    write_file(path, "DarkMode 1\nFutureFlag preserved\n"); CHECK(!utimensat(AT_FDCWD, path, times, 0));
    tiku_trk_settings_poll(owner, 1200); drain(); CHECK(tiku_trk_prefs_get(&prefs, "DarkMode") == 1);
    value = read_value(registry, "/settings/ui/theme"); CHECK(!strcmp(tiku_msg_find_string(value.applied, "value", 0), "dark")); tiku_resource_snapshot_clear(&value);
    write_file(path, "DarkMode 9\n"); tiku_trk_settings_poll(owner, 1400); drain(); CHECK(tiku_trk_settings_error(owner) && tiku_trk_prefs_get(&prefs, "DarkMode") == 1);
    write_file(path, "DarkMode 2\nFutureFlag preserved\n");
    tiku_trk_prefs_set(&prefs, "DarkMode", 0); CHECK(!tiku_trk_prefs_submit(&prefs, path)); drain();
    CHECK(tiku_trk_settings_error(owner) == TIKU_RESOURCE_STALE && tiku_trk_prefs_get(&prefs, "DarkMode") == 1);
    tiku_trk_settings_poll(owner, 1600); drain(); CHECK(tiku_trk_prefs_get(&prefs, "DarkMode") == 2);
    request = tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, 37); CHECK(!change(registry, "/settings/sound/volume", request, 7)); tiku_msg_free(request);
    value = read_value(registry, "/settings/sound/volume"); CHECK(tiku_msg_find_int64(value.persisted, "value", 0, &number) && number == 37); tiku_resource_snapshot_clear(&value);
    {
        int i, checked = 0;
        for (i = 0; i < prefs.n; i++) {
            const tiku_trk_pref_t *pref = &prefs.pref[i];
            const char *resource;
            size_t j;
            for (j = 0; j < sizeof paths / sizeof paths[0]; j++) if (!strcmp(pref->name, paths[j].name)) break;
            CHECK(j < sizeof paths / sizeof paths[0]); resource = paths[j].path;
            if (!strcmp(pref->name, "CurrentDevice") || !strcmp(pref->name, "CurrentDevicePort")) {
                value = read_value(registry, resource); tiku_resource_snapshot_clear(&value); continue;
            }
            if (!strcmp(pref->name, "DarkMode")) request = tiku_resource_value_string(TIKU_RESOURCE_ENUM, pref->value == 1 ? "light" : "dark");
            else if (pref->type == TIKU_TRK_PREF_TEXT) request = tiku_resource_value_string(TIKU_RESOURCE_TEXT, "test-clip.wav");
            else request = tiku_resource_value_integer(pref->type == TIKU_TRK_PREF_BOOL ? TIKU_RESOURCE_BOOLEAN : pref->type == TIKU_TRK_PREF_COLOUR ? TIKU_RESOURCE_COLOR : TIKU_RESOURCE_INTEGER,
                pref->type == TIKU_TRK_PREF_BOOL ? !pref->value : pref->type == TIKU_TRK_PREF_COLOUR ? (int64_t)((uint32_t)pref->value ^ 1u) : pref->value + 1);
            CHECK(request && !change(registry, resource, request, (unsigned)i + 100));
            value = read_value(registry, resource); CHECK(tiku_resource_value_equal(value.desired, request) && tiku_resource_value_equal(value.persisted, request));
            tiku_resource_snapshot_clear(&value); tiku_msg_free(request); checked++;
        }
        CHECK(checked == prefs.n - (tiku_trk_prefs_find(&prefs, "CurrentDevice") ? 2 : 0));
        tiku_trk_prefs_init(&restarted); CHECK(!tiku_trk_prefs_load(&restarted, path));
        for (i = 0; i < prefs.n; i++) CHECK(restarted.pref[i].value == prefs.pref[i].value && !strcmp(restarted.pref[i].text, prefs.pref[i].text));
        tiku_trk_prefs_free(&restarted);
        printf("PASS table persistence: %d writable entries\n", checked);
    }
    tiku_trk_settings_close(owner); tiku_trk_prefs_free(&prefs); tiku_resource_registry_free(registry);
    write_file(path, "DarkMode 9\nsound.volume 1000\nFutureFlag preserved\n");
    tiku_trk_prefs_init(&prefs); CHECK(!tiku_trk_prefs_load(&prefs, path));
    owner = tiku_trk_settings_open(&prefs, path, NULL, changed, &calls); CHECK(owner); drain();
    CHECK(tiku_trk_settings_error(owner) && !tiku_trk_prefs_get(&prefs, "DarkMode") && tiku_trk_prefs_get(&prefs, "sound.volume") == 100);
    tiku_trk_prefs_set(&prefs, "DarkMode", 2); CHECK(!tiku_trk_prefs_submit(&prefs, path)); drain();
    CHECK(!tiku_trk_settings_error(owner) && tiku_trk_prefs_get(&prefs, "DarkMode") == 2);
    tiku_trk_settings_close(owner); tiku_trk_prefs_free(&prefs);
    CHECK(!symlink(path, link)); tiku_trk_prefs_init(&prefs); CHECK(!tiku_trk_prefs_load(&prefs, link));
    owner = tiku_trk_settings_open(&prefs, link, NULL, changed, &calls); CHECK(owner); drain();
    tiku_trk_prefs_set(&prefs, "DarkMode", 0); CHECK(!tiku_trk_prefs_submit(&prefs, link)); drain(); CHECK(!lstat(link, &before) && S_ISLNK(before.st_mode));
    tiku_trk_settings_poll(owner, 2000); tiku_trk_settings_close(owner); tiku_trk_prefs_free(&prefs); drain();
    CHECK(!unlink(link) && !unlink(path) && !rmdir(directory));
    puts("PASS shared settings: typed changes, GUI save, worker persistence, restart, conflicts, same-stat reload, interlocks and owner teardown"); return 0;
}
