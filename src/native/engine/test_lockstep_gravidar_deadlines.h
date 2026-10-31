/* Offline tests for the Gravidar factor-pass and scan deadlines
 * (native_gate_gravidar_deadlines): first-Move re-base, keyed pass draws
 * independent of the natural word, restore (also on unwind), high/low
 * cadence, unmapped/field-less/thread/nested/inactive pass-through, failure
 * paths, the unit re-base at admission (active units only, under their lock),
 * the vtable slot CAS, arming refusals and bounded status. Synthetic sectors
 * and a TLS block live in owned memory; the stand-in Move runs the pass like
 * U::Sector::Move: due < now -> due = now + 1 (attention >= 5) or now + 5 + a
 * jitter from the TLS word. */
static __declspec(align(16)) unsigned char test_gdl_tls[0x400],test_gdl_sector[0x500];
static uint64_t test_gdl_seen;
static unsigned test_gdl_calls,test_gdl_extra;
static int test_gdl_throw,test_gdl_flip;
static double test_gdl_get(unsigned offset) {double v;memcpy(&v,test_gdl_sector+offset,8);return v;}
static void test_gdl_set(unsigned offset,double v) {memcpy(test_gdl_sector+offset,&v,8);}
static void test_gdl_move(void *self) {
    unsigned char *sector=self;test_gdl_seen=*test_sr_word;++test_gdl_calls;
    double due,now;int32_t attention;memcpy(&due,sector+GDL_DUE,8);memcpy(&now,test_gdl_tls+0x300,8);
    if(test_gdl_flip) {int32_t low=2;memcpy(sector+GDL_ATTENTION,&low,4);} /* dropped during the child Moves */
    memcpy(&attention,sector+GDL_ATTENTION,4);
    if(sector[GDL_FIELD] && !(due>=now)) {
        double next=now+1.0;
        if(attention<5) {test_sr_draw(1);next=now+5.0+(double)(*test_sr_word>>40)*(1.0/16777216.0);}
        memcpy(sector+GDL_DUE,&next,8);
    }
    test_sr_draw(test_gdl_extra);
    if(test_gdl_throw) RaiseException(0xe0475249,0,0,NULL);
}
static void test_gdl_call(uint64_t word) {
    memcpy(test_gdl_tls+0x318,&word,8);test_sr_word=(uint64_t*)(test_gdl_tls+0x318);gdl_run(test_gdl_move,test_gdl_tls,test_gdl_sector);
}
static uint64_t test_gdl_word(void) {uint64_t w;memcpy(&w,test_gdl_tls+0x318,8);return w;}
static void test_gdl_layout(uint64_t runtime,unsigned char field,double due,int32_t attention,double now) {
    memset(test_gdl_sector,0,sizeof(test_gdl_sector));memcpy(test_gdl_sector+8,&runtime,8);test_gdl_sector[GDL_FIELD]=field;
    test_gdl_set(GDL_DUE,due);memcpy(test_gdl_sector+GDL_ATTENTION,&attention,4);
    memset(test_gdl_tls,0,sizeof(test_gdl_tls));test_gdl_tls[0x10]=1;memcpy(test_gdl_tls+0x300,&now,8);
}
static void test_gdl_reset(void) {
    memset(gdl_seen,0,sizeof(gdl_seen));gdl_seen_count=0;memset(&gdl_now,0,sizeof(gdl_now));memset(&gdl_last,0,sizeof(gdl_last));
    gdl_depth=0;gdl_calls=gdl_passed=gdl_threads=gdl_nested=gdl_fields=gdl_first=gdl_rebased=gdl_due=0;
    gdl_keyed=gdl_unmapped=gdl_native=gdl_unscoped=gdl_passes=gdl_low=gdl_high=gdl_failures=0;gdl_stage="idle";
    test_gdl_calls=0;test_gdl_extra=0;test_gdl_throw=0;test_gdl_flip=0;script_rng_cache_flush();
}
/* Unit table for the admission walk: bucket 5 holds three units. */
typedef struct {void *parent,*left,*right;uint64_t pad,key;void *unit;} test_gdl_node;
static __declspec(align(16)) unsigned char test_gdl_units[3][0x100];
static test_gdl_node test_gdl_nodes[3];
static void test_gdl_unit(unsigned i,uintptr_t vtable,double a8,double b0,double b8) {
    memset(test_gdl_units[i],0,0x100);memcpy(test_gdl_units[i],&vtable,8);
    double t[3]={a8,b0,b8};memcpy(test_gdl_units[i]+0xa8,t,24);
    InitializeCriticalSection((CRITICAL_SECTION*)(test_gdl_units[i]+0x18));
    memset(&test_gdl_nodes[i],0,sizeof(test_gdl_nodes[i]));test_gdl_nodes[i].unit=test_gdl_units[i];
}
static void test_gravidar_deadlines_admit(void) {
    unsigned char *worker=VirtualAlloc(NULL,0x7000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(worker);
    void *vt=image+GDIG_VTABLE;memcpy(worker,&vt,8);worker[0x6988]=1;
    InitializeCriticalSection((CRITICAL_SECTION*)(worker+0x6960));
    for(unsigned b=0;b<GDIG_BUCKETS;b++) InitializeCriticalSection((CRITICAL_SECTION*)(worker+0x88+b*0x28));
    void *prior=*(void**)(image+GDIG_WORKER);*(void**)(image+GDIG_WORKER)=worker;
    /* Active unit applied at load time, active unit in constructor state, passive unit
     * whose +0xa8.. lies beyond its 0x68 bytes (here: sentinel bytes that must survive). */
    test_gdl_unit(0,(uintptr_t)(image+GDIG_ACTIVE),7.083,7.9,7.083);
    test_gdl_unit(1,(uintptr_t)(image+GDIG_ACTIVE),-1.0,-1.0,-1.0);
    test_gdl_unit(2,(uintptr_t)(image+0x2c67660),0,0,0);memset(test_gdl_units[2]+0xa8,0x5a,24);
    test_gdl_nodes[0].left=&test_gdl_nodes[1];test_gdl_nodes[0].right=&test_gdl_nodes[2];*(void**)(worker+0x2890+5*0x40+8)=&test_gdl_nodes[0];
    gdl_armed=1;gdl_admitted=0;
    /* Busy or foreign worker: refused, nothing written. */
    worker[0x6988]=0;assert(!gdl_admit() && !strcmp(gdl_stage,"busy") && !gdl_admitted);worker[0x6988]=1;
    void *bad=image+1;memcpy(worker,&bad,8);assert(!gdl_admit() && !strcmp(gdl_stage,"worker"));memcpy(worker,&vt,8);
    double t;memcpy(&t,test_gdl_units[0]+0xb0,8);assert(t==7.9);
    assert(gdl_admit() && gdl_admitted && !strcmp(gdl_stage,"admitted") && gdl_units_active==2 && gdl_units_rebased==1);
    double unset[3]={-1.0,-1.0,-1.0};unsigned char sentinel[24];memset(sentinel,0x5a,24);
    assert(!memcmp(test_gdl_units[0]+0xa8,unset,24) && !memcmp(test_gdl_units[1]+0xa8,unset,24) && !memcmp(test_gdl_units[2]+0xa8,sentinel,24));
    /* Once per run; unarmed is a no-op. */
    memcpy(test_gdl_units[0]+0xb0,&(double){5.0},8);assert(gdl_admit());memcpy(&t,test_gdl_units[0]+0xb0,8);assert(t==5.0);
    gdl_armed=0;gdl_admitted=0;assert(gdl_admit() && !gdl_admitted);gdl_armed=1;
    /* An unreadable node refuses. */
    test_gdl_nodes[1].left=(void*)8;assert(!gdl_admit() && !strcmp(gdl_stage,"units") && !gdl_admitted);test_gdl_nodes[1].left=NULL;
    gdl_armed=0;gdl_admitted=0;gdl_units_active=gdl_units_rebased=0;
    for(unsigned i=0;i<3;i++) DeleteCriticalSection((CRITICAL_SECTION*)(test_gdl_units[i]+0x18));
    for(unsigned b=0;b<GDIG_BUCKETS;b++) DeleteCriticalSection((CRITICAL_SECTION*)(worker+0x88+b*0x28));
    DeleteCriticalSection((CRITICAL_SECTION*)(worker+0x6960));
    *(void**)(image+GDIG_WORKER)=prior;VirtualFree(worker,0,MEM_RELEASE);
}
static void test_gravidar_deadlines(void) {
    world_identity_entry *prior=world_identities;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && world_identity_record(0x9101,0x7a5));
    stats.active=1;boot_mode=1;boot_phase=3;stats.issued=1;ui_thread=GetCurrentThreadId();
    gdl_armed=1;InterlockedExchange(&gdl_active,1);test_gdl_reset();
    const double now=7.0997;uint64_t k1=lockstep_seed(GDL_DOMAIN,1,0x7a5,0,0,0);

    /* First admitted Move: the load-time deadline goes to 0, the pass runs this
     * frame on the keyed word, the natural word comes back unchanged. */
    test_gdl_layout(0x9101,1,12.4,2,now);test_gdl_call(0x1111);
    double due=test_gdl_get(GDL_DUE);
    assert(test_gdl_seen==k1 && test_gdl_word()==0x1111 && test_gdl_calls==1 && gdl_first==1 && gdl_rebased==1 && gdl_due==1 &&
           gdl_keyed==1 && gdl_passes==1 && gdl_low==1 && !gdl_high && !gdl_failures && due>=now+5.0 && due<now+6.0);
    assert(gdl_now.tick==1 && gdl_now.passes==1 && gdl_now.keyed==1 && gdl_seen_count==1);
    uint64_t bits;memcpy(&bits,&due,8);assert(gdl_now.dueset==lockstep_mix(0x7a5^lockstep_mix(bits)));
    /* Another process: another load-time deadline and natural word, the same pass. */
    test_gdl_reset();test_gdl_layout(0x9101,1,12.9,2,now);test_gdl_call(0xdeadbeef);
    assert(test_gdl_seen==k1 && test_gdl_word()==0xdeadbeef && test_gdl_get(GDL_DUE)==due && gdl_rebased==1);
    /* Later Moves: not due, native (no scope, no re-base); due again on a later tick, keyed by that tick. */
    test_gdl_call(0x2222);assert(test_gdl_seen==0x2222 && gdl_due==1 && gdl_first==1 && gdl_rebased==1 && test_gdl_get(GDL_DUE)==due);
    stats.issued=400;memcpy(test_gdl_tls+0x300,&(double){due+0.001},8);test_gdl_call(0x3333);
    assert(test_gdl_seen==lockstep_seed(GDL_DOMAIN,400,0x7a5,0,0,0) && test_gdl_word()==0x3333 && gdl_passes==2 &&
           gdl_last.tick==1 && gdl_now.tick==400 && gdl_now.passes==1);
    /* High attention: the children's Moves run first and the pass takes now + 1 s
     * without a draw, natively: no scope, the natural word reaches the Move. */
    int32_t level=7;memcpy(test_gdl_sector+GDL_ATTENTION,&level,4);stats.issued=500;
    double later=due+30.0;memcpy(test_gdl_tls+0x300,&later,8);test_gdl_set(GDL_DUE,later-0.5);test_gdl_call(0x4444);
    assert(test_gdl_get(GDL_DUE)==later+1.0 && gdl_high==1 && test_gdl_seen==0x4444 && test_gdl_word()==0x4444 && gdl_keyed==2 &&
           gdl_native==1 && !gdl_unscoped && gdl_now.passes==1 && !gdl_now.keyed);
    /* An exactly-due deadline does not pass (comisd/jae), like the native test. */
    test_gdl_set(GDL_DUE,later);test_gdl_call(0x5555);assert(test_gdl_seen==0x5555 && gdl_due==3);
    /* Attention dropping below 5 inside a native Move: the low pass draws natively, counted. */
    test_gdl_set(GDL_DUE,later-0.5);test_gdl_flip=1;test_gdl_call(0x6666);test_gdl_flip=0;
    assert(test_gdl_seen==0x6666 && test_gdl_word()!=0x6666 && gdl_native==2 && gdl_unscoped==1 && gdl_low==3 && !gdl_failures);
    stats.issued=1;

    /* A field-less sector is never touched or recorded. */
    test_gdl_reset();test_gdl_layout(0x9101,0,12.4,2,now);test_gdl_call(0x1111);
    assert(test_gdl_seen==0x1111 && test_gdl_get(GDL_DUE)==12.4 && !gdl_fields && !gdl_seen_count && !gdl_failures);
    /* Unmapped field sector: re-based, natural draw, counted. */
    test_gdl_reset();test_gdl_layout(0x9999,1,12.4,2,now);test_gdl_call(0x1111);
    assert(test_gdl_seen==0x1111 && test_gdl_word()!=0x1111 && gdl_rebased==1 && gdl_unmapped==1 && !gdl_keyed && gdl_passes==1);
    /* Pass-through: inactive, stopped gate, boot phase 2, nested, another thread. */
    test_gdl_reset();test_gdl_layout(0x9101,1,12.4,2,now);InterlockedExchange(&gdl_active,0);test_gdl_call(5);
    assert(test_gdl_seen==5 && gdl_passed==1 && test_gdl_get(GDL_DUE)==12.4);InterlockedExchange(&gdl_active,1);
    test_gdl_layout(0x9101,1,12.4,2,now);
    stats.active=0;test_gdl_call(5);assert(test_gdl_seen==5 && gdl_passed==2);stats.active=1;
    boot_phase=2;test_gdl_call(5);assert(test_gdl_seen==5 && gdl_passed==3);boot_phase=3;
    gdl_depth=1;test_gdl_call(5);assert(test_gdl_seen==5 && gdl_nested==1);gdl_depth=0;
    ui_thread=GetCurrentThreadId()+1;test_gdl_call(5);assert(test_gdl_seen==5 && gdl_threads==1);ui_thread=GetCurrentThreadId();
    assert(!gdl_fields && !gdl_seen_count && !gdl_failures && test_gdl_get(GDL_DUE)==12.4);

    /* Failures run the original natively and are counted (reason 121). */
    test_gdl_reset();test_gdl_layout(0x9101,1,12.4,2,now);test_sr_word=(uint64_t*)(test_gdl_tls+0x318);
    gdl_run(test_gdl_move,NULL,test_gdl_sector);assert(test_gdl_calls==1 && gdl_failures==1 && !strcmp(gdl_stage,"read"));
    test_gdl_tls[0x10]=0;test_gdl_call(5);assert(test_gdl_calls==2 && gdl_failures==2);test_gdl_tls[0x10]=1;
    memset(test_gdl_sector+8,0,8);test_gdl_call(5);assert(test_gdl_calls==3 && gdl_failures==3);
    test_gdl_layout(0x9101,1,12.4,2,now);
    for(unsigned i=0;i<GDL_TABLE;i++) gdl_seen[i]=0x100000+i;gdl_seen_count=GDL_TABLE;
    test_gdl_call(5);assert(test_gdl_calls==4 && gdl_failures==4 && !strcmp(gdl_stage,"table") && test_gdl_get(GDL_DUE)==12.4);
    /* Unwinding restores the word and the depth and counts a failure. */
    test_gdl_reset();test_gdl_layout(0x9101,1,12.4,2,now);test_gdl_throw=1;int caught=0;
    __try {test_gdl_call(0x99);}
    __except(GetExceptionCode()==0xe0475249?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=1;}
    assert(caught && test_gdl_word()==0x99 && test_gdl_seen==k1 && gdl_failures==1 && !gdl_depth && !strcmp(gdl_stage,"unwind"));
    test_gdl_throw=0;
    /* Extra draws inside the scope never reach the caller's word. */
    test_gdl_reset();test_gdl_layout(0x9101,1,12.4,2,now);test_gdl_extra=17;test_gdl_call(0x77);assert(test_gdl_word()==0x77 && test_gdl_seen==k1);

    test_gravidar_deadlines_admit();

    /* Slot CAS: only our exact pointer is restored. Arming refuses before any
     * write while the pins fail (placeholder image) or a precondition is missing. */
    test_gdl_reset();gdl_armed=0;gdl_installed=0;gdl_restored=0;InterlockedExchange(&gdl_active,0);
    void **slot=(void**)(image+GDL_SLOT);*slot=image+GDL_MOVE;
    boot_phase=0;stats.issued=0;world_identity_installed=1;LONG prior_sync=gravidar_time_sync;InterlockedExchange(&gravidar_time_sync,1);
    assert(!gdl_arm() && *slot==image+GDL_MOVE && !gdl_armed && !gdl_active && !strcmp(gdl_stage,"pins"));
    InterlockedExchange(&gravidar_time_sync,0);assert(!gdl_arm());InterlockedExchange(&gravidar_time_sync,1);
    world_identity_installed=0;assert(!gdl_arm());world_identity_installed=1;
    stats.active=0;assert(!gdl_arm());stats.active=1;
    boot_mode=0;assert(!gdl_arm());boot_mode=1;
    boot_phase=2;assert(!gdl_arm());boot_phase=0;
    stats.issued=1;assert(!gdl_arm());stats.issued=0;
    ui_thread=GetCurrentThreadId()+1;assert(!gdl_arm());ui_thread=GetCurrentThreadId();
    assert(*slot==image+GDL_MOVE);
    assert(script_rng_slot_swap(GDL_SLOT,image+GDL_MOVE,(void*)gdl_move));
    gdl_installed=gdl_armed=1;InterlockedExchange(&gdl_active,1);
    assert(gdl_owned() && gdl_arm());
    *slot=image+1;assert(!gdl_owned());
    gdl_restore();assert(!gdl_restored && gdl_installed && !gdl_active && *slot==image+1);
    *slot=(void*)gdl_move;gdl_restore();
    assert(gdl_restored && !gdl_installed && *slot==image+GDL_MOVE && gdl_owned());
    /* A stopped hook passes through. */
    boot_phase=3;test_gdl_layout(0x9101,1,12.4,2,now);test_gdl_call(5);assert(test_gdl_seen==5 && gdl_passed==1 && test_gdl_get(GDL_DUE)==12.4);
    *slot=NULL;world_identity_installed=0;InterlockedExchange(&gravidar_time_sync,prior_sync);

    /* Bounded status. */
    char status[1536];assert(gdl_status(status,sizeof(status))>0 &&
        strstr(status,"armed=1 installed=0 active=0 restored=1 calls=1 passed=1 ") && strstr(status," stage=pins"));
    printf("NATIVE_GATE GRAVIDAR_DEADLINES tick=1 %s\n",status);
    gdl_armed=gdl_installed=gdl_restored=gdl_admitted=INT32_MIN;InterlockedExchange(&gdl_active,(LONG)(-2147483647-1));
    gdl_calls=gdl_passed=gdl_threads=gdl_nested=gdl_fields=gdl_first=gdl_rebased=gdl_due=INT64_MIN;
    gdl_keyed=gdl_unmapped=gdl_native=gdl_unscoped=gdl_passes=gdl_low=gdl_high=gdl_failures=INT64_MIN;gdl_seen_count=UINT32_MAX;
    gdl_units_active=gdl_units_rebased=UINT64_MAX;gdl_stage="admitted";
    memset(&gdl_now,0xff,sizeof(gdl_now));memset(&gdl_last,0xff,sizeof(gdl_last));
    int n=gdl_status(status,sizeof(status));
    assert(n>0 && (unsigned)n+strlen("[X4LAN] NATIVE_GATE GRAVIDAR_DEADLINES tick=18446744073709551615 \n")<sizeof(status));
    gdl_armed=gdl_installed=gdl_restored=gdl_admitted=0;InterlockedExchange(&gdl_active,0);test_gdl_reset();
    gdl_units_active=gdl_units_rebased=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=0;script_rng_cache_flush();
    stats.active=0;boot_mode=0;boot_phase=0;stats.issued=0;
    puts("Gravidar factor-pass re-base/keyed draw/restore/cadence, unit re-base at admission, slot CAS and status tests PASS");
}
/* With the X4 9.00 executable: Sector::Move's field/deadline/attention test,
 * the FloatState draw and both deadline stores, the constructor's 0 deadline,
 * the unit constructor's -1 times and both unit sizes, the apply's time
 * stores, the relocated slot, tamper refusals and a full arm/restore. */
