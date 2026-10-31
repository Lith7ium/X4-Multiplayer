static int world_find_pins(void) {
    static const world_action_pin pins[]={
      {0x1063750,3437,"0d1c301108dac632a0163a03f309310e188c2df418a4222694fcec3ca646c056"},
      {0x473600,107,"843aa5cc1726066e0a178ed0e4ecb588847dfd189ef683be4539cf4c19f788fa"},
      {0x473670,160,"eab79f3c53cc21a80aef17f697d890522dccc1d13b8fbdb6bc2ad7aaf901a98e"},
      {0x33d420,184,"60bad9f6942f799aad1afa992e6c3e114ddc9d404dda1d4a24481af443cd001f"},
      {0x34dde0,296,"3bf3acb6cb98352ab6dbe174a830907c5fb0a05b04bd4a9a64dd2ad1b3f141a4"},
      {0x1075c30,125,"8004aaee403942341ed60325f04ebe0ef984254cb20d6bf439b896ba93dbe2be"},
      {0x107b450,5231,"12609fca116f591829e65f75c6970f1ed6e10fcc2c55718645e158eeef489c9a"}};
    for(unsigned i=0;i<sizeof(pins)/sizeof(*pins);i++) if(!hash_ok(image+pins[i].rva,pins[i].size,pins[i].sha)) return 0;
    uintptr_t a=0,b=0;char source[sizeof(".?AVOrderIDBase@U@@")],target[sizeof(".?AVAIOrder@AI@@")];
    return read_local(image+0x2c69060,&a,8)&&a==(uintptr_t)(image+0x1075cb0)&&
        read_local(image+0x2c69068,&b,8)&&b==(uintptr_t)(image+0xb3d60)&&
        read_local(image+0x31fc828,source,sizeof(source))&&!memcmp(source,".?AVOrderIDBase@U@@",sizeof(source))&&
        read_local(image+0x31fc850,target,sizeof(target))&&!memcmp(target,".?AVAIOrder@AI@@",sizeof(target));
}
/* Compact wire has no Lua-number u64 fields. The native experiment's supported
 * selector is deliberately fixed; public manifest validation supplies every
 * field explicitly and refuses all other selectors/guards. */
static int world_find_parse_composition(const char *text,size_t length,uint64_t *shape,uint64_t *seed,unsigned char digest[32],unsigned *composition) {
    static const char prefix[]="FIND_COMPONENT_RNG_1;";
    static const char third[]="FIND_COMPONENT_RNG_2;";
    if(!text||!length||length>=sizeof(world_find_manifest)||memchr(text,0,length)) return 0;
    char copy[sizeof(world_find_manifest)];memcpy(copy,text,length);copy[length]=0;
    if(!strncmp(copy,prefix,sizeof(prefix)-1)) *composition=1;
    else if(!strncmp(copy,third,sizeof(third)-1)) *composition=2;else return 0;
    const char *p=copy+sizeof(prefix)-1;
    if(strlen(p)<65||memcmp(p,WORLD_FIND_SNAPSHOT,64)||p[64]!=';') return 0;
    unsigned char snapshot[32];for(unsigned i=0;i<32;i++) {
        unsigned a=p[i*2],b=p[i*2+1];a=a<='9'?a-'0':a-'a'+10;b=b<='9'?b-'0':b-'a'+10;snapshot[i]=(unsigned char)((a<<4)|b);
    }p+=65;uint64_t expected;
    if(!world_wait_number(&p,';',16,shape)||*shape!=WORLD_FIND_SHAPE||!world_wait_number(&p,';',16,&expected)||
       !world_find_key(snapshot,WORLD_FIND_SAVED,WORLD_FIND_ORDER,0,0,digest,seed)||*seed!=expected) return 0;
    return *composition==1?!*p:!strcmp(p,WORLD_FIND_THIRD_CONTRACT ";");
}
/* Same-session prerequisites, shared by first install and read-only UI retry.
 * Do not take a parent observer lock or interpret aggregate unknown coverage. */
