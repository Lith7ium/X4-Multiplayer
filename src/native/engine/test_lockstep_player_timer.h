/* native_gate_player_timer: the first admitted call per Player re-bases the
 * fleet rebuild deadline, later calls and checks pass through and are counted,
 * gating and readability, the object table, arming refusals, entry ownership,
 * restore, finish, status bounds, and the native pins. */
#define TEST_PT_OBJECT (PT_AUTOPILOT_DUE+8u)
static double test_pt_seen,test_pt_rearm;
static unsigned char *test_pt_last;
static void test_pt_original(void *self) {
    test_pt_last=self;memcpy(&test_pt_seen,(unsigned char*)self+PT_DUE,8);
    if(test_pt_rearm!=0) memcpy((unsigned char*)self+PT_DUE,&test_pt_rearm,8);
}
static void test_pt_reset(void) {
    pt_installed=pt_restored=0;pt_calls=pt_failures=0;pt_admitted=pt_rebased=pt_first=pt_fires=pt_last_fire=pt_unreadable=pt_full=0;
    pt_before=pt_after=pt_autopilot=pt_next=0;memset(pt_objects,0,sizeof(pt_objects));pt_object_count=0;
    pt_original_override=NULL;pt_stage="idle";test_pt_rearm=0;test_pt_last=NULL;
}
static uint64_t test_pt_bits(double value) {uint64_t bits;memcpy(&bits,&value,8);return bits;}
static void test_player_timer(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    test_pt_reset();
    unsigned char *player=calloc(1,TEST_PT_OBJECT),*other=calloc(1,TEST_PT_OBJECT),tls[0x320];assert(player && other);
    memset(tls,0,sizeof(tls));tls[0x10]=1;double now=26.5,loading=17.25,autopilot=4100.0,v;
    memcpy(tls+0x300,&now,8);memcpy(player+PT_DUE,&loading,8);memcpy(player+PT_AUTOPILOT_DUE,&autopilot,8);
    /* Not admitted: no gate, loading, no tick yet, another thread. */
    stats.active=0;boot_mode=1;boot_phase=3;stats.issued=1;ui_thread=GetCurrentThreadId();
    pt_run(test_pt_original,player,tls);assert(test_pt_last==player && test_pt_seen==loading && pt_calls==1 && !pt_admitted);
    stats.active=1;boot_phase=2;pt_run(test_pt_original,player,tls);boot_phase=3;
    stats.issued=0;pt_run(test_pt_original,player,tls);stats.issued=1;
    ui_thread=GetCurrentThreadId()+1;pt_run(test_pt_original,player,tls);ui_thread=GetCurrentThreadId();
    assert(pt_calls==4 && !pt_admitted && !pt_rebased && test_pt_seen==loading);
    /* Unreadable: no TLS block, TLS not set up, a non-finite time. */
    pt_run(test_pt_original,player,NULL);tls[0x10]=0;pt_run(test_pt_original,player,tls);tls[0x10]=1;
    v=NAN;memcpy(tls+0x300,&v,8);pt_run(test_pt_original,player,tls);memcpy(tls+0x300,&now,8);
    assert(pt_unreadable==3 && !pt_admitted && !pt_object_count && test_pt_seen==loading);
    /* The first admitted call: the original already sees time + 15 s. */
    stats.issued=7;pt_run(test_pt_original,player,tls);
    assert(test_pt_seen==now+PT_FIRST && pt_rebased==1 && pt_first==7 && pt_admitted==1 && pt_object_count==1 && !pt_fires);
    assert(pt_before==test_pt_bits(loading) && pt_after==test_pt_bits(now+PT_FIRST) && pt_autopilot==test_pt_bits(autopilot));
    memcpy(&v,player+PT_AUTOPILOT_DUE,8);assert(v==autopilot); /* the saved autopilot deadline is left alone */
    /* Later calls pass through; one that re-arms counts as a check. */
    now=30.0;memcpy(tls+0x300,&now,8);stats.issued=8;pt_run(test_pt_original,player,tls);
    assert(test_pt_seen==26.5+PT_FIRST && pt_rebased==1 && pt_admitted==2 && !pt_fires);
    now=41.5;memcpy(tls+0x300,&now,8);stats.issued=900;test_pt_rearm=now+60+3.25;pt_run(test_pt_original,player,tls);
    assert(pt_fires==1 && pt_last_fire==900 && pt_next==test_pt_bits(now+60+3.25) && pt_rebased==1 && test_pt_seen==26.5+PT_FIRST);
    stats.issued=901;pt_run(test_pt_original,player,tls);assert(pt_fires==1 && pt_last_fire==900); /* the same value again */
    test_pt_rearm=0;
    /* Each Player object once; beyond the table: counted, passed through. */
    memcpy(other+PT_DUE,&loading,8);pt_run(test_pt_original,other,tls);
    assert(pt_rebased==2 && test_pt_seen==now+PT_FIRST && pt_before==test_pt_bits(loading) && pt_first==7 && pt_object_count==2);
    unsigned char extra[2][TEST_PT_OBJECT];memset(extra,0,sizeof(extra));
    pt_run(test_pt_original,extra[0],tls);pt_run(test_pt_original,extra[1],tls);assert(pt_object_count==PT_OBJECTS && pt_rebased==4);
    unsigned char *spill=calloc(1,TEST_PT_OBJECT);assert(spill);memcpy(spill+PT_DUE,&loading,8);
    pt_run(test_pt_original,spill,tls);assert(pt_full==1 && pt_rebased==4 && test_pt_seen==loading);
    char status[512];int n=pt_status(status,sizeof(status));
    assert(n>0 && strstr(status,"installed=0 calls=15 admitted=8 rebased=4 first=7 ") && strstr(status," fires=1 last_fire=900 ") &&
           strstr(status," objects=4 unreadable=3 full=1 restored=0 stage=idle failures=0"));
    /* Arming: before the first admitted frame, on the MoveWorker; pins (zero image refuses). */
    test_pt_reset();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;
    stats.issued=1;assert(!pt_arm());stats.issued=0;
    ui_thread=GetCurrentThreadId()+1;assert(!pt_arm());ui_thread=GetCurrentThreadId();
    boot_phase=1;assert(!pt_arm());boot_phase=0;stats.active=0;assert(!pt_arm());stats.active=1;
    assert(!pt_arm() && !strcmp(pt_stage,"pins") && !pt_installed);
    /* Ownership and restore on a hand-set entry. */
    void *saved_entry;memcpy(&saved_entry,image+PT_ENTRY,8);
    *(void**)(image+PT_ENTRY)=(void*)pt_move;pt_installed=1;assert(pt_owned() && pt_arm()); /* rerun after a reload */
    *(void**)(image+PT_ENTRY)=(void*)test_pt_original;assert(!pt_owned() && !pt_check() && !strcmp(pt_stage,"entry_changed"));
    assert(!pt_check());pt_failures=0;pt_restore();
    assert(pt_installed && !pt_restored && *(void**)(image+PT_ENTRY)==(void*)test_pt_original); /* a foreign value stays */
    *(void**)(image+PT_ENTRY)=(void*)pt_move;pt_restore();assert(!pt_installed && pt_restored && *(void**)(image+PT_ENTRY)==image+PT_MOVE);
    assert(pt_owned());
    /* finish() gives the entry back. */
    *(void**)(image+PT_ENTRY)=(void*)pt_move;pt_installed=1;pt_restored=0;uint32_t saved_reason=stats.reason;stats.active=1;stats.restored=1;
    finish(PT_STOP_REASON);assert(stats.reason==PT_STOP_REASON && !pt_installed && pt_restored && *(void**)(image+PT_ENTRY)==image+PT_MOVE);
    /* Status bounds at the widest values. */
    pt_installed=1;pt_calls=pt_failures=INT64_MAX;pt_admitted=pt_rebased=pt_first=pt_fires=pt_last_fire=pt_unreadable=pt_full=UINT64_MAX;
    pt_before=pt_after=pt_autopilot=pt_next=UINT64_MAX;pt_object_count=PT_OBJECTS;pt_stage="block_changed";
    n=pt_status(status,sizeof(status));assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE PLAYER_TIMER_STOP \n")<512);
    memcpy(image+PT_ENTRY,&saved_entry,8);test_pt_reset();free(player);free(other);free(spill);
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    stats.reason=saved_reason;stats.restored=0;
    printf("player timer re-base/pass-through/checks/gating/table/arming/ownership/restore/finish/status tests PASS\n");
}
/* With the X4 9.00 executable: the entry in U::Player's vtable, the pinned
 * blocks and what they do, the import restoring only the autopilot deadline,
 * tamper refusals and a full arm/restore on the copy. */
