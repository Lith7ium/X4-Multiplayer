/* native_gate_physics_trace: step digests over synthetic Jolt bodies of two
 * clients whose runtime IDs, addresses and step-list orders differ (equal
 * digests), BodyID / input / output / contact differences each in its own
 * digest, contact set versus order, foreign and stray callbacks, rows and
 * their created-key resolution, call-site and vtable swaps, the pinned build.
 * Owned memory only; no X4 code runs. */
#define TEST_PT_BODIES 5u
typedef struct {
    __attribute__((aligned(16))) unsigned char body[0x80];
    __attribute__((aligned(16))) unsigned char motion[0x80];
    unsigned char record[0x40],component[0x280];
} test_pt_body;
typedef struct {
    __attribute__((aligned(16))) unsigned char system[0x700];
    test_pt_body bodies[TEST_PT_BODIES];
    unsigned char *slots[16];uint32_t list[16];
} test_pt_client;
static unsigned test_pt_listener_calls,test_pt_listener_sensor;
static void *test_pt_listener_args[5];
static void test_pt_listener(void *self,void *a,void *b,void *manifold,void *settings) {
    ++test_pt_listener_calls;void *args[5]={self,a,b,manifold,settings};memcpy(test_pt_listener_args,args,sizeof(args));
    if(test_pt_listener_sensor) ((unsigned char*)settings)[0x18]=1;
}
static void test_pt_body_init(test_pt_body *b,uint32_t id,uint64_t runtime,unsigned motion,float x,float vx) {
    memset(b,0,sizeof(*b));float pos[3]={x,2.0f,3.0f},rot[4]={0,0,0,1.0f},vel[3]={vx,0.5f,0},ang[3]={0,0.25f,0};
    memcpy(b->body,pos,12);memcpy(b->body+0x10,rot,16);
    unsigned char *mp=motion?b->motion:NULL,*record=b->record,*component=b->component;memcpy(b->body+0x48,&mp,8);
    memcpy(b->motion,vel,12);memcpy(b->motion+0x10,ang,12);uint32_t active=id&0xff;memcpy(b->motion+0x70,&active,4);
    memcpy(b->body+0x50,&record,8);memcpy(b->record,&component,8);memcpy(b->component+8,&runtime,8);memcpy(b->component+0x260,&record,8);
    memcpy(b->body+0x70,&id,4);uint16_t layer=1;memcpy(b->body+0x74,&layer,2);b->body[0x78]=(unsigned char)motion;b->body[0x79]=0x04;
}
/* Bodies 0,1 saved (0x30, 0x31), 2 created, 3 local, 4 static saved (0x32);
 * slots and list order are the client's own. */
static void test_pt_client_init(test_pt_client *c,const uint64_t runtimes[TEST_PT_BODIES],const uint32_t ids[TEST_PT_BODIES],const unsigned order[TEST_PT_BODIES]) {
    memset(c,0,sizeof(*c));
    for(unsigned i=0;i<TEST_PT_BODIES;i++) {
        test_pt_body_init(&c->bodies[i],ids[i],runtimes[i],i==4?0:2,10.0f*(float)i,1.0f+(float)i);
        c->slots[ids[i]&0x7fffff]=c->bodies[i].body;
    }
    c->slots[15]=(unsigned char*)(uintptr_t)0x1f; /* a freed slot */
    for(unsigned i=0;i<TEST_PT_BODIES;i++) c->list[i]=ids[order[i]];
    c->list[TEST_PT_BODIES]=0x0100000f; /* the freed slot: missing */
    uint64_t slots=16,count=TEST_PT_BODIES+1,head=0x1f;uint32_t num=TEST_PT_BODIES;unsigned char **bodies=c->slots;uint32_t *list=c->list;
    memcpy(c->system+0x10,&slots,8);memcpy(c->system+0x20,&bodies,8);memcpy(c->system+0x28,&num,4);memcpy(c->system+0x30,&head,8);
    c->system[0x2ac]=1;memcpy(c->system+0x680,&count,8);memcpy(c->system+0x690,&list,8);
}
static void test_pt_field(const char *row,const char *name,char *out,size_t cap) {
    char key[40];snprintf(key,sizeof(key)," %s=",name);const char *p=strstr(row,key);assert(p);p+=strlen(key);
    size_t n=strcspn(p," ");assert(n<cap);memcpy(out,p,n);out[n]=0;
}
/* A decorated root over a compound of three children (Jolt shape: +0x18 type,
 * +0x19 sub-type, decorated inner +0x20, compound count +0x50, sub-shapes +0x60). */
