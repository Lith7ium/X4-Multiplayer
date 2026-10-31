/* Execute the actual generated mid-function stub against synthetic native
 * continuations. The helper destroys every volatile GP/XMM register, flags and
 * all four shadow slots. Both taken routes must restore the original machine
 * state; the nonfinished branch must never call the helper. */
static void test_npc_stub_abi(int ready) {
    unsigned char *code=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(code);
    size_t stub_size=ready?npc_ready_stub(code,320,code+512,code+1024):npc_deadline_stub(code,320,code+512,code+1024,code+1280,code+1536);assert(stub_size==(ready?144u:247u));
    unsigned char *p=code+512;
#define D_EMIT(...) do {const unsigned char bytes[]={__VA_ARGS__};memcpy(p,bytes,sizeof(bytes));p+=sizeof(bytes);} while(0)
#define D_WORD(value) do {uint32_t word=(uint32_t)(value);memcpy(p,&word,4);p+=4;} while(0)
#define D_QWORD(value) do {uint64_t word=(uint64_t)(uintptr_t)(value);memcpy(p,&word,8);p+=8;} while(0)
    D_EMIT(0xf2,0x0f,0x11,0x89);D_WORD(0x2d0);
    D_EMIT(0x48,0x8b,0x41,0x18,0x48,0x89,0x81);D_WORD(0x2d8);
    D_EMIT(0x48,0x89,0xe0,0x48,0x83,0xe0,0x0f,0x48,0x89,0x81);D_WORD(0x2b8);
    D_EMIT(0x48,0xff,0x81);D_WORD(0x2c0);
    for(unsigned offset=8;offset<=32;offset+=8) {D_EMIT(0x48,0xc7,0x44,0x24,offset);D_WORD(0xffffffff);}
    D_EMIT(0x0f,0xb6,0x81);D_WORD(0x2c8);D_EMIT(0x48,0x89,0x44,0x24,8);
    for(unsigned i=0;i<6;i++) D_EMIT(0x66,0x0f,0xef,0xc0+i*9);
    for(unsigned i=0;i<7;i++) {D_EMIT(i<3?0x48:0x49,0xb8+(i<3?i:i-3));D_QWORD(0xdead0000ULL+i);}
    D_EMIT(0x48,0x8b,0x44,0x24,8,0xc3);assert(p<=code+768);
    p=code+768;
    D_EMIT(0x57,0x48,0x8b,0xf9,0x4c,0x8b,0xd2,0x48,0x89,0xa7);D_WORD(0x2b0);
    for(unsigned i=0;i<6;i++) {D_EMIT(0xf3,0x0f,0x6f,0x87+(i<<3));D_WORD(0x300);}
    const uint64_t patterns[7]={0x1122334455667701ULL,0x1122334455667702ULL,0x1122334455667703ULL,
        0x1122334455667704ULL,0x1122334455667705ULL,0,0x1122334455667707ULL};
    for(unsigned i=0;i<7;i++) if(i!=5) {D_EMIT(i<3?0x48:0x49,0xb8+(i<3?i:i-3));D_QWORD(patterns[i]);}
    if(ready) D_EMIT(0x4d,0x39,0xd2);
    D_EMIT(0xff,0x25,0,0,0,0);D_QWORD(code);assert(p<=code+1024);
    for(unsigned route=0;route<3;route++) {
        p=code+1024+route*256;
        for(unsigned i=0;i<7;i++) {D_EMIT(i<3?0x48:0x4c,0x89,0x87+((i<3?i:i-3)<<3));D_WORD(0x200+i*8);}
        for(unsigned i=0;i<6;i++) {D_EMIT(0xf3,0x0f,0x7f,0x87+(i<<3));D_WORD(0x240+i*16);}
        D_EMIT(0x9c,0x8f,0x87);D_WORD(0x2a0);D_EMIT(0x48,0x89,0xa7);D_WORD(0x2a8);
        D_EMIT(0x5f,0xb8);D_WORD(route+1);D_EMIT(0xc3);assert(p<=code+1280+route*256);
    }
#undef D_QWORD
#undef D_WORD
#undef D_EMIT
    DWORD old;assert(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old));FlushInstructionCache(GetCurrentProcess(),code,4096);
    unsigned char self[1024],audio[0x140];
    for(unsigned route=0;route<(ready?1u:3u);route++) {
        memset(self,0,sizeof(self));memset(audio,0,sizeof(audio));
        for(unsigned i=0;i<16;i++) self[0x300+i]=(unsigned char)(i+1);
        self[0x2c8]=(unsigned char)(route==2);*(uint32_t*)(audio+0x128)=route?11:10;
        assert(((int(*)(void*,void*))(code+768))(self,audio)==(int)route+1);
        for(unsigned i=0;i<7;i++) assert(*(uint64_t*)(self+0x200+i*8)==(i==5?(uint64_t)(uintptr_t)audio:patterns[i]));
        for(unsigned i=0;i<6;i++) assert(!memcmp(self+0x240+i*16,self+0x300,16));
        assert((*(uint64_t*)(self+0x2a0)&0x8d5)==(ready || route?0x44:0x95));
        assert(*(uint64_t*)(self+0x2a8)==*(uint64_t*)(self+0x2b0));
        assert(*(uint64_t*)(self+0x2c0)==(ready || route?1u:0u));
        if(ready) {assert(!memcmp(self+0x18,self+0x300,8) && !memcmp(self+0x2d0,self+0x300,8));assert(!*(uint64_t*)(self+0x2d8));}
        if(ready || route) assert(*(uint64_t*)(self+0x2b8)==8);
    }
    VirtualFree(code,0,MEM_RELEASE);
}

