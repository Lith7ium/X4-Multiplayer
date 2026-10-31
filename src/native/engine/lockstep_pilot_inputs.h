/* Default-off lockstep piloting prototype; stop reason 91.
 * Each slot names one saved ship. On every client, at tick T, that ship is
 * driven from its slot's pilot record for T by substituting only the value
 * argument of the six velocity requests its native Steering model already makes
 * (e5d380 from the six pinned ec7200 call sites: angular pitch/yaw/roll on
 * solver+12cd0, linear x/y/z on solver+6a00). The ship's own native solver,
 * limits, rounding, integration and publication are unchanged; no native call,
 * order, transform or timer is added. Unknown callers, other solvers and calls
 * outside the window pass through with all four arguments unchanged, once.
 * The request entry and its thunk slot (2496/2528) are shared with the
 * read-only request observer; the two are exclusive.
 * Capture samples the local PlayerController's processed input channels and
 * never changes local input; with parked controls the optional device sampler
 * (lockstep_pilot_device.h) fills the same channels from OS device state.
 * Optional local suppression parks native controls from start-3 until stop
 * with one owned share of the control deactivation count (menus and the map
 * add their own shares on top and are tolerated) and releases only that
 * share. Optional camera follow
 * (lockstep_pilot_camera.h) is presentation-side and outside the record.
 * Optional native controls (lockstep_pilot_native.h, stop reason 113) give the
 * capture X4's own input processing back from start-2 while the Agent keeps
 * motion authority. Optional stock piloting (lockstep_pilot_stock.h, stop reason
 * 155) flies slot 1 (and with lockstep_pilot_slots.h, stop reason 161, slots
 * 2..N) with X4's own path from its recorded dispatch stream; a stock slot then
 * has no pilot record and no Steering substitution. */
#define PILOT_RECORD_BYTES 28u
#define PILOT_RECORD_VERSION 1u
#define PILOT_SLOTS 4u
#define PILOT_TICK_MAX 8388607u /* a window's last tick, CREATED_TICK_LIMIT: 77 hours at 30 ticks a second */
#define PILOT_RING 64u
#define PILOT_LOCAL_RING 32u
#define PILOT_DELAY_MIN 2u
#define PILOT_DELAY_MAX 30u
#define PILOT_SUPPRESS_LEAD 3u
#define PILOT_DOMAIN 0x50494c4f54315631ULL
#define PILOT_MODE_ASSIST 1u
#define PILOT_MODE_BOOST 2u
#define PILOT_MODE_TRAVEL 4u
#define PILOT_MODE_TARGET_UNMAPPED 8u
#define PILOT_MODE_REQUESTS 16u /* q0..q5 are the occupied ship's native requests as fractions of its Agent limits */
#define PILOT_MODE_KNOWN 31u
#define PILOT_SOURCE_SCRIPT 1u
#define PILOT_SOURCE_WIRE 2u
/* Agent+340 flight limits as the Steering clamp reads them (floats): forward
 * +24, reverse lower bound +2c, x +30, y +34, angular axis1 +38, axis0 +3c,
 * axis2 +40. Read as eight floats from Agent+364. */
#define PILOT_LIMITS 0x364u
typedef struct {
    unsigned char mode,buttons;
    uint16_t capture16;
    int16_t q[7]; /* Q15: x y z pitch yaw roll, then throttle */
    uint64_t target;
} pilot_record;
typedef struct {
    uint64_t saved,runtime;
    unsigned char *object,*agent,*solver;
    pilot_record record;
    unsigned resolved,driving,player,reversed;
    unsigned seen,dead,renewed;  /* resolved once; no ship now; a ship built after a death (lockstep_pilot_death.h) */
    unsigned char *found;        /* this pass's registry answer for the runtime ID */
    volatile LONG64 applied[2];
} pilot_slot;
typedef struct {uint64_t tick;unsigned present;unsigned char bytes[PILOT_SLOTS][PILOT_RECORD_BYTES];} pilot_staged;
typedef struct {uint64_t capture,apply;unsigned used;unsigned char bytes[PILOT_RECORD_BYTES];} pilot_local;
typedef void *(*pilot_find_fn)(void *,uint64_t,unsigned);
typedef void (*pilot_controls_fn)(void);
typedef void (*pilot_refresh_fn)(char); /* d9cc30(controller present): re-evaluate the PlayerController's input contexts */
typedef unsigned char (*pilot_authority_fn)(void *); /* d8dd50: 1 = the pathing Agent, not the PlayerController, moves the ship */
static pilot_slot pilot_slots[PILOT_SLOTS];
static pilot_staged pilot_ring[PILOT_RING];
static pilot_local pilot_locals[PILOT_LOCAL_RING];
static unsigned pilot_slot_count,pilot_source,pilot_delay,pilot_suppress;
static uint64_t pilot_start,pilot_stop,pilot_frames,pilot_frame_tick,pilot_original_word,pilot_patched_word;
static int pilot_armed,pilot_installed,pilot_restored,pilot_controls_owned,pilot_capture_checked,pilot_suppress_checked;
static uint32_t pilot_controls_base,pilot_controls_seen;
static unsigned pilot_controls_contexts,pilot_controls_flight,pilot_controls_authority=2;
static LONG64 pilot_controls_retakes,pilot_controls_ui_passes,pilot_controls_refreshes,pilot_neutral;
static uint32_t *pilot_controls_counter_ptr;
static int pilot_controls_present;
static volatile LONG pilot_live;
static volatile LONG64 pilot_failures,pilot_open,pilot_restore_open,pilot_calls,pilot_frame_applied,pilot_frame_receipt,
    pilot_applied,pilot_receipt,pilot_missing,pilot_late,pilot_unresolved,pilot_no_solver,pilot_bad_limits,pilot_forced,
    pilot_staged_count,pilot_captures;
