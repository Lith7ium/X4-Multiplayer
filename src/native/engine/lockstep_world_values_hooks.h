/* Both entry prefixes are a five-byte home-slot store. Resident leaf
 * trampolines are emitted before the shared page's single RX publication. */
static const world_action_pin world_values_pin_list[]={
 {0xced70,184,"3209f7be0616de6d0ecc20ce3cfd82fd16d0524c09635d86995210598b0a5f36"},
 {0x10631b0,472,"b5468455eb7534da2330706d124b3e70c1aedf137869208687e6686adfb2917d"},
 {0x1063750,3437,"0d1c301108dac632a0163a03f309310e188c2df418a4222694fcec3ca646c056"},
 {WORLD_VALUES_ASSIGN,1403,"0863f3019f3027b33c2cac7c05d10a8708bd0db6e04ce1e24cb4fbf3bc327391"},
 {WORLD_VALUES_CANDIDATE,580,"31147ea3aa368fe67fca23ff4d35005584f63dcc520adc2cebba62e93b72ee84"},
 {0xb87a30,275,"b0e683b728526d447edd027a564797983e766ca0ebba80981f857fa45f937a69"},
 {0xcd3d60,201,"d7c57024ee3f22f900dfc41bf8616fad7438e99b4bcc2aa3147515a58f432273"},
 {0xd34980,460,"848c9eda3883474ef766b71d69f37daa61e9dacf0c2b8bb85df0a6cd70903c1f"},
 {0xcccaa0,764,"2cad3f743d517fcf15feb516ede74ad1e5b37dafb3e57a322a2d04ddff6a27e1"},
 {0xd16b10,167,"08e545be47b1bbae4d80047f8cbf2c58e4cb3b1e0bf03fa522fd7542123917b2"},
 {0x14dde20,97,"b472ced0db2e62425089baf6b0ef72b29b258f3a8a34fdabcf98b389171121b7"},
 {0x79f020,158,"28c138f6005560a947fdddb94fdf61dfe2424eeed7a4046e74117b3298a4380c"},
 {0xc5f680,1393,"a8c53d2bfa05d4c37acd064148a31143ae1761065cbd30f929d09f17a6bcf6d9"}
};
static int world_values_pins(void) {
    if(!world_action_pins()) return 0;
    for(unsigned i=0;i<sizeof(world_values_pin_list)/sizeof(*world_values_pin_list);i++) {
        const world_action_pin *p=&world_values_pin_list[i];if(!hash_ok(image+p->rva,p->size,p->sha)) return 0;
    }
    uintptr_t target=0;return read_local(image+0x2c0f3c8+0x18,&target,8)&&target==(uintptr_t)(image+0xb87a30);
}
static unsigned world_values_entry(unsigned index) {return index?WORLD_VALUES_CANDIDATE:WORLD_VALUES_ASSIGN;}
typedef BOOL (WINAPI *world_values_protect_fn)(LPVOID,SIZE_T,DWORD,PDWORD);
typedef BOOL (WINAPI *world_values_flush_fn)(HANDLE,LPCVOID,SIZE_T);
static unsigned world_values_pending(unsigned flush) {
    unsigned mask=0;for(unsigned i=0;i<2;i++) if(flush?world_values_patch_states[i].flush_pending:world_values_patch_states[i].protection_pending) mask|=1u<<i;return mask;
}
static void world_values_patch_failed(unsigned reason) {InterlockedOr(&world_values_patch_errors,(LONG)reason);InterlockedIncrement64(&world_values_patch_failures);InterlockedIncrement64(&world_values_failures);}
static int world_values_repair_with(unsigned index,world_values_protect_fn protect,world_values_flush_fn flush) {
    world_values_patch_state *state=&world_values_patch_states[index];unsigned char *entry=image+world_values_entry(index);DWORD ignored;
    if(state->protection_pending) {
        if(protect(entry,8,state->protection,&ignored)) state->protection_pending=0;else world_values_patch_failed(WV_PATCH_REPAIR_PROTECTION);
    }
    if(state->flush_pending) {
        if(flush(GetCurrentProcess(),entry,8)) state->flush_pending=0;else world_values_patch_failed(WV_PATCH_REPAIR_FLUSH);
    }
    return !state->protection_pending&&!state->flush_pending;
}
static int world_values_repair(unsigned index) {return world_values_repair_with(index,VirtualProtect,FlushInstructionCache);}
static world_values_swap_result world_values_swap_with(unsigned index,uint64_t expected,uint64_t value,
                                                        world_values_protect_fn protect,world_values_flush_fn flush) {
    world_values_swap_result result={0};world_values_patch_state *state=&world_values_patch_states[index];
    DWORD old,ignored;unsigned char *entry=image+world_values_entry(index);
    if(!protect(entry,8,PAGE_EXECUTE_READWRITE,&old)) {world_values_patch_failed(WV_PATCH_OPEN);return result;}
    /* Preserve the original permission obligation across rollback/retries. A
     * failed prior restoration must not turn temporary RWX into our new target. */
    if(!state->protection_pending) state->protection=old;state->protection_pending=1;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)entry,(LONG64)value,(LONG64)expected);
    result.changed=actual==expected;if(!result.changed) world_values_patch_failed(WV_PATCH_FOREIGN);state->flush_pending=1;
    if(protect(entry,8,state->protection,&ignored)) state->protection_pending=0;else world_values_patch_failed(WV_PATCH_PROTECTION);
    if(flush(GetCurrentProcess(),entry,8)) state->flush_pending=0;else world_values_patch_failed(WV_PATCH_FLUSH);
    result.ok=result.changed&&!state->protection_pending&&!state->flush_pending;return result;
}
static world_values_swap_result world_values_swap(unsigned index,uint64_t expected,uint64_t value) {
    return world_values_swap_with(index,expected,value,VirtualProtect,FlushInstructionCache);
}
static int world_values_patch(world_values_swap_result (*swap)(unsigned,uint64_t,uint64_t)) {
    for(unsigned i=0;i<2;i++) {
        world_values_swap_result result=swap(i,world_values_original[i],world_values_patched[i]);
        if(result.changed) world_values_sites|=1u<<i;
        if(!result.ok) {
            /* The current site also belongs to us when CAS succeeded but the
             * permission or cache publication operation subsequently failed. */
            for(unsigned j=0;j<=i;j++) if(world_values_sites&(1u<<j)) {
                result=swap(j,world_values_patched[j],world_values_original[j]);
                if(result.changed) world_values_sites&=~(1u<<j);
            }
            return 0;
        }
    }
    return 1;
}
static int world_values_prepare(void) {
    const unsigned char prefix[5]={0x48,0x89,0x5c,0x24,0x10};
    for(unsigned i=0;i<2;i++) {
        unsigned entry=world_values_entry(i),offset=3584+i*64;
        intptr_t distance=(intptr_t)(thunk+offset+32)-(intptr_t)(image+entry+5);
        if(distance<INT32_MIN||distance>INT32_MAX||!thunk_slot_ready(offset)||
           !read_local(image+entry,&world_values_original[i],8)||memcmp(&world_values_original[i],prefix,5)) return 0;
        world_values_patched[i]=world_values_original[i];unsigned char *patch=(unsigned char*)&world_values_patched[i];
        patch[0]=0xe9;int32_t relative=(int32_t)distance;memcpy(patch+1,&relative,4);
    }
    return 1;
}
static void world_values_restore_using(world_values_swap_result (*swap)(unsigned,uint64_t,uint64_t),int (*repair)(unsigned)) {
    AcquireSRWLockExclusive(&world_values_lock);world_values_closing=1;world_values_enabled=0;
    if(world_values_open>world_values_restore_open) world_values_restore_open=world_values_open;
    for(unsigned i=0;i<2;i++) {
        if(world_values_sites&(1u<<i)) {world_values_swap_result result=swap(i,world_values_patched[i],world_values_original[i]);if(result.changed) world_values_sites&=~(1u<<i);}
        if(world_values_patch_states[i].protection_pending||world_values_patch_states[i].flush_pending) repair(i);
    }
    ReleaseSRWLockExclusive(&world_values_lock);
}
static void world_values_restore(void) {world_values_restore_using(world_values_swap,world_values_repair);}
/* One immutable binding. Reattachment acknowledges existing state only; it
 * never clears captures, counters, STOP, failures or pending patch cleanup. */
