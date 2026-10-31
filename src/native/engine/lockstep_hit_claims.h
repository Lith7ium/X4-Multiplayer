/* Design prototype (no hook, no engine access, not armed by any option yet):
 * hit claims as lockstep inputs (reports/LOCKSTEP-PREDICTION-20261029.txt,
 * part B). The owning client's ahead instance decides every hit that involves
 * its own predicted slot ship, incoming (a projectile hits it) and outgoing
 * (its projectile hits something). It proposes those decisions as claims in
 * its per-tick input blob; every client admits the same blobs in the same tick
 * and applies the same claims at the same apply tick, after the same
 * validation, in the same order. Native hit detection for any pair involving a
 * predicted slot ship is suppressed on every client (a rule over the agreed
 * slot manifest, so it is identical everywhere); a projectile that no valid
 * claim consumes simply flies on. All other simulation keeps reacting to the
 * delayed, authoritative lockstep state.
 *
 * 1. Input blob multiplexing (one protocol-3 blob per slot and tick, <= 240
 *    bytes). Legacy single-channel blobs stay valid and canonical:
 *      28 bytes starting with 0x01  pilot record v1 alone
 *      starting with 0x50 ('P')     player orders v1 alone
 *    Everything else uses the container:
 *      0 'M' (0x4d) | 1 version 1 | sections: kind u8, length u8, payload
 *    kinds strictly ascending, each at most once: 0x01 pilot record (28 bytes),
 *    0x48 'H' hit claims, 0x4f 'O' nav octree report (20 bytes,
 *    lockstep_nav_octree.h 'agreed'), 0x50 'P' player orders (its own v1
 *    bytes), 0x53 'S' stock piloting record (lockstep_pilot_stock.h, first
 *    byte = its version 3), 0x55 'U' UI replay events (lockstep_ui_replay.h, first
 *    byte = its version 1). Unknown kinds, empty sections, trailing bytes and a
 *    container holding only a channel that has a legacy form are refused (one
 *    canonical encoding).
 * 2. Hit claim v1, 26 bytes; the hits payload is count u8 (1..8) + claims:
 *      0      version 1 << 4 | kind (1 incoming, 2 outgoing)
 *      1      flags, 0 (reserved)
 *      2..3   low 16 bits of the observe tick: the presentation frame in which
 *             the claimant's ahead instance saw the impact
 *      4..11  projectile key (portable; hit_projectile_key), nonzero
 *      12..19 target portable identity (saved or created ID), nonzero
 *      20..25 impact point in the target's local frame, 3 x int16 in 1/8 m
 *             (-32768 invalid)
 *    Little endian. A projectile key appears at most once per payload.
 * 3. Timing. A blob proposed with report tick r-1-k is admitted at tick r
 *    (k = the protocol 4 lookahead window, 0 for protocol 3). The observe tick
 *    o is reconstructed from its low 16 bits relative to that sealing tick
 *    s = r-1-k and must satisfy s-HIT_WINDOW <= o <= s. Incoming claims apply
 *    at r (the first tick every client can apply them); outgoing claims apply
 *    at o+d, when the claimant's projectile, simulated d ticks behind its own
 *    presentation, is where the claimant saw it hit; o+d < r is late.
 * 4. Resolution at apply tick t (every client, identical data): all staged
 *    claims of t in (rank, observe tick, slot, index) order, where rank puts
 *    the policy's preferred side of a player-vs-player projectile first. Each
 *    claim is validated against the simulated state at t; the first valid
 *    claim consumes its projectile; later claims for it are refused. Results
 *    fold into an order-independent receipt for the declared state.
 * Geometry is integer only (millimetres, Q30 rotation rows) so validation is
 * bit-identical across compilers and platforms. */
