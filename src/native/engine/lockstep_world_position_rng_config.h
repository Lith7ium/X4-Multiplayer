/* Configuration is a fixed semantic selector with one explicit dependency
 * composition. No Lua double carries any seed or other uint64 value. */
static struct {uintptr_t image,thunk;uint64_t started,options;unsigned slot,port;
    char token[33],snapshot[65],find[256],actions[WORLD_ACTION_MANIFEST_CAP];} wpr_binding;
static int wpr_bound(void) {
    return wpr_binding.image==(uintptr_t)image&&wpr_binding.thunk==(uintptr_t)thunk&&
        wpr_binding.started==boot_started&&wpr_binding.options==boot_options&&wpr_binding.slot==stats.slot&&wpr_binding.port==remote_port&&
        !memcmp(wpr_binding.token,secret,33)&&!memcmp(wpr_binding.snapshot,initial_state_digest,65)&&
        !memcmp(wpr_binding.find,world_find_manifest,sizeof(wpr_binding.find))&&
        !memcmp(wpr_binding.actions,world_action_manifest,sizeof(wpr_binding.actions));
}
static void wpr_bind(void) {
    wpr_binding.image=(uintptr_t)image;wpr_binding.thunk=(uintptr_t)thunk;wpr_binding.started=boot_started;wpr_binding.options=boot_options;
    wpr_binding.slot=stats.slot;wpr_binding.port=remote_port;memcpy(wpr_binding.token,secret,33);memcpy(wpr_binding.snapshot,initial_state_digest,65);
    memcpy(wpr_binding.find,world_find_manifest,sizeof(wpr_binding.find));memcpy(wpr_binding.actions,world_action_manifest,sizeof(wpr_binding.actions));
}
static int wpr_config_dependencies(unsigned composition) {
    int ok;AcquireSRWLockShared(&world_find_lock);
    ok=world_find_started&&world_find_enabled&&!world_find_closed&&world_find_composition==composition&&world_find_dependencies_for(composition)&&
        !world_find_failures&&!world_find_dropped&&!world_find_restore_open&&!world_find_open&&!world_find_anchor_pending&&!world_find_anchor_errors&&
        world_wait_anchor_observer==world_find_wait_anchor&&world_wait_rng_foreign_active==world_find_conflict&&world_values_candidate_call==world_find_call;
    ReleaseSRWLockShared(&world_find_lock);
    AcquireSRWLockShared(&wp_lock);
    ok=ok&&wp_started&&wp_enabled&&!wp_closing&&!wp_open&&!wp_restore_open&&!wp_failures&&!wp_dropped&&!wp_patch_failures&&wp_bound()&&
        wp_patch_mask(0)==15&&!wp_patch_mask(1)&&!wp_patch_mask(2);
    for(unsigned i=0;ok&&i<4;i++){
        unsigned __int128 actual=0;const wp_patch *p=&wp_patches[i];
        ok=read_local(image+p->rva,&actual,p->size)&&actual==p->patched&&thunk_slot_ready(p->offset);
    }
    ReleaseSRWLockShared(&wp_lock);return ok;
}
static int wpr_parse(const char *text,size_t length,unsigned *composition,unsigned char digest[32],uint64_t *seed) {
    static const char prefix[]="POSITION_RNG_1;" WORLD_FIND_SNAPSHOT ";";
    char wanted[256];
    if(!text||!length||length>=sizeof(wanted)||memchr(text,0,length)||!wpr_key(digest,seed))return 0;
    for(unsigned i=1;i<=2;i++){
        int n=snprintf(wanted,sizeof(wanted),"%s%u;985d66488a43c795;17bafa51eebe82a5;",prefix,i);
        if(n>0&&(size_t)n==length&&!memcmp(text,wanted,length)){*composition=i;return 1;}
    }return 0;
}
static int wpr_configure_using(const char *text,size_t length,int(*pins)(void)) {
    unsigned composition=0;unsigned char digest[32];uint64_t seed=0;
    if(GetCurrentThreadId()!=ui_thread||!wpr_parse(text,length,&composition,digest,&seed)||!wpr_config_dependencies(composition))return 0;
    AcquireSRWLockExclusive(&wpr_lock);int ok=stats.active&&boot_mode&&!wpr_closed;
    if(wpr_started){
        ok=ok&&wpr_enabled&&boot_phase<=3&&!wpr_failures&&!wpr_dropped&&!wpr_restore_open&&!wpr_open&&!wpr_debt&&wpr_bound()&&
            wpr_composition==composition&&wpr_seed==seed&&!memcmp(wpr_digest,digest,32)&&strlen(wpr_manifest)==length&&!memcmp(wpr_manifest,text,length)&&
            wp_rng_begin==wpr_begin&&wp_rng_sample==wpr_sample&&wp_rng_finish==wpr_finish&&world_find_extra_conflict==wpr_conflict;
    }else{
        ok=ok&&!boot_phase&&!stats.issued&&!wp_rng_begin&&!wp_rng_sample&&!wp_rng_finish&&!world_find_extra_conflict&&pins();
        if(ok&&!wpr_stopped()){
            wpr_composition=composition;wpr_seed=seed;memcpy(wpr_digest,digest,32);memcpy(wpr_manifest,text,length);wpr_manifest[length]=0;wpr_bind();
            wp_rng_begin=wpr_begin;wp_rng_sample=wpr_sample;wp_rng_finish=wpr_finish;world_find_extra_conflict=wpr_conflict;
            wpr_started=wpr_enabled=1;
        }else ok=0;
    }
    ReleaseSRWLockExclusive(&wpr_lock);return ok;
}
static int world_position_rng_configure(const char *text,size_t length){return wpr_configure_using(text,length,wpr_pins);}
