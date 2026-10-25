/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_resource_arguments.c - canonical byte transfer chunks and exact members.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_resource_arguments.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(t) do{if(!(t)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#t);exit(1);}}while(0)
int main(void)
{
    unsigned char bytes[3072],out[3072];char encoded[4097];for(size_t i=0;i<sizeof bytes;i++)bytes[i]=(unsigned char)i;
    for(size_t n=0;n<=sizeof bytes;n++){int size=trk_base64_encode(bytes,n,encoded,sizeof encoded);CHECK(size>=0&&trk_base64_decode(encoded,out,sizeof out)==(int)n&&!memcmp(bytes,out,n));}
    const char *bad[]={"A","AAA","A===","=AAA","AA=A","AB==","AAB=","AA==AAAA","AA==\n","AA_="};
    for(size_t i=0;i<sizeof bad/sizeof bad[0];i++)CHECK(trk_base64_decode(bad[i],out,sizeof out)<0);
    CHECK(trk_base64_decode("AAAA",out,2)<0&&trk_base64_encode(bytes,3,encoded,4)<0);
    const char *keys[]={"name","offset"};tiku_msg_t *value=trk_value_container("object");CHECK(!TRK_TEXT(value,"name","literal space name")&&!TRK_INT(value,"offset",4));
    char text[64];int64_t offset;CHECK(trk_arguments_keys(value,keys,2)&&trk_arguments_text(value,"name",text,sizeof text)&&!strcmp(text,"literal space name")&&trk_arguments_integer(value,"offset",0,8,&offset)&&offset==4);
    CHECK(!TRK_TEXT(value,"unknown","no"));CHECK(!trk_arguments_keys(value,keys,2));tiku_msg_free(value);
    puts("PASS resource arguments: exact members, canonical base64 and bounded round trips");return 0;
}
