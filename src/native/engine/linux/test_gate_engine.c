/* Engine emulator for the Linux gate (test only, no X4). One process is one peer:
 * a main thread runs the X4 main loop (semSimDone wait, the FrameTick call through
 * the call window of a fake image, the in-flight byte), a MoveWorker thread runs
 * the UI: the real tools/dev/native_gate_probe.lua in a real LuaJIT state, which
 * loads the gate test module (x4lan_gate_test.so, gate_linux.c built with
 * GATE_LINUX_TEST) through package.loadlib exactly as in X4. LoadGame emulates a
 * snapshot load: the Lua UI is closed, FrameTick runs with loading set, then the
 * UI is rebuilt and the probe runs again from the start (as X4 reloads the UI).
 * Peers talk to tools/dev/native_gate_relay.py over loopback.
 *   cd WORKDIR (holding extensions/x4_lan_lab/native/x4lan_gate.so)
 *   test_gate_engine PRELUDE.lua CONFIG.lua PROBE.lua [INPUTS_CORE.lua]
 * Environment: ENGINE_GAME0 (initial game time, default 1000), ENGINE_SECONDS
 * (deadline, default 60), ENGINE_LOADING (loading frames, default 5).
 * Prints the probe log, then one line: ENGINE_RESULT key=value ... */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include "lockstep_sha256.h"
#include "gate_sites.h"

#define EXPORT __attribute__((visibility("default"))) /* ffi.C lookups (-rdynamic) */
#define FAKE_END 0x7bb7000u
#define AT(va) (fake+((uintptr_t)(va)-LX_IMAGE_BASE))
typedef void (*tick_fn)(void *,unsigned char);
typedef struct lua_State lua_State;
static lua_State *(*lua_new)(void);
static void (*lua_libs)(lua_State *);
static int (*lua_loadfile_)(lua_State *,const char *);
static int (*lua_pcall_)(lua_State *,int,int,int);
static void (*lua_close_)(lua_State *);
static const char *(*lua_tolstring_)(lua_State *,int,size_t *);
static void (*lua_settop_)(lua_State *,int);
static void (*lua_getfield_)(lua_State *,int,const char *);
typedef struct {
    uint64_t issued, completed, waits, report_tick;
    double game, real, wait_game_delta, max_dt_error;
    uint32_t active, reason, restored, slot;
} gate_stats;
static int (*gate_setup)(unsigned char *,const char *,tick_fn,const lx_site *,unsigned,unsigned);
static int (*gate_world)(void *,void **);
static unsigned char *(*gate_tls)(void);
static int (*gate_poll)(gate_stats *);

static unsigned char *fake,table[0x138],window_original[16];
static lx_site sites[sizeof(lx_sites)/sizeof(*lx_sites)];
static char hashes[sizeof(lx_sites)/sizeof(*lx_sites)][65];
static sem_t sim_start,sim_done;
static volatile int done,loaded,load_pending,reload_pending,loading_frames;
static unsigned loading_total=5,reloads,ui_frames,main_frames;
static const char *prelude,*config,*probe,*inputs_core;
static double game0=1000.0;

/* World identities (ENGINE_WORLD=N, N<=15): the snapshot load imports N saved IDs through
 * a stand-in of the map insert (our bytes, the pinned original's first instructions); the
 * gate's find sees no object, so every capture hashes N absent rows on each peer. As X4's
 * load does, the emulated load then clears the map, which destroys the nodes before the
 * first live frame (also a stand-in, the pinned original's first 9 bytes). */
static unsigned world_imports;
static int world_done;
static __attribute__((aligned(16))) unsigned char world_nodes[16][0x30];
static void *world_map_slot=world_nodes; /* any non-null registry */
/* Created identities (ENGINE_CREATED=1, with the runtime IDs): objects made through AssignNewID in the load and in the
 * ticks. This table is all the find knows. */
#define CR_OBJECTS 64u
static int cr_on;
static __attribute__((aligned(16))) unsigned char cr_objects[CR_OBJECTS][0x800];
static uint64_t cr_runtimes[CR_OBJECTS];
static unsigned cr_count;
static void *engine_find(void *map,uint64_t runtime,unsigned mode,unsigned zero) {
    (void)map;(void)mode;(void)zero;
    for(unsigned i=0;i<cr_count;i++) if(cr_runtimes[i]==runtime) return cr_objects[i];
    return NULL;
}
typedef struct {unsigned char *node;uint64_t inserted;} insert_result;
static void world_cleared(void *map) {if(map==(void*)AT(LX_IMPORT_MAP_VA)) memset(world_nodes,0xa5,sizeof(world_nodes));}
static void world_standin(void) {
    unsigned char code[64];unsigned n=0;
    const unsigned char head[]={0x41,0x56,0x41,0x55,0x41,0x54,0x49,0x89,0xf4};          /* push r14/r13/r12; mov r12,rsi */
    const unsigned char key[]={0x49,0x8b,0x04,0x24,0x48,0x83,0xe0,0x0f,0x48,0x6b,0xc0,0x30,0x48,0xb9}; /* rax=(key&15)*0x30; mov rcx,imm64 */
    const unsigned char tail[]={0x48,0x01,0xc8,0x49,0x8b,0x14,0x24,0x48,0x89,0x50,0x20,0xba,0x01,0x00,0x00,0x00,
                                0x41,0x5c,0x41,0x5d,0x41,0x5e,0xc3};                       /* node=nodes+rax; node->key=key; {node,1} */
    uint64_t base=(uint64_t)(uintptr_t)world_nodes;
    memcpy(code,head,sizeof(head));n=sizeof(head);memcpy(code+n,key,sizeof(key));n+=sizeof(key);
    memcpy(code+n,&base,8);n+=8;memcpy(code+n,tail,sizeof(tail));n+=sizeof(tail);
    memcpy(AT(LX_IMPORT_INSERT_VA),code,n);
    uintptr_t page=(uintptr_t)AT(LX_IMPORT_INSERT_VA)&~(uintptr_t)4095;
    if(mprotect((void*)page,4096,PROT_READ|PROT_EXEC)) {perror("mprotect");exit(2);}
    const unsigned char clear_head[]={0x53,0x48,0x83,0xec,0x40,0x48,0x8b,0x57,0x08,0x48,0xb8}; /* push rbx; sub rsp,0x40; mov rdx,[rdi+8]; mov rax,imm64 */
    const unsigned char clear_tail[]={0xff,0xd0,0x48,0x83,0xc4,0x40,0x5b,0xc3};                 /* call rax; add rsp,0x40; pop rbx; ret */
    uint64_t callee=(uint64_t)(uintptr_t)world_cleared;
    memcpy(AT(LX_IMPORT_CLEAR_VA),clear_head,sizeof(clear_head));
    memcpy(AT(LX_IMPORT_CLEAR_VA)+sizeof(clear_head),&callee,8);
    memcpy(AT(LX_IMPORT_CLEAR_VA)+sizeof(clear_head)+8,clear_tail,sizeof(clear_tail));
    page=(uintptr_t)AT(LX_IMPORT_CLEAR_VA)&~(uintptr_t)4095;
    if(mprotect((void*)page,4096,PROT_READ|PROT_EXEC)) {perror("mprotect");exit(2);}
}
/* AI Wait restores (ENGINE_AI_WAIT=1): the AI importer's restore of three blocked waits during
 * the load, through the pinned call, on the owners of saved IDs 0x101..0x102 and one owner
 * without an identity. A restore draws its deadline from the thread word, whose natural value
 * differs per peer. Declared here; the importer call runs from world_import. */
static int ai_wait_on;
static unsigned ai_wait_lost;
static void code_jump(uintptr_t va,const void *target);
static __attribute__((aligned(16))) unsigned char aw_action[3][0x100],aw_exec[3][0x100],aw_context[3][0x200],aw_entity[3][0x20],aw_script[0x100];
static void *aw_program[3][1];
static void *aw_program_header[3][2];
static unsigned char aw_restore_standin(void *self,void *exec,void *value) {
    (void)self;
    uint64_t *word=(uint64_t*)(gate_tls()-0x2720);
    *word=*word*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;*word=(*word>>30)|(*word<<34);
    double start,due;memcpy(&start,(unsigned char*)exec+0x28,8);due=start+(double)(*word>>40)/16777216.0;
    uintptr_t payload;memcpy(&payload,(unsigned char*)value+8,8);memcpy((void*)(payload-0xa0+0x190),&due,8);
    return 1;
}
static void aw_standin(void) {
    code_jump(LX_AI_WAIT_RESTORE_VA,(void*)aw_restore_standin);
    void *restore=AT(LX_AI_WAIT_RESTORE_VA);memcpy(AT(LX_AI_WAIT_SLOT_VA),&restore,8);
    /* mov rax,[rdi]; sub rsp,8; lea rdx is in the stand-in's argument; call [rax+0x30]; add rsp,8; ret */
    unsigned char *site=AT(LX_AI_WAIT_RETURN_VA-3),*entry=site-7;
    uintptr_t page=(uintptr_t)entry&~(uintptr_t)4095;
    if(mprotect((void*)page,8192,PROT_READ|PROT_WRITE)) {perror("mprotect");exit(2);}
    const unsigned char code[]={0x48,0x8b,0x07,0x48,0x83,0xec,0x08,0xff,0x50,0x30,0x48,0x83,0xc4,0x08,0xc3};
    memcpy(entry,code,sizeof(code));
    if(mprotect((void*)page,8192,PROT_READ|PROT_EXEC)) {perror("mprotect");exit(2);}
    const char *name="order.fight.attack";size_t length=strlen(name);const void *data=aw_script+0x20;
    memcpy(aw_script+0x20,name,length);memcpy(aw_script+0x10,&data,8);memcpy(aw_script+0x18,&length,8);
    uint32_t version=7;memcpy(aw_script+0x58,&version,4);
    const void *wait_vtable=AT(LX_AI_WAIT_SLOT_VA-0x30),*context_vtable=AT(LX_AI_CONTEXT_VTABLE);
    for(unsigned i=0;i<3;i++) {
        unsigned char *a=aw_action[i],*x=aw_exec[i],*c=aw_context[i];
        memcpy(a,&wait_vtable,8);memcpy(c,&context_vtable,8);
        uint64_t lifetime=500+i,runtime=i<2?7001+i:9999;void *entity=aw_entity[i],*script=aw_script;
        memcpy(c+8,&lifetime,8);memcpy(c+0x120,&entity,8);memcpy(aw_entity[i]+8,&runtime,8);memcpy(c+0xd0,&script,8);
        aw_program[i][0]=a;aw_program_header[i][0]=aw_program[i];aw_program_header[i][1]=aw_program[i]+1;
        void *program=aw_program_header[i];memcpy(x+0x10,&program,8);
        uint64_t pc=40+i,slot=0;double start=6.9+i*0.25;
        memcpy(x+0x18,&pc,8);memcpy(x+0x20,&slot,8);memcpy(x+0x28,&start,8);x[0x30]=1;
        uint32_t tag=0x66;uintptr_t payload=(uintptr_t)c+0xa0;memcpy(x+0x38,&tag,4);memcpy(x+0x40,&payload,8);
    }
}
static void aw_import(void) {
    uint64_t natural=0x2000000000000000ull+(uint64_t)getpid();
    memcpy(gate_tls()-0x2720,&natural,8);
    unsigned char (*call)(void *,void *,void *)=(unsigned char (*)(void *,void *,void *))(void*)(AT(LX_AI_WAIT_RETURN_VA-3)-7);
    for(unsigned i=0;i<2;i++) {
        if(!call(aw_action[i],aw_exec[i],aw_exec[i]+0x38)) {fprintf(stderr,"restore failed\n");exit(2);}
        uint64_t after;memcpy(&after,gate_tls()-0x2720,8);
        if(after!=natural) ++ai_wait_lost;
    }
    /* An owner without an identity keeps the natural word, and a caller outside the importer passes. */
    call(aw_action[2],aw_exec[2],aw_exec[2]+0x38);
    unsigned char (*slot)(void *,void *,void *);memcpy(&slot,AT(LX_AI_WAIT_SLOT_VA),8);
    slot(aw_action[0],aw_exec[0],aw_exec[0]+0x38);
}
/* MD cue delays (ENGINE_MD_RESUME=1): the load's call through the pinned site, for a named cue,
 * an instance of it with the same saved time, and a cue outside the module's tree. */
static int md_resume_on;
static unsigned md_word_lost;
static __attribute__((aligned(16))) unsigned char md_cue[3][0x240],md_module[0x100],md_node[0x60];
static const char md_module_name[]="FactionLogic",md_cue_name[]="Manager_Delay";
static double md_delay_standin(void *cue,uint64_t slot) {
    (void)cue;
    uint64_t *word=(uint64_t*)(gate_tls()-0x2720);
    *word=*word*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;*word=(*word>>30)|(*word<<34);
    return (double)(*word>>40)/16777216.0+(double)slot;
}
static void md_standin(void) {
    code_jump(LX_MD_RESUME_DELAY_VA,(void*)md_delay_standin);
    unsigned char *site=AT(LX_MD_RESUME_SITE_VA),*entry=site-4;
    uintptr_t page=(uintptr_t)entry&~(uintptr_t)4095;
    if(mprotect((void*)page,8192,PROT_READ|PROT_WRITE)) {perror("mprotect");exit(2);}
    /* sub rsp,8; call the evaluator; add rsp,8; ret */
    int32_t rel=(int32_t)(AT(LX_MD_RESUME_DELAY_VA)-(site+5));
    memcpy(entry,"\x48\x83\xec\x08\xe8",5);memcpy(site+1,&rel,4);memcpy(site+5,"\x48\x83\xc4\x08\xc3",5);
    if(mprotect((void*)page,8192,PROT_READ|PROT_EXEC)) {perror("mprotect");exit(2);}
    uintptr_t data;uint64_t size;
    data=(uintptr_t)md_module_name;size=strlen(md_module_name);memcpy(md_module+0x18,&data,8);memcpy(md_module+0x20,&size,8);
    data=(uintptr_t)md_cue_name;size=strlen(md_cue_name);memcpy(md_node+0x20,&data,8);memcpy(md_node+0x28,&size,8);
    uintptr_t sentinel=(uintptr_t)md_module+0x60,first=(uintptr_t)md_node;
    memcpy(md_module+0x80,&first,8);memcpy(md_module+0x68,&first,8);memcpy(md_node,&sentinel,8);
    void *definition=md_cue[0],*module=md_module,*outside=md_cue[2];const void *vtable=AT(LX_CUE_VTABLE);
    memcpy(md_node+0x40,&definition,8);
    double saved[3]={5.5,5.5,6.25};
    for(unsigned i=0;i<3;i++) {
        memcpy(md_cue[i],&vtable,8);memcpy(md_cue[i]+0x160,i==2?&outside:&definition,8);memcpy(md_cue[i]+0x150,&module,8);
        memcpy(md_cue[i]+0x1f8,&saved[i],8);
    }
}
static void md_import(void) {
    uint64_t natural=0x3000000000000000ull+(uint64_t)getpid();
    memcpy(gate_tls()-0x2720,&natural,8);
    double (*call)(void *,uint64_t)=(double (*)(void *,uint64_t))(void*)(AT(LX_MD_RESUME_SITE_VA)-4);
    for(unsigned i=0;i<3;i++) {
        double delay=call(md_cue[i],i==2);
        uint64_t after;memcpy(&after,gate_tls()-0x2720,8);
        if(after!=natural || !(delay>=0)) ++md_word_lost;
    }
}
/* The script RNG (ENGINE_SCRIPT_RNG=1; ENGINE_SCRIPT_RNG_REVERSE on one peer): every entry of the module, once per
 * tick on the UI thread, from another natural word each time and in the opposite order on the other peer. Each
 * native draws once and adds what it drew, by entry, to an order-independent sum. */
