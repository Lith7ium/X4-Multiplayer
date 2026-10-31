/* UI replay, DEFAULT OFF, stop reason 164 (native_gate_ui_replay; needs stock slots, the presentation
 * and native_gate_runtime_ids). Stock UI actions that change the world become events of this client's
 * slot: captured before they mutate, carried by the input wire (section 'ui', kind 0x55), applied on
 * every client at the capture tick plus the agreed delay, in the slot's scope, through X4's own function.
 * Design and inventory: reports/LOCKSTEP-UI-REPLAY-20261030.txt.
 *
 *   capture   only in a local phase: slot 6 (UI::CUIController::Move, entry hooked here), slot 10 (the
 *             render split's flag), the slot 4 dispatch of presentation ids, an open presentation scope.
 *             The local client never applies early. Simulation code on the same functions passes through.
 *   families  travel  0x9fd120 / 0x9fd330, where StartPlayerActivity, StopPlayerActivity and the action
 *                     handler d86c80 all end: post startactivity / stopactivity on the singleton. The J
 *                     key rides the stock record (action 0x12f, replayed for slots 2+ with this family).
 *             target  the two simulation blocks inlined in the UI's soft target writer 0xaecb60 (0xaecfdc
 *                     clear, 0xaed115 set) jump to a stub; the UI half keeps running at once. Match speed
 *                     (state 0x5b) leaves the refused list: it reads the target through 9fcd70, per scope.
 *             orders  RemoveAllOrders, RemoveAllOrders2 captured natively, Lua CreateOrder wrapped in Lua
 *                     (tools/dev/native_gate_ui_replay_core.lua), the queue editor's FFI mutators refused
 *                     here with their failure value (UIREP_GUARD), counted.
 *   apply     native events in path_release after the stock records of the tick, slot 1 canonical, slot k
 *             in its slot scope: 0x710ed0 for travel in a scope (md/modes.xml does not listen on P_k),
 *             0x9fd120 / 0x9fd330 on slot 1 (MD on every client); 0x3e73f0 / 0x3e7560 for the soft target.
 *             Lua-run events (op >= 0x40) in slot 6 of the same tick from the probe's frame, canonical.
 *   section   version | capture tick low 16 bits | count | events: op u8, length u8, data. A component is
 *             its runtime ID u64 and a 32-bit check of its portable ID (little endian): runtime IDs are the
 *             same on every client under native_gate_runtime_ids, a check that differs stops the session.
 *   receipt   PUIREP_1:tick:events:hash:results|activity/target of every slot's player joins the pilot receipt, so
 *             an event that took on one client only shows at once, whatever it was.
 * Refused at capture (nothing ran anywhere, nothing diverged): counted and logged (UI_REPLAY_REFUSED). A
 * section or reference that fails, a hook lost, a fault in a replayed call: stop reason 164. */
#define UIREP_STOP_REASON 164u
#define UIREP_VERSION 1u
#define UIREP_HEAD 4u
#define UIREP_SECTION_MAX 236u      /* INPUT_MUX_MAX less the container and this section's header */
#define UIREP_EVENT_MAX 200u
#define UIREP_EVENTS_MAX 32u
#define UIREP_QUEUE 64u
#define UIREP_RING 64u
#define UIREP_REF 12u
#define UIREP_OFFSET 16u
#define UIREP_DOMAIN 0x5549524550315631ULL /* "UIREP1V1" */
#define UIREP_ACTIVITY_TRAVEL 1
#define UIREP_ACTIVITY_MAX 4
#define UIREP_PLAYER_ACTIVITY 0x6ab4u
#define UIREP_PLAYER_LAST_SHIP 0x6a30u    /* last controlled ship's runtime ID: 0x710ed0 stops travel on it */
#define UIREP_LAST_TARGET 0x38f0a88u      /* cycling hint of the target actions, UI only; 0x3e73f0 writes it too */
#define UIREP_MAP 0x6cf1500u
#define UIREP_ASSOCIATION 0x48u           /* Component: UI::ICom::UIAssociation, slots 4 and 5 are the two functions */
#define UIREP_ELEMENT_OFFSET 0x20u        /* UI target element: offset in the target, +0x6e30 of the player */
#define UIREP_ELEMENT_FLAG 0x31u          /* UI target element: +0x6e20 of the player */
#define UIREP_ID 8u                       /* Component: its runtime ID */
#define UIREP_DRIVE_ACTIVE 0x340u         /* the travel drive of 0x6d6d80: charging or active (0xea7650) */
#define UIREP_SHIP_CAN_TRAVEL 1008u       /* Ship vtable: CanStartTravelMode's call (+0x1f80) */
#define UIREP_PAGE 0x1000u
#define UIREP_STRIDE 0x40u
#define UIREP_STUB 0x20u                  /* in a hook's stride: the jump the entry patch targets */
enum {UIREP_OP_ACTIVITY_START=1,UIREP_OP_ACTIVITY_STOP=2,UIREP_OP_TARGET_SET=3,UIREP_OP_TARGET_CLEAR=4,
    UIREP_OP_LUA_FIRST=0x40,UIREP_OP_ORDERS_CLEAR=0x41,UIREP_OP_ORDERS_CLEAR2=0x42,UIREP_OP_LUA_CALL=0x43};
enum {UIREP_H_UI=0,UIREP_H_START,UIREP_H_STOP,UIREP_H_CLEAR,UIREP_H_CLEAR2,UIREP_H_CREATE3,UIREP_H_ENABLE,UIREP_H_ADJUST,UIREP_H_REMOVE2,
    UIREP_H_RESET,UIREP_H_PLANNED_ENABLE,UIREP_H_PLANNED_REMOVE,UIREP_H_DEPLOY,UIREP_HOOKS};
