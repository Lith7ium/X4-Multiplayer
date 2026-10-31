/* Stock piloting, slot 1's view inputs (part of lockstep_pilot_stock.h, DEFAULT OFF,
 * stop reason 155). Simulation code reads the local camera's mode in four places
 * that decide how the player's ship flies and fights (camera agent inventory,
 * section B; build 9.00, the camera is *(Universe+0x3d0), +0x868 its mode, 0 =
 * cockpit):
 *   d924d0  PlayerController coefficients: in cockpit mode the twelve rotation
 *           coefficients are scaled by the camera's FOV factor 97c140 (block
 *           d93052..d93087, SIM region "coefficients");
 *   54ee80  Engine (secondary vtable slot 5): the player's ship's engines get the
 *           macro's cockpit factor (+0xb70) only in cockpit mode (54efb5..54efd6);
 *   7d4d40  Ship::vfunc836 (XShapeFilter, query layer 0x395cd5c): the player
 *           camera's ship is excluded only in cockpit mode (7d4d7a: an external
 *           camera returns false at once);
 *   9fbf50  "floating view" (an external camera not on the player's object):
 *           Weapon::vfunc1041 81bb50 and MissileLauncher::vfunc1041 6093e0 (Weapon
 *           ::Move 81476c: manual control of the player's guns) are off while it is
 *           true (calls 81bba3, 609440).
 * The record carries slot 1's view (cockpit bit, floating bit, FOV factor) and
 * every client answers these four reads from the replayed view of the tick
 * (neutral before the first replayed record: cockpit, not floating, factor 1):
 * the three blocks start with a five-byte jump to a trampoline on a page near the
 * image (one 16-byte CAS each, inside one aligned block; the tail of the replaced
 * first instruction and the rest of the block are left as they were), the two
 * calls are re-pointed; the trampolines call the stubs below with the stock
 * register and stack state and jump back to the block's own continuations.
 * The record also carries slot 1's soft target (player+0x6dd0, as a portable ID;
 * 1 when it has none) for readers that need it per slot (the camera agent, MD
 * player.target); applying it is the UI replay's (lockstep_ui_replay.h: 9fcd70 is a getter,
 * the writer 0xaecb60 UI code). Presentation readers of the same camera fields
 * (6da210 cockpit effects, HUD, UI) keep the local camera. */
#define PVIEW_SITES 5u
#define PVIEW_PAGE 0x1000u
#define PVIEW_PATCH 5u   /* the jump or call written; the rest of a site's first instruction is never reached */
enum {PVIEW_JUMP=0,PVIEW_CALL=1};
enum {PVIEW_FOV=0,PVIEW_ENGINE=1,PVIEW_SHIP=2,PVIEW_WEAPON=3,PVIEW_MISSILE=4};
typedef struct {unsigned rva,length,kind,resume_a,resume_b;unsigned char original[7];} pview_site;
static const pview_site pview_sites[PVIEW_SITES]={
    {0xd93052u,7,PVIEW_JUMP,0xd93087u,0,{0x48,0x8b,0x0d,0x9f,0x68,0xf8,0x02}},       /* mov rcx,[Universe] */
    {0x54efb5u,7,PVIEW_JUMP,0x54efd6u,0x54efdfu,{0x48,0x8b,0x05,0x3c,0xa9,0x7c,0x03}}, /* mov rax,[Universe] */
    {0x7d4d7au,7,PVIEW_JUMP,0x7d4eb5u,0x7d4d93u,{0x48,0x8b,0x88,0xd0,0x03,0x00,0x00}}, /* mov rcx,[rax+0x3d0] */
    {0x81bba3u,5,PVIEW_CALL,0x9fbf50u,0,{0xe8,0xa8,0x03,0x1e,0x00}},                  /* call 9fbf50 */
    {0x609440u,5,PVIEW_CALL,0x9fbf50u,0,{0xe8,0x0b,0x2b,0x3f,0x00}}};                 /* call 9fbf50 */
