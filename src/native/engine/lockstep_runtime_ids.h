/* Determinism fix, DEFAULT OFF: the same runtime component IDs on every client
 * (native_gate_runtime_ids = a slot number, true for the default, or 'observe').
 *
 * The allocator. Every XLib::IDBase<U::UniverseID> takes its ID from one
 * AutoIDMap (g_UniverseIDMap 0x6cf1500, built by the GameClient constructor):
 * AssignNewID 0xcc220 pops the head of a FIFO free list, under the map's critical
 * section (+0x10), on whatever thread constructs the object. An ID is
 * generation16<<25 | slot (1-based, 25 bits). Slots live in blocks of 2^20 (32
 * pointers at +0x58, 3 slots per 32-byte bucket: free slot = prev | next<<32,
 * used slot = the object; +0x18 the 16-bit generations, +0x1c bit 16+4*i used).
 * Map fields: +0x38 inserts, +0x40 live, +0x44 free, +0x48 peak, +0x4c highest
 * generation, +0x50 head, +0x54 tail. EnsureFreeSlots 0xcc670 appends the next
 * block at the tail when free < 0x19000. Remove 0xcbe70 bumps the slot's
 * generation and appends it at the tail, so slots come back only after the list
 * in front of them is used up: head is a plain counter until block 0 runs out,
 * then the slots freed so far, then block 1.
 *
 * Why IDs differ. Components are not alone: event listeners and sources,
 * connections, junctions, physics shapes, movement controllers, animation
 * listeners, UI message sources and Lua notifications all take IDs from this
 * map (96 direct callers). Allocations before the gate arms vary per process;
 * from the arming on their count is equal (created identities: calls equal at
 * tick 1 in every run). So every saved object sits at one per-client offset
 * (st3-P15M: 77 objects, runtime 0x39af..0x7f974, offsets +5 and -23). Objects
 * made after the load have offset 0 while the simulation is in step
 * (sc-highway94a, full-G8 to its divergence): the slots freed before the load are
 * reused before block 1, which cancels the same pre-load count. Anything seeded,
 * hashed or ordered by +8 therefore parts on saved objects.
 *
 * Set mode, called on the UI thread right before LoadGame. Under the critical
 * section, after checking that the free list has the shape EnsureFreeSlots builds
 * (head to the end of block 0 consecutive, unused, generation 0):
 *  - the head moves forward to the agreed slot; the skipped slots stay unused;
 *  - the list is cut after block 0's last slot, so the slots freed so far, whose
 *    numbers and count are per process, never come back.
 * Remove's entry is wrapped: every slot of this list goes off the tail again as
 * soon as it is freed. Nothing is ever recycled, so the head is a plain counter
 * over the blocks EnsureFreeSlots appends in order: neither a per-process slot from
 * before this call nor the order objects die in (frees land on different ticks in
 * the first ticks, sc-rtid99a/b) can reach a later ID. Together: every slot this
 * list can hand out is the same on every client, in the same order, as long as
 * allocations are. No client's objects from before the call change. The price is
 * 11 MB per 2^20 allocations (play: about 16 a tick) and a limit: the head entering
 * block 29 stops the gate (ids_exhausted), about 1.8 million ticks after the load,
 * while EnsureFreeSlots still has room, so the limit fails loud instead of an
 * allocation failing inside X4.
 *
 * What it cannot do. A creation on one client only moves every later ID on that
 * client for good; two threads allocating in a different order swap IDs without
 * moving the head. Nothing here repairs that, it reports it: at each gate boundary
 * (MoveWorker and passes joined) the ID the next allocation would get is read
 * (status next=, gate.runtime_ids_word; with native_gate_runtime_ids_barrier it
 * joins the declared state, so the relay's mismatches are the ticks it differs)
 * and folds into a rolling digest that keeps any difference for good (digest=).
 * Only that ID counts: what happened before the load (inserts, skipped,
 * head_before) differs per process by design, and frees (tail, free, live) do not
 * move it. IDs of live objects are compared through the saved map digest at the
 * seal and the census.
 *
 * Local allocations, native_gate_runtime_ids_local (set mode, created identities
 * hook). play23-soak, client 2 camera_anchor 'own': the next ID parts from tick 36
 * on, and of 179 play-phase stacks exactly one differs: U::UIElement through
 * GetNewUIElement, CreateInstance 0xaf0ec0, the factory 0x8e0250 and its constructor
 * 0x80d420 (47 against 21 at tick 6000). A view object takes a shared slot and moves
 * every later simulation ID. So after the seal two more lists hand out what only
 * the view asks for, from blocks that are the map's own:
 *   block 31, ui:   U::UIElement, U::HoloMap, U::CutsceneAnchor, the Lua::NotifyOn*
 *                   listeners, the cockpit animation listeners, Modules::StartMenu;
 *                   whatever slot 6 (UI::CUIController::Move, except the probe's own
 *                   frame, gate.runtime_ids_probe) and slot 10 (GameRender::Move: its
 *                   render walk spawns effects by what the camera sees) make; everything
 *                   inside a presentation scope, whose state the simulation never sees
 *                   (lockstep_pilot_present.h), and inside a call this client makes alone
 *                   for its own view (rid_scope: the stock target view of
 *                   native_gate_pilot_camera_follow, 5429 transient IDs on client 2 at
 *                   tick 30 in stock-J2c). How many depends on the client.
 *   block 30, view: U::EffectObject, EffectObjectCreator, FogVolume, LensFlare,
 *                   Stardust; UI::ICom messages, sources and listeners with what the
 *                   message start makes (0xada7a0: U::Action on the speaker). What the
 *                   simulation's own events ask for, equal in play23-soak on every
 *                   capture: its IDs stay in step between clients as long as the events
 *                   do, and if one does depend on the client only this list moves.
 * ui wins over view: a stack that matches both is the camera's. A stack is the X4
 * return addresses of the allocation, up to 62 frames, matched exactly for a call
 * and by range for a function's body. AssignNewID and Remove only know the map's
 * head, tail and free fields, so under the critical section those three are swapped
 * for the list's around the original, which then works as native on any list (free
 * is biased by EnsureFreeSlots' threshold, so no block is due). The slots live in
 * the map's table, so Find, the generations and the destructor need nothing. Local
 * lists recycle FIFO, their IDs are nobody's business but this client's. allocs=
 * counts the shared list only; local= has allocs/frees/free per list.
 *
 * The barrier stays what it was. It reads the shared head: a simulation allocation
 * the table calls local on one client only, or a local allocation it does not know,
 * still moves the next ID and shows in the relay's mismatches; the route= column of
 * RUNTIME_IDS_CALLER names the stack that did it, and the table can grow. An empty
 * local list falls back on the shared one and stops the gate (local_exhausted).
 *
 * Rows. At the seal: count, sum digest, range and generation count of the saved
 * map and of the load-created identities, plus 48 digests by saved ID
 * (RUNTIME_IDS_MAP), so two clients' rows name the range that differs. A set-mode
 * seal also walks the whole list and stops the gate if a slot below the agreed
 * one is on it. With the created identities hook: a table of who allocates
 * (phase, thread class, the X4 return addresses; load: direct caller only), so
 * clients' tables name what leaks (RUNTIME_IDS_CALLER).
 *
 * 'observe' sets nothing and patches nothing: same rows, for a run that shows the
 * offsets first. Stop reason 130. */
