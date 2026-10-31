/* native_gate_economy_load_rng: the schedule draw at import runs on a key from the saved ID and the saved last run, whatever
 * the loading thread's word; the word comes back; unmapped containers by import order; failures; site CAS/check/restore; the pinned build. */
/* The thread's word: what the entry sees (test_eco_word_ptr, NULL for none) and what the native draws on. */
static uint64_t test_eco_word_value,*test_eco_word_ptr;
static uint64_t *test_eco_word(void) {return test_eco_word_ptr;}
static uint64_t test_eco_seen;static unsigned test_eco_calls;
/* What 0x430cc0 does, shape only: one PCG step on the thread's word, then +0xa38 = last + 600 - (word % 120). */
static void test_eco_native(void *container,double last) {
    ++test_eco_calls;uint64_t *w=&test_eco_word_value;test_eco_seen=*w;
    *w=*w*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;*w=(*w>>30)|(*w<<34);
    double next=last+600.0-(double)(*w%120);if(container) memcpy((unsigned char*)container+0xa38,&next,8);
}
static void test_eco_throw(void *container,double last) {(void)container;(void)last;RaiseException(0xe0454330,0,0,NULL);}
static double test_eco_next(const unsigned char *container) {double v;memcpy(&v,container+0xa38,8);return v;}
static void test_economy_load(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    world_identity_entry *prior=world_identities;LONG64 prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(world_identity_entry)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    stats.active=1;stats.issued=0;ui_thread=GetCurrentThreadId();
    eco_native=test_eco_native;eco_word_fn=test_eco_word;test_eco_word_ptr=&test_eco_word_value;
    eco_calls=eco_keyed=eco_unmapped=eco_threads=eco_failures=0;eco_draws=eco_keyset=eco_nextset=0;
    /* Station 139b9 imported on two clients whose loading words differ: the same schedule, and each word comes back. */
    static unsigned char a[0xa40],b[0xa40];*(uint64_t*)(a+8)=0x5001;*(uint64_t*)(b+8)=0x6001;
    assert(world_identity_record(0x5001,0x139b9) && world_identity_record(0x6001,0x42df2));
    double last=538.25;
    test_eco_word_value=0x1111;eco_entry(a,last);double first=test_eco_next(a);uint64_t key=test_eco_seen;
    assert(test_eco_word_value==0x1111 && key!=0x1111 && test_eco_calls==1);
    uint64_t sums[2]={eco_keyset,eco_nextset};
    eco_keyset=eco_nextset=0;test_eco_word_value=0x2222;memset(a+0xa38,0,8);eco_entry(a,last);
    assert(test_eco_next(a)==first && test_eco_seen==key && test_eco_word_value==0x2222);
    assert(eco_keyset==sums[0] && eco_nextset==sums[1]); /* two clients' sums agree */
    /* What the key holds: the saved ID and the last run; another station or another last run, another key. */
    eco_entry(b,last);assert(test_eco_seen!=key);
    eco_entry(a,last+1.0);assert(test_eco_seen!=key);
    double bits_last=last;uint64_t bits;memcpy(&bits,&bits_last,8);
    assert(key==lockstep_seed(ECO_DOMAIN,0,0x139b9,bits,0,0));
    assert(eco_keyed==4 && eco_draws==4 && !eco_unmapped && !eco_failures);
    /* No saved ID: keyed by the n-th such import, so two clients reading the save in one order agree. */
    static unsigned char c[0xa40];*(uint64_t*)(c+8)=0x7001;
    eco_entry(c,last);uint64_t u1=test_eco_seen;eco_entry(c,last);uint64_t u2=test_eco_seen;
    assert(eco_unmapped==2 && u1!=u2 && u1==lockstep_seed(ECO_DOMAIN,0,ECO_UNMAPPED,bits,1,0) && !eco_failures);
    /* Fail closed: no word, then an unreadable container; the native call still runs once each time. */
    unsigned before=test_eco_calls;test_eco_word_ptr=NULL;
    eco_entry(a,last);assert(eco_failures==1 && !strcmp(eco_stage,"unreadable") && test_eco_calls==before+1);
    test_eco_word_ptr=&test_eco_word_value;eco_failures=0;eco_stage="idle";
    eco_entry(NULL,last);assert(eco_failures==1 && !strcmp(eco_stage,"unreadable") && test_eco_calls==before+2);
    eco_failures=0;eco_stage="idle";
    /* An exception through the scope: the word comes back and the gate stops. */
    eco_native=test_eco_throw;test_eco_word_value=0x3333;int caught=0;
    __try {eco_entry(a,last);} __except(GetExceptionCode()==0xe0454330?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=1;}
    assert(caught && test_eco_word_value==0x3333 && eco_failures==1 && !strcmp(eco_stage,"unwound"));
    eco_native=test_eco_native;eco_failures=0;eco_stage="idle";
    /* Another thread: keyed alike, counted. */
    ui_thread=GetCurrentThreadId()+1;eco_entry(a,last);assert(eco_threads==1 && test_eco_seen==key);ui_thread=GetCurrentThreadId();
    char out[512];assert(eco_status(out,sizeof(out))>0 && strstr(out," unmapped=2 threads=1 ") && strstr(out," failures=0"));
    /* Arming refusals on the zero image. */
    eco_installed=0;boot_mode=1;boot_phase=0;stats.issued=0;
    assert(!eco_arm() && !strcmp(eco_stage,"pins") && !eco_installed);boot_phase=1;assert(!eco_arm());boot_phase=0;
    /* Site CAS, ownership, restore. */
    unsigned char *saved_page=eco_page;eco_page=eco_alloc_page();assert(eco_page);DWORD old,ignored;
    image[ECO_SITE]=0xe8;int32_t d=eco_rel(image+ECO_SCHEDULE);memcpy(image+ECO_SITE+1,&d,4);assert(eco_state()==0);
    assert(VirtualProtect(eco_page,0x1000,PAGE_READWRITE,&old));eco_write_stub(eco_page);assert(VirtualProtect(eco_page,0x1000,PAGE_EXECUTE_READ,&ignored));
    assert(eco_cas(eco_rel(image+ECO_SCHEDULE),eco_rel(eco_page)) && eco_state()==1);eco_installed=1;assert(eco_owned());
    image[ECO_SITE+2]^=1;assert(eco_state()<0 && !eco_owned());image[ECO_SITE+2]^=1;assert(eco_owned());
    eco_restore();assert(!eco_installed && eco_restored && eco_state()==0);
    memset(image+ECO_SITE,0,5);eco_restored=0;VirtualFree(eco_page,0,MEM_RELEASE);eco_page=saved_page;
    eco_native=NULL;eco_word_fn=NULL;eco_stage="idle";
    eco_calls=eco_keyed=eco_unmapped=eco_threads=eco_failures=0;eco_draws=eco_keyset=eco_nextset=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("economy load key/word/unmapped/failures/site tests PASS\n");
}
static void test_economy_load_native_image(const char *path) {
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
    unsigned char *saved_page=eco_page;eco_page=NULL;
    assert(eco_state()==0 && eco_pins());
    /* The call: last run in xmm1 (0x3f0d8f movaps xmm1,xmm0), container in rcx (mov rcx,r15), just stored to +0xa40. */
    assert(!memcmp(image+0x3f0d86,"\xf2\x41\x0f\x11\x87\x40\x0a\x00\x00\x0f\x28\xc8\x49\x8b\xcf",15));
    /* After it the caller writes r8, edx and rcx before reading them. */
    assert(!memcmp(image+0x3f0d9a,"\x45\x33\xc0\xba\xa3\x01\x00\x00\x48\x8b\xcf",11));
    /* 0x430cc0 draws on TLS slot0+0x318 through FloatRange and writes +0xa38. */
    assert(!memcmp(image+0x430ce5,"\xb9\x18\x03\x00\x00",5) && image[0x430cfd]==0xe8 && 0x430d02u+*(int32_t*)(image+0x430cfe)==ECO_FLOAT_RANGE);
    assert(!memcmp(image+0x430d1f,"\xf2\x0f\x11\xb7\x38\x0a\x00\x00",8));
    /* The component import reads the ID (0x79f720) before the container import (vtable slot 568, 0x11c0). */
    assert(image[0x8e5685]==0xe8 && 0x8e568au+*(int32_t*)(image+0x8e5686)==0x79f720u);
    assert(!memcmp(image+0x8e602c,"\x49\x8b\xd7\x49\x8b\xce\xff\x90\xc0\x11\x00\x00",12));
    image[ECO_SITE-3]^=1;assert(!eco_pins());image[ECO_SITE-3]^=1;assert(eco_pins());
    image[ECO_SCHEDULE+0x40]^=1;assert(!eco_pins());image[ECO_SCHEDULE+0x40]^=1;assert(eco_pins());
    /* Armed for real on this copy: the call routed, the pins hold with it patched, restored at stop. */
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    world_identity_entry *prior_ids=world_identities;static world_identity_entry one[1];world_identities=one;
    stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();eco_installed=0;
    assert(eco_arm()==1 && eco_installed && eco_state()==1 && eco_pins() && eco_owned() && eco_check() && eco_arm()==1);
    eco_restore();assert(!eco_installed && eco_restored && eco_state()==0 && eco_pins());
    eco_restored=0;eco_stage="idle";world_identities=prior_ids;
    assert(VirtualFree(eco_page,0,MEM_RELEASE));eco_page=saved_page;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("economy load site, arguments, draw and import order verified");
}
