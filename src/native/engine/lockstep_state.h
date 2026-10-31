/* Read stored shield state without invoking a lazy recharge getter (which can
 * write). Field layout is pinned by the executable and ShieldRechargeSource. */
static uint64_t shield_refresh_calls,shield_refresh_failures;
static int shield_refresh_verified;
static int shield_refresh_native(unsigned char *object) {
    /* The native getter writes value=max/time=0 only once recharge is complete
     * and the requested time is not ahead of this thread's simulation clock.
     * Materialize that native cache at a shared boundary, before reading it. */
    if(!shield_refresh_verified) {
        if(!hash_ok(image+0x51f5b0,210,"08ccd11a0e4609d2eb0b02482402e3409ae84a14a40bd9887ce0d45edbefd243") ||
           !hash_ok(image+0x79ddb0,376,"3902e1ea32691f964ebafb2d5d930c34b96a6f4058ffe83de28df3cdc0504a21") ||
           !hash_ok(image+0x79ef60,13,"de904b36971fcd4570eb1c8cc934d46ed992ced4fd26fd4e5376f63aaecbf17a")) return 0;
        shield_refresh_verified=1;
    }
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);double time;
    if(!tls || !tls[0] || !read_local((unsigned char*)tls[0]+0x300,&time,8) || !isfinite(time) || time<0) return 0;
    typedef float (*percent_fn)(void *,double);
    float percent=((percent_fn)(image+0x51f5b0))(object,time);
    ++shield_refresh_calls;
    return isfinite(percent) && percent>=0 && percent<=100.0f;
}
/* native_gate_world_hash_shields = 'canonical' (world hash only, DEFAULT OFF): a shield as the lazy
 * getter leaves it. GetShieldPercent 0x51f5b0 -> 0x79ddb0 (through the thunk 0x7c5f0c) reads value
 * (group+0x68), time (+0x70) and the recharge source (+0x78, the ShieldRechargeSource at +0x90): max
 * (slot 1, 0x790950), delay (slot 2, 0x790990 -> 0x791660) and rate (slot 3, 0x7909b0 -> 0x7916d0),
 * all from the group's own fields. Once the recharge is complete at the read time it writes value=max
 * and time=0. Every reader does this: a client's UI that shows a ship's shield bar rewrites the state on
 * that client alone, at the first frame the shield is full (scene1/scene2: three ships and their weapons
 * in slot 1's sector, health hash different from tick 331..380 on client 1 only, the same in scene3
 * without view scenes). Both forms read and recharge alike from then on (a full shield reads max, damage
 * reads it first), so the capture hashes the state a read at the capture time would leave: value=max,
 * time=0 when full, else the stored fields. The getters are reproduced here, nothing native is called and
 * nothing is written. Pins: the read and the three getters, the source's slots 1..3; checked when armed. */