enum {UIREP_S_TARGET_CLEAR=0,UIREP_S_TARGET_SET=1,UIREP_SITES=2};
typedef void (*uirep_activity_fn)(void *,int,unsigned char);
typedef uint64_t (*uirep_forward_fn)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef struct {
    void (*ui_move)(void *);
    uirep_activity_fn start,stop;                  /* 0x9fd120, 0x9fd330 */
    void (*set_activity)(void *,int);              /* 0x710ed0(player, activity) */
    unsigned char (*can_travel)(void *);           /* the ship's vtable slot 1008 */
    void **(*drive)(void *,void **);               /* 0x6d6d80(ship, out): its travel drive */
    void (*target_set)(void *,const void *,unsigned char);  /* 0x3e73f0(component+0x48, offset, flag) */
    void (*target_clear)(void *);                  /* 0x3e7560(component+0x48) */
    void *(*find)(void *,uint64_t,unsigned);       /* 0xced70 */
    int (*enter)(unsigned);                        /* pslot_enter */
    int (*leave)(unsigned);                        /* pslot_leave */
    uint64_t (*portable)(uint64_t);                /* world_identity_portable */
    uirep_forward_fn forward[UIREP_HOOKS];         /* the trampolines of the order hooks */
} uirep_fns;
typedef struct {unsigned char op,length;unsigned char data[UIREP_EVENT_MAX];} uirep_event;
typedef struct {uint64_t tick;unsigned present,length;unsigned char bytes[UIREP_SECTION_MAX];} uirep_staged;
typedef struct {uint64_t tick;const char *what,*why;} uirep_refusal;
#define UIREP_REFUSALS 32u
static uirep_fns uirep_fn;
static const char *uirep_stage_name="off",*uirep_why="-";
static int uirep_installed,uirep_restored=1,uirep_pins_checked;
static volatile LONG uirep_replaying;
static int uirep_stopped;     /* past the window: every wrapper is stock again, wherever the patches still are */
static unsigned char *uirep_page;
static unsigned char uirep_patched[UIREP_HOOKS][5],uirep_site_patched[UIREP_SITES][5];
static uirep_event uirep_queue[UIREP_QUEUE];
static unsigned uirep_queue_head,uirep_queue_count;
static uirep_staged uirep_ring[PILOT_SLOTS][UIREP_RING];
static uirep_refusal uirep_refusals[UIREP_REFUSALS];
static unsigned uirep_refusal_head,uirep_refusal_tail;
static uint64_t uirep_frame_tick,uirep_frame_hash,uirep_results_hash,uirep_results_tick;
static unsigned uirep_frame_events;
static volatile LONG64 uirep_captured,uirep_refused,uirep_taken,uirep_staged_count,uirep_applied_native,uirep_applied_lua,uirep_stale,
    uirep_passed,uirep_scope_start,uirep_scope_stop,uirep_scope_refused,uirep_scope_nested,uirep_unavailable,uirep_guard_refused,uirep_target_passed,uirep_lua_errors,
    uirep_waits;

/* ---- pins and sites (build 9.00) ---- */
static void uirep_ui_move(void *self);
static void uirep_activity_start(void *a,int activity,unsigned char flag);
static void uirep_activity_stop(void *a,int activity,unsigned char flag);
static uint64_t uirep_orders_clear(uint64_t id,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e);
static uint64_t uirep_orders_clear2(uint64_t id,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e);
static int uirep_local_phase(void);
static void uirep_guard_refuse(unsigned slot);
/* The queue editor's FFI mutators: a call from the UI gets the failure value, the others pass, and so does a call
 * with its check-only flag (the menus ask that to enable a button; it changes nothing). */
#define UIREP_GUARD(n,slot,checkonly) static uint64_t uirep_guard_##n(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f) { \
    if((checkonly) || InterlockedCompareExchange(&uirep_replaying,0,0) || uirep_stopped || !uirep_local_phase()) return uirep_fn.forward[slot](a,b,c,d,e,f); \
    uirep_guard_refuse(slot);return 0;}
UIREP_GUARD(create3,UIREP_H_CREATE3,0)
UIREP_GUARD(enable,UIREP_H_ENABLE,0)
UIREP_GUARD(adjust,UIREP_H_ADJUST,f&0xff)              /* (controllable, idx, newidx, enabled, forcestates, checkonly) */
UIREP_GUARD(remove2,UIREP_H_REMOVE2,d&0xff)            /* (controllable, idx, playercancelled, checkonly, onlyimmediate) */
UIREP_GUARD(reset,UIREP_H_RESET,0)
UIREP_GUARD(planned_enable,UIREP_H_PLANNED_ENABLE,b&0xff) /* (controllable, checkonly) */
UIREP_GUARD(planned_remove,UIREP_H_PLANNED_REMOVE,0)
UIREP_GUARD(deploy,UIREP_H_DEPLOY,0)
#define UIREP_ENTRY5 {0x48,0x89,0x5c,0x24,0x08}   /* mov [rsp+8],rbx */
#define UIREP_ENTRY7 {0x48,0x8b,0xc4,0x48,0x89,0x58,0x10}   /* mov rax,rsp; mov [rax+0x10],rbx */
static const struct {const char *name;unsigned rva,prefix,family;unsigned char bytes[9];void *wrapper;unsigned pin;const char *sha;} uirep_hooks[UIREP_HOOKS]={
    {"UIMove",0xb3bad0u,5,0,{0x48,0x89,0x5c,0x24,0x10},(void*)uirep_ui_move,206,"87a23ed23ef4be0fdf493a983342282860054eb3eb14f2388e134abc88762261"},
    {"StartActivity",0x9fd120u,7,UIREP_FAMILY_TRAVEL,UIREP_ENTRY7,(void*)uirep_activity_start,521,"3a686a762638a9d022d99bd44ab484ea5b225701adddf72b6cf40bf0d938df76"},
    {"StopActivity",0x9fd330u,7,UIREP_FAMILY_TRAVEL,UIREP_ENTRY7,(void*)uirep_activity_stop,501,"0b1d88c092f89e0479b3375abbd2a3c91fd96e4436b08decf71f7f6a441361b3"},
    {"RemoveAllOrders",0x1fe640u,5,UIREP_FAMILY_ORDERS,UIREP_ENTRY5,(void*)uirep_orders_clear,130,"5b18900b96d3f47edef7c8fd2fd6e547f5f94eeeb20c7992332d64b4a0ecfc2e"},
    {"RemoveAllOrders2",0x1fe6d0u,5,UIREP_FAMILY_ORDERS,UIREP_ENTRY5,(void*)uirep_orders_clear2,152,"27222818e2b7d5f90e2fc31f3e5b58f2f5014415de540580b4720f5e96bff171"},
    {"CreateOrder3",0x1f3b30u,5,UIREP_FAMILY_ORDERS,UIREP_ENTRY5,(void*)uirep_guard_create3,459,"5a806d32b71aa8658b46c2611794995ce5d02d1246906b42189043ac9df52f75"},
    {"EnableOrder",0x1f3d40u,5,UIREP_FAMILY_ORDERS,UIREP_ENTRY5,(void*)uirep_guard_enable,204,"b85f431536a8c264ad38455f0bb2fa5a95f5164764e8e30d4871ab59e8510a19"},
    {"AdjustOrder",0x1f2da0u,5,UIREP_FAMILY_ORDERS,UIREP_ENTRY5,(void*)uirep_guard_adjust,195,"204f34e28d4a7abea9e0577b4f76ff7ff270a6144c56099b0ef64eaf6d94f5f6"},
    {"RemoveOrder2",0x1fe9d0u,5,UIREP_FAMILY_ORDERS,UIREP_ENTRY5,(void*)uirep_guard_remove2,214,"a03780a483599329386676186590d88cf4c440e5154f81579a082447a46e40b6"},
    {"ResetOrder",0x1fef70u,5,UIREP_FAMILY_ORDERS,UIREP_ENTRY5,(void*)uirep_guard_reset,279,"bb81c2c0928e55754bb709555a6bbe6ac21669b9ae895f12cec327e2fd5c272a"},
    {"EnablePlannedDefaultOrder",0x1f3e10u,5,UIREP_FAMILY_ORDERS,UIREP_ENTRY5,(void*)uirep_guard_planned_enable,229,"1d5004fd5e115c97cc150dc2873d9c6904a8aa8c80e066c166ae7e927856e93b"},
    {"RemovePlannedDefaultOrder",0x1feed0u,5,UIREP_FAMILY_ORDERS,UIREP_ENTRY5,(void*)uirep_guard_planned_remove,150,"313a8f3739710504ac59d2723414bf80fa94d7db010664b730b58443219aa31e"},
    {"CreateDeployToStationOrder",0x1f37b0u,5,UIREP_FAMILY_ORDERS,UIREP_ENTRY5,(void*)uirep_guard_deploy,256,"2bdd9fdcb3a132a402b87fe3d4fbd66f8d5f6f33d891b511735a6016b951d0ea"},
};
/* The soft target blocks: five bytes inside one aligned 16-byte block (one CAS), the continuation behind each
 * block. The clear block starts across a 16-byte boundary, so the patch sits on the lookup's result in front of
 * it (mov rbx,rax; test rax,rax; jne 0xaecfdc): the stride repeats those and goes on to the stock error path at
 * 0xaecfcb without a component. */
