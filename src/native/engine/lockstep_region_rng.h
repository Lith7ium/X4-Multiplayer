/* Region updates (hazard damage fields) on per-region streams, DEFAULT OFF
 * (native_gate_region_rng = "key" | "observe").
 *
 * Census big-BW (war), big-BS/BS2 (war + spawn), cam-CamB (no war): the same
 * 357 imported objects diverge in the health domain only (stored hull and
 * shield fields), first visible at capture 180, 240 or 300 depending on the
 * run, and cam-CamA shows none within 600 ticks. They are every ship of the
 * zones 7a6/7c8/7c9/7ca/7cb/7cc of sector 0x7a5 (cluster_32_sector001) plus
 * the shield generators and weapons of the two XL carriers 0x97720/0xa4360
 * parked there. All have attention 2 and the save already records them as
 * attacked by the sector itself ("hitbyareadamage"). Motion and timing stay
 * equal, AI/MD script draws stay equal (big-BW joined fight.* rows: no
 * mismatch, same execution order every tick). Both clients change the health
 * of every 0x7c9 ship in every 60-tick window from 60 on; the values only
 * stop agreeing.
 *
 * Mechanism (static analysis of X4 9.00): the sector's hazard region is a
 * U::Region (cluster child, vtable 0x2b2ad60). Its Move (slot 674 0x767410,
 * dispatched by the component walk on the simulation thread through
 * [vtable+0x1510]) first calls slot 8 of every field in its field vector
 * (+0x288..+0x290). U::Regions::DamageField slot 8 (0xee16a0) runs a pass when
 * the TLS game time exceeds its due time (+0x10f8): every matching object of
 * the sector's zones takes damage proportional to dt = now - last pass
 * (+0x10f0) through a FixedDamageSource (0xee1a50 -> Destructible::ApplyDamage,
 * shields first, attacked event kind 6 with the sector as attacker). Then
 * last = now, and the next due time is now + 0.1 s when any zone has
 * attention >= 5, else now + 1.0 s + Random::FloatState(TLS+0x318, 0.1).
 * That low-attention jitter is a natural draw of the shared sim-thread stream
 * in the middle of the walk: the gate reseeds the word once per frame, but
 * any earlier walk consumer that drew a different number of times (the
 * end-of-frame word already differs on 40-450 of 600 ticks) shifts it. One
 * different jitter moves the next pass by up to six ticks, the pass after it
 * draws from another frame's stream, and dt-proportional damage makes the
 * stored hull/shield values of the whole sector disagree at every later
 * capture. The onset depends on whether a pass lands on such a frame, hence
 * the run-dependent 180/240/300/none. No wall clock, job, other thread or
 * address-ordered container is involved (the zone/object iteration follows
 * child-index vectors; no RNG per object).
 *
 * Fix: swap the U::Region slot 674 data pointer (0x2b2c270, no thunk) for
 * region_rng_move. On the sim thread in boot phase 3 every Region::Move runs
 * in a scope keyed by
 *   lockstep_seed(REGION_RNG_DOMAIN, tick, region saved ID, 0, ordinal, 0)
 * (ordinal = count of that region's Moves in this tick, normally 1) and the
 * caller's word is restored afterwards, also when unwinding. All draws of the
 * region's fields (and of nested script scopes, which key themselves) then
 * depend only on tick and region identity; later walk consumers see the word
 * they would have seen without the region. A region without a saved ID is
 * never given a fabricated key (counted unmapped, natural stream). "observe"
 * derives the same keys without writing. Rows: one per DamageField pass
 * (due time changed), with the word the pass drew from, the generator steps of
 * the whole Move, and the new last/due times as exact double bits; a per-tick
 * dueset sums (region, field, due) so two clients compare in one field.
 * Another thread, boot phases before 3 and nested Moves run natively and are
 * counted (threads/passed/nested). Failures (no TLS word, an unreadable region
 * identity or field vector, an exhausted ordinal table, abnormal unwind) stop
 * the gate with reason 93; stop restores the slot. */
