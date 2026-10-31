/* Stock piloting of slots 2..N, DEFAULT OFF, stop reason 161 (part of stock
 * piloting, lockstep_pilot_stock.h; native_gate_pilot_stock_slots). Slot 1 flies
 * through the canonical player (the Universe singleton) as before. Every other
 * stock slot k gets its own U::Player P_k and its own PlayerController C_k on its
 * ship, and X4's own player control path runs for it on every client inside a
 * slot scope:
 *
 *   scope k   the image regions of the player block (lockstep_virtual_players.h,
 *             classes SIM, DEVICE and LOCAL) exchanged with slot k's store, the
 *             singleton Universe+0x230 = P_k. Leaving puts every byte back (the
 *             canonical block is hashed before and after) and the singleton is
 *             slot 1's player again. Only the frame thread opens a scope, never
 *             nested, never inside d95180 (reentry flag 0x38f4db4 clear).
 *   seat      from take-1 to the release, for 9fc550/55b150/9f7860 (the player's
 *             controller and ship) and for a camera anchored on P_k: P_k's parent
 *             +0x70 = the cockpit room of slot k's ship (+0x410, class 0x19, as 716670
 *             seats; the ship itself when it has none: the walks only need ship k as
 *             the first class 0x70 ancestor) and its controlled-object vector +0x308 =
 *             {ship k} at the pilot post's index. Upward pointers only: the room's child
 *             list does not hold P_k, so no tree walk reaches it.
 *
 * P_k is built by X4's factory (vps_make, the construct spike) at take-2, never
 * connected to the component tree (no Player::Move, no PlayerCreatedEvent). At take-1,
 * inside scope k, the
 * ship's own take-control (vtable slot 1074, Defensible::vfunc1074 4ed040 ->
 * Controllable::vfunc1074 4500a0) creates C_k exactly as for the local player
 * (the bank reset d84c20 and the environment d8dfe0 then initialise slot k's
 * store). C_k then gets a private copy of the PlayerController vtable:
 *   slots 33/34/35/36  upkeep, environment, owner, environment change: inside
 *                      scope k (the tree phase calls 33 for ship k's controller);
 *   slots 6/7/10       transform, velocity, acceleration (inlined d8dd50 test of
 *                      the global bank): a per-slot route, the original inside
 *                      scope k, else the Agent base (ea5a70/ea6280/ea66c0) or the
 *                      PlayerController branch (a copy of the prologue jumping to
 *                      the flight-control continuation) from slot k's predicate
 *                      taken at its last scope exit; off-thread readers (physics,
 *                      pathing) never see a swapped global;
 *   slots 49/50/14/23  assist, Agent mode, smoothed throttle, no highway: outside
 *                      scope k slot k's answers taken at its last scope exit;
 *   other slots        a counting jump to the stock entry, split into simulating
 *                      (getters_out, with their callers: PILOT_STOCK_CALLERS) and
 *                      rendering (getters_render). Report section 16.9 has the verdict.
 * d8dd50 itself (the authority patch of pnat) answers C_k from its snapshot.
 * Per frame on every client, in fixed slot order:
 *   path_release  after slot 1's replay: scope k, slot k's events of this tick
 *                 (pstk_slot_replay: SPACE through the simulation half of d88710,
 *                 the rest through the stock handlers);
 *   tree phase    C_k's upkeep in scope k (vtable copy, slot 33);
 *   9fe2d7        the call of d95180 is re-pointed (near-page jump, one 16-byte
 *                 CAS): the stock d95180 for the canonical controller, then
 *                 d95180(C_k, game time) in scope k for every seated slot.
 * At stop+1 C_k is released inside scope k (ship vtable slot 1075, 7e1f10) and
 * the call site restored. The local client's own input never enters a scope:
 * slot k's input is client k's recorded dispatch stream (pstk, per slot).
 * Fixed per slot (identical on every client): LOCAL = controls started, no
 * deactivation share, contexts requested; DEVICE starts zeroed; SIM starts as a
 * copy of the canonical block, then the stock bind resets it.
 * Not per slot yet (measured, not hidden): getters other than 6/7/10 called
 * outside the scope, the engine cockpit factor and the camera ship filter
 * (pview: "the player's ship" is slot 1's), weapons and aim (slot 1 only),
 * the MD activities (travel, long-range scan, SETA refused for slots 2..N). */
#define PSLOT_STOP_REASON 161u
#define PSLOT_CALL_SITE 0x9fe2d7u           /* call d95180 in U::UniverseFrameUpdate */
#define PSLOT_UPDATE_RVA 0xd95180u
#define PSLOT_VTABLE 0x2c39488u             /* U::PlayerController */
#define PSLOT_VT_SLOTS 71u
#define PSLOT_TAKE 1074u                    /* ship vtable: take control (Defensible::vfunc1074) */
#define PSLOT_RELEASE 1075u                 /* ship vtable: release control (Ship::vfunc1075) */
#define PSLOT_TAKE_RVA 0x4ed040u
#define PSLOT_RELEASE_RVA 0x7e1f10u
#define PSLOT_IS_CLASS 573u                 /* component vtable +0x11e8 */
#define PSLOT_ROOM 0x410u                   /* the ship's cockpit room (716670) */
#define PSLOT_ROOM_CLASS 0x19
#define PSLOT_CONTROLLER_CLASS 0x2f
#define PSLOT_POST 0x38f4de8u               /* pilot post definition (runtime): its +0x1b0 is the vector index */
#define PSLOT_PILOT_POSITION 0x395cdb0u     /* pilot control position (runtime) */
#define PSLOT_VEC_MAX 64u
#define PSLOT_CLASSES ((1u<<VPL_SIM)|(1u<<VPL_DEVICE)|(1u<<VPL_LOCAL))
#define PSLOT_RETIRED 8u
typedef struct {void *route[3];void *spare;void *col;void *slots[PSLOT_VT_SLOTS];} pslot_vt;
typedef struct {
    unsigned char *player,*ship,*room,*controller;
    uint64_t runtime;                      /* the seated ship's ID (lockstep_pilot_death.h asks the registry about it) */
    void *saved_vector[3],*vector[PSLOT_VEC_MAX];
    unsigned post,length;
    unsigned char store[VPL_BLOCK_MAX];
    unsigned char authority;               /* the global part of d8dd50 at the last scope exit */
    unsigned char assist[2],agent_mode,no_highway; /* slots 49 (argument 0, 1), 50 and 23 at the last scope exit */
    float throttle;                        /* slot 14: the smoothed throttle at the last scope exit */
    int seated,released,roomless;
    /* After a ship's death (lockstep_pilot_death.h): the first seat's store for the next seat,
     * and the controllers that stayed with their dying ships under this vtable copy. */
    unsigned char first[VPL_BLOCK_MAX];
    unsigned first_length;
    void *retired[PSLOT_RETIRED];
    unsigned retired_next;
    pslot_vt vt;
    volatile LONG64 scopes,updates,upkeep,environment,owner,replays,refused;
} pslot_slot;
typedef struct {
    void (*update)(void *,double);          /* d95180 */
    void (*upkeep)(void *,double);          /* vfunc33 d97e80 */
    void (*environment)(void *,void *,void *);        /* vfunc34 d98e50 */
    void (*owner)(void *,void *,void *,void *);       /* vfunc35 d9cdb0 */
    void (*change)(void *,void *,void *,char);        /* vfunc36 d98de0 */
    void (*take)(void *),(*release)(void *);          /* ship vtable 1074/1075: 4ed040, 7e1f10 */
    void *original[3],*agent[3],*player[3]; /* vfunc 6/7/10 */
} pslot_fns;
static pslot_fns pslot_fn;
static pslot_slot pslots[PILOT_SLOTS+1];
static int pslot_armed,pslot_hook_installed,pslot_pins_checked;
static unsigned pslot_mask;                 /* slots 2..N (bit s-1), as pstk_slot_mask */
static const char *pslot_stage="off",*pslot_why="-";
static unsigned char *pslot_page,*pslot_p1;
static unsigned char pslot_site_original[5],pslot_site_patched[5];
static uint64_t pslot_update_tick,pslot_scope_hash;
static unsigned char pslot_g1;              /* the canonical controller's predicate while a scope is open */
static volatile LONG64 pslot_updates,pslot_hook_calls;
__attribute__((used)) static volatile LONG pslot_in_scope;
__attribute__((used)) static volatile LONG64 pslot_route_hits[3],pslot_getter_in[PSLOT_VT_SLOTS],pslot_getter_out[PSLOT_VT_SLOTS],
    pslot_getter_render[PSLOT_VT_SLOTS];
