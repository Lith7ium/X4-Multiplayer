/* native_gate_macro_arena: the id as the constructor will make it, address order is hash order whatever the creation order,
 * same address on re-creation, collisions and duplicates, the free route, the audit, the stubs run for real, arming refusals,
 * site CAS/check/restore with the free route kept, the pinned build. */
typedef struct {char sso[16];uint64_t size,cap;} test_ma_string;
static test_ma_string test_ma_named;
static void *test_ma_heap[8];
static unsigned test_ma_heap_used,test_ma_freed_native;
static void *test_ma_freed_last;
static void *test_ma_alloc(size_t size,void *a,void *b,void *c,uint32_t align) {
    (void)a;(void)b;(void)c;assert(align==0x10);test_ma_heap[test_ma_heap_used]=malloc(size);return test_ma_heap[test_ma_heap_used++];
}
static void test_ma_free(void *p) {++test_ma_freed_native;test_ma_freed_last=p;}
static const unsigned char *test_ma_attr(const unsigned char *node,uint32_t attr,const unsigned char *fallback) {
    assert(attr==MA_NAME_ATTR);
    return *(const int32_t*)(node+8)?(const unsigned char*)&test_ma_named:fallback;
}
static void test_ma_set(test_ma_string *s,const char *text) {
    size_t n=strlen(text);memset(s,0,sizeof(*s));s->size=n;
    if(n<16) {memcpy(s->sso,text,n);s->cap=15;}
    else {char *heap=malloc(n+1);assert(heap);memcpy(heap,text,n+1);memcpy(s->sso,&heap,8);s->cap=n;}
}
__asm__(
".text\n"
/* rcx=stub rdx=node r8=template: at the call site the two are in r13 and r14, the size in ecx. */
"test_ma_call:\n push %r13\n push %r14\n sub $0x28,%rsp\n mov %rcx,%rax\n mov %rdx,%r13\n mov %r8,%r14\n mov $0x200,%ecx\n call *%rax\n add $0x28,%rsp\n pop %r14\n pop %r13\n ret\n"
);
extern void *test_ma_call(const void *stub,const void *node,const void *macro_template);
static uint64_t test_ma_fnv(const char *text) {
    uint64_t h=0x811c9dc5u;
    for(;*text;text++) {h*=0x1000193u;h^=(uint64_t)(int64_t)(signed char)*text;}
    return h;
}
static void test_macro_arena(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    stats.active=1;boot_mode=1;boot_phase=3;stats.issued=1;ui_thread=GetCurrentThreadId();
    /* The hash is the database's (FNV-1, 64-bit state, sign-extended bytes), over the lowercase id. */
    assert(ma_hash_lower("",0)==0x811c9dc5u);
    assert(ma_hash_lower("abc",3)==test_ma_fnv("abc") && ma_hash_lower("ABC",3)==test_ma_fnv("abc") && ma_hash_lower("aBc_1",5)==test_ma_fnv("abc_1"));
    assert(ma_hash_lower("\xe4",1)==test_ma_fnv("\xe4"));
    /* The id: the node's name attribute, else the template's, else empty; the node's +0xc says whether it counts. */
    ma_attr=test_ma_attr;
    unsigned char node[0x20];memset(node,0,sizeof(node));
    unsigned char tmpl[0x60];memset(tmpl,0,sizeof(tmpl));test_ma_set((test_ma_string*)(tmpl+MA_ID),"ship_template_macro");
    test_ma_set(&test_ma_named,"Ship_NAMED_Long_Enough_For_The_Heap_macro");
    *(int32_t*)(node+0xc)=1;*(int32_t*)(node+8)=1;
    assert(ma_predict(node,tmpl)==test_ma_fnv("ship_named_long_enough_for_the_heap_macro"));
    *(int32_t*)(node+8)=0;assert(ma_predict(node,tmpl)==test_ma_fnv("ship_template_macro"));
    assert(ma_predict(node,NULL)==0x811c9dc5u);
    *(int32_t*)(node+8)=1;*(int32_t*)(node+0xc)=0;assert(ma_predict(node,tmpl)==test_ma_fnv("ship_template_macro"));assert(ma_predict(NULL,NULL)==0x811c9dc5u);
    /* The fixed base, and somewhere else when it is taken. */
    unsigned char *wanted=ma_reserve(),*elsewhere=ma_reserve();
    assert(wanted==(unsigned char*)(uintptr_t)MA_BASE && elsewhere && elsewhere!=wanted);
    assert(VirtualFree(wanted,0,MEM_RELEASE) && VirtualFree(elsewhere,0,MEM_RELEASE));
    /* A region to place in. */
    unsigned char *saved_base=ma_base;ma_base=VirtualAlloc(NULL,MA_REGION,MEM_RESERVE,PAGE_NOACCESS);assert(ma_base);
    ma_entry_t *saved_table=ma_table;ma_table=VirtualAlloc(NULL,sizeof(ma_entry_t)*MA_TABLE,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(ma_table);
    ma_row_t *saved_ring=ma_ring;ma_ring=VirtualAlloc(NULL,sizeof(ma_row_t)*MA_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(ma_ring);
    ao_entry_t *saved_audit=ma_audit_entries;ma_audit_entries=VirtualAlloc(NULL,sizeof(ao_entry_t)*AO_MAX,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(ma_audit_entries);
    ma_rows=0;ma_failures=0;ma_created=ma_freed=ma_live=ma_ui=ma_fallback=ma_collisions=ma_duplicates=ma_unique=ma_bad_free=0;
    ma_native_alloc=test_ma_alloc;ma_native_free=test_ma_free;test_ma_heap_used=0;test_ma_freed_native=0;
    /* Address order is hash order, whichever comes first; the memory is usable and aligned. */
    unsigned char *high=ma_place(0x90000000u),*low=ma_place(0x10000000u),*mid=ma_place(0x50000000u);
    assert(low && mid && high && low<mid && mid<high);
    assert(low==ma_base+(uint64_t)0x10000000u*MA_SLOT && !((uintptr_t)low%0x10));
    memset(low,0xab,MA_SLOT);assert(low[MA_SLOT-1]==0xab && ma_created==3 && ma_live==3 && ma_unique==3);
    /* Freed and created again: the same address, for that id, zeroed. */
    memset(mid,0xab,MA_SLOT);ma_free_entry(mid);assert(ma_live==2 && ma_freed==1 && !test_ma_freed_native);
    assert(ma_place(0x50000000u)==mid && ma_live==3 && ma_unique==3 && !mid[0] && !mid[MA_SLOT-1]);
    /* A second live macro of one id: the next slot, counted. */
    unsigned char *dup=ma_place(0x50000000u);assert(dup==mid+MA_SLOT && ma_duplicates==1 && ma_unique==4);
    /* One count per macro, not per slot walked past. */
    unsigned char *dup2=ma_place(0x50000000u);assert(dup2==mid+2*MA_SLOT && ma_duplicates==2 && ma_unique==5);
    ma_free_entry(dup2);assert(!ma_find(0x50000002u)->live);
    /* Another id on the same slot: the next free one, counted and named. */
    unsigned char *other=ma_place(0x1234567890000000ull);assert(other==high+MA_SLOT && ma_collisions==1);
    char out[512];LONG64 last=ma_rows-1;assert(ma_row((uint64_t)last,out,sizeof(out))>0);
    assert(strstr(out,"kind=collision id=1234567890000000 slot=90000001 other=0000000090000000"));
    /* Free route: arena pointers stay here, a double free is only counted, everything else is native. */
    ma_free_entry(dup);assert(!test_ma_freed_native);ma_free_entry(dup);assert(ma_bad_free==1 && !test_ma_freed_native);
    int stack_object;ma_free_entry(&stack_object);assert(test_ma_freed_native==1 && test_ma_freed_last==&stack_object);
    ma_free_entry(NULL);assert(test_ma_freed_native==2 && !test_ma_freed_last);
    ma_free_entry(ma_base-1);assert(test_ma_freed_native==3);
    ma_free_entry(ma_base+MA_REGION);assert(test_ma_freed_native==4);
    /* Two macros with no id: the slot of the empty hash and the next. */
    assert(ma_alloc_entry(MA_SLOT,NULL,NULL)==ma_base+(uint64_t)0x811c9dc5u*MA_SLOT);
    assert(ma_alloc_entry(MA_SLOT,NULL,NULL)==ma_base+(uint64_t)(0x811c9dc5u+1u)*MA_SLOT && !ma_failures);
    /* A wrong size, or no slot to be had: the heap, counted, named, and the gate stops. */
    void *fall=ma_alloc_entry(0x100,NULL,NULL);assert(fall==test_ma_heap[0] && ma_fallback==1 && ma_failures==1 && !strcmp(ma_stage,"fallback"));
    assert(ma_row((uint64_t)ma_rows-1,out,sizeof(out))>0 && strstr(out,"kind=fallback"));
    ma_failures=0;uint64_t unique=ma_unique;ma_unique=MA_TABLE/2;
    void *full=ma_alloc_entry(MA_SLOT,NULL,NULL);assert(full==test_ma_heap[1] && ma_fallback==2 && ma_failures==1);
    ma_unique=unique;ma_failures=0;ma_stage="idle";free(fall);free(full);test_ma_heap_used=0;
    ma_ui=0;void *ui=ma_ui_entry(MA_SLOT);assert(ui==test_ma_heap[0] && ma_ui==1);free(ui);test_ma_heap_used=0;
    assert(ma_row((uint64_t)ma_rows-1,out,sizeof(out))>0 && strstr(out,"kind=ui"));
    /* The stubs, run for real: the node and the template arrive from r13 and r14; the free takes rcx. */
    unsigned char *page=VirtualAlloc(NULL,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(page);
    for(unsigned i=0;i<MA_SITES;i++) ma_write_stub(page+MA_STUB*i,i);
    DWORD old,ignored;assert(VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&old));FlushInstructionCache(GetCurrentProcess(),page,0x1000);
    *(int32_t*)(node+8)=1;*(int32_t*)(node+0xc)=1;
    unsigned char *named=test_ma_call(page,node,tmpl);
    assert(named==ma_base+(uint64_t)(uint32_t)test_ma_fnv("ship_named_long_enough_for_the_heap_macro")*MA_SLOT);
    ((void(*)(void*))(void*)(page+MA_STUB*MA_SITE_FREE))(named);assert(!ma_find((uint32_t)test_ma_fnv("ship_named_long_enough_for_the_heap_macro"))->live);
    VirtualFree(page,0,MEM_RELEASE);
    /* The audit: a tree of three, in the arena at their ids. */
    tll_map map;tll_node a,b,c;tll_map3(&map,&a,&b,&c);
    a.key=(const void*)(uintptr_t)0x10000000u;*(uint64_t*)&a.a28=(uint64_t)(uintptr_t)low;
    b.key=(const void*)(uintptr_t)0x50000000u;*(uint64_t*)&b.a28=(uint64_t)(uintptr_t)mid;
    c.key=(const void*)(uintptr_t)0x90000000u;*(uint64_t*)&c.a28=(uint64_t)(uintptr_t)high;
    ma_failures=0;ma_stage="idle";ma_audit_map((const unsigned char*)&map);
    assert(ma_db==3 && !ma_outside && !ma_misplaced && !ma_probed && !ma_failures && ma_audits>=1);
    /* A walk that ends early is counted, not a stop. */
    uint64_t *first=(uint64_t*)((unsigned char*)&map+0x20),first_kept=*first;
    *first=1;ma_partial=0;ma_audit_map((const unsigned char*)&map);assert(ma_partial==1 && !ma_db && !ma_failures);*first=first_kept;
    /* A probed one is allowed; its slot's owner says so. */
    c.key=(const void*)(uintptr_t)0x1234567890000000ull;*(uint64_t*)&c.a28=(uint64_t)(uintptr_t)other;
    ma_audit_map((const unsigned char*)&map);assert(ma_probed==1 && !ma_misplaced && !ma_failures);
    /* A macro at a slot its id does not lead to stops the gate, named. */
    c.key=(const void*)(uintptr_t)0x70000000u;*(uint64_t*)&c.a28=(uint64_t)(uintptr_t)high;
    ma_audit_map((const unsigned char*)&map);assert(ma_misplaced==1 && ma_failures==1 && !strcmp(ma_stage,"misplaced"));
    assert(ma_row((uint64_t)ma_rows-1,out,sizeof(out))>0 && strstr(out,"kind=misplaced id=0000000070000000 slot=90000000"));
    /* A macro outside the arena stops the gate, named. */
    ma_failures=0;ma_stage="idle";*(uint64_t*)&c.a28=(uint64_t)(uintptr_t)&stack_object;c.key=(const void*)(uintptr_t)0x90000000u;
    ma_audit_map((const unsigned char*)&map);assert(ma_outside==1 && ma_failures==1 && !strcmp(ma_stage,"outside"));
    assert(ma_row((uint64_t)ma_rows-1,out,sizeof(out))>0 && strstr(out,"kind=outside id=0000000090000000"));
    ma_failures=0;ma_stage="idle";
    /* Arming refusals on the zero image. */
    ma_installed=0;stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;
    assert(!ma_arm() && !strcmp(ma_stage,"pins") && !ma_installed);
    boot_phase=1;assert(!ma_arm());boot_phase=0;
    /* Site CAS, ownership, and a restore that keeps the free route. */
    unsigned char *saved_page=ma_page;ma_page=ma_alloc_page();assert(ma_page);
    for(unsigned i=0;i<MA_SITES;i++) {image[ma_sites[i].site]=0xe8;int32_t d=ma_rel(i,image+ma_sites[i].target);memcpy(image+ma_sites[i].site+1,&d,4);assert(ma_state(i)==0);}
    assert(VirtualProtect(ma_page,0x1000,PAGE_READWRITE,&old));for(unsigned i=0;i<MA_SITES;i++) ma_write_stub(ma_page+MA_STUB*i,i);assert(VirtualProtect(ma_page,0x1000,PAGE_EXECUTE_READ,&ignored));
    for(unsigned i=0;i<MA_SITES;i++) {assert(ma_cas(i,ma_rel(i,image+ma_sites[i].target),ma_rel(i,ma_page+MA_STUB*i)) && ma_state(i)==1);ma_done|=1u<<i;}
    ma_installed=1;assert(ma_owned());
    image[ma_sites[1].site+2]^=1;assert(ma_state(1)<0 && !ma_owned());image[ma_sites[1].site+2]^=1;assert(ma_owned());
    ma_restore();assert(!ma_installed && ma_restored && ma_free_kept);
    assert(ma_state(MA_SITE_MACRO)==0 && ma_state(MA_SITE_UI)==0 && ma_state(MA_SITE_FREE)==1);
    assert(ma_owned()); /* after stop nothing is checked */
    /* The route still works after stop. */
    test_ma_freed_native=0;ma_free_entry(&stack_object);assert(test_ma_freed_native==1);
    int32_t d=ma_rel(MA_SITE_FREE,image+MA_FREE);assert(ma_cas(MA_SITE_FREE,ma_rel(MA_SITE_FREE,ma_page+MA_STUB*MA_SITE_FREE),d));
    for(unsigned i=0;i<MA_SITES;i++) memset(image+ma_sites[i].site,0,5);
    ma_done=0;ma_free_kept=0;ma_restored=0;
    VirtualFree(ma_page,0,MEM_RELEASE);ma_page=saved_page;
    assert(ma_status(out,sizeof(out))>0 && strstr(out," base=") && strstr(out," unique=") && strstr(out," free_kept=0 "));
    ma_attr=NULL;ma_native_alloc=NULL;ma_native_free=NULL;ma_stage="idle";ma_rows=0;ma_failures=0;
    VirtualFree(ma_audit_entries,0,MEM_RELEASE);ma_audit_entries=saved_audit;VirtualFree(ma_ring,0,MEM_RELEASE);ma_ring=saved_ring;
    VirtualFree(ma_table,0,MEM_RELEASE);ma_table=saved_table;VirtualFree(ma_base,0,MEM_RELEASE);ma_base=saved_base;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("macro arena id/order/slots/route/audit/stubs/arm/site tests PASS\n");
}
static void test_macro_arena_native_image(const char *path) {
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
    unsigned char *saved_page=ma_page;ma_page=NULL;
    assert(ma_pins());
    /* The macro allocation: size and alignment, the node in r13 and the template in r14 (0xa2e7c5 .. 0xa2e7e0). */
    assert(!memcmp(image+0xa2e7c5,"\x4d\x8b\xf0\x4c\x8b\xfa\x4c\x8b\xe9",9));
    assert(!memcmp(image+0xa2e7d3,"\xc7\x44\x24\x20\x10\x00\x00\x00\xb9\x00\x02\x00\x00",13));
    /* What the constructor gets is what the id is predicted from: template r14 in r9, node r13 in rdx (0xa2e7fa). */
    assert(!memcmp(image+0xa2e7fa,"\x4d\x8b\xce\x4d\x8b\xc7\x49\x8b\xd5",9));
    /* The constructor it feeds, the only caller of it. */
    assert(image[0xa2e806]==0xe8 && 0xa2e80bu+*(int32_t*)(image+0xa2e807)==0xa258a0u);
    /* The id: the template's by default (0xa25904), attribute 0x1ac if the node's +0xc is set (0xa266cc .. 0xa266ea), lowercased, assigned (0xa2673b). */
    assert(!memcmp(image+0xa266d3,"\x83\x78\x0c\x00",4) && !memcmp(image+0xa266e2,"\xba\xac\x01\x00\x00",5));
    assert(image[0xa266ea]==0xe8 && 0xa266efu+*(int32_t*)(image+0xa266eb)==MA_ATTR && image[0xa2673b]==0xe8);
    /* The database's hash: the 32-bit basis in a 64-bit state, multiply then xor (0xa2fe17, 0xa2fe29). */
    assert(!memcmp(image+0xa2fe17,"\xb8\xc5\x9d\x1c\x81",5) && !memcmp(image+0xa2fe29,"\x48\x69\xc0\x93\x01\x00\x01\x48\x33\xc1",10));
    /* The deleting destructor is vtable slot 0, and its free is the routed call. */
    assert(*(uint64_t*)(image+0x2ba5430)==0x140000000ull+0xa27100u);
    assert(image[0xa27148]==0xe8);
    /* The other constructor's only caller allocates at the counted site. */
    assert(image[0x8d2223]==0xe8 && 0x8d2228u+*(int32_t*)(image+0x8d2224)==0xa27180u);
    image[ma_sites[2].site-4]^=1;assert(!ma_pins());image[ma_sites[2].site-4]^=1;assert(ma_pins());
    /* Armed for real on this copy: every site routed, the pins still hold with the calls patched, the region at its base. */
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    unsigned char *saved_base=ma_base;ma_entry_t *saved_table=ma_table;ma_row_t *saved_ring=ma_ring;ao_entry_t *saved_audit=ma_audit_entries;
    stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    ma_base=NULL;ma_table=NULL;ma_ring=NULL;ma_audit_entries=NULL;ma_done=0;ma_installed=0;
    assert(ma_arm()==1 && ma_installed && !strcmp(ma_stage,"armed") && ma_base==(unsigned char*)(uintptr_t)MA_BASE);
    for(unsigned i=0;i<MA_SITES;i++) assert(ma_state(i)==1 && image[ma_sites[i].site]==0xe8);
    assert(ma_pins() && ma_owned() && ma_check() && ma_arm()==1);
    /* The route's stub sits where the destructor's call now lands, and takes the destructor's rcx. */
    int32_t landed=*(int32_t*)(image+ma_sites[MA_SITE_FREE].site+1);
    assert(image+ma_sites[MA_SITE_FREE].site+5+landed==ma_page+MA_STUB*MA_SITE_FREE);
    int outside_pointer=0;ma_native_free=test_ma_free;test_ma_freed_native=0;
    ((void(*)(void*))(void*)(ma_page+MA_STUB*MA_SITE_FREE))(&outside_pointer);assert(test_ma_freed_native==1 && test_ma_freed_last==&outside_pointer);
    /* A changed byte stops it. */
    image[ma_sites[MA_SITE_MACRO].site-3]^=1;assert(!ma_check() && !strcmp(ma_stage,"block_changed"));image[ma_sites[MA_SITE_MACRO].site-3]^=1;ma_failures=0;
    /* At stop the allocation sites go back and the free route stays; this process cannot arm again. */
    ma_restore();assert(!ma_installed && ma_restored && ma_free_kept);
    assert(ma_state(MA_SITE_MACRO)==0 && ma_state(MA_SITE_UI)==0 && ma_state(MA_SITE_FREE)==1);
    assert(!ma_arm() && !strcmp(ma_stage,"pins") && !ma_installed);
    test_ma_freed_native=0;((void(*)(void*))(void*)(ma_page+MA_STUB*MA_SITE_FREE))(&outside_pointer);assert(test_ma_freed_native==1);
    ma_native_free=NULL;ma_done=0;ma_free_kept=0;ma_restored=0;ma_stage="idle";ma_rows=0;ma_failures=0;
    assert(VirtualFree(ma_page,0,MEM_RELEASE) && VirtualFree(ma_base,0,MEM_RELEASE) && VirtualFree(ma_table,0,MEM_RELEASE) && VirtualFree(ma_ring,0,MEM_RELEASE) && VirtualFree(ma_audit_entries,0,MEM_RELEASE));
    ma_base=saved_base;ma_table=saved_table;ma_ring=saved_ring;ma_audit_entries=saved_audit;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    ma_page=saved_page;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("macro arena sites, id logic, hash and the delete path verified");
}