#define UIREP_CLEAR_ERROR 0xaecfcbu
static const struct {unsigned rva,resume;unsigned char bytes[5];} uirep_sites[UIREP_SITES]={
    {0xaecfc3u,0xaed0b3u,{0x48,0x8b,0xd8,0x48,0x85}},  /* mov rbx,rax; test rax,rax */
    {0xaed115u,0xaed219u,{0x41,0x0f,0xb6,0x6d,0x31}},  /* movzx ebp,byte [r13+0x31]: rbx = the target, r13 = the element */
};
static const struct {unsigned rva,size;const char *sha;} uirep_pin_table[]={
    {0x710ed0u,1268,"9ec1a432995414451cee06a8e063412551a756680fb20e3e6a32097dfa90c4c9"},  /* the activity setter */
    {0x55aeb0u,142,"b95c394b389e23c19cd668e9d15d5210cafde0810e5ebe1d3d957a73757b2a51"},   /* the player's ship */
    {0x7e1c80u,301,"f60d70b2f2888c1ae13328d2ca150332b3f47df28b5dddbcc3b85280ceae8a5c"},   /* Ship::vfunc1008, travel available */
    {0x6d6d80u,40,"4e40c27773f0457d146b6307457bffe330fa2fc92c933d4c53d6199954d1c0aa"},    /* the ship's travel drive */
    {0x314e80u,34,"69af1b534cdebe97eee6c3732cfc8fbb7ac33bf31e991ec53f92933ad43121cc"},    /* a stored ID's component (+0x6a30) */
    {0x3e73f0u,355,"bac88a495e92607e803b53d50bb580380adcfeca989e59cc465d4ccbd0dd586c"},   /* target set */
    {0x3e7560u,288,"a9edb6b8cfea1d92476c39ac4f09760431e347dba5a66ddb14242241be5abeee"},   /* target clear */
    {0xaecb60u,1915,"eec1c6e468a9a2dfa26dab7fd4915b96aea64f5d4a78d7c8d4afc1e48d32fa57"},  /* the UI's writer, both sites */
};
#define UIREP_PIN_COUNT (sizeof(uirep_pin_table)/sizeof(*uirep_pin_table))
static int uirep_pins(void) {
    if(!uirep_pins_checked) {
        int ok=1;
        for(unsigned i=0;ok && i<UIREP_HOOKS;i++)
            if(!(ok=hash_ok(image+uirep_hooks[i].rva,uirep_hooks[i].pin,uirep_hooks[i].sha) && !memcmp(image+uirep_hooks[i].rva,uirep_hooks[i].bytes,uirep_hooks[i].prefix))) pin_failed_rva=uirep_hooks[i].rva;
        for(unsigned i=0;ok && i<UIREP_PIN_COUNT;i++)
            if(!(ok=hash_ok(image+uirep_pin_table[i].rva,uirep_pin_table[i].size,uirep_pin_table[i].sha))) pin_failed_rva=uirep_pin_table[i].rva;
        for(unsigned i=0;ok && i<UIREP_SITES;i++)
            if(!(ok=!memcmp(image+uirep_sites[i].rva,uirep_sites[i].bytes,5))) pin_failed_rva=uirep_sites[i].rva;
        uirep_pins_checked=ok?1:-1;
    }
    return uirep_pins_checked==1;
}
static unsigned char uirep_can_travel_vtable(void *ship) {return ((unsigned char(*)(void*))(*(void***)ship)[UIREP_SHIP_CAN_TRAVEL])(ship);}
static void uirep_use_image(void) {
    uirep_fn.set_activity=(void(*)(void*,int))(image+0x710ed0u);uirep_fn.can_travel=uirep_can_travel_vtable;
    uirep_fn.drive=(void**(*)(void*,void**))(image+0x6d6d80u);
    uirep_fn.target_set=(void(*)(void*,const void*,unsigned char))(image+0x3e73f0u);uirep_fn.target_clear=(void(*)(void*))(image+0x3e7560u);
    uirep_fn.find=(void*(*)(void*,uint64_t,unsigned))(image+0xced70u);
    uirep_fn.enter=pslot_enter;uirep_fn.leave=pslot_leave;uirep_fn.portable=world_identity_portable;
}