/* The Linux gate (linux/gate_runtime_ids.h) defines these for its executable, and its own patch. */
#ifndef RID_OWN_PLATFORM
#define RID_MAP 0x6cf1500u
#define RID_REMOVE_ENTRY 0xcbe70u
#define RID_REMOVE_THUNK 192u
#define RID_F_INSERTS 0x38
#define RID_F_LIVE 0x40
#define RID_F_FREE 0x44
#define RID_F_GENERATION 0x4c
#define RID_F_HEAD 0x50
#define RID_F_TAIL 0x54
#define RID_F_BLOCKS 0x58
#define RID_F_BLOCK1 0x60
#endif
#define RID_STOP_REASON 130u
#define RID_DEFAULT_BASE 0x10000u
#define RID_BASE_MAX 0x80000u
#define RID_BLOCK_LAST 0x100000u /* the last slot of block 0 */
#define RID_FREE_MIN 0x19000u    /* EnsureFreeSlots appends a block below this */
#define RID_CALLERS 2048u
#define RID_FRAMES 8u
#define RID_STACK 62u
#define RID_BUCKETS 48u
#define RID_BLOCK_BYTES 0xaaaac0u  /* EnsureFreeSlots' block */
#define RID_BLOCK_LIMIT 29u        /* the head entering this block stops the gate */
#define RID_ALLOCATE 0x14b7140u    /* SMem::Allocate(size, -, -, -, alignment): what the map's destructor frees */
#define RID_LOCAL_UI_BLOCK 31u
#define RID_LOCAL_VIEW_BLOCK 30u
#define RID_LOCAL_MIN 1024u
enum {RID_OFF,RID_OBSERVE,RID_SET};
enum {RID_ROUTE_SHARED,RID_ROUTE_UI,RID_ROUTE_VIEW,RID_ROUTES};
typedef uint64_t (*rid_remove_fn)(void *,uint64_t);
/* An MD cue instance's ID (ScriptEventHandler's constructor called from MD::Cue_CreateInstance) carries
 * the static cue's name: rows of different cues on the same stack are counted apart (cue=...). */
