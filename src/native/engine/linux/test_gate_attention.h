/* The attention timer twin (gate_attention.h): the hook from the start options, 'all' on saved objects, the
 * restored word, the restoration. The timer is a stand-in behind the pinned prologue that does what the
 * executable's does: due at +0x2d8, the thread's time and word. Included by test_gate_linux.c. */
static unsigned ta_evaluations;
static void ta_timer(void *self) {
    unsigned char *object=self,*base=gate_tls_self();double now,due;
    memcpy(&now,gate_tls_time(base),8);memcpy(&due,object+LX_ATTENTION_DUE,8);
    if(now<due) return;
    uint64_t *word=gate_tls_rng(base);
    *word=*word*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;*word=(*word>>30)|(*word<<34);
    due=now+0.75+(double)(uint32_t)*word*(0.5/4294967296.0);memcpy(object+LX_ATTENTION_DUE,&due,8);++ta_evaluations;
}
static void ta_write_timer(void) {
    /* push rbp; push rbx; mov rbx,rdi; sub rsp,0x18 | add rsp,0x18; mov rdi,rbx; pop rbx; pop rbp; jmp [rip] -> ta_timer */
    unsigned char code[37]={0x55,0x53,0x48,0x89,0xfb,0x48,0x83,0xec,0x18,0x48,0x83,0xc4,0x18,0x48,0x89,0xdf,0x5b,0x5d,0xff,0x25,0,0,0,0};
    void *target=(void*)ta_timer;memcpy(code+24,&target,8);
    tl_write(LX(LX_ATTENTION_VA),code,32);
    assert(rd64(LX(LX_ATTENTION_VA))==LX_ATTENTION_ENTRY_ORIGINAL);
}
/* Two objects with saved IDs whose dues come from "loading" (different per run), one without; four ticks. */
static void ta_run(double loading_due,uint64_t natural,double dues[3]) {
    static __attribute__((aligned(16))) unsigned char objects[3][0x300];
    const uint64_t runtime[3]={8101,8102,8999};
    for(unsigned i=0;i<3;i++) {memset(objects[i],0,sizeof(objects[i]));memcpy(objects[i]+8,&runtime[i],8);memcpy(objects[i]+LX_ATTENTION_DUE,&loading_due,8);}
    unsigned char *base=gate_tls_self();memcpy(gate_tls_rng(base),&natural,8);
    void (*timer)(void *)=(void (*)(void *))(void*)LX(LX_ATTENTION_VA);
    for(uint64_t tick=1;tick<=4;tick++) {
        stats.issued=tick;double now=20.0+tick/60.0;memcpy(gate_tls_time(base),&now,8);
        for(unsigned i=0;i<3;i++) timer(objects[i]);
    }
    for(unsigned i=0;i<3;i++) memcpy(&dues[i],objects[i]+LX_ATTENTION_DUE,8);
}
static void test_attention_linux(void) {
    ta_write_timer();
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    thunk=make_thunk();assert(thunk);
    assert(!attention_all_arm()); /* no hook yet */
    world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));assert(world_identities);world_identity_installed=1;
    world_identity_count=0;world_identity_sealed=0;assert(world_identity_record(8101,0x201) && world_identity_record(8102,0x202));
    assert(attention_install() && attention_installed && attention_owned() && attention_install());
    unsigned char *entry=LX(LX_ATTENTION_VA);int32_t rel;memcpy(&rel,entry+1,4);
    assert(entry[0]==0xe9 && entry+5+rel==thunk+LXA_HOOK && !memcmp(thunk+LXA_THUNK,"\x55\x53\x48\x89\xfb\xff\x25",7));
    void *target;memcpy(&target,thunk+LXA_HOOK+6,8);assert(target==(void*)lxa_refresh);
    /* Without 'all' every call is native: the dues follow the run's loading values and word. */
    boot_phase=3;double a[3],b[3];
    ta_evaluations=0;ta_run(19.5,0x1111,a);ta_run(20.03,0x2222,b);assert(ta_evaluations>=4 && memcmp(a,b,sizeof(a)));
    /* With 'all': saved objects evaluate at their first admitted call and draw from (tick, key); the word comes back. */
    boot_phase=0;stats.issued=0;assert(attention_all_arm() && attention_all_armed);boot_phase=3;
    uint64_t before=0x3333;ta_run(19.5,before,a);
    assert(attention_all_inits==2 && attention_all_calls==8 && attention_all_unmapped==4 && attention_all_unmapped_ids==1 && attention_all_first==1);
    memset(attention_all_table,0,sizeof(attention_all_table));attention_all_count=0;
    ta_run(20.03,0x4444,b);
    assert(a[0]==b[0] && a[1]==b[1] && a[0]!=a[1] && a[2]!=b[2] && !attention_failures);
    /* Outside admitted frames, natively. */
    boot_phase=2;unsigned before_calls=(unsigned)attention_all_calls;ta_run(19.5,0x5555,a);assert(attention_all_calls==before_calls);boot_phase=3;
    /* Restored: the entry's 8 bytes as they were. */
    attention_restore();assert(!attention_installed && attention_restored && rd64(entry)==LX_ATTENTION_ENTRY_ORIGINAL);
    attention_all_armed=0;world_identity_installed=0;world_identity_count=0; /* the world hook test installs its own */
    printf("attention all PASS\n");
}