static int uirep_fail(const char *why) {uirep_stage_name="failed";if(!strcmp(uirep_why,"-")) uirep_why=why;uirep_failed=1;return 0;}
/* ---- capture ---- */
static int uirep_local_phase(void) {
    return GetCurrentThreadId()==ui_thread && (uirep_ui_depth>0 || uirep_dispatch_depth>0 || pres_open || render_rng_in_slot);
}
static int uirep_capture_now(void) {return !InterlockedCompareExchange(&uirep_replaying,0,0) && !uirep_stopped && uirep_local_phase();}
static void uirep_note_refusal(const char *what,const char *why) {
    InterlockedIncrement64(&uirep_refused);
    if(uirep_refusal_head-uirep_refusal_tail>=UIREP_REFUSALS) return;
    uirep_refusal *r=&uirep_refusals[uirep_refusal_head%UIREP_REFUSALS];
    r->tick=stats.issued;r->what=what;r->why=why;++uirep_refusal_head;
}
static void uirep_guard_refuse(unsigned slot) {
    InterlockedIncrement64(&uirep_guard_refused);uirep_note_refusal(uirep_hooks[slot].name,"not_replayed_yet");
}
static const char *uirep_op_name(unsigned op) {
    switch(op) {
    case UIREP_OP_ACTIVITY_START:return "activity_start";
    case UIREP_OP_ACTIVITY_STOP:return "activity_stop";
    case UIREP_OP_TARGET_SET:return "target_set";
    case UIREP_OP_TARGET_CLEAR:return "target_clear";
    case UIREP_OP_ORDERS_CLEAR:return "orders_clear";
    case UIREP_OP_ORDERS_CLEAR2:return "orders_clear2";
    case UIREP_OP_LUA_CALL:return "lua_call";
    default:return "?";
    }
}
/* One event of this client's slot, in call order. 0 = refused, nothing ran. */
static int uirep_push(unsigned op,const unsigned char *data,unsigned length) {
    const char *why=NULL;
    if(!uirep_armed || !pstk_capturing) why="not_capturing";
    else if(length>UIREP_EVENT_MAX) why="too_long";
    else if(!pstk_capture_start || stats.issued<pstk_capture_start || stats.issued>pilot_stop) why="not_live";
    else if(uirep_queue_count>=UIREP_QUEUE) why="queue_full";
    if(why) {uirep_note_refusal(uirep_op_name(op),why);return 0;}
    uirep_event *e=&uirep_queue[(uirep_queue_head+uirep_queue_count)%UIREP_QUEUE];
    e->op=(unsigned char)op;e->length=(unsigned char)length;memcpy(e->data,data,length);++uirep_queue_count;
    InterlockedIncrement64(&uirep_captured);return 1;
}
static uint32_t uirep_check_of(uint64_t runtime) {
    uint64_t portable=uirep_fn.portable?uirep_fn.portable(runtime):0;
    return portable?(uint32_t)lockstep_mix(portable)|1u:0u;
}
static void uirep_put_ref(unsigned char *out,uint64_t runtime) {
    uint32_t check=uirep_check_of(runtime);
    for(unsigned i=0;i<8;i++) out[i]=(unsigned char)(runtime>>(8*i));
    for(unsigned i=0;i<4;i++) out[8+i]=(unsigned char)(check>>(8*i));
}
static uint64_t uirep_get_ref(const unsigned char *in,uint32_t *check) {
    uint64_t runtime=0;uint32_t c=0;
    for(unsigned i=0;i<8;i++) runtime|=(uint64_t)in[i]<<(8*i);
    for(unsigned i=0;i<4;i++) c|=(uint32_t)in[8+i]<<(8*i);
    *check=c;return runtime;
}

/* ---- travel: the two functions every activity start and stop ends in ---- */
static int uirep_activity_valid(unsigned slot,int activity,int stop) {
    if(activity==UIREP_ACTIVITY_TRAVEL) return 1;
    if(slot>=2) return 0;                                  /* slots 2+: the adapter below has travel only */
    return activity>=(stop?0:1) && activity<=UIREP_ACTIVITY_MAX;
}
static void uirep_activity_capture(unsigned op,int activity,unsigned char flag) {
    if(!uirep_activity_valid(stats.slot,activity,op==UIREP_OP_ACTIVITY_STOP)) {uirep_note_refusal(uirep_op_name(op),"activity");return;}
    unsigned char data[2]={(unsigned char)activity,flag!=0};
    uirep_push(op,data,2);
}
static int uirep_drive_active(void *ship) {
    void *drive=NULL;unsigned char active=0;
    uirep_fn.drive(ship,&drive);
    return drive && read_local((unsigned char*)drive+UIREP_DRIVE_ACTIVE,&active,1) && active;
}
/* The setter tearing a slot's travel down: 0x6d9190 posts TravelModeChargeAborted / Stopped on the singleton at once,
 * U::Player's slots 262 and 264 (0x706000) answer with 0x9fd330 while the activity still reads travel (0x710ed0 writes it
 * after the teardown). In stock that is an event for MD, harmless by then; here it comes back into the adapter, and into
 * the setter (live u1..u3: stack overflow at the stop). What the setter causes is the setter's. */
static int uirep_setter_depth;
static void uirep_set_activity(pslot_slot *s,int activity) {
    ++uirep_setter_depth;
    __try {uirep_fn.set_activity(s->player,activity);}
    __finally {--uirep_setter_depth;}
}
/* md/modes.xml for travel on slot k's player: its check, then its set_player_activity. */
static void uirep_adapter_start(pslot_slot *s,int activity) {
    if(uirep_setter_depth) {InterlockedIncrement64(&uirep_scope_nested);return;}
    int current=0;memcpy(&current,s->player+UIREP_PLAYER_ACTIVITY,4);
    if(activity==current) return;
    if(activity!=UIREP_ACTIVITY_TRAVEL) {InterlockedIncrement64(&uirep_scope_refused);return;}
    if(!uirep_fn.can_travel(s->ship) && !uirep_drive_active(s->ship)) {InterlockedIncrement64(&uirep_unavailable);return;}
    uint64_t id=0;
    if(read_local(s->ship+UIREP_ID,&id,8)) memcpy(s->player+UIREP_PLAYER_LAST_SHIP,&id,8);
    uirep_set_activity(s,activity);
    InterlockedIncrement64(&uirep_scope_start);
}
static void uirep_adapter_stop(pslot_slot *s,int activity) {
    if(uirep_setter_depth) {InterlockedIncrement64(&uirep_scope_nested);return;}
    int current=0;memcpy(&current,s->player+UIREP_PLAYER_ACTIVITY,4);
    if(!current || activity!=current) return;
    uirep_set_activity(s,0);
    InterlockedIncrement64(&uirep_scope_stop);
}
static pslot_slot *uirep_scope_of_thread(void) {
    return pstk_scope_slot>=2 && GetCurrentThreadId()==ui_thread?&pslots[pstk_scope_slot]:NULL;
}
static void uirep_activity_start(void *a,int activity,unsigned char flag) {
    pslot_slot *s=uirep_scope_of_thread();
    if(s) {uirep_adapter_start(s,activity);return;}
    if(uirep_capture_now()) {uirep_activity_capture(UIREP_OP_ACTIVITY_START,activity,flag);return;}
    InterlockedIncrement64(&uirep_passed);uirep_fn.start(a,activity,flag);
}
static void uirep_activity_stop(void *a,int activity,unsigned char flag) {
    pslot_slot *s=uirep_scope_of_thread();
    if(s) {uirep_adapter_stop(s,activity);return;}
    if(uirep_capture_now()) {uirep_activity_capture(UIREP_OP_ACTIVITY_STOP,activity,flag);return;}
    InterlockedIncrement64(&uirep_passed);uirep_fn.stop(a,activity,flag);
}