#define RID_CUE 48u
#define RID_CUE_HANDLER 0xcaab18u     /* AssignNewID returns here, in the ScriptEventHandler constructor */
#define RID_CUE_CREATE 0x1111aaau     /* the constructor returns here, in MD::Cue_CreateInstance; rdi = the static cue */
#define RID_CUE_NAME 0xc8u            /* the static cue's name, std::string (copied to the instance at +0xc8) */
typedef struct {uint64_t hash,count;uint32_t frames[RID_FRAMES];unsigned char phase,thread,route;char cue[RID_CUE];} rid_caller_t;
typedef struct {uint64_t inserts;uint32_t live,free,head,tail,generation;} rid_counts_t;
typedef struct {uint64_t allocs,frees;uint32_t head,tail,free;} rid_local_t;
typedef struct {uint32_t head,tail,free;} rid_fields_t;
typedef struct {uint32_t lo,hi,target;unsigned char route,ui_move;} rid_site_t; /* return addresses lo..hi; target: the call that returns to lo */
static int rid_mode,rid_installed,rid_restored,rid_sealed;
static uint32_t rid_base,rid_head_before,rid_cut,rid_skipped,rid_below_base;
static rid_counts_t rid_before,rid_now;
static uint64_t rid_original_word,rid_patched_word,rid_digest,rid_frames_done,rid_next_id;
static uint64_t rid_map_sum,rid_map_min,rid_map_max,rid_load_sum;
static uint32_t rid_map_count,rid_map_generations,rid_load_count,rid_buckets[RID_BUCKETS];
static volatile LONG64 rid_failures,rid_dropped,rid_caller_overflow;
static volatile LONG rid_active;
static const char *rid_stage="idle";
static rid_remove_fn rid_remove_native; /* tests only: replaces the thunk's original */
#ifndef RID_OWN_PLATFORM
static created_assign_fn rid_assign_native; /* tests only: the same for AssignNewID */
#endif
static void *(*rid_allocate_native)(size_t); /* tests only: replaces SMem::Allocate */
static volatile LONG pres_open;   /* lockstep_pilot_present.h: its scope is open, on the UI thread */
static volatile LONG rid_local_on,rid_probe_frame;
static volatile DWORD rid_scope_thread; /* the thread in a client-only view call, 0 none */
static int rid_local_built;
static rid_local_t rid_local[RID_ROUTES],rid_local_now[RID_ROUTES];
#ifndef RID_OWN_PLATFORM
#define RID_CALL(ret,target,route) {ret,ret,target,route,0}
#define RID_BODY(start,end,route,ui_move) {start,(end)-1u,0,route,ui_move}
static const rid_site_t rid_sites[]={
    /* U::UIElement: both constructors' Positional calls, and the instance factory with what it makes. */
    RID_CALL(0x80d42eu,0x7429c0u,RID_ROUTE_UI),RID_CALL(0x80d1c7u,0x7426d0u,RID_ROUTE_UI),RID_BODY(0xaf0ec0u,0xaf10a0u,RID_ROUTE_UI,0),
    /* U::HoloMap's constructors; U::CutsceneAnchor in the factories 0x8e0250 and 0x8e2ec0. */
    RID_BODY(0xe186c0u,0xe18902u,RID_ROUTE_UI,0),RID_BODY(0xe189d0u,0xe18c12u,RID_ROUTE_UI,0),RID_CALL(0x8e167eu,0x7792f0u,RID_ROUTE_UI),RID_CALL(0x8e43d8u,0x7791c0u,RID_ROUTE_UI),
    /* Lua::NotifyOn*: the UI's event listeners. */
    RID_CALL(0xb0ebf8u,0xcc220u,RID_ROUTE_UI),RID_CALL(0xb0edd8u,0xcc220u,RID_ROUTE_UI),RID_CALL(0xb0efe9u,0xcc220u,RID_ROUTE_UI),RID_CALL(0xb0f1f9u,0xcc220u,RID_ROUTE_UI),
    RID_CALL(0xb0f3d8u,0xcc220u,RID_ROUTE_UI),RID_CALL(0xb0f6b8u,0xcc220u,RID_ROUTE_UI),RID_CALL(0xb0f868u,0xcc220u,RID_ROUTE_UI),RID_CALL(0xb0fa18u,0xcc220u,RID_ROUTE_UI),
    RID_CALL(0xb0fbf8u,0xcc220u,RID_ROUTE_UI),RID_CALL(0xb0fdd8u,0xcc220u,RID_ROUTE_UI),RID_CALL(0xb0ffb8u,0xcc220u,RID_ROUTE_UI),RID_CALL(0xb10168u,0xcc220u,RID_ROUTE_UI),
    RID_CALL(0xb10318u,0xcc220u,RID_ROUTE_UI),RID_CALL(0xb104c8u,0xcc220u,RID_ROUTE_UI),RID_CALL(0xb10678u,0xcc220u,RID_ROUTE_UI),RID_CALL(0xb114c8u,0xcc220u,RID_ROUTE_UI),
    RID_CALL(0xb1eaa8u,0xcc220u,RID_ROUTE_UI),RID_CALL(0xb1ec88u,0xcc220u,RID_ROUTE_UI),RID_CALL(0xb1ee38u,0xcc220u,RID_ROUTE_UI),RID_CALL(0xb1efe8u,0xcc220u,RID_ROUTE_UI),
    RID_CALL(0xb1f198u,0xcc220u,RID_ROUTE_UI),RID_CALL(0xb1f348u,0xcc220u,RID_ROUTE_UI),RID_CALL(0xb1f528u,0xcc220u,RID_ROUTE_UI),RID_CALL(0xb1f708u,0xcc220u,RID_ROUTE_UI),
    RID_CALL(0xb1f8c8u,0xcc220u,RID_ROUTE_UI),RID_CALL(0xb1fab8u,0xcc220u,RID_ROUTE_UI),RID_CALL(0xb1fc98u,0xcc220u,RID_ROUTE_UI),RID_CALL(0xb1feabu,0xcc220u,RID_ROUTE_UI),
    /* The cockpit animation listeners; Modules::StartMenu. */
    RID_CALL(0x116e158u,0xcc220u,RID_ROUTE_UI),RID_CALL(0x116e538u,0xcc220u,RID_ROUTE_UI),RID_CALL(0x116e608u,0xcc220u,RID_ROUTE_UI),RID_CALL(0xac472du,0xcc220u,RID_ROUTE_UI),
    /* Slot 6 (not the probe's frame) and slot 10. */
    RID_BODY(0xb3bad0u,0xb3bb9eu,RID_ROUTE_UI,1),RID_BODY(0x121c1f0u,0x121c25au,RID_ROUTE_UI,0),
    /* UI::ICom: a message and what its start makes, CMessageManager::CreateSource, the message config listener. */
    RID_CALL(0xaddb73u,0xcc220u,RID_ROUTE_VIEW),RID_BODY(0xada7a0u,0xada9e8u,RID_ROUTE_VIEW,0),RID_BODY(0xae9050u,0xae9568u,RID_ROUTE_VIEW,0),RID_CALL(0xae885au,0xcc220u,RID_ROUTE_VIEW),
    /* EffectObject, FogVolume, LensFlare, Stardust: the two constructors' Positional calls; EffectObjectCreator. */
    RID_CALL(0x8e1b44u,0x7429c0u,RID_ROUTE_VIEW),RID_CALL(0x8e48a6u,0x7426d0u,RID_ROUTE_VIEW),RID_CALL(0xd9d312u,0x7429c0u,RID_ROUTE_VIEW),RID_CALL(0x8e4bbeu,0x7426d0u,RID_ROUTE_VIEW),
    RID_CALL(0xe233feu,0x7429c0u,RID_ROUTE_VIEW),RID_CALL(0xe233aeu,0x7426d0u,RID_ROUTE_VIEW),RID_CALL(0xe417aeu,0x7429c0u,RID_ROUTE_VIEW),RID_CALL(0xe4175eu,0x7426d0u,RID_ROUTE_VIEW),
    RID_CALL(0xd7313cu,0xcc220u,RID_ROUTE_VIEW),RID_CALL(0xd70c1bu,0xcc220u,RID_ROUTE_VIEW)};
#define RID_SITES (sizeof(rid_sites)/sizeof(*rid_sites))
static const struct {unsigned rva,size;const char *sha;} rid_local_bodies[]={
    {0xaf0ec0u,0x1e0u,"ae79899a9a827afd1c6fe4f3e073e52cb9afb281834e1f6fdebbfb8b79d33678"},
    {0xe186c0u,0x242u,"cde2f87692a83176598cf57485e827c792d09e80bb73847a835620e7a5d39198"},
    {0xe189d0u,0x242u,"d90d3e7d011e4eb2f6e97fc1a34f793d9dbea88a610ede13909650902f40e32c"},
    {0xada7a0u,0x248u,"cc73ccaee5d464051845bbf7d696ff7d5b3afc2622e4e19b78d465dd9c729d39"},
    {0xae9050u,0x518u,"e82ea5ab07935a100dfdd738182a37ee5ddb73468bd31f570bab71657831c59b"},
    {0xb3bad0u,0xceu,"87a23ed23ef4be0fdf493a983342282860054eb3eb14f2388e134abc88762261"},
    {0x121c1f0u,0x6au,"5d20ad384ae9ce74bb15625b923fd33638ad20d3f9e553c5d9cb7d264b20ed6f"},
    {RID_ALLOCATE,0x161u,"3c6bc6a1e9200a38f67b8fb5b258ed984750257f9f8cd54996491b7e04defea1"}};
