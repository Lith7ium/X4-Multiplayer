static unsigned test_ww_calls,test_ww_mode;
static void *test_ww_self,*test_ww_exec,*test_ww_value,*test_ww_tls;
static HANDLE test_ww_entered,test_ww_release;
static unsigned char test_ww_original(void *self,void *exec,void *value) {
    assert(self==test_ww_self && exec==test_ww_exec && value==test_ww_value);++test_ww_calls;
    uintptr_t context=*(uintptr_t*)((unsigned char*)value+8)-0xa8;
    *(uint64_t*)((unsigned char*)test_ww_tls+0x318)+=7;*(double*)(context+0x190)=7.1;*(unsigned*)(context+0x18c)=2;
    if(test_ww_mode==1) ++*(uint64_t*)(context+8);
    if(test_ww_mode==2) {world_wait_restore();assert(world_wait_restore_open && !world_wait_admit());}
    if(test_ww_mode==3) {assert(SetEvent(test_ww_entered));assert(WaitForSingleObject(test_ww_release,2000)==WAIT_OBJECT_0);}
    return test_ww_mode==4?0xa5:1;
}
static DWORD WINAPI test_ww_worker(void *unused) {
    (void)unused;assert(world_wait_observe(test_ww_original,test_ww_self,test_ww_exec,test_ww_value,(uintptr_t)(image+WORLD_WAIT_CALLER),test_ww_tls)==1);return 0;
}
static void test_ww_copy_rva(unsigned char *bytes,size_t size,unsigned rva,void *out,size_t count) {
    IMAGE_DOS_HEADER *dos=(void*)bytes;assert(dos->e_magic==IMAGE_DOS_SIGNATURE && dos->e_lfanew>0);
    assert((size_t)dos->e_lfanew+sizeof(IMAGE_NT_HEADERS64)<size);IMAGE_NT_HEADERS64 *nt=(void*)(bytes+dos->e_lfanew);
    assert(nt->Signature==IMAGE_NT_SIGNATURE);IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(nt);
    assert((unsigned char*)(sections+nt->FileHeader.NumberOfSections)<=bytes+size);
    for(unsigned j=0;j<nt->FileHeader.NumberOfSections;j++) {
        IMAGE_SECTION_HEADER *s=&sections[j];if(rva<s->VirtualAddress || rva-s->VirtualAddress+count>s->SizeOfRawData) continue;
        size_t offset=s->PointerToRawData+rva-s->VirtualAddress;assert(offset+count<=size);memcpy(out,bytes+offset,count);return;
    }
    assert(!"missing actual-image RVA");
}
static void test_ww_image(unsigned char prefix[8]) {
    if(!test_wq_image_path) return;
    FILE *file=fopen(test_wq_image_path,"rb");assert(file && !fseek(file,0,SEEK_END));long size=ftell(file);
    assert(size>0 && size<512*1024*1024 && !fseek(file,0,SEEK_SET));unsigned char *bytes=malloc((size_t)size);
    assert(bytes && fread(bytes,1,(size_t)size,file)==(size_t)size);fclose(file);assert(hash_ok(bytes,(ULONG)size,EXE_SHA));
    unsigned char *saved_image=image;int saved_dt=dt_installed;
    image=VirtualAlloc(NULL,0x6fa9000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);dt_installed=0;
    const unsigned phase_rvas[]={TICK_RVA,0xac2a50,0xac2c70,0x9ee7a0,0x14b8dc0,0x14b8e20};
    const unsigned phase_sizes[]={2127,522,446,9772,89,101};
    for(unsigned i=0;i<6;i++) test_ww_copy_rva(bytes,(size_t)size,phase_rvas[i],image+phase_rvas[i],phase_sizes[i]);
    for(unsigned i=0;i<sizeof(world_wait_pin_list)/sizeof(world_wait_pin_list[0]);i++) {
        const world_wait_pin *p=&world_wait_pin_list[i];test_ww_copy_rva(bytes,(size_t)size,p->rva,image+p->rva,p->size);
    }
    uint64_t vtable[6];test_ww_copy_rva(bytes,(size_t)size,WORLD_WAIT_VTABLE,vtable,sizeof(vtable));
    assert(vtable[1]==0x140000000ULL+0x1062a50 && vtable[5]==0x140000000ULL+WORLD_WAIT_ENTRY);
    *(void**)(image+WORLD_WAIT_VTABLE+8)=image+0x1062a50;*(void**)(image+WORLD_WAIT_VTABLE+0x28)=image+WORLD_WAIT_ENTRY;
    assert(world_wait_pins());memcpy(prefix,image+WORLD_WAIT_ENTRY,8);
    for(unsigned i=0;i<sizeof(world_wait_pin_list)/sizeof(world_wait_pin_list[0]);i++) {
        const world_wait_pin *p=&world_wait_pin_list[i];image[p->rva+p->size-1]^=1;assert(!world_wait_pins());image[p->rva+p->size-1]^=1;
    }
    VirtualFree(image,0,MEM_RELEASE);image=saved_image;dt_installed=saved_dt;free(bytes);
    puts("world Wait full image pins and exact import/vtable/entry verified");
}
static void test_ww_prepare_nonvolatiles(unsigned char *code) {
    /* Fourth ABI argument supplies callee; first three remain unmodified.
     * Preserve caller's RBX/RSI/RDI, install sentinels, verify them after CALL,
     * then restore them. AL is captured before comparisons. */
    const unsigned char head[]={0x53,0x56,0x57,0x48,0x83,0xec,0x20,
        0x48,0xbb,0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11,
        0x48,0xbe,0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11,
        0x48,0xbf,0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11,
        0x41,0xff,0xd1,0x0f,0xb6,0xc0,
        0x49,0xba,0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11,
        0x4c,0x39,0xd3,0x75,0x0a,0x4c,0x39,0xd6,0x75,0x05,0x4c,0x39,0xd7,0x74,0x02,0x31,0xc0,
        0x48,0x83,0xc4,0x20,0x5f,0x5e,0x5b,0xc3};
    memcpy(code,head,sizeof(head));FlushInstructionCache(GetCurrentProcess(),code,sizeof(head));
}
static unsigned test_ww_nonvolatiles(unsigned char *code,world_wait_fn target) {
    typedef unsigned (*check_fn)(void*,void*,void*,world_wait_fn);
    return ((check_fn)code)(test_ww_self,test_ww_exec,test_ww_value,target);
}
static volatile LONG test_ww_frozen_done;
static void test_ww_saved_decimal(void) {
    /* Original arithmetic harness, not engine code execution. These explicit
     * SSE2 operations mirror the audited scalar path and prohibit FMA or
     * compiler reassociation. Python manifest tests use the same goldens. */
    const struct {const char *text;uint64_t bits;} vectors[]={
        {"0",0x0000000000000000ULL},{"1",0x3ff0000000000000ULL},
        {"6.91667",0x401baaab8a5ce5b5ULL},{"6.31667",0x4019444523f67f4eULL},
        {"7.083",0x401c54fdf3b645a2ULL},{"0.001",0x3f50624dd2f1a9fdULL},
        {"0.123456789012345",0x3fbf9add3746f62eULL},
        {"999999999999999.999999999999999",0x430c6bf526340000ULL},
        {"42.000000000000001",0x4045000000000000ULL},
        {"100000000000000.1",0x42d6bcc41e900006ULL},
        {"0.000000000000001",0x3cd203af9ee7561aULL}
    };
    unsigned mxcsr;__asm__ volatile("stmxcsr %0":"=m"(mxcsr));assert(!(mxcsr&0x6000));
    for(unsigned i=0;i<sizeof(vectors)/sizeof(vectors[0]);i++) {
        double sum=0,scale=0,ten=10,ascii_zero=48,tenth=0.1;
        for(const char *p=vectors[i].text;*p;p++) {
            if(*p=='.') {scale=1;continue;}
            if(!scale) {
                double ascii=(unsigned char)*p;
                __asm__ volatile("mulsd %1,%0\n\taddsd %2,%0\n\tsubsd %3,%0"
                    :"+&x"(sum):"x"(ten),"x"(ascii),"x"(ascii_zero));
            } else {
                double digit=*p-'0';
                __asm__ volatile("mulsd %1,%0":"+x"(scale):"x"(tenth));
                __asm__ volatile("mulsd %1,%0":"+x"(digit):"x"(scale));
                __asm__ volatile("addsd %1,%0":"+x"(sum):"x"(digit));
            }
        }
        uint64_t actual,parsed;memcpy(&actual,&sum,8);assert(actual==vectors[i].bits);
        assert(world_wait_rng_time(vectors[i].text,&parsed)&&parsed==actual);
    }
    puts("world Wait saved decimal: Windows SSE2 exact arithmetic goldens PASS");
}
static DWORD WINAPI test_ww_frozen_worker(void *unused) {
    (void)unused;world_wait_fn original=(world_wait_fn)(thunk+2752);
    assert(original(test_ww_self,test_ww_exec,test_ww_value)==1);assert(SetEvent(test_ww_entered));
    while(!InterlockedCompareExchange(&test_ww_frozen_done,0,0)) assert(original(test_ww_self,test_ww_exec,test_ww_value)==1);
    return 0;
}
#include "test_lockstep_world_wait_rng.h"
static void test_world_wait(void) {
    test_ww_saved_decimal();
    assert(!world_wait_started && !world_wait_enabled);
    const char *manifest="0000000000000099,4,5,401baaab8a5ce5b4,3fb999999999999a,3fe0000000000000,move.idle";
    world_wait_selection parsed[WORLD_WAIT_SELECTED];unsigned count=0;
    assert(world_wait_parse(manifest,strlen(manifest),parsed,&count) && count==1 && parsed[0].index==5);
    assert(world_wait_parse(manifest,strlen(manifest)-1,parsed,&count)); /* shorter valid name is still syntactically valid */
    assert(!world_wait_parse("",0,parsed,&count));assert(!world_wait_parse(manifest,strlen(manifest)+1,parsed,&count));
    assert(!world_wait_parse("-000000000000099,4,5,401baaab8a5ce5b4,3fb999999999999a,3fe0000000000000,move.idle",strlen(manifest),parsed,&count));
    assert(world_wait_parse(manifest,strlen(manifest),parsed,&count));
    unsigned char self[0xe0]={0},exec[0xc0]={0},context[0x210]={0},entity[0x10]={0},script[0x60]={0},tls[0x320]={0},table[0x134]={0};
    uintptr_t nodes[2]={(uintptr_t)self,0},program[2]={(uintptr_t)nodes,(uintptr_t)(nodes+1)};
    *(void**)self=image+WORLD_WAIT_VTABLE;*(unsigned*)(self+0x80)=2;*(unsigned*)(self+0x84)=1;*(unsigned*)(self+0x88)=1;
    *(unsigned*)(self+0x90)=8;*(double*)(self+0x98)=0.1;*(unsigned*)(self+0xa0)=8;*(double*)(self+0xa8)=0.5;
    *(void**)(exec+0x10)=program;*(uint64_t*)(exec+0x18)=17;*(uint64_t*)(exec+0x28)=parsed[0].time;exec[0x30]=1;
    *(unsigned*)(exec+0x38)=0x66;*(void**)(exec+0x40)=context+0xa8;
    *(void**)context=image+0x2c6a270;*(uint64_t*)(context+8)=7;*(void**)(context+0x120)=entity;*(void**)(context+0xd8)=script;*(uint64_t*)(entity+8)=0x9999;
    memcpy(script+0x10,"move.idle",9);*(uint64_t*)(script+0x20)=9;*(uint64_t*)(script+0x28)=15;*(unsigned*)(script+0x58)=4;
    *(void**)(image+TABLE_RVA)=table;*(unsigned*)(tls+0x314)=1;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(world_identities);
    assert(world_identity_record(0x9999,0x99));world_wait_selected[0]=parsed[0];world_wait_selected_count=1;
    test_ww_self=self;test_ww_exec=exec;test_ww_value=exec+0x38;test_ww_tls=tls;test_ww_calls=0;test_ww_mode=0;
    assert(world_wait_observe(test_ww_original,self,exec,exec+0x38,0,tls)==1 && test_ww_calls==1 && !world_wait_count);
    stats.active=1;stats.issued=0;boot_mode=1;boot_phase=2;boot_started=GetTickCount64();ui_thread=GetCurrentThreadId();
    world_wait_enabled=1;world_wait_restoring=0;
    assert(world_wait_observe(test_ww_original,self,exec,exec+0x38,0,tls)==1 && test_ww_calls==2 && world_wait_foreign_calls==1 && !world_wait_count);
    uintptr_t caller=(uintptr_t)(image+WORLD_WAIT_CALLER);
    assert(world_wait_observe(test_ww_original,self,exec,exec+0x38,caller,tls)==1 && test_ww_calls==3);
    assert(world_wait_ready()==1 && !world_wait_events[0].errors && world_wait_events[0].rng_after==world_wait_events[0].rng_before+7);
    assert(world_wait_events[0].due_after==*(uint64_t*)(context+0x190) && world_wait_events[0].before.slot==0 && !world_wait_events[0].index_status);
    world_wait_event maximum;memset(&maximum,0xff,sizeof(maximum));maximum.before.name[95]=maximum.after.name[95]=0;
    memset(maximum.before.name,'a',95);memset(maximum.after.name,'a',95);
    for(unsigned part=0;part<WORLD_WAIT_ROWS;part++) {
        char row[WORLD_WAIT_ROW_CAP],short_row[32];int n=world_wait_format(&maximum,UINT32_MAX,part,row,sizeof(row));
        assert(n>0 && n+sizeof("[X4LAN] NATIVE_GATE ")+1<2048);
        assert(world_wait_format(&maximum,1,part,short_row,sizeof(short_row))==-1);
    }
    /* Selected script mismatch is unselected, but selected version/expression
     * mismatches and unavailable saved mapping cannot be silently accepted. */
    script[0x10]='x';LONG64 before=world_wait_count;world_wait_observe(test_ww_original,self,exec,exec+0x38,caller,tls);assert(world_wait_count==before);script[0x10]='m';
    ++*(unsigned*)(script+0x58);world_wait_observe(test_ww_original,self,exec,exec+0x38,caller,tls);assert(world_wait_events[before].errors&8);--*(unsigned*)(script+0x58);
    *(unsigned*)(self+0xb0)=8;world_wait_observe(test_ww_original,self,exec,exec+0x38,caller,tls);assert(world_wait_events[before+1].errors&16);*(unsigned*)(self+0xb0)=0;
    *(uint64_t*)(entity+8)=0x9998;world_wait_observe(test_ww_original,self,exec,exec+0x38,caller,tls);assert(world_wait_events[before+2].errors&2 && world_wait_unknown==1);*(uint64_t*)(entity+8)=0x9999;
    test_ww_mode=1;world_wait_observe(test_ww_original,self,exec,exec+0x38,caller,tls);assert(world_wait_events[before+3].errors&32);test_ww_mode=0;
    *(uint64_t*)(exec+0x20)=2;world_wait_observe(test_ww_original,self,exec,exec+0x38,caller,tls);assert(world_wait_events[before+4].errors&1);*(uint64_t*)(exec+0x20)=0;
    test_ww_mode=4;assert(world_wait_observe(test_ww_original,self,exec,exec+0x38,caller,tls)==0xa5);
    assert(world_wait_events[before+5].errors&64);test_ww_mode=0;
    /* Worker has already entered original when STOP closes admission. The
     * completed event retains overlap, even when published after STOP. */
    test_ww_mode=3;test_ww_entered=CreateEventW(NULL,TRUE,FALSE,NULL);test_ww_release=CreateEventW(NULL,TRUE,FALSE,NULL);
    HANDLE worker=CreateThread(NULL,0,test_ww_worker,NULL,0,NULL);assert(worker && WaitForSingleObject(test_ww_entered,2000)==WAIT_OBJECT_0);
    LONG64 ready=world_wait_ready();world_wait_restore();assert(world_wait_open==1 && world_wait_restore_open==1 && !world_wait_admit());
    assert(world_wait_ready()==(unsigned)ready && SetEvent(test_ww_release) && WaitForSingleObject(worker,2000)==WAIT_OBJECT_0);
    assert(world_wait_events[ready].errors&128 && !world_wait_open);CloseHandle(worker);CloseHandle(test_ww_entered);CloseHandle(test_ww_release);test_ww_mode=0;
    world_wait_restoring=0;world_wait_enabled=1;world_wait_count=WORLD_WAIT_MAX;
    unsigned calls=test_ww_calls;world_wait_observe(test_ww_original,self,exec,exec+0x38,caller,tls);assert(test_ww_calls==calls+1 && world_wait_dropped==1);
    world_wait_count=0;memset(world_wait_events,0,sizeof(world_wait_events));
    /* Actual-image prefix replay, three arguments, AL return and resident
     * original continuation remain valid while restoration runs inside it. */
    unsigned char prefix[8]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74};test_ww_image(prefix);
    unsigned char *saved_thunk=thunk;DWORD old;
    thunk=make_thunk();assert(thunk && world_wait_thunk_matches());unsigned char *prepared=thunk;
    thunk=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(thunk);
    /* Own private unpublished test page: no transition-region test code or
     * other observer bytes are placed in the production thunk allocation. */
    memset(thunk,0xa5,4096);world_wait_prepare_thunk(thunk);
    for(unsigned i=0;i<4096;i++) if(i<2752 || i>=2816) assert(thunk[i]==0xa5);
    assert(!world_wait_thunk_matches()); /* RW is never an installable page */
    assert(VirtualProtect(thunk,4096,PAGE_EXECUTE_READ,&old) && world_wait_thunk_matches());
    VirtualFree(thunk,0,MEM_RELEASE);thunk=prepared;
    unsigned char *abi=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(abi);
    test_ww_prepare_nonvolatiles(abi);const unsigned char bad_callee[]={0x31,0xdb,0xb0,0xa5,0xc3};memcpy(abi+128,bad_callee,sizeof(bad_callee));
    assert(VirtualProtect(abi,4096,PAGE_EXECUTE_READ,&old));FlushInstructionCache(GetCurrentProcess(),abi,4096);
    assert(VirtualProtect(image+WORLD_WAIT_ENTRY,4096,PAGE_EXECUTE_READWRITE,&old));
    unsigned char *entry=image+WORLD_WAIT_ENTRY;memcpy(entry,prefix,5);entry[5]=0xff;entry[6]=0x25;memset(entry+7,0,4);
    void *continuation=(void*)test_ww_original;memcpy(entry+11,&continuation,8);
    assert(!memcmp(thunk+2752,prefix,5) && world_wait_thunk_matches());
    /* Byte corruption is tested on another unpublished page, never by
     * changing protection on the executable page used by the worker below. */
    unsigned char *corrupt=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(corrupt);
    world_wait_prepare_thunk(corrupt);corrupt[2780]^=1;assert(VirtualProtect(corrupt,4096,PAGE_EXECUTE_READ,&old));
    unsigned char *good=thunk;thunk=corrupt;assert(!world_wait_thunk_matches());thunk=good;VirtualFree(corrupt,0,MEM_RELEASE);
    test_ww_frozen_done=0;test_ww_entered=CreateEventW(NULL,TRUE,FALSE,NULL);test_ww_mode=0;
    worker=CreateThread(NULL,0,test_ww_frozen_worker,NULL,0,NULL);assert(worker && WaitForSingleObject(test_ww_entered,2000)==WAIT_OBJECT_0);
    for(unsigned i=0;i<1000;i++) assert(world_wait_thunk_matches());
    InterlockedExchange(&test_ww_frozen_done,1);assert(WaitForSingleObject(worker,2000)==WAIT_OBJECT_0);CloseHandle(worker);CloseHandle(test_ww_entered);
    memcpy(&world_wait_original,entry,8);world_wait_patched=world_wait_original;((unsigned char*)&world_wait_patched)[0]=0xe9;
    intptr_t reach=(intptr_t)(thunk+2784)-(intptr_t)(entry+5);assert(reach>=INT32_MIN && reach<=INT32_MAX);
    int32_t distance=(int32_t)reach;memcpy((unsigned char*)&world_wait_patched+1,&distance,4);
    assert((world_wait_original>>40)==(world_wait_patched>>40));assert(world_wait_swap(world_wait_original,world_wait_patched));world_wait_installed=1;
    test_ww_mode=4;calls=test_ww_calls;assert(((world_wait_fn)entry)(self,exec,exec+0x38)==0xa5 && test_ww_calls==calls+1);
    assert(test_ww_nonvolatiles(abi,(world_wait_fn)entry)==0xa5);
    assert(!test_ww_nonvolatiles(abi,(world_wait_fn)(abi+128)));
    test_ww_mode=2;calls=test_ww_calls;assert(((world_wait_fn)entry)(self,exec,exec+0x38)==1 && test_ww_calls==calls+1 && !world_wait_installed && !world_wait_open);
    test_ww_mode=0;calls=test_ww_calls;assert(((world_wait_fn)(thunk+2752))(self,exec,exec+0x38)==1 && test_ww_calls==calls+1); /* late resident original */
    uint64_t foreign=world_wait_patched^0x100;memcpy(entry,&foreign,8);world_wait_installed=1;world_wait_restore();assert(world_wait_installed && *(uint64_t*)entry==foreign);
    memcpy(entry,&world_wait_patched,8);world_wait_restore();assert(!world_wait_installed);
    world_wait_started=1;world_wait_enabled=1;world_wait_installed=1;strcpy(world_wait_manifest,manifest);
    memcpy(entry,&world_wait_patched,8);
    assert(world_wait_configure(manifest,strlen(manifest)) && !world_wait_configure(manifest,strlen(manifest)-1));
    world_wait_installed=0;assert(!world_wait_configure(manifest,strlen(manifest)));
    test_world_wait_rng();
    memcpy(entry,&world_wait_original,8);
    VirtualFree(thunk,0,MEM_RELEASE);VirtualFree(abi,0,MEM_RELEASE);thunk=saved_thunk;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;
    memset(world_wait_events,0,sizeof(world_wait_events));world_wait_count=world_wait_calls=world_wait_import_calls=world_wait_foreign_calls=0;
    world_wait_unknown=world_wait_failures=world_wait_dropped=world_wait_sequence=world_wait_open=world_wait_peak=world_wait_restore_open=0;
    world_wait_enabled=world_wait_restoring=0;world_wait_started=world_wait_selected_count=0;world_wait_manifest[0]=0;
    stats.active=0;stats.issued=0;boot_phase=0;ui_thread=0;puts("world Wait read-only guards, original-once, overflow and restoration tests passed");
}
