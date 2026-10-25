/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * shell_resources_client.c - independent SDK native settings, window actions and live snapshots.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_endpoint.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static tiku_resource_channel_t *channel;
static int print_result;

/** @brief Build a session-correlated request with a fresh mutation identity. */
static tiku_msg_t *request(const char *op, const char *path)
{
    char id[33]; tiku_msg_t *message = tiku_msg_new(TIKU_RESOURCE_MESSAGE_CODE);
    if (!message || tiku_resource_new_id(id) || !tiku_msg_add_string(message, "op", op) ||
        !tiku_msg_add_string(message, "request_id", id) || !tiku_msg_add_string(message, "session", tiku_resource_channel_session(channel)) ||
        (path && !tiku_msg_add_string(message, "path", path))) { tiku_msg_free(message); return NULL; }
    return message;
}

/** @brief Exchange one request, print structured failures and return an owned result. */
static tiku_msg_t *call(tiku_msg_t *message)
{
    tiku_msg_t *reply = NULL, *result, *error;
    if (!message || tiku_resource_channel_send(channel, message, 5000) || tiku_resource_channel_receive(channel, &reply, 5000)) {
        fprintf(stderr, "resource transport failed\n"); tiku_msg_free(message); return NULL;
    }
    result = tiku_msg_find_message(reply, "result", 0);
    error = tiku_msg_find_message(reply, "error", 0);
    if (error) fprintf(stderr, "%s\n", tiku_msg_find_string(error, "code", 0));
    tiku_msg_free(error); tiku_msg_free(reply); tiku_msg_free(message); return result;
}

/** @brief Append one typed member and release its temporary allocation. */
static int member(tiku_msg_t *object, const char *key, tiku_msg_t *value)
{
    tiku_msg_t *entry = tiku_msg_new(0);
    int ok = entry && value && tiku_msg_add_string(entry, "key", key) && tiku_msg_add_message(entry, "value", value) && tiku_msg_add_message(object, "member", entry);
    tiku_msg_free(entry); tiku_msg_free(value); return ok;
}
/** @brief Encode a complete background from checked native integer fields. */
static tiku_msg_t *background(char **arguments)
{
    const char *keys[] = {"mode", "x", "y"}; int i;
    tiku_msg_t *out = tiku_msg_new(0);
    if (!out || !tiku_msg_add_string(out, "type", "object") || !member(out,"path",tiku_resource_value_string(TIKU_RESOURCE_TEXT,arguments[0]))) goto fail;
    for (i=0;i<3;i++) {
        char *end; long long number; errno=0; number=strtoll(arguments[i+1],&end,10);
        if (errno || end==arguments[i+1] || *end || !member(out,keys[i],tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,number))) goto fail;
    }
    return out;
