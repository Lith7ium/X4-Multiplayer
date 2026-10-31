/* native_gate_ship_sound_rng: sound starts on a private word ('split') or
 * counted on the caller's ('observe'), exception unwinding, arming refusals,
 * slot ownership, restore, finish, status bounds, and the native pins. */
static unsigned test_ss_draws;
static uint64_t *test_ss_word,test_ss_seen;
static int test_ss_throw;
static uint64_t test_ss_step(uint64_t s) {s=s*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;return (s>>30)|(s<<34);}
static uintptr_t test_ss_original(void *self,void *list,void *data,uintptr_t flag) {
    test_ss_seen=*test_ss_word;
    for(unsigned i=0;i<test_ss_draws;i++) *test_ss_word=test_ss_step(*test_ss_word);
    if(test_ss_throw) RaiseException(0xe0535344,0,0,NULL);
    return (uintptr_t)self+(uintptr_t)list+(uintptr_t)data+flag;
}
static uintptr_t test_ss_guarded(uint64_t *word,int *thrown) {
    uintptr_t result=0;*thrown=0;
    __try {result=ss_run(test_ss_original,word,(void*)1,(void*)2,(void*)3,4);}
    __except(GetExceptionCode()==0xe0535344?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {*thrown=1;}
    return result;
}
static void test_ss_reset(void) {
    ss_mode=SS_OFF;ss_installed=ss_restored=0;memset(ss_swapped,0,sizeof(ss_swapped));ss_active=0;
    ss_calls=ss_drew=ss_draws=ss_unknown=ss_foreign=ss_counter=ss_failures=0;ss_tick=UINT64_MAX;
    ss_frame_calls=ss_frame_drew=ss_frame_draws=0;ss_stage="idle";ss_original_override=NULL;
}
static void test_ship_sound(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    test_ss_reset();uint64_t word=0x1234;test_ss_word=&word;test_ss_draws=3;int thrown;
    /* Inactive: the caller's word, nothing counted but the call. */
    assert(test_ss_guarded(&word,&thrown)==10 && !thrown && test_ss_seen==0x1234 && word!=0x1234 && ss_calls==1 && !ss_drew);
    /* Split: the original sees a private word; the caller's word is untouched. */
    stats.issued=669;ui_thread=GetCurrentThreadId();ss_mode=SS_SPLIT;ss_active=1;word=0x1234;
    assert(test_ss_guarded(&word,&thrown)==10 && !thrown && word==0x1234 && test_ss_seen!=0x1234);
    assert(test_ss_seen==lockstep_seed(SS_DOMAIN,1,0,0,0,0) && ss_drew==1 && ss_draws==3 && ss_frame_calls==1 && ss_frame_draws==3);
    uint64_t first=test_ss_seen;test_ss_guarded(&word,&thrown);assert(test_ss_seen!=first && word==0x1234); /* a fresh word per call */
    test_ss_draws=0;test_ss_guarded(&word,&thrown);assert(ss_drew==2 && ss_frame_calls==3 && word==0x1234);
    /* An exception unwinding through the start: the word is restored and the failure counted. */
    test_ss_draws=2;test_ss_throw=1;test_ss_guarded(&word,&thrown);test_ss_throw=0;
    assert(thrown && word==0x1234 && ss_failures==1 && !ss_check() && !strcmp(ss_stage,"exception"));ss_failures=0;
    /* Observe: the caller's word advances and the draws are counted. */
    ss_mode=SS_OBSERVE;test_ss_draws=2;test_ss_guarded(&word,&thrown);
    assert(test_ss_seen==0x1234 && word==test_ss_step(test_ss_step(0x1234)) && ss_draws==3+3+2+2);
    /* Beyond the step bound: unknown. Off the MoveWorker: foreign, no frame count. */
    test_ss_draws=ACTOR_ALL_STEP_CAP+2;word=0x1234;test_ss_guarded(&word,&thrown);assert(ss_unknown==1);
    test_ss_draws=0;ui_thread=GetCurrentThreadId()+1;uint64_t frame=ss_frame_calls;test_ss_guarded(&word,&thrown);
    assert(ss_foreign==1 && ss_frame_calls==frame);ui_thread=GetCurrentThreadId();
    /* No readable word: straight through. */
    LONG64 calls=ss_calls;assert(ss_run(test_ss_original,NULL,(void*)1,(void*)2,(void*)3,4)==10 && ss_calls==calls+1);
    char status[512];int n=ss_status(status,sizeof(status));
    assert(n>0 && strstr(status,"mode=observe installed=0 frame=669 calls=6 drew=4 draws=10 ") && strstr(status," unknown=1 foreign=1 "));
    /* Arming: before the first admitted frame, on the MoveWorker; pins (zero image refuses). */
    test_ss_reset();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;
    assert(!ss_arm(SS_OFF) && !ss_arm(3));stats.issued=1;assert(!ss_arm(SS_SPLIT));stats.issued=0;
    ui_thread=GetCurrentThreadId()+1;assert(!ss_arm(SS_SPLIT));ui_thread=GetCurrentThreadId();
    boot_phase=1;assert(!ss_arm(SS_SPLIT));boot_phase=0;
    assert(!ss_arm(SS_SPLIT) && !strcmp(ss_stage,"pins") && !ss_installed);
    /* Ownership and restore on hand-set slots. */
    void *saved_slots[SS_VTABLES];
    for(unsigned i=0;i<SS_VTABLES;i++) {memcpy(&saved_slots[i],image+ss_vtables[i]+SS_SLOT,8);*(void**)(image+ss_vtables[i]+SS_SLOT)=(void*)ss_wrapper;ss_swapped[i]=1;}
    ss_installed=1;ss_active=1;assert(ss_owned());
    *(void**)(image+ss_vtables[3]+SS_SLOT)=(void*)test_ss_original;assert(!ss_owned() && !ss_check() && !strcmp(ss_stage,"slot_changed"));
    ss_failures=0;ss_restore();
    assert(ss_installed && !ss_restored && !ss_active && *(void**)(image+ss_vtables[0]+SS_SLOT)==image+SS_TARGET &&
           *(void**)(image+ss_vtables[3]+SS_SLOT)==(void*)test_ss_original && ss_swapped[3] && !ss_swapped[0]);
    *(void**)(image+ss_vtables[3]+SS_SLOT)=(void*)ss_wrapper;ss_restore();
    assert(!ss_installed && ss_restored);
    for(unsigned i=0;i<SS_VTABLES;i++) assert(*(void**)(image+ss_vtables[i]+SS_SLOT)==image+SS_TARGET);
    /* finish() gives the slots back. */
    for(unsigned i=0;i<SS_VTABLES;i++) {*(void**)(image+ss_vtables[i]+SS_SLOT)=(void*)ss_wrapper;ss_swapped[i]=1;}
    ss_installed=1;ss_active=1;ss_restored=0;uint32_t saved_reason=stats.reason;stats.active=1;stats.restored=1;finish(SS_STOP_REASON);
    assert(stats.reason==SS_STOP_REASON && !ss_installed && ss_restored && *(void**)(image+ss_vtables[6]+SS_SLOT)==image+SS_TARGET);
    /* Status bounds at the widest values. */
    ss_calls=ss_drew=ss_draws=ss_unknown=ss_foreign=ss_failures=INT64_MAX;ss_tick=stats.issued=UINT64_MAX-1;
    ss_frame_calls=ss_frame_drew=ss_frame_draws=UINT64_MAX;ss_stage="block_changed";ss_mode=SS_OBSERVE;
    n=ss_status(status,sizeof(status));assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE SHIP_SOUND_RNG tick=18446744073709551615 \n")<512);
    for(unsigned i=0;i<SS_VTABLES;i++) memcpy(image+ss_vtables[i]+SS_SLOT,&saved_slots[i],8);
    test_ss_reset();
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    stats.reason=saved_reason;stats.restored=0;
    printf("ship sound RNG split/observe/exception/foreign/arming/ownership/restore/finish/status tests PASS\n");
}
/* With the X4 9.00 executable: the pinned body, the seven relocated slots and
 * their vtables' names, tamper refusals and a full arm/restore on the copy. */
static void test_ship_sound_native_image(const char *path) {
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
    /* The file holds preferred-base pointers: slot 1143 of each ship-family vtable, and slot 1144 the stop. */
    for(unsigned i=0;i<SS_VTABLES;i++) {
        assert(*(uint64_t*)(image+ss_vtables[i]+SS_SLOT)==nt.OptionalHeader.ImageBase+SS_TARGET);
        assert(*(uint64_t*)(image+ss_vtables[i]+SS_SLOT+8)==nt.OptionalHeader.ImageBase+0x7c8c60u);
        *(void**)(image+ss_vtables[i]+SS_SLOT)=image+SS_TARGET;
    }
    assert(ss_pins());
    /* The start ends in ret at 0x7c8c57 and runs the XSfx start 0x3e7ee0; the stop jumps to 0x3e8100. */
    assert(image[SS_TARGET+SS_BODY-1]==0xc3);
    int32_t rel;memcpy(&rel,image+0x7c8c38,4);assert(image[0x7c8c37]==0xe8 && 0x7c8c3c+rel==0x3e7ee0);
    memcpy(&rel,image+0x7c8c68,4);assert(image[0x7c8c67]==0xe9 && 0x7c8c6c+rel==0x3e8100);
    image[SS_TARGET+0x40]^=1;assert(!ss_pins());image[SS_TARGET+0x40]^=1;
    *(void**)(image+ss_vtables[5]+SS_SLOT)=image+SS_TARGET+1;assert(!ss_pins());*(void**)(image+ss_vtables[5]+SS_SLOT)=image+SS_TARGET;
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    test_ss_reset();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    assert(ss_arm(SS_SPLIT) && ss_installed && ss_owned() && ss_check() && ss_arm(SS_SPLIT) && !ss_arm(SS_OBSERVE));
    for(unsigned i=0;i<SS_VTABLES;i++) assert(*(void**)(image+ss_vtables[i]+SS_SLOT)==(void*)ss_wrapper);
    image[SS_TARGET+0x10]^=1;assert(!ss_check() && !strcmp(ss_stage,"block_changed"));image[SS_TARGET+0x10]^=1;ss_failures=0;
    ss_restore();assert(ss_restored && !ss_installed);
    for(unsigned i=0;i<SS_VTABLES;i++) assert(*(void**)(image+ss_vtables[i]+SS_SLOT)==image+SS_TARGET);
    /* A foreign slot refuses arming and leaves the others untouched. */
    *(void**)(image+ss_vtables[2]+SS_SLOT)=image+SS_TARGET+0x10;test_ss_reset();
    assert(!ss_arm(SS_SPLIT) && !strcmp(ss_stage,"pins"));
    for(unsigned i=0;i<SS_VTABLES;i++) if(i!=2) assert(*(void**)(image+ss_vtables[i]+SS_SLOT)==image+SS_TARGET);
    test_ss_reset();
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("ship sound RNG native body/slot pins (seven ship-family vtables), XSfx start and stop, tamper refusals and arm/restore verified");
}
