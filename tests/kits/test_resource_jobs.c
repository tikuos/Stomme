/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_resource_jobs.c - async controller deadlines, retention and owner loss.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "resource_fixture.h"

/** @brief Submit a queued request through the general dispatcher. */
static tiku_resource_job_info_t submit(resource_fixture_t *fixture, tiku_resource_service_t *service, unsigned serial)
{
    tiku_resource_job_info_t job;
    tiku_msg_t *request=resource_test_set(fixture,"/work/enabled",&fixture->work,serial,1);
    tiku_msg_t *result=resource_test_call(service,&fixture->caller,request,NULL);
    RES_CHECK(!strcmp(tiku_msg_find_string(result,"state",0),"queued"));
    RES_CHECK(!tiku_resource_job_status(fixture->registry,&fixture->caller,tiku_msg_find_string(result,"job",0),&job));
    tiku_msg_free(result); tiku_msg_free(request);
    return job;
}

/** @brief Read the current state and release any earlier copied result. */
static void refresh(resource_fixture_t *fixture, tiku_resource_job_info_t *job)
{
    char id[33]; strcpy(id,job->id); tiku_resource_job_info_clear(job);
    RES_CHECK(!tiku_resource_job_status(fixture->registry,&fixture->caller,id,job));
}

/** @brief Check queue/start/progress/completion and duplicate request IDs. */
static void lifecycle_test(void)
{
    resource_fixture_t fixture;
    tiku_resource_job_info_t job;
    tiku_resource_snapshot_t value;
    tiku_msg_t *request,*result;
    char id[33];
    resource_test_open(&fixture,0); job=submit(&fixture,fixture.a,1); strcpy(id,job.id);
    RES_CHECK(!fixture.worker->starts && tiku_resource_jobs_pump(fixture.registry)==1 && fixture.worker->starts==1);
    refresh(&fixture,&job); RES_CHECK(job.state==TIKU_RESOURCE_JOB_RUNNING);
    RES_CHECK(!tiku_resource_job_progress(fixture.registry,id,500,1));
    refresh(&fixture,&job); RES_CHECK(job.state==TIKU_RESOURCE_JOB_AWAITING_CONFIRMATION && job.progress==500);
    resource_test_snapshot(&value,1);
    RES_CHECK(!tiku_resource_job_complete(fixture.registry,id,&value,TIKU_RESOURCE_OK,TIKU_RESOURCE_EFFECT_PARTIAL));
    tiku_resource_snapshot_clear(&value);
    refresh(&fixture,&job);
    RES_CHECK(job.state==TIKU_RESOURCE_JOB_SUCCEEDED && job.resource_stamp.revision==2);
    RES_CHECK(tiku_resource_job_complete(fixture.registry,id,NULL,TIKU_RESOURCE_OK,TIKU_RESOURCE_EFFECT_NONE)==TIKU_RESOURCE_UNAVAILABLE);
    request=resource_test_set(&fixture,"/work/enabled",&fixture.work,1,1);
    result=resource_test_call(fixture.b,&fixture.caller,request,NULL);
    RES_CHECK(!strcmp(tiku_msg_find_string(result,"job",0),id) && !strcmp(tiku_msg_find_string(result,"state",0),"succeeded"));
    tiku_msg_free(request);tiku_msg_free(result);
    RES_CHECK(fixture.worker->starts==1);
    request=resource_test_request(&fixture,"request-status",NULL,2);
    RES_CHECK(tiku_msg_add_string(request,"prior_request_id","00000000000000000000000000000001"));
    result=resource_test_call(fixture.b,&fixture.caller,request,NULL);
    RES_CHECK(!strcmp(tiku_msg_find_string(result,"state",0),"succeeded"));
    tiku_msg_free(request);tiku_msg_free(result);
    tiku_resource_job_info_clear(&job); resource_test_close(&fixture);
}