static int script_on,script_reverse;
#define SR_CALLS 10u
static __attribute__((aligned(16))) unsigned char sr_script[0x80],sr_entity[3][0x20],sr_context[3][0x200],sr_exec[4][0xd0],sr_stack[0x100];
static void *sr_program[1],*sr_program_header[2];
static unsigned char sr_block_timed[16],sr_block_wait[16],sr_block_raise[16];
static unsigned sr_word_lost;
static uint64_t sr_calls,sr_sum,sr_last_tick;
static void sr_note(uint64_t which) {
    uint64_t *word=(uint64_t*)(gate_tls()-0x2720);
    *word=*word*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;*word=(*word>>30)|(*word<<34);
    sr_sum+=(*word^(which*0x9e3779b97f4a7c15ULL))*0xff51afd7ed558ccdULL;++sr_calls;
}
static void sr_exec_native(void *exec) {sr_note(1u+(uint64_t)((unsigned char*)exec-sr_exec[0])/0xd0u);}
static uintptr_t sr_range_native(void *exec,void *program,uint64_t begin,uint64_t end,uint32_t mode) {
    if(exec!=sr_exec[3] || program!=sr_program_header || begin || end!=1 || mode!=1) {fprintf(stderr,"range arguments lost\n");exit(2);}
    sr_note(10);return 0x55;
}
static void sr_ai_native(void *self) {sr_note(20u+(uint64_t)((unsigned char*)self-sr_context[0])/0x200u);}
static void sr_stack_native(void *self) {if(self!=sr_stack) {fprintf(stderr,"stack lost\n");exit(2);} sr_note(30);}
static unsigned sr_check_kind;
static void sr_check_native(void *cue) {sr_note(40u+sr_check_kind+16u*(uint64_t)((unsigned char*)cue-md_cue[0])/0x240u);}
static void sr_code(uintptr_t va,const unsigned char *code,size_t size,const void *target) {
    unsigned char *at=AT(va);void *page=(void*)((uintptr_t)at&~(uintptr_t)4095);
    if(mprotect(page,8192,PROT_READ|PROT_WRITE)) {perror("mprotect");exit(2);}
    memcpy(at,code,size);if(target) memcpy(at+size,&target,8);
    if(mprotect(page,8192,PROT_READ|PROT_EXEC)) {perror("mprotect");exit(2);}
}
/* sub rsp,8; call the check; add rsp,8; ret: a site whose call stays in its block */
static void sr_site(uintptr_t site) {
    unsigned char code[14]={0x48,0x83,0xec,0x08,0xe8,0,0,0,0,0x48,0x83,0xc4,0x08,0xc3};
    int32_t rel=(int32_t)(AT(LX_CUE_CHECK_VA)-(AT(site)+5));memcpy(code+5,&rel,4);
    sr_code(site-4,code,sizeof(code),NULL);
}
static void sr_standin(void) {
    /* push rbp; push rbx; mov rbx,rdi; sub rsp,8; add rsp,8; pop rbx; pop rbp; jmp [rip] */
    static const unsigned char exec_code[]={0x55,0x53,0x48,0x89,0xfb,0x48,0x83,0xec,0x08,0x48,0x83,0xc4,0x08,0x5b,0x5d,0xff,0x25,0,0,0,0};
    sr_code(LX_EXEC_RUN_VA,exec_code,sizeof(exec_code),(void*)sr_exec_native);
    /* push r15; push r14; push r13; push r12; pop r12; pop r13; pop r14; pop r15; jmp [rip] */
    static const unsigned char range_code[]={0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x41,0x5c,0x41,0x5d,0x41,0x5e,0x41,0x5f,0xff,0x25,0,0,0,0};
    sr_code(LX_EXEC_RANGE_VA,range_code,sizeof(range_code),(void*)sr_range_native);
    code_jump(LX_AI_EVENT_VA,(void*)sr_ai_native);code_jump(LX_STACK_EVENT_VA,(void*)sr_stack_native);
    code_jump(LX_CUE_CHECK_VA,(void*)sr_check_native);code_jump(LX_CUE_EVENT_VA,(void*)AT(LX_CUE_CHECK_VA));
    void *slot=AT(LX_AI_EVENT_VA);memcpy(AT(LX_AI_EVENT_SLOT_VA),&slot,8);
    slot=AT(LX_STACK_EVENT_VA);memcpy(AT(LX_STACK_EVENT_SLOT_VA),&slot,8);
    slot=AT(LX_CUE_EVENT_VA);memcpy(AT(LX_CUE_EVENT_SLOT_VA),&slot,8);
    sr_site(LX_CUE_TIMED_SITE_VA);sr_site(LX_CUE_RAISE_SITE_VA);
    /* push r14; mov r14,rdi; mov rdi,r14; call the check; pop r14; ret: this call crosses its block */
    unsigned char wait_code[16]={0x41,0x56,0x49,0x89,0xfe,0x4c,0x89,0xf7,0xe8,0,0,0,0,0x41,0x5e,0xc3};
    int32_t rel=(int32_t)(AT(LX_CUE_CHECK_VA)-(AT(LX_CUE_WAIT_SITE_VA)+5));memcpy(wait_code+9,&rel,4);
    sr_code(LX_CUE_WAIT_SITE_VA-8,wait_code,sizeof(wait_code),NULL);
    memcpy(sr_block_timed,AT(LX_CUE_TIMED_SITE_VA&~15u),16);memcpy(sr_block_wait,AT(LX_CUE_WAIT_SITE_VA&~15u),16);memcpy(sr_block_raise,AT(LX_CUE_RAISE_SITE_VA&~15u),16);
    const char *name="order.fight.attack";size_t length=strlen(name);const void *data=sr_script+0x20;
    memcpy(sr_script+0x20,name,length);memcpy(sr_script+0x10,&data,8);memcpy(sr_script+0x18,&length,8);
    sr_program_header[0]=sr_program;sr_program_header[1]=sr_program+1;
    const void *context_vtable=AT(LX_AI_CONTEXT_VTABLE),*stack_vtable=AT(LX_STACK_VTABLE);
    for(unsigned i=0;i<3;i++) {
        unsigned char *c=sr_context[i];uint64_t runtime=7001+i;void *entity=sr_entity[i],*script=sr_script;
        memcpy(c,&context_vtable,8);memcpy(c+0x120,&entity,8);memcpy(sr_entity[i]+8,&runtime,8);memcpy(c+0xd0,&script,8);
    }
    memcpy(sr_stack,&stack_vtable,8);void *owner=sr_context[1];memcpy(sr_stack+0xc8,&owner,8);
    const unsigned owners[4]={0,1,3,2};
    for(unsigned i=0;i<4;i++) {
        unsigned char *x=sr_exec[i];void *program=sr_program_header;uint64_t pc=12+i;uint32_t tag=owners[i]==3?0x5d:0x66;
        uintptr_t payload=owners[i]==3?(uintptr_t)md_cue[0]+0xa0:(uintptr_t)sr_context[owners[i]]+0xa0;
        memcpy(x+0x10,&program,8);memcpy(x+0x18,&pc,8);memcpy(x+0x38,&tag,4);memcpy(x+0x40,&payload,8);
    }
}
/* Run from the frame loop; tick is the gate's issued tick. */
static void sr_frame(uint64_t tick) {
    if(tick<1 || tick>80 || tick==sr_last_tick) return;
    sr_last_tick=tick;
    uint64_t *word=(uint64_t*)(gate_tls()-0x2720);
    for(unsigned n=0;n<SR_CALLS;n++) {
        const unsigned which=script_reverse?SR_CALLS-1-n:n;
        const uint64_t natural=0x4000000000000000ull+((uint64_t)getpid()<<20)+tick*977u+n;
        *word=natural;
        void *handler;
        switch(which) {
        case 0: case 1: case 2: ((void (*)(void *))(void*)AT(LX_EXEC_RUN_VA))(sr_exec[which]);break;
        case 3: if(((uintptr_t (*)(void *,void *,uint64_t,uint64_t,uint32_t))(void*)AT(LX_EXEC_RANGE_VA))(sr_exec[3],sr_program_header,0,1,1)!=0x55) ++sr_word_lost;break;
        case 4: memcpy(&handler,AT(LX_AI_EVENT_SLOT_VA),8);((void (*)(void *))handler)(sr_context[0]);break;
        case 5: memcpy(&handler,AT(LX_STACK_EVENT_SLOT_VA),8);((void (*)(void *))handler)(sr_stack);break;
        case 6: sr_check_kind=0;memcpy(&handler,AT(LX_CUE_EVENT_SLOT_VA),8);((void (*)(void *))handler)(md_cue[0]);break;
        case 7: sr_check_kind=1;((void (*)(void *))(void*)AT(LX_CUE_TIMED_SITE_VA-4))(md_cue[0]);break;
        case 8: sr_check_kind=2;((void (*)(void *))(void*)AT(LX_CUE_WAIT_SITE_VA-8))(md_cue[1]);break;
        default: sr_check_kind=3;((void (*)(void *))(void*)AT(LX_CUE_RAISE_SITE_VA-4))(md_cue[0]);break;
        }
        if(*word!=natural) ++sr_word_lost;
    }
}
/* One object through AssignNewID. klass: the class bitmap it answers to; macro and parent as X4 lays them out. */
static uintptr_t cr_vtable_ship,cr_vtable_ui,cr_vtable_other;
static unsigned char cr_macro[0x80];
static const char cr_macro_name[]="ship_arg_s_fighter_01_a_macro";
static unsigned char *cr_make(uintptr_t vtable,unsigned char *parent,int macro) {
    if(cr_count==CR_OBJECTS) {fprintf(stderr,"created objects full\n");exit(2);}
    unsigned char *object=cr_objects[cr_count];memset(object,0,0x800);
    memcpy(object,&vtable,8);
    if(macro) {void *m=cr_macro;memcpy(object+0x60,&m,8);}
    memcpy(object+0x70,&parent,8);
    ((uint64_t (*)(void *))(void*)AT(LX_ID_ASSIGN_VA))(object);
    memcpy(&cr_runtimes[cr_count],object+8,8);++cr_count;
    return object;
}
/* The class bitmaps are read from the stub at vtable+0x11e8, decoded and never run. */
static void cr_stub(uintptr_t va,const uint32_t bits[8],uintptr_t stub_va,uintptr_t bitmap_va) {
    static const unsigned char head[6]={0x48,0x63,0xc6,0x48,0x8d,0x15};
    static const unsigned char tail[22]={0x83,0xe6,0x0f,0x48,0xc1,0xe8,0x04,0x48,0x8d,0x0c,0x36,0x8b,0x04,0x82,0xd3,0xe8,0xa8,0x03,0x0f,0x95,0xc0,0xc3};
    unsigned char code[32];memcpy(code,head,6);memcpy(code+10,tail,22);
    int32_t rel=(int32_t)(bitmap_va-(stub_va+10));memcpy(code+6,&rel,4);
    sr_code(stub_va,code,sizeof(code),NULL);
    sr_code(bitmap_va,(const unsigned char*)bits,32,NULL);
    void *stub=AT(stub_va);memcpy(AT(va+0x11e8),&stub,8);
}
static void cr_standin(void) {
    const uintptr_t ship=0x3d23cd0u,ui=0x3d3b040u,other=0x3c00000u; /* ShipMedium, UIElement, not a Component */
    cr_vtable_ship=(uintptr_t)AT(ship);cr_vtable_ui=(uintptr_t)AT(ui);cr_vtable_other=(uintptr_t)AT(other);
    uint32_t component[8]={0};component[1]|=1u<<((22-16)*2);
    uint32_t element[8];memcpy(element,component,32);element[105/16]|=1u<<((105%16)*2);
    uint32_t none[8]={0};
    cr_stub(ship,component,LX_TEXT_VA+0x800,LX_TEXT_VA+0xa00);
    cr_stub(ui,element,LX_TEXT_VA+0x840,LX_TEXT_VA+0xa40);
    cr_stub(other,none,LX_TEXT_VA+0x880,LX_TEXT_VA+0xa80);
    const void *macro_vtable=AT(LX_MACRO_VTABLE),*getter=AT(LX_MACRO_NAME_GETTER_VA);
    memcpy(cr_macro,&macro_vtable,8);memcpy(AT(LX_MACRO_VTABLE+19u*8u),&getter,8);
    uintptr_t name=(uintptr_t)cr_macro_name;uint64_t length=strlen(cr_macro_name);memcpy(cr_macro+0x20,&name,8);memcpy(cr_macro+0x28,&length,8);
}
/* What the load makes after the import: a ship and its child. */
static void cr_load(void) {
    unsigned char *ship=cr_make(cr_vtable_ship,NULL,1);
    cr_make(cr_vtable_ship,ship,1);
}
/* At two ticks: a ship with a child made before it in the table's order, a view object, something else, and one that dies at once. */
static uint64_t cr_last_tick;
static void cr_frame(uint64_t tick) {
    if(tick==cr_last_tick || (tick!=10 && tick!=20)) return;
    cr_last_tick=tick;
    unsigned char *ship=cr_make(cr_vtable_ship,NULL,1);
    cr_make(cr_vtable_ship,ship,1);
    cr_make(cr_vtable_ship,ship,0);
    cr_make(cr_vtable_ui,NULL,0);
    cr_make(cr_vtable_other,NULL,0);
    cr_make(cr_vtable_ship,NULL,1);--cr_count; /* transient: gone from the find before the boundary */
}
/* The navigation deadlines (ENGINE_NAV=1): two controllers through the vtable slots, one parameter block on with a
 * deadline of this process's own from the load. The native refreshes when due, drawing from the word every peer has
 * this tick, and hashes the deadline; a peer that refreshes on another tick parts for good. */
