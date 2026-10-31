/* The Linear step trace (gate_step_trace.h): the vtable entry swapped and restored, a watched ship's call
 * recorded before and after the step, other ships and other ticks passed through. A stand-in step sits at the
 * step's address of the synthetic image; the entry holds it. Included by test_gate_linux.c. */
static unsigned ts_calls;
/* FR8's stand-ins: asinf and the builder behind their targets, and the three calls at their sites (asinf: sub rsp,8;
 * call; add rsp,8; ret; the builder: push r12; mov r12,rsi; sub rsp,0x90; movss [rsp+0x80],xmm3; call; add rsp,0x90;
 * pop r12; ret, so the stub finds a dt at the step's [rsp+0x80] and an agent in r12). */
static int ts_chain;
static unsigned char ts_rows[0x30];
typedef float (*tsc_asin_site)(float);
typedef void (*tsc_builder_site)(void *rows,const void *agent,float pitch,float yaw,float roll,float dt);
static float tsc_asinf(float x) {return x*0.5f;}
static void tsc_builder(void *rows,float pitch,float yaw,float roll) {
    float r[12];for(unsigned i=0;i<12;i++) r[i]=yaw*(float)i+pitch-roll;
    memcpy(rows,r,sizeof(r));
}
/* FR17's stand-in GetTargetPositionForWingMan: out[i] = 10*index + i + time; returns 1 in al (garbage above). */
typedef uint64_t (*tsc_fm_site)(void *formation,void *out,uint64_t index,double time);
static uint64_t tsc_fm(void *formation,void *out,uint64_t index,double time) {
    (void)formation;float o[16];for(unsigned i=0;i<16;i++) o[i]=(float)(10*index+i)+(float)time;
    memcpy(out,o,sizeof(o));return 0x7700u|1u;
}
/* FR18's stand-ins: the local transform copies the point's mat4 (+0x20) to out; the conversion into a frame does that
 * and runs the conversion through its site with the reference object tsc_pt_ref; the conversion adds 100*flag. The
 * three converter sites sit inside push rbx; mov rbx,rcx; call; pop rbx; ret (the agent in rbx as in 0x1431270). */
typedef void *(*tsc_pt_site)(void *out,void *point,void *frame,void *agent);
typedef uint64_t (*tsc_pt_math_site)(void *out,void *velocity,void *ref,void *frame,uint64_t flag);
static unsigned char *tsc_pt_ref;
static void *tsc_pt_local(void *out,void *point) {memcpy(out,(unsigned char*)point+0x20,0x40);return out;}
static void *tsc_pt_convert(void *out,void *point,void *frame) {
    memcpy(out,(unsigned char*)point+0x20,0x40);void *r=tsc_pt_ref,*f=frame;
    ((tsc_pt_math_site)(void*)(LX(LX_SC_PT_MATH_SITE_VA)-4))(out,NULL,&r,&f,1);return out;
}
static uint64_t tsc_pt_math(void *out,void *velocity,void *ref,void *frame,uint64_t flag) {
    (void)velocity;(void)ref;(void)frame;float m[16];memcpy(m,out,0x40);
    for(unsigned i=0;i<16;i++) m[i]+=100.0f*(float)(flag&0xff);
    memcpy(out,m,0x40);return 0;
}
/* FR19's stand-in SetTarget (as 0x141d140: the mat4 to +0x10, a3 to +0x50, a4 != 0 to +0x5c, 1 to +0x5d), reached through
 * the slot by a caller at 0x1431b58 (sub rsp,8; call [rip -> the slot]; add rsp,8; ret) whose call returns to 0x1431b62
 * as the real SetTarget call 0x1431b5f does. */
