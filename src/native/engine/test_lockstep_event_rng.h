/* native_gate_event_rng: keyed per-delivery scope (seed from tick, source key, event type and due time;
 * caller's word restored), observe counting, local sources, boot frames, foreign thread, unwinding,
 * rows, status, arming refusals, the tick check, and the pinned build. */
static uint64_t test_ev_word,test_ev_seen;
static unsigned test_ev_steps;
static int test_ev_raise;
static uint64_t *test_ev_word_fn(void) {return &test_ev_word;}
static uint64_t *test_ev_no_word(void) {return NULL;}
static uint64_t test_ev_step(uint64_t x) {x=x*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;return (x>>30)|(x<<34);}
/* A listener that draws test_ev_steps times from the thread word (and may raise). */
static void test_ev_native(void *source,void *event,double due) {
    (void)source;(void)event;(void)due;uint64_t *word=event_rng_word();
    if(!word) {test_ev_seen=0;return;} /* the no-word case: the listener runs on whatever it has */
    test_ev_seen=*word;for(unsigned i=0;i<test_ev_steps;i++) *word=test_ev_step(*word);
    if(test_ev_raise) RaiseException(0xe0000150u,0,0,NULL);
}
static void test_ev_deliver_raising(void *source,void *event,double due) {
    __try {event_rng_deliver(test_ev_native,source,event,due);}
    __except(GetExceptionCode()==0xe0000150u?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {}
}
static void test_event_rng(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    int saved_ids=world_identity_installed;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0x6b065,0xa1df9) && world_identity_record(0x6b066,0x43cc2));
    static unsigned char station[0x40],ship[0x40],local[0x40],event[16],other_event[16];
    memset(station,0,sizeof(station));memset(ship,0,sizeof(ship));memset(local,0,sizeof(local));
    void *vt=image+0x2b9bff8;memcpy(station,&vt,8);*(uint64_t*)(station+8)=0x6b066;
    vt=image+0x2b82440;memcpy(ship,&vt,8);*(uint64_t*)(ship+8)=0x6b065;
    vt=image+0x2b6cc38;memcpy(local,&vt,8);*(uint64_t*)(local+8)=0x77777; /* no portable key */
    vt=image+EVENT_RNG_TRADE_EVENT;memcpy(event,&vt,8);vt=image+0x2baf880;memcpy(other_event,&vt,8);
    uint64_t *(*saved_word)(void)=event_rng_word;event_rng_word=test_ev_word_fn;
    event_rng_row_t *saved_ring=event_rng_ring;
    event_rng_ring=VirtualAlloc(NULL,sizeof(event_rng_row_t)*EVENT_RNG_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(event_rng_ring);
    event_rng_reset();event_rng_mode=EVENT_RNG_KEY;event_rng_stage="armed";event_rng_armed=1;
    stats.active=1;boot_mode=1;boot_phase=3;stats.issued=2997;ui_thread=GetCurrentThreadId();
    /* Disarmed: plain native call on the caller's word. */
    event_rng_armed=0;test_ev_word=0x1111;test_ev_steps=2;event_rng_deliver(test_ev_native,station,event,57.0);
    assert(test_ev_seen==0x1111 && test_ev_word==test_ev_step(test_ev_step(0x1111)) && !event_rng_calls);event_rng_armed=1;
    /* key: the listener starts from (tick, source key, event type, due), whatever the caller's word; the word is put back. */
    double due=57.0;uint64_t bits;memcpy(&bits,&due,8);
    uint64_t seed=event_rng_seed(2997,0x43cc2,WORLD_KEY_SAVED,EVENT_RNG_TRADE_EVENT,bits);
    assert(seed==lockstep_seed(EVENT_RNG_DOMAIN,2997,0x43cc2,EVENT_RNG_TRADE_EVENT,bits,WORLD_KEY_SAVED));
    test_ev_word=0x1111;test_ev_steps=3;event_rng_deliver(test_ev_native,station,event,due);
    assert(test_ev_seen==seed && test_ev_word==0x1111);
    test_ev_word=0x9999;event_rng_deliver(test_ev_native,station,event,due);
    assert(test_ev_seen==seed && test_ev_word==0x9999); /* independent of the frame's earlier draws */
    assert(event_rng_calls==2 && event_rng_keyed==2 && event_rng_drawn==2 && event_rng_draws==6 && event_rng_trade==2 &&
           event_rng_trade_drawn==2 && event_rng_trade_draws==6 && !event_rng_local && !event_rng_failures);
    /* Another due time, tick, source or event type: another stream. */
    event_rng_deliver(test_ev_native,station,event,58.0);assert(test_ev_seen!=seed);
    stats.issued=2998;event_rng_deliver(test_ev_native,station,event,due);assert(test_ev_seen!=seed);stats.issued=2997;
    event_rng_deliver(test_ev_native,ship,event,due);
    assert(test_ev_seen==event_rng_seed(2997,0xa1df9,WORLD_KEY_SAVED,EVENT_RNG_TRADE_EVENT,bits));
    test_ev_steps=0;event_rng_deliver(test_ev_native,ship,other_event,due);
    assert(test_ev_seen==event_rng_seed(2997,0xa1df9,WORLD_KEY_SAVED,0x2baf880,bits) && event_rng_drawn==5 && event_rng_calls==6);
    /* A source without a portable key: its vtable RVA stands in for the key (class none). */
    test_ev_steps=1;event_rng_deliver(test_ev_native,local,other_event,due);
    assert(test_ev_seen==event_rng_seed(2997,0x2b6cc38,WORLD_KEY_NONE,0x2baf880,bits) && event_rng_local==1 && test_ev_word==0x9999);
    /* Rows: deliveries that drew, with the caller's word at entry. */
    char out[512];assert(event_rng_rows==6);
    assert(event_rng_row(1,out,sizeof(out))>0 && strstr(out,"tick=2997 class=saved key=0000000000043cc2 stype=2b9bff8 etype=2ac17b8 ") &&
           strstr(out," entry=0000000000009999 draws=3 keyed=1"));
    assert(event_rng_row(5,out,sizeof(out))>0 && strstr(out,"class=none key=0000000002b6cc38 stype=2b6cc38 etype=2baf880 ") && strstr(out," draws=1 keyed=1"));
    assert(!event_rng_row(6,out,sizeof(out)));
    /* Order-free sums: the same deliveries in another order give the same keyset/drawset. */
    uint64_t keyset=event_rng_keyset,drawset=event_rng_drawset;
    /* observe: the native word is used and advanced; the scope only counts. */
    event_rng_mode=EVENT_RNG_OBSERVE;test_ev_word=0x4444;test_ev_steps=2;event_rng_deliver(test_ev_native,station,event,due);
    assert(test_ev_seen==0x4444 && test_ev_word==test_ev_step(test_ev_step(0x4444)) && event_rng_keyed==7 && event_rng_drawn==7);
    assert(event_rng_row(6,out,sizeof(out))>0 && strstr(out," entry=0000000000004444 draws=2 keyed=0"));
    assert(event_rng_keyset!=keyset && event_rng_drawset!=drawset);
    event_rng_mode=EVENT_RNG_KEY;
    {
        uint64_t k=0,d=0;event_rng_keyset=event_rng_drawset=0;test_ev_steps=1;
        event_rng_deliver(test_ev_native,station,event,due);event_rng_deliver(test_ev_native,ship,other_event,due);k=event_rng_keyset;d=event_rng_drawset;
        event_rng_keyset=event_rng_drawset=0;
        event_rng_deliver(test_ev_native,ship,other_event,due);event_rng_deliver(test_ev_native,station,event,due);
        assert(event_rng_keyset==k && event_rng_drawset==d);
        test_ev_steps=2;event_rng_keyset=event_rng_drawset=0;event_rng_deliver(test_ev_native,ship,other_event,due);
        event_rng_deliver(test_ev_native,station,event,due);assert(event_rng_keyset==k && event_rng_drawset!=d);
    }
    /* Draws above the cap count as unknown (no row draws, no total). */
    uint64_t draws=event_rng_draws,unknown=event_rng_unknown;test_ev_steps=EVENT_RNG_STEP_CAP+1;
    event_rng_deliver(test_ev_native,station,event,due);assert(event_rng_unknown==unknown+1 && event_rng_draws==draws);
    LONG64 last=event_rng_rows-1;assert(event_rng_row((uint64_t)last,out,sizeof(out))>0 && strstr(out," draws=-1 "));
    /* Boot and loading frames stay native and are only counted. */
    test_ev_steps=1;boot_phase=2;test_ev_word=0x5555;uint64_t calls=event_rng_calls;event_rng_deliver(test_ev_native,station,event,due);
    assert(test_ev_seen==0x5555 && event_rng_boot==1 && event_rng_calls==calls);boot_phase=3;
    stats.active=0;event_rng_deliver(test_ev_native,station,event,due);assert(event_rng_boot==2);stats.active=1;
    assert(event_rng_check(1) && !event_rng_check(0) && !strcmp(event_rng_stage,"order"));event_rng_stage="armed";
    /* Unwinding through the listener still restores the caller's word, and fails the gate. */
    test_ev_raise=1;test_ev_word=0x6666;test_ev_deliver_raising(station,event,due);test_ev_raise=0;
    assert(test_ev_word==0x6666 && event_rng_unwound==1 && event_rng_failures==1 && !event_rng_depth && !event_rng_check(1));
    assert(!strcmp(event_rng_stage,"failure"));event_rng_failures=0;event_rng_unwound=0;event_rng_stage="armed";assert(event_rng_check(1));
    /* No thread word, or a delivery off the MoveWorker in an admitted frame: native, and the gate fails. */
    event_rng_word=test_ev_no_word;event_rng_deliver(test_ev_native,station,event,due);event_rng_word=test_ev_word_fn;
    assert(event_rng_failures==1 && !event_rng_check(1));event_rng_failures=0;event_rng_stage="armed";
    ui_thread=GetCurrentThreadId()+1;test_ev_word=0x7777;event_rng_deliver(test_ev_native,station,event,due);
    assert(test_ev_seen==0x7777 && event_rng_foreign==1 && event_rng_failures==1 && !event_rng_check(1));
    ui_thread=GetCurrentThreadId();event_rng_failures=0;event_rng_foreign=0;event_rng_stage="armed";
    /* An unreadable source (a released page) is left native and counted. */
    unsigned char *gone=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(gone && VirtualFree(gone,0,MEM_RELEASE));
    calls=event_rng_calls;test_ev_word=0x8888;event_rng_deliver(test_ev_native,gone,event,due);
    assert(test_ev_seen==0x8888 && event_rng_unreadable==1 && event_rng_calls==calls);
    assert(event_rng_status(out,sizeof(out))>0 && strstr(out,"armed=1 mode=key frame=2997 ") && strstr(out," unreadable=1 foreign=0 unwound=0 ") &&
           strstr(out," boot=2 ") && strstr(out,"stage=armed"));
    /* Row window: every delivery of its ticks is a row, draws=0 too; outside it only drawn ones. */
    {
        LONG saved_armed=event_rng_armed;event_rng_armed=0;
        assert(!event_rng_window(0,5) && !event_rng_window(10,5) && !event_rng_window(1,700) && event_rng_window(2990,3000));
        event_rng_armed=saved_armed;assert(!event_rng_window(1,2) && event_rng_window(2990,3000)); /* rerun: only the same window */
        LONG64 rows=event_rng_rows;test_ev_steps=0;stats.issued=2997;event_rng_deliver(test_ev_native,station,event,due);
        assert(event_rng_rows==rows+1 && event_rng_row((uint64_t)rows,out,sizeof(out))>0 && strstr(out," draws=0 keyed=1"));
        stats.issued=3001;event_rng_deliver(test_ev_native,station,event,due);assert(event_rng_rows==rows+1);
        assert(event_rng_status(out,sizeof(out))>0 && strstr(out," window=2990-3000 "));
        event_rng_all_from=event_rng_all_to=0;stats.issued=2997;
    }
    /* Arming refusals (the test image is not the pinned build). */
    event_rng_armed=0;stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;world_identity_installed=1;
    assert(!event_rng_arm(0,1) && !strcmp(event_rng_stage,"mode"));
    assert(!event_rng_arm(EVENT_RNG_KEY,0) && !strcmp(event_rng_stage,"needs"));
    world_identity_installed=0;assert(!event_rng_arm(EVENT_RNG_KEY,1) && !strcmp(event_rng_stage,"needs"));world_identity_installed=1;
    assert(!event_rng_arm(EVENT_RNG_KEY,1) && !strcmp(event_rng_stage,"pins") && !event_rng_armed);
    stats.issued=1;assert(!event_rng_arm(EVENT_RNG_KEY,1));stats.issued=0;boot_phase=3;assert(!event_rng_arm(EVENT_RNG_KEY,1));boot_phase=0;
    ui_thread=GetCurrentThreadId()+1;assert(!event_rng_arm(EVENT_RNG_KEY,1));ui_thread=GetCurrentThreadId();
    event_rng_stop();assert(!event_rng_armed && event_rng_check(0));
    VirtualFree(event_rng_ring,0,MEM_RELEASE);event_rng_ring=saved_ring;event_rng_word=saved_word;event_rng_reset();event_rng_mode=EVENT_RNG_OFF;event_rng_stage="idle";
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    world_identity_installed=saved_ids;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("event delivery keyed scope/observe/rows/failures tests PASS\n");
}
static void test_event_rng_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    assert(event_rng_pins());
    /* Both pinned functions step the word at TLS slot0+0x318 with the PCG the step count assumes. */
    assert(!memcmp(image+0x4071a9,"\xb8\x18\x03\x00\x00",5) && !memcmp(image+0x4071b8,"\x48\x0f\xaf\xca",4) &&
           !memcmp(image+0x40719f,"\x48\xba\x2d\x7f\x95\x4c\x2d\xf4\x51\x58",10) && !memcmp(image+0x4071bc,"\x48\xba\x4f\x81\x67\xf7\x7e\x7b\x05\x14",10) &&
           !memcmp(image+0x4071ce,"\x48\xc1\xc9\x1e",4));
    assert(!memcmp(image+0x14ddcce,"\x48\xba\x2d\x7f\x95\x4c\x2d\xf4\x51\x58",10) && !memcmp(image+0x14ddce9,"\x48\xc1\xc8\x1e",4));
    /* UpdateTradeOffersEvent: vtable slot 1 returns type 0x215; the poster 0x4070e0 builds one and
     * Container::UpdateTradeOffers 0x4079e0 reschedules through 0x407160 and runs the economy update 0x431f90. */
    assert(*(uint64_t*)(image+EVENT_RNG_TRADE_EVENT+8)==0x140000000ULL+0x34e070u && !memcmp(image+0x34e070,"\xb8\x15\x02\x00\x00\xc3",6));
    assert(!memcmp(image+0x407136,"\x48\x8d\x0d",3) && 0x40713du+*(int32_t*)(image+0x407139)==EVENT_RNG_TRADE_EVENT);
    assert(image[0x40a1e0]==0xe8 && 0x40a1e5u+*(int32_t*)(image+0x40a1e1)==EVENT_RNG_RESCHEDULE);
    assert(image[0x407a55]==0xe8 && 0x407a5au+*(int32_t*)(image+0x407a56)==0x431f90u);
    /* The economy update draws the thread word (inline PCG and Random::FloatRange on TLS+0x318). */
    assert(!memcmp(image+0x432b4d,"\xb8\x18\x03\x00\x00",5) && image[0x432dea]==0xe8 && 0x432defu+*(int32_t*)(image+0x432deb)==EVENT_RNG_FLOAT_RANGE);
    image[EVENT_RNG_RESCHEDULE+0x40]^=1;assert(!event_rng_pins());image[EVENT_RNG_RESCHEDULE+0x40]^=1;
    image[EVENT_RNG_FLOAT_RANGE+0x40]^=1;assert(!event_rng_pins());image[EVENT_RNG_FLOAT_RANGE+0x40]^=1;
    assert(event_rng_pins());
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("event RNG pins, trade offer event, reschedule and economy draw sites verified");
}
