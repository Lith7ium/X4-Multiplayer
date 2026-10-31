/* traffic_rows (diagnostic, a group of lockstep_region_camera.h; native_gate_traffic_rows = true): who queues mass
 * traffic ships, as policy rows (native_gate_viewpoint_policy_rows). Changes no behaviour.
 * A traffic object's queue (pending tree +0x290, count +0x2b8) is what Zone::Move's 0x8be450 makes real ships of
 * (0x8bfb40). reg-tour4/5: one ship on client 1 alone at 208, also with traffic_views = 'block', so not (only) the
 * render pass's late queue. The writers: the render pass 0x8aba40 (late 0x91eec0, early inline at 0x8abf46 for a
 * tracked member), Zone::Move's 0x82d680 -> 0x8bd190 (when the zone's +0x520 is set), the switch-on lane builds
 * (0x8aacb0 -> 0x8ad220), U::Player::vfunc10 and 0x71bac0 -> 0x8c4550, the event listener 0x82d040 -> 0x8bce50.
 * Rows (key: the traffic object's station or zone, as the policy rows key traffic objects):
 *   site 10a  the render pass queues a tracked member early (jump at 0x8abf2d): a = its MassTrafficID's low 32 bits
 *   site 10b  the render pass's late queue (0x8ac5af): a = the record's ID low 32 bits
 *   site 10c  a queue grew inside Zone::Move's 0x82d680 (0x834c95): a = before, b = after, pend = the object's ordinal
 *   site 10d  a queue grew since the last Zone::Move, outside it (seen at 0x834ca9): a, b, pend as 10c
 * A network's objects: +0x40, then +0x370 entries at +0x48 (inline when +0x368 is 100). The last count per object
 * is kept in a table by object pointer (local addresses, never logged). Status: traffic_rows=early,late,sim,outside.
 * reg-tour7: client 1 alone, tick 195 two 10a rows (key 432c1), tick 196 10d a=0 b=2, the ship at 196: the render
 * pass's early queue (write 2) of a zone client 1 renders.
 *
 * traffic_pass (group traffic_pass, needs this group; native_gate_traffic_views = 'observe' | 'view'): the render
 * pass's traffic update for every view point, from the simulation, the same on every client. 0x8aba40 writes four
 * things the simulation reads: the late queue (0x91eec0, a member near a player view), the early queue (a tracked
 * member in its lane window, any rendered zone), the placement of real traffic ships on their lane (0x6e59b0, two
 * calls) and their collision-ignore pairs (0x8c0180). The pass, at 0x834ca9 after the native check and the rows:
 * for every view point latched in the zone's sector (slot order), 0x82f1e0(network, zone, view) with view [0] an eye at
 * the anchor's origin
 * (zeros), [1] the view point, +0x90 the camera 0x82f1e0's loads find (a player view). Zone::vfunc693 calls 0x82f1e0
 * for every zone it renders, before its attention limit (8, for the fields): reg-tour8 with that limit here visited
 * the view points' own zones only (zones 313, calls 455 by 180) and never zone 432c1 of the render pass's early
 * queue (no pass early, no placement). 0x82f1e0's own zone test (0x83ef20, routed) keeps the range. Inside that call (tp_thread):
 * 0x3dde10 (three calls; it reads the published view banks) is the simulation's relative transform at the thread's
 * game time in the common cluster, 0xfb36d0 (instances) is skipped. 0x8aba40 also keeps the object's last update
 * time (+0xe8, stored at 0x8acc97): a member whose window began since then runs the behaviour's slot-16 hook (0x8abd76,
 * welding and build effects) and raises its MassTrafficID's times (max, at 0x8abe13). 'observe': the pass writes
 * nothing it can avoid (queues answer "not queued", placement, filters and the hook skipped, +0xe8 put back), the
 * render pass is untouched; the raised times are the same maxima the render pass writes for the zones it renders.
 * Rows 10a/10b carry b=2 for the pass's would-be queue. 'view' (with traffic_views = 'block' routing, so render
 * views are never player views): the pass writes all of it, the hook's effects included, on the simulation thread;
 * the render pass's early queue, placement and filters are skipped, and as the pass has already moved +0xe8 to the
 * frame's time its hook and raised times find nothing new: it only draws. Not covered: a zone a client renders but
 * no pass visits (attention below 8 in the simulation). Queue growth inside the pass: site 10e (as 10c). No RNG is
 * drawn by any of these. */#define TQ_SIM_CALL 44u       /* 0x834c95 in Zone::Move: 0x82d680(network, zone) */
