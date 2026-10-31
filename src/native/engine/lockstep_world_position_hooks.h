/* Four owned patches, with protection/cache debt independent of byte ownership. */
typedef struct {unsigned rva,size,offset,displacement;unsigned __int128 original,patched;DWORD protection;unsigned owned,protection_pending,flush_pending;} wp_patch;
static wp_patch wp_patches[4]={{WP_OUTER,8,3776,0,0,0,0,0,0,0},{0xc6fb40,16,3840,9,0,0,0,0,0,0},
    {0xc6fb60,16,3872,11,0,0,0,0,0,0},{WP_SAMPLER,8,3904,0,0,0,0,0,0,0}};
static LONG64 wp_patch_failures;static LONG wp_patch_errors;
enum {WP_PATCH_OPEN=1,WP_PATCH_PROTECT=2,WP_PATCH_FLUSH=4,WP_PATCH_FOREIGN=8};
static const world_action_pin wp_pins[]={
 {0xbe0ba0,280,"9d94310ae24fd08d72280562248f875c499ede1630badd8987e399ba237170d3"},
 {WP_OUTER,1181,"a0fec7baeee30e27c4cd03b5eff1a8524190868e881bd2264707d7b136bfa782"},
 {0xc59b00,216,"1df694729b48d4501df45415174d7814f28fb10441aff6361a6992b1bbfd60f0"},
 {0xc58da0,3224,"6497e4f7003ee60b44682b9ecf4fc83e4bad1370f4ccfd4679d2b1e026480ae0"},
 {0x3db8a0,1125,"6e28847e1ab6eb2e206ec564645b32dee94a0fdd2eefe0884bdedad46da1ad90"},
 {WP_SAMPLER,257,"1db1ba392a17c5d2bd2cbca21a6d2f7234bbf6e6c726a0ef0d72594c19acf921"},
 {0x14ddc90,120,"584854a7bdbf956043c7756157888674152bd0ffc0a307c00f29537332e8780e"},
 {0xd1c8a0,1010,"a7007dae4b099dbc10116e7917a4fe6e80994d2f212d62f1b244f2f23a263760"},
 {0xcc8550,456,"bb44d01e1bf3e3e8102efeb736bb002ec5431087ee6d2afe56a87fac9f7f19d5"},
 {0xc6fc50,626,"958743bfaefcb89b3b408914fc99556c9e5f999f9cffb7cc0b92d5bb5ca01fe7"},
 {0x325610,144,"27bfc28b6bfb7419830ce1e09ef918b06e5eeb2f163122b8168fb920df229819"}
};
static unsigned wp_patch_mask(unsigned kind) {
    unsigned mask=0;for(unsigned i=0;i<4;i++)if(kind==0?wp_patches[i].owned:kind==1?wp_patches[i].protection_pending:wp_patches[i].flush_pending)mask|=1u<<i;return mask;
}
static void wp_patch_fault(unsigned why) {InterlockedOr(&wp_patch_errors,(LONG)why);InterlockedIncrement64(&wp_patch_failures);InterlockedIncrement64(&wp_failures);}
static int wp_repair_with(unsigned i,world_values_protect_fn protect,world_values_flush_fn flush) {
    wp_patch *p=&wp_patches[i];DWORD ignored;unsigned char *address=image+p->rva;
    if(p->protection_pending) {if(protect(address,p->size,p->protection,&ignored))p->protection_pending=0;else wp_patch_fault(WP_PATCH_PROTECT);}
    if(p->flush_pending) {if(flush(GetCurrentProcess(),address,p->size))p->flush_pending=0;else wp_patch_fault(WP_PATCH_FLUSH);}
    return !p->protection_pending&&!p->flush_pending;
}
static world_values_swap_result wp_swap_with(unsigned i,unsigned __int128 expected,unsigned __int128 desired,
                                            world_values_protect_fn protect,world_values_flush_fn flush) {
    wp_patch *p=&wp_patches[i];world_values_swap_result result={0};DWORD old,ignored;unsigned char *address=image+p->rva;
    if(!protect(address,p->size,PAGE_EXECUTE_READWRITE,&old)){wp_patch_fault(WP_PATCH_OPEN);return result;}
    if(!p->protection_pending)p->protection=old;p->protection_pending=1;
    if(p->size==8)result.changed=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)address,(LONG64)desired,(LONG64)expected)==(uint64_t)expected;
    else {unsigned __int128 compare=expected;result.changed=__atomic_compare_exchange_n((unsigned __int128*)address,&compare,desired,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);}
    if(!result.changed)wp_patch_fault(WP_PATCH_FOREIGN);p->flush_pending=1;
    if(protect(address,p->size,p->protection,&ignored))p->protection_pending=0;else wp_patch_fault(WP_PATCH_PROTECT);
    if(flush(GetCurrentProcess(),address,p->size))p->flush_pending=0;else wp_patch_fault(WP_PATCH_FLUSH);
    result.ok=result.changed&&!p->protection_pending&&!p->flush_pending;return result;
}
static world_values_swap_result wp_swap(unsigned i,unsigned __int128 expected,unsigned __int128 desired) {return wp_swap_with(i,expected,desired,VirtualProtect,FlushInstructionCache);}
static int wp_repair(unsigned i) {return wp_repair_with(i,VirtualProtect,FlushInstructionCache);}
static void wp_restore_locked(world_values_swap_result(*swap)(unsigned,unsigned __int128,unsigned __int128),int(*repair)(unsigned)) {
    wp_enabled=0;wp_closing=1;if(wp_open>wp_restore_open)wp_restore_open=wp_open;
    for(unsigned i=0;i<4;i++) {wp_patch *p=&wp_patches[i];if(p->owned){world_values_swap_result r=swap(i,p->patched,p->original);if(r.changed)p->owned=0;}
        if(p->protection_pending||p->flush_pending)repair(i);}
}
static void world_position_restore(void) {AcquireSRWLockExclusive(&wp_lock);wp_restore_locked(wp_swap,wp_repair);ReleaseSRWLockExclusive(&wp_lock);}
static int wp_pins_ok(void) {
    unsigned char bytes[4096];
    for(unsigned i=0;i<sizeof(wp_pins)/sizeof(*wp_pins);i++) {
        const world_action_pin *pin=&wp_pins[i];if(pin->size>sizeof(bytes)||!read_local(image+pin->rva,bytes,pin->size))return 0;
        for(unsigned j=0;j<4;j++) {wp_patch *p=&wp_patches[j];if(p->rva>=pin->rva&&p->rva+p->size<=pin->rva+pin->size&&p->owned) {
            if(memcmp(bytes+p->rva-pin->rva,&p->patched,p->size))return 0;memcpy(bytes+p->rva-pin->rva,&p->original,p->size);}}
        if(!hash_ok(bytes,pin->size,pin->sha))return 0;
    }return 1;
}
static int wp_prepare(void) {
    const unsigned char outer[7]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x10},sampler[5]={0x48,0x89,0x6c,0x24,0x10};
    for(unsigned i=0;i<4;i++) {
        wp_patch *p=&wp_patches[i];unsigned end=p->rva+(i==1?13:i==2?15:5);unsigned target=p->offset+(p->size==8?32:0);
        intptr_t relative=(intptr_t)(thunk+target)-(intptr_t)(image+end);
        if(relative<INT32_MIN||relative>INT32_MAX||!thunk_slot_ready(p->offset)||!read_local(image+p->rva,&p->original,p->size))return 0;
        p->patched=p->original;unsigned char *bytes=(unsigned char*)&p->patched;int32_t rel=(int32_t)relative;
        if(p->size==8) {if(memcmp(bytes,i?sampler:outer,i?5:7))return 0;bytes[0]=0xe9;memcpy(bytes+1,&rel,4);}
        else {int32_t prior;memcpy(&prior,bytes+p->displacement,4);unsigned native=i==1?0x3db8a0:0xc59b00;
            if(bytes[p->displacement-1]!=0xe8||(intptr_t)end+prior!=native)return 0;memcpy(bytes+p->displacement,&rel,4);}
    }return 1;
}
static struct {uintptr_t image,thunk;uint64_t options,started;unsigned slot,port;char token[33],digest[65],manifest[WORLD_ACTION_MANIFEST_CAP];} wp_binding;
static void wp_bind(void) {
    wp_binding.image=(uintptr_t)image;wp_binding.thunk=(uintptr_t)thunk;wp_binding.options=boot_options;wp_binding.started=boot_started;
    wp_binding.slot=stats.slot;wp_binding.port=remote_port;memcpy(wp_binding.token,secret,33);memcpy(wp_binding.digest,initial_state_digest,65);
    memcpy(wp_binding.manifest,world_action_manifest,sizeof(wp_binding.manifest));
}
static int wp_dependencies(void) {
    if(!(world_values_started&&world_values_enabled&&!world_values_closing&&!world_values_failures&&!world_values_dropped&&
    !world_values_patch_failures&&!world_values_pending(0)&&!world_values_pending(1)&&world_values_bound()&&
    world_action_started&&world_action_enabled&&!world_action_closing&&!world_action_failures&&!world_action_dropped&&
    world_values_sites==3&&world_action_unknown_callback==world_values_unknown_capture))return 0;
    for(unsigned i=0;i<2;i++){uint64_t word=0;if(!read_local(image+world_values_entry(i),&word,8)||word!=world_values_patched[i]||!thunk_slot_ready(3584+i*64))return 0;}
    return 1;
}
static int wp_bound(void) {return wp_binding.image==(uintptr_t)image&&wp_binding.thunk==(uintptr_t)thunk&&wp_binding.options==boot_options&&
    wp_binding.started==boot_started&&wp_binding.slot==stats.slot&&wp_binding.port==remote_port&&!memcmp(wp_binding.token,secret,33)&&
    !memcmp(wp_binding.digest,initial_state_digest,65)&&!memcmp(wp_binding.manifest,world_action_manifest,sizeof(wp_binding.manifest));}