static const char *pilot_stage="off";
static unsigned pilot_stock_slots; /* bit s-1: slot s is flown by stock piloting (lockstep_pilot_stock.h) */
static int pslot_failed;          /* a slots 2..N invariant failed (lockstep_pilot_slots.h, stop reason 161) */
static int pres_failed;           /* a presentation invariant failed (lockstep_pilot_present.h, stop reason 162) */
static volatile LONG pres_open;   /* its scope is open: a slot scope refuses (lockstep_pilot_slots.h) */
/* Ship death (lockstep_pilot_death.h, stop reason 163): armed, failed, bit s-1 = slot s has no ship, this pass's lookup. */
static int pdeath_armed,pdeath_failed;
static unsigned pdeath_dead;
static pilot_find_fn pdeath_find;
static void *pdeath_map;
static const void *pdeath_player;
#define PDEATH_HULL 0x280u               /* Destructible hull, double */
#define PDEATH_HULL_EPSILON 1e-07        /* 518210's test for a hull at zero */
/* UI replay (lockstep_ui_replay.h, stop reason 164): what the piloting path needs to know of it. */
enum {UIREP_FAMILY_TRAVEL=1,UIREP_FAMILY_TARGET=2,UIREP_FAMILY_ORDERS=4,UIREP_FAMILIES=7};
static int uirep_armed,uirep_failed;
static unsigned uirep_families;
static volatile LONG uirep_ui_depth,uirep_dispatch_depth;  /* inside slot 6 / the slot 4 dispatch of presentation ids */
static int uirep_pass(uint64_t tick);static int uirep_frame(uint64_t tick,const char **why);static void uirep_restore(void);static const char *uirep_why;

/* Record v1, 28 bytes, little endian:
 *   0 version 1 | 1 mode bits (assist, boost, travel, target unmapped,
 *   native requests: q0..q5 are the occupied ship's own velocity requests as
 *   fractions of its Agent limits, which pilot_target maps back) |
 *   2..3 low 16 bits of the capture tick | 4..17 seven int16 Q15 values:
 *   x y z pitch yaw roll (processed input channels 6c..71) and throttle |
 *   18 buttons (low nibble: native trigger bytes 38f0728..72b) | 19 zero |
 *   20..27 saved ID of the soft target, 0 if none or not a saved object.
 * -32768 is not a valid Q15 value. Every client decodes the same integers. */
static int16_t pilot_quantize(float value) {
    if(value>1.0f) value=1.0f;
    if(value<-1.0f) value=-1.0f;
    return (int16_t)lrintf(value*32767.0f);
}
static float pilot_axis(int16_t q) {return (float)q/32767.0f;}
static void pilot_encode(const pilot_record *r,unsigned char out[PILOT_RECORD_BYTES]) {
    memset(out,0,PILOT_RECORD_BYTES);out[0]=PILOT_RECORD_VERSION;out[1]=r->mode;
    out[2]=(unsigned char)r->capture16;out[3]=(unsigned char)(r->capture16>>8);
    for(unsigned i=0;i<7;i++) {uint16_t v=(uint16_t)r->q[i];out[4+2*i]=(unsigned char)v;out[5+2*i]=(unsigned char)(v>>8);}
    out[18]=r->buttons;
    for(unsigned i=0;i<8;i++) out[20+i]=(unsigned char)(r->target>>(8*i));
}
static int pilot_decode(const unsigned char in[PILOT_RECORD_BYTES],pilot_record *r) {
    if(in[0]!=PILOT_RECORD_VERSION || (in[1]&~PILOT_MODE_KNOWN) || (in[18]&0xf0) || in[19]) return 0;
    memset(r,0,sizeof(*r));r->mode=in[1];r->buttons=in[18];r->capture16=(uint16_t)(in[2]|in[3]<<8);
    for(unsigned i=0;i<7;i++) {
        r->q[i]=(int16_t)(uint16_t)(in[4+2*i]|in[5+2*i]<<8);
        if(r->q[i]==INT16_MIN) return 0;
    }
    for(unsigned i=0;i<8;i++) r->target|=(uint64_t)in[20+i]<<(8*i);
    return !(r->target && (r->mode&PILOT_MODE_TARGET_UNMAPPED));
}
static void pilot_hex(const unsigned char *in,unsigned size,char *out) {
    static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<size;i++) {out[2*i]=digits[in[i]>>4];out[2*i+1]=digits[in[i]&15];}
    out[2*size]=0;
}
static int pilot_unhex(const char *text,size_t length,unsigned char out[PILOT_RECORD_BYTES]) {
    if(!text || length!=2*PILOT_RECORD_BYTES) return 0;
    for(unsigned i=0;i<2*PILOT_RECORD_BYTES;i++) {
        char c=text[i];unsigned d=c>='0' && c<='9'?(unsigned)(c-'0'):c>='a' && c<='f'?(unsigned)(c-'a'+10):16;
        if(d>15) return 0;
        if(i&1) out[i/2]=(unsigned char)(out[i/2]|d);else out[i/2]=(unsigned char)(d<<4);
    }
    return 1;
}
/* Deterministic test plan from (slot, tick) only: 120-tick segments of full
 * forward, forward with half yaw, half forward with half pitch, strafe with half
 * roll, then explicit neutral. Odd slots turn one way, even slots the other. */
static void pilot_script(unsigned slot,uint64_t tick,uint64_t start,pilot_record *r) {
    memset(r,0,sizeof(*r));r->mode=PILOT_MODE_ASSIST;r->capture16=(uint16_t)tick;
    int16_t full=(slot&1)?32767:-32767,half=(slot&1)?16384:-16384;
    switch(tick<start?4u:(unsigned)((tick-start)/120%5)) {
    case 0:r->q[2]=32767;break;
    case 1:r->q[2]=32767;r->q[4]=half;break;
    case 2:r->q[2]=16384;r->q[3]=half;break;
    case 3:r->q[0]=full;r->q[5]=half;break;
    default:break;
    }
    r->q[6]=r->q[2];
}
static int pilot_limits_valid(const float l[8]) {
    for(unsigned i=0;i<8;i++) if(i!=1 && !isfinite(l[i])) return 0; /* +368 is not a limit the clamp reads. */
    return l[0]>=0 && l[3]>=0 && l[4]>=0 && l[5]>=0 && l[6]>=0 && l[7]>=0;
}
/* The default control scheme with flight assist, applied to the driven ship's
 * own limits: linear x/y scale by the lateral limits, z by the forward limit
 * (or the reverse limit's magnitude), angular axis 0/1/2 = pitch/yaw/roll.
 * Boost or travel force full forward as the scheme does. Single precision. */