fail: tiku_msg_free(out); return NULL;
}
/** @brief Copy the server's current provider stamp into a mutation precondition. */
static int expected(tiku_msg_t *message, const tiku_msg_t *stamp)
{
    tiku_msg_t *tuple = tiku_msg_new(0);
    int ok = tuple && tiku_msg_add_string(tuple, "provider", tiku_msg_find_string(stamp, "provider", 0)) &&
        tiku_msg_add_string(tuple, "incarnation", tiku_msg_find_string(stamp, "incarnation", 0)) &&
        tiku_msg_add_string(tuple, "revision", tiku_msg_find_string(stamp, "revision", 0)) && tiku_msg_add_message(message, "expected", tuple);
    tiku_msg_free(tuple); return ok;
}
/** @brief Submit one replacement or action and require a terminal saved outcome. */
static int change(const char *path, tiku_msg_t *value, int invoke)
{
    tiku_msg_t *stamp = call(request("describe", path)), *message, *result;
    char job[33]; unsigned i;
    if (!stamp || !value) { tiku_msg_free(stamp); return -1; }
    message = request(invoke ? "invoke" : "set", path);
    if (!message || !expected(message, stamp) || !tiku_msg_add_message(message, invoke ? "arguments" : "value", value) ||
        (!invoke && !tiku_msg_add_string(message, "durability", "user")) || !tiku_msg_add_int32(message, "deadline_ms", 3000)) {
        tiku_msg_free(message); tiku_msg_free(stamp); return -1;
    }
    tiku_msg_free(stamp); result = call(message);
    if (!result) return -1;
    if (!tiku_msg_find_string(result, "job", 0)) { tiku_msg_free(result); return -1; }
    snprintf(job, sizeof job, "%s", tiku_msg_find_string(result, "job", 0)); tiku_msg_free(result);
    for (i = 0; i < 500; i++) {
        const char *state;
        message = request("job-status", NULL);
        if (!message || !tiku_msg_add_string(message, "job", job)) { tiku_msg_free(message); return -1; }
        result = call(message); if (!result) return -1;
        state = tiku_msg_find_string(result, "state", 0);
        if (state && !strcmp(state, "succeeded")) {
            if(print_result) {
                tiku_msg_t *snapshot=tiku_msg_find_message(result,"snapshot",0);
                tiku_msg_t *applied=tiku_msg_find_message(snapshot,"applied",0);char text[16000];
                int size=applied?tiku_resource_value_render(applied,text,sizeof text):-1;
                tiku_msg_free(applied);tiku_msg_free(snapshot);
                if(size<0){tiku_msg_free(result);return -1;}printf("%.*s\n",size,text);
            }
            tiku_msg_free(result); return 0;
        }
        if (!state || (strcmp(state, "queued") && strcmp(state, "running"))) {
            fprintf(stderr, "settings job: %s %s %s\n", state ? state : "invalid", tiku_msg_find_string(result,"code",0), tiku_msg_find_string(result,"effect",0)); tiku_msg_free(result); return -1;
        }
        tiku_msg_free(result); usleep(10000);
    }
    return -1;
}
/** @brief Render saved and active values without interpreting private native models. */
static int read_value(const char *path)
{
    const char *const names[] = {"desired", "persisted", "applied"};
    tiku_msg_t *result = call(request("read", path));
    unsigned i;
    if (!result) return -1;
    printf("{\"apply_state\":\"%s\"", tiku_msg_find_string(result, "apply_state", 0));
    for (i = 0; i < 3; i++) {
        char text[16000]; tiku_msg_t *value = tiku_msg_find_message(result, names[i], 0);
        int n = value ? tiku_resource_value_render(value, text, sizeof text) : -1;
        tiku_msg_free(value);
        if (n < 0) { tiku_msg_free(result); return -1; }
        printf(",\"%s\":%.*s", names[i], n, text);
    }
    puts("}"); tiku_msg_free(result); return 0;
}
/** @brief Verify expected discovery kinds and typed observations from a supplied path list. */
static int audit(const char *path)
{
    FILE *file = fopen(path, "r");
    char line[1024]; int count = 0, error = 0;
    if (!file) return -1;
    while (fgets(line, sizeof line, file)) {
        char *tab = strchr(line, '\t'), *newline;
        tiku_msg_t *description, *snapshot = NULL;
        const char *kind;
        if (!tab) { error = 1; break; }
        *tab++ = 0; newline = strchr(tab, '\n'); if (newline) *newline = 0;
        description = call(request("describe", line));
        kind = description ? tiku_msg_find_string(description, "kind", 0) : NULL;
        if (!kind || strcmp(kind, tab)) { fprintf(stderr, "missing catalog contract: %s %s\n", line, tab); tiku_msg_free(description); error = 1; break; }
        if (!strcmp(kind, "setting") || !strcmp(kind, "state")) {
            const char *const parts[] = {"desired", "persisted", "applied"}; unsigned i;
            snapshot = call(request("read", line));
            for (i = 0; i < 3; i++) {
                tiku_msg_t *value = snapshot ? tiku_msg_find_message(snapshot, parts[i], 0) : NULL;
                if (!value || tiku_resource_value_check(value)) error = 1;
                tiku_msg_free(value);
            }
        }
        tiku_msg_free(snapshot); tiku_msg_free(description); if (error) break; count++;
    }
    if (ferror(file)) error = 1;
    if (fclose(file)) error = 1;
    if (!error) printf("%d catalog bindings verified\n", count);
    return error ? -1 : 0;
}
/** @brief Read a complete frozen newline-delimited JSON snapshot through bounded leases. */
static int stream_value(const char *path)
{
    tiku_msg_t *stamp=call(request("describe",path)), *message, *result;
    char lease[33], offset[32]; uint64_t length, at=0;
    if (!stamp) return -1;
    message=request("stream-open",path);
    if (!message || !expected(message,stamp) || !tiku_msg_add_string(message,"maximum_bytes","4194304")) { tiku_msg_free(message); tiku_msg_free(stamp); return -1; }
    tiku_msg_free(stamp); result=call(message);
    if (!result || !tiku_msg_find_string(result,"lease",0) || tiku_resource_counter_parse(tiku_msg_find_string(result,"bytes",0),&length)) { tiku_msg_free(result); return -1; }
    snprintf(lease,sizeof lease,"%s",tiku_msg_find_string(result,"lease",0)); tiku_msg_free(result);
    while (at<length) {
        size_t bytes; const void *data;
        message=request("stream-read",NULL); snprintf(offset,sizeof offset,"%llu",(unsigned long long)at);
        if (!message || !tiku_msg_add_string(message,"lease",lease) || !tiku_msg_add_string(message,"offset",offset) || !tiku_msg_add_int32(message,"byte_count",32768)) { tiku_msg_free(message); return -1; }
        result=call(message); data=tiku_msg_find_blob(result,"data",0,&bytes);
        if (!data || !bytes || bytes>length-at || fwrite(data,1,bytes,stdout)!=bytes) { tiku_msg_free(result); return -1; }
        at+=bytes; tiku_msg_free(result);
    }
    message=request("stream-close",NULL);
    if (!message || !tiku_msg_add_string(message,"lease",lease)) { tiku_msg_free(message); return -1; }
    result=call(message); if (!result) return -1; tiku_msg_free(result); return 0;
}