static int nav_on;
static __attribute__((aligned(16))) unsigned char nv_controllers[2][0x100],nv_ships[2][0x20],nv_probes[2][0x400];
static uint64_t nv_hash=0xcbf29ce484222325ULL,nv_refreshes,nv_last_tick;
static int nv_word2;
static void nv_native(void *self,double time) {
    unsigned char *c=self;double due;unsigned char on=c[0xd8];memcpy(&due,c+0xd0,8);
    if(on && time>due) {
        uint64_t *word=(uint64_t*)(gate_tls()-0x2720);
        *word=*word*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;*word=(*word>>30)|(*word<<34);
        due=time+1.0+(double)(*word>>40)/16777216.0;memcpy(c+0xd0,&due,8);++nv_refreshes;
    }
    uint64_t bits;memcpy(&bits,c+0xd0,8);
    if(on) nv_hash=(nv_hash^bits)*0x100000001b3ULL;
}
static void nv_standin(void) {
    code_jump(LX_NAV_MOVE_VA,(void*)nv_native);
    void *move=AT(LX_NAV_MOVE_VA);memcpy(AT(LX_NAV_MOVE_SLOT_A_VA),&move,8);memcpy(AT(LX_NAV_MOVE_SLOT_B_VA),&move,8);
    static const unsigned char near[2]={0x77,0x2e},far_params[6]={0x0f,0x86,0x71,0x08,0,0},far_probe[6]={0x0f,0x86,0x48,0x08,0,0};
    sr_code(LX_NAV_PARAMS_NEAR_VA,near,2,NULL);sr_code(LX_NAV_PARAMS_FAR_VA,far_params,6,NULL);
    sr_code(LX_NAV_PROBE_NEAR_VA,near,2,NULL);sr_code(LX_NAV_PROBE_FAR_VA,far_probe,6,NULL);
    for(unsigned i=0;i<2;i++) {
        uint64_t runtime=7001+i;memcpy(nv_ships[i]+8,&runtime,8);
        void *ship=nv_ships[i],*probe=nv_probes[i];memcpy(nv_controllers[i]+0x20,&ship,8);memcpy(nv_controllers[i]+0x40,&probe,8);
        double due=1000.0+(double)((unsigned)getpid()%97u)/10.0+i;memcpy(nv_controllers[i]+0xd0,&due,8);memcpy(nv_probes[i]+0x3e0,&due,8);
        nv_controllers[i][0xd8]=i==0;
    }
}
static void nv_frame(uint64_t tick) {
    if(tick<1 || tick>80 || tick==nv_last_tick) return;
    nv_last_tick=tick;
    uint64_t word=0x7000000000000000ull+tick+(nv_word2?0x55u:0u);memcpy(gate_tls()-0x2720,&word,8); /* ENGINE_NAV_WORD2: another natural word */
    double time=1000.0+(double)tick/60.0;void *slot;
    memcpy(&slot,AT(LX_NAV_MOVE_SLOT_A_VA),8);((void (*)(void *,double))slot)(nv_controllers[0],time);
    memcpy(&slot,AT(LX_NAV_MOVE_SLOT_B_VA),8);((void (*)(void *,double))slot)(nv_controllers[1],time);
}
/* push rbp; mov rbp,rsp; push r15; push r14; pop r14; pop r15; pop rbp; jmp [rip] */
static uintptr_t spawn_native(void *,void *,void *,void *,uintptr_t,void *,void *,uintptr_t,uintptr_t,uintptr_t);
static void spawn_standin(void) {
    static const unsigned char code[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x5e,0x41,0x5f,0x5d,0xff,0x25,0,0,0,0};
    sr_code(LX_GENERATE_VA,code,sizeof(code),(void*)spawn_native);
}
/* find_* candidate lists (ENGINE_FIND_ORDER=1; ENGINE_FIND_ORDER_REVERSE on one peer): one call of the store per tick,
 * through the pinned call, with a list in the peer's own order, a repeated entry and an object without a saved ID. The
 * native store hashes what it is given. */
static int find_on,find_reverse;
static __attribute__((aligned(16))) unsigned char find_objects[4][0x20],find_declaration[8],find_exec[8];
static uint64_t find_hash=0xcbf29ce484222325ULL,find_calls,find_last_tick;
static uintptr_t find_assign_native(void *declaration,void **vector,void *exec,void *component) {
    if(declaration!=find_declaration || exec!=find_exec || component) {fprintf(stderr,"store arguments lost\n");exit(2);}
    for(void **p=(void**)vector[0];p<(void**)vector[1];p++) {
        uint64_t runtime;memcpy(&runtime,(unsigned char*)*p+8,8);find_hash=(find_hash^runtime)*0x100000001b3ULL;
    }
    find_hash=(find_hash^0xff)*0x100000001b3ULL;++find_calls;return 0x77;
}
static void find_standin(void) {
    code_jump(LX_FIND_ORDER_ASSIGN_VA,(void*)find_assign_native);
    /* push r13; mov r13,rsi; mov rsi,r13; call the store; pop r13; ret */
    unsigned char code[15]={0x41,0x55,0x49,0x89,0xf5,0x4c,0x89,0xee,0xe8,0,0,0,0,0x41,0x5d};
    int32_t rel=(int32_t)(AT(LX_FIND_ORDER_ASSIGN_VA)-(AT(LX_FIND_ORDER_SITE_VA)+5));memcpy(code+9,&rel,4);
    unsigned char *at=AT(LX_FIND_ORDER_SITE_VA-8);void *page=(void*)((uintptr_t)at&~(uintptr_t)4095);
    if(mprotect(page,8192,PROT_READ|PROT_WRITE)) {perror("mprotect");exit(2);}
    memcpy(at,code,sizeof(code));at[sizeof(code)]=0xc3;
    if(mprotect(page,8192,PROT_READ|PROT_EXEC)) {perror("mprotect");exit(2);}
    const uint64_t runtimes[4]={7003,7001,9999,7002};
    for(unsigned i=0;i<4;i++) memcpy(find_objects[i]+8,&runtimes[i],8);
}
static void find_frame(uint64_t tick) {
    if(tick<1 || tick>80 || tick==find_last_tick) return;
    find_last_tick=tick;
    void *list[5],*vector[3];
    for(unsigned i=0;i<4;i++) list[i]=find_objects[find_reverse?3-i:i];
    list[4]=list[1]; /* nothing removes a component seen twice */
    vector[0]=list;vector[1]=list+5;vector[2]=list+5;
    uintptr_t (*call)(void *,void *,void *,void *)=(uintptr_t (*)(void *,void *,void *,void *))(void*)AT(LX_FIND_ORDER_SITE_VA-8);
    if(call(find_declaration,vector,find_exec,NULL)!=0x77) {fprintf(stderr,"store result lost\n");exit(2);}
}
/* The Linear step trace (ENGINE_STEP_TRACE=1): the model's vtable entry holds a stand-in step at the step's
 * address; per tick 1-5 one call through the entry for a ship with a saved ID (7001) and one without (9999). */
static int st_on;
static uint64_t st_engine_calls,st_last_tick;
static __attribute__((aligned(16))) unsigned char st_ships[2][0x20],st_agents[2][0x1000],st_models[2][0x200];
static void st_engine_step(void *model,const void *vector,float dt,double time) {
    unsigned char *m=model;float v[4];memcpy(v,vector,16);const float x=v[0]+dt;memcpy(m+0x10,&x,4);memcpy(m+0x78,&time,8);++st_engine_calls;
}
static void st_standin(void) {
    code_jump(LX_ST_STEP_VA,(void*)st_engine_step);
    void *step=AT(LX_ST_STEP_VA);memcpy(AT(LX_ST_SLOT_VA),&step,8);
    const uint64_t runtime[2]={7001,9999};
    for(unsigned i=0;i<2;i++) {
        memcpy(st_ships[i]+8,&runtime[i],8);void *ship=st_ships[i],*agent=st_agents[i];
        memcpy(st_agents[i]+0xf10,&ship,8);memcpy(st_models[i]+0x8,&agent,8);
    }
}
static void st_frame(uint64_t tick) {
    if(tick<1 || tick>5 || tick==st_last_tick) return;
    st_last_tick=tick;
    void *entry;memcpy(&entry,AT(LX_ST_SLOT_VA),8);const float vector[4]={1.0f,2.0f,3.0f,0.0f};
    for(unsigned i=0;i<2;i++) ((void (*)(void *,const void *,float,double))entry)(st_models[i],vector,0.25f,100.0+(double)tick);
}
/* Objects' attention timers (ENGINE_ATTENTION=1; ENGINE_ATTENTION_PEER2 on one peer): the timer behind its pinned
 * prologue does what the executable's does (due at +0x2d8, the thread's time and word). Two objects with saved IDs and
 * one without, whose loading dues differ by peer, and on one peer the word one step further; per tick 1-80 one call
 * each. The saved objects' dues are hashed. */
static int at_on,at_peer2;
static uint64_t at_hash=0xcbf29ce484222325ULL,at_evaluations,at_last_tick;
static __attribute__((aligned(16))) unsigned char at_objects[3][0x300];
static void at_timer(void *self) {
    unsigned char *object=self;double now,due;
    memcpy(&now,gate_tls()-0x2728,8);memcpy(&due,object+0x2d8,8);
    if(now<due) return;
    uint64_t *word=(uint64_t*)(gate_tls()-0x2720);
    *word=*word*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;*word=(*word>>30)|(*word<<34);
    due=now+0.75+(double)(uint32_t)*word*(0.5/4294967296.0);memcpy(object+0x2d8,&due,8);++at_evaluations;
}
static void at_standin(void) {
    unsigned char code[32]={0x55,0x53,0x48,0x89,0xfb,0x48,0x83,0xec,0x18,0x48,0x83,0xc4,0x18,0x48,0x89,0xdf,0x5b,0x5d,0xff,0x25,0,0,0,0};
    void *target=(void*)at_timer;memcpy(code+24,&target,8);
    unsigned char *at=AT(LX_ATTENTION_VA);void *page=(void*)((uintptr_t)at&~(uintptr_t)4095);
    if(mprotect(page,8192,PROT_READ|PROT_WRITE)) {perror("mprotect");exit(2);}
    memcpy(at,code,sizeof(code));
    if(mprotect(page,8192,PROT_READ|PROT_EXEC)) {perror("mprotect");exit(2);}
    const uint64_t runtime[3]={7001,7002,9999};const double loading=at_peer2?100.2:99.9;
    for(unsigned i=0;i<3;i++) {memcpy(at_objects[i]+8,&runtime[i],8);memcpy(at_objects[i]+0x2d8,&loading,8);}
}
static void at_frame(uint64_t tick) {
    if(tick<1 || tick>80 || tick==at_last_tick) return;
    at_last_tick=tick;
    const double now=100.0+(double)tick/60.0;memcpy(gate_tls()-0x2728,&now,8);
    if(at_peer2) {uint64_t *word=(uint64_t*)(gate_tls()-0x2720);*word=*word*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;}
    void (*timer)(void *)=(void (*)(void *))(void*)AT(LX_ATTENTION_VA);
    for(unsigned i=0;i<3;i++) timer(at_objects[i]);
    for(unsigned i=0;i<2;i++) {uint64_t bits;memcpy(&bits,at_objects[i]+0x2d8,8);at_hash=(at_hash^bits)*0x100000001b3ULL;}
}
/* Stations' loadout targets (ENGINE_LOADOUT=1; ENGINE_LOADOUT_REVERSE on one peer): the six calls and both rests'
 * first instructions as the executable has them, an epilogue at each rest's exit. Per tick 1-80, one unit target call
 * through its pinned site and, once the rest loop leads to the gate, one unit rest of four over three: three drone
 * macros whose address order is the peer's own. The names in walk order and the counts are hashed. */
static int ll_on,ll_reverse;
static uint64_t ll_hash=0xcbf29ce484222325ULL,ll_calls_seen,ll_rests_seen,ll_last_tick;
typedef struct {unsigned char *parent,*left,*right;uint64_t pad;const void *key,*key2;int32_t amount,pad2;uint64_t pad3;} ll_node_t;
typedef struct {unsigned char *self,*root;uint64_t pad[2];unsigned char *first;uint64_t size;} ll_map_t;
typedef struct {const void *vtable;unsigned char pad[0x18];const char *data;uint64_t length;char local[16];} ll_macro_t;
static const void *ll_vtable[20];
static const char *const ll_names[3]={"ship_gen_xs_cargodrone_empty_01_a_macro","ship_gen_xs_repairdrone_empty_01_a_macro","ship_gen_xs_buildingdrone_01_a_macro"};
static ll_macro_t ll_macros[3];
static ll_node_t ll_nodes3[3];
static ll_map_t ll_map;
static void ll_put(uintptr_t va,const void *bytes,size_t size) {
    unsigned char *at=AT(va);uintptr_t page=(uintptr_t)at&~(uintptr_t)4095;size_t span=((uintptr_t)at+size-page+4095)&~(size_t)4095;
    if(mprotect((void*)page,span,PROT_READ|PROT_WRITE)) {perror("mprotect");exit(2);}
    memcpy(at,bytes,size);
    if(mprotect((void*)page,span,PROT_READ|PROT_EXEC)) {perror("mprotect");exit(2);}
}
static void ll_fill(void) {
    memset(&ll_map,0,sizeof(ll_map));memset(ll_nodes3,0,sizeof(ll_nodes3));
    ll_node_t *a=&ll_nodes3[0],*b=&ll_nodes3[1],*c=&ll_nodes3[2];
    ll_map.self=(unsigned char*)&ll_map;ll_map.root=(unsigned char*)b;ll_map.first=(unsigned char*)a;ll_map.size=3;
    b->parent=(unsigned char*)&ll_map;b->left=(unsigned char*)a;b->right=(unsigned char*)c;a->parent=c->parent=(unsigned char*)b;
    for(unsigned i=0;i<3;i++) ll_nodes3[i].key=&ll_macros[i];
}
static uint64_t ll_unit_native(void *container,void *map,void *c,void *d,void *e,void *f) {
    (void)c;(void)d;(void)e;(void)f;
    if(container!=(void*)&ll_vtable || map!=(void*)&ll_map) {fprintf(stderr,"loadout arguments lost\n");exit(2);}
    ll_fill();return 0x66;
}
static void ll_note(uint64_t value) {ll_hash=(ll_hash^value)*0x100000001b3ULL;}
static void ll_note_walk(void) {
    unsigned k=0;
    for(unsigned char *n=ll_map.first;n!=(unsigned char*)&ll_map && k<4;k++) {
        const ll_node_t *node=(const ll_node_t*)n;ll_note((unsigned char)((const ll_macro_t*)node->key)->data[12]);ll_note((uint64_t)node->amount);
        unsigned char *right=node->right;
        if(right) {while(((ll_node_t*)right)->left) right=((ll_node_t*)right)->left;n=right;}
        else {unsigned char *parent=node->parent;while(((ll_node_t*)parent)->left!=n && parent!=(unsigned char*)&ll_map) {n=parent;parent=((ll_node_t*)parent)->parent;}n=parent;}
    }
    ll_note(0xff);
}
__asm__(
".text\n"
"ll_enter_unit:\n" /* rdi code, rsi map, edx count, ecx size: the unit rest's stack, entered by jmp */
" push %r15\n push %r14\n push %r13\n push %r12\n push %rbp\n push %rbx\n sub $0x88,%rsp\n"
" mov %rsi,0x18(%rsp)\n movq $0,0x60(%rsp)\n mov %edx,%r15d\n mov %ecx,%ebx\n mov %rdi,%rax\n xor %edi,%edi\n jmp *%rax\n"
);
extern void ll_enter_unit(const void *code,void *map,uint32_t count,uint32_t size);
static void ll_standin(void) {
    const uintptr_t sites[6]={LX_LL_WARES_SITE_VA,LX_LL_AMMO_SITE_VA,LX_LL_DEPLOYABLE_SITE_VA,LX_LL_UNIT_SITE_VA,LX_LL_EXISTING_SITE_VA,LX_LL_PICK_SITE_VA};
    const uintptr_t targets[6]={LX_LL_WARES_VA,LX_LL_AMMO_VA,LX_LL_DEPLOYABLE_VA,LX_LL_UNIT_VA,LX_LL_EXISTING_VA,LX_LL_PICK_VA};
    for(unsigned i=0;i<6;i++) {
        unsigned char call[5]={0xe8};int32_t rel=(int32_t)(AT(targets[i])-(AT(sites[i])+5));memcpy(call+1,&rel,4);ll_put(sites[i],call,5);
    }
    /* sub rsp,8 before the unit call and add rsp,8; ret after it: a function at the site - 4. */
    static const unsigned char before[4]={0x48,0x83,0xec,0x08},after[5]={0x48,0x83,0xc4,0x08,0xc3};
    ll_put(LX_LL_UNIT_SITE_VA-4,before,4);ll_put(LX_LL_UNIT_SITE_VA+5,after,5);
    code_jump(LX_LL_UNIT_VA,(void*)ll_unit_native);
    static const unsigned char unit_rest[5]={0x4c,0x8b,0x4c,0x24,0x18},deployable_rest[5]={0x4c,0x8b,0x5c,0x24,0x40};
    ll_put(LX_LL_REST_UNIT_VA,unit_rest,5);ll_put(LX_LL_REST_DEPLOYABLE_VA,deployable_rest,5);
    static const unsigned char exit0[18]={0x48,0x81,0xc4,0x88,0,0,0,0x5b,0x5d,0x41,0x5c,0x41,0x5d,0x41,0x5e,0x41,0x5f,0xc3};
    ll_put(LX_LL_REST_UNIT_EXIT_VA,exit0,sizeof(exit0));
    ll_vtable[19]=AT(LX_MACRO_NAME_GETTER_VA);
    for(unsigned i=0;i<3;i++) {
        const char *name=ll_names[ll_reverse?2-i:i];
        ll_macros[i].vtable=ll_vtable;ll_macros[i].data=name;ll_macros[i].length=strlen(name);
    }
}
static void ll_frame(uint64_t tick) {
    if(tick<1 || tick>80 || tick==ll_last_tick) return;
    ll_last_tick=tick;
    uint64_t (*call)(void *,void *)=(uint64_t (*)(void *,void *))(void*)AT(LX_LL_UNIT_SITE_VA-4);
    if(call(&ll_vtable,&ll_map)!=0x66) {fprintf(stderr,"loadout result lost\n");exit(2);}
    ++ll_calls_seen;ll_note_walk();
    if(*AT(LX_LL_REST_UNIT_VA)!=0xe9) return; /* the loop is not the gate's: the synthetic bytes after it are no loop */
    ll_fill();ll_enter_unit(AT(LX_LL_REST_UNIT_VA),&ll_map,4,3);++ll_rests_seen;
    /* The amounts by macro, in the names' order (the nodes' order is the peer's own). */
    for(unsigned k=0;k<3;k++) for(unsigned i=0;i<3;i++)
        if(((const ll_macro_t*)ll_nodes3[i].key)->data==ll_names[k]) {ll_note(k);ll_note((uint64_t)ll_nodes3[i].amount);}
}
/* Ship loadouts (ENGINE_SPAWN=1): Generate behind its pinned prologue, called in the load with an explicit state, from
 * a natural word of this process. The native draws once and hashes what it drew. A null state keeps the natural word. */