#define RID_LOCAL_BODIES (sizeof(rid_local_bodies)/sizeof(*rid_local_bodies))
#endif
static rid_caller_t rid_callers[RID_CALLERS];
static SRWLOCK rid_callers_lock=SRWLOCK_INIT;
static unsigned rid_caller_count;
static int rid_fail(const char *why) {rid_stage=why;InterlockedIncrement64(&rid_failures);return 0;}
static unsigned char *rid_map(void) {return *(unsigned char *volatile *)(image+RID_MAP);}
/* The bucket of a 1-based slot and its index in it; NULL when the slot's block does not exist. */
static unsigned char *rid_bucket(unsigned char *map,uint32_t slot,unsigned *entry) {
    if(!slot) return NULL;
    const uint32_t index=slot-1u,block=index>>20;if(block>=32u) return NULL;
    unsigned char *base=*(unsigned char *volatile *)(map+RID_F_BLOCKS+block*8u);if(!base) return NULL;
    const uint32_t low=index&0xfffffu;*entry=low%3u;return base+(uint64_t)(low/3u)*0x20u;
}
static volatile uint64_t *rid_slot(unsigned char *map,uint32_t slot) {
    unsigned entry;unsigned char *b=rid_bucket(map,slot,&entry);return b?(volatile uint64_t *)(b+entry*8u):NULL;
}
static int rid_slot_used(unsigned char *map,uint32_t slot) {
    unsigned entry;unsigned char *b=rid_bucket(map,slot,&entry);
    return !b || ((*(volatile uint32_t *)(b+0x1c)>>(entry*4u+16u))&1u);
}
static uint32_t rid_slot_generation(unsigned char *map,uint32_t slot) {
    unsigned entry;unsigned char *b=rid_bucket(map,slot,&entry);if(!b) return 0xffffffffu;
    return (*(volatile uint32_t *)(b+0x18+(entry>>1)*4u)>>((entry&1u)*16u))&0xffffu;
}
static rid_counts_t rid_counts(unsigned char *map) {
    rid_counts_t c;memset(&c,0,sizeof(c));c.inserts=*(volatile uint64_t *)(map+RID_F_INSERTS);c.live=*(volatile uint32_t *)(map+RID_F_LIVE);c.free=*(volatile uint32_t *)(map+RID_F_FREE);
    c.generation=*(volatile uint32_t *)(map+RID_F_GENERATION);c.head=*(volatile uint32_t *)(map+RID_F_HEAD);c.tail=*(volatile uint32_t *)(map+RID_F_TAIL);return c;
}
/* Under the critical section: the ID the next allocation gets (AssignNewID, unless a new block is due first; free stays far above that). */
static uint64_t rid_next(unsigned char *map) {
    const uint32_t head=*(volatile uint32_t *)(map+RID_F_HEAD);
    return head?((uint64_t)(rid_slot_generation(map,head)&0xffffu)<<25)|head:0;
}
/* Under the critical section: head and tail end up identical on every client. 0 with rid_stage set: nothing changed. */
static int rid_apply(unsigned char *map,uint32_t base) {
    const rid_counts_t c=rid_counts(map);const uint32_t last=RID_BLOCK_LAST;
    rid_stage="head_past_base";if(!c.head || c.head>base) return 0;
    rid_stage="block_layout";if(!*(void *volatile *)(map+RID_F_BLOCKS) || *(void *volatile *)(map+RID_F_BLOCK1)) return 0;
    rid_stage="free_count";
    if(c.free<last-c.head+1u) return 0;
    const uint32_t freed=c.free-(last-c.head+1u);
    /* The fresh run: every slot unused, generation 0, linked to its neighbours. */
    rid_stage="fresh_run";
    for(uint32_t slot=c.head;slot<=last;slot++) {
        const volatile uint64_t *p=rid_slot(map,slot);if(!p || rid_slot_used(map,slot) || rid_slot_generation(map,slot)) return 0;
        const uint64_t v=*p;const uint32_t prev=(uint32_t)v,next=(uint32_t)(v>>32);
        if(prev!=(slot==c.head?0u:slot-1u)) return 0;
        if(slot<last?next!=slot+1u:(freed?!next:next!=0u)) return 0;
    }
    /* Behind it: exactly the slots freed so far. */
    rid_stage="freed_run";
    if(freed?c.tail==last:c.tail!=last) return 0;
    uint32_t at=(uint32_t)(*rid_slot(map,last)>>32),seen=0;
    for(;at && seen<=freed;seen++) {
        const volatile uint64_t *p=rid_slot(map,at);if(!p || rid_slot_used(map,at)) return 0;
        if(!(uint32_t)(*p>>32) && at!=c.tail) return 0;
        at=(uint32_t)(*p>>32);
    }
    if(seen!=freed) return 0;
    volatile uint64_t *first=rid_slot(map,base),*end=rid_slot(map,last);
    *first&=0xffffffff00000000ULL; /* no previous slot */
    *end&=0xffffffffULL;           /* no next slot */
    *(volatile uint32_t *)(map+RID_F_HEAD)=base;*(volatile uint32_t *)(map+RID_F_TAIL)=last;*(volatile uint32_t *)(map+RID_F_FREE)=last-base+1u;
    rid_head_before=c.head;rid_cut=freed;rid_skipped=base-c.head;rid_stage="set";return 1;
}
/* Under the critical section: AssignNewID and Remove work on this list until rid_local_out. */
static void rid_local_in(unsigned char *map,const rid_local_t *list,rid_fields_t *shared) {
    shared->head=*(volatile uint32_t *)(map+RID_F_HEAD);shared->tail=*(volatile uint32_t *)(map+RID_F_TAIL);shared->free=*(volatile uint32_t *)(map+RID_F_FREE);
    *(volatile uint32_t *)(map+RID_F_HEAD)=list->head;*(volatile uint32_t *)(map+RID_F_TAIL)=list->tail;*(volatile uint32_t *)(map+RID_F_FREE)=list->free+RID_FREE_MIN;
}
static void rid_local_out(unsigned char *map,rid_local_t *list,const rid_fields_t *shared) {
    list->head=*(volatile uint32_t *)(map+RID_F_HEAD);list->tail=*(volatile uint32_t *)(map+RID_F_TAIL);list->free=*(volatile uint32_t *)(map+RID_F_FREE)-RID_FREE_MIN;
    *(volatile uint32_t *)(map+RID_F_HEAD)=shared->head;*(volatile uint32_t *)(map+RID_F_TAIL)=shared->tail;*(volatile uint32_t *)(map+RID_F_FREE)=shared->free;
}
static rid_local_t *rid_local_of(uint32_t slot) {
    if(!rid_local_built || !slot) return NULL;
    const uint32_t block=(slot-1u)>>20;
    return block==RID_LOCAL_UI_BLOCK?&rid_local[RID_ROUTE_UI]:block==RID_LOCAL_VIEW_BLOCK?&rid_local[RID_ROUTE_VIEW]:NULL;
}
/* Remove's entry. A local slot goes back on its list; a shared one comes off the tail again, nothing recycles. Recursive critical section. */
static uint64_t rid_remove_call(rid_remove_fn original,void *map,uint64_t id) {
    const uint32_t slot=(uint32_t)id&0x1ffffffu;
    if(!InterlockedCompareExchange(&rid_active,0,0) || !slot || map!=(void*)rid_map()) return original(map,id);
    CRITICAL_SECTION *lock=(CRITICAL_SECTION*)((unsigned char*)map+0x10);EnterCriticalSection(lock);
    rid_local_t *list=rid_local_of(slot);uint64_t removed;
    if(list) {
        rid_fields_t shared;rid_local_in(map,list,&shared);
        removed=original(map,id);
        rid_local_out(map,list,&shared);
        if(removed&0xffu) ++list->frees;
    } else {
        removed=original(map,id);
        if((removed&0xffu) && *(volatile uint32_t *)((unsigned char*)map+RID_F_TAIL)==slot) {
            volatile uint64_t *p=rid_slot(map,slot);const uint32_t prev=p?(uint32_t)*p:0u;
            volatile uint64_t *q=prev?rid_slot(map,prev):NULL;
            if(q) {*q&=0xffffffffULL;*(volatile uint32_t *)((unsigned char*)map+RID_F_TAIL)=prev;*(volatile uint32_t *)((unsigned char*)map+RID_F_FREE)-=1u;InterlockedIncrement64(&rid_dropped);}
            else rid_fail("remove_tail");
        }
        else if(removed&0xffu) rid_fail("remove_not_tail");
    }
    LeaveCriticalSection(lock);return removed;
}
static uint64_t rid_remove(void *map,uint64_t id) {
    return rid_remove_call(rid_remove_native?rid_remove_native:(rid_remove_fn)(void *)(thunk+RID_REMOVE_THUNK),map,id);
}
#ifdef RID_OWN_PLATFORM
static int rid_pins(void);
static int rid_swap(uint64_t expected,uint64_t value);
static int rid_install(void);
#else
static int rid_pins(void) {
    return hash_ok(image+0xcc228,260,"160c9a5f794275df134c21359996420b316e8f1eee68a5cf794451fd71c172b9") && /* AssignNewID past the entry word */
           hash_ok(image+0xcc500,353,"b512d37e15c7d8d1e3deba7ae50e18cf3b94e93805668bea8a7834fb8eee22e3") && /* InsertAtSlot */
           hash_ok(image+0xcc670,468,"dd29f1a7637f7948e8c774d39713a2482966e268cf370eab106036f5e6d771c5") && /* EnsureFreeSlots */
           hash_ok(image+0xced70,184,"3209f7be0616de6d0ecc20ce3cfd82fd16d0524c09635d86995210598b0a5f36");    /* Find */
}
static int rid_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+RID_REMOVE_ENTRY,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+RID_REMOVE_ENTRY),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+RID_REMOVE_ENTRY,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+RID_REMOVE_ENTRY,8);return actual==expected;
}
static int rid_install(void) {
    static const unsigned char native[8]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c};
    if(!thunk || !thunk_slot_ready(RID_REMOVE_THUNK)) {rid_stage="thunk";return 0;}
    if(!hash_ok(image+RID_REMOVE_ENTRY,414,"2ace935f514d696f4be0acbcb538baa81c82ebd95c2839d0f3906a73b340a921")) {rid_stage="pins";return 0;}
    intptr_t distance=(intptr_t)(thunk+RID_REMOVE_THUNK+32)-(intptr_t)(image+RID_REMOVE_ENTRY+5);
    if(distance<INT32_MIN || distance>INT32_MAX) {rid_stage="distance";return 0;}
    memcpy(&rid_original_word,image+RID_REMOVE_ENTRY,8);if(memcmp(&rid_original_word,native,8)) {rid_stage="entry";return 0;}
    rid_patched_word=rid_original_word;
    unsigned char *patch=(unsigned char*)&rid_patched_word;patch[0]=0xe9;int32_t rel=(int32_t)distance;memcpy(patch+1,&rel,4);
    if(!rid_swap(rid_original_word,rid_patched_word)) {rid_stage="swap";return 0;}
    rid_installed=1;return 1;
}
#endif
/* Right before LoadGame (UI thread, boot phase 0). base 0: observe. A rerun after a loading-UI reload is accepted. */
static int rid_arm(uint32_t base) {
    if(rid_mode) return stats.active && rid_base==base && rid_mode==(base?RID_SET:RID_OBSERVE);
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) {rid_stage="phase";return 0;}
    if(!world_identity_installed || !world_identities) {rid_stage="identities";return 0;}
    if(base>RID_BASE_MAX) {rid_stage="base_range";return 0;}
    if(!rid_pins()) {rid_stage="pins";return 0;}
    unsigned char *map=rid_map();if(!map) {rid_stage="map";return 0;}
    CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(map+0x10);
    if(base && !rid_install()) return 0;
    EnterCriticalSection(lock);
    rid_before=rid_counts(map);rid_head_before=rid_before.head;
    int ok=!base || rid_apply(map,base);
    if(ok) {rid_base=base;rid_now=rid_counts(map);rid_mode=base?RID_SET:RID_OBSERVE;if(!base) rid_stage="observe";InterlockedExchange(&rid_active,base?1:0);}
    LeaveCriticalSection(lock);
    if(!ok && rid_installed && rid_swap(rid_patched_word,rid_original_word)) rid_installed=0;
    return ok;
}
#ifndef RID_OWN_PLATFORM
/* Every call to a shape checked: the table's return addresses are this build's. */
static int rid_local_pins(void) {
    for(unsigned i=0;i<RID_SITES;i++) {
        const rid_site_t *s=&rid_sites[i];if(!s->target) continue;
        int32_t rel;memcpy(&rel,image+s->lo-4u,4);
        if(image[s->lo-5u]!=0xe8 || (uint32_t)((int64_t)s->lo+rel)!=s->target) return 0;
    }
    for(unsigned i=0;i<RID_LOCAL_BODIES;i++) if(!hash_ok(image+rid_local_bodies[i].rva,rid_local_bodies[i].size,rid_local_bodies[i].sha)) return 0;
    return 1;
}
static void *rid_allocate(size_t size) {
    typedef void *(*allocate_fn)(size_t,uint64_t,uint64_t,uint64_t,uint32_t);
    return rid_allocate_native?rid_allocate_native(size):((allocate_fn)(void *)(image+RID_ALLOCATE))(size,0,0,0,0x10);
}
/* Block in the map's table, every slot free, the list in slot order: what EnsureFreeSlots builds. Under the critical section. */
static void rid_local_link(unsigned char *map,rid_local_t *list,unsigned block,void *base) {
    memcpy(map+RID_F_BLOCKS+block*8u,&base,8);
    const uint32_t first=block*0x100000u+1u,last=block==31u?0x1ffffffu:block*0x100000u+0x100000u;
    for(uint32_t slot=first;slot<=last;slot++) *rid_slot(map,slot)=(slot==first?0ULL:(uint64_t)(slot-1u))|((uint64_t)(slot<last?slot+1u:0u)<<32);
    memset(list,0,sizeof(*list));list->head=first;list->tail=last;list->free=last-first+1u;
}
static int rid_local_build(unsigned char *map) {
    if(*(void *volatile *)(map+RID_F_BLOCKS+RID_LOCAL_UI_BLOCK*8u) || *(void *volatile *)(map+RID_F_BLOCKS+RID_LOCAL_VIEW_BLOCK*8u)) {rid_stage="local_blocks";return 0;}
    void *ui=rid_allocate(RID_BLOCK_BYTES),*view=rid_allocate(RID_BLOCK_BYTES);
    if(!ui || !view) {rid_stage="local_allocate";return 0;}
    memset(ui,0,RID_BLOCK_BYTES);memset(view,0,RID_BLOCK_BYTES);
    CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(map+0x10);EnterCriticalSection(lock);
    rid_local_link(map,&rid_local[RID_ROUTE_UI],RID_LOCAL_UI_BLOCK,ui);
    rid_local_link(map,&rid_local[RID_ROUTE_VIEW],RID_LOCAL_VIEW_BLOCK,view);
    rid_local_built=1;memcpy(rid_local_now,rid_local,sizeof(rid_local_now));
    LeaveCriticalSection(lock);return 1;
}
/* Right after a set rid_arm, before the load. A rerun after a loading-UI reload is accepted. */
static int rid_local_arm(void) {
    if(rid_local_on) return rid_mode==RID_SET;
    if(rid_mode!=RID_SET || !stats.active || rid_sealed || GetCurrentThreadId()!=ui_thread) {rid_stage="local_phase";return 0;}
    if(!created_installed) {rid_stage="local_hook";return 0;}
    if(!rid_local_pins()) {rid_stage="local_pins";return 0;}
    unsigned char *map=rid_map();if(!map) {rid_stage="map";return 0;}
    if(!rid_local_build(map)) return 0;
    InterlockedExchange(&rid_local_on,1);return 1;
}
/* Around an engine call this client makes alone for its own view. */
static void rid_scope(int open) {rid_scope_thread=open?GetCurrentThreadId():0;}
/* Which list a stack belongs to: the presentation scope, a view call, then the table, ui over view. */
static unsigned rid_route_of(const uint32_t *frames,unsigned count) {
    if(!InterlockedCompareExchange(&rid_local_on,0,0) || !rid_sealed) return RID_ROUTE_SHARED;
    if(InterlockedCompareExchange(&pres_open,0,0) && GetCurrentThreadId()==ui_thread) return RID_ROUTE_UI;
    if(rid_scope_thread && rid_scope_thread==GetCurrentThreadId()) return RID_ROUTE_UI;
    const int probe=InterlockedCompareExchange(&rid_probe_frame,0,0)!=0;unsigned route=RID_ROUTE_SHARED;
    for(unsigned i=0;i<count;i++) for(unsigned s=0;s<RID_SITES;s++) {
        const rid_site_t *site=&rid_sites[s];
        if(frames[i]<site->lo || frames[i]>site->hi || (site->ui_move && probe)) continue;
        if(site->route==RID_ROUTE_UI) return RID_ROUTE_UI;
        route=site->route;
    }
    return route;
}
static created_assign_fn rid_assign_original(void) {return rid_assign_native?rid_assign_native:(created_assign_fn)(void *)(thunk+CREATED_THUNK);}
/* AssignNewID on a local list, the shared fields untouched afterwards. A list near its end: the shared one, and the gate stops at the boundary. */
static uint64_t rid_local_assign(rid_local_t *list,void *object) {
    const created_assign_fn original=rid_assign_original();unsigned char *map=rid_map();
    if(!map) return original(object);
    CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(map+0x10);EnterCriticalSection(lock);
    uint64_t result;
    if(list->free<RID_LOCAL_MIN) {rid_fail("local_exhausted");result=original(object);}
    else {
        const uint64_t before=*(volatile uint64_t *)(map+RID_F_INSERTS);rid_fields_t shared;
        rid_local_in(map,list,&shared);
        result=original(object);
        rid_local_out(map,list,&shared);
        list->allocs+=*(volatile uint64_t *)(map+RID_F_INSERTS)-before;
    }
    LeaveCriticalSection(lock);return result;
}
static uint64_t rid_assign_ui(void *object) {return rid_local_assign(&rid_local[RID_ROUTE_UI],object);}
static uint64_t rid_assign_view(void *object) {return rid_local_assign(&rid_local[RID_ROUTE_VIEW],object);}
#endif
/* One table entry per (phase, thread class, route, X4 return addresses[, cue]); load phase: the direct caller only. */
static void rid_caller_add_cue(const uint32_t *frames,unsigned phase,unsigned thread,unsigned route,const char *cue) {
    uint64_t hash=lockstep_mix(((uint64_t)phase<<16)|((uint64_t)route<<8)|thread);
    for(unsigned i=0;i<RID_FRAMES;i++) hash=lockstep_mix(hash^frames[i]);
    for(unsigned i=0;cue && cue[i] && i<RID_CUE;i++) hash=lockstep_mix(hash^(uint64_t)(unsigned char)cue[i]^((uint64_t)(i+1)<<40));
    AcquireSRWLockExclusive(&rid_callers_lock);
    unsigned at=(unsigned)hash&(RID_CALLERS-1u);
    for(unsigned probe=0;probe<RID_CALLERS;probe++,at=(at+1u)&(RID_CALLERS-1u)) {
        rid_caller_t *c=&rid_callers[at];
        if(!c->count) {
            if(rid_caller_count>=RID_CALLERS/2u) break;
            c->hash=hash;c->phase=(unsigned char)phase;c->thread=(unsigned char)thread;c->route=(unsigned char)route;memcpy(c->frames,frames,sizeof(c->frames));c->count=1;++rid_caller_count;
            c->cue[0]=0;if(cue) {strncpy(c->cue,cue,RID_CUE-1);c->cue[RID_CUE-1]=0;}
            ReleaseSRWLockExclusive(&rid_callers_lock);return;
        }
        if(c->hash==hash) {++c->count;ReleaseSRWLockExclusive(&rid_callers_lock);return;}
    }
    ReleaseSRWLockExclusive(&rid_callers_lock);InterlockedIncrement64(&rid_caller_overflow);
}
__attribute__((unused)) static void rid_caller_add(const uint32_t *frames,unsigned phase,unsigned thread,unsigned route) {rid_caller_add_cue(frames,phase,thread,route,NULL);}
/* The name of a static MD cue (an std::string at +0xc8), read without trusting it: letters, digits, _ . $ only. */
static void rid_cue_of_static(const unsigned char *cue,char out[RID_CUE]) {
    unsigned char text[24];uint64_t size=0,capacity=0;const unsigned char *at=NULL;
    out[0]=0;
    if(!cue || !read_local(cue+RID_CUE_NAME,text,16) || !read_local(cue+RID_CUE_NAME+0x10,&size,8) || !read_local(cue+RID_CUE_NAME+0x18,&capacity,8) ||
       !size || size>=RID_CUE || size>capacity) return;
    if(capacity>15) {if(!read_local(cue+RID_CUE_NAME,&at,8) || !at || !read_local(at,out,(size_t)size)) {out[0]=0;return;}}
    else memcpy(out,text,(size_t)size);
    out[size]=0;
    for(uint64_t i=0;i<size;i++) {
        char c=out[i];
        if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='.' || c=='$')) {out[0]=0;return;}
    }
}
#ifndef RID_OWN_PLATFORM
/* Pins of the cue path: Cue_CreateInstance keeps the static cue in rdi up to the constructor's call. */
static int rid_cue_pins_checked;
static int rid_cue_pins(void) {
    if(!rid_cue_pins_checked) rid_cue_pins_checked=
        hash_ok(image+0x1111a40,0x6a,"f7f334b8b83a479853ba30780c92e3534051bed88d1711fa66d3786f59e90bc6") &&
        image[0xcaab13]==0xe8?1:-1;
    return rid_cue_pins_checked==1;
}
/* From inside the allocation: unwind to Cue_CreateInstance's frame and take its rdi. */
static void rid_cue_name(char out[RID_CUE]) {
    CONTEXT c;out[0]=0;
    if(!rid_cue_pins()) return;
    RtlCaptureContext(&c);
    const DWORD64 want=(DWORD64)(uintptr_t)(image+RID_CUE_CREATE);
    for(unsigned depth=0;depth<RID_STACK && c.Rip;depth++) {
        if(c.Rip==want) {rid_cue_of_static((const unsigned char*)(uintptr_t)c.Rdi,out);return;}
        DWORD64 base=0;PRUNTIME_FUNCTION fn=RtlLookupFunctionEntry(c.Rip,&base,NULL);
        if(fn) {PVOID handler=NULL;DWORD64 frame=0;RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,c.Rip,fn,&c,&handler,&frame,NULL);}
        else {DWORD64 back=0;if(!read_local((const void*)(uintptr_t)c.Rsp,&back,8)) return;c.Rip=back;c.Rsp+=8;}
    }
}
#endif
#ifndef RID_OWN_PLATFORM
/* The X4 return addresses of an allocation, innermost first (AssignNewID's caller is its return address); before the seal, a million of them, only that one. */
static unsigned rid_frames_of(void *caller,uint32_t *frames) {
    const uintptr_t low=(uintptr_t)image,high=low+0x6fa9000u;unsigned n=0;
    if(rid_sealed) {
        void *stack[RID_STACK];const USHORT got=RtlCaptureStackBackTrace(0,RID_STACK,stack,NULL);
        for(USHORT i=0;i<got;i++) if((uintptr_t)stack[i]>=low && (uintptr_t)stack[i]<high) frames[n++]=(uint32_t)((uintptr_t)stack[i]-low);
    } else if((uintptr_t)caller>=low && (uintptr_t)caller<high) frames[n++]=(uint32_t)((uintptr_t)caller-low);
    return n;
}
#endif
static void rid_note_caller_cue(const uint32_t *frames,unsigned count,unsigned route,unsigned thread,const char *cue) {
    uint32_t row[RID_FRAMES];memset(row,0,sizeof(row));
    memcpy(row,frames,(count<RID_FRAMES?count:RID_FRAMES)*sizeof(*row));
    rid_caller_add_cue(row,rid_sealed?1u:0u,thread,route,cue);
}
__attribute__((unused)) static void rid_note_caller(const uint32_t *frames,unsigned count,unsigned route,unsigned thread) {rid_note_caller_cue(frames,count,route,thread,NULL);}
/* A stack that creates an MD cue instance (the first two frames). */
static int rid_is_cue(const uint32_t *frames,unsigned count) {return count>=2 && frames[0]==RID_CUE_HANDLER && frames[1]==RID_CUE_CREATE;}
#ifndef RID_OWN_PLATFORM
/* The created identities hook with this module on: the stack, the route, the original once, the note. */
static uint64_t rid_assign(void *object,void *caller) {
    const DWORD error=GetLastError();
    uint32_t frames[RID_STACK];
    const unsigned count=rid_frames_of(caller,frames),thread=created_thread_class(),route=rid_route_of(frames,count);
    char cue[RID_CUE];cue[0]=0;
    if(rid_sealed && rid_is_cue(frames,count)) rid_cue_name(cue);
    SetLastError(error);
    const uint64_t result=created_assign_execute(route==RID_ROUTE_UI?rid_assign_ui:route==RID_ROUTE_VIEW?rid_assign_view:rid_assign_original(),object);
    const DWORD after=GetLastError();
    rid_note_caller_cue(frames,count,route,thread,cue[0]?cue:NULL);
    SetLastError(after);
    return result;
}
#endif
/* The saved map and the load-created identities: order-independent, so table layout does not count. */
static void rid_map_fold(void) {
    rid_map_sum=rid_load_sum=0;rid_map_min=UINT64_MAX;rid_map_max=0;rid_map_count=rid_map_generations=rid_load_count=0;memset(rid_buckets,0,sizeof(rid_buckets));
    for(unsigned i=0;world_identities && i<WORLD_IDENTITY_CAPACITY;i++) {
        const uint64_t runtime=world_identities[i].runtime,saved=world_identities[i].saved;if(!runtime) continue;
        const uint64_t h=lockstep_mix(saved*0x9e3779b97f4a7c15ULL^runtime);rid_map_sum+=h;++rid_map_count;
        rid_buckets[(saved>>15)%RID_BUCKETS]+=(uint32_t)h;
        if(runtime<rid_map_min) rid_map_min=runtime;
        if(runtime>rid_map_max) rid_map_max=runtime;
        if(runtime>>25) ++rid_map_generations;
    }
#ifndef RID_OWN_PLATFORM
    for(unsigned i=0;created_live && i<created_live_count;i++) {
        rid_load_sum+=lockstep_mix(created_live[i].created*0x9e3779b97f4a7c15ULL^created_live[i].runtime);++rid_load_count;
    }
#endif
}
/* The list holds no slot below the agreed one and as many slots as the map says. */
static int rid_walk(unsigned char *map) {
    const rid_counts_t c=rid_counts(map);uint32_t at=c.head,seen=0;rid_below_base=0;
    for(;at && seen<=c.free;seen++) {
        const volatile uint64_t *p=rid_slot(map,at);if(!p) return rid_fail("walk_slot");
        if(at<rid_base) ++rid_below_base;
        at=(uint32_t)(*p>>32);
    }
    if(seen!=c.free) return rid_fail("walk_count");
    return rid_below_base?rid_fail("below_base"):1;
}
/* First live frame, main thread, after the identities are complete. */
static int rid_seal(void) {
    if(!rid_mode) return 1;
    rid_map_fold();int ok=!InterlockedCompareExchange64(&rid_failures,0,0);
    unsigned char *map=rid_map();
    if(map) {
        CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(map+0x10);EnterCriticalSection(lock);
        rid_now=rid_counts(map);rid_next_id=rid_next(map);memcpy(rid_local_now,rid_local,sizeof(rid_local_now));if(rid_mode==RID_SET && ok) ok=rid_walk(map);
        LeaveCriticalSection(lock);
    } else ok=rid_fail("map");
    rid_sealed=1;return ok;
}
/* Gate boundary (MoveWorker and passes joined): the ID the next allocation would get, the one thing clients must share. */
static int rid_frame(uint64_t tick) {
    if(!rid_mode) return 1;
    unsigned char *map=rid_map();if(!map) return rid_fail("map");
    CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(map+0x10);EnterCriticalSection(lock);rid_now=rid_counts(map);rid_next_id=rid_next(map);memcpy(rid_local_now,rid_local,sizeof(rid_local_now));LeaveCriticalSection(lock);
    rid_digest=lockstep_mix(lockstep_mix(rid_digest^tick)^rid_next_id);++rid_frames_done;
    if(rid_mode==RID_SET && rid_now.head && ((rid_now.head-1u)>>20)>=RID_BLOCK_LIMIT) rid_fail("ids_exhausted");
    return !InterlockedCompareExchange64(&rid_failures,0,0);
}
static void rid_restore(void) {
    InterlockedExchange(&rid_active,0);
    if(rid_installed && rid_swap(rid_patched_word,rid_original_word)) {rid_installed=0;rid_restored=1;}
}
static int rid_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"mode=%s base=%u installed=%d restored=%d stage=%s failures=%lld frame=%llu head_before=%u cut=%u skipped=%u "
        "next=%016llx allocs=%llu head=%u tail=%u free=%u live=%u inserts=%llu generation=%u dropped=%lld digest=%016llx map=%u,%016llx,%llx,%llx,gen:%u load=%u,%016llx below_base=%u callers=%u caller_overflow=%lld "
        "local=%d,ui:%llu/%llu/%u,view:%llu/%llu/%u",
        rid_mode==RID_SET?"set":rid_mode==RID_OBSERVE?"observe":"off",rid_base,rid_installed,rid_restored,rid_stage,(long long)rid_failures,(unsigned long long)rid_frames_done,
        rid_head_before,rid_cut,rid_skipped,(unsigned long long)rid_next_id,(unsigned long long)(rid_mode?rid_now.inserts-rid_before.inserts-rid_local_now[RID_ROUTE_UI].allocs-rid_local_now[RID_ROUTE_VIEW].allocs:0),rid_now.head,rid_now.tail,rid_now.free,rid_now.live,(unsigned long long)rid_now.inserts,rid_now.generation,(long long)rid_dropped,
        (unsigned long long)rid_digest,rid_map_count,(unsigned long long)rid_map_sum,(unsigned long long)(rid_map_count?rid_map_min:0),(unsigned long long)rid_map_max,rid_map_generations,
        rid_load_count,(unsigned long long)rid_load_sum,rid_below_base,rid_caller_count,(long long)rid_caller_overflow,(int)rid_local_on,
        (unsigned long long)rid_local_now[RID_ROUTE_UI].allocs,(unsigned long long)rid_local_now[RID_ROUTE_UI].frees,rid_local_now[RID_ROUTE_UI].free,
        (unsigned long long)rid_local_now[RID_ROUTE_VIEW].allocs,(unsigned long long)rid_local_now[RID_ROUTE_VIEW].frees,rid_local_now[RID_ROUTE_VIEW].free);
    return n>0 && (size_t)n<capacity?n:0;
}
static int rid_map_row(char *out,size_t capacity) {
    if(!rid_sealed) return 0;
    int n=snprintf(out,capacity,"count=%u sum=%016llx buckets=",rid_map_count,(unsigned long long)rid_map_sum);
    for(unsigned i=0;n>0 && (size_t)n<capacity && i<RID_BUCKETS;i++) n+=snprintf(out+n,capacity-(size_t)n,"%s%08x",i?",":"",rid_buckets[i]);
    return n>0 && (size_t)n<capacity?n:0;
}
/* The index-th used entry, in table order; 0 past the last. */
static int rid_caller_row(unsigned index,char *out,size_t capacity) {
    AcquireSRWLockShared(&rid_callers_lock);
    const rid_caller_t *found=NULL;unsigned seen=0;rid_caller_t copy;
    for(unsigned i=0;i<RID_CALLERS;i++) if(rid_callers[i].count && seen++==index) {copy=rid_callers[i];found=&copy;break;}
    ReleaseSRWLockShared(&rid_callers_lock);
    if(!found) return 0;
    static const char *const threads[3]={"moveworker","main","other"},*const phases[2]={"load","play"},*const routes[RID_ROUTES]={"shared","ui","view"};
    int n=snprintf(out,capacity,"phase=%s thread=%s route=%s count=%llu frames=",phases[found->phase&1u],found->thread<3u?threads[found->thread]:"?",found->route<RID_ROUTES?routes[found->route]:"?",(unsigned long long)found->count);
    for(unsigned i=0;n>0 && (size_t)n<capacity && i<RID_FRAMES;i++) n+=snprintf(out+n,capacity-(size_t)n,"%s%x",i?",":"",found->frames[i]);
    if(n>0 && (size_t)n<capacity && found->cue[0]) n+=snprintf(out+n,capacity-(size_t)n," cue=%s",found->cue);
    return n>0 && (size_t)n<capacity?n:0;
}