static void test_npc_deadline_eligibility(void) {
    unsigned char self[0xa0]={0},audio[0x140]={0},before[0xa0];
    *(uint64_t*)(self+8)=123;*(uint32_t*)(self+0x10)=5;
    *(uint32_t*)(self+0x28)=100;*(uint32_t*)(self+0x2c)=200;
    *(double*)(self+0x18)=1.5;*(double*)(self+0x20)=2;
    *(double*)(audio+0x110)=1.5;*(double*)(image+0x6b60e48)=3;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && world_identity_record(123,456));
    npc_speaker *speaker=npc_speaker_find(self,123,1);assert(speaker);speaker->object=self;speaker->entity=123;
    speaker->active_enqueue=7;speaker->active_origin=9;speaker->active_valid=1;
    speaker->active_page=100;speaker->active_line=200;memcpy(&speaker->active_start,self+0x20,8);
    speaker->active_serial=speaker->ready_serial=1;speaker->active_start_event=speaker->ready_start_event=1;
    speaker->ready_count=1;memcpy(&speaker->ready_duration,self+0x18,8);
    memcpy(before,self,sizeof(before));
    assert(npc_deadline_route(self,audio)==1 && npc_deadline_count==1 && npc_deadlines[0].early && !interrupt_failures);
    assert(npc_deadlines[0].enqueue==7 && npc_deadlines[0].saved==456 && !memcmp(before,self,sizeof(before)));
    *(double*)(image+0x6b60e48)=4;
    assert(npc_deadline_route(self,audio)==1 && !npc_deadlines[1].early && !memcmp(before,self,sizeof(before)));
    /* Finite initial estimates alone do not qualify: actual ready duration must match. */
    *(double*)(audio+0x110)=1.25;assert(!npc_deadline_route(self,audio));*(double*)(audio+0x110)=1.5;
    *(double*)(self+0x18)=NAN;assert(!npc_deadline_route(self,audio));
    *(double*)(self+0x18)=*(double*)(audio+0x110)=INFINITY;assert(!npc_deadline_route(self,audio));
    *(double*)(self+0x18)=*(double*)(audio+0x110)=0;assert(!npc_deadline_route(self,audio));
    *(double*)(self+0x18)=*(double*)(audio+0x110)=1.5;
    for(unsigned i=0;i<3;i++) {self[0x90+i]=1;assert(!npc_deadline_route(self,audio));self[0x90+i]=0;}
    *(uint32_t*)(self+0x10)=8;assert(!npc_deadline_route(self,audio));*(uint32_t*)(self+0x10)=5;
    *(uint32_t*)(self+0x2c)=201;assert(!npc_deadline_route(self,audio));*(uint32_t*)(self+0x2c)=200;
    speaker->active_valid=0;assert(!npc_deadline_route(self,audio));speaker->active_valid=1;
    *(double*)(image+0x6b60e48)=1;assert(!npc_deadline_route(self,audio));
    assert(interrupt_failures==11 && npc_deadline_count==2 && !memcmp(before,self,sizeof(before)));
    speaker->active_enqueue=0;assert(!npc_deadline_route(self,audio) && npc_deadline_foreign==1 && interrupt_failures==11);
    *(uint64_t*)(self+8)=999;assert(!npc_deadline_route(self,audio) && npc_deadline_foreign==2);
    memset(npc_speakers,0,sizeof(npc_speakers));npc_deadline_count=0;npc_deadline_calls=npc_deadline_seen=npc_deadline_foreign=0;interrupt_failures=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;
    npc_deadline_original=123;npc_deadline_patched=456;*(unsigned __int128*)(image+NPC_DEADLINE_WORD)=123;
    assert(npc_deadline_swap(123,456));npc_deadline_installed=1;
    *(unsigned __int128*)(image+NPC_DEADLINE_WORD)=789;assert(!npc_deadline_restore() && npc_deadline_installed);
    *(unsigned __int128*)(image+NPC_DEADLINE_WORD)=456;assert(npc_deadline_restore() && !npc_deadline_installed);
}

