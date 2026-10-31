#include <stdio.h>
#include <stdlib.h>
#include "lockstep_player_getup.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"GetUp check failed line%d: %s\n",__LINE__,#x);exit(1);}++checks;}while(0)
static unsigned checks,called,behavior;static pgetup_ledger ledger;
static char run[65]="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static HANDLE entered,release_native;
static int allow(void *user,uint64_t tick,int action) {(void)user;return tick>=1 && action==(tick==30);}
static int deny(void *user,uint64_t tick,int action) {(void)user;(void)tick;(void)action;return 0;}
static bool native(void) {
    ++called;CHECK(pgetup_request(&ledger)==1);
    if(behavior==1) return false;
    if(behavior==2) RaiseException(0xe0424242,0,0,NULL);
    if(behavior==3) pgetup_stop(&ledger);
    if(behavior==4) {SetEvent(entered);WaitForSingleObject(release_native,5000);}
    return true;
}
static void setup(void) {
    memset(&ledger,0,sizeof(ledger));called=behavior=0;
    CHECK(pgetup_configure(&ledger,run,1));CHECK(pgetup_configure(&ledger,run,1));
    for(unsigned i=1;i<30;i++) CHECK(pgetup_batch(&ledger,run,1,i,"00000000","00000000",allow,NULL,native));
    CHECK(!called && ledger.processed==29);
}
static int invoke(void) {return pgetup_batch(&ledger,run,1,30,"87010000","00000000",allow,NULL,native);}
static DWORD WINAPI worker(void *unused) {(void)unused;CHECK(!invoke());return 0;}
int main(void) {
    setup();CHECK(invoke());CHECK(called==1 && ledger.claimed && ledger.accepted && ledger.returns==1 && !ledger.open);
    CHECK(!pgetup_request(&ledger));CHECK(!invoke());CHECK(called==1);
    for(unsigned i=0;i<8;i++) {
        setup();const char *a="87010000",*b="00000000",*r=run;uint64_t epoch=1,tick=30;
        if(i==0)a="87020000";if(i==1)b="87010000";if(i==2)a="87010100";if(i==3)a="87010001";
        if(i==4)a="00000000";if(i==5)tick=31;if(i==6)epoch=2;if(i==7)r="bad";
        CHECK(!pgetup_batch(&ledger,r,epoch,tick,a,b,allow,NULL,native));CHECK(!called);
        CHECK(!invoke() && !called);
    }
    setup();CHECK(!pgetup_batch(&ledger,run,1,30,"87010000","00000000",deny,NULL,native));CHECK(!called);
    setup();pgetup_stop(&ledger);CHECK(!invoke() && !called && !ledger.claimed);
    setup();behavior=1;CHECK(!invoke() && called==1 && !ledger.accepted);CHECK(!invoke() && called==1);
    setup();behavior=2;unsigned caught=0;
    __try {invoke();} __except(GetExceptionCode()==0xe0424242?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=1;}
    CHECK(caught && called==1 && ledger.unwinds==1 && !ledger.returns && !ledger.open && !pgetup_request(&ledger));
    CHECK(!invoke() && called==1);
    setup();behavior=3;CHECK(!invoke() && called==1 && ledger.stop_open==1 && !ledger.open);
    setup();behavior=4;entered=CreateEvent(NULL,TRUE,FALSE,NULL);release_native=CreateEvent(NULL,TRUE,FALSE,NULL);
    HANDLE thread=CreateThread(NULL,0,worker,NULL,0,NULL);CHECK(thread && WaitForSingleObject(entered,5000)==WAIT_OBJECT_0);
    pgetup_stop(&ledger);CHECK(ledger.calls==1 && ledger.open==1 && ledger.stop_open==1);SetEvent(release_native);
    CHECK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);CHECK(called==1 && ledger.returns==1 && !ledger.open && ledger.stop_open==1);
    CloseHandle(thread);CloseHandle(entered);CloseHandle(release_native);
    printf("GetUp production claim core: %u checks PASS\n",checks);return 0;
}
