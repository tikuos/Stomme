/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_type_settings.c - native file edits, reloads and restart observations.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define tiku_atomic_file_guarded fixture_atomic_file
#include "../../system/shared/core/tiku_trk_type_settings.c"
#undef tiku_atomic_file_guarded
extern tiku_atomic_result_t tiku_atomic_file_guarded(const char *, int (*)(FILE *, void *), int (*)(void *), void *);
static int uncertain;
/** @brief Return a directory-sync failure after a completed fixture replacement. */
tiku_atomic_result_t fixture_atomic_file(const char *path, int (*produce_fn)(FILE *, void *), int (*check_fn)(void *), void *context)
{
    tiku_atomic_result_t result = tiku_atomic_file_guarded(path, produce_fn, check_fn, context);
    return uncertain && result == TIKU_ATOMIC_OK ? TIKU_ATOMIC_UNCERTAIN : result;
}
#define CHECK(test) do { if (!(test)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); exit(1); } } while (0)
static unsigned serial;
static tiku_resource_caller_t caller = {.principal = "00000000000000000000000000000001", .authenticated = 1, .scopes = 63};
/** @brief Write external fixture bytes to a regular file. */
static void put(const char *path, const char *bytes)
{
    FILE *file = fopen(path, "w");
    CHECK(file && fputs(bytes, file) >= 0 && !fclose(file));
}
/** @brief Capture complete native fixture bytes. */
static char *get(const char *path)
{
    type_file_t file;
    CHECK(!file_read(path, &file) && file.exists);
    return file.bytes;
}
/** @brief Force one complete external-file observation without waiting 500 ms. */
static void reload(tiku_trk_type_settings_t *owner)
{
    unsigned i;
    owner->next_poll = 0; tiku_trk_type_settings_poll(owner);
    for (i = 0; owner->reader && i < 5000; i++) { tiku_work_collect(); usleep(1000); }
    CHECK(!owner->reader);
    publish(owner);
}
/** @brief Read a copied resource using fixture authority. */
static tiku_resource_snapshot_t read_value(tiku_resource_registry_t *registry, const char *path)
{
    tiku_resource_snapshot_t out = {0};
    CHECK(!tiku_resource_read(registry, &caller, path, &out)); return out;
}
/** @brief Register the collection and its typed file children with async callbacks. */
static tiku_resource_error_t validate(void *owner, const char *path, const tiku_msg_t *input, int invoke)
{
    return tiku_trk_type_settings_validate(owner, path, input, invoke);
}
/** @brief Copy resource proposals into the fixture owner. */
static tiku_resource_error_t start(void *owner, const char *id, const char *path, const tiku_msg_t *input, int invoke, tiku_resource_snapshot_t *out)
{
    (void)out; return tiku_trk_type_settings_start(owner, id, path, input, invoke);
}
/** @brief Install owned initial snapshots and release registration temporaries. */
static void bind(tiku_trk_type_settings_t *owner, tiku_resource_registry_t *registry)
{
    size_t count = tiku_trk_type_settings_count(owner), i;
    tiku_resource_initial_t *initial = calloc(count + 1, sizeof *initial);
    tiku_resource_async_controller_t callbacks = {.validate = validate, .start = start};
    tiku_resource_stamp_t stamp;
    CHECK(initial);
    initial[0].spec = (tiku_resource_spec_t){.path = TYPE_ROOT, .kind = TIKU_RESOURCE_COLLECTION, .schema_version = 1, .read_scopes = TIKU_RESOURCE_READ, .public_read = 1, .persistence = "none", .controller = "fixture"};
    strcpy(initial[0].initial.apply_state, "applied");
    CHECK(!tiku_trk_type_settings_initial(owner, initial + 1));
    CHECK(!tiku_resource_register_async(registry, "types", TYPE_ROOT, NULL, initial, count + 1, &callbacks, owner, &stamp));
    for (i = 0; i <= count; i++) tiku_resource_snapshot_clear(&initial[i].initial);
    free(initial); tiku_trk_type_settings_bind(owner, registry);
}
/** @brief Submit a copied request and collect its terminal persistence outcome. */
static tiku_resource_error_t change(tiku_trk_type_settings_t *owner, const char *path, tiku_msg_t *input, int invoke)
{
    tiku_resource_snapshot_t before = read_value(owner->registry, path);
    tiku_resource_job_info_t job = {0}, out = {0};
    tiku_resource_error_t error;
    unsigned i;
    char id[33];
    snprintf(id, sizeof id, "%032x", ++serial);
    error = tiku_resource_job_submit(owner->registry, &caller, "00000000000000000000000000000002", path, &before.stamp, id, input, invoke, 10000, &job);
    tiku_resource_snapshot_clear(&before);
    if (error) return error;
    CHECK(tiku_resource_jobs_pump(owner->registry) == 1);
    for (i = 0; i < 5000; i++) {
        tiku_trk_type_settings_poll(owner); tiku_work_collect();
        tiku_resource_job_info_clear(&out);
        CHECK(!tiku_resource_job_status(owner->registry, &caller, job.id, &out));
        if (out.state >= TIKU_RESOURCE_JOB_SUCCEEDED) break;
        usleep(1000);
    }
    CHECK(i < 5000);
    error = out.error; tiku_resource_job_info_clear(&out); tiku_resource_job_info_clear(&job); return error;
}
/** @brief Construct a complete native record from fixture text fields. */
static tiku_msg_t *record(const char *mime, const char *description, const char *columns)
{
    tiku_trk_types_t db = {0};
    CHECK(tiku_trk_types_add(&db, mime, description, "file", columns) == 0);
    return record_value(&db.rec[0]);
}
/** @brief Compare a resource's saved and startup descriptions. */
static void descriptions(tiku_trk_type_settings_t *owner, const char *path, const char *saved, const char *applied)
{
    tiku_resource_snapshot_t out = read_value(owner->registry, path);
    tiku_trk_typerec_t a, b;
    CHECK(!input_record(out.persisted, &a) && !input_record(out.applied, &b));
    CHECK(!strcmp(a.desc, saved) && !strcmp(b.desc, applied));
    CHECK(!strcmp(out.apply_state, !strcmp(saved, applied) ? "applied" : "restart_required"));
    tiku_resource_snapshot_clear(&out);
}

