/* Render RNG split: generator step model, private word swap and restore (also
 * when an exception unwinds), observe mode, per-frame counters, ID counting,
 * entry CAS install/restore, foreign-patch refusal and status bounds. Camera
 * FOV perturbation: apply, fail-closed checks and restore. Native image: pins,
 * tamper refusals, the step constants, entries and field offsets. */
static volatile uint64_t *test_rr_word;
static volatile uint64_t test_rr_inserts;
static unsigned test_rr_draws,test_rr_calls,test_rr_throw,test_rr_new_ids,test_rr_reseed;
static void test_rr_original(void *self) {
    assert(self==(void*)&test_rr_calls);++test_rr_calls;
    for(unsigned i=0;i<test_rr_draws;i++) *test_rr_word=render_rng_step(*test_rr_word);
    if(test_rr_reseed) *test_rr_word=0xdeadbeefcafef00dULL;
    test_rr_inserts+=test_rr_new_ids;
    if(test_rr_throw) RaiseException(0xe0525231,0,0,NULL);
}
static int test_rr_execute_throwing(volatile uint64_t *word) {
    int caught=0;
    __try {render_rng_execute(test_rr_original,&test_rr_calls,word,&test_rr_inserts);}
    __except(EXCEPTION_EXECUTE_HANDLER) {caught=1;}
    return caught;
}
static uint64_t test_rr_steps(uint64_t v,unsigned n) {while(n--) v=render_rng_step(v);return v;}
static void test_rr_reset(int mode) {
    render_rng_mode=mode;render_rng_state=lockstep_seed(RENDER_RNG_DOMAIN,0,0,0,0,0);render_rng_frame=UINT64_MAX;
    render_rng_calls=render_rng_steps=render_rng_unknown=render_rng_ids=render_rng_foreign=0;render_rng_failures=0;
    test_rr_calls=test_rr_throw=test_rr_new_ids=test_rr_reseed=0;
}
static void test_render_rng_camera(void) {
    static unsigned char universe[0x400],camera[0x900],other[0x900];
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t mode=0;float fov=1.25f,value;
    *(unsigned char**)(image+CAMERA_UNIVERSE)=universe;*(unsigned char**)(universe+CAMERA_PLAYER)=camera;
    memcpy(camera+CAMERA_FOV,&fov,4);memcpy(camera+CAMERA_MODE,&mode,4);
    stats.active=0;assert(!camera_perturb_apply(1) && !strcmp(camera_perturb_stage,"guard"));
    stats.active=1;ui_thread=GetCurrentThreadId();camera_perturb_verified=0;
    assert(!camera_perturb_apply(1) && !strcmp(camera_perturb_stage,"pins") && !camera_perturb_saved); /* not the X4 image */
    camera_perturb_verified=1;camera_perturb_failures=0;
    assert(camera_perturb_apply(1));memcpy(&value,camera+CAMERA_FOV,4);assert(value==1.25f*0.7f);
    assert(camera_perturb_apply(2));memcpy(&value,camera+CAMERA_FOV,4);assert(value==1.25f*0.4f);
    assert(camera_perturb_apply(4));memcpy(&value,camera+CAMERA_FOV,4);assert(value==1.25f);
    mode=3;memcpy(camera+CAMERA_MODE,&mode,4); /* FOV not live: left alone, counted */
    assert(camera_perturb_apply(5) && camera_perturb_skipped==1 && !strcmp(camera_perturb_stage,"mode"));
    mode=4;memcpy(camera+CAMERA_MODE,&mode,4);
    assert(camera_perturb_apply(6));memcpy(&value,camera+CAMERA_FOV,4);assert(value==1.25f*0.4f && camera_perturb_calls==4);
    char status[384];assert(camera_perturb_status(status,sizeof(status)) && strstr(status,"tick=6 calls=4 skipped=1 factor=0.40 original=1.250000"));
    /* Another writer: fail closed, and never overwrite its value on restore. */
    float foreign=2.0f;memcpy(camera+CAMERA_FOV,&foreign,4);
    assert(!camera_perturb_apply(7) && !strcmp(camera_perturb_stage,"fov_changed") && camera_perturb_failures==1);
    camera_perturb_restore();memcpy(&value,camera+CAMERA_FOV,4);assert(!camera_perturb_restored && value==2.0f);
    memcpy(camera+CAMERA_FOV,&camera_perturb_written,4);
    *(unsigned char**)(universe+CAMERA_PLAYER)=other;assert(!camera_perturb_apply(8) && !strcmp(camera_perturb_stage,"camera_replaced"));
    camera_perturb_restore();assert(!camera_perturb_restored && !strcmp(camera_perturb_stage,"restore_camera"));
    *(unsigned char**)(universe+CAMERA_PLAYER)=camera;camera_perturb_restore();
    memcpy(&value,camera+CAMERA_FOV,4);assert(camera_perturb_restored && value==1.25f);
    camera_perturb_restore();assert(camera_perturb_restored); /* idempotent */
    /* Worst-width status. */
    camera_perturb_tick=camera_perturb_calls=camera_perturb_skipped=camera_perturb_failures=UINT64_MAX;
    float huge=-3.4e38f;memcpy(&camera_perturb_original,&huge,4);memcpy(&camera_perturb_written,&huge,4);camera_perturb_stage="restore_foreign";
    int n=camera_perturb_status(status,sizeof(status));assert(n>0 && n<(int)sizeof(status));
    camera_perturb_verified=camera_perturb_saved=camera_perturb_restored=0;camera_perturb_camera=NULL;
    camera_perturb_original=camera_perturb_written=0;camera_perturb_tick=camera_perturb_calls=camera_perturb_skipped=camera_perturb_failures=0;
    camera_perturb_stage="idle";*(unsigned char**)(image+CAMERA_UNIVERSE)=NULL;stats.active=0;stats.issued=saved_issued;ui_thread=saved_ui;
    printf("camera FOV perturbation apply/skip/fail-closed/restore tests PASS\n");
}
static void test_render_rng(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;
    /* Step model = tools/dev/rng_steps.c (rotr30 of the PCG-style LCG). */
    assert(render_rng_step(0x0123456789abcdefULL)==0xd77d1548b38cb48cULL);
    assert(test_rr_steps(0x0123456789abcdefULL,3)==0x9b259ec877c7557dULL);
    assert(render_rng_distance(5,5)==0 && render_rng_distance(5,test_rr_steps(5,150))==150 && render_rng_distance(5,6)==UINT64_MAX);
    /* Split: slot 10 draws from the private word; the caller's word comes back unchanged. */
    test_rr_reset(RENDER_RNG_SPLIT);stats.issued=5;
    uint64_t word=0x1234,render=render_rng_state;test_rr_word=&word;test_rr_draws=150;test_rr_new_ids=2;test_rr_inserts=10;
    render_rng_execute(test_rr_original,&test_rr_calls,&word,&test_rr_inserts);
    assert(word==0x1234 && test_rr_calls==1 && render_rng_state==test_rr_steps(render,150) && render_rng_steps==150 &&
           render_rng_frame==5 && render_rng_frame_steps==150 && render_rng_ids==2 && render_rng_frame_ids==2 &&
           render_rng_entry==0x1234 && render_rng_exit==0x1234 && !render_rng_unknown && !strcmp(render_rng_stage,"done"));
    /* The private word continues across calls; a second call in one frame adds up. */
    test_rr_draws=3;test_rr_new_ids=0;render_rng_execute(test_rr_original,&test_rr_calls,&word,&test_rr_inserts);
    assert(word==0x1234 && render_rng_state==test_rr_steps(render,153) && render_rng_frame_calls==2 && render_rng_frame_steps==153);
    /* A different view (draw count) leaves the same simulation word: the leak is closed. */
    uint64_t other=0x1234;test_rr_word=&other;test_rr_draws=17;
    render_rng_execute(test_rr_original,&test_rr_calls,&other,NULL);assert(other==word);
    test_rr_word=&word;
    stats.issued=6;test_rr_draws=0;render_rng_execute(test_rr_original,&test_rr_calls,&word,&test_rr_inserts);
    assert(render_rng_frame==6 && render_rng_frame_calls==1 && !render_rng_frame_steps && render_rng_steps==170 && render_rng_ids==2);
    /* An exception unwinding through slot 10 restores the caller's word and counts a failure. */
    uint64_t before=render_rng_state;test_rr_throw=1;test_rr_draws=4;
    assert(test_rr_execute_throwing(&word));
    assert(word==0x1234 && render_rng_state==test_rr_steps(before,4) && render_rng_failures==1 && !strcmp(render_rng_stage,"exception"));
    test_rr_throw=0;render_rng_failures=0;
    /* A word outside the step bound is counted, not guessed. */
    test_rr_reseed=1;test_rr_draws=0;render_rng_execute(test_rr_original,&test_rr_calls,&word,&test_rr_inserts);
    assert(word==0x1234 && render_rng_unknown==1 && render_rng_state==0xdeadbeefcafef00dULL);test_rr_reseed=0;
    /* Observe: no swap; slot 10's draws are measured on the simulation word. */
    test_rr_reset(RENDER_RNG_OBSERVE);render=render_rng_state;word=77;test_rr_draws=9;
    render_rng_execute(test_rr_original,&test_rr_calls,&word,&test_rr_inserts);
    assert(word==test_rr_steps(77,9) && render_rng_state==render && render_rng_exit==word && render_rng_entry==77 && render_rng_frame_steps==9);
    /* Entry install/restore by CAS; a foreign value is never overwritten. */
    void *original=image+RENDER_RNG_MOVE,*client=image+RENDER_RNG_CLIENT,*camera=image+RENDER_RNG_CAMERA,*foreign=(void*)test_rr_original;
    memcpy(image+RENDER_RNG_ENTRY,&original,8);memcpy(image+RENDER_RNG_CLIENT_ENTRY,&client,8);memcpy(image+RENDER_RNG_CAMERA_ENTRY,&camera,8);
    assert(!render_rng_foreign_patch());
    assert(render_rng_swap(original,(void*)render_rng_move) && !render_rng_swap(original,NULL));
    render_rng_installed=1;InterlockedExchange(&render_rng_active,1);assert(render_rng_check());
    memcpy(image+RENDER_RNG_CAMERA_ENTRY,&foreign,8); /* framediet's camera site */
    assert(!render_rng_check() && !strcmp(render_rng_stage,"foreign_patch") && render_rng_failures==1);
    memcpy(image+RENDER_RNG_CAMERA_ENTRY,&camera,8);assert(!render_rng_check()); /* a counted failure stays */
    render_rng_failures=0;memcpy(image+RENDER_RNG_CLIENT_ENTRY,&foreign,8);assert(!render_rng_check());
    memcpy(image+RENDER_RNG_CLIENT_ENTRY,&client,8);render_rng_failures=0;
    memcpy(image+RENDER_RNG_ENTRY,&foreign,8);assert(!render_rng_check() && !strcmp(render_rng_stage,"entry_changed"));
    render_rng_restore();
    assert(!render_rng_active && render_rng_installed && !render_rng_restored && *(void**)(image+RENDER_RNG_ENTRY)==foreign);
    void *wrapper=(void*)render_rng_move;memcpy(image+RENDER_RNG_ENTRY,&wrapper,8);render_rng_restore();
    assert(render_rng_restored && !render_rng_installed && *(void**)(image+RENDER_RNG_ENTRY)==original);
    /* finish() (any stop reason, 90 included) restores the entry; a foreign entry holds stats.restored at 0. */
    memcpy(image+RENDER_RNG_ENTRY,&wrapper,8);render_rng_installed=1;render_rng_active=1;render_rng_restored=0;
    uint32_t saved_reason=stats.reason;stats.active=1;stats.restored=1;finish(90);
    assert(stats.reason==90 && !stats.active && stats.restored==1 && !render_rng_active && !render_rng_installed && render_rng_restored &&
           *(void**)(image+RENDER_RNG_ENTRY)==original);
    memcpy(image+RENDER_RNG_ENTRY,&foreign,8);render_rng_installed=1;render_rng_active=1;render_rng_restored=0;
    stats.active=1;stats.restored=1;finish(90);
    assert(!stats.restored && render_rng_installed && !render_rng_restored && *(void**)(image+RENDER_RNG_ENTRY)==foreign);
    render_rng_installed=0;render_rng_restored=1;memcpy(image+RENDER_RNG_ENTRY,&original,8);stats.reason=saved_reason;stats.restored=0;
    /* Arming refuses without an active gate, off the UI thread, for unknown modes and on unpinned bytes. */
    assert(!render_rng_arm(RENDER_RNG_SPLIT));
    stats.active=1;ui_thread=GetCurrentThreadId();stats.issued=0;
    assert(!render_rng_arm(RENDER_RNG_OFF) && !render_rng_arm(3) && !render_rng_arm(RENDER_RNG_SPLIT) && !render_rng_installed);
    assert(*(void**)(image+RENDER_RNG_ENTRY)==original);stats.active=0;
    char status[640];int n=render_rng_status(status,sizeof(status));
    assert(n>0 && strstr(status,"mode=observe armed=0 installed=0 frame=6 calls=") && strstr(status," restored=1 stage=entry_changed failures=1"));
    render_rng_frame=render_rng_calls=render_rng_frame_calls=render_rng_steps=render_rng_frame_steps=render_rng_unknown=UINT64_MAX-1;
    render_rng_ids=render_rng_frame_ids=render_rng_foreign=render_rng_entry=render_rng_exit=render_rng_state=UINT64_MAX;
    render_rng_failures=INT64_MAX;render_rng_stage="foreign_patch";render_rng_active=1;render_rng_installed=1;
    n=render_rng_status(status,sizeof(status));assert(n>0 && n<(int)sizeof(status));
    assert(n+strlen("[X4LAN] NATIVE_GATE RENDER_RNG_SPLIT tick=18446744073709551615 \n")<2048);
    test_rr_reset(RENDER_RNG_OFF);render_rng_active=0;render_rng_installed=render_rng_restored=0;render_rng_stage="idle";
    render_rng_entry=render_rng_exit=render_rng_state=0;render_rng_frame_calls=render_rng_frame_steps=render_rng_frame_ids=0;
    memset(image+RENDER_RNG_ENTRY,0,8);memset(image+RENDER_RNG_CLIENT_ENTRY,0,8);memset(image+RENDER_RNG_CAMERA_ENTRY,0,8);
    stats.issued=saved_issued;ui_thread=saved_ui;
    printf("render RNG split step model/swap/exception/observe/CAS/foreign-patch/status tests PASS\n");
    test_render_rng_camera();
}
static void test_render_rng_native_image(const char *path) {
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
    assert(render_rng_pins());
    const unsigned tamper[]={RENDER_RNG_MOVE+0x69,RENDER_RNG_CLIENT+0x40,RENDER_RNG_CAMERA+0x1f,0x14ddcd0,0xcc64f,0xef46b3};
    for(unsigned i=0;i<sizeof(tamper)/sizeof(*tamper);i++) {image[tamper[i]]^=1;assert(!render_rng_pins());image[tamper[i]]^=1;}
    /* Unrelocated entries hold the preferred-base addresses of the pinned functions. */
    assert(*(uint64_t*)(image+RENDER_RNG_ENTRY)==0x140000000ULL+RENDER_RNG_MOVE);
    assert(*(uint64_t*)(image+RENDER_RNG_CLIENT_ENTRY)==0x140000000ULL+RENDER_RNG_CLIENT);
    assert(*(uint64_t*)(image+RENDER_RNG_CAMERA_ENTRY)==0x140000000ULL+RENDER_RNG_CAMERA);
    /* FloatState's constants and rotation are the step model's. */
    static const unsigned char step_bytes[]={0x48,0xba,0x2d,0x7f,0x95,0x4c,0x2d,0xf4,0x51,0x58,0x48,0x0f,0xaf,0xc2,
        0x48,0xba,0x4f,0x81,0x67,0xf7,0x7e,0x7b,0x05,0x14,0x48,0x03,0xc2,0x48,0xc1,0xc8,0x1e,0x48,0x89,0x01};
    assert(!memcmp(image+0x14ddcce,step_bytes,sizeof(step_bytes)));
    /* GameRender::Move walks the four render banks at VbiTable+0x110. */
    int32_t rel;memcpy(&rel,image+0x121c202,4);assert(0x121c206+rel==TABLE_RVA);
    assert(!memcmp(image+0x121c20b,"\x48\x81\xc3\x10\x01\x00\x00\xbf\x04\x00\x00\x00",12));
    /* The pathing kick (path_release's call) precedes the VBI dispatch in MoveWorker::Run. */
    assert(!memcmp(image+0xef4614,"\xff\x15",2) && !memcmp(image+0xef4681,"\x48\x8b\x3d",3));
    memcpy(&rel,image+0xef4684,4);assert(0xef4688+rel==TABLE_RVA);
    camera_perturb_verified=0;assert(camera_perturb_pins());
    memcpy(&rel,image+0x2a8518,4);assert(0x2a851c+rel==CAMERA_UNIVERSE);
    assert(!memcmp(image+0x2a852c,"\x48\x8b\x9b\xd0\x03\x00\x00",7) && !memcmp(image+0x2a855f,"\x8b\x83\x68\x08\x00\x00",6) &&
           !memcmp(image+0x2a85a4,"\xf3\x0f\x11\x8b\x5c\x08\x00\x00",8));
    image[0x2a85a8]^=1;camera_perturb_verified=0;assert(!camera_perturb_pins());image[0x2a85a8]^=1;camera_perturb_verified=0;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("render RNG split and camera perturbation native pins, entries, step constants and tamper refusals verified");
}
