/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * settings_profile_client.c - independent SDK composite preference workflow.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_endpoint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "profile client:%d: %s\n", __LINE__, #test); exit(1); } } while (0)
static tiku_resource_channel_t *channel;

/** @brief Create an independently correlated request on the authenticated session. */
static tiku_msg_t *request(const char *op, const char *path)
{
    char id[33]; tiku_msg_t *message = tiku_msg_new(TIKU_RESOURCE_MESSAGE_CODE);
    CHECK(message && !tiku_resource_new_id(id) && tiku_msg_add_string(message, "op", op) &&
        tiku_msg_add_string(message, "request_id", id) && tiku_msg_add_string(message, "session", tiku_resource_channel_session(channel)));
    if (path) CHECK(tiku_msg_add_string(message, "path", path));
    return message;
}

/** @brief Exchange one complete request and require its expected structured result. */
static tiku_msg_t *call(tiku_msg_t *message, const char *error)
{
    tiku_msg_t *reply = NULL, *result;
    CHECK(!tiku_resource_channel_send(channel, message, 5000) && !tiku_resource_channel_receive(channel, &reply, 5000));
    result = tiku_msg_find_message(reply, error ? "error" : "result", 0); CHECK(result);
    if (error) CHECK(!strcmp(tiku_msg_find_string(result, "code", 0), error));
    tiku_msg_free(message); tiku_msg_free(reply); return result;
}

/** @brief Build one complete profile proposal at the resource's current provider revision. */
static tiku_msg_t *replacement(const char *path, const tiku_msg_t *stamp, const tiku_msg_t *profile)
{
    tiku_msg_t *message = request("set", path), *tuple = tiku_msg_new(0);
    CHECK(tuple && tiku_msg_add_string(tuple, "provider", tiku_msg_find_string(stamp, "provider", 0)) &&
        tiku_msg_add_string(tuple, "incarnation", tiku_msg_find_string(stamp, "incarnation", 0)) &&
        tiku_msg_add_string(tuple, "revision", tiku_msg_find_string(stamp, "revision", 0)) &&
        tiku_msg_add_message(message, "expected", tuple) && tiku_msg_add_message(message, "value", profile) &&
        tiku_msg_add_string(message, "durability", "user") && tiku_msg_add_int32(message, "deadline_ms", 3000));
    tiku_msg_free(tuple); return message;
}

int main(int argc, char **argv)
{
    tiku_msg_t *stamp, *profile, *next, *result, *message;
    char job[33]; int i; unsigned attempt; int64_t volume;
    CHECK(argc == 2); channel = tiku_resource_endpoint_connect(argv[1], 5000);
    CHECK(channel && !tiku_resource_channel_negotiate(channel, 11, 0, 5000));
    stamp = call(request("read", "/settings/profile"), NULL);
    profile = tiku_msg_find_message(stamp, "desired", 0); next = tiku_msg_new(0);
    CHECK(profile && next && tiku_msg_add_string(next, "type", "object"));
    for (i = 0; i < tiku_msg_count(profile, "member"); i++) {
        tiku_msg_t *member = tiku_msg_find_message(profile, "member", i);
        const char *key = tiku_msg_find_string(member, "key", 0);
        if (!strcmp(key, "sound.volume") || !strcmp(key, "HideDotFiles")) {
            tiku_msg_t *copy = tiku_msg_new(0), *value = tiku_resource_value_integer(!strcmp(key, "sound.volume") ? TIKU_RESOURCE_INTEGER : TIKU_RESOURCE_BOOLEAN,
                !strcmp(key, "sound.volume") ? 42 : 1);
            CHECK(copy && value && tiku_msg_add_string(copy, "key", key) && tiku_msg_add_message(copy, "value", value));
            tiku_msg_free(member); member = copy; tiku_msg_free(value);
        }
        CHECK(tiku_msg_add_message(next, "member", member)); tiku_msg_free(member);
    }
    result = call(replacement("/settings/profile", stamp, next), NULL);
    CHECK(tiku_msg_find_string(result, "job", 0)); strcpy(job, tiku_msg_find_string(result, "job", 0));
    tiku_msg_free(result); tiku_msg_free(stamp); tiku_msg_free(profile); tiku_msg_free(next);
    for (attempt = 0; attempt < 500; attempt++) {
        const char *state;
        message = request("job-status", NULL); CHECK(tiku_msg_add_string(message, "job", job)); result = call(message, NULL);
        state = tiku_msg_find_string(result, "state", 0); CHECK(state);
        if (!strcmp(state, "succeeded")) { tiku_msg_free(result); break; }
        CHECK(!strcmp(state, "queued") || !strcmp(state, "running")); tiku_msg_free(result); usleep(10000);
    }
    CHECK(attempt < 500);
    stamp = call(request("read", "/settings/profile"), NULL); profile = tiku_msg_find_message(stamp, "desired", 0);
    CHECK(profile && tiku_msg_count(profile, "member") == 44);
    next = tiku_msg_new(0); result = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 1);
    CHECK(next && result && tiku_msg_add_string(next, "key", "UnknownPreference") && tiku_msg_add_message(next, "value", result) && tiku_msg_add_message(profile, "member", next));
    tiku_msg_free(next); tiku_msg_free(result); result = call(replacement("/settings/profile", stamp, profile), "invalid_value");
    tiku_msg_free(result); tiku_msg_free(stamp); tiku_msg_free(profile);
    result = call(request("read", "/settings/sound/volume"), NULL); profile = tiku_msg_find_message(result, "persisted", 0);
    CHECK(profile && tiku_msg_find_int64(profile, "value", 0, &volume) && volume == 42);
    tiku_msg_free(profile); tiku_msg_free(result);
    {
        const char *paths[]={"/settings/deskbar/profile","/settings/ui/font/profile"};unsigned k;
        for(k=0;k<2;k++){
            stamp=call(request("read",paths[k]),NULL);profile=tiku_msg_find_message(stamp,"desired",0);CHECK(profile);
            result=call(replacement(paths[k],stamp,profile),NULL);CHECK(tiku_msg_find_string(result,"job",0));strcpy(job,tiku_msg_find_string(result,"job",0));
            tiku_msg_free(stamp);tiku_msg_free(profile);tiku_msg_free(result);
            for(attempt=0;attempt<500;attempt++){
                const char *state;message=request("job-status",NULL);CHECK(tiku_msg_add_string(message,"job",job));result=call(message,NULL);
                state=tiku_msg_find_string(result,"state",0);CHECK(state);
                if(!strcmp(state,"succeeded")){tiku_msg_free(result);break;}
                CHECK(!strcmp(state,"queued") || !strcmp(state,"running"));tiku_msg_free(result);usleep(10000);
            }
            CHECK(attempt<500);
        }
    }
    tiku_resource_channel_free(channel);
    puts("PASS composite profile: complete SDK replacement, job outcome, unknown key rejection and scalar persistence"); return 0;
}
