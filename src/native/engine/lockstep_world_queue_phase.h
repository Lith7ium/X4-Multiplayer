/* Raw pre-admission context, not a native Load-function bracket. These reads
 * never take an engine lock or invoke a getter. A changing sample is unsupported. */
typedef struct {
    unsigned status,eligible,app_flags,vbi_loading,view,main;
    uint64_t elapsed,tls_time,game,real,saved;
    uintptr_t table;
} world_queue_phase_sample;
static int world_queue_phase_verified;

static int world_queue_phase_once(int (*reader)(const void *,void *,size_t),void *tls,
                                  world_queue_phase_sample *s) {
    return tls && reader(image+TABLE_RVA,&s->table,8) && s->table &&
        reader(image+0x6adf9fc,&s->app_flags,4) && reader((void*)(s->table+0x130),&s->vbi_loading,4) &&
        reader((unsigned char*)tls+0x300,&s->tls_time,8) && reader((unsigned char*)tls+0x314,&s->view,4) &&
        reader(image+GAME_RVA,&s->game,8) && reader(image+REAL_RVA,&s->real,8) && reader(image+0x3d19790,&s->saved,8);
}
static int world_queue_phase_read(int (*reader)(const void *,void *,size_t),void *tls,
                                  world_queue_phase_sample *s) {
    world_queue_phase_sample again={0};memset(s,0,sizeof(*s));
    if(!world_queue_phase_once(reader,tls,s) || !world_queue_phase_once(reader,tls,&again)) {s->status=2;return 0;}
    if(memcmp(s,&again,sizeof(again))) {s->status=3;return 0;}
    double times[4];memcpy(&times[0],&s->tls_time,8);memcpy(&times[1],&s->game,8);
    memcpy(&times[2],&s->real,8);memcpy(&times[3],&s->saved,8);
    if(s->vbi_loading>1 || s->view>1 || !isfinite(times[0]) || !isfinite(times[1]) ||
       !isfinite(times[2]) || !isfinite(times[3])) {s->status=4;return 0;}
    s->status=1;return 1;
}
static int world_queue_phase_pins(void) {
    /* FrameTick has only our verified fixed-dt two-byte branch replacement.
     * Reconstruct that owned word locally; never accept a foreign replacement. */
    unsigned char tick[2127];unsigned short branch;
    if(!read_local(image+TICK_RVA,tick,sizeof(tick))) return 0;
    memcpy(&branch,tick+0xfa3302-TICK_RVA,2);
    if(dt_installed) {
        if(branch!=0x9066) return 0;branch=0x0874;memcpy(tick+0xfa3302-TICK_RVA,&branch,2);
    }
    return hash_ok(tick,sizeof(tick),"5a966bc940eec10249ced85daf2751984ee5f1a2bfd28889d812831b221519be") &&
        hash_ok(image+0xac2a50,522,"2202cc0f6f274b684ad3535ea62549911d5f751778bf014b9120ba9aaa66746b") &&
        hash_ok(image+0xac2c70,446,"a2c4f27ac6dacdce5a58800b74dc3708db0cf10bdf03f1dabbed4760f99813a7") &&
        hash_ok(image+0x9ee7a0,9772,"38df90e5d9385e30279abbccfdb9f12f4789fe61cc2bed4632dc007cc2f54f9c") &&
        hash_ok(image+0x14b8dc0,89,"4d33bf40118fbb8789b8d3d92695100f42b704fd820bc0891148dc6d1a969347") &&
        hash_ok(image+0x14b8e20,101,"e416f11fb3a8d1f3d6c4e40995a2f57582064bd6e68539f636f96c2f3b6c3020");
}