static float pilot_target(const pilot_record *r,const float l[8],unsigned group,unsigned axis,int *forced) {
    if(group==0) {
        static const unsigned value[3]={3,4,5},limit[3]={6,5,7};
        return pilot_axis(r->q[value[axis]])*l[limit[axis]];
    }
    if(axis==0) return pilot_axis(r->q[0])*l[3];
    if(axis==1) return pilot_axis(r->q[1])*l[4];
    float z=pilot_axis(r->q[2]);
    if(r->mode&(PILOT_MODE_BOOST|PILOT_MODE_TRAVEL)) {z=1.0f;if(forced) *forced=1;}
    return z>=0?z*l[0]:z*fabsf(l[2]);
}
static int pilot_parse_manifest(const char *text,uint64_t saved[PILOT_SLOTS],unsigned *count) {
    unsigned n=0;size_t at=0;
    if(!text) return 0;
    for(;;) {
        uint64_t value=0;
        for(unsigned i=0;i<16;i++) {
            char c=text[at+i];unsigned d=c>='0' && c<='9'?(unsigned)(c-'0'):c>='a' && c<='f'?(unsigned)(c-'a'+10):16;
            if(d>15) return 0;value=(value<<4)|d;
        }
        if(!value || n>=PILOT_SLOTS) return 0;
        for(unsigned j=0;j<n;j++) if(saved[j]==value) return 0;
        saved[n++]=value;at+=16;
        if(!text[at]) break;
        if(text[at]!=',') return 0;
        ++at;
    }
    *count=n;return 1;
}
static int pilot_config_valid(unsigned count,unsigned source,unsigned delay,uint64_t start,uint64_t stop,unsigned suppress) {
    if(!count || count>PILOT_SLOTS || suppress>1 || stop<start || stop>PILOT_TICK_MAX || start<PILOT_SUPPRESS_LEAD+2) return 0;
    if(source==PILOT_SOURCE_SCRIPT) return delay==0;
    return source==PILOT_SOURCE_WIRE && delay>=PILOT_DELAY_MIN && delay<=PILOT_DELAY_MAX && start>delay;
}
/* A wire record captured at c arrives with the permit for c+1+k (protocol 4
 * window k, 0 for protocol 3) and must be staged before its apply tick c+delay. */
static int pilot_delay_covers(unsigned source,unsigned delay,unsigned lookahead) {
    return source!=PILOT_SOURCE_WIRE || delay>=PILOT_DELAY_MIN+lookahead;
}
static int pilot_arm(const char *manifest,unsigned source,unsigned delay,uint64_t start,uint64_t stop,unsigned suppress) {
    uint64_t saved[PILOT_SLOTS];unsigned count=0;
    if(!pilot_parse_manifest(manifest,saved,&count) || !pilot_config_valid(count,source,delay,start,stop,suppress) ||
       !pilot_delay_covers(source,delay,wire_lookahead)) return 0;
    if(pilot_armed) { /* Identical rerun after a loading-UI reload. */
        int same=count==pilot_slot_count && source==pilot_source && delay==pilot_delay && start==pilot_start &&
            stop==pilot_stop && suppress==pilot_suppress;
        for(unsigned i=0;same && i<count;i++) same=saved[i]==pilot_slots[i].saved;
        return same && stats.active;
    }
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || !thunk ||
       !path_installed || !(path_options&1) || !world_identity_installed || (boot_options&REQUEST_OPTION) || request_installed ||
       InterlockedCompareExchange(&pilot_entry_owner,0,0)) return 0;
    memset(pilot_slots,0,sizeof(pilot_slots));memset(pilot_ring,0,sizeof(pilot_ring));memset(pilot_locals,0,sizeof(pilot_locals));
    for(unsigned i=0;i<count;i++) pilot_slots[i].saved=saved[i];
    pilot_slot_count=count;pilot_source=source;pilot_delay=delay;pilot_start=start;pilot_stop=stop;pilot_suppress=suppress;
    pilot_armed=1;pilot_stage="armed";return 1;
}
/* Saved IDs map one way only; resolve the single runtime ID once. */
static uint64_t pilot_reverse(uint64_t saved) {
    uint64_t runtime=0;unsigned matches=0;
    if(!world_identities || !saved) return 0;
    AcquireSRWLockShared(&world_identity_lock);
    for(unsigned i=0;i<WORLD_IDENTITY_CAPACITY;i++)
        if(world_identities[i].runtime && world_identities[i].saved==saved) {runtime=world_identities[i].runtime;++matches;}
    ReleaseSRWLockShared(&world_identity_lock);
    return matches==1?runtime:0;
}
static int pilot_resolve(pilot_slot *s,pilot_find_fn find,void *map,const void *player_object) {
    unsigned char *object,*controller=NULL,*agent=NULL,*solver=NULL;void *back=NULL;uint64_t id=0;
    s->object=s->agent=s->solver=NULL;s->resolved=s->player=0;s->found=NULL;
    if(s->dead) return 0;                                                /* the slot has no ship: its ID stays for the row */
    if(!s->reversed) {s->runtime=pilot_reverse(s->saved);s->reversed=1;} /* Identities are complete after load. */
    if(!s->runtime || !find || !map) return 0;
    object=find(map,s->runtime,4);
    s->found=object;
    if(object && s->renewed) { /* Built after a death: no saved identity and, until the seat, no controller. */
        if(!read_local(object+8,&id,8) || id!=s->runtime) return 0;
        s->object=object;s->resolved=1;s->seen=1;s->player=player_object && object==player_object;return 1;
    }
    if(!object || !read_local(object+8,&id,8) || id!=s->runtime || world_identity_lookup(id)!=s->saved ||
       !read_local(object+0x200,&controller,8) || !controller || !read_local(controller+0x28,&back,8) || back!=object ||
       !read_local(controller+0x40,&agent,8) || !agent || !read_local(agent+0xef0,&back,8) || back!=object ||
       !read_local(agent+0xf80,&solver,8)) return 0;
    s->object=object;s->agent=agent;s->solver=solver;s->resolved=1;s->seen=1;s->player=player_object && object==player_object;
    return 1;
}
/* Staging from the wire: the record's own capture tick plus the agreed delay
 * gives its single apply tick, which must still be ahead of every pass. */