/** @brief Check queued cancellation and one accepted running cancel request. */
static void cancellation_test(void)
{
    resource_fixture_t fixture;
    tiku_resource_job_info_t queued,running;
    tiku_resource_cancel_t cancellation;
    tiku_resource_stamp_t old;
    resource_test_open(&fixture,0); queued=submit(&fixture,fixture.a,1);
    RES_CHECK(!tiku_resource_job_cancel(fixture.registry,&fixture.caller,queued.id,&queued.stamp,&cancellation));
    RES_CHECK(cancellation==TIKU_RESOURCE_CANCEL_ACCEPTED && !fixture.worker->starts && !fixture.worker->cancels);
    refresh(&fixture,&queued); RES_CHECK(queued.state==TIKU_RESOURCE_JOB_CANCELLED && queued.effect==TIKU_RESOURCE_EFFECT_NONE);
    running=submit(&fixture,fixture.a,2); old=running.stamp;
    RES_CHECK(tiku_resource_jobs_pump(fixture.registry)==1); refresh(&fixture,&running);
    RES_CHECK(tiku_resource_job_cancel(fixture.registry,&fixture.caller,running.id,&old,&cancellation)==TIKU_RESOURCE_STALE);
    RES_CHECK(!tiku_resource_job_cancel(fixture.registry,&fixture.caller,running.id,&running.stamp,&cancellation));
    RES_CHECK(cancellation==TIKU_RESOURCE_CANCEL_ACCEPTED && fixture.worker->cancels==1);
    refresh(&fixture,&running); RES_CHECK(running.state==TIKU_RESOURCE_JOB_RUNNING && running.cancel_requested);
    RES_CHECK(!tiku_resource_job_cancel(fixture.registry,&fixture.caller,running.id,&running.stamp,&cancellation) && fixture.worker->cancels==1);
    RES_CHECK(!tiku_resource_job_complete(fixture.registry,running.id,NULL,TIKU_RESOURCE_CANCELLED,TIKU_RESOURCE_EFFECT_NONE));
    refresh(&fixture,&running); RES_CHECK(running.state==TIKU_RESOURCE_JOB_CANCELLED);
    RES_CHECK(!tiku_resource_job_cancel(fixture.registry,&fixture.caller,running.id,&running.stamp,&cancellation));
    RES_CHECK(cancellation==TIKU_RESOURCE_CANCEL_TOO_LATE);
    tiku_resource_job_info_clear(&queued);tiku_resource_job_info_clear(&running);resource_test_close(&fixture);
}

/** @brief Replay a cancellation acknowledgement across broker connections. */
static void cancellation_replay_test(void)
{
    resource_fixture_t fixture;
    tiku_resource_job_info_t job;
    tiku_msg_t *request,*result;
    resource_test_open(&fixture,0);job=submit(&fixture,fixture.a,1);
    RES_CHECK(tiku_resource_jobs_pump(fixture.registry)==1);refresh(&fixture,&job);
    request=resource_test_request(&fixture,"cancel",NULL,10);
    resource_test_expected(request,&job.stamp);
    RES_CHECK(tiku_msg_add_string(request,"job",job.id) && tiku_msg_add_int32(request,"deadline_ms",50));
    result=resource_test_call(fixture.a,&fixture.caller,request,NULL);
    RES_CHECK(!strcmp(tiku_msg_find_string(result,"cancellation",0),"accepted"));tiku_msg_free(result);
    result=resource_test_call(fixture.b,&fixture.caller,request,NULL);
    RES_CHECK(!strcmp(tiku_msg_find_string(result,"cancellation",0),"accepted") && fixture.worker->cancels==1);tiku_msg_free(result);
    tiku_msg_remove(request,"deadline_ms");RES_CHECK(tiku_msg_add_int32(request,"deadline_ms",1));
    result=resource_test_call(fixture.b,&fixture.caller,request,"request_id_reused");tiku_msg_free(result);
    tiku_msg_free(request);tiku_resource_job_info_clear(&job);resource_test_close(&fixture);
}

