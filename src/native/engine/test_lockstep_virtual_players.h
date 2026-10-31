/* Tests of lockstep_virtual_players.h on the fake image region (no X4). */
static unsigned char *test_vpl_controller_value;
static void *test_vpl_controller(void *game) {(void)game;return test_vpl_controller_value;}
static DWORD test_vpl_thread_value;
static DWORD test_vpl_thread(void) {return test_vpl_thread_value;}
static void test_vpl_reset(void) {
    vpl_armed=vpl_pins_checked=vpl_census_done=0;vpl_stage="off";memset(&vpl_e,0,sizeof(vpl_e));
    vpl_passes=vpl_missing=vpl_identity_changes=vpl_config_changes=vpl_reentry_seen=vpl_unbound=vpl_read_failures=vpl_last_tick=0;
    memset(vpl_class_changes,0,sizeof(vpl_class_changes));memset(vpl_class_hash,0,sizeof(vpl_class_hash));
    memset(vpl_region_changes,0,sizeof(vpl_region_changes));memset(vpl_region_first,0,sizeof(vpl_region_first));
    memset(vpl_region_last,0,sizeof(vpl_region_last));vpl_changed_mask=0;vpl_have_prev=0;
    vpl_seen_player=vpl_seen_controller=vpl_seen_component=NULL;
    memset(vpl_stores,0,sizeof(vpl_stores));vpl_scope_slot=0;vpl_scope_thread=0;vpl_scope_hash=0;vpl_scope_player=NULL;
    vpl_scope_opened=vpl_scope_refused=0;vpl_thread_fn=vpl_thread_now;
    memset(vpl_census,0,sizeof(vpl_census));vpl_census_nodes=vpl_census_listed=vpl_census_dropped=vpl_census_failures=0;
    vps_mode=0;vps_tick=vps_at=vps_checks=vps_guard_bytes=0;vps_stage="off";vps_why="-";vps_p1=vps_p2=NULL;vps_create=NULL;vps_rng_word=NULL;
    vps_p1_refs=vps_p2_refs=vps_rng_moved=vps_render_moved=vps_context_moved=vps_singleton_moved=0;vps_guard_listed=0;
}
static int test_vpl_region_index(const char *name) {
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) if(!strcmp(vpl_regions[i].name,name)) return (int)i;
    assert(0);return -1;
}
/* The table: the input bank of d84c20 (0x2f4b720..0x2f4b998) is tiled exactly,
 * image regions never overlap, all lie in .data, player fields in the entity. */
