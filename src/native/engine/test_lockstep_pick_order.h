/* native_gate_pick_order: pool order against map order, the native draws and modes, free nodes, a pool that does not add up,
 * rows, the stub run for real with the registers it keeps, arming refusals, site CAS/check/restore, the pinned build. */
static unsigned test_po_plain_calls,test_po_state_calls;
static uint32_t test_po_value;
static void *test_po_state_seen;
static uint32_t test_po_plain(uint32_t n) {++test_po_plain_calls;assert(n>0);return test_po_value;}
static uint32_t test_po_state(void *state,uint32_t n) {++test_po_state_calls;assert(n>0);test_po_state_seen=state;return test_po_value;}
/* A table of four weighted entries in one chunk of 8, built with its tree in the reverse of the insertion order. */
typedef struct {unsigned char self[0x28];uint64_t size;uint64_t pool_total;unsigned char *chunks;int32_t mode,total;} test_po_map;
static unsigned char test_po_chunk[0x10+PO_NODE*8];
static unsigned char *test_po_node(unsigned i) {return test_po_chunk+0x10+PO_NODE*i;}
static uint64_t test_po_key(unsigned i) {return 0x1000+i;}
static void test_po_build(test_po_map *map,const int32_t *weights,unsigned count,int32_t mode,int32_t total) {
    memset(map,0,sizeof(*map));memset(test_po_chunk,0,sizeof(test_po_chunk));
    *(uint64_t*)test_po_chunk=8;
    unsigned char *hdr=(unsigned char*)map;
    /* The tree: a right chain, the last inserted first. */
    for(unsigned k=0;k<count;k++) {
        unsigned i=count-1-k;unsigned char *n=test_po_node(i);
        *(unsigned char **)n=k?test_po_node(i+1):hdr;
        *(unsigned char **)(n+0x10)=i?test_po_node(i-1):NULL;
        *(uint64_t*)(n+0x20)=test_po_key(i);*(int32_t*)(n+0x28)=weights[i];
    }
    *(unsigned char **)hdr=hdr;*(unsigned char **)(hdr+8)=count?test_po_node(count-1):NULL;
    *(unsigned char **)(hdr+0x20)=count?test_po_node(count-1):hdr;
    map->size=count;map->pool_total=8;map->chunks=test_po_chunk;map->mode=mode;map->total=total;
}
__asm__(
".text\n"
/* rcx=stub rdx=map r8=state: calls the stub as a caller would and fails if r10 or r11 moved. */
"test_po_call:\n sub $8,%rsp\n mov %rcx,%rax\n mov %rdx,%rcx\n mov %r8,%r9\n mov $0x1111,%r10\n mov $0x2222,%r11\n call *%rax\n"
" cmp $0x1111,%r10\n jne 1f\n cmp $0x2222,%r11\n jne 1f\n add $8,%rsp\n ret\n1:\n mov $-1,%rax\n add $8,%rsp\n ret\n"
);
extern uint64_t test_po_call(const void *stub,void *map,void *state);
static void test_pick_order(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    stats.active=1;boot_mode=1;boot_phase=3;stats.issued=187;ui_thread=GetCurrentThreadId();
    po_row_t *saved_ring=po_ring;po_ring=VirtualAlloc(NULL,sizeof(po_row_t)*PO_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(po_ring);
    po_plain=test_po_plain;po_state=test_po_state;po_rows=0;po_from=150;po_to=200;po_failures=0;po_picks=po_moved=po_small=0;
    test_po_map map;unsigned char *t=(unsigned char*)&map;
    const int32_t weights[4]={1,2,0,4}; /* a weight of 0 counts as 1: total 8 */
    /* The two orders. */
    test_po_build(&map,weights,4,4,8);
    const unsigned char *pool[8],*tree[8];
    assert(po_pool(t,pool,8)==4 && pool[0]==test_po_node(0) && pool[3]==test_po_node(3));
    assert(po_tree(t,tree,8)==4 && tree[0]==test_po_node(3) && tree[3]==test_po_node(0));
    assert(po_pool(t,pool,3)==0xffffffffu && po_tree(t,tree,3)==0xffffffffu);
    /* The walk subtracts, and 0 counts as 1. */
    assert(po_walk(pool,4,0)==test_po_node(0) && po_walk(pool,4,1)==test_po_node(1) && po_walk(pool,4,2)==test_po_node(1));
    assert(po_walk(pool,4,3)==test_po_node(2) && po_walk(pool,4,4)==test_po_node(3) && po_walk(pool,4,7)==test_po_node(3) && !po_walk(pool,4,8));
    /* 'observe': the map's order, as native (draw 0: the last inserted comes first in the tree). */
    po_mode=PO_OBSERVE;test_po_value=0;test_po_plain_calls=0;
    assert(po_pick(0,0xc7d3f6,t,NULL)==test_po_key(3) && test_po_plain_calls==1 && po_moved==1 && po_rows==1);
    char out[256];assert(po_row(0,out,sizeof(out))>0);
    assert(strstr(out,"tick=187 fn=4759e0 caller=c7d3f6 size=4 mode=4 total=8 draw=0 native=3 canon=0 moved=1"));
    /* 'canonical': file order, the same one draw. */
    po_mode=PO_CANONICAL;test_po_plain_calls=0;
    assert(po_pick(0,0,t,NULL)==test_po_key(0) && test_po_plain_calls==1 && po_moved==2);
    /* A draw both orders place on the same entry is not moved (tree: 4,0,2,1 weights 4,1,2,1 ; draw 4 is entry 2; pool draw 4 is entry 3). */
    test_po_value=7;LONG64 before=po_rows;uint64_t moved=po_moved;
    assert(po_pick(1,0,t,NULL)==test_po_key(3) && po_moved==moved+1 && po_rows==before+1);
    /* Modes: 0-2 the first of the order, no draw. */
    for(int mode=0;mode<3;mode++) {
        map.mode=mode;test_po_plain_calls=0;
        po_mode=PO_CANONICAL;assert(po_pick(0,0,t,NULL)==test_po_key(0));
        po_mode=PO_OBSERVE;assert(po_pick(0,0,t,NULL)==test_po_key(3));
        assert(!test_po_plain_calls);
    }
    /* Mode 3: one draw. An unknown mode: as 0-2. A total of 1 or less: no draw, the total itself walks. */
    map.mode=3;test_po_value=0;test_po_plain_calls=0;po_mode=PO_CANONICAL;
    assert(po_pick(0,0,t,NULL)==test_po_key(0) && test_po_plain_calls==1);
    map.mode=9;test_po_plain_calls=0;assert(po_pick(0,0,t,NULL)==test_po_key(0) && !test_po_plain_calls);
    map.mode=3;map.total=1;test_po_plain_calls=0;assert(po_pick(0,0,t,NULL)==test_po_key(1) && !test_po_plain_calls);
    /* Mode 4 running out draws once more; running out again gives the first. */
    map.mode=4;map.total=20;test_po_value=19;test_po_plain_calls=0;
    assert(po_pick(0,0,t,NULL)==test_po_key(0) && test_po_plain_calls==2);
    map.mode=3;test_po_plain_calls=0;assert(po_pick(0,0,t,NULL)==test_po_key(0) && test_po_plain_calls==1);
    /* The state goes to its generator on the variants that take one, and only those. */
    map.mode=3;map.total=8;test_po_value=0;test_po_plain_calls=test_po_state_calls=0;uint64_t word=5;
    assert(po_pick(2,0,t,&word)==test_po_key(0) && test_po_state_calls==1 && test_po_state_seen==&word && !test_po_plain_calls);
    assert(po_pick(3,0,t,&word)==test_po_key(0) && test_po_state_calls==2 && !test_po_plain_calls);
    assert(po_pick(2,0,t,NULL)==test_po_key(0) && test_po_plain_calls==1 && test_po_state_calls==2);
    assert(po_pick(0,0,t,&word)==test_po_key(0) && test_po_plain_calls==2 && test_po_state_calls==2);
    /* A table of one and an empty one: the same draws, no rows, not moved. */
    moved=po_moved;before=po_rows;
    const int32_t one[1]={3};test_po_build(&map,one,1,4,3);test_po_value=1;test_po_plain_calls=0;
    assert(po_pick(0,0,t,NULL)==test_po_key(0) && test_po_plain_calls==1);
    test_po_value=5;test_po_plain_calls=0;assert(po_pick(0,0,t,NULL)==test_po_key(0) && test_po_plain_calls==2);
    test_po_build(&map,one,0,4,3);test_po_value=0;test_po_plain_calls=0;assert(po_pick(0,0,t,NULL)==0 && test_po_plain_calls==2);
    test_po_build(&map,one,0,0,0);assert(po_pick(0,0,t,NULL)==0);
    assert(po_moved==moved && po_rows==before);
    /* A free node has no parent: skipped. Two chunks: the older one first. */
    static unsigned char older[0x10+PO_NODE*2];memset(older,0,sizeof(older));*(uint64_t*)older=2;
    test_po_build(&map,weights,2,3,3);
    *(unsigned char **)(test_po_chunk+8)=older;
    unsigned char *o=older+0x10;*(unsigned char **)o=t;*(uint64_t*)(o+0x20)=0x2000;*(int32_t*)(o+0x28)=1;
    map.size=3;
    assert(po_pool(t,pool,8)==3 && pool[0]==o && pool[1]==test_po_node(0) && pool[2]==test_po_node(1));
    *(unsigned char **)(test_po_chunk+8)=NULL;
    /* A pool that does not account for the size: 'canonical' stops and answers natively, 'observe' carries on. */
    test_po_build(&map,weights,4,0,8);map.size=4;*(unsigned char **)test_po_node(1)=NULL;
    assert(po_pool(t,pool,8)==0xffffffffu);
    *(unsigned char **)test_po_node(1)=test_po_node(2);
    map.chunks=NULL;po_mode=PO_OBSERVE;po_failures=0;assert(po_pick(0,0,t,NULL)==test_po_key(3) && !po_failures);
    po_mode=PO_CANONICAL;assert(po_pick(0,0,t,NULL)==test_po_key(3) && po_failures==1 && !strcmp(po_stage,"pool"));po_failures=0;po_stage="idle";
    /* Outside the window: counted, no row. */
    test_po_build(&map,weights,4,3,8);test_po_value=0;stats.issued=201;before=po_rows;moved=po_moved;
    po_pick(0,0,t,NULL);assert(po_rows==before && po_moved==moved+1);stats.issued=187;
    /* The stub, run for real: arguments, the caller from the stack, r10 and r11 kept. */
    unsigned char *page=VirtualAlloc(NULL,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(page);
    for(unsigned i=0;i<PO_TARGETS;i++) po_write_stub(page+PO_STUB*i,i);
    DWORD old,ignored;assert(VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&old));FlushInstructionCache(GetCurrentProcess(),page,0x1000);
    po_mode=PO_CANONICAL;test_po_value=0;test_po_state_calls=test_po_plain_calls=0;
    assert(test_po_call(page,t,&word)==test_po_key(0) && test_po_plain_calls==1 && !test_po_state_calls);
    assert(test_po_call(page+PO_STUB*2,t,&word)==test_po_key(0) && test_po_state_calls==1 && test_po_state_seen==&word);
    po_mode=PO_OBSERVE;assert(test_po_call(page+PO_STUB*3,t,NULL)==test_po_key(3));
    VirtualFree(page,0,MEM_RELEASE);
    assert(po_status(out,sizeof(out))>0 && strstr(out," window=150-200 "));
    /* Arming refusals (mode and window first, then the zero image). */
    po_installed=0;po_mode=PO_OFF;stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;po_stage="idle";
    assert(!po_arm(7) && !strcmp(po_stage,"mode"));
    assert(!po_window(0,10) && !po_window(10,5) && !po_window(1,700) && po_window(150,200) && po_from==150);
    assert(!po_arm(PO_OBSERVE) && !strcmp(po_stage,"pins") && !po_installed);
    /* Site CAS, ownership and restore on the plain image. */
    unsigned char *saved_page=po_page;po_page=po_alloc_page();assert(po_page);
    for(unsigned i=0;i<PO_TARGETS;i++) {memcpy(image+po_targets[i].rva,po_targets[i].orig,5);assert(po_state_of(i)==0);}
    assert(VirtualProtect(po_page,0x1000,PAGE_READWRITE,&old));for(unsigned i=0;i<PO_TARGETS;i++) po_write_stub(po_page+PO_STUB*i,i);assert(VirtualProtect(po_page,0x1000,PAGE_EXECUTE_READ,&ignored));
    for(unsigned i=0;i<PO_TARGETS;i++) {
        unsigned char patch[5]={0xe9};int32_t d=po_rel(i);memcpy(patch+1,&d,4);
        assert(view_cas_bytes(po_targets[i].rva,po_targets[i].orig,patch,5) && po_state_of(i)==1);po_done|=1u<<i;
    }
    po_installed=1;po_mode=PO_CANONICAL;assert(po_owned());
    image[po_targets[2].rva+2]^=1;assert(po_state_of(2)<0 && !po_owned());image[po_targets[2].rva+2]^=1;assert(po_owned());
    po_restore();assert(!po_installed && po_restored && !po_done && po_mode==PO_OFF);
    for(unsigned i=0;i<PO_TARGETS;i++) {assert(po_state_of(i)==0);memset(image+po_targets[i].rva,0,5);}
    VirtualFree(po_page,0,MEM_RELEASE);po_page=saved_page;
    po_plain=NULL;po_state=NULL;po_restored=0;po_stage="idle";po_from=po_to=0;VirtualFree(po_ring,0,MEM_RELEASE);po_ring=saved_ring;po_rows=0;po_failures=0;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("pick order pool/modes/draws/rows/stub/arm/site tests PASS\n");
}
static void test_pick_order_native_image(const char *path) {
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
    unsigned char *saved_page=po_page;po_page=NULL;
    assert(po_pins());
    /* The generators used: plain 0x14ddf70 (n in ecx), with state 0x14ddc40 (state rcx, n edx). */
    assert(image[0x475a24]==0xe8 && 0x475a29u+*(int32_t*)(image+0x475a25)==PO_RNG_PLAIN);
    assert(image[0x476d43]==0xe8 && 0x476d48u+*(int32_t*)(image+0x476d44)==PO_RNG_STATE && !memcmp(image+0x476d3d,"\x41\x8b\xd0\x49\x8b\xc9",6));
    assert(image[0x476d4d]==0xe8 && 0x476d52u+*(int32_t*)(image+0x476d4e)==PO_RNG_PLAIN);
    /* The pool (0x320850): a chunk {count, older}, nodes from +0x10, 0x30 each, handed out ascending, the list at header +0x38. */
    assert(!memcmp(image+0x320881,"\x49\x89\x18\x49\x89\x68\x08",7) && !memcmp(image+0x3208af,"\x48\x8d\x49\x30",4));
    assert(!memcmp(image+0x3208c7,"\x49\x8d\x40\x10",4) && !memcmp(image+0x3208d4,"\x48\x8b\x47\x38",4) && !memcmp(image+0x3208e5,"\x4c\x89\x47\x38",4));
    /* A table's insert takes the node from the pool head and links it with a parent (0x5a06af .. 0x5a06c3). */
    assert(!memcmp(image+0x5a06af,"\x49\x8b\x46\x10\x48\x89\x47\x10",8) && !memcmp(image+0x5a06c3,"\x49\x89\x3e",3));
    /* The library load: a saved game runs it (0x9eebab), it reaches the macros (0x9f5610, 0xa2d3d0) and they are cleared first (0xa2d3fd -> 0xa2d950). */
    assert(image[0x9eebab]==0xe8 && 0x9eebb0u+*(int32_t*)(image+0x9eebac)==0x9f3610u);
    assert(image[0x9f588b]==0xe8 && 0x9f5890u+*(int32_t*)(image+0x9f588c)==0xa2d3d0u && image[0xa2d3fd]==0xe8 && 0xa2d402u+*(int32_t*)(image+0xa2d3fe)==0xa2d950u);
    assert(!memcmp(image+0xa2d970,"\x48\x8b\x01\xba\x01\x00\x00\x00\xff\x10",10));
    image[po_targets[1].rva+0x20]^=1;assert(!po_pins());image[po_targets[1].rva+0x20]^=1;assert(po_pins());
    po_page=saved_page;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("pick order functions, generators, node pool and the library reload verified");
}
