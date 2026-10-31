/* Arming, hook placement and Lua status for lockstep_script_rng.h, DEFAULT OFF.
 * No new thunk slot. Scopes ride on hooks the gate already owns:
 *   ExecState_Run 0xb58bb0 (npc_exec_entry, thunk 1088; installed here when no
 *     NPC option did), the run range 0xb58ce0 (interrupt_observe) and the
 *     AIScriptContext event slot 0x2c6b418 (interrupt_event_observe), both from
 *     native_gate_interrupt_diag;
 * plus two pinned vtable data-pointer swaps that need no thunk:
 *   MD::Cue slot 565 0x2c76318 -> 0x111f990, a tail jump into
 *     MD::Cue_CheckConditions 0x111c290 (event-driven cue conditions), and
 *   AICallStackContext slot 565 0x2c6a260 -> 0x1062f70 (global interrupt
 *     handler conditions of a context's call stack).
 * Cue_CheckConditions is also called directly by MDScheduler_Update 0x1121f80
 * (timed checks), Cue_EnterWaiting 0x111d090 and Cue_RaiseCueEvent 0x111e370
 * (signal/activation listeners). Without native_gate_md_timed_rng
 * (lockstep_script_rng_md.h) those draws stay on the natural stream outside a
 * keyed scope and continue the enclosing stream inside one. */
/* The Linux gate (linux/gate_script_rng.h) has its own slots, pins and arming, and the entry hooks. */
#ifndef SCRIPT_RNG_OWN_PLATFORM
#define SCRIPT_RNG_CUE_SLOT 0x2c76318u
#define SCRIPT_RNG_CUE_HANDLER 0x111f990u
#define SCRIPT_RNG_STACK_SLOT 0x2c6a260u
#define SCRIPT_RNG_STACK_HANDLER 0x1062f70u
#endif
typedef uintptr_t (*script_rng_event_fn)(void *,void *,void *,void *);
/* The handlers take only the handler object; extra argument registers are
 * passed through untouched and the result is returned unchanged. */