static int spawn_on;
static uint64_t spawn_hash=0xcbf29ce484222325ULL,spawn_calls;
static unsigned spawn_lost;
static uint64_t spawn_states[3]={0x1111,0x2222,0x3333};
static uintptr_t spawn_native(void *result,void *generator,void *c,void *d,uintptr_t e,void *f,void *state,uintptr_t h,uintptr_t i,uintptr_t j) {
    if(!result || !generator || c || d || e!=5 || f || h!=8 || i!=9 || j!=10) {fprintf(stderr,"generate arguments lost\n");exit(2);}
    uint64_t *word=(uint64_t*)(gate_tls()-0x2720);
    *word=*word*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;*word=(*word>>30)|(*word<<34);
    if(state) spawn_hash=(spawn_hash^*word)*0x100000001b3ULL;
    ++spawn_calls;return 0x99;
}
static void spawn_standin(void);
static void spawn_load(void) {
    uint64_t *word=(uint64_t*)(gate_tls()-0x2720);const uint64_t natural=0x5000000000000000ull+(uint64_t)getpid();
    static unsigned char result[8],generator[8];
    uintptr_t (*call)(void *,void *,void *,void *,uintptr_t,void *,void *,uintptr_t,uintptr_t,uintptr_t)=
        (uintptr_t (*)(void *,void *,void *,void *,uintptr_t,void *,void *,uintptr_t,uintptr_t,uintptr_t))(void*)AT(LX_GENERATE_VA);
    for(unsigned n=0;n<4;n++) {
        *word=natural;
        void *state=n<3?(void*)&spawn_states[n]:NULL;
        if(call(result,generator,NULL,NULL,5,NULL,state,8,9,10)!=0x99) ++spawn_lost;
        if(n<3 && *word!=natural) ++spawn_lost;
    }
}
/* The boot hold (ENGINE_BOOT_HOLD=1): AIDirector::Move once on the UI thread when the load ends
 * (boot phase 2, as UniverseFrameUpdate does), then once per frame; a counting native Move
 * behind the pinned prologue. */
static int boot_hold_on;
static volatile int boot_in_phase2;
static unsigned boot_native_moves,boot_native_phase2;
static sem_t hold_go,hold_done;
static unsigned char boot_director[0x300];
static void boot_move_native(void *director) {
    if(director!=boot_director) {fprintf(stderr,"director lost\n");exit(2);}
    if(boot_in_phase2) ++boot_native_phase2;else ++boot_native_moves;
}
static void boot_standin(void) {
    unsigned char *entry=AT(LX_BOOT_HOLD_MOVE_VA);
    uintptr_t page=(uintptr_t)entry&~(uintptr_t)4095;
    if(mprotect((void*)page,8192,PROT_READ|PROT_WRITE)) {perror("mprotect");exit(2);}
    /* push rbp; mov rbp,rsp; push r15; push r14; pop r14; pop r15; pop rbp; jmp [rip] */
    static const unsigned char code[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x5e,0x41,0x5f,0x5d,0xff,0x25,0,0,0,0};
    memcpy(entry,code,sizeof(code));
    void *native=(void*)boot_move_native;memcpy(entry+sizeof(code),&native,8);
    if(mprotect((void*)page,8192,PROT_READ|PROT_EXEC)) {perror("mprotect");exit(2);}
    void *director=boot_director;memcpy(AT(LX_BOOT_HOLD_DIRECTOR_VA),&director,8);
}
static void boot_move_call(void) {((void (*)(void *))(void*)AT(LX_BOOT_HOLD_MOVE_VA))(boot_director);}
/* Event delivery (ENGINE_EVENT_ORDER=1; ENGINE_EVENT_ORDER_REVERSE on one peer): a due batch per frame
 * through the pinned call, slots in the peer's own address order; a hash of what was delivered. */
static int event_order_on,event_reverse;
#define EVENT_SOURCES 6u
static __attribute__((aligned(16))) unsigned char event_sources[EVENT_SOURCES][32],event_events[EVENT_SOURCES][16];
static uint64_t event_hash=0xcbf29ce484222325ULL,event_native_hash=0xcbf29ce484222325ULL;
static unsigned event_delivered;
static int event_hooked; /* only what ran under the hook: after the stop the peers keep their native orders */
static uint64_t event_mix(uint64_t hash,uint64_t value) {
    for(unsigned i=0;i<8;i++) {hash^=(value>>(i*8))&255;hash*=0x100000001b3ULL;}
    return hash;
}
static void event_deliver_standin(void *source,void *event,double time) {
    uint64_t runtime,bits;memcpy(&runtime,(unsigned char*)source+8,8);memcpy(&bits,&time,8);
    if(!event_hooked) return;
    event_hash=event_mix(event_mix(event_mix(event_hash,runtime),(uint64_t)((unsigned char*)event-event_events[0])),bits);++event_delivered;
}
static void event_standin(void) {
    code_jump(LX_EVENT_DELIVER_VA,(void*)event_deliver_standin);
    /* push r13; mov r13,rdi; mov rdi,[r13+0x18]; movsd xmm0,[r13+0x30]; nops; mov rsi,[r13+0x28]; call; pop r13; ret */
    unsigned char *call=AT(LX_EVENT_CALL_VA),*entry=call-0x23;
    uintptr_t page=(uintptr_t)entry&~(uintptr_t)4095;
    if(mprotect((void*)page,8192,PROT_READ|PROT_WRITE)) {perror("mprotect");exit(2);}
    memset(entry,0x90,0x23);
    memcpy(entry,"\x41\x55\x49\x89\xfd\x49\x8b\x7d\x18\xf2\x41\x0f\x10\x45\x30",15);
    memcpy(call-4,"\x49\x8b\x75\x28\xe8",5);
    int32_t rel=(int32_t)(AT(LX_EVENT_DELIVER_VA)-(call+5));memcpy(call+1,&rel,4);
    memcpy(call+5,"\x41\x5d\xc3",3);
    if(mprotect((void*)page,8192,PROT_READ|PROT_EXEC)) {perror("mprotect");exit(2);}
    for(unsigned i=0;i<EVENT_SOURCES;i++) {uint64_t runtime=i==5?9000:7001+i;memcpy(event_sources[i]+8,&runtime,8);}
}
/* One pass of ProcessDueEvents' delivery loop: the slot read at the call, then released. */
static void event_frame(void) {
    unsigned char *batch=AT(LX_EVENT_BATCH_VA);
    double due=1000.0;
    uint64_t count=EVENT_SOURCES,word;
    memcpy(&word,AT(LX_EVENT_CALL_WORD_VA),8);event_hooked=word!=LX_EVENT_CALL_WORD_ORIGINAL;
    for(unsigned i=0;i<EVENT_SOURCES;i++) {
        unsigned which=event_reverse?EVENT_SOURCES-1-i:i;
        void *source=event_sources[which],*event=event_events[which];
        unsigned char *slot=batch+i*0x38;
        memcpy(slot+0x18,&source,8);memcpy(slot+0x28,&event,8);memcpy(slot+0x30,&due,8);
        event_native_hash=event_mix(event_native_hash,(uint64_t)which);
    }
    memcpy(AT(LX_EVENT_COUNT_VA),&count,8);
    void (*call)(void *)=(void (*)(void *))(void*)(AT(LX_EVENT_CALL_VA)-0x23);
    for(unsigned i=0;i<EVENT_SOURCES;i++) {
        unsigned char *slot=batch+i*0x38;void *source;memcpy(&source,slot+0x18,8);
        if(source) call(slot);
    }
    for(unsigned i=0;i<EVENT_SOURCES;i++) memset(batch+i*0x38+0x18,0,8);
    count=0;memcpy(AT(LX_EVENT_COUNT_VA),&count,8);
}
/* The Gravidar (ENGINE_GRAVIDAR=1; ENGINE_GRAVIDAR_REVERSE on one peer): a worker with a sector list in the
 * peer's own address order, a thread named Gravidar with Run's frame on its stack, its own TLS word and game
 * time, one unit per sector. What a pass visits and draws goes into a hash. */
static int gravidar_on,gravidar_reverse;
#define GV_SECTORS 13u
static __attribute__((aligned(16))) unsigned char gv_worker[0x7800],gv_sectors[GV_SECTORS][0x80],gv_units[GV_SECTORS][0x40],gv_macro[0x40],gv_list[GV_SECTORS][32];
static const char gv_macro_name[]="cluster_77_sector001_macro";
static sem_t gv_kick;
static uint64_t gv_hash=0xcbf29ce484222325ULL,gv_passes,gv_last_word;
static volatile int gv_stop;
static void gv_mutex_init(void *mutex) {
    pthread_mutexattr_t attr;pthread_mutexattr_init(&attr);pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init((pthread_mutex_t*)((unsigned char*)mutex+8),&attr);
}
static void gv_lock(void *mutex) {pthread_mutex_lock((pthread_mutex_t*)((unsigned char*)mutex+8));}
static void gv_unlock(void *mutex) {pthread_mutex_unlock((pthread_mutex_t*)((unsigned char*)mutex+8));}
static void gz_check(void);
static int gz_on;
static void gv_publish(void *worker,unsigned char reset) {
    if(gz_on) gz_check();
    (void)reset;unsigned char *w=worker;uint32_t enabled;memcpy(&enabled,w+0x44,4);
    if(!enabled) return;
    gv_lock(w+0x7168);
    if(w[0x7198]) {w[0x7198]=0;sem_post(&gv_kick);}
    gv_unlock(w+0x7168);
}
static void gv_unit_native(void *unit,void *queue,void *result) {
    (void)unit;(void)queue;(void)result;
    uint64_t *word=(uint64_t*)(gate_tls()-0x2720);
    *word=*word*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;*word=(*word>>30)|(*word<<34);
    gv_last_word=*word;
}
/* The zone groups (ENGINE_GRAVIDAR_ZONES=1): three saved zones and one without an ID in each sector entry, in this
 * peer's own order, each group naming the next as its neighbour. What a pass sees goes into a hash; a publish must
 * find them back in the peer's order. */
