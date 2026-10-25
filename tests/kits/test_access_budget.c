/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_access_budget.c - complete native semantic packets within capture budgets.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_access.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(t) do{if(!(t)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#t);exit(1);}}while(0)
int main(void)
{
    tiku_access_node_t node={.parent=TIKU_ACCESS_NO_PARENT,.role=TIKU_ACCESS_DOCUMENT,.state=TIKU_ACCESS_VISIBLE,.name="Fixture",.text="Unicode \xc3\xa9 \xf0\x9f\x99\x82",.selection_start=-1,.selection_end=-1,.caret=-1};
    tiku_access_tree_t *tree=NULL,*copy=NULL;void *packet=NULL,*limited=(void *)1;size_t bytes=0,size=1;
    CHECK(!tiku_access_create(11,23,&node,1,&tree)&&!tiku_access_encode(tree,&packet,&bytes));
    CHECK(tiku_access_encode_limited(tree,bytes-1,&limited,&size)==-3&&!limited&&!size);
    CHECK(!tiku_access_encode_limited(tree,bytes,&limited,&size)&&size==bytes&&!memcmp(packet,limited,size));
    CHECK(!tiku_access_decode(limited,size,&copy)&&tiku_access_document(copy)==11&&tiku_access_generation(copy)==23);
    CHECK(!strcmp(tiku_access_node(copy,0)->text,node.text));free(packet);free(limited);tiku_access_release(tree);tiku_access_release(copy);
    puts("PASS access capture budget: complete native codec or refusal before packet allocation");return 0;
}
