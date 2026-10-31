/* native_gate_motion_watch: layout reads, partial layouts, references, path, row format, ring,
 * order and target rows. */
static __declspec(align(16)) unsigned char test_mw_object[0x400],test_mw_ctrl[0x200],test_mw_agent[0x1000],test_mw_model[0x100],test_mw_slots[0x400];
static __declspec(align(16)) unsigned char test_mw_turret[0x100],test_mw_module[0x100],test_mw_station[0x100],test_mw_path[0x200];
static uint64_t test_mw_ids[3];static unsigned char *test_mw_objects[3];
static void *test_mw_lookup(uint64_t runtime) {
    for(unsigned i=0;i<3;i++) if(test_mw_ids[i]==runtime) return test_mw_objects[i];
    return NULL;
}
/* turret 0x5ccca -> module 0x5ccaf -> station 0x3edc2 (saved 0x123456). */
static void test_mw_hierarchy(void) {
    memset(test_mw_turret,0,sizeof(test_mw_turret));memset(test_mw_module,0,sizeof(test_mw_module));memset(test_mw_station,0,sizeof(test_mw_station));
    unsigned char *objects[3]={test_mw_turret,test_mw_module,test_mw_station};uint64_t ids[3]={0x5ccca,0x5ccaf,0x3edc2};
    uint32_t types[3]={0x2b6e910u,0x2b518c8u,0x2b00000u};
    for(unsigned i=0;i<3;i++) {
        void *vt=image+types[i];memcpy(objects[i],&vt,8);memcpy(objects[i]+8,&ids[i],8);
        if(i<2) {void *parent=objects[i+1];memcpy(objects[i]+0x70,&parent,8);}
        test_mw_ids[i]=ids[i];test_mw_objects[i]=objects[i];
    }
}
static void test_motion_watch(void) {
    motion_watch_lookup_fn prior_lookup=motion_watch_lookup;motion_watch_lookup=test_mw_lookup;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0x5b065,0xa214c) && world_identity_record(0x3edc2,0x123456));
    test_mw_hierarchy();
    /* References: saved directly, through ancestors, unresolved, none. */
    motion_ref_t ref=motion_watch_ref(0x5ccca);
    assert(ref.key==0x123456 && ref.klass==WORLD_KEY_SAVED && ref.depth==2 && ref.type==0x2b6e910u);
    ref=motion_watch_ref(0x3edc2);assert(ref.key==0x123456 && !ref.depth && ref.type==0x2b00000u);
    ref=motion_watch_ref(0x9999);assert(!ref.key && ref.klass==WORLD_KEY_NONE && ref.depth==MOTION_REF_UNRESOLVED && ref.type==0xffffffffu);
    ref=motion_watch_ref(0);assert(!ref.key && ref.klass==WORLD_KEY_NONE && !ref.depth && !ref.type);
    memset(test_mw_module+0x70,0,8);ref=motion_watch_ref(0x5ccca);assert(!ref.key && ref.depth==MOTION_REF_UNRESOLVED && ref.type==0x2b6e910u);
    test_mw_hierarchy();
    char text[48];motion_ref_text(text,motion_watch_ref(0x5ccca));assert(!strcmp(text,"0000000000123456/0/2/2b6e910"));

    memset(test_mw_object,0,sizeof(test_mw_object));memset(test_mw_ctrl,0,sizeof(test_mw_ctrl));memset(test_mw_agent,0,sizeof(test_mw_agent));
    memset(test_mw_model,0,sizeof(test_mw_model));memset(test_mw_slots,0,sizeof(test_mw_slots));
    int attention=5;memcpy(test_mw_object+0x1ec,&attention,4);
    motion_watch_row_t r;void *agent=(void*)1;
    /* No controller: attention only. */
    assert(motion_watch_read(test_mw_object,0xa214c,7,&r,&agent) && r.attention==5 && r.ctrl_vt==0xffffffffu && r.flags==512 && !agent);
    /* A NavigationController with its back pointer, slots, cache and a flight model. */
    void *p=test_mw_ctrl;memcpy(test_mw_object+0x200,&p,8);
    void *vt=image+MOTION_WATCH_NAV_VTABLE;memcpy(test_mw_ctrl,&vt,8);
    p=test_mw_object;memcpy(test_mw_ctrl+0x28,&p,8);p=test_mw_slots;memcpy(test_mw_ctrl+0x30,&p,8);p=test_mw_agent;memcpy(test_mw_ctrl+0x40,&p,8);
    for(unsigned i=0;i<0x40;i++) test_mw_ctrl[0x60+i]=(unsigned char)(0x80+i);
    for(unsigned i=0;i<0x60;i++) {test_mw_slots[i]=(unsigned char)i;test_mw_slots[0x1a0+i]=(unsigned char)(0x40+i);}
    p=test_mw_object;memcpy(test_mw_agent+0xef0,&p,8);p=test_mw_model;memcpy(test_mw_agent+0xf10,&p,8);
    vt=image+0x2c4e9c8;memcpy(test_mw_model,&vt,8);for(unsigned i=0;i<0x50;i++) test_mw_model[0x20+i]=(unsigned char)(0xc0+i);
    uint64_t target_ref=0x5ccca;memcpy(test_mw_model+0x60,&target_ref,8);
    uint32_t buffer=1;memcpy(image+MOTION_WATCH_INDEX,&buffer,4);
    /* Empty path: count 0. */
    assert(motion_watch_read(test_mw_object,0xa214c,8,&r,&agent) && r.flags==(31|64|256|512|1024) && r.ctrl_vt==MOTION_WATCH_NAV_VTABLE && r.model_vt==0x2c4e9c8 && r.index==1);
    assert(!r.formation && !r.leader.key && !r.leader_ext[0]);
    assert(agent==test_mw_agent && !r.path_count && !r.path_ref.key);
    assert(r.slot[0][5]==5 && r.slot[1][5]==0x45 && r.cache[3]==0x83 && r.target[0x3f]==0xff);
    /* The target's reference ID is masked and mapped. */
    for(unsigned i=0x40;i<0x48;i++) assert(!r.target[i]);
    assert(r.target[0x48]==0x08 && r.target_ref.key==0x123456 && r.target_ref.depth==2 && r.target_ref.type==0x2b6e910u);
    /* Two FlightPoints: count 2, the first point with its reference masked and mapped and the connection as a flag. */
    memset(test_mw_path,0,sizeof(test_mw_path));
    for(unsigned i=0;i<0x50;i++) test_mw_path[0x10+i]=(unsigned char)(0x30+i);
    uint64_t point_ref=0x5b065,connection=0x1234;memcpy(test_mw_path+0x10,&point_ref,8);memcpy(test_mw_path+0x18,&connection,8);
    unsigned char *begin=test_mw_path,*end=test_mw_path+2*MOTION_PATH_STRIDE;memcpy(test_mw_agent+0x6a8,&begin,8);memcpy(test_mw_agent+0x6b0,&end,8);
    assert(motion_watch_read(test_mw_object,0xa214c,8,&r,NULL) && r.flags==(63|64|256|512|1024) && r.path_count==2 && r.path_conn==1);
    assert(r.path_ref.key==0xa214c && !r.path_ref.depth && r.path_ref.type==0xffffffffu);
    for(unsigned i=0;i<16;i++) assert(!r.path0[i]);
    assert(r.path0[0x10]==0x40 && r.path0[0x4f]==0x7f);
    /* A malformed vector (not a whole number of points): no path fields. */
    end=test_mw_path+MOTION_PATH_STRIDE+8;memcpy(test_mw_agent+0x6b0,&end,8);
    assert(motion_watch_read(test_mw_object,0xa214c,8,&r,NULL) && r.flags==(15|64|256|512|1024) && !r.path_count);
    end=test_mw_path+2*MOTION_PATH_STRIDE;memcpy(test_mw_agent+0x6b0,&end,8);
    /* Another controller type or a foreign back pointer: no pathing fields. */
    vt=image+0x2abd888;memcpy(test_mw_ctrl,&vt,8);assert(motion_watch_read(test_mw_object,0xa214c,9,&r,&agent) && r.ctrl_vt==0x2abd888 && r.flags==512 && !agent);
    vt=image+MOTION_WATCH_NAV_VTABLE;memcpy(test_mw_ctrl,&vt,8);p=test_mw_agent;memcpy(test_mw_ctrl+0x28,&p,8);
    assert(motion_watch_read(test_mw_object,0xa214c,9,&r,NULL) && r.flags==512);p=test_mw_object;memcpy(test_mw_ctrl+0x28,&p,8);
    /* An agent of another object: no path or model fields. */
    p=test_mw_ctrl;memcpy(test_mw_agent+0xef0,&p,8);
    assert(motion_watch_read(test_mw_object,0xa214c,9,&r,&agent) && r.flags==(7|512) && r.model_vt==0xffffffffu && !r.path_count && !agent);
    p=test_mw_object;memcpy(test_mw_agent+0xef0,&p,8);
    /* Agent fields, model plan and the formation leader. */
    {
        float speed=123.5f;memcpy(test_mw_agent+0x364,&speed,4);test_mw_agent[0x4d0]=1;test_mw_agent[0x4d2]=2;test_mw_agent[0xf7a]=7;test_mw_agent[0x4d3]=3;
        double decided=-1.0;memcpy(test_mw_agent+0xf40,&decided,8);float limit=96.0f;memcpy(test_mw_object+0x3b0,&limit,4);test_mw_object[0x3c8]=1;
        for(unsigned i=0;i<0x70;i++) test_mw_model[0x70+i]=(unsigned char)(0x10+i);
        static __declspec(align(16)) unsigned char formation[0x80],leader_agent[0x1000];static void *slot;
        memset(formation,0,sizeof(formation));memset(leader_agent,0,sizeof(leader_agent));
        slot=leader_agent;void *ps=&slot;memcpy(formation+0x48,&ps,8);p=formation;memcpy(test_mw_agent+0xf20,&p,8);
        float leader_speed=200.0f;memcpy(leader_agent+0x364,&leader_speed,4);p=test_mw_turret;memcpy(leader_agent+0xef0,&p,8); /* leader object: id 0x5ccca */
        assert(motion_watch_read(test_mw_object,0xa214c,8,&r,NULL) && r.flags==(63|64|128|256|512|1024) && r.formation==1);
        assert(!memcmp(r.agent_ext,&speed,4) && r.agent_ext[4]==1 && r.agent_ext[5]==2 && r.agent_ext[28]==7 && r.agent_ext[MOTION_AGENT_EXT-1]==3);
        assert(!memcmp(r.leader_ext,&leader_speed,4) && r.leader.key==0x123456 && r.leader.depth==2 && r.leader.type==0x2b6e910u);
        assert(r.model_ext[0]==0x10 && r.model_ext[0x6f]==0x7f);
        /* Models smaller than the window: only their own bytes; the zeroed rest cannot differ between clients. */
        {
            void *model_vt;memcpy(&model_vt,test_mw_model,8);
            static const struct {uint32_t vt,own;} small[3]={{0x2c4f7d0u,0},{0x2c4f6f0u,0x30},{0x2c4f8d0u,0x50}};
            for(unsigned k=0;k<3;k++) {
                void *v=image+small[k].vt;memcpy(test_mw_model,&v,8);
                assert(motion_watch_read(test_mw_object,0xa214c,8,&r,NULL) && (r.flags&256) && r.model_vt==small[k].vt);
                for(unsigned b=0;b<0x70;b++) {
                    const int padding=small[k].vt==0x2c4f6f0u && (b==0x17 || (b>=0x28 && b<0x30));
                    assert(r.model_ext[b]==(b<small[k].own && !padding?test_mw_model[0x70+b]:0));
                }
            }
            memcpy(test_mw_model,&model_vt,8);assert(motion_watch_read(test_mw_object,0xa214c,8,&r,NULL) && r.model_ext[0x6f]==0x7f);
        }
        assert(!memcmp(r.agent_ext+37,&decided,8) && !memcmp(r.ship_ext,&limit,4) && r.ship_ext[4]==1 && !r.wingmen && !r.wingman0.key);
        /* Flight parameters: the read slot (index 1), the written slot (index2 0) and the third copy (+0x340). */
        {
            float travel=2413.0f;memcpy(test_mw_agent+0x1a0+0x24,&speed,4);test_mw_agent[0x1a0+0x192]=1;memcpy(test_mw_agent+0x1a0+0x198,&travel,4);
            uint32_t prior_index2=0,zero=0;memcpy(&prior_index2,image+MOTION_PATHING_INDEX2,4);memcpy(image+MOTION_PATHING_INDEX2,&zero,4);
            assert(motion_watch_read(test_mw_object,0xa214c,8,&r,NULL) && (r.flags&1024) && r.index==1);
            assert(!memcmp(r.fp,&speed,4) && r.fp[6]==1 && !memcmp(r.fp+12,&travel,4) && !r.fp[16+6] && !memcmp(r.fp+32,&speed,4) && r.fp[32+6]==2);
            uint32_t two=2;memcpy(image+MOTION_PATHING_INDEX2,&two,4);
            assert(motion_watch_read(test_mw_object,0xa214c,8,&r,NULL) && !(r.flags&1024)); /* an index outside 0/1: no fields */
            memcpy(image+MOTION_PATHING_INDEX2,&prior_index2,4);
            memset(test_mw_agent+0x1a0+0x24,0,4);test_mw_agent[0x1a0+0x192]=0;memset(test_mw_agent+0x1a0+0x198,0,4);
        }
        /* As a formation leader: the wingmen count and the first wingman's ship. */
        static __declspec(align(16)) unsigned char led[0x80],wingman_agent[0x1000];static void *wingmen[2];
        memset(led,0,sizeof(led));memset(wingman_agent,0,sizeof(wingman_agent));wingmen[0]=wingman_agent;wingmen[1]=wingman_agent;
        void *wb=&wingmen[0],*we=&wingmen[2];memcpy(led+0x48,&wb,8);memcpy(led+0x50,&we,8);p=led;memcpy(test_mw_agent+0xf18,&p,8);
        p=test_mw_station;memcpy(wingman_agent+0xef0,&p,8); /* wingman ship: id 0x3edc2 (saved 0x123456) */
        assert(motion_watch_read(test_mw_object,0xa214c,8,&r,NULL) && r.wingmen==2 && r.wingman0.key==0x123456 && !r.wingman0.depth);
        memset(test_mw_agent+0xf18,0,8);
        memset(formation+0x48,0,8);assert(motion_watch_read(test_mw_object,0xa214c,8,&r,NULL) && r.formation==1 && !(r.flags&128) && !r.leader.key);
        memset(formation+0x48,0,8);memcpy(formation+0x48,&ps,8);
    }
    /* Rows through the ring: raw hex, no pointers or runtime IDs; the state slots as cur (slot[index]) and prev. */
    motion_watch_row_t *saved_ring=motion_watch_ring;LONG64 saved_rows=motion_watch_rows;
    static motion_watch_row_t ring[4];motion_watch_ring=ring;motion_watch_rows=1;
    assert(motion_watch_read(test_mw_object,0xa214c,12,&ring[0],NULL));
    char out[3072];assert(motion_watch_row(0,out,sizeof(out))>0);
    assert(strstr(out,"index=0 tick=12 saved=00000000000a214c attention=5 ctrl=02c51158 model=02c4e9c8 flags=2047 cur=404142"));
    assert(strstr(out," prev=000102") && strstr(out," cache=808182") && strstr(out," target=c0c1c2") && !strstr(out,"buffer="));
    assert(strstr(out," model_ext=101112") && strstr(out," agent_ext=0000f74201020000") && strstr(out,"070000000000000000000000000000f0bf0003 formation=1 leader=0000000000123456/0/2/2b6e910 leader_ext=00004843"));
    ring[0].index=0;assert(motion_watch_row(0,out,sizeof(out))>0 && strstr(out," cur=000102") && strstr(out," prev=404142"));
    assert(motion_watch_read(test_mw_object,0xa214c,12,&ring[0],NULL));
    assert(strstr(out,"fcfdfeff000000000000000008090a"));
    assert(strstr(out," target_ref=0000000000123456/0/2/2b6e910 path=2 path0=0000000000000000000000000000000040414243"));
    assert(strstr(out," path0_ref=00000000000a214c/0/0/ffffffff/1") && strstr(out," ship_ext=0000c04201 wingmen=0 wingman0=0000000000000000/2/0/0") && strlen(out)<2400);
    assert(!motion_watch_row(1,out,sizeof(out)));
    motion_watch_rows=MOTION_WATCH_RING+3;assert(motion_watch_row(1,out,sizeof(out))>0 && !strcmp(out,"index=1 evicted=1"));
    motion_watch_ring=saved_ring;motion_watch_rows=saved_rows;memset(image+MOTION_WATCH_INDEX,0,4);
    /* Adding refuses after the first admitted frame, off the MoveWorker, or when the lookup is not the pinned one (zero image). */
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;
    stats.issued=0;ui_thread=GetCurrentThreadId();assert(!motion_watch_add(0xa214c) && !motion_watch_count);
    stats.issued=3;assert(!motion_watch_add(0xa214c));stats.issued=0;assert(!motion_watch_add(0));
    ui_thread=GetCurrentThreadId()+1;assert(!motion_watch_add(0xa214c));
    stats.issued=saved_issued;ui_thread=saved_ui;
    assert(motion_watch_frame(5)); /* nothing armed: nothing to do */
    char status[256];assert(motion_watch_status(status,sizeof(status))>0 && strstr(status,"watched=0 ") && strstr(status," targets=0 "));
    /* Order rows: only watched ships, the point bytes (reference and connection masked) and the site; sites are e8 calls of the queue. */
    {
        motion_order_row_t *saved_ring=motion_order_ring;LONG64 saved_rows=motion_order_rows;static motion_order_row_t ring[4];
        memset(ring,0,sizeof(ring));motion_order_ring=ring;motion_order_rows=0;motion_watch_saved[0]=0xa214c;motion_watch_count=1;
        static unsigned char point[0x80];for(unsigned i=0;i<0x80;i++) point[i]=(unsigned char)i;
        uint64_t point_ref=0x5ccca;memcpy(point+0x10,&point_ref,8);
        motion_order_note(2,0x5b065,point);motion_order_note(1,0x9999,point);
        assert(motion_order_rows==1 && ring[0].ready==1 && ring[0].saved==0xa214c && ring[0].site==0x6e0c72 && !ring[0].point[0] && !ring[0].point[15]);
        assert(ring[0].point[0x10]==0x20 && ring[0].point[0x4f]==0x5f && ring[0].connection==1 && ring[0].ref.key==0x123456 && ring[0].ref.depth==2);
        char row[512];assert(motion_order_row(0,row,sizeof(row))>0 && strstr(row,"tick=") && strstr(row," saved=00000000000a214c site=006e0c72 chain=") &&
                            strstr(row," ref=0000000000123456/0/2/2b6e910/1 point=0000000000000000000000000000000020212223"));
        assert(!motion_order_row(1,row,sizeof(row)));
        ring[0].ready=0;assert(!motion_order_row(0,row,sizeof(row))); /* still being written */
        motion_order_ring=saved_ring;motion_order_rows=saved_rows;
        for(unsigned i=0;i<MOTION_ORDER_SITES;i++) assert(motion_order_state(i)<0); /* zero image: no e8 */
        for(unsigned i=0;i<MOTION_ORDER_SITES;i++) {image[motion_order_sites[i]]=0xe8;int32_t d=motion_order_rel(motion_order_sites[i],image+MOTION_ORDER_QUEUE);memcpy(image+motion_order_sites[i]+1,&d,4);}
        for(unsigned i=0;i<MOTION_ORDER_SITES;i++) assert(motion_order_state(i)==0);
        assert(!motion_order_pins() && !motion_target_pins() && !motion_order_arm() && !motion_order_installed);
        for(unsigned i=0;i<MOTION_ORDER_SITES;i++) memset(image+motion_order_sites[i],0,5);
    }
    /* Target rows: only models whose agent is a watched ship's last read agent; transform and reference as passed. */
    {
        motion_target_row_t *saved_ring=motion_target_ring;LONG64 saved_rows=motion_target_rows;static motion_target_row_t ring[4];
        memset(ring,0,sizeof(ring));motion_target_ring=ring;motion_target_rows=0;
        motion_watch_saved[0]=0xa214c;motion_watch_agent[0]=test_mw_agent;motion_watch_count=1;
        static __declspec(align(16)) unsigned char model[0x80],other[0x80],transform[0x40];
        memset(model,0,sizeof(model));memset(other,0,sizeof(other));
        vt=image+0x2c4f610;memcpy(model,&vt,8);memcpy(other,&vt,8);p=test_mw_agent;memcpy(model+0x10,&p,8);p=test_mw_ctrl;memcpy(other+0x10,&p,8);
        for(unsigned i=0;i<0x40;i++) transform[i]=(unsigned char)(0x50+i);
        motion_target_note(other,transform,0x5ccca,NULL);assert(!motion_target_rows);
        motion_target_note(model,transform,0x5ccca,(void*)transform);
        assert(motion_target_rows==1 && ring[0].ready==1 && ring[0].saved==0xa214c && ring[0].model_vt==0x2c4f610u && ring[0].flag==1);
        assert(ring[0].transform[0]==0x50 && ring[0].transform[0x3f]==0x8f && ring[0].ref.key==0x123456 && ring[0].ref.depth==2);
        motion_target_note(model,NULL,0,NULL);assert(motion_target_rows==2 && ring[1].flag==0x80000000u && !ring[1].ref.type);
        char row[512];assert(motion_target_row(0,row,sizeof(row))>0 && strstr(row,"index=0 tick=") &&
            strstr(row," saved=00000000000a214c model=02c4f610 flag=1 chain=") &&
            strstr(row," ref=0000000000123456/0/2/2b6e910 transform=505152") && strstr(row,"8d8e8f"));
        assert(!motion_target_row(2,row,sizeof(row)));
        ring[1].ready=0;assert(!motion_target_row(1,row,sizeof(row)));
        motion_target_ring=saved_ring;motion_target_rows=saved_rows;
        assert(motion_target_owned()); /* nothing swapped */
    }
    /* Event rows: only events whose target is a watched ship (runtime compare); time, the thread's game time, +0x18. */
    {
        motion_event_row_t *saved_ring=motion_event_ring;LONG64 saved_rows=motion_event_rows;static motion_event_row_t ring[4];
        memset(ring,0,sizeof(ring));motion_event_ring=ring;motion_event_rows=0;
        motion_watch_saved[0]=0xa214c;motion_watch_runtime[0]=0x5b065;motion_watch_count=1;
        static __declspec(align(16)) unsigned char ship[0x40],other[0x40],event[0x40];memset(ship,0,sizeof(ship));memset(other,0,sizeof(other));
        uint64_t id=0x5b065,other_id=0x9999,data=0x1122334455667788ULL;memcpy(ship+8,&id,8);memcpy(other+8,&other_id,8);memcpy(event+0x18,&data,8);
        motion_event_note(1,event,other,12.5);assert(!motion_event_rows);
        motion_event_note(1,event,ship,12.5);
        double t=12.5;uint64_t bits;memcpy(&bits,&t,8);
        assert(motion_event_rows==1 && ring[0].ready==1 && ring[0].saved==0xa214c && ring[0].kind==1 && ring[0].time==bits && ring[0].data==0x88);
        char row[512];assert(motion_event_row(0,row,sizeof(row))>0 && strstr(row," saved=00000000000a214c event=stop_travel time=4029000000000000 now=") &&
            strstr(row," data=0000000000000088 ui=") && strstr(row," chain="));
        /* +0x18 is the charge end time for charge-started; the other events end before it (no data). */
        motion_event_note(2,event,ship,12.5);motion_event_note(3,event,ship,12.5);
        assert(motion_event_rows==3 && ring[1].data==data && !ring[2].data);
        ring[0].ready=0;assert(!motion_event_row(0,row,sizeof(row)));assert(!motion_event_row(3,row,sizeof(row)));
        motion_event_ring=saved_ring;motion_event_rows=saved_rows;motion_watch_runtime[0]=0;
        assert(!motion_event_pins() && motion_event_owned()); /* zero image; nothing swapped */
    }
    /* Refresh rows: the controller's ship decides; rows print the call site, the slot and the fields. */
    {
        motion_watch_saved[0]=0xa214c;motion_watch_runtime[0]=0x5b065;motion_watch_count=1;
        static __declspec(align(16)) unsigned char nav[0x80],ship[0x40];memset(nav,0,sizeof(nav));memset(ship,0,sizeof(ship));
        uint64_t id=0x5b065;memcpy(ship+8,&id,8);void *q=ship;memcpy(nav+0x28,&q,8);
        assert(motion_refresh_watched(nav)==0);id=0x9999;memcpy(ship+8,&id,8);assert(motion_refresh_watched(nav)<0);assert(motion_refresh_watched(NULL)<0);
        motion_refresh_row_t *saved_ring=motion_refresh_ring;LONG64 saved_rows=motion_refresh_rows;static motion_refresh_row_t ring[2];
        memset(ring,0,sizeof(ring));motion_refresh_ring=ring;motion_refresh_rows=1;
        ring[0].tick=574;ring[0].saved=0x78855;ring[0].site=0xea1d9a;ring[0].flag=0;ring[0].slot=1;ring[0].ui=1;ring[0].after[6]=1;ring[0].ready=1;
        char row[512];assert(motion_refresh_row(0,row,sizeof(row))>0 &&
            strstr(row,"index=0 tick=574 saved=0000000000078855 site=00ea1d9a flag=0 slot=written ui=1 chain=") &&
            strstr(row," before=00000000000000000000000000000000 after=00000000000001000000000000000000"));
        ring[0].ready=0;assert(!motion_refresh_row(0,row,sizeof(row)));assert(!motion_refresh_row(1,row,sizeof(row)));
        motion_refresh_ring=saved_ring;motion_refresh_rows=saved_rows;motion_watch_runtime[0]=0;
        for(unsigned i=0;i<MOTION_REFRESH_SITES;i++) assert(motion_refresh_state(i)<0); /* zero image */
        assert(!motion_refresh_pins());
    }
    motion_watch_count=0;motion_watch_saved[0]=0;motion_watch_agent[0]=NULL;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    motion_watch_lookup=prior_lookup;
    printf("motion watch layout/reference/path/row/ring/order/target/refusal tests PASS\n");
}
static void test_motion_watch_native_image(const char *path) {
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
    /* The pinned lookup, the NavigationController vtable (slot 33 is its upkeep 0xea77f0) and the flight model slot. */
    assert(hash_ok(image+0xced70,184,"3209f7be0616de6d0ecc20ce3cfd82fd16d0524c09635d86995210598b0a5f36"));
    assert(*(uint64_t*)(image+MOTION_WATCH_NAV_VTABLE+33*8)==0x140000000ULL+0xea77f0u);
    /* The four AI move point queue calls and their pins. */
    for(unsigned i=0;i<MOTION_ORDER_SITES;i++) assert(motion_order_state(i)==0);
    assert(motion_order_pins());
    image[0x6e0900]^=1;assert(!motion_order_pins());image[0x6e0900]^=1;
    /* Spline, Linear, Forced and MassTraffic keep the base SetTarget in slot 3 (unrelocated file image: absolute VAs). */
    for(unsigned i=0;i<MOTION_TARGET_ENTRIES;i++) assert(*(uint64_t*)(image+motion_target_entries[i])==0x140000000ULL+MOTION_TARGET_SET);
    assert(*(uint64_t*)(image+0x2c4e9c8+0x18)!=0x140000000ULL+MOTION_TARGET_SET); /* Steering overrides it */
    assert(motion_target_pins());
    image[MOTION_TARGET_SET+0x25]^=1;assert(!motion_target_pins());image[MOTION_TARGET_SET+0x25]^=1;
    /* The boost and travel mode events: Dispatch in vtable slot 2, pinned. */
    for(unsigned i=0;i<MOTION_EVENT_KINDS;i++) assert(*(uint64_t*)(image+motion_event_kinds[i].vtable+0x10)==0x140000000ULL+motion_event_kinds[i].dispatch);
    assert(motion_event_pins());
    image[motion_event_kinds[2].dispatch+4]^=1;assert(!motion_event_pins());image[motion_event_kinds[2].dispatch+4]^=1;
    /* The traced calls of the flight parameter refresh 0xea1e80, pinned; none inside another module's pinned code. */
    for(unsigned i=0;i<MOTION_REFRESH_SITES;i++) assert(motion_refresh_state(i)==0);
    assert(motion_refresh_pins());
    image[MOTION_REFRESH_FN+0x12e]^=1;assert(!motion_refresh_pins());image[MOTION_REFRESH_FN+0x12e]^=1;
    for(unsigned i=0;i<MOTION_REFRESH_SITES;i++) {
        unsigned at=motion_refresh_sites[i];
        assert(!(at+5>NAV_CADENCE_PIN_A && at<NAV_CADENCE_PIN_A+NAV_CADENCE_PIN_A_SIZE) && !(at+5>NAV_CADENCE_PIN_B && at<NAV_CADENCE_PIN_B+NAV_CADENCE_PIN_B_SIZE));
        for(unsigned p=0;p<sizeof(world_nav_pins)/sizeof(*world_nav_pins);p++)
            assert(!(at+5>world_nav_pins[p].rva && at<world_nav_pins[p].rva+world_nav_pins[p].length));
        assert(!(at+5>0xea8cf0u && at<0xea8cf0u+1332u)); /* creation trace contract and registry origin identity */
    }
    /* The refresh's writes: +0x191 boost, +0x192 the engine's travel byte +0x340, +0x193, +0x194, +0x198. */
    assert(!memcmp(image+0xea1f83,"\x41\x88\x87\x91\x01\x00\x00",7) && !memcmp(image+0xea1fa3,"\x0f\xb6\x88\x40\x03\x00\x00",7) &&
           !memcmp(image+0xea1fae,"\x41\x88\x8f\x92\x01\x00\x00",7));
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("motion watch native lookup pin, NavigationController vtable, order sites and SetTarget entries verified");
}