/** @brief Check acceptance/execution expiry and rejection of late completion. */
static void deadlines_test(void)
{
    resource_fixture_t fixture;
    tiku_resource_job_info_t queued,running;
    tiku_resource_snapshot_t result,read;
    tiku_resource_job_info_t expired;
    resource_test_open(&fixture,0); queued=submit(&fixture,fixture.a,1);
    fixture.now+=51; RES_CHECK(!tiku_resource_jobs_pump(fixture.registry) && !fixture.worker->starts);
    refresh(&fixture,&queued); RES_CHECK(queued.state==TIKU_RESOURCE_JOB_FAILED && queued.error==TIKU_RESOURCE_DEADLINE && queued.effect==TIKU_RESOURCE_EFFECT_NONE);
    running=submit(&fixture,fixture.a,2);RES_CHECK(tiku_resource_jobs_pump(fixture.registry)==1);
    fixture.now+=101;RES_CHECK(!tiku_resource_jobs_pump(fixture.registry));
    refresh(&fixture,&running);RES_CHECK(running.state==TIKU_RESOURCE_JOB_UNKNOWN && running.effect==TIKU_RESOURCE_EFFECT_UNKNOWN);
    resource_test_snapshot(&result,1);
    RES_CHECK(tiku_resource_job_complete(fixture.registry,running.id,&result,TIKU_RESOURCE_OK,TIKU_RESOURCE_EFFECT_PARTIAL)==TIKU_RESOURCE_DEADLINE);
    tiku_resource_snapshot_clear(&result);
    RES_CHECK(tiku_resource_read(fixture.registry,&fixture.caller,"/work/enabled",&read)==TIKU_RESOURCE_UNAVAILABLE);
    fixture.now+=60001;
    RES_CHECK(tiku_resource_job_status(fixture.registry,&fixture.caller,running.id,&expired)==TIKU_RESOURCE_OUTCOME_UNAVAILABLE);
    tiku_resource_job_info_clear(&queued);tiku_resource_job_info_clear(&running);resource_test_close(&fixture);
}

/** @brief Retire the owner while work runs, then reject its late result. */
static void owner_loss_test(void)
{
    resource_fixture_t fixture;
    tiku_resource_job_info_t job;
    tiku_resource_snapshot_t result,read;
    char incarnation[33];
    resource_test_open(&fixture,0);job=submit(&fixture,fixture.a,1);
    RES_CHECK(tiku_resource_jobs_pump(fixture.registry)==1);strcpy(incarnation,fixture.work.incarnation);
    RES_CHECK(!tiku_resource_unregister(fixture.registry,&fixture.work));
    free(fixture.worker);fixture.worker=NULL;
    refresh(&fixture,&job);RES_CHECK(job.state==TIKU_RESOURCE_JOB_UNKNOWN && job.effect==TIKU_RESOURCE_EFFECT_UNKNOWN);
    resource_test_register_work(&fixture);RES_CHECK(strcmp(incarnation,fixture.work.incarnation));
    resource_test_snapshot(&result,1);
    RES_CHECK(tiku_resource_job_complete(fixture.registry,job.id,&result,TIKU_RESOURCE_OK,TIKU_RESOURCE_EFFECT_PARTIAL)==TIKU_RESOURCE_OWNER_LOST);
    RES_CHECK(!tiku_resource_read(fixture.registry,&fixture.caller,"/work/enabled",&read));
    { int enabled=1; RES_CHECK(tiku_msg_find_bool(read.desired,"value",0,&enabled) && !enabled); }
    RES_CHECK(!fixture.worker->starts);
    tiku_resource_snapshot_clear(&read);tiku_resource_snapshot_clear(&result);
    tiku_resource_job_info_clear(&job);
    job=submit(&fixture,fixture.a,2);RES_CHECK(!tiku_resource_unregister(fixture.registry,&fixture.work));
    refresh(&fixture,&job);RES_CHECK(job.state==TIKU_RESOURCE_JOB_FAILED && job.error==TIKU_RESOURCE_OWNER_LOST && job.effect==TIKU_RESOURCE_EFFECT_NONE);
    tiku_resource_job_info_clear(&job);resource_test_close(&fixture);
}

