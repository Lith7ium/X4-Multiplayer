/* Pilot firing on fake engine memory: weapon-group discovery (guns only, own
 * ship only), lease through fake start/stop shooting (foreign controller
 * replaced, firing gun waits), trigger from the record with the travel mask,
 * a controller the simulation replaces re-taken (also while firing, and after
 * a deletion), a trigger the simulation changes restored, a destroyed gun
 * dropped, a player controller class or a change on the local player's ship
 * failing closed, window end (a taken gun let go) and stop clearing, the
 * script schedule and the PFIRE_1 receipt. Native image: pins, Mindless and
 * player controller vtable slots. */
static unsigned char test_pfire_controllers[16][0xb0];
static unsigned test_pfire_controller_count,test_pfire_starts,test_pfire_stops;
static struct {uint64_t runtime;void *object;} test_pfire_objects[16];
static unsigned test_pfire_object_count;
static unsigned char test_pfire_start(void *weapon) {
    unsigned char *gun=weapon,*controller=*(unsigned char**)(gun+PFIRE_CONTROLLER);++test_pfire_starts;
    if(!controller) {
        controller=test_pfire_controllers[test_pfire_controller_count++];memset(controller,0,0xb0);
        uintptr_t vtable=(uintptr_t)(image+PFIRE_MINDLESS_VTABLE);memcpy(controller,&vtable,8);memcpy(gun+PFIRE_CONTROLLER,&controller,8);
    }
    controller[PFIRE_FLAG]=1;return 1; /* slot 31 sets the flag */
}
static unsigned char test_pfire_stop(void *weapon) {
    unsigned char *gun=weapon,*controller=*(unsigned char**)(gun+PFIRE_CONTROLLER);++test_pfire_stops;
    if(controller && *(uintptr_t*)controller==(uintptr_t)(image+PFIRE_MINDLESS_VTABLE)) {controller[PFIRE_FLAG]=0;return 1;}
    memset(gun+PFIRE_CONTROLLER,0,8);return 0; /* any other controller is deleted */
}
static void *test_pfire_find(void *map,uint64_t runtime,unsigned flags) {
    assert(!map && flags==4);
    for(unsigned i=0;i<test_pfire_object_count;i++) if(test_pfire_objects[i].runtime==runtime) return test_pfire_objects[i].object;
    return NULL;
}
static void test_pfire_object(unsigned char *object,uint64_t runtime,unsigned vtable_rva,void *parent) {
    memset(object,0,0x400);uintptr_t vtable=(uintptr_t)(image+vtable_rva);memcpy(object,&vtable,8);memcpy(object+8,&runtime,8);
    memcpy(object+0x70,&parent,8);
    test_pfire_objects[test_pfire_object_count].runtime=runtime;test_pfire_objects[test_pfire_object_count++].object=object;
}
static void test_pfire_native_image(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;IMAGE_SECTION_HEADER sections[32];
    assert(fread(&dos,1,sizeof(dos),f)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && !fseek(f,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),f)==sizeof(nt) && nt.FileHeader.TimeDateStamp==0x6a1eaded && nt.FileHeader.NumberOfSections<=32);
    assert(fread(sections,sizeof(*sections),nt.FileHeader.NumberOfSections,f)==nt.FileHeader.NumberOfSections);
    static unsigned char bytes[6000];
    #define TEST_PFIRE_READ(rva,size) do { unsigned offset_=0; \
        for(unsigned j_=0;j_<nt.FileHeader.NumberOfSections;j_++) \
            if((rva)>=sections[j_].VirtualAddress && (rva)+(size)<=sections[j_].VirtualAddress+sections[j_].SizeOfRawData) \
                offset_=sections[j_].PointerToRawData+(rva)-sections[j_].VirtualAddress; \
        assert(offset_ && !fseek(f,(long)offset_,SEEK_SET) && fread(bytes,1,(size),f)==(size)); } while(0)
    for(unsigned i=0;i<sizeof(pfire_pins_list)/sizeof(*pfire_pins_list);i++) {
        assert(pfire_pins_list[i].size<=sizeof(bytes));TEST_PFIRE_READ(pfire_pins_list[i].rva,pfire_pins_list[i].size);
        assert(hash_ok(bytes,pfire_pins_list[i].size,pfire_pins_list[i].sha));
        bytes[pfire_pins_list[i].size-1]^=1;assert(!hash_ok(bytes,pfire_pins_list[i].size,pfire_pins_list[i].sha));
    }
    for(unsigned i=0;i<sizeof(pfire_vtable_slots)/sizeof(*pfire_vtable_slots);i++) {
        uint64_t entry;TEST_PFIRE_READ(PFIRE_MINDLESS_VTABLE+pfire_vtable_slots[i].slot,8);memcpy(&entry,bytes,8);
        assert(entry==nt.OptionalHeader.ImageBase+pfire_vtable_slots[i].target);
    }
    for(unsigned i=0;i<sizeof(pfire_player_slots)/sizeof(*pfire_player_slots);i++) {
        uint64_t entry;TEST_PFIRE_READ(pfire_player_slots[i].vtable+pfire_player_slots[i].slot,8);memcpy(&entry,bytes,8);
        assert(entry==nt.OptionalHeader.ImageBase+pfire_player_slots[i].target);
    }
    #undef TEST_PFIRE_READ
    fclose(f);
    puts("pinned native image pilot fire: Weapon::Move, shoot decision, start/stop shooting, Mindless leaves and vtable slots, player controller vtable slots verified");
}
/* X4's lazy refill: after the reload time the magazine is full and the last shot is now minus the cooldown. */
static unsigned test_pfire_reloads;
static unsigned char test_pfire_reload(void *weapon,double now) {
    unsigned char *gun=weapon;double last=0;uint32_t magazine=0;
    ++test_pfire_reloads;
    memcpy(&last,gun+PFIRE_LAST_SHOT,8);memcpy(&magazine,gun+PFIRE_MAGAZINE,4);
    if(magazine>=20 || now<=last+5.0) return 0;
    last=now-0.5;magazine=20;memcpy(gun+PFIRE_LAST_SHOT,&last,8);memcpy(gun+PFIRE_MAGAZINE,&magazine,4);return 1;
}
static double test_pfire_clock;
static int test_pfire_now(double *now) {*now=test_pfire_clock;return 1;}
static void test_pilot_fire(void) {
    DWORD protection;
    assert(VirtualProtect(image+PFIRE_START,16,PAGE_EXECUTE_READWRITE,&protection) && VirtualProtect(image+PFIRE_STOP,16,PAGE_EXECUTE_READWRITE,&protection));
    thunk_absolute_jump(image+PFIRE_START,(void*)test_pfire_start);thunk_absolute_jump(image+PFIRE_STOP,(void*)test_pfire_stop);
    assert(VirtualProtect(image+PFIRE_RELOAD,16,PAGE_EXECUTE_READWRITE,&protection));
    thunk_absolute_jump(image+PFIRE_RELOAD,(void*)test_pfire_reload);
    int (*saved_now)(double *)=pfire_now;pfire_now=test_pfire_now;test_pfire_clock=0;test_pfire_reloads=0;
    pfire_find_override=test_pfire_find;test_pfire_object_count=test_pfire_controller_count=test_pfire_starts=test_pfire_stops=0;
    uint64_t saved_issued=stats.issued;int saved_active=stats.active,saved_identity=world_identity_installed;DWORD saved_ui=ui_thread;
    stats.active=1;stats.issued=0;ui_thread=GetCurrentThreadId();world_identity_installed=1;
    /* Two ships; ship 1's active primary group: two guns, a missile launcher, a gun of ship 2. */
    static unsigned char ship1[0x800],ship2[0x800],gun1[0x400],gun2[0x400],launcher[0x400],gun3[0x400],basic[0xb0],zone[0x100];
    test_pfire_object(ship1,0xa1,0x7777,zone);test_pfire_object(ship2,0xa2,0x7777,zone);
    test_pfire_object(gun1,0xb1,PFIRE_WEAPON_VTABLE,ship1);test_pfire_object(gun2,0xb2,PFIRE_WEAPON_VTABLE,ship1);
    test_pfire_object(launcher,0xb3,0x2b02988u,ship1);test_pfire_object(gun3,0xb4,PFIRE_WEAPON_VTABLE,ship2);
    unsigned char *basic_=basic;uintptr_t other_vtable=(uintptr_t)(image+0x2bcd000);memcpy(basic,&other_vtable,8);
    memcpy(gun2+PFIRE_CONTROLLER,&basic_,8); /* an AI controller on gun 2 */
    static uint64_t ids1[4]={0xb1,0xb2,0xb3,0xb4},ids2[1]={0xb4};static unsigned char group1[2][0x18],group2[0x18];
    uint64_t *at=ids1,*end=ids1+4;memcpy(group1[1],&at,8);memcpy(group1[1]+8,&end,8); /* active group 1 */
    unsigned char *g=group1[0],*ge=group1[0]+2*0x18;memcpy(ship1+PFIRE_GROUPS,&g,8);memcpy(ship1+PFIRE_GROUPS+8,&ge,8);
    uint32_t active=1;memcpy(ship1+PFIRE_ACTIVE,&active,4);
    at=ids2;end=ids2+1;memcpy(group2,&at,8);memcpy(group2+8,&end,8);g=group2;ge=group2+0x18;
    memcpy(ship2+PFIRE_GROUPS,&g,8);memcpy(ship2+PFIRE_GROUPS+8,&ge,8);
    gun3[PFIRE_FIRING]=1; /* ship 2's gun is firing: its lease waits */
    memset(pilot_slots,0,sizeof(pilot_slots));pilot_slot_count=2;pilot_start=10;pilot_stop=20;pilot_armed=1;
    pilot_slots[0]=(pilot_slot){.saved=0x51,.object=ship1,.resolved=1,.driving=1};pilot_slots[1]=(pilot_slot){.saved=0x52,.object=ship2,.resolved=1,.driving=1};
    /* Arming. */
    pfire_pins_checked=0;assert(!pfire_arm(PFIRE_RECORD) && pfire_pins_checked==-1);
    pfire_pins_checked=1;assert(!pfire_arm(PFIRE_OFF) && pfire_arm(PFIRE_RECORD) && pfire_arm(PFIRE_RECORD) && !pfire_arm(PFIRE_SCRIPT));
    /* Before the window nothing happens. */
    stats.issued=pilot_frame_tick=9;assert(pfire_before_release() && !test_pfire_starts && !pfire_slots[0].leased);
    stats.issued=8;assert(!pfire_before_release() && !strcmp(pfire_stage,"frame"));
    /* Tick 10: ship 1 leased (gun 2's AI controller replaced), ship 2 waits; trigger set. */
    pilot_slots[0].record.buttons=1;pilot_slots[1].record.buttons=1;stats.issued=pilot_frame_tick=10;
    assert(pfire_before_release() && pfire_slots[0].leased && pfire_slots[0].count==2 && !pfire_slots[1].leased && pfire_lease_waits==1);
    unsigned char *c1=*(unsigned char**)(gun1+PFIRE_CONTROLLER),*c2=*(unsigned char**)(gun2+PFIRE_CONTROLLER);
    assert(c1 && c2 && c2!=basic && test_pfire_starts==2 && test_pfire_stops==1 && pfire_installs==2 && pfire_replaced_at_lease==1);
    assert(c1[PFIRE_FLAG]==1 && c2[PFIRE_FLAG]==1 && pfire_triggered==1);
    /* Tick 11: released trigger; ship 2's gun stopped firing and is leased. */
    pilot_slots[0].record.buttons=0;gun3[PFIRE_FIRING]=0;stats.issued=pilot_frame_tick=11;
    assert(pfire_before_release() && !c1[PFIRE_FLAG] && !c2[PFIRE_FLAG] && pfire_slots[1].leased && pfire_slots[1].count==1);
    unsigned char *c3=*(unsigned char**)(gun3+PFIRE_CONTROLLER);assert(c3 && c3[PFIRE_FLAG]==1);
    /* Travel masks the trigger; an undriven slot does not fire. */
    pilot_slots[0].record.buttons=1;pilot_slots[0].record.mode=PILOT_MODE_TRAVEL;pilot_slots[1].driving=0;stats.issued=pilot_frame_tick=12;
    assert(pfire_before_release() && !c1[PFIRE_FLAG] && !c3[PFIRE_FLAG]);
    pilot_slots[0].record.mode=PILOT_MODE_ASSIST;pilot_slots[1].driving=1;
    /* The receipt reads the guns' native firing state. */
    gun1[PFIRE_FIRING]=1;double last=99.5;memcpy(gun1+PFIRE_LAST_SHOT,&last,8);uint32_t magazine=7;memcpy(gun1+PFIRE_MAGAZINE,&magazine,4);
    char text[1024];assert(pfire_frame_text(text,sizeof(text)) && !strncmp(text,"PFIRE_1:12:1:",13));
    uint64_t fold=pfire_receipt;assert(pfire_frame_text(text,sizeof(text)) && pfire_receipt==fold);
    magazine=6;memcpy(gun1+PFIRE_MAGAZINE,&magazine,4);assert(pfire_frame_text(text,sizeof(text)) && pfire_receipt!=fold);
    /* A HUD that already asked for this gun changes nothing: the receipt asks at the same game time on every client (live fire-S). */
    {   unsigned reloads=test_pfire_reloads;double now=200.0,polled_last=0;uint32_t polled_magazine=0;test_pfire_clock=now;
        assert(pfire_frame_text(text,sizeof(text)) && test_pfire_reloads>reloads);uint64_t unpolled=pfire_receipt;
        memcpy(&polled_last,gun1+PFIRE_LAST_SHOT,8);memcpy(&polled_magazine,gun1+PFIRE_MAGAZINE,4);assert(polled_last==199.5 && polled_magazine==20);
        last=99.5;memcpy(gun1+PFIRE_LAST_SHOT,&last,8);magazine=6;memcpy(gun1+PFIRE_MAGAZINE,&magazine,4);
        test_pfire_reload(gun1,now);assert(pfire_frame_text(text,sizeof(text)) && pfire_receipt==unpolled);
        char guns[256];assert(pfire_guns_text(guns,sizeof(guns)) && strstr(guns,":20:") && strstr(guns,"mindless"));
        test_pfire_clock=0;last=99.5;memcpy(gun1+PFIRE_LAST_SHOT,&last,8);magazine=6;memcpy(gun1+PFIRE_MAGAZINE,&magazine,4);}
    gun1[PFIRE_FIRING]=0;
    /* The simulation takes a leased gun (vps-H3k2, tick ~466: the AI-flown slot ship's fight script
     * <shoot_at> installs a LargeTarget controller): identical on every client, so the gun is
     * re-taken at the next path_release - the AI controller deleted, a Mindless installed, the
     * trigger rewritten - counted, not a stop. */
    static unsigned char large[0xb0],multiple[0xb0],player_ctl[0xb0];
    uintptr_t vt=(uintptr_t)(image+0x2b7d088u);memcpy(large,&vt,8);vt=(uintptr_t)(image+0x2b7cdf8u);memcpy(multiple,&vt,8);
    vt=(uintptr_t)(image+PFIRE_PLAYER_VTABLE);memcpy(player_ctl,&vt,8);
    unsigned char *large_=large,*multiple_=multiple,*player_=player_ctl,*none=NULL;
    unsigned starts=test_pfire_starts,stops=test_pfire_stops;
    memcpy(gun2+PFIRE_CONTROLLER,&large_,8);stats.issued=pilot_frame_tick=13;
    assert(pfire_before_release() && !strcmp(pfire_stage,"live") && pfire_replaced_by_sim==1 && pfire_retaken==1);
    assert(test_pfire_stops==stops+1 && test_pfire_starts==starts+1);
    unsigned char *c2b=*(unsigned char**)(gun2+PFIRE_CONTROLLER);
    assert(c2b && c2b!=large && c2b[PFIRE_FLAG]==1 && c1[PFIRE_FLAG]==1 && pfire_slots[0].guns[1].controller==c2b);
    assert(pfire_last_tick==13 && pfire_last_slot==1 && pfire_last_gun==1 && !strcmp(pfire_last_class,"large_target") && !pfire_last_local);
    /* The fight script's <stop_shooting> clears our trigger: rewritten from the record, counted. */
    c1[PFIRE_FLAG]=0;stats.issued=pilot_frame_tick=14;
    assert(pfire_before_release() && c1[PFIRE_FLAG]==1 && pfire_trigger_restored==1 && pfire_retaken==1);
    /* Taken while firing: the gun waits, its trigger untouched; once it stops firing it is re-taken. */
    memcpy(gun1+PFIRE_CONTROLLER,&multiple_,8);gun1[PFIRE_FIRING]=1;stops=test_pfire_stops;stats.issued=pilot_frame_tick=15;
    assert(pfire_before_release() && pfire_retake_waits==1 && pfire_slots[0].guns[0].foreign && pfire_replaced_by_sim==2);
    assert(*(unsigned char**)(gun1+PFIRE_CONTROLLER)==multiple && test_pfire_stops==stops && c2b[PFIRE_FLAG]==1);
    gun1[PFIRE_FIRING]=0;stats.issued=pilot_frame_tick=16;
    assert(pfire_before_release() && pfire_retaken==2 && pfire_replaced_by_sim==2 && !pfire_slots[0].guns[0].foreign);
    c1=*(unsigned char**)(gun1+PFIRE_CONTROLLER);assert(c1 && c1!=multiple && c1[PFIRE_FLAG]==1 && test_pfire_stops==stops+1);
    /* Deleted without a successor: start shooting alone. */
    memcpy(gun2+PFIRE_CONTROLLER,&none,8);starts=test_pfire_starts;stops=test_pfire_stops;stats.issued=pilot_frame_tick=17;
    assert(pfire_before_release() && pfire_retaken==3 && test_pfire_starts==starts+1 && test_pfire_stops==stops);
    c2=*(unsigned char**)(gun2+PFIRE_CONTROLLER);assert(c2 && c2[PFIRE_FLAG]==1 && !strcmp(pfire_last_class,"none"));
    /* Possibly this client alone: a player controller class on any ship, or any change on the local
     * player's ship - fail closed (112), and the gun is not touched. */
    memcpy(gun1+PFIRE_CONTROLLER,&player_,8);stops=test_pfire_stops;stats.issued=pilot_frame_tick=18;
    assert(!pfire_before_release() && !strcmp(pfire_stage,"controller_replaced") && pfire_last_local && !strcmp(pfire_last_class,"player"));
    assert(test_pfire_stops==stops && *(unsigned char**)(gun1+PFIRE_CONTROLLER)==player_ctl);
    memcpy(gun1+PFIRE_CONTROLLER,&c1,8);pilot_slots[0].player=1;memcpy(gun2+PFIRE_CONTROLLER,&large_,8);
    assert(!pfire_before_release() && !strcmp(pfire_stage,"controller_replaced") && pfire_last_local &&
           !strcmp(pfire_last_class,"large_target") && pfire_last_gun==1);
    memcpy(gun2+PFIRE_CONTROLLER,&c2,8);c1[PFIRE_FLAG]=1;assert(pfire_before_release() && pfire_trigger_restored==1); /* ours again */
    pilot_slots[0].player=0;
    /* A gun destroyed (no longer found by its id) is dropped. */
    test_pfire_objects[3].runtime=0xdead;stats.issued=pilot_frame_tick=19;
    assert(pfire_before_release() && pfire_slots[0].count==1 && pfire_slots[0].guns[0].object==gun1 && pfire_guns_lost==1);
    char text2[1024];assert(pfire_frame_text(text2,sizeof(text2)));
    test_pfire_objects[3].runtime=0xb2;
    /* After the window the triggers are cleared once; a gun the simulation took meanwhile is let go. */
    memcpy(gun1+PFIRE_CONTROLLER,&large_,8);
    assert(c1[PFIRE_FLAG] && c3[PFIRE_FLAG]);stats.issued=pilot_frame_tick=21;
    assert(pfire_before_release() && c3[PFIRE_FLAG]==0 && pfire_let_go==1 && pfire_slots[0].guns[0].foreign && !pfire_slots[0].guns[0].flag);
    assert(*(unsigned char**)(gun1+PFIRE_CONTROLLER)==large);
    stats.issued=pilot_frame_tick=22;assert(pfire_before_release() && pfire_let_go==1); /* once */
    /* A stop clears what is still ours, and nothing that is not. */
    c3[PFIRE_FLAG]=1;pfire_slots[1].guns[0].flag=1;pfire_slots[0].guns[0].flag=1;large[PFIRE_FLAG]=1;
    pfire_restore();assert(!c3[PFIRE_FLAG] && large[PFIRE_FLAG]==1 && pfire_cleared>=1 && pfire_released);
    assert(pfire_status(text,sizeof(text)) && strstr(text,"mode=record armed=1") && strstr(text," s1=1/1/0 s2=1/1/0"));
    assert(strstr(text,"replaced_by_sim=4 retaken=3 retake_waits=1 trigger_restored=1 guns_lost=1 let_go=1") &&
           strstr(text,"last_replaced=t21/s1/g0/large_target/sim"));
    /* Script schedule: 30 of every 120 ticks per slot, offset by slot. */
    pfire_mode=PFIRE_SCRIPT;
    assert(pfire_wanted(&pilot_slots[0],1,10+113) && !pfire_wanted(&pilot_slots[0],1,10+30) && pfire_wanted(&pilot_slots[1],2,10+106) &&
           !pfire_wanted(&pilot_slots[0],1,9));
    /* Script fire of one slot: a shooter and a target. */
    pfire_script_mask=2;assert(!pfire_wanted(&pilot_slots[0],1,10+113) && pfire_wanted(&pilot_slots[1],2,10+106));pfire_script_mask=0;
    /* Reset to default off. */
    pfire_armed=0;pfire_mode=PFIRE_OFF;pfire_pins_checked=0;pfire_released=0;pfire_stage="off";memset(pfire_slots,0,sizeof(pfire_slots));
    pfire_frames=pfire_tick=pfire_triggered=pfire_lease_waits=pfire_installs=pfire_replaced_at_lease=pfire_cleared=pfire_receipt_tick=pfire_receipt=0;
    pfire_replaced_by_sim=pfire_retaken=pfire_retake_waits=pfire_trigger_restored=pfire_guns_lost=pfire_let_go=pfire_last_tick=0;
    pfire_last_slot=pfire_last_gun=0;pfire_last_local=0;pfire_last_class="none";
    pfire_failures=0;pfire_find_override=NULL;memset(pilot_slots,0,sizeof(pilot_slots));pilot_slot_count=0;pilot_start=pilot_stop=pilot_frame_tick=0;
    pilot_armed=0;memset(image+PFIRE_START,0,16);memset(image+PFIRE_STOP,0,16);memset(image+PFIRE_RELOAD,0,16);pfire_now=saved_now;
    stats.issued=saved_issued;stats.active=saved_active;world_identity_installed=saved_identity;ui_thread=saved_ui;
    printf("pilot fire group discovery/lease/replace/wait/trigger/travel mask/simulation re-take (H3k2)/trigger restore/lost gun/"
           "local fail-closed/window end let-go/stop/script/receipt tests PASS\n");
}
