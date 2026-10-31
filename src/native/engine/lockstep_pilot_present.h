/* Presentation of a stock slot on its own client, DEFAULT OFF, stop reason 162
 * (native_gate_pilot_present; needs stock slots 2..N, lockstep_pilot_slots.h, and the
 * render split). Client k's stock camera, HUD and UI present slot k's ship as "the
 * player": X4's own code runs for presentation and finds slot k's ship when it asks about
 * the player. The simulation never sees it: every byte is back before simulation code runs.
 *
 * The singleton does not change. stock-K1 ran UI code with the singleton = P_k and faulted
 * at once (ui_fault:3de6aa, a NULL component into the view-cache transform): P_k was built
 * by the factory and is not in the tree, so it has no published view cache and no state from
 * a world load (report section 16.12). So X4's own player, which has all of that, keeps its
 * object and takes slot k's identity for the length of a scope:
 *
 *   presentation scope (this client's own slot only, frame thread, no slot scope open)
 *     on the canonical player: parent +0x70 = slot k's cockpit room, controlled-object
 *     vector +0x308 = P_k's, control position +0x6ab8 = P_k's, the published parent of its
 *     view cache for this thread's bank = the room, its SIM fields = P_k's; on the room:
 *     attention 11, live and published (Room::vfunc703 0x76e520 gives the player's room 11,
 *     another ship's cockpit its ship's 9 or less; the simulation's value is back at the exit);
 *     SIM and DEVICE image regions: a COPY of slot k's store (the store is never touched,
 *     so a write refuses itself: it is discarded at the exit, and a changed SIM byte is
 *     counted and its region named, refused=). LOCAL stays this client's. At the exit every
 *     byte comes back. The camera stays anchored on the canonical player as in stock play,
 *     its cockpit mode then finds slot k's ship through the same walks.
 *   entry points (nothing else runs in it)
 *     VBI slot 6   UI::CUIController::Move, vtable entry 0x2bc07d8: the UI and its FFI
 *                  queries, except the probe's own frame (gate.pilot_present_pause/resume:
 *                  gate modules read the canonical state);
 *     VBI slot 10  GameRender::Move (through the render split's wrapper): HUD draw,
 *                  animation, every camera;
 *     VBI slot 4   the dispatch of presentation ids (camera, menus, map, targets);
 *     9fe573       the camera's view bank refill in UniverseFrameUpdate (re-pointed
 *                  through the near page);
 *     9fe5d0       the camera pick 717ed0 (crosshair, what is under the cursor): it writes
 *                  the player's DEVICE block from the camera and must not hit ship k itself.
 *   a self-test at the start, in the scope, before any UI runs in it: X4's own
 *     GetPlayerOccupiedShipID and the view-cache ancestor lookup 0x3d91e0 must answer slot
 *     k's ship and room (identity_occupied, identity_view_parent: stop 162);
 *     GetPlayerControlledShipID and GetPlayerZoneID are logged (asks=). The real engine
 *     confirms the overlay and the cache layout offline tests cannot.
 *   the HUD's place (mode 'hud', the default; 'camera' leaves it out): the UI anchor
 *     (Universe+0x470/0x478, ui_screen_positions) in a save hangs under the player's own
 *     camera_anchor_scene (Universe+0x428), so it follows the player in the scope with no
 *     connection at all. Only when it hangs elsewhere (a room) X4's own 0x8e8450 connects it
 *     to the player's cockpit room, in the scope; then a real connection, local to this
 *     client: a census difference in a --present run that --present camera does not show is
 *     this. hud=mode/moves: stock-K3 reissued it every tick (571), it should now say 0.
 *   PILOT_PRESENT_DIFF (once at the start, gate.pilot_present_diff): the words where slot 1's
 *     ship and room differ from slot k's in a way that matters (one zero, or small numbers):
 *     what a player's presence set on them.
 * The start, the anchor reissue and the stop run on this client only, inside the frame: the
 * thread's simulation RNG word is put back around them (rng_moved= counts a draw).
 * Faults: an access violation inside a presentation entry point closes the scope and
 * takes everything down at once, on the frame thread (UI entry, refill call, the HUD's
 * place: present_restored=1), and the session stops (162, why=<entry>_fault:<rva>).
 * present_fault= has the address, access, registers and the unwound call chain.
 * The soft target (+0x6dd0): what client k selects stays in the scope from then on (its HUD shows
 * it) and rides the record (a per-slot reader can see it); it does not reach P_k or any other
 * client's simulation yet, as slot 1's target stays local on client 1 (targets=). With the UI replay's
 * target family it is P_k's own, replayed on every client (lockstep_ui_replay.h), and nothing here keeps it.
 * Not yet: other world-changing UI actions as recorded events (refused, counted), Player::Move's
 * camera rule for external views outside slot 1's sector. */