/* Who calls a stock entry outside the scope while simulating: a ring of (return address, slot)
 * written by the thunks, drained into a table every pass (PILOT_STOCK_CALLERS). */
#define PSLOT_CALLER_RING 1024u
#define PSLOT_CALLER_TABLE 256u
__attribute__((used)) static struct {uint64_t address,slot;} pslot_caller_ring[PSLOT_CALLER_RING];
__attribute__((used)) static volatile LONG pslot_caller_head;
static unsigned pslot_caller_tail,pslot_callers;
static struct {unsigned slot,rva;uint64_t count;} pslot_caller_table[PSLOT_CALLER_TABLE];
static uint64_t pslot_caller_dropped;
__attribute__((used)) static void *pslot_getter_orig[PSLOT_VT_SLOTS];
__attribute__((used)) static void *pslot_player_resume[3];
static const unsigned pslot_routed[3]={6,7,10};

/* ---- vtable copy pieces ---- */
/* vfunc 6/7/10: the route of the controller's own copy (vptr-0x28/-0x20/-0x18). */
__attribute__((naked)) static void pslot_route6(void) {
    __asm__ volatile("lock incq pslot_route_hits(%rip)\n\tmovq (%rcx), %rax\n\tjmpq *-0x28(%rax)\n\t");
}
__attribute__((naked)) static void pslot_route7(void) {
    __asm__ volatile("lock incq pslot_route_hits+8(%rip)\n\tmovq (%rcx), %rax\n\tjmpq *-0x20(%rax)\n\t");
}
__attribute__((naked)) static void pslot_route10(void) {
    __asm__ volatile("lock incq pslot_route_hits+16(%rip)\n\tmovq (%rcx), %rax\n\tjmpq *-0x18(%rax)\n\t");
}
/* The PlayerController branch of 6/7/10: the stock prologue without the global
 * test (cmp [0x2f4b7d8],0 and its jne), continuing where flight control 1 goes
 * (d8a6ee / d8b45a / d8b7e4: the +0xd1 test, then the controller's own state). */
static const unsigned char pslot_pro6a[27]={0x48,0x89,0x5c,0x24,0x20,0xf2,0x0f,0x11,0x54,0x24,0x18,0x55,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,
    0x80,0x48,0x81,0xec,0x80,0x01,0x00,0x00};
static const unsigned char pslot_pro6b[23]={0x45,0x0f,0xb6,0xf9,0x44,0x0f,0x29,0xa4,0x24,0x10,0x01,0x00,0x00,0x48,0x8b,0xf2,0x44,0x0f,0x28,
    0xe2,0x48,0x8b,0xd9};
static const unsigned char pslot_pro7a[24]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x20,0xf2,0x0f,0x11,0x54,0x24,0x18,0x57,0x48,0x81,
    0xec,0x80,0x00,0x00,0x00};
static const unsigned char pslot_pro7b[10]={0x41,0x0f,0xb6,0xe9,0x48,0x8b,0xfa,0x48,0x8b,0xd9};
static const unsigned char pslot_pro10a[15]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x30};
static const unsigned char pslot_pro10b[18]={0x41,0x0f,0xb6,0xf1,0x0f,0x29,0x74,0x24,0x20,0x48,0x8b,0xda,0x0f,0x28,0xf2,0x48,0x8b,0xf9};
static const struct {unsigned a,a_len,b,b_len,resume;const unsigned char *pa,*pb;} pslot_branches[3]={
    {0xd8a680u,27,0xd8a6a3u,23,0xd8a6eeu,pslot_pro6a,pslot_pro6b},
    {0xd8b3e0u,24,0xd8b400u,10,0xd8b45au,pslot_pro7a,pslot_pro7b},
    {0xd8b790u,15,0xd8b7a7u,18,0xd8b7e4u,pslot_pro10a,pslot_pro10b}};
__attribute__((naked)) static void pslot_player6(void) {
    __asm__ volatile(".byte 0x48,0x89,0x5c,0x24,0x20,0xf2,0x0f,0x11,0x54,0x24,0x18,0x55,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24\n\t"
        ".byte 0x80,0x48,0x81,0xec,0x80,0x01,0x00,0x00\n\t"
        ".byte 0x45,0x0f,0xb6,0xf9,0x44,0x0f,0x29,0xa4,0x24,0x10,0x01,0x00,0x00,0x48,0x8b,0xf2,0x44,0x0f,0x28\n\t"
        ".byte 0xe2,0x48,0x8b,0xd9\n\tjmpq *pslot_player_resume(%rip)\n\t");
}
__attribute__((naked)) static void pslot_player7(void) {
    __asm__ volatile(".byte 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x20,0xf2,0x0f,0x11,0x54,0x24,0x18,0x57,0x48,0x81\n\t"
        ".byte 0xec,0x80,0x00,0x00,0x00\n\t"
        ".byte 0x41,0x0f,0xb6,0xe9,0x48,0x8b,0xfa,0x48,0x8b,0xd9\n\tjmpq *pslot_player_resume+8(%rip)\n\t");
}
__attribute__((naked)) static void pslot_player10(void) {
    __asm__ volatile(".byte 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x30\n\t"
        ".byte 0x41,0x0f,0xb6,0xf1,0x0f,0x29,0x74,0x24,0x20,0x48,0x8b,0xda,0x0f,0x28,0xf2,0x48,0x8b,0xf9\n\t"
        "jmpq *pslot_player_resume+16(%rip)\n\t");
}
/* Every other slot: count (inside a scope, rendering or simulating; the last with its caller,
 * rax, r10 and r11 are free at a call) and jump to the stock entry. */
#define PSLOT_THUNK(i) __attribute__((naked)) static void pslot_thunk_##i(void) { \
    __asm__ volatile("cmpl $0, pslot_in_scope(%rip)\n\tjne 1f\n\tcmpl $0, render_rng_in_slot(%rip)\n\tjne 2f\n\t" \
        "lock incq pslot_getter_out+8*" #i "(%rip)\n\tmovl $1, %eax\n\tlock xaddl %eax, pslot_caller_head(%rip)\n\t" \
        "andl $1023, %eax\n\tshlq $4, %rax\n\tleaq pslot_caller_ring(%rip), %r11\n\tmovq (%rsp), %r10\n\t" \
        "movq %r10, (%r11,%rax)\n\tmovq $" #i ", 8(%r11,%rax)\n\tjmpq *pslot_getter_orig+8*" #i "(%rip)\n" \
        "1:\n\tlock incq pslot_getter_in+8*" #i "(%rip)\n\tjmpq *pslot_getter_orig+8*" #i "(%rip)\n" \
        "2:\n\tlock incq pslot_getter_render+8*" #i "(%rip)\n\tjmpq *pslot_getter_orig+8*" #i "(%rip)\n\t");}
#define PSLOT_THUNKS(X) X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12) X(13) X(14) X(15) X(16) X(17) \
    X(18) X(19) X(20) X(21) X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31) X(32) X(33) X(34) X(35) X(36) X(37) \
    X(38) X(39) X(40) X(41) X(42) X(43) X(44) X(45) X(46) X(47) X(48) X(49) X(50) X(51) X(52) X(53) X(54) X(55) X(56) X(57) \
    X(58) X(59) X(60) X(61) X(62) X(63) X(64) X(65) X(66) X(67) X(68) X(69) X(70)