#define TQ_CHECK_CALL 45u     /* 0x834ca9 in Zone::Move: 0x831390(network), rdx = zone (r14) */
#define TQ_LATE_CALL 46u      /* 0x8ac5af in 0x8aba40: 0x91eec0(tree, out, record) */
#define TQ_EARLY_SITE 0x8abf2du
#define TQ_EARLY_TRACKED 0x8abf46u
#define TQ_EARLY_UNTRACKED 0x8abf34u
#define TQ_TABLE 4096u
#define TP_RELATIVE_CALL 47u   /* 47, 48, 49: 0x8abbb4, 0x8ac0da, 0x8ac6a3 in 0x8aba40: 0x3dde10 */
#define TP_PLACE_CALL 50u      /* 50, 51: 0x8ac354, 0x8ac8fc: 0x6e59b0 */
#define TP_FILTER_CALL 52u     /* 0x8ac642: 0x8c0180 */
#define TP_DRAW_CALL 53u       /* 0x8acc2c: 0xfb36d0 */
#define TP_TRAFFIC 0x82f1e0u
#define TP_HOOK_SITE 0x8abd6fu /* movaps xmm2,xmm12; mov rdx,rsi; then call [rax+0x80] at 0x8abd76 */
#define TP_HOOK_CALL 0x8abd76u
#define TP_HOOK_AFTER 0x8abd7cu
enum {TP_OFF,TP_OBSERVE,TP_VIEW};
static int tp_mode;
static volatile DWORD tp_thread; /* the thread inside the pass's own 0x82f1e0 call, else 0 */
static volatile DWORD tp_hook_thread; /* the same in 'observe' only: the hook thunk skips the slot-16 hook for it */
typedef struct {void *slot[0x13];} tp_view_t;
static __declspec(align(16)) unsigned char tp_eyes[VIEW_MAX][0x90];
static tp_view_t tp_views[VIEW_MAX];
static volatile LONG64 tp_zones,tp_calls,tp_late[2],tp_early[2],tp_place[2],tp_place_skipped,tp_filter[2],tp_filter_skipped,
    tp_draw_skipped,tp_relative,tp_grown;