static int gz_on;
static __attribute__((aligned(16))) unsigned char gz_zones[4][0x20],gz_groups[GV_SECTORS][4][0x58];
static uint64_t gz_lists[GV_SECTORS][4];
static uint64_t gz_hash=0xcbf29ce484222325ULL;
static unsigned gz_lost;
static volatile int gz_gated; /* only the gated ticks: earlier passes keep the address order */
static void gz_note(unsigned char *entry) {
    unsigned char *begin,*end;memcpy(&begin,entry+8,8);memcpy(&end,entry+16,8);
    for(unsigned char *g=begin;g<end;g+=0x58) {
        unsigned char *zone;uint64_t runtime,*list;memcpy(&zone,g,8);memcpy(&runtime,zone+8,8);memcpy(&list,g+0x38,8);
        uint64_t neighbour=(uint64_t)-1;
        if(list) {unsigned char *other;memcpy(&other,begin+*list*0x58,8);memcpy(&neighbour,other+8,8);}
        gz_hash=(gz_hash^runtime)*0x100000001b3ULL;gz_hash=(gz_hash^neighbour)*0x100000001b3ULL;
    }
}
/* The peer's own order, as the publish must find it. */
static void gz_check(void) {
    for(unsigned s=0;s<GV_SECTORS;s++) for(unsigned i=0;i<4;i++) {
        unsigned char *zone;memcpy(&zone,gz_groups[s][i],8);
        if(zone!=gz_zones[gravidar_reverse?3-i:i]) ++gz_lost;
    }
}
static void gv_apply(unsigned char *entry,int counting) {
    if(gz_on && gz_gated) gz_note(entry);
    unsigned char *sector;memcpy(&sector,entry,8);
    unsigned index=(unsigned)((sector-gv_sectors[0])/0x80);uint64_t runtime,now;
    void (*slot)(void *,void *,void *);memcpy(&slot,AT(LX_GRAVIDAR_UNIT_SLOT_VA),8);
    char queue[8],result[64];memset(result,0,sizeof(result));
    slot(gv_units[index],queue,result);
    memcpy(&runtime,sector+8,8);memcpy(&now,gate_tls()-0x2728,8);
    if(counting) gv_hash=event_mix(event_mix(event_mix(gv_hash,runtime),gv_last_word),now);
}
static void *gv_thread(void *unused) {
    (void)unused;
    pthread_setname_np(pthread_self(),"Gravidar");
    /* Run's frame: the return address at [48], worker+0x40 below it, the cursor below that. */
    uint64_t frame[64];memset(frame,0,sizeof(frame));
    frame[48]=(uint64_t)(uintptr_t)AT(LX_TEXT_VA+0x100);frame[48-0xa8/8]=(uint64_t)(uintptr_t)gv_worker+0x40;
    volatile uint64_t *cursor=&frame[48-0xf8/8];
    uint64_t natural=0x4000000000000000ull+(uint64_t)getpid();memcpy(gate_tls()-0x2720,&natural,8);
    double start=0;memcpy(gate_tls()-0x2728,&start,8);
    unsigned char *w=gv_worker;
    for(;;) {
        struct timespec until;clock_gettime(CLOCK_REALTIME,&until);until.tv_nsec+=20000000;
        if(until.tv_nsec>=1000000000) {until.tv_sec++;until.tv_nsec-=1000000000;}
        if(sem_timedwait(&gv_kick,&until)) {if(gv_stop) return NULL;continue;}
        gv_lock(w+0x7168);gv_unlock(w+0x7168); /* the ready lock barrier: the order is applied under it */
        void *apply=AT(LX_GRAVIDAR_UNIT_APPLY_VA),*now;memcpy(&now,AT(LX_GRAVIDAR_UNIT_SLOT_VA),8);
        int counting=now!=apply;
        unsigned char *begin,*end;uint64_t priority;
        gv_lock(w+0x7090);
        memcpy(&begin,w+0x70c0,8);memcpy(&end,w+0x70c8,8);memcpy(&priority,w+0x70d8,8);
        uint64_t count=(uint64_t)(end-begin)/32,step=count/10?count/10:1;
        gv_apply(begin+priority*32,counting);
        for(uint64_t i=0;i<step;i++) {
            if(*cursor!=priority) gv_apply(begin+*cursor*32,counting);
            *cursor=*cursor+1<count?*cursor+1:0;
        }
        gv_unlock(w+0x7090);
        if(counting) ++gv_passes;
        gv_lock(w+0x7168);w[0x7198]=1;gv_unlock(w+0x7168);
    }
}
static void gv_standin(void) {
    code_jump(LX_MUTEX_LOCK_VA,(void*)gv_lock);code_jump(LX_MUTEX_UNLOCK_VA,(void*)gv_unlock);
    code_jump(LX_GRAVIDAR_PUBLISH_VA,(void*)gv_publish);code_jump(LX_GRAVIDAR_UNIT_APPLY_VA,(void*)gv_unit_native);
    /* sub rsp,8; nops; xor esi,esi; call publish; add rsp,8; ret */
    unsigned char *call=AT(LX_GRAVIDAR_CALL_VA),*entry=call-9;
    uintptr_t page=(uintptr_t)entry&~(uintptr_t)4095;
    if(mprotect((void*)page,8192,PROT_READ|PROT_WRITE)) {perror("mprotect");exit(2);}
    memcpy(entry,"\x48\x83\xec\x08\x90\x90\x90\x31\xf6\xe8",10);
    int32_t rel=(int32_t)(AT(LX_GRAVIDAR_PUBLISH_VA)-(call+5));memcpy(call+1,&rel,4);
    memcpy(call+5,"\x48\x83\xc4\x08\xc3",5);
    if(mprotect((void*)page,8192,PROT_READ|PROT_EXEC)) {perror("mprotect");exit(2);}
    void *apply=AT(LX_GRAVIDAR_UNIT_APPLY_VA);memcpy(AT(LX_GRAVIDAR_UNIT_SLOT_VA),&apply,8);
    void *worker=gv_worker;memcpy(AT(LX_GRAVIDAR_WORKER_VA),&worker,8);
    const void *vtable=AT(LX_GRAVIDAR_VTABLE);memcpy(gv_worker,&vtable,8);
    uint32_t running=2;memcpy(gv_worker+0x40,&running,4);
    gv_worker[0x7198]=1;
    gv_mutex_init(gv_worker+0x7168);gv_mutex_init(gv_worker+0x7090);gv_mutex_init(AT(LX_GAME_TIME_LOCK_VA));
    /* Twelve sectors with saved IDs, one known only by its macro. */
    const void *macro_vtable=AT(LX_MACRO_VTABLE),*getter=AT(LX_MACRO_NAME_GETTER_VA);
    memcpy(gv_macro,&macro_vtable,8);memcpy(AT(LX_MACRO_VTABLE+19u*8u),&getter,8);
    uintptr_t name=(uintptr_t)gv_macro_name;uint64_t length=strlen(gv_macro_name);memcpy(gv_macro+0x20,&name,8);memcpy(gv_macro+0x28,&length,8);
    void *macro=gv_macro;
    for(unsigned i=0;i<GV_SECTORS;i++) {
        uint64_t runtime=i<12?7001+i:8000;unsigned at=gravidar_reverse?GV_SECTORS-1-i:i;
        memcpy(gv_sectors[i]+8,&runtime,8);memcpy(gv_units[i]+8,&runtime,8);
        if(i==12) memcpy(gv_sectors[i]+0x60,&macro,8);
        void *sector=gv_sectors[i];memcpy(gv_list[at],&sector,8);
        if(gz_on) {
            const uint64_t runtimes[4]={7003,9000,7001,7002};
            for(unsigned z=0;z<4;z++) memcpy(gz_zones[z]+8,&runtimes[z],8);
            for(unsigned z=0;z<4;z++) {
                unsigned char *g=gz_groups[at][z];void *zone=gz_zones[gravidar_reverse?3-z:z];memcpy(g,&zone,8);
                /* the group of the zone after this one in gz_zones, wherever this peer put it */
                const unsigned here=gravidar_reverse?3-z:z,next=(here+1)%4;
                gz_lists[at][z]=gravidar_reverse?3-next:next;
                void *list=&gz_lists[at][z],*list_end=&gz_lists[at][z]+1;memcpy(g+0x38,&list,8);memcpy(g+0x40,&list_end,8);memcpy(g+0x48,&list_end,8);
            }
            void *group_begin=gz_groups[at][0],*group_end=gz_groups[at][0]+4*0x58;
            memcpy(gv_list[at]+8,&group_begin,8);memcpy(gv_list[at]+16,&group_end,8);memcpy(gv_list[at]+24,&group_end,8);
        }
    }
    void *begin=gv_list,*end=gv_list[0]+sizeof(gv_list);memcpy(gv_worker+0x70c0,&begin,8);memcpy(gv_worker+0x70c8,&end,8);
    uint64_t priority=gravidar_reverse?GV_SECTORS-1-3:3;memcpy(gv_worker+0x70d8,&priority,8);
}
/* Two field sectors in the walk (with ENGINE_GRAVIDAR): low and high attention, each peer's load-time
 * deadline its own, as after the ungated boot frame; a pass draws from the walk's word at low attention. */
static __attribute__((aligned(16))) unsigned char gd_sectors[2][0x500];
static uint64_t gd_word_lost;
static void gd_move_native(void *self) {
    unsigned char *s=self;double now,due;int32_t attention;
    memcpy(&now,gate_tls()-0x2728,8);memcpy(&due,s+0x440,8);memcpy(&attention,s+0x1ec,4);
    if(!s[0x43a] || due>=now) return;
    if(attention>=5) due=now+1.0;
    else {
        uint64_t *word=(uint64_t*)(gate_tls()-0x2720);
        *word=*word*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;*word=(*word>>30)|(*word<<34);
        due=now+5.0+(double)(*word>>40)/16777216.0;
    }
    memcpy(s+0x440,&due,8);
}
static void gd_standin(void) {
    code_jump(LX_SECTOR_MOVE_VA,(void*)gd_move_native);
    void *move=AT(LX_SECTOR_MOVE_VA);memcpy(AT(LX_SECTOR_MOVE_SLOT_VA),&move,8);
    for(unsigned i=0;i<2;i++) {
        uint64_t runtime=7001+i;int32_t attention=i?6:2;double due=1000.0+(double)(getpid()%7)/8.0;
        memcpy(gd_sectors[i]+8,&runtime,8);memcpy(gd_sectors[i]+0x1ec,&attention,4);memcpy(gd_sectors[i]+0x440,&due,8);gd_sectors[i][0x43a]=1;
    }
}
static void gd_frame(void) {
    uint64_t natural=0x5000000000000000ull+(uint64_t)getpid(),after;
    /* The walk's time for these Moves only: the pathing stand-in owns this thread's time around its pass. */
    double game,thread_time;memcpy(&game,AT(LX_GAME_VA),8);memcpy(&thread_time,gate_tls()-0x2728,8);memcpy(gate_tls()-0x2728,&game,8);
    memcpy(gate_tls()-0x2720,&natural,8);
    void (*move)(void *);memcpy(&move,AT(LX_SECTOR_MOVE_SLOT_VA),8);
    int hooked=move!=(void (*)(void *))(void*)AT(LX_SECTOR_MOVE_VA);
    for(unsigned i=0;i<2;i++) move(gd_sectors[i]);
    memcpy(&after,gate_tls()-0x2720,8);
    memcpy(gate_tls()-0x2728,&thread_time,8);
    if(hooked && after!=natural) ++gd_word_lost;
}
/* Init during the load: enabled, and the load pass unless the ready byte says busy. */
static void gv_init(void) {
    uint32_t enabled=1;memcpy(gv_worker+0x44,&enabled,4);
    gv_lock(gv_worker+0x7168);
    if(gv_worker[0x7198]) {gv_worker[0x7198]=0;sem_post(&gv_kick);}
    gv_unlock(gv_worker+0x7168);
}
/* Runtime IDs (ENGINE_RUNTIME_IDS=1): the free list of X4's map, with a pre-load history of its own on each
 * peer (how many slots were taken, which were freed). The load allocates and frees through it; the hash of the
 * IDs it hands out is what must be equal. A native Remove behind the pinned prologue. */
static int rid_on;
static unsigned char id_map[0x200];
#define ID_BLOCK_BYTES 0xaaaac0u
static uint64_t id_hash=0xcbf29ce484222325ULL;
static uint64_t id_early[64];
static unsigned id_early_count;
static unsigned char *id_bucket(uint32_t slot,unsigned *entry) {
    const uint32_t index=slot-1u;unsigned char *base;memcpy(&base,id_map+0x60+(index>>20)*8u,8);
    const uint32_t low=index&0xfffffu;*entry=low%3u;return base+(uint64_t)(low/3u)*0x20u;
}
static uint64_t id_slot_read(uint32_t slot) {unsigned entry;unsigned char *b=id_bucket(slot,&entry);uint64_t v;memcpy(&v,b+entry*8u,8);return v;}
static void id_slot_write(uint32_t slot,uint64_t v) {unsigned entry;unsigned char *b=id_bucket(slot,&entry);memcpy(b+entry*8u,&v,8);}
static uint32_t id_field(unsigned at) {uint32_t v;memcpy(&v,id_map+at,4);return v;}
static void id_field_set(unsigned at,uint32_t v) {memcpy(id_map+at,&v,4);}
/* AssignNewID: pop the head, a used slot holds the object. */
static uint64_t id_assign(void) {
    const uint32_t slot=id_field(0x58);unsigned entry;unsigned char *b=id_bucket(slot,&entry);
    const uint64_t v=id_slot_read(slot);const uint32_t next=(uint32_t)(v>>32);
    id_field_set(0x58,next);if(!next) id_field_set(0x5c,0);
    else id_slot_write(next,id_slot_read(next)&0xffffffff00000000ull);
    id_slot_write(slot,(uint64_t)(uintptr_t)id_map);
    uint32_t used;memcpy(&used,b+0x1c,4);used|=1u<<(entry*4u+16u);memcpy(b+0x1c,&used,4);
    id_field_set(0x4c,id_field(0x4c)-1u);id_field_set(0x48,id_field(0x48)+1u);
    uint64_t inserts;memcpy(&inserts,id_map+0x40,8);++inserts;memcpy(id_map+0x40,&inserts,8);
    uint32_t generations;memcpy(&generations,b+0x18+(entry>>1)*4u,4);
    return ((uint64_t)((generations>>((entry&1u)*16u))&0xffffu)<<25)|slot;
}
/* What the load calls: the native behind the pinned prologue, so the gate's hook can sit on it. */
static uint64_t id_assign_native(void *object) {
    const uint64_t id=id_assign();
    if(object!=(void*)id_map) memcpy((unsigned char*)object+8,&id,8);
    return id;
}
static uint64_t id_assign_call(void) {return ((uint64_t (*)(void *))(void*)AT(LX_ID_ASSIGN_VA))(id_map);}
/* Remove: the generation up, the slot on the tail. */
static uint64_t id_remove_native(void *map,uint64_t id) {
    if(map!=(void*)id_map) {fprintf(stderr,"map lost\n");exit(2);}
    const uint32_t slot=(uint32_t)id&0x1ffffffu;unsigned entry;unsigned char *b=id_bucket(slot,&entry);
    uint32_t used,generations;memcpy(&used,b+0x1c,4);used&=~(0xfu<<(entry*4u+16u));memcpy(b+0x1c,&used,4);
    memcpy(&generations,b+0x18+(entry>>1)*4u,4);generations+=1u<<((entry&1u)*16u);memcpy(b+0x18+(entry>>1)*4u,&generations,4);
    const uint32_t tail=id_field(0x5c);
    id_slot_write(slot,(uint64_t)tail);
    if(tail) id_slot_write(tail,(id_slot_read(tail)&0xffffffffull)|((uint64_t)slot<<32));else id_field_set(0x58,slot);
    id_field_set(0x5c,slot);id_field_set(0x4c,id_field(0x4c)+1u);id_field_set(0x48,id_field(0x48)-1u);
    return 1;
}
static void id_standin(void) {
    code_jump(LX_MUTEX_LOCK_VA,(void*)gv_lock);code_jump(LX_MUTEX_UNLOCK_VA,(void*)gv_unlock);
    /* push r15; push r14; push r13; push r12; pop r12; pop r13; pop r14; pop r15; jmp [rip] */
    static const unsigned char code[]={0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x41,0x5c,0x41,0x5d,0x41,0x5e,0x41,0x5f,0xff,0x25,0,0,0,0};
    unsigned char *entry=AT(LX_ID_REMOVE_VA);uintptr_t page=(uintptr_t)entry&~(uintptr_t)4095;
    if(mprotect((void*)page,8192,PROT_READ|PROT_WRITE)) {perror("mprotect");exit(2);}
    memcpy(entry,code,sizeof(code));void *native=(void*)id_remove_native;memcpy(entry+sizeof(code),&native,8);
    /* push r14; push r13; push r12; push rbp; pop rbp; pop r12; pop r13; pop r14; jmp [rip] */
    static const unsigned char assign_code[]={0x41,0x56,0x41,0x55,0x41,0x54,0x55,0x48,0x89,0xfd,0x5d,0x41,0x5c,0x41,0x5d,0x41,0x5e,0xff,0x25,0,0,0,0};
    unsigned char *assign_entry=AT(LX_ID_ASSIGN_VA);uintptr_t assign_page=(uintptr_t)assign_entry&~(uintptr_t)4095;
    if(mprotect((void*)assign_page,8192,PROT_READ|PROT_WRITE)) {perror("mprotect");exit(2);}
    memcpy(assign_entry,assign_code,sizeof(assign_code));void *assign_native=(void*)id_assign_native;memcpy(assign_entry+sizeof(assign_code),&assign_native,8);
    if(mprotect((void*)assign_page,8192,PROT_READ|PROT_EXEC)) {perror("mprotect");exit(2);}
    if(mprotect((void*)page,8192,PROT_READ|PROT_EXEC)) {perror("mprotect");exit(2);}
    void *map=id_map;memcpy(AT(LX_ID_MAP_VA),&map,8);
    gv_mutex_init(id_map+0x10);
    void *block=calloc(1,ID_BLOCK_BYTES);if(!block) {perror("calloc");exit(2);}
    memcpy(id_map+0x60,&block,8);
    for(uint32_t slot=1;slot<=0x100000u;slot++) id_slot_write(slot,(uint64_t)(slot==1?0u:slot-1u)|((uint64_t)(slot<0x100000u?slot+1u:0u)<<32));
    id_field_set(0x58,1);id_field_set(0x5c,0x100000u);id_field_set(0x4c,0x100000u);
    /* The history before the load differs per process. */
    const unsigned taken=300u+(unsigned)(getpid()%97),freed=5u+(unsigned)(getpid()%11);
    uint64_t ids[400];
    for(unsigned i=0;i<taken;i++) ids[i]=id_assign();
    for(unsigned i=0;i<freed;i++) id_remove_native(id_map,ids[i*2]);
    /* Objects from before the load that die during it: their slots are this process's own. */
    for(unsigned i=0;i<8;i++) id_early[id_early_count++]=ids[taken-1-i];
}
/* The load: allocations and, between them, deaths of objects from before and from during the load. */
static void id_load(void) {
    uint64_t made[64];
    for(unsigned i=0;i<4000;i++) {
        const uint64_t id=id_assign_call();
        id_hash=(id_hash^id)*0x100000001b3ULL;
        if(i%64<8) made[i%64]=id;
        if(i%500==250 && id_early_count) ((uint64_t (*)(void *,uint64_t))(void*)AT(LX_ID_REMOVE_VA))(id_map,id_early[--id_early_count]);
        if(i%500==499) ((uint64_t (*)(void *,uint64_t))(void*)AT(LX_ID_REMOVE_VA))(id_map,made[(i/500)%8]);
    }
}
/* The inline import of each saved ID, as X4's loader: insert, then store the runtime ID. */
static void world_import(void) {
    for(unsigned i=1;i<=world_imports;i++) {
        uint64_t saved=0x100+i,runtime=7000+i;
        insert_result r=((insert_result (*)(void *,const uint64_t *))(void*)AT(LX_IMPORT_INSERT_VA))(AT(LX_IMPORT_MAP_VA),&saved);
        if(ai_wait_on && i==world_imports) aw_import(); /* this node has no runtime ID yet */
        memcpy(r.node+0x28,&runtime,8);
    }
    if(md_resume_on) md_import();
    if(gravidar_on) gv_init();
    if(rid_on) id_load();
    if(cr_on) cr_load();
    if(spawn_on) spawn_load();
    ((void (*)(void *))(void*)AT(LX_IMPORT_CLEAR_VA))(AT(LX_IMPORT_MAP_VA));
    world_done=1;
}
/* Serial pathing (ENGINE_PATH=N, N<=world imports): a stand-in PathingManager with N batched
 * ships (the imported identities), a pathing thread answering each release, and stand-ins of
 * the engine functions the gate calls, reached by jumps at their pinned addresses. The batch
 * order is reversed with ENGINE_PATH_REVERSE=1, as heap order differs between clients. */
