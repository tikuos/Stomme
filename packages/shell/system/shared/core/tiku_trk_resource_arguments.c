/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_resource_arguments.c - exact typed arguments and canonical byte
 * chunks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_resource_arguments.h"
#include <limits.h>

int trk_arguments_keys(const tiku_msg_t *value, const char *const *keys,
                       size_t count)
{
    const char *type=tiku_msg_find_string(value,"type",0);uint64_t seen=0;
    if(!type||strcmp(type,"object")||count>64)return 0;
    for(int i=0;i<tiku_msg_count(value,"member");i++) {
        tiku_msg_t *member = tiku_msg_find_message(value, "member", i);
        const char *key = tiku_msg_find_string(member, "key", 0);
        size_t at;
        for(at=0;at<count;at++)if(key&&!strcmp(key,keys[at]))break;
        tiku_msg_free(member);
        if (at == count || (seen & (UINT64_C(1) << at)))
            return 0;
        seen |= UINT64_C(1) << at;
    }
    return 1;
}
int trk_arguments_text(const tiku_msg_t *value, const char *key, char *out,
                       size_t capacity)
{
    tiku_msg_t *member = trk_value_find(value, key);
    const char *type = tiku_msg_find_string(member, "type", 0),
               *text = tiku_msg_find_string(member, "value", 0);
    int valid=type&&!strcmp(type,"text")&&text&&strlen(text)<capacity;
    if(valid)memcpy(out,text,strlen(text)+1);
    tiku_msg_free(member);return valid;
}
int trk_arguments_integer(const tiku_msg_t *value, const char *key,
                          int64_t low, int64_t high, int64_t *out)
{
    tiku_msg_t *member = trk_value_find(value, key);
    const char *type = tiku_msg_find_string(member, "type", 0);
    int64_t number;
    int valid = out && type && !strcmp(type, "integer") &&
                tiku_msg_find_int64(member, "value", 0, &number) &&
                number >= low && number <= high;
    if(valid)*out=number;
    tiku_msg_free(member);return valid;
}
int trk_arguments_counter(const tiku_msg_t *value,const char *key,uint64_t *out)
{
    char text[32];
    return trk_arguments_text(value, key, text, sizeof text) &&
           !tiku_resource_counter_parse(text, out);
}
int trk_arguments_has(const tiku_msg_t *value,const char *key)
{
    tiku_msg_t *member = trk_value_find(value, key);
    int present = member != NULL;
    tiku_msg_free(member);
    return present;
}
/** @brief Decode one base64 alphabet byte, excluding padding. */
static int digit(unsigned char c)
{
    if(c>='A'&&c<='Z')return c-'A';
    if(c>='a'&&c<='z')return c-'a'+26;
    if(c>='0'&&c<='9')return c-'0'+52;
    return c=='+'?62:c=='/'?63:-1;
}
int trk_base64_decode(const char *text,void *out,size_t capacity)
{
    if(!text||(!out&&capacity))return -1;
    size_t size = strlen(text), used = 0;
    unsigned char *bytes = out;
    if (size % 4)
        return -1;
    for(size_t i=0;i<size;i+=4) {
        int a = digit(text[i]), b = digit(text[i + 1]),
            c = text[i + 2] == '=' ? 0 : digit(text[i + 2]),
            d = text[i + 3] == '=' ? 0 : digit(text[i + 3]);
        int pad=text[i+2]=='='?2:text[i+3]=='='?1:0;size_t n=(size_t)(3-pad);
        if (a < 0 || b < 0 || c < 0 || d < 0 || (pad && i + 4 != size) ||
            (pad == 2 && text[i + 3] != '=') || (pad == 2 && (b & 15)) ||
            (pad == 1 && (c & 3)) || used > capacity || n > capacity - used)
            return -1;
        if(n)bytes[used++]=(unsigned char)((a<<2)|(b>>4));
        if(n>1)bytes[used++]=(unsigned char)((b<<4)|(c>>2));
        if(n>2)bytes[used++]=(unsigned char)((c<<6)|d);
    }
    return used>INT_MAX?-1:(int)used;
}
int trk_base64_encode(const void *input,size_t size,char *out,size_t capacity)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const unsigned char *bytes=input;size_t used=0;
    if ((!input && size) || !out || size > SIZE_MAX - 2 ||
        (size + 2) / 3 > (SIZE_MAX - 1) / 4 ||
        capacity < 4 * ((size + 2) / 3) + 1)
        return -1;
    for(size_t i=0;i<size;i+=3) {
        unsigned a=bytes[i],b=i+1<size?bytes[i+1]:0,c=i+2<size?bytes[i+2]:0;
        out[used++]=alphabet[a>>2];out[used++]=alphabet[((a&3)<<4)|(b>>4)];
        out[used++] =
            i + 1 < size ? alphabet[((b & 15) << 2) | (c >> 6)] : '=';
        out[used++] = i + 2 < size ? alphabet[c & 63] : '=';
    }
    out[used]=0;return used>INT_MAX?-1:(int)used;
}