/** @brief Reject stale worker output and support nonblocking immediate completion. */
static void stale_completion_test(void)
{
    resource_fixture_t fixture;
    tiku_resource_job_info_t job;
    tiku_resource_snapshot_t value,read;
    tiku_resource_stamp_t current;
    resource_test_open(&fixture,0);job=submit(&fixture,fixture.a,1);
    RES_CHECK(tiku_resource_jobs_pump(fixture.registry)==1);
    resource_test_snapshot(&value,1);
    RES_CHECK(!tiku_resource_publish(fixture.registry,&fixture.work,"/work/enabled",&value,&current));
    tiku_resource_snapshot_clear(&value);resource_test_snapshot(&value,0);
    RES_CHECK(tiku_resource_job_complete(fixture.registry,job.id,&value,TIKU_RESOURCE_OK,TIKU_RESOURCE_EFFECT_NONE)==TIKU_RESOURCE_STALE);
    refresh(&fixture,&job);RES_CHECK(job.state==TIKU_RESOURCE_JOB_FAILED && job.error==TIKU_RESOURCE_STALE);
    RES_CHECK(!tiku_resource_read(fixture.registry,&fixture.caller,"/work/enabled",&read));
    RES_CHECK(read.stamp.revision==current.revision);
    tiku_resource_snapshot_clear(&read);tiku_resource_snapshot_clear(&value);
    tiku_resource_job_info_clear(&job);resource_test_close(&fixture);
    resource_test_open(&fixture,0);fixture.worker->immediate=1;job=submit(&fixture,fixture.a,1);
    RES_CHECK(tiku_resource_jobs_pump(fixture.registry)==1);refresh(&fixture,&job);
    RES_CHECK(job.state==TIKU_RESOURCE_JOB_SUCCEEDED && job.progress==1000);
    tiku_resource_job_info_clear(&job);resource_test_close(&fixture);
    resource_test_open(&fixture,0);fixture.worker->immediate=2;job=submit(&fixture,fixture.a,1);
    RES_CHECK(tiku_resource_jobs_pump(fixture.registry)==1);refresh(&fixture,&job);
    RES_CHECK(job.state==TIKU_RESOURCE_JOB_UNKNOWN && job.effect==TIKU_RESOURCE_EFFECT_UNKNOWN);
    tiku_resource_job_info_clear(&job);resource_test_close(&fixture);
}

/** @brief Keep pending IDs while full and cancel only a closing connection's queue. */
static void capacity_disconnect_test(void)
{
    resource_fixture_t fixture;
    tiku_resource_job_info_t a,b;
    tiku_msg_t *request,*result;
    resource_test_open(&fixture,1);a=submit(&fixture,fixture.a,1);
    request=resource_test_set(&fixture,"/settings/enabled",&fixture.settings,2,1);
    result=resource_test_call(fixture.b,&fixture.caller,request,"busy");
    RES_CHECK(!fixture.changes);tiku_msg_free(request);tiku_msg_free(result);
    resource_test_close(&fixture);tiku_resource_job_info_clear(&a);
    resource_test_open(&fixture,0);a=submit(&fixture,fixture.a,1);b=submit(&fixture,fixture.b,2);
    tiku_resource_service_free(fixture.a);fixture.a=NULL;
    refresh(&fixture,&a);refresh(&fixture,&b);
    RES_CHECK(a.state==TIKU_RESOURCE_JOB_CANCELLED && b.state==TIKU_RESOURCE_JOB_QUEUED);
    RES_CHECK(tiku_resource_jobs_pump(fixture.registry)==1 && fixture.worker->starts==1);
    tiku_resource_job_info_clear(&a);tiku_resource_job_info_clear(&b);resource_test_close(&fixture);
}

/** @brief Recheck controller validation and retained-result scopes. */
static void scopes_validation_test(void)
{
    resource_fixture_t fixture;
    tiku_resource_job_info_t job,other;
    tiku_resource_caller_t restricted;
    resource_test_open(&fixture,0);job=submit(&fixture,fixture.a,1);
    restricted=fixture.caller;restricted.scopes&=~TIKU_RESOURCE_CONTENT;
    RES_CHECK(tiku_resource_job_status(fixture.registry,&restricted,job.id,&other)==TIKU_RESOURCE_OUTCOME_UNAVAILABLE);
    fixture.worker->reject=1;
    RES_CHECK(!tiku_resource_jobs_pump(fixture.registry) && !fixture.worker->starts);
    refresh(&fixture,&job);RES_CHECK(job.state==TIKU_RESOURCE_JOB_FAILED && job.error==TIKU_RESOURCE_VALUE);
    tiku_resource_job_info_clear(&job);resource_test_close(&fixture);
}

