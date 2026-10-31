/* Determinism fix, DEFAULT OFF: a cutscene is ready on the game clock, never on the local asset
 * streaming (native_gate_cutscene_ready = true; stop reason 178; report LOCKSTEP-VIRTUAL-PLAYERS
 * section 16.21).
 *
 * s2-victim7: NPC_UseCases.OnCutsceneReady (event_cutscene_ready, a comm monitor's talking head)
 * ran at tick 1623 on client 1 and 1610 on client 2; the use case's next steps, the cue instances
 * and their runtime IDs followed apart. X4 posts U::CutsceneReadyEvent once per cutscene (+0x4ee) in
 * 0x10d7a40, called from the cutscene camera's update (Cutscenes::ShotCamera::vfunc2 0x10d7f60,
 * CameraAnimated::vfunc2 0x10e22f0), when the render target is set (+0x880 of its entry), the
 * shown object is loaded at level 6 (component vfunc +0x1360) and its children pass 0x380d20, or
 * else when the shot's own time (+0xc8) is past 2.0 s. All of it is each client's streaming.
 *
 * With the fix the native post never runs (the jne after the +0x4ee test at 0x10d7a82 becomes
 * nop + jmp: always "already posted"), and the gate posts the same event itself, through X4's own
 * allocation, string copy and deferred post, for every cutscene in the manager's active list
 * (0x3d19900, list +0x30) 2.0 s of game time after the first pass that sees it there; then it sets
 * +0x4ee as X4 does. The active list is simulation state (the manager's frame update in
 * UniverseFrameUpdate 0x9fe413), so first sight and the post are the same tick on every client.
 * Cost: the talking head can show before its body has streamed in; the local render catches up.
 * A cutscene that natively had no camera update would now get a ready event as well (same on
 * every client). Runs in path_release (the frame thread, before the pass), with serial pathing. */