typedef struct {
    float (*fov)(void *);          /* 97c140(camera) */
    unsigned char (*floating)(void); /* 9fbf50 */
    uint64_t (*portable)(uint64_t);  /* world_identity_portable */
} pview_fns;
static pview_fns pview_fn;
static unsigned char *pview_page;
static int pview_installed;
/* The replayed view of one tick for every thread: bit 63 valid, 34..62 the tick's
 * low 29 bits, 33 floating, 32 cockpit, 0..31 the FOV factor's bits. */
static volatile LONG64 pview_state[PILOT_SLOTS];
static uint64_t pview_target_replay[PILOT_SLOTS],pview_target_tick[PILOT_SLOTS];
static volatile LONG64 pview_calls[PVIEW_SITES],pview_neutral,pview_samples,pview_unportable;
/* Calls per phase: 0 the frame thread simulating (MoveWorker: the tree, d95180), 1 the
 * main thread, 2 any other thread (physics and pathing jobs), 3 the frame thread rendering
 * (VBI slot 10). Live, J2c: nothing on 1 or 2; X4 renders on the frame thread. The first
 * three must be equal on every client; only rendering follows the camera. */
static volatile LONG64 pview_thread_calls[4][PVIEW_SITES];
static void pview_count(unsigned site) {
    DWORD thread=GetCurrentThreadId();
    unsigned phase=thread==ui_thread?(render_rng_in_slot?3u:0u):thread==main_thread?1u:2u;
    InterlockedIncrement64(&pview_calls[site]);InterlockedIncrement64(&pview_thread_calls[phase][site]);
}
#define PVIEW_TICK_MASK 0x1fffffffULL
static uint64_t pview_pack(uint64_t tick,unsigned char cockpit,unsigned char floating,float fov) {
    uint32_t bits;memcpy(&bits,&fov,4);
    return 1ULL<<63|(tick&PVIEW_TICK_MASK)<<34|(uint64_t)(floating!=0)<<33|(uint64_t)(cockpit!=0)<<32|bits;
}
/* Slot s's view this tick (neutral without a replayed record of this tick). */
static void pview_of_slot(unsigned slot,unsigned char *cockpit,unsigned char *floating,float *fov) {
    uint64_t s=slot>=1 && slot<=PILOT_SLOTS?(uint64_t)InterlockedCompareExchange64(&pview_state[slot-1],0,0):0;
    if((s>>63) && ((s>>34)&PVIEW_TICK_MASK)==(stats.issued&PVIEW_TICK_MASK)) {
        uint32_t bits=(uint32_t)s;memcpy(fov,&bits,4);*cockpit=(s>>32)&1;*floating=(s>>33)&1;return;
    }
    InterlockedIncrement64(&pview_neutral);*cockpit=1;*floating=0;*fov=1.0f;
}
/* The view the current reader sees: the slot scope's on the frame thread inside one, else slot 1's. */
static void pview_now(unsigned char *cockpit,unsigned char *floating,float *fov) {
    unsigned slot=pstk_scope_slot && GetCurrentThreadId()==ui_thread?pstk_scope_slot:1u;
    pview_of_slot(slot,cockpit,floating,fov);
}
/* The stubs the trampolines call (stock register and stack state, Win64 ABI). */
static float pview_fov_stub(void) {
    unsigned char c,f;float fov;pview_count(PVIEW_FOV);pview_now(&c,&f,&fov);return c?fov:1.0f;
}
static unsigned char pview_cockpit_stub(void) {
    unsigned char c,f;float fov;pview_count(PVIEW_ENGINE);pview_now(&c,&f,&fov);return c;
}
static unsigned char pview_external_stub(void) {
    unsigned char c,f;float fov;pview_count(PVIEW_SHIP);pview_now(&c,&f,&fov);return !c;
}
static unsigned char pview_floating_stub(void) {
    unsigned char c,f;float fov;pview_count(PVIEW_WEAPON);pview_now(&c,&f,&fov);return f;
}
/* The capturing slot's player for the target field when it is not the singleton (client k's seated P_k). */
static unsigned char *pview_slot_player;
static uint64_t pview_slot_target;  /* the target selected on this client for its slot (lockstep_pilot_present.h) */
/* The capturing slot's view of this frame (capturing client, Lua, frame thread). */
static int pview_sample(unsigned char *bits,float *fov,uint64_t *target) {
    unsigned char *universe=NULL,*camera=NULL,*player=pview_slot_player?pview_slot_player:pnat_player();int mode=0;uint64_t runtime=0;
    if(!pview_fn.fov || !pview_fn.floating || !read_local(pnat_base+PNAT_UNIVERSE,&universe,8) || !universe ||
       !read_local(universe+0x3d0,&camera,8) || (camera && !read_local(camera+0x868,&mode,4))) return 0;
    unsigned char cockpit=!camera || !mode;
    *fov=camera && !mode?pview_fn.fov(camera):1.0f;
    if(!isfinite(*fov)) return 0;
    *bits=(unsigned char)(cockpit|(pview_fn.floating()?2:0));
    *target=0;
    if(pview_slot_player && !(uirep_families&UIREP_FAMILY_TARGET)) runtime=pview_slot_target;
    else if(player) read_local(player+0x6dd0,&runtime,8);
    if(runtime) {
        *target=pview_fn.portable?pview_fn.portable(runtime):0;
        if(!*target) {*target=1;InterlockedIncrement64(&pview_unportable);}
    }
    InterlockedIncrement64(&pview_samples);return 1;
}
/* The replayed view of tick t (path_release, before the frame). */
static void pview_replay(unsigned slot,uint64_t tick,int valid,unsigned char bits,float fov,uint64_t target) {
    if(slot<1 || slot>PILOT_SLOTS) return;
    if(valid) {InterlockedExchange64(&pview_state[slot-1],(LONG64)pview_pack(tick,bits&1,(bits>>1)&1,fov));pview_target_replay[slot-1]=target;}
    else {InterlockedExchange64(&pview_state[slot-1],0);pview_target_replay[slot-1]=0;}
    pview_target_tick[slot-1]=tick;
}
/* For other modules (camera routing): slot's replayed view this tick; 0 = none. */
static int pview_of(unsigned slot,unsigned char *cockpit,unsigned char *floating,float *fov,uint64_t *target) {
    if(slot<1 || slot>PILOT_SLOTS || !pview_installed) return 0;
    pview_of_slot(slot,cockpit,floating,fov);
    *target=pview_target_tick[slot-1]==stats.issued?pview_target_replay[slot-1]:0;
    return 1;
}