static int pilot_stage_record(unsigned slot,const unsigned char bytes[PILOT_RECORD_BYTES],uint64_t now) {
    pilot_record r;
    if(!pilot_armed || pilot_source!=PILOT_SOURCE_WIRE || slot<1 || slot>pilot_slot_count || (pilot_stock_slots>>(slot-1)&1u) ||
       !pilot_decode(bytes,&r)) return 0;
    uint64_t back=(uint16_t)(now-r.capture16);
    if(back>now) return 0;
    uint64_t capture=now-back,apply=capture+pilot_delay;
    if(!capture || apply<=now || apply>=now+PILOT_RING) {InterlockedIncrement64(&pilot_late);pilot_stage="late_record";return 0;}
    if(slot==stats.slot) for(unsigned i=0;i<PILOT_LOCAL_RING;i++) {
        pilot_local *l=&pilot_locals[i];
        if(l->used && l->capture==capture && memcmp(l->bytes,bytes,PILOT_RECORD_BYTES)) {pilot_stage="echo_mismatch";return 0;}
    }
    pilot_staged *e=&pilot_ring[apply%PILOT_RING];unsigned bit=1u<<(slot-1);
    if(e->tick!=apply) {
        if(e->present && e->tick>now) {pilot_stage="ring_conflict";return 0;}
        memset(e,0,sizeof(*e));e->tick=apply;
    }
    if(e->present&bit) return !memcmp(e->bytes[slot-1],bytes,PILOT_RECORD_BYTES);
    memcpy(e->bytes[slot-1],bytes,PILOT_RECORD_BYTES);e->present|=bit;InterlockedIncrement64(&pilot_staged_count);return 1;
}
static int pilot_frame_records(uint64_t tick) {
    int window=tick>=pilot_start && tick<=pilot_stop;
    pilot_staged *e=&pilot_ring[tick%PILOT_RING];
    for(unsigned i=0;i<pilot_slot_count;i++) {
        pilot_slot *s=&pilot_slots[i];pilot_record r;s->driving=0;
        if(!window || (pilot_stock_slots>>i&1u)) continue;
        if(pilot_source==PILOT_SOURCE_SCRIPT) pilot_script(i+1,tick,pilot_start,&r);
        else {
            if(e->tick!=tick || !(e->present&(1u<<i))) {InterlockedIncrement64(&pilot_missing);pilot_stage="missing_record";return 0;}
            if(!pilot_decode(e->bytes[i],&r) || r.capture16!=(uint16_t)(tick-pilot_delay)) {pilot_stage="delay_mismatch";return 0;}
        }
        s->record=r;
        if(!s->resolved) InterlockedIncrement64(&pilot_unresolved);
        else if(!s->solver) InterlockedIncrement64(&pilot_no_solver);
        else s->driving=1;
    }
    return 1;
}
/* Parking (opt-in): this mod holds one share of the native control
 * deactivation count 2f4b7e4, exactly as the UI views do (3060e0: count+1,
 * then d9cc30). While any share is held, d9cc30 keeps the PlayerController's
 * input contexts (SPACE 8, SHIP 0xf, FLIGHT 0x10, requester 6) unrequested, so
 * the SPACE context callback d88710 drops flight control (2f4b828 = 0) and
 * queues the Steering model (d9c250), and nothing hands motion back to the
 * PlayerController until the last share is gone. Menus and the map add and
 * remove their own shares on top (count above ours: a UI hold, tolerated and
 * reported); the resume paths ab1c40/ab2600 and ActivatePlayerControls only
 * clear the DeactivatePlayerControls flag 38f0a51 and its share, which this
 * mod does not use. Every pass checks flight control and the authority
 * predicate d8dd50: only a pass in which the PlayerController would move the
 * ship fails closed. A lost share (count 0) is taken again at once. */
static int pilot_controls_take(pilot_refresh_fn refresh,uint32_t *counter,const unsigned char *contexts,int present) {
    uint32_t count=0;unsigned char requested=1;
    if(pilot_controls_owned || !refresh || !read_local(counter,&count,4) || count==UINT32_MAX) return 0;
    *(volatile uint32_t*)counter=count+1;refresh((char)(present!=0)); /* the native view pattern, once */
    if(!read_local(counter,&pilot_controls_seen,4) || pilot_controls_seen!=count+1 || !read_local(contexts,&requested,1) || requested) {
        *(volatile uint32_t*)counter=count;refresh((char)(present!=0));return 0;
    }
    pilot_controls_owned=1;pilot_controls_base=count;pilot_controls_counter_ptr=counter;pilot_controls_present=present;return 1;
}
/* One pass inside the window: 1 = parked (possibly under a UI hold), 0 = the
 * PlayerController would move the ship in this frame. */
