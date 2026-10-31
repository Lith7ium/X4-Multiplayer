/* native_gate_actor_rng_all: umbrella ship/agent scopes and fallbacks for
 * non-fixture ships, fixture streams unchanged, thread/order independence,
 * per-frame tallies, refusals and per-call cost. Owned memory only. */
#define TEST_AA_SAVED_A 0x51ULL
#define TEST_AA_SAVED_B 0x52ULL
#define TEST_AA_CREATED (CREATED_BIT|(15ULL<<CREATED_TICK_SHIFT)|0x3333333333ULL)
/* The key domains are part of the interface: pinned here. */
#define ACTOR_ALL_SHIP_DOMAIN_TEST 0x58344c4153484950ULL
#define ACTOR_ALL_AGENT_DOMAIN_TEST 0x58344c4141474e54ULL
#define ACTOR_ALL_FLIGHT_DOMAIN_TEST 0x58344c41464c4954ULL
#define ACTOR_ALL_NAV_DOMAIN_TEST 0x58344c414e415655ULL
#define ACTOR_ALL_WEAPON_DOMAIN_TEST 0x58344c4157454150ULL
static _Thread_local uint64_t *test_aa_word;
static _Thread_local uint64_t test_aa_seen[4];
static _Thread_local unsigned test_aa_draws;
static void (*test_aa_inner)(void);
static uint64_t test_aa_step(uint64_t s) {s=s*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;return (s>>30)|(s<<34);}
static void test_aa_draw(unsigned n) {for(unsigned i=0;i<n;i++) *test_aa_word=test_aa_step(*test_aa_word);}
static void test_aa_ship(void *self) {
    (void)self;test_aa_seen[0]=*test_aa_word;test_aa_draw(test_aa_draws);
    if(test_aa_inner) {void (*inner)(void)=test_aa_inner;test_aa_inner=NULL;inner();}
    test_aa_draw(1);
}
static void test_aa_agent(void *self,void *commands,float dt,double time,unsigned char cheap) {
    (void)self;(void)dt;(void)time;(void)cheap;
    if(commands) *(uint64_t*)commands=*test_aa_word;
    test_aa_seen[1]=*test_aa_word;test_aa_draw(test_aa_draws);
    if(test_aa_inner) {void (*inner)(void)=test_aa_inner;test_aa_inner=NULL;inner();}
}
static void test_aa_flight(void *self,void *slot,float dt,double time,unsigned char cheap,unsigned char player) {
    (void)self;(void)slot;(void)dt;(void)time;(void)cheap;(void)player;test_aa_seen[2]=*test_aa_word;test_aa_draw(2);
}
static void test_aa_nav(void *self,double time) {(void)self;(void)time;test_aa_seen[3]=*test_aa_word;test_aa_draw(1);}
static void test_aa_throw(void *self) {(void)self;test_aa_draw(2);RaiseException(0xe0414131,0,0,NULL);}
typedef struct {unsigned char object[0x100],agent[0x1000],model[0x40],controller[0x40];} test_aa_ship_t;
static void test_aa_make(test_aa_ship_t *s,uint64_t runtime) {
    memset(s,0,sizeof(*s));*(uint64_t*)(s->object+8)=runtime;*(void**)(s->agent+0xef0)=s->object;
    *(void**)(s->model+0x10)=s->agent;*(void**)(s->controller+0x28)=s->object;
}
static uint64_t test_aa_bits(double value) {uint64_t bits;memcpy(&bits,&value,8);return bits;}
static uint32_t test_aa_fbits(float value) {uint32_t bits;memcpy(&bits,&value,4);return bits;}
static test_aa_ship_t test_aa_ships[4],test_aa_docked;
static void test_aa_nested_ship(void) {uint64_t *w=test_aa_word;test_aa_draws=1;actor_all_ship_execute(test_aa_ship,test_aa_docked.object,w);}
static void test_aa_nested_plain(void) {test_aa_draws=1;test_aa_ship(test_aa_docked.object);} /* the hook with the umbrella off */
static void test_aa_nested_agent(void) {actor_all_agent_execute(test_aa_agent,test_aa_docked.agent,NULL,1.0f/60.0f,8.0,0,test_aa_word);}
static void test_aa_nested_flight(void) {actor_all_flight_execute(test_aa_flight,test_aa_ships[0].model,NULL,1.0f/60.0f,8.0,0,0,test_aa_word);}
static void test_aa_nested_nav(void) {actor_all_nav_execute(test_aa_nav,test_aa_ships[0].controller,8.0,test_aa_word);}
static void test_aa_reset(void) {
    memset(actor_all_shards,0,sizeof(actor_all_shards));memset(actor_all_total,0,sizeof(actor_all_total));
    memset(actor_all_frame_delta,0,sizeof(actor_all_frame_delta));actor_all_frames=actor_all_frame_tick=actor_all_table_full=0;
    actor_all_failures=0;actor_all_ship_depth=0;actor_all_agent_depth=0;actor_all_epoch_tick=UINT64_MAX;test_aa_inner=NULL;
}
static uint64_t test_aa_field(unsigned path,unsigned field) {
    uint64_t sum=0;for(unsigned i=0;i<ACTOR_ALL_SHARDS;i++) sum+=actor_all_shards[i].v[path][field];return sum;
}

