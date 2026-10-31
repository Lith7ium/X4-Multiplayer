#include "lockstep_gate.c"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include "test_lockstep_world_queue.h"
#include "test_lockstep_world_wait.h"
#include "test_lockstep_ai_wait_rng.h"
#include "test_lockstep_world_action.h"
#include "test_lockstep_world_trace.h"
#include "test_lockstep_npc_deadline.h"
#include "test_lockstep_path_determinism.h"
#include "test_lockstep_travel_determinism.h"
#include "test_lockstep_travel_snapshot.h"
#include "test_lockstep_path_perf.h"
#include "test_lockstep_ai_schedule.h"
#include "test_lockstep_event_order.h"
#include "test_lockstep_gravidar_watch.h"
#include "test_lockstep_speech_time.h"
#include "test_lockstep_render_rng.h"
#include "test_lockstep_relay_host.h"
#include "test_lockstep_digest.h"
#include "test_lockstep_sample.h"
#include "test_lockstep_world_reader.h"
#include "test_lockstep_bullet_cadence.h"
#include "test_lockstep_speak_timer.h"
#include "test_lockstep_cutscene_ready.h"
#include "test_lockstep_shield_canonical.h"
#include "test_lockstep_nav_cadence.h"
#include "test_lockstep_nav_player.h"
#include "test_lockstep_ship_sound.h"
#include "test_lockstep_draw_watch.h"
#include "test_lockstep_player_timer.h"
#include "test_lockstep_warp_rng.h"
#include "test_lockstep_transition_rows.h"
#include "test_lockstep_tempzone_order.h"
#include "test_lockstep_nav_octree_sites.h"
#include "test_lockstep_gravidar_posts.h"
#include "test_lockstep_gravidar_wide.h"
#include "test_lockstep_command_order.h"
#include "test_lockstep_view_points.h"
#include "test_lockstep_camera_offset.h"
#include "test_lockstep_mass_traffic.h"
#include "test_lockstep_gravidar_digest.h"
#include "test_lockstep_attention_all.h"
#include "test_lockstep_view_scenes.h"
#include "test_lockstep_region_camera.h"
#include "test_lockstep_weapon_fire.h"
#include "test_lockstep_engine_rng.h"
#include "test_lockstep_portable_math.h"
#include "test_lockstep_exact_reciprocal.h"
#include "test_lockstep_fp_env.h"
#include "test_lockstep_tick.h"
#include "test_lockstep_path_budget.h"
#include "test_lockstep_decimal.h"
#include "test_lockstep_motion_watch.h"
#include "test_lockstep_pilot_inputs.h"
#include "test_lockstep_pilot_native.h"
#include "test_lockstep_pilot_stock.h"
#include "test_lockstep_pilot_slots.h"
#include "test_lockstep_ui_replay.h"
#include "test_lockstep_pilot_device.h"
#include "test_lockstep_pilot_predict.h"
#include "test_lockstep_hit_claims.h"
#include "test_lockstep_hit_native.h"
#include "test_lockstep_pilot_fire.h"
#include "test_lockstep_virtual_players.h"
static uint64_t test_rng;
static unsigned test_calls,test_cheap,test_action_throw,test_action_stop;
static void test_action_native_maybe_throw(void) {
    if(!test_action_throw)return;
    if(test_action_stop)stats.active=0;
    RaiseException(0xe04d5044,0,0,NULL);
}
static void *test_flight_self;
static unsigned test_random_calls;
static void *test_radar_context,*test_radar_source;
static void test_radar_original(void *context,void *source,unsigned flags) {
    assert(context==test_radar_context && source==test_radar_source && flags==1234);++test_calls;
}
static HANDLE test_gravidar_thread;
static DWORD WINAPI test_gravidar_publish(LPVOID worker) {
    unsigned char *w=worker;Sleep(20);
    EnterCriticalSection((CRITICAL_SECTION*)(w+0x6960));w[0x6988]=1;
    LeaveCriticalSection((CRITICAL_SECTION*)(w+0x6960));return 0;
}
static void test_gravidar_original(void *worker,unsigned char reset) {
    assert(!reset && gravidar_in_frame);++test_calls;unsigned char *w=worker;
    EnterCriticalSection((CRITICAL_SECTION*)(w+0x6960));w[0x6988]=0;
    LeaveCriticalSection((CRITICAL_SECTION*)(w+0x6960));
    test_gravidar_thread=CreateThread(NULL,0,test_gravidar_publish,w,0,NULL);assert(test_gravidar_thread);
}
static DWORD WINAPI test_spline_publish(LPVOID request) {
    Sleep(20);
    EnterCriticalSection((CRITICAL_SECTION*)(image+0x3d74938));
    *(uint32_t*)((unsigned char*)request+0xac)=0x01000001;
    *(uint32_t*)((unsigned char*)request+0xa8)=1;
    LeaveCriticalSection((CRITICAL_SECTION*)(image+0x3d74938));
    return 0;
}
static float test_flight_random(uint64_t *state,float limit) {
    uint32_t bits;memcpy(&bits,&limit,4);
    assert(bits==(test_random_calls%2 ? 0x40490fdbu : 0x40c90fdbu));
    ++test_random_calls;*state=*state*6364136223846793005ULL+1442695040888963407ULL;
    return (float)(*state&65535)/65536.0f*limit;
}
static void test_flight_original(void *self,void *slot,float dt,double time,unsigned char cheap,unsigned char player) {
    assert(self==test_flight_self && dt==1.0f/60.0f && time==7.0 && player==0);
    ++test_calls;test_cheap=cheap;
    test_rng=test_rng*6364136223846793005ULL+1442695040888963407ULL;
    *(uint64_t*)slot=test_rng;
}
static void test_ai_original(void *output) {
    ++test_calls;test_rng=test_rng*6364136223846793005ULL+1442695040888963407ULL;
    *(uint64_t*)output=test_rng;
}
static void test_gravidar_unit_original(void *unit,void *queue,void *result) {
    assert(queue==&test_calls && result==&test_rng);test_ai_original(unit);
}
static uintptr_t test_interrupt_original(void *exec,void *program,uint64_t begin,uint64_t end,unsigned char mode) {
    assert(exec && program==&test_calls && begin==11 && end==22 && mode==1);++test_calls;
    test_rng+=123;return 0x1234567812345678ULL;
}
static void test_interrupt_step_original(void *exec) {
    ++test_calls;test_action_native_maybe_throw();++*(uint64_t*)((unsigned char*)exec+0x18);test_rng+=7;
    *(uint16_t*)((unsigned char*)exec+0x30)=1;
}
static void *test_nested_child;
static void test_nested_original(void *node,void *exec,void *frame) {
    assert(frame==(unsigned char*)exec+0x38);++test_calls;test_action_native_maybe_throw();test_rng+=11;
    if(test_nested_child) {
        void *child=test_nested_child;test_nested_child=NULL;
        assert(nested_current && nested_current->depth==1);
        nested_action_observe(test_nested_original,node,child,(unsigned char*)child+0x38,&test_rng);
        assert(nested_current && nested_current->depth==1);
    }
    *(uint64_t*)((unsigned char*)exec+0x18)=2;
}
/* A frame the observer refuses to vouch for still reaches the native action. */
static void test_nested_any_frame(void *node,void *exec,void *frame) {(void)node;(void)exec;(void)frame;++test_calls;}
static void test_npc_string(unsigned char *address,const char *value) {
    size_t length=strlen(value);assert(length<16);memset(address,0,32);
    memcpy(address,value,length);*(uint64_t*)(address+16)=length;*(uint64_t*)(address+24)=15;
}
static uint64_t test_npc_draw;
static int test_npc_recurse;
static void test_npc_exec_original(void *exec) {
    ++test_calls;assert(exec);test_npc_draw=test_rng;test_rng+=11;
}
static void test_npc_exec_recursive(void *exec) {
    ++test_calls;assert(npc_exec_current && npc_exec_current->depth==1);test_rng+=7;
    if(test_npc_recurse) {test_npc_recurse=0;npc_exec_observe(test_npc_exec_original,exec,&test_rng);}
    assert(npc_exec_current && npc_exec_current->depth==1);test_rng+=9;
}
#include "test_lockstep_npc_trigger.h"
#include "test_lockstep_npc_provenance.h"
#include "test_lockstep_npc_pilot.h"
#include "test_lockstep_npc_early.h"
#include "test_lockstep_interrupt_origin.h"
static void test_npc_names(void) {
    unsigned char exec[0x50]={0},cue[0x180]={0},definition[0x180]={0},module[0x100]={0};
    unsigned char nodes[3][0x60]={{0}};char name[256];
    *(uint32_t*)(exec+0x38)=0x5d;*(void**)(exec+0x40)=cue+0xb0;
    *(void**)(cue+0x160)=definition;*(void**)(definition+0x150)=module;
    test_npc_string(module+0x18,"NPC_UseCases");test_npc_string(nodes[1]+0x20,"UseCase");
    *(void**)(module+0x80)=nodes[0];*(void**)(module+0x68)=nodes[1];
    *(void**)nodes[0]=nodes[1];*(void**)nodes[1]=module+0x60;
    *(void**)(nodes[1]+8)=nodes[0];*(void**)(nodes[1]+0x10)=nodes[2];*(void**)nodes[2]=nodes[1];
    *(void**)(nodes[1]+0x40)=definition;
    assert(npc_cue_name(exec,name)==1 && !strcmp(name,"NPC_UseCases.UseCase"));
    unsigned char action[40]={0};uintptr_t entries[]={(uintptr_t)action};uintptr_t program[]={(uintptr_t)entries,(uintptr_t)(entries+1)};
    *(void**)action=image+0x1000;*(void**)(exec+0x10)=program;
    test_calls=0;test_rng=999;npc_exec_observe(test_npc_exec_original,exec,&test_rng);
    uint64_t selected=test_npc_draw;
    assert(test_calls==1 && test_rng==999 && npc_exec_calls==1 && npc_exec_count==1 && !interrupt_failures);
    assert(npc_exec_samples[0].before==999 && npc_exec_samples[0].scoped==selected && npc_exec_samples[0].after==selected+11 &&
           npc_exec_samples[0].restored==999 && npc_exec_samples[0].call==1);
    memset(npc_exec_keys,0,sizeof(npc_exec_keys));test_rng=123;
    npc_exec_observe(test_npc_exec_original,exec,&test_rng);
    assert(test_rng==123 && test_npc_draw==selected); /* Caller RNG and addresses are not seed inputs. */
    test_npc_recurse=1;test_calls=0;npc_exec_observe(test_npc_exec_recursive,exec,&test_rng);
    assert(test_calls==2 && test_rng==123 && !npc_exec_current && npc_exec_samples[2].call==2 && npc_exec_samples[3].call==3);
    assert(npc_exec_samples[3].parent==npc_exec_samples[2].ordinal && npc_exec_samples[3].depth==2 &&
           npc_exec_samples[2].after==npc_exec_samples[2].scoped+16);
    npc_exec_count=NPC_EXEC_LIMIT;npc_exec_observe(test_npc_exec_original,exec,&test_rng);
    assert(test_calls==3 && interrupt_failures==1 && npc_exec_calls==4);
    npc_exec_count=0;npc_exec_calls=0;interrupt_failures=0;memset(npc_exec_keys,0,sizeof(npc_exec_keys));
    test_npc_trigger_legacy_only(exec);
    test_npc_trigger_identity(exec,action);
    test_npc_provenance(exec,cue,action);
    memset(npc_names,0,sizeof(npc_names));*(void**)(nodes[1]+0x40)=NULL;*(void**)(nodes[2]+0x40)=definition;
    test_npc_string(nodes[2]+0x20,"child name");
    assert(npc_cue_name(exec,name)==1 && !strcmp(name,"NPC_UseCases.child%20name"));
    memset(npc_names,0,sizeof(npc_names));*(void**)(nodes[2]+0x40)=NULL;
    assert(npc_cue_name(exec,name)==0); /* A missing tree entry is not a known different module. */
    *(void**)(nodes[0]+0x10)=nodes[0];*(void**)(nodes[0]+8)=nodes[0];
    assert(npc_cue_name(exec,name)==0); /* Cyclic tree terminates within the read budget. */
    test_npc_string(module+0x18,"Other");assert(npc_cue_name(exec,name)==2);
    *(uint64_t*)(module+0x28)=96;assert(npc_cue_name(exec,name)==0);
    *(uint32_t*)(exec+0x38)=0x66;assert(npc_cue_name(exec,name)==2);
    memset(npc_names,0,sizeof(npc_names));
}
static void test_npc_speak_original(void *self) {
    ++test_calls;test_rng+=11;unsigned char *s=self;
    *(uint32_t*)(s+0x10)=5;*(uint32_t*)(s+0x28)=123;*(uint32_t*)(s+0x2c)=456;
    *(double*)(s+0x18)=1.5;*(double*)(s+0x20)=2.0;
}
static unsigned test_enqueue_calls,test_enqueue_immediate,test_enqueue_nested,test_enqueue_accept=1;
static unsigned test_finish_calls;
static void test_npc_finish_original(void *self,unsigned char interrupted) {
    assert(self && interrupted==1);++test_finish_calls;test_rng+=13;
}
static unsigned char test_npc_enqueue_original(void *self,void *vector,int priority,unsigned char interrupt,void *recipient,void *source,unsigned flags,void *text) {
    assert(vector && priority==-7 && interrupt==1 && !recipient && !source && flags==6 && text==&test_calls);
    ++test_enqueue_calls;test_rng+=3;
    if(test_enqueue_nested) {
        test_enqueue_nested=0;unsigned saved_accept=test_enqueue_accept;test_enqueue_accept=1;
        assert(npc_enqueue_observe(test_npc_enqueue_original,self,vector,priority,interrupt,recipient,source,flags,text,&test_rng));
        test_enqueue_accept=saved_accept;
    }
    if(test_enqueue_immediate) npc_speak_observe(test_npc_speak_original,self,&test_rng,1234);
    return (unsigned char)test_enqueue_accept;
}
static void test_npc_speech(void) {
    unsigned char speaker[0xa0]={0};*(uint64_t*)(speaker+8)=123;
    unsigned char line[0x30]={0};*(uint32_t*)line=123;*(uint32_t*)(line+4)=456;
    uintptr_t vector[3]={(uintptr_t)line,(uintptr_t)line+sizeof(line),(uintptr_t)line+sizeof(line)};
    npc_action_sample action={0};action.state.ordinal=9;action.state.action=0xbf1e80;npc_current=npc_speech_owner=&action;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && world_identity_record(123,456));
    test_calls=0;test_rng=100;test_enqueue_immediate=1;
    assert(npc_enqueue_observe(test_npc_enqueue_original,speaker,vector,-7,1,NULL,NULL,6,&test_calls,&test_rng)==1);
    assert(test_enqueue_calls==1 && test_calls==1 && test_rng==114 && npc_speak_count==1 && npc_speak_calls==1 && !interrupt_failures);
    assert(npc_enqueue_count==1 && npc_enqueues[0].lines[0][0]==123 && npc_enqueues[0].lines[0][1]==456 && npc_enqueues[0].result==1);
    assert(npc_speaks[0].saved==456 && npc_speaks[0].action==9 && npc_speaks[0].b.page==123 && npc_speaks[0].b.line==456 &&
           npc_speaks[0].a.state==0 && npc_speaks[0].b.state==5 && npc_speaks[0].enqueue==1 && npc_speaks[0].origin==9);
    assert(npc_speaker_find(speaker,123,0)->active_valid && npc_speaker_find(speaker,123,0)->active_page==123);
    npc_current=NULL;npc_speak_observe(test_npc_speak_original,speaker,&test_rng,1234);
    assert(test_calls==2 && npc_speak_count==2 && !npc_speaks[1].action && npc_speaks[1].enqueue==1);
    npc_current=&action;test_enqueue_immediate=0;test_enqueue_accept=0;
    assert(!npc_enqueue_observe(test_npc_enqueue_original,speaker,vector,-7,1,NULL,NULL,6,&test_calls,&test_rng));
    assert(test_enqueue_calls==2 && npc_speaker_find(speaker,123,0)->enqueue==1); /* Refusal preserves prior owner. */
    test_enqueue_nested=1;
    assert(!npc_enqueue_observe(test_npc_enqueue_original,speaker,vector,-7,1,NULL,NULL,6,&test_calls,&test_rng));
    assert(test_enqueue_calls==4 && npc_speaker_find(speaker,123,0)->enqueue==4); /* Outer refusal cannot erase newer nested acceptance. */
    unsigned char audio[0x80]={0};audio[0x78]=1;*(void**)(image+0x6cf15b8)=audio;
    *(double*)(image+0x6b60e48)=3.0;
    npc_finish_observe(test_npc_finish_original,speaker,1,&test_rng,0x563f7b);
    assert(test_finish_calls==1 && npc_finish_count==1 && !interrupt_failures && npc_finishes[0].audio==1 &&
           npc_finishes[0].enqueue==1 && npc_finishes[0].origin==9 && npc_finishes[0].saved==456 && npc_finishes[0].state.line==456);
    assert(npc_finishes[0].after==npc_finishes[0].before+13); /* Pending request4 cannot relabel active request1. */
    npc_current=NULL;
    *(uint64_t*)(speaker+8)=987;npc_speak_observe(test_npc_speak_original,speaker,&test_rng,1234);
    assert(test_calls==3 && npc_speak_count==2); /* Reused pointer is not the registered speaker. */
    npc_current=&action;test_enqueue_accept=1;
    assert(npc_enqueue_observe(test_npc_enqueue_original,speaker,vector,-7,1,NULL,NULL,6,&test_calls,&test_rng));
    assert(test_enqueue_calls==5 && npc_enqueue_count==5 && interrupt_failures==1 && !npc_enqueues[4].saved);
    npc_speak_observe(test_npc_speak_original,speaker,&test_rng,1234);
    assert(test_calls==4 && npc_speak_count==3 && interrupt_failures==2 && !npc_speaks[2].saved);
    assert(npc_speak_seen==4 && npc_speak_unowned==1 && !npc_speak_foreign_thread);
    /* A foreign accepted request must not inherit the last NPC ownership;
     * rejection must preserve it, even when the same SpeakData is reused. */
    *(uint64_t*)(speaker+8)=123;test_enqueue_immediate=1;interrupt_failures=0;
    assert(npc_enqueue_observe(test_npc_enqueue_original,speaker,vector,-7,1,NULL,NULL,6,&test_calls,&test_rng));
    npc_speaker *registered=npc_speaker_find(speaker,123,0);uint64_t owned_request=registered->enqueue;
    npc_current=&action;npc_speech_owner=NULL;test_enqueue_immediate=0;test_enqueue_accept=0;
    assert(!npc_enqueue_observe(test_npc_enqueue_original,speaker,vector,-7,1,NULL,NULL,6,&test_calls,&test_rng));
    assert(registered->enqueue==owned_request && registered->active_enqueue==owned_request);
    test_enqueue_accept=1;test_enqueue_immediate=1;
    assert(npc_enqueue_observe(test_npc_enqueue_original,speaker,vector,-7,1,NULL,NULL,6,&test_calls,&test_rng));
    assert(!registered->enqueue && !registered->active_enqueue && !registered->active_valid && !interrupt_failures);
    unsigned finished=npc_finish_count;npc_finish_observe(test_npc_finish_original,speaker,1,&test_rng,0x563f7b);
    assert(npc_finish_count==finished && npc_finish_unowned==1);
    npc_current=npc_speech_owner=NULL;npc_speak_count=0;npc_speak_calls=0;interrupt_failures=0;memset(npc_speakers,0,sizeof(npc_speakers));
    npc_enqueue_count=0;npc_enqueue_calls=0;npc_speak_seen=npc_speak_unowned=npc_speak_foreign_thread=0;
    npc_request_serial=0;
    npc_finish_count=0;npc_finish_calls=0;npc_finish_seen=npc_finish_unowned=npc_finish_foreign_thread=0;*(void**)(image+0x6cf15b8)=NULL;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;
    npc_exec_original_word=123;npc_exec_patched_word=456;*(uint64_t*)(image+NPC_EXEC_ENTRY)=123;
    assert(npc_exec_swap(123,456));npc_exec_installed=1;
    *(uint64_t*)(image+NPC_EXEC_ENTRY)=789;assert(!npc_exec_restore() && npc_exec_installed);
    *(uint64_t*)(image+NPC_EXEC_ENTRY)=456;assert(npc_exec_restore() && !npc_exec_installed);
    npc_speak_original_word=123;npc_speak_patched_word=456;*(uint64_t*)(image+NPC_SPEAK_ENTRY)=123;
    assert(npc_speak_swap(123,456));npc_speak_installed=1;
    *(uint64_t*)(image+NPC_SPEAK_ENTRY)=789;assert(!npc_speak_restore() && npc_speak_installed);
    *(uint64_t*)(image+NPC_SPEAK_ENTRY)=456;assert(npc_speak_restore() && !npc_speak_installed);
    npc_enqueue_original_word=123;npc_enqueue_patched_word=456;*(uint64_t*)(image+NPC_ENQUEUE_ENTRY)=123;
    assert(npc_enqueue_swap(123,456));npc_enqueue_installed=1;
    *(uint64_t*)(image+NPC_ENQUEUE_ENTRY)=789;assert(!npc_enqueue_restore() && npc_enqueue_installed);
    *(uint64_t*)(image+NPC_ENQUEUE_ENTRY)=456;assert(npc_enqueue_restore() && !npc_enqueue_installed);
    npc_finish_original_word=123;npc_finish_patched_word=456;*(uint64_t*)(image+NPC_FINISH_ENTRY)=123;
    assert(npc_finish_swap(123,456));npc_finish_installed=1;
    *(uint64_t*)(image+NPC_FINISH_ENTRY)=789;assert(!npc_finish_restore() && npc_finish_installed);
    *(uint64_t*)(image+NPC_FINISH_ENTRY)=456;assert(npc_finish_restore() && !npc_finish_installed);
}
static void test_interrupt_event_original(void *context) {
    ++test_calls;test_rng+=234;*(uint32_t*)((unsigned char*)context+0x188)|=4;
}
static double test_damage_original(void *group,void *victim,double amount,int priority,double time) {
    assert(victim==&test_rng && amount==2.75 && priority==3 && time==7.0);++test_calls;
    *(double*)((unsigned char*)group+0x68)-=amount;*(double*)((unsigned char*)group+0x70)=time;return amount;
}
static void *test_contact_a,*test_contact_b,*test_contact_manifold;
static void test_contact_original(void *self,void *a,void *b,void *manifold,void *settings,unsigned char added) {
    assert(self==&test_calls && a==test_contact_a && b==test_contact_b && manifold==test_contact_manifold && added==1);
    ++test_calls;((unsigned char*)settings)[0x18]=1;
}
static void *test_contact_component,*test_contact_motion;
static void test_contact_properties_original(void *component) {
    assert(component==test_contact_component);++test_calls;
    *(uint32_t*)((unsigned char*)test_contact_motion+0x70)=19;
}
static void test_contacts(void) {
    unsigned char bodies[2][0x80]={{0}},records[2][0x40]={{0}},components[2][0x270]={{0}};
    unsigned char motion[0x80]={0},manifold[0x850]={0},settings[64]={0},saved[0x850];
    void *scene=image+0x5000;*(void**)(image+0x38f0748)=scene;
    void *shape=image+0x2000;*(void**)shape=image+0x3000;
    for(unsigned i=0;i<2;i++) {
        *(void**)components[i]=image+0x4000;*(uint64_t*)(components[i]+8)=100+i;
        *(void**)records[i]=components[i];*(void**)(bodies[i]+0x50)=records[i];
        *(void**)(records[i]+0x18)=scene;
        *(void**)(components[i]+0x260+i*8)=records[i];
        *(void**)(bodies[i]+0x40)=shape;*(uint32_t*)(bodies[i]+0x70)=10+i;
    }
    *(void**)(bodies[0]+0x48)=motion; /* Body 2 is static, with no motion properties. */
    *(uint32_t*)(motion+0x70)=7;*(uint32_t*)(motion+0x74)=9;
    *(uint16_t*)(bodies[0]+0x74)=4;bodies[0][0x77]=2;bodies[0][0x78]=2;bodies[0][0x79]=0x15;
    actor_components[0]=components[0];actor_ids[0]=100;
    assert(contact_actor(bodies[0])==1 && !contact_actor(bodies[1]));
    actor_ids[0]=999;assert(!contact_actor(bodies[0]));actor_ids[0]=100;
    *(unsigned*)(manifold+0x30)=2;*(unsigned*)(manifold+0x440)=2;
    *(float*)(manifold+0x20)=0.25f;*(float*)(manifold+0x40)=3.0f;*(float*)(manifold+0x450)=4.0f;
    memcpy(saved,manifold,sizeof(saved));test_calls=0;
    test_contact_a=bodies[0];test_contact_b=bodies[1];test_contact_manifold=manifold;
    contact_event e={0};contact_failures=0;
    contact_observe(test_contact_original,&e,&test_calls,bodies[0],bodies[1],manifold,settings,1);
    assert(test_calls==1 && !contact_failures && e.count==2 && e.bodies[0].actor==1 && !e.bodies[1].actor);
    assert(e.bodies[0].record_kind==0 && e.bodies[1].record_kind==1);
    assert(e.bodies[0].active_index==7 && e.bodies[0].island_index==9 && e.bodies[0].scene==0 &&
        e.bodies[0].layer==4 && e.bodies[0].broadphase_layer==2 && e.bodies[0].motion_type==2 && e.bodies[0].flags==0x15);
    assert(e.bodies[1].active_index==UINT32_MAX && e.bodies[1].island_index==UINT32_MAX);
    assert(!memcmp(manifold,saved,sizeof(saved)) && !e.settings[0][6] && e.settings[1][6]==1);
    assert(e.points[0][0][0]==0x40400000 && e.points[1][0][0]==0x40800000 && e.bodies[1].velocity[0]==0);
    *(unsigned*)(manifold+0x440)=65;
    contact_observe(test_contact_original,&e,&test_calls,bodies[0],bodies[1],manifold,settings,1);
    assert(test_calls==2 && contact_failures==1 && e.count==0);
    unsigned char system[0x1c8]={0},iface[16]={0},manager[0x18]={0},xbody[16]={0};void *body_list[11]={0};
    *(void**)((unsigned char*)scene+0x20)=system;*(void**)(system+0x1b8)=iface;*(void**)(iface+8)=manager;
    *(uint64_t*)manager=11;*(void**)(manager+0x10)=body_list;body_list[10]=bodies[0];
    *(void**)(records[0]+0x10)=xbody;*(uint32_t*)(xbody+8)=10;
    *(uint32_t*)(records[0]+0x28)=8;*(uint32_t*)(records[0]+0x2c)=4;
    test_contact_component=components[0];test_contact_motion=motion;contact_failures=0;test_calls=0;
    contact_properties_event property={0};
    contact_properties_observe(test_contact_properties_original,&property,components[0]);
    assert(test_calls==1 && !contact_failures && property.before[0].present && !property.before[1].present &&
        property.before[0].body.active_index==7 && property.after[0].body.active_index==19 &&
        property.after[0].group==8 && property.after[0].filter==4);
    contact_properties_body property_body;
    *(uint32_t*)(xbody+8)=0x80000a;assert(!contact_properties_read(components[0],0,&property_body));
    *(uint32_t*)(xbody+8)=10;*(void**)records[0]=components[1];
    assert(!contact_properties_read(components[0],0,&property_body));*(void**)records[0]=components[0];
    assert(!contact_properties_read(components[0],2,&property_body));
    assert(!contact_properties_install());contact_properties_original_word=321;contact_properties_patched_word=654;
    *(uint64_t*)(image+CONTACT_PROPERTIES_ENTRY)=654;contact_properties_installed=1;contact_properties_restore();
    assert(contact_properties_restored && !contact_properties_installed && *(uint64_t*)(image+CONTACT_PROPERTIES_ENTRY)==321);
    *(uint64_t*)(image+CONTACT_PROPERTIES_ENTRY)=987;contact_properties_installed=1;contact_properties_restore();
    assert(!contact_properties_restored && contact_properties_installed && *(uint64_t*)(image+CONTACT_PROPERTIES_ENTRY)==987);
    contact_properties_installed=0;
    contact_failures=0;actor_components[0]=NULL;actor_ids[0]=0;
    assert(!contact_install());contact_original_word=123;contact_patched_word=456;
    *(uint64_t*)(image+CONTACT_ENTRY)=123;assert(contact_swap(123,456) && !contact_swap(123,789));
    contact_installed=1;contact_restore();assert(contact_restored && !contact_installed && *(uint64_t*)(image+CONTACT_ENTRY)==123);
    *(uint64_t*)(image+CONTACT_ENTRY)=789;contact_installed=1;contact_restore();
    assert(!contact_restored && contact_installed && *(uint64_t*)(image+CONTACT_ENTRY)==789);contact_installed=0;
}
static void test_dock_shape_identity(void) {
    unsigned char anchors[2][0x78]={{0}},modules[2][0x78]={{0}},middle[0x78]={0};
    for(unsigned i=0;i<2;i++) {
        *(uint64_t*)(anchors[i]+8)=100+i;*(uint64_t*)(modules[i]+8)=200+i;
        *(void**)modules[i]=image+0x2adbdc0;*(void**)(modules[i]+0x70)=anchors[i];
    }
    assert(!shape_bind_dock(0,anchors[0],modules[0]));
    assert(!shape_bind_dock(1,anchors[1],modules[0]));
    *(void**)modules[0]=image+123;assert(!shape_bind_dock(1,anchors[0],modules[0]));
    *(void**)modules[0]=image+0x2adbdc0;
    *(void**)(modules[0]+0x70)=middle;*(void**)(middle+0x70)=anchors[0];
    assert(shape_bind_dock(1,anchors[0],modules[0]) && shape_dock_registered==1);
    assert(shape_bind_dock(1,anchors[0],modules[0]) && shape_dock_registered==1);
    assert(!shape_bind_dock(2,anchors[0],modules[0]));
    *(uint64_t*)(modules[0]+8)=300;assert(!shape_bind_dock(1,anchors[0],modules[0]));
    *(uint64_t*)(modules[0]+8)=200;assert(shape_bind_dock(2,anchors[1],modules[1]) && shape_dock_registered==2);
    memset(shape_dock_objects,0,sizeof(shape_dock_objects));memset(shape_dock_anchors,0,sizeof(shape_dock_anchors));
    memset(shape_dock_ids,0,sizeof(shape_dock_ids));memset(shape_dock_anchor_ids,0,sizeof(shape_dock_anchor_ids));shape_dock_registered=0;
}
static void test_dock_extra_shape_identity(void) {
    unsigned char object[0x78]={0},record[0x28]={0},other[0x28]={0},xbody[0x10]={0};
    unsigned char scene[0x28]={0},system[0x1c0]={0},iface[0x10]={0},manager[0x18]={0},body[0x80]={0};
    void *bodies[1]={body};unsigned which=0;
    shape_dock_objects[0]=object;shape_dock_ids[0]=77;*(uint64_t*)(object+8)=77;
    *(void**)record=object;*(void**)(record+0x10)=xbody;*(uint32_t*)(xbody+8)=0x800000u;
    *(void**)(record+0x18)=scene;*(void**)(image+0x38f0748)=scene;
    *(void**)(scene+0x20)=system;*(void**)(system+0x1b8)=iface;*(void**)(iface+8)=manager;
    *(uint64_t*)manager=1;*(void**)(manager+0x10)=bodies;*(void**)(body+0x50)=record;
    *(uint32_t*)(body+0x70)=0x800000u;*(uint16_t*)(body+0x74)=1;
    assert(shape_extra_role(0,record,&which) && which==3 && shape_extra_registered==1);
    assert(shape_extra_role(0,record,&which) && shape_extra_registered==1);
    memcpy(other,record,sizeof(record));*(void**)(body+0x50)=other;
    assert(!shape_extra_role(0,other,&which)); /* Ambiguous same scene/layer. */
    *(void**)(body+0x50)=record;
    /* Reused record, new BodyID generation in the registered body's slot: that
     * body is provably gone, so the role is re-registered (same role and seed). */
    *(uint32_t*)(xbody+8)=0x1000000u;*(uint32_t*)(body+0x70)=0x1000000u;
    assert(shape_extra_role(0,record,&which) && which==3 && shape_extra_registered==1 && shape_extra_replaced==1);
    assert(shape_extra[0][1].record==record && shape_extra[0][1].body_id==0x1000000u && shape_extra[0][1].replaced==1);
    assert(shape_extra_role(0,record,&which) && shape_extra_replaced==1); /* Stable afterwards. */
    *(uint32_t*)(xbody+8)=0x800000u;*(uint32_t*)(body+0x70)=0x800000u;
    assert(shape_extra_role(0,record,&which) && shape_extra_replaced==2); /* 0x1000000 is gone again. */
    *(uint64_t*)(object+8)=78;assert(!shape_extra_role(0,record,&which));*(uint64_t*)(object+8)=77;
    *(uint16_t*)(body+0x74)=5;assert(!shape_extra_role(0,record,&which));*(uint16_t*)(body+0x74)=1;
    *(uint64_t*)manager=0;assert(!shape_extra_role(0,record,&which));*(uint64_t*)manager=1;
    *(void**)(image+0x38f0748)=NULL;assert(!shape_extra_role(0,record,&which));
    *(void**)(image+0x38f0748)=scene;
    /* A second body in slot 1 under another record (the attention round trip:
     * old body freed, new body elsewhere). */
    unsigned char xother[0x10]={0},second[0x80]={0};void *two[2]={body,second};
    *(void**)(manager+0x10)=two;*(uint64_t*)manager=2;
    memcpy(other,record,sizeof(record));*(void**)(other+0x10)=xother;*(uint32_t*)(xother+8)=0x800001u;
    *(void**)(second+0x50)=other;*(uint32_t*)(second+0x70)=0x800001u;*(uint16_t*)(second+0x74)=1;
    assert(shape_extra[0][1].record==record && shape_extra[0][1].body_id==0x800000u);
    assert(!shape_extra_role(0,other,&which) && shape_extra[0][1].record==record); /* True duplicate: slot 0 still live. */
    two[0]=(void*)((uintptr_t)body|1); /* Slot 0 freed (Jolt free-list encoding). */
    assert(shape_extra_role(0,other,&which) && which==3 && shape_extra_replaced==3 && shape_extra_registered==1);
    assert(shape_extra[0][1].record==other && shape_extra[0][1].body_id==0x800001u && shape_extra[0][1].replaced==3);
    /* Back to record in slot 0 while slot 1 is live: duplicate. */
    two[0]=body;assert(!shape_extra_role(0,record,&which));
    /* Unreadable registered slot (index beyond the manager's count) fails closed. */
    *(uint64_t*)manager=1;assert(!shape_extra_role(0,record,&which) && shape_extra[0][1].record==other);
    /* A replacement never crosses roles: another layer registers its own role. */
    *(uint64_t*)manager=2;two[1]=(void*)((uintptr_t)second|1);*(uint16_t*)(body+0x74)=2;
    assert(shape_extra_role(0,record,&which) && which==4 && shape_extra_registered==2 && shape_extra[0][1].record==other);
    *(uint16_t*)(body+0x74)=1;
    memset(shape_extra,0,sizeof(shape_extra));shape_extra_registered=shape_extra_replaced=0;
    shape_dock_objects[0]=NULL;shape_dock_ids[0]=0;
    puts("dock extra shape role registration, provable replacement and duplicate refusal tests PASS");
}
static void test_agent_original(void *self,void *commands,float dt,double time,unsigned char cheap) {
    assert(!self && dt==1.0f/60.0f && time==7.0);
    test_cheap=cheap;test_ai_original(commands);
}
static void test_params_original(void *self,void *destination,unsigned char flag) {
    assert(self==&test_rng && destination==&test_calls && flag==0);++test_calls;
}
static void test_nav_original(void *self,double time) {
    assert(time==7.0);test_ai_original(self);
}
static void test_shape_original(void *self,unsigned char flag) {
    test_cheap=flag;test_ai_original(self);
}
static double test_shape_due_seen;
static void test_shape_deadline_original(void *self,unsigned char flag) {
    ++test_calls;test_cheap=flag;test_shape_due_seen=*(double*)((unsigned char*)self+0x28);
    *(double*)((unsigned char*)self+0x28)=1000.0;test_rng^=12345;
}
static void test_death_original(void *self,void *source,double time,unsigned reason,void *killer) {
    assert(self==source && time==7.0 && reason==6 && killer==&test_calls);test_ai_original(self);
}

