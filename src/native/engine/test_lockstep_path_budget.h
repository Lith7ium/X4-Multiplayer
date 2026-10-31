/* native_gate_path_budget: on the relocated X4.exe, the real UpdateBatch runs a batch of fake
 * agents (its agent call goes to a recorder): N full updates per pass, the rest cheap, the
 * next pass starting at the first cheap agent, in both pass orders; then the view for other
 * modules' pins, tamper, restore and refusals. */
#define TEST_PBUD_AGENTS 10u
static unsigned test_pbud_seen[64],test_pbud_cheap[64],test_pbud_calls;
static void test_pbud_agent(void *agent,void *commands,float dt,double time,unsigned char cheap) {
    (void)commands;assert(dt==0.25f && time==7.5 && test_pbud_calls<64);
    test_pbud_seen[test_pbud_calls]=(unsigned)(uintptr_t)agent;test_pbud_cheap[test_pbud_calls]=cheap;++test_pbud_calls;
}
typedef struct test_pbud_node {struct test_pbud_node *prev,*next;void *agent;} test_pbud_node;
/* The emitted block runs: rsi below N-1 leaves by the first exit, else by the second. */
static void test_path_budget_emit(void) {
    unsigned char *page=VirtualAlloc(NULL,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(page);
    /* push rsi; mov rsi,rcx; call block; pop rsi; ret; the block's exits return 1 and 2 through it */
    static const unsigned char wrap[]={0x56,0x48,0x89,0xce,0xe8,0x07,0,0,0,0x5e,0xc3,0xcc,0xcc,0xcc,0xcc,0xcc};
    memcpy(page,wrap,sizeof(wrap));
    unsigned char *block=page+0x10,*full=page+0x80,*cheap=page+0x90;
    static const unsigned char full_exit[]={0xb8,1,0,0,0,0xc3},cheap_exit[]={0xb8,2,0,0,0,0xc3};
    memcpy(full,full_exit,sizeof(full_exit));memcpy(cheap,cheap_exit,sizeof(cheap_exit));
    unsigned char code[PBUD_SIZE];pbud_emit(code,3,(int32_t)(full-block),(int32_t)(cheap-block));
    memcpy(block,code,PBUD_SIZE);assert(block+PBUD_SIZE<=full && code[PBUD_SIZE-1]==0xcc);
    FlushInstructionCache(GetCurrentProcess(),page,0x1000);
    uint64_t (*run)(uint64_t)=(uint64_t(*)(uint64_t))(void*)page;
    /* rsi < N-1: stay full. The Nth agent (index N-1) is the last full one. */
    assert(run(0)==1 && run(1)==1 && run(2)==2 && run(3)==2 && run(UINT64_MAX)==2);
    pbud_emit(code,PBUD_MAX,(int32_t)(full-block),(int32_t)(cheap-block));
    memcpy(block,code,PBUD_SIZE);FlushInstructionCache(GetCurrentProcess(),page,0x1000);
    assert(run(PBUD_MAX-2)==1 && run(PBUD_MAX-1)==2);
    assert(VirtualFree(page,0,MEM_RELEASE));
    puts("path budget block: compares the loop index with N-1 and takes the block's own exits PASS");
}
/* native_gate_fast_reads: the same answers as the system call, and a fault fails the read. */
static void test_fast_reads(void) {
    int saved=read_local_fast;uint64_t before=read_local_calls;
    static unsigned char source[READ_LOCAL_FAST_MAX+16],copy[sizeof(source)];
    for(unsigned i=0;i<sizeof(source);i++) source[i]=(unsigned char)(i*7+1);
    for(unsigned mode=0;mode<2;mode++) {
        read_local_fast=(int)mode;memset(copy,0,sizeof(copy));
        assert(read_local(source,copy,17) && !memcmp(copy,source,17) && !copy[17]);
        assert(read_local(source+3,copy,READ_LOCAL_FAST_MAX) && !memcmp(copy,source+3,READ_LOCAL_FAST_MAX));
        assert(!read_local(NULL,copy,8) && !read_local((void*)16,copy,8));
    }
    /* Fast reads take no system call up to the limit; larger ones do. */
    read_local_fast=1;before=read_local_calls;
    assert(read_local(source,copy,READ_LOCAL_FAST_MAX) && read_local_calls==before);
    assert(read_local(source,copy,READ_LOCAL_FAST_MAX+1) && read_local_calls==before+1);
    /* A page without access and a guard page fail the read and leave the process running. */
    unsigned char *page=VirtualAlloc(NULL,0x2000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);DWORD old;assert(page);
    assert(VirtualProtect(page+0x1000,0x1000,PAGE_NOACCESS,&old));
    assert(read_local(page+0x1000-8,copy,8) && !read_local(page+0x1000-8,copy,9));
    assert(VirtualProtect(page+0x1000,0x1000,PAGE_READWRITE|PAGE_GUARD,&old) && !read_local(page+0x1000,copy,8));
    assert(VirtualFree(page,0,MEM_RELEASE));
    read_local_fast=saved;
    puts("fast reads: same bytes as ReadProcessMemory, faults fail the read, large reads stay system calls PASS");
}
typedef void (*test_pbud_batch_fn)(void *,void *,void *,unsigned char);
static void test_path_budget_native_image(const char *path) {
    unsigned char *prior=image;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;uint64_t saved_issued=stats.issued;
    int saved_boot=boot_mode,saved_phase=boot_phase,saved_path=path_installed;unsigned saved_options=path_options;
    image=test_pmath_map(path);
    assert(hash_ok(image+PBUD_BLOCK,PBUD_SIZE,PBUD_SHA) && hash_ok(image+0xeaed74,353,"0d710205daf528902a2b95da8418d7173ea4e0fea8676d1b297c3885b6b140cf"));
    /* The batch's calls go to the recorder; the imports it uses are the real ones. */
    unsigned char *stubs=NULL;
    for(uintptr_t at=((uintptr_t)image+PBUD_BLOCK)&~(uintptr_t)65535,distance=65536;!stubs && distance<0x70000000;distance+=65536)
        stubs=VirtualAlloc((void*)(at-distance),0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
    assert(stubs);
    {void *target=(void*)test_pbud_agent;stubs[0]=0xff;stubs[1]=0x25;memset(stubs+2,0,4);memcpy(stubs+6,&target,8);}
    int32_t rel=(int32_t)(stubs-(image+0xeaed74));memcpy(image+0xeaed70,&rel,4);
    {   /* lea rcx,[rbp+0x1d0]; call [Enter/Leave]: the two critical section imports of the tail */
        int32_t d;void *enter=(void*)EnterCriticalSection,*leave=(void*)LeaveCriticalSection;
        memcpy(&d,image+0xeaee67+2,4);memcpy(image+0xeaee67+6+d,&enter,8);
        memcpy(&d,image+0xeaee9e+2,4);memcpy(image+0xeaee9e+6+d,&leave,8);
    }
    *(volatile LONG*)(image+0xeaecf2+0x2eca41e)=5; /* the pass gate is open, no waiter to wake */
    static unsigned char manager[0x400];memset(manager,0,sizeof(manager));
    InitializeCriticalSection((CRITICAL_SECTION*)(manager+0x1d0));
    *(float*)(manager+0x1f8)=0.25f;*(double*)(manager+0x200)=7.5;
    static test_pbud_node nodes[TEST_PBUD_AGENTS],sentinel;
    sentinel.next=&nodes[0];sentinel.prev=&nodes[TEST_PBUD_AGENTS-1];
    for(unsigned i=0;i<TEST_PBUD_AGENTS;i++) {
        nodes[i].agent=(void*)(uintptr_t)(i+1);nodes[i].next=i+1<TEST_PBUD_AGENTS?&nodes[i+1]:&sentinel;nodes[i].prev=i?&nodes[i-1]:&sentinel;
    }
    struct {uint64_t members;test_pbud_node *sentinel,*cursor;} batch={TEST_PBUD_AGENTS,&sentinel,&nodes[0]};
    /* Installed through the real arm, N=3. */
    stats.active=1;stats.issued=0;boot_mode=1;boot_phase=0;ui_thread=GetCurrentThreadId();path_installed=1;path_options=1;
    unsigned char original[PBUD_SIZE];memcpy(original,image+PBUD_BLOCK,PBUD_SIZE);
    assert(!pbud_arm(0) && !pbud_arm(PBUD_MAX+1));
    path_options=0;assert(!pbud_arm(3) && !strcmp(pbud_stage,"state"));path_options=1;
    assert(pbud_arm(3) && pbud_installed && pbud_owned() && pbud_check() && image[PBUD_BLOCK]==0x48 && memcmp(image+PBUD_BLOCK,original,PBUD_SIZE));
    assert(pbud_arm(3) && !pbud_arm(4)); /* a reloaded UI state: only the same N */
    /* Other modules' pins over the batch loop pass through the view; the raw bytes are ours. */
    assert(hash_ok(image+PBUD_BLOCK,PBUD_SIZE,PBUD_SHA) && hash_ok(image+0xeaed74,353,"0d710205daf528902a2b95da8418d7173ea4e0fea8676d1b297c3885b6b140cf"));
    {unsigned char view[PBUD_SIZE];assert(read_local(image+PBUD_BLOCK,view,PBUD_SIZE) && !memcmp(view,original,PBUD_SIZE));}
    test_pbud_batch_fn run=(test_pbud_batch_fn)(void*)(image+0xeaecc0);
    unsigned start=0;
    for(unsigned pass=0;pass<4;pass++) {
        test_pbud_calls=0;run(manager,(void*)0x1234,&batch,0);
        assert(test_pbud_calls==TEST_PBUD_AGENTS);
        for(unsigned i=0;i<TEST_PBUD_AGENTS;i++) {
            assert(test_pbud_seen[i]==(start+i)%TEST_PBUD_AGENTS+1);
            assert(test_pbud_cheap[i]==(i>=3));
        }
        start=(start+3)%TEST_PBUD_AGENTS;
        assert((uintptr_t)batch.cursor->agent==start+1);
    }
    /* A flagged batch keeps stock's first 101 full updates: with 10 agents, all of them. */
    test_pbud_calls=0;run(manager,(void*)0x1234,&batch,1);
    for(unsigned i=0;i<TEST_PBUD_AGENTS;i++) assert(!test_pbud_cheap[i]);
    assert((uintptr_t)batch.cursor->agent==start+1);
    /* Tamper: the tick check stops the gate; a foreign block is not restored over. */
    image[PBUD_BLOCK+8]^=1;assert(!pbud_owned() && !pbud_check() && !strcmp(pbud_stage,"site_changed"));
    pbud_restore();assert(pbud_installed && !strcmp(pbud_stage,"restore_foreign"));
    image[PBUD_BLOCK+8]^=1;pbud_failures=0;
    char out[128];assert(pbud_status(out,sizeof(out))>0 && strstr(out,"installed=1 n=3 "));
    pbud_restore();assert(!pbud_installed && pbud_restored && !memcmp(image+PBUD_BLOCK,original,PBUD_SIZE) && !InterlockedCompareExchange(&pbud_any,0,0));
    /* A changed block is refused. */
    image[PBUD_BLOCK+1]^=1;assert(!pbud_arm(3) && !strcmp(pbud_stage,"pins"));image[PBUD_BLOCK+1]^=1;
    DeleteCriticalSection((CRITICAL_SECTION*)(manager+0x1d0));
    VirtualFree(stubs,0,MEM_RELEASE);pbud_restored=0;pbud_stage="idle";pbud_busy=0;pbud_n=0;
    stats.active=saved_active;stats.issued=saved_issued;ui_thread=saved_ui;boot_mode=saved_boot;boot_phase=saved_phase;path_installed=saved_path;path_options=saved_options;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("path budget on X4.exe: the real UpdateBatch gives N full updates, the rest cheap, and rotates the cursor; view, tamper, restore PASS");
}
