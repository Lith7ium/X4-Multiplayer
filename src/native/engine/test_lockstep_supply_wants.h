/* native_gate_supply_wants: wanted-order rows in the window (decoded entries, ware ids from SSO and heap strings),
 * native call always, stubs, arming refusals, site CAS/check/restore, the pinned build. */
static unsigned test_sw_calls;
static unsigned char test_sw_ware1[0x100],test_sw_ware2[0x100];
static char test_sw_heap_name[64];
static int32_t test_sw_amount=3;
static void *test_sw_native(void *container,void *list,uint64_t a3,uint64_t a4) {
    (void)container;(void)a3;(void)a4;++test_sw_calls;unsigned char *l=list;
    /* Two entries in the inline storage: ware = q ^ entry, amount = (low dword & 0x7fffffff) ^ dword +8. */
    uint64_t q0=(uint64_t)(uintptr_t)test_sw_ware1^(uint64_t)(uintptr_t)l,q1=(uint64_t)(uintptr_t)test_sw_ware2^(uint64_t)(uintptr_t)(l+16);
    uint32_t d0=((uint32_t)q0&0x7fffffffu)^12u,d1=((uint32_t)q1&0x7fffffffu)^(uint32_t)test_sw_amount;
    memcpy(l,&q0,8);memcpy(l+8,&d0,4);memcpy(l+16,&q1,8);memcpy(l+24,&d1,4);
    uint64_t count=2;memcpy(l+0x28,&count,8);return list;
}
static void test_supply_wants(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    int saved_ids=world_identity_installed;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;assert(world_identity_record(0xb0001,0x3bccb));
    static unsigned char station[0x40];memset(station,0,sizeof(station));*(uint64_t*)(station+8)=0xb0001;
    /* Ware 1: an SSO string at +0x20 ("dronecomp", size 9, capacity 15); ware 2: a heap string at +0x08. */
    memset(test_sw_ware1,0,sizeof(test_sw_ware1));memset(test_sw_ware2,0,sizeof(test_sw_ware2));
    memcpy(test_sw_ware1+0x20,"dronecomp",9);*(uint64_t*)(test_sw_ware1+0x30)=9;*(uint64_t*)(test_sw_ware1+0x38)=15;
    strcpy(test_sw_heap_name,"missile_heatseeker_light_mk1");
    *(uint64_t*)(test_sw_ware2+0x08)=(uint64_t)(uintptr_t)test_sw_heap_name;*(uint64_t*)(test_sw_ware2+0x18)=28;*(uint64_t*)(test_sw_ware2+0x20)=31;
    sw_row_t *saved_ring=sw_ring;sw_ring=VirtualAlloc(NULL,sizeof(sw_row_t)*SW_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(sw_ring);
    sw_native=test_sw_native;sw_rows=0;sw_calls=0;sw_from=150;sw_to=200;
    stats.active=1;boot_mode=1;boot_phase=3;stats.issued=187;ui_thread=GetCurrentThreadId();
    unsigned char list[0x30];memset(list,0,sizeof(list));*(uint64_t*)(list+0x20)=2;
    test_sw_calls=0;assert(sw_entries[1](station,list,0,1)==list && test_sw_calls==1 && sw_calls==1 && sw_rows==1);
    char out[384];assert(sw_row(0,out,sizeof(out))>0);
    assert(strstr(out,"tick=187 site=trade class=saved key=000000000003bccb a3=0 a4=1 count=2 wants=dronecomp:12,missile_heatseeker_ligh:3,-:0 other=0"));
    /* A surplus is negative; another thread's call says so. */
    test_sw_amount=-1;ui_thread=GetCurrentThreadId()+1;assert(sw_entries[1](station,list,0,1)==list && sw_rows==2);ui_thread=GetCurrentThreadId();test_sw_amount=3;
    assert(sw_row(1,out,sizeof(out))>0 && strstr(out,"wants=dronecomp:12,missile_heatseeker_ligh:-1,-:0 other=1"));
    /* Outside the window or a loading frame: native, no row. */
    stats.issued=201;sw_entries[2](station,list,1,0);assert(test_sw_calls==3 && sw_rows==2);
    stats.issued=187;boot_phase=2;sw_entries[2](station,list,1,0);assert(test_sw_calls==4 && sw_rows==2);boot_phase=3;
    char id[24];sw_ware_id(NULL,id);assert(!strcmp(id,"?"));unsigned char none[0x100];memset(none,0,sizeof(none));sw_ware_id(none,id);assert(!strcmp(id,"?"));
    assert(sw_status(out,sizeof(out))>0 && strstr(out," calls=4 rows=2 window=150-200 "));
    unsigned char t[14];sw_write_stub(t,3);assert(*(void**)(t+6)==(void*)sw_entries[3] && sw_stub_ok(t,3) && !sw_stub_ok(t,2));
    /* Arming refusals (window first, then the zero image). */
    sw_installed=0;stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;
    assert(!sw_arm(0,10) && !strcmp(sw_stage,"window") && !sw_arm(10,5) && !sw_arm(1,700));
    world_identity_installed=0;assert(!sw_arm(150,200) && !strcmp(sw_stage,"needs"));world_identity_installed=1;
    assert(!sw_arm(150,200) && !strcmp(sw_stage,"pins") && !sw_installed);
    /* Site CAS, ownership and restore on the plain image. */
    unsigned char *saved_page=sw_page;sw_page=sw_alloc_page();assert(sw_page);
    for(unsigned i=0;i<SW_SITES;i++) {image[sw_sites[i].site]=0xe8;int32_t d=sw_rel(i,image+SW_TARGET);memcpy(image+sw_sites[i].site+1,&d,4);assert(sw_state(i)==0);}
    DWORD old,ignored;assert(VirtualProtect(sw_page,0x1000,PAGE_READWRITE,&old));for(unsigned i=0;i<SW_SITES;i++) sw_write_stub(sw_page+SW_STUB*i,i);
    assert(VirtualProtect(sw_page,0x1000,PAGE_EXECUTE_READ,&ignored));
    for(unsigned i=0;i<SW_SITES;i++) {assert(sw_cas(i,sw_rel(i,image+SW_TARGET),sw_rel(i,sw_page+SW_STUB*i)) && sw_state(i)==1);sw_done|=1u<<i;}
    sw_installed=1;assert(sw_owned());
    image[sw_sites[1].site+2]^=1;assert(sw_state(1)<0 && !sw_owned());image[sw_sites[1].site+2]^=1;assert(sw_owned());
    sw_restore();assert(!sw_installed && sw_restored && !sw_done);
    for(unsigned i=0;i<SW_SITES;i++) {assert(sw_state(i)==0);memset(image+sw_sites[i].site,0,5);}
    VirtualFree(sw_page,0,MEM_RELEASE);sw_page=saved_page;
    sw_restored=0;sw_stage="idle";sw_native=NULL;sw_from=sw_to=0;VirtualFree(sw_ring,0,MEM_RELEASE);sw_ring=saved_ring;sw_rows=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    world_identity_installed=saved_ids;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("supply wants rows/decoding/stubs/arm/site tests PASS\n");
}
static void test_supply_wants_native_image(const char *path) {
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
    unsigned char *saved_page=sw_page;sw_page=NULL;
    assert(sw_pins());
    /* The branch in UpdateTradeOffers: 0x42ae40's list count tested at 0x407df0, the cargo calls behind it at 0x407eb9 / 0x407f1f. */
    assert(image[0x407deb]==0xe8 && 0x407df0u+*(int32_t*)(image+0x407dec)==0x42ae40u && !memcmp(image+0x407df0,"\x48\x83\x7d\x28\x00",5));
    assert(image[0x407eb9]==0xe8 && 0x407ebeu+*(int32_t*)(image+0x407eba)==0x3fa8d0u && image[0x407f1f]==0xe8 && 0x407f24u+*(int32_t*)(image+0x407f20)==0x3fcd30u);
    /* The list: inline capacity 2 from [0x2cc0910] = {2, 0}; entries decoded as above (0x42aef0..0x42af11). */
    assert(*(uint64_t*)(image+0x2cc0910)==2 && !*(uint64_t*)(image+0x2cc0918));
    assert(!memcmp(image+0x42aef0,"\x48\x8b\x1e\x48\x8b\xcb\x48\x33\xce",9) && !memcmp(image+0x42af0d,"\x0f\xba\xf3\x1f\x33\x5e\x08",7));
    image[sw_sites[2].site-4]^=1;assert(!sw_pins());image[sw_sites[2].site-4]^=1;assert(sw_pins());
    sw_page=saved_page;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("supply wants call sites, the UpdateTradeOffers branch and the list decoding verified");
}