static int shield_canonical;
static uint64_t shield_canonical_count;
static int shield_canonical_pins(void) {
    return hash_ok(image+0x79ddb0,376,"3902e1ea32691f964ebafb2d5d930c34b96a6f4058ffe83de28df3cdc0504a21") &&
        hash_ok(image+0x7c5f0c,0xc,"2738d3ac99f46d09b62acf72b99b177250fe443ca97dc813c315e045f0d2f369") &&
        hash_ok(image+0x790950,0x34,"30fd22408fd8abc4111aa92c26be9f75678b662721c4bf92bff8d458a0e5d70c") &&
        hash_ok(image+0x790990,0x11,"f276621c3747d4e3f9021682b677d9a8c760a339fd837aaabac595ba5cb48608") &&
        hash_ok(image+0x7909b0,0x11,"30626c23cdd7e1d2278aeff331998995f66b7197da7053f9f9221e0cefa7fe81") &&
        hash_ok(image+0x791660,0x6d,"5955d393a6f264f66f32072dc02e572385533b0b17fa0b92bb3262345d97e96a") &&
        hash_ok(image+0x7916d0,0x6e,"e8bdf6b9e511c779121391c0bd0583732d451c8deac30af87b9c9a84ee1a9693") &&
        *(void **)(image+0x2b19708)==(void *)(image+0x790950) && *(void **)(image+0x2b19710)==(void *)(image+0x790990) &&
        *(void **)(image+0x2b19718)==(void *)(image+0x7909b0);
}
/* 0x79ddb0's test for a complete recharge at now, with the getters' arithmetic (float where they are float). */
static int shield_full_at(double value,double changed,double maximum,double delay,float rate,float factor,uint64_t count,
                          const float mods[3],double now,double *max_out) {
    const double max=(double)mods[0]*maximum;
    if(!(fabs(max)>=1e-7) || !(fabs(value-max)>=1e-7)) return 0;
    const double ready=changed+(count?((double)mods[1]*delay)/(double)(int64_t)count:0.0);
    if(!(now>ready)) return 0;
    float per=rate;
    if(count) per=per/(float)count;
    per=per*factor*mods[2];
    if(!(fabsf(per)>=9.99999975e-05f)) return 0;
    if(!(now>=(max-value)/(double)per+ready)) return 0;
    *max_out=max;return 1;
}
static int shield_state(unsigned char *object,char out[192]);
static int shield_canonical_reading;
static double shield_canonical_now,shield_reading_now;
static int shield_state_at(unsigned char *object,char out[192],const double *now) {
    if(!now) return shield_state(object,out);
    unsigned char *group,*source;
    if(!sample_read(object+0x290,&group,8)) return 0;
    if(group && (!sample_read(group+0x78,&source,8) || source!=group+0x90)) return 0;
    shield_reading_now=*now;shield_canonical_reading=1;const int ok=shield_state(object,out);shield_canonical_reading=0;
    return ok;
}
static int shield_state(unsigned char *object,char out[192]) {
    unsigned char *group,*source_group,*modifiers;void *source_vtable;
    uintptr_t begin,end;uint64_t value,changed,maximum,delay;uint32_t rate,factor,mods[3]={0x3f800000,0x3f800000,0x3f800000};
    if(!sample_read(object+0x290,&group,8)) return 0;
    if(!group) {strcpy(out,"none");return 1;}
    if(!sample_read(group+0x90,&source_vtable,8) || source_vtable!=image+0x2b19700 ||
       !sample_read(group+0x98,&source_group,8) || source_group!=group ||
       !sample_read(group+0x10,&begin,8) || !sample_read(group+0x18,&end,8) || end<begin || (end-begin)%8 || (end-begin)/8>4096 ||
       !sample_read(group+0x68,&value,8) || !sample_read(group+0x70,&changed,8) ||
       !sample_read(group+0x40,&maximum,8) || !sample_read(group+0x48,&delay,8) ||
       !sample_read(group+0x28,&rate,4) || !sample_read(group+0x50,&factor,4) ||
       !sample_read(group+0xa0,&modifiers,8) || (modifiers && !sample_read(modifiers+0x18,mods,12))) return 0;
    if(shield_canonical_reading) {
        double v,c,m,d,full;float r,f,fm[3];
        memcpy(&v,&value,8);memcpy(&c,&changed,8);memcpy(&m,&maximum,8);memcpy(&d,&delay,8);memcpy(&r,&rate,4);memcpy(&f,&factor,4);memcpy(fm,mods,12);
        if(shield_full_at(v,c,m,d,r,f,(uint64_t)((end-begin)/8),fm,shield_reading_now,&full)) {
            memcpy(&value,&full,8);changed=0;++shield_canonical_count;
        }
    }
    snprintf(out,192,"%016llx%016llx%016llx%016llx%08x%08x%016llx%u%08x%08x%08x",
        (unsigned long long)value,(unsigned long long)changed,(unsigned long long)maximum,(unsigned long long)delay,
        rate,factor,(unsigned long long)((end-begin)/8),modifiers!=NULL,mods[0],mods[1],mods[2]);
    return 1;
}