static unsigned char test_world_identity_original(void *object,void *source) {
    assert(object && source);++test_calls;return (unsigned char)test_cheap;
}
static void test_world_identity(void) {
    unsigned char source[32]={0},object[16]={0};uint64_t saved=0;
    memcpy(source,"[0xae79c]",10);*(uint64_t*)(source+16)=9;*(uint64_t*)(source+24)=15;
    assert(world_identity_parse(source,&saved) && saved==0xae79c);
    char large[]="[0xffffffffffffffff]";
    *(void**)source=large;*(uint64_t*)(source+16)=20;*(uint64_t*)(source+24)=31;
    assert(world_identity_parse(source,&saved) && saved==UINT64_MAX);
    large[3]='g';assert(!world_identity_parse(source,&saved));large[3]='f';
    *(uint64_t*)(source+16)=21;assert(!world_identity_parse(source,&saved));
    memset(source,0,32);memcpy(source,"[0xae79c]",10);*(uint64_t*)(source+16)=9;*(uint64_t*)(source+24)=15;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);*(uint64_t*)(object+8)=0x2000000000000001ULL;
    test_calls=0;test_cheap=1;
    assert(world_identity_observe(test_world_identity_original,object,source,1)==1);
    assert(test_calls==1 && world_identity_calls==1 && world_identity_count==1 && !world_identity_failures);
    assert(world_identity_lookup(0x2000000000000001ULL)==0xae79c && !world_identity_lookup(1));
    assert(world_identity_record(0x2000000000000001ULL,0xae79c) && world_identity_count==1);
    assert(!world_identity_record(0x2000000000000001ULL,0xae79d));
    uint64_t collision=2;while(world_identity_bucket(collision)!=world_identity_bucket(0x2000000000000001ULL)) ++collision;
    assert(world_identity_record(collision,123) && world_identity_lookup(collision)==123);
    world_identity_count=WORLD_IDENTITY_CAPACITY/2;
    assert(!world_identity_record(12345,23456));world_identity_count=2;
    test_cheap=0;assert(!world_identity_observe(test_world_identity_original,object,source,1));
    assert(test_calls==2 && world_identity_failures==1);
    test_cheap=1;source[0]='!';assert(world_identity_observe(test_world_identity_original,object,source,1)==1);
    assert(test_calls==3 && world_identity_failures==2 && world_identity_count==2);
    assert(world_identity_observe(test_world_identity_original,object,source,0)==1);
    assert(test_calls==4 && world_identity_late==1 && world_identity_calls==3 && world_identity_failures==2);
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;
    world_identity_count=world_identity_calls=world_identity_failures=world_identity_late=0;
    assert(!world_identity_install());world_identity_original_word=123;world_identity_patched_word=456;
    *(uint64_t*)(image+WORLD_IDENTITY_ENTRY)=123;assert(world_identity_swap(123,456));world_identity_installed=1;
    *(uint64_t*)(image+WORLD_IDENTITY_ENTRY)=789;world_identity_restore();
    assert(!world_identity_restored && world_identity_installed && *(uint64_t*)(image+WORLD_IDENTITY_ENTRY)==789);
    *(uint64_t*)(image+WORLD_IDENTITY_ENTRY)=456;world_identity_restore();
    assert(world_identity_restored && !world_identity_installed && *(uint64_t*)(image+WORLD_IDENTITY_ENTRY)==123);
}

