/* native_gate_loadout_order: the rest in native and id order, the relinked chain walked by the native successor,
 * ware list and candidates in id order, rows, the rest-loop stubs run for real, arming refusals, site CAS/check/restore,
 * the pinned build. */
#define TLL_MAX 6
typedef struct {unsigned char *self;unsigned char *left,*right;uint64_t pad;const void *key;int32_t a28,pad2;int32_t a30,pad3;uint64_t a38;} tll_node;
typedef struct {unsigned char *self,*root;uint64_t free_list,chunks;unsigned char *first;uint64_t size;} tll_map;
typedef struct {unsigned char pad[0x20];char sso[16];uint64_t size,cap;} tll_macro;
static tll_macro tll_macros[TLL_MAX];
static void tll_macro_set(unsigned i,const char *name) {
    size_t n=strlen(name);memset(&tll_macros[i],0,sizeof(tll_macros[i]));
    tll_macros[i].size=n;
    if(n<16) {memcpy(tll_macros[i].sso,name,n);tll_macros[i].cap=15;}
    else {char *heap=malloc(n+1);assert(heap);memcpy(heap,name,n+1);memcpy(tll_macros[i].sso,&heap,8);tll_macros[i].cap=n;}
}
/* A balanced map of three: b is the root, a left, c right; address order a, b, c whatever the macros are called. */
static void tll_map3(tll_map *map,tll_node *a,tll_node *b,tll_node *c) {
    memset(map,0,sizeof(*map));memset(a,0,sizeof(*a));memset(b,0,sizeof(*b));memset(c,0,sizeof(*c));
    map->self=(unsigned char*)map;map->root=(unsigned char*)b;map->first=(unsigned char*)a;map->size=3;
    b->self=(unsigned char*)map;b->left=(unsigned char*)a;b->right=(unsigned char*)c;
    a->self=(unsigned char*)b;c->self=(unsigned char*)b;
}
static unsigned test_ll_calls[LL_CALLS];
static tll_map *test_ll_out;
static uint64_t test_ll_generator(void *a,void *b,void *c,void *d,void *e,void *f,void *g,void *h) {
    (void)a;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;++test_ll_calls[LL_UNIT];assert((void*)b==(void*)test_ll_out);return 7;
}
static uint64_t test_ll_wares(void *a,void *b,void *c,void *d,void *e,void *f,void *g,void *h) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;++test_ll_calls[LL_WARES];return 0;
}
static const unsigned char *test_ll_seen[8];
static uint64_t test_ll_pick(void *a,void *b,void *c,void *d,void *e,void *f,void *g,void *h) {
    (void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;++test_ll_calls[LL_PICK];
    const unsigned char *begin=*(unsigned char **)a;test_ll_seen[0]=begin;return *(const uint64_t*)(begin+8);
}
static uint64_t test_ll_existing(void *a,void *b,void *c,void *d,void *e,void *f,void *g,void *h) {
    (void)a;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;++test_ll_calls[LL_EXISTING];
    unsigned char *map=b;unsigned k=0;for(unsigned char *n=*(unsigned char **)(map+0x20);n!=map;n=ll_next(n)) test_ll_seen[k++]=n;
    return k;
}
/* The rest loops are entered by jmp with the stack as the loop has it; the stub leaves by jmp to test_ll_back*. */
__asm__(
".text\n"
"test_ll_enter0:\n push %rbx\n push %rbp\n push %r12\n mov %rdx,%r10\n mov %r8d,%ebx\n mov %r9d,%r11d\n jmp *%rcx\n"
"test_ll_back0:\n mov %r11d,%eax\n pop %r12\n pop %rbp\n pop %rbx\n ret\n"
"test_ll_enter1:\n push %rbx\n push %rbp\n push %r12\n lea -0x57(%rdx),%rbp\n mov %r8d,%r10d\n jmp *%rcx\n"
"test_ll_back1:\n mov %r10d,%eax\n pop %r12\n pop %rbp\n pop %rbx\n ret\n"
);
extern uint32_t test_ll_enter0(const void *stub,void *map,uint32_t todo,uint32_t left);
extern uint32_t test_ll_enter1(const void *stub,void *slot,uint32_t left);
extern char test_ll_back0[],test_ll_back1[];
static void test_loadout_order(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    int saved_ids=world_identity_installed;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;assert(world_identity_record(0xb0001,0x3bccb));
    stats.active=1;boot_mode=1;boot_phase=3;stats.issued=187;ui_thread=GetCurrentThreadId();
    ll_row_t *saved_ring=ll_ring;ll_ring=VirtualAlloc(NULL,sizeof(ll_row_t)*LL_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(ll_ring);
    ll_rows=0;ll_from=150;ll_to=200;ll_failures=0;ll_rests=ll_moved=ll_relinked=0;memset(ll_count,0,sizeof(ll_count));
    tll_macro_set(0,"ship_gen_xs_repairdrone_empty_01_a_macro");tll_macro_set(1,"ship_gen_xs_cargodrone_empty_01_a_macro");tll_macro_set(2,"zz_short");
    char name[LL_NAME];
    assert(ll_macro_name(&tll_macros[1],name,sizeof(name))==39 && !strcmp(name,"ship_gen_xs_cargodrone_empty_01_a_macro"));
    assert(ll_macro_name(&tll_macros[2],name,sizeof(name))==8 && !strcmp(name,"zz_short"));
    assert(ll_macro_name(NULL,name,sizeof(name))==0 && !name[0]);
    /* Address order: repair (a), cargo (b), fighter (c). Native: the rest goes to the lowest address. */
    tll_map map;tll_node a,b,c;unsigned char *nodes[LL_NODES];
    tll_map3(&map,&a,&b,&c);a.key=&tll_macros[0];b.key=&tll_macros[1];c.key=&tll_macros[2];a.a30=10;b.a30=20;c.a30=30;
    assert(ll_nodes((unsigned char*)&map,nodes)==3 && nodes[0]==(unsigned char*)&a && nodes[1]==(unsigned char*)&b && nodes[2]==(unsigned char*)&c);
    ll_mode=LL_OBSERVE;ll_current=NULL;
    assert(ll_rest((unsigned char*)&map,4,100,0)==96);
    assert(a.a30==12 && b.a30==21 && c.a30==31 && ll_rests==1 && ll_moved==1 && ll_rows==1);
    /* Canonical: the same arithmetic, the rest to the first id (cargo). */
    tll_map3(&map,&a,&b,&c);a.key=&tll_macros[0];b.key=&tll_macros[1];c.key=&tll_macros[2];a.a30=10;b.a30=20;c.a30=30;
    ll_mode=LL_CANONICAL;
    assert(ll_rest((unsigned char*)&map,4,100,0)==96);
    assert(a.a30==11 && b.a30==22 && c.a30==31 && ll_moved==2 && !ll_failures);
    /* Rest 2 of 3, already id-ordered first two: nothing moves; divisible: no rest, no row. */
    tll_map3(&map,&a,&b,&c);a.key=&tll_macros[1];b.key=&tll_macros[0];c.key=&tll_macros[2];a.a30=b.a30=c.a30=0;
    assert(ll_rest((unsigned char*)&map,2,2,0)==0 && a.a30==1 && b.a30==1 && c.a30==0 && ll_moved==2);
    LONG64 before=ll_rows;
    assert(ll_rest((unsigned char*)&map,3,5,0)==2 && a.a30==2 && b.a30==2 && c.a30==1 && ll_rests==3 && ll_rows==before);
    tll_map empty;memset(&empty,0,sizeof(empty));empty.self=(unsigned char*)&empty;empty.first=(unsigned char*)&empty;
    assert(ll_rest((unsigned char*)&empty,3,5,0)==5);
    /* The row: kind, rest, macros in native (address) order. */
    char out[512];assert(ll_row(0,out,sizeof(out))>0);
    assert(strstr(out,"tick=187 kind=remainder site=remainder class=none key=0000000000000000 count=3 todo=4 rem=1 moved=1 macros=ship_gen_xs_repairdrone_empty_01_a_macr:10@"));
    assert(strstr(out,",ship_gen_xs_cargodrone_empty_01_a_macro:20@") && strstr(out,",zz_short:30@"));
    /* A missing id: 'canonical' stops, 'observe' carries on. */
    tll_map3(&map,&a,&b,&c);a.key=&tll_macros[0];b.key=NULL;c.key=&tll_macros[2];a.a30=b.a30=c.a30=0;
    ll_failures=0;ll_mode=LL_OBSERVE;assert(ll_rest((unsigned char*)&map,1,1,0)==0 && a.a30==1 && !ll_failures);
    ll_mode=LL_CANONICAL;assert(ll_rest((unsigned char*)&map,1,1,0)==0 && a.a30==2 && ll_failures==1 && !strcmp(ll_stage,"name"));ll_failures=0;ll_stage="idle";
    /* Too many nodes: the native walk, and 'canonical' stops. */
    tll_map3(&map,&a,&b,&c);a.key=&tll_macros[0];b.key=&tll_macros[1];c.key=&tll_macros[2];map.size=LL_NODES+1;
    assert(ll_nodes((unsigned char*)&map,nodes)==0xffffffffu);
    ll_canonical_map((unsigned char*)&map,0);assert(ll_failures==1);ll_failures=0;ll_stage="idle";
    /* The generator's result: relinked, and the walk the native loops do finds id order, then the header. */
    tll_map3(&map,&a,&b,&c);a.key=&tll_macros[0];b.key=&tll_macros[1];c.key=&tll_macros[2];a.a30=3;b.a30=5;c.a30=7;
    static unsigned char station[0x600];memset(station,0,sizeof(station));*(uint64_t*)(station+8)=0xb0001;
    /* Stock: one node, cargo -> 4. */
    tll_map *stock=(tll_map*)(station+0x510);tll_node stock_node;memset(&stock_node,0,sizeof(stock_node));
    stock->self=(unsigned char*)stock;stock->root=(unsigned char*)&stock_node;stock->first=(unsigned char*)&stock_node;stock->size=1;
    stock_node.self=(unsigned char*)stock;stock_node.key=&tll_macros[1];stock_node.a28=4;
    assert(ll_stock(station,&tll_macros[1])==4 && ll_stock(station,&tll_macros[0])==0);
    ll_native[LL_UNIT]=test_ll_generator;test_ll_out=&map;test_ll_calls[LL_UNIT]=0;before=ll_rows;
    assert(ll_unit_entry(station,&map,NULL,NULL,NULL,NULL,NULL,NULL)==7 && test_ll_calls[LL_UNIT]==1 && ll_count[LL_UNIT]==1 && ll_relinked==1);
    assert(ll_nodes((unsigned char*)&map,nodes)==3 && nodes[0]==(unsigned char*)&b && nodes[1]==(unsigned char*)&a && nodes[2]==(unsigned char*)&c);
    assert(map.first==(unsigned char*)&b && map.root==(unsigned char*)&b && b.left==NULL && b.right==(unsigned char*)&a && c.right==NULL && c.self==(unsigned char*)&a);
    assert(ll_rows==before+1 && ll_row((uint64_t)before,out,sizeof(out))>0);
    assert(strstr(out,"kind=out site=unit class=saved key=000000000003bccb count=3 todo=0 rem=0 moved=0 macros=ship_gen_xs_repairdrone_empty_01_a_macr:3/0@"));
    assert(strstr(out,",ship_gen_xs_cargodrone_empty_01_a_macro:5/4@") && strstr(out,",zz_short:7/0@"));
    /* 'observe' leaves the links alone. */
    tll_map3(&map,&a,&b,&c);a.key=&tll_macros[0];b.key=&tll_macros[1];c.key=&tll_macros[2];
    ll_mode=LL_OBSERVE;ll_unit_entry(station,&map,NULL,NULL,NULL,NULL,NULL,NULL);assert(map.root==(unsigned char*)&b && map.first==(unsigned char*)&a && ll_relinked==1);
    /* A row keeps the ammunition count at +0x28. */
    tll_map3(&map,&a,&b,&c);a.key=&tll_macros[0];b.key=&tll_macros[1];c.key=&tll_macros[2];a.a28=-1;b.a28=2;c.a28=3;before=ll_rows;
    ll_note_out(LL_AMMO,station,(unsigned char*)&map);assert(ll_row((uint64_t)before,out,sizeof(out))>0 && strstr(out,"site=ammo") && strstr(out,":-1/0@"));
    /* Outside the window or a loading frame: no row. */
    stats.issued=201;before=ll_rows;ll_note_out(LL_UNIT,station,(unsigned char*)&map);assert(ll_rows==before);
    stats.issued=187;boot_phase=2;ll_note_out(LL_UNIT,station,(unsigned char*)&map);assert(ll_rows==before);boot_phase=3;
    /* The existing-users map: the second key breaks the tie, and the callee walks id order. */
    ll_mode=LL_CANONICAL;tll_map3(&map,&a,&b,&c);a.key=&tll_macros[1];*(const void**)&a.a28=&tll_macros[2];b.key=&tll_macros[1];*(const void**)&b.a28=&tll_macros[0];c.key=&tll_macros[0];*(const void**)&c.a28=NULL;
    ll_native[LL_EXISTING]=test_ll_existing;
    assert(ll_existing_entry(NULL,&map,NULL,NULL,NULL,NULL,NULL,NULL)==3 && test_ll_seen[0]==(unsigned char*)&b && test_ll_seen[1]==(unsigned char*)&a && test_ll_seen[2]==(unsigned char*)&c);
    /* The ware list: small vector, inline capacity 4, re-sorted after the native sort; a heap vector too. */
    static unsigned char ware_a[0x100],ware_b[0x100],ware_c[0x100];memset(ware_a,0,sizeof(ware_a));memset(ware_b,0,sizeof(ware_b));memset(ware_c,0,sizeof(ware_c));
    memcpy(ware_a+0x20,"zinc",4);*(uint64_t*)(ware_a+0x30)=4;*(uint64_t*)(ware_a+0x38)=15;
    memcpy(ware_b+0x20,"alpha",5);*(uint64_t*)(ware_b+0x30)=5;*(uint64_t*)(ware_b+0x38)=15;
    memcpy(ware_c+0x20,"mid",3);*(uint64_t*)(ware_c+0x30)=3;*(uint64_t*)(ware_c+0x38)=15;
    uint64_t vec[6];memset(vec,0,sizeof(vec));vec[0]=(uint64_t)(uintptr_t)ware_a;vec[1]=(uint64_t)(uintptr_t)ware_b;vec[2]=(uint64_t)(uintptr_t)ware_c;vec[4]=4;vec[5]=3;
    ll_native[LL_WARES]=test_ll_wares;test_ll_calls[LL_WARES]=0;
    ll_wares_entry(vec,NULL,NULL,NULL,NULL,NULL,NULL,NULL);
    assert(test_ll_calls[LL_WARES]==1 && vec[0]==(uint64_t)(uintptr_t)ware_b && vec[1]==(uint64_t)(uintptr_t)ware_c && vec[2]==(uint64_t)(uintptr_t)ware_a);
    uint64_t heap[3]={(uint64_t)(uintptr_t)ware_a,(uint64_t)(uintptr_t)ware_c,(uint64_t)(uintptr_t)ware_b};
    memset(vec,0,sizeof(vec));vec[0]=(uint64_t)(uintptr_t)heap;vec[4]=8;vec[5]=3;
    ll_canonical_wares((unsigned char*)vec);assert(heap[0]==(uint64_t)(uintptr_t)ware_b && heap[1]==(uint64_t)(uintptr_t)ware_c && heap[2]==(uint64_t)(uintptr_t)ware_a);
    ll_mode=LL_OBSERVE;heap[0]=(uint64_t)(uintptr_t)ware_a;heap[2]=(uint64_t)(uintptr_t)ware_b;
    ll_wares_entry(vec,NULL,NULL,NULL,NULL,NULL,NULL,NULL);assert(heap[0]==(uint64_t)(uintptr_t)ware_a);ll_mode=LL_CANONICAL;
    /* The pick's candidates: 0x28-byte entries {ware, macro, ...}, sorted whole. */
    unsigned char candidates[3][0x28];memset(candidates,0,sizeof(candidates));
    for(unsigned i=0;i<3;i++) {*(const void**)(candidates[i]+8)=&tll_macros[i];candidates[i][0x20]=(unsigned char)(i+1);}
    unsigned char *range[3]={candidates[0],candidates[0]+3*0x28,NULL};
    ll_native[LL_PICK]=test_ll_pick;
    assert(ll_pick_entry(range,NULL,NULL,NULL,NULL,NULL,NULL,NULL)==(uint64_t)(uintptr_t)&tll_macros[1]);
    assert(candidates[0][0x20]==2 && candidates[1][0x20]==1 && candidates[2][0x20]==3);
    assert(!ll_failures);
    /* Both rest stubs, run for real on an executable page (exit: back to the harness). */
    unsigned char *page=VirtualAlloc(NULL,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(page);
    ll_write_detour(page,0,test_ll_back0);ll_write_detour(page+0x40,1,test_ll_back1);
    DWORD old,ignored;assert(VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&old));FlushInstructionCache(GetCurrentProcess(),page,0x1000);
    tll_map3(&map,&a,&b,&c);a.key=&tll_macros[0];b.key=&tll_macros[1];c.key=&tll_macros[2];a.a30=b.a30=c.a30=0;
    assert(test_ll_enter0(page,&map,4,100)==96 && a.a30==1 && b.a30==2 && c.a30==1);
    tll_map3(&map,&a,&b,&c);a.key=&tll_macros[0];b.key=&tll_macros[1];c.key=&tll_macros[2];a.a30=b.a30=c.a30=0;
    void *slot=&map;assert(test_ll_enter1(page+0x40,&slot,7)==0 && a.a30==2 && b.a30==3 && c.a30==2);
    VirtualFree(page,0,MEM_RELEASE);
    assert(ll_status(out,sizeof(out))>0 && strstr(out,"mode=canonical frame=187") && strstr(out," window=150-200 "));
    /* Stubs and arming refusals (mode and window first, then the zero image). */
    unsigned char t[14];ll_write_stub(t,3);assert(*(void**)(t+6)==(void*)ll_entries[3]);
    ll_installed=0;ll_mode=LL_OFF;stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ll_stage="idle";
    assert(!ll_arm(7) && !strcmp(ll_stage,"mode"));
    assert(!ll_window(0,10) && !ll_window(10,5) && !ll_window(1,700) && ll_window(150,200) && ll_from==150 && ll_to==200);
    world_identity_installed=0;assert(!ll_arm(LL_OBSERVE) && !strcmp(ll_stage,"needs"));world_identity_installed=1;
    assert(!ll_arm(LL_OBSERVE) && !strcmp(ll_stage,"pins") && !ll_installed);
    assert(ll_window(0,0) && !ll_to);world_identity_installed=0;assert(!ll_arm(LL_CANONICAL) && !strcmp(ll_stage,"pins"));world_identity_installed=1;
    /* Site CAS, ownership and restore on the plain image. */
    unsigned char *saved_page=ll_page;ll_page=ll_alloc_page();assert(ll_page);
    for(unsigned i=0;i<LL_CALLS;i++) {image[ll_calls[i].site]=0xe8;int32_t d=ll_call_rel(i,image+ll_calls[i].target);memcpy(image+ll_calls[i].site+1,&d,4);assert(ll_call_state(i)==0);}
    for(unsigned j=0;j<LL_DETOURS;j++) {memcpy(image+ll_detours[j].site,ll_detours[j].orig,5);assert(ll_detour_state(j)==0);}
    assert(VirtualProtect(ll_page,0x1000,PAGE_READWRITE,&old));ll_write_code();assert(VirtualProtect(ll_page,0x1000,PAGE_EXECUTE_READ,&ignored));
    for(unsigned i=0;i<LL_CALLS;i++) {assert(ll_call_cas(i,ll_call_rel(i,image+ll_calls[i].target),ll_call_rel(i,ll_page+LL_STUB*i)) && ll_call_state(i)==1);ll_done|=1u<<i;}
    for(unsigned j=0;j<LL_DETOURS;j++) {
        unsigned char patch[5]={0xe9};int32_t d=ll_detour_rel(j);memcpy(patch+1,&d,4);
        assert(view_cas_bytes(ll_detours[j].site,ll_detours[j].orig,patch,5) && ll_detour_state(j)==1);ll_done|=1u<<(LL_CALLS+j);
    }
    ll_installed=1;ll_mode=LL_CANONICAL;assert(ll_owned());
    image[ll_calls[1].site+2]^=1;assert(ll_call_state(1)<0 && !ll_owned());image[ll_calls[1].site+2]^=1;assert(ll_owned());
    image[ll_detours[1].site+2]^=1;assert(ll_detour_state(1)<0 && !ll_owned());image[ll_detours[1].site+2]^=1;assert(ll_owned());
    ll_restore();assert(!ll_installed && ll_restored && !ll_done && ll_mode==LL_OFF);
    for(unsigned i=0;i<LL_CALLS;i++) {assert(ll_call_state(i)==0);memset(image+ll_calls[i].site,0,5);}
    for(unsigned j=0;j<LL_DETOURS;j++) {assert(ll_detour_state(j)==0);memset(image+ll_detours[j].site,0,5);}
    VirtualFree(ll_page,0,MEM_RELEASE);ll_page=saved_page;
    for(unsigned i=0;i<LL_CALLS;i++) ll_native[i]=NULL;
    ll_restored=0;ll_stage="idle";ll_from=ll_to=0;VirtualFree(ll_ring,0,MEM_RELEASE);ll_ring=saved_ring;ll_rows=0;ll_failures=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    world_identity_installed=saved_ids;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("loadout order rest/relink/wares/candidates/rows/stubs/arm/site tests PASS\n");
}
static void test_loadout_order_native_image(const char *path) {
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
    unsigned char *saved_page=ll_page;ll_page=NULL;
    assert(ll_pins());
    /* The range 0x429ad0 hands every generator: {-1 (the container's level), 0 (no random fill)}. */
    float range[2];memcpy(range,image+0x31be918,8);assert(range[0]==-1.0f && range[1]==0.0f);
    assert(!memcmp(image+0x42a3a9,"\x4c\x8d\x35",3) && 0x42a3b0u+*(int32_t*)(image+0x42a3ac)==0x31be918u);
    /* The rest loop in map order: quotient and rest (0x503356), first node (0x503362), the increment (0x503388). */
    assert(!memcmp(image+0x503356,"\x33\xd2\x8b\xc3\xf7\xf1",6) && !memcmp(image+0x503362,"\x49\x8b\x4a\x20",4) && !memcmp(image+0x503388,"\xff\x41\x30",3));
    assert(!memcmp(image+0x5043df,"\x33\xd2\x41\x8b\xc2\xf7\xf1",7) && !memcmp(image+0x5043e6,"\x49\x8b\x4b\x20",4) && !memcmp(image+0x50440b,"\xff\x41\x30",3));
    /* After the loops only r11d / r10d and the frame are read. */
    assert(!memcmp(image+0x5033e4,"\x8b\x5d\x67\x41\x2b\xdb",6) && !memcmp(image+0x504464,"\x8b\x45\x67\x41\x2b\xc2",6));
    /* The unit walk in 0x429ad0: first +0x20 (0x42a9bb), size +0x28 (0x42a9ad), end = the header (0x42abff), freed by chunks (0x42ac0f). */
    assert(!memcmp(image+0x42a9bb,"\x48\x8b\x9d\x30\x03\x00\x00",7) && !memcmp(image+0x42a9ad,"\x48\x83\xbd\x38\x03\x00\x00\x00",8));
    assert(!memcmp(image+0x42abff,"\x48\x8d\x85\x10\x03\x00\x00\x48\x3b\xd8",10) && !memcmp(image+0x42ac0f,"\x48\x8b\x9d\x50\x03\x00\x00",7));
    /* The stock: map at +0x510, count at +0x28 of the node (0x42ab14). */
    assert(!memcmp(image+0x42aadd,"\x4d\x8d\x84\x24\x10\x05\x00\x00",8) && !memcmp(image+0x42ab14,"\x8b\x79\x28",3));
    image[ll_detours[0].site+0x20]^=1;assert(!ll_pins());image[ll_detours[0].site+0x20]^=1;assert(ll_pins());
    image[ll_calls[3].site-4]^=1;assert(!ll_pins());image[ll_calls[3].site-4]^=1;assert(ll_pins());
    ll_page=saved_page;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("loadout order call sites, rest loops, unit walk and the range verified");
}
