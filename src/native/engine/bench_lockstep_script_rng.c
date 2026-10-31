/* Offline self-cost benchmark of the per-script RNG scopes (no engine):
 * 1000 synthetic AI contexts (24 shared scripts, a 200k-entry identity table)
 * each run once per frame with one child event (another context) and one
 * same-owner call-stack event, plus 200 MD cue runs: 3200 entries per frame.
 * Reports the best-of-7 frame time active minus pass-through and a key
 * fingerprint of one fixed frame, which must not change between versions.
 * Builds against any tree that has the script RNG (caches are detected; for
 * an older tree copy it next to that tree's lockstep_gate.c), e.g.
 *   zig cc -target x86_64-windows-gnu -mcx16 -std=c11 -O2 -fms-extensions -w
 *     -Isrc/native/engine src/native/engine/portable_rotation.c
 *     src/native/engine/bench_lockstep_script_rng.c -lbcrypt -lws2_32 -o bench.exe
 * Measured (this PC): 92151d9 5.26 ms/frame (1645 ns/entry); cached 0.17 ms
 * (52 ns/entry); fingerprint keyset=600f60f4efaebe91 drawset=9e92afa9b6a24df9
 * on both. */
#include "lockstep_gate.c"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#define CTX 1000u
#define CUES 200u
#define SCRIPTS 24u
typedef struct {unsigned char context[0x200],entity[0x80],exec[0x80],callstack[0xd0];uintptr_t entries[40],program[2];unsigned char pad[1536];} bai;
typedef struct {unsigned char cue[0x2a0],module[0x100],node[0x60],exec[0x80];uintptr_t entries[8],program[2];unsigned char pad[1024];} bmd;
static uint64_t *bw;static unsigned bdraw=3;
static uint64_t bstep(uint64_t s){s=s*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;return (s>>30)|(s<<34);}
static void bd(unsigned n){for(unsigned i=0;i<n;i++)*bw=bstep(*bw);}
static void bstr(unsigned char *a,const char *v,char *heap){size_t n=strlen(v);memset(a,0,32);
    if(n<16){memcpy(a,v,n);*(uint64_t*)(a+16)=n;*(uint64_t*)(a+24)=15;}
    else {strcpy(heap,v);*(char**)a=heap;*(uint64_t*)(a+16)=n;*(uint64_t*)(a+24)=n|15;}}
static bai *ais;static bmd *mds;static unsigned char scripts[SCRIPTS][0x80];static char heaps[SCRIPTS+CUES*2][64];
static uintptr_t bhandler(void *self,void *a,void *b,void *c){(void)self;(void)a;(void)b;(void)c;bd(2);return 0;}
static unsigned cur;
static void bexec(void *exec){(void)exec;bd(bdraw);
    script_rng_event_run(SR_EVENT,bhandler,bw,ais[(cur*7+1)%CTX].context,NULL,NULL,NULL);   /* child */
    script_rng_event_run(SR_STACK,bhandler,bw,ais[cur].callstack,NULL,NULL,NULL);           /* same */
    bd(1);}
