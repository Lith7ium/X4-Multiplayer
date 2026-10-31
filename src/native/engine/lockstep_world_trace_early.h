/* Optional preload manifest and snapshots at existing joined lifecycle sites.
 * Import only retains identity/pointer values; it never reads incomplete Agent
 * or controller objects. Full reads begin after loading at the gate boundary. */
static volatile LONG64 world_trace_early_failures;
static SRWLOCK world_trace_import_lock=SRWLOCK_INIT;
static unsigned world_trace_imports[WORLD_TRACE_WATCH_MAX],world_trace_bound;
static const char *world_trace_early_stage="off";

static int world_trace_manifest(const char *text,size_t length,unsigned last) {
    uint64_t saved[WORLD_TRACE_WATCH_MAX];
    if(!text || !length || (length+1)%17 || (length+1)/17>WORLD_TRACE_WATCH_MAX || last<2 || last>12) return 0;
    unsigned count=(unsigned)((length+1)/17);
    for(unsigned i=0;i<count;i++) {
        uint64_t value=0;
        for(unsigned j=0;j<16;j++) {
            char c=text[i*17+j];unsigned digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:16;
            if(digit>15) return 0;value=(value<<4)|digit;
        }
        if(!value || (i+1<count && text[i*17+16]!=',')) return 0;
        unsigned position=i;while(position && saved[position-1]>value) {saved[position]=saved[position-1];--position;}
        if(position && saved[position-1]==value) return 0;
        if(position<i && saved[position]==value) return 0;
        saved[position]=value;
    }
    if(world_trace_early) {
        if(count!=world_trace_watch_count || last!=world_trace_last_tick) return 0;
        for(unsigned i=0;i<count;i++) if(saved[i]!=world_trace_watches[i].saved) return 0;
        return 1; /* Exact UI-reload retry; never reinitialize trace storage. */
    }
    if(!stats.active || !boot_mode || boot_phase || stats.issued || world_trace_events ||
       !world_identity_installed || !path_installed || !(path_options&1) || GetCurrentThreadId()!=ui_thread ||
       !world_trace_layout_pins() || !ai_installed || !ai_full_layout_verified) return 0;
    for(unsigned i=0;i<count;i++) world_trace_watches[i]=(world_trace_watch){.saved=saved[i]};
    world_trace_watch_count=count;world_trace_last_tick=last;world_trace_early_stage="configured";
    InterlockedExchange(&world_trace_early,1);return 1;
}
static void world_trace_import_capture(void *object,uint64_t runtime,uint64_t saved) {
    if(!world_trace_early) return;
    for(unsigned i=0;i<world_trace_watch_count;i++) if(world_trace_watches[i].saved==saved) {
        AcquireSRWLockExclusive(&world_trace_import_lock);
        if(world_trace_imports[i] || world_trace_bound || !object || !runtime || boot_phase>=3)
            InterlockedIncrement64(&world_trace_early_failures);
        else {world_trace_watches[i].object=object;world_trace_watches[i].runtime=runtime;world_trace_imports[i]=1;}
        ReleaseSRWLockExclusive(&world_trace_import_lock);return;
    }
}
static int world_trace_boundary(unsigned kind) {
    if(!world_trace_early || !world_trace_bound || stats.issued>world_trace_last_tick) return 1;
    for(unsigned i=0;i<world_trace_watch_count;i++) {
        world_trace_event *event=world_trace_begin(kind,world_trace_watches[i].object,NULL,0,0,0,0);
        if(!event) return 0;
        world_trace_end(event);if(event->errors) return 0;
    }
    return 1;
}
static int world_trace_bind(void) {
    if(!world_trace_early || world_trace_bound) return 1;
    world_trace_early_stage="bind_guard";
    if(stats.issued || boot_phase!=3 || GetCurrentThreadId()!=main_thread || world_trace_early_failures) return 0;
    for(unsigned i=0;i<world_trace_watch_count;i++) {
        world_trace_watch *w=&world_trace_watches[i];unsigned char *slots;
        world_trace_early_stage="import_identity";
        if(world_trace_imports[i]!=1 || !w->object || !w->runtime || world_identity_lookup(w->runtime)!=w->saved) return 0;
        for(unsigned j=0;j<ACTOR_CAPACITY;j++) if(w->object==actor_components[j]) return 0;
        world_trace_early_stage="navigation_lifetime";
        if(!read_local(w->object+0x200,&w->controller,8) || !read_local(w->controller+0x30,&slots,8) ||
           !slots || (uintptr_t)slots<0xad0) return 0;
        w->agent=slots-0xad0;if(!world_trace_identity(w)) return 0;
    }
    world_trace_early_stage="allocation";
    if(!world_trace_prepare(world_trace_last_tick)) return 0;
    world_trace_bound=world_trace_watch_count;world_trace_early_stage="loaded_snapshot";
    if(!world_trace_boundary(WT_LOADED)) return 0;
    world_trace_early_stage="complete";return 1;
}
