/* FR8 on the Windows X4 9.00 executable (lockstep_step_trace.h, native_gate_step_chain; diagnostic, read-only,
 * default off). Included by lockstep_gate.c only. Linux twin: linux/gate_step_trace.h.
 *
 * The Linear step 0xeb27c0's attitude block, 0xeb379b..0xeb3917: the current pitch asinf(clamp(bank row2.y)) at
 * 0xeb37ff, the target pitch asinf(clamp(T.y)) at 0xeb3892 (asinf 0x1b43ce0, xmm0 in and out; portable math may
 * have moved its entry, which these calls reach as any other caller does), the approach helper 0xeb1db0 (rcx &angle,
 * xmm1 target, xmm2 rate, xmm3 dt, fifth argument the wrap period; nothing returned) for yaw 0xeb386f, pitch 0xeb38b5
 * (period xmm12 = 0.0: no wrap) and roll 0xeb38f2, and the rotation builder 0x34bff0 at 0xeb3912 (rcx the written
 * bank, xmm1 yaw, xmm2 pitch, xmm3 roll; rows at +0x10..+0x3f). After each of these calls the step uses only
 * callee-saved registers and memory, so a C hook with the same signature that calls the original and records stands
 * in for it. Each call's rel32 lies inside one 16-byte line; it is swapped to a stub (jmp [rip]) on a page within
 * rel32 reach. Pins: the block and the helper, as the file has them; while armed, hash_ok and read_local see the
 * original rel32 bytes (fp_patch_view), so other modules' pins over the step (the world trace's) keep passing. */
#define STC_BLOCK 0xeb379bu
#define STC_BLOCK_SIZE 0x17cu
#define STC_BLOCK_SHA "8b5ed48bfccb83af289c8355af8a4b322bf85952062b5ccd00543e32dca833aa"
#define STC_HELPER 0xeb1db0u
#define STC_HELPER_SIZE 0xafu
#define STC_HELPER_SHA "8ecad21ab6fae7289e6aee01ee801edab9d39753470e6ba98dd0ba7beaf744be"
#define STC_ASINF 0x1b43ce0u
#define STC_BUILDER 0x34bff0u
/* FR17: GetTargetPositionForWingMan (rcx formation: members' agents at +0x48/+0x50, shape +0x60; rdx out mat4; r8 the
 * member index; xmm3 the double time; returns al), called by the agent's wrapper 0xe8b990 (formation agent+0xf20, index
 * +0xf28) at 0xe8ba17 and by 0xe9a909 at 0xe9a977; pinned with the wrapper. */
#define STC_FM 0xe99d70u
#define STC_FM_SIZE 0xa95u
#define STC_FM_SHA "167ea3707c777eceb78653d1b7b4728b73e2c85b1a0513e956d8d6e95ba7435b"
#define STC_FM_WRAPPER 0xe8b990u
#define STC_FM_WRAPPER_SIZE 0xb8u
#define STC_FM_WRAPPER_SHA "a8f5feb887cc1e94b5f778f412f17ce1fdf853ad8d120650f0e6d8e226c18c6a"
/* FR18 (lockstep_step_trace.h): UpdatePathing 0xe8c380 converts the agent's first path point at 0xe8cf11: 0xe8be60(rcx
 * -, rdx out mat4, r8 the point [agent+0x6a8], r9 the frame object, [rsp+0x20] / [rsp+0x28] two more objects; returns
 * al), then hands out to SetTarget (0xe8cf52, [model+0x18]). 0xe8be60 stores rcx to its home slot and reuses that slot
 * as a dword local, never reading rcx, so the stub puts the agent (r15 of 0xe8c380, set once from its rcx) there; the
 * stack arguments stay where a jump leaves them. Inside, 0xe8bd20(out, point, frame) calls the conversion 0xe8a4a0(rcx
 * out, rdx velocity 0, r8 &ref, r9 &frame, [rsp+0x20] byte 1) at 0xe8bdeb. Pinned: 0xe8be60, 0xe8bd20 and the caller's
 * bytes 0xe8cef6..0xe8cf55 (the stack arguments, the point load, the call, the SetTarget call). */