PSLOT_THUNKS(PSLOT_THUNK)
#define PSLOT_THUNK_PTR(i) (void*)pslot_thunk_##i,
static void *const pslot_thunks[PSLOT_VT_SLOTS]={PSLOT_THUNKS(PSLOT_THUNK_PTR)};

/* Pins (build 9.00): the call site with its neighbourhood, the routed and wrapped
 * vfuncs, take/release control and their controller factory, the getters the scope
 * satisfies (9fc550 is in pstk_pin_table, d95180 in vpl_pin_table). The call site only,
 * not all of UniverseFrameUpdate: other modules patch their own calls inside it (boot
 * hold 9fe093 at arming, stock-J1; region camera 9fe2e0/9fe337, body order 9fe427,
 * camera anchor 9fe573), each pinning its own neighbourhood. The image test still
 * hashes the whole function. */
static const struct {unsigned rva,size;const char *sha;} pslot_pin_table[]={
    {0x9fe2a9,0x33,"7ce653dcf89011d22010f2ceb315c853d46ef170a4ea6339590ffde18658bd67"},  /* 9fc550 -> rdi, game time -> xmm1, the call */
    {0xd8a680,141,"732be0557cfb5103282ab7743d0949f8f43e334df6c2aaaeba5a4d3a4cbf64e9"},   /* vfunc6 head */
    {0xd8b3e0,135,"7c6fd539b01b174e2a6f5a06dbeebae2b2be8d3cbbdbb808167cdb3551cf9f22"},   /* vfunc7 head */
    {0xd8b790,536,"00d51884aa385b3e085f78e78c754d7de98a664281fd0f7276932cc3481f0792"},   /* vfunc10 */
    {0xd97e80,3926,"2b297f805f6795a221c5c4ab139615e396380a46d000085902f41cca2aa50724"},  /* vfunc33 */
    {0xd98e50,4959,"09d2e0bb638b687d15713ed8042d918622493c75a217c1d753c244ec488fb384"},  /* vfunc34 */
    {0xd9cdb0,202,"4bf36ee839d2c6e13cd05f73a203a795dfd818ffb6c9c541df1d5a7d1219b573"},   /* vfunc35 */
    {0xd98de0,111,"e1e9e6b015b5270829cb2a4f981cbf0d6682d8211d42528c3e928ffb58f9f478"},   /* vfunc36 */
    {0x4ed040,144,"86e4c032c48f7b681dd7c2d189723e6ab6d700591f03293e12b895d831ac65bd"},   /* take control */
    {0x4500a0,730,"9c05b270f67d60de38a00713724c8e6cce9e2934692151be662e878d62b883d9"},   /* Controllable take */
    {0x7e1f10,100,"d82afe88fdea028b34c229f6c44cb3903cdf909d3257bd803318b4b7f9321cb6"},   /* release control */
    {0x450380,1110,"8682c85c8ad6cc9040517c50831418a3ed43fb11b80c5d599fb8169baa5ac60e"},  /* Controllable release */
    {0xd83ae0,761,"8dc02f6d8f93ead6ba5f019efcebfcc668721cfb63bf05de76f6c1b44616cd93"},   /* PlayerController from the Agent's */
    {0xd83550,787,"e97ff648019827beca69d4b62e3411c90d59f846d6eae31a1c4209477c1a70df"},   /* PlayerController, new */
    {0xd84c20,1244,"be54c529ecbce01377450ef1c583fe747c80399bac1b226fd9d58ce41f973a17"},  /* bank reset */
    {0xd8dfe0,1022,"c860534e68204c9e8309d38549afbd3a00c6372f8c564d96ef88893497f57e9c"},  /* environment */
    {0xd9bf70,163,"b8c0b8b83decfbed296f7de7dc147d4d8e81e16a9ee811f4be000a88cd36b4c8"},   /* vfunc49 assist */
    {0x55b150,273,"dd70be4494461ad9d35c55a1a413f1cdfc5c7bdd348974df2928f4853a1d2522"},   /* the player's occupied ship */
    {0x9f7860,233,"2a2c6ea4ca48d79475c17527433f4941bfb567f34bd091958eee92f1832a573b"},   /* the player's ship (class 0x49) */
    {0x70e580,28,"be9be51346175f6fa098fc794543d8e021870885cebf0f76e3c1b518ff8a1dc1"},    /* Player::vfunc906 */
    {0xea5a70,25,"11a6d2c6519e9c4b3763d2e36fd123b4d831564236675cf5165a1b7acf159c08"}};   /* Agent transform */
#define PSLOT_PIN_COUNT (sizeof(pslot_pin_table)/sizeof(*pslot_pin_table))
static const unsigned char pslot_call_bytes[5]={0xe8,0xa4,0x6e,0x39,0x00};  /* call d95180 from 9fe2dc */
static int pslot_pins(void) {
    if(!pslot_pins_checked) {
        int ok=!memcmp(image+PSLOT_CALL_SITE,pslot_call_bytes,5);
        if(!ok) pin_failed_rva=PSLOT_CALL_SITE;
        for(unsigned i=0;ok && i<PSLOT_PIN_COUNT;i++)
            if(!(ok=hash_ok(image+pslot_pin_table[i].rva,pslot_pin_table[i].size,pslot_pin_table[i].sha))) pin_failed_rva=pslot_pin_table[i].rva;
        static void (*const branches[3])(void)={pslot_player6,pslot_player7,pslot_player10};
        for(unsigned i=0;ok && i<3;i++) {
            const unsigned char *t=(const unsigned char*)branches[i];
            ok=!memcmp(image+pslot_branches[i].a,pslot_branches[i].pa,pslot_branches[i].a_len) &&
               !memcmp(image+pslot_branches[i].b,pslot_branches[i].pb,pslot_branches[i].b_len) &&
               !memcmp(t,pslot_branches[i].pa,pslot_branches[i].a_len) && !memcmp(t+pslot_branches[i].a_len,pslot_branches[i].pb,pslot_branches[i].b_len);
            if(!ok) pin_failed_rva=pslot_branches[i].a;
        }
        for(unsigned i=0;ok && i<PSLOT_VT_SLOTS;i++) if(!(ok=((void**)(image+PSLOT_VTABLE))[i]!=NULL)) pin_failed_rva=PSLOT_VTABLE+8*i;
        pslot_pins_checked=ok?1:-1;
    }
    return pslot_pins_checked==1;
}
static void pslot_use_image(void) {
    pslot_fn.update=(void(*)(void*,double))(image+PSLOT_UPDATE_RVA);pslot_fn.upkeep=(void(*)(void*,double))(image+0xd97e80);
    pslot_fn.environment=(void(*)(void*,void*,void*))(image+0xd98e50);pslot_fn.owner=(void(*)(void*,void*,void*,void*))(image+0xd9cdb0);
    pslot_fn.change=(void(*)(void*,void*,void*,char))(image+0xd98de0);
    pslot_fn.take=(void(*)(void*))(image+PSLOT_TAKE_RVA);pslot_fn.release=(void(*)(void*))(image+PSLOT_RELEASE_RVA);
    static void (*const branches[3])(void)={pslot_player6,pslot_player7,pslot_player10};
    for(unsigned i=0;i<3;i++) {
        pslot_fn.original[i]=image+pnat_vf_originals[i];pslot_fn.agent[i]=image+pnat_vf_bases[i];pslot_fn.player[i]=(void*)branches[i];
        pslot_player_resume[i]=image+pslot_branches[i].resume;
    }
    vps_create=(vps_create_fn)(image+VPS_FACTORY);vps_rng_word=vps_tls_rng;
}