static int pilot_controls_check(pilot_refresh_fn refresh,uint32_t *counter,const unsigned char *contexts,const unsigned char *flight,
                                pilot_authority_fn authority,void *controller,int present) {
    uint32_t count=0;unsigned char requested=0,control=1;
    if(!pilot_controls_owned || !read_local(counter,&count,4) || !read_local(contexts,&requested,1) || !read_local(flight,&control,1)) return 0;
    pilot_controls_seen=count;pilot_controls_contexts=requested;pilot_controls_flight=control;pilot_controls_present=present;
    if(control) return 0;                                      /* flight control is back: the controller owns motion */
    if(!count) {                                               /* a foreign path removed our share: take it again */
        *(volatile uint32_t*)counter=1;refresh((char)(present!=0));InterlockedIncrement64(&pilot_controls_retakes);
        if(!read_local(counter,&count,4) || !count || !read_local(contexts,&requested,1)) return 0;
        pilot_controls_seen=count;pilot_controls_contexts=requested;pilot_controls_base=0;
    }
    if(requested) {                                            /* requests back while held: remove them again */
        refresh((char)(present!=0));InterlockedIncrement64(&pilot_controls_refreshes);
        if(!read_local(contexts,&requested,1) || requested) return 0;
        pilot_controls_contexts=0;
    }
    if(count>pilot_controls_base+1) InterlockedIncrement64(&pilot_controls_ui_passes);
    if(authority && controller) {
        pilot_controls_authority=authority(controller)!=0;
        if(!pilot_controls_authority) return 0;
    }
    return 1;
}
/* Our share only; a UI still holding its own share keeps controls parked. */
static void pilot_controls_release(pilot_refresh_fn refresh,uint32_t *counter,int present) {
    uint32_t count=0;
    if(!pilot_controls_owned || !refresh || !read_local(counter,&count,4)) return;
    if(count) {*(volatile uint32_t*)counter=count-1;refresh((char)(present!=0));}
    pilot_controls_owned=0;pilot_controls_counter_ptr=NULL;
}
/* Capture side: a menu, the map or another view holds controls on this client. */
static int pilot_controls_ui_hold(void) {
    uint32_t count=0;
    return pilot_controls_owned && pilot_controls_counter_ptr && read_local(pilot_controls_counter_ptr,&count,4) && count>pilot_controls_base+1;
}
static unsigned pilot_locals_pending(uint64_t tick) {
    unsigned n=0;
    for(unsigned i=0;i<PILOT_LOCAL_RING;i++) {
        pilot_local *l=&pilot_locals[i];
        if(l->used && l->apply<=tick) l->used=0;
        n+=l->used;
    }
    return n;
}
static unsigned char *pilot_player_controller(void);
#include "lockstep_pilot_native.h"
static int pilot_install(void);
/* Main pass boundary, before the pathing pass is released: resolve every slot,
 * choose this tick's records and publish an immutable table for the workers. */