#define INPUT_MUX_TAG 0x4du
#define INPUT_MUX_VERSION 1u
#define INPUT_MUX_MAX 240u
#define INPUT_KIND_PILOT 0x01u
#define INPUT_KIND_HITS 0x48u
#define INPUT_KIND_OCTREE 0x4fu
#define INPUT_OCTREE_BYTES 20u
#define INPUT_KIND_ORDERS 0x50u
#define INPUT_KIND_STOCK 0x53u
#define INPUT_STOCK_VERSION 3u
#define INPUT_KIND_UI 0x55u
#define INPUT_UI_VERSION 1u
#define HIT_CLAIM_BYTES 26u
#define HIT_CLAIM_VERSION 1u
#define HIT_CLAIMS_MAX 8u
#define HIT_INCOMING 1u
#define HIT_OUTGOING 2u
#define HIT_WINDOW 8u
#define HIT_RING 64u
#define HIT_SLOTS 8u
#define HIT_STAGED_MAX 64u
#define HIT_CONSUMED_CAP 4096u
#define HIT_POINT_UNIT_MM 125
#define HIT_TOLERANCE_MM 10000      /* 10 m: prediction error plus shape extent */
#define HIT_POINT_MARGIN_MM 5000
#define HIT_DOMAIN 0x484954434c4d3156ULL /* "HITCLM1V" */
#define HIT_PROJECTILE_DOMAIN 0x50524f4a4b455931ULL
enum {HIT_POLICY_SHOOTER=1,HIT_POLICY_VICTIM,HIT_POLICY_FIRST};
enum {HIT_ACCEPTED,HIT_REFUSED_LATE,HIT_REFUSED_FAR,HIT_REFUSED_PROJECTILE,HIT_REFUSED_CONSUMED,HIT_REFUSED_TARGET,
      HIT_REFUSED_OWNER,HIT_REFUSED_SELF,HIT_REFUSED_POINT,HIT_REFUSED_DISTANCE,HIT_REFUSED_PVP,HIT_REFUSED_APPLY,HIT_RESULTS};
static const char *const hit_result_names[HIT_RESULTS]={"applied","late","far","projectile","consumed","target","owner","self",
    "point","distance","pvp_policy","apply"};
typedef struct {const unsigned char *pilot,*hits,*orders,*octree,*stock,*ui;unsigned pilot_len,hits_len,orders_len,octree_len,stock_len,ui_len;} input_sections;
typedef struct {unsigned kind,flags;uint16_t observe16;uint64_t projectile,target;int16_t point[3];} hit_claim;
typedef struct {hit_claim claim;uint64_t observe,apply;unsigned slot,index;} hit_staged;
/* The simulated state a claim is validated against (native adapter later). */
typedef struct {uint64_t shooter;int64_t position[3],velocity[3];} hit_projectile; /* mm and mm per tick */
typedef struct {int64_t position[3],velocity[3],radius;int32_t rows[9];} hit_target; /* rows Q30, local axes in parent */
typedef struct {
    void *context;
    int (*projectile)(void *context,uint64_t key,hit_projectile *out);  /* 1: exists and not yet resolved natively */
    int (*target)(void *context,uint64_t id,hit_target *out);           /* 1: exists and operational */
    uint64_t (*slot_ship)(void *context,unsigned slot);                 /* predicted slot ship identity, 0 none */
    int (*apply)(void *context,const hit_staged *claim,const int64_t world_mm[3]); /* native hit; 1 applied */
} hit_world;
typedef struct {uint64_t projectile,target,receipt;unsigned slot,index,result;} hit_outcome;
static uint64_t hit_consumed[HIT_CONSUMED_CAP];
static unsigned hit_consumed_count;
static hit_staged hit_stage_ring[HIT_RING][HIT_STAGED_MAX];
static uint64_t hit_stage_tick[HIT_RING];
static unsigned hit_stage_count[HIT_RING];
static uint64_t hit_counts[HIT_RESULTS],hit_receipt_total,hit_stage_overflow;