#define REGION_RNG_DOMAIN 0x58344c524547494eULL /* "X4LREGIN" */
/* The Linux gate (linux/gate_region.h) defines these for its executable, and its own pins. */
#ifndef REGION_RNG_SLOT
#define REGION_RNG_SLOT 0x2b2c270u          /* U::Region vtable 0x2b2ad60, slot 674 */
#define REGION_RNG_MOVE 0x767410u           /* U::Region::Move */
#define REGION_RNG_MOVE_LENGTH 477u
#define REGION_RNG_FIELD_VTABLE 0x2c4b2f0u  /* U::Regions::DamageField::vftable */
#define REGION_RNG_FIELD_SLOT 0x2c4b330u    /* its slot 8 (per-frame update) */
#define REGION_RNG_FIELD_UPDATE 0xee16a0u
#define REGION_RNG_FIELD_UPDATE_LENGTH 940u
#define REGION_RNG_FIELDS 0x288u            /* Region field vector begin/end */
#endif
#define REGION_RNG_LAST 0x10f0u             /* DamageField last pass time */
#define REGION_RNG_DUE 0x10f8u              /* DamageField next pass time */
#define REGION_RNG_FIELD_CAP 8u
#define REGION_RNG_FIELD_MAX 256u
#define REGION_RNG_TABLE 1024u
#define REGION_RNG_PROBES 128u
#define REGION_RNG_RING 4096u
enum {REGION_RNG_OBSERVE=0,REGION_RNG_KEY=1};
typedef void (*region_rng_fn)(void *);
typedef struct {uint64_t saved;uint32_t count,epoch;} region_rng_slot_t;
typedef struct {uintptr_t field;uint64_t last,due;unsigned index;} region_rng_field_t;
typedef struct {
    uint64_t tick,region,ordinal,key,word,exit,last,due,prior;
    uint32_t draws,field;uint8_t seeded,mapped;
} region_rng_row_t;
typedef struct {uint64_t tick,calls,keyed,passes,draws,keyset,dueset;uint32_t open;} region_rng_tally;
static volatile LONG region_rng_active;
static int region_rng_armed,region_rng_installed,region_rng_restored,region_rng_mode,region_rng_depth;
static uint32_t region_rng_epoch;
static uint64_t region_rng_epoch_tick=UINT64_MAX,region_rng_draws;
static region_rng_slot_t region_rng_slots[REGION_RNG_TABLE];
static region_rng_row_t region_rng_ring[REGION_RNG_RING];
static region_rng_tally region_rng_now,region_rng_last;
static volatile LONG64 region_rng_calls,region_rng_scoped,region_rng_keyed,region_rng_passed,region_rng_threads,region_rng_nested,
    region_rng_unmapped,region_rng_table_full,region_rng_fields_seen,region_rng_field_overflow,region_rng_passes,
    region_rng_draws_unknown,region_rng_cursor,region_rng_failures;

/* Per-tick ordinal of a region's Moves; 0 when the bounded table is exhausted.
 * A slot of an older epoch is empty, so a new tick costs one increment. */
static uint64_t region_rng_ordinal(uint64_t tick,uint64_t saved) {
    if(region_rng_epoch_tick!=tick) {
        region_rng_epoch_tick=tick;
        if(!++region_rng_epoch) {memset(region_rng_slots,0,sizeof(region_rng_slots));region_rng_epoch=1;}
    }
    unsigned first=(unsigned)(lockstep_mix(saved)&(REGION_RNG_TABLE-1));
    for(unsigned n=0;n<REGION_RNG_PROBES;n++) {
        region_rng_slot_t *s=&region_rng_slots[(first+n)&(REGION_RNG_TABLE-1)];
        if(s->epoch!=region_rng_epoch) {s->epoch=region_rng_epoch;s->saved=saved;s->count=1;return 1;}
        if(s->saved==saved) return ++s->count;
    }
    return 0;
}
/* The DamageFields of a region's field vector (guarded reads; at most CAP
 * are followed, the rest counted). 0 when the vector itself is unreadable. */