static int pilot_before_release_using(pilot_find_fn find,void *map,const void *player_object,void *controller,
                                      pilot_refresh_fn refresh,pilot_authority_fn authority,uint32_t *counter,
                                      const unsigned char *contexts,const unsigned char *flight) {
    if(!pilot_armed) return 1;
    uint64_t tick=stats.issued;
    InterlockedExchange(&pilot_live,0);
    if(!pilot_installed && !pilot_install()) {pilot_stage="install";return 0;}
    InterlockedExchange64(&pilot_frame_applied,0);InterlockedExchange64(&pilot_frame_receipt,0);
    pdeath_find=find;pdeath_map=map;pdeath_player=player_object;
    for(unsigned i=0;i<pilot_slot_count;i++) pilot_resolve(&pilot_slots[i],find,map,player_object);
    if(pilot_suppress && pnat_armed) {
        if(!pnat_pass(tick,controller,refresh,counter,contexts,flight,authority)) return 0;
    } else if(pilot_suppress) {
        int present=controller!=NULL;
        if(tick==pilot_start-PILOT_SUPPRESS_LEAD && !pilot_controls_take(refresh,counter,contexts,present)) {pilot_stage="controls_take";return 0;}
        if(tick>pilot_start-PILOT_SUPPRESS_LEAD && tick<=pilot_stop &&
           !pilot_controls_check(refresh,counter,contexts,flight,authority,controller,present)) {pilot_stage="authority_lost";return 0;}
        if(tick==pilot_stop+1) pilot_controls_release(refresh,counter,present);
    }
    if(!pilot_frame_records(tick)) return 0;
    pilot_locals_pending(tick);pilot_frame_tick=tick;++pilot_frames;pilot_stage="live";
    InterlockedExchange(&pilot_live,1); /* ReleaseSemaphore publishes it to the pass. */
    return 1;
}
/* Pathing workers: only XMM1 of an admitted Steering call can differ. */
static uintptr_t pilot_request(request_fn original,void *self,double value,int axis,double epsilon,uintptr_t caller) {
    unsigned group=0,expected=0;double chosen=value;
    /* Hot path for every other caller: no shared writes. The trampoline stays
     * resident, so a call that passed this test may finish after restoration. */
    unsigned char *native_controller=pnat_request_owner(self);
    if(native_controller) return pnat_request(original,self,value,axis,epsilon,native_controller);
    if(!InterlockedCompareExchange(&pilot_live,0,0) || !request_site(caller,&group,&expected)) return original(self,value,axis,epsilon);
    InterlockedIncrement64(&pilot_open);InterlockedIncrement64(&pilot_calls);
    if(axis==(int)expected && pilot_frame_tick==stats.issued) {
        unsigned char *solver=(unsigned char*)self-(group?0x6a00:0x12cd0);
        for(unsigned i=0;i<pilot_slot_count;i++) {
            pilot_slot *s=&pilot_slots[i];unsigned char *actual=NULL;float limits[8];int forced=0;
            if(!s->driving || s->solver!=solver) continue;
            if(!read_local(s->agent+0xf80,&actual,8) || actual!=solver) break;
            if(!read_local(s->agent+PILOT_LIMITS,limits,sizeof(limits)) || !pilot_limits_valid(limits)) {
                InterlockedIncrement64(&pilot_bad_limits);break;
            }
            chosen=(double)pilot_target(&s->record,limits,group,expected,&forced);
            uint64_t bits;memcpy(&bits,&chosen,8);
            uint64_t mix=lockstep_seed(PILOT_DOMAIN,pilot_frame_tick,s->saved,group*3+expected,bits,0);
            InterlockedIncrement64(&s->applied[group]);InterlockedIncrement64(&pilot_frame_applied);InterlockedIncrement64(&pilot_applied);
            InterlockedExchangeAdd64(&pilot_frame_receipt,(LONG64)mix);InterlockedExchangeAdd64(&pilot_receipt,(LONG64)mix);
            if(forced) InterlockedIncrement64(&pilot_forced);
            break;
        }
    }
    uintptr_t result=original(self,chosen,axis,epsilon); /* Exactly once. */
    InterlockedDecrement64(&pilot_open);return result;
}
static int pilot_pins(void) {
    return hash_ok(image+0xced70,184,"3209f7be0616de6d0ecc20ce3cfd82fd16d0524c09635d86995210598b0a5f36");
}
static int pilot_capture_pins(void) {
    if(!pilot_capture_checked) pilot_capture_checked=
        hash_ok(image+0x9fc550,277,"fca3a29a2c0ca40fafa27dac44da67e9b8bd0aa52b3d812c72b72f4ed212ca67") &&
        hash_ok(image+0xd8e5b0,4712,"2a2145135a34b685e82fdc774c6e1b255f4e99c3b72ad26eac3a8961bbfaf487") &&
        hash_ok(image+0xb09880,91,"46f11b7670b45cff7e4f4e96e0087afa3afb93d491622160803a99bddc7469d3") &&
        hash_ok(image+0xea7600,77,"929dedb5c752070cacf8e090a08a8c963e34ed124129184e1951e86d20960fa3") &&
        hash_ok(image+0xea7650,56,"d19a760d39407a1e7831ddffddd40702bb2e6ce23ab5d6ba7b6b8173a03f9c3e")?1:-1;
    return pilot_capture_checked==1;
}
static int pilot_suppress_pins(void) {
    if(!pilot_suppress_checked) pilot_suppress_checked=
        hash_ok(image+0x179760,54,"ec618b3d0aa0db0021012ccc43509c3f6c7d0cbaa234f506d7a7b0c08bd474c7") &&
        hash_ok(image+0x175a00,66,"712e88aa8e43cf4271c06f866dd64b6a0c2fbf938852efa14138ecc78fc9455e") &&
        hash_ok(image+0xd9cc30,53,"71e1d4718f219b147fe6dc46d86dd4e34956b0305aa07babd05f2a6a5c59a91c") &&
        hash_ok(image+0xd9cc30,137,"2f09f72542b0e0526df12d2df393b4c5fb2a2a909ac4539162a8125417f6ee27") && /* with its removal tail */
        hash_ok(image+0xd8d9d0,493,"01165eddbdd9bfe5cb36d6fc1ce4c84f5f59012df7f92dfd00fe813ff26482f1") &&
        hash_ok(image+0x1252850,241,"5fe3f1305271b133209201783f6adffe36e8ab1e64ccdd5eff7ecbbd9302d7f4") && /* context request removal */
        hash_ok(image+0xd8dd50,81,"d819afff6cb97c4b41d44bfb9a52a6290cf93cc53659d34ac994e1428cbc0600")?1:-1; /* authority predicate */
    return pilot_suppress_checked==1;
}
static int pilot_install(void) {
    if(pilot_installed) return 1;
    if(!pilot_armed || !stats.active || request_installed || (boot_options&REQUEST_OPTION) || !thunk ||
       GetCurrentThreadId()!=ui_thread || !request_pins() || !pilot_pins() || (pilot_suppress && !pilot_suppress_pins()) ||
       !thunk_slot_ready(REQUEST_TRAMPOLINE)) return 0;
    intptr_t distance=(intptr_t)(thunk+REQUEST_DISPATCH)-(intptr_t)(image+REQUEST_RVA+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    memcpy(&pilot_original_word,image+REQUEST_RVA,8);pilot_patched_word=pilot_original_word;
    unsigned char *p=(unsigned char*)&pilot_patched_word;p[0]=0xe9;int32_t rel=(int32_t)distance;memcpy(p+1,&rel,4);
    InterlockedExchange(&pilot_entry_owner,1); /* Route before the first patched call can arrive. */
    if(!request_swap(pilot_original_word,pilot_patched_word)) {InterlockedExchange(&pilot_entry_owner,0);return 0;}
    pilot_installed=1;pilot_restored=0;return 1;
}
static int pilot_owned(void) {return !pilot_installed || *(volatile uint64_t*)(image+REQUEST_RVA)==pilot_patched_word;}
static void pilot_restore_using(pilot_refresh_fn refresh,uint32_t *counter,int present) {
    if(!pilot_armed) return;
    pnat_restore(); /* the native pieces first: their routing and dispatch end before the share is released */
    InterlockedExchange(&pilot_live,0);
    LONG64 open=InterlockedCompareExchange64(&pilot_open,0,0);
    if(open>pilot_restore_open) pilot_restore_open=open; /* Never waits for a native call. */
    if(pilot_installed && request_swap(pilot_patched_word,pilot_original_word)) {
        pilot_installed=0;InterlockedExchange(&pilot_entry_owner,0);
    }
    pilot_controls_release(refresh,counter,present);
    pilot_restored=!pilot_installed && !pilot_controls_owned && (!pnat_armed || pnat_restored);
    if(!pilot_restored) pilot_stage="restore_refused";
}
static void pilot_restore(void) {
    if(!pilot_armed) return;
    pilot_restore_using((pilot_refresh_fn)(image+0xd9cc30),(uint32_t*)(image+0x2f4b7e4),pilot_controls_present);
}
typedef float (*pilot_value_fn)(void *,unsigned);
typedef unsigned char (*pilot_flag_fn)(void);
typedef unsigned char (*pilot_boost_fn)(void *,double);
typedef unsigned char (*pilot_travel_fn)(void *);
typedef void *(*pilot_controller_fn)(void *);
static unsigned char *pilot_player_controller(void) {
    unsigned char *controller;uintptr_t vtable=0;
    if(!pilot_capture_pins()) return NULL;
    controller=((pilot_controller_fn)(image+0x9fc550))(image+0x3d196f0);
    return controller && read_local(controller,&vtable,8) && vtable==(uintptr_t)(image+0x2c39488)?controller:NULL;
}
static int pilot_before_release(void) {
    unsigned char *controller=pilot_player_controller(),*object=NULL;
    if(controller && !read_local(controller+0x28,&object,8)) object=NULL;
    return pilot_before_release_using((pilot_find_fn)(image+0xced70),*(void**)(image+0x6cf1500),object,controller,
        (pilot_refresh_fn)(image+0xd9cc30),(pilot_authority_fn)(image+0xd8dd50),(uint32_t*)(image+0x2f4b7e4),
        image+0x2f4b829,image+0x2f4b828);
}
#include "lockstep_pilot_device.h"
/* Soft target (read only): saved ID, or the unmapped bit for an object without one. */
static void pilot_soft_target(pilot_record *r) {
    unsigned char *manager=NULL,*target=NULL;uint64_t runtime=0;
    if(read_local(image+0x6cf2210,&manager,8) && manager && read_local(manager+0x40,&target,8) && target &&
       read_local(target+0x50,&runtime,8) && runtime) {
        r->target=world_identity_lookup(runtime);
        if(!r->target) r->mode|=PILOT_MODE_TARGET_UNMAPPED;
    }
}
/* Local capture on the UI thread after this frame's native input update. It
 * reads the processed channels the control scheme reads; nothing is written. */
static int pilot_capture_native(uint64_t tick,pilot_record *r,const char **why) {
    unsigned char *controller=pilot_player_controller();float values[7];
    unsigned char triggers[4];void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);double time;
    memset(r,0,sizeof(*r));
    if(!controller) {*why="no_player_controller";return 0;}
    for(unsigned i=0;i<6;i++) values[i]=((pilot_value_fn)(image+0xd8e5b0))(controller,0x6c+i);
    if(!read_local(image+0x2f4b760,&values[6],4)) {*why="throttle";return 0;}
    for(unsigned i=0;i<7;i++) {if(!isfinite(values[i])) {*why="nonfinite";return 0;}r->q[i]=pilot_quantize(values[i]);}
    if(!slots || !slots[0] || !((unsigned char*)slots[0])[0x10]) {*why="tls";return 0;}
    memcpy(&time,(unsigned char*)slots[0]+0x300,8);
    if(((pilot_flag_fn)(image+0xb09880))()) r->mode|=PILOT_MODE_ASSIST;
    if(((pilot_boost_fn)(image+0xea7600))(controller,time)) r->mode|=PILOT_MODE_BOOST;
    if(((pilot_travel_fn)(image+0xea7650))(controller)) r->mode|=PILOT_MODE_TRAVEL;
    if(!read_local(image+0x38f0728,triggers,4)) {*why="triggers";return 0;}
    for(unsigned i=0;i<4;i++) if(triggers[i]) r->buttons|=(unsigned char)(1u<<i);
    pilot_soft_target(r);
    r->capture16=(uint16_t)tick;return 1;
}
/* Parked controls: the mod-owned device sampler fills the same channels. */
static int pilot_capture_device(uint64_t tick,pilot_record *r,const char **why) {
    if(!pilot_device_capture(tick,r)) {*why="device";return 0;}
    pilot_soft_target(r);
    r->capture16=(uint16_t)tick;return 1;
}
/* While a menu, the map or another view holds controls on this client, keys
 * belong to that UI: the record is neutral (no steering, strafe or roll; the
 * throttle setpoint is held; flight assist on) and no device is sampled. */