int main(void)
{
    char home[] = "/tmp/stomme-types-XXXXXX", path[512], moved[520], target[520], typed[TIKU_RESOURCE_PATH_MAX], added[TIKU_RESOURCE_PATH_MAX];
    const char *initial = "# local definitions\nopaque extension line\ntext/plain\tPlain text\tfile\tsize,attr:rating,document,future:column\nimage/png\tPNG image\timage\tsize\n";
    tiku_trk_types_t applied = {0}, restarted = {0};
    tiku_resource_limits_t limits = {.resources = 1024};
    tiku_resource_registry_t *registry;
    tiku_trk_type_settings_t *owner;
    tiku_msg_t *value, *batch, *empty;
    tiku_resource_snapshot_t out;
    char *bytes;
    CHECK(mime_valid("tiku-") && !mime_valid("invalid"));
    CHECK(mkdtemp(home)); snprintf(path, sizeof path, "%s/types", home); snprintf(moved, sizeof moved, "%s.saved", path); snprintf(target, sizeof target, "%s.target", path);
    put(path, initial); CHECK(tiku_trk_types_load(&applied, path) == 2);
    owner = tiku_trk_type_settings_open(path, &applied); registry = tiku_resource_registry_new(&limits); CHECK(owner && registry); bind(owner, registry); reload(owner);
    CHECK(!tiku_trk_catalog_settings_path(TYPE_ROOT, "text/plain", NULL, typed));
    CHECK(!tiku_trk_catalog_settings_path(TYPE_ROOT, "application/x-example", NULL, added));
    descriptions(owner, typed, "Plain text", "Plain text");
    value = record("text/plain", "Edited text", "name,modified,attr:score,document");
    CHECK(!change(owner, typed, value, 0)); tiku_msg_free(value);
    descriptions(owner, typed, "Edited text", "Plain text");
    bytes = get(path); CHECK(strstr(bytes, "# local definitions\nopaque extension line\n") && strstr(bytes, "future:column") && strstr(bytes, "image/png\tPNG image\timage\tsize\n")); free(bytes);
    CHECK(tiku_trk_types_load(&restarted, path) == 2 && !strcmp(tiku_trk_types_exact(&restarted, "text/plain")->desc, "Edited text"));
    value = record("image/png", "Wrong identity", "size"); CHECK(change(owner, typed, value, 0) == TIKU_RESOURCE_VALUE); tiku_msg_free(value);
    value = record("text/plain", "Bad\nrow", "size"); CHECK(change(owner, typed, value, 0) == TIKU_RESOURCE_VALUE); tiku_msg_free(value);
    { tiku_trk_typerec_t bad = owner->saved.rec[0]; bad.nattr = 1; strcpy(bad.attr[0], "abcdefghijklmnopqrs"); value = record_value(&bad); }
    CHECK(!attr_valid("abcdefghijklmnopqrs")); { tiku_trk_typerec_t bad; CHECK(input_record(value, &bad) != 0); } CHECK(change(owner, typed, value, 0) == TIKU_RESOURCE_VALUE); tiku_msg_free(value);
    value = record("text/plain", "Unknown field", "size"); CHECK(member(value, "unexpected", tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, 1))); CHECK(change(owner, typed, value, 0) == TIKU_RESOURCE_VALUE); tiku_msg_free(value);
    value = record("text/plain", "Bad column", "size");
    { tiku_trk_typerec_t bad = owner->saved.rec[0]; bad.col[0] = 99; tiku_msg_free(value); value = record_value(&bad); }
    { tiku_trk_typerec_t bad; CHECK(input_record(value, &bad) != 0); }
    CHECK(change(owner, typed, value, 0) == TIKU_RESOURCE_VALUE); tiku_msg_free(value);
    {
        uint64_t epoch = 0; size_t count; tiku_resource_entry_t entries[1];
        CHECK(!tiku_resource_list(registry, &caller, TYPE_ROOT, 0, &epoch, entries, 1, &count) && count == 1);
        value = record("application/x-example", "Example", "size,document"); CHECK(!change(owner, TYPE_ROOT "/upsert", value, 1)); tiku_msg_free(value);
        CHECK(tiku_resource_list(registry, &caller, TYPE_ROOT, entries[0].node_id, &epoch, entries, 1, &count) == TIKU_RESOURCE_STALE);
    }
    out = read_value(registry, added); CHECK(tiku_msg_count(out.persisted, "member") == 6 && !tiku_msg_count(out.applied, "member") && !strcmp(out.apply_state, "restart_required")); tiku_resource_snapshot_clear(&out);
    empty = container("object"); CHECK(!change(owner, added, empty, 0));
    out = read_value(registry, added); CHECK(!tiku_msg_count(out.persisted, "member") && !tiku_msg_count(out.applied, "member") && !strcmp(out.apply_state, "applied")); tiku_resource_snapshot_clear(&out);
    CHECK(!change(owner, typed, empty, 0)); tiku_msg_free(empty);
    out = read_value(registry, typed); CHECK(!tiku_msg_count(out.persisted, "member") && tiku_msg_count(out.applied, "member") == 6 && !strcmp(out.apply_state, "restart_required")); tiku_resource_snapshot_clear(&out);
    batch = container("array"); value = record("text/plain", "Batch text", "size,document"); CHECK(tiku_msg_add_message(batch, "item", value)); tiku_msg_free(value);
    value = record("application/x-example", "Batch example", "name,attr:rating"); CHECK(tiku_msg_add_message(batch, "item", value)); tiku_msg_free(value);
    CHECK(!change(owner, TYPE_ROOT "/replace", batch, 1)); tiku_msg_free(batch);
    bytes = get(path); CHECK(strstr(bytes, "opaque extension line") && !strstr(bytes, "image/png") && strstr(bytes, "Batch example")); free(bytes);
    descriptions(owner, typed, "Batch text", "Plain text");
    {
        struct stat st; struct timespec times[2];
        CHECK(!stat(path, &st)); times[0] = st.st_atim; times[1] = st.st_mtim;
        bytes = get(path); char *name = strstr(bytes, "Batch text"); CHECK(name); memcpy(name, "Other text", 10); put(path, bytes); free(bytes);
        CHECK(!utimensat(AT_FDCWD, path, times, 0)); reload(owner);
        descriptions(owner, typed, "Other text", "Plain text");
    }
    {
        type_job_t guard = {0}; strcpy(guard.path, path); CHECK(!file_copy(&guard.before, &owner->file)); atomic_init(&guard.cancelled, 0);
        put(path, initial); CHECK(check(&guard) != 0); file_clear(&guard.before);
    }
    reload(owner); descriptions(owner, typed, "Plain text", "Plain text");
    {
        value = record("text/plain", "Stale edit", "size");
        CHECK(tiku_trk_type_settings_start(owner, "00000000000000000000000000000999", typed, value, 0) == TIKU_RESOURCE_PENDING); tiku_msg_free(value);
        put(path, "text/plain\tOutside edit\tfile\tsize\n");
        for (int i = 0; owner->writer && i < 5000; i++) { tiku_trk_type_settings_poll(owner); tiku_work_collect(); usleep(1000); }
        CHECK(!owner->writer && owner->error == TIKU_RESOURCE_STALE);
        bytes = get(path); CHECK(strstr(bytes, "Outside edit") && !strstr(bytes, "Stale edit")); free(bytes);
    }
    reload(owner); descriptions(owner, typed, "Outside edit", "Plain text");
    uncertain = 1; value = record("text/plain", "Uncertain edit", "size");
    CHECK(change(owner, typed, value, 0) == TIKU_RESOURCE_UNKNOWN_OUTCOME); tiku_msg_free(value); uncertain = 0;
    CHECK(!owner->ready && owner->error == TIKU_RESOURCE_UNKNOWN_OUTCOME);
    bytes = get(path); CHECK(strstr(bytes, "Uncertain edit")); free(bytes);
    reload(owner); descriptions(owner, typed, "Uncertain edit", "Plain text");
    put(path, "text/plain\tOutside edit\tfile\tsize\n"); reload(owner);
    CHECK(!rename(path, moved) && !mkdir(path, 0700));
    value = record("text/plain", "Failed edit", "size"); CHECK(change(owner, typed, value, 0) != TIKU_RESOURCE_OK); tiku_msg_free(value);
    CHECK(!rmdir(path) && !rename(moved, path)); reload(owner); descriptions(owner, typed, "Outside edit", "Plain text");
    put(path, "text/plain\tFirst\tfile\tsize\ntext/plain\tDuplicate\tfile\tsize\n"); reload(owner); CHECK(!owner->ready && owner->error == TIKU_RESOURCE_VALUE && !strcmp(owner->saved.rec[0].desc, "Outside edit"));
    value = record("text/plain", "Rejected invalid source", "size"); CHECK(change(owner, typed, value, 0) == TIKU_RESOURCE_UNAVAILABLE); tiku_msg_free(value);
    {
        char large[TYPE_FILE_MAX + 2]; type_file_t source = {.bytes = large, .exists = 1}; tiku_trk_types_t parsed;
        memset(large, 'x', sizeof large); large[sizeof large - 1] = 0;
        put(path, large); reload(owner); CHECK(!owner->ready && owner->error == TIKU_RESOURCE_PERSISTENCE_FAILED);
        source.size = 512; CHECK(parse(&source, &parsed) == TIKU_RESOURCE_LIMIT);
        source.bytes = "text/plain\tValid\tfile\tsize,size,size,size,size,size,size\n"; source.size = strlen(source.bytes);
        CHECK(parse(&source, &parsed) == TIKU_RESOURCE_VALUE);
    }
    put(path, ""); reload(owner); CHECK(owner->ready && owner->saved.n == 0);
    CHECK(!unlink(path)); reload(owner); CHECK(owner->ready && !owner->file.exists && owner->saved.n > 0);
    value = record("text/plain", "Missing file edit", "size"); { int result = change(owner, typed, value, 0); if (result) fprintf(stderr, "missing save error %d ready %d resync %d\n", result, owner->ready, owner->resync); CHECK(!result); } tiku_msg_free(value); CHECK(owner->file.exists && owner->saved.n > 1);
    {
        tiku_trk_types_t baseline = owner->saved;
        for (int turn = 0; turn < 2; turn++) {
            batch = container("array");
            int count = TYPE_IDENTITIES_MAX - owner->identities; if (count > 40) count = 40;
            for (int i = 0; i < count; i++) {
                char mime[48]; snprintf(mime, sizeof mime, "application/x-batch-%d-%d", turn, i);
                value = record(mime, "Retained identity", "size"); CHECK(tiku_msg_add_message(batch, "item", value)); tiku_msg_free(value);
            }
            CHECK(!change(owner, TYPE_ROOT "/replace", batch, 1)); tiku_msg_free(batch);
        }
        CHECK(owner->identities == TYPE_IDENTITIES_MAX);
        value = record("application/x-over-limit", "Rejected identity", "size"); CHECK(change(owner, TYPE_ROOT "/upsert", value, 1) == TIKU_RESOURCE_LIMIT); tiku_msg_free(value);
        CHECK(tiku_trk_types_save(&baseline, path) == 0); reload(owner);
    }
    CHECK(!rename(path, target) && !symlink(target, path)); reload(owner);
    value = record("text/plain", "Symlink edit", "size"); CHECK(!change(owner, typed, value, 0)); tiku_msg_free(value);
    { struct stat st; CHECK(!lstat(path, &st) && S_ISLNK(st.st_mode)); }
    bytes = get(target); CHECK(strstr(bytes, "Symlink edit")); free(bytes);
    {
        type_job_t guard = {0}; strcpy(guard.path, path); CHECK(!file_copy(&guard.before, &owner->file)); atomic_init(&guard.cancelled, 0);
        bytes = get(path); put(moved, bytes); free(bytes); CHECK(!unlink(path) && !symlink(moved, path)); CHECK(check(&guard) != 0); file_clear(&guard.before);
    }
    tiku_trk_type_settings_close(owner); tiku_resource_registry_free(registry);
    CHECK(!unlink(path) && !unlink(target) && !unlink(moved) && !rmdir(home));
    CHECK(tiku_trk_types_save(&applied, "/dev/full") == -1);
    puts("type settings: PASS"); return 0;
}