/* ---- the slot scope ---- */
static void pslot_async(const char *why) {
    if(!strcmp(pslot_why,"-")) pslot_why=why;
    pslot_failed=1;pnat_async_fail(why);
}
static int pslot_fail(const char *why) {pslot_stage="failed";if(!strcmp(pslot_why,"-")) pslot_why=why;pslot_failed=1;return 0;}
static int pslot_frame_thread(void) {return GetCurrentThreadId()==ui_thread;}
static uintptr_t pslot_canonical_vtable(uintptr_t vtable) {
    for(unsigned k=2;k<=PILOT_SLOTS;k++) if(vtable==(uintptr_t)&pslots[k].vt.slots[0]) return (uintptr_t)(pnat_base?pnat_base:image)+PSLOT_VTABLE;
    return vtable;
}
static int pslot_vtable_is_copy(uintptr_t vtable) {return pslot_canonical_vtable(vtable)!=vtable;}
static unsigned pslot_of(const void *controller) {
    for(unsigned k=2;controller && k<=PILOT_SLOTS;k++) if(pslots[k].controller==controller) return k;
    return 0;
}
/* A controller left with its dying ship (pslot_retire): not a slot's any more, its scoped entries do nothing. */
static int pslot_is_retired(const void *controller) {
    for(unsigned k=2;controller && k<=PILOT_SLOTS;k++) for(unsigned i=0;i<PSLOT_RETIRED;i++) if(pslots[k].retired[i]==controller) return 1;
    return 0;
}
/* Live <-> store over the image regions of the scope classes (table order). */
static int pslot_exchange(unsigned char *store,unsigned length) {
    unsigned n=0;
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) {
        const vpl_region *r=&vpl_regions[i];
        if(r->base!=VPL_BASE_IMAGE || !(PSLOT_CLASSES>>r->cls&1u)) continue;
        if(n+r->size>length) return -1;
        volatile unsigned char *at=pnat_base+r->offset;
        for(unsigned j=0;j<r->size;j++) {unsigned char t=at[j];at[j]=store[n+j];store[n+j]=t;}
        n+=r->size;
    }
    return n==length?(int)n:-1;
}
static uint64_t pslot_hash(void) {
    uint64_t h=0xcbf29ce484222325ULL;
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) {
        const vpl_region *r=&vpl_regions[i];
        if(r->base==VPL_BASE_IMAGE && (PSLOT_CLASSES>>r->cls&1u)) h=vpl_fnv((const unsigned char*)(pnat_base+r->offset),r->size,h);
    }
    return h;
}
/* Slot k's first block: SIM from the canonical block (identical on every client),
 * DEVICE zero, LOCAL fixed (controls started 0x2f4b7e0 = 1, no deactivation share,
 * PlayerController contexts requested 0x2f4b829 = 1). */
static unsigned pslot_store_init(unsigned char *store) {
    unsigned n=0;
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) {
        const vpl_region *r=&vpl_regions[i];
        if(r->base!=VPL_BASE_IMAGE || !(PSLOT_CLASSES>>r->cls&1u)) continue;
        if(n+r->size>VPL_BLOCK_MAX) return 0;
        if(r->cls==VPL_SIM) memcpy(store+n,(const void*)(pnat_base+r->offset),r->size);
        else memset(store+n,0,r->size);
        if(r->offset==0x2f4b7e0u || r->offset==0x2f4b829u) store[n]=1;
        n+=r->size;
    }
    return n;
}
static unsigned char pslot_assist(void *controller) {
    void **vtable=*(void***)controller;return ((unsigned char(*)(void*,unsigned char))vtable[49])(controller,0);
}
/* The global part of d8dd50 (the +0xd1 test stays live): autopilot, or no flight
 * control with assist outside a highway. Read in the scope that owns the globals. */
static unsigned char pslot_global_part(void *controller) {
    if(*(volatile uint64_t*)(pnat_base+PNAT_AUTOPILOT)) return 1;
    return !*PNAT_U8(PNAT_FLIGHT) && pslot_assist(controller) && !*PNAT_U8(PNAT_HIGHWAY);
}
/* Slot k's answers for readers outside its scope: the stock getters run in the scope that
 * owns the state, at its exit; the state changes only inside it. */
static void pslot_take_answers(pslot_slot *s) {
    void **vtable=*(void***)s->controller;
    s->assist[0]=((unsigned char(*)(void*,unsigned char))vtable[49])(s->controller,0);
    s->assist[1]=((unsigned char(*)(void*,unsigned char))vtable[49])(s->controller,1);
    s->agent_mode=((unsigned char(*)(void*))vtable[50])(s->controller);
    s->no_highway=!*PNAT_U8(PNAT_HIGHWAY);
    memcpy(&s->throttle,(const void*)(pnat_base+0x2f4b764u),4);
}
static void pslot_set_routes(pslot_slot *s,int live) {
    for(unsigned i=0;i<3;i++)
        InterlockedExchangePointer(&s->vt.route[i],live?pslot_fn.original[i]:s->authority?pslot_fn.agent[i]:pslot_fn.player[i]);
}
static int pslot_enter(unsigned k) {
    pslot_slot *s=k>=2 && k<=PILOT_SLOTS?&pslots[k]:NULL;unsigned char *universe=NULL,*now=NULL;
    if(!s || !s->player || !s->length || pstk_scope_slot || pres_open || !pslot_frame_thread() || !vpl_reentry_clear(pnat_base) ||
       !read_local(pnat_base+VPL_UNIVERSE,&universe,8) || !universe || !read_local(universe+VPS_SINGLETON,&now,8) || !now || now!=pslot_p1) {
        if(s) InterlockedIncrement64(&s->refused);
        return 0;
    }
    /* Slot 1's controller goes with its ship before the pass can see it (the eject, lockstep_pilot_death.h). */
    {   unsigned char *canonical=pnat_controller;
        if(pdeath_armed && canonical && (unsigned char*)pnat_fn.controller(pnat_base+PNAT_CONTROLLER_KEY)!=canonical) canonical=NULL;
        pslot_g1=canonical?pslot_global_part(canonical):0;}
    pslot_scope_hash=pslot_hash();
    if(pslot_exchange(s->store,s->length)<0) {InterlockedIncrement64(&s->refused);return 0;}
    *(unsigned char *volatile*)(universe+VPS_SINGLETON)=s->player;
    pstk_scope_slot=k;InterlockedExchange(&pslot_in_scope,1);
    if(s->controller) pslot_set_routes(s,1);
    InterlockedIncrement64(&s->scopes);
    return 1;
}
static int pslot_leave(unsigned k) {
    pslot_slot *s=&pslots[k];unsigned char *universe=NULL,*now=NULL,*parent=NULL;void *vector[3];int ok=1;
    if(k<2 || k>PILOT_SLOTS || pstk_scope_slot!=k || !pslot_frame_thread()) return 0;
    if(!read_local(pnat_base+VPL_UNIVERSE,&universe,8) || !universe || !read_local(universe+VPS_SINGLETON,&now,8) || now!=s->player) ok=0;
    memcpy(&parent,s->player+0x70,8);memcpy(vector,s->player+0x308,sizeof(vector));
    if(parent!=s->room || vector[0]!=(void*)&s->vector[0] || vector[1]!=(void*)&s->vector[s->post+1] || !vpl_reentry_clear(pnat_base)) ok=0;
    if(s->controller) {s->authority=pslot_global_part(s->controller);pslot_take_answers(s);pslot_set_routes(s,0);}
    if(universe) *(unsigned char *volatile*)(universe+VPS_SINGLETON)=pslot_p1;
    if(pslot_exchange(s->store,s->length)<0) ok=0;
    pstk_scope_slot=0;InterlockedExchange(&pslot_in_scope,0);
    if(pslot_hash()!=pslot_scope_hash) ok=0; /* the canonical bytes came back unchanged */
    return ok;
}