#define CR_STOP_REASON 178u
#define CR_SITE 0x10d7a82u                /* jne after cmp byte [rdx+0x4ee],0 in the native post */
#define CR_ORIGINAL 0x850fu               /* 0f 85 */
#define CR_PATCHED 0xe990u                /* 90 e9: the same rel32 from the same next address */
#define CR_MANAGER 0x3d19900u
#define CR_ACTIVE 0x30u                   /* manager: list of active cutscenes (node[0] next, node[2] cutscene) */
#define CR_FLAG 0x4eeu
#define CR_KEY 0x480u                     /* std::string, copied into the event */
#define CR_ID 0x8u
#define CR_EXTRA 0x68u
#define CR_EVENT_VTABLE 0x2c6d7e0u        /* U::CutsceneReadyEvent */
#define CR_ALLOCATE 0x14b7140u            /* SMem::Allocate(size, -, -, -, alignment) */
#define CR_COPY 0xd2310u                  /* std::string copy construct (dst, src) */
#define CR_POST 0x99a720u                 /* EventSource::PostEventDeferred(source, event, time) */
#define CR_DELAY 2.0                      /* X4's own fallback in 0x10d7a40 */
#define CR_TABLE 64u
#define CR_LIST_MAX 256u
typedef void *(*cr_alloc_fn)(size_t,uintptr_t,uintptr_t,uintptr_t,unsigned);
typedef void (*cr_copy_fn)(void *,const void *);
typedef void (*cr_post_fn)(void *,void *,double);
typedef struct {cr_alloc_fn alloc;cr_copy_fn copy;cr_post_fn post;int (*now)(double *);} cr_fns;
static cr_fns cr_fn;
typedef struct {unsigned char *cutscene;int id;double first;unsigned char seen,posted;} cr_entry;
static cr_entry cr_table[CR_TABLE];
static int cr_armed,cr_patched,cr_restored,cr_pins_checked;
static uint64_t cr_posted,cr_dropped,cr_seen,cr_frames,cr_last_id,cr_last_tick;
static volatile LONG64 cr_failures;
static const char *cr_stage="off";
static unsigned char *cr_base;                      /* image, or a test region */
static char cr_rows[8][96];
static unsigned cr_row_head,cr_row_tail;
static int cr_pins(void) {
    if(!cr_pins_checked) {
        unsigned char copy[0x461];uint16_t site=CR_ORIGINAL;
        int ok=read_local(image+0x10d7a40u,copy,sizeof(copy));
        if(ok) memcpy(copy+(CR_SITE-0x10d7a40u),&site,2);   /* the site as X4 has it */
        ok=ok && hash_ok(copy,sizeof(copy),"37de7f1739250ac193dc880973b83f5218c2368902185df06b203c7c568178e0") &&
            hash_ok(image+0x10f1930u,0x11bu,"7ccdec616bd2aa0b537f47f7cc7c208573da921a22a967f6b09e6f2d9c6d8b10") &&  /* manager frame update: list +0x30 */
            hash_ok(image+CR_POST,0xedu,"58b80a15cd07746aeb8e15c6b91230adeb30b3355fe80f58dc1b93b5c72925c3") &&
            hash_ok(image+CR_COPY,0x9bu,"5f86799588500c46ea15775f00849e0cc191d2c79faf2cfb87379628d78ad07e");
        if(ok) {void *slot=NULL;ok=read_local(image+CR_EVENT_VTABLE+8,&slot,8) && slot==(void*)(image+0x10d1090u);} /* GetType */
        cr_pins_checked=ok?1:-1;
    }
    return cr_pins_checked==1;
}
static void cr_use_image(void) {
    cr_base=image;cr_fn.alloc=(cr_alloc_fn)(image+CR_ALLOCATE);cr_fn.copy=(cr_copy_fn)(image+CR_COPY);cr_fn.post=(cr_post_fn)(image+CR_POST);
    cr_fn.now=pnat_time_tls;
}
static uint16_t cr_site(void) {uint16_t v=0;read_local(cr_base+CR_SITE,&v,2);return v;}
static int cr_swap(uint16_t expected,uint16_t value) {
    DWORD old,ignored;if(!VirtualProtect(cr_base+CR_SITE,2,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint16_t actual=(uint16_t)InterlockedCompareExchange16((volatile SHORT*)(cr_base+CR_SITE),(SHORT)value,(SHORT)expected);
    VirtualProtect(cr_base+CR_SITE,2,old,&ignored);FlushInstructionCache(GetCurrentProcess(),cr_base+CR_SITE,2);
    return actual==expected;
}
static int cr_fail(const char *why) {cr_stage=why;InterlockedIncrement64(&cr_failures);return 0;}
static void cr_row(uint64_t tick,int id,const unsigned char *cutscene) {
    char key[48];uint64_t size=0,capacity=0;const unsigned char *at=cutscene+CR_KEY;key[0]=0;
    if(read_local(cutscene+CR_KEY+0x10,&size,8) && read_local(cutscene+CR_KEY+0x18,&capacity,8) && size && size<sizeof(key) &&
       (capacity<=15 || read_local(cutscene+CR_KEY,&at,8)) && read_local(at,key,(size_t)size)) key[size]=0;
    for(unsigned i=0;key[i];i++) if(!((key[i]>='a' && key[i]<='z') || (key[i]>='A' && key[i]<='Z') || (key[i]>='0' && key[i]<='9') || key[i]=='_')) {key[0]=0;break;}
    snprintf(cr_rows[cr_row_head++%8u],sizeof(cr_rows[0]),"tick=%llu id=%d key=%s",(unsigned long long)tick,id,key[0]?key:"-");
    if(cr_row_head-cr_row_tail>8u) cr_row_tail=cr_row_head-8u;
}
/* X4's own post, with the same fields as 0x10d7a40's. */
static int cr_post_ready(unsigned char *manager,unsigned char *cutscene,double now) {
    unsigned char *event=cr_fn.alloc(0x48,0,0,0,0x10);
    if(!event) return cr_fail("alloc");
    void *vtable=cr_base+CR_EVENT_VTABLE;int id=0;uint64_t extra=0;
    read_local(cutscene+CR_ID,&id,4);read_local(cutscene+CR_EXTRA,&extra,8);
    memset(event,0,0x18);memcpy(event,&vtable,8);
    cr_fn.copy(event+0x18,cutscene+CR_KEY);
    memcpy(event+0x38,&id,4);memcpy(event+0x40,&extra,8);
    cr_fn.post(manager,event,now);
    cutscene[CR_FLAG]=1;
    return 1;
}
/* path_release (frame thread): the active list as the simulation left it after the last frame. */
static int cr_before_release(void) {
    unsigned char *manager=NULL,*sentinel=NULL,*node=NULL;double now=0;
    if(!cr_armed) return 1;
    if(InterlockedCompareExchange64(&cr_failures,0,0)) return 0;
    if(cr_site()!=CR_PATCHED) return cr_fail("site_changed");
    ++cr_frames;
    if(!read_local(cr_base+CR_MANAGER,&manager,8) || !manager) return 1;   /* no cutscene manager (menus) */
    if(!cr_fn.now(&now)) return cr_fail("time");
    if(!read_local(manager+CR_ACTIVE,&sentinel,8) || !sentinel || !read_local(sentinel,&node,8)) return cr_fail("list");
    for(unsigned i=0;i<CR_TABLE;i++) cr_table[i].seen=0;
    for(unsigned steps=0;node && node!=sentinel;steps++) {
        unsigned char *cutscene=NULL;int id=0;
        if(steps>=CR_LIST_MAX || !read_local(node+0x10,&cutscene,8) || !cutscene || !read_local(cutscene+CR_ID,&id,4)) return cr_fail("node");
        cr_entry *e=NULL,*free_slot=NULL;
        for(unsigned i=0;i<CR_TABLE;i++) {
            if(cr_table[i].cutscene==cutscene && cr_table[i].id==id) {e=&cr_table[i];break;}
            if(!cr_table[i].cutscene && !free_slot) free_slot=&cr_table[i];
        }
        if(!e) {
            if(!free_slot) return cr_fail("table_full");
            e=free_slot;e->cutscene=cutscene;e->id=id;e->first=now;e->posted=0;++cr_seen;
        }
        e->seen=1;
        if(!e->posted && now>=e->first+CR_DELAY) {
            if(!cr_post_ready(manager,cutscene,now)) return 0;
            e->posted=1;++cr_posted;cr_last_id=(uint64_t)(unsigned)id;cr_last_tick=stats.issued;cr_row(stats.issued,id,cutscene);
        }
        if(!read_local(node,&node,8)) return cr_fail("node");
    }
    for(unsigned i=0;i<CR_TABLE;i++) if(cr_table[i].cutscene && !cr_table[i].seen) {
        if(!cr_table[i].posted) ++cr_dropped;   /* stopped before its deadline */
        memset(&cr_table[i],0,sizeof(cr_table[i]));
    }
    return 1;
}
static int cr_arm(void) {
    if(cr_armed) return stats.active!=0;   /* identical rerun after a loading-UI reload */
    if(!stats.active || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!path_installed) {cr_stage="serial_pathing";return 0;}
    if(!cr_pins()) {cr_stage="pins";return 0;}
    cr_use_image();
    if(cr_site()!=CR_ORIGINAL) {cr_stage="site";return 0;}
    if(!cr_swap(CR_ORIGINAL,CR_PATCHED)) {cr_stage="cas";return 0;}
    memset(cr_table,0,sizeof(cr_table));cr_patched=1;cr_restored=0;cr_armed=1;cr_stage="armed";return 1;
}
static int cr_owned(void) {return !cr_patched || cr_site()==CR_PATCHED;}
static void cr_restore(void) {
    if(!cr_armed) return;
    if(cr_patched) {
        if(!cr_swap(CR_PATCHED,CR_ORIGINAL)) return;   /* a foreign value stays */
        cr_patched=0;
    }
    cr_restored=1;cr_armed=0;
}
static const char *cr_take_row(void) {return cr_row_tail!=cr_row_head?cr_rows[cr_row_tail++%8u]:NULL;}
static int cr_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"armed=%d site=%04x seen=%llu posted=%llu dropped=%llu frames=%llu last=%llu/t%llu restored=%d stage=%s failures=%lld",
        cr_armed,cr_base?cr_site():0,(unsigned long long)cr_seen,(unsigned long long)cr_posted,(unsigned long long)cr_dropped,
        (unsigned long long)cr_frames,(unsigned long long)cr_last_id,(unsigned long long)cr_last_tick,cr_restored,cr_stage,(long long)cr_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
