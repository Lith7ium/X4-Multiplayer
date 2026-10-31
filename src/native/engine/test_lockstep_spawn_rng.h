/* native_gate_spawn_rng: the production Generate forwarder over synthetic
 * originals (keyed, observed, null/unreadable state, nesting, unwinding),
 * rows/status bounds, arm refusals and the entry-word CAS in owned memory;
 * with the X4 image, the pinned bodies and the static facts the option
 * relies on. No X4 code runs. */
static uint64_t test_spawn_tls,test_spawn_seen[4],test_spawn_states[2];
static unsigned test_spawn_mode;
static uint64_t *test_spawn_word(void) {return &test_spawn_tls;}
static uint64_t test_spawn_step(uint64_t s) {s=s*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;return (s>>30)|(s<<34);}
static uintptr_t test_spawn_generate(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,unsigned char e,uintptr_t f,uintptr_t g,
                                     unsigned h,unsigned char i,unsigned char j) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)h;(void)i;(void)j;
    test_spawn_seen[0]=test_spawn_tls;for(unsigned n=0;n<3;n++) test_spawn_tls=test_spawn_step(test_spawn_tls);
    if(test_spawn_mode==1) { /* a nested generation (e.g. a subordinate) with its own state */
        test_spawn_mode=0;creation_generate_execute(test_spawn_generate,0x1234,0,0,0,0,0,0,(uintptr_t)&test_spawn_states[1],0,0,0);
        test_spawn_seen[1]=test_spawn_tls;
    }
    if(test_spawn_mode==2) RaiseException(0xe04d5053,0,0,NULL);
    return g;
}
static void test_spawn_reset(void) {
    memset(spawn_rng_ring,0,sizeof(spawn_rng_ring));
    spawn_rng_calls=spawn_rng_keyed=spawn_rng_null=spawn_rng_unreadable=spawn_rng_failures=spawn_rng_cursor=spawn_rng_boot_calls=0;
}
static void test_spawn_rng(void) {
    uint64_t *(*prior)(void)=spawn_rng_word;spawn_rng_word=test_spawn_word;
    stats.active=1;boot_mode=1;boot_phase=3;stats.issued=21;ui_thread=GetCurrentThreadId();
    test_spawn_reset();spawn_rng_mode=SPAWN_RNG_KEY;InterlockedExchange(&spawn_rng_active,1);
    /* Keyed: the generation runs on lockstep_seed(domain, *state); the caller's word comes back. */
    test_spawn_states[0]=0x1122334455667788ULL;test_spawn_states[1]=0x99;test_spawn_tls=0xabcdef;test_spawn_mode=0;
    uint64_t key=lockstep_seed(SPAWN_RNG_DOMAIN,test_spawn_states[0],0,0,0,0);
    assert(creation_generate_execute(test_spawn_generate,0x1234,1,2,3,4,5,6,(uintptr_t)&test_spawn_states[0],8,9,10)==(uintptr_t)&test_spawn_states[0]);
    assert(test_spawn_seen[0]==key && test_spawn_tls==0xabcdef && spawn_rng_calls==1 && spawn_rng_keyed==1 && spawn_rng_cursor==1);
    spawn_rng_row_t *r=&spawn_rng_ring[0];
    assert(r->tick==21 && r->phase==3 && r->ui==1 && r->normal==1 && r->mode==SPAWN_RNG_KEY && r->state==test_spawn_states[0] &&
           r->key==key && r->caller==0xabcdef && r->entry==key && r->exit==test_spawn_step(test_spawn_step(test_spawn_step(key))) && r->draws==3);
    /* Another caller word gives the same generation: the word is not an input. */
    test_spawn_tls=0x5555;creation_generate_execute(test_spawn_generate,0x1234,0,0,0,0,0,0,(uintptr_t)&test_spawn_states[0],0,0,0);
    assert(test_spawn_seen[0]==key && test_spawn_tls==0x5555 && spawn_rng_ring[1].exit==r->exit);
    /* Null and unreadable state pointers keep the native word, counted. */
    test_spawn_tls=0x77;creation_generate_execute(test_spawn_generate,0x1234,0,0,0,0,0,0,0,0,0,0);
    uint64_t natural=test_spawn_step(test_spawn_step(test_spawn_step(0x77)));
    assert(test_spawn_seen[0]==0x77 && test_spawn_tls==natural && spawn_rng_null==1 && spawn_rng_cursor==2);
    test_spawn_tls=0x77;creation_generate_execute(test_spawn_generate,0x1234,0,0,0,0,0,0,(uintptr_t)8,0,0,0);
    assert(test_spawn_seen[0]==0x77 && spawn_rng_unreadable==1 && spawn_rng_cursor==2);
    /* Nesting: the inner state keys the inner generation; the outer stream resumes after it. */
    test_spawn_tls=0x42;test_spawn_mode=1;creation_generate_execute(test_spawn_generate,0x1234,0,0,0,0,0,0,(uintptr_t)&test_spawn_states[0],0,0,0);
    uint64_t outer=test_spawn_step(test_spawn_step(test_spawn_step(key)));
    assert(test_spawn_seen[0]==lockstep_seed(SPAWN_RNG_DOMAIN,0x99,0,0,0,0) && test_spawn_seen[1]==outer && test_spawn_tls==0x42);
    assert(spawn_rng_cursor==4 && spawn_rng_ring[2].state==0x99 && spawn_rng_ring[3].state==test_spawn_states[0] && spawn_rng_ring[3].exit==outer);
    /* Unwinding: the caller's word is restored and the scope counts as a failure. */
    test_spawn_tls=0x31;test_spawn_mode=2;int caught=0;
    __try {creation_generate_execute(test_spawn_generate,0x1234,0,0,0,0,0,0,(uintptr_t)&test_spawn_states[0],0,0,0);}
    __except(GetExceptionCode()==0xe04d5053?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=1;}
    assert(caught && test_spawn_tls==0x31 && spawn_rng_failures==1 && !spawn_rng_ring[4].normal);test_spawn_mode=0;spawn_rng_failures=0;
    /* Observe: the same key in the row, no write; boot-phase calls are counted. */
    test_spawn_reset();spawn_rng_mode=SPAWN_RNG_OBSERVE;boot_phase=2;stats.issued=0;test_spawn_tls=0x99aa;
    creation_generate_execute(test_spawn_generate,0x1234,0,0,0,0,0,0,(uintptr_t)&test_spawn_states[0],0,0,0);
    assert(test_spawn_seen[0]==0x99aa && test_spawn_tls==test_spawn_step(test_spawn_step(test_spawn_step(0x99aa))) && !spawn_rng_keyed);
    assert(spawn_rng_ring[0].key==key && spawn_rng_ring[0].entry==0x99aa && spawn_rng_ring[0].phase==2 && spawn_rng_boot_calls==1);
    /* Inactive: untouched, uncounted. */
    InterlockedExchange(&spawn_rng_active,0);test_spawn_tls=0x10;
    creation_generate_execute(test_spawn_generate,0x1234,0,0,0,0,0,0,(uintptr_t)&test_spawn_states[0],0,0,0);
    assert(test_spawn_seen[0]==0x10 && spawn_rng_calls==1);
    /* Rows and status stay bounded; evicted rows say so. */
    char row[384],status[512];
    assert(spawn_rng_row(0,row,sizeof(row))>0 && strstr(row,"index=0 tick=0 phase=2 ui=1 mode=0 normal=1 state=1122334455667788") &&
           strstr(row," draws=3") && !spawn_rng_row(1,row,sizeof(row)));
    memset(&spawn_rng_ring[0],0xff,sizeof(spawn_rng_ring[0]));spawn_rng_ring[0].draws=UINT32_MAX-1;
    int n=spawn_rng_row(0,row,sizeof(row));assert(n>0 && (unsigned)n+64<sizeof(row));printf("NATIVE_GATE SPAWN_RNG_ROW %s\n",row);
    spawn_rng_calls=spawn_rng_keyed=spawn_rng_null=spawn_rng_unreadable=spawn_rng_failures=spawn_rng_boot_calls=INT64_MAX;spawn_rng_cursor=INT64_MAX;
    n=spawn_rng_status(status,sizeof(status));assert(n>0 && (unsigned)n+48<sizeof(status));
    assert(spawn_rng_row(0,row,sizeof(row))>0 && strstr(row,"evicted=1"));
    test_spawn_reset();
    /* Arming refusals precede any write: phase, tick, thread, the creation
     * trace owning the entry, the pins on the placeholder image. */
    unsigned char *prior_thunk=thunk;boot_phase=0;stats.issued=0;
    boot_phase=1;assert(!spawn_rng_arm(SPAWN_RNG_KEY));boot_phase=0;
    stats.issued=1;assert(!spawn_rng_arm(SPAWN_RNG_KEY));stats.issued=0;
    ui_thread=GetCurrentThreadId()+1;assert(!spawn_rng_arm(SPAWN_RNG_KEY));ui_thread=GetCurrentThreadId();
    creation_started=1;assert(!spawn_rng_arm(SPAWN_RNG_KEY));creation_started=0;
    thunk=NULL;assert(!spawn_rng_arm(SPAWN_RNG_KEY));thunk=prior_thunk;
    if(!spawn_rng_pins()) assert(!spawn_rng_arm(SPAWN_RNG_KEY));
    assert(!spawn_rng_armed && !spawn_rng_installed);
    /* Entry-word CAS, ownership and restoration in owned memory. */
    static const unsigned char entry[8]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74};uint64_t original;
    unsigned char saved[8];memcpy(saved,image+SPAWN_RNG_ENTRY,8);memcpy(image+SPAWN_RNG_ENTRY,entry,8);memcpy(&original,entry,8);
    spawn_rng_original_word=original;spawn_rng_patched_word=original;((unsigned char*)&spawn_rng_patched_word)[0]=0xe9;
    assert(spawn_rng_swap(original,spawn_rng_patched_word) && !spawn_rng_swap(original,spawn_rng_patched_word));
    spawn_rng_installed=1;spawn_rng_armed=1;assert(spawn_rng_owned());
    image[SPAWN_RNG_ENTRY+7]^=1;assert(!spawn_rng_owned());
    spawn_rng_restore();assert(!spawn_rng_restored && spawn_rng_installed);
    image[SPAWN_RNG_ENTRY+7]^=1;spawn_rng_restore();
    assert(spawn_rng_restored && !spawn_rng_installed && !memcmp(image+SPAWN_RNG_ENTRY,entry,8) && spawn_rng_owned() && !spawn_rng_active);
    /* The creation trace refuses while the spawn option owns the entry. */
    spawn_rng_installed=1;assert(!creation_configure_using(CREATION_MANIFEST,sizeof(CREATION_MANIFEST)-1,creation_swap));spawn_rng_installed=0;
    memcpy(image+SPAWN_RNG_ENTRY,saved,8);spawn_rng_armed=spawn_rng_restored=0;spawn_rng_original_word=spawn_rng_patched_word=0;
    spawn_rng_word=prior;stats.active=0;boot_mode=0;boot_phase=0;stats.issued=0;
    puts("spawn RNG: keyed/observed generator state, caller word restored (nesting, unwinding), null/unreadable kept, rows/status bounds, arm refusals and entry CAS PASS");
}
/* The exact image: pinned bodies and the static facts the option uses. */
static void test_spawn_rng_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        assert(section.VirtualAddress<=nt.OptionalHeader.SizeOfImage && section.SizeOfRawData<=nt.OptionalHeader.SizeOfImage-section.VirtualAddress);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    assert(spawn_rng_pins());
    int32_t rel;
    /* Generate keeps its 7th argument (the explicit state) in r15 and passes it to InitializeShip. */
    assert(!memcmp(image+0xc841ea,"\x4c\x8b\xbd\xb0\x00\x00\x00",7) && !memcmp(image+0xc84502,"\x4c\x89\x7c\x24\x30",5));
    assert(image[0xc8452c]==0xe8);memcpy(&rel,image+0xc8452d,4);assert(0xc84531+rel==0xc82060);
    /* InitializeShip generates the loadout with 0x50e4a0 (state not passed) ... */
    assert(image[0xc82420]==0xe8);memcpy(&rel,image+0xc82421,4);assert(0xc82425+rel==0x50e4a0);
    assert(image[0xc82467]==0xe8);memcpy(&rel,image+0xc82468,4);assert(0xc8246c+rel==0x50e4a0);
    /* ... which draws from the calling thread's TLS word slot+0x318. */
    assert(!memcmp(image+0x50e531,"\x41\xbc\x18\x03\x00\x00",6) && !memcmp(image+0x50e537,"\x65\x48\x8b\x04\x25\x58\x00\x00\x00",9) &&
           !memcmp(image+0x50e543,"\x4d\x03\xe6",3));
    /* The job spawn passes job+0x528 (the per-job state) and steps it itself. */
    assert(!memcmp(image+0xf06701,"\x49\x8b\x85\x28\x05\x00\x00",7) && !memcmp(image+0xf06708,"\x48\x89\x44\x24\x30",5));
    assert(image[0xf06735]==0xe8);memcpy(&rel,image+0xf06736,4);assert(0xf0673a+rel==0xc841b0);
    /* JobDB seeds that state: FNV-1a of the job ID xor the seed argument. */
    assert(!memcmp(image+0xef8fe7,"\x48\x33\x85\x90\x00\x00\x00\x48\x89\x02",10) && !memcmp(image+0xef90ed,"\x49\x89\x94\x24\x28\x05\x00\x00",8));
    VirtualFree(image,0,MEM_RELEASE);image=prior;
    puts("spawn RNG native image: Generate/InitializeShip/loadout pins, explicit state into InitializeShip, TLS-word loadout, job state per job PASS");
}