static void pilot_neutral_record(uint64_t tick,pilot_record *r) {
    memset(r,0,sizeof(*r));r->mode=PILOT_MODE_ASSIST;r->capture16=(uint16_t)tick;
    if(pilot_device_armed) {r->q[2]=pilot_quantize(pilot_device_mem.throttle);r->q[6]=r->q[2];}
    InterlockedIncrement64(&pilot_neutral);
}
/* Native controls (lockstep_pilot_native.h): the stock channels; on the slot
 * whose ship is the PlayerController's ship, the stock scheme's own requests
 * (autoroll, roll limits) replace q0..q5 (mode bit 0x10). While a menu, the map
 * or a view holds controls, steering, strafe and buttons are neutral and the
 * throttle stays as the stock setpoint leaves it. */
static int pilot_capture_native_controls(uint64_t tick,pilot_record *r,const char **why) {
    if(!pilot_capture_native(tick,r,why)) return 0;
    if(pnat_ui_hold()) {
        r->q[0]=r->q[1]=r->q[3]=r->q[4]=r->q[5]=0;r->buttons=0;InterlockedIncrement64(&pilot_neutral);return 1;
    }
    r->buttons|=pnat_fire_buttons(); /* refused locally, carried for native_gate_pilot_fire */
    if(pnat_capture_requests && stats.slot>=1 && stats.slot<=pilot_slot_count) {
        pilot_slot *s=&pilot_slots[stats.slot-1];float limits[8];
        if(s->resolved && s->player && s->agent && read_local(s->agent+PILOT_LIMITS,limits,sizeof(limits)) && pilot_limits_valid(limits))
            pnat_request_record(tick,limits,r);
    }
    return 1;
}
/* 113 when a native-controls invariant failed, 155 a stock-piloting one, 161 one of the stock
 * slots 2..N, 162 their presentation, 164 the UI replay, 91 for every other pilot stop. */