typedef void (*tsc_sg_site)(void *model,const void *mat,void *a3,void *a4);
static void tsc_settarget(void *model,const void *mat,void *a3,void *a4) {
    unsigned char *m=model;memcpy(m+0x10,mat,0x40);memcpy(m+0x50,&a3,8);m[0x5c]=a4!=NULL;m[0x5d]=1;
}
#define TSC_SG_CALLER 0x1431b58u
#define TSC_BUILDER_PREFIX 21u
static void ts_step(void *model,const void *vector,float dt,double time) {
    unsigned char *m=model;float v[4];memcpy(v,vector,16);
    const float x=v[0]+dt;memcpy(m+0x10,&x,4);memcpy(m+0x78,&time,8);++ts_calls;
    if(ts_chain) {
        const void *agent;memcpy(&agent,m+ST_MODEL_AGENT,8);
        const float t=((tsc_asin_site)(void*)(LX(LX_SC_ASIN_TARGET_SITE_VA)-4))(v[1]),c=((tsc_asin_site)(void*)(LX(LX_SC_ASIN_CUR_SITE_VA)-4))(v[2]);
        ((tsc_builder_site)(void*)(LX(LX_SC_BUILDER_SITE_VA)-TSC_BUILDER_PREFIX))(ts_rows,agent,c+t,0.5f,0.25f,dt);
    }
}
static void tsc_stand_ins(void) {
    unsigned char call[14]={0x48,0x83,0xec,0x08,0xe8,0,0,0,0,0x48,0x83,0xc4,0x08,0xc3};
    const uint32_t asin_sites[2]={LX_SC_ASIN_TARGET_SITE_VA,LX_SC_ASIN_CUR_SITE_VA};
    for(unsigned i=0;i<2;i++) {
        const int32_t rel=(int32_t)((intptr_t)LX(LX_SC_ASINF_VA)-(intptr_t)(LX(asin_sites[i])+5));memcpy(call+5,&rel,4);
        tl_write(LX(asin_sites[i])-4,call,sizeof(call));
    }
    unsigned char b[TSC_BUILDER_PREFIX+5+10]={0x41,0x54,0x49,0x89,0xf4,0x48,0x81,0xec,0x90,0,0,0,0xf3,0x0f,0x11,0x9c,0x24,0x80,0,0,0,
        0xe8,0,0,0,0,0x48,0x81,0xc4,0x90,0,0,0,0x41,0x5c,0xc3};
    const int32_t rel=(int32_t)((intptr_t)LX(LX_SC_BUILDER_VA)-(intptr_t)(LX(LX_SC_BUILDER_SITE_VA)+5));memcpy(b+TSC_BUILDER_PREFIX+1,&rel,4);
    tl_write(LX(LX_SC_BUILDER_SITE_VA)-TSC_BUILDER_PREFIX,b,sizeof(b));
    unsigned char jump[14]={0xff,0x25,0,0,0,0};void *target=(void*)tsc_asinf;memcpy(jump+6,&target,8);tl_write(LX(LX_SC_ASINF_VA),jump,14);
    target=(void*)tsc_builder;memcpy(jump+6,&target,8);tl_write(LX(LX_SC_BUILDER_VA),jump,14);
    /* FR17: each formation call site inside sub rsp,8; call; add rsp,8; ret, the target a stand-in. */
    const uint32_t fm_sites[2]={LX_SC_FM_AGENT_SITE_VA,LX_SC_FM_OTHER_SITE_VA};
    for(unsigned i=0;i<2;i++) {
        const int32_t r=(int32_t)((intptr_t)LX(LX_SC_FM_VA)-(intptr_t)(LX(fm_sites[i])+5));memcpy(call+5,&r,4);
        tl_write(LX(fm_sites[i])-4,call,sizeof(call));
    }
    target=(void*)tsc_fm;memcpy(jump+6,&target,8);tl_write(LX(LX_SC_FM_VA),jump,14);
    /* FR18 */
    const uint32_t pt_sites[3]={LX_SC_PT_LOCAL_SITE_VA,LX_SC_PT_CONVERT_A_SITE_VA,LX_SC_PT_CONVERT_B_SITE_VA};
    for(unsigned i=0;i<3;i++) {
        unsigned char pc[11]={0x53,0x48,0x89,0xcb,0xe8,0,0,0,0,0x5b,0xc3};
        const int32_t r=(int32_t)(int64_t)((uintptr_t)LX(i?LX_SC_PT_CONVERT_VA:LX_SC_PT_LOCAL_VA)-(uintptr_t)(LX(pt_sites[i])+5)); /* targets below their sites */memcpy(pc+5,&r,4);
        tl_write(LX(pt_sites[i])-4,pc,sizeof(pc));
    }
    {const int32_t r=(int32_t)(int64_t)((uintptr_t)LX(LX_SC_PT_MATH_VA)-(uintptr_t)(LX(LX_SC_PT_MATH_SITE_VA)+5));memcpy(call+5,&r,4);
     tl_write(LX(LX_SC_PT_MATH_SITE_VA)-4,call,sizeof(call));}
    target=(void*)tsc_pt_local;memcpy(jump+6,&target,8);tl_write(LX(LX_SC_PT_LOCAL_VA),jump,14);
    target=(void*)tsc_pt_convert;memcpy(jump+6,&target,8);tl_write(LX(LX_SC_PT_CONVERT_VA),jump,14);
    target=(void*)tsc_pt_math;memcpy(jump+6,&target,8);tl_write(LX(LX_SC_PT_MATH_VA),jump,14);
    /* FR19 */
    target=(void*)tsc_settarget;memcpy(jump+6,&target,8);tl_write(LX(LX_SG_FN_VA),jump,14);
    {unsigned char sg[15]={0x48,0x83,0xec,0x08,0xff,0x15,0,0,0,0,0x48,0x83,0xc4,0x08,0xc3};
     const int32_t d=(int32_t)(LX_SG_SLOT_VA-(TSC_SG_CALLER+10));memcpy(sg+6,&d,4);tl_write(LX(TSC_SG_CALLER),sg,sizeof(sg));}
}
static void test_step_trace_linux(void) {
    /* jmp [rip] -> ts_step at the step's address; the entry holds the step. */
    unsigned char code[14]={0xff,0x25,0,0,0,0};void *target=(void*)ts_step;memcpy(code+6,&target,8);
    tl_write(LX(LX_ST_STEP_VA),code,sizeof(code));
    void *step=LX(LX_ST_STEP_VA),*saved_entry;memcpy(&saved_entry,LX(LX_ST_SLOT_VA),8);
    {uintptr_t page=(uintptr_t)LX(LX_ST_SLOT_VA)&~(uintptr_t)(GATE_PAGE-1);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));
     memcpy(LX(LX_ST_SLOT_VA),&step,8);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ));}
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));assert(world_identities);world_identity_installed=1;
    world_identity_count=0;world_identity_sealed=0;assert(world_identity_record(9101,0x7dcd0) && world_identity_record(9102,0x7dd7c));
    st_id_count=0;st_installed=0;
    assert(!st_arm(3) && !strcmp(st_stage,"refused")); /* nothing watched */
    assert(st_watch(0x7dcd0) && st_watch(0x7dcd0) && st_id_count==1);
    boot_phase=2;assert(!st_arm(3));boot_phase=0;
    assert(st_arm(3) && st_installed && st_arm(3) && !st_arm(4));
    assert(st_watch(0x7dcd0) && !st_watch(0x7dd7c) && st_id_count==1); /* the reload's rerun: the same ID again, no new one */
    void *entry;memcpy(&entry,LX(LX_ST_SLOT_VA),8);assert(entry==(void*)st_step && st_native==step);
    /* Two ships, the watched one with a saved ID, the other without being watched. */
    static __attribute__((aligned(16))) unsigned char ships[2][0x20],agents[2][0x1000],models[2][ST_MODEL_BYTES];
    const uint64_t runtime[2]={9101,9102};
    for(unsigned i=0;i<2;i++) {
        memset(models[i],0,sizeof(models[i]));memset(agents[i],0x11*(i+1),sizeof(agents[i]));
        memcpy(ships[i]+8,&runtime[i],8);void *ship=ships[i],*agent=agents[i];
        memcpy(agents[i]+ST_AGENT_OWNER,&ship,8);memcpy(models[i]+ST_MODEL_AGENT,&agent,8);
    }
    const float vector[4]={1.5f,2.0f,3.0f,0.0f};
    st_step_fn call=(st_step_fn)entry;
    boot_phase=3;ts_calls=0;
    for(uint64_t tick=1;tick<=4;tick++) {stats.issued=tick;for(unsigned i=0;i<2;i++) call(models[i],vector,0.25f,10.0+tick);}
    assert(ts_calls==8 && st_calls==8 && st_watched_calls==3 && st_records==3 && !st_dropped);
    /* The record of tick 2: the model before and after, the agent, the arguments. */
    char row[1024];
    assert(st_row(1*ST_CHUNKS+0,row,sizeof(row)) && strstr(row,"tick=2 key=000000000007dcd0 ordinal=0 thread=ui part=args offset=0 dt=3e800000 time=4028000000000000 bytes=0000c03f0000004000004040"));
    assert(st_row(1*ST_CHUNKS+1,row,sizeof(row)) && strstr(row,"part=model_pre offset=0 "));
    assert(st_row(1*ST_CHUNKS+3,row,sizeof(row)) && strstr(row,"part=model_post offset=0 ") && strstr(row,"bytes=0000000000000000")); /* +0..+7 the vtable slot, zero here */
    const char *post=strstr(row,"bytes=")+6;assert(!strncmp(post+0x10*2,"0000e03f",8)); /* +0x10 = 1.75f */
    assert(st_row(1*ST_CHUNKS+10,row,sizeof(row)) && strstr(row,"part=agent_pre offset=500 ") && strstr(row,"bytes=")+6+0x40*2==strstr(row," banks="));
    /* The pose banks (agent+0xaf0, both): before and after, the last chunk 0x40 bytes; the bank indices at the end. */
    assert(ST_CHUNKS==25 && st_row(1*ST_CHUNKS+17,row,sizeof(row)) && strstr(row,"part=pose_pre offset=0 ") && strstr(row,"bytes=11111111"));
    uint32_t banks[2];st_banks(banks);char want[64];snprintf(want,sizeof(want)," banks=%x,%x/%x,%x",banks[0],banks[1],banks[0],banks[1]);
    assert(banks[0]<=1 && banks[1]<=1 && banks[0]+banks[1]==1); /* the synthetic image's read and written bank */
    assert(st_row(1*ST_CHUNKS+24,row,sizeof(row)) && strstr(row,"part=pose_post offset=300 ") && strstr(row,want) &&
           strstr(row,"bytes=")+6+0x40*2==strstr(row," banks="));
    assert(!st_row(3*ST_CHUNKS,row,sizeof(row))); /* tick 4 is past the window */
    char status[384];assert(st_status(status,sizeof(status)) && strstr(status,"installed=1 restored=0 ids=1 ticks=3 calls=8 watched_calls=3 records=3 dropped=0"));
    st_restore();memcpy(&entry,LX(LX_ST_SLOT_VA),8);assert(!st_installed && st_restored && entry==step);
    {uintptr_t page=(uintptr_t)LX(LX_ST_SLOT_VA)&~(uintptr_t)(GATE_PAGE-1);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));
     memcpy(LX(LX_ST_SLOT_VA),&saved_entry,8);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));}
    st_id_count=0;world_identity_installed=0;world_identity_count=0;
    printf("step trace PASS\n");
}
/* FR8 with the step trace: refused on a foreign site, then armed (sites before the entry), the stand-in step calling
 * the three sites: only the watched ship's calls in ticks 1-3 recorded (26 rows a record), every call counted; the
 * chain row's words; ownership; the restoration byte-exact. */
