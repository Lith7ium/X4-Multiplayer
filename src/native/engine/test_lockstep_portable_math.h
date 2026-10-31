/* native_gate_portable_math: stubs, entry CAS with the other threads frozen, the
 * entry-window guard, check, restore and arm refusals on the synthetic image; with
 * X4.exe: the pins, and the CRT entries and helpers executed in a relocated copy of
 * the image before, during and after the redirection. */
static DWORD WINAPI test_pmath_spin(LPVOID unused) {(void)unused;for(;;) Sleep(1);}
static void test_portable_math(void) {
    unsigned char *saved_page=pmath_page;uint32_t saved_active=stats.active;uint64_t saved_issued=stats.issued;
    DWORD saved_ui=ui_thread;int saved_boot=boot_mode,saved_phase=boot_phase;
    static const unsigned char prologue[5]={0x48,0x83,0xec,0x68,0x90};
    for(unsigned i=0;i<PMATH_COUNT;i++) {memcpy(image+pmath_sites[i].rva,prologue,5);memcpy(pmath_original[i],prologue,5);}
    pmath_page=pmath_alloc_page();assert(pmath_page && pmath_write_page() && pmath_page_ok());
    for(unsigned i=0;i<PMATH_COUNT;i++) assert(pmath_state(i)==0);
    /* Freeze/thaw with a live thread; then every entry is a jmp to its stub and every stub to its target. */
    HANDLE other=CreateThread(NULL,0,test_pmath_spin,NULL,0,NULL);assert(other);Sleep(5);
    assert(pmath_switch(1)==1 && !pmath_frozen_count);
    for(unsigned i=0;i<PMATH_COUNT;i++) {
        unsigned char *entry=image+pmath_sites[i].rva;int32_t rel;memcpy(&rel,entry+1,4);
        assert(pmath_state(i)==1 && entry[0]==0xe9 && entry+5+rel==pmath_page+i*PMATH_STUB);
        unsigned char *stub=pmath_page+i*PMATH_STUB;void *target;memcpy(&target,stub+6,8);
        assert(stub[0]==0xff && stub[1]==0x25 && !stub[2] && !stub[5] && target==pmath_sites[i].target);
    }
    pmath_installed=1;pmath_failures=0;assert(pmath_owned() && pmath_check());
    image[pmath_sites[3].rva+2]^=1;assert(!pmath_owned());image[pmath_sites[3].rva+2]^=1;assert(pmath_owned());
    DWORD old,ignored;assert(VirtualProtect(pmath_page,0x1000,PAGE_READWRITE,&old));pmath_page[7*PMATH_STUB+9]^=1;
    assert(!pmath_owned() && !pmath_check() && !strcmp(pmath_stage,"site_changed"));
    pmath_page[7*PMATH_STUB+9]^=1;assert(VirtualProtect(pmath_page,0x1000,PAGE_EXECUTE_READ,&ignored));pmath_failures=0;
    char out[320];assert(pmath_status(out,sizeof(out))>0 && strstr(out,"installed=1 sites=29 "));
    pmath_restore();assert(!pmath_installed && pmath_restored && !strcmp(pmath_stage,"restored"));
    for(unsigned i=0;i<PMATH_COUNT;i++) assert(pmath_state(i)==0 && !memcmp(image+pmath_sites[i].rva,prologue,5));
    /* A thread standing inside a 5-byte window blocks the switch; nothing is written. */
    unsigned rva=pmath_sites[5].rva;assert(VirtualProtect(image+rva,16,PAGE_EXECUTE_READWRITE,&old));
    image[rva+2]=0xeb;image[rva+3]=0xfe; /* jmp $ */
    HANDLE parked=CreateThread(NULL,0,(LPTHREAD_START_ROUTINE)(void*)(image+rva+2),NULL,0,NULL);assert(parked);Sleep(20);
    memcpy(pmath_original[5],image+rva,5);
    unsigned busy=pmath_busy;assert(pmath_switch(1)==0 && pmath_busy>=busy+200);
    for(unsigned i=0;i<PMATH_COUNT;i++) assert(pmath_state(i)==0);
    TerminateThread(parked,0);WaitForSingleObject(parked,INFINITE);CloseHandle(parked);
    memcpy(image+rva,prologue,5);memcpy(pmath_original[5],prologue,5);VirtualProtect(image+rva,16,old,&ignored);
    TerminateThread(other,0);WaitForSingleObject(other,INFINITE);CloseHandle(other);
    /* Arm refusals: state, then the pins of the synthetic image. */
    stats.active=1;stats.issued=0;boot_mode=1;boot_phase=0;ui_thread=GetCurrentThreadId();
    assert(!pmath_arm() && !strcmp(pmath_stage,"pins") && pmath_kat_value==X4PM_KAT_EXPECTED);
    stats.issued=1;assert(!pmath_arm() && !strcmp(pmath_stage,"state"));stats.issued=0;
    boot_phase=2;assert(!pmath_arm());boot_phase=0;ui_thread=GetCurrentThreadId()+1;assert(!pmath_arm());
    /* The lane adapters. */
    float s,c,s2,c2;x4pm_sincosf(0.5f,&s,&c);x4pm_sincosf(-2.0f,&s2,&c2);
    float lanes[4];_mm_storeu_ps(lanes,pmath_sincosf_two(0.5f,-2.0f));
    assert(!memcmp(&lanes[0],&s,4) && !memcmp(&lanes[1],&c,4) && !memcmp(&lanes[2],&s2,4) && !memcmp(&lanes[3],&c2,4));
    _mm_storeu_ps(lanes,pmath_sincosf_one(-2.0f));assert(!memcmp(&lanes[0],&s2,4) && !memcmp(&lanes[1],&c2,4) && lanes[2]==0.0f);
    double ds,dc,dl[2];x4pm_sincos(3.0,&ds,&dc);_mm_storeu_pd(dl,pmath_sincos_one(3.0));assert(dl[0]==ds && dl[1]==dc);
    for(unsigned i=0;i<PMATH_COUNT;i++) memset(image+pmath_sites[i].rva,0,5);
    VirtualFree(pmath_page,0,MEM_RELEASE);pmath_page=saved_page;pmath_restored=0;pmath_stage="idle";pmath_busy=pmath_attempts=0;
    stats.active=saved_active;stats.issued=saved_issued;ui_thread=saved_ui;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("portable math stubs/freeze/window guard/check/restore/arm/lane tests PASS\n");
}