/* Ship Move umbrella and the sim-thread fallbacks. */
static void test_aa_ship_paths(void) {
    test_pc_begin();test_aa_reset();actor_all_armed=1;stats.issued=20;
    assert(world_identity_record(0x9001,TEST_AA_SAVED_A) && world_identity_record(0x9002,TEST_AA_SAVED_B));
    test_aa_make(&test_aa_ships[0],0x9001);test_aa_make(&test_aa_ships[1],0x9003);test_aa_make(&test_aa_ships[2],0x9004);
    test_aa_make(&test_aa_docked,0x9002);test_pc_create(0x9003,TEST_AA_CREATED);
    uint64_t word=0x1111;test_aa_word=&word;test_aa_draws=3;
    /* Saved ship: its own stream, ordinal 1, the caller's word restored. */
    uint64_t key=lockstep_seed(ACTOR_ALL_SHIP_DOMAIN_TEST,20,TEST_AA_SAVED_A,0,1,0);
    actor_all_ship_execute(test_aa_ship,test_aa_ships[0].object,&word);
    assert(test_aa_seen[0]==key && word==0x1111 && !actor_all_ship_depth);
    assert(test_aa_field(ACTOR_ALL_SHIP,AA_KEYED)==1 && test_aa_field(ACTOR_ALL_SHIP,AA_DRAWN)==1 && test_aa_field(ACTOR_ALL_SHIP,AA_DRAWS)==4);
    actor_all_ship_execute(test_aa_ship,test_aa_ships[0].object,&word); /* second Move this tick */
    assert(test_aa_seen[0]==lockstep_seed(ACTOR_ALL_SHIP_DOMAIN_TEST,20,TEST_AA_SAVED_A,0,2,0));
    /* Created ship: unmapped without portable keys, its created ID with them. */
    actor_all_ship_execute(test_aa_ship,test_aa_ships[1].object,&word);
    assert(test_aa_seen[0]==0x1111 && word!=0x1111 && test_aa_field(ACTOR_ALL_SHIP,AA_UNMAPPED)==1);
    word=0x1111;world_portable_created=1;
    actor_all_ship_execute(test_aa_ship,test_aa_ships[1].object,&word);
    assert(test_aa_seen[0]==lockstep_seed(ACTOR_ALL_SHIP_DOMAIN_TEST,20,TEST_AA_CREATED,0,1,0) && word==0x1111 &&
           test_aa_field(ACTOR_ALL_SHIP,AA_CREATED)==1);
    /* No portable identity: natural word. */
    actor_all_ship_execute(test_aa_ship,test_aa_ships[2].object,&word);
    assert(test_aa_seen[0]==0x1111 && test_aa_field(ACTOR_ALL_SHIP,AA_UNMAPPED)==2);word=0x1111;
    /* Another client: other runtime IDs and addresses, the same keys. */
    static test_aa_ship_t other;test_aa_make(&other,0x7001);assert(world_identity_record(0x7001,TEST_AA_SAVED_A));
    stats.issued=21;actor_all_ship_execute(test_aa_ship,other.object,&word);
    uint64_t other_key=test_aa_seen[0];stats.issued=22;actor_all_ship_execute(test_aa_ship,test_aa_ships[0].object,&word);
    assert(other_key==lockstep_seed(ACTOR_ALL_SHIP_DOMAIN_TEST,21,TEST_AA_SAVED_A,0,1,0) &&
           test_aa_seen[0]==lockstep_seed(ACTOR_ALL_SHIP_DOMAIN_TEST,22,TEST_AA_SAVED_A,0,1,0));
    /* A docked ship moved inside a keyed ship continues its stream. */
    stats.issued=23;test_aa_inner=test_aa_nested_ship;test_aa_draws=2;
    actor_all_ship_execute(test_aa_ship,test_aa_ships[0].object,&word);
    uint64_t parent=lockstep_seed(ACTOR_ALL_SHIP_DOMAIN_TEST,23,TEST_AA_SAVED_A,0,1,0);
    assert(test_aa_seen[0]==test_aa_step(test_aa_step(parent)) && word==0x1111 && test_aa_field(ACTOR_ALL_SHIP,AA_NESTED)==1);
    /* Fixture: ship_execute's key and stream consumption are the same with
     * the umbrella armed; a non-fixture ship nested in it continues it. */
    uint64_t fixture_after[2],nested_seen[2];ship_sample fixture;ship_seeded=1;
    for(unsigned on=0;on<2;on++) {
        actor_all_armed=on;memset(&fixture,0,sizeof(fixture));fixture.calls=1;word=0x2222;test_aa_draws=1;
        test_aa_inner=on?test_aa_nested_ship:test_aa_nested_plain;
        ship_execute(test_aa_ship,&fixture,6,&word,test_aa_ships[0].object);
        assert(word==0x2222 && fixture.rng_before==actor_seed(0x534849505f4d4f56ULL,23,6,0,1,0) && !actor_all_ship_depth);
        fixture_after[on]=fixture.rng_after;nested_seen[on]=test_aa_seen[0];
    }
    assert(fixture_after[0]==fixture_after[1] && nested_seen[0]==nested_seen[1] && test_aa_field(ACTOR_ALL_SHIP,AA_NESTED)==2);
    ship_seeded=0;actor_all_armed=1;
    /* Navigation and weapon fallbacks: nested in a ship scope, else own key. */
    test_aa_inner=test_aa_nested_nav;actor_all_ship_execute(test_aa_ship,test_aa_ships[0].object,&word);
    assert(test_aa_field(ACTOR_ALL_NAV,AA_NESTED)==1 && !test_aa_field(ACTOR_ALL_NAV,AA_KEYED));
    stats.issued=24;actor_all_nav_execute(test_aa_nav,test_aa_ships[0].controller,8.0,&word);
    assert(test_aa_seen[3]==lockstep_seed(ACTOR_ALL_NAV_DOMAIN_TEST,24,TEST_AA_SAVED_A,0,1,test_aa_bits(8.0)) && word==0x2222);
    test_aa_draws=1;actor_all_weapon_execute(test_aa_ship,test_aa_ships[1].object,&word); /* a created weapon's own ID */
    assert(test_aa_seen[0]==lockstep_seed(ACTOR_ALL_WEAPON_DOMAIN_TEST,24,TEST_AA_CREATED,0,1,0) && word==0x2222);
    /* An exception unwinds the scope: word and depth restored, counted. */
    int caught=0;
    __try {actor_all_ship_execute(test_aa_throw,test_aa_ships[0].object,&word);}
    __except(GetExceptionCode()==0xe0414131?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=1;}
    assert(caught && word==0x2222 && !actor_all_ship_depth && actor_all_failures==1);
    /* Ordinal table exhaustion: ordinal 0, counted. */
    stats.issued=25;
    for(unsigned i=0;i<ACTOR_ALL_ORDINALS+ACTOR_ALL_PROBES;i++) actor_all_ordinal(ACTOR_ALL_SHIP,0x100000+i);
    assert(actor_all_table_full>0 && actor_all_ordinal(ACTOR_ALL_SHIP,0x100000)==2);
    static const uint64_t domains[ACTOR_ALL_PATHS]={ACTOR_ALL_SHIP_DOMAIN_TEST,ACTOR_ALL_AGENT_DOMAIN_TEST,ACTOR_ALL_FLIGHT_DOMAIN_TEST,
        ACTOR_ALL_NAV_DOMAIN_TEST,ACTOR_ALL_WEAPON_DOMAIN_TEST};
    for(unsigned p=0;p<ACTOR_ALL_PATHS;p++) assert(actor_all_domains[p]==domains[p]);
    world_portable_created=0;actor_all_armed=0;test_aa_reset();test_pc_end();
    puts("actor RNG all ships: saved/created/unmapped ship keys, ordinals, two clients, nesting, fixture stream unchanged, nav/weapon fallbacks, unwind PASS");
}