static void test_step_chain_linux(void) {
    tsc_stand_ins();
    unsigned char lines[ST_CHAIN_SITE_COUNT][16];
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) memcpy(lines[i],(void*)((uintptr_t)LX(lxc_sites[i].site+1)&~(uintptr_t)15),16);
    void *step=LX(LX_ST_STEP_VA),*saved_entry;memcpy(&saved_entry,LX(LX_ST_SLOT_VA),8);
    void *saved_sg,*sg_fn=LX(LX_SG_FN_VA);
    {uintptr_t page=(uintptr_t)LX(LX_ST_SLOT_VA)&~(uintptr_t)(GATE_PAGE-1);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));
     memcpy(LX(LX_ST_SLOT_VA),&step,8);memcpy(&saved_sg,LX(LX_SG_SLOT_VA),8);memcpy(LX(LX_SG_SLOT_VA),&sg_fn,8);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ));}
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));assert(world_identities);world_identity_installed=1;
    world_identity_count=0;world_identity_sealed=0;assert(world_identity_record(9101,0x7dcd0) && world_identity_record(9102,0x7dd7c));
    st_id_count=0;st_installed=0;st_restored=0;st_chain_wanted=0;st_chain_installed=0;st_chain_restored=0;
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) st_chain_hits[i]=0;
    st_chain_recorded=st_chain_no_slot=0;
    thunk=make_thunk();assert(thunk);
    assert(st_watch(0x7dd7c) && st_chain_want() && st_chain_wanted);
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) assert(lxc_site_state(i)==0);
    /* A foreign call target at one site: refused, nothing changed. */
    unsigned char *cur=LX(LX_SC_ASIN_CUR_SITE_VA);int32_t rel;memcpy(&rel,cur+1,4);const int32_t off=rel+16;tl_write(cur+1,&off,4);
    assert(!st_arm(3) && !strcmp(st_stage,"chain") && !strcmp(st_chain_stage,"pins") && !st_installed && !st_chain_installed);
    tl_write(cur+1,&rel,4);
    assert(st_arm(3) && st_installed && st_chain_installed && st_chunks==ST_CHUNKS+1+ST_SPLINE_CHUNKS+ST_FM_CHUNKS+ST_PT_CHUNKS+ST_SG_CHUNKS && st_arm(3) && st_sg_installed && st_chain_want());
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) assert(lxc_site_state(i)==1);
    assert(!memcmp(thunk+LXC_STUBS+0x40,"\x48\x8d\x74\x24\x08\x4c\x89\xe2\xff\x25",10) && st_chain_owned());
    assert(!memcmp(thunk+LXC_STUBS+0x60,"\xff\x25",2) && !memcmp(thunk+LXC_STUBS+0x80,"\xff\x25",2));
    for(unsigned i=5;i<8;i++) assert(!memcmp(thunk+LXC_STUBS+0x20*i,"\x48\x89\xd9\xff\x25\0\0\0\0",9));
    assert(!memcmp(thunk+LXC_STUBS+0x100,"\xff\x25\0\0\0\0",6));
    void *entry;memcpy(&entry,LX(LX_ST_SLOT_VA),8);assert(entry==(void*)st_step);
    static __attribute__((aligned(16))) unsigned char ships[2][0x20],agents[2][0x1000],models[2][ST_MODEL_BYTES];
    const uint64_t runtime[2]={9101,9102};
    for(unsigned i=0;i<2;i++) {
        memset(models[i],0,sizeof(models[i]));memset(agents[i],0x11*(i+1),sizeof(agents[i]));
        memcpy(ships[i]+8,&runtime[i],8);void *ship=ships[i],*agent=agents[i];
        memcpy(agents[i]+ST_AGENT_OWNER,&ship,8);memcpy(models[i]+ST_MODEL_AGENT,&agent,8);
    }
    /* FR9: the watched model's spline, two segments (0xa0 each) and their starts. */
    static __attribute__((aligned(16))) unsigned char segs[2][ST_SPLINE_SEGMENT],starts[2][8];
    {unsigned char *p=(unsigned char*)segs,*q=(unsigned char*)starts;
     for(unsigned i=0;i<sizeof(segs);i++) p[i]=(unsigned char)(i*7+1);
     for(unsigned i=0;i<sizeof(starts);i++) q[i]=(unsigned char)(0xa0+i);}
    {const uint64_t b=(uintptr_t)segs,e=b+sizeof(segs),s=(uintptr_t)starts;const float k=1.0f;unsigned char *h=models[1]+ST_MODEL_SPLINE;
     memcpy(h+8,&k,4);memcpy(h+0x10,&b,8);memcpy(h+0x18,&e,8);memcpy(h+0x28,&s,8);}
    const float vector[4]={1.5f,2.0f,3.0f,0.0f};
    st_step_fn call=(st_step_fn)entry;
    /* FR17: a formation of the two agents (leader agents[0], unwatched; wingman agents[1], watched), its shape, read bank 1. */
    static __attribute__((aligned(16))) unsigned char formation[0x80],shape[0x80],fm_out[0x40];
    {void *members[2]={agents[0],agents[1]};static void *member_store[2];memcpy(member_store,members,sizeof(members));
     const uint64_t b=(uintptr_t)member_store,e=b+sizeof(member_store);memset(formation,0,sizeof(formation));
     memcpy(formation+ST_FM_MEMBERS,&b,8);memcpy(formation+ST_FM_MEMBERS+8,&e,8);
     for(unsigned i=0;i<sizeof(shape);i++) shape[i]=(unsigned char)(0x40+i);
     void *vt=LX(0x3b00000u),*sp=shape;memcpy(shape,&vt,8);memcpy(formation+ST_FM_SHAPE,&sp,8);
     float pose[31];for(unsigned i=0;i<31;i++) pose[i]=1.0f+(float)i;memcpy(agents[0]+ST_AGENT_POSE+ST_BANK_STRIDE,pose,sizeof(pose));
     const uint32_t bank=1;tl_write(ST_BANK_READ,&bank,4);}
    /* FR18: the path point (reference runtime 9103 at +0x8, a mat4 at +0x20), its reference object and the frame object
     * (runtime ids 9103/9104, controllers, the reference's parent the first ship). */
    static __attribute__((aligned(16))) unsigned char point[0x80],ref[0x300],frame[0x300],ref_controller[0x60],frame_controller[0x60],pt_out[0x40];
    float m[16];
    {assert(world_identity_record(9103,0x1234) && world_identity_record(9104,0x5678));
     const uint64_t ref_runtime=9103,frame_runtime=9104;void *rc=ref_controller,*fc=frame_controller,*parent=ships[0];
     for(unsigned i=0;i<sizeof(point);i++) point[i]=(unsigned char)(0x30+i);
     for(unsigned i=0;i<16;i++) m[i]=0.5f*(float)i-3.0f;
     memcpy(point+0x20,m,0x40);memcpy(point+ST_PT_REF,&ref_runtime,8);
     memset(ref,0,sizeof(ref));memset(frame,0,sizeof(frame));memcpy(ref+8,&ref_runtime,8);memcpy(frame+8,&frame_runtime,8);
     memcpy(ref+ST_OBJ_CONTROLLER,&rc,8);memcpy(frame+ST_OBJ_CONTROLLER,&fc,8);memcpy(ref+ST_OBJ_PARENT,&parent,8);
     for(unsigned i=0;i<0x60;i++) {ref_controller[i]=(unsigned char)(0x80+i);frame_controller[i]=(unsigned char)(0xe0-i);}
     tsc_pt_ref=ref;void *pp=point;memcpy(agents[1]+ST_AGENT_POINT,&pp,8);}
    float sg_target[16];for(unsigned i=0;i<16;i++) sg_target[i]=7.0f*(float)i-1.5f;
    boot_phase=3;ts_calls=0;ts_chain=1;
    for(uint64_t tick=1;tick<=4;tick++) {
        stats.issued=tick;
        if(tick==2 || tick==4) {
            memset(fm_out,0x5a,sizeof(fm_out));
            assert((((tsc_fm_site)(void*)(LX(LX_SC_FM_AGENT_SITE_VA)-4))(formation,fm_out,1,12.5)&0xff)==1);
            assert((((tsc_fm_site)(void*)(LX(LX_SC_FM_OTHER_SITE_VA)-4))(formation,fm_out,0,12.5)&0xff)==1); /* the leader: not kept */
            if(tick==2) assert((((tsc_fm_site)(void*)(LX(LX_SC_FM_OTHER_SITE_VA)-4))(formation,fm_out,1,13.5)&0xff)==1);
            /* FR18: tick 2 the wingman's local and frame conversions kept, the leader's (unwatched) not; tick 4 past. */
            memset(pt_out,0x5a,sizeof(pt_out));
            if(tick==2) {
                assert(((tsc_pt_site)(void*)(LX(LX_SC_PT_LOCAL_SITE_VA)-4))(pt_out,point,NULL,agents[1])==pt_out);
                assert(((tsc_pt_site)(void*)(LX(LX_SC_PT_CONVERT_B_SITE_VA)-4))(pt_out,point,frame,agents[0])==pt_out);
            }
            assert(((tsc_pt_site)(void*)(LX(LX_SC_PT_CONVERT_A_SITE_VA)-4))(pt_out,point,frame,agents[1])==pt_out);
            ((tsc_sg_site)(void*)LX(TSC_SG_CALLER))(models[1],sg_target,tick==2?(void*)ships[0]:NULL,tick==2?(void*)1:NULL);
            if(tick==2) ((tsc_sg_site)(void*)LX(TSC_SG_CALLER))(models[0],sg_target,NULL,NULL);
        }
        for(unsigned i=0;i<2;i++) call(models[i],vector,0.25f,10.0+tick);
    }
    {void *r=ref,*f=frame;((tsc_pt_math_site)(void*)(LX(LX_SC_PT_MATH_SITE_VA)-4))(pt_out,NULL,&r,&f,1);} /* outside any call */
    ts_chain=0;
    assert(ts_calls==8 && st_watched_calls==3 && st_records==3 && !st_dropped && !st_live_open && !st_live());
    assert(st_chain_hits[0]==8 && st_chain_hits[1]==8 && st_chain_hits[2]==8 && st_chain_recorded==9 && !st_chain_no_slot);
    assert(st_chain_hits[3]==2 && st_chain_hits[4]==3 && st_fm_kept==2);
    assert(st_chain_hits[5]==1 && st_chain_hits[6]==2 && st_chain_hits[7]==1 && st_chain_hits[8]==4 && st_pt_kept==2 && st_pt_conversions==1 &&
           !st_pt_no_slot && !st_pt_open && !st_pt_live());
    /* FR19: the tick-2 record carries the watched ship's SetTarget call (caller 0x1431b62, a3 the first ship, a4 1, the
     * mat4, its point and bank, the target after); the unwatched ship's and the tick-4 call are not kept. */
    assert(st_sg_installed && st_sg_calls==3 && st_sg_kept==1);
    {const uint32_t *g=st_ring[1].settarget;uint64_t k;
     assert(g[0]==ST_SG_TAG && g[1]==2 && g[2]==0x1431b62u-LX_IMAGE_BASE && g[3]==0 && g[4]==1 && g[5]==0 && g[14]==0 && g[127]==1);
     memcpy(&k,g+6,8);assert(k==0x7dd7c);memcpy(&k,g+8,8);assert(k==(uint64_t)(uintptr_t)ships[0]);memcpy(&k,g+10,8);assert(k==0x7dcd0);
     memcpy(&k,g+12,8);assert(k==1);
     assert(!memcmp(g+16,sg_target,0x40) && !memcmp(g+52,point,0x80) && g[84]==0x22222222u && !memcmp(g+100,sg_target,0x40));
     for(unsigned i=0;i<ST_SG_WORDS;i++) assert(!g[ST_SG_WORDS+i] && !st_ring[0].settarget[i] && !st_ring[2].settarget[i]);}
    /* FR18: the tick-2 record carries both kept entries (the local one: no conversion, no frame; the frame one: the
     * conversion's matrix before and after, both objects, their controllers and parents), two calls; the others none. */
    {const uint32_t *p=st_ring[1].path,*q=p+ST_PT_WORDS;float plus[16];uint64_t k;
     for(unsigned i=0;i<16;i++) plus[i]=m[i]+100.0f;
     assert(p[0]==ST_PT_TAG && p[1]==2 && p[2]==0 && p[3]==1 && p[4]==0 && p[5]==1 && !p[16] && p[17]==0 && p[18]==0 && p[191]==2);
     memcpy(&k,p+6,8);assert(k==0x7dd7c);memcpy(&k,p+8,8);assert(k==0x1234);memcpy(&k,p+10,8);assert(!k);memcpy(&k,p+19,8);assert(!k);
     assert(!memcmp(p+24,point,0x80) && !memcmp(p+56,m,0x40) && !p[72] && !p[104] && p[152]==0x22222222u);
     assert(q[0]==ST_PT_TAG && q[1]==2 && q[2]==1 && q[3]==1 && q[16]==1 && q[17]==1 && q[18]==1 && q[191]==2);
     memcpy(&k,q+8,8);assert(k==0x1234);memcpy(&k,q+10,8);assert(k==0x5678);memcpy(&k,q+19,8);assert(k==0x1234);
     memcpy(&k,q+21,8);assert(k==0x5678);memcpy(&k,q+168,8);assert(k==0x7dcd0);memcpy(&k,q+170,8);assert(!k);
     assert(!memcmp(q+56,plus,0x40) && !memcmp(q+72,m,0x40) && !memcmp(q+88,plus,0x40));
     assert(!memcmp(q+104,ref_controller,0x60) && !memcmp(q+128,frame_controller,0x60));
     for(unsigned i=0;i<ST_PT_WORDS;i++) assert(!p[2*ST_PT_WORDS+i] && !st_ring[0].path[i] && !st_ring[2].path[i]);}
    /* FR17: the tick-2 record carries both kept calls of the wingman (ordinals 0 and 1, two calls), the others none. */
    {const uint32_t *f=st_ring[1].formation;float want[16];
     assert(f[0]==ST_FM_TAG && f[1]==2 && f[2]==0 && f[3]==1 && f[4]==2 && f[7]==1 && f[8]==0 && f[9]==1 && f[15]==0 && f[127]==2);
     uint64_t k;memcpy(&k,f+10,8);assert(k==0x7dd7c);memcpy(&k,f+12,8);assert(k==0x7dcd0);
     double t;memcpy(&t,f+5,8);assert(t==12.5 && f[14]==0x3b00000u-LX_IMAGE_BASE);
     for(unsigned i=0;i<16;i++) want[i]=1.0f+(float)i;
     assert(!memcmp(f+16,want,0x40) && !memcmp(f+32+2,shape+8,0x78) && f[112]==st_bits(17.0f) && f[126]==st_bits(31.0f));
     for(unsigned i=0;i<16;i++) want[i]=(float)(10+i)+12.5f;
     assert(!memcmp(f+64,want,0x40) && f[80]==0x5a5a5a5au && f[96]==0x22222222u);
     const uint32_t *g=f+ST_FM_WORDS;assert(g[0]==ST_FM_TAG && g[2]==1 && g[15]==1 && g[127]==2);
     for(unsigned i=0;i<16;i++) want[i]=(float)(10+i)+13.5f;
     assert(!memcmp(g+64,want,0x40));
     for(unsigned i=0;i<ST_FM_WORDS;i++) assert(!f[2*ST_FM_WORDS+i] && !st_ring[0].formation[i] && !st_ring[2].formation[i]);}
    /* The watched ship (agent bytes 0x22): asinf 2.0 -> 1.0 (target), 3.0 -> 1.5 (current); the builder's pitch 2.5, yaw
     * 0.5, roll 0.25, rows yaw*i + pitch - roll, dt 0.25 from the step's frame, the agent's rates. */
    const st_record_t *r=&st_ring[1];const uint32_t *w=r->chain,*s=w+ST_CHAIN_HEAD;
    float rows[12];for(unsigned i=0;i<12;i++) rows[i]=0.5f*(float)i+2.5f-0.25f;
    assert(r->tick==2 && w[0]==ST_CHAIN_TAG && w[1]==1 && w[2]==0x3f000000u && w[3]==0x40200000u && w[4]==0x3e800000u && !memcmp(w+5,rows,0x30));
    assert(w[17]==0x3e800000u && w[18]==0x22222222u && w[19]==0x22222222u && w[20]==0x22222222u);
    assert(!s[0] && !s[7] && !s[14] && s[21]==1 && s[22]==0x40400000u && s[27]==0x3fc00000u && s[28]==1 && s[29]==0x40000000u && s[34]==0x3f800000u);
    char row[1024];
    assert(st_chunks==ST_CHUNKS+1+ST_SPLINE_CHUNKS+ST_FM_CHUNKS+ST_PT_CHUNKS+ST_SG_CHUNKS && st_chunks==53);
    assert(st_row(1*st_chunks+ST_CHUNKS+1+ST_SPLINE_CHUNKS,row,sizeof(row)) && strstr(row,"tick=2 key=000000000007dd7c ordinal=0 thread=ui part=formation offset=0 ") &&
           strstr(row,"bytes=0117524602000000000000000100000002000000"));
    assert(st_row(1*st_chunks+ST_CHUNKS+ST_SPLINE_CHUNKS+ST_FM_CHUNKS,row,sizeof(row)) && strstr(row,"part=formation offset=500 ") && strstr(row,"bytes=")+6+0x100*2==strstr(row," banks="));
    assert(st_row(1*st_chunks+ST_CHUNKS+1+ST_SPLINE_CHUNKS+ST_FM_CHUNKS,row,sizeof(row)) && strstr(row,"tick=2 key=000000000007dd7c ordinal=0 thread=ui part=path offset=0 ") &&
           strstr(row,"bytes=0118524602000000000000000100000000000000"));
    assert(st_row(1*st_chunks+ST_CHUNKS+ST_SPLINE_CHUNKS+ST_FM_CHUNKS+ST_PT_CHUNKS,row,sizeof(row)) && strstr(row,"part=path offset=800 ") && strstr(row,"bytes=")+6+0x100*2==strstr(row," banks="));
    assert(st_row(1*st_chunks+ST_CHUNKS+1+ST_SPLINE_CHUNKS+ST_FM_CHUNKS+ST_PT_CHUNKS,row,sizeof(row)) && strstr(row,"tick=2 key=000000000007dd7c ordinal=0 thread=ui part=settarget offset=0 ") &&
           strstr(row,"bytes=0119524602000000621b0301"));
    assert(st_row(1*st_chunks+st_chunks-1,row,sizeof(row)) && strstr(row,"part=settarget offset=700 ") && strstr(row,"bytes=")+6+0x100*2==strstr(row," banks="));
    assert(st_row(1*st_chunks+0,row,sizeof(row)) && strstr(row,"tick=2 key=000000000007dd7c ordinal=0 thread=ui part=args "));
    assert(st_row(1*st_chunks+ST_CHUNKS,row,sizeof(row)) && strstr(row,"tick=2 key=000000000007dd7c ordinal=0 thread=ui part=chain offset=0 ") &&
           strstr(row,"bytes=0138524601000000")); /* the tag, one builder call */
    assert(strstr(row,"bytes=")+6+ST_CHAIN_WORDS*8==strstr(row," banks="));
    /* FR9: n = 2, the header (k 1.0), the two starts, the two segments, the rest zero; the last chunk 0x80 bytes. */
    assert(!memcmp(r->spline,"\x02\0\0\0\0\0\0\0",8) && !memcmp(r->spline+8+8,"\0\0\x80\x3f",4) && !memcmp(r->spline+0x38,starts,16) &&
           !memcmp(r->spline+0x38+ST_SPLINE_SEGMENTS*8u,segs,sizeof(segs)) && !r->spline[ST_SPLINE_BYTES-1]);
    assert(st_row(1*st_chunks+ST_CHUNKS+1,row,sizeof(row)) && strstr(row,"part=spline offset=0 ") && strstr(row,"bytes=0200000000000000"));
    assert(st_row(1*st_chunks+ST_CHUNKS+4,row,sizeof(row)) && strstr(row,"part=spline offset=300 ") && strstr(row,"bytes=")+6+0x80*2==strstr(row," banks="));
    assert(!st_row(3*st_chunks,row,sizeof(row)));
    char status[768];assert(st_status(status,sizeof(status)) && strstr(status,"installed=1 restored=0 ids=1 ticks=3 calls=8 watched_calls=3 records=3 dropped=0 stage=armed "
        "chain=armed chain_installed=1 chain_restored=0 chain_owned=1 chain_recorded=9 chain_no_slot=0 chain_hits=asin_target:8,asin_cur:8,builder:8"));
    /* Ownership: a changed rel32 is seen; then the restoration, byte-exact, the entry first. */
    unsigned char *b=LX(LX_SC_BUILDER_SITE_VA);memcpy(&rel,b+1,4);const int32_t moved=rel+1;tl_write(b+1,&moved,4);assert(!st_chain_owned());tl_write(b+1,&rel,4);
    assert(st_chain_owned());
    st_restore();memcpy(&entry,LX(LX_ST_SLOT_VA),8);
    assert(!st_installed && st_restored && entry==step && !st_chain_installed && st_chain_restored && !strcmp(st_chain_stage,"restored"));
    assert(!st_sg_installed && *(void**)LX(LX_SG_SLOT_VA)==sg_fn);
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) assert(lxc_site_state(i)==0 && !memcmp(lines[i],(void*)((uintptr_t)LX(lxc_sites[i].site+1)&~(uintptr_t)15),16));
    {uintptr_t page=(uintptr_t)LX(LX_ST_SLOT_VA)&~(uintptr_t)(GATE_PAGE-1);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));
     memcpy(LX(LX_ST_SLOT_VA),&saved_entry,8);memcpy(LX(LX_SG_SLOT_VA),&saved_sg,8);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));}
    st_id_count=0;st_chain_wanted=0;st_chain_restored=0;st_chain_stage="off";st_chunks=ST_CHUNKS;world_identity_installed=0;world_identity_count=0;
    printf("step chain (FR8) sites/flow/rows/restore PASS\n");
}
/* The executable's bytes: the three calls and the facts their hooks rely on, the swaps and their exact restoration. */
static void test_step_chain_elf(void) {
    unsigned char lines[ST_CHAIN_SITE_COUNT][16];
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) {
        const uintptr_t at=(uintptr_t)LX(lxc_sites[i].site+1);
        assert(lxc_site_state(i)==0 && (at&~(uintptr_t)15)==((at+3)&~(uintptr_t)15));memcpy(lines[i],(void*)(at&~(uintptr_t)15),16);
    }
    /* The current pitch kept at [rsp+0x100] and the target at [rsp+0x130]; the pitch approach: dt [rsp+0x80] times the
     * rate [r12+0x37c]; the builder's rows lea rdi,[r15+0x10]; the builder writes rsi before it reads it. */
    assert(!memcmp(LX(0x13edcd2),"\xf3\x0f\x11\x84\x24\x00\x01\x00\x00",9) && !memcmp(LX(0x13edc46),"\xf3\x0f\x11\x84\x24\x30\x01\x00\x00",9));
    assert(!memcmp(LX(0x13eddf3),"\xf3\x0f\x10\x84\x24\x80\x00\x00\x00",9) && !memcmp(LX(0x13ede00),"\xf3\x41\x0f\x59\x84\x24\x7c\x03\x00\x00",10));
    assert(!memcmp(LX(0x13edeb0),"\x49\x8d\x7f\x10",4) && !memcmp(LX(0x1e02226),"\x4c\x89\xe6",3));
    /* FR17: the members at formation+0x48/+0x50 and the shape at +0x60; the leader pose a copy of the read bank at
     * agent+0xaf0 (the bank index times 0x1a0); the wrapper's formation agent+0xf40 and index +0xf48. */
    assert(!memcmp(LX(0x1e592e4),"\x48\x8b\x4f\x48\x48\x8b\x47\x50",8) && !memcmp(LX(0x1e59367),"\x48\x8b\x6d\x60",4));
    assert(!memcmp(LX(0x1e5931c),"\x4c\x8b\x28\x4c\x89\xee\xe8",7) && !memcmp(LX(0x1427924),"\x66\x0f\x6f\x80\xf0\x0a\x00\x00",8));
    assert(!memcmp(LX(0x14285e8),"\x48\x8b\xbe\x40\x0f\x00\x00",7) && !memcmp(LX(0x14285f8),"\x48\x8b\x96\x48\x0f\x00\x00",7));
    /* FR18: each converter call takes the point [rbx+0x6d0] (rbx the agent) and out rbp, the frame calls r14 as the frame;
     * the local transform reads the mat4 at +0x20 and the sub id at +0x10; the point's reference id at +0x8 (0x14312d0);
     * the frame conversion calls the local transform first, then the conversion (rdi out, esi 0, rdx &ref, rcx &frame,
     * r8d 1). */
    assert(!memcmp(LX(0x1431ac0),"\x48\x8b\xb3\xd0\x06\x00\x00\x48\x89\xef",10));
    assert(!memcmp(LX(0x14326c7),"\x48\x8b\xb3\xd0\x06\x00\x00\x4c\x89\xf2\x48\x89\xef",13) && !memcmp(LX(0x14326de),"\x48\x8b\xb3\xd0\x06\x00\x00\x4c\x89\xf2\x48\x89\xef",13));
    assert(!memcmp(LX(0x14bd2db),"\x66\x0f\x6f\x46\x20\x48\x83\x7e\x10\x00",10) && !memcmp(LX(0x14312d0),"\x4d\x8b\x6e\x08",4));
    /* FR19: the Linear vtable's SetTarget entry and SetTarget's copy of the mat4 to +0x10, a3 to +0x50. */
    {uint64_t v;memcpy(&v,LX(LX_SG_SLOT_VA),8);assert(v==LX_SG_FN_VA && !memcmp(LX(LX_SG_FN_VA),"\x0f\x28\x06\x48\x85\xc9\x0f\x29\x47\x10",10) && !memcmp(LX(0x141d161),"\x48\x89\x57\x50",4));}
    assert(!memcmp(LX(0x141fa61),"\xe8\x6a\xd8\x09\x00",5) && !memcmp(LX(0x141fa91),"\x31\xf6\x48\x89\xef\x48\x8d\x4c\x24\x08\x48\x8d\x54\x24\x10\x41\xb8\x01\x00\x00\x00",21));
    thunk=make_thunk();assert(thunk);st_chain_installed=0;st_chain_restored=0;
    assert(st_chain_arm() && st_chain_installed && st_chain_owned());
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) assert(lxc_site_state(i)==1);
    st_chain_restore();assert(!st_chain_installed && st_chain_restored);
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) assert(lxc_site_state(i)==0 && !memcmp(lines[i],(void*)((uintptr_t)LX(lxc_sites[i].site+1)&~(uintptr_t)15),16));
    st_chain_restored=0;st_chain_stage="off";
    printf("step chain (FR8) on the executable's bytes PASS\n");
}
