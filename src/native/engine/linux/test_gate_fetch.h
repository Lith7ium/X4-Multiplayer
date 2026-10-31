/* gate_read_local: the same answers both ways, the fault recovery and its chain to the prior
 * handler, a later owner of the signal; what a read and a Region::Move scope cost here.
 * Included by test_gate_linux.c. */
#include <setjmp.h>
#include <sys/resource.h>
#include <sys/wait.h>
static sigjmp_buf fetch_jump;
static volatile int fetch_info_calls,fetch_plain_calls;
static void fetch_prior_info(int sig,siginfo_t *info,void *context) {(void)sig;(void)info;(void)context;++fetch_info_calls;siglongjmp(fetch_jump,1);}
static void fetch_prior_plain(int sig) {(void)sig;++fetch_plain_calls;siglongjmp(fetch_jump,1);}
static void fetch_answers(unsigned char *page) {
    static const size_t sizes[]={1,2,4,7,8,9,16,64,4096};
    static unsigned char out[4096];uint64_t word=0;
    for(unsigned i=0;i<sizeof(sizes)/sizeof(*sizes);i++) {
        memset(out,0,sizeof(out));
        assert(gate_read_local(page,out,sizes[i]) && !memcmp(out,page,sizes[i]));
    }
    assert(!gate_read_local(page+GATE_PAGE-4,&word,8));
    assert(!gate_read_local(page+GATE_PAGE-4,out,16));
    assert(!gate_read_local(page+GATE_PAGE,&word,8));
    assert(!gate_read_local(page+GATE_PAGE,out,1));
    assert(!gate_read_local(NULL,&word,8));
    assert(!gate_read_local((const void*)(uintptr_t)8,&word,8));
    assert(!gate_read_local((const void*)(uintptr_t)0x8000000000000000ull,&word,8)); /* not canonical */
    assert(!gate_read_local((const void*)(uintptr_t)0xffff800000000000ull,out,16));  /* the kernel's */
}
static void test_fetch(void) {
    unsigned char *page=mmap(NULL,2*GATE_PAGE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(page!=MAP_FAILED && !mprotect(page+GATE_PAGE,GATE_PAGE,PROT_NONE));
    for(unsigned i=0;i<GATE_PAGE;i++) page[i]=(unsigned char)(i*7+1);
    volatile unsigned char *gone=page+GATE_PAGE;
    struct sigaction saved,saved_bus,now,info,plain,later;
    assert(!sigaction(SIGSEGV,NULL,&saved) && saved.sa_handler==SIG_DFL && !sigaction(SIGBUS,NULL,&saved_bus));
    /* Both ways, the same answers. */
    fetch_answers(page);
    assert(!gate_fetch_on && !gate_fetch_faults);
    gate_fetch_install();
    assert(gate_fetch_on && gate_fetch_installed && gate_fetch_ours(SIGSEGV) && gate_fetch_ours(SIGBUS));
    fetch_answers(page);
    assert(gate_fetch_faults>=6);
    gate_fetch_check();
    assert(gate_fetch_on && !gate_fetch_lost);
    gate_fetch_remove();
    assert(!gate_fetch_on && !sigaction(SIGSEGV,NULL,&now) && now.sa_handler==SIG_DFL);
    /* A fault outside the copy reaches the prior handler, of both kinds; ours still recovers after. */
    memset(&info,0,sizeof(info));sigemptyset(&info.sa_mask);info.sa_sigaction=fetch_prior_info;info.sa_flags=SA_SIGINFO;
    memset(&plain,0,sizeof(plain));sigemptyset(&plain.sa_mask);plain.sa_handler=fetch_prior_plain;
    assert(!sigaction(SIGSEGV,&info,NULL));
    gate_fetch_install();
    if(!sigsetjmp(fetch_jump,1)) {*gone=1;assert(0);}
    assert(fetch_info_calls==1);
    uint64_t word=0;
    assert(!gate_read_local(page+GATE_PAGE,&word,8) && fetch_info_calls==1);
    gate_fetch_remove();
    assert(!sigaction(SIGSEGV,NULL,&now) && (now.sa_flags&SA_SIGINFO) && now.sa_sigaction==fetch_prior_info);
    assert(!sigaction(SIGSEGV,&plain,NULL));
    gate_fetch_install();
    if(!sigsetjmp(fetch_jump,1)) {*gone=1;assert(0);}
    assert(fetch_plain_calls==1 && fetch_info_calls==1);
    /* A later owner: the slow way from then on, and the owner stays. */
    assert(gate_fetch_on && !sigaction(SIGSEGV,&info,NULL));
    gate_fetch_check();
    assert(!gate_fetch_on && gate_fetch_lost==1);
    uint64_t faults=gate_fetch_faults;
    assert(!gate_read_local(page+GATE_PAGE,&word,8) && gate_read_local(page,&word,8) && gate_fetch_faults==faults && fetch_info_calls==1);
    gate_fetch_remove();
    assert(!sigaction(SIGSEGV,NULL,&now) && (now.sa_flags&SA_SIGINFO) && now.sa_sigaction==fetch_prior_info);
    assert(!sigaction(SIGSEGV,&saved,NULL) && !sigaction(SIGBUS,&saved_bus,NULL));
    (void)later;
    /* No prior handler: a real fault still ends the process with SIGSEGV. */
    pid_t child=fork();
    assert(child>=0);
    if(!child) {
        struct rlimit none={0,0};setrlimit(RLIMIT_CORE,&none);
        gate_fetch_install();
        *gone=1;
        _exit(0);
    }
    int status=0;
    assert(waitpid(child,&status,0)==child && WIFSIGNALED(status) && WTERMSIG(status)==SIGSEGV);
    munmap(page,2*GATE_PAGE);
    gate_fetch_faults=gate_fetch_lost=0;
    printf("guarded read PASS\n");
}
/* One Region::Move scope around an empty Move: 5 fields, none a DamageField (12 reads). */
static void fetch_empty_move(void *self) {(void)self;}
static uint64_t fetch_region_ns(unsigned char *region,unsigned rounds) {
    uint64_t word=1,reads=gate_read_count,start=gate_now_ns();
    for(unsigned i=0;i<rounds;i++) region_rng_run(fetch_empty_move,&word,region);
    uint64_t took=gate_now_ns()-start;
    assert(gate_read_count-reads==12ull*rounds && !region_rng_failures && word==1);
    return took/rounds;
}
static void test_fetch_region_cost(void) {
    static __attribute__((aligned(16))) unsigned char region[0x300],fields[5][16];
    static void *vector[5];
    const void *other=fields;uint64_t runtime=7001;
    memcpy(region+8,&runtime,8);
    for(unsigned i=0;i<5;i++) {memcpy(fields[i],&other,8);vector[i]=fields[i];}
    void *begin=vector,*end=vector+5;memcpy(region+REGION_RNG_FIELDS,&begin,8);memcpy(region+REGION_RNG_FIELDS+8,&end,8);
    world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));
    assert(world_identities && world_identity_record(runtime,0x101));
    world_identity_sealed=1;
    region_rng_active=1;region_rng_mode=REGION_RNG_KEY;stats.active=1;boot_phase=3;ui_thread=GetCurrentThreadId();
    uint64_t slow=fetch_region_ns(region,2000);
    gate_fetch_install();
    uint64_t fast=fetch_region_ns(region,200000);
    gate_fetch_remove();
    assert(region_rng_keyed==202000);
    printf("FETCH_COST read_slow_ns=%llu read_fast_ns=%llu.%llu gettid_ns=%llu region_call_slow_ns=%llu region_call_fast_ns=%llu reads_per_call=12\n",
        (unsigned long long)gate_fetch_cost_slow_ns,(unsigned long long)(gate_fetch_cost_fast_dns/10),(unsigned long long)(gate_fetch_cost_fast_dns%10),
        (unsigned long long)gate_fetch_cost_tid_ns,
        (unsigned long long)slow,(unsigned long long)fast);
    /* Back to the start state of the other tests. */
    free(world_identities);world_identities=NULL;world_identity_sealed=0;world_identity_count=0;
    region_rng_active=0;region_rng_mode=0;region_rng_calls=region_rng_scoped=region_rng_keyed=0;region_rng_epoch_tick=UINT64_MAX;
    memset(&region_rng_now,0,sizeof(region_rng_now));
    stats.active=0;boot_phase=0;ui_thread=0;gate_read_count=0;
}