#define PRES_STOP_REASON 162u
#define PRES_UI_ENTRY 0x2bc07d8u        /* UI::CUIController, XVbiHandler vtable 0x2bc07d0 slot 1 */
#define PRES_UI_MOVE 0xb3bad0u
#define PRES_UI_LOCK 0x70u              /* its critical section: entered at the top, left at the bottom */
#define PRES_REFILL_CALL 0x9fe573u      /* UniverseFrameUpdate: call 0x979ce0 for each listed camera */
#define PRES_REFILL 0x979ce0u
#define PRES_PICK_CALL 0x9fe5d0u        /* UniverseFrameUpdate: call 0x717ed0 with the player */
#define PRES_PICK 0x717ed0u
#define PRES_PICK_STUB 0x60u            /* on the slots' near page, after the refill's stub */
#define PRES_ATTENTION 0x1ecu           /* Component: attention */
#define PRES_VIEW_ATTENTION 0x118u      /* Component: its published attention, +bank*0x80 */
#define PRES_ROOM_ATTENTION 11
#define PRES_DIFF_SHIP 0xc00u
#define PRES_DIFF_ROOM 0x400u
#define PRES_UI_ANCHORS 0x8e8450u       /* Universe: connects the UI anchor to the player's cockpit room */
#define PRES_UI_ANCHOR 0x470u           /* Universe: the anchor, +0x478 the cutscene one */
#define PRES_PARENT 0x70u               /* Component */
#define PRES_VECTOR 0x308u              /* Player: controlled-object vector (begin, end, capacity) */
#define PRES_POSITION 0x6ab8u           /* Player: control position */
#define PRES_TARGET 0x6dd0u             /* Player: soft target */
#define PRES_VIEW_CACHE 0xe0u           /* Component: view cache [A] at +0xe0+A*0x80, +0 the published parent */
#define PRES_VIEW_STRIDE 0x80u
#define PRES_VIEW_INDEX 0x38f4ee0u      /* the bank this thread reads (0x3de630, 0x3d91e0: TLS flag set on the MoveWorker) */
#define PRES_OCCUPIED 0xb05e30u         /* GetPlayerOccupiedShipID */
#define PRES_CONTROLLED 0x1e9bd0u       /* GetPlayerControlledShipID */
#define PRES_ZONE 0x1a4f90u             /* GetPlayerZoneID */
#define PRES_ANCESTOR 0x3d91e0u         /* the view-cache ancestor lookup (component, out, class, 0: from the published parent) */
#define PRES_STUB_OFFSET 0x40u          /* on the slots' near page, after the d95180 jump */
#define PRES_CAMERA 0x3d0u              /* Universe: the local camera */
#define PRES_MODE 0x868u
#define PRES_ID 8u                      /* Component: its runtime ID */
#define PRES_CHAIN_MAX 24u
typedef struct {
    void (*ui_move)(void *);
    void (*refill)(void *);
    void (*anchors)(void *);
    uint64_t (*occupied)(void);
    uint64_t (*controlled)(void);
    uint64_t (*zone)(void);
    void **(*ancestor)(void *,void **,int,char);
    void (*pick)(void *);
} pres_fns;
static pres_fns pres_fn;
static const struct {unsigned rva,size;const char *sha;} pres_pin_table[]={
    {PRES_UI_MOVE,206,"87a23ed23ef4be0fdf493a983342282860054eb3eb14f2388e134abc88762261"},
    {PRES_REFILL,221,"dbcedb05d414d1b42c092b8ac98f15ad81442e524a020496c4a59bf94150c1e7"},
    {PRES_UI_ANCHORS,2105,"edf2e6fd86b091a01ce37af61a99dcf409422b2cbda5e1d7738804c54d6f9393"},
    {PRES_OCCUPIED,75,"68bd4e0d9276b98dcfab77a1cc25a1203c5de68b1411dd3a96abf6970d90e1e0"},
    {PRES_CONTROLLED,36,"86c093f6d8e6afadfd95d3f6749744d32d24febedde94f4996bdec057d192d39"},
    {PRES_ZONE,105,"e30fedfabb28e3bb89c200b429c417cce27329e4d30db7e5cbf6406dd9bd1e0f"},
    {PRES_ANCESTOR,219,"a77acbafe216c2d105ceb10605dbdcdb13f50fe7862e09ec3944a601fe38a45a"},
    {PRES_PICK,3710,"46342dd30a7245bf93a0634836f03a0d9a106fe1b5ebef736f2002223375262d"},
    {0x9fe55a,0x32,"e1658b86f806df2d83e4129e52a4d59715d886d7bcc7ed7b686b420a94d709b3"}}; /* the refill loop */