static void test_vpl_table(void) {
    unsigned at=0x2f4b720;
    for(;;) {
        int found=0;
        for(unsigned i=0;i<VPL_REGION_COUNT;i++) if(vpl_regions[i].base==VPL_BASE_IMAGE && vpl_regions[i].offset==at) {at+=vpl_regions[i].size;found=1;break;}
        if(!found || at>=0x2f4b998) break;
    }
    assert(at==0x2f4b998 && VPL_REGION_COUNT<=64); /* the changed-region mask is 64 bits */
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) {
        const vpl_region *a=&vpl_regions[i];
        assert(a->size && a->cls>=VPL_SIM && a->cls<VPL_CLASSES && a->name && strlen(a->name)<32);
        if(a->base==VPL_BASE_IMAGE) assert(a->offset>=0x2f3c000 && a->offset+a->size<=0x6d26140);
        else assert(a->base==VPL_BASE_PLAYER && a->offset+a->size<=0x8a700);
        for(unsigned j=i+1;j<VPL_REGION_COUNT;j++) {
            const vpl_region *b=&vpl_regions[j];
            assert(strcmp(a->name,b->name));
            if(a->base==b->base) assert(a->offset+a->size<=b->offset || b->offset+b->size<=a->offset);
        }
    }
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) if(vpl_regions[i].base==VPL_BASE_IMAGE) {
        int covered=0;
        for(unsigned g=0;g<VPL_GUARD_COUNT;g++)
            covered|=vpl_regions[i].offset>=vpl_guard_pages[g] && vpl_regions[i].offset+vpl_regions[i].size<=vpl_guard_pages[g]+4096u;
        assert(covered);
    }
    /* Neighbours: disjoint from every image region, the reentry flag and each other; each touches a guard page. */
    assert(VPL_NEIGHBOUR_COUNT<=254);
    for(unsigned k=0;k<VPL_NEIGHBOUR_COUNT;k++) {
        const vpl_neighbour *a=&vpl_neighbours[k];int touches=0;
        assert(a->size && a->name && strlen(a->name)<32 && a->offset>=0x2f3c000 && a->offset+a->size<=0x6d26140);
        assert(VPL_REENTRY<a->offset || VPL_REENTRY>=a->offset+a->size);
        for(unsigned i=0;i<VPL_REGION_COUNT;i++) if(vpl_regions[i].base==VPL_BASE_IMAGE)
            assert(a->offset+a->size<=vpl_regions[i].offset || vpl_regions[i].offset+vpl_regions[i].size<=a->offset);
        for(unsigned j=k+1;j<VPL_NEIGHBOUR_COUNT;j++)
            assert(strcmp(a->name,vpl_neighbours[j].name) &&
                   (a->offset+a->size<=vpl_neighbours[j].offset || vpl_neighbours[j].offset+vpl_neighbours[j].size<=a->offset));
        for(unsigned g=0;g<VPL_GUARD_COUNT;g++) touches|=a->offset<vpl_guard_pages[g]+4096u && a->offset+a->size>vpl_guard_pages[g];
        assert(touches);
    }
    /* vp-D1: the padding after the bool at 0x31be718 is not declared; the zone pointer hashes by identity. */
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) if(vpl_regions[i].base==VPL_BASE_IMAGE)
        assert(vpl_regions[i].offset+vpl_regions[i].size<=0x31be719u || vpl_regions[i].offset>=0x31be71cu);
    {   const vpl_region *z=&vpl_regions[test_vpl_region_index("environment_zone")];
        unsigned char bytes[8]={0};uint64_t zero=0,one=1;static unsigned char object[16];uint64_t runtime=0x1234;unsigned char *o=object;
        assert(z->pointer && z->size==8 && z->offset==0x6b661e8 && vpl_region_hash(z,bytes,7)==vpl_fnv((const unsigned char*)&zero,8,7));
        memcpy(object+8,&runtime,8);memcpy(bytes,&o,8);
        assert(vpl_region_hash(z,bytes,7)==vpl_fnv((const unsigned char*)&one,8,7)); /* no portable identity in the tests: 1 */
        for(unsigned i=0;i<VPL_REGION_COUNT;i++) assert(!vpl_regions[i].pointer || vpl_regions[i].size==8);}
    /* What the scopes must never move: the UI's control share and its contexts. */
    assert(vpl_regions[test_vpl_region_index("bank_controls_share")].cls==VPL_LOCAL && vpl_regions[test_vpl_region_index("bank_controls_share")].offset==0x2f4b7e0);
    assert(vpl_regions[test_vpl_region_index("bank_contexts")].cls==VPL_LOCAL && vpl_regions[test_vpl_region_index("bank_contexts")].offset==0x2f4b829);
    assert(vpl_regions[test_vpl_region_index("bank_flight_control")].cls==VPL_SIM);
    assert(vpl_block_bytes(0xffu)<=(int)VPL_BLOCK_MAX && vpl_block_bytes(1u<<VPL_SIM)>0 && vpl_block_bytes(1u<<VPL_SIM)<vpl_block_bytes(0xffu));
    for(unsigned i=0;i<VPL_HANDLER_COUNT;i++) for(unsigned j=i+1;j<VPL_HANDLER_COUNT;j++) assert(vpl_handlers[i].rva!=vpl_handlers[j].rva);
    assert(vpl_handler_kind((uintptr_t)(image+0xd870b0),image)==VPL_H_FLIGHT && vpl_handler_kind((uintptr_t)(image+0x7e8ef0),image)==VPL_H_WEAPON);
    assert(vpl_handler_kind((uintptr_t)(image+0x71bac0),image)==VPL_H_PLAYER && vpl_handler_kind((uintptr_t)(image+0x122c070),image)==VPL_H_CLIENT);
    assert(vpl_handler_kind((uintptr_t)(image+0x123456),image)==VPL_H_UNKNOWN && vpl_handler_kind((uintptr_t)image-16,image)==VPL_H_UNKNOWN);
}
static void test_vpl_fill(unsigned char *base,unsigned char *player,unsigned cls_mask,unsigned char seed) {
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) {
        const vpl_region *r=&vpl_regions[i];
        if(!(cls_mask>>r->cls&1u)) continue;
        unsigned char *at=r->base==VPL_BASE_IMAGE?base+r->offset:player+r->offset;
        for(unsigned j=0;j<r->size;j++) at[j]=(unsigned char)(seed+i*7u+j);
    }
}
static unsigned char test_vpl_player[0x8000];
static void test_vpl_swaps(void) {
    unsigned char *base=image,*player=test_vpl_player;
    unsigned char before[VPL_BLOCK_MAX],after[VPL_BLOCK_MAX],other[VPL_BLOCK_MAX],store[VPL_BLOCK_MAX];
    test_vpl_fill(base,player,0xffu,0x11);
    int all=vpl_gather(base,player,0xffu,before,sizeof(before)),sim=vpl_block_bytes(1u<<VPL_SIM);
    assert(all==vpl_block_bytes(0xffu) && vpl_gather(base,player,1u<<VPL_SIM,store,sizeof(store))==sim);
    assert(vpl_gather(base,player,0xffu,before,8)==-1 && vpl_gather(base,NULL,1u<<VPL_SIM,store,sizeof(store))==-1);
    /* Exchange swaps exactly the SIM bytes; every other class keeps its bytes. */
    for(int i=0;i<sim;i++) store[i]=(unsigned char)(0xa0+i);
    memcpy(other,store,(size_t)sim);
    assert(vpl_exchange(base,player,1u<<VPL_SIM,store,(size_t)sim)==sim);
    unsigned char live[VPL_BLOCK_MAX];assert(vpl_gather(base,player,1u<<VPL_SIM,live,sizeof(live))==sim && !memcmp(live,other,(size_t)sim));
    unsigned char device_now[VPL_BLOCK_MAX],device_then[VPL_BLOCK_MAX];int dev=vpl_block_bytes(1u<<VPL_DEVICE);
    test_vpl_fill(base,player,1u<<VPL_DEVICE,0x11);
    assert(vpl_gather(base,player,1u<<VPL_DEVICE,device_now,sizeof(device_now))==dev);
    assert(vpl_exchange(base,player,1u<<VPL_SIM,store,(size_t)sim)==sim);
    assert(vpl_gather(base,player,0xffu,after,sizeof(after))==all && !memcmp(before,after,(size_t)all));
    assert(vpl_gather(base,player,1u<<VPL_DEVICE,device_then,sizeof(device_then))==dev && !memcmp(device_now,device_then,(size_t)dev));
    assert(!memcmp(store,other,(size_t)sim));
    assert(vpl_exchange(base,player,1u<<VPL_SIM,store,(size_t)sim-1)==-1 && vpl_exchange(base,player,1u<<VPL_SIM,store,(size_t)sim+1)==-1);
    test_vpl_fill(base,player,0xffu,0x11);
    /* Scopes: MoveWorker only, slots 2..4 with a store, reentry clear, never nested. */
    test_vpl_reset();vpl_thread_fn=test_vpl_thread;test_vpl_thread_value=77;base[VPL_REENTRY]=0;
    assert(!vpl_enter(2,base,player,77));                         /* no store yet */
    assert(!vpl_store_init(1,base,player) && !vpl_store_init(5,base,player) && vpl_store_init(2,base,player) && vpl_store_init(3,base,player));
    assert(vpl_stores[2].length==(unsigned)sim);
    for(unsigned i=0;i<vpl_stores[2].length;i++) vpl_stores[2].data[i]^=0x5a; /* slot 2 differs from slot 1 */
    memcpy(other,vpl_stores[2].data,vpl_stores[2].length);
    assert(!vpl_enter(1,base,player,77) && !vpl_enter(5,base,player,77) && !vpl_enter(2,base,NULL,77) && !vpl_enter(2,base,player,78));
    base[VPL_REENTRY]=1;assert(!vpl_enter(2,base,player,77));base[VPL_REENTRY]=0;
    assert(vpl_scope_refused==6 && !vpl_scope_slot);
    assert(vpl_gather(base,player,1u<<VPL_SIM,before,sizeof(before))==sim);
    assert(vpl_enter(2,base,player,77) && vpl_scope_slot==2 && vpl_scope_opened==1);
    assert(vpl_gather(base,player,1u<<VPL_SIM,live,sizeof(live))==sim && !memcmp(live,other,(size_t)sim) && !memcmp(vpl_stores[2].data,before,(size_t)sim));
    assert(!vpl_enter(3,base,player,77) && vpl_scope_slot==2);   /* never nested */
    /* The stock path writes slot 2's state while the scope is open. */
    base[vpl_regions[test_vpl_region_index("bank_rot_throttle")].offset+0xc]^=0xff;
    player[0x6ab4]=3;
    test_vpl_thread_value=78;assert(!vpl_leave(base));test_vpl_thread_value=77; /* the opening thread only */
    base[VPL_REENTRY]=1;assert(!vpl_leave(base));base[VPL_REENTRY]=0;
    assert(vpl_leave(base) && !vpl_scope_slot);
    assert(vpl_gather(base,player,1u<<VPL_SIM,live,sizeof(live))==sim && !memcmp(live,before,(size_t)sim)); /* canonical back */
    assert(memcmp(vpl_stores[2].data,other,(size_t)sim));                                                 /* slot 2 kept its writes */
    assert(!vpl_leave(base));
    /* A canonical byte changed while stored is caught on leave. */
    assert(vpl_enter(3,base,player,77));
    vpl_stores[3].data[0]^=1;assert(!vpl_leave(base) && !vpl_scope_slot);
    vpl_stores[3].data[0]^=1;
    printf("virtual players block, exchange and scope tests PASS (block %d bytes, sim %d)\n",all,sim);
    test_vpl_reset();
}
static void test_vpl_link(pnat_node *parent,pnat_node *child,int left) {child->parent=parent;if(left) parent->left=child;else parent->right=child;}
static void test_vpl_observe(void) {
    unsigned char *base=image,*player=test_vpl_player;
    test_vpl_reset();
    for(unsigned g=0;g<VPL_GUARD_COUNT;g++) memset(base+vpl_guard_pages[g],0,4096);
    memset(base+VPL_UNIVERSE,0,8);memset(base+VPL_INPUT,0,8);
    unsigned char *none=NULL;memcpy(base+VPL_PLAYER_NONE,&none,8);
    static unsigned char controller[0x100],component[16];
    uintptr_t vt=(uintptr_t)(base+VPL_CONTROLLER_VTABLE);unsigned char *comp=component;
    memcpy(controller,&vt,8);memcpy(controller+0x28,&comp,8);controller[0xe0]=1;
    test_vpl_controller_value=controller;
    assert(vpl_arm_using(base,test_vpl_controller) && vpl_armed && vpl_arm_using(base,test_vpl_controller) && !vpl_arm_using(base+1,test_vpl_controller));
    /* No player yet: counted, nothing compared. */
    vpl_pass_using(1,base);assert(vpl_missing==1 && !strcmp(vpl_stage,"no_player") && !vpl_have_prev);
    memcpy(base+VPL_PLAYER_NONE,&player,8);
    test_vpl_fill(base,player,0xffu,0x21);
    base[VPL_REENTRY]=0;vpl_pass_using(2,base);assert(!vpl_census_done && vpl_census_failures==1); /* no input object yet: retried */
    /* The fake input object with three multimaps: actions {0x12f flight},
     * states {0x5, 0x11 flight; 0x18 weapon; 0x99 unknown}, ranges empty. */
    static unsigned char input[0x6500];static pnat_node nodes[5];
    unsigned char *in=input;memcpy(base+VPL_INPUT,&in,8);
    pnat_node *actions=(pnat_node*)(input+VPL_MAPPING),*states=(pnat_node*)(input+VPL_MAPPING+0x40),*ranges=(pnat_node*)(input+VPL_MAPPING+0x80);
    memset(nodes,0,sizeof(nodes));memset(actions,0,sizeof(*actions));memset(states,0,sizeof(*states));memset(ranges,0,sizeof(*ranges));
    nodes[0].key=0x12f;nodes[0].handler=base+0xd86c80;actions->left=&nodes[0];nodes[0].parent=actions;
    nodes[1].key=0x11;nodes[1].handler=base+0xd870b0;states->left=&nodes[1];nodes[1].parent=states;
    nodes[2].key=0x5;nodes[2].handler=base+0xd870b0;test_vpl_link(&nodes[1],&nodes[2],1);
    nodes[3].key=0x18;nodes[3].handler=base+0x7e8ef0;test_vpl_link(&nodes[1],&nodes[3],0);
    nodes[4].key=0x99;nodes[4].handler=base+0x123456;test_vpl_link(&nodes[3],&nodes[4],0);
    base[VPL_REENTRY]=0;
    vpl_pass_using(2,base);
    assert(vpl_have_prev && !vpl_changed_mask && !vpl_identity_changes && !strcmp(vpl_stage,"observing") && vpl_seen_controller==controller);
    assert(vpl_census_done==1 && vpl_census_nodes==5 && vpl_census[0][VPL_H_FLIGHT]==1 && vpl_census[1][VPL_H_FLIGHT]==2 &&
           vpl_census[1][VPL_H_WEAPON]==1 && vpl_census[1][VPL_H_UNKNOWN]==1 && vpl_census_listed==1 && vpl_census_list[0].id==0x99 &&
           vpl_census_list[0].rva==0x123456);
    char text[4096];
    assert(vpl_census_text(text,sizeof(text))>0 && strstr(text,"census=1 nodes=5") && strstr(text,"action.flight=1") &&
           strstr(text,"state.flight=2") && strstr(text,"state.weapon=1") && strstr(text,"list=state:unknown:0x99:123456"));
    uint64_t sim=vpl_class_hash[VPL_SIM],device=vpl_class_hash[VPL_DEVICE];
    /* A SIM byte (activity) and a DEVICE byte (key flag) change. */
    player[0x6ab4]^=1;base[0x2f4b7ea]^=1;
    vpl_pass_using(3,base);
    int activity=test_vpl_region_index("player_assist_activity"),keys=test_vpl_region_index("bank_device_keys");
    assert(vpl_changed_mask==((1ULL<<activity)|(1ULL<<keys)) && vpl_class_changes[VPL_SIM]==1 && vpl_class_changes[VPL_DEVICE]==1);
    assert(vpl_region_changes[activity]==1 && vpl_region_first[activity]==3 && vpl_region_last[activity]==3);
    assert(vpl_class_hash[VPL_SIM]!=sim && vpl_class_hash[VPL_DEVICE]!=device && !vpl_config_changes && !vpl_undeclared_changes);
    /* CONFIG must never change; an undeclared guard byte is reported by RVA. Neighbour
     * bytes (the pathing buffer index, the event listener stack on the next guard page)
     * are counted per neighbour, never as undeclared. */
    base[0x3d19b11]^=1;base[0x2f4b9d3]^=1;base[0x2f4b9d4]^=1;base[VPL_REENTRY]=1;controller[0xe0]=0;
    base[0x2f4b6f4]^=1;base[0x2f4b6f7]^=1;base[0x38f4000]^=1;
    vpl_pass_using(4,base);
    assert(vpl_config_changes==1 && !strcmp(vpl_stage,"config_changed") && vpl_undeclared_changes==2 && vpl_reentry_seen==1 && vpl_unbound==1);
    assert(vpl_undeclared_text(text,sizeof(text))>0 && !strcmp(text,"2f4b9d3+2"));
    assert(vpl_neighbours_text(text,sizeof(text))>0 && strstr(text,"zone_move:0,") && strstr(text,",pathing_buffer_index:1,") &&
           strstr(text,",event_listener_stack:1"));
    {char small[16];assert(!vpl_neighbours_text(small,sizeof(small)));}
    assert(vpl_regions_text(text,sizeof(text))>0 && strstr(text,"player_assist_activity:sim:1:3:3") && strstr(text,"flight_settings:config:1:4:4"));
    assert(vpl_status(text,sizeof(text))>0 && strstr(text,"mode=observe stage=config_changed passes=5 tick=4 player=1 controller=1") &&
           strstr(text,"config_changes=1 undeclared=2 census=1 nodes=5"));
    /* Per-region hashes and raw class bytes of the last pass, for lane comparison. */
    assert(vpl_hashes_text(text,sizeof(text))>0 && strstr(text,"bank_device_rot:") && strstr(text,",player_shoot_flag:"));
    assert(vpl_raw_text(VPL_CONFIG,text,sizeof(text))==2*vpl_block_bytes(1u<<VPL_CONFIG) && !vpl_raw_text(VPL_CLASSES,text,sizeof(text)));
    {char small[8];assert(!vpl_raw_text(VPL_CONFIG,small,sizeof(small)));}
    /* Another controller (the player changed ship) is an identity change; no controller is reported. */
    base[VPL_REENTRY]=0;controller[0xe0]=1;test_vpl_controller_value=NULL;
    vpl_pass_using(5,base);assert(vpl_identity_changes==1 && vpl_seen_controller==NULL);
    vt=0;memcpy(controller,&vt,8);test_vpl_controller_value=controller;   /* a foreign vtable is no PlayerController */
    vpl_pass_using(6,base);assert(vpl_seen_controller==NULL && vpl_identity_changes==1);
    printf("virtual players observe tests PASS (%llu passes, census %u nodes)\n",(unsigned long long)vpl_passes,vpl_census_nodes);
    test_vpl_reset();
}
/* Step 3 spike 'construct' on the fake image: a fake factory does what 6fa8e0
 * does to the singleton, the references, the RNG word and the two flags; the
 * spike must put all of it back, keep the canonical block byte-identical and fail
 * closed on every deviation. */