/* ---- the wrapped vfuncs of C_k (vtable copy) ---- */
static int pslot_scope_begin(unsigned k,pslot_slot *s,const char *why) {
    if(pstk_scope_slot || !pslot_frame_thread()) {InterlockedIncrement64(&s->refused);pslot_async(why);return 0;}
    if(!pslot_enter(k)) {pslot_async(why);return 0;}
    return 1;
}
/* Slots 49, 50, 14, 23: pure reads of slot state (the player's assist fields, flight control,
 * autopilot, highway, the smoothed throttle). Scripts reach 49 (EvaluateComponentProperty ->
 * 6d9b60), the Agent parameters too (ea1e80). Outside the scope they answer slot k's last values. */
static void pslot_note_caller(unsigned slot,void *address) {
    if(pstk_scope_slot) {InterlockedIncrement64(&pslot_getter_in[slot]);return;}
    if(render_rng_in_slot) {InterlockedIncrement64(&pslot_getter_render[slot]);return;}
    InterlockedIncrement64(&pslot_getter_out[slot]);
    unsigned at=(unsigned)InterlockedIncrement(&pslot_caller_head)-1u;
    at&=PSLOT_CALLER_RING-1u;pslot_caller_ring[at].address=(uint64_t)(uintptr_t)address;pslot_caller_ring[at].slot=slot;
}
static int pslot_in_own_scope(unsigned k) {return pstk_scope_slot==k && pslot_frame_thread();}
static unsigned char pslot_vf49(void *controller,unsigned char argument) {
    unsigned k=pslot_of(controller);
    pslot_note_caller(49,__builtin_return_address(0));
    if(!k || pslot_in_own_scope(k)) return ((unsigned char(*)(void*,unsigned char))pslot_getter_orig[49])(controller,argument);
    return pslots[k].assist[argument!=0];
}
static unsigned char pslot_vf50(void *controller) {
    unsigned k=pslot_of(controller);
    pslot_note_caller(50,__builtin_return_address(0));
    if(!k || pslot_in_own_scope(k)) return ((unsigned char(*)(void*))pslot_getter_orig[50])(controller);
    return pslots[k].agent_mode;
}
static float pslot_vf14(void *controller) {
    unsigned k=pslot_of(controller);
    pslot_note_caller(14,__builtin_return_address(0));
    if(!k || pslot_in_own_scope(k)) return ((float(*)(void*))pslot_getter_orig[14])(controller);
    return pslots[k].throttle;
}
static unsigned char pslot_vf23(void *controller) {
    unsigned k=pslot_of(controller);
    pslot_note_caller(23,__builtin_return_address(0));
    if(!k || pslot_in_own_scope(k)) return ((unsigned char(*)(void*))pslot_getter_orig[23])(controller);
    return pslots[k].no_highway;
}
static void pslot_vf33(void *controller,double now) {
    unsigned k=pslot_of(controller);
    if(!k && pslot_is_retired(controller)) return;
    if(!k) {pslot_async("upkeep_unknown");return;}
    pslot_slot *s=&pslots[k];InterlockedIncrement64(&s->upkeep);
    if(pstk_scope_slot==k && pslot_frame_thread()) {pslot_fn.upkeep(controller,now);return;}
    if(!pslot_scope_begin(k,s,"upkeep_scope")) return;
    __try {pslot_fn.upkeep(controller,now);} __finally {if(!pslot_leave(k)) pslot_async("upkeep_leave");}
}
static void pslot_vf34(void *controller,void *a,void *b) {
    unsigned k=pslot_of(controller);
    if(!k && pslot_is_retired(controller)) return;
    if(!k) {pslot_async("environment_unknown");return;}
    pslot_slot *s=&pslots[k];InterlockedIncrement64(&s->environment);
    if(pstk_scope_slot==k && pslot_frame_thread()) {pslot_fn.environment(controller,a,b);return;}
    if(!pslot_scope_begin(k,s,"environment_scope")) return;
    __try {pslot_fn.environment(controller,a,b);} __finally {if(!pslot_leave(k)) pslot_async("environment_leave");}
}
static void pslot_vf35(void *controller,void *a,void *b,void *c) {
    unsigned k=pslot_of(controller);
    if(!k && pslot_is_retired(controller)) return;
    if(!k) {pslot_async("owner_unknown");return;}
    pslot_slot *s=&pslots[k];InterlockedIncrement64(&s->owner);
    if(pstk_scope_slot==k && pslot_frame_thread()) {pslot_fn.owner(controller,a,b,c);return;}
    if(!pslot_scope_begin(k,s,"owner_scope")) return;
    __try {pslot_fn.owner(controller,a,b,c);} __finally {if(!pslot_leave(k)) pslot_async("owner_leave");}
}
static void pslot_vf36(void *controller,void *a,void *b,char c) {
    unsigned k=pslot_of(controller);
    if(!k && pslot_is_retired(controller)) return;
    if(!k) {pslot_async("change_unknown");return;}
    pslot_slot *s=&pslots[k];InterlockedIncrement64(&s->environment);
    if(pstk_scope_slot==k && pslot_frame_thread()) {pslot_fn.change(controller,a,b,c);return;}
    if(!pslot_scope_begin(k,s,"change_scope")) return;
    __try {pslot_fn.change(controller,a,b,c);} __finally {if(!pslot_leave(k)) pslot_async("change_leave");}
}
/* d8dd50 (pnat's authority patch): C_k outside its own scope answers from its
 * snapshot; the canonical controller from its own while a slot scope is open. */
static int pslot_authority(void *controller,unsigned char *answer) {
    unsigned k=pslot_of(controller);unsigned char *ship=NULL,flag=0,global;
    if(!pslot_armed || !controller) return 0;
    if(!k && pslot_is_retired(controller)) {*answer=1;return 1;}
    if(k) {if(pstk_scope_slot==k && pslot_frame_thread()) return 0;global=pslots[k].authority;}
    else if(controller==pnat_controller && pstk_scope_slot) global=pslot_g1;
    else return 0;
    if(global) {*answer=1;return 1;}
    *answer=!read_local((unsigned char*)controller+0x28,&ship,8) || !ship || !read_local(ship+0xd1,&flag,1) || !flag;
    return 1;
}
static void pslot_update_slots(double now) {
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        pslot_slot *s=&pslots[k];
        if(!s->seated) continue;
        /* Its ship died earlier in this frame: simulation state, the same on every client; the next pass ends the seat. */
        if(pdeath_armed) {
            double hull=1.0;
            if(!pdeath_find || pdeath_find(pdeath_map,s->runtime,4)!=s->ship ||
               (read_local(s->ship+PDEATH_HULL,&hull,8) && fabs(hull)<PDEATH_HULL_EPSILON)) continue;
        }
        if(!pslot_enter(k)) {pslot_async("update_scope");return;}
        InterlockedIncrement64(&s->updates);
        int left=0;
        __try {pslot_fn.update(s->controller,now);} __finally {left=pslot_leave(k);}
        if(!left) {pslot_async("update_leave");return;}
    }
}
/* The call at 9fe2d7: the canonical update, then every seated slot in its scope. */
static void pslot_update(void *controller,double now) {
    pslot_fn.update(controller,now);
    InterlockedIncrement64(&pslot_hook_calls);
    if(!pslot_frame_thread() || pstk_scope_slot) {pslot_async("update_outside_frame");return;}
    pslot_update_tick=stats.issued;InterlockedIncrement64(&pslot_updates);
    pslot_update_slots(now);
}