#define PRES_PIN_COUNT (sizeof(pres_pin_table)/sizeof(*pres_pin_table))
static const unsigned char pres_refill_bytes[5]={0xe8,0x68,0xb7,0xf7,0xff}; /* call 0x979ce0 from 0x9fe578 */
static const unsigned char pres_pick_bytes[5]={0xe8,0xfb,0x98,0xd1,0xff};   /* call 0x717ed0 from 0x9fe5d5 */
static int pres_armed,pres_active,pres_paused,pres_ui_installed,pres_refill_installed,pres_pick_installed,pres_pins_checked,pres_hud,pres_anchor_moved;
static int pres_room_attention_saved,pres_room_view_attention_saved,pres_diff_ready;
static char pres_diff_text[3584];
static int pres_restored=1;
static unsigned pres_slot;
static const char *pres_stage="off",*pres_why="-",*pres_refused_region="-";
static char pres_fault_text[48],pres_fault_detail[640];
static unsigned char pres_saved[VPL_BLOCK_MAX],pres_player_saved[0x40],*pres_camera;
static unsigned char *pres_parent_saved,*pres_view_parent_saved,*pres_view_at,*pres_vector_saved[3];
static int pres_position_saved,pres_target_set;
static uint64_t pres_local_target;
static volatile LONG64 pres_targets;
static unsigned char pres_site_patched[5],pres_pick_patched[5];
static uint64_t pres_started_tick,pres_asked_ship,pres_asked_controlled,pres_asked_zone;
static volatile LONG64 pres_scopes,pres_ui,pres_render,pres_local,pres_refills,pres_picks,pres_refused,pres_refused_player,pres_faults,pres_skipped,pres_anchor_moves,pres_rng_moved;
/* This client's own calls must not move the thread's simulation stream. */
static uint64_t *pres_rng_hold(uint64_t *word) {
    uint64_t *rng=vps_rng_word?vps_rng_word():NULL;
    *word=rng?*rng:0;return rng;
}
static void pres_rng_back(uint64_t *rng,uint64_t word) {
    if(rng && *rng!=word) {InterlockedIncrement64(&pres_rng_moved);*rng=word;}
}
static int pres_pins(void) {
    if(!pres_pins_checked) {
        int ok=!memcmp(image+PRES_REFILL_CALL,pres_refill_bytes,5);
        if(!ok) pin_failed_rva=PRES_REFILL_CALL;
        if(ok && memcmp(image+PRES_PICK_CALL,pres_pick_bytes,5)) {ok=0;pin_failed_rva=PRES_PICK_CALL;}
        for(unsigned i=0;ok && i<PRES_PIN_COUNT;i++)
            if(!(ok=hash_ok(image+pres_pin_table[i].rva,pres_pin_table[i].size,pres_pin_table[i].sha))) pin_failed_rva=pres_pin_table[i].rva;
        if(ok && *(void**)(image+PRES_UI_ENTRY)!=(void*)(image+PRES_UI_MOVE)) {ok=0;pin_failed_rva=PRES_UI_ENTRY;}
        pres_pins_checked=ok?1:-1;
    }
    return pres_pins_checked==1;
}
static void pres_use_image(void) {
    pres_fn.ui_move=(void(*)(void*))(image+PRES_UI_MOVE);pres_fn.refill=(void(*)(void*))(image+PRES_REFILL);
    pres_fn.anchors=(void(*)(void*))(image+PRES_UI_ANCHORS);
    pres_fn.occupied=(uint64_t(*)(void))(image+PRES_OCCUPIED);pres_fn.controlled=(uint64_t(*)(void))(image+PRES_CONTROLLED);
    pres_fn.zone=(uint64_t(*)(void))(image+PRES_ZONE);
    pres_fn.ancestor=(void**(*)(void*,void**,int,char))(image+PRES_ANCESTOR);
    pres_fn.pick=(void(*)(void*))(image+PRES_PICK);
}
/* ---- the presentation scope ---- */
static void pres_stop_presenting(const char *why) {
    pres_active=0;pres_stage="failed";
    if(!strcmp(pres_why,"-")) pres_why=why;
    pres_failed=1;pnat_async_fail(why);
}
static int pres_has_seat(void) {return pslots[pres_slot].seated;}
static int pres_enter(void) {
    pslot_slot *s=&pslots[pres_slot];unsigned char *universe=NULL,*now=NULL,*p1=pslot_p1;unsigned n=0,p=0,bank=0;
    if(!pres_active) return 0;
    /* The seat's ship went earlier in this frame (lockstep_pilot_death.h): the room may be gone, the next pass ends the seat. */
    if(pdeath_armed && s->seated && (!pdeath_find || pdeath_find(pdeath_map,s->runtime,4)!=s->ship)) {InterlockedIncrement64(&pres_skipped);return 0;}
    if(pres_open || pstk_scope_slot || GetCurrentThreadId()!=ui_thread || !s->seated || !p1 || !vpl_reentry_clear(pnat_base) ||
       !read_local(pnat_base+VPL_UNIVERSE,&universe,8) || !universe || !read_local(universe+VPS_SINGLETON,&now,8) || now!=p1 ||
       !read_local(pnat_base+PRES_VIEW_INDEX,&bank,4) || bank>1) {
        InterlockedIncrement64(&pres_skipped);return 0;
    }
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) {
        const vpl_region *r=&vpl_regions[i];
        if(r->base==VPL_BASE_PLAYER) {
            if(r->cls==VPL_SIM) {
                memcpy(pres_player_saved+p,p1+r->offset,r->size);memcpy(p1+r->offset,s->player+r->offset,r->size);p+=r->size;
            }
            continue;
        }
        if(!(PSLOT_CLASSES>>r->cls&1u)) continue;
        if(r->cls!=VPL_LOCAL) {
            memcpy(pres_saved+n,(const void*)(pnat_base+r->offset),r->size);
            memcpy((void*)(pnat_base+r->offset),s->store+n,r->size);
        }
        n+=r->size;
    }
    if(pres_target_set) memcpy(p1+PRES_TARGET,&pres_local_target,8);
    /* The player's identity: upward pointers only, its children and the room's child list are as they were. */
    memcpy(&pres_position_saved,p1+PRES_POSITION,4);memcpy(p1+PRES_POSITION,s->player+PRES_POSITION,4);
    memcpy(pres_vector_saved,p1+PRES_VECTOR,sizeof(pres_vector_saved));memcpy(p1+PRES_VECTOR,s->player+PRES_VECTOR,sizeof(pres_vector_saved));
    pres_parent_saved=*(unsigned char *volatile*)(p1+PRES_PARENT);*(unsigned char *volatile*)(p1+PRES_PARENT)=s->room;
    pres_view_at=p1+PRES_VIEW_CACHE+bank*PRES_VIEW_STRIDE;
    pres_view_parent_saved=*(unsigned char *volatile*)pres_view_at;*(unsigned char *volatile*)pres_view_at=s->room;
    /* The room as the player's room: only when it is a room (a ship without one keeps its attention). */
    if(!s->roomless) {
        memcpy(&pres_room_attention_saved,s->room+PRES_ATTENTION,4);
        memcpy(&pres_room_view_attention_saved,s->room+PRES_VIEW_ATTENTION+bank*PRES_VIEW_STRIDE,4);
        *(volatile int*)(s->room+PRES_ATTENTION)=PRES_ROOM_ATTENTION;
        *(volatile int*)(s->room+PRES_VIEW_ATTENTION+bank*PRES_VIEW_STRIDE)=PRES_ROOM_ATTENTION;
    }
    InterlockedExchange(&pres_open,1);InterlockedIncrement64(&pres_scopes);
    return 1;
}
static void pres_leave(void) {
    pslot_slot *s=&pslots[pres_slot];unsigned char *p1=pslot_p1;unsigned n=0,p=0;
    if(!pres_open) return;
    if(!s->roomless) {
        size_t bank=(size_t)(pres_view_at-(p1+PRES_VIEW_CACHE));
        *(volatile int*)(s->room+PRES_ATTENTION)=pres_room_attention_saved;
        *(volatile int*)(s->room+PRES_VIEW_ATTENTION+bank)=pres_room_view_attention_saved;
    }
    *(unsigned char *volatile*)pres_view_at=pres_view_parent_saved;
    *(unsigned char *volatile*)(p1+PRES_PARENT)=pres_parent_saved;
    memcpy(p1+PRES_VECTOR,pres_vector_saved,sizeof(pres_vector_saved));
    memcpy(p1+PRES_POSITION,&pres_position_saved,4);
    {   uint64_t now=0,was=0;
        memcpy(&now,p1+PRES_TARGET,8);
        if(pres_target_set) was=pres_local_target;else memcpy(&was,s->player+PRES_TARGET,8);
        if(now!=was && !(uirep_families&UIREP_FAMILY_TARGET)) {pres_local_target=now;pres_target_set=1;pview_slot_target=now;InterlockedIncrement64(&pres_targets);}
        memcpy(p1+PRES_TARGET,s->player+PRES_TARGET,8);   /* not a refused write: the player's own value goes back below */
    }
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) {
        const vpl_region *r=&vpl_regions[i];
        if(r->base==VPL_BASE_PLAYER) {
            if(r->cls==VPL_SIM) {
                if(memcmp(p1+r->offset,s->player+r->offset,r->size)) {InterlockedIncrement64(&pres_refused_player);pres_refused_region=r->name;}
                memcpy(p1+r->offset,pres_player_saved+p,r->size);
                p+=r->size;
            }
            continue;
        }
        if(!(PSLOT_CLASSES>>r->cls&1u)) continue;
        if(r->cls!=VPL_LOCAL) {
            if(r->cls==VPL_SIM && memcmp((const void*)(pnat_base+r->offset),s->store+n,r->size)) {
                InterlockedIncrement64(&pres_refused);pres_refused_region=r->name;
            }
            memcpy((void*)(pnat_base+r->offset),pres_saved+n,r->size);
        }
        n+=r->size;
    }
    InterlockedExchange(&pres_open,0);
}
/* ---- a fault: what it was and how the code got there ---- */
static void pres_fault_describe(const EXCEPTION_POINTERS *info) {
    const CONTEXT *c=info->ContextRecord;const EXCEPTION_RECORD *e=info->ExceptionRecord;
    uintptr_t base=(uintptr_t)image,at=(uintptr_t)e->ExceptionAddress;
    int n=snprintf(pres_fault_detail,sizeof(pres_fault_detail),"addr=%llx write=%llu rcx=%llx rdx=%llx rbx=%llx rsi=%llx rdi=%llx r8=%llx r9=%llx chain=",
        e->NumberParameters>1?(unsigned long long)e->ExceptionInformation[1]:0ull,e->NumberParameters>0?(unsigned long long)e->ExceptionInformation[0]:0ull,
        (unsigned long long)c->Rcx,(unsigned long long)c->Rdx,(unsigned long long)c->Rbx,(unsigned long long)c->Rsi,(unsigned long long)c->Rdi,
        (unsigned long long)c->R8,(unsigned long long)c->R9);
    CONTEXT walk=*c;
    (void)at;
    for(unsigned frame=0;frame<PRES_CHAIN_MAX && n>0 && (size_t)n<sizeof(pres_fault_detail) && walk.Rip;frame++) {
        uintptr_t rip=(uintptr_t)walk.Rip;
        int more=rip>=base && rip-base<VPL_IMAGE_SPAN?snprintf(pres_fault_detail+n,sizeof(pres_fault_detail)-(size_t)n,"%s%llx",frame?">":"",(unsigned long long)(rip-base)):
            snprintf(pres_fault_detail+n,sizeof(pres_fault_detail)-(size_t)n,"%s@%llx",frame?">":"",(unsigned long long)rip);
        if(more<0) break;
        n+=more;
        DWORD64 module=0;PRUNTIME_FUNCTION fn=RtlLookupFunctionEntry(walk.Rip,&module,NULL);
        if(fn) {
            PVOID handler=NULL;DWORD64 frame_base=0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER,module,walk.Rip,fn,&walk,&handler,&frame_base,NULL);
        } else {
            DWORD64 back=0;
            if(!read_local((const void*)(uintptr_t)walk.Rsp,&back,8)) break;
            walk.Rip=back;walk.Rsp+=8;
        }
    }
}
static int pres_fault_filter(EXCEPTION_POINTERS *info,const char *entry) {
    if(info->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t at=(uintptr_t)info->ExceptionRecord->ExceptionAddress,base=(uintptr_t)image;
    snprintf(pres_fault_text,sizeof(pres_fault_text),"%s_fault:%llx",entry,(unsigned long long)(at>=base && at-base<VPL_IMAGE_SPAN?at-base:at));
    pres_fault_describe(info);
    return EXCEPTION_EXECUTE_HANDLER;
}
static void pres_stop(void);
/* After a fault nothing presents any more: every byte back now, on this thread. */
static void pres_fault_stop(void) {
    InterlockedIncrement64(&pres_faults);
    pres_stop_presenting(pres_fault_text);
    pres_leave();pres_paused=0;
    pres_stop();
}
/* ---- entry points ---- */
static void pres_ui_move(void *self) {
    if(!pres_enter()) {pres_fn.ui_move(self);return;}
    InterlockedIncrement64(&pres_ui);
    __try {pres_fn.ui_move(self);pres_leave();pres_paused=0;}
    __except(pres_fault_filter(GetExceptionInformation(),"ui")) {
        pres_fault_stop();
        /* The fault skipped X4's LeaveCriticalSection at the bottom of the entry. */
        CRITICAL_SECTION *lock=(CRITICAL_SECTION*)((unsigned char*)self+PRES_UI_LOCK);
        if(lock->OwningThread==(HANDLE)(uintptr_t)GetCurrentThreadId()) LeaveCriticalSection(lock);
    }
}
static void pres_render_call(render_rng_fn original,void *self) {
    if(!pres_enter()) {original(self);return;}
    InterlockedIncrement64(&pres_render);
    __try {original(self);pres_leave();}
    __except(pres_fault_filter(GetExceptionInformation(),"render")) {pres_fault_stop();}
}
static int pres_local_enter(void) {
    if(!pres_enter()) return 0;
    InterlockedIncrement64(&pres_local);return 1;
}
static void pres_local_leave(int entered) {if(entered) pres_leave();}
static void pres_refill_call(void *camera) {
    if(camera!=(void*)pres_camera || !pres_enter()) {pres_fn.refill(camera);return;}
    InterlockedIncrement64(&pres_refills);
    __try {pres_fn.refill(camera);pres_leave();}
    __except(pres_fault_filter(GetExceptionInformation(),"refill")) {pres_fault_stop();}
}
static void pres_pick_call(void *player) {
    if(!pres_enter()) {pres_fn.pick(player);return;}
    InterlockedIncrement64(&pres_picks);
    __try {pres_fn.pick(player);pres_leave();}
    __except(pres_fault_filter(GetExceptionInformation(),"pick")) {pres_fault_stop();}
}
/* gate.pilot_present_pause / resume: the probe's own frame runs canonical inside slot 6. */
static int pres_pause(void) {
    if(!pres_open || GetCurrentThreadId()!=ui_thread) return 0;
    pres_leave();pres_paused=1;return 1;
}
static void pres_resume(void) {
    if(pres_paused && !pres_open) {pres_paused=0;pres_enter();}
}
/* ---- the HUD's place ---- */
/* Under the player (camera_anchor_scene in a save, or the player itself when the ship has no room) or in
 * the room: the anchor follows the scope without a connection. */
static int pres_anchor_placed(unsigned char *universe,const pslot_slot *s) {
    for(unsigned i=0;i<2;i++) {
        unsigned char *at=NULL;
        if(!read_local(universe+PRES_UI_ANCHOR+8*i,&at,8) || !at) continue;
        for(unsigned step=0;at && step<4;step++) {
            if(!read_local(at+PRES_PARENT,&at,8)) break;
            if(at==s->room || (at && at==pslot_p1)) return 1;
        }
    }
    return 0;
}
/* In the pass: X4 moved it back for slot 1's player. The caller is outside any scope. */
static int pres_anchor_check(void) {
    pslot_slot *s=&pslots[pres_slot];unsigned char *universe=NULL;
    if(!pres_hud || !read_local(pnat_base+VPL_UNIVERSE,&universe,8) || !universe || pres_anchor_placed(universe,s)) return 1;
    if(!pres_enter()) return 1;   /* the next pass */
    int fault=0;uint64_t word=0,*rng=pres_rng_hold(&word);
    __try {pres_fn.anchors(universe);InterlockedIncrement64(&pres_anchor_moves);pres_anchor_moved=1;}
    __except(pres_fault_filter(GetExceptionInformation(),"anchors")) {fault=1;InterlockedIncrement64(&pres_faults);}
    pres_rng_back(rng,word);
    pres_leave();
    return !fault;
}
/* ---- start and stop (frame thread, pass) ---- */
static int pres_fail(const char *why) {pres_stage="failed";if(!strcmp(pres_why,"-")) pres_why=why;pres_failed=1;return 0;}
static int pres_entry_swap(void *expected,void *value) {
    return pnat_swap8(pnat_base+PRES_UI_ENTRY,expected,value,1);
}
/* A call in UniverseFrameUpdate re-pointed through the slots' near page. */
static int pres_site_install(unsigned site,unsigned target,unsigned stub_offset,void *wrapper,unsigned char patched[5]) {
    int32_t rel=(int32_t)((intptr_t)(pnat_base+target)-(intptr_t)(pnat_base+site+5));
    unsigned char original[5]={0xe8,0,0,0,0};memcpy(original+1,&rel,4);
    if(memcmp(pnat_base+site,original,5) || !pslot_page) return 0;
    DWORD old,ignored;unsigned char *stub=pslot_page+stub_offset;
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_READWRITE,&old)) return 0;
    pview_abs_jump(stub,wrapper);
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) return 0;
    FlushInstructionCache(GetCurrentProcess(),pslot_page,PVIEW_PAGE);
    rel=(int32_t)((intptr_t)stub-(intptr_t)(pnat_base+site+5));
    patched[0]=0xe8;memcpy(patched+1,&rel,4);
    return pview_cas(site,original,patched,5);
}
static int pres_site_uninstall(unsigned site,unsigned target,const unsigned char patched[5]) {
    int32_t rel=(int32_t)((intptr_t)(pnat_base+target)-(intptr_t)(pnat_base+site+5));
    unsigned char original[5]={0xe8,0,0,0,0};memcpy(original+1,&rel,4);
    return pview_cas(site,patched,original,5);
}
static int pres_refill_install(void) {
    if(!pres_site_install(PRES_REFILL_CALL,PRES_REFILL,PRES_STUB_OFFSET,(void*)pres_refill_call,pres_site_patched)) return 0;
    pres_refill_installed=1;return 1;
}
static void pres_refill_uninstall(void) {
    if(pres_refill_installed && pres_site_uninstall(PRES_REFILL_CALL,PRES_REFILL,pres_site_patched)) pres_refill_installed=0;
}
static int pres_pick_install(void) {
    if(!pres_site_install(PRES_PICK_CALL,PRES_PICK,PRES_PICK_STUB,(void*)pres_pick_call,pres_pick_patched)) return 0;
    pres_pick_installed=1;return 1;
}
static void pres_pick_uninstall(void) {
    if(pres_pick_installed && pres_site_uninstall(PRES_PICK_CALL,PRES_PICK,pres_pick_patched)) pres_pick_installed=0;
}
/* What a player's presence set on a ship and its room: the words of slot 1's and slot k's where one is
 * zero or both are small and differ (pointers and floats that merely differ say nothing). */
