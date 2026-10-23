#include "tiku_shell_product.h"
/* App discovery reads manifests, never executable module contents. */
#include <assert.h>
#include "../../system/desktop/tiku_trk_desktop_apps.c"

static int offered, menu_rows;
static char startup[96], legacy_id[96];

void desktop_startup_offer_id(desktop_t *app, const char *path,
    const char *label, const char *id, const char *legacy)
{
    (void)app; (void)path; (void)label;
    offered++; snprintf(startup, sizeof startup, "%s", id);
    snprintf(legacy_id, sizeof legacy_id, "%s", legacy);
}

void desktop_startup_offer(desktop_t *app, const char *path,
    const char *label, const char *group)
{ (void)app; (void)path; (void)label; (void)group; assert(0); }

int tiku_trk_deskbar_add_app_in(tiku_trk_deskbar_t *db, int group,
    const char *name, const char *path, const char *icon)
{
    (void)db;
    assert(group == TIKU_TRK_DB_GROUP_DEMOS);
    assert(!strcmp(name, "Custom camera tool"));
    assert(strstr(path, "/unrelated.so"));
    assert(!strcmp(icon, "image_viewer"));
    menu_rows++; return 1;
}

static void file(const char *directory, const char *leaf, const char *text)
{
    char path[TIKU_PATH_MAX];
    snprintf(path, sizeof path, "%s/%s", directory, leaf);
    FILE *f = fopen(path, "w"); assert(f);
    assert(fputs(text, f) >= 0 && !fclose(f));
}

static void remove_file(const char *directory, const char *leaf)
{
    char path[TIKU_PATH_MAX];
    snprintf(path, sizeof path, "%s/%s", directory, leaf);
    assert(!unlink(path));
}

int main(void)
{
    char root[] = "/tmp/tiku-discovery-XXXXXX", path[TIKU_PATH_MAX];
    char bundle[TIKU_PATH_MAX], duplicate[TIKU_PATH_MAX];
    desktop_t *app = calloc(1, sizeof *app); assert(app && mkdtemp(root));
    snprintf(bundle, sizeof bundle, "%s/first", root); assert(!mkdir(bundle, 0700));
    snprintf(duplicate, sizeof duplicate, "%s/second", root);
    assert(!mkdir(duplicate, 0700));
    const char *manifest = "version=1\nid=camera\nname=Custom camera tool\n"
        "module=unrelated.so\ncategory=demos\nicon=image_viewer\n"
        "types=image/png\nstartup=apps/old.so\n";
    file(bundle, "app.manifest", manifest);
    file(bundle, "unrelated.so", "not an executable; discovery must not load this");
    file(duplicate, "app.manifest", manifest);
    file(duplicate, "unrelated.so", "duplicate ID");
    offer_directory(app, root, "/runner", "apps");
    assert(offered == 1 && menu_rows == 1 && app->apps.count == 1);
    assert(!strcmp(app->apps.app[0].id, "camera"));
    assert(!strcmp(app->apps.app[0].icon, "image_viewer"));
    assert(!strcmp(app->apps.app[0].type[0], "image/png"));
    assert(!strcmp(startup, "app/camera") && !strcmp(legacy_id, "apps/old.so"));

    /* The relative development layout is canonical before a child chdirs. */
    assert(!setenv("TIKU_APPS", "build/runtime/apps", 1));
    assert(find_apps_dir(TIKU_SHELL_TEST_RUNTIME_BINARY, path, sizeof path));
    assert(path[0] == '/' && strstr(path, "/build/runtime/apps"));
    unsetenv("TIKU_APPS");
    assert(find_apps_dir(TIKU_SHELL_TEST_RUNTIME_BINARY, path, sizeof path));
    assert(path[0] == '/' && strstr(path, "/build/runtime/apps"));
    assert(!setenv("TIKU_HELP", "assets/help", 1));
    assert(find_help_dir(TIKU_SHELL_TEST_RUNTIME_BINARY, path, sizeof path));
    assert(path[0] == '/' && strstr(path, "/assets/help"));
    unsetenv("TIKU_HELP");
    assert(find_help_dir(TIKU_SHELL_TEST_RUNTIME_BINARY, path, sizeof path));
    assert(path[0] == '/');
    char tiny[2]; assert(!canonical_directory(root, tiny, sizeof tiny));

    remove_file(bundle, "app.manifest"); remove_file(bundle, "unrelated.so");
    remove_file(duplicate, "app.manifest"); remove_file(duplicate, "unrelated.so");
    assert(!rmdir(bundle) && !rmdir(duplicate) && !rmdir(root));
    free(app);
    puts("PASS: data-only discovery, duplicate IDs, category/types, absolute launch resources");
}