static int region_rng_fields(const unsigned char *self,region_rng_field_t *out,unsigned *count,int counted) {
    uintptr_t bounds[2];*count=0;
    if(!script_rng_read(self+REGION_RNG_FIELDS,bounds,sizeof(bounds)) || bounds[1]<bounds[0] || ((bounds[1]-bounds[0])&7) ||
       (bounds[1]-bounds[0])/8>REGION_RNG_FIELD_MAX || (bounds[1]>bounds[0] && !bounds[0])) return 0;
    for(unsigned i=0;i<(unsigned)((bounds[1]-bounds[0])/8);i++) {
        uintptr_t field=0,vtable=0;
        if(!script_rng_read((const void*)(bounds[0]+8*(uintptr_t)i),&field,8) || !field || !script_rng_read((const void*)field,&vtable,8)) return 0;
        if(vtable!=(uintptr_t)(image+REGION_RNG_FIELD_VTABLE)) continue;
        if(*count==REGION_RNG_FIELD_CAP) {if(counted) InterlockedIncrement64(&region_rng_field_overflow);continue;}
        region_rng_field_t *f=&out[*count];f->field=field;f->index=i;
        if(!script_rng_read((const void*)(field+REGION_RNG_LAST),&f->last,8) || !script_rng_read((const void*)(field+REGION_RNG_DUE),&f->due,8)) return 0;
        ++*count;
    }
    return 1;
}
static void region_rng_roll(uint64_t tick) {
    if(region_rng_now.open && region_rng_now.tick==tick) return;
    if(region_rng_now.open) region_rng_last=region_rng_now;
    memset(&region_rng_now,0,sizeof(region_rng_now));region_rng_now.tick=tick;region_rng_now.open=1;
}
/* A pass is a changed due time; the field must still sit at the same index. */
static void region_rng_record(const unsigned char *self,const region_rng_field_t *before,unsigned count,uint64_t tick,uint64_t saved,
                              uint64_t ordinal,uint64_t key,uint64_t word,uint64_t exit,int seeded) {
    uint32_t draws=exit==word?0:script_rng_steps(word,exit);
    if(draws==UINT32_MAX) InterlockedIncrement64(&region_rng_draws_unknown);
    else {region_rng_draws+=draws;region_rng_now.draws+=draws;}
    if(!count) return;
    region_rng_field_t after[REGION_RNG_FIELD_CAP];unsigned now=0;
    if(!region_rng_fields(self,after,&now,0)) {InterlockedIncrement64(&region_rng_failures);return;}
    for(unsigned i=0;i<count;i++) {
        const region_rng_field_t *b=&before[i],*a=NULL;
        for(unsigned j=0;j<now && !a;j++) if(after[j].field==b->field && after[j].index==b->index) a=&after[j];
        if(!a || a->due==b->due) continue;
        InterlockedIncrement64(&region_rng_passes);++region_rng_now.passes;
        region_rng_now.dueset+=lockstep_mix(saved^lockstep_mix(((uint64_t)a->index<<32)^lockstep_mix(a->due^lockstep_mix(a->last))));
        LONG64 index=InterlockedIncrement64(&region_rng_cursor)-1;
        region_rng_row_t *r=&region_rng_ring[(uint64_t)index%REGION_RNG_RING];
        r->tick=tick;r->region=saved;r->ordinal=ordinal;r->key=key;r->word=word;r->exit=exit;r->last=a->last;r->due=a->due;
        r->prior=b->due;r->draws=draws;r->field=a->index;r->seeded=(uint8_t)seeded;r->mapped=saved!=0;
    }
}
/* rng = the executing thread's TLS word. */
static void region_rng_run(region_rng_fn original,uint64_t *rng,void *self) {
    InterlockedIncrement64(&region_rng_calls);
    if(!InterlockedCompareExchange(&region_rng_active,0,0) || !stats.active || boot_phase!=3) {
        InterlockedIncrement64(&region_rng_passed);original(self);return;
    }
    /* Like the script scopes: another thread runs natively and is counted. */
    if(GetCurrentThreadId()!=ui_thread) {InterlockedIncrement64(&region_rng_threads);original(self);return;}
    if(region_rng_depth) {InterlockedIncrement64(&region_rng_nested);original(self);return;}
    unsigned char *object=self;uint64_t runtime=0;region_rng_field_t fields[REGION_RNG_FIELD_CAP];unsigned count=0;
    if(!rng || !script_rng_read(object+8,&runtime,8) || !runtime || !region_rng_fields(object,fields,&count,1)) {
        InterlockedIncrement64(&region_rng_failures);original(self);return;
    }
    uint64_t tick=stats.issued,saved=script_rng_saved(runtime),ordinal=0,key=0;
    region_rng_roll(tick);++region_rng_now.calls;
    if(count) InterlockedAdd64(&region_rng_fields_seen,(LONG64)count);
    if(!saved) InterlockedIncrement64(&region_rng_unmapped);
    else if(!(ordinal=region_rng_ordinal(tick,saved))) {
        InterlockedIncrement64(&region_rng_table_full);InterlockedIncrement64(&region_rng_failures);original(self);return;
    } else {key=lockstep_seed(REGION_RNG_DOMAIN,tick,saved,0,ordinal,0);InterlockedIncrement64(&region_rng_scoped);}
    int seeded=saved && region_rng_mode==REGION_RNG_KEY;
    uint64_t natural=*rng,word=natural,exit=natural;DWORD error=GetLastError();
    if(seeded) {*rng=key;word=key;InterlockedIncrement64(&region_rng_keyed);++region_rng_now.keyed;region_rng_now.keyset+=lockstep_mix(key);}
    SetLastError(error);++region_rng_depth;
    __try {original(self);}
    __finally {
        error=GetLastError();--region_rng_depth;exit=*rng;if(seeded) *rng=natural;
        if(AbnormalTermination()) InterlockedIncrement64(&region_rng_failures);
        SetLastError(error);
    }
    error=GetLastError();region_rng_record(object,fields,count,tick,saved,ordinal,key,word,exit,seeded);SetLastError(error);
}
/* noinline keeps the TEB read out of the SEH funclet of the scope. */
static __attribute__((noinline)) void region_rng_move(void *self) {
    region_rng_run((region_rng_fn)(image+REGION_RNG_MOVE),interrupt_rng(),self);
}
/* Region::Move (field vector +0x288/+0x290, slot-8 calls, then the walk),
 * DamageField slot 8 (TLS time/due/last/jitter layout) and both vtable slots. */
