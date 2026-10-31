/* Census diagnostic, DEFAULT OFF, read-only: native_gate_nav_octree_sites = true
 * (needs native_gate_nav_octree 'deadline' or 'agreed': it counts in that
 * module's invalidation stubs, so it adds no patch of its own).
 * st3-F54a/F55a: the invalidation count of the nav octree status
 * (invalidations=, every call of 0x75b130 through its five call sites with an
 * object ID) differs by one between clients from the first admitted tick on
 * (F54a 269897/269897/269898 at tick 1, F55a 269900/269899/269900) and the
 * ~374 invalidations of the run itself agree: the extra call is made while the
 * snapshot loads (with the same 1365963 AutoIDMap assignments on every
 * client, so no object more), and it dropped no record (requested=0 at tick
 * 1; 76 records assigned, agreed and shown alike, no invalidated lookup row).
 * The five sites (each calls 0x75b130, which drops the object's octrees and
 * pending requests and, outside app state 4, adds it to the invalidated set):
 * - 0x7450d3 U::Positional::vfunc586 (also reached from Destructible,
 *   UIElement, 0xaf0140 and 0xaf05f0), when slots 874 and 894 allow it;
 * - 0x745255 U::Positional::vfunc587 (from Galaxy, Highway, Zone, Object,
 *   Player, Entity, Bullet, Region, Sector, HoloMap ...), the same test;
 * - 0x753510 U::Positional::vfunc701, the attention change: U::Component::
 *   SetAttention 0x3d5660 writes the level +0x1ec and calls slot 701 with
 *   (old level, was active, new level, is active); leaving the active range
 *   invalidates here, entering it (level 1) asks for the octrees through
 *   0x75b280 (0x75356e). Attention follows the local player and camera
 *   natively (PeriodicAttentionRefresh 0x6d1ff0, Ship slots 366/367,
 *   Zone slot 674, ConnectTo ...), so this is a per-client input before the
 *   gate's attention modules align it;
 * - the tail jump 0x75b2f2 of the creation request 0x75b280 (from 0x75356e,
 *   0x51c25a, 0x51c8a6), for an object that slots 688/894/895 say gets no
 *   octree;
 * - 0x784691 U::Sector's destructor (secondary vtable slot 2).
 * This module counts every call by (phase, site, return address - for the tail
 * jump the caller of 0x75b280 -, object class = vtable RVA, thread class) with a count, calls without an object ID, the last object and
 * the first and last tick, in a 512-entry table under an SRW lock (any
 * thread). Phases: menu until the load is observed (boot phases 0 and 1: the
 * main menu's own universe still runs, on wall-clock time), load until the
 * first admitted tick, run after it. The probe logs the
 * table (NAV_OCTREE_SITE rows: last= keyed when it has a portable ID) at the
 * first admitted tick and at the stop, and NAV_OCTREE_SITES per tick (per
 * site totals of each phase), so the entry that differs between clients
 * names the site, the class and the thread of the extra call. Nothing changes.
 * st3-F59a (the first run with it): the only difference was from=519696 (the
 * tail jump reached from U::Destructible::vfunc148 through 0x51c790, which
 * re-asks a ship's octrees when a nav-relevant component is connected under
 * it) on ShipLarge runtime 0x47 (22 or 23 calls) and once on ShipMedium
 * 0x3b. Those runtime IDs are far below the save's (its objects got
 * 0xf8xx..., and different ones per client, so the clients had made
 * different numbers of objects before the load): they are the main menu's
 * background universe (with sector 0x22 and its five siblings, whose
 * destructor invalidates at the load), which runs on wall-clock time between
 * the arming and the load and is destroyed by it. No record existed then,
 * and the IDs are not given again, so nothing after the load can see them;
 * menu rows are expected to differ, load and run rows must not. */
