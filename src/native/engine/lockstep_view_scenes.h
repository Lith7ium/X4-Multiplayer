/* Physics near every view point, DEFAULT OFF: native_gate_view_scenes = true (needs
 * native_gate_physics_scenes = 'viewpoints'; a group of lockstep_region_camera.h,
 * stop reason 130).
 *
 * A zone's physics scene comes from vtable slot 943 (U::Zone 0x83fe70, U::Highway
 * 0x5f9680): A ("Main") for the player object's sector, B ("Cutscene") for the active
 * camera's, which the scenes group makes view point 2's, otherwise none. Bodies are
 * created by Destructible slot 904 from that scene, relative to its anchor (+0x40), and
 * rays and shape casts convert through the same anchor. Natively a scene's anchor is set
 * in one place: 0x97dff0, when the player camera or a cutscene camera changes zone, calls
 * 0xbd410(scene, &zone, rebase). So two things are missing away from slot 1:
 *  - Scene B has no anchor. The stand-in camera never changes parent, so +0x40 stays NULL:
 *    st-C2Ra's slot 2 ship is at 0,0,0 in scene 1 on its first tick and then in coordinates
 *    of its own zone, and bodies of two zones overlap in one frame.
 *  - A third sector has no scene at all.
 *
 * With this group:
 *  - Scenes C and D are made at the first frame like A and B (0x83f7d0: SMem::Allocate 0x50,
 *    0xba090 with the name and three times 0x10000). The constructor registers them where
 *    Scene::Step and the asset loader find every scene.
 *  - B, C and D belong to sectors, not to slots: a table of (sector, scene), kept in rc_frame
 *    in slot order. A sector keeps its scene while a view point is in it, because a body
 *    stays in the scene it was made in; view point 1's sector is A's and never in the table.
 *    A sector no view point is in gives its scene back, and that scene is free again once the
 *    bodies of the old sector have died with their attention: as soon as its PhysicsSystem
 *    holds no body (counted, early=), at the latest VSC_GRACE ticks after the release. The
 *    body counts are the lockstep's own (the physics trace's num= and bodies= agree between
 *    clients per step), so every client frees the scene at the same tick. No scene free:
 *    counted (blocked=), that view point stays without one this frame.
 *  - Each held scene is anchored like A: the zone of the first view point in its sector, by
 *    the same call with the same rule (rebase when the old and new anchor share a cluster).
 *  - Slot 943 of U::Zone and U::Highway (data CAS, no thunk) answers from the table first,
 *    then natively, and a native B that the table does not hold is no scene.
 *  - UniverseFrameUpdate's step of B (0x9fe48b) goes through a stub that also steps C and D.
 *
 * Stop: rc_restore clears vsc_active first, so a stub still in place steps nothing more and
 * slot 943 answers natively, then the entries go back by CAS. C and D stay allocated and are
 * not destroyed: bodies still in them are released by their components' own teardown through
 * record+0x18, which needs the scene alive, and 0x841b70 (universe teardown) only knows A, B
 * and Navigation. They hold memory until the process ends.
 *
 * Not here: a sector whose scene changes while a view point stays in it (slot 1 enters or
 * leaves a sector another slot is in) keeps its existing bodies in the old scene; new ones go
 * to the new scene. Traffic and highway parameters still answer for two sectors (uncovered=
 * in the status). */