/* ---- the schedule (frame thread, path_release, inside pnat_pass) ---- */
static int pslot_controller_has(unsigned char *ship,int cls,const void *which) {
    void *c=NULL;
    if(!read_local(ship+0x200,&c,8)) return -1;
    for(unsigned steps=0;c && steps<16;steps++) {
        void **vtable=*(void***)c;
        if(which?c==which:((unsigned char(*)(void*,int))vtable[3])(c,cls)!=0) return 1;
        c=((void*(*)(void*))vtable[20])(c);
    }
    return 0;
}
static int pslot_is(void *component,int cls) {
    void **vtable=*(void***)component;return ((unsigned char(*)(void*,int))vtable[PSLOT_IS_CLASS])(component,cls)!=0;
}
static int pslot_construct_all(void) {
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        if(!(pslot_mask>>(k-1)&1u)) continue;
        unsigned char *p1=NULL,*made=NULL;
        if(!vps_make(pnat_base,&p1,&made)) return pslot_fail(vps_why);
        if(pslot_p1 && p1!=pslot_p1) return pslot_fail("p1_changed");
        pslot_p1=p1;pslots[k].player=made;
    }
    pslot_stage="constructed";return 1;
}
/* P_k's seat: its parent and controlled-object vector while it holds the ship. */
static void pslot_seat_player(pslot_slot *s) {
    memcpy(s->saved_vector,s->player+0x308,sizeof(s->saved_vector));
    void *vector[3]={&s->vector[0],&s->vector[s->post+1],&s->vector[s->post+1]};
    memcpy(s->player+0x308,vector,sizeof(vector));
    *(unsigned char *volatile*)(s->player+0x70)=s->room;
}
static void pslot_unseat_player(pslot_slot *s) {
    if(pview_slot_player==s->player) pview_slot_player=NULL;
    *(unsigned char *volatile*)(s->player+0x70)=NULL;
    memcpy(s->player+0x308,s->saved_vector,sizeof(s->saved_vector));
}
static int pslot_seat_ship(unsigned k,unsigned char *ship) {
    pslot_slot *s=&pslots[k];
    unsigned char *room=NULL,*parent=(unsigned char*)1,*post=NULL;void **vtable=NULL,*take=NULL,*release=NULL;
    int64_t index=0;int position=0;
    if(!read_local(ship,&vtable,8) || !vtable || !read_local(vtable+PSLOT_TAKE,&take,8) || take!=(void*)pslot_fn.take ||
       !read_local(vtable+PSLOT_RELEASE,&release,8) || release!=(void*)pslot_fn.release) return pslot_fail("ship_vtable");
    if(pslot_controller_has(ship,PSLOT_CONTROLLER_CLASS,NULL)!=0) return pslot_fail("ship_controlled");
    s->roomless=!read_local(ship+PSLOT_ROOM,&room,8) || !room || !pslot_is(room,PSLOT_ROOM_CLASS);
    if(s->roomless) room=ship;
    if(!read_local(s->player+0x70,&parent,8) || parent) return pslot_fail("player_parented");
    if(!read_local(pnat_base+PSLOT_POST,&post,8)) return pslot_fail("post");
    if(post && (!read_local(post+0x1b0,&index,8) || index<0 || index>=(int64_t)PSLOT_VEC_MAX-1)) return pslot_fail("post_index");
    if(!read_local(pnat_base+PSLOT_PILOT_POSITION,&position,4) || !position) return pslot_fail("pilot_position");
    /* The player side of the seat (716670 for the pilot position, without the
     * connection, Gravidar, HUD and menus): control position, assist from slot 1's
     * player (identical on every client), no activity, no soft target. */
    memcpy(s->player+0x6ab8,&position,4);memcpy(s->player+0x6aac,pslot_p1+0x6aac,4);
    memset(s->player+0x6ab4,0,4);memset(s->player+0x6dd0,0,8);s->player[0x6e20]=0;
    memset(s->vector,0,sizeof(s->vector));s->post=(unsigned)index;s->vector[s->post]=ship;
    s->ship=ship;s->room=room;s->controller=NULL;s->runtime=0;read_local(ship+8,&s->runtime,8);
    /* The first seat's store: canonical SIM at the start. A later one (after a death) starts from the same. */
    if(s->first_length) {memcpy(s->store,s->first,s->first_length);s->length=s->first_length;}
    else {
        if(!(s->length=pslot_store_init(s->store))) return pslot_fail("store");
        memcpy(s->first,s->store,s->length);s->first_length=s->length;
    }
    pslot_seat_player(s);
    if(!pslot_enter(k)) {pslot_unseat_player(s);return pslot_fail("seat_scope");}
    void *controller=NULL;int fault=0;
    __try {((void(*)(void*))take)(ship);controller=pnat_fn.controller(pnat_base+PNAT_CONTROLLER_KEY);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    void *back=NULL,*cvt=NULL;
    int ok=!fault && controller && read_local(controller,&cvt,8) && cvt==(void*)(pnat_base+PSLOT_VTABLE) &&
        read_local((unsigned char*)controller+0x28,&back,8) && back==ship && pslot_controller_has(ship,0,controller)==1;
    if(ok) s->controller=controller;
    if(ok && k==stats.slot) pview_slot_player=s->player;
    int left=pslot_leave(k);
    if(!ok) pslot_unseat_player(s);
    if(!left) return pslot_fail("seat_leave");
    if(fault) return pslot_fail("take_fault");
    if(!ok) return pslot_fail("take_controller");
    /* C_k's own vtable: counting thunks, the routes, the scoped entries. */
    void **stock=(void**)(pnat_base+PSLOT_VTABLE);
    s->vt.col=stock[-1];s->vt.spare=NULL;
    for(unsigned i=0;i<PSLOT_VT_SLOTS;i++) {
        if(!pslot_getter_orig[i]) pslot_getter_orig[i]=stock[i];
        s->vt.slots[i]=pslot_thunks[i];
    }
    s->vt.slots[6]=(void*)pslot_route6;s->vt.slots[7]=(void*)pslot_route7;s->vt.slots[10]=(void*)pslot_route10;
    s->vt.slots[33]=(void*)pslot_vf33;s->vt.slots[34]=(void*)pslot_vf34;s->vt.slots[35]=(void*)pslot_vf35;s->vt.slots[36]=(void*)pslot_vf36;
    s->vt.slots[49]=(void*)pslot_vf49;s->vt.slots[50]=(void*)pslot_vf50;s->vt.slots[14]=(void*)pslot_vf14;s->vt.slots[23]=(void*)pslot_vf23;
    pslot_set_routes(s,0);
    if(InterlockedCompareExchangePointer((void*volatile*)controller,&s->vt.slots[0],pnat_base+PSLOT_VTABLE)!=(void*)(pnat_base+PSLOT_VTABLE))
        return pslot_fail("controller_vtable");
    s->seated=1;s->released=0;
    return 1;
}
static int pslot_seat(unsigned k) {
    pslot_slot *s=&pslots[k];pilot_slot *ps=&pilot_slots[k-1];
    if(!s->player) return pslot_fail("no_player");
    if(!ps->resolved || !ps->object || ps->player) return pslot_fail("ship_unresolved");
    return pslot_seat_ship(k,ps->object);
}
static unsigned char *pslot_alloc_page(void) {
    uintptr_t at=(uintptr_t)(pnat_base+PSLOT_CALL_SITE)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            unsigned char *p=VirtualAlloc((void*)(sign<0?at-distance:at+distance),PVIEW_PAGE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            intptr_t d=(intptr_t)p-(intptr_t)(pnat_base+PSLOT_CALL_SITE+5);
            if(d>INT32_MIN+0x10000 && d<INT32_MAX-0x10000) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int pslot_hook_install(void) {
    if(pslot_hook_installed) return 1;
    int32_t rel=(int32_t)((intptr_t)(pnat_base+PSLOT_UPDATE_RVA)-(intptr_t)(pnat_base+PSLOT_CALL_SITE+5));
    pslot_site_original[0]=0xe8;memcpy(pslot_site_original+1,&rel,4);
    if(memcmp(pnat_base+PSLOT_CALL_SITE,pslot_site_original,5)) return 0;
    if(!pslot_page && !(pslot_page=pslot_alloc_page())) return 0;
    DWORD old,ignored;
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_READWRITE,&old)) return 0;
    memset(pslot_page,0xcc,64);pview_abs_jump(pslot_page,(void*)pslot_update);
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) return 0;
    FlushInstructionCache(GetCurrentProcess(),pslot_page,PVIEW_PAGE);
    rel=(int32_t)((intptr_t)pslot_page-(intptr_t)(pnat_base+PSLOT_CALL_SITE+5));
    pslot_site_patched[0]=0xe8;memcpy(pslot_site_patched+1,&rel,4);
    if(!pview_cas(PSLOT_CALL_SITE,pslot_site_original,pslot_site_patched,5)) return 0;
    pslot_hook_installed=1;return 1;
}
static int pslot_hook_uninstall(void) {
    if(!pslot_hook_installed) return 1;
    if(!pview_cas(PSLOT_CALL_SITE,pslot_site_patched,pslot_site_original,5)) return 0;
    pslot_hook_installed=0;return 1;
}
static int pslot_hook_intact(void) {
    unsigned char jump[14];
    if(!pslot_hook_installed || !pslot_page || memcmp(pnat_base+PSLOT_CALL_SITE,pslot_site_patched,5)) return 0;
    pview_abs_jump(jump,(void*)pslot_update);return !memcmp(pslot_page,jump,14);
}
static int pslot_seat_all(void) {
    for(unsigned k=2;k<=PILOT_SLOTS;k++) if((pslot_mask>>(k-1)&1u) && !pslot_seat(k)) return 0;
    if(!pslot_hook_install()) return pslot_fail("hook_install");
    pslot_stage="seated";return 1;
}
static int pslot_release(unsigned k) {
    pslot_slot *s=&pslots[k];
    if(!s->seated) return 1;
    void **vtable=NULL,*release=NULL;int fault=0;
    if(!read_local(s->ship,&vtable,8) || !vtable || !read_local(vtable+PSLOT_RELEASE,&release,8) || release!=(void*)pslot_fn.release)
        return pslot_fail("release_vtable");
    if(!pslot_enter(k)) return pslot_fail("release_scope");
    int restored=InterlockedCompareExchangePointer((void*volatile*)s->controller,pnat_base+PSLOT_VTABLE,&s->vt.slots[0])==(void*)&s->vt.slots[0];
    if(restored) {
        __try {((void(*)(void*))release)(s->ship);}
        __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
        s->controller=NULL;
    }
    int left=pslot_leave(k);
    if(!restored) return pslot_fail("release_vtable_lost");
    pslot_unseat_player(s);
    s->seated=0;s->released=1;
    if(fault) return pslot_fail("release_fault");
    return left?1:pslot_fail("release_leave");
}
/* The ship is already being killed: the controller stays with it, under this vtable copy, until X4
 * deletes it. Readers get the last answers; what would run in a scope does nothing. */
