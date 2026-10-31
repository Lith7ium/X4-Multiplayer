/* native_gate_order_canon: candidates in id order at the picks, the table comparator by id for wares and macros, native for
 * the rest, both modes, rows, the comparator stub run for real, arming refusals, site CAS/check/restore, the pinned build.
 * native_gate_address_order: the walk, address order, rows, arm/tick sampling. */
static unsigned test_oc_calls;
static uint64_t test_oc_first;
static uint64_t test_oc_pick(void *a,void *b,void *c,void *d,void *e,void *f,void *g,void *h) {
    (void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;++test_oc_calls;
    const unsigned char *begin=*(unsigned char **)a;test_oc_first=*(const uint64_t*)(begin+8);return 42;
}
__asm__(
".text\n"
"test_oc_enter:\n push %rbx\n push %rdi\n push %r12\n mov %rdx,%rbx\n mov %r8,%rdi\n jmp *%rcx\n"
"test_oc_back:\n movzx %al,%eax\n pop %r12\n pop %rdi\n pop %rbx\n ret\n"
);
extern uint64_t test_oc_enter(const void *stub,const void *a,const void *b);
extern char test_oc_back[];
typedef struct {unsigned char pad[0x10];char sso[16];uint64_t size,cap;} tll_ware;
static void test_order_canon(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    stats.active=1;boot_mode=1;boot_phase=3;stats.issued=187;ui_thread=GetCurrentThreadId();
    oc_row_t *saved_ring=oc_ring;oc_ring=VirtualAlloc(NULL,sizeof(oc_row_t)*OC_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(oc_ring);
    oc_rows=0;oc_from=150;oc_to=200;oc_failures=0;oc_picks_seen=oc_moved=oc_less_ware=oc_less_macro=oc_inverted_ware=oc_inverted_macro=0;
    tll_macro_set(0,"ship_gen_xs_repairdrone_empty_01_a_macro");tll_macro_set(1,"ship_gen_xs_cargodrone_empty_01_a_macro");tll_macro_set(2,"zz_short");
    /* Candidates in address order repair, cargo, short. 'observe' leaves them and says id order differs. */
    unsigned char candidates[3][0x28];memset(candidates,0,sizeof(candidates));
    for(unsigned i=0;i<3;i++) {*(const void**)(candidates[i]+8)=&tll_macros[i];candidates[i][0x20]=(unsigned char)(i+1);}
    unsigned char *range[3]={candidates[0],candidates[0]+3*0x28,NULL};
    oc_native=test_oc_pick;test_oc_calls=0;oc_mode=OC_OBSERVE;
    assert(oc_pick(0x5065fe,range,NULL,NULL,NULL,NULL,NULL,NULL,NULL)==42 && test_oc_calls==1 && test_oc_first==(uint64_t)(uintptr_t)&tll_macros[0]);
    assert(candidates[0][0x20]==1 && oc_moved==1 && oc_rows==1);
    char out[512];assert(oc_row(0,out,sizeof(out))>0);
    assert(strstr(out,"tick=187 site=5065fe count=3 moved=1 macros=ship_gen_xs_repairdrone_empty_01_a_macr@") && strstr(out,",zz_short@"));
    /* 'canonical': sorted whole before the call. */
    oc_mode=OC_CANONICAL;
    assert(oc_pick(0x506638,range,NULL,NULL,NULL,NULL,NULL,NULL,NULL)==42 && test_oc_first==(uint64_t)(uintptr_t)&tll_macros[1]);
    assert(candidates[0][0x20]==2 && candidates[1][0x20]==1 && candidates[2][0x20]==3 && oc_moved==2 && oc_rows==2 && !oc_failures);
    /* Already in order: not moved. One candidate: nothing to do, no row. */
    assert(oc_pick(0x506638,range,NULL,NULL,NULL,NULL,NULL,NULL,NULL)==42 && oc_moved==2 && oc_rows==3);
    assert(oc_row(2,out,sizeof(out))>0 && strstr(out," moved=0 "));
    unsigned char *one[3]={candidates[0],candidates[0]+0x28,NULL};
    assert(oc_pick(0x506638,one,NULL,NULL,NULL,NULL,NULL,NULL,NULL)==42 && oc_rows==3);
    /* A candidate without an id stops 'canonical', and the pick still runs. */
    *(const void**)(candidates[1]+8)=NULL;
    assert(oc_pick(0x506638,range,NULL,NULL,NULL,NULL,NULL,NULL,NULL)==42 && oc_failures==1 && !strcmp(oc_stage,"name"));oc_failures=0;oc_stage="idle";
    /* Outside the window: counted, no row. */
    *(const void**)(candidates[0]+8)=&tll_macros[0];*(const void**)(candidates[1]+8)=&tll_macros[1];*(const void**)(candidates[2]+8)=&tll_macros[2];
    stats.issued=201;oc_mode=OC_OBSERVE;LONG64 before=oc_rows;uint64_t seen=oc_moved;
    assert(oc_pick(0x506638,range,NULL,NULL,NULL,NULL,NULL,NULL,NULL)==42 && oc_rows==before && oc_moved==seen+1);stats.issued=187;
    /* The comparator: wares by the id at +0x10, macros by +0x20, ties and everything else by value. */
    static tll_ware ware_low,ware_high;memset(&ware_low,0,sizeof(ware_low));memset(&ware_high,0,sizeof(ware_high));
    /* ware_low has the lower address only if the statics are laid out so: use the pointers as they are. */
    memcpy(ware_low.sso,"zinc",4);ware_low.size=4;ware_low.cap=15;memcpy(ware_high.sso,"alpha",5);ware_high.size=5;ware_high.cap=15;
    tll_ware *wa=&ware_low,*wb=&ware_high;
    struct {int type,pad;uint64_t value;} a={OC_TYPE_WARE,0,(uint64_t)(uintptr_t)wa},b={OC_TYPE_WARE,0,(uint64_t)(uintptr_t)wb};
    int raw=(uintptr_t)wa<(uintptr_t)wb;
    oc_mode=OC_OBSERVE;oc_less_ware=oc_inverted_ware=oc_inverted_macro=0;
    assert((int)oc_less((unsigned char*)&a,(unsigned char*)&b)==raw && oc_less_ware==1);
    assert(oc_inverted_ware==(raw?1u:0u) && !oc_inverted_macro);
    oc_mode=OC_CANONICAL;
    assert(oc_less((unsigned char*)&a,(unsigned char*)&b)==0); /* zinc after alpha */
    assert(oc_less((unsigned char*)&b,(unsigned char*)&a)==1);
    assert(oc_less((unsigned char*)&a,(unsigned char*)&a)==0);
    a.type=b.type=OC_TYPE_MACRO;a.value=(uint64_t)(uintptr_t)&tll_macros[0];b.value=(uint64_t)(uintptr_t)&tll_macros[1];
    assert(oc_less((unsigned char*)&a,(unsigned char*)&b)==0 && oc_less((unsigned char*)&b,(unsigned char*)&a)==1); /* repair after cargo */
    a.type=b.type=0xa;a.value=7;b.value=9;
    assert(oc_less((unsigned char*)&a,(unsigned char*)&b)==1 && oc_less((unsigned char*)&b,(unsigned char*)&a)==0);
    /* No id (or a null value): as native, without touching the pointer. */
    a.type=b.type=OC_TYPE_WARE;a.value=0;b.value=(uint64_t)(uintptr_t)wb;
    assert(oc_less((unsigned char*)&a,(unsigned char*)&b)==1 && oc_less((unsigned char*)&b,(unsigned char*)&a)==0);
    /* The same id on two objects: as native. */
    tll_ware twin=ware_high;a.value=(uint64_t)(uintptr_t)&twin;b.value=(uint64_t)(uintptr_t)wb;
    assert((int)oc_less((unsigned char*)&a,(unsigned char*)&b)==((uintptr_t)&twin<(uintptr_t)wb));
    /* A prefix sorts first. */
    tll_ware shorter;memset(&shorter,0,sizeof(shorter));memcpy(shorter.sso,"alp",3);shorter.size=3;shorter.cap=15;
    a.value=(uint64_t)(uintptr_t)&shorter;
    assert(oc_less((unsigned char*)&a,(unsigned char*)&b)==1 && oc_less((unsigned char*)&b,(unsigned char*)&a)==0);
    /* The comparator's stub, run for real: values in rbx and rdi, the answer in al at the exit. */
    unsigned char *page=VirtualAlloc(NULL,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(page);
    oc_write_less_stub(page,test_oc_back);
    DWORD old,ignored;assert(VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&old));FlushInstructionCache(GetCurrentProcess(),page,0x1000);
    a.value=(uint64_t)(uintptr_t)wa;b.value=(uint64_t)(uintptr_t)wb;
    assert(test_oc_enter(page,&b,&a)==1 && test_oc_enter(page,&a,&b)==0);
    VirtualFree(page,0,MEM_RELEASE);
    assert(oc_status(out,sizeof(out))>0 && strstr(out,"mode=canonical frame=187 ") && strstr(out," window=150-200 "));
    /* Arming refusals (mode and window first, then the zero image). */
    oc_installed=0;oc_mode=OC_OFF;stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;oc_stage="idle";
    assert(!oc_arm(7) && !strcmp(oc_stage,"mode"));
    assert(!oc_window(0,10) && !oc_window(10,5) && !oc_window(1,700) && oc_window(150,200) && oc_from==150);
    assert(!oc_arm(OC_OBSERVE) && !strcmp(oc_stage,"pins") && !oc_installed);
    /* Site CAS, ownership and restore on the plain image. */
    unsigned char *saved_page=oc_page;oc_page=oc_alloc_page();assert(oc_page);
    for(unsigned i=0;i<OC_PICKS;i++) {image[oc_picks[i].site]=0xe8;int32_t d=oc_pick_rel(i,image+OC_TARGET);memcpy(image+oc_picks[i].site+1,&d,4);assert(oc_pick_state(i)==0);}
    memcpy(image+OC_LESS_SITE,oc_less_orig,OC_LESS_SIZE);assert(oc_less_state()==0);
    assert(VirtualProtect(oc_page,0x1000,PAGE_READWRITE,&old));oc_write_code();assert(VirtualProtect(oc_page,0x1000,PAGE_EXECUTE_READ,&ignored));
    for(unsigned i=0;i<OC_PICKS;i++) {assert(oc_pick_cas(i,oc_pick_rel(i,image+OC_TARGET),oc_pick_rel(i,oc_page+OC_STUB_PICK)) && oc_pick_state(i)==1);oc_done|=1u<<i;}
    unsigned char patch[5]={0xe9};int32_t rel=oc_less_rel();memcpy(patch+1,&rel,4);
    assert(view_cas_bytes(OC_LESS_SITE,oc_less_orig,patch,5) && oc_less_state()==1);oc_done|=1u<<OC_PICKS;
    oc_installed=1;oc_mode=OC_CANONICAL;assert(oc_owned());
    image[oc_picks[1].site+2]^=1;assert(oc_pick_state(1)<0 && !oc_owned());image[oc_picks[1].site+2]^=1;assert(oc_owned());
    image[OC_LESS_SITE+2]^=1;assert(oc_less_state()<0 && !oc_owned());image[OC_LESS_SITE+2]^=1;assert(oc_owned());
    image[OC_LESS_SITE+6]^=1;assert(oc_less_state()<0);image[OC_LESS_SITE+6]^=1;
    oc_restore();assert(!oc_installed && oc_restored && !oc_done && oc_mode==OC_OFF);
    for(unsigned i=0;i<OC_PICKS;i++) {assert(oc_pick_state(i)==0);memset(image+oc_picks[i].site,0,5);}
    assert(oc_less_state()==0);memset(image+OC_LESS_SITE,0,OC_LESS_SIZE);
    VirtualFree(oc_page,0,MEM_RELEASE);oc_page=saved_page;
    oc_native=NULL;oc_restored=0;oc_stage="idle";oc_from=oc_to=0;VirtualFree(oc_ring,0,MEM_RELEASE);oc_ring=saved_ring;oc_rows=0;oc_failures=0;
    /* Address order: a map of three, ids listed by the objects' addresses, not by the tree. */
    static unsigned char objects[3][16];
    tll_map map;tll_node na,nb,nc;tll_map3(&map,&na,&nb,&nc);
    na.key=(const void*)(uintptr_t)0xaaaa;nb.key=(const void*)(uintptr_t)0xbbbb;nc.key=(const void*)(uintptr_t)0xcccc;
    /* The tree in id order a,b,c; the objects in address order c,a,b. */
    *(uint64_t*)&na.a28=(uint64_t)(uintptr_t)&objects[1];*(uint64_t*)&nb.a28=(uint64_t)(uintptr_t)&objects[2];*(uint64_t*)&nc.a28=(uint64_t)(uintptr_t)&objects[0];
    ao_entry_t entries[8];int partial=1;
    assert(ao_walk((const unsigned char*)&map,entries,8,&partial)==3 && !partial);
    assert(entries[0].id==0xaaaa && entries[1].id==0xbbbb && entries[2].id==0xcccc);
    size_t got=ao_walk((const unsigned char*)&map,entries,2,&partial);assert(got==2 && partial);
    ao_row_t *saved_ao=ao_ring;ao_ring=VirtualAlloc(NULL,sizeof(ao_row_t)*AO_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(ao_ring);
    ao_rows=0;ao_sample_map(1,0,(const unsigned char*)&map);
    assert(ao_rows==1 && ao_totals[1][0]==3 && ao_row(0,out,sizeof(out))>0);
    assert(strstr(out,"when=tick kind=macro total=3 part=0 partial=0 at=") && strstr(out," ids=0000cccc,0000aaaa,0000bbbb"));
    /* Many ids: split over rows; an empty map still says so. */
    ao_entry_t *lots=malloc(sizeof(ao_entry_t)*70);assert(lots);
    for(unsigned i=0;i<70;i++) {lots[i].address=i;lots[i].id=i;}
    ao_rows=0;ao_emit(0,1,lots,70,0);assert(ao_rows==3 && ao_row(2,out,sizeof(out))>0 && strstr(out,"when=arm kind=ware total=70 part=2 partial=0 at=40 ids=00000040,"));free(lots);
    ao_rows=0;ao_emit(0,0,NULL,0,0);assert(ao_rows==1 && ao_row(0,out,sizeof(out))>0 && strstr(out,"total=0 part=0"));
    /* Arming: a tick in range, before the first frame; the sample at the tick once. */
    ao_armed=0;ao_stage="idle";stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    assert(!ao_arm(0) && !strcmp(ao_stage,"tick") && !ao_arm(18001));
    assert(ao_arm(150) && ao_armed && ao_tick==150 && ao_arm(150) && !ao_arm(151));
    boot_phase=3;stats.issued=149;ao_on_tick();assert(!ao_sampled);
    /* Another thread does not sample; the drain's thread does, once. */
    stats.issued=150;ui_thread=GetCurrentThreadId()+1;ao_on_tick();assert(!ao_sampled);ui_thread=GetCurrentThreadId();
    ao_on_tick();assert(ao_sampled && !strcmp(ao_stage,"sampled"));
    ao_stage="again";ao_on_tick();assert(!strcmp(ao_stage,"again"));ao_stage="sampled";
    assert(ao_status(out,sizeof(out))>0 && strstr(out,"armed=1 frame=150 tick=150 sampled=1 "));
    ao_armed=0;ao_sampled=0;ao_stage="idle";ao_tick=0;ao_rows=0;memset(ao_totals,0,sizeof(ao_totals));VirtualFree(ao_ring,0,MEM_RELEASE);ao_ring=saved_ao;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("order canon picks/comparator/rows/stub/arm/site and address order tests PASS\n");
}
static void test_order_canon_native_image(const char *path) {
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
    unsigned char *saved_page=oc_page;oc_page=NULL;
    assert(oc_pins());
    /* The comparator's dispatch: types 0x47 and 0x50 reach the unsigned compare; its epilogue reads al only. */
    for(int type=OC_TYPE_WARE;type;type=type==OC_TYPE_WARE?OC_TYPE_MACRO:0) {
        unsigned char k=image[0xccab14+type];assert(*(uint32_t*)(image+0xccaad8+4u*k)==OC_LESS_SITE);
    }
    assert(!memcmp(image+OC_LESS_BACK,"\x48\x8b\x5c\x24\x60\x48\x8b\x74\x24\x68\x48\x83\xc4\x50\x5f\xc3",16));
    /* A ware script value becomes the ware (FindTradeOffer collects type 0x47 as wares), a macro value comes from the macro database. */
    assert(!memcmp(image+0xb8f4b8,"\x83\xf9\x47\x75",4));
    /* The ware id at +0x10, the databases at their globals (GetWareData 0x281457, macro lookup 0xa2f119). */
    assert(!memcmp(image+0x3cf3d2,"\x48\x83\xc2\x10\x48\x83\x7a\x18\x10",9));
    assert(image[0x281457]==0x48 && 0x28145eu+*(int32_t*)(image+0x28145a)==AO_WARE_DB && !memcmp(image+0x28145e,"\x48\x83\xc2\x10",4));
    assert(!memcmp(image+0xa2f119,"\x4c\x8d\x59\x40",4) && !memcmp(image+0xa2f1a3,"\x48\x8b\x51\x28",4));
    /* The macro database global is what a macro lookup is called with (0x429da6, 0x429dc7). */
    assert(0x429dadu+*(int32_t*)(image+0x429da9)==AO_MACRO_DB && image[0x429dc7]==0xe8 && 0x429dccu+*(int32_t*)(image+0x429dc8)==0xa2f050u);
    /* The candidates' source: GetEquipmentWares sorts by address (0x436ab7) and 0x4bd9d0 calls it. */
    assert(image[0x436ab7]==0xe8 && 0x436abcu+*(int32_t*)(image+0x436ab8)==0x316f60u && image[0x4bda25]==0xe8 && 0x4bda2au+*(int32_t*)(image+0x4bda26)==0x4354e0u);
    image[oc_picks[3].site-4]^=1;assert(!oc_pins());image[oc_picks[3].site-4]^=1;assert(oc_pins());
    image[OC_LESS_SITE+1]^=1;assert(!oc_pins());image[OC_LESS_SITE+1]^=1;assert(oc_pins());
    oc_page=saved_page;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("order canon pick sites, comparator dispatch, ware id and databases verified");
}
