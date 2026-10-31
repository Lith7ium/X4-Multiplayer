/* Hit-claim prototype: blob multiplexing, the claim codec, staging rules,
 * integer geometry, validation, conflict policies and order independence on
 * a fake world. No engine memory is involved. */
#define Q30 (1<<30)
typedef struct {uint64_t key,shooter;int64_t position[3],velocity[3];int alive;} test_hit_projectile;
typedef struct {uint64_t id;int64_t position[3],velocity[3],radius;int32_t rows[9];int alive;} test_hit_target;
typedef struct {
    test_hit_projectile projectiles[8];test_hit_target targets[6];uint64_t ships[3];
    unsigned applied,refuse_apply;uint64_t last_key;int64_t last_point[3];
} test_hit_world_state;
static int test_hit_projectile_fn(void *c,uint64_t key,hit_projectile *out) {
    test_hit_world_state *w=c;
    for(unsigned i=0;i<8;i++) if(w->projectiles[i].alive && w->projectiles[i].key==key) {
        out->shooter=w->projectiles[i].shooter;memcpy(out->position,w->projectiles[i].position,24);memcpy(out->velocity,w->projectiles[i].velocity,24);return 1;
    }
    return 0;
}
static int test_hit_target_fn(void *c,uint64_t id,hit_target *out) {
    test_hit_world_state *w=c;
    for(unsigned i=0;i<6;i++) if(w->targets[i].alive && w->targets[i].id==id) {
        memcpy(out->position,w->targets[i].position,24);memcpy(out->velocity,w->targets[i].velocity,24);
        out->radius=w->targets[i].radius;memcpy(out->rows,w->targets[i].rows,36);return 1;
    }
    return 0;
}
static uint64_t test_hit_slot_ship(void *c,unsigned slot) {test_hit_world_state *w=c;return slot>=1 && slot<=2?w->ships[slot]:0;}
static int test_hit_apply(void *c,const hit_staged *s,const int64_t world_mm[3]) {
    test_hit_world_state *w=c;
    if(w->refuse_apply) return 0;
    for(unsigned i=0;i<8;i++) if(w->projectiles[i].key==s->claim.projectile) w->projectiles[i].alive=0; /* the native hit ends it */
    ++w->applied;w->last_key=s->claim.projectile;memcpy(w->last_point,world_mm,24);return 1;
}
static void test_hit_world(test_hit_world_state *w,hit_world *api) {
    memset(w,0,sizeof(*w));w->ships[1]=0xa1;w->ships[2]=0xb2;
    const struct {uint64_t id;int64_t x;} targets[]={{0xa1,0},{0xb2,100000},{0xc3,200000},{0xd4,300000}};
    for(unsigned i=0;i<4;i++) {
        test_hit_target *t=&w->targets[i];t->id=targets[i].id;t->position[0]=targets[i].x;t->velocity[2]=5000; /* 300 m/s */
        t->radius=20000;t->rows[0]=t->rows[4]=t->rows[8]=Q30;t->alive=1;
    }
    /* key, shooter, near which target (x), alive */
    const struct {uint64_t key,shooter;int64_t x;} shots[]={{0x11,0xd4,0},{0x22,0xa1,200000},{0x33,0xa1,100000},{0x44,0xb2,0},{0x55,0xa1,0}};
    for(unsigned i=0;i<5;i++) {
        test_hit_projectile *p=&w->projectiles[i];p->key=shots[i].key;p->shooter=shots[i].shooter;
        p->position[0]=shots[i].x;p->position[2]=1000;p->velocity[0]=-30000;p->alive=1; /* 1.8 km/s */
    }
    api->context=w;api->projectile=test_hit_projectile_fn;api->target=test_hit_target_fn;api->slot_ship=test_hit_slot_ship;api->apply=test_hit_apply;
}
static hit_claim test_hit_claim(unsigned kind,uint64_t observe,uint64_t projectile,uint64_t target) {
    hit_claim c;memset(&c,0,sizeof(c));c.kind=kind;c.observe16=(uint16_t)observe;c.projectile=projectile;c.target=target;c.point[2]=8;return c;
}
static void test_hit_mux(void) {
    unsigned char pilot[PILOT_RECORD_BYTES],orders[12]={0x50,1,1,0,0,0,9,0,1,1,6,0},hits[1+HIT_CLAIM_BYTES],blob[INPUT_MUX_MAX+8],again[INPUT_MUX_MAX];
    pilot_record r;memset(&r,0,sizeof(r));r.mode=PILOT_MODE_ASSIST;pilot_encode(&r,pilot);
    hit_claim c=test_hit_claim(HIT_INCOMING,99,0x11,0xa1);assert(hit_payload_encode(&c,1,hits,sizeof(hits))==sizeof(hits));
    input_sections in,out;const char *why;
    assert(input_mux_decode(blob,0,&out,&why) && !out.pilot && !out.hits && !out.orders);
    assert(input_mux_decode(pilot,PILOT_RECORD_BYTES,&out,&why) && out.pilot==pilot && !out.hits);
    /* The UI replay's section (lockstep_ui_replay.h): a container alone, after stock, its version checked. */
    {   static const unsigned char ui[]={INPUT_UI_VERSION,100,0,1,1,2,1,0};unsigned char built[32];input_sections with;
        memset(&with,0,sizeof(with));with.ui=ui;with.ui_len=sizeof(ui);
        size_t n=input_mux_encode(&with,built,sizeof(built));
        assert(n==2+2+sizeof(ui) && built[2]==INPUT_KIND_UI && input_mux_decode(built,n,&out,&why) && out.ui_len==sizeof(ui) && !memcmp(out.ui,ui,sizeof(ui)));
        built[4]=2;assert(!input_mux_decode(built,n,&out,&why) && !strcmp(why,"ui_version"));}
    assert(!input_mux_decode(pilot,PILOT_RECORD_BYTES-1,&out,&why) && !strcmp(why,"pilot_length"));
    assert(input_mux_decode(orders,sizeof(orders),&out,&why) && out.orders==orders && out.orders_len==sizeof(orders));
    /* Legacy forms are their own canonical encoding. */
    memset(&in,0,sizeof(in));in.pilot=pilot;in.pilot_len=PILOT_RECORD_BYTES;
    assert(input_mux_encode(&in,blob,sizeof(blob))==PILOT_RECORD_BYTES && !memcmp(blob,pilot,PILOT_RECORD_BYTES));
    /* Container: every combination of pilot, hits, octree report and orders, round trip; kinds ascend. */
    unsigned char octree[INPUT_OCTREE_BYTES];for(unsigned i=0;i<sizeof(octree);i++) octree[i]=(unsigned char)(0x30+i);
    for(unsigned mask=1;mask<16;mask++) {
        memset(&in,0,sizeof(in));
        if(mask&1) {in.pilot=pilot;in.pilot_len=PILOT_RECORD_BYTES;}
        if(mask&2) {in.hits=hits;in.hits_len=sizeof(hits);}
        if(mask&4) {in.orders=orders;in.orders_len=sizeof(orders);}
        if(mask&8) {in.octree=octree;in.octree_len=sizeof(octree);}
        size_t n=input_mux_encode(&in,blob,sizeof(blob));assert(n);
        assert(input_mux_decode(blob,n,&out,&why));
        assert(!!out.pilot==!!(mask&1) && !!out.hits==!!(mask&2) && !!out.orders==!!(mask&4) && !!out.octree==!!(mask&8));
        if(out.hits) assert(out.hits_len==sizeof(hits) && !memcmp(out.hits,hits,sizeof(hits)));
        if(out.octree) assert(out.octree_len==sizeof(octree) && !memcmp(out.octree,octree,sizeof(octree)));
        assert(input_mux_encode(&out,again,sizeof(again))==n && !memcmp(again,blob,n));
        unsigned count=!!(mask&1)+!!(mask&2)+!!(mask&4)+!!(mask&8);
        assert((blob[0]==INPUT_MUX_TAG)==(count>1 || (mask&2) || (mask&8)));
        if(mask==(2|4|8)) assert(blob[2]==INPUT_KIND_HITS && blob[4+sizeof(hits)]==INPUT_KIND_OCTREE && blob[6+sizeof(hits)+sizeof(octree)]==INPUT_KIND_ORDERS);
    }
    /* An octree report of another length, or after the orders, is refused. */
    unsigned char odd[64];size_t m=0;odd[m++]=INPUT_MUX_TAG;odd[m++]=1;odd[m++]=INPUT_KIND_OCTREE;odd[m++]=19;memcpy(odd+m,octree,19);m+=19;
    assert(!input_mux_decode(odd,m,&out,&why) && !strcmp(why,"octree_length"));
    m=2;odd[m++]=INPUT_KIND_ORDERS;odd[m++]=sizeof(orders);memcpy(odd+m,orders,sizeof(orders));m+=sizeof(orders);
    odd[m++]=INPUT_KIND_OCTREE;odd[m++]=sizeof(octree);memcpy(odd+m,octree,sizeof(octree));m+=sizeof(octree);
    assert(!input_mux_decode(odd,m,&out,&why) && !strcmp(why,"order"));
    memset(&in,0,sizeof(in));in.octree=octree;in.octree_len=19;assert(!input_mux_encode(&in,blob,sizeof(blob)));
    /* Strict refusals. */
    memset(&in,0,sizeof(in));in.pilot=pilot;in.pilot_len=PILOT_RECORD_BYTES;in.hits=hits;in.hits_len=sizeof(hits);
    size_t n=input_mux_encode(&in,blob,sizeof(blob));
    unsigned char bad[INPUT_MUX_MAX+8];
    memcpy(bad,blob,n);bad[1]=2;assert(!input_mux_decode(bad,n,&out,&why) && !strcmp(why,"version"));
    memcpy(bad,blob,n);bad[2]=0x02;assert(!input_mux_decode(bad,n,&out,&why) && !strcmp(why,"kind"));
    memcpy(bad,blob,n);bad[n]=0;assert(!input_mux_decode(bad,n+1,&out,&why) && !strcmp(why,"truncated"));
    memcpy(bad,blob,n);bad[n]=0x50;bad[n+1]=0;assert(!input_mux_decode(bad,n+2,&out,&why) && !strcmp(why,"section_length"));
    memcpy(bad,blob,n);bad[3]=29;assert(!input_mux_decode(bad,n,&out,&why)); /* pilot section length */
    /* hits before pilot: order */
    bad[0]=INPUT_MUX_TAG;bad[1]=1;bad[2]=INPUT_KIND_HITS;bad[3]=sizeof(hits);memcpy(bad+4,hits,sizeof(hits));
    bad[4+sizeof(hits)]=INPUT_KIND_PILOT;bad[5+sizeof(hits)]=PILOT_RECORD_BYTES;memcpy(bad+6+sizeof(hits),pilot,PILOT_RECORD_BYTES);
    assert(!input_mux_decode(bad,6+sizeof(hits)+PILOT_RECORD_BYTES,&out,&why) && !strcmp(why,"order"));
    bad[2]=INPUT_KIND_PILOT;bad[3]=PILOT_RECORD_BYTES;memcpy(bad+4,pilot,PILOT_RECORD_BYTES);
    assert(!input_mux_decode(bad,4+PILOT_RECORD_BYTES,&out,&why) && !strcmp(why,"noncanonical")); /* pilot alone has a legacy form */
    bad[0]=0x33;assert(!input_mux_decode(bad,4,&out,&why) && !strcmp(why,"channel"));
    memset(bad,0x50,sizeof(bad));assert(!input_mux_decode(bad,INPUT_MUX_MAX+1,&out,&why) && !strcmp(why,"too_long"));
    /* Encoder refuses what does not fit. */
    unsigned char big[200];memset(big,0x50,sizeof(big));memset(&in,0,sizeof(in));in.pilot=pilot;in.pilot_len=PILOT_RECORD_BYTES;in.orders=big;in.orders_len=sizeof(big);
    in.hits=hits;in.hits_len=sizeof(hits);assert(!input_mux_encode(&in,blob,sizeof(blob)));
}
static void test_hit_codec(void) {
    hit_claim c=test_hit_claim(HIT_OUTGOING,0xbeef,0x0123456789abcdefULL,0x176ad5),d,many[HIT_CLAIMS_MAX+1];unsigned char b[HIT_CLAIM_BYTES],x[HIT_CLAIM_BYTES],payload[1+(HIT_CLAIMS_MAX+1)*HIT_CLAIM_BYTES];
    c.point[0]=-8;c.point[1]=32767;hit_claim_encode(&c,b);
    assert(b[0]==0x12 && !b[1] && b[2]==0xef && b[3]==0xbe && b[4]==0xef && b[11]==0x01 && b[12]==0xd5 && b[20]==0xf8 && b[21]==0xff);
    assert(hit_claim_decode(b,&d) && d.kind==HIT_OUTGOING && d.observe16==0xbeef && d.projectile==c.projectile && d.target==0x176ad5 &&
           d.point[0]==-8 && d.point[1]==32767 && d.point[2]==8);
    memcpy(x,b,sizeof(x));x[0]=0x22;assert(!hit_claim_decode(x,&d));
    memcpy(x,b,sizeof(x));x[0]=0x13;assert(!hit_claim_decode(x,&d));
    memcpy(x,b,sizeof(x));x[1]=1;assert(!hit_claim_decode(x,&d));
    memcpy(x,b,sizeof(x));x[24]=0;x[25]=0x80;assert(!hit_claim_decode(x,&d));
    memcpy(x,b,sizeof(x));memset(x+4,0,8);assert(!hit_claim_decode(x,&d));
    memcpy(x,b,sizeof(x));memset(x+12,0,8);assert(!hit_claim_decode(x,&d));
    for(unsigned i=0;i<=HIT_CLAIMS_MAX;i++) many[i]=test_hit_claim(HIT_INCOMING,7,0x100+i,0xa1);
    unsigned count=0;hit_claim back[HIT_CLAIMS_MAX];
    assert(!hit_payload_encode(many,0,payload,sizeof(payload)) && !hit_payload_encode(many,HIT_CLAIMS_MAX+1,payload,sizeof(payload)));
    size_t n=hit_payload_encode(many,HIT_CLAIMS_MAX,payload,sizeof(payload));
    assert(n==1+HIT_CLAIMS_MAX*HIT_CLAIM_BYTES && hit_payload_decode(payload,(unsigned)n,back,&count) && count==HIT_CLAIMS_MAX && back[7].projectile==0x107);
    assert(!hit_payload_decode(payload,(unsigned)n-1,back,&count));
    hit_claim_encode(&many[0],payload+1+HIT_CLAIM_BYTES);assert(!hit_payload_decode(payload,(unsigned)n,back,&count)); /* duplicate projectile */
    payload[0]=0;assert(!hit_payload_decode(payload,1,back,&count));
}
static void test_hit_stage_and_resolve(void) {
    test_hit_world_state w;hit_world api;hit_outcome out[16];uint64_t receipt=0;
    /* Staging: incoming at admission, outgoing at observe + delay, window, 16-bit wrap. */
    hit_reset();
    hit_claim c[6]={test_hit_claim(HIT_INCOMING,99,0x11,0xa1),test_hit_claim(HIT_OUTGOING,99,0x22,0xc3),test_hit_claim(HIT_OUTGOING,95,0x23,0xc3),
                    test_hit_claim(HIT_INCOMING,91,0x24,0xa1),test_hit_claim(HIT_INCOMING,90,0x25,0xa1),test_hit_claim(HIT_INCOMING,100,0x26,0xa1)};
    assert(hit_stage(1,c,6,100,3,0)==3 && hit_counts[HIT_REFUSED_LATE]==1 && hit_counts[HIT_REFUSED_FAR]==2); /* 90 too old, 100 not yet reported */
    assert(hit_stage_tick[100%HIT_RING]==100 && hit_stage_count[100%HIT_RING]==2 && hit_stage_ring[100%HIT_RING][0].claim.projectile==0x11 &&
           hit_stage_ring[100%HIT_RING][1].observe==91);
    assert(hit_stage_tick[102%HIT_RING]==102 && hit_stage_ring[102%HIT_RING][0].observe==99 && hit_stage_ring[102%HIT_RING][0].apply==102);
    hit_reset();
    hit_claim wrap=test_hit_claim(HIT_INCOMING,0xffff,0x11,0xa1);
    assert(hit_stage(1,&wrap,1,65538,3,0)==1 && hit_stage_ring[65538%HIT_RING][0].observe==65535);
    assert(!hit_stage(0,c,1,100,3,0) && !hit_stage(HIT_SLOTS+1,c,1,100,3,0) && !hit_stage(1,c,1,1,3,0) && !hit_stage(1,c,1,100,0,0));
    /* Protocol 4 lookahead k = 2: blobs sealed with tick r-1-k are admitted at r. */
    hit_reset();
    hit_claim ahead[4]={test_hit_claim(HIT_INCOMING,99,0x31,0xa1),test_hit_claim(HIT_OUTGOING,99,0x32,0xc3),
                        test_hit_claim(HIT_OUTGOING,98,0x33,0xc3),test_hit_claim(HIT_INCOMING,100,0x34,0xa1)};
    assert(hit_stage(1,ahead,4,102,3,2)==2 && hit_counts[HIT_REFUSED_LATE]==1 && hit_counts[HIT_REFUSED_FAR]==1);
    assert(hit_stage_tick[102%HIT_RING]==102 && hit_stage_count[102%HIT_RING]==2 && hit_stage_ring[102%HIT_RING][0].observe==99 &&
           hit_stage_ring[102%HIT_RING][1].apply==102); /* incoming at r, outgoing at 99+3 */
    assert(!hit_stage(1,ahead,1,3,3,2)); /* nothing was sealed before tick 0 */
    /* Integer geometry: local x mapped to parent -z. */
    hit_target t;memset(&t,0,sizeof(t));t.position[0]=5;t.rows[2]=-Q30;t.rows[4]=Q30;t.rows[6]=Q30;
    int64_t world[3];int16_t point[3]={8,0,-16};hit_world_point(&t,point,world);
    assert(world[0]==5-2000 && world[1]==0 && world[2]==-1000);
    assert(hit_within((const int64_t[3]){3,4,0},(const int64_t[3]){0,0,0},5) && !hit_within((const int64_t[3]){3,4,1},(const int64_t[3]){0,0,0},5));
    /* Resolution: valid incoming and outgoing claims, then every refusal. */
    hit_reset();test_hit_world(&w,&api);
    hit_claim one[]={test_hit_claim(HIT_INCOMING,99,0x11,0xa1),test_hit_claim(HIT_OUTGOING,97,0x22,0xc3)};
    assert(hit_stage(1,one,2,100,3,0)==2 && hit_resolve(&api,100,2,3,0,HIT_POLICY_SHOOTER,out,16,&receipt)==2);
    assert(out[0].result==HIT_ACCEPTED && out[1].result==HIT_ACCEPTED && w.applied==2 && out[0].projectile==0x22); /* earlier observe first */
    assert(w.last_key==0x11 && w.last_point[0]==0 && w.last_point[2]==1000 && receipt==out[0].receipt+out[1].receipt && hit_counts[HIT_ACCEPTED]==2);
    hit_claim again=test_hit_claim(HIT_INCOMING,100,0x11,0xa1);
    assert(hit_stage(1,&again,1,101,3,0)==1 && hit_resolve(&api,101,2,3,0,HIT_POLICY_SHOOTER,out,16,&receipt)==1 && out[0].result==HIT_REFUSED_CONSUMED);
    struct {unsigned slot;hit_claim claim;unsigned result;} cases[]={
        {2,test_hit_claim(HIT_INCOMING,101,0x55,0xa1),HIT_REFUSED_OWNER},     /* slot 2 claims a hit on slot 1's ship */
        {1,test_hit_claim(HIT_OUTGOING,99,0x44,0xa1),HIT_REFUSED_OWNER},      /* slot 1 claims slot 2's shot */
        {1,test_hit_claim(HIT_INCOMING,101,0x55,0xa1),HIT_REFUSED_SELF},      /* its own shot hitting itself */
        {1,test_hit_claim(HIT_INCOMING,101,0x99,0xa1),HIT_REFUSED_PROJECTILE},
        {1,test_hit_claim(HIT_OUTGOING,99,0x33,0xe5),HIT_REFUSED_TARGET}};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++) {
        assert(hit_stage(cases[i].slot,&cases[i].claim,1,102,3,0)==1 && hit_resolve(&api,102,2,3,0,HIT_POLICY_SHOOTER,out,16,&receipt)==1);
        assert(out[0].result==cases[i].result);
    }
    hit_claim outside=test_hit_claim(HIT_OUTGOING,99,0x33,0xb2);outside.point[2]=(int16_t)((20000+HIT_POINT_MARGIN_MM)/HIT_POINT_UNIT_MM+1);
    assert(hit_stage(1,&outside,1,102,3,0)==1 && hit_resolve(&api,102,2,3,0,HIT_POLICY_SHOOTER,out,16,&receipt)==1 && out[0].result==HIT_REFUSED_POINT);
    w.projectiles[2].position[1]=(30000+5000)*5+HIT_TOLERANCE_MM+1; /* beyond what 5 ticks of both speeds allow */
    hit_claim off=test_hit_claim(HIT_OUTGOING,99,0x33,0xb2);
    assert(hit_stage(1,&off,1,102,3,0)==1 && hit_resolve(&api,102,2,3,0,HIT_POLICY_SHOOTER,out,16,&receipt)==1 && out[0].result==HIT_REFUSED_DISTANCE);
    w.projectiles[2].position[1]=0;
    w.refuse_apply=1;
    assert(hit_stage(1,&off,1,102,3,0)==1 && hit_resolve(&api,102,2,3,0,HIT_POLICY_SHOOTER,out,16,&receipt)==1 && out[0].result==HIT_REFUSED_APPLY);
    assert(!hit_was_consumed(0x33));w.refuse_apply=0;
    /* Player versus player (0x33: slot 1 shoots slot 2's ship): both claim for tick 103. */
    hit_claim shooter=test_hit_claim(HIT_OUTGOING,100,0x33,0xb2),victim=test_hit_claim(HIT_INCOMING,102,0x33,0xb2);
    const int policies[3]={HIT_POLICY_SHOOTER,HIT_POLICY_VICTIM,HIT_POLICY_FIRST};
    const unsigned winner[3]={1,2,1},loser_result[3]={HIT_REFUSED_CONSUMED,HIT_REFUSED_CONSUMED,HIT_REFUSED_CONSUMED};
    uint64_t receipts[3][2];
    for(unsigned p=0;p<3;p++) for(unsigned order=0;order<2;order++) {
        hit_reset();test_hit_world(&w,&api);
        if(order) {assert(hit_stage(2,&victim,1,103,3,0)==1 && hit_stage(1,&shooter,1,103,3,0)==1);}
        else {assert(hit_stage(1,&shooter,1,103,3,0)==1 && hit_stage(2,&victim,1,103,3,0)==1);}
        assert(hit_resolve(&api,103,2,3,0,policies[p],out,16,&receipt)==2 && w.applied==1);
        unsigned win=out[0].result==HIT_ACCEPTED?0:1;
        assert(out[win].result==HIT_ACCEPTED && out[win].slot==winner[p] && out[1-win].result==loser_result[p]);
        receipts[p][order]=receipt;
    }
    for(unsigned p=0;p<3;p++) assert(receipts[p][0]==receipts[p][1]); /* arrival order does not matter */
    assert(receipts[0][0]!=receipts[1][0]);
    /* The side a policy does not let decide is refused even when it claims alone. */
    for(unsigned p=0;p<2;p++) {
        hit_reset();test_hit_world(&w,&api);const hit_claim *alone=p?&shooter:&victim;
        assert(hit_stage(p?1:2,alone,1,103,3,0)==1 && hit_resolve(&api,103,2,3,0,policies[p],out,16,&receipt)==1);
        assert(out[0].result==HIT_REFUSED_PVP && !w.applied);
    }
    hit_reset();
}
static void test_hit_claims(void) {
    test_hit_mux();test_hit_codec();test_hit_stage_and_resolve();
    assert(hit_projectile_key(10,0xa1,0,0)!=hit_projectile_key(10,0xa1,0,1) && (hit_projectile_key(1,2,3,4)&1));
    printf("hit claims mux/codec/staging/geometry/validation/policies/order-independence tests PASS\n");
}