static void pslot_retire(unsigned k) {
    pslot_slot *s=&pslots[k];
    s->retired[s->retired_next++%PSLOT_RETIRED]=s->controller;
    s->controller=NULL;s->seated=0;s->released=1;
    pslot_unseat_player(s);
    s->room=NULL;
}
static int pslot_release_all(void) {
    for(unsigned k=2;k<=PILOT_SLOTS;k++) if(!pslot_release(k)) return 0;
    if(!pslot_hook_uninstall()) return pslot_fail("hook_restore");
    pslot_stage="released";return 1;
}
/* The caller ring into the table (frame thread, every pass); out of order is fine for counts. */
static void pslot_callers_drain(void) {
    unsigned head=(unsigned)InterlockedCompareExchange(&pslot_caller_head,0,0);
    if(head-pslot_caller_tail>PSLOT_CALLER_RING) {pslot_caller_dropped+=head-pslot_caller_tail-PSLOT_CALLER_RING;pslot_caller_tail=head-PSLOT_CALLER_RING;}
    for(;pslot_caller_tail!=head;pslot_caller_tail++) {
        unsigned at=pslot_caller_tail&(PSLOT_CALLER_RING-1u),slot=(unsigned)pslot_caller_ring[at].slot;
        uintptr_t address=(uintptr_t)pslot_caller_ring[at].address,base=(uintptr_t)pnat_base;
        unsigned rva=address>=base && address-base<VPL_IMAGE_SPAN?(unsigned)(address-base):0xffffffffu,i=0;
        for(;i<pslot_callers;i++) if(pslot_caller_table[i].slot==slot && pslot_caller_table[i].rva==rva) break;
        if(i==pslot_callers) {
            if(pslot_callers==PSLOT_CALLER_TABLE) {++pslot_caller_dropped;continue;}
            pslot_caller_table[i].slot=slot;pslot_caller_table[i].rva=rva;pslot_caller_table[i].count=0;++pslot_callers;
        }
        ++pslot_caller_table[i].count;
    }
}
/* slot:caller rva:count of the stock entries called outside the scope while simulating. */
static int pslot_callers_text(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"callers=%u dropped=%llu",pslot_callers,(unsigned long long)pslot_caller_dropped);
    for(unsigned s=0;n>0 && (size_t)n<capacity && s<PSLOT_VT_SLOTS;s++)
        for(unsigned i=0;n>0 && (size_t)n<capacity && i<pslot_callers;i++) if(pslot_caller_table[i].slot==s) {
            int more=snprintf(out+n,capacity-(size_t)n," %u:%x:%llu",s,pslot_caller_table[i].rva,(unsigned long long)pslot_caller_table[i].count);
            if(more<0) return 0;
            n+=more;
        }
    return n>0 && (size_t)n<capacity?n:0;
}
/* Every live pass: the call site and every seat as installed, last frame's updates done. */
static int pslot_check(uint64_t tick) {
    unsigned char *universe=NULL,*now=NULL;
    if(!pslot_hook_intact()) return pslot_fail("hook_lost");
    if(pslot_update_tick+1!=tick) {
        /* X4 skips the call when its player has no PlayerController (9fe2b4): a dead slot 1 can be there, on
         * every client, as it is simulation state. Slots 2..N then update here, the frame after. */
        double now=0;
        if(!pilot_slots[0].dead) return pslot_fail("update_missing");
        if(!pnat_fn.time(&now)) return pslot_fail("update_time");
        pslot_update_slots(now);
    }
    if(!read_local(pnat_base+VPL_UNIVERSE,&universe,8) || !universe || !read_local(universe+VPS_SINGLETON,&now,8) || now!=pslot_p1)
        return pslot_fail("singleton_changed");
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        pslot_slot *s=&pslots[k];void *vtable=NULL;int refs=0;
        if(!s->seated) continue;
        /* Seated while dead: in its suit (lockstep_pilot_death.h), which is not the slot's ship. */
        if(!pilot_slots[k-1].dead && (!pilot_slots[k-1].resolved || pilot_slots[k-1].object!=s->ship)) return pslot_fail("ship_lost");
        if(pslot_controller_has(s->ship,0,s->controller)!=1 || !read_local(s->controller,&vtable,8) || vtable!=(void*)&s->vt.slots[0])
            return pslot_fail("controller_lost");
        if(!read_local(s->player+0x40,&refs,4) || refs<1) return pslot_fail("player_lost");
    }
    return 1;
}
/* The slots' part of a pass (pnat_pass, every tick): 0 stops with 161. */
static int pslot_pass(uint64_t tick) {
    if(!pslot_armed) return 1;
    uint64_t take=pilot_start-PILOT_SUPPRESS_LEAD;
    if(pslot_failed) return pslot_fail(pslot_why);
    if(tick+2==take) return pslot_construct_all();
    if(tick+1==take) return pslot_seat_all();
    if(tick>=take && tick<=pilot_stop) {pslot_callers_drain();if(!pslot_check(tick)) return 0;pslot_stage="live";return 1;}
    if(tick==pilot_stop+1) return pslot_release_all();
    return 1;
}
/* After slot 1's replay (pstk_frame), every seated slot's events of this tick. */
static int pslot_replay(uint64_t tick) {
    double now=0;
    if(!pslot_armed) return 1;
    if(!pnat_fn.time(&now)) return pslot_fail("replay_time");
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        pslot_slot *s=&pslots[k];
        if(!s->seated || pilot_slots[k-1].dead) continue;   /* a dead slot's records are consumed and ignored, in a suit too */
        if(pstk_slot_frames[k-1].tick!=tick) return pslot_fail("replay_missing");
        if(!pslot_enter(k)) return pslot_fail("replay_scope");
        int replayed=0,left=0;
        __try {replayed=pstk_slot_replay(k,now);} __finally {left=pslot_leave(k);}
        if(!left) return pslot_fail("replay_leave");
        if(!replayed) return pslot_fail("replay_refused");
        InterlockedIncrement64(&s->replays);
    }
    return 1;
}
#define PSLOT_STOP_WAIT 120u
static unsigned pslot_stop_waits;
/* Arming (Lua, before the first admitted tick; pins checked by the caller). */
static int pslot_arm_using(unsigned mask) {
    if(pslot_armed) return pslot_mask==(mask&~1u)?1:pstk_arm_refuse("slots_rerun");
    if(!(mask&~1u)) return pstk_arm_refuse("slots_none");
    if(pilot_start<PILOT_SUPPRESS_LEAD+3) return pstk_arm_refuse("slots_window");  /* the player needs take-2 >= 1 */
    if(!vps_create || !vps_rng_word) return pstk_arm_refuse("slots_factory");
    if(!pslot_fn.update) return pstk_arm_refuse("slots_fns");
    memset(pslots,0,sizeof(pslots));memset(pslot_getter_orig,0,sizeof(pslot_getter_orig));
    pslot_mask=mask&~1u;pslot_failed=0;pslot_stage="armed";pslot_why="-";pslot_p1=NULL;pslot_update_tick=0;pslot_hook_installed=0;
    pslot_callers=0;pslot_caller_tail=(unsigned)pslot_caller_head;pslot_caller_dropped=0;pslot_stop_waits=0;
    pslot_armed=1;return 1;
}
/* An abnormal stop: release on the frame thread outside any scope, else leave the
 * seats working (their scopes stay correct) and say so. */