/** @brief Execute a typed async action with invocation authority and one revision. */
static void asynchronous_action_test(void)
{
    resource_fixture_t fixture;
    tiku_resource_initial_t initial={0};
    tiku_resource_async_controller_t controller={resource_test_validate,resource_test_start,resource_test_cancel,100};
    tiku_resource_snapshot_t value;
    tiku_resource_stamp_t stamp;
    tiku_resource_caller_t restricted;
    tiku_resource_service_t *limited;
    tiku_resource_job_info_t job;
    tiku_msg_t *request,*arguments,*result;
    char id[33];
    resource_test_open(&fixture,0);fixture.worker->immediate=1;
    resource_test_snapshot(&value,0);
    initial.spec=(tiku_resource_spec_t){.path="/actions/run",.kind=TIKU_RESOURCE_ACTION,.schema_version=1,
        .schema={.type=TIKU_RESOURCE_BOOLEAN},.public_read=1,.read_scopes=TIKU_RESOURCE_READ,
        .change_scopes=TIKU_RESOURCE_INVOKE,.persistence="none",.controller="worker"};
    initial.initial=value;
    RES_CHECK(!tiku_resource_register_async(fixture.registry,"actions","/actions",NULL,&initial,1,&controller,fixture.worker,&stamp));
    tiku_resource_snapshot_clear(&value);
    request=resource_test_request(&fixture,"invoke","/actions/run",5000);resource_test_expected(request,&stamp);
    arguments=tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,0);
    RES_CHECK(arguments && tiku_msg_add_message(request,"arguments",arguments) && tiku_msg_add_int32(request,"deadline_ms",50));
    tiku_msg_free(arguments);
    restricted=fixture.caller;restricted.scopes&=~TIKU_RESOURCE_INVOKE;
    limited=tiku_resource_service_new(fixture.registry,&restricted,15);RES_CHECK(limited);
    result=resource_test_call(limited,&restricted,request,"permission_denied");RES_CHECK(!fixture.worker->starts);
    tiku_msg_free(result);tiku_resource_service_free(limited);
    result=resource_test_call(fixture.a,&fixture.caller,request,NULL);strcpy(id,tiku_msg_find_string(result,"job",0));tiku_msg_free(result);
    RES_CHECK(tiku_resource_service_pump(fixture.a)==1 && fixture.worker->starts==1);
    RES_CHECK(!tiku_resource_job_status(fixture.registry,&fixture.caller,id,&job));
    RES_CHECK(job.state==TIKU_RESOURCE_JOB_SUCCEEDED && job.resource_stamp.revision==2);
    result=resource_test_call(fixture.b,&fixture.caller,request,NULL);RES_CHECK(!strcmp(tiku_msg_find_string(result,"state",0),"succeeded") && fixture.worker->starts==1);
    tiku_msg_free(result);tiku_msg_free(request);tiku_resource_job_info_clear(&job);resource_test_close(&fixture);
}

int main(int argc,char **argv)
{
    struct {const char *name;void (*run)(void);} cases[]={
        {"lifecycle",lifecycle_test},{"cancellation",cancellation_test},{"cancel-replay",cancellation_replay_test},{"deadlines",deadlines_test},
        {"owner-loss",owner_loss_test},{"capacity",capacity_disconnect_test},{"stale-completion",stale_completion_test},{"scopes",scopes_validation_test},{"async-action",asynchronous_action_test}};
    size_t i;
    for(i=0;i<sizeof cases/sizeof cases[0];i++)if(argc==1||(argc==2&&!strcmp(argv[1],cases[i].name))){cases[i].run();printf("PASS resource jobs %s\n",cases[i].name);}
    return 0;
}