#define NOS_ENTRIES 512u
#define NOS_SITES 10u /* noct_site_of: 0..3 lookups, 4..7 the four call sites, 8 the tail jump, 9 other */
enum {NOS_MENU,NOS_LOAD,NOS_RUN,NOS_PHASES};
typedef struct {uint64_t count,noid,last,first_tick,last_tick;uint32_t klass,ret;uint8_t phase,site,thread,used;} nos_entry_t;
static nos_entry_t nos_entries[NOS_ENTRIES];
static uint64_t nos_totals[NOS_PHASES][NOS_SITES];
static unsigned nos_used;
static uint64_t nos_overflow;
static SRWLOCK nos_lock=SRWLOCK_INIT;
static volatile LONG nos_active;
static int nos_armed;
static const char *const nos_phase_names[NOS_PHASES]={"menu","load","run"};
static const char *const nos_site_names[NOS_SITES]={"446a8b","53f129","53fcfa","75b0db","7450d3","745255","753510","784691","tail","other"};
/* Any thread, before the native invalidation: one call of the given site (return address ret) for object. */
static void nos_note(unsigned site,const void *ret,const void *object) {
    if(!InterlockedCompareExchange(&nos_active,0,0)) return;
    DWORD error=GetLastError();
    uint64_t id=0,tick=stats.issued;uintptr_t vtable=0;
    if(object) {read_local((const unsigned char*)object+8,&id,8);read_local(object,&vtable,8);}
    uint32_t klass=vtable>=(uintptr_t)image && vtable-(uintptr_t)image<0x6fa9000u?(uint32_t)(vtable-(uintptr_t)image):0xffffffffu;
    uint32_t from=(uintptr_t)ret>=(uintptr_t)image && (uintptr_t)ret-(uintptr_t)image<0x6fa9000u?(uint32_t)((uintptr_t)ret-(uintptr_t)image):0xffffffffu;
    uint8_t phase=tick && boot_phase==3?NOS_RUN:boot_mode && boot_phase<2?NOS_MENU:NOS_LOAD,thread=(uint8_t)path_thread_class();
    if(site>=NOS_SITES) site=NOS_SITES-1;
    unsigned h=(unsigned)(lockstep_mix(((uint64_t)klass<<32)^((uint64_t)from<<8)^((uint64_t)site<<4)^((uint64_t)thread<<1)^phase)&(NOS_ENTRIES-1));
    AcquireSRWLockExclusive(&nos_lock);
    ++nos_totals[phase][site];
    nos_entry_t *e=NULL;
    for(unsigned n=0;n<NOS_ENTRIES;n++,h=(h+1)&(NOS_ENTRIES-1)) {
        nos_entry_t *c=&nos_entries[h];
        if(c->used && c->klass==klass && c->ret==from && c->site==site && c->thread==thread && c->phase==phase) {e=c;break;}
        if(!c->used) {*c=(nos_entry_t){0,0,0,tick,tick,klass,from,phase,(uint8_t)site,thread,1};++nos_used;e=c;break;}
    }
    if(e) {++e->count;if(!id) ++e->noid;else e->last=id;e->last_tick=tick;} else ++nos_overflow;
    ReleaseSRWLockExclusive(&nos_lock);
    SetLastError(error);
}
/* Before the load, on the MoveWorker, after the nav octree module installed its invalidation stubs. */
static int nos_arm(int stubs) {
    if(nos_armed) return stats.active!=0;
    if(!stubs || !stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    AcquireSRWLockExclusive(&nos_lock);
    memset(nos_entries,0,sizeof(nos_entries));memset(nos_totals,0,sizeof(nos_totals));nos_used=0;nos_overflow=0;
    ReleaseSRWLockExclusive(&nos_lock);
    nos_armed=1;InterlockedExchange(&nos_active,1);return 1;
}
static void nos_restore(void) {InterlockedExchange(&nos_active,0);}
/* Table slot index (0..511): the entry, or "index=N free" for an unused slot; 0 past the table. */
static int nos_row(uint64_t index,char *out,size_t cap) {
    if(index>=NOS_ENTRIES) return 0;
    AcquireSRWLockShared(&nos_lock);nos_entry_t e=nos_entries[index];ReleaseSRWLockShared(&nos_lock);
    int n;
    if(!e.used) n=snprintf(out,cap,"index=%llu free",(unsigned long long)index);
    else {
        unsigned klass=WORLD_KEY_NONE;uint64_t key=e.last?world_identity_key(e.last,&klass):0;
        n=snprintf(out,cap,"index=%llu phase=%s site=%s from=%x class=%08x thread=%s count=%llu noid=%llu last=%c:%016llx runtime=%016llx first=%llu last_tick=%llu",
            (unsigned long long)index,nos_phase_names[e.phase<NOS_PHASES?e.phase:NOS_LOAD],nos_site_names[e.site<NOS_SITES?e.site:NOS_SITES-1],e.ret,e.klass,path_thread_name(e.thread),
            (unsigned long long)e.count,(unsigned long long)e.noid,key?(klass==WORLD_KEY_CREATED?'c':'s'):'-',(unsigned long long)key,
            (unsigned long long)e.last,(unsigned long long)e.first_tick,(unsigned long long)e.last_tick);
    }
    return n>0 && (size_t)n<cap?n:0;
}
static int nos_status(char *out,size_t cap) {
    uint64_t totals[NOS_PHASES][NOS_SITES];unsigned used;uint64_t overflow;
    AcquireSRWLockShared(&nos_lock);memcpy(totals,nos_totals,sizeof(totals));used=nos_used;overflow=nos_overflow;ReleaseSRWLockShared(&nos_lock);
    int n=snprintf(out,cap,"armed=%d active=%ld entries=%u overflow=%llu",nos_armed,nos_active,used,(unsigned long long)overflow);
    for(unsigned p=0;p<NOS_PHASES && n>0 && (size_t)n<cap;p++) {
        n+=snprintf(out+n,cap-(size_t)n," %s=",nos_phase_names[p]);
        for(unsigned s=4;s<NOS_SITES && n>0 && (size_t)n<cap;s++)
            n+=snprintf(out+n,cap-(size_t)n,"%s%s:%llu",s==4?"":",",nos_site_names[s],(unsigned long long)totals[p][s]);
    }
    return n>0 && (size_t)n<cap?n:0;
}
