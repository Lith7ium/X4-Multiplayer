/* The Linear step trace on the Linux X4 9.00 executable (lockstep_step_trace.h, Option B evidence E2): the
 * vtable entry 0x3b62558 holds LinearFlightControlModel's step 0x13ed050 (Windows 0xeb27c0 in 0x2c4f680). The
 * model's agent is at +0x8, the agent's ship at +0xf10. The entry lies in RELRO: one aligned 8-byte word,
 * swapped with the page writable for the moment. Diagnostic, read-only, default off; no stop reason.
 *
 * FR8 (native_gate_step_chain): the step's attitude block, compiled with the approach inlined (the pitch's without
 * the wrap, as Windows' period 0): asinf@plt 0x6579c0 of the target pitch at 0x13edc39 (input min(1, max(-1, T.y)),
 * T normalised just above) and of the current pitch at 0x13edcc5, both results kept at [rsp+0x130] / [rsp+0x100]
 * for the approach (0x13edde3..0x13ede19, rate [r12+0x37c] times dt [rsp+0x80]); the builder 0x1e02200 at
 * 0x13edecb (rdi r15+0x10, xmm0 pitch, xmm1 yaw, xmm2 roll; it takes nothing in rsi/rdx). Each call's rel32 is
 * swapped in its 16-byte line to a stub on the thunk page: asinf's jump to their hooks, the builder's passes the
 * caller's rsp (rsi) and r12 (rdx) first, so the row has the step's dt and the agent's three rates. The file pin
 * covers every byte; the sites must call their targets. */