typedef void (*tp_traffic_fn)(void *,void *,void *);
typedef void *(*tp_relative_fn)(void *,float *,void *,unsigned char);
typedef void (*tp_place_fn)(void *,void *,void *,void *);
typedef void *(*tp_filter_fn)(void *,void *,double,double,double,void *);
typedef void (*tp_draw_fn)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
/* tests only: replace the native functions */
static tp_traffic_fn tp_traffic_native;static tp_relative_fn tp_relative_native;static tp_place_fn tp_place_native;
static tp_filter_fn tp_filter_native;static tp_draw_fn tp_draw_native;
static int tp_ours(void) {const DWORD t=tp_thread;return t && t==GetCurrentThreadId();}
/* What a writing call skips: ours in 'observe', the render pass's in 'view'. */
static int tp_skip(int ours) {return ours?tp_mode==TP_OBSERVE:tp_mode==TP_VIEW;}
static void tp_reset(void) {
    tp_mode=TP_OFF;tp_thread=tp_hook_thread=0;tp_zones=tp_calls=tp_place_skipped=tp_filter_skipped=tp_draw_skipped=tp_relative=tp_grown=0;
    for(unsigned k=0;k<2;k++) tp_late[k]=tp_early[k]=tp_place[k]=tp_filter[k]=0;
}
typedef void (*tq_sim_fn)(void *,void *);
typedef char (*tq_check_fn)(void *);
typedef void **(*tq_late_fn)(void *,void **,void *);
typedef char (*tq_member_fn)(void *);
static tq_sim_fn tq_sim_native;static tq_check_fn tq_check_native;static tq_late_fn tq_late_native; /* tests only */
static volatile LONG64 tq_early_count,tq_late_count,tq_sim_count,tq_outside_count;
static struct {void *object;uint32_t count;} tq_table[TQ_TABLE];
static void tq_reset(void) {tq_early_count=tq_late_count=tq_sim_count=tq_outside_count=0;memset(tq_table,0,sizeof(tq_table));}
/* The slot of an object in the table (0 when full). */
static uint32_t *tq_last(void *object) {
    uint64_t h=lockstep_mix((uint64_t)(uintptr_t)object);
    for(unsigned probe=0;probe<TQ_TABLE;probe++) {
        unsigned i=(unsigned)((h+probe)%TQ_TABLE);
        if(tq_table[i].object==object) return &tq_table[i].count;
        if(!tq_table[i].object) {tq_table[i].object=object;tq_table[i].count=0;return &tq_table[i].count;}
    }
    return NULL;
}
/* A network's traffic objects, in its order. */
static unsigned tq_objects(unsigned char *network,unsigned char *out[],unsigned cap) {
    unsigned n=0;unsigned char *first=NULL,**array=NULL;uint64_t capacity=0,count=0;
    if(!network || !read_local(network+0x40,&first,8)) return 0;
    if(first && n<cap) out[n++]=first;
    if(!read_local(network+0x368,&capacity,8) || !read_local(network+0x370,&count,8)) return n;
    if(capacity==100) array=(unsigned char **)(network+0x48);
    else if(!read_local(network+0x48,&array,8) || !array) return n;
    for(uint64_t i=0;i<count && n<cap;i++) {unsigned char *o=NULL;if(read_local(&array[i],&o,8) && o) out[n++]=o;}
    return n;
}
static uint32_t tq_queued(unsigned char *object) {uint64_t c=0;read_local(object+0x2b8,&c,8);return (uint32_t)c;}
/* Rows for every object whose queue grew since its last count; then the counts are current. */
static void tq_note(void *network,uint32_t site,volatile LONG64 *counter) {
    unsigned char *objects[128];const unsigned n=tq_objects(network,objects,128);
    for(unsigned i=0;i<n;i++) {
        uint32_t *last=tq_last(objects[i]);if(!last) continue;
        const uint32_t now=tq_queued(objects[i]);
        if(now>*last) {InterlockedIncrement64(counter);rc_pick_note_ab(site,objects[i],RC_OBJ_R12_TRAFFIC,0,*last,now,i);}
        *last=now;
    }
}
/* 0x834c95: Zone::Move's own queue (0x82d680 -> 0x8bd190) when the zone is flagged. */
static void tq_sim(void *network,void *zone) {
    InterlockedIncrement64(&rc_stub_calls[TQ_SIM_CALL]);
    tq_note(network,0x10du,&tq_outside_count); /* growth since the last count, before Zone::Move's own queue: outside */
    (tq_sim_native?tq_sim_native:(tq_sim_fn)(void *)(image+rc_calls[TQ_SIM_CALL].target))(network,zone);
    tq_note(network,0x10cu,&tq_sim_count);
}
/* The camera 0x82f1e0 compares a view's +0x90 with: its two loads (cutscene manager +0xb0, else universe +0x3d0), as installed. */
static void *tp_camera(void) {
    const int routed=rc_page && (rc_installed_groups&(1u<<RC_TRAFFIC_VIEWS));
    unsigned char *manager=routed?rc_page+RC_MANAGER:*(unsigned char *volatile *)(image+RC_CUTSCENE),*universe;void *camera=NULL;
    if(manager) read_local(manager+0xb0,&camera,8);
    if(!camera) {
        universe=routed?rc_page+RC_SHADOW:*(unsigned char *volatile *)(image+RC_UNIVERSE);
        if(universe) read_local(universe+0x3d0,&camera,8);
    }
    return camera;
}
/* The render pass's traffic update of one zone for every view point in its sector. */
static void tp_pass(void *network,void *zone) {
    if(tp_mode==TP_OFF || !network || !zone || tp_thread) return;
    void *const sector=rc_ancestor(zone,0x58);const LONG count=InterlockedCompareExchange(&rc_shape_count,0,0);
    if(!sector || count<=0) return;
    int any=0;void *const camera=tp_camera();
    for(LONG i=0;i<count && i<(LONG)VIEW_MAX;i++) {
        void *const point=rc_shape_points[i];
        if(!point || rc_shape_sectors[i]!=sector) continue;
        if(!any++) InterlockedIncrement64(&tp_zones);
        tp_view_t *const view=&tp_views[i];memset(view,0,sizeof(*view));memset(tp_eyes[i],0,sizeof(tp_eyes[i]));
        view->slot[0]=tp_eyes[i];view->slot[1]=point;view->slot[0x12]=camera;
        /* 'observe': the objects' last update times are put back, so the render pass runs as natively. */
        unsigned char *objects[128];double times[128];unsigned n=0;
        if(tp_mode==TP_OBSERVE) {n=tq_objects(network,objects,128);for(unsigned k=0;k<n;k++) read_local(objects[k]+0xe8,&times[k],8);}
        tp_thread=GetCurrentThreadId();if(tp_mode==TP_OBSERVE) tp_hook_thread=tp_thread;
        (tp_traffic_native?tp_traffic_native:(tp_traffic_fn)(void *)(image+TP_TRAFFIC))(network,zone,view);
        tp_thread=tp_hook_thread=0;InterlockedIncrement64(&tp_calls);
        for(unsigned k=0;k<n;k++) memcpy(objects[k]+0xe8,&times[k],8);
    }
    if(any) tq_note(network,0x10eu,&tp_grown);
}
/* 0x834ca9: every Zone::Move of a zone with a network; growth since the last count is from outside Zone::Move;
 * then the native check and, when it passes, the pass. */