typedef struct {
    __attribute__((aligned(16))) unsigned char root[0x28],compound[0x80],children[3][0x30];unsigned char subs[3*0x28];
} test_pt_shape;
static void test_pt_shape_init(test_pt_shape *t,const unsigned order[3]) {
    memset(t,0,sizeof(*t));unsigned char *vt=image+0x2a2acf0,*inner=t->compound,*subs=t->subs;uint64_t count=3;
    memcpy(t->root,&vt,8);t->root[0x18]=2;memcpy(t->root+0x20,&inner,8);
    memcpy(t->compound,&vt,8);t->compound[0x18]=1;memcpy(t->compound+0x50,&count,8);memcpy(t->compound+0x60,&subs,8);
    for(unsigned i=0;i<3;i++) {
        unsigned char *child=t->children[order[i]],*cvt=image+0x2a2acf0+0x10*order[i];float x=(float)order[i];
        memcpy(child,&cvt,8);child[0x18]=0;child[0x19]=0x10;memcpy(subs+i*0x28,&child,8);memcpy(subs+i*0x28+8,&x,4);
        uint64_t part=order[i]==1?0x101:0;memcpy(child+0x28,&part,8); /* child 1 stands for saved part 0x101 (key 0x31) */
    }
}
static int test_pt_same(const char *a,const char *b,const char *name) {
    char x[80],y[80];test_pt_field(a,name,x,sizeof(x));test_pt_field(b,name,y,sizeof(y));return !strcmp(x,y);
}
static void test_pt_manifold(unsigned char *m,float depth) {
    memset(m,0,0x850);float base[3]={5,6,7},normal[3]={0,1,0},p[3]={0.5f,0.25f,0};uint32_t count=2,sub1=0x11,sub2=0x22;
    memcpy(m,base,12);memcpy(m+0x10,normal,12);memcpy(m+0x20,&depth,4);memcpy(m+0x24,&sub1,4);memcpy(m+0x28,&sub2,4);
    memcpy(m+0x30,&count,4);memcpy(m+0x440,&count,4);
    for(unsigned i=0;i<2;i++) {memcpy(m+0x40+i*16,p,12);memcpy(m+0x450+i*16,p,12);}
}
/* One step of a client: begin, the given contacts (body index pairs), end; the step row. */
static unsigned test_pt_padding;
static void test_pt_step(test_pt_client *c,const unsigned (*pairs)[2],unsigned pairs_count,float vx_after,char *row,size_t cap) {
    static unsigned char manifold[0x850],settings[0x40];
    LONG64 before=ptrace_step_rows;ptrace_tick_steps=0;
    assert(ptrace_begin(c->system));
    float vel[3]={vx_after,0.5f,0};memcpy(c->bodies[2].motion,vel,12); /* the created body moves in the step */
    for(unsigned i=0;i<pairs_count;i++) {
        /* Each pair its own depth; the padding after mIsSensor never repeats. */
        test_pt_manifold(manifold,-0.01f*(float)(pairs[i][0]+1));memset(settings,0,sizeof(settings));settings[0x19]=(unsigned char)++test_pt_padding;
        ptrace_contact(test_pt_listener,1,(void*)0x77,c->bodies[pairs[i][0]].body,c->bodies[pairs[i][1]].body,manifold,settings);
    }
    ptrace_end(0);
    assert(ptrace_step_rows==before+1 && ptrace_row(PTRACE_ROW_STEP,(uint64_t)before,0,row,cap)>0);
}
static void test_physics_trace(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    int saved_ids=world_identity_installed;
    test_pc_begin();assert(created_allocate(0));world_portable_created=1;
    stats.active=1;boot_phase=3;ui_thread=GetCurrentThreadId();stats.issued=20;
    ptrace_list=VirtualAlloc(NULL,sizeof(*ptrace_list)*PTRACE_BODIES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    ptrace_ids=VirtualAlloc(NULL,sizeof(*ptrace_ids)*PTRACE_BODIES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    ptrace_step_ring=VirtualAlloc(NULL,sizeof(*ptrace_step_ring)*PTRACE_STEP_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    ptrace_body_ring=VirtualAlloc(NULL,sizeof(*ptrace_body_ring)*PTRACE_BODY_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    ptrace_contact_ring=VirtualAlloc(NULL,sizeof(*ptrace_contact_ring)*PTRACE_CONTACT_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(ptrace_list && ptrace_ids && ptrace_step_ring && ptrace_body_ring && ptrace_contact_ring);
    ptrace_installed=1;ptrace_row_ticks=30;ptrace_failures=ptrace_foreign=0;ptrace_step_rows=ptrace_body_rows=ptrace_contact_rows=0;ptrace_stray=0;
    static test_pt_client a,b;char ra[1024],rb[1024],text[1024];
    const unsigned contacts[2][2]={{0,2},{1,3}},reversed[2][2]={{1,3},{0,2}};
    /* Client A. */
    const uint64_t runtimes_a[TEST_PT_BODIES]={0x100,0x101,0x200,0x300,0x102};
    const uint32_t ids_a[TEST_PT_BODIES]={0x01000001,0x01000002,0x01000003,0x01000004,0x01000005};
    const unsigned order_a[TEST_PT_BODIES]={0,1,2,3,4};
    assert(world_identity_record(0x100,0x30) && world_identity_record(0x101,0x31) && world_identity_record(0x102,0x32));
    test_pc_create(0x200,TEST_PC_CREATED_A);
    test_pt_client_init(&a,runtimes_a,ids_a,order_a);
    test_pt_listener_calls=0;test_pt_step(&a,contacts,2,9.0f,ra,sizeof(ra));
    assert(strstr(ra," tick=20 step=1 scene=3 result=0 det=1 bodies=5 keyed=4 created=1 local=1 missing=1 "));
    assert(strstr(ra," contacts=2 ckeyed=2 ccreated=1 clocal=0 cforeign=0 "));
    assert(test_pt_listener_calls==2 && test_pt_listener_args[0]==(void*)0x77 && test_pt_listener_args[1]==a.bodies[1].body);
    { char pre[40],post[40];test_pt_field(ra,"cpre",pre,sizeof(pre));test_pt_field(ra,"cpost",post,sizeof(post));assert(strcmp(pre,post)); }
    /* Client B: other runtimes, addresses, slots and list order, same keys and states. */
    test_created_reset();test_pc_create(0xa00,TEST_PC_CREATED_A);
    const uint64_t runtimes_b[TEST_PT_BODIES]={0x900,0x901,0xa00,0xb00,0x902};
    const unsigned order_b[TEST_PT_BODIES]={4,2,0,3,1};
    assert(world_identity_record(0x900,0x30) && world_identity_record(0x901,0x31) && world_identity_record(0x902,0x32));
    test_pt_client_init(&b,runtimes_b,ids_a,order_b);
    test_pt_step(&b,contacts,2,9.0f,rb,sizeof(rb));
    static const char *const digests[]={"manager","ids","active","pre","post","cids","cpre","cpost","cset","corder","ccset"};
    for(unsigned i=0;i<sizeof(digests)/sizeof(*digests);i++) assert(test_pt_same(ra,rb,digests[i]));
    /* The same callbacks in another order: the set agrees, the order does not. */
    test_pt_step(&b,reversed,2,9.0f,rb,sizeof(rb));
    assert(test_pt_same(ra,rb,"cset") && test_pt_same(ra,rb,"ccset") && !test_pt_same(ra,rb,"corder") && test_pt_same(ra,rb,"post"));
    /* Other BodyIDs for the same bodies: ids part, inputs and outputs agree. */
    const uint32_t ids_b[TEST_PT_BODIES]={0x02000003,0x01000002,0x01000001,0x01000004,0x01000005};
    test_pt_client_init(&b,runtimes_b,ids_b,order_b);
    test_pt_step(&b,contacts,2,9.0f,rb,sizeof(rb));
    assert(!test_pt_same(ra,rb,"ids") && !test_pt_same(ra,rb,"cids") && test_pt_same(ra,rb,"pre") && test_pt_same(ra,rb,"cpre") && test_pt_same(ra,rb,"post"));
    /* Another output velocity: only the outputs part; another input: pre. */
    test_pt_client_init(&b,runtimes_b,ids_a,order_b);
    test_pt_step(&b,contacts,2,9.5f,rb,sizeof(rb));
    assert(test_pt_same(ra,rb,"ids") && test_pt_same(ra,rb,"pre") && !test_pt_same(ra,rb,"post") && !test_pt_same(ra,rb,"cpost") && test_pt_same(ra,rb,"pre"));
    test_pt_client_init(&b,runtimes_b,ids_a,order_b);float moved=1.5f;memcpy(b.bodies[0].motion,&moved,4);
    test_pt_step(&b,contacts,2,9.0f,rb,sizeof(rb));
    assert(!test_pt_same(ra,rb,"pre") && test_pt_same(ra,rb,"cpre") && test_pt_same(ra,rb,"ids"));
    /* A callback that turns the contact into a sensor parts the contact digests. */
    test_pt_client_init(&b,runtimes_b,ids_a,order_b);test_pt_listener_sensor=1;
    test_pt_step(&b,contacts,2,9.0f,rb,sizeof(rb));test_pt_listener_sensor=0;
    assert(!test_pt_same(ra,rb,"cset") && strstr(rb," sensors=2 ") && test_pt_same(ra,rb,"pre"));
    /* The created body's compound with the same children in another order:
     * only the shape digests part (a SubShapeID difference). */
    static test_pt_shape sa,sb;const unsigned built[3]={0,1,2},appended[3]={1,2,0};
    test_pt_shape_init(&sa,built);test_pt_shape_init(&sb,appended);unsigned char *root=sa.root;
    test_pt_client_init(&a,runtimes_a,ids_a,order_a);memcpy(a.bodies[2].body+0x40,&root,8);
    test_created_reset();test_pc_create(0x200,TEST_PC_CREATED_A);test_pt_step(&a,contacts,2,9.0f,ra,sizeof(ra));
    test_created_reset();test_pc_create(0xa00,TEST_PC_CREATED_A);
    test_pt_client_init(&b,runtimes_b,ids_a,order_b);root=sb.root;memcpy(b.bodies[2].body+0x40,&root,8);
    test_pt_step(&b,contacts,2,9.0f,rb,sizeof(rb));
    assert(!test_pt_same(ra,rb,"shape") && !test_pt_same(ra,rb,"cshape") && test_pt_same(ra,rb,"ids") && test_pt_same(ra,rb,"pre") && test_pt_same(ra,rb,"flags"));
    root=sa.root;memcpy(b.bodies[2].body+0x40,&root,8);test_pt_step(&b,contacts,2,9.0f,rb,sizeof(rb));assert(test_pt_same(ra,rb,"cshape"));
    /* PHYSICS_CHILD rows: each child of a keyed body's compound with its class, sub-type, part and pose, for ticks 1..M. */
    {
        ptrace_child_ring=VirtualAlloc(NULL,sizeof(*ptrace_child_ring)*PTRACE_CHILD_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(ptrace_child_ring);
        ptrace_child_ticks=20;ptrace_child_from=1;ptrace_child_rows=0;root=sb.root;memcpy(b.bodies[2].body+0x40,&root,8);
        test_pt_step(&b,contacts,2,9.0f,rb,sizeof(rb));assert(ptrace_child_rows==3);
        char crow[512];assert(ptrace_row(PTRACE_ROW_CHILD,0,1,crow,sizeof(crow))>0);
        {char key[64];snprintf(key,sizeof(key)," key=%016llx klass=1 which=0 child=0/3 ",(unsigned long long)TEST_PC_CREATED_A);assert(strstr(crow,key));}
        assert(strstr(crow," class=02a2ad00 type=00/10 readable=1 part=0:0000000000000031 part_runtime=101 pose=3f800000"));
        assert(ptrace_row(PTRACE_ROW_CHILD,1,1,crow,sizeof(crow))>0 && strstr(crow," child=1/3 class=02a2ad10 ") && strstr(crow," part_runtime=0 pose=40000000"));
        assert(!ptrace_row(PTRACE_ROW_CHILD,3,1,crow,sizeof(crow)));
        ptrace_child_ticks=19;LONG64 before=ptrace_child_rows;test_pt_step(&b,contacts,2,9.0f,rb,sizeof(rb));assert(ptrace_child_rows==before); /* past M */
        char st[512];assert(ptrace_status(st,sizeof(st)) && strstr(st," child_ticks=19 child_from=1 child_rows=3 saved_rows=0"));
        ptrace_child_ticks=30;ptrace_child_from=21;before=ptrace_child_rows;test_pt_step(&b,contacts,2,9.0f,rb,sizeof(rb));assert(ptrace_child_rows==before); /* before A */
        ptrace_child_from=1;
        ptrace_child_ticks=0;ptrace_child_rows=0;VirtualFree(ptrace_child_ring,0,MEM_RELEASE);ptrace_child_ring=NULL;
        root=sa.root;memcpy(b.bodies[2].body+0x40,&root,8);
    }
    /* Another inverse mass on the created body: only the mass digests part. */
    test_pt_client_init(&b,runtimes_b,ids_a,order_b);root=sa.root;memcpy(b.bodies[2].body+0x40,&root,8);
    float inv_mass=0.5f;memcpy(b.bodies[2].motion+0x58,&inv_mass,4);test_pt_step(&b,contacts,2,9.0f,rb,sizeof(rb));
    assert(!test_pt_same(ra,rb,"mass") && !test_pt_same(ra,rb,"cmass") && test_pt_same(ra,rb,"pre") && test_pt_same(ra,rb,"cshape"));
    memset(b.bodies[2].motion+0x58,0,4);
    /* Jolt's transient InvalidateContactCache flag (0x08) parts flags, not ids. */
    b.bodies[0].body[0x79]|=8;test_pt_step(&b,contacts,2,9.0f,rb,sizeof(rb));
    assert(test_pt_same(ra,rb,"ids") && !test_pt_same(ra,rb,"flags"));b.bodies[0].body[0x79]&=~8;
    /* Foreign and stray callbacks run the original, counted. */
    unsigned calls=test_pt_listener_calls;static unsigned char m[0x850],s[0x40];test_pt_manifold(m,0);
    ptrace_cur.in_step=0;ptrace_contact(test_pt_listener,0,NULL,b.bodies[0].body,b.bodies[1].body,m,s);
    assert(ptrace_stray==1 && test_pt_listener_calls==calls+1);
    ui_thread=GetCurrentThreadId()+1;ptrace_contact(test_pt_listener,0,NULL,b.bodies[0].body,b.bodies[1].body,m,s);ui_thread=GetCurrentThreadId();
    assert(ptrace_foreign==1 && test_pt_listener_calls==calls+2 && !ptrace_failures);
    /* Saved body rows: off, only the created body's row; on, every keyed body's (saved 0x30, 0x31, 0x32 and the created one). */
    {
        LONG64 before=ptrace_body_rows;test_pt_client_init(&b,runtimes_b,ids_a,order_b);root=sa.root;memcpy(b.bodies[2].body+0x40,&root,8);
        test_pt_step(&b,contacts,2,9.0f,rb,sizeof(rb));assert(ptrace_body_rows==before+1);
        ptrace_saved_rows=1;before=ptrace_body_rows;test_pt_step(&b,contacts,2,9.0f,rb,sizeof(rb));assert(ptrace_body_rows==before+4);
        char brow[1024];int saved_seen=0;
        for(LONG64 i=before;i<before+4;i++) {assert(ptrace_row(PTRACE_ROW_BODY,(uint64_t)i,1,brow,sizeof(brow)));saved_seen+=strstr(brow," key=0000000000000031 klass=0 ")!=NULL;}
        assert(saved_seen==1);ptrace_saved_rows=0;
    }
    /* Rows: completed ticks only (or final), created keys resolved when printed. */
    LONG64 body_rows=ptrace_body_rows,contact_rows=ptrace_contact_rows;assert(body_rows>=6 && contact_rows>=6);
    assert(!ptrace_row(PTRACE_ROW_BODY,0,0,text,sizeof(text)) && ptrace_row(PTRACE_ROW_BODY,0,1,text,sizeof(text)));
    stats.issued=21;assert(ptrace_row(PTRACE_ROW_BODY,0,0,text,sizeof(text)));
    {char key[80];snprintf(key,sizeof(key)," key=%016llx klass=1 which=0 body=01000003 ",(unsigned long long)TEST_PC_CREATED_A);assert(strstr(text,key));}
    assert(strstr(text," layer=0001 children=0 shape=0000000000000000 mass="));
    assert(ptrace_row(PTRACE_ROW_CONTACT,0,0,text,sizeof(text)) && strstr(text," ord=1 added=1 a=0:0000000000000030/0/01000001/2 b=1:") &&
           strstr(text," sub=00000011/00000022 flip=0 ") && strstr(text," count=2 ") && strstr(text," sensor=0"));
    /* A body created in its own tick (no key yet) is local in the digests; a
     * contact row with it gets its created ID once the boundary assigned it. */
    test_pt_client_init(&b,runtimes_b,ids_a,order_b);uint64_t fresh=0xc00;memcpy(b.bodies[3].component+8,&fresh,8);
    const unsigned fresh_pair[1][2]={{2,3}};stats.issued=22;LONG64 first=ptrace_contact_rows;
    test_pt_step(&b,fresh_pair,1,9.0f,rb,sizeof(rb));assert(strstr(rb," keyed=4 created=1 local=1 ") && ptrace_contact_rows==first+1);
    assert(ptrace_row(PTRACE_ROW_CONTACT,(uint64_t)first,1,text,sizeof(text)) && strstr(text," b=2:0000000000000000/0/01000004/2 "));
    test_pc_create(0xc00,TEST_PC_CREATED_B);
    {char key[48];snprintf(key,sizeof(key)," b=1:%016llx/0/01000004/2 ",(unsigned long long)TEST_PC_CREATED_B);
     assert(ptrace_row(PTRACE_ROW_CONTACT,(uint64_t)first,1,text,sizeof(text)) && strstr(text,key));}
    fresh=0xd00;memcpy(b.bodies[2].component+8,&fresh,8);
    first=ptrace_body_rows;test_pt_step(&b,contacts,2,9.0f,rb,sizeof(rb));
    assert(strstr(rb," keyed=4 created=1 local=1 ") && ptrace_body_rows==first+1); /* 0xc00 (body 3) is created now, 0xd00 local */
    /* No rows after the row ticks; step rows go on. */
    stats.issued=31;first=ptrace_body_rows;test_pt_step(&b,contacts,2,9.0f,rb,sizeof(rb));assert(ptrace_body_rows==first && strstr(rb," tick=31 "));
    /* Evicted rows say so. */
    LONG64 saved_rows=ptrace_step_rows;ptrace_step_rows=PTRACE_STEP_RING+5;
    assert(ptrace_row(PTRACE_ROW_STEP,0,0,text,sizeof(text)) && strstr(text,"evicted=1"));ptrace_step_rows=saved_rows;
    /* A read fault fails the step closed. */
    test_pt_client_init(&b,runtimes_b,ids_a,order_b);{unsigned char *bad=(unsigned char*)(uintptr_t)0x10000;memcpy(b.bodies[1].body+0x48,&bad,8);}
    assert(!ptrace_begin(b.system) && ptrace_failures==1 && !ptrace_check());ptrace_failures=0;
    uint64_t huge=PTRACE_BODIES+1;memcpy(b.system+0x680,&huge,8);assert(!ptrace_begin(b.system) && !strcmp(ptrace_stage,"list_capacity"));ptrace_failures=0;
    char status[512];assert(ptrace_status(status,sizeof(status)) && strstr(status,"installed=1 rows=30 ") && strstr(status," foreign=1 stray=1 "));
    ptrace_installed=0;
    /* Arming refusals on the zero image. */
    stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;
    world_identity_installed=0;assert(!ptrace_arm(30) && !strcmp(ptrace_stage,"needs"));world_identity_installed=1;
    assert(!ptrace_arm(30) && !strcmp(ptrace_stage,"pins") && !ptrace_installed);
    assert(!ptrace_children_arm(5,1) && !ptrace_saved_arm()); /* the physics trace first */
    assert(!ptrace_arm(PTRACE_ROW_TICKS_MAX+1));stats.issued=1;assert(!ptrace_arm(30));stats.issued=0;
    boot_phase=2;assert(!ptrace_arm(30));boot_phase=0;ui_thread=GetCurrentThreadId()+1;assert(!ptrace_arm(30));ui_thread=GetCurrentThreadId();
    /* Call site and vtable slots: swap, ownership, restore (the plain image's original bytes). */
    unsigned char call[5]={0xe8};int32_t rel=(int32_t)(PTRACE_UPDATE-(PTRACE_CALL+5));memcpy(call+1,&rel,4);memcpy(image+PTRACE_CALL,call,5);
    for(unsigned i=0;i<PTRACE_SLOTS;i++) *(void**)(image+ptrace_slots[i])=image+ptrace_targets[i];
    if(!ptrace_stub) ptrace_stub=ptrace_alloc_near(PTRACE_CALL,PTRACE_STUB_SIZE);
    assert(ptrace_stub && ptrace_write_stub() && ptrace_stub_owned() && ptrace_call_state()==0);
    for(unsigned i=0;i<PTRACE_SLOTS;i++) {assert(ptrace_swap(i,image+ptrace_targets[i],ptrace_hook(i)));ptrace_swapped|=1u<<i;}
    assert(ptrace_cas_call(ptrace_rel_to(image+PTRACE_UPDATE),ptrace_rel_to(ptrace_stub)) && ptrace_call_state()==1);
    ptrace_call_done=1;ptrace_installed=1;assert(ptrace_owned());
    *(void**)(image+ptrace_slots[1])=(void*)0x1234;assert(!ptrace_owned());*(void**)(image+ptrace_slots[1])=ptrace_hook(1);assert(ptrace_owned());
    ptrace_restore();assert(!ptrace_installed && ptrace_restored && !ptrace_swapped && !ptrace_call_done && ptrace_call_state()==0);
    for(unsigned i=0;i<PTRACE_SLOTS;i++) {assert(*(void**)(image+ptrace_slots[i])==image+ptrace_targets[i]);*(void**)(image+ptrace_slots[i])=NULL;}
    memset(image+PTRACE_CALL,0,5);ptrace_restored=0;ptrace_stage="idle";
    VirtualFree(ptrace_list,0,MEM_RELEASE);VirtualFree(ptrace_ids,0,MEM_RELEASE);VirtualFree(ptrace_step_ring,0,MEM_RELEASE);
    VirtualFree(ptrace_body_ring,0,MEM_RELEASE);VirtualFree(ptrace_contact_ring,0,MEM_RELEASE);
    ptrace_list=NULL;ptrace_ids=NULL;ptrace_step_ring=NULL;ptrace_body_ring=NULL;ptrace_contact_ring=NULL;
    ptrace_step_rows=ptrace_body_rows=ptrace_contact_rows=0;ptrace_stray=0;ptrace_foreign=0;ptrace_contacts=ptrace_steps=0;
    test_pc_end();world_identity_installed=saved_ids;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("physics trace digests/contacts/rows/swap tests PASS\n");
}
static void test_physics_trace_native_image(const char *path) {
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
    assert(ptrace_pins_ok() && ptrace_call_state()==0);
    /* native_gate_view_scenes: a system of scene C or D has its own number, an unknown one stays 3. */
    {
        static unsigned char scene_c[0x40],scene_d[0x40],system_c[8],system_d[8],other[8];
        unsigned char *pc=system_c,*pd=system_d;memcpy(scene_c+0x20,&pc,8);memcpy(scene_d+0x20,&pd,8);
        vsc_extra[0]=scene_c;vsc_extra[1]=scene_d;
        assert(ptrace_scene_of(system_c)==4 && ptrace_scene_of(system_d)==5 && ptrace_scene_of(other)==3);
        vsc_extra[0]=vsc_extra[1]=NULL;assert(ptrace_scene_of(system_c)==3);
    }
    /* Scene::Step's Update call: system = scene+0x20, job system = system+0x648. */
    assert(!memcmp(image+0xba8bc,"\x49\x8b\x4d\x20\x48\x8d\x91\x48\x06\x00\x00",11));
    /* The listener's slots 2/3 in .rdata are relocated absolute addresses of the wrappers. */
    for(unsigned i=0;i<PTRACE_SLOTS;i++) assert(*(uint64_t*)(image+ptrace_slots[i])==0x140000000ULL+ptrace_targets[i]);
    /* QueueJob forwards only a job whose dependency count (+0x54) is non-zero;
     * Barrier::Wait takes a ready job (CAS 0 -> 0xe0e0e0e0) and runs it itself. */
    assert(!memcmp(image+0xc5a8c,"\x8b\x42\x54\x85\xc0\x74",6) && !memcmp(image+0xc5f13,"\x33\xc0\xf0\x44\x0f\xb1\x77\x54",8));
    assert(!memcmp(image+0xc5def,"\x41\xbe\xe0\xe0\xe0\xe0",6)); /* mov r14d,cExecutingState */
    /* CreateJob queues at dependency count 0 (test r15d,r15d; jne). */
    assert(!memcmp(image+0xc59ed,"\x45\x85\xff\x75",4));
    image[0xc5a91]^=1;assert(!ptrace_pins_ok());image[0xc5a91]^=1;
    image[PTRACE_CALL+1]^=1;assert(ptrace_call_state()<0 && !ptrace_pins_ok());image[PTRACE_CALL+1]^=1;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("physics trace native pins, Update call, listener slots and serial job system verified");
}