#define PATH_SHIPS_MAX 15u
static unsigned path_ships,path_passes;
static int path_reverse;
static __attribute__((aligned(16))) unsigned char path_manager[0x400],path_batch[2][0x20],path_sentinel[2][0x20],path_node[PATH_SHIPS_MAX][0x20],
    path_agent[PATH_SHIPS_MAX][0x1000],path_object[PATH_SHIPS_MAX][0x400],path_controller[PATH_SHIPS_MAX][0x100];
static sem_t path_go,path_done;
static volatile uint32_t path_done_count;
static unsigned char *(*gate_tls)(void);
#define PATH_TLS_TIME (-0x2728)
#define PATH_TLS_PREVIOUS (-0x2628)
static void path_count_store(void) {uint32_t c=__atomic_load_n(&path_done_count,__ATOMIC_SEQ_CST);memcpy(path_manager+0x10+0x58,&c,4);}
static void path_sem_release(void *semaphore,unsigned count) {
    if(semaphore==(void*)(path_manager+0x70)) {for(unsigned i=0;i<count;i++) sem_post(&path_go);return;}
    if(semaphore!=(void*)(path_manager+0x10)) {fprintf(stderr,"unknown semaphore\n");exit(2);}
    __atomic_add_fetch(&path_done_count,count,__ATOMIC_SEQ_CST);path_count_store();
    for(unsigned i=0;i<count;i++) sem_post(&path_done);
}
static int path_sem_timed_wait(void *semaphore,unsigned ms) {
    if(semaphore!=(void*)(path_manager+0x10)) {fprintf(stderr,"unknown semaphore\n");exit(2);}
    struct timespec until;clock_gettime(CLOCK_REALTIME,&until);until.tv_sec+=ms/1000;
    if(sem_timedwait(&path_done,&until)) return 0;
    __atomic_sub_fetch(&path_done_count,1,__ATOMIC_SEQ_CST);path_count_store();
    return 1;
}
/* The travel decision (ENGINE_TRAVEL=1, with ENGINE_PATH): the batches run through the job the pass's LEAs
 * name. Each agent first reads its leader's flag through the pinned instruction, then sets its own, so a
 * wingman updated after its leader sees this pass's value unless it reads the snapshot. ship 0 leads. */
static int travel_on;
static uint64_t travel_results[PATH_SHIPS_MAX];
static unsigned travel_pass;
static uint64_t travel_jobs;
typedef unsigned char (*travel_read_fn)(const void *leader);
static void travel_job_native(void **params) {
    unsigned char *batch=params[0],*sentinel,*node;
    memcpy(&sentinel,batch+0x10,8);memcpy(&node,sentinel+8,8);
    uint64_t word;memcpy(&word,AT(LX_TRAVEL_READ_WORD_VA),8);
    const int moved=((word>>40)&0xff)==0xa8;
    for(;node!=sentinel;memcpy(&node,node+8,8)) {
        unsigned char *agent;memcpy(&agent,node+0x10,8);
        const unsigned ship=(unsigned)((agent-path_agent[0])/0x1000);
        unsigned char seen=((travel_read_fn)(void*)AT(LX_TRAVEL_READ_WORD_VA))(path_agent[0]);
        if(moved) travel_results[ship]=travel_results[ship]*31+seen;
        agent[0xf9b]=(unsigned char)((travel_pass>>ship)&1);
    }
    ++travel_jobs;
}
static void travel_pass_run(void) {
    ++travel_pass;
    static const unsigned sites[2]={LX_PATH_JOB_LEA_A_VA+LX_PATH_JOB_LEA_A_DISP,LX_PATH_JOB_LEA_B_VA+LX_PATH_JOB_LEA_B_DISP};
    for(unsigned v=0;v<2;v++) {
        int32_t rel;memcpy(&rel,AT(sites[v]),4);
        void (*job)(void *,void *)=(void (*)(void *,void *))(void*)(AT(sites[v])+4+rel);
        void *params[8]={path_batch[v],0,0,0,0,0,0,0};
        job(params,NULL);
    }
}
static void travel_standin(void) {
    code_jump(LX_PATH_JOB_VA,(void*)travel_job_native);
    /* lea r12,[rip+job] twice, as the pass; the read: mov rdx,rdi; cmp byte [rdx+0xf9b],0; setne al; ret */
    unsigned char *lea_a=AT(LX_PATH_JOB_LEA_A_VA+LX_PATH_JOB_LEA_A_DISP-3),*lea_b=AT(LX_PATH_JOB_LEA_B_VA+LX_PATH_JOB_LEA_B_DISP-3),*read=AT(LX_TRAVEL_READ_WORD_VA);
    uintptr_t page=(uintptr_t)lea_a&~(uintptr_t)4095;
    if(mprotect((void*)page,8192,PROT_READ|PROT_WRITE)) {perror("mprotect");exit(2);}
    memcpy(lea_a,"\x4c\x8d\x25",3);int32_t rel=(int32_t)(AT(LX_PATH_JOB_VA)-(lea_a+7));memcpy(lea_a+3,&rel,4);
    memcpy(lea_b,"\x4c\x8d\x25",3);rel=(int32_t)(AT(LX_PATH_JOB_VA)-(lea_b+7));memcpy(lea_b+3,&rel,4);
    if(mprotect((void*)page,8192,PROT_READ|PROT_EXEC)) {perror("mprotect");exit(2);}
    page=(uintptr_t)read&~(uintptr_t)4095;
    if(mprotect((void*)page,8192,PROT_READ|PROT_WRITE)) {perror("mprotect");exit(2);}
    memcpy(read,"\x48\x89\xfa\x80\xba\x9b\x0f\x00\x00\x00\x0f\x95\xc0\xc3",14);
    if(mprotect((void*)page,8192,PROT_READ|PROT_EXEC)) {perror("mprotect");exit(2);}
    /* The manager's unassigned list: empty, a sentinel of its own. */
    static unsigned char fresh[0x20];void *self=fresh;memcpy(fresh+8,&self,8);memcpy(path_manager+0x2a8,&self,8);
}
static void *path_thread(void *unused) {
    (void)unused;
    for(;;) {
        sem_wait(&path_go);
        ++path_passes;
        if(travel_on) travel_pass_run();
        struct timespec work={0,(long)(rand()%3000000)};nanosleep(&work,NULL); /* a pass of no fixed length */
        path_sem_release(path_manager+0x10,1);
    }
    return NULL;
}
static void path_sync(void) {
    unsigned char *tls=gate_tls();double game;
    memcpy(tls+PATH_TLS_PREVIOUS,tls+PATH_TLS_TIME,8);memcpy(&game,AT(LX_GAME_VA),8);memcpy(tls+PATH_TLS_TIME,&game,8);
}
static void path_refresh(unsigned char *controller) {
    uint32_t bank,cursor;double now,last;unsigned char *tls=gate_tls();
    memcpy(&bank,AT(LX_BANK_READ_VA),4);memcpy(&cursor,controller+0xc8,4);memcpy(&now,tls+PATH_TLS_TIME,8);memcpy(&last,controller+0xc0,8);
    if(now<=last || cursor==bank) return;
    memcpy(controller+0xc0,&now,8);memcpy(controller+0xc8,&bank,4);
}
static void code_jump(uintptr_t va,const void *target) {
    unsigned char *at=AT(va);void *page=(void*)((uintptr_t)at&~(uintptr_t)4095);
    if(mprotect(page,8192,PROT_READ|PROT_WRITE)) {perror("mprotect");exit(2);} /* another stand-in may share the page */
    at[0]=0xff;at[1]=0x25;memset(at+2,0,4);memcpy(at+6,&target,8);
    if(mprotect(page,8192,PROT_READ|PROT_EXEC)) {perror("mprotect");exit(2);}
}
static void path_standin(void) {
    const uint64_t word=LX_PATH_RELEASE_ORIGINAL;
    uintptr_t page=(uintptr_t)AT(LX_PATH_RELEASE_SITE)&~(uintptr_t)4095;
    memcpy((void*)((uintptr_t)AT(LX_PATH_RELEASE_SITE)&~(uintptr_t)7),&word,8); /* add rdi,0x70 (its last 3 bytes); call the release */
    const unsigned char after[]={0x48,0x83,0xc4,0x08,0xc3};                     /* add rsp,8; ret */
    memcpy(AT(LX_PATH_RELEASE_SITE)+5,after,sizeof(after));
    /* The entry: sub rsp,8 (a called function's alignment for the call below); jmp to the site. */
    unsigned char *entry=AT(LX_PATH_RELEASE_SITE-0x20);
    const unsigned char head[]={0x48,0x83,0xec,0x08,0xe9};memcpy(entry,head,sizeof(head));
    int32_t rel=(int32_t)(AT(LX_PATH_RELEASE_SITE)-(entry+sizeof(head)+4));memcpy(entry+sizeof(head),&rel,4);
    if(mprotect((void*)page,4096,PROT_READ|PROT_EXEC)) {perror("mprotect");exit(2);}
    code_jump(LX_SEM_RELEASE_VA,(void*)path_sem_release);code_jump(LX_SEM_TIMED_WAIT_VA,(void*)path_sem_timed_wait);
    code_jump(LX_PATH_SYNC_VA,(void*)path_sync);code_jump(LX_PATH_REFRESH_VA,(void*)path_refresh);
    /* The manager: state running, budget native, the ships in two batch vectors. */
    void *manager=path_manager;memcpy(AT(LX_PATH_MANAGER_VA),&manager,8);
    int32_t state=2;memcpy(path_manager+0x194,&state,4);
    double budget=0.001;memcpy(path_manager+LX_PATH_BUDGET,&budget,8);
    const void *nav_vtable=AT(0x3b697e8);
    for(unsigned i=0;i<path_ships;i++) {
        unsigned char *a=path_agent[i],*o=path_object[i],*c=path_controller[i],*slots=a+0xaf0;
        uint64_t runtime=7000+i+1;
        memcpy(a+0xf10,&o,8);memcpy(o+8,&runtime,8);memcpy(o+0x200,&c,8);
        memcpy(c,&nav_vtable,8);memcpy(c+0x20,&o,8);memcpy(c+0x38,&a,8);memcpy(c+0x28,&slots,8);
        uint32_t never=2;memcpy(c+0xc8,&never,4);
    }
    const unsigned first=path_ships/2;
    for(unsigned v=0;v<2;v++) {
        unsigned char *begin=path_batch[v],*end=begin+0x20,*sentinel=path_sentinel[v],*previous=sentinel;
        memcpy(path_manager+0x2b0+v*0x18,&begin,8);memcpy(path_manager+0x2b8+v*0x18,&end,8);
        const unsigned low=v?first:0,high=v?path_ships:first;uint64_t members=high-low;
        memcpy(path_batch[v]+8,&members,8);memcpy(path_batch[v]+0x10,&sentinel,8);
        for(unsigned k=low;k<high;k++) {
            const unsigned ship=path_reverse?path_ships-1-k:k;
            unsigned char *node=path_node[ship],*agent=path_agent[ship];
            memcpy(previous+8,&node,8);memcpy(node+0x10,&agent,8);previous=node;
        }
        memcpy(previous+8,&sentinel,8);
    }
}
/* MoveWorker's frame start: its release of the pass through the pinned call, then its time. */
static void path_frame_begin(void) {
    path_manager[0x198]=0;
    ((void (*)(void *,unsigned))(void*)AT(LX_PATH_RELEASE_SITE-0x20))(path_manager+0x70,1);
    path_sync();
}
/* WaitAndSwapBuffers: the engine's own late join, then the bank flip. */
static void path_frame_end(void) {
    sem_wait(&path_done);__atomic_sub_fetch(&path_done_count,1,__ATOMIC_SEQ_CST);path_count_store();
    path_manager[0x198]=1;
    uint32_t read,write;memcpy(&read,AT(LX_BANK_READ_VA),4);memcpy(&write,AT(LX_BANK_WRITE_VA),4);
    read=1-read;write=1-write;memcpy(AT(LX_BANK_READ_VA),&read,4);memcpy(AT(LX_BANK_WRITE_VA),&write,4);
}
/* A hazard region (ENGINE_REGION=1): one stand-in Region::Move in the vtable slot, one damage
 * field. A pass draws its next due time from the thread word, which holds a different natural
 * value on each peer, as an earlier consumer of the stream can leave it. */