static void pslot_restore(void) {
    if(!pslot_armed) return;
    int seated=0;
    for(unsigned k=2;k<=PILOT_SLOTS;k++) seated|=pslots[k].seated;
    if(seated && pslot_frame_thread() && !pstk_scope_slot) {
        for(unsigned k=2;k<=PILOT_SLOTS;k++) if(pslots[k].seated) pslot_release(k);
        seated=0;
        for(unsigned k=2;k<=PILOT_SLOTS;k++) seated|=pslots[k].seated;
    }
    if(!seated) pslot_hook_uninstall();
    else pslot_stage="left_seated";
}
/* A failure found outside the pass (a fault in a presentation entry, a record refused): the seats
 * are the frame thread's to release, and stopping from the tick gate leaves them seated. Its next
 * pass stops instead (path_release), this gate lets the frames run for that long at most. */
static int pslot_stop_deferred(void) {
    int seated=0;
    for(unsigned k=2;k<=PILOT_SLOTS;k++) seated|=pslots[k].seated;
    return pslot_armed && seated && GetCurrentThreadId()!=ui_thread && ++pslot_stop_waits<=PSLOT_STOP_WAIT;
}
/* For the local-player work (presentation of slot k on client k): slot k's player,
 * ship and PlayerController while seated (NULL otherwise; slot 1's are the singleton's). */
__attribute__((unused)) static void *vpl_slot_player(unsigned k) {return k>=2 && k<=PILOT_SLOTS && pslots[k].seated?pslots[k].player:NULL;}
__attribute__((unused)) static void *vpl_slot_ship(unsigned k) {return k>=2 && k<=PILOT_SLOTS && pslots[k].seated?pslots[k].ship:NULL;}
__attribute__((unused)) static void *vpl_slot_controller(unsigned k) {return k>=2 && k<=PILOT_SLOTS && pslots[k].seated?pslots[k].controller:NULL;}
static int pslot_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity," slots=%s slots_why=%s slots_mask=%x hook=%d updates=%lld/%lld routes=%lld/%lld/%lld",pslot_stage,pslot_why,
        pslot_mask,pslot_hook_installed,(long long)pslot_updates,(long long)pslot_hook_calls,(long long)pslot_route_hits[0],
        (long long)pslot_route_hits[1],(long long)pslot_route_hits[2]);
    for(unsigned k=2;n>0 && (size_t)n<capacity && k<=PILOT_SLOTS;k++) {
        pslot_slot *s=&pslots[k];
        if(!(pslot_mask>>(k-1)&1u)) continue;
        int more=snprintf(out+n,capacity-(size_t)n," s%u=%d/%u/%d/%lld/%lld/%lld/%lld/%lld/%lld/%lld",k,s->seated,s->authority,!s->roomless,
            (long long)s->scopes,
            (long long)s->updates,(long long)s->upkeep,(long long)s->environment,(long long)s->owner,(long long)s->replays,(long long)s->refused);
        if(more<0) return 0;
        n+=more;
    }
    for(unsigned i=0,first=1;n>0 && (size_t)n<capacity && i<PSLOT_VT_SLOTS;i++) {
        if(!pslot_getter_out[i]) continue;
        int more=snprintf(out+n,capacity-(size_t)n,"%s%u:%lld",first?" getters_out=":",",i,(long long)pslot_getter_out[i]);
        if(more<0) return 0;
        n+=more;first=0;
    }
    for(unsigned i=0,first=1;n>0 && (size_t)n<capacity && i<PSLOT_VT_SLOTS;i++) {
        if(!pslot_getter_render[i]) continue;
        int more=snprintf(out+n,capacity-(size_t)n,"%s%u:%lld",first?" getters_render=":",",i,(long long)pslot_getter_render[i]);
        if(more<0) return 0;
        n+=more;first=0;
    }
    return n>0 && (size_t)n<capacity?n:0;
}