/* ---- blob multiplexing ---- */
static int input_mux_decode(const unsigned char *b,size_t n,input_sections *out,const char **why) {
    memset(out,0,sizeof(*out));*why="ok";
    if(n>INPUT_MUX_MAX) {*why="too_long";return 0;}
    if(!n) return 1;
    if(b[0]==INPUT_KIND_PILOT) {
        if(n!=PILOT_RECORD_BYTES) {*why="pilot_length";return 0;}
        out->pilot=b;out->pilot_len=(unsigned)n;return 1;
    }
    if(b[0]==INPUT_KIND_ORDERS) {out->orders=b;out->orders_len=(unsigned)n;return 1;}
    if(b[0]!=INPUT_MUX_TAG) {*why="channel";return 0;}
    if(n<2 || b[1]!=INPUT_MUX_VERSION) {*why="version";return 0;}
    unsigned last=0,sections=0;size_t at=2;
    while(at<n) {
        if(n-at<2) {*why="truncated";return 0;}
        unsigned kind=b[at],length=b[at+1];at+=2;
        if(kind<=last) {*why="order";return 0;}
        if(!length || length>n-at) {*why="section_length";return 0;}
        if(kind==INPUT_KIND_PILOT) {if(length!=PILOT_RECORD_BYTES || b[at]!=PILOT_RECORD_VERSION) {*why="pilot_length";return 0;}out->pilot=b+at;out->pilot_len=length;}
        else if(kind==INPUT_KIND_HITS) {out->hits=b+at;out->hits_len=length;}
        else if(kind==INPUT_KIND_OCTREE) {if(length!=INPUT_OCTREE_BYTES) {*why="octree_length";return 0;}out->octree=b+at;out->octree_len=length;}
        else if(kind==INPUT_KIND_ORDERS) {if(b[at]!=INPUT_KIND_ORDERS) {*why="orders_channel";return 0;}out->orders=b+at;out->orders_len=length;}
        else if(kind==INPUT_KIND_STOCK) {if(b[at]!=INPUT_STOCK_VERSION) {*why="stock_version";return 0;}out->stock=b+at;out->stock_len=length;}
        else if(kind==INPUT_KIND_UI) {if(b[at]!=INPUT_UI_VERSION) {*why="ui_version";return 0;}out->ui=b+at;out->ui_len=length;}
        else {*why="kind";return 0;}
        last=kind;++sections;at+=length;
    }
    if(!sections || (sections==1 && !out->hits && !out->octree && !out->stock && !out->ui)) {*why="noncanonical";memset(out,0,sizeof(*out));return 0;}
    return 1;
}
static size_t input_mux_encode(const input_sections *in,unsigned char *out,size_t capacity) {
    unsigned sections=!!in->pilot_len+!!in->hits_len+!!in->orders_len+!!in->octree_len+!!in->stock_len+!!in->ui_len;
    if(in->pilot_len && in->pilot_len!=PILOT_RECORD_BYTES) return 0;
    if(in->octree_len && in->octree_len!=INPUT_OCTREE_BYTES) return 0;
    if(in->stock_len && in->stock[0]!=INPUT_STOCK_VERSION) return 0;
    if(in->ui_len && in->ui[0]!=INPUT_UI_VERSION) return 0;
    if(!sections) return 0;
    if(sections==1 && !in->hits_len && !in->octree_len && !in->stock_len && !in->ui_len) {
        const unsigned char *p=in->pilot_len?in->pilot:in->orders;unsigned length=in->pilot_len?in->pilot_len:in->orders_len;
        if(length>capacity || length>INPUT_MUX_MAX) return 0;
        memcpy(out,p,length);return length;
    }
    size_t at=2;
    const unsigned char *parts[6]={in->pilot,in->hits,in->octree,in->orders,in->stock,in->ui};
    const unsigned lengths[6]={in->pilot_len,in->hits_len,in->octree_len,in->orders_len,in->stock_len,in->ui_len},
                   kinds[6]={INPUT_KIND_PILOT,INPUT_KIND_HITS,INPUT_KIND_OCTREE,INPUT_KIND_ORDERS,INPUT_KIND_STOCK,INPUT_KIND_UI};
    if(capacity<2) return 0;
    out[0]=INPUT_MUX_TAG;out[1]=INPUT_MUX_VERSION;
    for(unsigned i=0;i<6;i++) {
        if(!lengths[i]) continue;
        if(lengths[i]>255 || at+2+lengths[i]>capacity || at+2+lengths[i]>INPUT_MUX_MAX) return 0;
        out[at]=(unsigned char)kinds[i];out[at+1]=(unsigned char)lengths[i];memcpy(out+at+2,parts[i],lengths[i]);at+=2+lengths[i];
    }
    return at;
}

