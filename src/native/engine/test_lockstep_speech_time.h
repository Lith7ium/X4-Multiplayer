/* native_gate_speech_game_time: every eligible audio-finished speech line is routed
 * to the native deadline path, registered NPC or not; keys are recorded only;
 * flagged lines keep the native path; inconsistent state is a failure (stop 120);
 * arming refusals and the shared, already-owned deadline patch. No native code. */
static void test_speech_time(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;
    int saved_boot=boot_mode,saved_phase=boot_phase,saved_installed=npc_deadline_installed;
    unsigned char self[0xa0]={0},audio[0x140]={0},before[0xa0];
    *(uint64_t*)(self+8)=0x5123;*(uint32_t*)(self+0x10)=5;
    *(uint32_t*)(self+0x28)=10002;*(uint32_t*)(self+0x2c)=920;
    *(double*)(self+0x18)=1.5;*(double*)(self+0x20)=2;
    *(double*)(audio+0x110)=1.5;*(double*)(image+0x6b60e48)=3;
    if(!world_identities) {
        world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        assert(world_identities);
    }
    LONG saved_sealed=world_identity_sealed;world_identity_sealed=0;
    assert(world_identity_record(0x5123,0x11e13));
    memset(npc_speakers,0,sizeof(npc_speakers));LONG64 npc_seen=npc_deadline_seen;uint64_t npc_foreign=npc_deadline_foreign;
    stats.active=1;boot_phase=3;ui_thread=GetCurrentThreadId();stats.issued=185;
    /* Off: the NPC experiment's rule applies and an unregistered speaker stays native. */
    speech_time_armed=0;assert(!npc_deadline_route(self,audio) && npc_deadline_seen==npc_seen+1 && npc_deadline_foreign==npc_foreign+1);
    /* On: any speaker; the line completes on the game-clock deadline path. */
    speech_time_armed=1;memcpy(before,self,sizeof(before));
    LONG64 published=speech_time_published,routed=speech_time_routed;
    assert(npc_deadline_route(self,audio)==1 && npc_deadline_seen==npc_seen+1 && !memcmp(before,self,sizeof(before)));
    assert(speech_time_routed==routed+1 && speech_time_published==published+1 && !speech_time_failures);
    char out[384],expect[384];
    assert(speech_time_row_text((uint64_t)published,out,sizeof(out)));
    snprintf(expect,sizeof(expect),"index=%lld tick=185 key=0000000000011e13 kind=saved page=10002 line=920 duration=3ff8000000000000 "
        "start=4000000000000000 clock=4008000000000000 early=1",(long long)published);
    assert(!strcmp(out,expect));
    /* A speaker without a portable identity is routed as well, counted unportable. */
    *(uint64_t*)(self+8)=0x9999;stats.issued=212;*(double*)(image+0x6b60e48)=3.5;
    assert(npc_deadline_route(self,audio)==1 && speech_time_unportable==1);
    assert(speech_time_row_text((uint64_t)published+1,out,sizeof(out)) && strstr(out," key=0000000000000000 kind=none ") && strstr(out," early=0"));
    *(uint64_t*)(self+8)=0x5123;
    /* Timer/interruption flags: native path, counted, not a failure. */
    for(unsigned i=0;i<3;i++) {self[0x90+i]=1;assert(!npc_deadline_route(self,audio));self[0x90+i]=0;}
    assert(speech_time_flagged==3 && !speech_time_failures);
    /* Inconsistent state is a failure (the gate then stops with SPEECH_TIME_STOP_REASON). */
    *(double*)(audio+0x110)=1.25;assert(!npc_deadline_route(self,audio));*(double*)(audio+0x110)=1.5;
    *(uint32_t*)(self+0x10)=4;assert(!npc_deadline_route(self,audio));*(uint32_t*)(self+0x10)=5;
    *(double*)(self+0x18)=*(double*)(audio+0x110)=NAN;assert(!npc_deadline_route(self,audio));
    *(double*)(self+0x18)=*(double*)(audio+0x110)=0;assert(!npc_deadline_route(self,audio));
    *(double*)(self+0x18)=*(double*)(audio+0x110)=1.5;
    *(double*)(image+0x6b60e48)=1;assert(!npc_deadline_route(self,audio));*(double*)(image+0x6b60e48)=3;
    assert(!npc_deadline_route(self,NULL));
    assert(speech_time_failures==6 && speech_time_routed==routed+2);
    assert(speech_time_status(out,sizeof(out)) && strstr(out,"armed=1 ") && strstr(out," flagged=3 unportable=1 ") && strstr(out," failures=6 "));
    /* Outside the gated frames nothing is routed. */
    boot_phase=2;assert(!npc_deadline_route(self,audio));boot_phase=3;
    /* Ring: rows older than the ring report evicted, never a stale row. */
    for(unsigned i=0;i<SPEECH_TIME_RING;i++) assert(npc_deadline_route(self,audio)==1);
    assert(speech_time_row_text((uint64_t)published,out,sizeof(out)) && strstr(out,"evicted=1"));
    assert(!speech_time_row_text((uint64_t)speech_time_published,out,sizeof(out)));
    /* Arming: UI thread, boot phase 0, before frame 1; the pinned function must match
     * (the synthetic image does not), unless the deadline word is already owned. */
    speech_time_armed=0;speech_time_failures=0;boot_mode=1;boot_phase=0;stats.issued=0;npc_deadline_installed=0;
    assert(!speech_time_arm() && !speech_time_armed);
    npc_deadline_installed=1;
    stats.issued=1;assert(!speech_time_arm());stats.issued=0;
    boot_phase=1;assert(!speech_time_arm());boot_phase=0;
    ui_thread=GetCurrentThreadId()+1;assert(!speech_time_arm());ui_thread=GetCurrentThreadId();
    stats.active=0;assert(!speech_time_arm());stats.active=1;
    assert(speech_time_arm() && speech_time_armed && speech_time_arm()); /* a reload re-arms the owned word */
    npc_deadline_installed=0;assert(!speech_time_arm());
    speech_time_armed=0;speech_time_routed=speech_time_flagged=speech_time_unportable=speech_time_foreign_thread=0;
    npc_deadline_installed=saved_installed;world_identity_sealed=saved_sealed;npc_deadline_seen=npc_seen;npc_deadline_foreign=npc_foreign;
    stats.issued=saved_issued;stats.active=saved_active;ui_thread=saved_ui;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("speech game time route/flag/failure/ring/arm tests PASS\n");
}