static unsigned char test_vps_universe[0x300],test_vps_macro[0x80],test_vps_p2[0x100],test_vps_render[0x10];
static uint64_t test_vps_rng;
static int test_vps_variant,test_vps_calls;
static uint64_t *test_vps_rng_word(void) {return &test_vps_rng;}
static void **test_vps_factory(void *universe,void **out,void *parent,void *conn,void *a5,void *a6,void *macro,void *a8,uint64_t activate,
    void *a10,uint64_t a11,const void *s12,const void *s13,const void *s14,void *a15,uint64_t mode,uint64_t post) {
    ++test_vps_calls;
    assert(universe==test_vps_universe && !parent && !conn && !a5 && !a6 && macro==test_vps_macro && !a8 && activate==1 && !a10 && !a11 &&
           s12==image+VPS_EMPTY_STRING && s13==s12 && s14==s12 && !a15 && mode==0 && post==1);
    if(test_vps_variant==4) {volatile int *nothing=NULL;*nothing=1;}             /* a fault inside the factory */
    if(test_vps_variant==3) {*out=NULL;return out;}                              /* nothing built */
    unsigned char *p1=NULL;memcpy(&p1,test_vps_universe+VPS_SINGLETON,8);
    uintptr_t vt=(uintptr_t)(image+VPS_PLAYER_VTABLE);unsigned char *m=test_vps_macro;
    memset(test_vps_p2,0,sizeof(test_vps_p2));memcpy(test_vps_p2,&vt,8);memcpy(test_vps_p2+0x60,&m,8);
    *(int*)(test_vps_p2+0x40)=2;                                                 /* the Universe's and out's references */
    if(test_vps_variant!=2) {unsigned char *p2=test_vps_p2;memcpy(test_vps_universe+VPS_SINGLETON,&p2,8);--*(int*)(p1+0x40);}
    test_vps_rng=test_vps_rng*6364136223846793005ULL+1;test_vps_rng=test_vps_rng*6364136223846793005ULL+1;  /* two draws */
    *(int*)(test_vps_render+8)=7;image[VPS_CONTEXT_FLAG]=0;
    if(test_vps_variant==1) image[0x2f4b750]^=0x40;                               /* a SIM byte of the canonical block */
    if(test_vps_variant==5) image[0x2f4b6f4]^=1;                                  /* a guard byte outside the block */
    *out=test_vps_p2;return out;
}
static void test_vps_setup(unsigned char *p1,int variant,int refs) {
    unsigned char *base=image,*u=test_vps_universe,*m=test_vps_macro,*r=test_vps_render;
    uintptr_t vt=(uintptr_t)(base+VPS_PLAYER_VTABLE);
    test_vpl_reset();
    memset(test_vps_universe,0,sizeof(test_vps_universe));memset(test_vps_macro,0,sizeof(test_vps_macro));memset(test_vps_render,0,sizeof(test_vps_render));
    memcpy(base+VPL_UNIVERSE,&u,8);memcpy(u+VPS_SINGLETON,&p1,8);
    memcpy(p1,&vt,8);memcpy(p1+0x60,&m,8);*(int*)(p1+0x40)=refs;memset(p1+0x70,0,8);*(int*)(m+0x44)=VPS_PLAYER_CLASS;
    memset(base+VPS_EMPTY_STRING,0,32);memcpy(base+VPS_RENDER_FLAG,&r,8);*(int*)(r+8)=3;base[VPS_CONTEXT_FLAG]=1;base[VPL_REENTRY]=0;
    test_vps_rng=0x1234;test_vps_variant=variant;test_vps_calls=0;
    vps_create=test_vps_factory;vps_rng_word=test_vps_rng_word;
    static unsigned char controller[0x100],component[16];
    uintptr_t cvt=(uintptr_t)(base+VPL_CONTROLLER_VTABLE);unsigned char *comp=component;
    memcpy(controller,&cvt,8);memcpy(controller+0x28,&comp,8);controller[0xe0]=1;test_vpl_controller_value=controller;
    assert(vpl_arm_using(base,test_vpl_controller));
    vps_mode=1;vps_tick=5;vps_stage="waiting";
}
static void test_vps_construct(void) {
    unsigned char *base=image,*p1=test_vpl_player,*now=NULL;char text[2048];
    /* Normal: waits for its tick, builds once, puts everything back, then checks every pass. */
    test_vps_setup(p1,0,2);
    assert(vpl_pass_using(4,base) && test_vps_calls==0 && !strcmp(vps_stage,"waiting") && vpl_passes==1);
    assert(vpl_pass_using(5,base) && test_vps_calls==1 && !strcmp(vps_stage,"constructed") && vps_p2==test_vps_p2 && vps_at==5);
    memcpy(&now,test_vps_universe+VPS_SINGLETON,8);
    assert(now==p1 && *(int*)(p1+0x40)==2 && *(int*)(test_vps_p2+0x40)==1 && test_vps_rng==0x1234 && *(int*)(test_vps_render+8)==3 &&
           base[VPS_CONTEXT_FLAG]==1 && vps_rng_moved && vps_render_moved && vps_context_moved && vps_singleton_moved && !vps_guard_bytes);
    assert(vpl_pass_using(6,base) && test_vps_calls==1 && vps_checks==1 && vpl_passes==3);
    assert(vpl_status(text,sizeof(text))>0 && !strncmp(text,"mode=construct stage=",21) &&
           strstr(text," spike=construct spike_stage=constructed why=- at=5 due=5 p1_refs=2 p2_refs=1 singleton=1 rng=1 render=1 context=1 guard=0 checks=1"));
    /* Afterwards: the singleton taken over, the new player lost or parented: stop. */
    unsigned char *p2=test_vps_p2;memcpy(test_vps_universe+VPS_SINGLETON,&p2,8);
    assert(!vpl_pass_using(7,base) && !strcmp(vps_stage,"failed") && !strcmp(vps_why,"singleton_changed") && !vpl_pass_using(8,base));
    test_vps_setup(p1,0,2);assert(vpl_pass_using(5,base));
    *(int*)(test_vps_p2+0x40)=0;assert(!vpl_pass_using(6,base) && !strcmp(vps_why,"p2_lost"));
    test_vps_setup(p1,0,2);assert(vpl_pass_using(5,base));
    memcpy(test_vps_p2+0x70,&p1,8);assert(!vpl_pass_using(6,base) && !strcmp(vps_why,"p2_parented"));
    /* A guard byte outside the block that the factory moved is counted, not refused. */
    test_vps_setup(p1,5,2);assert(vpl_pass_using(5,base) && vps_guard_bytes==1 && vps_guard_listed==1 && vps_guard_rva[0]==0x2f4b6f4);
    assert(vpl_status(text,sizeof(text))>0 && strstr(text," guard=1 checks=0 guard_at=2f4b6f4"));
    /* Refused before the factory runs: references, class, vtable, the empty string, reentry, no RNG word. */
    test_vps_setup(p1,0,1);assert(!vpl_pass_using(5,base) && !strcmp(vps_why,"player_refs") && !test_vps_calls);
    test_vps_setup(p1,0,2);*(int*)(test_vps_macro+0x44)=0x4b;assert(!vpl_pass_using(5,base) && !strcmp(vps_why,"player_class") && !test_vps_calls);
    test_vps_setup(p1,0,2);memset(p1,0,8);assert(!vpl_pass_using(5,base) && !strcmp(vps_why,"player_vtable") && !test_vps_calls);
    test_vps_setup(p1,0,2);base[VPS_EMPTY_STRING+0x10]=3;assert(!vpl_pass_using(5,base) && !strcmp(vps_why,"empty_string") && !test_vps_calls);
    test_vps_setup(p1,0,2);base[VPL_REENTRY]=1;assert(!vpl_pass_using(5,base) && !strcmp(vps_why,"reentry") && !test_vps_calls);
    test_vps_setup(p1,0,2);vps_rng_word=NULL;assert(!vpl_pass_using(5,base) && !strcmp(vps_why,"no_factory") && !test_vps_calls);
    /* After the factory: a changed canonical block, an unexpected singleton, nothing built, a fault. */
    test_vps_setup(p1,1,2);assert(!vpl_pass_using(5,base) && !strcmp(vps_why,"block_changed"));
    memcpy(&now,test_vps_universe+VPS_SINGLETON,8);assert(now==p1 && test_vps_rng==0x1234);
    test_vps_setup(p1,2,2);assert(!vpl_pass_using(5,base) && !strcmp(vps_why,"singleton_unexpected"));
    test_vps_setup(p1,3,2);assert(!vpl_pass_using(5,base) && !strcmp(vps_why,"no_object") && test_vps_rng==0x1234);
    test_vps_setup(p1,4,2);assert(!vpl_pass_using(5,base) && !strcmp(vps_why,"construct_fault") && test_vps_rng==0x1234);
    memcpy(&now,test_vps_universe+VPS_SINGLETON,8);assert(now==p1);
    /* Off: a pass never builds anything. */
    test_vps_setup(p1,0,2);vps_mode=0;vps_stage="off";assert(vpl_pass_using(5,base) && vpl_pass_using(6,base) && !test_vps_calls);
    assert(vpl_status(text,sizeof(text))>0 && !strncmp(text,"mode=observe ",13) && !strstr(text,"spike="));
    printf("virtual players construct spike tests PASS (restores, checks, %d refusals)\n",13);
    test_vpl_reset();
}
/* The fake image region is shared with the other tests: every page touched here
 * (all regions lie in the guard pages), the input pointer, the empty string and
 * the render flag pointer are put back. */