#define ST_SLOT (LX_ST_SLOT_VA-LX_IMAGE_BASE)
#define ST_STEP (LX_ST_STEP_VA-LX_IMAGE_BASE)
#define ST_MODEL_AGENT 0x8
#define ST_AGENT_OWNER LXW_AGENT_OWNER
#define ST_AGENT_POSE LXW_AGENT_SLOTS /* the two pathing state banks (Windows +0xad0) */
#define ST_BANK_READ LX(LX_BANK_READ_VA)
#define ST_BANK_WRITE LX(LX_BANK_WRITE_VA)
#define ST_CHAIN_SITE_COUNT 9u /* FR8's three, FR17's two, FR18's four */
#define ST_PT_REF 0x8 /* FR18: a path point's reference object runtime id (+0x10 its sub id, +0x20 the offset mat4; Windows +0x10) */
#define ST_SG_SLOT (LX_SG_SLOT_VA-LX_IMAGE_BASE) /* FR19: SetTarget's slot (RELRO, swapped like the step's) */
#define ST_SG_FN (LX_SG_FN_VA-LX_IMAGE_BASE)
#define ST_MODEL_TARGET 0x10 /* Windows 0x20 */
#define ST_AGENT_POINT 0x6d0 /* the path points' begin (Windows [agent+0x6a8]) */
#define ST_MODEL_SPLINE 0xd0 /* FR9: the path spline (vtable 0x3b30558), built by 0x1151760 at 0x13ed3e3 (Windows +0xe0) */
#include "lockstep_step_trace.h"
static int st_slot_swap(void *expected,void *value) {
    return gate_data_swap64(LX(LX_ST_SLOT_VA),(uint64_t)(uintptr_t)expected,(uint64_t)(uintptr_t)value);
}
static int st_sg_swap(void *expected,void *value) {
    return gate_data_swap64(LX(LX_SG_SLOT_VA),(uint64_t)(uintptr_t)expected,(uint64_t)(uintptr_t)value);
}
/* nine stubs 0x20 apart: asinf target, asinf current, the builder's, FR17's two, FR18's four (to 0xb20) */
#define LXC_STUBS 0xa00u
static const struct {uint32_t site,target;const char *name;} lxc_sites[ST_CHAIN_SITE_COUNT]={
    {LX_SC_ASIN_TARGET_SITE_VA,LX_SC_ASINF_VA,"asin_target"},{LX_SC_ASIN_CUR_SITE_VA,LX_SC_ASINF_VA,"asin_cur"},
    {LX_SC_BUILDER_SITE_VA,LX_SC_BUILDER_VA,"builder"},
    {LX_SC_FM_AGENT_SITE_VA,LX_SC_FM_VA,"fm_agent"},{LX_SC_FM_OTHER_SITE_VA,LX_SC_FM_VA,"fm_other"},
    {LX_SC_PT_LOCAL_SITE_VA,LX_SC_PT_LOCAL_VA,"pt"},{LX_SC_PT_CONVERT_A_SITE_VA,LX_SC_PT_CONVERT_VA,"pt_a"},
    {LX_SC_PT_CONVERT_B_SITE_VA,LX_SC_PT_CONVERT_VA,"pt_b"},{LX_SC_PT_MATH_SITE_VA,LX_SC_PT_MATH_VA,"pt_conv"},
};
static const char *st_chain_site_name(unsigned i) {return i<ST_CHAIN_SITE_COUNT?lxc_sites[i].name:"?";}
static unsigned lxc_done;
typedef float (*lxc_asin_fn)(float);
typedef void (*lxc_builder_fn)(void *,float,float,float);
static float lxc_asin_target(float x) {const float y=((lxc_asin_fn)(void*)LX(LX_SC_ASINF_VA))(x);st_chain_call(0,4,x,0.0f,0.0f,0.0f,0.0f,y);return y;}
static float lxc_asin_cur(float x) {const float y=((lxc_asin_fn)(void*)LX(LX_SC_ASINF_VA))(x);st_chain_call(1,3,x,0.0f,0.0f,0.0f,0.0f,y);return y;}
/* From the builder's stub: rdi rows, rsi the step's rsp at the call, rdx its r12 (the agent), xmm0..2 pitch, yaw, roll. */
static void lxc_builder(void *rows,const unsigned char *frame,const unsigned char *agent,float pitch,float yaw,float roll) {
    ((lxc_builder_fn)(void*)LX(LX_SC_BUILDER_VA))(rows,pitch,yaw,roll);
    st_chain_builder(2,(const unsigned char*)rows,yaw,pitch,roll,frame+0x80,agent?agent+0x378:NULL);
}
/* FR17: GetTargetPositionForWingMan (rdi formation, rsi out, rdx index, xmm0 the double time; the same signature). */
static uint64_t lxc_fm_agent(void *f,void *out,uint64_t index,double time) {return st_fm_call(3,0,LX(LX_SC_FM_VA),f,out,index,time);}
static uint64_t lxc_fm_other(void *f,void *out,uint64_t index,double time) {return st_fm_call(4,1,LX(LX_SC_FM_VA),f,out,index,time);}
/* FR18: the three converter calls, their stubs passing the agent (rbx) in rcx; the conversion inside 0x141fa50. */
typedef void *(*lxc_pt_local_fn)(void *,void *);
typedef void *(*lxc_pt_convert_fn)(void *,void *,void *);
typedef uint64_t (*lxc_pt_math_fn)(void *,void *,void *,void *,uint64_t);
static void *lxc_pt(unsigned site,unsigned which,void *out,void *point,void *frame,void *agent) {
    st_pt_ctx c;const int on=st_pt_begin(&c,site,which,agent,point,frame,NULL,NULL);
    void *result=which?((lxc_pt_convert_fn)(void*)LX(LX_SC_PT_CONVERT_VA))(out,point,frame):((lxc_pt_local_fn)(void*)LX(LX_SC_PT_LOCAL_VA))(out,point);
    if(on) st_pt_end(&c,out,1);
    return result;
}
static void *lxc_pt_local(void *out,void *point,void *unused,void *agent) {(void)unused;return lxc_pt(5,0,out,point,NULL,agent);}
static void *lxc_pt_a(void *out,void *point,void *frame,void *agent) {return lxc_pt(6,1,out,point,frame,agent);}
static void *lxc_pt_b(void *out,void *point,void *frame,void *agent) {return lxc_pt(7,2,out,point,frame,agent);}
static uint64_t lxc_pt_conv(void *out,void *velocity,void *ref,void *frame,uint64_t flag) {
    uint32_t *e;st_pt_conversion_begin(8,out,ref,frame,&e);
    const uint64_t result=((lxc_pt_math_fn)(void*)LX(LX_SC_PT_MATH_VA))(out,velocity,ref,frame,flag);
    st_pt_conversion_end(e,out);
    return result;
}
static void lxc_thunk_write(unsigned char *page) {
    unsigned char *p=page+LXC_STUBS;
    lxw_jump_write(p,(void*)lxc_asin_target);lxw_jump_write(p+0x20,(void*)lxc_asin_cur);
    memcpy(p+0x40,"\x48\x8d\x74\x24\x08\x4c\x89\xe2",8); /* lea rsi,[rsp+8]; mov rdx,r12 */
    lxw_jump_write(p+0x48,(void*)lxc_builder);
    lxw_jump_write(p+0x60,(void*)lxc_fm_agent);lxw_jump_write(p+0x80,(void*)lxc_fm_other);
    void *const pt[3]={(void*)lxc_pt_local,(void*)lxc_pt_a,(void*)lxc_pt_b};
    for(unsigned i=0;i<3;i++) {memcpy(p+0xa0+0x20*i,"\x48\x89\xd9",3);lxw_jump_write(p+0xa3+0x20*i,pt[i]);} /* mov rcx,rbx */
    lxw_jump_write(p+0x100,(void*)lxc_pt_conv);
}
static unsigned char *lxc_stub(unsigned i) {return thunk+LXC_STUBS+0x20u*i;}
/* 0 original, 1 ours, -1 anything else. */
static int lxc_site_state(unsigned i) {
    const unsigned char *site=LX(lxc_sites[i].site);int32_t d;
    if(site[0]!=0xe8) return -1;
    memcpy(&d,site+1,4);
    if(site+5+d==LX(lxc_sites[i].target)) return 0;
    if(thunk && site+5+d==lxc_stub(i)) return 1;
    return -1;
}
/* The rel32 in its aligned 16-byte line (checked offline for the three sites). */
static int lxc_swap(unsigned i,const void *expected,const void *value) {
    unsigned char *site=LX(lxc_sites[i].site),*line=(unsigned char*)((uintptr_t)(site+1)&~(uintptr_t)15);
    const unsigned at=(unsigned)(site+1-line);
    const int64_t e=(intptr_t)expected-(intptr_t)(site+5),v=(intptr_t)value-(intptr_t)(site+5);
    if(at>12 || e<INT32_MIN || e>INT32_MAX || v<INT32_MIN || v>INT32_MAX) return 0;
    const int32_t e32=(int32_t)e,v32=(int32_t)v;int32_t now;
    unsigned __int128 before,after;memcpy(&before,line,16);after=before;memcpy(&now,(unsigned char*)&before+at,4);
    if(now!=e32) return 0;
    memcpy((unsigned char*)&after+at,&v32,4);
    return gate_code_swap128(line,before,after);
}
static void st_chain_restore(void) {
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++)
        if((lxc_done>>i)&1u && lxc_site_state(i)==1 && lxc_swap(i,lxc_stub(i),LX(lxc_sites[i].target))) lxc_done&=~(1u<<i);
    if(!lxc_done) {st_chain_installed=0;st_chain_restored=1;st_chain_stage="restored";}
}
static int st_chain_arm(void) {
    if(!thunk) {st_chain_stage="page";return 0;}
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) if(lxc_site_state(i)!=0) {st_chain_stage="pins";return 0;}
    st_chain_installed=1;st_chain_restored=0;lxc_done=0;
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) {
        if(!lxc_swap(i,LX(lxc_sites[i].target),lxc_stub(i))) {st_chain_restore();st_chain_installed=lxc_done!=0;st_chain_stage="cas";return 0;}
        lxc_done|=1u<<i;
    }
    st_chain_stage="armed";return 1;
}
/* The thunk page is r-x for good; the sites must still lead there. */
static int st_chain_owned(void) {
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) if((lxc_done>>i)&1u && lxc_site_state(i)!=1) return 0;
    return 1;
}
static int lua_step_trace(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);uint64_t id=0;int ok=text && length==16;
    for(size_t i=0;ok && i<16;i++) {
        const char c=text[i];ok=(c>='0' && c<='9') || (c>='a' && c<='f');
        id=id*16+(uint64_t)(c<='9'?c-'0':c-'a'+10);
    }
    p_lua_pushnumber(L,ok && st_watch(id));return 1;
}
static int lua_step_trace_arm(lua_State *L) {
    double ticks=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(ticks) && ticks>=1 && ticks<=600 && floor(ticks)==ticks && st_arm((uint64_t)ticks));return 1;
}
static int lua_step_trace_status(lua_State *L) {
    char out[768];int n=accept_ui()?st_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_step_trace_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[1024];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=st_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* step_chain(): FR8's hooks with the step trace (native_gate_step_chain), before step_trace_arm. */
static int lua_step_chain(lua_State *L) {p_lua_pushnumber(L,st_chain_want());return 1;}