/* ---- claims ---- */
static uint64_t hit_projectile_key(uint64_t tick,uint64_t shooter,unsigned weapon,unsigned ordinal) {
    return lockstep_seed(HIT_PROJECTILE_DOMAIN,tick,shooter,weapon,ordinal,0)|1u;
}
static void hit_claim_encode(const hit_claim *c,unsigned char out[HIT_CLAIM_BYTES]) {
    memset(out,0,HIT_CLAIM_BYTES);out[0]=(unsigned char)(HIT_CLAIM_VERSION<<4|c->kind);out[1]=(unsigned char)c->flags;
    out[2]=(unsigned char)c->observe16;out[3]=(unsigned char)(c->observe16>>8);
    for(unsigned i=0;i<8;i++) {out[4+i]=(unsigned char)(c->projectile>>(8*i));out[12+i]=(unsigned char)(c->target>>(8*i));}
    for(unsigned i=0;i<3;i++) {uint16_t v=(uint16_t)c->point[i];out[20+2*i]=(unsigned char)v;out[21+2*i]=(unsigned char)(v>>8);}
}
static int hit_claim_decode(const unsigned char in[HIT_CLAIM_BYTES],hit_claim *c) {
    memset(c,0,sizeof(*c));
    if(in[0]>>4!=HIT_CLAIM_VERSION || ((in[0]&15)!=HIT_INCOMING && (in[0]&15)!=HIT_OUTGOING) || in[1]) return 0;
    c->kind=in[0]&15u;c->observe16=(uint16_t)(in[2]|in[3]<<8);
    for(unsigned i=0;i<8;i++) {c->projectile|=(uint64_t)in[4+i]<<(8*i);c->target|=(uint64_t)in[12+i]<<(8*i);}
    for(unsigned i=0;i<3;i++) {c->point[i]=(int16_t)(uint16_t)(in[20+2*i]|in[21+2*i]<<8);if(c->point[i]==INT16_MIN) return 0;}
    return c->projectile && c->target;
}
static size_t hit_payload_encode(const hit_claim *claims,unsigned count,unsigned char *out,size_t capacity) {
    if(!count || count>HIT_CLAIMS_MAX || capacity<1+(size_t)count*HIT_CLAIM_BYTES) return 0;
    out[0]=(unsigned char)count;
    for(unsigned i=0;i<count;i++) hit_claim_encode(&claims[i],out+1+i*HIT_CLAIM_BYTES);
    return 1+(size_t)count*HIT_CLAIM_BYTES;
}
static int hit_payload_decode(const unsigned char *p,unsigned length,hit_claim *claims,unsigned *count) {
    if(length<1 || !p[0] || p[0]>HIT_CLAIMS_MAX || length!=1+(unsigned)p[0]*HIT_CLAIM_BYTES) return 0;
    for(unsigned i=0;i<p[0];i++) {
        if(!hit_claim_decode(p+1+i*HIT_CLAIM_BYTES,&claims[i])) return 0;
        for(unsigned j=0;j<i;j++) if(claims[j].projectile==claims[i].projectile) return 0;
    }
    *count=p[0];return 1;
}
/* Admission at tick r (the blob was sealed with report r-1-lookahead):
 * reconstruct the observe tick and stage every claim for its apply tick.
 * Returns the number staged; claims that can never apply are counted as
 * late/far. */
static unsigned hit_stage(unsigned slot,const hit_claim *claims,unsigned count,uint64_t r,unsigned delay,unsigned lookahead) {
    unsigned staged=0;
    if(!slot || slot>HIT_SLOTS || r<2+(uint64_t)lookahead || !delay || delay>=HIT_RING/2 || lookahead>=HIT_RING/2) return 0;
    const uint64_t sealed=r-1-lookahead;
    for(unsigned i=0;i<count;i++) {
        const hit_claim *c=&claims[i];uint64_t back=(uint16_t)((uint16_t)sealed-c->observe16);
        if(back>HIT_WINDOW || back>sealed) {++hit_counts[HIT_REFUSED_FAR];continue;}
        uint64_t observe=sealed-back,apply=c->kind==HIT_INCOMING?r:observe+delay;
        if(apply<r) {++hit_counts[HIT_REFUSED_LATE];continue;}
        unsigned at=(unsigned)(apply%HIT_RING);
        if(hit_stage_tick[at]!=apply) {hit_stage_tick[at]=apply;hit_stage_count[at]=0;}
        if(hit_stage_count[at]>=HIT_STAGED_MAX) {++hit_stage_overflow;continue;}
        hit_staged *s=&hit_stage_ring[at][hit_stage_count[at]++];
        s->claim=*c;s->observe=observe;s->apply=apply;s->slot=slot;s->index=i;++staged;
    }
    return staged;
}
static int hit_was_consumed(uint64_t key) {
    for(unsigned i=0;i<hit_consumed_count;i++) if(hit_consumed[i]==key) return 1;
    return 0;
}
/* Consumed projectile keys, oldest half dropped when full. An applied claim
 * also ends the native projectile (it then fails the projectile lookup), so
 * this set only has to cover the same tick and the native removal lag. */