static struct {
    int valid;uintptr_t image,thunk;uint64_t options,started;unsigned slot,port;
    char token[33],digest[65];
} world_values_binding;
static void world_values_bind(void) {
    world_values_binding.image=(uintptr_t)image;world_values_binding.thunk=(uintptr_t)thunk;
    world_values_binding.options=boot_options;world_values_binding.started=boot_started;
    world_values_binding.slot=stats.slot;world_values_binding.port=remote_port;
    memcpy(world_values_binding.token,secret,sizeof(secret));memcpy(world_values_binding.digest,initial_state_digest,sizeof(initial_state_digest));
    world_values_binding.valid=1;
}
static int world_values_bound(void) {
    return world_values_binding.valid&&boot_mode&&world_values_binding.image==(uintptr_t)image&&world_values_binding.thunk==(uintptr_t)thunk&&
        world_values_binding.options==boot_options&&world_values_binding.started==boot_started&&world_values_binding.slot==stats.slot&&world_values_binding.port==remote_port&&
        !memcmp(world_values_binding.token,secret,sizeof(secret))&&!memcmp(world_values_binding.digest,initial_state_digest,sizeof(initial_state_digest));
}
static int world_values_configure_using(int (*pins)(void),int (*prepare)(void),world_values_swap_result (*swap)(unsigned,uint64_t,uint64_t)) {
    if(GetCurrentThreadId()!=ui_thread) return 0;
    AcquireSRWLockExclusive(&world_values_lock);
    int ok=stats.active&&!world_values_closing;
    if(world_values_started) {
        ok=ok&&world_values_bound()&&world_values_enabled&&world_values_sites==3&&!world_values_failures&&!world_values_dropped&&
            world_action_started&&world_action_enabled&&!world_action_closing&&!world_action_failures&&!world_action_dropped&&
            world_action_unknown_callback==world_values_unknown_capture&&!world_values_patch_failures&&!world_values_pending(0)&&!world_values_pending(1);
        for(unsigned i=0;ok&&i<2;i++) {uint64_t actual;ok=read_local(image+world_values_entry(i),&actual,8)&&actual==world_values_patched[i]&&thunk_slot_ready(3584+i*64);}
    } else {
        ok=ok&&boot_mode&&!boot_phase&&!stats.issued&&world_action_started&&world_action_enabled&&
            !world_action_closing&&pins()&&prepare();
        if(ok) {
            world_values_started=1;ok=world_values_patch(swap);
            if(ok) {world_values_bind();world_values_enabled=1;world_action_unknown_callback=world_values_unknown_capture;}else world_values_closing=1;
        }
    }
    ReleaseSRWLockExclusive(&world_values_lock);return ok;
}
static int world_values_configure(void) {return world_values_configure_using(world_values_pins,world_values_prepare,world_values_swap);}