/* Agent umbrella on several threads in different orders. */
#define TEST_AA_AGENTS 64u
static test_aa_ship_t test_aa_fleet[TEST_AA_AGENTS];
typedef struct {unsigned first,step,count;uint64_t word;} test_aa_job;
static uint64_t test_aa_out[TEST_AA_AGENTS];
static DWORD WINAPI test_aa_worker(void *argument) {
    test_aa_job *job=argument;test_aa_word=&job->word;
    for(unsigned n=0;n<job->count;n++) {
        unsigned i=(job->first+n*job->step)%TEST_AA_AGENTS;test_aa_draws=i%5;
        actor_all_agent_execute(test_aa_agent,test_aa_fleet[i].agent,&test_aa_out[i],1.0f/60.0f,8.5,0,&job->word);
    }
    return 0;
}
static void test_aa_run(unsigned threads,unsigned step,uint64_t out[TEST_AA_AGENTS],uint64_t tally[4]) {
    test_aa_job jobs[4];HANDLE handles[4];unsigned per=TEST_AA_AGENTS/threads;
    for(unsigned t=0;t<threads;t++) {
        jobs[t]=(test_aa_job){.first=t*per*step,.step=step,.count=per,.word=0x1234567ULL*(t+1)};
        handles[t]=CreateThread(NULL,0,test_aa_worker,&jobs[t],0,NULL);assert(handles[t]);
    }
    WaitForMultipleObjects(threads,handles,TRUE,10000);
    for(unsigned t=0;t<threads;t++) {CloseHandle(handles[t]);assert(jobs[t].word==0x1234567ULL*(t+1));}
    memcpy(out,test_aa_out,sizeof(test_aa_out));actor_all_frame(stats.issued);
    tally[0]=actor_all_frame_delta[ACTOR_ALL_AGENT][AA_KEYED];tally[1]=actor_all_frame_delta[ACTOR_ALL_AGENT][AA_DRAWS];
    tally[2]=actor_all_frame_delta[ACTOR_ALL_AGENT][AA_KEYSET];tally[3]=actor_all_frame_delta[ACTOR_ALL_AGENT][AA_DRAWSET];
}
static void test_aa_agent_paths(void) {
    test_pc_begin();test_aa_reset();actor_all_armed=1;stats.issued=30;world_portable_created=1;
    for(unsigned i=0;i<TEST_AA_AGENTS;i++) {
        test_aa_make(&test_aa_fleet[i],0xa000+i);
        if(i%3) assert(world_identity_record(0xa000+i,0x600+i));else test_pc_create(0xa000+i,TEST_AA_CREATED+i);
    }
    uint64_t one[TEST_AA_AGENTS],other[TEST_AA_AGENTS],tally_one[4],tally_other[4];
    test_aa_run(1,1,one,tally_one);test_aa_run(4,7,other,tally_other); /* sequential vs four threads, permuted */
    assert(!memcmp(one,other,sizeof(one)) && !memcmp(tally_one,tally_other,sizeof(tally_one)) && tally_one[0]==TEST_AA_AGENTS);
    uint64_t draws=0;for(unsigned i=0;i<TEST_AA_AGENTS;i++) draws+=i%5;assert(tally_one[1]==draws);
    for(unsigned i=0;i<TEST_AA_AGENTS;i++) {
        uint64_t id=i%3?0x600+i:TEST_AA_CREATED+i;
        assert(one[i]==lockstep_seed(ACTOR_ALL_AGENT_DOMAIN_TEST,30,id,test_aa_fbits(1.0f/60.0f),0,test_aa_bits(8.5)));
    }
    assert(actor_all_frames==2 && actor_all_frame_tick==30 && actor_all_total[ACTOR_ALL_AGENT][AA_KEYED]==2*TEST_AA_AGENTS);
    /* Nesting: an agent and a steering update inside an agent scope continue it;
     * the same inside the fixture's agent_execute; outside, steering keys itself. */
    test_aa_make(&test_aa_ships[0],0xa001);test_aa_make(&test_aa_docked,0xa002);
    uint64_t word=0x99;test_aa_word=&word;test_aa_draws=1;stats.issued=31;
    test_aa_inner=test_aa_nested_agent;actor_all_agent_execute(test_aa_agent,test_aa_ships[0].agent,NULL,1.0f/60.0f,8.0,0,&word);
    assert(test_aa_field(ACTOR_ALL_AGENT,AA_NESTED)==1 && word==0x99 && !actor_all_agent_depth);
    test_aa_inner=test_aa_nested_flight;actor_all_agent_execute(test_aa_agent,test_aa_ships[0].agent,NULL,1.0f/60.0f,8.0,0,&word);
    assert(test_aa_field(ACTOR_ALL_FLIGHT,AA_NESTED)==1 && !test_aa_field(ACTOR_ALL_FLIGHT,AA_KEYED));
    agent_sample fixture;memset(&fixture,0,sizeof(fixture));fixture.calls=1;agent_seeded=1;test_aa_inner=test_aa_nested_agent;
    agent_execute(test_aa_agent,&fixture,3,&word,NULL,NULL,1.0f/60.0f,8.0,0);
    assert(fixture.rng_before==actor_seed(0x4147454e545f524eULL,31,3,0,1,0) && word==0x99 && test_aa_field(ACTOR_ALL_AGENT,AA_NESTED)==2);
    agent_seeded=0;
    actor_all_flight_execute(test_aa_flight,test_aa_ships[0].model,NULL,1.0f/60.0f,8.0,0,0,&word);
    assert(test_aa_seen[2]==lockstep_seed(ACTOR_ALL_FLIGHT_DOMAIN_TEST,31,0x601,test_aa_fbits(1.0f/60.0f),0,test_aa_bits(8.0)) && word==0x99);
    /* Unmapped agent: natural word, nothing keyed. */
    static test_aa_ship_t stranger;test_aa_make(&stranger,0xbeef);
    actor_all_agent_execute(test_aa_agent,stranger.agent,NULL,1.0f/60.0f,8.0,0,&word);
    assert(test_aa_seen[1]==0x99 && test_aa_field(ACTOR_ALL_AGENT,AA_UNMAPPED)==1);
    /* Status: bounded, per path, with the last frame's sums. */
    actor_all_frame(31);char status[3072];int n=actor_all_status(status,sizeof(status));
    assert(n>0 && strstr(status,"armed=1 frames=3 last_tick=31 ") && strstr(status," agent_keyed=") && strstr(status," weapon_keyset="));
    printf("NATIVE_GATE ACTOR_RNG_ALL tick=31 %s\n",status);
    for(unsigned p=0;p<ACTOR_ALL_PATHS;p++) for(unsigned f=0;f<AA_FIELDS;f++) actor_all_total[p][f]=actor_all_frame_delta[p][f]=UINT64_MAX;
    actor_all_frames=actor_all_frame_tick=actor_all_table_full=UINT64_MAX;actor_all_failures=INT64_MIN;
    n=actor_all_status(status,sizeof(status));assert(n>0 && (unsigned)n+64<sizeof(status));
    printf("actor RNG all ships worst-case status %d bytes\n",n);
    world_portable_created=0;actor_all_armed=0;test_aa_reset();test_pc_end();
    puts("actor RNG all ships: agent keys independent of thread and order, frame tallies, agent/steering nesting, fixture agent unchanged PASS");
}