static void hit_consume(uint64_t key) {
    if(hit_consumed_count>=HIT_CONSUMED_CAP) {
        memmove(hit_consumed,hit_consumed+HIT_CONSUMED_CAP/2,sizeof(hit_consumed)/2);hit_consumed_count=HIT_CONSUMED_CAP/2;
    }
    hit_consumed[hit_consumed_count++]=key;
}
/* Target-local point (1/8 m) to world millimetres: position + sum(local * row). */
static void hit_world_point(const hit_target *t,const int16_t point[3],int64_t out[3]) {
    for(unsigned k=0;k<3;k++) {
        int64_t sum=0;
        for(unsigned j=0;j<3;j++) sum+=(int64_t)point[j]*HIT_POINT_UNIT_MM*(int64_t)t->rows[3*j+k];
        out[k]=t->position[k]+(sum>=0?(sum+(1<<29))>>30:-((-sum+(1<<29))>>30));
    }
}
static int64_t hit_norm1(const int64_t v[3]) {
    int64_t n=0;for(unsigned i=0;i<3;i++) n+=v[i]<0?-v[i]:v[i];return n;
}
/* Squared distance bound in integers: |d|^2 <= bound^2 with bound from speeds
 * (L1 norms bound the Euclidean ones from above). Values stay below 2^62. */
static int hit_within(const int64_t a[3],const int64_t b[3],int64_t bound) {
    int64_t sum=0;
    if(bound<0 || bound>(INT64_C(1)<<30)) return 0;
    for(unsigned i=0;i<3;i++) {
        int64_t d=a[i]-b[i];if(d<0) d=-d;
        if(d>bound) return 0;
        sum+=d*d;
    }
    return sum<=bound*bound;
}
static unsigned hit_side(const hit_world *w,uint64_t id,unsigned slots) {
    for(unsigned s=1;s<=slots;s++) if(w->slot_ship(w->context,s)==id) return s;
    return 0;
}
static unsigned hit_validate(const hit_world *w,const hit_staged *s,unsigned slots,unsigned delay,int policy,int64_t world_mm[3]) {
    /* delay is the slack in ticks: the wire delay plus the lookahead window */
    hit_projectile p;hit_target t;const hit_claim *c=&s->claim;
    uint64_t own=w->slot_ship(w->context,s->slot);
    if(!own) return HIT_REFUSED_OWNER;
    if(hit_was_consumed(c->projectile)) return HIT_REFUSED_CONSUMED;
    if(!w->projectile(w->context,c->projectile,&p)) return HIT_REFUSED_PROJECTILE;
    if(!w->target(w->context,c->target,&t)) return HIT_REFUSED_TARGET;
    if(c->kind==HIT_INCOMING ? (c->target!=own) : (p.shooter!=own)) return HIT_REFUSED_OWNER;
    if(p.shooter==c->target) return HIT_REFUSED_SELF;
    /* Player versus player: both owners may claim the same projectile. */
    unsigned shooter_slot=hit_side(w,p.shooter,slots),victim_slot=hit_side(w,c->target,slots);
    if(shooter_slot && victim_slot && ((policy==HIT_POLICY_SHOOTER && c->kind==HIT_INCOMING) ||
       (policy==HIT_POLICY_VICTIM && c->kind==HIT_OUTGOING))) return HIT_REFUSED_PVP;
    int64_t local[3];
    for(unsigned i=0;i<3;i++) local[i]=(int64_t)c->point[i]*HIT_POINT_UNIT_MM;
    if(!hit_within(local,(const int64_t[3]){0,0,0},t.radius+HIT_POINT_MARGIN_MM)) return HIT_REFUSED_POINT;
    hit_world_point(&t,c->point,world_mm);
    /* The simulated projectile at t versus the claimed impact: both sides may
     * have moved up to delay+2 ticks since what the claimant saw. */
    int64_t bound=(hit_norm1(p.velocity)+hit_norm1(t.velocity))*(int64_t)(delay+2)+HIT_TOLERANCE_MM;
    if(!hit_within(p.position,world_mm,bound)) return HIT_REFUSED_DISTANCE;
    return HIT_ACCEPTED;
}
static unsigned hit_rank(const hit_world *w,const hit_staged *s,unsigned slots,int policy) {
    hit_projectile p;
    if(policy==HIT_POLICY_FIRST || !w->projectile(w->context,s->claim.projectile,&p)) return 0;
    if(!hit_side(w,p.shooter,slots) || !hit_side(w,s->claim.target,slots)) return 0;
    return (policy==HIT_POLICY_SHOOTER)==(s->claim.kind==HIT_OUTGOING)?0:1;
}
/* Every client at tick t, after admission: resolve and apply the claims of t.
 * delay = wire delay d, lookahead = protocol 4 window k (both widen the bound:
 * an incoming claim waits 1+k ticks for admission). Returns the number of
 * outcomes written; *receipt gets their fold. */