static uintptr_t script_rng_event_run(unsigned source,script_rng_event_fn original,uint64_t *rng,void *self,void *a,void *b,void *c) {
    script_rng_frame scope;int scoped=script_rng_enter(&scope,source,self,NULL,0,0,0,rng);uintptr_t result=0;
    __try {result=original(self,a,b,c);}
    __finally {script_rng_leave(&scope,scoped,rng,AbnormalTermination());}
    return result;
}
/* noinline keeps the TEB read out of the SEH funclet of the scope. */
static __attribute__((noinline)) uintptr_t script_rng_cue_event(void *self,void *a,void *b,void *c) {
    return script_rng_event_run(SR_CUE,(script_rng_event_fn)(image+SCRIPT_RNG_CUE_HANDLER),interrupt_rng(),self,a,b,c);
}
static __attribute__((noinline)) uintptr_t script_rng_stack_event(void *self,void *a,void *b,void *c) {
    return script_rng_event_run(SR_STACK,(script_rng_event_fn)(image+SCRIPT_RNG_STACK_HANDLER),interrupt_rng(),self,a,b,c);
}
#ifndef SCRIPT_RNG_OWN_PLATFORM
static int script_rng_slot_swap(unsigned rva,void *expected,void *value) {
    DWORD old,ignored;if(!VirtualProtect(image+rva,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((PVOID volatile*)(image+rva),value,expected);
    VirtualProtect(image+rva,8,old,&ignored);return actual==expected;
}
#endif
static int script_rng_owned(void) {
    return !((script_rng_cue_installed && *(void**)(image+SCRIPT_RNG_CUE_SLOT)!=(void*)script_rng_cue_event) ||
             (script_rng_stack_installed && *(void**)(image+SCRIPT_RNG_STACK_SLOT)!=(void*)script_rng_stack_event));
}
#ifndef SCRIPT_RNG_OWN_PLATFORM
/* Layout pins: cue name tree, Cue_CreateInstance (MD::Cue vtable, +0x160),
 * AIScriptContext ctor (vtable, +0xd8 script, +0x120 entity), the call-stack
 * context setup 0x1066ba0 (vtable, +0xc8 owner) and both swapped handlers. */
static int script_rng_pinned(unsigned mask) {
    return npc_layout_pinned() &&
        hash_ok(image+0x1111a40,905,"bde488a4463be31fa0a9d096c61c1b5922d2b1e3191a939bf11302905e688ee4") &&
        hash_ok(image+0x10631b0,472,"b5468455eb7534da2330706d124b3e70c1aedf137869208687e6686adfb2917d") &&
        hash_ok(image+0x1066ba0,613,"ab00b7598d2b6cce00d9d0135844f47bd2c9a94d9a9fcc1cf9508a994a8d9434") &&
        (!(mask&SCRIPT_RNG_MD) || (hash_ok(image+SCRIPT_RNG_CUE_HANDLER,5,"997fb200eb855b39f1b0e8c3bbce1ef473dccfddc2972fb1083ac3e37e82a7b8") &&
            hash_ok(image+0x111c290,712,"5f9346c838fde113044bdca972ea8c18f13dadf33fb48dde50e69664ac5688d1") &&
            *(void**)(image+SCRIPT_RNG_CUE_SLOT)==(void*)(image+SCRIPT_RNG_CUE_HANDLER))) &&
        (!(mask&SCRIPT_RNG_AI) || (hash_ok(image+SCRIPT_RNG_STACK_HANDLER,566,"964abbd0a451bd506703052d97535b5b097a959c6fae5fbb2559e1430862cbac") &&
            *(void**)(image+SCRIPT_RNG_STACK_SLOT)==(void*)(image+SCRIPT_RNG_STACK_HANDLER)));
}
#endif
static int script_rng_unswap(void) {
    int a=!script_rng_cue_installed || script_rng_slot_swap(SCRIPT_RNG_CUE_SLOT,(void*)script_rng_cue_event,image+SCRIPT_RNG_CUE_HANDLER);
    if(a) script_rng_cue_installed=0;
    int b=!script_rng_stack_installed || script_rng_slot_swap(SCRIPT_RNG_STACK_SLOT,(void*)script_rng_stack_event,image+SCRIPT_RNG_STACK_HANDLER);
    if(b) script_rng_stack_installed=0;
    return a && b;
}
#ifndef SCRIPT_RNG_OWN_PLATFORM
/* Armed once at boot, before load, like the job RNG hook; idempotent on a
 * loading-UI reload that reruns the probe configuration with the same mode.
 * Requires the interrupt/nested hooks and the world identity map; key mode
 * refuses the other script RNG writers (ai/interrupt/npc RNG), the boot-phase
 * bit refuses the boot RNG anchor (an anchor written inside a keyed scope
 * would be undone by its restore). */
static int script_rng_arm(int mode,unsigned mask,int rows,unsigned options) {
    mode=mode==SCRIPT_RNG_KEY?SCRIPT_RNG_KEY:SCRIPT_RNG_OBSERVE;rows=rows!=0;
    if(script_rng_armed) return stats.active && script_rng_mode==mode && script_rng_mask==mask && script_rng_rows==rows &&
        script_rng_options==options;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread ||
       !mask || mask>7 || !(mask&(SCRIPT_RNG_AI|SCRIPT_RNG_MD)) || options>SCRIPT_RNG_OPTIONS ||
       ((options&SCRIPT_RNG_OPT_NATURAL) && !rows) || ((options&SCRIPT_RNG_OPT_UNMAPPED) && !(mask&SCRIPT_RNG_AI))) return 0;
    if(!interrupt_installed || !interrupt_event_installed || !nested_action_installed || !world_identity_installed) return 0;
    if(mode==SCRIPT_RNG_KEY && (ai_seeded || interrupt_seeded || (boot_options&140737488355328ULL))) return 0;
    if((mask&SCRIPT_RNG_BOOT) && boot_rng_anchor_armed) return 0;
    if(!script_rng_pinned(mask)) return 0;
    int exec_owned=0;
    if(!npc_exec_installed) {if(!npc_exec_install()) return 0;exec_owned=1;}
    if((mask&SCRIPT_RNG_MD) && !(script_rng_cue_installed=script_rng_slot_swap(SCRIPT_RNG_CUE_SLOT,image+SCRIPT_RNG_CUE_HANDLER,(void*)script_rng_cue_event))) goto refuse;
    if((mask&SCRIPT_RNG_AI) && !(script_rng_stack_installed=script_rng_slot_swap(SCRIPT_RNG_STACK_SLOT,image+SCRIPT_RNG_STACK_HANDLER,(void*)script_rng_stack_event))) goto refuse;
    script_rng_mode=mode;script_rng_mask=mask;script_rng_rows=rows;script_rng_options=options;script_rng_exec_owned=exec_owned;script_rng_armed=1;
    script_rng_cache_flush();script_rng_clock_start();InterlockedExchange(&script_rng_active,1);return 1;
refuse:
    script_rng_unswap();if(exec_owned) npc_exec_restore();return 0;
}
#endif
static int script_rng_phase2_armed(void) {return script_rng_armed && (script_rng_mask&SCRIPT_RNG_BOOT);}
/* Open scopes still restore their own words; the ExecState_Run entry is
 * restored with the interrupt hooks (npc_exec_restore). */
static void script_rng_restore(void) {
    if(!script_rng_armed) return;
    InterlockedExchange(&script_rng_active,0);script_rng_restored=script_rng_unswap();
}
static int script_rng_status(char *out,size_t cap) {
    LONG64 cursor=InterlockedCompareExchange64(&script_rng_cursor,0,0);
    LONG64 dropped=cursor>(LONG64)SCRIPT_RNG_RING?cursor-(LONG64)SCRIPT_RNG_RING:0;
    const script_rng_tally *t=&script_rng_now,*l=&script_rng_last;
    int n=snprintf(out,cap,"installed=%d active=%ld mode=%d mask=%u restored=%d exec_hook=%d exec_owned=%d cue_slot=%d stack_slot=%d"
        " entries=%lld exec=%lld range=%lld event=%lld stack=%lld cue=%lld timed=%lld wait=%lld raise=%lld outer=%lld child=%lld same=%lld nested_unkeyed=%lld unkeyed=%lld"
        " unreadable=%lld unmapped=%lld unnamed=%lld foreign=%lld masked=%lld table_full=%lld threads=%lld depth_max=%u names=%u"
        " restores=%lld failures=%lld records=%lld dropped=%lld ring=%u draws=%llu draws_unknown=%lld"
        " cur_tick=%llu cur_entries=%u cur_outer=%u cur_child=%u cur_unkeyed=%u cur_draws=%llu cur_unknown=%u cur_keyset=%016llx cur_drawset=%016llx"
        " last_tick=%llu last_entries=%u last_outer=%u last_child=%u last_unkeyed=%u last_draws=%llu last_unknown=%u last_keyset=%016llx last_drawset=%016llx"
        " rows=%d self_us=%llu cur_self_us=%llu last_self_us=%llu ai_hits=%lld ai_misses=%lld identity_sealed=%ld created=%lld"
        " options=%u unmapped_keyed=%lld unkeyed_rows=%lld",
        script_rng_armed,script_rng_active,script_rng_mode,script_rng_mask,script_rng_restored,npc_exec_installed,script_rng_exec_owned,
        script_rng_cue_installed,script_rng_stack_installed,
        (long long)(script_rng_sources[SR_EXEC]+script_rng_sources[SR_RANGE]+script_rng_sources[SR_EVENT]+script_rng_sources[SR_STACK]+
            script_rng_sources[SR_CUE]+script_rng_sources[SR_TIMED]+script_rng_sources[SR_WAIT]+script_rng_sources[SR_RAISE]),
        (long long)script_rng_sources[SR_EXEC],(long long)script_rng_sources[SR_RANGE],(long long)script_rng_sources[SR_EVENT],
        (long long)script_rng_sources[SR_STACK],(long long)script_rng_sources[SR_CUE],(long long)script_rng_sources[SR_TIMED],
        (long long)script_rng_sources[SR_WAIT],(long long)script_rng_sources[SR_RAISE],(long long)script_rng_outer,(long long)script_rng_child,
        (long long)script_rng_same,(long long)script_rng_nested_unkeyed,(long long)script_rng_unkeyed,
        (long long)script_rng_why[SR_UNREADABLE],(long long)script_rng_why[SR_UNMAPPED],(long long)script_rng_why[SR_UNNAMED],
        (long long)script_rng_why[SR_FOREIGN],(long long)script_rng_why[SR_MASKED],(long long)script_rng_table_full,
        (long long)script_rng_threads,script_rng_depth_max,script_rng_name_count,(long long)script_rng_restores,(long long)script_rng_failures,
        (long long)cursor,(long long)dropped,SCRIPT_RNG_RING,(unsigned long long)script_rng_draws,(long long)script_rng_draws_unknown,
        (unsigned long long)t->tick,t->entries,t->outer,t->child,t->unkeyed,(unsigned long long)t->draws,t->unknown,
        (unsigned long long)t->keyset,(unsigned long long)t->drawset,
        (unsigned long long)l->tick,l->entries,l->outer,l->child,l->unkeyed,(unsigned long long)l->draws,l->unknown,
        (unsigned long long)l->keyset,(unsigned long long)l->drawset,
        script_rng_rows,(unsigned long long)script_rng_us(script_rng_self),(unsigned long long)script_rng_us(t->self),
        (unsigned long long)script_rng_us(l->self),(long long)script_rng_ai_hits,(long long)script_rng_ai_misses,world_identity_sealed,
        (long long)script_rng_created,script_rng_options,(long long)script_rng_unmapped_keyed,(long long)script_rng_unkeyed_rows);
    return n>0 && (size_t)n<cap?n:0;
}
