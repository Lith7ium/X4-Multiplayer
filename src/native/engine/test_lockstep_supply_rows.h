/* native_gate_supply_rows: post rows (site, key, due, thread, chain) in admitted frames only, native post always,
 * stubs per site, arming refusals, site CAS/check/restore, the pinned build. */
static unsigned test_spr_posts;
static double test_spr_due;
static void test_spr_native(void *station,void *event,double due,uint64_t flag) {(void)station;(void)event;(void)flag;++test_spr_posts;test_spr_due=due;}
static void test_supply_rows(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    int saved_ids=world_identity_installed;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;assert(world_identity_record(0x9e001,0x340cf));
    static unsigned char station[0x40];memset(station,0,sizeof(station));*(uint64_t*)(station+8)=0x9e001;
    spr_row_t *saved_ring=spr_ring;spr_ring=VirtualAlloc(NULL,sizeof(spr_row_t)*SPR_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(spr_ring);
    spr_native=test_spr_native;spr_rows=0;spr_posts=spr_outside=0;spr_installed=1;
    stats.active=1;boot_mode=1;boot_phase=3;stats.issued=1078;ui_thread=GetCurrentThreadId();
    test_spr_posts=0;spr_entries[0](station,NULL,24.95,0);
    assert(test_spr_posts==1 && test_spr_due==24.95 && spr_posts==1 && spr_rows==1);
    char out[384];assert(spr_row(0,out,sizeof(out))>0 && strstr(out,"tick=1078 site=added class=saved key=00000000000340cf due=4038f33333333333 thread=ui chain="));
    spr_entries[2](station,NULL,24.95,0);assert(spr_row(1,out,sizeof(out))>0 && strstr(out," site=removed "));
    boot_phase=2;spr_entries[3](station,NULL,1.0,0);assert(test_spr_posts==3 && spr_rows==2 && spr_outside==1);boot_phase=3;
    assert(spr_status(out,sizeof(out))>0 && strstr(out," posts=2 outside=1 rows=2 "));
    unsigned char t[14];spr_write_stub(t,1);assert(t[0]==0xff && t[1]==0x25 && !*(uint32_t*)(t+2) && *(void**)(t+6)==(void*)spr_entries[1] && spr_stub_ok(t,1));
    assert(!spr_stub_ok(t,0));
    spr_installed=0;stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;
    world_identity_installed=0;assert(!spr_arm() && !strcmp(spr_stage,"needs"));world_identity_installed=1;
    assert(!spr_arm() && !strcmp(spr_stage,"pins") && !spr_installed);
    stats.issued=1;assert(!spr_arm());stats.issued=0;boot_phase=2;assert(!spr_arm());boot_phase=0;
    unsigned char *saved_page=spr_page;spr_page=spr_alloc_page();assert(spr_page);
    for(unsigned i=0;i<SPR_SITES;i++) {image[spr_sites[i].site]=0xe8;int32_t d=spr_rel(i,image+SPR_POST);memcpy(image+spr_sites[i].site+1,&d,4);assert(spr_state(i)==0);}
    DWORD old,ignored;assert(VirtualProtect(spr_page,0x1000,PAGE_READWRITE,&old));for(unsigned i=0;i<SPR_SITES;i++) spr_write_stub(spr_page+SPR_STUB*i,i);
    assert(VirtualProtect(spr_page,0x1000,PAGE_EXECUTE_READ,&ignored));
    for(unsigned i=0;i<SPR_SITES;i++) {assert(spr_cas(i,spr_rel(i,image+SPR_POST),spr_rel(i,spr_page+SPR_STUB*i)) && spr_state(i)==1);spr_done|=1u<<i;}
    spr_installed=1;assert(spr_owned());
    image[spr_sites[2].site+2]^=1;assert(spr_state(2)<0 && !spr_owned());image[spr_sites[2].site+2]^=1;assert(spr_owned());
    spr_restore();assert(!spr_installed && spr_restored && !spr_done);
    for(unsigned i=0;i<SPR_SITES;i++) {assert(spr_state(i)==0);memset(image+spr_sites[i].site,0,5);}
    VirtualFree(spr_page,0,MEM_RELEASE);spr_page=saved_page;
    spr_restored=0;spr_stage="idle";spr_native=NULL;VirtualFree(spr_ring,0,MEM_RELEASE);spr_ring=saved_ring;spr_rows=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    world_identity_installed=saved_ids;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("supply post rows/stubs/arm/site tests PASS\n");
}
static void test_supply_rows_native_image(const char *path) {
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
    unsigned char *saved_page=spr_page;spr_page=NULL;
    assert(spr_pins());
    /* Each function builds its event (SupplyWaresAdded ctor 0x9cd1f0 / SupplyWaresRemoved ctor 0x9cd260) and posts it
     * with xmm2 = xmm6 (the game time read from TLS+0x300) right after. */
    static const uint32_t ctor_calls[SPR_SITES]={0x428a2fu,0x428c14u,0x428e08u,0x428fc4u},ctors[SPR_SITES]={0x9cd1f0u,0x9cd1f0u,0x9cd260u,0x9cd260u};
    for(unsigned i=0;i<SPR_SITES;i++) {
        assert(spr_state(i)==0 && image[ctor_calls[i]]==0xe8 && ctor_calls[i]+5u+*(int32_t*)(image+ctor_calls[i]+1)==ctors[i]);
        assert(!memcmp(image+spr_sites[i].site-9,"\x0f\x28\xd6",3));
    }
    image[spr_sites[1].func+0x30]^=1;assert(!spr_pins());image[spr_sites[1].func+0x30]^=1;assert(spr_pins());
    spr_page=saved_page;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("supply wares post sites (added/removed) and their event constructors verified");
}