/** @brief Enumerate the complete catalog through consumed page cursors. */
static int list_values(const char *path)
{
    char cursor[33] = "";tiku_resource_protocol_limits_t limits;
    if(tiku_resource_channel_limits(channel,&limits))return -1;
    do {
        tiku_msg_t *message=request("list",path), *result; int i;
        if (!message || !tiku_msg_add_int32(message,"page_size",(int32_t)limits.page_entries) || (*cursor && !tiku_msg_add_string(message,"cursor",cursor))) { tiku_msg_free(message); return -1; }
        result=call(message); if (!result) return -1;
        for (i=0;i<tiku_msg_count(result,"entry");i++) {
            tiku_msg_t *entry=tiku_msg_find_message(result,"entry",i);
            printf("%s\t%s\n",tiku_msg_find_string(entry,"path",0),tiku_msg_find_string(entry,"kind",0)); tiku_msg_free(entry);
        }
        snprintf(cursor,sizeof cursor,"%s",tiku_msg_find_string(result,"next_cursor",0) ? tiku_msg_find_string(result,"next_cursor",0) : "");
        tiku_msg_free(result);
    } while (*cursor);
    return 0;
}

/** @brief Build checked scalar fields without access to native C record structures. */
static tiku_msg_t *integer_object(const char *const *keys, int count, char **arguments)
{
    tiku_msg_t *out=tiku_msg_new(0); int i;
    if (!out || !tiku_msg_add_string(out,"type","object")) goto fail;
    for (i=0;i<count;i++) {
        char *end; long long number; errno=0; number=strtoll(arguments[i],&end,10);
        if (errno || end==arguments[i] || *end || !member(out,keys[i],tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,number))) goto fail;
    }
    return out;
fail: tiku_msg_free(out); return NULL;
}

