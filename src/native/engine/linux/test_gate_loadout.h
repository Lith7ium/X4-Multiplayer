/* The loadout order twin (gate_loadout_order.h): its sites, stubs, Linux layouts and restoration on the
 * synthetic image, and on the executable's bytes both native rest loops against the stubs. The shared
 * order and arithmetic are tested in test_lockstep_loadout_order.h. Included by test_gate_linux.c. */
typedef struct {unsigned char *parent,*left,*right;uint64_t pad;const void *key,*key2;int32_t amount,pad2;uint64_t pad3;} tlx_node;
typedef struct {unsigned char *self,*root;uint64_t pad[2];unsigned char *first;uint64_t size;} tlx_map;
typedef struct {const void *vtable;unsigned char pad[0x18];const char *data;uint64_t length;char local[16];} tlx_macro;
typedef struct {uint64_t head;const char *p;uint64_t size;char local[16];unsigned char rest[0xd8];} tlx_ware;
static const void *tlx_vtable[20];
static tlx_macro tlx_macros[3];
static tlx_ware tlx_wares[3];
static void tlx_macro_set(unsigned i,const char *name) {
    tlx_macro *m=&tlx_macros[i];memset(m,0,sizeof(*m));
    tlx_vtable[19]=LX(LX_MACRO_NAME_GETTER_VA);m->vtable=tlx_vtable;
    m->length=strlen(name);m->data=name; /* a heap string: the pointer, not the local buffer */
}
static void tlx_ware_set(unsigned i,const char *id) {
    tlx_ware *w=&tlx_wares[i];memset(w,0,sizeof(*w));
    w->size=strlen(id);assert(w->size<16);memcpy(w->local,id,w->size);w->p=w->local; /* local: p points 0x10 past itself */
}
/* Three nodes: b the root, a left, c right; the walk is a, b, c. Keys are macros 0, 1, 2. */
static void tlx_map3(tlx_map *map,tlx_node *a,tlx_node *b,tlx_node *c) {
    memset(map,0,sizeof(*map));memset(a,0,sizeof(*a));memset(b,0,sizeof(*b));memset(c,0,sizeof(*c));
    map->self=(unsigned char*)map;map->root=(unsigned char*)b;map->first=(unsigned char*)a;map->size=3;
    b->parent=(unsigned char*)map;b->left=(unsigned char*)a;b->right=(unsigned char*)c;
    a->parent=(unsigned char*)b;c->parent=(unsigned char*)b;
    a->key=&tlx_macros[0];b->key=&tlx_macros[1];c->key=&tlx_macros[2];
}
static const char *tlx_walk(tlx_map *map,char *out) {
    out[0]=0;
    for(unsigned char *n=map->first;n!=(unsigned char*)map;n=ll_next(n)) {
        const tlx_macro *m=((tlx_node*)n)->key;strncat(out,m->data,1);
    }
    return out;
}
static unsigned tlx_calls[LL_CALLS];
static uint64_t tlx_generator(void *a,void *b,void *c,void *d,void *e,void *f,void *g,void *h) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;++tlx_calls[LL_UNIT];return 0x55;
}
static uint64_t tlx_wares_native(void *a,void *b,void *c,void *d,void *e,void *f,void *g,void *h) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;++tlx_calls[LL_WARES];return 0;
}
static uint64_t tlx_pick(void *a,void *b,void *c,void *d,void *e,void *f,void *g,void *h) {
    (void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;++tlx_calls[LL_PICK];return *(const uint64_t*)(*(unsigned char **)a+8);
}
/* The rest loops are entered by jmp with the stack the function has there (16-byte aligned); they leave
 * through the function's own epilogue, which returns from these. */
__asm__(
".text\n"
"tlx_enter_unit:\n" /* rdi code, rsi map, edx count, ecx map size */
" push %r15\n push %r14\n push %r13\n push %r12\n push %rbp\n push %rbx\n sub $0x88,%rsp\n"
" mov %rsi,0x18(%rsp)\n movq $0,0x60(%rsp)\n mov %edx,%r15d\n mov %ecx,%ebx\n mov %rdi,%rax\n xor %edi,%edi\n jmp *%rax\n"
"tlx_enter_deployable:\n" /* rdi code, rsi map, edx count */
" push %r15\n push %r14\n push %r13\n push %r12\n push %rbp\n push %rbx\n sub $0x178,%rsp\n"
" mov %rsi,0x40(%rsp)\n mov %edx,0x4c(%rsp)\n mov %edx,%eax\n xor %r14d,%r14d\n jmp *%rdi\n"
);
extern void tlx_enter_unit(const void *code,void *map,uint32_t count,uint32_t size);
extern void tlx_enter_deployable(const void *code,void *map,uint32_t count);
static void tlx_amounts(const tlx_node *a,const tlx_node *b,const tlx_node *c,int32_t x,int32_t y,int32_t z) {
    if(a->amount!=x || b->amount!=y || c->amount!=z) {fprintf(stderr,"amounts %d %d %d, want %d %d %d\n",a->amount,b->amount,c->amount,x,y,z);assert(0);}
}
/* Run one rest of `count` over a fresh map of three, by the unit (0) or the deployable (1) loop. */
static void tlx_rest(unsigned j,uint32_t count,tlx_node *a,tlx_node *b,tlx_node *c) {
    static tlx_map map;tlx_map3(&map,a,b,c);
    if(j==0) tlx_enter_unit(LX(LX_LL_REST_UNIT_VA),&map,count,3);else tlx_enter_deployable(LX(LX_LL_REST_DEPLOYABLE_VA),&map,count);
}
static void tlx_session(void) {
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    thunk=make_thunk();assert(thunk);
    ll_done=0;ll_installed=0;ll_restored=0;ll_mode=LL_OFF;ll_failures=0;ll_from=ll_to=0;ll_stage="idle";memset(ll_native,0,sizeof(ll_native));
}
static void tlx_sites_original(void) {
    for(unsigned i=0;i<LL_CALLS;i++) assert(ll_call_state(i)==0);
    for(unsigned j=0;j<LL_DETOURS;j++) assert(ll_detour_state(j)==0);
}
/* Synthetic image: the original calls and the loops' first instructions, an epilogue at each exit. */
static void test_loadout_order_linux(void) {
    for(unsigned i=0;i<LL_CALLS;i++) {
        unsigned char call[5]={0xe8};int32_t rel=(int32_t)(LX(ll_calls[i].target+LX_IMAGE_BASE)-(ll_site(i)+5));memcpy(call+1,&rel,4);
        tl_write(ll_site(i),call,5);
    }
    for(unsigned j=0;j<LL_DETOURS;j++) tl_write(ll_detour_at(j),ll_detours[j].orig,5);
    static const unsigned char exit0[18]={0x48,0x81,0xc4,0x88,0,0,0,0x5b,0x5d,0x41,0x5c,0x41,0x5d,0x41,0x5e,0x41,0x5f,0xc3};
    static const unsigned char exit1[18]={0x48,0x81,0xc4,0x78,0x01,0,0,0x5b,0x5d,0x41,0x5c,0x41,0x5d,0x41,0x5e,0x41,0x5f,0xc3};
    tl_write(LX(LX_LL_REST_UNIT_EXIT_VA),exit0,sizeof(exit0));tl_write(LX(LX_LL_REST_DEPLOYABLE_EXIT_VA),exit1,sizeof(exit1));
    tlx_session();
    /* Not before the load, not off the MoveWorker, not without identities when rows are asked for. */
    boot_phase=2;assert(!ll_arm(LL_CANONICAL) && !ll_installed);boot_phase=0;
    const int identities=world_identity_installed;world_identity_installed=0;
    assert(ll_window(150,200) && !ll_arm(LL_CANONICAL) && !strcmp(ll_stage,"needs"));ll_from=ll_to=0;world_identity_installed=identities;
    assert(!ll_arm(3) && !strcmp(ll_stage,"mode"));
    tlx_sites_original();
    assert(ll_arm(LL_CANONICAL) && ll_installed && ll_owned() && ll_check() && ll_arm(LL_CANONICAL) && !ll_arm(LL_OBSERVE));
    for(unsigned i=0;i<LL_CALLS;i++) {
        assert(ll_call_state(i)==1);
        void *target;memcpy(&target,thunk+LXL_STUB+0x20*i+6,8);assert(target==(void*)ll_entries[i]);
    }
    for(unsigned j=0;j<LL_DETOURS;j++) assert(ll_detour_state(j)==1);
    char perms[5];perms_of(thunk,perms);assert(!strcmp(perms,"r-xp"));
    /* Generator through its stub: the result map in id order (a "c", b "b", c "a": the walk becomes c, b, a). */
    tlx_macro_set(0,"c_drone_macro");tlx_macro_set(1,"b_drone_macro");tlx_macro_set(2,"a_drone_macro");
    static tlx_node a,b,c;static tlx_map map;char walk[8];tlx_map3(&map,&a,&b,&c);
    ll_native[LL_UNIT]=tlx_generator;memset(tlx_calls,0,sizeof(tlx_calls));
    ll_fn unit=(ll_fn)(void*)(thunk+LXL_STUB+0x20*LL_UNIT);
    assert(unit((void*)0x1000,&map,0,0,0,0,0,0)==0x55 && tlx_calls[LL_UNIT]==1 && ll_relinked==1);
    assert(!strcmp(tlx_walk(&map,walk),"abc") && map.size==3 && !ll_failures);
    /* Existing users: the second key breaks a tie of the first. */
    static tlx_node e1,e2,e3;static tlx_map users;tlx_map3(&users,&e1,&e2,&e3);
    e1.key=e2.key=e3.key=&tlx_macros[2];e1.key2=&tlx_macros[0];e2.key2=&tlx_macros[2];e3.key2=&tlx_macros[1];
    ll_native[LL_EXISTING]=tlx_wares_native;
    ((ll_fn)(void*)(thunk+LXL_STUB+0x20*LL_EXISTING))(NULL,&users,0,0,0,0,0,0);
    assert(users.first==(unsigned char*)&e2 && ll_next((unsigned char*)&e2)==(unsigned char*)&e3 && ll_next((unsigned char*)&e3)==(unsigned char*)&e1 && !ll_failures);
    /* The ware list: 8-byte head, four inline, capacity +0x28, count +0x30; ids "z", "m", "a". */
    tlx_ware_set(0,"zz_ware");tlx_ware_set(1,"mm_ware");tlx_ware_set(2,"aa_ware");
    static struct {uint64_t head;uint64_t data[4];uint64_t capacity,count;} list;
    memset(&list,0,sizeof(list));list.capacity=4;list.count=3;
    for(unsigned i=0;i<3;i++) list.data[i]=(uint64_t)(uintptr_t)&tlx_wares[i];
    ll_native[LL_WARES]=tlx_wares_native;
    ((ll_fn)(void*)(thunk+LXL_STUB+0x20*LL_WARES))(&list,0,0,0,0,0,0,0);
    assert(list.data[0]==(uint64_t)(uintptr_t)&tlx_wares[2] && list.data[1]==(uint64_t)(uintptr_t)&tlx_wares[1] && list.data[2]==(uint64_t)(uintptr_t)&tlx_wares[0] && !ll_failures);
    /* The candidates: 0x28-byte entries, the macro at +8; the pick sees id order. */
    static unsigned char entries[3][0x28];void *vec[3]={entries,entries[3],entries[3]};
    for(unsigned i=0;i<3;i++) {const void *m=&tlx_macros[i];memset(entries[i],0,0x28);memcpy(entries[i]+8,&m,8);entries[i][0x20]=(unsigned char)i;}
    ll_native[LL_PICK]=tlx_pick;
    uint64_t picked=((ll_fn)(void*)(thunk+LXL_STUB+0x20*LL_PICK))(vec,0,0,0,0,0,0,0);
    assert(picked==(uint64_t)(uintptr_t)&tlx_macros[2] && entries[0][0x20]==2 && entries[1][0x20]==1 && entries[2][0x20]==0);
    /* Something that is not a macro: no order is made up, the gate stops. */
    static tlx_map bad;static tlx_node x,y,z;tlx_map3(&bad,&x,&y,&z);y.key=&tlx_wares[0];
    unit((void*)0x1000,&bad,0,0,0,0,0,0);assert(ll_failures==1 && !strcmp(ll_stage,"name") && !ll_check());
    ll_failures=0;ll_stage="armed";
    /* The rests through the stubs: four over three, the extra one to the first in id order (node c). */
    tlx_rest(0,4,&a,&b,&c);tlx_amounts(&a,&b,&c,1,1,2);
    tlx_rest(1,4,&a,&b,&c);tlx_amounts(&a,&b,&c,1,1,2);
    tlx_rest(0,2,&a,&b,&c);tlx_amounts(&a,&b,&c,0,1,1);
    assert(ll_rests==3 && ll_moved==3 && !ll_failures);
    /* A site taken over is a failure; restored, every byte is the original's. */
    unsigned char saved[5];memcpy(saved,ll_site(LL_PICK),5);
    unsigned char foreign[5]={0xe8,0,0,0,0};tl_write(ll_site(LL_PICK),foreign,5);
    assert(!ll_owned() && !ll_check() && ll_failures==1 && !strcmp(ll_stage,"site_changed"));
    tl_write(ll_site(LL_PICK),saved,5);ll_failures=0;
    ll_restore();assert(!ll_installed && ll_restored && !ll_done);
    tlx_sites_original();
    printf("loadout order (synthetic) PASS\n");
}
/* The executable's bytes: the native loops run (their pages made executable), the stubs give the same counts
 * in 'observe' and the id order in 'canonical'; the sites are restored exactly. */
static void test_loadout_order_elf(void) {
    unsigned char before_unit[0xf1],before_deployable[0x12d];
    memcpy(before_unit,LX(0x1e9e7ad),sizeof(before_unit));memcpy(before_deployable,LX(0x1df8187),sizeof(before_deployable));
    const uintptr_t pages[3]={0x1e9e000,0x1df7000,0x1df8000};
    for(unsigned i=0;i<3;i++) assert(!mprotect(LX(pages[i]),GATE_PAGE,PROT_READ|PROT_EXEC));
    tlx_macro_set(0,"c_drone_macro");tlx_macro_set(1,"b_drone_macro");tlx_macro_set(2,"a_drone_macro");
    static tlx_node a,b,c;
    /* Native: the rest goes to the first nodes in map order. */
    const uint32_t counts[4]={4,2,7,3};
    int32_t native[2][4][3];
    for(unsigned j=0;j<2;j++) for(unsigned k=0;k<4;k++) {
        tlx_rest(j,counts[k],&a,&b,&c);native[j][k][0]=a.amount;native[j][k][1]=b.amount;native[j][k][2]=c.amount;
    }
    tlx_amounts(&a,&b,&c,1,1,1);
    assert(native[0][0][0]==2 && native[0][0][1]==1 && native[0][0][2]==1 && native[0][1][0]==1 && native[0][1][1]==1 && native[0][1][2]==0);
    assert(!memcmp(native[0],native[1],sizeof(native[0])));
    tlx_session();tlx_sites_original();
    assert(ll_arm(LL_OBSERVE) && ll_owned());
    for(unsigned j=0;j<2;j++) for(unsigned k=0;k<4;k++) {tlx_rest(j,counts[k],&a,&b,&c);tlx_amounts(&a,&b,&c,native[j][k][0],native[j][k][1],native[j][k][2]);}
    assert(ll_rests==6 && ll_moved==6 && !ll_failures); /* every rest with a remainder, each moved by the id order */
    ll_restore();assert(!ll_installed);tlx_sites_original();
    tlx_session();assert(ll_arm(LL_CANONICAL));
    tlx_rest(0,4,&a,&b,&c);tlx_amounts(&a,&b,&c,1,1,2);
    tlx_rest(1,2,&a,&b,&c);tlx_amounts(&a,&b,&c,0,1,1);
    tlx_rest(1,7,&a,&b,&c);tlx_amounts(&a,&b,&c,2,2,3);
    assert(!ll_failures);
    ll_restore();assert(!ll_installed);tlx_sites_original();
    assert(!memcmp(before_unit,LX(0x1e9e7ad),sizeof(before_unit)) && !memcmp(before_deployable,LX(0x1df8187),sizeof(before_deployable)));
    assert(verify() && !strcmp(verify_stage,"ok"));
    for(unsigned i=0;i<3;i++) assert(!mprotect(LX(pages[i]),GATE_PAGE,PROT_READ|PROT_WRITE));
    printf("loadout order on the executable PASS\n");
}