static void test_player_timer_native_image(const char *path) {
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
    uint64_t base=nt.OptionalHeader.ImageBase;
    /* The file holds preferred-base pointers: slot 674 of the vtable whose locator names U::Player. */
    assert(*(uint64_t*)(image+PT_ENTRY)==base+PT_MOVE && PT_ENTRY-0x2b329a8u==674u*8u);
    uint64_t locator=*(uint64_t*)(image+0x2b329a8u-8u)-base;uint32_t type=*(uint32_t*)(image+locator+12);
    assert(!strcmp((const char*)image+type+16,".?AVPlayer@U@@"));
    /* Positional::Move calls it as [vtable+0x1510] with the child alone. */
    assert(!memcmp(image+0x748650,"\xff\x90\x10\x15\x00\x00",6));
    *(void**)(image+PT_ENTRY)=image+PT_MOVE;assert(pt_pins());
    /* The check: movsd xmm0,[r15+0x8a538]; comisd xmm0,[r12+rsi]; ja (not due); the re-arm draws
     * FloatState(TLS+0x318, [0x2cbec4c] = 10) and stores time + [0x2cbeac8] = 60 + draw. */
    assert(!memcmp(image+PT_CHECK,"\xf2\x41\x0f\x10\x87\x38\xa5\x08\x00\x66\x41\x0f\x2f\x04\x34\x0f\x87",17));
    assert(!memcmp(image+0x7133e0,"\xb9\x18\x03\x00\x00",5) && image[0x7133f0]==0xe8 && 0x7133f5+*(int32_t*)(image+0x7133f1)==0x14ddc90);
    assert(0x7133f0+*(int32_t*)(image+0x7133ec)==0x2cbec4c && *(float*)(image+0x2cbec4c)==10.0f);
    assert(0x713403+*(int32_t*)(image+0x7133ff)==0x2cbeac8 && *(double*)(image+0x2cbeac8)==60.0);
    assert(!memcmp(image+0x71340b,"\xf2\x41\x0f\x11\x8f\x38\xa5\x08\x00",9));
    /* The constructor's first deadline: time + [0x2cbe9f8] = 10 + FloatState(word, 10), unkeyed while loading. */
    assert(image[0x6fae76]==0xe8 && 0x6fae7b+*(int32_t*)(image+0x6fae77)==0x14ddc90);
    assert(0x6fae89+*(int32_t*)(image+0x6fae85)==0x2cbe9f8 && *(double*)(image+0x2cbe9f8)==10.0);
    assert(!memcmp(image+0x6fae91,"\xf2\x0f\x11\x8e\x38\xa5\x08\x00",8));
    /* The import (vfunc568) stores the saved autopilot deadline +0x8a540. */
    assert(!memcmp(image+0x6fd6be,"\xf2\x41\x0f\x11\x84\x24\x40\xa5\x08\x00",10));
    image[PT_MOVE+0x20]^=1;assert(!pt_pins());image[PT_MOVE+0x20]^=1;
    image[PT_CHECK+0x60]^=1;assert(!pt_pins());image[PT_CHECK+0x60]^=1;
    image[PT_INIT+0x10]^=1;assert(!pt_pins());image[PT_INIT+0x10]^=1;
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    test_pt_reset();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    assert(pt_arm() && pt_installed && pt_owned() && pt_check() && pt_arm() && *(void**)(image+PT_ENTRY)==(void*)pt_move);
    image[PT_CHECK+0x10]^=1;assert(!pt_check() && !strcmp(pt_stage,"block_changed"));image[PT_CHECK+0x10]^=1;pt_failures=0;
    pt_restore();assert(pt_restored && !pt_installed && *(void**)(image+PT_ENTRY)==image+PT_MOVE);
    /* A foreign entry refuses arming and stays. */
    *(void**)(image+PT_ENTRY)=image+PT_MOVE+0x10;test_pt_reset();
    assert(!pt_arm() && !strcmp(pt_stage,"entry") && *(void**)(image+PT_ENTRY)==image+PT_MOVE+0x10);
    test_pt_reset();
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("player timer native entry (U::Player slot 674), check/re-arm and constructor pins, import, tamper refusals and arm/restore verified");
}
