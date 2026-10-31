/* Windows unwind/ABI checks over an independent mapped image, never a game. */
typedef struct {unsigned end,reg,allocation;} test_tu_operation;
static const test_tu_operation test_tu_operations[5][8]={
    {{4,5,0},{5,3,0},{6,6,0},{7,7,0},{9,12,0},{11,13,0},{13,14,0},{15,15,0}},
    {{2,7,0},{6,0,32}},{{4,3,0},{5,6,0}},{{2,3,0},{6,0,96}},{{4,0,56}},
};
static const unsigned test_tu_counts[]={8,2,2,2,1},test_tu_stack[]={64,40,16,104,56};
static DWORD64 *test_tu_reg(CONTEXT *c,unsigned reg) {
    switch(reg) {
    case 0:return &c->Rax;case 1:return &c->Rcx;case 2:return &c->Rdx;case 3:return &c->Rbx;
    case 4:return &c->Rsp;case 5:return &c->Rbp;case 6:return &c->Rsi;case 7:return &c->Rdi;
    case 8:return &c->R8;case 9:return &c->R9;case 10:return &c->R10;case 11:return &c->R11;
    case 12:return &c->R12;case 13:return &c->R13;case 14:return &c->R14;default:assert(reg==15);return &c->R15;
    }
}
static void test_tu_unwind_offsets(unsigned which) {
    const thunk_unwind_recipe *u=&thunk_unwind_recipes[which];
    const unsigned nonvol[]={3,5,6,7,12,13,14,15};
    /* Every byte of the copied prologue probes CodeOffset progress. Architectural
     * instruction boundaries in the continuation are MOV at prefix and JMP at
     * prefix+10. Interior immediate bytes are not valid execution RIP values. */
    for(unsigned pc=0;pc<=u->prefix_size+10;pc++) {
        if(pc>u->prefix_size && pc<u->prefix_size+10) continue;
        CONTEXT c={0};uint64_t stack[128]={0};uint64_t caller=0x123456789abcdefULL;
        for(unsigned reg=0;reg<16;reg++) *test_tu_reg(&c,reg)=0x1111000000000000ULL+reg;
        c.Rsp=(DWORD64)(uintptr_t)&stack[80];stack[80]=caller;c.EFlags=0x246;
        unsigned char xmm[256];memset(xmm,0x5a,sizeof(xmm));memcpy(&c.Xmm0,xmm,sizeof(xmm));
        for(unsigned i=0;i<test_tu_counts[which];i++) {
            const test_tu_operation *op=&test_tu_operations[which][i];if(op->end>pc) break;
            if(op->allocation) c.Rsp-=op->allocation;
            else {c.Rsp-=8;*(uint64_t*)(uintptr_t)c.Rsp=*test_tu_reg(&c,op->reg);*test_tu_reg(&c,op->reg)=0xdead;}
        }
        if(which==4 && pc>=9) {
            memcpy((void*)(uintptr_t)(c.Rsp+32),&c.Xmm6,16);memset(&c.Xmm6,0xa5,16);
        }
        c.Rip=(DWORD64)(uintptr_t)thunk+u->offset+pc;DWORD64 base=0,frame=0;PVOID handler=NULL;
        RUNTIME_FUNCTION *f=RtlLookupFunctionEntry(c.Rip,&base,NULL);
        assert(f==&thunk_unwind_functions[which] && base==(DWORD64)(uintptr_t)thunk);
        assert(!RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,c.Rip,f,&c,&handler,&frame,NULL));
        assert(c.Rip==caller && c.Rsp==(DWORD64)(uintptr_t)&stack[81] && c.EFlags==0x246);
        assert(!memcmp(&c.Xmm0,xmm,sizeof(xmm)));
        for(unsigned i=0;i<8;i++) assert(*test_tu_reg(&c,nonvol[i])==0x1111000000000000ULL+nonvol[i]);
    }
    DWORD64 base=0;
    assert(!RtlLookupFunctionEntry((DWORD64)(uintptr_t)thunk+u->offset+u->prefix_size+13,&base,NULL));
    assert(!RtlLookupFunctionEntry((DWORD64)(uintptr_t)thunk+u->offset+u->info_offset,&base,NULL));
}
typedef struct {uint64_t gp[16];unsigned char xmm[16][16];uint64_t flags,args[8];} test_tu_capture;
static void test_tu_bytes(unsigned char **p,const void *bytes,unsigned size) {memcpy(*p,bytes,size);*p+=size;}
static void test_tu_rip(unsigned char **p,unsigned rex,unsigned opcode,unsigned reg,const void *address) {
    *(*p)++=(unsigned char)rex;*(*p)++=(unsigned char)opcode;*(*p)++=(unsigned char)(5+((reg&7)<<3));
    intptr_t distance=(intptr_t)address-(intptr_t)(*p+4);assert(distance>=INT32_MIN && distance<=INT32_MAX);
    int32_t rel=(int32_t)distance;test_tu_bytes(p,&rel,4);
}
static void test_tu_xmm_rip(unsigned char **p,unsigned opcode,unsigned reg,const void *address) {
    *(*p)++=0xf3;if(reg>=8)*(*p)++=0x44;*(*p)++=0x0f;*(*p)++=(unsigned char)opcode;
    *(*p)++=(unsigned char)(5+((reg&7)<<3));intptr_t distance=(intptr_t)address-(intptr_t)(*p+4);
    assert(distance>=INT32_MIN && distance<=INT32_MAX);int32_t rel=(int32_t)distance;test_tu_bytes(p,&rel,4);
}
static void test_tu_xmm_stack(unsigned char **p,unsigned opcode,unsigned reg,unsigned offset) {
    *(*p)++=0xf3;if(reg>=8)*(*p)++=0x44;*(*p)++=0x0f;*(*p)++=(unsigned char)opcode;
    *(*p)++=(unsigned char)(0x84+((reg&7)<<3));*(*p)++=0x24;test_tu_bytes(p,&offset,4);
}
static void test_tu_mov_imm(unsigned char **p,unsigned reg,uint64_t value) {
    *(*p)++=(unsigned char)(0x48+(reg>=8));*(*p)++=(unsigned char)(0xb8+(reg&7));test_tu_bytes(p,&value,8);
}
static void test_tu_epilogue(unsigned char **p,unsigned which) {
    if(which==4) {
        const unsigned char restore[]={0x0f,0x28,0x74,0x24,0x20,0x48,0x83,0xc4,0x38,0xc3};
        test_tu_bytes(p,restore,sizeof(restore));return;
    }
    if(which==1 || which==3) {unsigned char add[]={0x48,0x83,0xc4,(unsigned char)(which==1?32:96)};test_tu_bytes(p,add,4);}
    for(unsigned i=test_tu_counts[which];i>0;i--) {
        const test_tu_operation *op=&test_tu_operations[which][i-1];if(op->allocation)continue;
        if(op->reg>=8)*(*p)++=0x41;*(*p)++=(unsigned char)(0x58+(op->reg&7));
    }
    *(*p)++=0xc3;
}
static void test_tu_executable_abi(unsigned which) {
    unsigned char *code=VirtualAlloc(NULL,8192,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(code);
    const thunk_recipe *r=thunk_recipe_at(thunk_unwind_recipes[which].offset);
    test_tu_capture *capture=(test_tu_capture*)(code+4096),samples[2];void **entry=(void**)(code+4608);
    unsigned char (*xmm)[16]=(unsigned char(*)[16])(code+4672);
    for(unsigned reg=0;reg<16;reg++)for(unsigned byte=0;byte<16;byte++)xmm[reg][byte]=(unsigned char)(reg*16+byte);
    unsigned char *p=code;
    test_tu_rip(&p,0x48,0x89,1,entry); /* Incoming RCX is the selected original. */
    const unsigned char pushes[]={0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xec,8,1,0,0};
    test_tu_bytes(&p,pushes,sizeof(pushes));
    for(unsigned reg=6;reg<16;reg++)test_tu_xmm_stack(&p,0x7f,reg,96+(reg-6)*16);
    for(unsigned arg=0;arg<8;arg++) {
        test_tu_mov_imm(&p,0,0x4444000000000000ULL+arg);
        const unsigned char store[]={0x48,0x89,0x44,0x24,(unsigned char)(arg*8)};test_tu_bytes(&p,store,sizeof(store));
    }
    for(unsigned reg=0;reg<16;reg++)if(reg!=4)test_tu_mov_imm(&p,reg,0x1111000000000000ULL+reg);
    for(unsigned reg=0;reg<16;reg++)test_tu_xmm_rip(&p,0x6f,reg,xmm[reg]);
    const unsigned char flags[]={0x68,2,2,0,0,0x9d};test_tu_bytes(&p,flags,sizeof(flags));
    *p++=0xff;*p++=0x15;int32_t rel=(int32_t)((unsigned char*)entry-(p+4));test_tu_bytes(&p,&rel,4);
    for(unsigned reg=6;reg<16;reg++)test_tu_xmm_stack(&p,0x6f,reg,96+(reg-6)*16);
    const unsigned char pops[]={0x48,0x81,0xc4,8,1,0,0,0x41,0x5f,0x41,0x5e,0x41,0x5d,0x41,0x5c,0x5f,0x5e,0x5b,0x5d,0xc3};
    test_tu_bytes(&p,pops,sizeof(pops));assert(p<code+1024);
    p=code+1024;test_tu_bytes(&p,r->prefix,r->prefix_size);
    for(unsigned reg=0;reg<16;reg++)test_tu_rip(&p,reg>=8?0x4c:0x48,0x89,reg,&capture->gp[reg]);
    for(unsigned reg=0;reg<16;reg++)test_tu_xmm_rip(&p,0x7f,reg,capture->xmm[reg]);
    *p++=0x9c;*p++=0x8f;*p++=0x05;rel=(int32_t)((unsigned char*)&capture->flags-(p+4));test_tu_bytes(&p,&rel,4);
    for(unsigned arg=0;arg<8;arg++) {
        const unsigned char load[]={0x48,0x8b,0x84,0x24};test_tu_bytes(&p,load,4);
        unsigned offset=test_tu_stack[which]+8+arg*8;test_tu_bytes(&p,&offset,4);test_tu_rip(&p,0x48,0x89,0,&capture->args[arg]);
    }
    test_tu_mov_imm(&p,0,0x1234567800000001ULL);test_tu_epilogue(&p,which);assert(p<code+2048);
    assert(thunk_emit_slot_at(code+2048,r,code+1024));
    /* Generated code is sealed separately; captures/argument slots remain RW. */
    DWORD old;assert(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old));assert(FlushInstructionCache(GetCurrentProcess(),code,4096));
    for(unsigned mode=0;mode<2;mode++) {
        memset(capture,0,sizeof(*capture));
        assert(((uint64_t(*)(void*))code)(code+(mode?2048:1024))==0x1234567800000001ULL);samples[mode]=*capture;
    }
    for(unsigned reg=0;reg<16;reg++)if(reg!=11)assert(samples[0].gp[reg]==samples[1].gp[reg]);
    assert(samples[0].gp[11]==0x111100000000000bULL && samples[1].gp[11]==(uintptr_t)(code+1024+r->prefix_size));
    assert(!memcmp(samples[0].xmm,samples[1].xmm,sizeof(samples[0].xmm)) && !memcmp(samples[0].xmm,xmm,256));
    assert(samples[0].flags==samples[1].flags && !memcmp(samples[0].args,samples[1].args,sizeof(samples[0].args)));
    for(unsigned arg=0;arg<8;arg++)assert(samples[0].args[arg]==0x4444000000000000ULL+arg);
    assert(VirtualFree(code,0,MEM_RELEASE));
}
static unsigned test_tu_add_calls;
static BOOLEAN WINAPI test_tu_refuse(PRUNTIME_FUNCTION f,DWORD count,DWORD64 base) {
    assert(count==1 && base==(DWORD64)(uintptr_t)thunk && f>=thunk_unwind_functions && f<thunk_unwind_functions+THUNK_UNWIND_COUNT);
    ++test_tu_add_calls;return FALSE;
}
static int test_tu_install(unsigned which) {
    switch(which) {case 0:return params_install();case 1:return attention_install(0,0);case 2:return damage_install();default:return solver_install();}
}
static void test_tu_restore(unsigned which) {
    switch(which) {case 0:params_restore();break;case 1:attention_restore();break;case 2:damage_restore();break;default:solver_restore();break;}
}
static HANDLE test_tu_entered,test_tu_release;
static unsigned test_tu_which;static volatile LONG test_tu_calls;
static void test_tu_pause_original(void) {
    InterlockedIncrement(&test_tu_calls);assert(SetEvent(test_tu_entered));assert(WaitForSingleObject(test_tu_release,5000)==WAIT_OBJECT_0);
}
static DWORD WINAPI test_tu_worker(LPVOID ignored) {
    (void)ignored;((void(*)(void))(thunk+thunk_unwind_recipes[test_tu_which].offset))();return 0;
}
static void test_tu_native_image(void) {
    unsigned char *prior=thunk;thunk=make_thunk();assert(thunk);
    unsigned char frozen[4096];memcpy(frozen,thunk,4096);test_tu_add_calls=0;
    for(unsigned i=0;i<4;i++) {
        const thunk_unwind_recipe *u=&thunk_unwind_recipes[i];const thunk_recipe *r=thunk_recipe_at(u->offset);DWORD64 base=0;
        assert(!thunk_unwind_bases[i] && !RtlLookupFunctionEntry((DWORD64)(uintptr_t)thunk+u->offset,&base,NULL));
        uint64_t native=*(uint64_t*)(image+r->entry);
        assert(!thunk_unwind_prepare_using(u->offset,test_tu_refuse) && test_tu_add_calls==i+1 && !thunk_unwind_bases[i]);
        assert(*(uint64_t*)(image+r->entry)==native && thunk_unwind_prepare(u->offset));
        assert(thunk_unwind_prepare_using(u->offset,test_tu_refuse) && test_tu_add_calls==i+1);
        test_tu_unwind_offsets(i);test_tu_executable_abi(i);
        for(unsigned n=0;n<2;n++) {
            DWORD old;unsigned at=u->offset+(n?u->info_offset:u->prefix_size);
            assert(VirtualProtect(thunk,4096,PAGE_READWRITE,&old));thunk[at]^=1;
            assert(VirtualProtect(thunk,4096,PAGE_EXECUTE_READ,&old));assert(!thunk_unwind_prepare(u->offset));
            assert(VirtualProtect(thunk,4096,PAGE_READWRITE,&old));thunk[at]^=1;
            assert(VirtualProtect(thunk,4096,PAGE_EXECUTE_READ,&old));assert(FlushInstructionCache(GetCurrentProcess(),thunk,4096));
            assert(thunk_unwind_prepare(u->offset));
        }
        RUNTIME_FUNCTION saved=thunk_unwind_functions[i];thunk_unwind_functions[i].EndAddress++;
        assert(!thunk_unwind_prepare(u->offset));thunk_unwind_functions[i]=saved;
    }
    unsigned char *base=thunk,*other=make_thunk();assert(other);thunk=other;
    for(unsigned i=0;i<4;i++)assert(!thunk_unwind_prepare(thunk_unwind_recipes[i].offset));
    assert(VirtualFree(other,0,MEM_RELEASE));thunk=base;
    assert(!thunk_unwind_prepare(0) && !thunk_unwind_prepare_using(320,NULL));
    assert(!stats.active && !boot_mode && !solver_installed && !params_installed && !attention_installed && !damage_installed);
    boot_mode=1;stats.active=1;stats.issued=1;boot_phase=3;ui_thread=GetCurrentThreadId();
    player_observer_enabled_tick=flight_installed=path_installed=1;path_options=1;boot_options=SOLVER_OPTION|1;
    image[ATTENTION_ENTRY+187]^=1;assert(!attention_install(0,0));image[ATTENTION_ENTRY+187]^=1;
    test_tu_entered=CreateEventW(NULL,TRUE,FALSE,NULL);test_tu_release=CreateEventW(NULL,TRUE,FALSE,NULL);assert(test_tu_entered && test_tu_release);
    for(unsigned i=0;i<4;i++) {
        const thunk_unwind_recipe *u=&thunk_unwind_recipes[i];const thunk_recipe *r=thunk_recipe_at(u->offset);
        unsigned char original[64];memcpy(original,image+r->entry,64);assert(test_tu_install(i));
        assert(thunk_unwind_bases[i]==(DWORD64)(uintptr_t)thunk);
        unsigned char *p=image+r->entry+r->prefix_size;DWORD old,ignored;assert(VirtualProtect(p,64,PAGE_EXECUTE_READWRITE,&old));
        unsigned extra=(i==0 || i==2)?40:0;
        if(extra) {const unsigned char sub[]={0x48,0x83,0xec,40};test_tu_bytes(&p,sub,4);}
        test_tu_mov_imm(&p,0,(uintptr_t)test_tu_pause_original);*p++=0xff;*p++=0xd0;
        if(extra) {const unsigned char add[]={0x48,0x83,0xc4,40};test_tu_bytes(&p,add,4);}
        test_tu_epilogue(&p,i);assert(p<image+r->entry+64);assert(FlushInstructionCache(GetCurrentProcess(),image+r->entry,64));
        /* The private native stand-in changes preserved bytes beyond the jump.
         * Record its exact owned CAS word; the original restoration word stays
         * the real image's bytes. No production patch or guard is relaxed. */
        if(i==0)memcpy(&params_patched_word,image+r->entry,16);
        else if(i==1)memcpy(&attention_patched_word,image+r->entry,8);
        else if(i==2)memcpy(&damage_patched_word,image+r->entry,8);
        else memcpy(&solver_patched_word,image+r->entry,8);
        test_tu_which=i;test_tu_calls=0;ResetEvent(test_tu_entered);ResetEvent(test_tu_release);
        HANDLE worker=CreateThread(NULL,0,test_tu_worker,NULL,0,NULL);assert(worker);
        assert(WaitForSingleObject(test_tu_entered,5000)==WAIT_OBJECT_0);test_tu_restore(i);
        assert(thunk_unwind_prepare(u->offset) && !memcmp(frozen,thunk,4096));
        assert(SetEvent(test_tu_release));assert(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0 && test_tu_calls==1);CloseHandle(worker);
        memcpy(image+r->entry,original,64);assert(FlushInstructionCache(GetCurrentProcess(),image+r->entry,64));
        assert(VirtualProtect(image+r->entry+r->prefix_size,64,old,&ignored));
        test_tu_unwind_offsets(i);
    }
    assert(params_restored && attention_restored && damage_restored && solver_restored);
    CloseHandle(test_tu_entered);CloseHandle(test_tu_release);
    assert(!params_swap(params_original_word^1,params_patched_word) && thunk_unwind_prepare(320));
    params_original_word=params_patched_word=0;params_restored=0;attention_original_word=attention_patched_word=0;attention_restored=0;
    damage_original_word=damage_patched_word=0;damage_restored=0;solver_original_word=solver_patched_word=0;solver_restored=0;solver_enabled_tick=0;
    boot_mode=0;stats.active=0;stats.issued=0;boot_phase=0;ui_thread=0;
    player_observer_enabled_tick=flight_installed=path_installed=0;path_options=0;boot_options=0;
    /* Test allocations alone are retired, after every worker has returned. */
    for(unsigned i=0;i<4;i++) {assert(RtlDeleteFunctionTable(&thunk_unwind_functions[i]));thunk_unwind_bases[i]=0;}
    assert(VirtualFree(thunk,0,MEM_RELEASE));thunk=prior;
    puts("four prefix-only unwind records: instruction progress, MOV/JMP, exact ABI, registration/tamper/base guards and STOP with open originals verified");
}