/** @brief Build one named versioned record from its public typed fields. */
static tiku_msg_t *record_value(const char *kind, int count, char **arguments)
{
    static const char *const frame_keys[]={"x","y","width","height"}, *const pose_keys[]={"x","y","flags"}, *const dock_keys[]={"dock","width"}, *const position_keys[]={"x","y"};
    tiku_msg_t *out=NULL,*item=NULL;
    if(!strcmp(kind,"object")&&count%2==0) {
        out=tiku_msg_new(0);int valid=out&&tiku_msg_add_string(out,"type","object");
        for(int i=0;i<count&&valid;i+=2) {
            const char *key=arguments[i]+2;tiku_msg_t *value=NULL;
            if(arguments[i][0]=='t'&&arguments[i][1]==':')value=tiku_resource_value_string(TIKU_RESOURCE_TEXT,arguments[i+1]);
            else if(arguments[i][0]=='i'&&arguments[i][1]==':') {
                char *end;errno=0;long long number=strtoll(arguments[i+1],&end,10);
                if(!errno&&end!=arguments[i+1]&&!*end)value=tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,number);
            }
            valid=*key&&member(out,key,value);
        }
        if(valid)return out;
    } else if(!strcmp(kind,"document")&&count==1) {
        out=tiku_msg_new(0);
        if(out&&tiku_msg_add_string(out,"type","object")&&member(out,"path",tiku_resource_value_string(TIKU_RESOURCE_TEXT,arguments[0])))return out;
    } else if(!strcmp(kind,"script")&&count>=5&&(count-5)%2==0) {
        static const char *const keys[]={"window","generation","order_token","property","specifier"};
        out=tiku_msg_new(0);int valid=out&&tiku_msg_add_string(out,"type","object");
        for(int i=0;i<5&&valid;i++)valid=member(out,keys[i],tiku_resource_value_string(TIKU_RESOURCE_TEXT,arguments[i]));
        for(int i=5;i<count&&valid;i+=2) {
            tiku_msg_t *value;
            if(!strcmp(arguments[i],"index")||!strcmp(arguments[i],"end")) {
                char *end;errno=0;long long number=strtoll(arguments[i+1],&end,10);
                if(errno||end==arguments[i+1]||*end){valid=0;break;}
                value=tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,number);
            } else value=tiku_resource_value_string(TIKU_RESOURCE_TEXT,arguments[i+1]);
            valid=member(out,arguments[i],value);
        }
        if(valid)return out;
    } else if (!strcmp(kind,"resize") && count==2) {
        static const char *const keys[]={"width","height"}; return integer_object(keys,2,arguments);
    }
    if (!strcmp(kind,"frame") && count==4) return integer_object(frame_keys,4,arguments);
    if (!strcmp(kind,"position") && count==2) return integer_object(position_keys,2,arguments);
    if (!strcmp(kind,"dock") && count==2) return integer_object(dock_keys,2,arguments);
    if (!strcmp(kind,"pose") && count==3) {
        out=integer_object(pose_keys,3,arguments);
        if (out && member(out,"version",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,2))) return out;
    } else if (!strcmp(kind,"view") && count==2) {
        static const char *const keys[]={"icon_size"}; out=integer_object(keys,1,arguments+1);
        if (out && member(out,"version",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,1)) && member(out,"mode",tiku_resource_value_string(TIKU_RESOURCE_TEXT,arguments[0]))) return out;
    } else if (!strcmp(kind,"session") && count==1) {
        item=tiku_msg_new(0); out=tiku_msg_new(0);
        if (item && out && tiku_msg_add_string(item,"type","object") && tiku_msg_add_string(out,"type","array") &&
            member(item,"path",tiku_resource_value_string(TIKU_RESOURCE_TEXT,arguments[0])) &&
            member(item,"minimized",tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,0)) && tiku_msg_add_message(out,"item",item)) { tiku_msg_free(item); return out; }
    } else if (!strcmp(kind,"columns") && count==1) {
        static const char *const keys[]={"width"}; item=integer_object(keys,1,arguments); out=tiku_msg_new(0);
        if (item && out && tiku_msg_add_string(out,"type","array") && member(item,"version",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,2)) &&
            member(item,"field",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,0)) && member(item,"offset",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,0)) &&
            member(item,"alignment",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,0)) && member(item,"attribute",tiku_resource_value_string(TIKU_RESOURCE_TEXT,"")) &&
            tiku_msg_add_message(out,"item",item)) { tiku_msg_free(item); return out; }
    }
    tiku_msg_free(item); tiku_msg_free(out); return NULL;
}

int main(int argc, char **argv)
{
    tiku_msg_t *value=NULL; int status=1;
    if (argc<4) return 2;
    channel=tiku_resource_endpoint_connect(argv[1],5000);
    if (!channel || tiku_resource_channel_negotiate(channel,203,0,5000)) goto done;
    if (argc==4 && !strcmp(argv[2],"list")) { status=list_values(argv[3])?1:0; goto done; }
    if (argc==4 && !strcmp(argv[2],"read")) { status=read_value(argv[3])?1:0; goto done; }
    if (argc==4 && !strcmp(argv[2],"stream")) { status=stream_value(argv[3])?1:0; goto done; }
    if (argc==4 && !strcmp(argv[2],"audit")) { status=audit(argv[3])?1:0; goto done; }
    if (argc>=4 && (!strcmp(argv[2],"action")||!strcmp(argv[2],"action-result"))) {
        print_result=!strcmp(argv[2],"action-result");
        value=argc==4?tiku_msg_new(0):argc>=5?record_value(argv[4],argc-5,argv+5):NULL;
        if (argc==4 && value && !tiku_msg_add_string(value,"type","object")) { tiku_msg_free(value);value=NULL; }
        if (value && !change(argv[3],value,1)) status=0;
    } else if (argc>=5 && !strcmp(argv[2],"record")) {
        value=record_value(argv[4],argc-5,argv+5); if (value && !change(argv[3],value,0)) status=0;
    } else if (argc==4 && !strcmp(argv[2],"invoke")) {
        value=tiku_msg_new(0); if (value && tiku_msg_add_string(value,"type","object") && !change(argv[3],value,1)) status=0;
    } else if (argc==7 && !strcmp(argv[2],"background")) {
        value=background(argv+3); if (value && !change("/settings/desktop/bg",value,0)) status=0;
    }
done: tiku_msg_free(value); tiku_resource_channel_free(channel); return status;
}