static int world_position_configure_using(int(*pins)(void),int(*prepare)(void),world_values_swap_result(*swap)(unsigned,unsigned __int128,unsigned __int128),int(*repair)(unsigned)) {
    if(GetCurrentThreadId()!=ui_thread)return 0;AcquireSRWLockExclusive(&wp_lock);
    int ok=stats.active&&boot_mode&&!wp_stopped()&&wp_dependencies();
    if(wp_started) {
        ok=ok&&wp_enabled&&wp_bound()&&!wp_failures&&!wp_dropped&&!wp_patch_failures&&wp_patch_mask(0)==15&&!wp_patch_mask(1)&&!wp_patch_mask(2);
        for(unsigned i=0;ok&&i<4;i++){wp_patch *p=&wp_patches[i];unsigned __int128 actual=0;ok=read_local(image+p->rva,&actual,p->size)&&actual==p->patched&&thunk_slot_ready(p->offset);}
        if(ok)ok=pins(); /* reconstruct exact owned words; no mutation */
    } else {
        ok=ok&&!boot_phase&&!stats.issued&&pins()&&prepare();
        if(ok) {wp_started=1;
            for(unsigned i=0;i<4;i++){if(wp_stopped()){ok=0;break;}wp_patch *p=&wp_patches[i];world_values_swap_result r=swap(i,p->original,p->patched);if(r.changed)p->owned=1;if(!r.ok){ok=0;break;}}
            if(wp_stopped())ok=0;
            if(ok){wp_bind();wp_enabled=1;}else wp_restore_locked(swap,repair);
        }
    }
    ReleaseSRWLockExclusive(&wp_lock);return ok;
}
static int world_position_configure(void) {return world_position_configure_using(wp_pins_ok,wp_prepare,wp_swap,wp_repair);}