static unsigned char *test_snapshot_objects[12];
static void *test_snapshot_find(void *map,uint64_t runtime,unsigned flags) {
    assert(map && flags==4);return runtime>=100 && runtime<112?test_snapshot_objects[runtime-100]:NULL;
}
static void test_snapshot_fixture(void) {
    *(uint32_t*)(image+0x38f4ee4)=0;*(uint32_t*)(image+0x2f4b6f4)=1;
    world_bank_phase phase;assert(world_banks_read(&phase) && phase.read==0 && phase.write==1);
    *(uint32_t*)(image+0x38f4ee4)=1;assert(!world_banks_read(&phase));
    *(uint32_t*)(image+0x2f4b6f4)=0;assert(world_banks_read(&phase) && phase.read==1 && phase.write==0);
    *(uint32_t*)(image+0x38f4ee4)=2;assert(!world_banks_read(&phase));
    *(uint32_t*)(image+0x38f4ee4)=0;*(uint32_t*)(image+0x2f4b6f4)=1;
    unsigned char objects[12][0x288]={{0}},controllers[12][0xe0]={{0}},agents[12][0x1200]={{0}},macros[12][0x40]={{0}};
    char manifest[204],duplicate[204];
    for(unsigned i=0;i<12;i++) snprintf(manifest+i*17,sizeof(manifest)-i*17,"%016llx%s",(unsigned long long)(1000+i),i<11?",":"");
    memcpy(duplicate,manifest,sizeof(manifest));memcpy(duplicate+17,manifest,16);
    assert(!snapshot_manifest(duplicate,203) && !snapshot_manifest(manifest,202));
    stats.active=1;stats.issued=0;boot_mode=1;boot_phase=0;ui_thread=GetCurrentThreadId();
    world_identity_installed=1;path_installed=1;path_options=1;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(world_identities);
    assert(snapshot_manifest(manifest,203));boot_phase=2;assert(snapshot_manifest(manifest,203));
    duplicate[0]='1';assert(!snapshot_manifest(duplicate,203));
    *(void**)(image+0x2ba5430+18*8)=image+0xefb10;
    for(unsigned i=0;i<12;i++) {
        const char *macro=i<6?"ship_arg_s_fighter_01_a_macro":"ship_xen_s_fighter_01_a_macro";
        test_snapshot_objects[i]=objects[i];*(void**)objects[i]=image+0x2b7feb8;*(uint64_t*)(objects[i]+8)=100+i;
        *(void**)(objects[i]+0x60)=macros[i];*(void**)macros[i]=image+0x2ba5430;
        *(const char**)(macros[i]+0x20)=macro;*(uint64_t*)(macros[i]+0x30)=strlen(macro);*(uint64_t*)(macros[i]+0x38)=31;
        *(void**)(objects[i]+0x200)=controllers[i];*(void**)controllers[i]=image+0x2c51158;
        *(void**)(controllers[i]+0x28)=objects[i];*(void**)(controllers[i]+0x30)=agents[i]+0xad0;
        *(void**)(controllers[i]+0x40)=agents[i];*(void**)(agents[i]+0xef0)=objects[i];
        assert(world_identity_record(100+i,1000+i));
        if(i<11)snapshot_import_capture(objects[i],100+i,1000+i);
    }
    boot_phase=3;assert(!snapshot_bind(test_snapshot_find,objects));assert(!actor_components[0]);
    snapshot_import_capture(objects[11],111,1011);
    *(void**)objects[5]=image+0x2b7feb0;assert(!snapshot_bind(test_snapshot_find,objects));assert(!actor_components[0]);
    *(void**)objects[5]=image+0x2b7feb8;
    *(const char**)(macros[5]+0x20)="ship_xen_s_fighter_01_a_macro";assert(!snapshot_bind(test_snapshot_find,objects));assert(!actor_components[0]);
    *(const char**)(macros[5]+0x20)="ship_arg_s_fighter_01_a_macro";
    assert(snapshot_bind(test_snapshot_find,objects) && snapshot_bound==12 && snapshot_bound_tick==0);
    unsigned char model[0x3a0]={0};flight_installed=1;
    assert(snapshot_flight_register(1,agents[0]+0xad0) && !strcmp(snapshot_flight_kinds[0],"none"));
    *(void**)(agents[0]+0xf10)=model;*(void**)model=image+SPLINE_ENTRY-0x70;*(void**)(model+0x10)=agents[0];
    assert(snapshot_flight_register(1,agents[0]+0xad0) && !strcmp(snapshot_flight_kinds[0],"spline") && !flight_samples[0].model);
    *(void**)(model+0x10)=agents[1];assert(!snapshot_flight_register(1,agents[0]+0xad0));*(void**)(model+0x10)=agents[0];
    *(void**)model=image+1;assert(!snapshot_flight_register(1,agents[0]+0xad0));
    *(void**)model=image+FLIGHT_ENTRY-0x70;
    assert(snapshot_flight_register(1,agents[0]+0xad0) && !strcmp(snapshot_flight_kinds[0],"steering") && flight_samples[0].model==model);
    flight_installed=0;memset(flight_samples,0,sizeof(flight_samples));
    stats.issued=1;++ui_thread;assert(!snapshot_prepare());--ui_thread;
    stats.issued=2;assert(!snapshot_prepare());stats.issued=1;
    assert(!snapshot_enroll(1,100));snapshot_prepared=12;
    assert(!snapshot_enroll(1,101));assert(snapshot_enroll(1,100) && snapshot_enroll(1,100) && snapshot_enrolled==1);
    for(unsigned i=1;i<12;i++)assert(snapshot_enroll(i+1,100+i));assert(snapshot_enrolled==12);
    nav_sample s={.tick=1,.calls=1,.initialized=1,.deadline_after=123};snapshot_nav_observe(6,&s);
    s.tick=2;s.deadline_after=456;snapshot_nav_observe(6,&s);assert(snapshot_actors[5].first_nav.tick==1 && snapshot_actors[5].first_nav.deadline_after==123);
    snapshot_import_capture(objects[0],100,1000);assert(snapshot_failures==1);
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;
    memset(snapshot_actors,0,sizeof(snapshot_actors));snapshot_configured=snapshot_bound=snapshot_prepared=snapshot_enrolled=0;
    snapshot_bound_tick=snapshot_prepared_tick=0;snapshot_failures=0;
    snapshot_prepare_stage="none";snapshot_attempt_tick=snapshot_attempt_time=0;
    snapshot_attempt_actor=snapshot_attempt_ui=snapshot_attempt_main=snapshot_attempt_view=snapshot_attempt_model=0;
    memset(snapshot_flight_kinds,0,sizeof(snapshot_flight_kinds));
    memset(actor_components,0,sizeof(actor_components));memset(actor_ids,0,sizeof(actor_ids));
    stats.active=0;stats.issued=0;boot_mode=0;boot_phase=0;world_identity_installed=0;path_installed=0;path_options=0;
}
static void test_world_hash_semantics(void) {
    assert(!world_hash_watch_add("0000000000000000",16));
    assert(!world_hash_watch_add("00000000000000gg",16));
    assert(world_hash_watch_add("ffffffffffffffff",16));
    assert(world_hash_watch_add("0000000000000001",16));
    assert(world_hash_watches[0].saved==1 && world_hash_watches[1].saved==UINT64_MAX);
    assert(!world_hash_watch_add("0000000000000001",16));
    world_hash_scans=1;assert(!world_hash_watch_add("0000000000000002",16));world_hash_scans=0;
    for(unsigned i=2;i<64;i++) {char id[17];snprintf(id,sizeof(id),"%016x",i);assert(world_hash_watch_add(id,16));}
    assert(!world_hash_watch_add("0000000000000040",16));world_hash_watch_count=0;
    unsigned char object[0x300]={0},parent[16]={0},controller[0x80]={0};
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && world_identity_record(9001,444) && world_identity_record(9002,444));
    world_class_count=1;world_classes[0]=(world_class_cache){(uintptr_t)(image+0x4000),3};
    *(void**)object=image+0x4000;*(uint64_t*)(object+8)=100;*(void**)(object+0x70)=parent;
    *(uint64_t*)(parent+8)=9001;*(double*)(object+0x280)=100.0;*(void**)(object+0x200)=controller;
    *(void**)controller=image+0x2a34b38;*(void**)(image+0x2a34b38+0x30)=image+0x10ba20;
    for(unsigned i=0;i<16;i++) ((uint32_t*)(controller+0x10))[i]=100+i;
    world_hash_item item={.runtime=100,.saved=77};world_hash_data a[4]={0},b[4]={0};
    assert(world_hash_record(&item,a,object) && item.parent==444 && item.flags==7);
    *(uint64_t*)(parent+8)=9002;*(uint32_t*)(controller+0x10+12)=0xdeadbeef;
    assert(world_hash_record(&item,b,object) && !memcmp(a,b,sizeof(a))); /* runtime identity and vector padding excluded */
    memset(b,0,sizeof(b));*(double*)(object+0x280)=99.0;
    assert(world_hash_record(&item,b,object) && memcmp(&a[1],&b[1],sizeof(a[1])));
    assert(!memcmp(&a[0],&b[0],sizeof(a[0])) && !memcmp(&a[2],&b[2],sizeof(a[2])) && !memcmp(&a[3],&b[3],sizeof(a[3])));
    unsigned char nav[0xe0]={0},slots[0x340]={0};*(void**)nav=image+0x2c51158;*(void**)(nav+0x30)=slots;
    *(void**)(object+0x200)=nav;*(unsigned*)(image+0x38f4ee4)=0;
    memset(a,0,sizeof(a));memset(b,0,sizeof(b));assert(world_hash_record(&item,a,object));
    *(uint32_t*)(slots+0xcc)=0x33;*(double*)(slots+0x150)=1.0;slots[0x159]=1;
    assert(world_hash_record(&item,b,object) && memcmp(&a[2],&b[2],sizeof(a[2])) && memcmp(&a[3],&b[3],sizeof(a[3])));
    assert(!memcmp(&a[0],&b[0],sizeof(a[0])) && !memcmp(&a[1],&b[1],sizeof(a[1])));
    *(uint64_t*)(object+8)=101;memset(b,0,sizeof(b));assert(!world_hash_record(&item,b,object));
    *(uint64_t*)(object+8)=100;*(uint64_t*)(parent+8)=9999;memset(b,0,sizeof(b));
    assert(world_hash_record(&item,b,object) && (item.flags&16));
    memset(b,0,sizeof(b));assert(world_hash_record(&item,b,NULL) && !item.flags && !item.type);
    BCRYPT_ALG_HANDLE algorithm=NULL;BCRYPT_HASH_HANDLE hash=NULL;unsigned char raw[32];char text[65];
    assert(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)>=0);
    assert(BCryptCreateHash(algorithm,&hash,NULL,0,NULL,0,BCRYPT_HASH_REUSABLE_FLAG)>=0);
    for(unsigned i=0;i<2;i++) {
        assert(BCryptHashData(hash,(PUCHAR)"abc",3,0)>=0 && BCryptFinishHash(hash,raw,32,0)>=0);hex32(raw,text);
        assert(!strcmp(text,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    }
    BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;world_class_count=0;
}
static void test_player_value(void *context,unsigned code,float value) {
    assert(context==(void*)123 && code==0x6e && value==0.5f);++test_calls;
}
static void test_player_authority(void) {
    stats.issued=30;player_action_tick=0;test_calls=0;
    assert(!player_throttle_once(test_player_value,(void*)123,30,2,7,7,0.5));
    assert(!player_throttle_once(test_player_value,(void*)123,30,1,7,8,0.5));
    assert(!player_throttle_once(test_player_value,(void*)123,29,1,7,7,0.5));
    assert(!player_throttle_once(test_player_value,(void*)123,30,1,7,7,NAN));
    assert(!player_throttle_once(test_player_value,(void*)123,30,1,7,7,1.01));
    assert(!player_throttle_once(test_player_value,NULL,30,1,7,7,0.5));
    assert(test_calls==0);
    assert(player_throttle_once(test_player_value,(void*)123,30,1,7,7,0.5));
    assert(test_calls==1 && player_action_tick==30);
    assert(!player_throttle_once(test_player_value,(void*)123,30,1,7,7,0.5));
    assert(test_calls==1);stats.issued=0;player_action_tick=0;test_calls=0;
}
static void test_player_update_original(void *self,double time) {
    ++test_calls;*(double*)((unsigned char*)self+0xf0)=time;
    *(float*)(image+0x2f4b764)=0.5f;
}
static uintptr_t test_player_shoot_original(void *self,void *weapon) {
    assert(weapon==player_observer_object);++test_calls;
    ((unsigned char*)self)[0xa0]=1;return 0x1234567800000001ULL;
}
static void test_player_observation(void) {
    unsigned char controller[0x19600]={0},object[0x80]={0},shoot[0xc0]={0};
    *(void**)(controller+0x28)=object;*(uint64_t*)(object+8)=777;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && world_identity_record(777,123));
    player_observer_object=object;player_observer_controller=controller;player_observer_actor=123;
    stats.active=0;test_calls=0;player_update_observe(test_player_update_original,controller,1.0);
    assert(test_calls==1 && player_observer_count==0);
    stats.active=1;stats.issued=5;boot_phase=3;ui_thread=GetCurrentThreadId();
    player_update_observe(test_player_update_original,controller,2.0);
    player_observation *s=&player_observations[0];double before,after;
    memcpy(&before,s->local[0],8);memcpy(&after,s->local[1],8);
    assert(test_calls==2 && player_observer_count==1 && s->ready && s->valid && s->role==1 &&
           s->actor==123 && s->owner==123 && before==1.0 && after==2.0 && !player_observer_failures);
    assert(player_shoot_observe(test_player_shoot_original,shoot,object)==0x1234567800000001ULL);
    assert(test_calls==3 && player_observations[1].local[0][0]==0 && player_observations[1].local[1][0]==1 &&
           player_observations[1].result==1 && player_observations[1].valid);
    player_observer_count=PLAYER_OBSERVER_CAPACITY;
    player_update_observe(test_player_update_original,controller,3.0);
    assert(test_calls==4 && player_observer_failures==1); /* Overflow never duplicates/skips native. */
    *(void**)(image+PLAYER_UPDATE_ENTRY)=(void*)123;*(void**)(image+PLAYER_SHOOT_ENTRY)=(void*)player_shoot_observer;
    player_update_installed=player_shoot_installed=1;player_observer_restore();
    assert(player_update_installed && !player_shoot_installed && !player_observer_restored &&
           *(void**)(image+PLAYER_UPDATE_ENTRY)==(void*)123);
    *(void**)(image+PLAYER_UPDATE_ENTRY)=(void*)player_update_observer;player_observer_restore();
    assert(player_observer_restored && !player_update_installed);
    stats.issued=1;assert(!player_observer_install(controller,123) && !player_observer_enabled_tick);
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;
    memset(player_observations,0,sizeof(player_observations));player_observer_count=player_observer_failures=0;
    player_observer_restored=0;player_observer_actor=0;player_observer_controller=player_observer_object=NULL;
    stats.active=0;stats.issued=0;boot_phase=0;ui_thread=0;test_calls=0;
}
static void test_player_trigger_original(unsigned code,unsigned char pressed) {
    assert(code==0x18);++test_calls;
    image[0x38f0728]=pressed;player_fire_player[0x8a550]=pressed;
}
static unsigned char test_player_projectile[0x300];
static void test_player_gun_original(void *self) {
    (void)self;++test_calls;assert(player_gun_context);player_bullet_created(test_player_projectile);
}
static void test_player_bullet_original(void *self) {
    ++test_calls;assert(player_projectile_context);++*(int*)((unsigned char*)self+0x280);
    damage_event event={.actor=1};double amount=2.0,before=100.0,after=98.0;
    memcpy(&event.amount,&amount,8);memcpy(&event.absorbed,&amount,8);
    memcpy(&event.before[0],&before,8);memcpy(&event.after[0],&after,8);player_damage_observed(&event);
}
static void test_player_fire(void) {
    unsigned char universe[0x240]={0},object[0x80]={0},gun[0x80]={0};
    unsigned char *player=calloc(1,0x8a600);assert(player);
    *(void**)(image+0x3d198f8)=universe;*(void**)(universe+0x230)=player;*(uint64_t*)(player+8)=777;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && world_identity_record(777,123) && world_identity_record(778,124) && world_identity_record(779,125));
    player_fire_player=player;player_fire_player_saved=123;test_calls=0;player_action_tick=0;
    memset(image+0x38f0728,0,6);
    assert(!player_trigger_apply(test_player_trigger_original,1,2) && !test_calls);
    assert(player_trigger_apply(test_player_trigger_original,1,1) && test_calls==1 && player_action_tick==1);
    assert(!player_trigger_apply(test_player_trigger_original,2,1) && test_calls==1);
    image[0x38f072c]=1;assert(!player_trigger_apply(test_player_trigger_original,2,0) && test_calls==1);
    image[0x38f072c]=0;assert(player_trigger_apply(test_player_trigger_original,2,0) && test_calls==2);
    *(uint64_t*)(player+8)=999;assert(!player_trigger_apply(test_player_trigger_original,3,1) && test_calls==2);
    *(uint64_t*)(player+8)=777;stats.active=1;stats.issued=3;boot_phase=3;ui_thread=GetCurrentThreadId();
    player_fire_enabled=1;player_observer_object=object;player_observer_actor=124;*(uint64_t*)(object+8)=778;
    *(void**)gun=image+weapon_vtables[0];*(uint64_t*)(gun+8)=779;*(void**)(gun+0x70)=object;
    *(void**)test_player_projectile=image+0x2c42ac8;*(uint64_t*)(test_player_projectile+8)=780;
    actor_ids[0]=778;test_calls=0;test_rng=47;
    assert(player_weapon_observe(test_player_gun_original,gun) && test_calls==1 && !player_gun_context);
    assert(player_projectile_count==1 && player_fire_events[0].gun==125 && player_fire_events[0].spawn==1);
    assert(player_bullet_observe(test_player_bullet_original,test_player_projectile) && test_calls==2 && !player_projectile_context);
    assert(test_rng==47 && player_fire_event_count==4 && player_fire_moves==1 && player_fire_damage==1 && !player_fire_failures);
    assert(player_fire_events[1].kind==2 && player_fire_events[2].kind==4 && player_fire_events[2].target==124 && player_fire_events[3].kind==3);
    *(uint64_t*)(test_player_projectile+8)=999;assert(!player_bullet_observe(test_player_bullet_original,test_player_projectile) && test_calls==2);
    player_fire_enabled=0;assert(!player_weapon_observe(test_player_gun_original,gun) && test_calls==2);
    free(player);VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;
    memset(player_guns,0,sizeof(player_guns));memset(player_projectiles,0,sizeof(player_projectiles));memset(player_fire_events,0,sizeof(player_fire_events));
    player_gun_count=player_projectile_count=player_fire_event_count=0;player_fire_moves=player_fire_damage=0;
    player_fire_player=NULL;player_fire_player_saved=player_observer_actor=0;player_observer_object=NULL;
    stats.active=0;stats.issued=0;boot_phase=0;ui_thread=0;player_action_tick=0;test_calls=0;actor_ids[0]=0;
    *(void**)(image+0x3d198f8)=NULL;
}
static player_gun test_nested_gun;
static void test_launch_inner(void *self) {
    (void)self;++test_calls;
    assert(player_gun_context==&test_nested_gun && test_rng==player_launch_seed(7,124,126,1));
    test_rng+=17;
}
static void test_launch_outer(void *self) {
    (void)self;++test_calls;assert(test_rng==player_launch_seed(7,124,125,1));
    player_launch_event *outer=player_launch_context;
    test_rng+=9;uint64_t before=test_rng;
    player_weapon_scope(test_launch_inner,&test_nested_gun,test_nested_gun.object,&test_rng);
    assert(test_rng==before && player_launch_context==outer && player_gun_context->saved==125);
    player_bullet_created(test_player_projectile);
    *(double*)(test_player_projectile+0x268)=8.0;
    *(double*)(test_player_projectile+0x270)=8.1;
    *(double*)(test_player_projectile+0x278)=8.0;
}
static void test_launch_noop(void *self) {(void)self;++test_calls;}
static void test_player_launch_rng(void) {
    unsigned char a[0x80]={0},b[0x80]={0};*(void**)a=*(void**)b=image+weapon_vtables[0];
    player_gun gun={.object=a,.saved=125,.tick=7,.calls=1};
    test_nested_gun=(player_gun){.object=b,.saved=126,.tick=7,.calls=1};
    stats.issued=7;player_observer_actor=124;test_calls=0;test_rng=47;
    assert(!player_launch_enable());
    uint64_t seed=player_launch_seed(7,124,125,1);
    assert(seed==0x4e23dfabd32d0b4aULL); /* Independent validator golden tuple. */
    assert(seed!=player_launch_seed(8,124,125,1) && seed!=player_launch_seed(7,125,125,1) &&
           seed!=player_launch_seed(7,124,126,1) && seed!=player_launch_seed(7,124,125,2));
    player_weapon_scope(test_launch_noop,&gun,a,&test_rng);
    assert(test_calls==1 && !player_launch_count && test_rng==47 && !player_gun_context);
    player_weapon_seeded=1;player_fire_enabled=1;test_calls=0;
    memset(test_player_projectile,0,sizeof(test_player_projectile));
    *(void**)test_player_projectile=image+0x2c42ac8;*(uint64_t*)(test_player_projectile+8)=780;
    *(double*)(test_player_projectile+0x268)=-1.0;
    player_weapon_scope(test_launch_outer,&gun,a,&test_rng);
    assert(test_calls==2 && test_rng==47 && !player_gun_context && !player_launch_context);
    assert(player_launch_count==3 && player_launch_scopes==2 && player_launch_restored==2 && !player_launch_failures);
    assert(player_launch_events[0].seed==seed && player_launch_events[0].after==seed+9 && player_launch_events[0].original==1 && player_launch_events[0].valid);
    assert(player_launch_events[1].caller==seed+9 && player_launch_events[1].restored==seed+9 && player_launch_events[1].valid);
    double pre,post;memcpy(&pre,&player_launch_events[2].pre[0],8);memcpy(&post,&player_launch_events[2].post[0],8);
    assert(player_launch_events[2].kind==2 && player_launch_events[2].spawn==1 && pre==-1.0 && post==8.0 && player_launch_events[2].valid);
    player_weapon_scope(test_launch_noop,&gun,a,NULL);
    assert(test_calls==3 && test_rng==47 && player_launch_failures==1 && !player_launch_events[3].valid && player_launch_events[3].original==1);
    player_launch_count=PLAYER_LAUNCH_CAP;player_weapon_scope(test_launch_noop,&gun,a,&test_rng);
    assert(test_calls==4 && test_rng==47 && player_launch_failures==3 && !player_gun_context && !player_launch_context);
    player_launch_count=0;*(void**)a=image+weapon_vtables[1];player_weapon_scope(test_launch_noop,&gun,a,&test_rng);
    assert(test_calls==5 && test_rng==47 && player_launch_failures==4);
    memset(player_launch_events,0,sizeof(player_launch_events));player_launch_count=0;
    player_launch_scopes=player_launch_restored=player_launch_failures=0;player_weapon_seeded=player_fire_enabled=0;
    memset(player_projectiles,0,sizeof(player_projectiles));memset(player_fire_events,0,sizeof(player_fire_events));
    player_projectile_count=player_fire_event_count=0;player_observer_actor=0;stats.issued=0;test_calls=0;
}
static void test_actor_registry_roles(void) {
    for(unsigned i=1;i<=12;i++) {
        assert(actor_is_ship(i) && !actor_is_dock(i));
        assert(actor_seed(7,11,i,13,17,19)==lockstep_seed(7,11,i,13,17,19));
    }
    assert(actor_is_dock(13) && actor_is_dock(14));
    assert(!actor_is_ship(13) && !actor_is_ship(14) && !actor_is_ship(15));
    dynamic_ships[0].handle=0x0000000500000003ULL;
    assert(actor_is_ship(15) && !actor_is_dock(15));
    assert(!actor_number_is_ship(NAN) && !actor_number_is_ship(15.5) && !actor_number_is_ship(31));
    uint64_t seed=actor_seed(7,11,15,13,17,19);
    assert(seed==lockstep_seed(7,11,0x8000000500000003ULL,13,17,19));
    assert(seed!=actor_seed(7,11,3,13,17,19));
    dynamic_ships[1]=dynamic_ships[0];memset(&dynamic_ships[0],0,sizeof(dynamic_ships[0]));
    assert(actor_seed(7,11,16,13,17,19)==seed);
    ++dynamic_ships[1].handle;assert(actor_seed(7,11,16,13,17,19)!=seed);
    memset(dynamic_ships,0,sizeof(dynamic_ships));
}
static void *test_dynamic_object;
static void *test_dynamic_find(void *map,uint64_t runtime,unsigned flags) {
    assert(map && flags==4);return runtime==999?test_dynamic_object:NULL;
}
#include "test_lockstep_registry_raw.h"
#include "test_lockstep_registry_origin_identity.h"
#include "test_lockstep_registry_origin.h"
static DWORD WINAPI test_pending_update_thread(LPVOID object) {
    registry_record_update(object,999,REGISTRY_AI);return GetCurrentThreadId();
}
static void test_dynamic_registry(void) {
    const char *macro="ship_arg_xl_builder_01_a_macro";uint64_t handle=0,runtime=0;
    assert(registry_handle("0000000500000003",16,&handle) && handle==0x500000003ULL);
    assert(!registry_handle("8000000500000003",16,&runtime));
    assert(!registry_handle("0000000503000003",16,&runtime));
    assert(!registry_handle("0000000500000000",16,&runtime));
    assert(!registry_handle("000000050000000Z",16,&runtime));
    assert(registry_runtime_text("18446744073709551615",20,&runtime) && runtime==UINT64_MAX);
    assert(!registry_runtime_text("18446744073709551616",20,&runtime));
    assert(!registry_runtime_text("12ULL",5,&runtime));
    assert(registry_reserve(handle,macro,strlen(macro),"player",6,5)==15);
    assert(registry_reserve(handle,macro,strlen(macro),"player",6,5)==15 && dynamic_ship_count==1);
    assert(!registry_reserve(handle,macro,strlen(macro),"argon",5,5));
    assert(!registry_reserve(handle,macro,strlen(macro),"player",6,6));
    assert(!registry_reserve(handle-1,macro,strlen(macro),"player",6,5));
    for(unsigned i=1;i<16;i++) assert(registry_reserve(handle+i,macro,strlen(macro),"player",6,5)==15+i);
    assert(!registry_reserve(handle+16,macro,strlen(macro),"player",6,5));
    memset(dynamic_ships,0,sizeof(dynamic_ships));dynamic_ship_count=0;
    assert(registry_reserve(handle,macro,strlen(macro),"player",6,5)==15);
    unsigned char object[0x400]={0},m[0x40]={0},faction[0x40]={0},controller[0xe0]={0},agent[0xf20]={0};
    test_dynamic_object=object;*(void**)object=image+0x2b7feb8;*(uint64_t*)(object+8)=999;
    *(void**)(object+0x60)=m;*(void**)m=image+0x2ba5430;*(void**)(image+0x2ba5430+18*8)=image+0xefb10;
    *(const char**)(m+0x20)=macro;*(uint64_t*)(m+0x30)=strlen(macro);*(uint64_t*)(m+0x38)=31;
    *(void**)(image+0x2b7feb8+0x1610)=image+0x352af0;
    *(void**)(image+0x2b7feb8+0x1620)=image+0x352b10;*(void**)(object+0x348)=faction;
    *(void**)(image+0x2b99a88+0x1620)=image+0x352b10;
    memcpy(faction+0x10,"player",7);*(uint64_t*)(faction+0x20)=6;*(uint64_t*)(faction+0x28)=15;
    *(void**)(object+0x200)=controller;*(void**)controller=image+0x2c51158;*(void**)(controller+0x28)=object;
    *(void**)(controller+0x30)=agent+0xad0;*(void**)(controller+0x40)=agent;*(void**)(agent+0xef0)=object;
    void *found=NULL;unsigned char *slots=NULL;
    assert(registry_native_ship(15,999,test_dynamic_find,object,&found,&slots) && found==object && slots==agent+0xad0);
    faction[0x10]='a';assert(!registry_native_ship(15,999,test_dynamic_find,object,&found,&slots));
    assert(!strcmp(registry_stage,"identity_owner_name") && strstr(registry_identity_note,"owner_length=6 owner_capacity=15 owner_read=2 owner_name=alayer"));faction[0x10]='p';
    *(void**)(controller+0x28)=m;assert(!registry_native_ship(15,999,test_dynamic_find,object,&found,&slots));
    assert(!strcmp(registry_stage,"identity_controller_owner") && strstr(registry_identity_note,"controller_owner=0"));*(void**)(controller+0x28)=object;
    *(uint64_t*)(object+8)=998;assert(!registry_native_ship(15,999,test_dynamic_find,object,&found,&slots));
    assert(!strcmp(registry_stage,"identity_runtime") && strstr(registry_identity_note,"runtime_equal=0"));*(uint64_t*)(object+8)=999;
    *(void**)object=image+0x2b7feb0;assert(!registry_native_ship(15,999,test_dynamic_find,object,&found,&slots));
    assert(!strcmp(registry_stage,"identity_object_type") && strstr(registry_identity_note,"object_type=2b7feb0"));*(void**)object=image+0x2b7feb8;
    *(void**)m=image+0x2ba5428;assert(!registry_native_ship(15,999,test_dynamic_find,object,&found,&slots));
    assert(!strcmp(registry_stage,"identity_macro_type") && strstr(registry_identity_note,"macro_type=2ba5428"));*(void**)m=image+0x2ba5430;
    *(uint64_t*)(m+0x30)=64;assert(!registry_native_ship(15,999,test_dynamic_find,object,&found,&slots));
    assert(!strcmp(registry_stage,"identity_macro_name") && strstr(registry_identity_note,"macro_length=64 macro_capacity=31 macro_read=1 macro_name=unread"));*(uint64_t*)(m+0x30)=strlen(macro);
    *(void**)(controller+0x40)=NULL;assert(!registry_native_ship(15,999,test_dynamic_find,object,&found,&slots));
    assert(!strcmp(registry_stage,"identity_parameters") && strstr(registry_identity_note,"parameters=0 agent_owner=0"));*(void**)(controller+0x40)=agent;
    unsigned char object_before[sizeof(object)];memcpy(object_before,object,sizeof(object));
    registry_describe_identity(object,999);
    assert(!memcmp(object_before,object,sizeof(object)) && strstr(registry_identity_note,"reads=1fff"));
    assert(registry_native_ship(15,999,test_dynamic_find,object,&found,&slots) && !registry_identity_note[0]);
    stats.active=1;boot_phase=3;stats.issued=6;ui_thread=GetCurrentThreadId();
    *(void**)object=image+0x2b84a98;assert(!registry_native_ship(15,999,test_dynamic_find,object,&found,&slots));
    registry_observe_object(object,REGISTRY_NAV);assert(!registry_pending_count); /* Unsupported/base type cannot enroll. */
    *(void**)object=image+0x2b9bff8;assert(!registry_native_ship(15,999,test_dynamic_find,object,&found,&slots));
    registry_observe_object(object,REGISTRY_NAV);assert(!registry_pending_count); /* Separate Large class remains unsupported. */
    *(void**)object=image+0x2b99a88;assert(registry_native_ship(15,999,test_dynamic_find,object,&found,&slots));
    test_registry_raw(object,controller,agent);
    test_registry_origin_identity(object,controller,agent);
    test_registry_origin(object);
    registry_md_installed=1;registry_observe_object(object,REGISTRY_NAV);
    assert(registry_pending_count==1 && registry_pending[0].runtime==999 && registry_pending[0].kind==REGISTRY_NAV);
    assert(registry_pending[0].order==1 && registry_pending[0].thread==GetCurrentThreadId());
    registry_pending_update first_update=registry_pending[0];registry_record_update(object,999,REGISTRY_AI);
    assert(!memcmp(&first_update,&registry_pending[0],sizeof(first_update)));
    HANDLE pending_worker=CreateThread(NULL,0,test_pending_update_thread,object,0,NULL);assert(pending_worker);
    assert(WaitForSingleObject(pending_worker,1000)==WAIT_OBJECT_0);DWORD pending_thread=0;
    assert(GetExitCodeThread(pending_worker,&pending_thread) && pending_thread!=first_update.thread);CloseHandle(pending_worker);
    assert(!memcmp(&first_update,&registry_pending[0],sizeof(first_update))); /* Later worker cannot replace first kind/order/thread. */
    unsigned char exec[0x40]={0},context[0x128]={0},script_data[0x30]={0},tls_data[0x320]={0},tls_before[0x320];
    *(void**)(context+0xd8)=script_data;*(void**)(context+0x120)=object;
    memcpy(script_data+0x10,"order.wait",11);*(uint64_t*)(script_data+0x20)=10;*(uint64_t*)(script_data+0x28)=15;
    *(uint64_t*)(exec+0x18)=42;*(double*)(tls_data+0x300)=1.5;tls_data[0x314]=1;*(uint64_t*)(tls_data+0x318)=123456;
    memcpy(tls_before,tls_data,sizeof(tls_data));registry_stage="reservation";
    registry_attach_ai(exec,context,image+0x106e3fb,object,999,0,tls_data);
    registry_pending_ai first_ai=registry_pending[0].ai;
    assert(first_ai.order==2 && first_ai.recorded && first_ai.reads==31 && first_ai.tick==6 && first_ai.pc==42 && first_ai.type==0x2b99a88 &&
        first_ai.caller==0x106e3fb && first_ai.ui==1 && first_ai.view==1 && !strcmp(first_ai.script,"order.wait") &&
        !strcmp(first_ai.operation,"reservation") && !memcmp(tls_before,tls_data,sizeof(tls_data)));
    *(uint64_t*)(exec+0x18)=43;registry_stage="status";
    registry_attach_ai(exec,context,image+0x106e3fc,object,999,1,tls_data);
    registry_observe_ai(exec,context,image+0x106e3fc);
    assert(!memcmp(&first_ai,&registry_pending[0].ai,sizeof(first_ai))); /* First observation is immutable. */
    assert(registry_md_order==2);
    registry_pending_ai unsupported=registry_read_ai(NULL,NULL,NULL,NULL,0,NULL);
    assert(unsupported.recorded && !unsupported.reads && !strcmp(unsupported.script,"unsupported"));
    unsigned char other_object[sizeof(object)];memcpy(other_object,object,sizeof(object));*(uint64_t*)(other_object+8)=1000;
    registry_attach_ai(exec,context,image+0x106e3fb,other_object,1000,0,tls_data);assert(registry_pending_count==1);
    registry_record_update(other_object,1000,REGISTRY_AI);
    registry_attach_ai(NULL,NULL,NULL,other_object,1000,0,NULL);
    unsupported=registry_pending[1].ai;assert(unsupported.recorded && unsupported.reads==1 && !strcmp(unsupported.script,"unsupported"));
    registry_attach_ai(exec,context,image+0x106e3fb,other_object,1000,0,tls_data);
    assert(!memcmp(&unsupported,&registry_pending[1].ai,sizeof(unsupported))); /* Failed first read cannot be replaced. */
    memset(&registry_pending[1],0,sizeof(registry_pending[1]));registry_pending_count=1;
    registry_describe_identity(object,999);assert(strstr(registry_identity_note,"pending_tick=6 pending_kind=2"));
    assert(!dynamic_ships[0].late_updates && !dynamic_ships[0].bound_tick); /* Diagnostic cannot enroll/classify it. */
    assert(!registry_bind(15,999,test_dynamic_find,object) && !strcmp(registry_stage,"late_update"));
    assert(strstr(registry_identity_note,"object_type=2b99a88") && strstr(registry_identity_note,"pending_tick=6 pending_kind=2"));
    assert(strstr(registry_identity_note,"pending_ai=1") && strstr(registry_identity_note,"ai_operation=reservation") &&
        strstr(registry_identity_note,"ai_caller=106e3fb ai_pc=2a") && strstr(registry_identity_note,"ai_script=order.wait ai_order=2") &&
        strstr(registry_identity_note,"pending_order=1 pending_thread="));
    char update_receipt[384];assert(registry_update_format(0,update_receipt,sizeof(update_receipt)) &&
        strstr(update_receipt,"runtime=00000000000003e7 tick=6 kind=2 order=1 thread=") && strstr(update_receipt,"ai_order=2 ai_thread="));
    assert(!registry_update_format(2,update_receipt,sizeof(update_receipt)) && !registry_update_format(0,update_receipt,2));
    assert(!actor_components[14] && !actor_ids[14] && !params_done[14] && !dynamic_ships[0].bound_tick);
    assert(dynamic_ships[0].late_updates==1 && dynamic_ships[0].first_update_tick==6);
    dynamic_ship recycled={0};assert(!registry_late_update(&recycled,1000)); /* A new native ID is a new lifetime. */
    registry_record_update(object,999,REGISTRY_SHIP);assert(registry_pending_count==1 && registry_pending[0].kind==REGISTRY_NAV);
    registry_pending_count=REGISTRY_PENDING_CAPACITY;registry_record_update(object,1000,REGISTRY_SHIP);assert(registry_failures==1);
    registry_pending_count=0;registry_failures=0;memset(registry_pending,0,sizeof(registry_pending));
    actor_components[14]=object;actor_ids[14]=999;dynamic_ships[0].bound_tick=6;
    dynamic_ships[0].first_update_tick=0;dynamic_ships[0].first_update_kind=0;dynamic_ships[0].late_updates=0;
    registry_preparing_actor=15;registry_record_update(object,999,REGISTRY_PARAMS);
    assert(!dynamic_ships[0].first_update_tick && !registry_failures);
    ++ui_thread;registry_record_update(object,999,REGISTRY_PARAMS);--ui_thread;
    assert(dynamic_ships[0].first_update_tick==6 && dynamic_ships[0].late_updates==1 && registry_failures==1);
    registry_failures=0;dynamic_ships[0].late_updates=0;registry_record_update(object,999,REGISTRY_NAV);
    assert(dynamic_ships[0].late_updates==1 && registry_failures==1); /* Only intentional UI params are exempt. */
    registry_preparing_actor=0;registry_failures=0;actor_components[14]=NULL;actor_ids[14]=0;
    dynamic_ships[0].bound_tick=0;
    nav_sample first={.tick=6,.calls=1,.initialized=1,.rng_before=123};registry_nav_observe(15,&first);
    first.tick=7;first.rng_before=456;registry_nav_observe(15,&first);
    assert(registry_first_nav[0].tick==6 && registry_first_nav[0].rng_before==123);
    memset(registry_first_nav,0,sizeof(registry_first_nav));
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && world_identity_record(999,123));registry_observe_object(object,REGISTRY_NAV);assert(!registry_pending_count);
    assert(!registry_native_ship(15,999,test_dynamic_find,object,&found,&slots));
    assert(!strcmp(registry_stage,"identity_imported") && strstr(registry_identity_note,"imported=1"));
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;
    memset(dynamic_ships,0,sizeof(dynamic_ships));dynamic_ship_count=0;registry_stage="none";InterlockedExchange(&registry_observing,0);
    registry_md_installed=0;registry_md_order=0;
    stats.active=0;stats.issued=0;boot_phase=0;
}
#include "test_lockstep_solver.h"
#include "test_lockstep_job_rng.h"
#include "test_lockstep_script_rng.h"
#include "test_lockstep_script_rng_md.h"
#include "test_lockstep_script_rng_perf.h"
#include "test_lockstep_md_resume_rng.h"
#include "test_lockstep_find_order.h"
#include "test_lockstep_region_rng.h"
#include "test_lockstep_gravidar_deadlines.h"
#include "test_lockstep_options.h"
#include "test_lockstep_registry_md.h"
#include "test_lockstep_request.h"
#include "test_lockstep_thunk_page.h"
#include "test_lockstep_thunk_unwind.h"
#include "test_lockstep_portable_clocks.h"
#include "test_lockstep_creation_trace.h"
#include "test_lockstep_registry_origin_image.h"
#include "test_lockstep_wire_v3.h"
#include "test_lockstep_lookahead.h"
#include "test_lockstep_world_created.h"
#include "test_lockstep_runtime_ids.h"
#include "test_lockstep_portable_created.h"
#include "test_lockstep_created_load.h"
#include "test_lockstep_spawn_rng.h"
#include "test_lockstep_actor_all.h"
#include "test_lockstep_nav_params_created.h"
#include "test_lockstep_physics_trace.h"
#include "test_lockstep_body_order.h"
#include "test_lockstep_shape_order.h"
#include "test_lockstep_nav_octree.h"
#include "test_lockstep_event_rng.h"
#include "test_lockstep_build_rng.h"
#include "test_lockstep_ui_cargo.h"
#include "test_lockstep_supply_rows.h"
#include "test_lockstep_ui_lazy.h"
#include "test_lockstep_supply_wants.h"
#include "test_lockstep_loadout_order.h"
#include "test_lockstep_order_canon.h"
#include "test_lockstep_pick_order.h"
#include "test_lockstep_macro_arena.h"
#include "test_lockstep_build_rows.h"
#include "test_lockstep_economy_load.h"
#include "test_lockstep_fp_redirect.h"
#include "test_lockstep_step_chain.h"
int main(int argc,char **argv) {
    assert(argc<=2);
    if(argc==2) {test_solver_native_image(argv[1]);test_registry_md_native_image(argv[1]);test_request_native_image(argv[1]);test_registry_origin_native_image(argv[1]);test_event_order_native_image(argv[1]);test_render_rng_native_image(argv[1]);test_fo_native_image(argv[1]);test_pilot_native_image(argv[1]);test_pilot_native_controls_image(argv[1]);test_pilot_stock_image(argv[1]);test_virtual_players_image(argv[1]);test_mdt_native_image(argv[1]);test_mdr_native_image(argv[1]);test_region_rng_native_image(argv[1]);test_created_native_image(argv[1]);test_bullet_cadence_native_image(argv[1]);test_speak_timer_native_image(argv[1]);test_shield_canonical_native_image(argv[1]);test_nav_cadence_native_image(argv[1]);test_nav_player_native_image(argv[1]);test_command_order_native_image(argv[1]);test_region_camera_native_image(argv[1]);test_weapon_fire_native_image(argv[1]);test_motion_watch_native_image(argv[1]);test_view_points_native_image(argv[1]);test_camera_offset_native_image(argv[1]);test_mass_traffic_native_image(argv[1]);test_predict_native_image(argv[1]);test_spawn_rng_native_image(argv[1]);test_hitn_native_image(argv[1]);test_pfire_native_image(argv[1]);test_gdl_native_image(argv[1]);test_ship_sound_native_image(argv[1]);test_physics_trace_native_image(argv[1]);test_body_order_native_image(argv[1]);test_shape_order_native_image(argv[1]);test_player_timer_native_image(argv[1]);test_engine_rng_native_image(argv[1]);test_nav_octree_native_image(argv[1]);test_warp_rng_native_image(argv[1]);test_transition_rows_native_image(argv[1]);test_tempzone_order_native_image(argv[1]);test_gravidar_posts_native_image(argv[1]);test_event_rng_native_image(argv[1]);test_build_rng_native_image(argv[1]);test_ui_cargo_native_image(argv[1]);test_supply_rows_native_image(argv[1]);test_ui_lazy_native_image(argv[1]);test_gravidar_wide_native_image(argv[1]);test_portable_math_native_image(argv[1]);test_exact_reciprocal_native_image(argv[1]);test_fp_env_native_image(argv[1]);test_tick_native_image(argv[1]);test_path_budget_native_image(argv[1]);test_decimal_native_image(argv[1]);test_supply_wants_native_image(argv[1]);test_loadout_order_native_image(argv[1]);test_order_canon_native_image(argv[1]);test_pick_order_native_image(argv[1]);test_macro_arena_native_image(argv[1]);test_build_rows_native_image(argv[1]);test_economy_load_native_image(argv[1]);test_pilot_slots_image(argv[1]);}
    if(argc==2)test_wq_image_path=argv[1];
    if(argc==2) test_runtime_ids_native_image(argv[1]);
    if(argc==2) test_ui_replay_native_image(argv[1]);
    if(argc==2) test_fp_redirect_native_image(argv[1]);
    if(argc==2) test_step_chain_native_image(argv[1]);
    test_registry_md_options();
    test_player_authority();
    test_actor_registry_roles();
    char first[9]="unchange",second[9]="unchange";
    const char *prefix="NGGO|2|test|1|61";
    assert(gate_permit_inputs("NGGO|2|test|1|61|02010000|01070100",prefix,1,"02010000",first,second)==1);
    assert(!strcmp(first,"02010000")&&!strcmp(second,"01070100"));
    assert(gate_permit_inputs("NGGO|2|test|1|61|02010000|01070100",prefix,2,"02010000",first,second)==-1);
    assert(!gate_permit_inputs("NGGO|2|test|1|62|02010000|01070100",prefix,1,"02010000",first,second));
    assert(!gate_permit_inputs("NGGO|2|test|1|61|02010000|0107010G",prefix,1,"02010000",first,second));
    assert(!gate_permit_inputs("NGGO|2|test|1|61|02010000|01070100extra",prefix,1,"02010000",first,second));
    assert(!gate_permit_inputs("NGGO|2|test|1|61",prefix,1,"02010000",first,second));
    test_wire_v3();
    const char *token="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const char *hash="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    assert(x4gate_start(0,12345,token,1000,hash)==10);
    assert(x4gate_start(1,12345,token,1000,hash)==11); /* Never patches a non-X4 process. */
    assert(!installed && !used);
    test_boot_option_composition();
    assert(boot_next(0,0)==0 && boot_next(0,1)==0);
    assert(boot_next(1,0)==1 && boot_next(1,1)==2);
    assert(boot_next(2,1)==2 && boot_next(2,0)==3 && boot_next(3,1)==3);
    image=VirtualAlloc(NULL,0x6fa9000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    test_solver_abi(NULL);test_solver_observation();test_request_observation();
    assert(image);test_player_observation();test_player_fire();test_player_launch_rng();
    assert(image);test_contacts();test_dock_shape_identity();test_dock_extra_shape_identity();test_world_identity();test_world_hash_semantics();test_world_reader_equivalence();test_snapshot_fixture();test_dynamic_registry();test_world_trace();
    test_registry_md();
    test_world_wait();
    test_ai_wait();
    test_world_action();
    test_path_determinism();
    test_travel_determinism();
    test_travel_snapshot();
    test_path_perf_equivalence();
    test_ai_schedule();
    test_event_order();
    test_gravidar_watch();
    test_speech_time();
    test_world_created();
    test_runtime_ids();
    test_runtime_ids_local();
    test_render_rng();
    test_relay_host();
    test_digest();
    test_sample_primitives();
    test_bullet_cadence();
    test_speak_timer();
    test_cutscene_ready();
    test_nav_cadence();
    test_nav_probe_init();
    test_nav_player();
    test_nav_params_init();
    test_ship_sound();
    test_draw_watch();
    test_draw_watch_frame();
    test_player_timer();
    test_warp_rng();
    test_transition_rows();
    test_tempzone_order();
    test_nav_octree_sites();
    test_gravidar_posts();
    test_gravidar_wide();
    test_gravidar_wide_sort();
    test_command_order();
    test_view_points();
    test_view_rows();
    test_view_source();
    test_camera_offset();
    test_mass_traffic();
    test_gravidar_digest();
    test_attention_all();
    test_region_camera();
    test_rc_field_fill();
    test_rc_traffic_rows();
    test_rc_traffic_pass();
    test_rc_standins();
    test_view_scenes();
    test_weapon_fire();
    test_engine_rng();
    test_portable_math();
    test_exact_reciprocal();
    test_fp_env();
    test_tick();
    test_fast_reads();
    test_path_budget_emit();
    test_decimal();
    test_event_rng();
    test_build_rng();
    test_ui_cargo();
    test_supply_rows();
    test_ui_lazy();
    test_supply_wants();
    test_loadout_order();
    test_fp_redirect();
    test_order_canon();
    test_pick_order();
    test_macro_arena();
    test_build_rows();
    test_economy_load();
    test_camera_follow();
    test_motion_watch();
    test_physics_trace();
    test_body_order();
    test_shape_order();
    test_nav_octree();
    test_nav_octree_deadline();
    test_nav_octree_agreed();
    test_nav_octree_snapshot();
    test_pilot_inputs();
    test_pilot_native_controls();
    test_pilot_stock();
    test_pilot_slots();
    test_crash_note();
    test_ui_replay();
    test_pilot_device();
    test_pilot_camera();
    test_pilot_predict();
    test_hit_claims();
    test_hit_native();
    test_pilot_fire();
    test_virtual_players();
    test_lookahead();
    assert(image);
    assert(!damage_install());damage_original_word=123;damage_patched_word=456;
    *(uint64_t*)(image+DAMAGE_ENTRY)=123;assert(damage_swap(123,456) && !damage_swap(123,789));
    damage_installed=1;damage_restore();assert(damage_restored && !damage_installed && *(uint64_t*)(image+DAMAGE_ENTRY)==123);
    unsigned char damaged_group[0x78]={0};*(double*)(damaged_group+0x68)=100.0;damage_event damaged={0};test_calls=0;
    assert(damage_observe(test_damage_original,&damaged,damaged_group,&test_rng,2.75,3,7.0)==2.75);
    double old_charge,new_charge;memcpy(&old_charge,&damaged.before[0],8);memcpy(&new_charge,&damaged.after[0],8);
    assert(test_calls==1 && old_charge==100.0 && new_charge==97.25 && damaged.priority==3 && !damage_failures);
    unsigned char radar_worker[0x7000]={0};*(void**)radar_worker=image+0x2c68860;
    *(uint32_t*)(radar_worker+0x40)=2;*(uint32_t*)(radar_worker+0x44)=1;radar_worker[0x6988]=1;
    InitializeCriticalSection((CRITICAL_SECTION*)(radar_worker+0x6960));test_calls=0;
    assert(gravidar_frame(test_gravidar_original,radar_worker,1000));
    WaitForSingleObject(test_gravidar_thread,1000);CloseHandle(test_gravidar_thread);
    assert(test_calls==1 && gravidar_frames==1 && gravidar_waits==1 && !gravidar_timeouts && !gravidar_in_frame);
    radar_worker[0x6988]=0;assert(!gravidar_frame(test_gravidar_original,radar_worker,0));
    assert(test_calls==1 && gravidar_timeouts==1 && !gravidar_in_frame);
    radar_worker[0x6988]=1;*(uint32_t*)(radar_worker+0x44)=2;assert(!gravidar_wait(radar_worker,0));
    *(uint32_t*)(radar_worker+0x44)=1;*(uint32_t*)(radar_worker+0x40)=0;assert(!gravidar_wait(radar_worker,0));
    DeleteCriticalSection((CRITICAL_SECTION*)(radar_worker+0x6960));
    gravidar_frames=gravidar_waits=gravidar_timeouts=0;test_calls=0;
    assert(!gravidar_install(0,0));
    gravidar_original_word=123;gravidar_patched_word=456;*(uint64_t*)(image+GRAVIDAR_WORD)=123;
    assert(gravidar_swap(123,456) && !gravidar_swap(123,789));gravidar_installed=1;
    *(void**)(image+GRAVIDAR_UNIT_ENTRY)=image+GRAVIDAR_UNIT_APPLY;
    assert(gravidar_unit_swap(image+GRAVIDAR_UNIT_APPLY,(void*)gravidar_unit));gravidar_unit_installed=1;
    *(void**)(image+GRAVIDAR_UNIT_ENTRY)=NULL;gravidar_restore();
    assert(!gravidar_restored && !gravidar_installed && gravidar_unit_installed && !*(void**)(image+GRAVIDAR_UNIT_ENTRY));
    *(void**)(image+GRAVIDAR_UNIT_ENTRY)=(void*)gravidar_unit;gravidar_restore();assert(gravidar_restored && !gravidar_unit_installed);
    assert(!shield_refresh_native(NULL) && !shield_refresh_verified && !shield_refresh_calls);
    assert(!spline_install(0,0) && !spline_installed);
    void *spline_original=image+SPLINE_UPDATE;
    memcpy(image+SPLINE_ENTRY,&spline_original,8);
    assert(spline_swap(spline_original,(void*)spline_update));
    assert(!spline_swap(spline_original,NULL));
    spline_installed=1;spline_restore();
    assert(spline_restored && !spline_installed && *(void**)(image+SPLINE_ENTRY)==spline_original);
    uint64_t value=0x1122334455667788ULL,changed=0x11223344aabbccddULL;
    memcpy(image+WORD_RVA,&value,8);
    assert(swap_word(value,changed));assert(*(uint64_t*)(image+WORD_RVA)==changed);
    assert(!swap_word(value,0));assert(*(uint64_t*)(image+WORD_RVA)==changed);
    assert(swap_word(changed,value));
    memcpy(image+FLIGHT_CTOR,&value,8);
    assert(flight_ctor_swap(value,changed));assert(!flight_ctor_swap(value,0));
    flight_ctor_original_word=value;flight_ctor_patched_word=changed;flight_ctor_installed=1;
    flight_restore();assert(flight_ctor_restored && !flight_ctor_installed && *(uint64_t*)(image+FLIGHT_CTOR)==value);
    if(IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128)) {
        unsigned __int128 a=((unsigned __int128)value<<64)|changed;
        unsigned __int128 b=((unsigned __int128)changed<<64)|value;
        memcpy(image+0xef4610,&a,16);
        assert(swap_path(a,b));assert(*(unsigned __int128*)(image+0xef4610)==b);
        assert(!swap_path(a,0));assert(*(unsigned __int128*)(image+0xef4610)==b);
        assert(swap_path(b,a));
    }
    original_word=value;patched_word=changed;installed=1;stats.active=1;
    memcpy(image+WORD_RVA,&changed,8);finish(1);
    assert(stats.restored && !installed && !stats.active && stats.reason==1);
    installed=1;stats.active=1;stats.restored=0;
    memcpy(image+WORD_RVA,&value,8); /* A different owner has already changed it. */
    finish(3);assert(!stats.restored && !stats.active && installed);
    assert(*(uint64_t*)(image+WORD_RVA)==value); /* Never overwrite a foreign patch. */
    *(unsigned short*)(image+0xfa3302)=0x0874;
    assert(swap_dt_branch(0x0874,0x9066));
    assert(!swap_dt_branch(0x0874,0));
    saved_dt_bits=value;fixed_dt_bits=changed;
    *(uint64_t*)(image+0x6adfc60)=changed;dt_installed=1;
    restore_dt();assert(dt_restored && !dt_installed);
    assert(*(uint64_t*)(image+0x6adfc60)==value && *(unsigned short*)(image+0xfa3302)==0x0874);
    dt_installed=1;dt_restored=0;
    *(unsigned short*)(image+0xfa3302)=0xcccc;
    *(uint64_t*)(image+0x6adfc60)=0;
    restore_dt();assert(!dt_restored && dt_installed);
    assert(*(unsigned short*)(image+0xfa3302)==0xcccc && !*(uint64_t*)(image+0x6adfc60));
    assert(flight_seed(1,1,1)!=flight_seed(1,2,1));
    assert(flight_seed(1,1,1)!=flight_seed(2,1,1));
    assert(flight_seed(1,1,1)!=flight_seed(1,1,2));
    /* Legacy XOR packing made actor2/slot1 and actor1/slot2 identical. */
    assert(lockstep_seed(7,1,2,1,1,0)!=lockstep_seed(7,1,1,2,1,0));
    assert(lockstep_seed(7,1,1,0,1,0)!=lockstep_seed(7,1,0,1,1,0));
    assert(lockstep_seed(7,1,1,1,1,0)!=lockstep_seed(8,1,1,1,1,0));
    assert(lockstep_seed(7,1,1,1,1,0)!=lockstep_seed(7,1,1,0,0,0));
    assert(lockstep_seed(7,1,1,1,1,0)!=lockstep_seed(7,1,1,1,1,1));
    *(void**)(image+FLIGHT_ENTRY)=image+FLIGHT_UPDATE;
    assert(flight_swap(image+FLIGHT_UPDATE,(void*)flight_update));
    assert(!flight_swap(image+FLIGHT_UPDATE,NULL));
    flight_installed=1;flight_restore();assert(flight_restored && !flight_installed);
    assert(*(void**)(image+FLIGHT_ENTRY)==image+FLIGHT_UPDATE);
    flight_sample sample={0};sample.calls=1;uint64_t out1,out2;
    stats.issued=14;flight_seeded=1;flight_full=1;test_rng=111;
    flight_execute(test_flight_original,&sample,3,&test_rng,NULL,&out1,1.0f/60.0f,7.0,1,0);
    assert(test_rng==111 && test_calls==1 && test_cheap==0);
    test_rng=222;
    flight_execute(test_flight_original,&sample,3,&test_rng,NULL,&out2,1.0f/60.0f,7.0,1,0);
    assert(test_rng==222 && test_calls==2 && out1==out2);
    flight_seeded=0;flight_full=0;
    flight_execute(test_flight_original,&sample,3,&test_rng,NULL,&out2,1.0f/60.0f,7.0,1,0);
    assert(test_rng==out2 && test_calls==3 && test_cheap==1);
    unsigned char context[0x200]={0},pilot[0x200]={0},ship[0x200]={0},script[0x80]={0};
    *(void**)(context+0x120)=pilot;*(void**)(pilot+0x70)=ship;
    *(uint64_t*)(ship+8)=123;actor_components[2]=ship;actor_ids[2]=123;
    assert(ai_actor(context)==3);
    actor_ids[2]=456;assert(ai_actor(context)==0); /* Pointer reuse alone is not identity. */
    *(void**)(pilot+0x70)=pilot;assert(ai_actor(context)==0);
    *(void**)(context+0xd8)=script;strcpy((char*)script+0x10,"test.attack");
    *(uint64_t*)(script+0x20)=11;*(uint64_t*)(script+0x28)=15;
    char name[96];assert(ai_script(context,name) && !strcmp(name,"test.attack"));
    *(uint64_t*)(script+0x20)=96;assert(!ai_script(context,name));
    assert(!radar_install());
    *(void**)(image+RADAR_ENTRY)=image+RADAR_HANDLER;
    assert(radar_swap(image+RADAR_HANDLER,(void*)radar_handler));
    assert(!radar_swap(image+RADAR_HANDLER,NULL));
    radar_installed=1;radar_restore();assert(radar_restored && !radar_installed);
    *(uint64_t*)(script+0x20)=11;actor_ids[2]=123;*(void**)(pilot+0x70)=ship;
    test_radar_context=context;test_radar_source=ship;test_calls=0;
    stats.active=1;boot_phase=3;ui_thread=GetCurrentThreadId();
    radar_execute(test_radar_original,context,ship,1234,0);
    assert(test_calls==1 && radar_calls==1 && radar_event_count==1 && !radar_failures);
    assert(radar_events[0].actor==3 && radar_events[0].source==3 && radar_events[0].flags==1234 &&
           radar_events[0].ordinal==1 && radar_events[0].tick==stats.issued && !strcmp(radar_events[0].script,"test.attack"));
    test_radar_source=NULL;radar_execute(test_radar_original,context,NULL,1234,0);
    assert(test_calls==2 && radar_events[1].source==0 && radar_events[1].ordinal==2);
    radar_event_count=RADAR_EVENT_LIMIT;radar_execute(test_radar_original,context,NULL,1234,0);
    assert(test_calls==3 && !radar_failures && radar_dropped==1 && radar_event_count==RADAR_EVENT_LIMIT);radar_dropped=0;
    ui_thread=GetCurrentThreadId()+1;radar_execute(test_radar_original,context,NULL,1234,0);
    assert(test_calls==4 && radar_failures==1 && radar_calls==3);
    /* Play mode records nothing; another thread stays a stop, the serial radar key depends on it. */
    ui_thread=GetCurrentThreadId();gate_play_mode=1;radar_event_count=0;radar_execute(test_radar_original,context,NULL,1234,0);
    assert(test_calls==5 && !radar_event_count && radar_calls==4 && radar_failures==1);gate_play_mode=0;--radar_calls;test_calls=4;
    ui_thread=GetCurrentThreadId()+1;
    ui_thread=GetCurrentThreadId();radar_event_count=0;radar_failures=0;radar_calls=0;
    actor_ids[2]=456;radar_execute(test_radar_original,context,NULL,1234,0);
    assert(test_calls==5 && !radar_calls && !radar_event_count && !radar_failures);
    actor_ids[2]=123;test_calls=0;test_rng=100;
    unsigned char irq_exec[0x50]={0};*(uint32_t*)(irq_exec+0x38)=0x66;*(uintptr_t*)(irq_exec+0x40)=(uintptr_t)context+0xa8;
    *(uint32_t*)(context+0x18)=0x506;*(uint64_t*)(context+0x140)=368;
    assert(interrupt_observe(test_interrupt_original,&test_rng,irq_exec,&test_calls,11,22,1,0)==0x1234567812345678ULL);
    assert(test_calls==1 && test_rng==223 && interrupt_calls==1 && interrupt_count==1 && !interrupt_failures);
    assert(interrupt_samples[0].actor==3 && interrupt_samples[0].kind==1 && interrupt_samples[0].pc==368 &&
           interrupt_samples[0].begin==11 && interrupt_samples[0].end==22 && interrupt_samples[0].rng_before==100 && interrupt_samples[0].rng_after==223);
    interrupt_event_observe(test_interrupt_event_original,&test_rng,context,0);
    assert(test_calls==2 && test_rng==457 && interrupt_calls==2 && interrupt_samples[1].flags_before==0 && interrupt_samples[1].flags_after==4);
    interrupt_seeded=1;test_rng=999;interrupt_actor_tick[2]=0;
    interrupt_observe(test_interrupt_original,&test_rng,irq_exec,&test_calls,11,22,1,0);
    uint64_t irq_seed=interrupt_samples[2].rng_before,irq_after=interrupt_samples[2].rng_after;
    assert(test_rng==999 && irq_after==irq_seed+123 && interrupt_seeds==1);
    test_rng=12345;interrupt_actor_tick[2]=0;
    interrupt_observe(test_interrupt_original,&test_rng,irq_exec,&test_calls,11,22,1,0);
    assert(test_rng==12345 && interrupt_samples[3].rng_before==irq_seed && interrupt_samples[3].rng_after==irq_after);
    interrupt_observe(test_interrupt_original,&test_rng,irq_exec,&test_calls,11,22,1,0);
    assert(test_rng==12345 && interrupt_samples[4].rng_before!=irq_seed && interrupt_seeds==3);
    interrupt_seeded=0;test_calls=2;
    /* A full table stops recording and the game goes on; only the key that samples it stops the gate. */
    ifault_reset();
    interrupt_count=INTERRUPT_LIMIT;interrupt_event_observe(test_interrupt_event_original,&test_rng,context,0);
    assert(test_calls==3 && !interrupt_failures && interrupt_dropped==1 && !ifault_total && interrupt_count==INTERRUPT_LIMIT);
    interrupt_seeded=1;interrupt_event_observe(test_interrupt_event_original,&test_rng,context,0);
    assert(test_calls==4 && interrupt_failures==1 && interrupt_dropped==2);interrupt_seeded=0;
    /* Another thread: a fault with a row, and a stop only for that key. */
    interrupt_failures=0;interrupt_count=0;ifault_reset();ui_thread=GetCurrentThreadId()+1;
    interrupt_event_observe(test_interrupt_event_original,&test_rng,context,0);
    assert(test_calls==5 && !interrupt_failures && ifault_counts[IFAULT_THREAD]==1 && !interrupt_count);
    char fault_row[256];assert(ifault_row(0,fault_row,sizeof(fault_row))>0 && strstr(fault_row,"site=thread actor=3 "));
    assert(!ifault_row(1,fault_row,sizeof(fault_row)));
    ui_thread=GetCurrentThreadId();
    /* Play mode: nothing is recorded and nothing faults. */
    gate_play_mode=1;interrupt_calls=0;ifault_reset();
    interrupt_event_observe(test_interrupt_event_original,&test_rng,context,0);
    assert(test_calls==6 && !interrupt_count && !interrupt_calls && !ifault_total);
    gate_play_mode=0;
    interrupt_count=0;interrupt_failures=0;interrupt_calls=0;ifault_reset();
    unsigned char step_exec[0x40]={0},step_node[0x28]={0};
    uintptr_t step_nodes[]={(uintptr_t)step_node};uintptr_t step_program[]={(uintptr_t)step_nodes,(uintptr_t)(step_nodes+1)};
    *(void**)step_node=image+0x1000;*(void**)(image+0x1018)=image+0x2000;*(unsigned*)(step_node+0x10)=17;
    *(void**)(step_exec+0x10)=step_program;
    interrupt_sample step_parent={0};step_parent.actor=3;step_parent.ordinal=42;
    interrupt_current=&step_parent;test_calls=0;test_rng=100;
    interrupt_step_observe(test_interrupt_step_original,step_exec,&test_rng);
    assert(test_calls==1 && test_rng==107 && interrupt_step_count==1 && interrupt_step_calls==1 && !interrupt_failures);
    interrupt_step_sample *st=&interrupt_steps[0];
    assert(st->parent==42 && st->actor==3 && st->pc==0 && st->next==1 && st->type==0x1000 && st->action==0x2000 &&
        st->chance==17 && st->flags_before==0 && st->flags_after==1 && st->rng_before==100 && st->rng_after==107);
    assert(step_nodes[0]==(uintptr_t)step_node && *(unsigned*)(step_node+0x10)==17);
    /* Invalid program bounds still call the original once; the fault is named and nothing stops. */
    strcpy(step_parent.script,"test.script");
    interrupt_step_observe(test_interrupt_step_original,step_exec,&test_rng);
    assert(test_calls==2 && !interrupt_failures && ifault_counts[IFAULT_STEP_READ]==1);
    assert(ifault_row(0,fault_row,sizeof(fault_row))>0 && strstr(fault_row,"site=step_read actor=3 pc=1 depth=0 script=test.script"));
    interrupt_current=NULL;interrupt_step_observe(test_interrupt_step_original,step_exec,&test_rng);
    assert(test_calls==3 && interrupt_step_count==2 && ifault_total==1);
    interrupt_current=&step_parent;interrupt_step_count=INTERRUPT_STEP_LIMIT;
    interrupt_step_observe(test_interrupt_step_original,step_exec,&test_rng);
    assert(test_calls==4 && !interrupt_failures && interrupt_step_dropped==1 && interrupt_step_count==INTERRUPT_STEP_LIMIT);
    /* With the key that samples these, the same read stops the gate, alike on every client. */
    interrupt_step_count=2;interrupt_seeded=1;interrupt_step_observe(test_interrupt_step_original,step_exec,&test_rng);
    assert(test_calls==5 && interrupt_failures==1 && ifault_counts[IFAULT_STEP_READ]==2);interrupt_seeded=0;
    /* The rows of one site are bounded, its count is not. */
    for(unsigned i=0;i<20;i++) interrupt_fault(IFAULT_STEP_READ,3,1,0,"x");
    assert(ifault_counts[IFAULT_STEP_READ]==22 && ifault_rows==IFAULT_ROWS_PER_SITE);
    char fault_status[1024];assert(ifault_status(fault_status,sizeof(fault_status))>0 && strstr(fault_status,"faults=22 ") && strstr(fault_status,"sites=step_read:22"));
    interrupt_current=NULL;interrupt_step_count=0;interrupt_step_calls=0;interrupt_failures=0;ifault_reset();
    unsigned char nested_exec[0x50]={0},nested_child[0x50]={0};
    *(void**)(nested_exec+0x10)=step_program;*(uint64_t*)(nested_exec+0x18)=1;*(uint32_t*)(nested_exec+0x38)=0x5d;
    memcpy(nested_child,nested_exec,sizeof(nested_child));
    interrupt_step_sample nested_step={0};nested_step.ordinal=77;
    interrupt_current=&step_parent;interrupt_step_active=&nested_step;test_nested_child=nested_child;test_rng=100;test_calls=0;
    nested_action_observe(test_nested_original,step_node,nested_exec,nested_exec+0x38,&test_rng);
    assert(test_calls==2 && test_rng==122 && nested_action_count==2 && nested_action_calls==2 && !interrupt_failures && !nested_current);
    assert(nested_actions[0].step==77 && nested_actions[0].interrupt==42 && nested_actions[0].owner==0x5d &&
           nested_actions[0].depth==1 && !nested_actions[0].parent && nested_actions[0].before==100 && nested_actions[0].after==122);
    assert(nested_actions[1].parent==1 && nested_actions[1].depth==2 && nested_actions[1].before==111 && nested_actions[1].after==122 &&
           nested_actions[1].program==nested_actions[0].program && nested_actions[1].pc==0 && nested_actions[1].next==2);
    interrupt_step_active=NULL;nested_action_observe(test_nested_original,step_node,nested_exec,nested_exec+0x38,&test_rng);
    assert(test_calls==3 && nested_action_calls==2 && !interrupt_failures);
    interrupt_step_active=&nested_step;nested_action_count=NESTED_ACTION_LIMIT;
    nested_action_observe(test_nested_original,step_node,nested_exec,nested_exec+0x38,&test_rng);
    assert(test_calls==4 && !interrupt_failures && nested_action_dropped==1 && nested_action_calls==2);
    /* The observer's own checks, each named, none fatal. */
    nested_action_count=0;ifault_reset();*(uint64_t*)(nested_exec+0x18)=1;
    nested_action_observe(test_nested_any_frame,step_node,nested_exec,nested_exec,&test_rng);
    assert(test_calls==5 && !interrupt_failures && ifault_counts[IFAULT_ACTION_FRAME]==1);
    *(uint64_t*)(nested_exec+0x18)=1;step_nodes[0]=(uintptr_t)step_exec;
    nested_action_observe(test_nested_original,step_node,nested_exec,nested_exec+0x38,&test_rng);
    assert(test_calls==6 && !interrupt_failures && ifault_counts[IFAULT_ACTION_READ]==1);
    step_nodes[0]=(uintptr_t)step_node;
    interrupt_step_active=NULL;interrupt_current=NULL;nested_action_count=0;nested_action_calls=0;interrupt_failures=0;ifault_reset();
    test_npc_names();
    test_npc_pilot();
    test_npc_provenance_early();
    test_interrupt_origin();
    test_npc_speech();
    test_npc_stub_abi(0);
    test_npc_stub_abi(1);
    test_npc_deadline_eligibility();
    test_npc_completion_lifetime();
    test_npc_ready_lifetime();
    *(uint64_t*)(nested_exec+0x18)=1;*(uintptr_t*)(nested_exec+0x40)=0;
    test_calls=0;test_rng=100;
    /* play26-long, reason 68 at tick 15547: nothing reads these rows without native_gate_npc_lifecycle or
     * native_gate_npc_diag, so nothing records them; past the limit is as good as before it. */
    uint64_t kept_options=boot_options;boot_options&=~281474976710656ULL;npc_action_rows=0;
    for(unsigned i=0;i<NPC_ACTION_LIMIT+76u;i+=4096) {
        npc_action_observe(test_nested_original,step_node,nested_exec,nested_exec+0x38,&test_rng);
        assert(!npc_action_count && !npc_action_calls && !interrupt_failures && !ifault_total);
    }
    assert(test_calls==5);test_calls=0;
    /* With a reader: a full table loses rows and keeps the session. */
    npc_action_rows=1;
    npc_action_count=NPC_ACTION_LIMIT;
    npc_action_observe(test_nested_original,step_node,nested_exec,nested_exec+0x38,&test_rng);
    assert(test_calls==1 && npc_action_dropped==1 && !interrupt_failures && npc_action_count==NPC_ACTION_LIMIT);
    npc_action_count=0;test_calls=0;test_rng=100;ifault_reset();
    npc_action_observe(test_nested_original,step_node,nested_exec,nested_exec+0x38,&test_rng);
    assert(test_calls==1 && test_rng==111 && npc_action_count==1 && npc_action_calls==1 && !interrupt_failures && ifault_counts[IFAULT_NPC_NAME]==1);
    assert(!npc_actions[0].named && !strcmp(npc_actions[0].cue,"unreadable"));
    assert(ifault_row(0,fault_row,sizeof(fault_row))>0 && strstr(fault_row,"site=npc_name") && strstr(fault_row,"script=unreadable"));
    /* Play mode wins over a reader. */
    gate_play_mode=1;npc_action_count=0;npc_action_observe(test_nested_original,step_node,nested_exec,nested_exec+0x38,&test_rng);
    assert(!npc_action_count);gate_play_mode=0;
    ifault_reset();
    /* Always-installed observers must unwind even with NPC47/48 disabled.
     * Their previous pointers may be non-null; never just clear everything. */
    test_action_throw=1;
    for(unsigned stop=0;stop<2;stop++)for(unsigned path=0;path<3;path++) {
        stats.active=1;test_action_stop=stop;test_calls=0;test_nested_child=NULL;
        interrupt_failures=0;ifault_reset();interrupt_step_count=nested_action_count=npc_action_count=0;
        interrupt_step_calls=nested_action_calls=npc_action_calls=0;
        *(uint64_t*)(step_exec+0x18)=0;*(uint64_t*)(nested_exec+0x18)=1;
        interrupt_current=&step_parent;interrupt_step_active=&nested_step;
        nested_action_sample old_nested={0};nested_current=&old_nested;
        npc_action_sample old_npc={0},old_speech={0};npc_current=&old_npc;npc_speech_owner=&old_speech;
        unsigned caught=0;
        __try {
            if(path==0)interrupt_step_observe(test_interrupt_step_original,step_exec,&test_rng);
            else if(path==1)nested_action_observe(test_nested_original,step_node,nested_exec,nested_exec+0x38,&test_rng);
            else npc_action_observe(test_nested_original,step_node,nested_exec,nested_exec+0x38,&test_rng);
        } __except(GetExceptionCode()==0xe04d5044?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=1;}
        assert(caught && test_calls==1 && !interrupt_failures && ifault_total && interrupt_current==&step_parent &&
            interrupt_step_active==&nested_step && nested_current==&old_nested && npc_current==&old_npc && npc_speech_owner==&old_speech);
        if(path==0)assert(interrupt_step_count==1 && !interrupt_steps[0].next && !interrupt_steps[0].rng_after);
        else assert(nested_action_count==1 && !nested_actions[0].next && !nested_actions[0].after);
        if(path==2)assert(npc_action_count==1 && !npc_actions[0].state.next && !npc_actions[0].state.after);
        interrupt_current=NULL;interrupt_step_active=NULL;nested_current=NULL;npc_current=NULL;npc_speech_owner=NULL;
    }
    test_action_throw=test_action_stop=0;stats.active=1;
    interrupt_step_count=nested_action_count=0;interrupt_step_calls=nested_action_calls=0;
    npc_action_count=0;npc_action_calls=0;interrupt_failures=0;ifault_reset();npc_action_rows=0;boot_options=kept_options;
    nested_action_original=123;nested_action_patched=456;*(unsigned __int128*)(image+NESTED_ACTION_WORD)=123;
    assert(nested_action_swap(123,456));nested_action_installed=1;
    *(unsigned __int128*)(image+NESTED_ACTION_WORD)=789;assert(!nested_action_restore() && nested_action_installed);
    assert(*(unsigned __int128*)(image+NESTED_ACTION_WORD)==789);
    *(unsigned __int128*)(image+NESTED_ACTION_WORD)=456;assert(nested_action_restore() && !nested_action_installed);
    interrupt_step_original_word=123;interrupt_step_patched_word=456;*(uint64_t*)(image+INTERRUPT_STEP_ENTRY)=123;
    assert(interrupt_step_swap(123,456));interrupt_step_installed=1;
    *(uint64_t*)(image+INTERRUPT_STEP_ENTRY)=789;assert(!interrupt_step_restore() && interrupt_step_installed);
    assert(*(uint64_t*)(image+INTERRUPT_STEP_ENTRY)==789);
    *(uint64_t*)(image+INTERRUPT_STEP_ENTRY)=456;assert(interrupt_step_restore() && !interrupt_step_installed);
    interrupt_original_word=0x1122334455667788ULL;interrupt_patched_word=0x11909012345678e9ULL;
    *(uint64_t*)(image+INTERRUPT_ENTRY)=interrupt_original_word;
    *(void**)(image+INTERRUPT_EVENT_ENTRY)=image+INTERRUPT_EVENT_HANDLER;
    assert(interrupt_swap(interrupt_original_word,interrupt_patched_word));
    assert(interrupt_event_swap(image+INTERRUPT_EVENT_HANDLER,(void*)interrupt_event));
    interrupt_installed=interrupt_event_installed=1;interrupt_restore();
    assert(interrupt_restored && !interrupt_installed && !interrupt_event_installed && *(uint64_t*)(image+INTERRUPT_ENTRY)==interrupt_original_word);
    *(uint64_t*)(image+INTERRUPT_ENTRY)=0x9999;interrupt_installed=1;interrupt_restore();
    assert(!interrupt_restored && interrupt_installed && *(uint64_t*)(image+INTERRUPT_ENTRY)==0x9999);interrupt_installed=0;
    stats.active=0;boot_phase=0;
    unsigned char radar_unit[0xc0]={0};*(void**)radar_unit=image+0x2c67610;*(uint64_t*)(radar_unit+8)=321;
    actor_ids[11]=321;InitializeCriticalSection((CRITICAL_SECTION*)(radar_unit+0x18));
    *(double*)(radar_unit+0xa8)=4;*(double*)(radar_unit+0xb0)=5;*(double*)(radar_unit+0xb8)=3;
    assert(gravidar_initialize_unit(12,radar_unit,7.0) && gravidar_inits==1);
    assert(*(double*)(radar_unit+0xa8)==7.0-1.0/60.0 && *(double*)(radar_unit+0xb0)==7.0-1.0/60.0 && *(double*)(radar_unit+0xb8)==7.0);
    assert(gravidar_initialize_unit(12,radar_unit,8.0) && gravidar_inits==1 && *(double*)(radar_unit+0xb8)==7.0);
    /* Bounded radar snapshots preserve the native vectors and copy identities,
     * including contacts outside our twelve-ship fixture. */
    uint64_t radar_ids[]={321,999};uintptr_t radar_vector[]={(uintptr_t)radar_ids,(uintptr_t)(radar_ids+2),(uintptr_t)(radar_ids+2)};
    gravidar_contacts radar_copy={0};assert(gravidar_read_contacts(radar_vector,&radar_copy));
    assert(radar_copy.present && radar_copy.count==2 && radar_copy.ids[0]==321 && radar_copy.ids[1]==999);
    radar_ids[1]=1234;assert(radar_copy.ids[1]==999);radar_ids[1]=999;
    /* A crowded unit is shown truncated: rows of a watched unit, not worth a stop. */
    static uint64_t crowded_ids[GRAVIDAR_CONTACT_LIMIT+5];for(unsigned i=0;i<GRAVIDAR_CONTACT_LIMIT+5;i++) crowded_ids[i]=i+1;
    uintptr_t crowded[]={(uintptr_t)crowded_ids,(uintptr_t)(crowded_ids+GRAVIDAR_CONTACT_LIMIT+5),(uintptr_t)(crowded_ids+GRAVIDAR_CONTACT_LIMIT+5)};
    gravidar_contacts_truncated=0;assert(gravidar_read_contacts(crowded,&radar_copy));
    assert(radar_copy.present && radar_copy.count==GRAVIDAR_CONTACT_LIMIT && radar_copy.ids[GRAVIDAR_CONTACT_LIMIT-1]==GRAVIDAR_CONTACT_LIMIT && gravidar_contacts_truncated==1);
    uintptr_t invalid_vector[]={radar_vector[0],radar_vector[0]+8*(GRAVIDAR_CONTACT_LIMIT+1),radar_vector[0]+8*(GRAVIDAR_CONTACT_LIMIT+1)};
    invalid_vector[1]=radar_vector[0]-8;assert(!gravidar_read_contacts(invalid_vector,&radar_copy));
    invalid_vector[1]=radar_vector[0]+1;assert(!gravidar_read_contacts(invalid_vector,&radar_copy));
    assert(gravidar_read_contacts(NULL,&radar_copy) && !radar_copy.present && !radar_copy.count);
    uintptr_t empty_vector[3]={0};assert(gravidar_read_contacts(empty_vector,&radar_copy) && radar_copy.present && !radar_copy.count);
    unsigned char radar_result[0x40]={0};memcpy(radar_result+0x10,radar_vector,24);
    memcpy(radar_unit+0x68,radar_vector,24);*(void**)(radar_unit+0x98)=radar_vector;
    *(void**)(radar_unit+0xa0)=empty_vector;
    gravidar_sample radar_observed={0};assert(gravidar_snapshot_contacts(&radar_observed,radar_unit,radar_result,0));
    assert(gravidar_snapshot_contacts(&radar_observed,radar_unit,radar_result,1));
    assert(radar_observed.contacts[0].count==2 && radar_observed.contacts[1].count==2 &&
        radar_observed.contacts[2].count==2 && radar_observed.contacts[3].count==0 &&
        radar_observed.contacts[4].count==2 && radar_observed.contacts[5].count==2 &&
        radar_observed.contacts[6].present && !radar_observed.contacts[6].count && radar_observed.contacts[7].present);
    assert(!memcmp(radar_unit+0x68,radar_vector,24) && !memcmp(radar_result+0x10,radar_vector,24) && radar_ids[1]==999);
    radar_ids[1]=0;assert(!gravidar_read_contacts(radar_vector,&radar_copy));radar_ids[1]=999;
    *(uint64_t*)(radar_unit+8)=322;assert(!gravidar_initialize_unit(12,radar_unit,7.0));
    DeleteCriticalSection((CRITICAL_SECTION*)(radar_unit+0x18));gravidar_inits=0;gravidar_initialized[11]=NULL;actor_ids[11]=0;
    gravidar_sample grs={0};grs.calls=1;gravidar_seeded=1;test_calls=0;test_rng=111;
    gravidar_execute(test_gravidar_unit_original,&grs,3,&test_rng,&out1,&test_calls,&test_rng);
    assert(test_calls==1 && test_rng==111 && grs.rng_after==out1);
    test_rng=222;gravidar_execute(test_gravidar_unit_original,&grs,3,&test_rng,&out2,&test_calls,&test_rng);
    assert(test_calls==2 && test_rng==222 && out1==out2);
    gravidar_seeded=0;gravidar_execute(test_gravidar_unit_original,&grs,3,&test_rng,&out2,&test_calls,&test_rng);
    assert(test_calls==3 && test_rng==out2);
    if(IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128)) {
        ai_original_word=1;ai_patched_word=2;*(unsigned __int128*)(image+AI_WORD)=1;
        assert(ai_swap(1,2));assert(!ai_swap(1,3));ai_installed=1;
        ai_restore();assert(ai_restored && !ai_installed && *(unsigned __int128*)(image+AI_WORD)==1);
    }
    ai_sample ais={0};ais.calls=1;ais.script_hash=123;ais.pc_before=5;
    ai_seeded=1;test_rng=111;test_calls=0;
    ai_execute(test_ai_original,&ais,2,&test_rng,&out1);
    assert(test_calls==1 && test_rng==111);
    test_rng=222;ai_execute(test_ai_original,&ais,2,&test_rng,&out2);
    assert(test_calls==2 && test_rng==222 && out1==out2);
    ai_seeded=0;ai_execute(test_ai_original,&ais,2,&test_rng,&out2);
    assert(test_calls==3 && test_rng==out2);
    agent_original_rel=123;agent_patched_rel=456;*(int32_t*)(image+AGENT_REL)=123;
    image[AGENT_REL-1]=0xe8;image[AGENT_REL+4]=0xab;
    assert(agent_swap(123,456) && !agent_swap(123,789));
    assert(image[AGENT_REL-1]==0xe8 && image[AGENT_REL+4]==0xab);
    agent_installed=1;agent_restore();assert(agent_restored && !agent_installed && *(int32_t*)(image+AGENT_REL)==123);
    agent_sample ags={0};ags.calls=1;agent_seeded=1;agent_full=1;test_rng=111;test_calls=0;
    agent_execute(test_agent_original,&ags,3,&test_rng,NULL,&out1,1.0f/60.0f,7.0,1);
    assert(test_calls==1 && test_rng==111 && test_cheap==0);
    test_rng=222;agent_execute(test_agent_original,&ags,3,&test_rng,NULL,&out2,1.0f/60.0f,7.0,1);
    assert(test_calls==2 && test_rng==222 && out1==out2);
    agent_full=agent_seeded=0;agent_execute(test_agent_original,&ags,3,&test_rng,NULL,&out2,1.0f/60.0f,7.0,1);
    assert(test_calls==3 && test_rng==out2 && test_cheap==1);
    unsigned char object[0x208]={0},ctrl[0x38]={0},path_agent[0xf18]={0};
    actor_components[0]=object;actor_ids[0]=123;*(uint64_t*)(object+8)=123;
    *(void**)(object+0x200)=ctrl;*(void**)ctrl=image+0x2c51158;*(void**)(ctrl+0x30)=path_agent+0xad0;
    assert(agent_identity(0,path_agent));
    *(uint64_t*)(object+8)=456;assert(!agent_identity(0,path_agent));
    *(uint64_t*)(object+8)=123;*(void**)(ctrl+0x30)=path_agent+0xad8;assert(!agent_identity(0,path_agent));
    *(void**)(image+SHIP_ENTRY)=image+SHIP_MOVE;
    assert(ship_swap(image+SHIP_MOVE,(void*)ship_move) && !ship_swap(image+SHIP_MOVE,NULL));
    ship_owned_slots=1;ship_installed=1;ship_restore();assert(ship_restored && !ship_installed && *(void**)(image+SHIP_ENTRY)==image+SHIP_MOVE);
    *(void**)(image+SHIP_XL_ENTRY)=object;
    assert(!ship_install_slots() && !ship_installed && !ship_owned_slots && ship_restored);
    assert(*(void**)(image+SHIP_ENTRY)==image+SHIP_MOVE && *(void**)(image+SHIP_XL_ENTRY)==object);
    *(void**)(image+SHIP_XL_ENTRY)=image+SHIP_MOVE;
    assert(ship_install_slots() && ship_owned_slots==3 && !ship_install_slots());
    *(void**)(image+SHIP_ENTRY)=object;ship_restore();
    assert(!ship_restored && ship_installed && ship_owned_slots==1 && *(void**)(image+SHIP_ENTRY)==object);
    assert(*(void**)(image+SHIP_XL_ENTRY)==image+SHIP_MOVE); /* Restore other owned slot despite first conflict. */
    *(void**)(image+SHIP_ENTRY)=(void*)ship_move;ship_restore();assert(ship_restored && !ship_installed && !ship_owned_slots);
    ship_sample ships={0};ships.calls=1;ship_seeded=1;test_rng=111;test_calls=0;
    ship_execute(test_ai_original,&ships,6,&test_rng,&out1);assert(test_calls==1 && test_rng==111);
    test_rng=222;ship_execute(test_ai_original,&ships,6,&test_rng,&out2);assert(test_calls==2 && test_rng==222 && out1==out2);
    ship_seeded=0;ship_execute(test_ai_original,&ships,6,&test_rng,&out2);assert(test_calls==3 && test_rng==out2);
    test_calls=0;
    assert(!params_once(test_params_original,0,&test_rng,&test_calls));
    assert(params_once(test_params_original,1,&test_rng,&test_calls));
    assert(params_once(test_params_original,1,&test_rng,&test_calls));
    assert(test_calls==1 && params_calls==1);
    *(void**)(image+WEAPON_ENTRY)=image+WEAPON_MOVE;
    assert(weapon_swap(0,image+WEAPON_MOVE,(void*)weapon_move) && !weapon_swap(0,image+WEAPON_MOVE,NULL));
    weapon_installed=1;weapon_restore();assert(weapon_restored && !weapon_installed && *(void**)(image+WEAPON_ENTRY)==image+WEAPON_MOVE);
    *(void**)(image+weapon_entries[1])=image+WEAPON_MOVE;
    assert(weapon_swap(1,image+WEAPON_MOVE,(void*)weapon_move));weapon_installed=2;assert(weapon_owned());
    *(void**)(image+weapon_entries[1])=NULL;assert(!weapon_owned());weapon_restore();assert(!weapon_restored && weapon_installed==2);
    *(void**)(image+weapon_entries[1])=(void*)weapon_move;weapon_restore();assert(weapon_restored && !weapon_installed);
    weapon_sample ws={0};ws.calls=1;weapon_seeded=1;test_rng=111;test_calls=0;
    weapon_context=&weapon_samples[0][0];weapon_context_actor=1;weapon_context_slot=1;
    weapon_execute(test_ai_original,&ws,6,2,&test_rng,&out1);assert(test_calls==1 && test_rng==111);
    assert(weapon_context==&weapon_samples[0][0] && weapon_context_actor==1 && weapon_context_slot==1);
    test_rng=222;weapon_execute(test_ai_original,&ws,6,2,&test_rng,&out2);assert(test_calls==2 && test_rng==222 && out1==out2);
    weapon_execute(test_ai_original,&ws,6,3,&test_rng,&out2);assert(out1!=out2 && test_rng==222);
    weapon_seeded=0;weapon_execute(test_ai_original,&ws,6,2,&test_rng,&out2);assert(test_calls==4 && test_rng==out2);
    unsigned char gun[0x80]={0};*(void**)gun=image+0x2b68278;*(uint64_t*)(gun+8)=789;*(void**)(gun+0x70)=object;
    assert(weapon_register(gun,789,1,2));assert(weapon_register(gun,789,1,2));assert(weapon_registered==1);
    assert(!weapon_register(gun,789,1,3));assert(!weapon_register(gun,789,2,2));assert(!weapon_register(gun,790,1,2));
    assert(!weapon_register(gun,789,0,2));assert(!weapon_register(gun,789,1,33));
    *(uint64_t*)(object+8)=999;assert(!weapon_register(gun,789,1,2));
    if(IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128)) {
        params_original_word=((unsigned __int128)0x4857515355415241ULL<<64)|1;
        params_patched_word=((unsigned __int128)0x4890000000000000ULL<<64)|2;
        *(unsigned __int128*)(image+PARAMS_ENTRY)=params_original_word;
        assert(params_swap(params_original_word,params_patched_word));
        assert(!params_swap(params_original_word,0));
        params_installed=1;params_restore();assert(params_restored && !params_installed);
        assert(*(unsigned __int128*)(image+PARAMS_ENTRY)==params_original_word);
        assert(image[PARAMS_ENTRY+15]==0x48);
    }
    *(void**)(image+NAV_ENTRY)=image+NAV_MOVE;
    assert(nav_swap(image+NAV_MOVE,(void*)nav_move));assert(!nav_swap(image+NAV_MOVE,NULL));
    nav_installed=1;nav_restore();assert(nav_restored && !nav_installed && *(void**)(image+NAV_ENTRY)==image+NAV_MOVE);
    nav_sample ns={0};ns.calls=1;nav_seeded=nav_initial_refresh=1;
    unsigned char nav_object[0xd8]={0};*(double*)(nav_object+0xd0)=20.0;
    test_rng=111;test_calls=0;nav_execute(test_nav_original,&ns,1,&test_rng,nav_object,7.0);out1=*(uint64_t*)nav_object;
    assert(test_calls==1 && test_rng==111 && ns.initialized==1 && nav_inits==1);
    assert(*(double*)(nav_object+0xd0)==7.0-1.0/60.0);
    *(double*)(nav_object+0xd0)=21.0;test_rng=222;
    nav_execute(test_nav_original,&ns,1,&test_rng,nav_object,7.0);assert(out1==*(uint64_t*)nav_object && test_rng==222);
    assert(ns.initialized==0 && nav_inits==1 && *(double*)(nav_object+0xd0)==21.0);
    nav_seeded=0;nav_execute(test_nav_original,&ns,1,&test_rng,nav_object,7.0);assert(test_rng==*(uint64_t*)nav_object);
    unsigned char health_object[0x298]={0},group[0xa8]={0},group_copy[0xa8];char shield[192];
    assert(shield_state(health_object,shield) && !strcmp(shield,"none"));
    *(void**)(health_object+0x290)=group;assert(!shield_state(health_object,shield));
    *(void**)(group+0x90)=image+0x2b19700;*(void**)(group+0x98)=group;
    *(uintptr_t*)(group+0x10)=8;*(uintptr_t*)(group+0x18)=24;*(double*)(group+0x68)=2.5;
    memcpy(group_copy,group,sizeof(group));assert(shield_state(health_object,shield));
    assert(!strncmp(shield,"4004000000000000",16) && !memcmp(group_copy,group,sizeof(group)));
    *(uintptr_t*)(group+0x18)=7;assert(!shield_state(health_object,shield));
    /* world hash shields: scene2's 6f27e (value 1156.6 at 3.08, max 1196, delay 15, rate 1 x 50, one generator) is full
     * from about 18.866; client 1's UI had left it at value=max, time=0. Both hash alike once full, and only then. */
    {
        *(uintptr_t*)(group+0x18)=16;uint64_t v=0x40921278d4fdf3b6ULL,c=0x4008aaa8eb463498ULL;double max=1196.0,delay=15.0;float r=1.0f,f=50.0f;
        memcpy(group+0x68,&v,8);memcpy(group+0x70,&c,8);memcpy(group+0x40,&max,8);memcpy(group+0x48,&delay,8);memcpy(group+0x28,&r,4);memcpy(group+0x50,&f,4);
        *(void**)(group+0x78)=group+0x90;char raw[192],canon[192],ui[192];
        double before=18.87,after=18.88;
        assert(shield_state(health_object,raw) && shield_state_at(health_object,canon,&before) && !strcmp(raw,canon) && !shield_canonical_count);
        assert(shield_state_at(health_object,canon,&after) && strcmp(raw,canon) && shield_canonical_count==1);
        assert(!strncmp(canon,"4092b000000000000000000000000000",32) && !strcmp(canon+32,raw+32));
        uint64_t v2;memcpy(&v2,group+0x68,8);assert(v2==v); /* nothing written */
        uint64_t full=0x4092b00000000000ULL,zero=0;memcpy(group+0x68,&full,8);memcpy(group+0x70,&zero,8);
        assert(shield_state(health_object,ui) && !strcmp(ui,canon) && shield_state_at(health_object,ui,&after) && !strcmp(ui,canon) && shield_canonical_count==1);
        memcpy(group+0x68,&v,8);memcpy(group+0x70,&c,8);
        /* The getters: modifiers scale max (+0x18), delay (+0x1c) and rate (+0x20); generators divide delay and rate. */
        double m=0;
        const float none[3]={1,1,1},slow[3]={1.0f,2.0f,0.5f};
        assert(!shield_full_at(1156.6,3.08,1196,15,1,50,1,none,18.80,&m) && shield_full_at(1156.6,3.08,1196,15,1,50,1,none,18.87,&m) && m==1196.0);
        assert(!shield_full_at(1156.6,3.08,1196,15,1,50,1,slow,34.0,&m) && shield_full_at(1156.6,3.08,1196,15,1,50,1,slow,34.7,&m));
        assert(!shield_full_at(1156.6,3.08,1196,15,1,50,2,none,10.0,&m) && !shield_full_at(1156.6,3.08,1196,15,1,50,2,none,12.0,&m) && shield_full_at(1156.6,3.08,1196,15,1,50,2,none,12.2,&m));
        assert(shield_full_at(1156.6,3.08,1196,15,1,50,0,none,4.0,&m)); /* no generator: no delay, the rate undivided */
        assert(!shield_full_at(1196.0,3.08,1196,15,1,50,1,none,99.0,&m) && !shield_full_at(0,0,0,15,1,50,1,none,99.0,&m)); /* already full, no maximum */
        assert(!shield_full_at(1156.6,3.08,1196,15,0.00005f,1,1,none,1e9,&m)); /* no recharge */
        assert(!shield_full_at(NAN,3.08,1196,15,1,50,1,none,99.0,&m) && !shield_full_at(1156.6,3.08,1196,15,1,50,1,none,NAN,&m));
        /* The source must be the group's ShieldRechargeSource. */
        *(void**)(group+0x78)=group;assert(!shield_state_at(health_object,canon,&after));
        *(void**)(health_object+0x290)=NULL;assert(shield_state_at(health_object,canon,&after) && !strcmp(canon,"none"));
        *(void**)(health_object+0x290)=group;shield_canonical=0;shield_canonical_count=0;
    }
    unsigned char dock_controller[0x130]={0},dock_copy[0x130];char dock_text[512],dock_before[512];
    *(void**)dock_controller=image+DOCK_CONTROLLER_VTABLE;*(void**)(dock_controller+0x70)=health_object;
    *(double*)(dock_controller+0x60)=7.0;*(unsigned*)(dock_controller+0x120)=1;dock_controller[0x124]=1;
    memcpy(dock_copy,dock_controller,sizeof(dock_copy));
    assert(dock_controller_state(dock_controller,health_object,dock_text) && strlen(dock_text)==377);
    assert(!memcmp(dock_copy,dock_controller,sizeof(dock_copy)));strcpy(dock_before,dock_text);
    *(unsigned*)(dock_controller+0x2c)=0x7fc00000; /* vector padding is not a coordinate */
    assert(dock_controller_state(dock_controller,health_object,dock_text) && !strcmp(dock_before,dock_text));
    *(double*)(dock_controller+0x60)=8.0;
    assert(dock_controller_state(dock_controller,health_object,dock_text) && strcmp(dock_before,dock_text));
    assert(!dock_controller_state(dock_controller,NULL,dock_text));
    *(unsigned*)(dock_controller+0x24)=0x7fc00000;
    assert(!dock_controller_state(dock_controller,health_object,dock_text));
    *(unsigned*)(dock_controller+0x24)=0;dock_controller[0x124]=2;
    assert(!dock_controller_state(dock_controller,health_object,dock_text));
    dock_controller[0x124]=1;*(void**)dock_controller=NULL;
    assert(!dock_controller_state(dock_controller,health_object,dock_text));
    *(void**)dock_controller=image+UNDOCK_CONTROLLER_VTABLE;
    assert(dock_controller_state(dock_controller,health_object,dock_text) && strlen(dock_text)==233 && !strncmp(dock_text,"UNDOCKCTL_1:",11));
    strcpy(dock_before,dock_text);*(float*)(dock_controller+0x84)=2.0f;
    assert(dock_controller_state(dock_controller,health_object,dock_text) && strcmp(dock_before,dock_text));
    dock_controller[0xc4]=2;assert(!dock_controller_state(dock_controller,health_object,dock_text));
    *(void**)(image+VELOCITY_ENTRY)=image+VELOCITY_MOVE;
    assert(velocity_swap(image+VELOCITY_MOVE,(void*)velocity_move));assert(!velocity_swap(image+VELOCITY_MOVE,NULL));
    velocity_installed=1;velocity_restore();assert(velocity_restored && !velocity_installed);
    unsigned char velocity_slots[0x340]={0};uint32_t velocities[8];
    *(void**)(ctrl+0x30)=velocity_slots;*(unsigned*)(image+0x38f4ee4)=1;
    *(unsigned*)(velocity_slots+0x1a0+0x40)=0x3f800000;
    assert(velocity_state(ctrl,velocities) && velocities[0]==0x3f800000);
    *(unsigned*)(image+0x38f4ee4)=2;assert(!velocity_state(ctrl,velocities));
    char body_text[1024];assert(body_state(health_object,0,body_text) && !strcmp(body_text,"none"));
    assert(!body_state(health_object,2,body_text));
    unsigned char record[0x80]={0},xb[0x10]={0},scene[0x28]={0},system[0x1c0]={0},iface[0x10]={0};
    unsigned char manager[0x18]={0},body[0x80]={0},motion[0x80]={0};void *body_array[1]={body};
    *(void**)(health_object+0x260)=record;*(void**)record=health_object;*(void**)(record+0x10)=xb;
    *(void**)(record+0x18)=scene;*(void**)(scene+0x20)=system;*(void**)(system+0x1b8)=iface;
    *(void**)(iface+8)=manager;*(uint64_t*)manager=1;*(void**)(manager+0x10)=body_array;
    *(void**)(body+0x48)=motion;*(void**)(body+0x50)=record;
    assert(body_state(health_object,0,body_text));
    { /* The text is the per-word %08x layout (hex_words keeps it byte for byte). */
        for(unsigned i=0;i<8;i++) {((uint32_t*)body)[i]=0x01234567u*(i+1);((uint32_t*)(record+0x40))[i]=0xfedcba98u-i;((uint32_t*)(record+0x60))[i]=i*0x11111111u;}
        for(unsigned i=0;i<32;i++) ((uint32_t*)motion)[i]=0x9e3779b9u*(i+3);
        for(unsigned i=0;i<8;i++) if(i!=4) ((uint32_t*)(body+0x60))[i]=0xa5a5a5a5u^i;
        *(uint32_t*)(record+0x38)=0x2a;
        char expected[1024];int n=snprintf(expected,sizeof(expected),"flags=%08x shape_due=%016llx body_id=%08x pose=",0x2au,0ULL,0u);
        for(unsigned i=0;i<8;i++) n+=snprintf(expected+n,sizeof(expected)-(size_t)n,"%08x",((uint32_t*)body)[i]);
        n+=snprintf(expected+n,sizeof(expected)-(size_t)n," motion=");
        for(unsigned i=0;i<32;i++) n+=snprintf(expected+n,sizeof(expected)-(size_t)n,"%08x",((uint32_t*)motion)[i]);
        n+=snprintf(expected+n,sizeof(expected)-(size_t)n," pre=");
        for(unsigned i=0;i<8;i++) n+=snprintf(expected+n,sizeof(expected)-(size_t)n,"%08x",((uint32_t*)(record+0x40))[i]);
        n+=snprintf(expected+n,sizeof(expected)-(size_t)n," delta=");
        for(unsigned i=0;i<8;i++) n+=snprintf(expected+n,sizeof(expected)-(size_t)n,"%08x",((uint32_t*)(record+0x60))[i]);
        n+=snprintf(expected+n,sizeof(expected)-(size_t)n," meta=");
        for(unsigned i=0;i<8;i++) n+=snprintf(expected+n,sizeof(expected)-(size_t)n,"%08x",i==4?0u:((uint32_t*)(body+0x60))[i]);
        assert(body_state(health_object,0,body_text) && !strcmp(body_text,expected));
        memset(body,0,0x48);memset(motion,0,sizeof(motion));memset(record+0x38,0,0x48);memset(body+0x60,0,0x20);
    }
    *(unsigned*)(body+0x70)=1;assert(!body_state(health_object,0,body_text));
    *(unsigned*)(body+0x70)=0;*(uint64_t*)manager=0;assert(!body_state(health_object,0,body_text));
    shape_original_word=0x89485708245c8948ULL;shape_patched_word=0x89485700000000e9ULL;
    memcpy(image+SHAPE_ENTRY,&shape_original_word,8);assert(shape_swap(shape_original_word,shape_patched_word));
    assert(!shape_swap(shape_original_word,0));shape_installed=1;shape_restore();assert(shape_restored && !shape_installed);
    assert(*(uint64_t*)(image+SHAPE_ENTRY)==shape_original_word);
    unsigned char holder[0x30]={0};*(double*)(holder+0x28)=99.0;*(uint64_t*)(holder+8)=100;shape_sample ss={0};ss.calls=1;
    /* A body generation transition at the same holder address must be visible
     * even when the existing initializer considers that address already seen. */
    shape_lifetime *life=shape_lifetime_observe(1,0,record,holder);
    assert(life && !life->before.present && life->after.body_id==0 && !life->seen && life->due_before!=0);
    assert(!shape_lifetime_observe(1,0,record,holder));
    shape_initialized[0][0]=(shape_holder_key){holder,100};*(uint32_t*)(xb+8)=0x800000;
    life=shape_lifetime_observe(1,0,record,holder);
    assert(life && life->before.holder==life->after.holder && life->before.body_id==0 && life->after.body_id==0x800000 && life->seen);
    assert(*(double*)(holder+0x28)==99.0);shape_initialized[0][0]=(shape_holder_key){0};
    unsigned lifetime_count=shape_lifetime_count;shape_lifetime_count=SHAPE_LIFETIMES;uint64_t previous_shape_failures=shape_failures;
    *(uint32_t*)(xb+8)=0x1000000;assert(!shape_lifetime_observe(1,0,record,holder));
    /* Full: a row lost, the identity still tracked, no stop; the same identity again is not counted twice. */
    assert(shape_failures==previous_shape_failures && shape_lifetime_dropped==1 && shape_identities[0][0].body_id==0x1000000);
    assert(!shape_lifetime_observe(1,0,record,holder) && shape_lifetime_dropped==1);shape_lifetime_dropped=0;shape_lifetime_count=lifetime_count;
    gate_play_mode=1;*(uint32_t*)(xb+8)=0x2000000;assert(!shape_lifetime_observe(1,0,record,holder) && shape_lifetime_count==lifetime_count);gate_play_mode=0;
    shape_seeded=shape_initial=1;test_rng=111;test_calls=0;
    shape_execute(test_shape_original,&ss,1,0,&test_rng,&out1,holder,7.0,1);
    assert(test_calls==1 && test_cheap==1 && test_rng==111 && shape_inits==1 && ss.initialized==1);
    assert(*(double*)(holder+0x28)==7.0-1.0/60.0);
    *(double*)(holder+0x28)=21.0;test_rng=222;
    shape_execute(test_shape_original,&ss,1,0,&test_rng,&out2,holder,7.0,0);
    assert(test_calls==2 && test_cheap==0 && test_rng==222 && out1==out2 && shape_inits==1 && ss.initialized==0);
    shape_execute(test_shape_original,&ss,1,1,&test_rng,&out2,holder,7.0,0);
    assert(shape_inits==1 && *(double*)(holder+0x28)==21.0); /* Shared main/second holder initializes once. */
    unsigned char replacement[0x30]={0};*(uint64_t*)(replacement+8)=200;
    shape_execute(test_shape_original,&ss,1,0,&test_rng,&out2,replacement,7.0,0);
    assert(shape_inits==2 && ss.init_total==2 && ss.last_init==stats.issued);
    shape_seeded=0;shape_execute(test_shape_original,&ss,1,0,&test_rng,&out2,NULL,7.0,0);
    assert(test_rng==out2);
    shape_seeded=1;shape_sample extra_sample={0};extra_sample.calls=1;unsigned previous_inits=shape_inits;
    shape_execute(test_shape_original,&extra_sample,13,3,&test_rng,&out1,holder,7.0,1);
    assert(shape_inits==previous_inits+1 && extra_sample.initialized==1);
    *(double*)(holder+0x28)=21.0;test_rng=333;
    shape_execute(test_shape_original,&extra_sample,13,3,&test_rng,&out2,holder,7.0,0);
    assert(test_rng==333 && out1==out2 && extra_sample.initialized==0);
    shape_execute(test_shape_original,&extra_sample,13,0,&test_rng,&out2,holder,7.0,0);
    assert(shape_inits==previous_inits+1 && *(double*)(holder+0x28)==21.0);
    /* Same address, fresh native ID: refresh once, then share the refreshed
     * holder across roles without seeding from its process-local identity. */
    *(uint64_t*)(holder+8)=101;
    shape_execute(test_shape_original,&extra_sample,13,3,&test_rng,&out2,holder,7.0,0);
    assert(extra_sample.initialized && shape_inits==previous_inits+2 && out1==out2 && test_rng==333);
    *(double*)(holder+0x28)=21.0;
    shape_execute(test_shape_original,&extra_sample,13,0,&test_rng,&out2,holder,7.0,0);
    assert(!extra_sample.initialized && shape_inits==previous_inits+2 && *(double*)(holder+0x28)==21.0);
    shape_execute(test_shape_original,&extra_sample,13,3,&test_rng,&out2,replacement,7.0,0);
    assert(extra_sample.initialized && shape_inits==previous_inits+3);
    shape_execute(test_shape_original,&extra_sample,13,0,&test_rng,&out2,holder,7.0,0);
    assert(!extra_sample.initialized && shape_inits==previous_inits+3 && *(double*)(holder+0x28)==21.0);
    life=shape_lifetime_observe(1,0,record,holder);
    assert(life && life->before.holder_id==100 && life->after.holder_id==101 && !life->seen);
    previous_shape_failures=shape_failures;unsigned previous_calls=test_calls;
    *(uint64_t*)(holder+8)=0;
    shape_execute(test_shape_original,&extra_sample,13,0,NULL,&out2,holder,7.0,0);
    assert(test_calls==previous_calls+1 && shape_failures==previous_shape_failures+1 && *(double*)(holder+0x28)==21.0);
    *(uint64_t*)(holder+8)=101;shape_failures=previous_shape_failures;
    shape_dock_full=1;shape_sample cadence={0};cadence.calls=1;test_calls=0;test_rng=321;
    shape_execute(test_shape_deadline_original,&cadence,13,2,&test_rng,holder,holder,7.0,1);
    double native_due,applied_due;memcpy(&native_due,&cadence.native_due_after,8);memcpy(&applied_due,&cadence.due_after,8);
    assert(test_calls==1 && test_cheap==1 && test_rng==321 && test_shape_due_seen==7.0-1.0/60.0);
    assert(native_due==1000.0 && applied_due==7.0 && cadence.forced && shape_forced_refreshes==1);
    shape_execute(test_shape_deadline_original,&cadence,1,0,&test_rng,holder,holder,7.0,0);
    assert(test_calls==2 && !cadence.forced && shape_forced_refreshes==1 && *(double*)(holder+0x28)==1000.0);
    shape_dock_full=0;shape_execute(test_shape_deadline_original,&cadence,13,2,&test_rng,holder,holder,7.0,0);
    assert(test_calls==3 && test_rng==321 && !cadence.forced && shape_forced_refreshes==1 && *(double*)(holder+0x28)==1000.0);
    for(unsigned i=0;i<2;i++) {
        *(void**)(image+bullet_entries[i])=bullet_original(i);
        assert(bullet_swap(i,bullet_original(i),bullet_wrapper(i)));
        assert(!bullet_swap(i,bullet_original(i),NULL));
    }
    bullet_installed=3;assert(bullet_owned());
    *(void**)(image+bullet_entries[1])=NULL;assert(!bullet_owned());
    bullet_restore();assert(bullet_installed==2 && !bullet_restored);
    *(void**)(image+bullet_entries[1])=bullet_wrapper(1);bullet_restore();
    assert(bullet_restored && !bullet_installed);
    unsigned char projectile[16]={0};*(void**)projectile=image+0x2c42ac8;*(uint64_t*)(projectile+8)=123;
    assert(!bullet_register(projectile,&ws,0,1));
    assert(bullet_register(projectile,&ws,6,2));
    bullet_identity *bs=bullet_find(projectile,0);assert(bs && bs->actor==6 && bs->slot==2 && bs->spawn==1);
    assert(!bullet_register(projectile,&ws,6,2)); /* Duplicate birth must not get a new identity. */
    *(uint64_t*)(projectile+8)=124;assert(bullet_register(projectile,&ws,6,2)); /* Address reuse. */
    assert(bs->id==124 && bs->spawn==2 && bullet_created==2);
    bullet_event be={0};bs->calls=1;bullet_seeded=1;test_calls=0;test_rng=111;
    bullet_execute(test_ai_original,bs,&be,&test_rng,&out1);assert(test_calls==1 && test_rng==111);
    test_rng=222;bullet_execute(test_ai_original,bs,&be,&test_rng,&out2);
    assert(test_calls==2 && test_rng==222 && out1==out2 && be.after==out2);
    ++bs->spawn;bullet_execute(test_ai_original,bs,&be,&test_rng,&out2);assert(out1!=out2 && test_rng==222);
    bullet_seeded=0;bullet_execute(test_ai_original,bs,&be,&test_rng,&out2);assert(test_rng==out2);
    assert(bullet_seed(7,2,1,1,1)!=bullet_seed(7,3,0,1,1));
    bullet_event_count=BULLET_EVENTS;assert(!bullet_record(bs,0) && !bullet_failures && bullet_dropped==1);
    /* Play mode: no row, no count, no stop; the key is bullet_execute's and does not need one. */
    bullet_event_count=0;gate_play_mode=1;bullet_dropped=0;assert(!bullet_record(bs,0) && !bullet_event_count && !bullet_dropped);gate_play_mode=0;
    bullet_event_count=0;
    *(void**)(image+DEATH_ENTRY)=image+DEATH_LISTENER;
    assert(death_swap(image+DEATH_LISTENER,(void*)death_listener));
    assert(!death_swap(image+DEATH_LISTENER,NULL));death_owned_slots=1;death_installed=1;death_restore();
    assert(death_restored && !death_installed);
    *(void**)(image+DEATH_XL_ENTRY)=object;
    assert(!death_install_slots() && !death_installed && !death_owned_slots && death_restored);
    assert(*(void**)(image+DEATH_ENTRY)==image+DEATH_LISTENER && *(void**)(image+DEATH_XL_ENTRY)==object);
    *(void**)(image+DEATH_XL_ENTRY)=image+DEATH_LISTENER;
    assert(death_install_slots() && death_owned_slots==3 && !death_install_slots());
    *(void**)(image+DEATH_ENTRY)=object;death_restore();
    assert(!death_restored && death_installed && death_owned_slots==1 && *(void**)(image+DEATH_ENTRY)==object);
    assert(*(void**)(image+DEATH_XL_ENTRY)==image+DEATH_LISTENER);
    *(void**)(image+DEATH_ENTRY)=(void*)death_listener;death_restore();assert(death_restored && !death_installed && !death_owned_slots);
    death_event de={.tick=stats.issued,.call=1,.actor=8};death_seeded=1;test_rng=111;test_calls=0;
    death_execute(test_death_original,&de,&test_rng,&out1,&out1,7.0,6,&test_calls);
    assert(test_calls==1 && test_rng==111 && de.after==out1);
    test_rng=222;death_execute(test_death_original,&de,&test_rng,&out2,&out2,7.0,6,&test_calls);
    assert(test_calls==2 && test_rng==222 && out1==out2);
    ++de.actor;death_execute(test_death_original,&de,&test_rng,&out2,&out2,7.0,6,&test_calls);
    assert(out1!=out2 && test_rng==222);
    death_seeded=0;death_execute(test_death_original,&de,&test_rng,&out2,&out2,7.0,6,&test_calls);
    assert(test_rng==out2);
    mass_original_word=0x485810588948c48bULL;mass_patched_word=0x48909000000000e9ULL;
    memcpy(image+MASS_ENTRY,&mass_original_word,8);assert(mass_swap(mass_original_word,mass_patched_word));
    assert(!mass_swap(mass_original_word,0));mass_installed=1;mass_restore();
    assert(mass_restored && !mass_installed && *(uint64_t*)(image+MASS_ENTRY)==mass_original_word);
    void *body_interface[1]={iface};uint32_t local_body_id=0,found_id;
    *(uint64_t*)manager=1;
    assert(mass_body(body_interface,&local_body_id,&found_id)==body && found_id==0);
    local_body_id=0x1000000;assert(!mass_body(body_interface,&local_body_id,&found_id));
    local_body_id=1;assert(!mass_body(body_interface,&local_body_id,&found_id));
    local_body_id=0;body_array[0]=(void*)((uintptr_t)body|1);assert(!mass_body(body_interface,&local_body_id,&found_id));
    attention_original_word=0x8b4820ec83485740ULL;attention_patched_word=0x8b489000000000e9ULL;
    memcpy(image+ATTENTION_ENTRY,&attention_original_word,8);assert(attention_swap(attention_original_word,attention_patched_word));
    assert(!attention_swap(attention_original_word,0));attention_installed=1;attention_restore();
    assert(attention_restored && !attention_installed && *(uint64_t*)(image+ATTENTION_ENTRY)==attention_original_word);
    unsigned char attention_object[0x2e8]={0};attention_sample ats={0};ats.calls=1;
    *(double*)(attention_object+0x2e0)=99.0;*(unsigned*)(attention_object+0x1ec)=9;
    attention_seeded=attention_initial=1;test_calls=0;test_rng=111;
    attention_execute(test_ai_original,&ats,1,&test_rng,attention_object,7.0);out1=*(uint64_t*)attention_object;
    assert(test_calls==1 && test_rng==111 && ats.initialized==1 && attention_inits==1 && ats.level_after==9);
    assert(*(double*)(attention_object+0x2e0)==7.0-1.0/60.0);
    *(double*)(attention_object+0x2e0)=22.0;test_rng=222;
    attention_execute(test_ai_original,&ats,1,&test_rng,attention_object,7.0);
    assert(out1==*(uint64_t*)attention_object && test_rng==222 && !ats.initialized && attention_inits==1);
    assert(*(double*)(attention_object+0x2e0)==22.0);
    attention_seeded=0;attention_execute(test_ai_original,&ats,1,&test_rng,attention_object,7.0);
    assert(test_rng==*(uint64_t*)attention_object);
    unsigned char obstacle_model[0xa8]={0};uint32_t obstacle[0x80/4]={123,456};
    flight_avoidance_refresh=1;flight_full=1;test_flight_self=obstacle_model;
    *(double*)(obstacle_model+0x88)=22.0;
    flight_execute(test_flight_original,&sample,1,&test_rng,obstacle_model,&out1,1.0f/60.0f,7.0,1,0);
    assert(*(double*)(obstacle_model+0x88)==-1.0 && flight_refreshes==1);
    flight_avoidance_refresh=0;flight_full=0;test_flight_self=NULL;
    unsigned char spline_model[0x4b0]={0},spline_agent[0x4e0]={0},spline_slot[0x1a0]={0};
    spline_sample spline_test={0};test_flight_self=spline_model;test_calls=0;stats.issued=77;
    spline_model[0x412]=42;spline_agent[0x340]=21;
    spline_observe(test_flight_original,&spline_test,spline_model,spline_agent,spline_slot,1.0f/60.0f,7.0,1,0);
    assert(test_calls==1 && test_cheap==1 && spline_calls==1 && spline_failures==0);
    assert(spline_test.tick==77 && spline_test.calls==1 && spline_test.cheap==1);
    assert(((unsigned char*)spline_test.model[0])[0x412]==42 && ((unsigned char*)spline_test.params)[0]==21);
    assert(!memcmp(spline_test.model[0],spline_test.model[1],sizeof(spline_model)));
    assert(*(uint64_t*)spline_test.slot[1]==test_rng);
    spline_refresh=1;*(double*)(spline_model+0x108)=42.0;
    spline_observe(test_flight_original,&spline_test,spline_model,spline_agent,spline_slot,1.0f/60.0f,7.0,1,0);
    assert(test_calls==2 && test_cheap==0 && spline_test.cheap==1 && spline_test.applied_cheap==0);
    assert(*(double*)(spline_model+0x108)==-1.0 && spline_refreshes==1 && spline_failures==0);
    void *spline_vtable=image+SPLINE_ENTRY-0x70;void *spline_owner=spline_agent;uint32_t spline_values[4];
    memcpy(spline_model,&spline_vtable,8);memcpy(spline_model+0x10,&spline_owner,8);
    *(uint32_t*)(spline_model+0xdc)=0x3f800000;
    assert(spline_state(spline_model,spline_agent,spline_values) && spline_values[3]==0x3f800000);
    assert(!spline_state(spline_model,NULL,spline_values));
    *(uint32_t*)(spline_model+0xdc)=0x7fc00000;
    assert(!spline_state(spline_model,spline_agent,spline_values));
    unsigned char path_request[0xb0]={0};void *path_block[2]={NULL,path_request};void *path_map[2]={NULL,path_block};
    uint64_t pending_queue[5]={0,(uintptr_t)path_map,2,3,1};
    memcpy(spline_model+0x428,pending_queue,sizeof(pending_queue));
    *(uint32_t*)(path_request+0xa8)=1;*(uint32_t*)(path_request+0xac)=0x01000100;
    InitializeCriticalSection((CRITICAL_SECTION*)(image+0x3d74938));
    assert(spline_pending(&spline_test,spline_model,0));
    assert(spline_test.pending_count[0]==1 && spline_test.pending_status[0]==1 && spline_test.pending_flags[0]==0x01000100);
    assert(spline_join_pending(&spline_test,spline_model,1000) && !spline_test.waited && spline_joins==1);
    unsigned char fake_calculator[0x60]={0};*(void**)fake_calculator=image+0x2c68f98;fake_calculator[0x58]=1;
    *(void**)(image+0x6cf29c0)=fake_calculator;
    *(uint32_t*)(path_request+0xa8)=0;
    HANDLE publisher=CreateThread(NULL,0,test_spline_publish,path_request,0,NULL);assert(publisher);
    assert(spline_join_pending(&spline_test,spline_model,1000));
    assert(WaitForSingleObject(publisher,1000)==WAIT_OBJECT_0);CloseHandle(publisher);
    assert(spline_test.waited && spline_test.pending_status[0]==1 && spline_test.pending_flags[0]==0x01000001);
    assert(spline_joins==2 && spline_waits==1 && spline_timeouts==0);
    *(uint32_t*)(path_request+0xa8)=0;
    assert(!spline_join_pending(&spline_test,spline_model,0) && spline_timeouts==1);
    fake_calculator[0x58]=0;
    assert(!spline_join_pending(&spline_test,spline_model,1000));
    pending_queue[2]=3;memcpy(spline_model+0x428,pending_queue,sizeof(pending_queue));
    assert(!spline_pending(&spline_test,spline_model,0));
    pending_queue[4]=0;memcpy(spline_model+0x428,pending_queue,sizeof(pending_queue));
    assert(spline_pending(&spline_test,spline_model,0) && !spline_test.pending_count[0] && !spline_test.pending_status[0]);
    DeleteCriticalSection((CRITICAL_SECTION*)(image+0x3d74938));
    spline_refresh=0;test_flight_self=NULL;
    unsigned char init_model[0x3a0]={0},other_model[0x3a0]={0};flight_sample init_sample={0};
    test_rng=12345;init_model[0x338]=0x5a;init_model[0x344]=0xa5;
    flight_init_model(test_flight_random,&init_sample,1,init_model);
    assert(test_random_calls==2 && flight_inits==1 && init_sample.init_generation==1 && test_rng==12345);
    assert(init_model[0x338]==0x5a && init_model[0x344]==0xa5);
    uint64_t init_angles;memcpy(&init_angles,init_model+0x33c,8);
    flight_init_model(test_flight_random,&init_sample,1,init_model);
    assert(test_random_calls==2 && flight_inits==1);
    flight_sample same_identity={0};flight_init_model(test_flight_random,&same_identity,1,other_model);
    assert(!memcmp(init_model+0x33c,other_model+0x33c,8));
    flight_init_model(test_flight_random,&init_sample,1,other_model);
    assert(test_random_calls==6 && init_sample.init_generation==2);
    assert(memcmp(&init_angles,other_model+0x33c,8));
    uint64_t second_angles;memcpy(&second_angles,other_model+0x33c,8);
    flight_init_generation(test_flight_random,&same_identity,1,other_model);
    assert(same_identity.init_generation==2 && !memcmp(&second_angles,other_model+0x33c,8));
    flight_init_generation(test_flight_random,&init_sample,1,other_model);
    {
        /* reg-big2/3, reason 55 on client 2 only: a new ship's Agent at the address of a dead fixture's. */
        static unsigned char reused_agent[0x1000],old_component[0x20],new_component[0x20],new_model[0x3a0],old_model[0x3a0];
        int kept_phase=boot_phase;uint32_t kept_active=stats.active;uint64_t failures_before=flight_failures,calls_before=flight_ctor_calls;
        void *kept_component=actor_components[6];uint64_t kept_id=actor_ids[6];flight_sample kept_sample=flight_samples[6];
        stats.active=1;boot_phase=3;flight_fault_reset();
        actor_components[6]=old_component;actor_ids[6]=700;*(uint64_t*)(old_component+8)=700;*(uint64_t*)(new_component+8)=900;
        memset(&flight_samples[6],0,sizeof(flight_samples[6]));flight_samples[6].agent=reused_agent;flight_samples[6].model=old_model;
        *(void**)(reused_agent+0xef0)=new_component;*(void**)(new_model+0x10)=reused_agent;*(void**)new_model=image+FLIGHT_ENTRY-0x70;
        flight_construct_observe(test_flight_random,new_model,new_model,reused_agent);
        assert(flight_failures==failures_before && !flight_samples[6].agent && !flight_samples[6].model && flight_fault_counts[FLIGHT_FAULT_STALE_AGENT]==1);
        assert(!*(uint32_t*)(new_model+0x33c) && !*(uint32_t*)(new_model+0x340) && flight_ctor_calls==calls_before); /* no keyed values: it is no fixture */
        char flight_row[256];assert(flight_fault_row(0,flight_row,sizeof(flight_row))>0 && strstr(flight_row,"site=stale_agent actor=7 other=0000000000000384 fatal=0"));
        assert(!flight_fault_row(1,flight_row,sizeof(flight_row)));
        /* The actor's own Agent, its own component: the keyed values, as always. */
        flight_samples[6].agent=reused_agent;*(void**)(reused_agent+0xef0)=old_component;
        flight_construct_observe(test_flight_random,new_model,new_model,reused_agent);
        assert(flight_failures==failures_before && flight_samples[6].model==new_model && flight_ctor_calls==calls_before+1 && *(uint32_t*)(new_model+0x33c));
        /* A live fixture the constructor left wrong: its keyed values are missing, so this one stops, with a row. */
        *(void**)new_model=image+1;flight_construct_observe(test_flight_random,new_model,new_model,reused_agent);
        assert(flight_failures==failures_before+1 && flight_fault_counts[FLIGHT_FAULT_CONSTRUCT]==1 && flight_samples[6].agent==reused_agent);
        assert(flight_fault_row(1,flight_row,sizeof(flight_row))>0 && strstr(flight_row,"site=construct actor=7 other=00000000000002bc fatal=1"));
        flight_failures=failures_before;*(void**)new_model=image+FLIGHT_ENTRY-0x70;
        /* The update's match: true while it is the actor's, retired when the address went to another ship. */
        unsigned matched=0;assert(flight_sample_for(new_model,&matched)==&flight_samples[6] && matched==7);
        *(void**)(reused_agent+0xef0)=new_component;matched=0;
        assert(!flight_sample_for(new_model,&matched) && !matched && !flight_samples[6].model && !flight_samples[6].agent);
        assert(flight_fault_counts[FLIGHT_FAULT_STALE_MODEL]==1 && flight_failures==failures_before);
        /* An Agent whose component is not readable is not this actor's either. */
        flight_samples[6].agent=reused_agent;flight_samples[6].model=new_model;*(void**)(reused_agent+0xef0)=NULL;
        assert(!flight_sample_for(new_model,&matched) && flight_fault_counts[FLIGHT_FAULT_STALE_MODEL]==2 && flight_failures==failures_before);
        /* The rows of one site are bounded, its count is not; a diagnostic read never stops. */
        for(unsigned i=0;i<20;i++) flight_fault(FLIGHT_FAULT_AVOIDANCE_AFTER,1,0,0);
        assert(flight_fault_counts[FLIGHT_FAULT_AVOIDANCE_AFTER]==20 && flight_fault_rows==4+FLIGHT_FAULT_ROWS_PER_SITE && flight_failures==failures_before);
        char flight_status[512];assert(flight_fault_status(flight_status,sizeof(flight_status))>0 && strstr(flight_status,"sites=stale_agent:1,stale_model:2,avoidance_after:20,construct:1"));
        flight_fault_reset();flight_samples[6]=kept_sample;actor_components[6]=kept_component;actor_ids[6]=kept_id;boot_phase=kept_phase;stats.active=kept_active;
    }
    assert(init_sample.init_generation==3 && memcmp(&second_angles,other_model+0x33c,8));
    assert(test_rng==12345); /* Reused-address initialization still preserves TLS RNG. */
    assert(flight_read_avoidance(&sample,obstacle_model,0) && sample.avoidance_count[0]==0);
    *(void**)(obstacle_model+0x90)=obstacle;*(void**)(obstacle_model+0x98)=obstacle+0x80/4;
    *(void**)(obstacle_model+0xa0)=obstacle+0x80/4;
    assert(flight_read_avoidance(&sample,obstacle_model,1) && sample.avoidance_count[1]==1);
    assert(sample.avoidance[1][0][0]==123 && sample.avoidance[1][0][1]==456);
    *(uintptr_t*)(obstacle_model+0x98)+=1;assert(!flight_read_avoidance(&sample,obstacle_model,1));
    /* Crowded lists are truncated to FLIGHT_OBSTACLES and counted, not refused. */
    static uint32_t crowd[FLIGHT_OBSTACLES+1][0x80/4];crowd[0][0]=7;crowd[FLIGHT_OBSTACLES-1][0]=9;crowd[FLIGHT_OBSTACLES][0]=11;
    uint64_t truncated=flight_avoidance_truncated;
    *(uintptr_t*)(obstacle_model+0x90)=(uintptr_t)crowd;
    *(uintptr_t*)(obstacle_model+0x98)=(uintptr_t)crowd+0x80*(FLIGHT_OBSTACLES+1);
    *(uintptr_t*)(obstacle_model+0xa0)=*(uintptr_t*)(obstacle_model+0x98);
    assert(flight_read_avoidance(&sample,obstacle_model,1) && sample.avoidance_count[1]==FLIGHT_OBSTACLES);
    assert(sample.avoidance[1][0][0]==7 && sample.avoidance[1][FLIGHT_OBSTACLES-1][0]==9 && flight_avoidance_truncated==truncated+1);
    /* Implausible lengths still refuse. */
    *(uintptr_t*)(obstacle_model+0x98)=(uintptr_t)crowd+0x80*65537;*(uintptr_t*)(obstacle_model+0xa0)=*(uintptr_t*)(obstacle_model+0x98);
    assert(!flight_read_avoidance(&sample,obstacle_model,1));
    test_job_rng();
    test_script_rng();
    test_mdt_timed();
    test_script_rng_perf();
    test_boot_hold();
    test_md_resume();
    test_find_order();
    test_region_rng();
    test_gravidar_deadlines();
    test_portable_created();
    test_created_load();
    test_spawn_rng();
    test_actor_all();
    test_nav_params_created();
    VirtualFree(image,0,MEM_RELEASE);
    puts("native gate refusal/CAS/restoration tests pass");return 0;
}