#ifndef REGION_RNG_OWN_PLATFORM
static int region_rng_pinned(void) {
    return hash_ok(image+REGION_RNG_MOVE,REGION_RNG_MOVE_LENGTH,"2881619c10d25c81e817fb6a142740b71fe32c99d4ec22ebd43c0a838a9a4ef7") &&
        hash_ok(image+REGION_RNG_FIELD_UPDATE,REGION_RNG_FIELD_UPDATE_LENGTH,"9717109c4eaf193fe03575f6cf27297cca1600a766d94820327ead9bd275fc79") &&
        *(void**)(image+REGION_RNG_SLOT)==(void*)(image+REGION_RNG_MOVE) &&
        *(void**)(image+REGION_RNG_FIELD_SLOT)==(void*)(image+REGION_RNG_FIELD_UPDATE);
}
#endif
static int region_rng_owned(void) {return !region_rng_installed || *(void**)(image+REGION_RNG_SLOT)==(void*)region_rng_move;}
/* Armed once at boot, before load (idempotent on a loading-UI reload with the
 * same mode). Keys need the world identity map. */
static int region_rng_arm(int mode) {
    mode=mode==REGION_RNG_KEY?REGION_RNG_KEY:REGION_RNG_OBSERVE;
    if(region_rng_armed) return stats.active && region_rng_installed && region_rng_mode==mode;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || !world_identity_installed ||
       !region_rng_pinned() || !script_rng_slot_swap(REGION_RNG_SLOT,image+REGION_RNG_MOVE,(void*)region_rng_move)) return 0;
    region_rng_installed=1;region_rng_mode=mode;region_rng_armed=1;InterlockedExchange(&region_rng_active,1);return 1;
}
/* Open scopes still restore their own words; this gives the slot back. */
static void region_rng_restore(void) {
    if(!region_rng_armed) return;
    InterlockedExchange(&region_rng_active,0);
    if(region_rng_installed && script_rng_slot_swap(REGION_RNG_SLOT,(void*)region_rng_move,image+REGION_RNG_MOVE)) region_rng_installed=0;
    region_rng_restored=!region_rng_installed;
}
static int region_rng_status(char *out,size_t cap) {
    LONG64 cursor=InterlockedCompareExchange64(&region_rng_cursor,0,0);
    LONG64 dropped=cursor>(LONG64)REGION_RNG_RING?cursor-(LONG64)REGION_RNG_RING:0;
    const region_rng_tally *t=&region_rng_now,*l=&region_rng_last;
    int n=snprintf(out,cap,"armed=%d installed=%d active=%ld mode=%d restored=%d calls=%lld scoped=%lld keyed=%lld passed=%lld"
        " threads=%lld nested=%lld unmapped=%lld table_full=%lld fields=%lld field_overflow=%lld passes=%lld draws=%llu draws_unknown=%lld"
        " cur_tick=%llu cur_calls=%llu cur_keyed=%llu cur_passes=%llu cur_draws=%llu cur_keyset=%016llx cur_dueset=%016llx"
        " last_tick=%llu last_calls=%llu last_keyed=%llu last_passes=%llu last_draws=%llu last_keyset=%016llx last_dueset=%016llx"
        " records=%lld dropped=%lld ring=%u failures=%lld",
        region_rng_armed,region_rng_installed,region_rng_active,region_rng_mode,region_rng_restored,(long long)region_rng_calls,
        (long long)region_rng_scoped,(long long)region_rng_keyed,(long long)region_rng_passed,(long long)region_rng_threads,
        (long long)region_rng_nested,(long long)region_rng_unmapped,(long long)region_rng_table_full,(long long)region_rng_fields_seen,
        (long long)region_rng_field_overflow,(long long)region_rng_passes,(unsigned long long)region_rng_draws,(long long)region_rng_draws_unknown,
        (unsigned long long)t->tick,(unsigned long long)t->calls,(unsigned long long)t->keyed,(unsigned long long)t->passes,
        (unsigned long long)t->draws,(unsigned long long)t->keyset,(unsigned long long)t->dueset,
        (unsigned long long)l->tick,(unsigned long long)l->calls,(unsigned long long)l->keyed,(unsigned long long)l->passes,
        (unsigned long long)l->draws,(unsigned long long)l->keyset,(unsigned long long)l->dueset,
        (long long)cursor,(long long)dropped,REGION_RNG_RING,(long long)region_rng_failures);
    return n>0 && (size_t)n<cap?n:0;
}
/* One census row per DamageField pass; evicted rows report evicted=1. Times
 * are exact double bits: last = this pass, due = next pass, prior = old due. */
static int region_rng_row(uint64_t index,char *out,size_t cap) {
    LONG64 cursor=InterlockedCompareExchange64(&region_rng_cursor,0,0);
    if((LONG64)index>=cursor) return 0;
    int n;
    if(cursor>(LONG64)REGION_RNG_RING && index<(uint64_t)(cursor-(LONG64)REGION_RNG_RING))
        n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index);
    else {
        const region_rng_row_t *r=&region_rng_ring[index%REGION_RNG_RING];
        n=snprintf(out,cap,"index=%llu tick=%llu region=%016llx field=%u ord=%llu mapped=%u seeded=%u key=%016llx word=%016llx exit=%016llx"
            " draws=%lld last=%016llx due=%016llx prior=%016llx",
            (unsigned long long)index,(unsigned long long)r->tick,(unsigned long long)r->region,r->field,(unsigned long long)r->ordinal,
            r->mapped,r->seeded,(unsigned long long)r->key,(unsigned long long)r->word,(unsigned long long)r->exit,
            r->draws==UINT32_MAX?-1LL:(long long)r->draws,(unsigned long long)r->last,(unsigned long long)r->due,(unsigned long long)r->prior);
    }
    return n>0 && (size_t)n<cap?n:0;
}
