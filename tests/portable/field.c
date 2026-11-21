/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * field.c - explicit clipboard ownership and atomic field transfers.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_field.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern unsigned tiku_service_heap_calls;
/** @brief Independent bounded clipboard with injected transfer refusals. */
typedef struct { char text[256]; unsigned reads, writes; int failure; } clipboard_t;
/** @brief Copy one fixture transfer or return a selected malformed length. */
static int read_text(void *context, char *text, size_t capacity, size_t *bytes)
{
    clipboard_t *c=context; ++c->reads;
    if(c->failure==1) return 0;
    size_t n=strlen(c->text); assert(n<capacity);
    memcpy(text,c->text,n+1); *bytes=n;
    if(c->failure==2) *bytes=capacity;
    if(c->failure==3) text[n]='!';
    if(c->failure==4 && n>1) text[1]=0;
    return 1;
}
/** @brief Publish only on success; retain the preceding clipboard on failure. */
static int write_text(void *context, const char *text, size_t bytes)
{
    clipboard_t *c=context; ++c->writes;
    if(c->failure) return 0;
    assert(bytes<sizeof c->text && text[bytes]==0);
    memcpy(c->text,text,bytes+1); return 1;
}
/** @brief Exercise the model with two separate clipboard contexts. */
int main(void)
{
    clipboard_t a={.text="previous"}, b={.text="second"};
    tiku_field_clipboard_t ca={&a,read_text,write_text}, cb={&b,read_text,write_text};
    tiku_field_t field, before;
    tiku_field_start(&field,"caf\xc3\xa9"); before=field;
    assert(tiku_field_key(&field,TIKU_FIELD_COPY,0,0)==TIKU_FIELD_REJECTED);
    assert(!memcmp(&field,&before,sizeof field));
    assert(tiku_field_key_with_clipboard(&field,TIKU_FIELD_COPY,0,0,&ca)==TIKU_FIELD_HANDLED);
    assert(!strcmp(a.text,"caf\xc3\xa9") && !strcmp(b.text,"second") && a.writes==1 && !b.writes);
    a.failure=1;
    assert(tiku_field_key_with_clipboard(&field,TIKU_FIELD_CUT,0,0,&ca)==TIKU_FIELD_REJECTED);
    assert(!memcmp(&field,&before,sizeof field));
    a.failure=0;
    assert(tiku_field_key_with_clipboard(&field,TIKU_FIELD_CUT,0,0,&ca)==TIKU_FIELD_HANDLED);
    assert(!field.len && !field.text[0]);
    assert(tiku_field_key_with_clipboard(&field,TIKU_FIELD_PASTE,0,0,&ca)==TIKU_FIELD_HANDLED);
    assert(!strcmp(field.text,"caf\xc3\xa9"));
    assert(tiku_field_key(&field,TIKU_FIELD_LEFT,0,0)==TIKU_FIELD_HANDLED && field.caret==3);
    assert(tiku_field_key(&field,TIKU_FIELD_DELETE,0,0)==TIKU_FIELD_HANDLED && !strcmp(field.text,"caf"));
    assert(tiku_field_key(&field,TIKU_FIELD_SELECT_ALL,0,0)==TIKU_FIELD_HANDLED);
    assert(tiku_field_key_with_clipboard(&field,TIKU_FIELD_PASTE,0,0,&cb)==TIKU_FIELD_HANDLED);
    assert(!strcmp(field.text,"second") && b.reads==1);
    for(int failure=1;failure<=4;++failure) {
        b.failure=failure; before=field;
        assert(tiku_field_key_with_clipboard(&field,TIKU_FIELD_PASTE,0,0,&cb)==TIKU_FIELD_REJECTED);
        assert(!memcmp(&field,&before,sizeof field));
    }
    b.failure=0; tiku_field_deny(&field,"/"); before=field;
    strcpy(b.text,"bad/name");
    assert(tiku_field_key_with_clipboard(&field,TIKU_FIELD_PASTE,0,0,&cb)==TIKU_FIELD_REJECTED);
    assert(!memcmp(&field,&before,sizeof field));
    strcpy(b.text,"\xc0\xaf");
    assert(tiku_field_key_with_clipboard(&field,TIKU_FIELD_PASTE,0,0,&cb)==TIKU_FIELD_REJECTED);
    assert(!memcmp(&field,&before,sizeof field));
    strcpy(b.text,"line\nbreak");
    assert(tiku_field_key_with_clipboard(&field,TIKU_FIELD_PASTE,0,0,&cb)==TIKU_FIELD_REJECTED);
    assert(!memcmp(&field,&before,sizeof field));
    assert(tiku_field_key(&field,TIKU_FIELD_SELECT_ALL,0,0)==TIKU_FIELD_HANDLED);
    tiku_field_set_editable(&field,0); before=field;
    unsigned reads=b.reads,writes=b.writes;
    assert(tiku_field_key_with_clipboard(&field,TIKU_FIELD_CUT,0,0,&cb)==TIKU_FIELD_HANDLED);
    assert(tiku_field_key_with_clipboard(&field,TIKU_FIELD_PASTE,0,0,&cb)==TIKU_FIELD_HANDLED);
    assert(!memcmp(&field,&before,sizeof field) && b.reads==reads && b.writes==writes);
    tiku_field_start(&field,"x");
    memset(b.text,'z',255);b.text[255]=0;
    assert(tiku_field_key_with_clipboard(&field,TIKU_FIELD_PASTE,0,0,&cb)==TIKU_FIELD_HANDLED);
    assert(field.len==255 && !strcmp(field.text,b.text));
    before=field;
    assert(tiku_field_key_with_clipboard(&field,TIKU_FIELD_PASTE,0,0,&cb)==TIKU_FIELD_REJECTED);
    assert(!memcmp(&field,&before,sizeof field));
    tiku_field_start(&field,field.text); assert(field.len==255 && field.sel_a==0 && field.sel_b==255);
    assert(!tiku_service_heap_calls);
    puts("Field provider: isolated clipboards, refusal, UTF-8, limits and no heap passed");
    return 0;
}