/* ---- soft target: the blocks of 0xaecb60; outside a local phase (MD) the stock functions at once ---- */
static uint64_t uirep_component_id(const unsigned char *component) {
    uint64_t id=0;if(component) read_local(component+UIREP_ID,&id,8);return id;
}
static void uirep_set_hint(uint64_t id) {*(volatile uint64_t*)(pnat_base+UIREP_LAST_TARGET)=id;}
static uint64_t uirep_get_hint(void) {return *(volatile uint64_t*)(pnat_base+UIREP_LAST_TARGET);}
static void uirep_target_set_stub(unsigned char *component,unsigned char *element) {
    unsigned char flag=0,offset[UIREP_OFFSET];
    memset(offset,0,sizeof(offset));
    read_local(element+UIREP_ELEMENT_FLAG,&flag,1);read_local(element+UIREP_ELEMENT_OFFSET,offset,UIREP_OFFSET);
    if(!uirep_capture_now()) {
        InterlockedIncrement64(&uirep_target_passed);
        uirep_fn.target_set(component+UIREP_ASSOCIATION,offset,flag);return;
    }
    uint64_t id=uirep_component_id(component);unsigned char data[UIREP_REF+1+UIREP_OFFSET];
    uirep_put_ref(data,id);data[UIREP_REF]=flag;memcpy(data+UIREP_REF+1,offset,UIREP_OFFSET);
    if(uirep_push(UIREP_OP_TARGET_SET,data,sizeof(data))) uirep_set_hint(id);
}
static void uirep_target_clear_stub(unsigned char *component) {
    if(!uirep_capture_now()) {
        InterlockedIncrement64(&uirep_target_passed);
        uirep_fn.target_clear(component+UIREP_ASSOCIATION);return;
    }
    unsigned char data[UIREP_REF];uint64_t id=uirep_component_id(component);
    uirep_put_ref(data,id);
    if(uirep_push(UIREP_OP_TARGET_CLEAR,data,sizeof(data))) uirep_set_hint(0);
}

/* ---- orders: the FFI side (Lua runs them, in order with the Lua calls) ---- */
static uint64_t uirep_orders_clear(uint64_t id,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e) {
    if(!uirep_capture_now()) {InterlockedIncrement64(&uirep_passed);return uirep_fn.forward[UIREP_H_CLEAR](id,a,b,c,d,e);}
    unsigned char data[UIREP_REF];uirep_put_ref(data,id);
    return (uint64_t)uirep_push(UIREP_OP_ORDERS_CLEAR,data,sizeof(data));
}
static uint64_t uirep_orders_clear2(uint64_t id,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e) {
    if(!uirep_capture_now()) {InterlockedIncrement64(&uirep_passed);return uirep_fn.forward[UIREP_H_CLEAR2](id,a,b,c,d,e);}
    unsigned char data[UIREP_REF+1];uirep_put_ref(data,id);
    data[UIREP_REF]=(unsigned char)(((a&0xff)!=0)|(((b&0xff)!=0)<<1));
    return (uint64_t)uirep_push(UIREP_OP_ORDERS_CLEAR2,data,sizeof(data));
}
static void uirep_ui_move(void *self) {
    InterlockedIncrement(&uirep_ui_depth);
    __try {uirep_fn.ui_move(self);}
    __finally {InterlockedDecrement(&uirep_ui_depth);}
}

/* ---- the section ---- */
static int uirep_event_ok(unsigned slot,unsigned op,const unsigned char *data,unsigned n,const char **why) {
    switch(op) {
    case UIREP_OP_ACTIVITY_START:case UIREP_OP_ACTIVITY_STOP:
        if(!(uirep_families&UIREP_FAMILY_TRAVEL) || n!=2 || data[1]>1 || !uirep_activity_valid(slot,data[0],op==UIREP_OP_ACTIVITY_STOP)) {*why="ui_activity";return 0;}
        return 1;
    case UIREP_OP_TARGET_SET:
        if(!(uirep_families&UIREP_FAMILY_TARGET) || n!=UIREP_REF+1+UIREP_OFFSET || data[UIREP_REF]>1) {*why="ui_target";return 0;}
        return 1;
    case UIREP_OP_TARGET_CLEAR:
        if(!(uirep_families&UIREP_FAMILY_TARGET) || n!=UIREP_REF) {*why="ui_target";return 0;}
        return 1;
    case UIREP_OP_ORDERS_CLEAR:
        if(!(uirep_families&UIREP_FAMILY_ORDERS) || n!=UIREP_REF) {*why="ui_orders";return 0;}
        return 1;
    case UIREP_OP_ORDERS_CLEAR2:
        if(!(uirep_families&UIREP_FAMILY_ORDERS) || n!=UIREP_REF+1 || data[UIREP_REF]>3) {*why="ui_orders";return 0;}
        return 1;
    case UIREP_OP_LUA_CALL:
        if(!(uirep_families&UIREP_FAMILY_ORDERS) || !n) {*why="ui_orders";return 0;}
        return 1;
    default:*why="ui_op";return 0;
    }
}
/* A whole section as the wire carries it; on the wire every byte is accounted for. */
static int uirep_section_ok(unsigned slot,const unsigned char *b,unsigned n,uint16_t *capture16,const char **why) {
    if(n<UIREP_HEAD+2 || n>UIREP_SECTION_MAX || b[0]!=UIREP_VERSION) {*why="ui_section";return 0;}
    unsigned count=b[3],at=UIREP_HEAD;
    if(!count || count>UIREP_EVENTS_MAX) {*why="ui_section";return 0;}
    for(unsigned i=0;i<count;i++) {
        if(n-at<2) {*why="ui_section";return 0;}
        unsigned op=b[at],length=b[at+1];at+=2;
        if(length>n-at || length>UIREP_EVENT_MAX) {*why="ui_section";return 0;}
        if(!uirep_event_ok(slot,op,b+at,length,why)) return 0;
        at+=length;
    }
    if(at!=n) {*why="ui_section";return 0;}
    *capture16=(uint16_t)(b[1]|b[2]<<8);return 1;
}
/* This tick's section of the queued events that fit (Lua, the mux's proposal): 0 = nothing this tick. */
static unsigned uirep_take(uint64_t tick,unsigned budget,unsigned char *out) {
    if(budget>UIREP_SECTION_MAX) budget=UIREP_SECTION_MAX;
    if(!uirep_queue_count) return 0;
    unsigned at=UIREP_HEAD,count=0;
    while(count<uirep_queue_count && count<UIREP_EVENTS_MAX) {
        const uirep_event *e=&uirep_queue[(uirep_queue_head+count)%UIREP_QUEUE];
        if(at+2u+e->length>budget) break;
        out[at]=e->op;out[at+1]=e->length;memcpy(out+at+2,e->data,e->length);at+=2u+e->length;++count;
    }
    if(!count) {InterlockedIncrement64(&uirep_waits);return 0;}
    out[0]=UIREP_VERSION;out[1]=(unsigned char)tick;out[2]=(unsigned char)(tick>>8);out[3]=(unsigned char)count;
    uirep_queue_head=(uirep_queue_head+count)%UIREP_QUEUE;uirep_queue_count-=count;
    InterlockedExchangeAdd64(&uirep_taken,(LONG64)count);
    return at;
}
/* Staging from the wire (every client): one apply tick, still ahead of every pass. */
static int uirep_stage(unsigned slot,const unsigned char *bytes,unsigned length,uint64_t now,const char **why) {
    uint16_t capture16=0;
    if(slot<1 || slot>PILOT_SLOTS || !(pstk_slot_mask>>(slot-1)&1u)) {*why="ui_slot";return 0;}
    if(!uirep_section_ok(slot,bytes,length,&capture16,why)) return 0;
    uint64_t back=(uint16_t)(now-capture16);
    if(back>now) {*why="ui_tick";return 0;}
    uint64_t capture=now-back,apply=capture+pilot_delay;
    if(!capture || capture<pstk_capture_start || apply<=now || apply>=now+UIREP_RING) {*why="ui_late";return 0;}
    uirep_staged *e=&uirep_ring[slot-1][apply%UIREP_RING];
    if(e->present && e->tick==apply) {
        if(e->length==length && !memcmp(e->bytes,bytes,length)) return 1;
        *why="ui_conflict";return 0;
    }
    if(e->present && e->tick>now) {*why="ui_ring";return 0;}
    memset(e,0,sizeof(*e));e->tick=apply;e->length=length;memcpy(e->bytes,bytes,length);e->present=1;
    InterlockedIncrement64(&uirep_staged_count);return 1;
}

