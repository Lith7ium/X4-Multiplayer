/* native_gate_ui_lazy: UI (Lua) queries counted/blocked in admitted frames, simulation calls native, rows,
 * trampoline and stub layout, arming refusals, the pinned prologues. */
static int test_ul_lua;
static int test_ul_from_lua(uint32_t *chain) {for(unsigned i=0;i<UL_CHAIN;i++) chain[i]=0x1000u+i;return test_ul_lua;}
static void test_ui_lazy(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    int saved_ids=world_identity_installed;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;assert(world_identity_record(0xa0001,0x1f2df));
    static unsigned char station[0x700];memset(station,0,sizeof(station));*(uint64_t*)(station+8)=0xa0001;*(uint32_t*)(station+0x680)=17;
    ul_row_t *saved_ring=ul_ring;ul_ring=VirtualAlloc(NULL,sizeof(ul_row_t)*UL_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(ul_ring);
    ul_from_lua_fn=test_ul_from_lua;memset(ul_lua,0,sizeof(ul_lua));memset(ul_sim,0,sizeof(ul_sim));ul_blocked=ul_unmapped=0;ul_rows=0;
    stats.active=1;boot_mode=1;boot_phase=3;stats.issued=1077;ui_thread=GetCurrentThreadId();
    /* Off: never a UI query. */
    ul_mode=UL_OFF;test_ul_lua=1;assert(!ul_ui_query(UL_TRADE,station,0,0) && !ul_rows);
    /* Simulation calls (no Lua on the stack) run natively and are counted. */
    ul_mode=UL_BLOCK;test_ul_lua=0;assert(!ul_ui_query(UL_TRADE,station,0,0) && ul_sim[UL_TRADE]==1 && !ul_rows);
    /* A UI query: blocked, counted, one row with the portable key. */
    test_ul_lua=1;assert(ul_ui_query(UL_TRADE,station,0,0) && ul_lua[UL_TRADE]==1 && ul_blocked==1 && ul_rows==1);
    char out[384];assert(ul_row(0,out,sizeof(out))>0 && strstr(out,"tick=1077 target=trade class=saved key=000000000001f2df lua=1 blocked=1 ") && strstr(out," chain=00001000,00001001"));
    /* observe: counted and recorded, not blocked. */
    ul_mode=UL_OBSERVE;assert(!ul_ui_query(UL_CARGO,station,0,0) && ul_lua[UL_CARGO]==1 && ul_blocked==1 && ul_rows==2);
    assert(ul_row(1,out,sizeof(out))>0 && strstr(out," target=cargo ") && strstr(out," blocked=0 "));
    /* Loading frames and other threads: native, uncounted. */
    ul_mode=UL_BLOCK;boot_phase=2;assert(!ul_ui_query(UL_UNITS,station,0,0));boot_phase=3;
    ui_thread=GetCurrentThreadId()+1;assert(!ul_ui_query(UL_UNITS,station,0,0));ui_thread=GetCurrentThreadId();
    /* units blocked: the cached total. */
    assert(ul_units(station,NULL,0)==17 && ul_lua[UL_UNITS]==1);
    static unsigned char ship[0x40];memset(ship,0,sizeof(ship));*(uint64_t*)(ship+8)=0x9999;assert(ul_ui_query(UL_CARGO,ship,0,0) && ul_unmapped==1);
    assert(ul_status(out,sizeof(out))>0 && strstr(out,"mode=block frame=1077 lua=2/1/1 sim=0/1/0 other=0/0/1 blocked=3 unmapped=1 "));
    /* Row window: simulation calls in it are rows with their arguments and state. */
    {
        int saved_installed=ul_installed;ul_installed=0;
        assert(!ul_window(0,5) && !ul_window(10,5) && !ul_window(1,700) && ul_window(1070,1080));
        ul_installed=1;assert(!ul_window(1,2) && ul_window(1070,1080));ul_installed=saved_installed;
        *(double*)(station+0xa18)=25.0;LONG64 rows=ul_rows;test_ul_lua=0;uint64_t bits;double t=25.03;memcpy(&bits,&t,8);
        assert(!ul_ui_query(UL_CARGO,station,bits,0) && ul_rows==rows+1);
        assert(ul_row((uint64_t)rows,out,sizeof(out))>0 && strstr(out," target=cargo class=saved key=000000000001f2df lua=0 blocked=0 a1=403907ae147ae148 a2=0 state=4039000000000000 "));
        *(uint8_t*)(station+0x684)=1;assert(!ul_ui_query(UL_UNITS,station,0,0) && ul_row((uint64_t)rows+1,out,sizeof(out))>0 && strstr(out," state=0000000100000011 "));
        stats.issued=1081;assert(!ul_ui_query(UL_CARGO,station,bits,0) && ul_rows==rows+2);stats.issued=1077;
        /* Another thread: native, counted, and in the window a row that says so. */
        assert(ul_row((uint64_t)rows,out,sizeof(out))>0 && strstr(out," other=0"));
        ui_thread=GetCurrentThreadId()+1;assert(!ul_ui_query(UL_TRADE,station,1,0) && ul_rows==rows+3 && ul_other[UL_TRADE]==1);ui_thread=GetCurrentThreadId();
        assert(ul_row((uint64_t)rows+2,out,sizeof(out))>0 && strstr(out," target=trade ") && strstr(out," lua=0 ") && strstr(out," other=1"));
        stats.issued=1081;ui_thread=GetCurrentThreadId()+1;assert(!ul_ui_query(UL_TRADE,station,1,0) && ul_rows==rows+3 && ul_other[UL_TRADE]==2);ui_thread=GetCurrentThreadId();stats.issued=1077;
        assert(ul_status(out,sizeof(out))>0 && strstr(out," window=1070-1080 "));
        assert(ul_filter_class(NULL)==0);ul_window_from=ul_window_to=0;test_ul_lua=1;
    }
    /* Code layout: stolen bytes, jmp [rip] back; entry stub at +0x20 jmp [rip] to the C entry. */
    unsigned char page[UL_STUB];const unsigned char stolen[7]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x10};ul_write_code(page,UL_TRADE,stolen);
    assert(!memcmp(page,stolen,7) && page[7]==0xff && page[8]==0x25 && *(void**)(page+13)==(void*)(image+0x4079e7u));
    assert(page[0x20]==0xff && page[0x21]==0x25 && *(void**)(page+0x26)==ul_entries[UL_TRADE] && page[0x15]==0xcc);
    /* Arming refusals on the zero image. */
    ul_mode=UL_OFF;ul_installed=0;stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;
    assert(!ul_arm(0) && !strcmp(ul_stage,"mode"));
    world_identity_installed=0;assert(!ul_arm(UL_BLOCK) && !strcmp(ul_stage,"needs"));world_identity_installed=1;
    assert(!ul_arm(UL_BLOCK) && !strcmp(ul_stage,"pins") && !ul_installed);
    stats.issued=1;assert(!ul_arm(UL_BLOCK));stats.issued=0;boot_phase=2;assert(!ul_arm(UL_BLOCK));boot_phase=0;
    ui_thread=GetCurrentThreadId()+1;assert(!ul_arm(UL_BLOCK));ui_thread=GetCurrentThreadId();
    ul_from_lua_fn=NULL;ul_stage="idle";ul_mode=UL_OFF;
    VirtualFree(ul_ring,0,MEM_RELEASE);ul_ring=saved_ring;ul_rows=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    world_identity_installed=saved_ids;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("UI lazy query block/observe/rows/code/arm tests PASS\n");
}
static void test_ui_lazy_native_image(const char *path) {
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
    unsigned char *saved_page=ul_page;ul_page=NULL;
    for(unsigned i=0;i<UL_TARGETS;i++) assert(ul_state(i)==0);
    /* The stolen bytes are whole, position-independent instructions: mov rax,rsp; mov [rax+8/0x10],rbx; mov [rsp+0x10],rbx. */
    assert(!memcmp(image+0x41a910,"\x48\x8b\xc4\x48\x89\x58\x08",7) && !memcmp(image+0x4079e0,"\x48\x8b\xc4\x48\x89\x58\x10",7));
    assert(!memcmp(image+0x4d8110,"\x48\x89\x5c\x24\x10",5));
    /* 0x4d8110 is Defensible slot 1105 in the Container/BuildStorage/Station vtables; its fast path returns +0x680. */
    assert(*(uint64_t*)(image+0x2a91478)==0x140000000ULL+0x4d8110u && !memcmp(image+0x4d813e,"\x8b\x81\x80\x06\x00\x00",6));
    /* The trade offer query 0x40a3f0 (script property lookups) calls UpdateTradeOffers without force. */
    assert(image[0x40a405]==0xe8 && 0x40a40au+*(int32_t*)(image+0x40a406)==0x4079e0u);
    image[0x41a910+0x40]^=1;assert(ul_state(UL_CARGO)<0);image[0x41a910+0x40]^=1;assert(ul_state(UL_CARGO)==0);
    ul_page=saved_page;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("UI lazy query detour prologues, units slot and trade query call verified");
}
