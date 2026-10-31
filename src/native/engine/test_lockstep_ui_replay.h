/* UI replay (lockstep_ui_replay.h) on stubs and on a private executable region: arming and its refusals, capture
 * in and outside the local phases, the queue and its budget, the section's validation, the delay rule, travel for
 * slot k through the adapter (availability, the last ship's ID, start and stop) and for slot 1 through X4's
 * functions, the soft target's two blocks with the stale, mismatch and hint rules, the order hooks and guards, the
 * Lua-run list, the receipt being equal on two clients, every stride executed for real, the restore, and the
 * stop reason. The image test checks every pinned function, the prefixes and the vtable slots relied on. */
typedef struct {char what;void *a;uint64_t b,c;unsigned char scope;} test_uirep_call;
static test_uirep_call test_uirep_calls[128];
static unsigned test_uirep_call_count;
static int test_uirep_available,test_uirep_drive_on,test_uirep_enter_ok=1,test_uirep_leave_ok=1,test_uirep_fault,test_uirep_events_back;
static unsigned char test_uirep_drive[0x400];
static uint64_t test_uirep_portable_salt;
static __attribute__((aligned(16))) unsigned char test_uirep_player[0x7000],test_uirep_ship[0x400],test_uirep_target[0x100],test_uirep_element[0x100];
static unsigned char test_uirep_offset_seen[UIREP_OFFSET];
static void test_uirep_log(char what,void *a,uint64_t b,uint64_t c) {
    assert(test_uirep_call_count<128);
    test_uirep_calls[test_uirep_call_count++]=(test_uirep_call){what,a,b,c,(unsigned char)pstk_scope_slot};
}
static unsigned test_uirep_count(char what) {unsigned n=0;for(unsigned i=0;i<test_uirep_call_count;i++) n+=test_uirep_calls[i].what==what;return n;}
static void test_uirep_ui(void *self) {test_uirep_log('U',self,(uint64_t)uirep_ui_depth,0);}
static void test_uirep_start(void *a,int activity,unsigned char flag) {test_uirep_log('S',a,(uint64_t)activity,flag);}
static void test_uirep_stop(void *a,int activity,unsigned char flag) {test_uirep_log('E',a,(uint64_t)activity,flag);}
static void test_uirep_set_activity(void *player,int activity) {
    if(test_uirep_fault) {volatile int *nothing=NULL;*nothing=1;}
    test_uirep_log('A',player,(uint64_t)activity,0);
    /* As X4's: the teardown's events reach U::Player while the activity still reads the old one, its answer lands here. */
    if(test_uirep_events_back) {
        int old=0;memcpy(&old,(unsigned char*)player+UIREP_PLAYER_ACTIVITY,4);
        uirep_activity_stop(NULL,old,1);uirep_activity_start(NULL,activity,0);
    }
    memcpy((unsigned char*)player+UIREP_PLAYER_ACTIVITY,&activity,4);
}
static unsigned char test_uirep_can_travel(void *ship) {assert(ship==test_uirep_ship);return (unsigned char)test_uirep_available;}
static void **test_uirep_drive_of(void *ship,void **out) {
    assert(ship==test_uirep_ship);*out=test_uirep_drive;test_uirep_drive[UIREP_DRIVE_ACTIVE]=(unsigned char)test_uirep_drive_on;return out;
}
static void test_uirep_target_set(void *association,const void *offset,unsigned char flag) {
    test_uirep_log('T',association,flag,0);memcpy(test_uirep_offset_seen,offset,UIREP_OFFSET);uirep_set_hint(0xdead);
}
static void test_uirep_target_clear(void *association) {test_uirep_log('C',association,0,0);uirep_set_hint(0xbeef);}
static void *test_uirep_find(void *map,uint64_t id,unsigned flags) {
    assert(flags==4 && map==(void*)test_uirep_drive);
    if(id==0x7001) return test_uirep_target;
    return NULL;
}
static int test_uirep_enter(unsigned k) {
    test_uirep_log('{',NULL,k,0);
    if(!test_uirep_enter_ok) return 0;
    pstk_scope_slot=k;return 1;
}
static int test_uirep_leave(unsigned k) {pstk_scope_slot=0;test_uirep_log('}',NULL,k,0);return test_uirep_leave_ok;}
static uint64_t test_uirep_portable(uint64_t runtime) {return runtime==0x7001?0x123456^test_uirep_portable_salt:0;}
static uint64_t test_uirep_forward(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f) {
    test_uirep_log('F',(void*)(uintptr_t)a,b,c);assert(d==4 && e==5 && f==6);return 77;
}
static void test_uirep_reset(void) {
    test_uirep_call_count=0;test_uirep_available=1;test_uirep_drive_on=0;test_uirep_enter_ok=test_uirep_leave_ok=1;test_uirep_portable_salt=0;
    uirep_armed=0;uirep_families=0;uirep_failed=0;uirep_stopped=0;uirep_installed=0;uirep_restored=1;uirep_why="-";uirep_stage_name="off";
    uirep_queue_head=uirep_queue_count=0;uirep_refusal_head=uirep_refusal_tail=0;memset(uirep_ring,0,sizeof(uirep_ring));
    uirep_frame_tick=uirep_results_tick=uirep_frame_hash=uirep_results_hash=0;uirep_frame_events=0;uirep_ui_depth=0;uirep_dispatch_depth=0;uirep_replaying=0;
    memset(test_uirep_player,0,sizeof(test_uirep_player));memset(test_uirep_ship,0,sizeof(test_uirep_ship));
    pnat_failed=0;pnat_fail_stage=NULL;pstk_scope_slot=0;
    uirep_captured=uirep_refused=uirep_taken=uirep_staged_count=uirep_applied_native=uirep_applied_lua=uirep_stale=uirep_passed=0;
    uirep_scope_start=uirep_scope_stop=uirep_scope_refused=uirep_scope_nested=uirep_unavailable=uirep_guard_refused=uirep_target_passed=uirep_lua_errors=uirep_waits=0;
}
/* A stride reached as its patch reaches it: inside X4's frame (rsp 16-aligned, shadow space there), by a jump. */
extern void test_uirep_site_back(void);
__attribute__((naked)) static void test_uirep_run_site(__attribute__((unused)) void *stride,__attribute__((unused)) void *rbx_value,
    __attribute__((unused)) void *r13_value,__attribute__((unused)) void *rax_value) {
    __asm__ volatile("pushq %rbx\n\tpushq %r13\n\tsubq $0x28, %rsp\n\tmovq %rdx, %rbx\n\tmovq %r8, %r13\n\tmovq %r9, %rax\n\tjmpq *%rcx\n\t"
        ".globl test_uirep_site_back\n\ttest_uirep_site_back:\n\tmovq %rbx, test_uirep_rbx_after(%rip)\n\tmovq %r13, test_uirep_r13_after(%rip)\n\t"
        "addq $0x28, %rsp\n\tpopq %r13\n\tpopq %rbx\n\tretq\n\t");
}
__attribute__((used)) static void *test_uirep_rbx_after,*test_uirep_r13_after;
static void test_ui_replay_section(void) {
    /* Sections as the wire carries them. */
    unsigned char s[64]={UIREP_VERSION,0x64,0,1,UIREP_OP_ACTIVITY_START,2,1,0};uint16_t c=0;const char *why="";
    assert(uirep_section_ok(2,s,8,&c,&why) && c==0x64);
    assert(!uirep_section_ok(2,s,7,&c,&why) && !strcmp(why,"ui_section"));              /* truncated */
    assert(!uirep_section_ok(2,s,9,&c,&why) && !strcmp(why,"ui_section"));              /* trailing */
    s[0]=2;assert(!uirep_section_ok(2,s,8,&c,&why));s[0]=UIREP_VERSION;
    s[3]=0;assert(!uirep_section_ok(2,s,8,&c,&why));s[3]=1;
    s[4]=9;assert(!uirep_section_ok(2,s,8,&c,&why) && !strcmp(why,"ui_op"));s[4]=UIREP_OP_ACTIVITY_START;
    s[6]=4;assert(!uirep_section_ok(2,s,8,&c,&why) && !strcmp(why,"ui_activity"));      /* SETA on slot 2 */
    assert(uirep_section_ok(1,s,8,&c,&why));                                            /* slot 1: MD decides as ever */
    s[6]=1;s[7]=2;assert(!uirep_section_ok(2,s,8,&c,&why) && !strcmp(why,"ui_activity"));s[7]=0;
    {unsigned f=uirep_families;uirep_families=UIREP_FAMILY_ORDERS;assert(!uirep_section_ok(2,s,8,&c,&why));uirep_families=f;}
    unsigned char t[UIREP_HEAD+2+UIREP_REF]={UIREP_VERSION,1,0,1,UIREP_OP_TARGET_CLEAR,UIREP_REF};
    assert(uirep_section_ok(2,t,sizeof(t),&c,&why));
    t[5]=UIREP_REF-1;assert(!uirep_section_ok(2,t,sizeof(t)-1,&c,&why) && !strcmp(why,"ui_target"));
    assert(!uirep_section_ok(2,s,UIREP_SECTION_MAX+1,&c,&why));
}
static void test_ui_replay(void) {
    unsigned char *saved_base=pnat_base;uirep_fns saved_fn=uirep_fn;DWORD saved_ui=ui_thread;uint64_t saved_issued=stats.issued;unsigned saved_slot=stats.slot;
    uint64_t saved_stop=pilot_stop,saved_start=pstk_capture_start;unsigned saved_delay=pilot_delay,saved_count=pilot_slot_count,saved_mask=pstk_slot_mask;
    int saved_capturing=pstk_capturing,saved_armed=pnat_armed,saved_mode=pnat_mode,saved_slots=pslot_armed,saved_present=pres_armed,saved_rid=rid_mode;
    pslot_slot saved_pslot=pslots[2];
    unsigned char *region=VirtualAlloc(NULL,TEST_PNAT_REGION,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(region);
    pnat_base=region;ui_thread=GetCurrentThreadId();test_uirep_reset();
    {void *map=test_uirep_drive;memcpy(region+UIREP_MAP,&map,8);}
    uirep_fn=(uirep_fns){.ui_move=test_uirep_ui,.start=test_uirep_start,.stop=test_uirep_stop,.set_activity=test_uirep_set_activity,.can_travel=test_uirep_can_travel,
        .drive=test_uirep_drive_of,.target_set=test_uirep_target_set,.target_clear=test_uirep_target_clear,.find=test_uirep_find,.enter=test_uirep_enter,.leave=test_uirep_leave,
        .portable=test_uirep_portable};
    for(unsigned i=UIREP_H_CLEAR;i<UIREP_HOOKS;i++) uirep_fn.forward[i]=test_uirep_forward;
    /* Arming: what it needs, one refusal at a time. */
    pnat_armed=0;pnat_mode=0;pstk_capturing=0;pilot_slot_count=2;pstk_slot_mask=3;pslot_armed=0;pres_armed=0;rid_mode=RID_OBSERVE;
    assert(!uirep_arm_using(7) && !strcmp(uirep_arm_why,"needs_stock"));
    pnat_armed=1;pnat_mode=PNAT_MODE_STOCK;pstk_capturing=1;
    assert(!uirep_arm_using(7) && !strcmp(uirep_arm_why,"needs_all_stock_slots"));
    pslot_armed=1;assert(!uirep_arm_using(7) && !strcmp(uirep_arm_why,"needs_present"));
    pres_armed=1;assert(!uirep_arm_using(0) && !uirep_arm_using(8) && !strcmp(uirep_arm_why,"families"));
    pstk_slot_mask=1;assert(!uirep_arm_using(7) && !strcmp(uirep_arm_why,"needs_all_stock_slots"));pstk_slot_mask=3;
    assert(uirep_arm_using(7) && uirep_armed && uirep_arm_using(7) && !uirep_arm_using(3) && !strcmp(uirep_arm_why,"rerun"));
    test_ui_replay_section();
    /* The verdicts the families open. */
    assert(pstk_verdict_slot(PSTK_ACTION,0x12f,2)==PSTK_REPLAY && pstk_verdict_slot(PSTK_ACTION,0x131,2)==PSTK_REFUSE && pstk_verdict_slot(PSTK_ACTION,0xe1,2)==PSTK_REFUSE);
    assert(pstk_verdict(PSTK_STATE,0x5b)==PSTK_REPLAY && pstk_verdict(PSTK_STATE,0x18)==PSTK_REFUSE);
    {unsigned f=uirep_families;uirep_families=0;assert(pstk_verdict_slot(PSTK_ACTION,0x12f,2)==PSTK_REFUSE && pstk_verdict(PSTK_STATE,0x5b)==PSTK_REFUSE);uirep_families=f;}
    /* Capture on client 2. Simulation code passes through, the UI is captured and never applied here. */
    stats.slot=2;stats.issued=100;pstk_capture_start=50;pilot_stop=1000;pilot_delay=3;
    uirep_activity_start((void*)1,1,0);assert(test_uirep_count('S')==1 && uirep_passed==1 && !uirep_queue_count);
    uirep_ui_depth=1;uirep_activity_start(NULL,1,0);assert(test_uirep_count('S')==1 && uirep_queue_count==1 && uirep_captured==1);uirep_ui_depth=0;
    uirep_dispatch_depth=1;uirep_activity_stop(NULL,1,1);assert(test_uirep_count('E')==0 && uirep_queue_count==2);uirep_dispatch_depth=0;
    InterlockedExchange(&render_rng_in_slot,1);uirep_activity_stop(NULL,1,1);assert(uirep_queue_count==3);InterlockedExchange(&render_rng_in_slot,0);
    InterlockedExchange(&pres_open,1);uirep_activity_start(NULL,4,0);assert(uirep_queue_count==3 && uirep_refused==1);InterlockedExchange(&pres_open,0); /* SETA on slot 2 */
    {char text[256];assert(uirep_refusals_text(text,sizeof(text)) && !strcmp(text,"100:activity_start:activity"));}
    /* The UI's Move is a local phase for its whole length. */
    test_uirep_ui(NULL);test_uirep_call_count=0;uirep_ui_move((void*)5);assert(test_uirep_count('U')==1 && test_uirep_calls[0].b==1 && !uirep_ui_depth);
    /* Outside the window: refused, nothing runs. */
    test_uirep_call_count=0;stats.issued=20;uirep_ui_depth=1;uirep_activity_start(NULL,1,0);assert(!test_uirep_call_count && uirep_queue_count==3 && uirep_refused==2);
    stats.issued=1001;uirep_activity_start(NULL,1,0);assert(uirep_queue_count==3 && uirep_refused==3);uirep_ui_depth=0;stats.issued=100;
    /* The queue: budget, order, the rest kept. */
    unsigned char section[UIREP_SECTION_MAX];
    assert(!uirep_take(100,UIREP_HEAD+3,section) && uirep_waits==1 && uirep_queue_count==3);
    unsigned length=uirep_take(100,UIREP_HEAD+4+4,section);
    assert(length==UIREP_HEAD+8 && section[3]==2 && section[1]==100 && uirep_queue_count==1 && uirep_taken==2);
    assert(section[4]==UIREP_OP_ACTIVITY_START && section[8]==UIREP_OP_ACTIVITY_STOP && section[10]==1 && section[11]==1);
    /* Stage: the one apply tick, ahead of every pass; identical again is fine, different is not. */
    const char *why="";
    assert(!uirep_stage(3,section,length,101,&why) && !strcmp(why,"ui_slot"));
    assert(uirep_stage(2,section,length,101,&why) && uirep_stage(2,section,length,101,&why) && uirep_staged_count==1);
    section[11]=0;assert(!uirep_stage(2,section,length,101,&why) && !strcmp(why,"ui_conflict"));section[11]=1;
    assert(!uirep_stage(2,section,length,103,&why) && !strcmp(why,"ui_late"));                   /* apply tick == now */
    {unsigned char early[UIREP_SECTION_MAX];memcpy(early,section,length);early[1]=40;assert(!uirep_stage(2,early,length,41,&why) && !strcmp(why,"ui_late"));} /* before the capture window */
    /* Slot 2's seat for the apply. */
    {uint64_t id=0x5001;memcpy(test_uirep_ship+UIREP_ID,&id,8);}
    memset(&pslots[2],0,sizeof(pslots[2]));pslots[2].player=test_uirep_player;pslots[2].ship=test_uirep_ship;
    /* Apply at 103: start then stop, in slot 2's scope, through the setter; the capturing client did nothing before. */
    test_uirep_call_count=0;stats.issued=103;
    assert(uirep_frame(102,&why) && !test_uirep_call_count && !uirep_frame_events);  /* nothing due */
    assert(uirep_frame(103,&why) && uirep_frame_events==2 && uirep_applied_native==2 && !uirep_failed);
    assert(test_uirep_call_count==4 && test_uirep_calls[0].what=='{' && test_uirep_calls[0].b==2 && test_uirep_calls[3].what=='}');
    assert(test_uirep_calls[1].what=='A' && test_uirep_calls[1].a==test_uirep_player && test_uirep_calls[1].b==1 && test_uirep_calls[1].scope==2);
    assert(test_uirep_calls[2].what=='A' && test_uirep_calls[2].b==0);
    {uint64_t last=0;memcpy(&last,test_uirep_player+UIREP_PLAYER_LAST_SHIP,8);assert(last==0x5001);}   /* 0x710ed0 stops travel on this ID */
    assert(!uirep_scope_refused && uirep_scope_start==1 && uirep_scope_stop==1);
    /* The two wrappers inside a slot scope (the J key's record, the simulation's own stops): the adapter. */
    test_uirep_call_count=0;pstk_scope_slot=2;
    test_uirep_available=0;uirep_activity_start(NULL,1,0);assert(!test_uirep_call_count && uirep_unavailable==1);   /* MD's check */
    test_uirep_drive_on=1;uirep_activity_start(NULL,1,0);assert(test_uirep_count('A')==1);test_uirep_drive_on=0;test_uirep_available=1; /* already charging */
    uirep_activity_start(NULL,1,0);assert(test_uirep_count('A')==1);                                                 /* already the activity */
    uirep_activity_start(NULL,3,0);assert(test_uirep_count('A')==1 && uirep_scope_refused==1);                       /* only travel here */
    uirep_activity_stop(NULL,3,1);assert(test_uirep_count('A')==1);                                                  /* not the activity */
    uirep_activity_stop(NULL,1,1);assert(test_uirep_count('A')==2 && test_uirep_calls[1].b==0);
    uirep_activity_stop(NULL,0,1);assert(test_uirep_count('A')==2);                                                  /* none */
    assert(!test_uirep_count('S') && !test_uirep_count('E'));pstk_scope_slot=0;
    /* The setter's own events coming back (live u1..u3, a stack overflow at the stop): the setter runs once, stop and start. */
    {LONG64 nested=uirep_scope_nested;test_uirep_call_count=0;pstk_scope_slot=2;test_uirep_events_back=1;
     uirep_activity_start(NULL,1,0);assert(test_uirep_count('A')==1 && test_uirep_calls[0].b==1 && uirep_scope_nested==nested+2);
     int now=0;memcpy(&now,test_uirep_player+UIREP_PLAYER_ACTIVITY,4);assert(now==1);
     uirep_activity_stop(NULL,1,1);assert(test_uirep_count('A')==2 && test_uirep_calls[1].b==0 && uirep_scope_nested==nested+4);
     memcpy(&now,test_uirep_player+UIREP_PLAYER_ACTIVITY,4);assert(!now && !uirep_setter_depth);
     /* Afterwards the adapter answers again: the simulation's own stop of a later travel still ends it. */
     test_uirep_events_back=0;uirep_activity_start(NULL,1,0);uirep_activity_stop(NULL,1,1);assert(test_uirep_count('A')==4);
     pstk_scope_slot=0;}
    /* Slot 1 goes through X4's own functions, canonical: MD on every client. */
    test_uirep_call_count=0;stats.slot=1;uirep_ui_depth=1;uirep_activity_start(NULL,4,0);uirep_activity_stop(NULL,1,1);uirep_ui_depth=0;stats.slot=2;
    assert(!test_uirep_call_count && uirep_queue_count==3);   /* slot 1 captures any activity; the left-over event of client 2 goes first */
    uirep_queue_head=uirep_queue_count=0;memset(uirep_ring,0,sizeof(uirep_ring));uirep_staged_count=0;
    {unsigned char one[]={UIREP_VERSION,110,0,2,UIREP_OP_ACTIVITY_START,2,4,0,UIREP_OP_ACTIVITY_STOP,2,1,1};
     assert(uirep_stage(1,one,sizeof(one),111,&why));
     test_uirep_call_count=0;assert(uirep_frame(113,&why) && test_uirep_call_count==2);
     assert(test_uirep_calls[0].what=='S' && test_uirep_calls[0].b==4 && test_uirep_calls[0].scope==0 && test_uirep_calls[1].what=='E' && test_uirep_calls[1].b==1 && test_uirep_calls[1].c==1);}
    /* A fault in a replayed call stops, the scope closed. */
    {unsigned char one[]={UIREP_VERSION,120,0,1,UIREP_OP_ACTIVITY_START,2,1,0};
     memset(test_uirep_player+UIREP_PLAYER_ACTIVITY,0,4);assert(uirep_stage(2,one,sizeof(one),121,&why));
     test_uirep_fault=1;test_uirep_call_count=0;
     assert(!uirep_frame(123,&why) && !strcmp(why,"ui_fault") && uirep_failed && !strcmp(uirep_why,"ui_fault") && test_uirep_count('}')==1 && !pstk_scope_slot && !uirep_replaying && !uirep_setter_depth);
     test_uirep_fault=0;uirep_failed=0;uirep_why="-";
     assert(uirep_stage(2,one,sizeof(one),121,&why));   /* the same section, staged again by the test */
     test_uirep_enter_ok=0;assert(!uirep_frame(123,&why) && !strcmp(why,"ui_scope"));test_uirep_enter_ok=1;uirep_failed=0;uirep_why="-";
     test_uirep_leave_ok=0;assert(!uirep_frame(123,&why) && !strcmp(why,"ui_scope_leave"));test_uirep_leave_ok=1;uirep_failed=0;uirep_why="-";}
    /* The soft target's blocks: the UI half goes on, the simulation half becomes an event. */
    {uint64_t id=0x7001;memset(test_uirep_target,0,sizeof(test_uirep_target));memcpy(test_uirep_target+UIREP_ID,&id,8);}
    for(unsigned i=0;i<UIREP_OFFSET;i++) test_uirep_element[UIREP_ELEMENT_OFFSET+i]=(unsigned char)(0x40+i);
    test_uirep_element[UIREP_ELEMENT_FLAG]=1;
    stats.slot=2;stats.issued=200;uirep_queue_head=uirep_queue_count=0;memset(uirep_ring,0,sizeof(uirep_ring));test_uirep_call_count=0;
    uirep_set_hint(0x11);
    uirep_target_clear_stub(test_uirep_target);assert(test_uirep_count('C')==1 && uirep_get_hint()==0xbeef && uirep_target_passed==1);  /* outside the local phases (MD): at once, stock */
    uirep_target_set_stub(test_uirep_target,test_uirep_element);assert(test_uirep_count('T')==1 && uirep_target_passed==2 && test_uirep_calls[1].a==test_uirep_target+UIREP_ASSOCIATION && test_uirep_calls[1].b==1 && test_uirep_offset_seen[3]==0x43);
    test_uirep_call_count=0;uirep_set_hint(0x11);InterlockedExchange(&pres_open,1);
    uirep_target_clear_stub(test_uirep_target);assert(!test_uirep_call_count && uirep_queue_count==1 && !uirep_get_hint());
    uirep_target_set_stub(test_uirep_target,test_uirep_element);assert(!test_uirep_call_count && uirep_queue_count==2 && uirep_get_hint()==0x7001);
    InterlockedExchange(&pres_open,0);
    length=uirep_take(200,UIREP_SECTION_MAX,section);assert(length==UIREP_HEAD+2+UIREP_REF+2+UIREP_REF+1+UIREP_OFFSET);
    /* Two clients apply it alike: the same calls in slot 2's scope, the same receipt, the local hint kept. */
    char receipt[2][192];
    for(unsigned client=0;client<2;client++) {
        const char *w="";test_uirep_call_count=0;memset(uirep_ring,0,sizeof(uirep_ring));
        assert(uirep_stage(2,section,length,201,&w));
        uirep_set_hint(client?0x22:0x7001);
        assert(uirep_frame(203,&w) && uirep_frame_events==2);
        assert(test_uirep_call_count==4 && test_uirep_calls[1].what=='C' && test_uirep_calls[2].what=='T' && test_uirep_calls[1].a==test_uirep_target+UIREP_ASSOCIATION && test_uirep_calls[2].scope==2);
        assert(test_uirep_calls[2].b==1 && test_uirep_offset_seen[15]==0x4f && uirep_get_hint()==(client?0x22u:0x7001u));
        assert(uirep_frame_text(receipt[client],sizeof(receipt[client])));
    }
    assert(!strcmp(receipt[0],receipt[1]) && !strncmp(receipt[0],"PUIREP_1:203:2:",15));
    /* The receipt holds every slot's activity and soft target: what took on one client only shows. */
    {char text[192];memcpy(test_uirep_player+UIREP_PLAYER_ACTIVITY,(int[]){1},4);*(uint64_t*)(test_uirep_player+PRES_TARGET)=0x7001;
     assert(uirep_frame_text(text,sizeof(text)) && strstr(text,"|0/0,1/7001"));
     memset(test_uirep_player+UIREP_PLAYER_ACTIVITY,0,4);*(uint64_t*)(test_uirep_player+PRES_TARGET)=0;}
    /* A dead slot's event is consumed and hashed, not applied. */
    {const char *w="";memset(uirep_ring,0,sizeof(uirep_ring));assert(uirep_stage(2,section,length,201,&w));LONG64 stale=uirep_stale;test_uirep_call_count=0;
     pdeath_armed=1;pdeath_dead=2u;assert(uirep_frame(203,&w) && uirep_frame_events==2 && !test_uirep_call_count && uirep_stale==stale+1);pdeath_armed=0;pdeath_dead=0;}
    /* A target gone is the same everywhere, a portable ID that differs stops. */
    {unsigned char gone[UIREP_HEAD+2+UIREP_REF]={UIREP_VERSION,210&0xff,0,1,UIREP_OP_TARGET_CLEAR,UIREP_REF,0x99};
     LONG64 stale=uirep_stale;assert(uirep_stage(1,gone,sizeof(gone),211,&why));test_uirep_call_count=0;
     assert(uirep_frame(213,&why) && !test_uirep_call_count && uirep_stale==stale+1);
     memset(uirep_ring,0,sizeof(uirep_ring));assert(uirep_stage(2,section,length,201,&why));
     test_uirep_portable_salt=1;assert(!uirep_frame(203,&why) && !strcmp(why,"ui_ref_mismatch") && uirep_failed);test_uirep_portable_salt=0;uirep_failed=0;uirep_why="-";}
    /* Order hooks: captured in the UI for Lua to run, forwarded otherwise. Guards refuse with the failure value. */
    test_uirep_call_count=0;uirep_queue_head=uirep_queue_count=0;memset(uirep_ring,0,sizeof(uirep_ring));
    assert(uirep_orders_clear2(0x7001,1,0,4,5,6)==77 && test_uirep_count('F')==1);
    assert(uirep_guard_create3(0x7001,1,2,4,5,6)==77 && test_uirep_count('F')==2 && !uirep_guard_refused);
    uirep_ui_depth=1;test_uirep_call_count=0;
    assert(uirep_orders_clear(0x7001,0,0,0,0,0)==1 && uirep_orders_clear2(0x7001,1,1,0,0,0)==1 && !test_uirep_call_count && uirep_queue_count==2);
    assert(!uirep_guard_enable(0x7001,1,2,4,5,6) && !uirep_guard_adjust(1,1,2,4,5,0) && uirep_guard_refused==2 && !test_uirep_call_count);
    assert(uirep_guard_adjust(1,1,2,4,5,6)==77 && uirep_guard_refused==2 && test_uirep_count('F')==1);test_uirep_call_count=0;   /* check-only: a query */
    {char text[256];assert(uirep_refusals_text(text,sizeof(text)) && strstr(text,"200:EnableOrder:not_replayed_yet"));}
    InterlockedExchange(&uirep_replaying,1);assert(uirep_guard_enable(0x7001,1,2,4,5,6)==77);assert(uirep_orders_clear2(0x7001,1,2,4,5,6)==77);InterlockedExchange(&uirep_replaying,0);
    uirep_stopped=1;assert(uirep_guard_enable(0x7001,1,2,4,5,6)==77);uirep_stopped=0;
    uirep_ui_depth=0;
    /* Lua's own event, in order with them; the Lua-run list; no scope opens for them natively. */
    {unsigned char lua_event[3]={2,0,0};assert(uirep_push(UIREP_OP_LUA_CALL,lua_event,3));}
    length=uirep_take(200,UIREP_SECTION_MAX,section);assert(length && section[3]==3);
    assert(uirep_stage(2,section,length,201,&why));test_uirep_call_count=0;
    assert(uirep_frame(203,&why) && !test_uirep_call_count && uirep_frame_events==3);
    {char text[1024];int n=uirep_lua_events(203,text,sizeof(text));
     assert(n>0 && !strncmp(text,"2:65:",5) && strstr(text,";2:66:") && strstr(text,";2:67:020000") && uirep_applied_lua==3);
     assert(!uirep_lua_events(203,text,sizeof(text)));}
    uirep_result(203,1);uirep_result(203,0);assert(uirep_lua_errors==1);
    {char text[192];assert(uirep_frame_text(text,sizeof(text)));char ok_only[192];uint64_t h=uirep_results_hash;uirep_results_tick=0;uirep_result(203,1);uirep_result(203,1);assert(uirep_frame_text(ok_only,sizeof(ok_only)) && strcmp(text,ok_only) && uirep_results_hash!=h);}
    /* A push that is too long or a full queue refuses and runs nothing. */
    {static unsigned char big[UIREP_EVENT_MAX+1];unsigned before=uirep_queue_count;LONG64 refused=uirep_refused;
     assert(!uirep_push(UIREP_OP_LUA_CALL,big,UIREP_EVENT_MAX+1) && uirep_queue_count==before && uirep_refused==refused+1);
     for(unsigned i=0;i<UIREP_QUEUE;i++) assert(uirep_push(UIREP_OP_LUA_CALL,big,1));
     assert(!uirep_push(UIREP_OP_LUA_CALL,big,1) && uirep_queue_count==UIREP_QUEUE);
     unsigned n=uirep_take(201,UIREP_SECTION_MAX,section);assert(n && section[3]==UIREP_EVENTS_MAX && uirep_queue_count==UIREP_QUEUE-UIREP_EVENTS_MAX);
     uirep_queue_head=uirep_queue_count=0;}
    /* Install on executable fake bytes: every patch one CAS, every stride run for real, everything back. */
    for(unsigned i=0;i<UIREP_HOOKS;i++) {
        memset(region+uirep_hooks[i].rva,0xcc,0x20);memcpy(region+uirep_hooks[i].rva,uirep_hooks[i].bytes,uirep_hooks[i].prefix);
        region[uirep_hooks[i].rva+uirep_hooks[i].prefix]=0xc3;   /* ret behind the prefix */
    }
    for(unsigned i=0;i<UIREP_SITES;i++) memcpy(region+uirep_sites[i].rva,uirep_sites[i].bytes,5);
    {   /* The continuations go back into the runner. */
        const void *back=(const void*)test_uirep_site_back;
        pview_abs_jump(region+uirep_sites[0].resume,back);pview_abs_jump(region+uirep_sites[1].resume,back);pview_abs_jump(region+UIREP_CLEAR_ERROR,back);
    }
    uirep_fns stubs=uirep_fn;
    uirep_fn.ui_move=NULL;uirep_fn.start=uirep_fn.stop=NULL;memset(uirep_fn.forward,0,sizeof(uirep_fn.forward));
    assert(uirep_install() && uirep_installed && !uirep_restored && uirep_intact());
    for(unsigned i=0;i<UIREP_HOOKS;i++) assert(region[uirep_hooks[i].rva]==0xe9);
    assert(region[uirep_sites[0].rva]==0xe9 && region[uirep_sites[1].rva]==0xe9);
    assert(((uirep_sites[0].rva+4)&~15u)==(uirep_sites[0].rva&~15u) && ((uirep_sites[1].rva+4)&~15u)==(uirep_sites[1].rva&~15u));
    /* A hook's whole chain: X4's entry -> the near page -> our wrapper -> the trampoline -> behind the prefix. */
    ((void(*)(void*))(region+uirep_hooks[UIREP_H_UI].rva))((void*)9);
    assert(!uirep_ui_depth);
    /* Prefix bytes then the jump behind them, on the page. */
    assert(!memcmp(uirep_stride(UIREP_H_START),uirep_hooks[UIREP_H_START].bytes,7) && uirep_stride(UIREP_H_START)[7]==0xff && uirep_stride(UIREP_H_START)[8]==0x25);
    assert(uirep_fn.start==(uirep_activity_fn)uirep_stride(UIREP_H_START) && uirep_fn.forward[UIREP_H_DEPLOY]==(uirep_forward_fn)uirep_stride(UIREP_H_DEPLOY));
    {unsigned char t1[UIREP_STRIDE],t2[UIREP_STRIDE];assert(uirep_write_site(0,t1)<=UIREP_STRIDE && uirep_write_site(1,t2)<=UIREP_STRIDE);}
    /* The soft target's strides, entered as their patches enter them. */
    uirep_fn=stubs;test_uirep_call_count=0;
    test_uirep_run_site(uirep_site_stride(UIREP_S_TARGET_SET),test_uirep_target,test_uirep_element,NULL);
    assert(test_uirep_count('T')==1 && test_uirep_calls[0].a==test_uirep_target+UIREP_ASSOCIATION && test_uirep_rbx_after==test_uirep_target && test_uirep_r13_after==test_uirep_element);
    test_uirep_run_site(uirep_site_stride(UIREP_S_TARGET_CLEAR),NULL,test_uirep_element,test_uirep_target);
    assert(test_uirep_count('C')==1 && test_uirep_calls[1].a==test_uirep_target+UIREP_ASSOCIATION && test_uirep_rbx_after==test_uirep_target);
    test_uirep_run_site(uirep_site_stride(UIREP_S_TARGET_CLEAR),(void*)1,test_uirep_element,NULL);  /* no component: X4's error path, nothing called */
    assert(test_uirep_count('C')==1 && test_uirep_rbx_after==NULL);
    /* The pass: runtime IDs in set mode (armed after this module), installed once, intact checked, a lost patch stops with 164. */
    pilot_start=60;pilot_stop=1000;
    {int installed=uirep_installed;uirep_installed=0;rid_mode=RID_OBSERVE;
     assert(!uirep_pass(100) && uirep_failed && !strcmp(uirep_why,"ui_runtime_ids"));
     uirep_failed=0;uirep_why="-";rid_mode=RID_SET;uirep_installed=installed;}
    assert(uirep_pass(100) && !uirep_failed);
    {unsigned char keep=region[uirep_hooks[UIREP_H_STOP].rva+1];region[uirep_hooks[UIREP_H_STOP].rva+1]^=1;
     assert(!uirep_pass(101) && uirep_failed && !strcmp(uirep_why,"ui_hook_lost"));
     pnat_mode=PNAT_MODE_STOCK;pnat_failed=1;assert(pilot_stop_reason()==UIREP_STOP_REASON);pnat_failed=0;
     region[uirep_hooks[UIREP_H_STOP].rva+1]=keep;uirep_failed=0;uirep_why="-";}
    /* Behind the window everything is back, and whatever still runs is stock. */
    assert(uirep_pass(1001) && !uirep_installed && uirep_restored && uirep_stopped);
    for(unsigned i=0;i<UIREP_HOOKS;i++) assert(!memcmp(region+uirep_hooks[i].rva,uirep_hooks[i].bytes,uirep_hooks[i].prefix));
    for(unsigned i=0;i<UIREP_SITES;i++) assert(!memcmp(region+uirep_sites[i].rva,uirep_sites[i].bytes,5));
    uirep_ui_depth=1;test_uirep_call_count=0;uirep_activity_start(NULL,1,0);assert(test_uirep_count('S')==1);uirep_ui_depth=0;
    /* Only the armed families' code is touched; a restore off the pass works too. */
    uirep_families=UIREP_FAMILY_TRAVEL;uirep_stopped=0;
    assert(uirep_install() && region[uirep_hooks[UIREP_H_UI].rva]==0xe9 && region[uirep_hooks[UIREP_H_START].rva]==0xe9 && region[uirep_hooks[UIREP_H_CLEAR].rva]==0x48 && region[uirep_sites[0].rva]==0x48);
    uirep_restore();assert(!uirep_installed && uirep_restored && region[uirep_hooks[UIREP_H_START].rva]==0x48);
    /* A status line a log can carry. */
    {char text[640];assert(uirep_status(text,sizeof(text)) && strstr(text,"ui_replay=") && strstr(text,"families=1"));}
    /* Everything off. */
    test_uirep_reset();
    if(uirep_page) {VirtualFree(uirep_page,0,MEM_RELEASE);uirep_page=NULL;}
    VirtualFree(region,0,MEM_RELEASE);
    pnat_base=saved_base;uirep_fn=saved_fn;ui_thread=saved_ui;stats.issued=saved_issued;stats.slot=saved_slot;pilot_stop=saved_stop;pstk_capture_start=saved_start;
    pilot_delay=saved_delay;pilot_slot_count=saved_count;pstk_slot_mask=saved_mask;pstk_capturing=saved_capturing;pnat_armed=saved_armed;pnat_mode=saved_mode;
    pslot_armed=saved_slots;pres_armed=saved_present;rid_mode=saved_rid;pslots[2]=saved_pslot;pilot_start=0;
    printf("UI replay on stubs and an executable region verified\n");
}
/* The pinned image, read-only apart from a private mapping: pins, prefixes, the slots relied on, a whole install. */
static void test_ui_replay_native_image(const char *path) {
    unsigned char *prior=image,*prior_base=pnat_base;uirep_fns saved_fn=uirep_fn;int saved_mode=pnat_mode;
    image=test_pmath_map(path);pnat_base=image;
    uirep_pins_checked=0;assert(uirep_pins());
    /* Ship vtable slot 1008 is what CanStartTravelMode calls; slots 4 and 5 of the component's +0x48 interface. */
    {void **ship_vt=(void**)(image+0x2b7feb8u);assert(ship_vt[UIREP_SHIP_CAN_TRAVEL]==(void*)(image+0x7e1c80u));}
    {void **assoc=(void**)(image+0x2a93178u);assert(assoc[4]==(void*)(image+0x3e73f0u) && assoc[5]==(void*)(image+0x3e7560u));}
    /* CanStartTravelMode's own call and the stops' uses of the same fields (no other meaning of the offsets). */
    {static const unsigned char call[]={0x48,0xff,0xa0,0x80,0x1f,0x00,0x00};assert(!memcmp(image+0x177658u,call,sizeof(call)));}
    /* The exports that reach the two activity functions by a tail jump. */
    {int32_t rel=0;assert(image[0x1bc5a6u]==0xe9);memcpy(&rel,image+0x1bc5a7u,4);assert(0x1bc5abu+rel==0x9fd120u);
     assert(image[0x1bcb66u]==0xe9);memcpy(&rel,image+0x1bcb67u,4);assert(0x1bcb6bu+rel==0x9fd330u);}
    /* CreateOrder and CreateOrder2 end in CreateOrder3, RemoveOrder in RemoveOrder2. */
    {int32_t rel=0;assert(image[0x1f3d2cu]==0xe8);memcpy(&rel,image+0x1f3d2du,4);assert(0x1f3d31u+rel==0x1f3b30u);
     assert(image[0x1f3d09u]==0xe8);memcpy(&rel,image+0x1f3d0au,4);assert(0x1f3d0eu+rel==0x1f3b30u);
     assert(image[0x1feab9u]==0xe8);memcpy(&rel,image+0x1feabau,4);assert(0x1feabeu+rel==0x1fe9d0u);}
    /* Every patch on the real bytes, one CAS each, and back. */
    uirep_families=UIREP_FAMILIES;uirep_armed=1;uirep_fn=(uirep_fns){0};uirep_page=NULL;uirep_installed=0;uirep_stopped=0;
    assert(uirep_install() && uirep_intact());
    for(unsigned i=0;i<UIREP_HOOKS;i++) assert(image[uirep_hooks[i].rva]==0xe9);
    /* The site's resume points: the block ends the report names. */
    {static const unsigned char after_clear[]={0x4d,0x85,0xed},after_set[]={0x80,0x3d};
     assert(!memcmp(image+uirep_sites[UIREP_S_TARGET_CLEAR].resume,after_clear,3) && !memcmp(image+uirep_sites[UIREP_S_TARGET_SET].resume,after_set,2));}
    uirep_uninstall();assert(!uirep_installed && uirep_restored);
    uirep_pins_checked=0;assert(uirep_pins());
    image[0x3e73f0u+3]^=1;uirep_pins_checked=0;assert(!uirep_pins() && pin_failed_rva==0x3e73f0u);image[0x3e73f0u+3]^=1;uirep_pins_checked=0;
    uirep_armed=0;uirep_families=0;
    if(uirep_page) {VirtualFree(uirep_page,0,MEM_RELEASE);uirep_page=NULL;}
    VirtualFree(image,0,MEM_RELEASE);
    image=prior;pnat_base=prior_base;uirep_fn=saved_fn;pnat_mode=saved_mode;
    printf("UI replay verified on the pinned image: pins, prefixes, vtable slots, patches\n");
}