static int region_on;
static unsigned region_word_lost;
static __attribute__((aligned(16))) unsigned char region_object[0x400],region_field[0x1200];
static void *region_fields[1];
static void region_standin_move(void *self) {
    if(self!=(void*)region_object) {fprintf(stderr,"region argument lost\n");exit(2);}
    uint64_t *word=(uint64_t*)(gate_tls()-0x2720);
    double now,due;memcpy(&now,gate_tls()-0x2728,8);memcpy(&due,region_field+0x10f8,8);
    if(now<=due) return;
    *word=*word*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;*word=(*word>>30)|(*word<<34);
    due=now+1.0/6.0+(double)(*word>>40)/16777216.0*0.1;
    memcpy(region_field+0x10f0,&now,8);memcpy(region_field+0x10f8,&due,8);
}
static void region_standin(void) {
    code_jump(LX_REGION_MOVE_VA,(void*)region_standin_move);
    code_jump(LX_DAMAGE_FIELD_UPDATE_VA,(void*)region_standin_move);
    void *move=AT(LX_REGION_MOVE_VA),*update=AT(LX_DAMAGE_FIELD_UPDATE_VA);
    memcpy(AT(LX_REGION_SLOT_VA),&move,8);memcpy(AT(LX_DAMAGE_FIELD_VTABLE+0x48u),&update,8);
    const void *region_vtable=AT(LX_REGION_VTABLE),*field_vtable=AT(LX_DAMAGE_FIELD_VTABLE);
    memcpy(region_object,&region_vtable,8);memcpy(region_field,&field_vtable,8);
    uint64_t runtime=7001;memcpy(region_object+8,&runtime,8);
    region_fields[0]=region_field;
    void *begin=region_fields,*end=region_fields+1;memcpy(region_object+0x280,&begin,8);memcpy(region_object+0x288,&end,8);
}
/* The sim walk's dispatch through the slot, with this peer's natural word. */
static void region_frame(void) {
    uint64_t natural=0x1000000000000000ull+(uint64_t)getpid();
    double game;memcpy(&game,AT(LX_GAME_VA),8);memcpy(gate_tls()-0x2728,&game,8);
    memcpy(gate_tls()-0x2720,&natural,8);
    void (*move)(void *);memcpy(&move,AT(LX_REGION_SLOT_VA),8);move(region_object);
    uint64_t after;memcpy(&after,gate_tls()-0x2720,8);
    if(move!=(void (*)(void *))(void*)AT(LX_REGION_MOVE_VA) && after!=natural) ++region_word_lost; /* the hook's scope gives the word back */
}
static double getd(uintptr_t va) {double v;memcpy(&v,AT(va),8);return v;}
static void putd(uintptr_t va,double v) {memcpy(AT(va),&v,8);}
static void hex32(const unsigned char *raw,char out[65]) {
    for(unsigned i=0;i<32;i++) snprintf(out+2*i,3,"%02x",raw[i]);
}
/* The fake image: the ELF header and segment rows, the FrameTick call, the dt
 * qwords and 1/60 as in the executable; pins recomputed from these bytes. */
static void fake_image(void) {
    fake=mmap(NULL,FAKE_END-LX_IMAGE_BASE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);
    if(fake==MAP_FAILED) {perror("mmap");exit(2);}
    uint16_t h16;uint32_t h32;uint64_t h64;
    memcpy(fake,"\177ELF\2\1\1",7);h16=2;memcpy(fake+16,&h16,2);h16=62;memcpy(fake+18,&h16,2);
    h64=64;memcpy(fake+32,&h64,8);h16=56;memcpy(fake+54,&h16,2);h16=12;memcpy(fake+56,&h16,2);
    unsigned char *text=fake+64+56*3,*tls=fake+64+56*8;
    h32=1;memcpy(text,&h32,4);h32=5;memcpy(text+4,&h32,4);h64=LX_TEXT_VA;memcpy(text+16,&h64,8);
    h32=7;memcpy(tls,&h32,4);h64=LX_TLS_VADDR;memcpy(tls+16,&h64,8);h64=LX_TLS_MEMSZ;memcpy(tls+40,&h64,8);h64=LX_TLS_ALIGN;memcpy(tls+48,&h64,8);
    *AT(LX_CALL_VA)=0xe8;h32=(uint32_t)(LX_TICK_VA-(LX_CALL_VA+5));memcpy(AT(LX_CALL_VA)+1,&h32,4);
    h64=LX_DT_LOAD_ORIGINAL;memcpy(AT(LX_DT_LOAD_VA),&h64,8);h64=LX_DT_MIN_ORIGINAL;memcpy(AT(LX_DT_MIN_VA),&h64,8);
    putd(LX_ONE_SIXTIETH_VA,1.0/60.0);
    uint32_t bank=0;memcpy(AT(LX_BANK_READ_VA),&bank,4);bank=1;memcpy(AT(LX_BANK_WRITE_VA),&bank,4);
    if(world_imports) world_standin();
    if(path_ships) path_standin();
    if(travel_on) travel_standin();
    if(region_on) region_standin();
    if(ai_wait_on) aw_standin();
    if(md_resume_on || script_on) md_standin();
    if(script_on) sr_standin();
    if(find_on) find_standin();
    if(ll_on) ll_standin();
    if(at_on) at_standin();
    if(st_on) st_standin();
    if(spawn_on) spawn_standin();
    if(cr_on) cr_standin();
    if(nav_on) nv_standin();
    if(boot_hold_on) boot_standin();
    if(event_order_on) event_standin();
    if(gravidar_on) {gv_standin();gd_standin();}
    if(rid_on) id_standin();
    for(unsigned i=0;i<sizeof(lx_sites)/sizeof(*lx_sites);i++) {
        unsigned char raw[32];sites[i]=lx_sites[i];
        lockstep_sha256(AT(lx_sites[i].va),lx_sites[i].length,raw);hex32(raw,hashes[i]);sites[i].sha256=hashes[i];
    }
    void *t=table;memcpy(AT(LX_VBI_TABLE_VA),&t,8);
    putd(LX_SPEED_VA,1.0);putd(LX_GAME_VA,game0);putd(LX_REAL_VA,game0+10.0);
    if(gravidar_on) putd(LX_GAME_TIME_GLOBAL_VA,game0);
    *AT(LX_IN_FLIGHT_VA)=1;
    memcpy(window_original,AT(LX_CALL_WINDOW),16);
}
static int dt_fixed(void) {
    uint64_t min;memcpy(&min,AT(LX_DT_MIN_VA),8);return min==LX_DT_MIN_PATCHED;
}
/* The double the patched dt load reads (its displacement, as the executable would). */
static double dt_patched(void) {
    int32_t d;double v;memcpy(&d,AT(LX_DT_LOAD_VA)+4,4);memcpy(&v,AT(LX_DT_LOAD_VA)+8+d,8);return v;
}
/* FrameTick stand-in: the timer advances by dt (1/hz under the gate's fixed-dt
 * patch, else a variable clock dt), then semSimStart starts the MoveWorker frame. */
static void fake_frame_tick(void *frame,unsigned char loading) {
    if(frame!=(void*)0x5eed) {fprintf(stderr,"frame argument lost\n");exit(2);}
    if(loading) return;
    double dt=dt_fixed()?dt_patched():0.016+0.001*(main_frames%3);
    putd(LX_GAME_VA,getd(LX_GAME_VA)+dt*getd(LX_SPEED_VA));putd(LX_REAL_VA,getd(LX_REAL_VA)+dt);
    if(gravidar_on) putd(LX_GAME_TIME_GLOBAL_VA,getd(LX_GAME_VA));
    sem_post(&sim_start);
}
/* What the X4 main loop calls: the target of the call at LX_CALL_VA. */
static void call_frame(int loading) {
    int32_t rel;memcpy(&rel,AT(LX_CALL_VA)+1,4);
    unsigned char *target=AT(LX_CALL_VA)+5+rel;
    if(target==AT(LX_TICK_VA)) fake_frame_tick((void*)0x5eed,(unsigned char)loading);
    else ((tick_fn)(void*)target)((void*)0x5eed,(unsigned char)loading);
    ++main_frames;
}
/* ffi.C functions the probe calls in a snapshot/clock run. */
EXPORT double GetCurrentGameTime(void) {return getd(LX_GAME_VA);}
EXPORT const char *GetSaveFolderPath(void) {return "/engine-test/45841/save";}
EXPORT bool IsStartmenu(void) {return !loaded;}
EXPORT bool IsSaveListLoadingComplete(void) {return true;}
EXPORT void ReloadSaveList(void) {}
EXPORT void engine_load_game(const char *name) {(void)name;load_pending=1;}

