/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_native_record.c - typed native record round trips and malformed replacements.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_native_record.h"
#include "tiku_trk_resource_values.h"
#include "poseview/tiku_trk_poseview_private.h"
#include "tiku_trk_frame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(test) do { if (!(test)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#test); exit(1); } } while (0)

/** @brief Verify a native record preserves its interpreted public fields after encoding. */
static void round_trip(tiku_trk_native_record_kind_t kind, const void *bytes, size_t length)
{
    unsigned char buffer[8192]; size_t count;
    tiku_msg_t *decoded=tiku_trk_native_record_decode(kind,bytes,length), *again;
    CHECK(decoded && !tiku_resource_value_check(decoded));
    CHECK(!tiku_trk_native_record_encode(kind,decoded,buffer,sizeof buffer,&count));
    again=tiku_trk_native_record_decode(kind,buffer,count); CHECK(again && tiku_resource_value_equal(decoded,again));
    tiku_msg_free(again); tiku_msg_free(decoded);
}

int main(void)
{
    tiku_trk_frame_t frame={100,120,640,400}; int32_t position[]={30,40},dock[]={4,180};
    pose_info_t pose={-50000,50000,1}; pose_info_v1_t old_pose={42,-72};
    column_rec_t columns[2]={{.key=COLUMN_REC_KEY,.version=COLUMN_REC_VERSION,.field=0,.offset=0,.width=180,.alignment=0},
        {.key=COLUMN_REC_KEY,.version=COLUMN_REC_VERSION,.field=16,.offset=180,.width=120,.alignment=1,.attr="document:title"}};
    tiku_msg_t *value; unsigned char buffer[8192]; size_t length; int kind;
    for(kind=TIKU_TRK_RECORD_COLUMNS;kind<TIKU_TRK_RECORD_ORIGINAL;kind++) round_trip(kind,NULL,0);
    round_trip(TIKU_TRK_RECORD_FRAME,&frame,sizeof frame); round_trip(TIKU_TRK_RECORD_POSITION,position,sizeof position);
    round_trip(TIKU_TRK_RECORD_DOCK,dock,sizeof dock); round_trip(TIKU_TRK_RECORD_POSE,&pose,sizeof pose); round_trip(TIKU_TRK_RECORD_POSE,&old_pose,sizeof old_pose);
    round_trip(TIKU_TRK_RECORD_COLUMNS,columns,sizeof columns);
    round_trip(TIKU_TRK_RECORD_VIEW,"icon 64 3 -4 5 6 -7 0 -1 1 0",strlen("icon 64 3 -4 5 6 -7 0 -1 1 0"));
    round_trip(TIKU_TRK_RECORD_VIEW,"mini",4);
    round_trip(TIKU_TRK_RECORD_SESSION,"M /tmp/one space\nO /tmp/two\n",strlen("M /tmp/one space\nO /tmp/two\n"));
    round_trip(TIKU_TRK_RECORD_REPLICANTS,"value\t/sys/fixture\t12\n",strlen("value\t/sys/fixture\t12\n"));
    CHECK(!tiku_trk_native_record_decode(TIKU_TRK_RECORD_FRAME,&frame,sizeof frame-1));
    frame.w=179; CHECK(!tiku_trk_native_record_decode(TIKU_TRK_RECORD_FRAME,&frame,sizeof frame));
    pose.x=50001; CHECK(!tiku_trk_native_record_decode(TIKU_TRK_RECORD_POSE,&pose,sizeof pose));
    columns[1].field=0; columns[1].attr[0]=0; CHECK(!tiku_trk_native_record_decode(TIKU_TRK_RECORD_COLUMNS,columns,sizeof columns)); columns[1].field=16; strcpy(columns[1].attr,"document:title");
    columns[0].field=TIKU_TRK_COL_SIZE; CHECK(!tiku_trk_native_record_decode(TIKU_TRK_RECORD_COLUMNS,columns,sizeof columns)); columns[0].field=TIKU_TRK_COL_NAME;
    columns[1].version=3; CHECK(!tiku_trk_native_record_decode(TIKU_TRK_RECORD_COLUMNS,columns,sizeof columns)); columns[1].version=2;
    columns[1].width=-1; CHECK(!tiku_trk_native_record_decode(TIKU_TRK_RECORD_COLUMNS,columns,sizeof columns)); columns[1].width=120;
    CHECK(!tiku_trk_native_record_decode(TIKU_TRK_RECORD_VIEW,"icon 9999999999999999999999999",30));
    CHECK(!tiku_trk_native_record_decode(TIKU_TRK_RECORD_VIEW,"icon 64 0 50001",15));
    CHECK(!tiku_trk_native_record_decode(TIKU_TRK_RECORD_REPLICANTS,"bad\t/sys/test\t13\n",strlen("bad\t/sys/test\t13\n")));
    value=tiku_trk_native_record_decode(TIKU_TRK_RECORD_POSITION,position,sizeof position); CHECK(value);
    CHECK(!trk_value_member(value,"extra",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,1)));
    CHECK(tiku_trk_native_record_encode(TIKU_TRK_RECORD_POSITION,value,buffer,sizeof buffer,&length)); tiku_msg_free(value);
    value=trk_value_container("object"); CHECK(!trk_value_member(value,"x",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,INT64_MAX)));
    CHECK(!trk_value_member(value,"y",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,0)));
    CHECK(tiku_trk_native_record_encode(TIKU_TRK_RECORD_POSITION,value,buffer,sizeof buffer,&length)); tiku_msg_free(value);
    puts("PASS native record codecs: versions, fields, native sizes, bounds and complete round trips"); return 0;
}