static char tq_check(void *network,void *zone) {
    InterlockedIncrement64(&rc_stub_calls[TQ_CHECK_CALL]);
    tq_note(network,0x10du,&tq_outside_count);
    const char ready=(tq_check_native?tq_check_native:(tq_check_fn)(void *)(image+rc_calls[TQ_CHECK_CALL].target))(network);
    if(ready && (rc_installed_groups&(1u<<RC_TRAFFIC_PASS))) tp_pass(network,zone);
    return ready;
}
/* 0x8ac5af: the late queue (b = 2 for the pass's). In 'observe' the pass's answers "not queued" (the tree's header). */
static void **tq_late(void *tree,void **out,void *record) {
    InterlockedIncrement64(&rc_stub_calls[TQ_LATE_CALL]);const int ours=tp_ours();
    if(ours) InterlockedIncrement64(&tp_late[1]);else {InterlockedIncrement64(&tq_late_count);InterlockedIncrement64(&tp_late[0]);}
    uint64_t id=0;if(record) read_local(record,&id,8);
    rc_pick_note_ab(0x10bu,tree?(unsigned char *)tree-0x290:NULL,RC_OBJ_R12_TRAFFIC,0,(uint32_t)id,ours?2:0,0);
    if(ours && tp_mode==TP_OBSERVE) {*out=tree;return out;}
    return (tq_late_native?tq_late_native:(tq_late_fn)(void *)(image+rc_calls[TQ_LATE_CALL].target))(tree,out,record);
}
/* The jump at 0x8abf2d: the member's slot 9 as natively; a row when a tracked member not yet queued (entry == header) is queued. */
static char tq_tracked(void *member,void *entry,void *header) {
    const char tracked=((tq_member_fn)(*(void ***)member)[0x48/8])(member);
    if(!tracked || entry!=header) return tracked;
    const int ours=tp_ours();uint64_t id=0;read_local((unsigned char *)member+8,&id,8);
    if(ours) InterlockedIncrement64(&tp_early[1]);else {InterlockedIncrement64(&tq_early_count);InterlockedIncrement64(&tp_early[0]);}
    rc_pick_note_ab(0x10au,header?(unsigned char *)header-0x290:NULL,RC_OBJ_R12_TRAFFIC,0,(uint32_t)id,ours?2:1,0);
    return tp_skip(ours)?0:tracked; /* not queued where this call must not write */
}
/* rdx = rbx (the entry found), r8 = r14 (the tree's header), al = tq_tracked(rcx), then the native branch. */
static unsigned tq_early_stub_bytes(unsigned char *p) {
    static const unsigned char head[]={0x48,0x89,0xda, 0x4d,0x89,0xf0, 0x48,0xb8};
    static const unsigned char mid[]={0xff,0xd0, 0x84,0xc0, 0x74,0x0e, 0xff,0x25,0,0,0,0};
    static const unsigned char jump[]={0xff,0x25,0,0,0,0};
    void *fn=(void *)tq_tracked,*yes=image+TQ_EARLY_TRACKED,*no=image+TQ_EARLY_UNTRACKED;unsigned n=0;
    memcpy(p+n,head,sizeof(head));n+=sizeof(head);memcpy(p+n,&fn,8);n+=8;memcpy(p+n,mid,sizeof(mid));n+=sizeof(mid);
    memcpy(p+n,&yes,8);n+=8;memcpy(p+n,jump,sizeof(jump));n+=sizeof(jump);memcpy(p+n,&no,8);n+=8;return n;
}
static void *tp_target(unsigned call) {return image+rc_calls[call].target;}
/* 0x3dde10(a, out, b, flag): a's transform in b's frame. Ours: from the simulation, both in a's cluster at the thread's time. */
static void *tp_relative_at(void *a,float *out,void *b,unsigned char flag,unsigned call) {
    InterlockedIncrement64(&rc_stub_calls[call]);
    void *const cluster=a && b && tp_ours()?rc_ancestor(a,0xf):NULL;
    if(!cluster || rc_ancestor(b,0xf)!=cluster) return (tp_relative_native?tp_relative_native:(tp_relative_fn)tp_target(call))(a,out,b,flag);
    InterlockedIncrement64(&tp_relative);
    __declspec(align(16)) float ta[16],tb[16];const double now=rc_thread_time();
    rc_gtr(a,ta,now,cluster);rc_gtr(b,tb,now,cluster);
    const float d[3]={ta[0]-tb[0],ta[1]-tb[1],ta[2]-tb[2]};float m[16];
    for(unsigned j=0;j<3;j++) m[j]=d[0]*tb[4+4*j]+d[1]*tb[5+4*j]+d[2]*tb[6+4*j];
    m[3]=0;
    for(unsigned i=0;i<3;i++) {
        for(unsigned j=0;j<3;j++) m[4+4*i+j]=ta[4+4*i]*tb[4+4*j]+ta[5+4*i]*tb[5+4*j]+ta[6+4*i]*tb[6+4*j];
        m[7+4*i]=0;
    }
    memcpy(out,m,sizeof(m));return out;
}
static void *tp_relative_a(void *a,float *out,void *b,unsigned char flag) {return tp_relative_at(a,out,b,flag,TP_RELATIVE_CALL);}
static void *tp_relative_b(void *a,float *out,void *b,unsigned char flag) {return tp_relative_at(a,out,b,flag,TP_RELATIVE_CALL+1);}
static void *tp_relative_c(void *a,float *out,void *b,unsigned char flag) {return tp_relative_at(a,out,b,flag,TP_RELATIVE_CALL+2);}
/* 0x6e59b0(ship, position, transform, parent): a real traffic ship placed on its lane. */
static void tp_place_at(void *ship,void *pos,void *posrot,void *parent,unsigned call) {
    InterlockedIncrement64(&rc_stub_calls[call]);const int ours=tp_ours();
    if(tp_skip(ours)) {InterlockedIncrement64(&tp_place_skipped);return;}
    InterlockedIncrement64(&tp_place[ours]);
    (tp_place_native?tp_place_native:(tp_place_fn)tp_target(call))(ship,pos,posrot,parent);
}
static void tp_place_a(void *ship,void *pos,void *posrot,void *parent) {tp_place_at(ship,pos,posrot,parent,TP_PLACE_CALL);}
static void tp_place_b(void *ship,void *pos,void *posrot,void *parent) {tp_place_at(ship,pos,posrot,parent,TP_PLACE_CALL+1);}
/* 0x8c0180(traffic, ship, now, start, end, lane): a real traffic ship's collision-ignore pairs (result unused at the call). */
static void *tp_filter_call(void *traffic,void *ship,double now,double start,double end,void *lane) {
    InterlockedIncrement64(&rc_stub_calls[TP_FILTER_CALL]);const int ours=tp_ours();
    if(tp_skip(ours)) {InterlockedIncrement64(&tp_filter_skipped);return NULL;}
    InterlockedIncrement64(&tp_filter[ours]);
    return (tp_filter_native?tp_filter_native:(tp_filter_fn)tp_target(TP_FILTER_CALL))(traffic,ship,now,start,end,lane);
}
/* 0xfb36d0: an instance drawn for the view; never for the pass's views. */
static void tp_draw(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h) {
    InterlockedIncrement64(&rc_stub_calls[TP_DRAW_CALL]);
    if(tp_ours()) {InterlockedIncrement64(&tp_draw_skipped);return;}
    (tp_draw_native?tp_draw_native:(tp_draw_fn)tp_target(TP_DRAW_CALL))(a,b,c,d,e,f,g,h);
}
/* 0x8abd6f: the two replaced moves, then past the slot-16 hook when this thread is in an 'observe' pass (r11 is dead here). */
static unsigned tp_hook_stub_bytes(unsigned char *p) {
    static const unsigned char head[]={0x41,0x0f,0x28,0xd4, 0x48,0x8b,0xd6, 0x49,0xbb};
    static const unsigned char mid[]={0x45,0x8b,0x1b, 0x65,0x44,0x3b,0x1c,0x25,0x48,0,0,0, 0x74,0x0e, 0xff,0x25,0,0,0,0};
    static const unsigned char jump[]={0xff,0x25,0,0,0,0};
    void *flag=(void *)&tp_hook_thread,*call=image+TP_HOOK_CALL,*after=image+TP_HOOK_AFTER;unsigned n=0;
    memcpy(p+n,head,sizeof(head));n+=sizeof(head);memcpy(p+n,&flag,8);n+=8;memcpy(p+n,mid,sizeof(mid));n+=sizeof(mid);
    memcpy(p+n,&call,8);n+=8;memcpy(p+n,jump,sizeof(jump));n+=sizeof(jump);memcpy(p+n,&after,8);n+=8;return n;
}
static void tp_status(char *out,size_t capacity) {
    if(tp_mode==TP_OFF) {snprintf(out,capacity,"off");return;}
    snprintf(out,capacity,"%s,zones:%llu,calls:%llu,late:%llu/%llu,early:%llu/%llu,place:%llu/%llu/%llu,filter:%llu/%llu/%llu,draw_skip:%llu,relative:%llu,grown:%llu",
        tp_mode==TP_VIEW?"view":"observe",(unsigned long long)tp_zones,(unsigned long long)tp_calls,
        (unsigned long long)tp_late[1],(unsigned long long)tp_late[0],(unsigned long long)tp_early[1],(unsigned long long)tp_early[0],
        (unsigned long long)tp_place[1],(unsigned long long)tp_place[0],(unsigned long long)tp_place_skipped,
        (unsigned long long)tp_filter[1],(unsigned long long)tp_filter[0],(unsigned long long)tp_filter_skipped,
        (unsigned long long)tp_draw_skipped,(unsigned long long)tp_relative,(unsigned long long)tp_grown);
}