/* ---- apply: native events of the tick (path_release) ---- */
static unsigned char *uirep_component(uint64_t runtime) {
    void *map=NULL;
    if(!uirep_fn.find || !read_local(pnat_base+UIREP_MAP,&map,8)) return NULL;
    return (unsigned char*)uirep_fn.find(map,runtime,4);
}
/* 1 = resolved, 0 = gone (the same on every client), -1 = the identities differ: stop. */
static int uirep_resolve(const unsigned char *data,uint64_t *runtime,unsigned char **component) {
    uint32_t check=0;*runtime=uirep_get_ref(data,&check);
    *component=*runtime?uirep_component(*runtime):NULL;
    if(!*component) return 0;
    return uirep_check_of(*runtime)==check?1:-1;
}
static int uirep_run_native(unsigned slot,unsigned op,const unsigned char *data,const char **why) {
    uint64_t runtime=0;unsigned char *component=NULL;
    switch(op) {
    case UIREP_OP_ACTIVITY_START:
        if(slot>=2) uirep_adapter_start(&pslots[slot],data[0]);else uirep_fn.start(NULL,data[0],data[1]);
        break;
    case UIREP_OP_ACTIVITY_STOP:
        if(slot>=2) uirep_adapter_stop(&pslots[slot],data[0]);else uirep_fn.stop(NULL,data[0],data[1]);
        break;
    case UIREP_OP_TARGET_SET:case UIREP_OP_TARGET_CLEAR: {
        int resolved=uirep_resolve(data,&runtime,&component);
        if(resolved<0) {*why="ui_ref_mismatch";return 0;}
        if(!resolved) {InterlockedIncrement64(&uirep_stale);break;}
        /* The stock functions also write the cycling hint: it stays what this client's UI left. */
        uint64_t hint=uirep_get_hint();
        if(op==UIREP_OP_TARGET_SET) {
            unsigned char offset[UIREP_OFFSET];memcpy(offset,data+UIREP_REF+1,UIREP_OFFSET);
            uirep_fn.target_set(component+UIREP_ASSOCIATION,offset,data[UIREP_REF]);
        } else uirep_fn.target_clear(component+UIREP_ASSOCIATION);
        uirep_set_hint(hint);
        break;
    }
    default:break;
    }
    InterlockedIncrement64(&uirep_applied_native);return 1;
}
static int uirep_run_section(unsigned slot,const uirep_staged *e,const char **why) {
    unsigned count=e->bytes[3],at=UIREP_HEAD;
    for(unsigned i=0;i<count;i++) {
        unsigned op=e->bytes[at],length=e->bytes[at+1];const unsigned char *data=e->bytes+at+2;
        if(op<UIREP_OP_LUA_FIRST && !uirep_run_native(slot,op,data,why)) return 0;
        at+=2u+length;
    }
    return 1;
}
static int uirep_has_native(const uirep_staged *e) {
    unsigned count=e->bytes[3],at=UIREP_HEAD;
    for(unsigned i=0;i<count;i++) {
        if(e->bytes[at]<UIREP_OP_LUA_FIRST) return 1;
        at+=2u+e->bytes[at+1];
    }
    return 0;
}
static int uirep_frame_run(uint64_t tick,const char **why) {
    uirep_frame_tick=tick;uirep_frame_events=0;uirep_frame_hash=0;
    for(unsigned k=1;k<=PILOT_SLOTS;k++) {
        const uirep_staged *e=&uirep_ring[k-1][tick%UIREP_RING];
        if(!e->present || e->tick!=tick) continue;
        uirep_frame_events+=e->bytes[3];
        uirep_frame_hash=pstk_fnv(e->bytes,e->length,uirep_frame_hash^(UIREP_DOMAIN+k));
        if(!uirep_has_native(e)) continue;
        if(pdeath_armed && (pdeath_dead>>(k-1)&1u)) {InterlockedIncrement64(&uirep_stale);continue;} /* no ship: the hash stays, nothing applies */
        int scoped=k>=2,ok=0,left=1,fault=0;
        if(scoped && !uirep_fn.enter(k)) {*why="ui_scope";return 0;}
        InterlockedExchange(&uirep_replaying,1);
        __try {ok=uirep_run_section(k,e,why);}
        __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
        InterlockedExchange(&uirep_replaying,0);
        if(scoped) left=uirep_fn.leave(k);
        if(fault) {*why="ui_fault";return 0;}
        if(!left) {*why="ui_scope_leave";return 0;}
        if(!ok) return 0;
    }
    return 1;
}
static int uirep_frame(uint64_t tick,const char **why) {
    return uirep_frame_run(tick,why) || uirep_fail(*why);
}
/* The Lua-run events of this tick for the probe's frame: "slot:op:hex;...", NULL when none. */
static int uirep_lua_events(uint64_t tick,char *out,size_t capacity) {
    int n=0;
    for(unsigned k=1;k<=PILOT_SLOTS;k++) {
        uirep_staged *e=&uirep_ring[k-1][tick%UIREP_RING];
        if(!e->present || e->tick!=tick) continue;
        unsigned count=e->bytes[3],at=UIREP_HEAD;
        for(unsigned i=0;i<count;i++) {
            unsigned op=e->bytes[at],length=e->bytes[at+1];
            if(op>=UIREP_OP_LUA_FIRST) {
                char hex[2*UIREP_EVENT_MAX+1];pilot_hex(e->bytes+at+2,length,hex);
                int more=snprintf(out+n,capacity-(size_t)n,"%s%u:%u:%s",n?";":"",k,op,hex);
                if(more<0 || (size_t)more>=capacity-(size_t)n) return -1;
                n+=more;InterlockedIncrement64(&uirep_applied_lua);
            }
            at+=2u+length;
        }
        e->present=0;
    }
    return n;
}
static void uirep_result(uint64_t tick,int ok) {
    if(uirep_results_tick!=tick) {uirep_results_tick=tick;uirep_results_hash=0;}
    unsigned char b=(unsigned char)(ok!=0);uirep_results_hash=pstk_fnv(&b,1,uirep_results_hash^UIREP_DOMAIN);
    if(!ok) InterlockedIncrement64(&uirep_lua_errors);
}
static int uirep_frame_text(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"PUIREP_1:%llu:%u:%016llx:%016llx",(unsigned long long)uirep_frame_tick,uirep_frame_events,(unsigned long long)uirep_frame_hash,
        (unsigned long long)(uirep_results_tick==uirep_frame_tick?uirep_results_hash:0));
    for(unsigned k=1;n>0 && (size_t)n<capacity && k<=pilot_slot_count && k<=PILOT_SLOTS;k++) {
        const unsigned char *player=k==1?pnat_player():pslots[k].player;int activity=0;uint64_t target=0;
        if(player) {read_local(player+UIREP_PLAYER_ACTIVITY,&activity,4);read_local(player+PRES_TARGET,&target,8);}
        if(target && !uirep_component(target)) target=0;   /* dead: 9fcd70 clears it when someone asks, which is not simulation */
        int more=snprintf(out+n,capacity-(size_t)n,"%s%d/%llx",k==1?"|":",",activity,(unsigned long long)target);
        if(more<0) return 0;
        n+=more;
    }
    return n>0 && (size_t)n<capacity?n:0;
}