#define VSC_ENTRIES 3u
#define VSC_GRACE 600u
#define VSC_BODIES 0x10000u
#define VSC_SCENE_B 0x38f0740u
#define VSC_ZONE 0x83fe70u
#define VSC_ZONE_SLOT 0x2b67df8u
#define VSC_HIGHWAY 0x5f9680u
#define VSC_HIGHWAY_SLOT 0x2b0af58u
#define VSC_CTOR 0xba090u
#define VSC_ANCHOR 0xbd410u
#define VSC_STEP 0xba600u
typedef void *(*vsc_ctor_fn)(void *,void *,uint64_t,uint32_t,uint32_t,uint32_t);
typedef void (*vsc_anchor_fn)(void *,void **,unsigned char);
typedef void (*vsc_step_fn)(void *,double);
typedef void *(*vsc_zone_fn)(void *);
typedef struct {char text[16];uint64_t size,capacity;} vsc_name_t; /* std::string, inline */
typedef struct {void *scene;void *volatile sector;void *point;uint64_t released;uint32_t view;} vsc_entry_t;
static vsc_ctor_fn vsc_ctor_native;     /* tests only: replaces 0xba090 */
static vsc_anchor_fn vsc_anchor_native; /* tests only: replaces 0xbd410 */
static vsc_step_fn vsc_step_native;     /* tests only: replaces 0xba600 */
static vsc_zone_fn vsc_zone_native,vsc_highway_native; /* tests only: replace slot 943's natives */
static vsc_entry_t vsc_entries[VSC_ENTRIES];
static void *volatile vsc_extra[2];     /* scenes C and D, for the modules that name a scene */
static volatile LONG vsc_active;
static int vsc_armed,vsc_created,vsc_installed;
static uint64_t vsc_steps,vsc_moves,vsc_rebases,vsc_assigns,vsc_releases,vsc_early,vsc_blocked_ticks;
static unsigned vsc_blocked_now;
static int vsc_read_ok(void *p,uint64_t id) {uint64_t now=0;return p && read_local((unsigned char*)p+8,&now,8) && now==id;}
/* B from the image, C and D new. Frame thread, first frame. */
static void *vsc_new_scene(void) {
    void *self=rid_allocate(0x50);if(!self) return NULL;
    vsc_name_t name;memset(&name,0,sizeof(name));memcpy(name.text,"Cutscene",8);name.size=8;name.capacity=15;
    return (vsc_ctor_native?vsc_ctor_native:(vsc_ctor_fn)(void *)(image+VSC_CTOR))(self,&name,0,VSC_BODIES,VSC_BODIES,VSC_BODIES);
}
static int vsc_create(void) {
    void *b=NULL;if(!read_local(image+VSC_SCENE_B,&b,8) || !b) return 0;
    void *c=vsc_new_scene(),*d=c?vsc_new_scene():NULL;if(!c || !d) return 0;
    memset(vsc_entries,0,sizeof(vsc_entries));
    vsc_entries[0].scene=b;vsc_entries[1].scene=c;vsc_entries[2].scene=d;vsc_extra[0]=c;vsc_extra[1]=d;
    vsc_created=1;return 1;
}
/* No body left in a scene: its PhysicsSystem (scene+0x20) counts none (+0x28) and has none in its
 * active list (+0x680), the physics trace's num= and bodies=. Unreadable is not empty. */