/* Per-slot view inputs for other modules (camera agent: native_gate_xs_camera).
 * Slots 1..vpl_slot_count() are the pilot slots of a stock session; every stock slot
 * has its record (the others answer "no soft target, cockpit"). Values are the
 * replayed record of this tick, identical on every client (neutral before the
 * first record). The soft target is a portable ID (world identities: compare with
 * world_identity_portable of a ship's runtime ID; 1 = a target without one, 0 =
 * none). */
__attribute__((unused)) static unsigned vpl_slot_count(void) {return pnat_armed && pnat_mode==PNAT_MODE_STOCK && pview_installed?pilot_slot_count:0;}
__attribute__((unused)) static uint64_t vpl_soft_target(unsigned k) {
    unsigned char cockpit,floating;float fov;uint64_t target=0;
    return k>=1 && k<=vpl_slot_count() && pview_of(k,&cockpit,&floating,&fov,&target)?target:0;
}
__attribute__((unused)) static int vpl_external_view(unsigned k) {
    unsigned char cockpit=1,floating=0;float fov;uint64_t target;
    return k>=1 && k<=vpl_slot_count() && pview_of(k,&cockpit,&floating,&fov,&target) && !cockpit;
}
/* ---- the patches ---- */
static int32_t pview_rel(unsigned from_rva,const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(pnat_base+from_rva+5));}
static unsigned char *pview_tramp(unsigned site) {return pview_page+site*0x40u;}
static unsigned pview_abs_jump(unsigned char *p,const void *target) {
    static const unsigned char jmp[6]={0xff,0x25,0,0,0,0};memcpy(p,jmp,6);memcpy(p+6,&target,8);return 14;
}
static unsigned pview_mov_rax(unsigned char *p,const void *value) {p[0]=0x48;p[1]=0xb8;memcpy(p+2,&value,8);return 10;}
/* The trampoline of one site (deterministic bytes, checked every pass). */
static void pview_write_tramp(unsigned site,unsigned char *p) {
    const pview_site *s=&pview_sites[site];unsigned n=0;memset(p,0xcc,0x40);
    if(site==PVIEW_FOV) {           /* xmm0 = factor; xmm11 *= f; xmm9 *= f; back to d93087 */
        static const unsigned char tail[12]={0xff,0xd0,0xf3,0x44,0x0f,0x59,0xd8,0xf3,0x44,0x0f,0x59,0xc8};
        n+=pview_mov_rax(p+n,(void*)pview_fov_stub);memcpy(p+n,tail,12);n+=12;n+=pview_abs_jump(p+n,pnat_base+s->resume_a);
    } else if(site==PVIEW_ENGINE) { /* cockpit: multiply at 54efd6, else skip to 54efdf */
        static const unsigned char test[6]={0xff,0xd0,0x84,0xc0,0x74,0x0e};
        n+=pview_mov_rax(p+n,(void*)pview_cockpit_stub);memcpy(p+n,test,6);n+=6;
        n+=pview_abs_jump(p+n,pnat_base+s->resume_a);n+=pview_abs_jump(p+n,pnat_base+s->resume_b);
    } else if(site==PVIEW_SHIP) {   /* keep the Universe (rax) in rbx; external: return false (7d4eb5), else 7d4d93 */
        static const unsigned char save[3]={0x48,0x8b,0xd8},test[9]={0xff,0xd0,0x84,0xc0,0x48,0x8b,0xc3,0x74,0x0e};
        memcpy(p,save,3);n=3;n+=pview_mov_rax(p+n,(void*)pview_external_stub);memcpy(p+n,test,9);n+=9;
        n+=pview_abs_jump(p+n,pnat_base+s->resume_a);n+=pview_abs_jump(p+n,pnat_base+s->resume_b);
    } else {                        /* the call itself: jump to the stub, it returns to the caller */
        n+=pview_abs_jump(p,(void*)pview_floating_stub);
    }
    (void)n;
}
/* The bytes a site holds while patched. */
static void pview_patched_bytes(unsigned site,unsigned char *out) {
    const pview_site *s=&pview_sites[site];int32_t rel=pview_rel(s->rva,pview_tramp(site));
    out[0]=s->kind==PVIEW_CALL?0xe8:0xe9;memcpy(out+1,&rel,4);
}
/* One 16-byte CAS: every site lies inside one aligned block. */
static int pview_cas(unsigned rva,const unsigned char *expected,const unsigned char *value,unsigned n) {
    unsigned char *block=pnat_base+(rva&~15u);DWORD old,ignored;
    if(((rva+n-1)&~15u)!=(rva&~15u) || !VirtualProtect(block,16,PAGE_EXECUTE_READWRITE,&old)) return 0;
    unsigned __int128 current,desired;memcpy(&current,(const void*)block,16);desired=current;
    int ok=!memcmp((unsigned char*)&current+(rva&15u),expected,n);
    if(ok) {memcpy((unsigned char*)&desired+(rva&15u),value,n);ok=__sync_val_compare_and_swap((unsigned __int128*)block,current,desired)==current;}
    VirtualProtect(block,16,old,&ignored);FlushInstructionCache(GetCurrentProcess(),block,16);
    return ok;
}
static unsigned char *pview_alloc_page(void) {
    uintptr_t at=(uintptr_t)(pnat_base+0xd93052u)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,PVIEW_PAGE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int reachable=1;
            for(unsigned i=0;i<PVIEW_SITES;i++) {
                intptr_t d=(intptr_t)p-(intptr_t)(pnat_base+pview_sites[i].rva+5);
                if(d<=INT32_MIN+0x10000 || d>=INT32_MAX-0x10000) reachable=0;
            }
            if(reachable) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int pview_uninstall(void) {
    int ok=1;
    for(unsigned i=PVIEW_SITES;i-->0;) {
        const pview_site *s=&pview_sites[i];unsigned char patched[7];
        if(!pview_page) break;
        pview_patched_bytes(i,patched);
        if(!memcmp(pnat_base+s->rva,s->original,PVIEW_PATCH)) continue;
        if(!pview_cas(s->rva,patched,s->original,PVIEW_PATCH)) ok=0;
    }
    if(ok) {pview_installed=0;for(unsigned i=0;i<PILOT_SLOTS;i++) InterlockedExchange64(&pview_state[i],0);}
    return ok;
}
/* From the first pass of the session (frame thread, before the pass is released). */
static int pview_install(void) {
    if(pview_installed) return 1;
    for(unsigned i=0;i<PVIEW_SITES;i++) if(memcmp(pnat_base+pview_sites[i].rva,pview_sites[i].original,pview_sites[i].length)) return 0;
    if(!pview_page) pview_page=pview_alloc_page();
    if(!pview_page) return 0;
    DWORD old,ignored;
    if(!VirtualProtect(pview_page,PVIEW_PAGE,PAGE_READWRITE,&old)) return 0;
    for(unsigned i=0;i<PVIEW_SITES;i++) pview_write_tramp(i,pview_tramp(i));
    if(!VirtualProtect(pview_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) return 0;
    FlushInstructionCache(GetCurrentProcess(),pview_page,PVIEW_PAGE);
    for(unsigned i=0;i<PILOT_SLOTS;i++) InterlockedExchange64(&pview_state[i],0);
    pview_installed=1;
    for(unsigned i=0;i<PVIEW_SITES;i++) {
        unsigned char patched[7];pview_patched_bytes(i,patched);
        if(!pview_cas(pview_sites[i].rva,pview_sites[i].original,patched,PVIEW_PATCH)) {pview_uninstall();pview_installed=0;return 0;}
    }
    return 1;
}
static int pview_intact(void) {
    if(!pview_installed || !pview_page) return 0;
    for(unsigned i=0;i<PVIEW_SITES;i++) {
        unsigned char patched[7],tramp[0x40];pview_patched_bytes(i,patched);pview_write_tramp(i,tramp);
        if(memcmp(pnat_base+pview_sites[i].rva,patched,PVIEW_PATCH) || memcmp(pview_tramp(i),tramp,0x40)) return 0;
    }
    return 1;
}
static int pview_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity," view=%d/%lld/%lld/%lld/%lld/%lld/%lld/%lld",pview_installed,(long long)pview_calls[PVIEW_FOV],
        (long long)pview_calls[PVIEW_ENGINE],(long long)pview_calls[PVIEW_SHIP],(long long)pview_calls[PVIEW_WEAPON],
        (long long)pview_neutral,(long long)pview_samples,(long long)pview_unportable);
    for(unsigned t=0;n>0 && (size_t)n<capacity && t<4;t++) {
        int more=snprintf(out+n,capacity-(size_t)n,"%s%lld/%lld/%lld/%lld",t?",":" view_threads=",(long long)pview_thread_calls[t][PVIEW_FOV],
            (long long)pview_thread_calls[t][PVIEW_ENGINE],(long long)pview_thread_calls[t][PVIEW_SHIP],(long long)pview_thread_calls[t][PVIEW_WEAPON]);
        if(more<0) return 0;
        n+=more;
    }
    return n>0 && (size_t)n<capacity?n:0;
}