static lua_State *ui;
static int run_file(lua_State *L,const char *path) {
    if(lua_loadfile_(L,path) || lua_pcall_(L,0,0,0)) {
        printf("ENGINE_LUA_ERROR %s: %s\n",path,lua_tolstring_(L,-1,NULL));lua_settop_(L,0);return 0;
    }
    return 1;
}
/* A fresh UI: X4 stand-ins, the probe configuration, the input core, the probe. */
static int ui_build(void) {
    ui=lua_new();lua_libs(ui);
    return run_file(ui,prelude) && run_file(ui,config) && (!inputs_core || run_file(ui,inputs_core)) && run_file(ui,probe);
}
static void *ui_main(void *unused) {
    (void)unused;
    if(!ui_build()) {done=2;sem_post(&sim_done);return NULL;}
    unsigned after_stop=0;
    while(!done) {
        if(boot_hold_on && !sem_trywait(&hold_go)) {boot_move_call();sem_post(&hold_done);continue;}
        struct timespec until;clock_gettime(CLOCK_REALTIME,&until);until.tv_nsec+=50000000;
        if(until.tv_nsec>=1000000000) {until.tv_sec++;until.tv_nsec-=1000000000;}
        if(sem_timedwait(&sim_start,&until)) continue;
        int32_t worker=1;memcpy(gate_tls()-0x2620,&worker,4); /* MoveWorker's view selector */
        const int pathing=path_ships && loaded;
        if(gravidar_on) ((void (*)(void *))(void*)(AT(LX_GRAVIDAR_CALL_VA)-9))(gv_worker); /* the MoveWorker's publish, before admission */
        if(gravidar_on && loaded) gd_frame();
        if(region_on && loaded) region_frame();
        if(boot_hold_on && loaded) boot_move_call();
        if(event_order_on && loaded) event_frame();
        if(script_on && loaded) {gate_stats live;if(gate_poll(&live) && live.active) sr_frame(live.issued);}
        if(find_on && loaded) {gate_stats live;if(gate_poll(&live) && live.active) find_frame(live.issued);}
        if(ll_on && loaded) {gate_stats live;if(gate_poll(&live) && live.active) ll_frame(live.issued);}
        if(at_on && loaded) {gate_stats live;if(gate_poll(&live) && live.active) at_frame(live.issued);}
        if(st_on && loaded) {gate_stats live;if(gate_poll(&live) && live.active) st_frame(live.issued);}
        if(cr_on && loaded) {gate_stats live;if(gate_poll(&live) && live.active) cr_frame(live.issued);}
        if(nav_on && loaded) {gate_stats live;if(gate_poll(&live) && live.active) nv_frame(live.issued);}
        if(gz_on && loaded) {gate_stats live;gz_gated=gate_poll(&live) && live.active && live.issued>=1 && live.issued<=80;}
        if(pathing) path_frame_begin();
        if(reload_pending) {lua_close_(ui);reload_pending=0;++reloads;if(!ui_build()) {done=2;sem_post(&sim_done);break;}}
        lua_getfield_(ui,-10002,"ENGINE_FRAME"); /* LUA_GLOBALSINDEX */
        if(lua_pcall_(ui,0,0,0)) {printf("ENGINE_LUA_ERROR frame: %s\n",lua_tolstring_(ui,-1,NULL));lua_settop_(ui,0);}
        ++ui_frames;
        if(pathing) path_frame_end();
        gate_stats s;
        if(gate_poll(&s) && !s.active && (s.issued || s.reason)) {
            if(!after_stop) printf("ENGINE_STOP reason=%u completed=%llu ui_frame=%u\n",s.reason,(unsigned long long)s.completed,ui_frames);
            if(++after_stop>=3) done=1;
        }
        if(load_pending) {
            /* X4 tears the UI down for the load and runs FrameTick with loading set. */
            load_pending=0;reload_pending=1;loading_frames=(int)loading_total;
            int32_t one=1;memcpy(table+LX_VBI_LOADING,&one,4);
        }
        sem_post(&sim_done);
    }
    return NULL;
}
static void *resolve(void *module,const char *name) {
    void *p=dlsym(module,name);if(!p) {fprintf(stderr,"missing %s\n",name);exit(2);}
    return p;
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IOLBF,0);
    if(argc<4) {fprintf(stderr,"usage: test_gate_engine PRELUDE CONFIG PROBE [INPUTS_CORE]\n");return 2;}
    prelude=argv[1];config=argv[2];probe=argv[3];inputs_core=argc>4?argv[4]:NULL;
    if(getenv("ENGINE_GAME0")) game0=atof(getenv("ENGINE_GAME0"));
    if(getenv("ENGINE_LOADING")) loading_total=(unsigned)atoi(getenv("ENGINE_LOADING"));
    if(getenv("ENGINE_WORLD")) world_imports=(unsigned)atoi(getenv("ENGINE_WORLD"));
    if(world_imports>15) {fprintf(stderr,"ENGINE_WORLD is at most 15\n");return 2;}
    if(getenv("ENGINE_PATH")) path_ships=(unsigned)atoi(getenv("ENGINE_PATH"));
    path_reverse=getenv("ENGINE_PATH_REVERSE")!=NULL;
    region_on=getenv("ENGINE_REGION")!=NULL;
    ai_wait_on=getenv("ENGINE_AI_WAIT")!=NULL;
    md_resume_on=getenv("ENGINE_MD_RESUME")!=NULL;
    script_on=getenv("ENGINE_SCRIPT_RNG")!=NULL;
    find_on=getenv("ENGINE_FIND_ORDER")!=NULL;
    ll_on=getenv("ENGINE_LOADOUT")!=NULL;
    at_on=getenv("ENGINE_ATTENTION")!=NULL;
    st_on=getenv("ENGINE_STEP_TRACE")!=NULL;
    at_peer2=getenv("ENGINE_ATTENTION_PEER2")!=NULL;
    ll_reverse=getenv("ENGINE_LOADOUT_REVERSE")!=NULL;
    spawn_on=getenv("ENGINE_SPAWN")!=NULL;
    cr_on=getenv("ENGINE_CREATED")!=NULL;
    nav_on=getenv("ENGINE_NAV")!=NULL;
    nv_word2=getenv("ENGINE_NAV_WORD2")!=NULL;
    gz_on=getenv("ENGINE_GRAVIDAR_ZONES")!=NULL;
    find_reverse=getenv("ENGINE_FIND_ORDER_REVERSE")!=NULL;
    script_reverse=getenv("ENGINE_SCRIPT_RNG_REVERSE")!=NULL;
    boot_hold_on=getenv("ENGINE_BOOT_HOLD")!=NULL;
    event_order_on=getenv("ENGINE_EVENT_ORDER")!=NULL;
    event_reverse=getenv("ENGINE_EVENT_ORDER_REVERSE")!=NULL;
    gravidar_on=getenv("ENGINE_GRAVIDAR")!=NULL;
    travel_on=getenv("ENGINE_TRAVEL")!=NULL;
    rid_on=getenv("ENGINE_RUNTIME_IDS")!=NULL;
    gravidar_reverse=getenv("ENGINE_GRAVIDAR_REVERSE")!=NULL;
    if(path_ships>world_imports) {fprintf(stderr,"ENGINE_PATH is at most ENGINE_WORLD\n");return 2;}
    srand((unsigned)getpid());
    unsigned seconds=getenv("ENGINE_SECONDS")?(unsigned)atoi(getenv("ENGINE_SECONDS")):60;
    void *lua=dlopen("libluajit-5.1.so.2",RTLD_NOW|RTLD_GLOBAL);if(!lua) {fprintf(stderr,"%s\n",dlerror());return 2;}
    lua_new=resolve(lua,"luaL_newstate");lua_libs=resolve(lua,"luaL_openlibs");lua_loadfile_=resolve(lua,"luaL_loadfile");
    lua_pcall_=resolve(lua,"lua_pcall");lua_close_=resolve(lua,"lua_close");lua_tolstring_=resolve(lua,"lua_tolstring");
    lua_settop_=resolve(lua,"lua_settop");lua_getfield_=resolve(lua,"lua_getfield");
    /* The path the probe loads, so package.loadlib returns this same module. */
    void *gate=dlopen("./extensions/x4_lan_lab/native/x4lan_gate.so",RTLD_NOW|RTLD_GLOBAL);if(!gate) {fprintf(stderr,"%s\n",dlerror());return 2;}
    gate_setup=resolve(gate,"x4gate_test_setup");gate_tls=resolve(gate,"x4gate_test_tls");gate_poll=resolve(gate,"x4gate_poll");
    fake_image();
    if(!gate_setup(fake,NULL,fake_frame_tick,sites,sizeof(sites)/sizeof(*sites),2000)) {fprintf(stderr,"setup refused\n");return 2;}
    gate_world=resolve(gate,"x4gate_test_world");
    if(!gate_world((void*)engine_find,&world_map_slot)) {fprintf(stderr,"world setup refused\n");return 2;}
    sem_init(&sim_start,0,0);sem_init(&sim_done,0,1);
    sem_init(&hold_go,0,0);sem_init(&hold_done,0,0);
    if(gravidar_on) {
        sem_init(&gv_kick,0,0);
        pthread_t scanner;pthread_create(&scanner,NULL,gv_thread,NULL);pthread_detach(scanner);
    }
    if(path_ships) {
        sem_init(&path_go,0,0);sem_init(&path_done,0,0);
        pthread_t pathing;pthread_create(&pathing,NULL,path_thread,NULL);pthread_detach(pathing);
    }
    pthread_t thread;pthread_create(&thread,NULL,ui_main,NULL);
    struct timespec started;clock_gettime(CLOCK_MONOTONIC,&started);
    int in_flight=1,timed_out=0;
    while(!done) {
        struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
        if(now.tv_sec-started.tv_sec>(time_t)seconds) {timed_out=1;done=3;break;}
        if(in_flight) {
            struct timespec until;clock_gettime(CLOCK_REALTIME,&until);until.tv_nsec+=16000000;
            if(until.tv_nsec>=1000000000) {until.tv_sec++;until.tv_nsec-=1000000000;}
            if(sem_timedwait(&sim_done,&until)) continue;
        }
        if(done) break;
        *AT(LX_IN_FLIGHT_VA)=0;in_flight=0;
        int loading=loading_frames>0;
        call_frame(loading);
        if(loading && world_imports && !world_done) world_import(); /* after the first loading frame: boot phase 2 */
        if(loading) {
            if(!--loading_frames) {
                int32_t zero=0;memcpy(table+LX_VBI_LOADING,&zero,4);loaded=1;
                if(boot_hold_on) { /* the worker's last update of the load, still in boot phase 2 */
                    boot_in_phase2=1;sem_post(&hold_go);
                    struct timespec limit;clock_gettime(CLOCK_REALTIME,&limit);limit.tv_sec+=5;
                    if(sem_timedwait(&hold_done,&limit)) {fprintf(stderr,"the UI thread missed the post-load Move\n");return 2;}
                    boot_in_phase2=0;
                }
            }
        } else {*AT(LX_IN_FLIGHT_VA)=1;in_flight=1;}
    }
    done=done?done:1;pthread_join(thread,NULL);
    /* The probe's STOP line carries reason/restored/completed; here the bytes themselves. */
    uint64_t min;memcpy(&min,AT(LX_DT_MIN_VA),8);
    int window=!memcmp(window_original,AT(LX_CALL_WINDOW),16);
    printf("ENGINE_RESULT status=%s ui_frames=%u main_frames=%u reloads=%u window_restored=%d dt_original=%d game=%.17g\n",
        timed_out?"timeout":done==2?"lua_error":"stopped",ui_frames,main_frames,reloads,window,min==LX_DT_MIN_ORIGINAL,getd(LX_GAME_VA));
    if(rid_on) {
        uint64_t head,assign_head;memcpy(&head,AT(LX_ID_REMOVE_VA),8);memcpy(&assign_head,AT(LX_ID_ASSIGN_VA),8);
        printf("ENGINE_RUNTIME_IDS entry_restored=%d left=%u hash=%016llx assign_restored=%d\n",head==LX_ID_REMOVE_ENTRY_ORIGINAL,id_early_count,(unsigned long long)id_hash,assign_head==LX_ID_ASSIGN_ENTRY_ORIGINAL);
    }
    if(travel_on) {
        uint64_t word,results=0xcbf29ce484222325ULL;memcpy(&word,AT(LX_TRAVEL_READ_WORD_VA),8);
        int32_t rel_a,rel_b;memcpy(&rel_a,AT(LX_PATH_JOB_LEA_A_VA+LX_PATH_JOB_LEA_A_DISP),4);memcpy(&rel_b,AT(LX_PATH_JOB_LEA_B_VA+LX_PATH_JOB_LEA_B_DISP),4);
        for(unsigned i=0;i<path_ships;i++) results=(results^travel_results[i])*0x100000001b3ULL;
        printf("ENGINE_TRAVEL read_restored=%d job_restored=%d jobs=%llu results=%016llx\n",((word>>40)&0xff)==0x9b,
            AT(LX_PATH_JOB_LEA_A_VA+LX_PATH_JOB_LEA_A_DISP)+4+rel_a==AT(LX_PATH_JOB_VA) && AT(LX_PATH_JOB_LEA_B_VA+LX_PATH_JOB_LEA_B_DISP)+4+rel_b==AT(LX_PATH_JOB_VA),
            (unsigned long long)travel_jobs,(unsigned long long)results);
    }
    if(gravidar_on) {
        gv_stop=1;
        void *slot;int32_t rel;memcpy(&slot,AT(LX_GRAVIDAR_UNIT_SLOT_VA),8);memcpy(&rel,AT(LX_GRAVIDAR_CALL_VA)+1,4);
        printf("ENGINE_GRAVIDAR call_restored=%d slot_restored=%d passes=%llu hash=%016llx\n",AT(LX_GRAVIDAR_CALL_VA)+5+rel==AT(LX_GRAVIDAR_PUBLISH_VA),
            slot==(void*)AT(LX_GRAVIDAR_UNIT_APPLY_VA),(unsigned long long)gv_passes,(unsigned long long)gv_hash);
    }
    if(gravidar_on) {
        void *slot;memcpy(&slot,AT(LX_SECTOR_MOVE_SLOT_VA),8);
        printf("ENGINE_SECTOR slot_restored=%d word_lost=%llu\n",slot==(void*)AT(LX_SECTOR_MOVE_VA),(unsigned long long)gd_word_lost);
    }
    if(event_order_on) {
        uint64_t word;memcpy(&word,AT(LX_EVENT_CALL_WORD_VA),8);
        printf("ENGINE_EVENT_ORDER word_restored=%d delivered=%u sequence=%016llx native=%016llx\n",word==LX_EVENT_CALL_WORD_ORIGINAL,event_delivered,
            (unsigned long long)event_hash,(unsigned long long)event_native_hash);
    }
    if(nav_on) {
        void *a,*b;memcpy(&a,AT(LX_NAV_MOVE_SLOT_A_VA),8);memcpy(&b,AT(LX_NAV_MOVE_SLOT_B_VA),8);
        printf("ENGINE_NAV slots_restored=%d sites_restored=%d refreshes=%llu hash=%016llx\n",a==(void*)AT(LX_NAV_MOVE_VA) && b==(void*)AT(LX_NAV_MOVE_VA),
            !memcmp(AT(LX_NAV_PARAMS_NEAR_VA),"\x77\x2e",2) && !memcmp(AT(LX_NAV_PARAMS_FAR_VA),"\x0f\x86\x71\x08\0\0",6) && !memcmp(AT(LX_NAV_PROBE_NEAR_VA),"\x77\x2e",2) && !memcmp(AT(LX_NAV_PROBE_FAR_VA),"\x0f\x86\x48\x08\0\0",6),
            (unsigned long long)nv_refreshes,(unsigned long long)nv_hash);
    }
    if(gz_on) printf("ENGINE_GRAVIDAR_ZONES lost=%u hash=%016llx\n",gz_lost,(unsigned long long)gz_hash);
    if(spawn_on) {
        uint64_t head;memcpy(&head,AT(LX_GENERATE_VA),8);
        printf("ENGINE_SPAWN entry_restored=%d lost=%u calls=%llu hash=%016llx\n",head==LX_GENERATE_ENTRY_ORIGINAL,spawn_lost,(unsigned long long)spawn_calls,(unsigned long long)spawn_hash);
    }
    if(st_on) {
        void *entry;memcpy(&entry,AT(LX_ST_SLOT_VA),8);
        printf("ENGINE_STEP_TRACE entry_restored=%d calls=%llu\n",entry==(void*)AT(LX_ST_STEP_VA),(unsigned long long)st_engine_calls);
    }
    if(at_on) {
        uint64_t head;memcpy(&head,AT(LX_ATTENTION_VA),8);
        printf("ENGINE_ATTENTION entry_restored=%d evaluations=%llu hash=%016llx\n",head==LX_ATTENTION_ENTRY_ORIGINAL,(unsigned long long)at_evaluations,(unsigned long long)at_hash);
    }
    if(ll_on) {
        const uintptr_t sites[6]={LX_LL_WARES_SITE_VA,LX_LL_AMMO_SITE_VA,LX_LL_DEPLOYABLE_SITE_VA,LX_LL_UNIT_SITE_VA,LX_LL_EXISTING_SITE_VA,LX_LL_PICK_SITE_VA};
        const uintptr_t targets[6]={LX_LL_WARES_VA,LX_LL_AMMO_VA,LX_LL_DEPLOYABLE_VA,LX_LL_UNIT_VA,LX_LL_EXISTING_VA,LX_LL_PICK_VA};
        int restored=*AT(LX_LL_REST_UNIT_VA)==0x4c && *AT(LX_LL_REST_DEPLOYABLE_VA)==0x4c;
        for(unsigned i=0;i<6;i++) {int32_t rel;memcpy(&rel,AT(sites[i])+1,4);restored=restored && *AT(sites[i])==0xe8 && AT(sites[i])+5+rel==AT(targets[i]);}
        printf("ENGINE_LOADOUT sites_restored=%d calls=%llu rests=%llu hash=%016llx\n",restored,(unsigned long long)ll_calls_seen,(unsigned long long)ll_rests_seen,(unsigned long long)ll_hash);
    }
    if(find_on) {
        int32_t rel;memcpy(&rel,AT(LX_FIND_ORDER_SITE_VA)+1,4);
        printf("ENGINE_FIND_ORDER site_restored=%d calls=%llu hash=%016llx\n",AT(LX_FIND_ORDER_SITE_VA)+5+rel==AT(LX_FIND_ORDER_ASSIGN_VA),(unsigned long long)find_calls,(unsigned long long)find_hash);
    }
    if(script_on) {
        uint64_t exec_head,range_head;void *ai,*stack,*cue;
        memcpy(&exec_head,AT(LX_EXEC_RUN_VA),8);memcpy(&range_head,AT(LX_EXEC_RANGE_VA),8);
        memcpy(&ai,AT(LX_AI_EVENT_SLOT_VA),8);memcpy(&stack,AT(LX_STACK_EVENT_SLOT_VA),8);memcpy(&cue,AT(LX_CUE_EVENT_SLOT_VA),8);
        printf("ENGINE_SCRIPT_RNG entries_restored=%d slots_restored=%d blocks_restored=%d word_lost=%u calls=%llu sum=%016llx\n",
            exec_head==LX_EXEC_RUN_ENTRY_ORIGINAL && range_head==LX_EXEC_RANGE_ENTRY_ORIGINAL,
            ai==(void*)AT(LX_AI_EVENT_VA) && stack==(void*)AT(LX_STACK_EVENT_VA) && cue==(void*)AT(LX_CUE_EVENT_VA),
            !memcmp(sr_block_timed,AT(LX_CUE_TIMED_SITE_VA&~15u),16) && !memcmp(sr_block_wait,AT(LX_CUE_WAIT_SITE_VA&~15u),16) && !memcmp(sr_block_raise,AT(LX_CUE_RAISE_SITE_VA&~15u),16),
            sr_word_lost,(unsigned long long)sr_calls,(unsigned long long)sr_sum);
    }
    if(md_resume_on) {
        int32_t rel;memcpy(&rel,AT(LX_MD_RESUME_SITE_VA)+1,4);
        printf("ENGINE_MD_RESUME site_restored=%d word_lost=%u\n",AT(LX_MD_RESUME_SITE_VA)+5+rel==AT(LX_MD_RESUME_DELAY_VA),md_word_lost);
    }
    if(boot_hold_on) {
        uint64_t head;memcpy(&head,AT(LX_BOOT_HOLD_MOVE_VA),8);
        printf("ENGINE_BOOT_HOLD entry_restored=%d phase2_native=%u native=%u\n",head==LX_BOOT_HOLD_ENTRY_ORIGINAL,boot_native_phase2,boot_native_moves);
    }
    if(ai_wait_on) {
        void *slot;memcpy(&slot,AT(LX_AI_WAIT_SLOT_VA),8);
        printf("ENGINE_AI_WAIT slot_restored=%d word_lost=%u\n",slot==(void*)AT(LX_AI_WAIT_RESTORE_VA),ai_wait_lost);
    }
    if(region_on) {
        void *slot;memcpy(&slot,AT(LX_REGION_SLOT_VA),8);
        printf("ENGINE_REGION slot_restored=%d word_lost=%u\n",slot==(void*)AT(LX_REGION_MOVE_VA),region_word_lost);
    }
    if(path_ships) {
        uint64_t word,budget;memcpy(&word,(void*)((uintptr_t)AT(LX_PATH_RELEASE_SITE)&~(uintptr_t)7),8);memcpy(&budget,path_manager+LX_PATH_BUDGET,8);
        printf("ENGINE_PATH passes=%u site_restored=%d budget_native=%d\n",path_passes,word==LX_PATH_RELEASE_ORIGINAL,budget==0x3f50624dd2f1a9fcull);
    }
    return timed_out || done==2;
}
