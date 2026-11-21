/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * field_event.c - provider refusal, selection and atomic undo contracts.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_field_event.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern unsigned tiku_service_heap_calls;
/** @brief Fixture clipboard retaining its preceding value on refusal. */
typedef struct { char text[256]; int refuse; unsigned reads, writes; } clip_t;
/** @brief Return one bounded transfer. */
static int read_text(void *ctx,char *out,size_t cap,size_t *bytes)
{
    clip_t *c=ctx; ++c->reads;
    if(c->refuse) return 0;
    *bytes=strlen(c->text); assert(*bytes<cap);
    memcpy(out,c->text,*bytes+1); return 1;
}
/** @brief Publish a selection when the fixture accepts it. */
static int write_text(void *ctx,const char *text,size_t bytes)
{
    clip_t *c=ctx; ++c->writes;
    if(c->refuse) return 0;
    assert(bytes<sizeof c->text); memcpy(c->text,text,bytes+1); return 1;
}
/** @brief Route one keyboard event using the explicit clipboard. */
static tiku_field_result_t key(tiku_field_t *f,tiku_field_input_t *in,
    const tiku_field_clipboard_t *c,unsigned code,unsigned mods)
{
    tiku_input_event_t e={.type=TIKU_EVENT_KEY_DOWN,.key=code,.modifiers=mods};
    return tiku_field_event_with_clipboard(f,in,&e,(tiku_rect_t){0,0,40,32},1,c);
}
/** @brief Exercise real provider failures before mutation and history changes. */
int main(void)
{
    tiku_field_t f,before; tiku_field_input_t in,history;
    clip_t c={.text="older"}; tiku_field_clipboard_t provider={&c,read_text,write_text};
    tiku_field_start(&f,"caf\xc3\xa9");tiku_field_input_init(&in);
    c.refuse=1;before=f;history=in;
    assert(key(&f,&in,&provider,'x',TIKU_MOD_CMD)==TIKU_FIELD_REJECTED);
    assert(!memcmp(&f,&before,sizeof f) && !memcmp(&in,&history,sizeof in));
    assert(!strcmp(c.text,"older")); c.refuse=0;
    assert(key(&f,&in,&provider,'x',TIKU_MOD_CMD)==TIKU_FIELD_HANDLED);
    assert(!f.len && !strcmp(c.text,"caf\xc3\xa9") && in.nundo==1);
    key(&f,&in,&provider,'z',TIKU_MOD_CMD);
    assert(!strcmp(f.text,"caf\xc3\xa9") && f.sel_a==0 && f.sel_b==5 && in.nredo==1);
    before=f;history=in;c.refuse=1;
    assert(key(&f,&in,&provider,'v',TIKU_MOD_CMD)==TIKU_FIELD_REJECTED);
    assert(!memcmp(&f,&before,sizeof f) && !memcmp(&in,&history,sizeof in));c.refuse=0;
    key(&f,&in,&provider,'z',TIKU_MOD_CMD|TIKU_MOD_SHIFT);assert(!f.len);
    key(&f,&in,&provider,'v',TIKU_MOD_CMD);assert(!strcmp(f.text,c.text));
    key(&f,&in,&provider,TIKU_KEY_LEFT,0);assert(f.caret==3);
    key(&f,&in,&provider,TIKU_KEY_DELETE,0);assert(!strcmp(f.text,"caf"));
    key(&f,&in,&provider,'z',TIKU_MOD_CMD);assert(!strcmp(f.text,"caf\xc3\xa9"));
    key(&f,&in,&provider,'a',TIKU_MOD_CMD);tiku_field_deny(&f,"/");
    strcpy(c.text,"bad/name");before=f;history=in;
    assert(key(&f,&in,&provider,'v',TIKU_MOD_CMD)==TIKU_FIELD_REJECTED);
    assert(!memcmp(&f,&before,sizeof f) && !memcmp(&in,&history,sizeof in));
    strcpy(c.text,"\xc0\xaf");assert(key(&f,&in,&provider,'v',TIKU_MOD_CMD)==TIKU_FIELD_REJECTED);
    assert(!memcmp(&f,&before,sizeof f) && !memcmp(&in,&history,sizeof in));
    tiku_field_set_editable(&f,0);before=f;history=in;unsigned reads=c.reads,writes=c.writes;
    key(&f,&in,&provider,'x',TIKU_MOD_CMD);key(&f,&in,&provider,'v',TIKU_MOD_CMD);
    key(&f,&in,&provider,'z',TIKU_MOD_CMD);
    assert(!memcmp(&f,&before,sizeof f) && !memcmp(&in,&history,sizeof in));
    assert(c.reads==reads && c.writes==writes);
    tiku_field_set_editable(&f,1);
    assert(key(&f,&in,NULL,'x',TIKU_MOD_CMD)==TIKU_FIELD_REJECTED);
    assert(key(&f,&in,&provider,TIKU_KEY_RETURN,0)==TIKU_FIELD_COMMITTED && f.active);
    assert(key(&f,&in,&provider,TIKU_KEY_ESCAPE,0)==TIKU_FIELD_CANCELLED && f.active);
    assert(!tiku_service_heap_calls);
    puts("Field events: refused transfers, UTF-8, selection, undo/redo and no heap passed");
    return 0;
}