static unsigned deadline_test_completions,deadline_test_restart,deadline_test_interrupted;
static uint64_t deadline_test_rng;
static void deadline_test_start(void *self) {
    *(uint32_t*)((unsigned char*)self+0x10)=5;*(double*)((unsigned char*)self+0x20)=3.0;
}
static void deadline_test_finish(void *self,unsigned char interrupted) {
    ++deadline_test_completions;deadline_test_interrupted=interrupted;
    *(uint32_t*)((unsigned char*)self+0x10)=7;
    if(deadline_test_restart) npc_speak_observe(deadline_test_start,self,&deadline_test_rng,0x562575);
}
static void test_npc_completion_lifetime(void) {
    unsigned char self[0xa0]={0},audio[0x80]={0};audio[0x78]=1;*(void**)(image+0x6cf15b8)=audio;
    *(uint64_t*)(self+8)=123;*(uint32_t*)(self+0x10)=5;*(double*)(self+0x18)=1.5;*(double*)(self+0x20)=2;
    *(uint32_t*)(self+0x28)=100;*(uint32_t*)(self+0x2c)=200;*(double*)(image+0x6b60e48)=3;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && world_identity_record(123,456));
    npc_speaker *speaker=npc_speaker_find(self,123,1);assert(speaker);speaker->object=self;speaker->entity=123;
    speaker->enqueue=8;speaker->origin=10;speaker->active_enqueue=7;speaker->active_origin=9;speaker->active_valid=1;
    npc_finish_observe(deadline_test_finish,self,1,&deadline_test_rng,0x564265);
    assert(deadline_test_completions==1 && deadline_test_interrupted==1 && !speaker->active_enqueue && !speaker->active_valid);
    assert(npc_finishes[0].enqueue==7 && npc_finishes[0].origin==9 && npc_finishes[0].interrupted==1);
    /* A synchronous native next-line callback owns its fresh active identity;
     * outer completion must not retire or relabel that replacement. */
    speaker->active_enqueue=7;speaker->active_origin=9;speaker->active_valid=1;deadline_test_restart=1;
    npc_finish_observe(deadline_test_finish,self,0,&deadline_test_rng,0x564119);
    assert(deadline_test_completions==2 && !deadline_test_interrupted && speaker->active_enqueue==8 && speaker->active_valid);
    assert(npc_finishes[1].enqueue==7 && npc_speaks[0].enqueue==8 && npc_speaks[0].origin==10 && !interrupt_failures);
    memset(npc_speakers,0,sizeof(npc_speakers));npc_finish_count=0;npc_finish_calls=0;npc_finish_seen=npc_finish_unowned=npc_finish_foreign_thread=0;
    npc_speak_count=0;npc_speak_calls=0;npc_speak_seen=npc_speak_unowned=npc_speak_foreign_thread=0;*(void**)(image+0x6cf15b8)=NULL;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;
}

static void test_npc_ready_lifetime(void) {
    unsigned char self[0xa0]={0},before[0xa0];*(uint64_t*)(self+8)=123;
    *(uint32_t*)(self+0x10)=3;*(uint32_t*)(self+0x28)=100;*(uint32_t*)(self+0x2c)=200;
    *(double*)(self+0x18)=2.84;*(double*)(self+0x20)=2;*(double*)(image+0x6b60e48)=3;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && world_identity_record(123,456));
    npc_speaker *speaker=npc_speaker_find(self,123,1);assert(speaker);speaker->object=self;speaker->entity=123;
    speaker->active_enqueue=1;speaker->active_origin=2;speaker->active_valid=1;speaker->active_serial=1;speaker->active_start_event=1;
    speaker->active_page=100;speaker->active_line=200;memcpy(&speaker->active_start,self+0x20,8);memcpy(before,self,sizeof(before));
    npc_ready_observe(self,1.5);npc_ready_observe(self,1.5);
    assert(npc_ready_count==2 && speaker->ready_count==2 && !interrupt_failures && !memcmp(before,self,sizeof(before)));
    assert(npc_ready_samples[0].repeat==1 && npc_ready_samples[1].repeat==2 && npc_ready_samples[0].estimate!=npc_ready_samples[0].duration);
    npc_ready_observe(self,1.6);assert(interrupt_failures==1 && npc_ready_count==2);
    speaker->active_serial=2;npc_ready_observe(self,1.5);assert(interrupt_failures==2 && npc_ready_count==2);
    speaker->ready_count=0;speaker->active_start_event=2;speaker->active_enqueue=2;
    npc_ready_observe(self,1.7);assert(npc_ready_count==3 && npc_ready_samples[2].repeat==1 && npc_ready_samples[2].generation==2);
    npc_ready_observe(self,NAN);npc_ready_observe(self,0);assert(interrupt_failures==4 && npc_ready_count==3);
    *(uint32_t*)(self+0x10)=8;npc_ready_observe(self,1.7);assert(interrupt_failures==5);
    speaker->active_enqueue=0;npc_ready_observe(self,1.7);assert(npc_ready_foreign==1 && interrupt_failures==5);
    memset(npc_speakers,0,sizeof(npc_speakers));npc_ready_count=0;npc_ready_calls=npc_ready_seen=npc_ready_foreign=0;interrupt_failures=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;
    npc_ready_original=123;npc_ready_patched=456;*(unsigned __int128*)(image+NPC_READY_WORD)=123;
    assert(npc_ready_swap(123,456));npc_ready_installed=1;
    *(unsigned __int128*)(image+NPC_READY_WORD)=789;assert(!npc_ready_restore() && npc_ready_installed);
    *(unsigned __int128*)(image+NPC_READY_WORD)=456;assert(npc_ready_restore() && !npc_ready_installed);
}