/* Arming: before loading, on the UI thread, with the seeded agent and ship modules. */
static void test_aa_arm(void) {
    int prior[4]={agent_installed,agent_seeded,ship_installed,ship_seeded};int identity=world_identity_installed;
    stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();world_identity_installed=1;
    agent_installed=agent_seeded=ship_installed=ship_seeded=1;actor_all_armed=0;
    agent_seeded=0;assert(!actor_all_arm());agent_seeded=1;
    ship_installed=0;assert(!actor_all_arm());ship_installed=1;
    world_identity_installed=0;assert(!actor_all_arm());world_identity_installed=1;
    boot_phase=1;assert(!actor_all_arm());boot_phase=0;
    ui_thread=GetCurrentThreadId()+1;assert(!actor_all_arm());ui_thread=GetCurrentThreadId();
    assert(!actor_all_armed && actor_all_arm() && actor_all_armed && actor_all_arm()); /* a reload accepts */
    ship_seeded=0;assert(!actor_all_arm());
    actor_all_armed=0;agent_installed=prior[0];agent_seeded=prior[1];ship_installed=prior[2];ship_seeded=prior[3];
    world_identity_installed=identity;stats.active=0;boot_mode=0;
    puts("actor RNG all ships: arm refusals and reload PASS");
}

/* Cost: a saved table of the BS4 size, 10,000 ships, a trivial original. */
static void test_aa_bench(void) {
    test_pc_begin();test_aa_reset();actor_all_armed=1;world_portable_created=1;stats.issued=40;
    QueryPerformanceCounter(&actor_all_qpc0);actor_all_tsc0=__builtin_ia32_rdtsc();
    for(unsigned i=0;i<521929;i++) assert(world_identity_record(0x200000+i*7,0x1000+i));
    static test_aa_ship_t *ships;ships=VirtualAlloc(NULL,sizeof(*ships)*10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(ships);
    for(unsigned i=0;i<10000;i++) {
        if(i%10) test_aa_make(&ships[i],0x200000+(i*977%521929)*7);
        else {test_aa_make(&ships[i],0x900000+i);test_pc_create(0x900000+i,TEST_AA_CREATED+i);}
    }
    world_identity_sealed=1;uint64_t word=5;test_aa_word=&word;test_aa_draws=1;
    LARGE_INTEGER f,a,b,c;QueryPerformanceFrequency(&f);const unsigned rounds=20;
    QueryPerformanceCounter(&a);
    for(unsigned r=0;r<rounds;r++) for(unsigned i=0;i<10000;i++) test_aa_agent(ships[i].agent,NULL,1.0f/60.0f,8.0,0);
    QueryPerformanceCounter(&b);
    for(unsigned r=0;r<rounds;r++) for(unsigned i=0;i<10000;i++) actor_all_agent_execute(test_aa_agent,ships[i].agent,NULL,1.0f/60.0f,8.0,0,&word);
    QueryPerformanceCounter(&c);
    double bare=(double)(b.QuadPart-a.QuadPart)*1e9/f.QuadPart/(rounds*10000.0),keyed=(double)(c.QuadPart-b.QuadPart)*1e9/f.QuadPart/(rounds*10000.0);
    QueryPerformanceCounter(&a);
    for(unsigned r=0;r<rounds;r++) {++stats.issued;for(unsigned i=0;i<10000;i++) actor_all_ship_execute(test_aa_ship,ships[i].object,&word);}
    QueryPerformanceCounter(&b);
    double ship=(double)(b.QuadPart-a.QuadPart)*1e9/f.QuadPart/(rounds*10000.0);
    assert(test_aa_field(ACTOR_ALL_AGENT,AA_KEYED)==rounds*10000 && test_aa_field(ACTOR_ALL_AGENT,AA_CREATED)==rounds*1000);
    printf("actor RNG all ships cost: agent original %.1f ns, keyed agent %.1f ns (+%.1f), keyed ship %.1f ns per call;"
           " 10,000 ships x (agent+ship) = %.2f ms per frame; timed self cost agent %llu ns ship %llu ns\n",
           bare,keyed,keyed-bare,ship,(keyed-bare+ship-bare)*10000/1e6,
           (unsigned long long)actor_all_ns(test_aa_field(ACTOR_ALL_AGENT,AA_TICKS),test_aa_field(ACTOR_ALL_AGENT,AA_TIMED)),
           (unsigned long long)actor_all_ns(test_aa_field(ACTOR_ALL_SHIP,AA_TICKS),test_aa_field(ACTOR_ALL_SHIP,AA_TIMED)));
    assert(keyed-bare<5000.0 && ship-bare<5000.0); /* generous bound: correctness run, not a benchmark gate */
    VirtualFree(ships,0,MEM_RELEASE);world_portable_created=0;actor_all_armed=0;test_aa_reset();test_pc_end();
}
/* Scope rows: one per keyed scope that drew, with its path, portable ID and draws. */
static void test_aa_rows(void) {
    test_pc_begin();test_aa_reset();actor_all_armed=1;stats.issued=664;
    assert(world_identity_record(0x9001,TEST_AA_SAVED_A) && world_identity_record(0x9002,TEST_AA_SAVED_B));
    test_aa_make(&test_aa_ships[0],0x9001);test_aa_make(&test_aa_ships[1],0x9002);
    uint64_t word=0x1111;test_aa_word=&word;
    actor_all_rows=0;actor_all_rows_armed=0;test_aa_draws=2;
    actor_all_ship_execute(test_aa_ship,test_aa_ships[0].object,&word);assert(!actor_all_rows); /* not armed */
    actor_all_rows_armed=1;actor_all_ship_execute(test_aa_ship,test_aa_ships[0].object,&word);
    test_aa_draws=0;actor_all_ship_execute(test_aa_ship,test_aa_ships[1].object,&word); /* draws once (test_aa_ship's tail) */
    test_aa_draws=2;actor_all_nav_execute(test_aa_nav,test_aa_ships[1].controller,8.0,&word);
    assert(actor_all_rows==3 && word==0x1111);
    char row[256];
    assert(actor_all_row(0,row,sizeof(row))>0 && !strcmp(row,"index=0 tick=664 path=ship id=0000000000000051 draws=3"));
    assert(actor_all_row(1,row,sizeof(row))>0 && !strcmp(row,"index=1 tick=664 path=ship id=0000000000000052 draws=1"));
    assert(actor_all_row(2,row,sizeof(row))>0 && !strcmp(row,"index=2 tick=664 path=nav id=0000000000000052 draws=1"));
    assert(!actor_all_row(3,row,sizeof(row)));
    actor_all_rows=ACTOR_ALL_ROW_RING+4;assert(actor_all_row(1,row,sizeof(row))>0 && !strcmp(row,"index=1 evicted=1"));
    actor_all_ring[5%ACTOR_ALL_ROW_RING]=(actor_all_row_t){UINT64_MAX,UINT64_MAX,ACTOR_ALL_WEAPON,UINT32_MAX};
    assert(actor_all_row(5,row,sizeof(row))>0 && strstr(row," path=weapon id=ffffffffffffffff draws=unknown"));
    actor_all_rows=0;actor_all_rows_armed=0;memset(actor_all_ring,0,sizeof(actor_all_ring));
    actor_all_armed=0;test_aa_reset();test_pc_end();
    puts("actor RNG scope rows (drawing scopes only, path/id/draws, eviction) PASS");
}
static void test_actor_all(void) {
    test_aa_rows();
    test_aa_ship_paths();
    test_aa_agent_paths();
    test_aa_arm();
    test_aa_bench();
}