/* X4.exe mapped, relocated and executable, so the pinned CRT entries can be called in place. */
static unsigned char *test_pmath_map(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *m=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(m);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(m+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    IMAGE_DATA_DIRECTORY reloc=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    uint64_t delta=(uint64_t)(uintptr_t)m-nt.OptionalHeader.ImageBase;unsigned applied=0;
    for(unsigned at=0;at+8<=reloc.Size;) {
        IMAGE_BASE_RELOCATION *block=(IMAGE_BASE_RELOCATION*)(m+reloc.VirtualAddress+at);
        if(block->SizeOfBlock<8) break;
        WORD *entries=(WORD*)(block+1);unsigned count=(block->SizeOfBlock-8)/2;
        for(unsigned i=0;i<count;i++) if(entries[i]>>12==IMAGE_REL_BASED_DIR64) {
            *(uint64_t*)(m+block->VirtualAddress+(entries[i]&0xfff))+=delta;++applied;
        }
        at+=block->SizeOfBlock;
    }
    assert(applied>100000);
    return m;
}
typedef float (*test_pmath_f1)(float);typedef float (*test_pmath_f2)(float,float);
typedef double (*test_pmath_d1)(double);typedef double (*test_pmath_d2)(double,double);
typedef __m128 (*test_pmath_p1)(float);typedef __m128 (*test_pmath_p2)(float,float);typedef __m128d (*test_pmath_pd)(double);
static const float test_pmath_inputs[][2]={{0.5f,2.0f},{1.25f,-0.75f},{-3.0f,0.3f},{100.0f,1.5f},{1e-3f,7.0f},{0.875f,0.125f}};
#define TEST_PMATH_N (sizeof(test_pmath_inputs)/sizeof(*test_pmath_inputs))
/* Inside each domain and without overflow: the native error paths need the CRT's per-thread data. */
static void test_pmath_args(const char *name,float *x) {
    if(!strncmp(name,"asin",4) || !strncmp(name,"acos",4)) *x=*x/128.0f;
    if(!strncmp(name,"log",3) || !strncmp(name,"pow",3)) *x=*x<0?-*x:*x;
    if(!strncmp(name,"exp",3)) *x=*x/8.0f;
}
/* Every entry at every input: 4 lanes of bits (unused lanes 0). */
static void test_pmath_run(uint32_t out[PMATH_COUNT][TEST_PMATH_N][4]) {
    memset(out,0,sizeof(uint32_t)*PMATH_COUNT*TEST_PMATH_N*4);
    for(unsigned i=0;i<PMATH_COUNT;i++) for(unsigned k=0;k<TEST_PMATH_N;k++) {
        void *f=image+pmath_sites[i].rva;float x=test_pmath_inputs[k][0],y=test_pmath_inputs[k][1];
        test_pmath_args(pmath_sites[i].name,&x);
        float rf;double rd;float lanes[4];double dl[2];
        switch(pmath_sites[i].kind) {
        case PMATH_F1:rf=((test_pmath_f1)f)(x);memcpy(&out[i][k][0],&rf,4);break;
        case PMATH_F2:rf=((test_pmath_f2)f)(x,y);memcpy(&out[i][k][0],&rf,4);break;
        case PMATH_D1:rd=((test_pmath_d1)f)((double)x);memcpy(&out[i][k][0],&rd,8);break;
        case PMATH_D2:rd=((test_pmath_d2)f)((double)x,(double)y);memcpy(&out[i][k][0],&rd,8);break;
        case PMATH_P1:_mm_storeu_ps(lanes,((test_pmath_p1)f)(x));memcpy(out[i][k],lanes,8);break;
        case PMATH_P2:_mm_storeu_ps(lanes,((test_pmath_p2)f)(x,y));memcpy(out[i][k],lanes,16);break;
        case PMATH_PD:_mm_storeu_pd(dl,((test_pmath_pd)f)((double)x));memcpy(out[i][k],dl,16);break;
        }
    }
}
static double test_pmath_value(const pmath_site *s,const uint32_t bits[4],unsigned lane) {
    float f;double d;
    if(s->kind==PMATH_D1 || s->kind==PMATH_D2 || s->kind==PMATH_PD) {memcpy(&d,bits+2*lane,8);return d;}
    memcpy(&f,bits+lane,4);return f;
}
static void test_portable_math_native_image(const char *path) {
    unsigned char *prior=image,*saved_page=pmath_page;image=test_pmath_map(path);pmath_page=NULL;
    assert(pmath_pins());
    for(unsigned i=0;i<PMATH_COUNT;i++) memcpy(pmath_original[i],image+pmath_sites[i].rva,5);
    /* The CRT keeps its SSE2 bodies here (__use_fma3_lib is 0 before CRT start-up). */
    int flag=-1;memcpy(&flag,image+PMATH_FMA3_FLAG,4);assert(flag==0);
    static uint32_t native[PMATH_COUNT][TEST_PMATH_N][4],ours[PMATH_COUNT][TEST_PMATH_N][4],back[PMATH_COUNT][TEST_PMATH_N][4];
    test_pmath_run(native);
    /* Native results name the functions the table claims (loose: the CRT is not the reference). */
    for(unsigned i=0;i<PMATH_COUNT;i++) for(unsigned k=0;k<TEST_PMATH_N;k++) {
        const pmath_site *s=&pmath_sites[i];float fx=test_pmath_inputs[k][0];test_pmath_args(s->name,&fx);
        double x=fx,y=test_pmath_inputs[k][1];
        double want[4]={0},got;unsigned lanes=1;const char *n=s->name;
        if(!strcmp(n,"sincosf_one") || !strcmp(n,"sincos_one")) {want[0]=sin(x);want[1]=cos(x);lanes=2;}
        else if(!strcmp(n,"sincosf_two")) {want[0]=sin(x);want[1]=cos(x);want[2]=sin((float)y);want[3]=cos((float)y);lanes=4;}
        else if(!strncmp(n,"sin",3)) want[0]=sin(x);
        else if(!strncmp(n,"cos",3)) want[0]=cos(x);
        else if(!strncmp(n,"tan",3)) want[0]=tan(x);
        else if(!strncmp(n,"asin",4)) want[0]=asin(x);
        else if(!strncmp(n,"acos",4)) want[0]=acos(x);
        else if(!strncmp(n,"atan2",5)) want[0]=atan2(x,y);
        else if(!strncmp(n,"atan",4)) want[0]=atan(x);
        else if(!strncmp(n,"exp2",4)) want[0]=exp2(x);
        else if(!strncmp(n,"exp",3)) want[0]=exp(x);
        else if(!strncmp(n,"log10",5)) want[0]=log10(x);
        else if(!strncmp(n,"log2",4)) want[0]=log2(x);
        else if(!strncmp(n,"log",3)) want[0]=log(x);
        else if(!strncmp(n,"pow",3)) want[0]=pow(x,y);
        else assert(!"unclassified entry");
        for(unsigned lane=0;lane<lanes;lane++) {
            got=test_pmath_value(s,native[i][k],lane);
            double tolerance=s->kind==PMATH_P1 || s->kind==PMATH_P2?2e-5:1e-5*(fabs(want[lane])>1?fabs(want[lane]):1);
            if(fabs(got-want[lane])>tolerance) {printf("native %s(%g,%g) lane %u = %.9g, want %.9g\n",n,x,y,lane,got,want[lane]);assert(0);}
        }
    }
    /* Redirected: every entry returns the portable library's bits. */
    pmath_page=pmath_alloc_page();assert(pmath_page && pmath_write_page());
    assert(pmath_switch(1)==1);pmath_installed=1;assert(pmath_owned() && pmath_pins());
    /* Other modules' pins over these functions (world position RNG hashes acosf, sinf and
     * 0x1750210 whole) pass while installed: hash_ok, read_local and sample_read see the
     * originals; the raw bytes are our jumps. */
    {   static unsigned char viewed[0x2000];
        for(unsigned i=0;i<PMATH_COUNT;i++) {
            const pmath_site *s=&pmath_sites[i];assert(image[s->rva]==0xe9 && hash_ok(image+s->rva,s->size,s->sha));
            assert(read_local(image+s->rva,viewed,s->size) && hash_ok(viewed,s->size,s->sha));
            unsigned char around[9];assert(sample_read(image+s->rva-2,around,9) && !memcmp(around+2,pmath_original[i],5));
        }
    }
    test_pmath_run(ours);
    for(unsigned i=0;i<PMATH_COUNT;i++) for(unsigned k=0;k<TEST_PMATH_N;k++) {
        const pmath_site *s=&pmath_sites[i];uint32_t want[4]={0};float x=test_pmath_inputs[k][0],y=test_pmath_inputs[k][1];
        test_pmath_args(s->name,&x);
        float rf;double rd;float lanes[4];double dl[2];
        switch(s->kind) {
        case PMATH_F1:rf=((test_pmath_f1)s->target)(x);memcpy(want,&rf,4);break;
        case PMATH_F2:rf=((test_pmath_f2)s->target)(x,y);memcpy(want,&rf,4);break;
        case PMATH_D1:rd=((test_pmath_d1)s->target)((double)x);memcpy(want,&rd,8);break;
        case PMATH_D2:rd=((test_pmath_d2)s->target)((double)x,(double)y);memcpy(want,&rd,8);break;
        case PMATH_P1:_mm_storeu_ps(lanes,((test_pmath_p1)s->target)(x));memcpy(want,lanes,8);break;
        case PMATH_P2:_mm_storeu_ps(lanes,((test_pmath_p2)s->target)(x,y));memcpy(want,lanes,16);break;
        case PMATH_PD:_mm_storeu_pd(dl,((test_pmath_pd)s->target)((double)x));memcpy(want,dl,16);break;
        }
        assert(!memcmp(ours[i][k],want,16));
    }
    /* Restored: the native results again, bit for bit. */
    pmath_restore();assert(!pmath_installed && pmath_pins() && !InterlockedCompareExchange(&pmath_any,0,0));
    test_pmath_run(back);assert(!memcmp(native,back,sizeof(native)));
    VirtualFree(pmath_page,0,MEM_RELEASE);pmath_page=saved_page;pmath_restored=0;pmath_stage="idle";
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("portable math: 29 pinned CRT entries and helpers executed natively, redirected and restored; other modules' pins over them pass through the original-byte view");
}