static int pres_diff_words(char *out,size_t capacity,int n,const char *name,const unsigned char *a,const unsigned char *b,unsigned size) {
    static unsigned char left[PRES_DIFF_SHIP],right[PRES_DIFF_SHIP];
    if(size>PRES_DIFF_SHIP || !read_local(a,left,size) || !read_local(b,right,size)) return n;
    for(unsigned at=0;at+8<=size && n>0 && (size_t)n+40<capacity;at+=8) {
        uint64_t x,y;memcpy(&x,left+at,8);memcpy(&y,right+at,8);
        if(x==y || !((!x)!=(!y) || (x<0x10000 && y<0x10000))) continue;
        int more=snprintf(out+n,capacity-(size_t)n," %s+%x:%llx/%llx",name,at,(unsigned long long)x,(unsigned long long)y);
        if(more<0) break;
        n+=more;
    }
    return n;
}
static void pres_diff_build(const pslot_slot *s) {
    unsigned char *ship1=pilot_slots[0].object,*room1=NULL;
    int n=snprintf(pres_diff_text,sizeof(pres_diff_text),"slot=%u",pres_slot);
    if(!ship1 || !read_local(ship1+PSLOT_ROOM,&room1,8)) return;
    n=pres_diff_words(pres_diff_text,sizeof(pres_diff_text),n,"ship",ship1,s->ship,PRES_DIFF_SHIP);
    if(room1 && !s->roomless) pres_diff_words(pres_diff_text,sizeof(pres_diff_text),n,"room",room1,s->room,PRES_DIFF_ROOM);
    pres_diff_ready=1;
}
/* The UI entry and the re-pointed calls back (compare-and-swap, from any thread once no scope is open). */
static void pres_unhook(void) {
    pres_refill_uninstall();pres_pick_uninstall();
    if(pres_ui_installed && pres_entry_swap((void*)pres_ui_move,(void*)pres_fn.ui_move)) pres_ui_installed=0;
    pres_restored=!pres_ui_installed && !pres_refill_installed && !pres_pick_installed;
}
static int pres_start(uint64_t tick) {
    pslot_slot *s=&pslots[pres_slot];unsigned char *universe=NULL;uint64_t want=0;
    if(!s->seated || !s->player) return pres_fail("slot_not_seated");
    if(!read_local(pnat_base+VPL_UNIVERSE,&universe,8) || !universe || !read_local(universe+PRES_CAMERA,&pres_camera,8) || !pres_camera) return pres_fail("no_camera");
    if(!read_local(s->ship+PRES_ID,&want,8) || !want) return pres_fail("ship_id");
    pres_restored=0;
    if(!pres_entry_swap((void*)pres_fn.ui_move,(void*)pres_ui_move)) return pres_fail("ui_entry");
    pres_ui_installed=1;
    if(!pres_refill_install()) return pres_fail("refill_site");
    if(!pres_pick_install()) return pres_fail("pick_site");
    pres_diff_build(s);
    pres_active=1;
    if(!pres_enter()) {pres_active=0;return pres_fail("start_scope");}
    int fault=0;uint64_t word=0,*rng=pres_rng_hold(&word);void *up=NULL;
    __try {
        /* X4 itself says whose ship this is now, before any UI runs in the scope. */
        pres_asked_ship=pres_fn.occupied();pres_asked_controlled=pres_fn.controlled();pres_asked_zone=pres_fn.zone();
        if(!s->roomless) {
            pres_fn.ancestor(pslot_p1,&up,PSLOT_ROOM_CLASS,0);
            if(up) InterlockedDecrement((volatile LONG*)((unsigned char*)up+0x40));   /* the lookup's reference */
        }
        if(pres_hud && pres_asked_ship==want && !pres_anchor_placed(universe,s)) {pres_fn.anchors(universe);InterlockedIncrement64(&pres_anchor_moves);pres_anchor_moved=1;}
    }
    __except(pres_fault_filter(GetExceptionInformation(),"start")) {fault=1;InterlockedIncrement64(&pres_faults);}
    pres_rng_back(rng,word);
    pres_leave();
    if(fault) {pres_active=0;return pres_fail(pres_fault_text);}
    if(pres_asked_ship!=want) {pres_active=0;return pres_fail("identity_occupied");}
    if(!s->roomless && up!=(void*)s->room) {pres_active=0;return pres_fail("identity_view_parent");}
    pres_started_tick=tick;pres_stage="presenting";
    return 1;
}
/* The HUD's place back with the canonical player (the end of presenting, or the slot's ship died). */
static void pres_anchor_home(void) {
    if(!pres_anchor_moved) return;
    uint64_t word=0,*rng=pres_rng_hold(&word);unsigned char *universe=NULL;
    pres_anchor_moved=0;
    __try {if(read_local(pnat_base+VPL_UNIVERSE,&universe,8) && universe) pres_fn.anchors(universe);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {InterlockedIncrement64(&pres_faults);}
    pres_rng_back(rng,word);
}
/* Everything back, as the canonical player, on the frame thread. */
static void pres_stop(void) {
    if(pres_open) pres_leave();
    pres_active=0;
    pres_anchor_home();
    pres_camera=NULL;pres_target_set=0;pres_local_target=0;pview_slot_target=0;
    pres_unhook();
    if(strcmp(pres_stage,"failed")) pres_stage="stopped";
}
/* The presentation's part of a pass (pnat_pass, every tick, before the slots' part). */
static int pres_pass(uint64_t tick) {
    if(!pres_armed || !pres_slot) return 1;
    if(pres_failed) return pres_fail(pres_why);
    if(tick==pilot_start) return pres_start(tick);
    if(tick>pilot_start && tick<=pilot_stop) {
        if(pres_open) return pres_fail("scope_left_open");
        if(*(void*volatile*)(pnat_base+PRES_UI_ENTRY)!=(void*)pres_ui_move) return pres_fail("ui_entry_lost");
        if(memcmp(pnat_base+PRES_REFILL_CALL,pres_site_patched,5)) return pres_fail("refill_site_lost");
        if(memcmp(pnat_base+PRES_PICK_CALL,pres_pick_patched,5)) return pres_fail("pick_site_lost");
        if(!pres_anchor_check()) return pres_fail(pres_fault_text);
        return 1;
    }
    if(tick==pilot_stop+1) pres_stop();
    return 1;
}
/* Arming (Lua, before the first tick, after gate.pilot_stock): client 1 presents slot 1 as X4 always does. */
static int pres_arm_using(int hud) {
    if(pres_armed) return pres_hud==hud;
    pres_hud=hud;
    if(!pnat_armed || pnat_mode!=PNAT_MODE_STOCK) return pstk_arm_refuse("present_without_stock");
    pres_slot=stats.slot>=2 && (pstk_slot_mask>>(stats.slot-1)&1u)?stats.slot:0;
    if(stats.slot>=2 && !pres_slot) return pstk_arm_refuse("present_slot_not_stock");
    if(pres_slot && !pslot_armed) return pstk_arm_refuse("present_without_slots");
    if(pres_slot && !render_rng_installed) return pstk_arm_refuse("present_without_render_split");
    if(pres_slot && (!pres_fn.ui_move || !pres_fn.refill || !pres_fn.anchors || !pres_fn.occupied || !pres_fn.controlled || !pres_fn.zone || !pres_fn.ancestor || !pres_fn.pick))
        return pstk_arm_refuse("present_functions");
    pres_failed=0;pres_active=0;pres_stage=pres_slot?"armed":"own_player";pres_why="-";pres_refused_region="-";pres_fault_detail[0]=0;pres_restored=1;pres_anchor_moved=0;
    pres_armed=1;return 1;
}
static int pres_arm(int hud) {
    if(!pres_armed && stats.issued) return pstk_arm_refuse("present_after_first_tick");
    if(!pres_armed && stats.slot>=2) {
        if(!pres_pins()) {snprintf(pstk_arm_why,sizeof(pstk_arm_why),"pins_present:%x",pin_failed_rva);return 0;}
        pres_use_image();
    }
    return pres_arm_using(hud);
}
static void pres_restore(void) {
    if(!pres_armed || !pres_slot) return;
    if(GetCurrentThreadId()==ui_thread && !pstk_scope_slot) {pres_stop();return;}
    /* Off the frame thread (a stop at the session's end): nothing presents from here on, the entry and
     * the call go back once the frame thread is out of its scope. The anchor stays where it is: only
     * the frame thread may connect it, and the session is over. */
    pres_active=0;
    for(unsigned i=0;i<200 && pres_open;i++) Sleep(1);
    if(pres_open) {if(strcmp(pres_stage,"failed")) pres_stage="left_open";return;}
    pres_unhook();
    if(strcmp(pres_stage,"failed")) pres_stage=pres_anchor_moved?"stopped_anchor_left":"stopped";
}
/* gate.pilot_present_diff: the text once, nil before and after. */
static const char *pres_diff_take(void) {
    if(!pres_diff_ready) return NULL;
    pres_diff_ready=0;return pres_diff_text;
}
static int pres_status(char *out,size_t capacity) {
    int mode=-1;unsigned char *universe=NULL,*camera=NULL;
    if(read_local(pnat_base+VPL_UNIVERSE,&universe,8) && universe && read_local(universe+PRES_CAMERA,&camera,8) && camera) read_local(camera+PRES_MODE,&mode,4);
    int n=snprintf(out,capacity," present=%s present_why=%s present_slot=%u scopes=%lld ui=%lld render=%lld local=%lld refills=%lld picks=%lld skipped=%lld "
        "refused=%lld/%lld/%s targets=%lld/%llx faults=%lld camera_mode=%d asks=%llx/%llx/%llx hud=%d/%lld rng_moved=%lld present_restored=%d%s%s",pres_stage,pres_why,pres_slot,(long long)pres_scopes,(long long)pres_ui,
        (long long)pres_render,(long long)pres_local,(long long)pres_refills,(long long)pres_picks,(long long)pres_skipped,(long long)pres_refused,(long long)pres_refused_player,
        pres_refused_region,(long long)pres_targets,(unsigned long long)pres_local_target,(long long)pres_faults,mode,(unsigned long long)pres_asked_ship,(unsigned long long)pres_asked_controlled,(unsigned long long)pres_asked_zone,
        pres_hud,(long long)pres_anchor_moves,(long long)pres_rng_moved,pres_restored,pres_fault_detail[0]?" present_fault=":"",pres_fault_detail);
    return n>0 && (size_t)n<capacity?n:0;
}