#define STC_PT 0xe8be60u
#define STC_PT_SIZE 0x520u
#define STC_PT_SHA "4c0e585c1be84586d171aa40f14ccdc0724bcf3167dc0be62eac7150fc95300e"
#define STC_PT_CONVERT 0xe8bd20u
#define STC_PT_CONVERT_SIZE 0x140u
#define STC_PT_CONVERT_SHA "f7a2196cf7a462cca42f404a629036eabd7122d790fbe97d9105ebce2490ba43"
#define STC_PT_CALLER 0xe8cef6u
#define STC_PT_CALLER_SIZE 0x5fu
#define STC_PT_CALLER_SHA "9daa93d1e96ae8227328d551d2e94fa768ea96934b45ff0e7fb184dab4f00789"
#define STC_PT_MATH 0xe8a4a0u
#define STC_PT_SITE 8u /* its stub: mov rcx,r15 first */
static const struct {uint32_t site,target;const char *name;} stc_sites[ST_CHAIN_SITE_COUNT]={
    {0xeb37ff,STC_ASINF,"asin_cur"},{0xeb386f,STC_HELPER,"yaw"},{0xeb3892,STC_ASINF,"asin_target"},
    {0xeb38b5,STC_HELPER,"pitch"},{0xeb38f2,STC_HELPER,"roll"},{0xeb3912,STC_BUILDER,"builder"},
    {0xe8ba17,STC_FM,"fm_agent"},{0xe9a977,STC_FM,"fm_other"},
    {0xe8cf11,STC_PT,"pt"},{0xe8bdeb,STC_PT_MATH,"pt_conv"},
};
/* Stubs: the first eight 0x10 apart, FR18's 0x20 apart from 0x80. */
#define STC_CODE_BYTES (0x80u+0x20u*(ST_CHAIN_SITE_COUNT-8u))
static unsigned stc_stub(unsigned i) {return i<8u?0x10u*i:0x80u+0x20u*(i-8u);}
static const char *st_chain_site_name(unsigned i) {return i<ST_CHAIN_SITE_COUNT?stc_sites[i].name:"?";}
static unsigned char *stc_page;
static unsigned stc_done;
typedef void (*stc_approach_fn)(float *,float,float,float,float);
typedef float (*stc_asin_fn)(float);
typedef void (*stc_builder_fn)(void *,float,float,float);
static void stc_approach(unsigned site,unsigned slot,float *angle,float target,float rate,float dt,float period) {
    const float in=*angle;
    ((stc_approach_fn)(void*)(image+STC_HELPER))(angle,target,rate,dt,period);
    st_chain_call(site,slot,in,target,rate,dt,period,*angle);
}
static void stc_yaw(float *a,float t,float r,float dt,float p) {stc_approach(1,0,a,t,r,dt,p);}
static void stc_pitch(float *a,float t,float r,float dt,float p) {stc_approach(3,1,a,t,r,dt,p);}
static void stc_roll(float *a,float t,float r,float dt,float p) {stc_approach(4,2,a,t,r,dt,p);}
static float stc_asin_cur(float x) {const float y=((stc_asin_fn)(void*)(image+STC_ASINF))(x);st_chain_call(0,3,x,0.0f,0.0f,0.0f,0.0f,y);return y;}
static float stc_asin_target(float x) {const float y=((stc_asin_fn)(void*)(image+STC_ASINF))(x);st_chain_call(2,4,x,0.0f,0.0f,0.0f,0.0f,y);return y;}
static void stc_builder(void *bank,float yaw,float pitch,float roll) {
    ((stc_builder_fn)(void*)(image+STC_BUILDER))(bank,yaw,pitch,roll);
    st_chain_builder(5,(const unsigned char*)bank+0x10,yaw,pitch,roll,NULL,NULL);
}
static uint64_t stc_fm_agent(void *f,void *out,uint64_t index,double time) {return st_fm_call(6,0,image+STC_FM,f,out,index,time);}
static uint64_t stc_fm_other(void *f,void *out,uint64_t index,double time) {return st_fm_call(7,1,image+STC_FM,f,out,index,time);}
/* FR18: the converter with the agent in rcx (from the stub), the original called with the same six arguments. */
typedef uint64_t (*stc_pt_fn)(void *,void *,void *,void *,void *,void *);
typedef uint64_t (*stc_pt_math_fn)(void *,void *,void *,void *,uint64_t);
static uint64_t stc_pt(void *agent,void *out,void *point,void *frame,void *a5,void *a6) {
    st_pt_ctx c;const int on=st_pt_begin(&c,8,0,agent,point,frame,a5,a6);
    const uint64_t result=((stc_pt_fn)(void*)(image+STC_PT))(agent,out,point,frame,a5,a6);
    if(on) st_pt_end(&c,out,(uint32_t)(result&0xff));
    return result;
}
static uint64_t stc_pt_conv(void *out,void *velocity,void *ref,void *frame,uint64_t flag) {
    uint32_t *e;st_pt_conversion_begin(9,out,ref,frame,&e);
    const uint64_t result=((stc_pt_math_fn)(void*)(image+STC_PT_MATH))(out,velocity,ref,frame,flag);
    st_pt_conversion_end(e,out);
    return result;
}
static void *stc_hook(unsigned i) {
    switch(i) {
    case 0: return (void*)stc_asin_cur;
    case 1: return (void*)stc_yaw;
    case 2: return (void*)stc_asin_target;
    case 3: return (void*)stc_pitch;
    case 4: return (void*)stc_roll;
    case 5: return (void*)stc_builder;
    case 6: return (void*)stc_fm_agent;
    case 7: return (void*)stc_fm_other;
    case 8: return (void*)stc_pt;
    default: return (void*)stc_pt_conv;
    }
}
/* Stub i at page+stc_stub(i): jmp [rip] -> its hook (FR18's converter: mov rcx,r15 first). */
static void stc_write_code(unsigned char *p) {
    memset(p,0xcc,STC_CODE_BYTES);
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) {
        static const unsigned char jump[6]={0xff,0x25,0,0,0,0},agent[3]={0x4c,0x89,0xf9};void *hook=stc_hook(i);
        unsigned char *s=p+stc_stub(i);
        if(i==STC_PT_SITE) {memcpy(s,agent,3);s+=3;}
        memcpy(s,jump,6);memcpy(s+6,&hook,8);
    }
}
static int32_t stc_rel(unsigned i,const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+stc_sites[i].site+5));}
/* 0 original, 1 ours, -1 anything else. Read raw: read_local would show the view. */
static int stc_site_state(unsigned i) {
    unsigned char now[5];int32_t d;
    memcpy(now,(const void*)(image+stc_sites[i].site),5);
    if(now[0]!=0xe8) return -1;
    memcpy(&d,now+1,4);
    if(d==stc_rel(i,image+stc_sites[i].target)) return 0;
    if(stc_page && d==stc_rel(i,stc_page+stc_stub(i))) return 1;
    return -1;
}
static unsigned char *stc_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+STC_BLOCK)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            intptr_t d=(intptr_t)p-(intptr_t)(image+STC_BLOCK);
            if(d>INT32_MIN+0x10000 && d<INT32_MAX-0x10000) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int stc_site_cas(unsigned i,const void *expected,const void *value) {
    const int32_t e=stc_rel(i,expected),v=stc_rel(i,value);unsigned char eb[4],vb[4];memcpy(eb,&e,4);memcpy(vb,&v,4);
    return view_cas_bytes(stc_sites[i].site+1,eb,vb,4);
}
/* The view for other modules' pins: the original rel32 of every site that is ours. */
static int stc_overlaps(const void *source,size_t bytes) {
    if(!stc_done) return 0;
    const uintptr_t a=(uintptr_t)source,b=a+bytes;
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) {
        const uintptr_t s=(uintptr_t)(image+stc_sites[i].site+1);
        if((stc_done>>i)&1u && s<b && s+4>a) return 1;
    }
    return 0;
}
static void stc_unpatch_copy(const void *source,void *copy,size_t bytes) {
    if(!stc_done) return;
    const uintptr_t a=(uintptr_t)source;
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) {
        if(!((stc_done>>i)&1u)) continue;
        const int32_t d=stc_rel(i,image+stc_sites[i].target);unsigned char rel[4];memcpy(rel,&d,4);
        const uintptr_t s=(uintptr_t)(image+stc_sites[i].site+1);
        for(unsigned k=0;k<4;k++) if(s+k>=a && s+k<a+bytes) ((unsigned char*)copy)[s+k-a]=rel[k];
    }
}
static void st_chain_restore(void) {
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++)
        if((stc_done>>i)&1u && stc_site_state(i)==1 && stc_site_cas(i,stc_page+stc_stub(i),image+stc_sites[i].target)) stc_done&=~(1u<<i);
    if(!stc_done) {st_chain_installed=0;st_chain_restored=1;st_chain_stage="restored";}
}
/* With st_arm, before the entry swap: every site original, the pins, the page, then the six swaps. */
static int st_chain_arm(void) {
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) if(stc_site_state(i)!=0) {st_chain_stage="pins";return 0;}
    if(!hash_ok(image+STC_BLOCK,STC_BLOCK_SIZE,STC_BLOCK_SHA) || !hash_ok(image+STC_HELPER,STC_HELPER_SIZE,STC_HELPER_SHA) ||
       !hash_ok(image+STC_FM,STC_FM_SIZE,STC_FM_SHA) || !hash_ok(image+STC_FM_WRAPPER,STC_FM_WRAPPER_SIZE,STC_FM_WRAPPER_SHA) ||
       !hash_ok(image+STC_PT,STC_PT_SIZE,STC_PT_SHA) || !hash_ok(image+STC_PT_CONVERT,STC_PT_CONVERT_SIZE,STC_PT_CONVERT_SHA) ||
       !hash_ok(image+STC_PT_CALLER,STC_PT_CALLER_SIZE,STC_PT_CALLER_SHA)) {st_chain_stage="pins";return 0;}
    if(!stc_page) stc_page=stc_alloc_page();
    if(!stc_page) {st_chain_stage="page";return 0;}
    DWORD old,ignored;if(!VirtualProtect(stc_page,0x1000,PAGE_READWRITE,&old)) {st_chain_stage="page";return 0;}
    stc_write_code(stc_page);
    if(!VirtualProtect(stc_page,0x1000,PAGE_EXECUTE_READ,&ignored)) {st_chain_stage="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),stc_page,0x1000);
    st_chain_installed=1;st_chain_restored=0;stc_done=0;
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) {
        if(!stc_site_cas(i,image+stc_sites[i].target,stc_page+stc_stub(i))) {st_chain_restore();st_chain_installed=stc_done!=0;st_chain_stage="cas";return 0;}
        stc_done|=1u<<i;
    }
    st_chain_stage="armed";return 1;
}
static int st_chain_owned(void) {
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) if((stc_done>>i)&1u && stc_site_state(i)!=1) return 0;
    unsigned char want[STC_CODE_BYTES];stc_write_code(want);
    return stc_page && !memcmp(stc_page,want,sizeof(want));
}