static unsigned hit_resolve(const hit_world *w,uint64_t t,unsigned slots,unsigned delay,unsigned lookahead,int policy,
                            hit_outcome *out,unsigned capacity,uint64_t *receipt) {
    unsigned at=(unsigned)(t%HIT_RING),n=hit_stage_tick[at]==t?hit_stage_count[at]:0,order[HIT_STAGED_MAX],ranks[HIT_STAGED_MAX];
    *receipt=0;
    for(unsigned i=0;i<n;i++) {order[i]=i;ranks[i]=hit_rank(w,&hit_stage_ring[at][i],slots,policy);}
    for(unsigned i=1;i<n;i++) { /* insertion sort: (rank, observe, slot, index) */
        unsigned v=order[i],j=i;const hit_staged *a=&hit_stage_ring[at][v];
        while(j) {
            const hit_staged *b=&hit_stage_ring[at][order[j-1]];unsigned ra=ranks[v],rb=ranks[order[j-1]];
            int before=ra!=rb?ra<rb:a->observe!=b->observe?a->observe<b->observe:a->slot!=b->slot?a->slot<b->slot:a->index<b->index;
            if(!before) break;
            order[j]=order[j-1];--j;
        }
        order[j]=v;
    }
    unsigned written=0;
    for(unsigned k=0;k<n;k++) {
        const hit_staged *s=&hit_stage_ring[at][order[k]];int64_t world_mm[3]={0,0,0};
        unsigned result=hit_validate(w,s,slots,delay+lookahead,policy,world_mm);
        if(result==HIT_ACCEPTED) {
            if(w->apply && w->apply(w->context,s,world_mm)) hit_consume(s->claim.projectile);
            else result=HIT_REFUSED_APPLY;
        }
        uint64_t mix=lockstep_seed(HIT_DOMAIN,t,s->claim.projectile,s->claim.target,s->slot,result);
        *receipt+=mix;hit_receipt_total+=mix;++hit_counts[result];
        if(written<capacity) {
            hit_outcome *o=&out[written++];o->projectile=s->claim.projectile;o->target=s->claim.target;o->receipt=mix;
            o->slot=s->slot;o->index=s->index;o->result=result;
        }
    }
    hit_stage_count[at]=0;
    return written;
}
static void hit_reset(void) {
    memset(hit_consumed,0,sizeof(hit_consumed));hit_consumed_count=0;memset(hit_stage_tick,0,sizeof(hit_stage_tick));
    memset(hit_stage_count,0,sizeof(hit_stage_count));memset(hit_counts,0,sizeof(hit_counts));hit_receipt_total=0;hit_stage_overflow=0;
}