static void bcue(void *exec){(void)exec;bd(2);}
static uint64_t checksum;
static double frame_ns(int active,unsigned frames){
    InterlockedExchange(&script_rng_active,active);LARGE_INTEGER f,a,b;QueryPerformanceFrequency(&f);uint64_t word=1;bw=&word;
    QueryPerformanceCounter(&a);
    for(unsigned fr=0;fr<frames;fr++){++stats.issued;
        for(cur=0;cur<CTX;cur++) npc_exec_entry(bexec,ais[cur].exec,&word,0);
        for(unsigned i=0;i<CUES;i++) npc_exec_entry(bcue,mds[i].exec,&word,0);}
    QueryPerformanceCounter(&b);return (double)(b.QuadPart-a.QuadPart)*1e9/(double)f.QuadPart/frames;
}
int main(void){
    image=VirtualAlloc(NULL,0x6fa9000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    uint64_t x=0x9e3779b97f4a7c15ULL;
    for(unsigned i=0;i<200000;i++){x=lockstep_mix(x+i);assert(world_identity_record(((x|1)&0x7fffffffffffULL)|0x1000000000ULL,100000+i));}
    ais=VirtualAlloc(NULL,sizeof(bai)*CTX,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);mds=VirtualAlloc(NULL,sizeof(bmd)*CUES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    static const char *names[]={"move.idle","fight.attack.object.fighter","move.generic","order.trade.routine","move.patrol","interrupt.attacked"};
    for(unsigned s=0;s<SCRIPTS;s++){char n[64];snprintf(n,sizeof n,"%s.%u",names[s%6],s);bstr(scripts[s]+0x10,n,heaps[s]);}
    for(unsigned i=0;i<CTX;i++){bai *a=&ais[i];
        *(void**)a->context=image+SCRIPT_RNG_AI_VT;*(void**)(a->context+0x120)=a->entity;*(uint64_t*)(a->entity+8)=0x800000+i;
        assert(world_identity_record(0x800000+i,0x40000+i));
        *(void**)(a->context+0xd8)=scripts[i%SCRIPTS];a->program[0]=(uintptr_t)a->entries;a->program[1]=(uintptr_t)(a->entries+40);
        *(void**)(a->exec+0x10)=a->program;*(uint64_t*)(a->exec+0x18)=i%37;*(uint32_t*)(a->exec+0x38)=0x66;*(void**)(a->exec+0x40)=a->context+0xa8;
        *(void**)a->callstack=image+SCRIPT_RNG_STACK_VT;*(void**)(a->callstack+0xc8)=a->context;
        *(uint32_t*)(a->context+0x18)=0x506;*(uint32_t*)(a->context+0x20)=0xa;*(uint64_t*)(a->context+0x28)=0x800000+(i*13)%CTX;}
    for(unsigned i=0;i<CUES;i++){bmd *m=&mds[i];char n[64];
        *(void**)m->cue=image+SCRIPT_RNG_CUE_VT;*(void**)(m->cue+0x160)=m->cue;*(void**)(m->cue+0x150)=m->module;
        snprintf(n,sizeof n,"FactionLogic%u",i%7);bstr(m->module+0x18,n,heaps[SCRIPTS+2*i]);snprintf(n,sizeof n,"Cue_%u",i);bstr(m->node+0x20,n,heaps[SCRIPTS+2*i+1]);
        *(void**)(m->module+0x80)=m->node;*(void**)m->node=m->module+0x60;*(void**)(m->node+0x40)=m->cue;
        m->program[0]=(uintptr_t)m->entries;m->program[1]=(uintptr_t)(m->entries+8);
        *(void**)(m->exec+0x10)=m->program;*(uint32_t*)(m->exec+0x38)=0x5d;*(void**)(m->exec+0x40)=m->cue+0xb0;}
    stats.active=1;boot_mode=1;boot_phase=3;stats.issued=100;ui_thread=GetCurrentThreadId();
    script_rng_mask=SCRIPT_RNG_AI|SCRIPT_RNG_MD;script_rng_mode=SCRIPT_RNG_KEY;
#ifdef SCRIPT_RNG_AI_CACHE
    script_rng_rows=0;world_identity_seal();script_rng_clock_start();
#endif
    frame_ns(1,5);frame_ns(0,5);
    double best_on=1e18,best_off=1e18;
    for(int r=0;r<7;r++){double on=frame_ns(1,20),off=frame_ns(0,20);if(on<best_on)best_on=on;if(off<best_off)best_off=off;}
    printf("why unreadable=%lld unmapped=%lld unnamed=%lld foreign=%lld masked=%lld full=%lld\n",(long long)script_rng_why[1],(long long)script_rng_why[2],
        (long long)script_rng_why[3],(long long)script_rng_why[4],(long long)script_rng_why[5],(long long)script_rng_table_full);
    unsigned entries=CTX*3+CUES;
    printf("entries/frame=%u active=%.0f us pass=%.0f us self=%.0f us/frame %.0f ns/entry outer=%lld child=%lld same=%lld unkeyed=%lld failures=%lld\n",
        entries,best_on/1e3,best_off/1e3,(best_on-best_off)/1e3,(best_on-best_off)/entries,(long long)script_rng_outer,(long long)script_rng_child,
        (long long)script_rng_same,(long long)script_rng_unkeyed,(long long)script_rng_failures);
    /* Key fingerprint of one fixed frame: identical across implementations. */
    stats.issued=777;memset(&script_rng_now,0,sizeof(script_rng_now));InterlockedExchange(&script_rng_active,1);
    {uint64_t word=1;bw=&word;for(cur=0;cur<CTX;cur++) npc_exec_entry(bexec,ais[cur].exec,&word,0);
     for(unsigned i=0;i<CUES;i++) npc_exec_entry(bcue,mds[i].exec,&word,0);
     printf("fingerprint tick=777 outer=%u child=%u keyset=%016llx drawset=%016llx draws=%llu word=%016llx\n",script_rng_now.outer,script_rng_now.child,
        (unsigned long long)script_rng_now.keyset,(unsigned long long)script_rng_now.drawset,(unsigned long long)script_rng_now.draws,(unsigned long long)word);}
#ifdef SCRIPT_RNG_AI_CACHE
    char s[4096];if(script_rng_status(s,sizeof s)) {const char *p=strstr(s,"self_us");printf("status: %.220s\n",p?p:"-");}
#endif
    return 0;
}