/* ---- install ---- */
static unsigned char *uirep_alloc_page(void) {
    uintptr_t at=(uintptr_t)(pnat_base+0x800000u)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            unsigned char *p=VirtualAlloc((void*)(sign<0?at-distance:at+distance),UIREP_PAGE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            intptr_t a=(intptr_t)p-(intptr_t)(pnat_base+0x100000u),b=(intptr_t)p-(intptr_t)(pnat_base+0xc00000u);
            if(a>INT32_MIN+0x10000 && a<INT32_MAX-0x10000 && b>INT32_MIN+0x10000 && b<INT32_MAX-0x10000) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int uirep_hook_used(unsigned i) {return !uirep_hooks[i].family || (uirep_families&uirep_hooks[i].family);}
static unsigned char *uirep_stride(unsigned i) {return uirep_page+UIREP_STRIDE*i;}
static unsigned char *uirep_site_stride(unsigned i) {return uirep_page+UIREP_STRIDE*(UIREP_HOOKS+i);}
/* mov rcx,rbx; [mov rdx,r13;] mov rax,stub; call rax; jmp [rip] resume: the frame is X4's, aligned, with its shadow
 * space, and rbx, r13 and rdi (zero) survive the stub. */
static unsigned uirep_write_site(unsigned i,unsigned char *p) {
    static const unsigned char head[8]={0x48,0x8b,0xd8,0x48,0x85,0xc0,0x75,0x0e},mov_rcx[3]={0x48,0x89,0xd9},mov_rdx[3]={0x4c,0x89,0xea},call_rax[2]={0xff,0xd0};
    void *stub=i==UIREP_S_TARGET_SET?(void*)uirep_target_set_stub:(void*)uirep_target_clear_stub;unsigned n=0;
    memset(p,0xcc,UIREP_STRIDE);
    if(i==UIREP_S_TARGET_CLEAR) {   /* no component: the stock error path; the jne hops over this 14-byte jump */
        memcpy(p,head,8);n=8;n+=pview_abs_jump(p+n,pnat_base+UIREP_CLEAR_ERROR);
    }
    memcpy(p+n,mov_rcx,3);n+=3;
    if(i==UIREP_S_TARGET_SET) {memcpy(p+n,mov_rdx,3);n+=3;}
    n+=pview_mov_rax(p+n,stub);memcpy(p+n,call_rax,2);n+=2;
    n+=pview_abs_jump(p+n,pnat_base+uirep_sites[i].resume);
    return n;
}
static void uirep_write_hook(unsigned i,unsigned char *p) {
    memset(p,0xcc,UIREP_STRIDE);
    memcpy(p,uirep_hooks[i].bytes,uirep_hooks[i].prefix);
    pview_abs_jump(p+uirep_hooks[i].prefix,pnat_base+uirep_hooks[i].rva+uirep_hooks[i].prefix);
    pview_abs_jump(p+UIREP_STUB,uirep_hooks[i].wrapper);
}
static int uirep_write(unsigned rva,const unsigned char *expected,const unsigned char *value) {return pview_cas(rva,expected,value,5);}
static void uirep_uninstall(void) {
    for(unsigned i=UIREP_SITES;i-->0;) {
        if(!(uirep_families&UIREP_FAMILY_TARGET) || !uirep_page || memcmp(pnat_base+uirep_sites[i].rva,uirep_site_patched[i],5)) continue;
        uirep_write(uirep_sites[i].rva,uirep_site_patched[i],uirep_sites[i].bytes);
    }
    for(unsigned i=UIREP_HOOKS;i-->0;) {
        if(!uirep_hook_used(i) || !uirep_page || memcmp(pnat_base+uirep_hooks[i].rva,uirep_patched[i],5)) continue;
        uirep_write(uirep_hooks[i].rva,uirep_patched[i],uirep_hooks[i].bytes);
    }
    int clean=1;
    for(unsigned i=0;i<UIREP_HOOKS;i++) if(uirep_hook_used(i) && uirep_page && memcmp(pnat_base+uirep_hooks[i].rva,uirep_hooks[i].bytes,5)) clean=0;
    if(uirep_families&UIREP_FAMILY_TARGET) for(unsigned i=0;i<UIREP_SITES;i++) if(uirep_page && memcmp(pnat_base+uirep_sites[i].rva,uirep_sites[i].bytes,5)) clean=0;
    uirep_restored=clean;
    if(clean) uirep_installed=0;
}
static int uirep_install(void) {
    if(uirep_installed) return 1;
    for(unsigned i=0;i<UIREP_HOOKS;i++) if(uirep_hook_used(i) && memcmp(pnat_base+uirep_hooks[i].rva,uirep_hooks[i].bytes,uirep_hooks[i].prefix)) return 0;
    if(uirep_families&UIREP_FAMILY_TARGET) for(unsigned i=0;i<UIREP_SITES;i++) if(memcmp(pnat_base+uirep_sites[i].rva,uirep_sites[i].bytes,5)) return 0;
    if(!uirep_page && !(uirep_page=uirep_alloc_page())) return 0;
    DWORD old,ignored;
    if(!VirtualProtect(uirep_page,UIREP_PAGE,PAGE_READWRITE,&old)) return 0;
    for(unsigned i=0;i<UIREP_HOOKS;i++) uirep_write_hook(i,uirep_stride(i));
    for(unsigned i=0;i<UIREP_SITES;i++) uirep_write_site(i,uirep_site_stride(i));
    if(!VirtualProtect(uirep_page,UIREP_PAGE,PAGE_EXECUTE_READ,&ignored)) return 0;
    FlushInstructionCache(GetCurrentProcess(),uirep_page,UIREP_PAGE);
    if(!uirep_fn.ui_move) uirep_fn.ui_move=(void(*)(void*))uirep_stride(UIREP_H_UI);
    if(!uirep_fn.start) uirep_fn.start=(uirep_activity_fn)uirep_stride(UIREP_H_START);
    if(!uirep_fn.stop) uirep_fn.stop=(uirep_activity_fn)uirep_stride(UIREP_H_STOP);
    for(unsigned i=UIREP_H_CLEAR;i<UIREP_HOOKS;i++) if(!uirep_fn.forward[i]) uirep_fn.forward[i]=(uirep_forward_fn)uirep_stride(i);
    for(unsigned i=0;i<UIREP_HOOKS;i++) {
        int32_t rel=(int32_t)((intptr_t)(uirep_stride(i)+UIREP_STUB)-(intptr_t)(pnat_base+uirep_hooks[i].rva+5));
        uirep_patched[i][0]=0xe9;memcpy(uirep_patched[i]+1,&rel,4);
    }
    for(unsigned i=0;i<UIREP_SITES;i++) {
        int32_t rel=(int32_t)((intptr_t)uirep_site_stride(i)-(intptr_t)(pnat_base+uirep_sites[i].rva+5));
        uirep_site_patched[i][0]=0xe9;memcpy(uirep_site_patched[i]+1,&rel,4);
    }
    uirep_restored=0;uirep_installed=1;
    for(unsigned i=0;i<UIREP_HOOKS;i++)
        if(uirep_hook_used(i) && !uirep_write(uirep_hooks[i].rva,uirep_hooks[i].bytes,uirep_patched[i])) {uirep_uninstall();uirep_installed=0;return 0;}
    if(uirep_families&UIREP_FAMILY_TARGET) for(unsigned i=0;i<UIREP_SITES;i++)
        if(!uirep_write(uirep_sites[i].rva,uirep_sites[i].bytes,uirep_site_patched[i])) {uirep_uninstall();uirep_installed=0;return 0;}
    return 1;
}
static int uirep_intact(void) {
    if(!uirep_installed || !uirep_page) return 0;
    for(unsigned i=0;i<UIREP_HOOKS;i++) if(uirep_hook_used(i) && memcmp(pnat_base+uirep_hooks[i].rva,uirep_patched[i],5)) return 0;
    if(uirep_families&UIREP_FAMILY_TARGET) for(unsigned i=0;i<UIREP_SITES;i++) if(memcmp(pnat_base+uirep_sites[i].rva,uirep_site_patched[i],5)) return 0;
    return 1;
}
/* Lua found a problem (a refused section, a reference): the next pass stops, on the frame thread. */
static void uirep_async_fail(const char *why) {
    uirep_fail(why);pnat_async_fail(why);
}
/* The module's part of a pass (pnat_pass, every tick, frame thread, before the stock records). */
static int uirep_pass(uint64_t tick) {
    if(uirep_failed) return uirep_fail(uirep_why);
    if(tick>pilot_stop) {
        uirep_stopped=1;
        if(uirep_installed) {uirep_uninstall();if(!uirep_restored) return uirep_fail("ui_restore");uirep_stage_name="stopped";}
        return 1;
    }
    if(!uirep_installed) {
        if(rid_mode!=RID_SET) return uirep_fail("ui_runtime_ids"); /* set right before the load, after the arming */
        if(!uirep_install()) return uirep_fail("ui_install");
        uirep_stage_name="live";return 1;
    }
    if(!uirep_intact()) return uirep_fail("ui_hook_lost");
    return 1;
}
static void uirep_restore(void) {
    if(!uirep_armed) return;
    uirep_stopped=1;
    if(uirep_installed) uirep_uninstall();
    if(uirep_installed) uirep_stage_name="restore_refused";
}
/* Arming (Lua, before the first admitted tick, after gate.pilot_stock and gate.pilot_present). */
static const char *uirep_arm_why="-";
static int uirep_refuse(const char *why) {uirep_arm_why=why;return 0;}
static int uirep_arm_using(unsigned families) {
    if(uirep_armed) return uirep_families==families?1:uirep_refuse("rerun");
    if(!families || (families&~UIREP_FAMILIES)) return uirep_refuse("families");
    if(!pnat_armed || pnat_mode!=PNAT_MODE_STOCK || !pstk_capturing) return uirep_refuse("needs_stock");
    if(pilot_slot_count<1 || pstk_slot_mask!=(1u<<pilot_slot_count)-1u || (pilot_slot_count>=2 && !pslot_armed)) return uirep_refuse("needs_all_stock_slots");
    if(!pres_armed) return uirep_refuse("needs_present");
    uirep_families=families;uirep_failed=0;uirep_stopped=0;uirep_stage_name="armed";uirep_why="-";uirep_restored=1;uirep_installed=0;uirep_arm_why="-";
    memset(uirep_ring,0,sizeof(uirep_ring));uirep_queue_head=uirep_queue_count=0;uirep_refusal_head=uirep_refusal_tail=0;
    uirep_frame_tick=uirep_results_tick=0;
    uirep_armed=1;return 1;
}
static int uirep_arm(unsigned families) {
    if(uirep_armed) return uirep_arm_using(families);
    if(stats.issued) return uirep_refuse("after_first_tick");
    if(GetCurrentThreadId()!=ui_thread) return uirep_refuse("thread");
    if(!uirep_pins()) {static char pin_why[32];snprintf(pin_why,sizeof(pin_why),"pins_ui_replay:%x",pin_failed_rva);return uirep_refuse(pin_why);}
    uirep_use_image();
    return uirep_arm_using(families);
}
static int uirep_refusals_text(char *out,size_t capacity) {
    int n=0;out[0]=0;
    while(uirep_refusal_tail!=uirep_refusal_head) {
        const uirep_refusal *r=&uirep_refusals[uirep_refusal_tail%UIREP_REFUSALS];
        int more=snprintf(out+n,capacity-(size_t)n,"%s%llu:%s:%s",n?",":"",(unsigned long long)r->tick,r->what,r->why);
        if(more<0 || (size_t)more>=capacity-(size_t)n) break;
        n+=more;++uirep_refusal_tail;
    }
    return n;
}
static int uirep_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"ui_replay=%s why=%s families=%x installed=%d restored=%d captured=%lld refused=%lld guard=%lld taken=%lld waits=%lld queued=%u staged=%lld "
        "native=%lld lua=%lld lua_errors=%lld stale=%lld passed=%lld target_passed=%lld scope=%lld/%lld/%lld nested=%lld unavailable=%lld",uirep_stage_name,uirep_why,uirep_families,uirep_installed,uirep_restored,
        (long long)uirep_captured,(long long)uirep_refused,(long long)uirep_guard_refused,(long long)uirep_taken,(long long)uirep_waits,uirep_queue_count,(long long)uirep_staged_count,
        (long long)uirep_applied_native,(long long)uirep_applied_lua,(long long)uirep_lua_errors,(long long)uirep_stale,(long long)uirep_passed,(long long)uirep_target_passed,
        (long long)uirep_scope_start,(long long)uirep_scope_stop,(long long)uirep_scope_refused,(long long)uirep_scope_nested,(long long)uirep_unavailable);
    return n>0 && (size_t)n<capacity?n:0;
}