static void test_virtual_players(void) {
    static unsigned char saved_pages[VPL_GUARD_COUNT][4096],saved_input[8],saved_empty[32],saved_render[8];
    assert(image);
    for(unsigned g=0;g<VPL_GUARD_COUNT;g++) memcpy(saved_pages[g],image+vpl_guard_pages[g],4096);
    memcpy(saved_input,image+VPL_INPUT,8);memcpy(saved_empty,image+VPS_EMPTY_STRING,32);memcpy(saved_render,image+VPS_RENDER_FLAG,8);
    test_vpl_table();test_vpl_swaps();test_vpl_observe();test_vps_construct();
    memcpy(image+VPS_EMPTY_STRING,saved_empty,32);memcpy(image+VPS_RENDER_FLAG,saved_render,8);
    for(unsigned g=0;g<VPL_GUARD_COUNT;g++) memcpy(image+vpl_guard_pages[g],saved_pages[g],4096);
    memcpy(image+VPL_INPUT,saved_input,8);
}
/* Against the pinned X4.exe: every pin, and every handler of the census table is
 * the target of a lea r8,[rip+disp] in one of the pinned registration functions. */
static void test_virtual_players_image(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;IMAGE_SECTION_HEADER sections[32];
    assert(fread(&dos,1,sizeof(dos),f)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && !fseek(f,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),f)==sizeof(nt) && nt.FileHeader.TimeDateStamp==0x6a1eaded && nt.FileHeader.NumberOfSections<=32);
    unsigned count=nt.FileHeader.NumberOfSections;
    assert(fread(sections,sizeof(*sections),count,f)==count);
    static unsigned char bytes[12000];
    unsigned found[VPL_HANDLER_COUNT];memset(found,0,sizeof(found));
    for(unsigned i=0;i<VPL_PIN_COUNT;i++) {
        assert(vpl_pin_table[i].size<=sizeof(bytes) && test_pnat_file_bytes(f,sections,count,vpl_pin_table[i].rva,vpl_pin_table[i].size,bytes));
        assert(hash_ok(bytes,vpl_pin_table[i].size,vpl_pin_table[i].sha));
        for(unsigned j=0;j+7<=vpl_pin_table[i].size;j++)
            if(bytes[j]==0x4c && bytes[j+1]==0x8d && bytes[j+2]==0x05) {
                int32_t disp;memcpy(&disp,bytes+j+3,4);unsigned target=vpl_pin_table[i].rva+j+7u+(unsigned)disp;
                for(unsigned h=0;h<VPL_HANDLER_COUNT;h++) if(vpl_handlers[h].rva==target) ++found[h];
            }
    }
    for(unsigned h=0;h<VPL_HANDLER_COUNT;h++) assert(found[h]);
    /* The flight registrations name the multimap: mov rbx,[rip->0x6cf1560]; ...; add rbx,0x63e0. */
    static const unsigned char add_mapping[7]={0x48,0x81,0xc3,0xe0,0x63,0x00,0x00};
    uint32_t disp;assert(test_pnat_file_bytes(f,sections,count,0xd898c0,32,bytes));
    memcpy(&disp,bytes+9,4);
    assert(bytes[6]==0x48 && bytes[7]==0x8b && bytes[8]==0x1d && 0xd898c0u+13u+disp==VPL_INPUT && !memcmp(bytes+0x14,add_mapping,7));
    unsigned data_lo=0,data_hi=0;
    for(unsigned j=0;j<count;j++) if(!memcmp(sections[j].Name,".data",6)) {data_lo=sections[j].VirtualAddress;data_hi=data_lo+sections[j].Misc.VirtualSize;}
    assert(data_lo);
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) if(vpl_regions[i].base==VPL_BASE_IMAGE) assert(vpl_regions[i].offset>=data_lo && vpl_regions[i].offset+vpl_regions[i].size<=data_hi);
    for(unsigned k=0;k<VPL_NEIGHBOUR_COUNT;k++) assert(vpl_neighbours[k].offset>=data_lo && vpl_neighbours[k].offset+vpl_neighbours[k].size<=data_hi);
    /* Two neighbours by their accesses: d8ddb0 reads the pathing buffer index; 99b450
     * clears the listener stack depth and then 0x100 x 0x40 bytes of the stack. */
    static const struct {unsigned at,length,disp_at,target;} reads[]={{0xd8ddc2,6,2,0x2f4b6f4},{0x99b985,7,3,0x38f0bb8},{0x99b98c,7,3,0x38f0bc0}};
    for(unsigned i=0;i<sizeof(reads)/sizeof(*reads);i++) {
        int32_t d;assert(test_pnat_file_bytes(f,sections,count,reads[i].at,reads[i].length,bytes));memcpy(&d,bytes+reads[i].disp_at,4);
        assert(reads[i].at+reads[i].length+(unsigned)d==reads[i].target);
    }
    assert(test_pnat_file_bytes(f,sections,count,0x99b993,5,bytes) && bytes[0]==0xb9 && bytes[1]==0x00 && bytes[2]==0x01 && !bytes[3] && !bytes[4]);
    assert(vpl_neighbours[VPL_NEIGHBOUR_COUNT-1].offset==0x38f0bb8 && vpl_neighbours[VPL_NEIGHBOUR_COUNT-1].size==8+0x100*0x40);
    /* Step 3 spike: the pinned factory path of a U::Player and the call layout it copies. */
    for(unsigned i=0;i<VPS_PIN_COUNT;i++) {
        assert(vps_pin_table[i].size<=sizeof(bytes) && test_pnat_file_bytes(f,sections,count,vps_pin_table[i].rva,vps_pin_table[i].size,bytes));
        assert(hash_ok(bytes,vps_pin_table[i].size,vps_pin_table[i].sha));
    }
    uint32_t entry=0;int32_t rel=0;uint64_t slot=0;
    assert(test_pnat_file_bytes(f,sections,count,0x8e2a38+4*VPS_PLAYER_CLASS,4,bytes));memcpy(&entry,bytes,4);assert(entry==0x8e2149u);
    assert(test_pnat_file_bytes(f,sections,count,0x8e2160,5,bytes) && bytes[0]==0xe8);memcpy(&rel,bytes+1,4);assert(0x8e2165u+(unsigned)rel==0x6fa0b0u);
    static const unsigned char alloc_size[5]={0xb9,0xe0,0xa7,0x08,0x00};                 /* mov ecx,0x8a7e0 */
    assert(test_pnat_file_bytes(f,sections,count,0x879bc4,5,bytes) && !memcmp(bytes,alloc_size,5));
    /* Player vtable: slot 578 is the post-init Player::vfunc578, 574 the empty init (ret 0). */
    assert(test_pnat_file_bytes(f,sections,count,VPS_PLAYER_VTABLE+578*8,8,bytes));memcpy(&slot,bytes,8);assert(slot==0x140000000ULL+0x6fb230);
    assert(test_pnat_file_bytes(f,sections,count,VPS_PLAYER_VTABLE+574*8,8,bytes));memcpy(&slot,bytes,8);assert(slot==0x140000000ULL+0x963b0);
    /* Universe::CreateComponent's call: post-init and activate bytes 1, empty string in r15, the factory. */
    static const unsigned char post17[8]={0xc6,0x84,0x24,0x80,0x00,0x00,0x00,0x01},activate9[5]={0xc6,0x44,0x24,0x40,0x01};
    assert(test_pnat_file_bytes(f,sections,count,0x338a8d,8,bytes) && !memcmp(bytes,post17,8));
    assert(test_pnat_file_bytes(f,sections,count,0x338abd,5,bytes) && !memcmp(bytes,activate9,5));
    assert(test_pnat_file_bytes(f,sections,count,0x338ae7,5,bytes) && bytes[0]==0xe8);memcpy(&rel,bytes+1,4);assert(0x338aecu+(unsigned)rel==VPS_FACTORY);
    assert(test_pnat_file_bytes(f,sections,count,0x3386b0,7,bytes) && bytes[0]==0x4c && bytes[1]==0x8d && bytes[2]==0x3d);
    memcpy(&rel,bytes+3,4);assert(0x3386b7u+(unsigned)rel==VPS_EMPTY_STRING);
    fclose(f);
    printf("pinned native image virtual players verified (%u functions, %u handlers, %u regions, spike %u functions)\n",(unsigned)VPL_PIN_COUNT,
        (unsigned)VPL_HANDLER_COUNT,(unsigned)VPL_REGION_COUNT,(unsigned)VPS_PIN_COUNT);
}