static int world_find_dependencies_for(unsigned composition) {
    if(!boot_mode||!world_wait_enabled||world_wait_restoring||!world_wait_rng_started||
       !world_action_enabled||world_action_closing||!world_values_enabled||world_values_closing||
       world_values_sites!=3||world_wait_selected_count!=3) return 0;
    if(composition==2) {
        static const uint64_t saved[]={0x80d54,0x9d3a4,0x6d945};
        static const uint64_t time[]={0x401baaab8a5ce5b5ULL,0x4019444523f67f4eULL,0x401beeee0f3cb3e6ULL};
        static const uint64_t minimum[]={0x3fb999999999999aULL,0x3f50624dd2f1a9fcULL,0x3fb999999999999aULL};
        static const uint64_t maximum[]={0x3fe0000000000000ULL,0x3ff0000000000000ULL,0x3fe0000000000000ULL};
        static const uint64_t seed[]={0x41aa3ad655ad9a9cULL,0x8b0f91e8910d9f7cULL,0x4cf7dda6c67c9349ULL};
        static const char *const names[]={"move.idle","order.fight.patrol","move.idle"};
        static const char *const texts[]={"6.91667","6.31667","6.98333"};
        static const uint64_t snapshot[]={0x7816351056e6040dULL,0x92ae9a4619ae2e15ULL,0xa577fa26a19134f1ULL,0x1e20746317e61aa8ULL};
        /* Versioned composition, not an observe-or-intervene relaxation. */
        if(world_wait_rng_schema!=2||memcmp(world_wait_rng_snapshot,snapshot,sizeof(snapshot))||
           strnlen(world_wait_rng_manifest,sizeof(world_wait_rng_manifest))!=sizeof(WORLD_FIND_THIRD_POLICY)-1||
           memcmp(world_wait_rng_manifest,WORLD_FIND_THIRD_POLICY,sizeof(WORLD_FIND_THIRD_POLICY))) return 0;
        for(unsigned i=0;i<3;i++) {
            const world_wait_selection *w=&world_wait_selected[i];const world_wait_rng_selection *r=&world_wait_rng_selected[i];
            if(w->saved!=saved[i]||memcmp(w->name,names[i],strlen(names[i])+1)||w->version!=(i==1?10u:4u)||w->index!=(i==1?1u:5u)||
               w->time!=time[i]||w->minimum!=minimum[i]||w->maximum!=maximum[i]||r->policy!=WWR_INTERVENE||
               r->slot!=(i==1?121u:70u)||r->pc!=(i==1?122u:71u)||r->since||r->seed!=seed[i]||r->used>1||
               memcmp(r->text,texts[i],strlen(texts[i])+1)||r->script_hash!=world_wait_rng_hash(names[i])||r->time_hash!=world_wait_rng_hash(texts[i])) return 0;
        }return 1;
    }
    if(composition!=1) return 0;
    unsigned found=0,trader=0,natural=0;
    for(unsigned i=0;i<world_wait_selected_count;i++) {
        const world_wait_selection *w=&world_wait_selected[i];const world_wait_rng_selection *r=&world_wait_rng_selected[i];
        if(w->saved==WORLD_FIND_SAVED&&!strcmp(w->name,"order.fight.patrol")&&w->version==10&&w->index==1&&
           r->policy==WWR_INTERVENE&&r->slot==121&&r->pc==122&&r->seed==0x8b0f91e8910d9f7cULL) ++found;
        if(w->saved==0x80d54&&!strcmp(w->name,"move.idle")&&w->version==4&&w->index==5&&
           r->policy==WWR_INTERVENE&&r->slot==70&&r->pc==71&&r->seed==0x41aa3ad655ad9a9cULL) ++trader;
        if(w->saved==0x6d945&&!strcmp(w->name,"move.idle")&&w->version==4&&w->index==5&&r->policy==WWR_OBSERVE) ++natural;
    }return found==1&&trader==1&&natural==1;
}
static int world_find_dependencies(void) {return world_find_dependencies_for(world_find_composition?world_find_composition:1);}
static int world_find_configure_using(const char *text,size_t length,int(*pins)(void)) {
    uint64_t shape=0,seed=0;unsigned char digest[32];unsigned composition=0;
    if(!world_find_parse_composition(text,length,&shape,&seed,digest,&composition)||GetCurrentThreadId()!=ui_thread) return 0;
    AcquireSRWLockExclusive(&world_find_lock);int ok=stats.active&&!world_find_closed;
    if(world_find_started) ok=ok&&world_find_enabled&&world_find_composition==composition&&world_find_dependencies()&&
        !world_find_failures&&!world_find_dropped&&!world_find_restore_open&&!world_find_anchor_errors&&!world_find_open&&!world_find_anchor_pending&&
        world_wait_anchor_observer==world_find_wait_anchor&&world_wait_rng_foreign_active==world_find_conflict&&world_values_candidate_call==world_find_call&&
        world_find_seed==seed&&world_find_shape==shape&&!memcmp(world_find_digest,digest,32)&&strlen(world_find_manifest)==length&&!memcmp(world_find_manifest,text,length);
    else {
        ok=ok&&!boot_phase&&!stats.issued&&world_find_dependencies_for(composition)&&pins();
        if(ok&&stats.active&&!world_find_closed) {
            world_find_composition=composition;world_find_shape=shape;world_find_seed=seed;memcpy(world_find_digest,digest,32);
            memcpy(world_find_manifest,text,length);world_find_manifest[length]=0;
            world_wait_anchor_observer=world_find_wait_anchor;world_wait_rng_foreign_active=world_find_conflict;
            world_values_candidate_call=world_find_call;world_find_started=world_find_enabled=1;
        }else ok=0;
    }
    ReleaseSRWLockExclusive(&world_find_lock);return ok;
}
static int world_find_configure(const char *text,size_t length) {return world_find_configure_using(text,length,world_find_pins);}