static unsigned pilot_stop_reason(void) {
    return pnat_failed?(pnat_mode==PNAT_MODE_STOCK?(uirep_failed?164u:pres_failed?162u:pslot_failed?161u:pdeath_failed?163u:PSTK_STOP_REASON):PNAT_STOP_REASON):91u;
}
/* The local ring keeps this client's proposed, not yet applied records with
 * their capture and apply ticks (presentation prediction input, later). */
static int pilot_capture(uint64_t tick,unsigned char out[PILOT_RECORD_BYTES],const char **why) {
    pilot_local *l=&pilot_locals[tick%PILOT_LOCAL_RING];pilot_record r;
    if(!pilot_armed || !tick) {*why="not_armed";return 0;}
    if(l->used && l->capture==tick) {memcpy(out,l->bytes,PILOT_RECORD_BYTES);return 1;}
    if(pnat_armed && pnat_mode==PNAT_MODE_NATIVE) {if(!pilot_capture_native_controls(tick,&r,why)) return 0;}
    else if(pnat_armed && stats.slot>=1 && (pilot_stock_slots>>(stats.slot-1)&1u)) {*why="stock_slot";return 0;} /* its record is the stock section */
    else if(pnat_armed?pnat_ui_hold():pilot_controls_ui_hold()) pilot_neutral_record(tick,&r);
    else if(!(pilot_device_armed?pilot_capture_device(tick,&r,why):pilot_capture_native(tick,&r,why))) return 0;
    pilot_encode(&r,out);InterlockedIncrement64(&pilot_captures);
    if(pilot_source==PILOT_SOURCE_WIRE) {
        if(l->used && l->apply>pilot_frame_tick) {*why="local_ring_full";return 0;}
        l->capture=tick;l->apply=tick+pilot_delay;l->used=1;memcpy(l->bytes,out,PILOT_RECORD_BYTES);
    }
    return 1;
}
static int pilot_pending(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"frame_tick=%llu",(unsigned long long)pilot_frame_tick);
    for(unsigned i=0;n>0 && (size_t)n<capacity && i<PILOT_LOCAL_RING;i++) {
        pilot_local *l=&pilot_locals[i];char hex[2*PILOT_RECORD_BYTES+1];
        if(!l->used || l->apply<=pilot_frame_tick) continue;
        pilot_hex(l->bytes,PILOT_RECORD_BYTES,hex);
        n+=snprintf(out+n,capacity-(size_t)n," %llu:%llu:%s",(unsigned long long)l->capture,(unsigned long long)l->apply,hex);
    }
    return n>0 && (size_t)n<capacity?n:0;
}
static int pilot_frame_text(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"PILOT_1:%llu:%lld:%016llx",(unsigned long long)pilot_frame_tick,
        (long long)InterlockedCompareExchange64(&pilot_frame_applied,0,0),
        (unsigned long long)InterlockedCompareExchange64(&pilot_frame_receipt,0,0));
    return n>0 && (size_t)n<capacity?n:0;
}
static int pilot_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"armed=%d installed=%d live=%ld source=%s delay=%u window=%llu-%llu slots=%u frames=%llu "
        "frame_tick=%llu frame_applied=%lld frame_receipt=%016llx applied=%lld receipt=%016llx calls=%lld missing=%lld late=%lld "
        "unresolved=%lld no_solver=%lld bad_limits=%lld forced=%lld staged=%lld captures=%lld local_pending=%u suppress=%u "
        "controls_owned=%d park=%u/%u/%u/%u/%u retakes=%lld refreshes=%lld ui_passes=%lld neutral=%lld "
        "open=%lld restore_open=%lld restored=%d stage=%s failures=%lld",
        pilot_armed,pilot_installed,pilot_live,pilot_source==PILOT_SOURCE_WIRE?"wire":pilot_source==PILOT_SOURCE_SCRIPT?"script":"none",
        pilot_delay,(unsigned long long)pilot_start,(unsigned long long)pilot_stop,pilot_slot_count,(unsigned long long)pilot_frames,
        (unsigned long long)pilot_frame_tick,(long long)pilot_frame_applied,(unsigned long long)pilot_frame_receipt,
        (long long)pilot_applied,(unsigned long long)pilot_receipt,(long long)pilot_calls,(long long)pilot_missing,(long long)pilot_late,
        (long long)pilot_unresolved,(long long)pilot_no_solver,(long long)pilot_bad_limits,(long long)pilot_forced,
        (long long)pilot_staged_count,(long long)pilot_captures,pilot_locals_pending(pilot_frame_tick),pilot_suppress,
        pilot_controls_owned,pilot_controls_base,pilot_controls_seen,pilot_controls_contexts,pilot_controls_flight,pilot_controls_authority,
        (long long)pilot_controls_retakes,(long long)pilot_controls_refreshes,(long long)pilot_controls_ui_passes,(long long)pilot_neutral,
        (long long)pilot_open,(long long)pilot_restore_open,pilot_restored,pilot_stage,(long long)pilot_failures);
    for(unsigned i=0;n>0 && (size_t)n<capacity && i<pilot_slot_count;i++) {
        pilot_slot *s=&pilot_slots[i];
        n+=snprintf(out+n,capacity-(size_t)n," s%u=%016llx/%u/%u/%u/%lld/%lld",i+1,(unsigned long long)s->saved,
            s->resolved,s->driving,s->player,(long long)s->applied[0],(long long)s->applied[1]);
    }
    if(n>0 && (size_t)n<capacity && pilot_device_armed) {
        int more=pilot_device_status(out+n,capacity-(size_t)n);
        if(!more) return 0;
        n+=more;
    }
    if(n>0 && (size_t)n<capacity && pnat_armed) {
        int more=pnat_status(out+n,capacity-(size_t)n);
        if(!more) return 0;
        n+=more;
    }
    return n>0 && (size_t)n<capacity?n:0;
}
#include "lockstep_pilot_camera.h"