static int vsc_empty(void *scene) {
    unsigned char *system=NULL;uint32_t num=1;uint64_t active=1;
    return scene && read_local((unsigned char*)scene+0x20,&system,8) && system &&
           read_local(system+0x28,&num,4) && read_local(system+0x680,&active,8) && !num && !active;
}
/* 0 held or still waiting, 1 free (never used or the grace over), 2 free early (released and empty). */
static int vsc_free(const vsc_entry_t *e,uint64_t tick) {
    if(e->sector) return 0;
    if(!e->released || tick>=e->released+VSC_GRACE) return 1;
    return vsc_empty(e->scene)?2:0;
}
/* The table for this frame, from the latched view points; rc_view_sector58 is view point 1's sector (A's). */
static void vsc_latch(uint64_t tick) {
    void *const home=rc_view_sector58;void *points[VIEW_MAX],*sectors[VIEW_MAX];
    for(unsigned k=0;k<VSC_ENTRIES;k++) vsc_entries[k].point=NULL;
    /* Who still holds a scene: the first view point in slot order. The others in a held sector need nothing more. */
    for(unsigned i=1;i<view_count && i<VIEW_MAX;i++) {
        points[i]=view_points[i];sectors[i]=vsc_read_ok(points[i],view_ids[i])?rc_ancestor(points[i],0x58):NULL;
        if(sectors[i]==home) sectors[i]=NULL;
        if(!sectors[i]) continue;
        for(unsigned k=0;k<VSC_ENTRIES;k++) if(vsc_entries[k].sector==sectors[i]) {
            if(!vsc_entries[k].point) {vsc_entries[k].point=points[i];vsc_entries[k].view=i;}
            sectors[i]=NULL;break;
        }
    }
    /* Nobody in it any more: back to the pool once empty, after the grace at the latest. */
    for(unsigned k=0;k<VSC_ENTRIES;k++) if(vsc_entries[k].sector && !vsc_entries[k].point) {
        vsc_entries[k].sector=NULL;vsc_entries[k].released=tick;++vsc_releases;
    }
    /* New sectors, in slot order, to the first free scene (B first). */
    vsc_blocked_now=0;
    for(unsigned i=1;i<view_count && i<VIEW_MAX;i++) {
        if(!sectors[i]) continue;
        int held=0;
        for(unsigned k=0;k<VSC_ENTRIES;k++) held|=vsc_entries[k].sector==sectors[i];
        if(held) continue;
        unsigned k=0;int free=0;
        while(k<VSC_ENTRIES && !(free=vsc_free(&vsc_entries[k],tick))) ++k;
        if(k==VSC_ENTRIES) {++vsc_blocked_now;continue;}
        if(free==2) ++vsc_early;
        vsc_entries[k].point=points[i];vsc_entries[k].view=i;vsc_entries[k].released=0;MemoryBarrier();vsc_entries[k].sector=sectors[i];++vsc_assigns;
    }
    if(vsc_blocked_now) ++vsc_blocked_ticks;
}
/* The native anchor call for one held scene, when its view point's zone is not the anchor any more. */
static void vsc_anchor(vsc_entry_t *e) {
    unsigned char *scene=e->scene,*zone=rc_ancestor(e->point,0x6d);if(!scene || !zone) return;
    unsigned char *now=*(unsigned char *volatile *)(scene+0x40);if(now==zone) return;
    const unsigned char rebase=now && rc_ancestor(now,0xf)==rc_ancestor(zone,0xf);
    /* As 0x97e4xx holds the zone across the call; the scene takes its own reference, so this one is never the last. */
    volatile LONG *state=(volatile LONG *)(zone+0x44),*count=(volatile LONG *)(zone+0x40);
    InterlockedCompareExchange(state,2,1);InterlockedIncrement(count);
    void *held=zone;(vsc_anchor_native?vsc_anchor_native:(vsc_anchor_fn)(void *)(image+VSC_ANCHOR))(scene,&held,rebase);
    if(!InterlockedDecrement(count)) rc_fail("scene_anchor_ref");
    ++vsc_moves;vsc_rebases+=rebase;
}
/* rc_frame, after view point 1 is latched. */
static int vsc_frame(uint64_t tick) {
    if(!vsc_created) {
        if(!vsc_create()) return 0;
        InterlockedExchange(&vsc_active,1);
    }
    vsc_latch(tick);
    for(unsigned k=0;k<VSC_ENTRIES;k++) if(vsc_entries[k].sector && vsc_entries[k].point) vsc_anchor(&vsc_entries[k]);
    return 1;
}
/* Slot 943: the table, then the native answer; a native B the table does not hold is no scene. */
static void *vsc_answer(void *zone,vsc_zone_fn native) {
    if(!InterlockedCompareExchange(&vsc_active,0,0)) return native(zone);
    void *sector=rc_ancestor(zone,0x58);
    for(unsigned k=0;sector && k<VSC_ENTRIES;k++) if(vsc_entries[k].sector==sector) return vsc_entries[k].scene;
    void *scene=native(zone);
    return scene==vsc_entries[0].scene?NULL:scene;
}
static void *vsc_zone_scene(void *zone) {return vsc_answer(zone,vsc_zone_native?vsc_zone_native:(vsc_zone_fn)(void *)(image+VSC_ZONE));}
static void *vsc_highway_scene(void *zone) {return vsc_answer(zone,vsc_highway_native?vsc_highway_native:(vsc_zone_fn)(void *)(image+VSC_HIGHWAY));}
/* UniverseFrameUpdate's step of B, then C and D. */
static void vsc_step(void *scene,double time) {
    const vsc_step_fn step=vsc_step_native?vsc_step_native:(vsc_step_fn)(void *)(image+VSC_STEP);
    InterlockedIncrement64(&rc_stub_calls[RC_VIEW_STEP_CALL]);
    step(scene,time);
    if(!InterlockedCompareExchange(&vsc_active,0,0)) return;
    for(unsigned i=0;i<2u;i++) if(vsc_extra[i]) {step(vsc_extra[i],time);++vsc_steps;}
}
static int vsc_swap(unsigned slot,void *expected,void *value) {
    DWORD old,ignored;if(!VirtualProtect(image+slot,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void *volatile *)(image+slot),value,expected);
    VirtualProtect(image+slot,8,old,&ignored);return actual==expected;
}
static int vsc_slots_native(void) {
    return *(void *volatile *)(image+VSC_ZONE_SLOT)==(void *)(image+VSC_ZONE) && *(void *volatile *)(image+VSC_HIGHWAY_SLOT)==(void *)(image+VSC_HIGHWAY);
}
static int vsc_install(void) {
    if(!vsc_swap(VSC_ZONE_SLOT,image+VSC_ZONE,(void *)vsc_zone_scene)) return 0;
    if(!vsc_swap(VSC_HIGHWAY_SLOT,image+VSC_HIGHWAY,(void *)vsc_highway_scene)) {vsc_swap(VSC_ZONE_SLOT,(void *)vsc_zone_scene,image+VSC_ZONE);return 0;}
    vsc_installed=1;return 1;
}
static int vsc_owned(void) {
    return !vsc_installed || (*(void *volatile *)(image+VSC_ZONE_SLOT)==(void *)vsc_zone_scene && *(void *volatile *)(image+VSC_HIGHWAY_SLOT)==(void *)vsc_highway_scene);
}
static int vsc_remove(void) {
    if(!vsc_installed) return 1;
    const int zone=vsc_swap(VSC_ZONE_SLOT,(void *)vsc_zone_scene,image+VSC_ZONE),highway=vsc_swap(VSC_HIGHWAY_SLOT,(void *)vsc_highway_scene,image+VSC_HIGHWAY);
    if(zone && highway) vsc_installed=0;
    return zone && highway;
}
static void vsc_status(char *out,size_t capacity) {
    uint64_t anchors[VSC_ENTRIES];
    for(unsigned k=0;k<VSC_ENTRIES;k++) {
        void *zone=NULL;anchors[k]=0;
        if(vsc_entries[k].scene && read_local((unsigned char*)vsc_entries[k].scene+0x40,&zone,8) && zone) read_local((unsigned char*)zone+8,&anchors[k],8);
    }
    snprintf(out,capacity,"%d,slots:%u/%u/%u,anchors:%llx/%llx/%llx,steps:%llu,moves:%llu,rebases:%llu,assigns:%llu,releases:%llu,early:%llu,blocked:%llu/%u",(int)vsc_active,
        vsc_entries[0].sector?vsc_entries[0].view+1u:0u,vsc_entries[1].sector?vsc_entries[1].view+1u:0u,vsc_entries[2].sector?vsc_entries[2].view+1u:0u,
        (unsigned long long)anchors[0],(unsigned long long)anchors[1],(unsigned long long)anchors[2],(unsigned long long)vsc_steps,(unsigned long long)vsc_moves,
        (unsigned long long)vsc_rebases,(unsigned long long)vsc_assigns,(unsigned long long)vsc_releases,(unsigned long long)vsc_early,(unsigned long long)vsc_blocked_ticks,vsc_blocked_now);
}