static void test_gdl_native_image(const char *path) {
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
    /* The file holds preferred-base pointers; apply the relocation the loader would. */
    assert(GDL_SLOT==GDIG_SECTOR+674u*8u && *(uint64_t*)(image+GDL_SLOT)==nt.OptionalHeader.ImageBase+GDL_MOVE);
    assert(*(uint64_t*)(image+GDIG_ACTIVE+7*8)==nt.OptionalHeader.ImageBase+GRAVIDAR_UNIT_APPLY);
    *(void**)(image+GDL_SLOT)=image+GDL_MOVE;
    assert(gdl_pinned());
    /* Sector::Move: field byte, deadline vs TLS time (r15d=0x300 off the TLS block), attention 5,
     * high store now + 1.0, low: ecx=0x318 -> Random::FloatState 0x14ddc90, now + 5.0 + draw. */
    assert(!memcmp(image+0x784250,"\x80\xbf\x42\x04\x00\x00\x00",7) && !memcmp(image+0x784293,"\xf2\x0f\x10\x87\x48\x04\x00\x00",8));
    assert(!memcmp(image+0x78429b,"\x41\xbf\x00\x03\x00\x00",6) && !memcmp(image+0x7842a1,"\x66\x43\x0f\x2f\x04\x37",6) && image[0x7842a7]==0x0f && image[0x7842a8]==0x83);
    assert(!memcmp(image+0x784485,"\x83\xbf\xec\x01\x00\x00\x05",7) && !memcmp(image+0x7844aa,"\xf2\x0f\x11\x87\x48\x04\x00\x00",8));
    assert(!memcmp(image+0x7844c5,"\xb9\x18\x03\x00\x00",5) && !memcmp(image+0x7844e8,"\xf2\x0f\x11\x8f\x48\x04\x00\x00",8));
    int32_t rel;memcpy(&rel,image+0x7844ce,4);assert(image[0x7844cd]==0xe8 && 0x7844d2+rel==0x14ddc90);
    double one,five;float range;memcpy(&one,image+0x2cbe870,8);memcpy(&five,image+0x2cbe990,8);memcpy(&range,image+0x2cbe5d4,4);
    assert(one==1.0 && five==5.0 && range==1.0f);
    /* Sector constructor 0x781e60: +0x442 = 0 and +0x448 = 0 (r12 zeroed). */
    assert(!memcmp(image+0x781ec0,"\x45\x33\xe4",3) && !memcmp(image+0x781f11,"\x45\x88\xa7\x42\x04\x00\x00",7) &&
           !memcmp(image+0x781f18,"\x4d\x89\xa7\x48\x04\x00\x00",7));
    /* ActiveGravidarUnit ctor: -1.0 to +0xa8/+0xb0/+0xb8; built with 0xc8 bytes, a passive unit with 0x68. */
    assert(!memcmp(image+0x10ac23e,"\x48\xb8\x00\x00\x00\x00\x00\x00\xf0\xbf",10) && !memcmp(image+0x10ac248,"\x48\x89\x87\xa8\x00\x00\x00",7) &&
           !memcmp(image+0x10ac24f,"\x48\x89\x87\xb0\x00\x00\x00",7) && !memcmp(image+0x10ac256,"\x48\x89\x87\xb8\x00\x00\x00",7));
    assert(!memcmp(image+0x10b2887,"\xb9\xc8\x00\x00\x00",5) && !memcmp(image+0x10b2b9b,"\xb9\x68\x00\x00\x00",5));
    memcpy(&rel,image+0x10ac1af,4);assert(0x10ac1b3+rel==GDIG_ACTIVE); /* lea of the ActiveGravidarUnit vtable */
    memcpy(&rel,image+0x10b2bbc,4);assert(0x10b2bc0+rel==0x2c67660);   /* lea of the passive GravidarUnit vtable */
    /* Apply: +0xa8 = TLS time, +0xb0 = time + 0.5 + draw, +0xb8 = time; the scan reads +0xb0 as the due time. */
    assert(!memcmp(image+0x10ad326,"\x48\x89\x85\xa8\x00\x00\x00",7) && !memcmp(image+0x10ad38d,"\xf2\x0f\x11\x8d\xb0\x00\x00\x00",8) &&
           !memcmp(image+0x10ad54c,"\x48\x89\x81\xb8\x00\x00\x00",7) && !memcmp(image+0x10b3850,"\xf2\x0f\x10\xb0\xb0\x00\x00\x00",8));
    const unsigned tampers[]={GDL_MOVE,0x784253,0x784297,0x7844c6,0x7844ce,GDL_MOVE+GDL_MOVE_LENGTH-1,
                              GDL_UNIT_CTOR,0x10ac246,0x10ac24c,GDL_UNIT_CTOR+GDL_UNIT_CTOR_LENGTH-1,0x10ad329,0x10b2dc0};
    for(unsigned i=0;i<sizeof(tampers)/sizeof(*tampers);i++) {image[tampers[i]]^=1;assert(!gdl_pinned());image[tampers[i]]^=1;}
    *(void**)(image+GDL_SLOT)=image+GDL_MOVE+1;assert(!gdl_pinned());
    *(void**)(image+GDL_SLOT)=image+GDL_MOVE;assert(gdl_pinned());
    /* Full arm and restore on the copy: only the slot pointer changes. */
    stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();world_identity_installed=1;
    LONG prior_sync=gravidar_time_sync;InterlockedExchange(&gravidar_time_sync,1);
    gdl_armed=gdl_installed=gdl_restored=0;
    assert(gdl_arm() && *(void**)(image+GDL_SLOT)==(void*)gdl_move && gdl_owned() && gdl_arm());
    gdl_restore();assert(gdl_restored && !gdl_installed && *(void**)(image+GDL_SLOT)==image+GDL_MOVE);
    gdl_armed=gdl_installed=gdl_restored=0;InterlockedExchange(&gdl_active,0);gdl_stage="idle";
    stats.active=0;boot_mode=0;world_identity_installed=0;InterlockedExchange(&gravidar_time_sync,prior_sync);
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("U::Sector::Move slot 674 / factor pass, unit constructor/apply times and sizes: native pins, relocated slot, tamper refusals and arm/restore PASS");
}
