/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * word_checksum.c - literal byte identity and bounded desktop checksum decoding.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_word_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/** @brief Check output boundaries, rejected inputs and the named CRC vectors. */
int main(void)
{
    struct {unsigned char before,bytes[255],after;} out={.before=0x17,.after=0x89};size_t n;
    assert(tiku_word_checksum_bytes("123456789",0,out.bytes,&n)&&n==9);
    assert(tiku_word_crc32(out.bytes,n)==0xcbf43926&&tiku_word_crc16(out.bytes,n)==0x29b1);
    assert(tiku_word_checksum_bytes("31 32 33 34 35 36 37 38 39",1,out.bytes,&n)&&n==9);
    assert(tiku_word_crc32(out.bytes,n)==0xcbf43926&&tiku_word_crc16(out.bytes,n)==0x29b1);
    const char literal[]="é\n\t ";assert(tiku_word_checksum_bytes(literal,0,out.bytes,&n)&&n==sizeof literal-1&&!memcmp(literal,out.bytes,n));
    assert(tiku_word_checksum_bytes("",0,out.bytes,&n)&&n==0&&tiku_word_crc32(out.bytes,n)==0&&tiku_word_crc16(out.bytes,n)==0xffff);
    char text[257];memset(text,'A',255);text[255]=0;assert(tiku_word_checksum_bytes(text,0,out.bytes,&n)&&n==255&&!memcmp(text,out.bytes,n));
    n=999;assert(!tiku_word_checksum_bytes(text,1,out.bytes,&n)&&n==999);text[254]=0;
    assert(tiku_word_checksum_bytes(text,1,out.bytes,&n)&&n==127);for(size_t i=0;i<n;++i)assert(out.bytes[i]==0xaa);
    memset(text,' ',255);text[255]=0;assert(tiku_word_checksum_bytes(text,1,out.bytes,&n)&&n==0);
    memset(text,'A',256);text[256]=0;n=999;assert(!tiku_word_checksum_bytes(text,0,out.bytes,&n)&&n==999);assert(!tiku_word_checksum_bytes(text,1,out.bytes,&n)&&n==999);
    const char *bad[]={"1","1 2","GG","0xAA","AA\nBB","é"};for(unsigned i=0;i<sizeof bad/sizeof *bad;++i){n=999;assert(!tiku_word_checksum_bytes(bad[i],1,out.bytes,&n)&&n==999);}
    assert(tiku_word_checksum_bytes("aA\tBb",1,out.bytes,&n)&&n==2&&out.bytes[0]==0xaa&&out.bytes[1]==0xbb);
    assert(!tiku_word_checksum_bytes(NULL,0,out.bytes,&n));assert(!tiku_word_checksum_bytes("",2,out.bytes,&n));assert(!tiku_word_checksum_bytes("",0,NULL,&n));assert(!tiku_word_checksum_bytes("",0,out.bytes,NULL));
    for(unsigned i=1;i<256;++i){char one[2]={(char)i,0};assert(tiku_word_checksum_bytes(one,0,out.bytes,&n)&&n==1&&out.bytes[0]==i);}
    assert(out.before==0x17&&out.after==0x89);puts("Checksum input: 255 literal bytes, 127 hex pairs, exact UTF-8/newline bytes, CRC vectors and admission passed");return 0;
}
