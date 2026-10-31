/* native_gate_camera_offset: apply once, keep, fail closed on any rewrite,
 * restore by CAS; pinned GetOffset semantics on the image. */
static void test_camoff_reset(void) {
    camoff_applied=camoff_restored=0;camoff_camera=NULL;camoff_tick=camoff_calls=camoff_first=camoff_holds=camoff_resumed=0;camoff_failures=0;camoff_stage="idle";
    memset(camoff_delta,0,sizeof(camoff_delta));memset(camoff_original,0,sizeof(camoff_original));memset(camoff_written,0,sizeof(camoff_written));
}
/* Stands in for GetAsyncKeyState: one held key. */
/* Stands in for GetAsyncKeyState: one held key, the modifier held or not, one tapped key (low bit, read once). */
static int test_camfol_held,test_camfol_mod=1,test_camfol_tapped;
static SHORT WINAPI test_camfol_keys(int vk) {
    if(vk==test_camfol_tapped) {test_camfol_tapped=0;return 1;}
    return (SHORT)((vk==test_camfol_held || (vk==VK_CONTROL && test_camfol_mod))?0x8000:0);
}
/* Stands in for the bank refill 0x979ce0: records the camera mode it ran with. */
static LONG test_camfol_mode_seen;static unsigned test_camfol_refills;
static void test_camfol_refill(void *camera) {test_camfol_mode_seen=*(volatile LONG *)((unsigned char*)camera+CAMFOL_MODE);++test_camfol_refills;}
/* native_gate_camera_anchor: the anchor pair follows a slot's latched ship, retargets, holds, fails closed, restores. */
static void test_camera_follow(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;
    unsigned saved_latched=view_latched,saved_count=view_count;void *saved_points[VIEW_MAX];uint64_t saved_ids[VIEW_MAX];
    memcpy(saved_points,view_points,sizeof(saved_points));memcpy(saved_ids,view_ids,sizeof(saved_ids));
    int saved_verified=camoff_verified,saved_offset=camoff_applied;void *saved_universe=*(void**)(image+CAMOFF_UNIVERSE);
    static __declspec(align(16)) unsigned char universe[0x400],camera[0x900],ship2[0x40],ship3[0x40];
    memset(universe,0,sizeof(universe));memset(camera,0,sizeof(camera));memset(ship2,0,sizeof(ship2));memset(ship3,0,sizeof(ship3));
    void *cp=camera;memcpy(universe+CAMOFF_CAMERA,&cp,8);*(void**)(image+CAMOFF_UNIVERSE)=universe;
    void *original[2]={(void*)0x1111,(void*)0x2222};memcpy(camera+CAMFOL_ANCHOR,original,16);
    *(uint64_t*)(ship2+8)=0x5ca74;*(uint64_t*)(ship3+8)=0x5ca75;
    view_latched=1;view_count=2;view_points[1]=ship2;view_ids[1]=0x5ca74;
    stats.active=1;ui_thread=GetCurrentThreadId();camoff_verified=1;camoff_applied=0;
    camfol_applied=camfol_restored=0;camfol_slot=0;camfol_failures=0;camfol_calls=camfol_retargets=camfol_absent=0;camfol_stage="idle";
    /* Key maps (before the first call): virtual keys 1..254, the modifier also 0, no key twice; the default is Ctrl+J/L/I/K/U/O/P. */
    {
        const int defaults[CAMFOL_KEYS]={VK_CONTROL,'J','L','I','K','U','O','P'};assert(!memcmp(camfol_keys,defaults,sizeof(defaults)));
        const int numpad[CAMFOL_KEYS]={0,VK_NUMPAD4,VK_NUMPAD6,VK_NUMPAD8,VK_NUMPAD2,VK_ADD,VK_SUBTRACT,VK_NUMPAD5};
        const int twice[CAMFOL_KEYS]={VK_CONTROL,'J','J','I','K','U','O','P'},wide[CAMFOL_KEYS]={VK_CONTROL,'J','L','I','K','U','O',300};
        assert(!camfol_keys_set(twice) && !camfol_keys_set(wide) && !memcmp(camfol_keys,defaults,sizeof(defaults)));
        assert(camfol_keys_set(numpad) && camfol_keys[CAMFOL_KEY_RESET]==VK_NUMPAD5);
        assert(camfol_keys_set(defaults));
    }
    /* First call: the pair becomes the slot's ship, the control update is held. */
    assert(camfol_apply(5,2,0,0) && camfol_applied && camfol_first==5);
    void *now[2];memcpy(now,camera+CAMFOL_ANCHOR,16);assert(now[0]==ship2 && now[1]==ship2);
    double hold;memcpy(&hold,camera+CAMOFF_HOLD,8);assert(hold==-1.0);
    memset(camera+CAMOFF_HOLD,0,8);assert(camfol_apply(6,2,0,0) && !strcmp(camfol_stage,"kept"));memcpy(&hold,camera+CAMOFF_HOLD,8);assert(hold==-1.0);
    /* Another ship in the slot: retargeted; the slot empty: parked (the engine's pair back), then resumed. */
    view_points[1]=ship3;view_ids[1]=0x5ca75;assert(camfol_apply(7,2,0,0) && camfol_retargets==1 && !strcmp(camfol_stage,"retargeted"));
    memcpy(now,camera+CAMFOL_ANCHOR,16);assert(now[0]==ship3 && now[1]==ship3);
    view_count=1;assert(camfol_apply(8,2,0,0) && camfol_absent==1 && camfol_parked && camfol_parks==1 && !strcmp(camfol_stage,"parked"));
    memcpy(now,camera+CAMFOL_ANCHOR,16);assert(now[0]==original[0] && now[1]==original[1]);
    assert(camfol_apply(9,2,0,0) && camfol_absent==2 && camfol_parks==1); /* stays parked */
    char text[640];assert(camfol_status(text,sizeof(text))>0 && strstr(text," applied=1 slot=2 ") && strstr(text," retargets=1 absent=2 ") && strstr(text," parked=1 parks=1 "));
    /* Restore while parked: the engine's pair is in place already. */
    camfol_restore();assert(camfol_restored);memcpy(now,camera+CAMFOL_ANCHOR,16);assert(now[0]==original[0] && now[1]==original[1]);
    camfol_restored=0;view_count=2;assert(camfol_apply(10,2,0,0) && !camfol_parked && camfol_retargets==2 && !strcmp(camfol_stage,"resumed"));
    memcpy(now,camera+CAMFOL_ANCHOR,16);assert(now[0]==ship3 && now[1]==ship3);
    camfol_restore();assert(camfol_restored);memcpy(now,camera+CAMFOL_ANCHOR,16);assert(now[0]==original[0] && now[1]==original[1]);
    /* Fail closed: overwritten, camera replaced, another slot, the offset harness active. */
    camfol_restored=0;memcpy(camera+CAMFOL_ANCHOR,camfol_written,16);
    void *foreign[2]={(void*)0x3333,(void*)0x3333};memcpy(camera+CAMFOL_ANCHOR,foreign,16);
    assert(!camfol_apply(9,2,0,0) && !strcmp(camfol_stage,"overwritten") && !camfol_check());
    camfol_failures=0;memcpy(camera+CAMFOL_ANCHOR,camfol_written,16);
    static __declspec(align(16)) unsigned char other[0x900];memset(other,0,sizeof(other));cp=other;memcpy(universe+CAMOFF_CAMERA,&cp,8);
    assert(!camfol_apply(10,2,0,0) && !strcmp(camfol_stage,"camera_replaced"));cp=camera;memcpy(universe+CAMOFF_CAMERA,&cp,8);camfol_failures=0;
    assert(!camfol_apply(11,1,0,0) && !strcmp(camfol_stage,"slot"));camfol_failures=0;
    camoff_applied=1;assert(!camfol_apply(12,2,0,0) && !strcmp(camfol_stage,"camera_offset"));camoff_applied=0;camfol_failures=0;
    /* Not yet latched at the first call: waits without writing. */
    camfol_applied=0;camfol_slot=0;memcpy(camera+CAMFOL_ANCHOR,original,16);view_count=1;
    assert(camfol_apply(13,2,0,0) && !camfol_applied && !strcmp(camfol_stage,"waiting"));memcpy(now,camera+CAMFOL_ANCHOR,16);assert(now[0]==original[0]);
    camfol_applied=camfol_restored=0;camfol_slot=0;camfol_failures=0;camfol_stage="idle";camfol_calls=camfol_retargets=camfol_absent=0;
    /* Chase view: identity rows and (0, up, -back) in the anchor's frame, fourth floats kept; the camera keeps
     * its mode and only UniverseFrameUpdate's bank refill (0x9fe573, through our stub) sees mode 1; engine
     * rewrites are adopted and ours written again (bounded); stop restores the call, PosRot and pair. */
    {
        float local[16]={0.5f,1.5f,-0.25f,7.0f, 0.9f,0.1f,0.0f,8.0f, -0.1f,0.9f,0.0f,9.0f, 0.0f,0.0f,1.0f,10.0f},check[16];
        memcpy(camera+CAMFOL_LOCAL,local,64);memcpy(camera+CAMFOL_ANCHOR,original,16);view_count=2;view_points[1]=ship2;view_ids[1]=0x5ca74;
        int32_t cockpit=0,mode_now;memcpy(camera+CAMFOL_MODE,&cockpit,4); /* mode 0, as cam-C1e's client 2 */
        unsigned char saved_call[5];memcpy(saved_call,image+CAMFOL_REFILL_CALL,5);
        image[CAMFOL_REFILL_CALL]=0xe8;int32_t rel=camfol_rel_to(image+CAMFOL_REFILL);memcpy(image+CAMFOL_REFILL_CALL+1,&rel,4);
        assert(camfol_refill_state()==0);
        uint32_t saved_entry=*(uint32_t*)(image+CAMFOL_MODE_TABLE+4);*(uint32_t*)(image+CAMFOL_MODE_TABLE+4)=0;camfol_pins_override=1;
        assert(!camfol_apply(19,2,40,10) && !strcmp(camfol_stage,"pins") && !camfol_applied && camfol_refill_state()==0);camfol_failures=0;camfol_calls=0;
        *(uint32_t*)(image+CAMFOL_MODE_TABLE+4)=CAMFOL_MODE_ANCHOR;
        assert(!camfol_apply(20,2,CAMFOL_CHASE_LIMIT*2,0) && !strcmp(camfol_stage,"chase"));camfol_failures=0;camfol_calls=0;
        assert(camfol_apply(21,2,40,10) && camfol_applied && camfol_chase && camfol_patched && camfol_refill_state()==1);
        memcpy(check,camera+CAMFOL_LOCAL,64);
        assert(check[0]==0 && check[1]==10 && check[2]==-40 && check[3]==7.0f);
        assert(check[4]==1 && check[5]==0 && check[6]==0 && check[7]==8.0f && check[8]==0 && check[9]==1 && check[10]==0 && check[11]==9.0f &&
               check[12]==0 && check[13]==0 && check[14]==1 && check[15]==10.0f);
        void *pair[2];memcpy(pair,camera+CAMFOL_ANCHOR,16);assert(pair[0]==ship2 && pair[1]==ship2);
        memcpy(&mode_now,camera+CAMFOL_MODE,4);assert(mode_now==0); /* the camera keeps the engine's mode */
        assert(camfol_apply(22,2,40,10) && !strcmp(camfol_stage,"kept"));
        /* The refill of our camera runs with mode 1 and puts mode 0 back; other cameras and a foreign pair pass. */
        test_camfol_mode_seen=-1;test_camfol_refills=0;camfol_refill_native=test_camfol_refill;
        camfol_refill(camera);assert(test_camfol_refills==1 && test_camfol_mode_seen==CAMFOL_CHASE_MODE && camfol_refills==1);
        memcpy(&mode_now,camera+CAMFOL_MODE,4);assert(mode_now==0);
        ((void (*)(void *))(void *)camfol_stub)(camera); /* through the stub, as the patched call reaches it */
        assert(test_camfol_refills==2 && test_camfol_mode_seen==CAMFOL_CHASE_MODE && camfol_refills==2);
        static __declspec(align(16)) unsigned char second_camera[0x900];memset(second_camera,0,sizeof(second_camera));
        camfol_refill(second_camera);assert(test_camfol_refills==3 && test_camfol_mode_seen==0 && camfol_refills==2);
        /* eye=: the published view bank's entry for the followed ship (inline entries at bank+0x40). */
        uint32_t saved_index=*(uint32_t*)(image+CAMFOL_VIEW_INDEX);*(uint32_t*)(image+CAMFOL_VIEW_INDEX)=1;
        unsigned char *bank=camera+0x40+CAMFOL_BANK;uint64_t capacity=8,count=2;float eye[4]={0,10,-40,0},other_eye[4]={7,7,7,0};
        void *other_key=(void*)0x7777,*ship_key=ship2;
        memcpy(bank+0x280,&capacity,8);memcpy(bank+0x290,&count,8);
        memcpy(bank,&other_key,8);memcpy(bank+0x10,other_eye,16);memcpy(bank+0x50,&ship_key,8);memcpy(bank+0x60,eye,16);
        char chase_text[640];assert(camfol_status(chase_text,sizeof(chase_text))>0 && strstr(chase_text," chase=40.0,10.0 local_restored=0 mode=0 refills=2 ") &&
                                     strstr(chase_text," resets=0 ") && strstr(chase_text," eye=0.0,10.0,-40.0"));
        count=0;memcpy(bank+0x290,&count,8);assert(camfol_status(chase_text,sizeof(chase_text))>0 && strstr(chase_text," eye=-"));
        *(uint32_t*)(image+CAMFOL_VIEW_INDEX)=saved_index;
        assert(!camfol_apply(23,2,30,10) && !strcmp(camfol_stage,"chase"));camfol_failures=0;
        /* An engine rewrite (a reset at a warp): the pair names components, the mode is valid: adopted, ours back. */
        static __declspec(align(16)) unsigned char engine_ship[0x40],engine_seat[0x40];
        memset(engine_ship,0,sizeof(engine_ship));memset(engine_seat,0,sizeof(engine_seat));
        void *in_image=image+0x2b7feb8;memcpy(engine_ship,&in_image,8);memcpy(engine_seat,&in_image,8);
        void *engine_pair[2]={engine_ship,engine_seat};memcpy(camera+CAMFOL_ANCHOR,engine_pair,16);
        float engine_local[16];memcpy(engine_local,local,64);engine_local[0]=2.0f;memcpy(camera+CAMFOL_LOCAL,engine_local,64);
        assert(camfol_apply(24,2,40,10) && !strcmp(camfol_stage,"reset") && camfol_resets==1 && camfol_reset_tick==24);
        memcpy(pair,camera+CAMFOL_ANCHOR,16);assert(pair[0]==ship2 && pair[1]==ship2 && camfol_original[0]==engine_ship && camfol_original[1]==engine_seat);
        memcpy(check,camera+CAMFOL_LOCAL,64);assert(check[0]==0 && check[1]==10 && check[2]==-40 && !memcmp(camfol_local_original,engine_local,64));
        assert(camfol_apply(25,2,40,10) && !strcmp(camfol_stage,"kept"));
        /* A foreign pair (no component) or a storm of resets: fail closed. */
        void *foreign2[2]={(void*)0x3333,(void*)0x3333};memcpy(camera+CAMFOL_ANCHOR,foreign2,16);
        assert(!camfol_apply(26,2,40,10) && !strcmp(camfol_stage,"overwritten"));camfol_failures=0;
        memcpy(camera+CAMFOL_ANCHOR,engine_pair,16);camfol_window_resets=CAMFOL_RESET_LIMIT;camfol_window_start=20;
        assert(!camfol_apply(27,2,40,10) && !strcmp(camfol_stage,"reset_storm"));camfol_failures=0;
        /* A new window allows resets again (a long session sees view changes). */
        assert(camfol_apply(20+CAMFOL_RESET_WINDOW,2,40,10) && !strcmp(camfol_stage,"reset") && camfol_window_resets==1 &&
               camfol_window_start==20+CAMFOL_RESET_WINDOW && camfol_resets==2);
        memcpy(pair,camera+CAMFOL_ANCHOR,16);assert(pair[0]==ship2);
        int32_t bad_mode=9;memcpy(camera+CAMFOL_MODE,&bad_mode,4);memcpy(camera+CAMFOL_ANCHOR,engine_pair,16); /* moved, but no valid mode */
        assert(!camfol_apply(28,2,40,10) && !strcmp(camfol_stage,"overwritten"));camfol_failures=0;memcpy(camera+CAMFOL_MODE,&cockpit,4);
        memcpy(camera+CAMFOL_ANCHOR,camfol_written,16);assert(camfol_apply(29,2,40,10) && !strcmp(camfol_stage,"kept"));
        /* The engine replaced the player camera: the old one gets its values back by CAS, the new one is followed. */
        {
            static __declspec(align(16)) unsigned char fresh[0x900];memset(fresh,0,sizeof(fresh));
            memcpy(fresh+CAMFOL_ANCHOR,engine_pair,16);memcpy(fresh+CAMFOL_LOCAL,engine_local,64);memcpy(fresh+CAMFOL_MODE,&cockpit,4);
            void *fp=fresh;memcpy(universe+CAMOFF_CAMERA,&fp,8);uint64_t before=camfol_resets;
            assert(camfol_apply(30,2,40,10) && !strcmp(camfol_stage,"reset") && camfol_camera==fresh && camfol_resets==before+1);
            memcpy(pair,fresh+CAMFOL_ANCHOR,16);assert(pair[0]==ship2 && pair[1]==ship2);
            memcpy(check,fresh+CAMFOL_LOCAL,64);assert(check[1]==10 && check[2]==-40);
            memcpy(pair,camera+CAMFOL_ANCHOR,16);assert(pair[0]==engine_ship && pair[1]==engine_seat); /* the old camera's values back */
            memcpy(check,camera+CAMFOL_LOCAL,64);assert(!memcmp(check,engine_local,64));
            assert(camfol_apply(31,2,40,10) && !strcmp(camfol_stage,"kept"));
            /* Back to the first camera for the rest of the test (it is replaced again: adopted). */
            void *cp2=camera;memcpy(universe+CAMOFF_CAMERA,&cp2,8);assert(camfol_apply(32,2,40,10) && !strcmp(camfol_stage,"reset") && camfol_camera==camera);
        }
        /* Orbit (keypad, foreground only): yaw 90 degrees in 60 ticks turns the rig about the ship's y axis. */
        {
            camfol_orbit=1;camfol_foreground_override=1;camfol_key_override=test_camfol_keys;test_camfol_held='L';
            for(unsigned t=0;t<60;t++) assert(camfol_apply(100+t,2,40,10) && !strcmp(camfol_stage,"kept"));
            assert(camfol_moves==60 && fabs(camfol_yaw-3.14159265358979323846/2)<1e-9 && camfol_pitch==0);
            memcpy(check,camera+CAMFOL_LOCAL,64);
            /* Ry(90): eye (0,10,-40) -> (-40,10,0); the camera's z axis (row 2) -> (1,0,0): still towards the ship's side it faced. */
            assert(fabsf(check[0]+40)<1e-3f && fabsf(check[1]-10)<1e-4f && fabsf(check[2])<1e-3f && check[3]==engine_local[3]);
            assert(fabsf(check[12]-1)<1e-6f && fabsf(check[13])<1e-6f && fabsf(check[14])<1e-6f && fabsf(check[9]-1)<1e-6f);
            assert(fabsf(check[4])<1e-6f && fabsf(check[6]+1)<1e-6f); /* row 0 (x axis) -> (0,0,-1) */
            /* Pitch is limited to 80 degrees; zoom within 0.25..20; not in the foreground: no input. */
            test_camfol_held='I';for(unsigned t=0;t<80;t++) camfol_apply(200+t,2,40,10);
            assert(fabs(camfol_pitch-80.0*3.14159265358979323846/180.0)<1e-9);
            test_camfol_held='O';for(unsigned t=0;t<400;t++) camfol_apply(300+t,2,40,10);assert(camfol_scale==20.0);
            camfol_foreground_override=-1;uint64_t moves=camfol_moves;test_camfol_held='J';
            assert(camfol_apply(800,2,40,10) && camfol_moves==moves);camfol_foreground_override=1;
            /* 5: back to the plain chase. */
            /* Without the modifier (Ctrl) the letters do nothing (X4 binds them); a tap between ticks moves one step. */
            test_camfol_mod=0;assert(camfol_apply(801,2,40,10) && camfol_moves==moves);
            char input_text[768];assert(camfol_status(input_text,sizeof(input_text))>0 && strstr(input_text," input=1,0,02 taps=0 "));
            test_camfol_mod=1;test_camfol_held=0;test_camfol_tapped='L';double yaw=camfol_yaw;
            assert(camfol_apply(802,2,40,10) && camfol_moves==moves+1 && camfol_yaw>yaw && camfol_taps==1 && camfol_last_move==802);
            assert(camfol_status(input_text,sizeof(input_text))>0 && strstr(input_text," input=1,1,04 taps=1 last_move=802 "));
            /* P: back to the plain chase. */
            test_camfol_held='P';assert(camfol_apply(803,2,40,10) && camfol_yaw==0 && camfol_pitch==0 && camfol_scale==1);
            memcpy(check,camera+CAMFOL_LOCAL,64);assert(check[0]==0 && check[1]==10 && check[2]==-40 && check[4]==1 && check[9]==1 && check[14]==1);
            char orbit_text[768];assert(camfol_status(orbit_text,sizeof(orbit_text))>0 && strstr(orbit_text," orbit=1 yaw=0.0 pitch=0.0 scale=1.00 moves="));
            /* An engine rewrite while orbiting keeps the pose (the reset writes the current pose). */
            test_camfol_held='L';for(unsigned t=0;t<30;t++) camfol_apply(900+t,2,40,10);test_camfol_held=0;
            float rewritten[16];memcpy(rewritten,engine_local,64);memcpy(camera+CAMFOL_LOCAL,rewritten,64);
            assert(camfol_apply(931,2,40,10) && !strcmp(camfol_stage,"reset"));
            memcpy(check,camera+CAMFOL_LOCAL,64);assert(fabsf(check[0]+40*sinf((float)(3.14159265358979323846/4)))<1e-2f);
            camfol_orbit=0;camfol_foreground_override=0;camfol_key_override=NULL;test_camfol_held=0;camfol_yaw=camfol_pitch=0;camfol_scale=1;camfol_moves=0;
            camfol_taps=camfol_last_move=0;
            __declspec(align(16)) float plain[16];camfol_pose_local(camfol_local_original,plain);
            assert(camfol_local_cas(camera,camfol_local_written,plain));memcpy(camfol_local_written,plain,64);
        }
        /* Our refill site replaced: fail closed. */
        image[CAMFOL_REFILL_CALL+2]^=1;assert(!camfol_apply(33,2,40,10) && !strcmp(camfol_stage,"refill_site"));image[CAMFOL_REFILL_CALL+2]^=1;camfol_failures=0;
        /* Stop: the call, the PosRot and the engine's latest pair go back. */
        camfol_restore();assert(camfol_restored && camfol_local_restored && camfol_unpatched && camfol_refill_state()==0);
        memcpy(check,camera+CAMFOL_LOCAL,64);assert(!memcmp(check,engine_local,64));
        memcpy(pair,camera+CAMFOL_ANCHOR,16);assert(pair[0]==engine_ship && pair[1]==engine_seat);
        camfol_refill(camera);assert(test_camfol_mode_seen==0); /* after stop the stub passes through */
        /* Status at the widest values fits the binding's buffer. */
        {
            uint64_t s_tick=camfol_tick,s_calls=camfol_calls;camfol_tick=camfol_calls=camfol_first=camfol_retargets=camfol_absent=UINT64_MAX;
            camfol_resets=camfol_reset_tick=camfol_reset_key=UINT64_MAX;camfol_refills=camfol_refill_skips=INT64_MAX;camfol_failures=INT64_MAX;
            camfol_back=-CAMFOL_CHASE_LIMIT;camfol_up=-CAMFOL_CHASE_LIMIT;camfol_stage="overwritten_local";char wide[768];
            assert(camfol_status(wide,sizeof(wide))>0 && strlen(wide)<700);
            camfol_tick=s_tick;camfol_calls=s_calls;camfol_first=0;camfol_failures=0;
        }
        assert(camfol_chase_valid(-40,0) && !camfol_chase_valid(NAN,0) && !camfol_chase_valid(0,INFINITY));
        float out[16];camfol_back=-20;camfol_up=5;camfol_yaw=camfol_pitch=0;camfol_scale=1;camfol_pose_local(local,out);
        assert(out[0]==0 && out[2]==20 && out[1]==5 && out[3]==7.0f && out[4]==1 && out[5]==0 && out[9]==1 && out[14]==1 && out[7]==8.0f);
        camfol_applied=camfol_restored=camfol_chase=camfol_local_restored=0;camfol_slot=0;camfol_failures=0;camfol_stage="idle";
        camfol_calls=camfol_retargets=camfol_absent=0;camfol_back=camfol_up=0;camfol_resets=camfol_reset_tick=camfol_reset_key=0;
        camfol_parked=0;camfol_parks=camfol_window_start=camfol_window_resets=0;
        camfol_patched=camfol_unpatched=0;camfol_refills=camfol_refill_skips=0;camfol_refill_native=NULL;camfol_pins_override=0;
        *(uint32_t*)(image+CAMFOL_MODE_TABLE+4)=saved_entry;memcpy(image+CAMFOL_REFILL_CALL,saved_call,5);
    }
    *(void**)(image+CAMOFF_UNIVERSE)=saved_universe;camoff_verified=saved_verified;camoff_applied=saved_offset;
    view_latched=saved_latched;view_count=saved_count;memcpy(view_points,saved_points,sizeof(saved_points));memcpy(view_ids,saved_ids,sizeof(saved_ids));
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;
    printf("camera anchor follow/hold/retarget/restore/refusal tests PASS\n");
}
static void test_camera_offset(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;
    static __declspec(align(16)) unsigned char universe[0x400],camera[0x900],other[0x900];
    memset(universe,0,sizeof(universe));memset(camera,0,sizeof(camera));memset(other,0,sizeof(other));
    *(void**)(image+CAMOFF_UNIVERSE)=universe;*(void**)(universe+CAMOFF_CAMERA)=camera;
    float local[4]={1.0f,2.0f,3.0f,1.0f};memcpy(camera+CAMOFF_LOCAL,local,16);*(uint32_t*)(camera+CAMOFF_MODE)=1;
    test_camoff_reset();stats.active=1;ui_thread=GetCurrentThreadId();camoff_verified=1; /* zero image: pins taken as verified */
    /* Guards: inactive gate, wrong thread, a zero or out-of-range offset. */
    stats.active=0;assert(!camoff_apply(1,0,70000,0) && !strcmp(camoff_stage,"guard"));stats.active=1;
    ui_thread=GetCurrentThreadId()+1;assert(!camoff_apply(1,0,70000,0));ui_thread=GetCurrentThreadId();
    assert(!camoff_apply(1,0,0,0) && !camoff_apply(1,0,2e7,0) && !camoff_apply(1,NAN,1,0) && !strcmp(camoff_stage,"offset"));
    camoff_failures=0;
    /* Apply once: local position += offset, w kept; then kept. */
    assert(camoff_apply(1,10.0,70000.0,-5.0) && camoff_applied && camoff_first==1 && camoff_mode==1 && !strcmp(camoff_stage,"applied"));
    float now[4];memcpy(now,camera+CAMOFF_LOCAL,16);
    assert(now[0]==11.0f && now[1]==70002.0f && now[2]==-2.0f && now[3]==1.0f);
    /* The control update is held in its "not started" state: -1.0 in +0x838 after every call. */
    double held;memcpy(&held,camera+CAMOFF_HOLD,8);assert(held==-1.0 && camoff_holds==1 && camoff_resumed==1);
    double ran=7.25;memcpy(camera+CAMOFF_HOLD,&ran,8); /* the skipped update stored the game time */
    assert(camoff_apply(2,10.0,70000.0,-5.0) && camoff_calls==2 && !strcmp(camoff_stage,"kept") && camoff_check());
    memcpy(&held,camera+CAMOFF_HOLD,8);assert(held==-1.0 && camoff_holds==2 && camoff_resumed==2);
    assert(camoff_apply(2,10.0,70000.0,-5.0) && camoff_holds==3 && camoff_resumed==2); /* nothing ran in between */
    /* Any rewrite, another camera or another offset fails closed. */
    assert(!camoff_apply(3,10.0,70001.0,-5.0) && !strcmp(camoff_stage,"offset_changed") && !camoff_check());camoff_failures=0;
    *(void**)(universe+CAMOFF_CAMERA)=other;assert(!camoff_apply(3,10.0,70000.0,-5.0) && !strcmp(camoff_stage,"camera_replaced"));
    *(void**)(universe+CAMOFF_CAMERA)=camera;camoff_failures=0;
    ((float*)(camera+CAMOFF_LOCAL))[1]=5.0f;assert(!camoff_apply(4,10.0,70000.0,-5.0) && !strcmp(camoff_stage,"overwritten"));
    /* Restore only our value; a foreign value stays. */
    camoff_restore();assert(!camoff_restored && !strcmp(camoff_stage,"restore_foreign"));
    ((float*)(camera+CAMOFF_LOCAL))[1]=70002.0f;camoff_restore();
    memcpy(now,camera+CAMOFF_LOCAL,16);assert(camoff_restored && !memcmp(now,local,16));
    /* finish() restores through camoff_restore. */
    test_camoff_reset();camoff_verified=1;assert(camoff_apply(5,0,0,1000));
    uint32_t saved_reason=stats.reason;stats.restored=0;finish(98);
    memcpy(now,camera+CAMOFF_LOCAL,16);assert(stats.reason==98 && camoff_restored && !memcmp(now,local,16));
    /* An unaligned local position refuses the CAS. */
    test_camoff_reset();camoff_verified=1;stats.active=1;
    *(void**)(universe+CAMOFF_CAMERA)=camera+8;assert(!camoff_apply(6,0,0,1000) && !strcmp(camoff_stage,"cas"));
    /* Status bounds at extreme values. */
    for(unsigned i=0;i<4;i++) camoff_original[i]=camoff_written[i]=-3.0e38f;
    for(unsigned i=0;i<3;i++) camoff_delta[i]=-1.0e7f;
    camoff_tick=camoff_calls=camoff_first=camoff_holds=camoff_resumed=UINT64_MAX;camoff_mode=UINT32_MAX;camoff_failures=INT64_MAX;camoff_stage="camera_replaced";
    char status[768];int n=camoff_status(status,sizeof(status));
    assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE CAMERA_OFFSET tick=18446744073709551615 \n")<2048);
    test_camoff_reset();camoff_verified=0;*(void**)(image+CAMOFF_UNIVERSE)=NULL;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;stats.reason=saved_reason;stats.restored=0;
    printf("camera offset apply/keep/fail-closed/restore/finish/status tests PASS\n");
}
static void test_camera_offset_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        assert(section.VirtualAddress<=nt.OptionalHeader.SizeOfImage && section.SizeOfRawData<=nt.OptionalHeader.SizeOfImage-section.VirtualAddress);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    camoff_verified=0;assert(camoff_pins());
    /* GetOffset: anchor +0x770, attached +0x778, mode +0x868, then rows +0x5b0/+0x5c0/+0x5a0 and position +0x590
     * applied to the anchor transform, the position added to the output position. */
    assert(!memcmp(image+0x97a313,"\x4c\x8b\x81\x70\x07\x00\x00",7) && !memcmp(image+0x97a337,"\x48\x8b\x99\x78\x07\x00\x00",7) &&
           !memcmp(image+0x97a34b,"\x48\x63\x81\x68\x08\x00\x00",7));
    assert(!memcmp(image+0x97a5a8,"\x0f\x10\x8e\xb0\x05\x00\x00",7) && !memcmp(image+0x97a5af,"\x0f\x10\x96\xc0\x05\x00\x00",7) &&
           !memcmp(image+0x97a5b9,"\x0f\x10\xae\x90\x05\x00\x00",7) && !memcmp(image+0x97a5f5,"\x44\x0f\x10\x8e\xa0\x05\x00\x00",8));
    assert(!memcmp(image+0x97a654,"\x0f\x58\x4d\x00\x0f\x11\x4d\x00",8));
    image[0x97a5bc]^=1;camoff_verified=0;assert(!camoff_pins());image[0x97a5bc]^=1;camoff_verified=0;
    /* The mode table: 0 the anchor plus the camera controller (+0x20), 1 and 6 the anchor alone, 2..5 the
     * class-0x57 (Space) ancestor of +0x778; the chase view's mode 1. */
    {
        const uint32_t *table=(const uint32_t*)(image+CAMFOL_MODE_TABLE);
        assert(table[0]==0x97a3fdu && table[1]==CAMFOL_MODE_ANCHOR && table[6]==CAMFOL_MODE_ANCHOR && camfol_mode_pin());
        for(unsigned m=2;m<=5;m++) assert(table[m]==0x97a383u);
        assert(!memcmp(image+0x97a383,"\x48\x8b\x03\xba\x57\x00\x00\x00",8) && !memcmp(image+0x97a36e,"\x48\x8b\xd5\xc6\x44\x24\x20\x00\x49\x8b\xc8",11) &&
               !memcmp(image+0x97a47f,"\x48\x8b\x5e\x20",4));
        /* The render eye: Camera slot 3 takes the anchor's class-0x6d zone (0x4873e0) and 0x9835a0 the bank entry for it
         * (0x979ef0); UniverseFrameUpdate refills the banks (0x9fe573 -> 0x979ce0 -> 0x9799f0: GetOffset per ancestor). */
        assert(image[0x98335b]==0xe8 && 0x983360u+*(int32_t*)(image+0x98335c)==0x4873e0u && !memcmp(image+0x4873ed,"\x41\xb8\x6d\x00\x00\x00",6));
        assert(image[0x983630]==0xe8 && 0x983635u+*(int32_t*)(image+0x983631)==0x979ef0u);
        assert(image[0x9fe573]==0xe8 && 0x9fe578u+*(int32_t*)(image+0x9fe574)==0x979ce0u && image[0x979d1c]==0xe8 &&
               0x979d21u+*(int32_t*)(image+0x979d1d)==0x9799f0u && image[0x979b0a]==0xe8 && 0x979b0fu+*(int32_t*)(image+0x979b0b)==0x97a300u);
        /* 0x979ef0 reads the published bank (g_ComponentViewCacheIndex off the MoveWorker): entries at +0x40, capacity +0x280, count +0x290. */
        assert(!memcmp(image+0x979f0e,"\x8b\x05",2) && 0x979f14u+*(int32_t*)(image+0x979f10)==CAMFOL_VIEW_INDEX &&
               !memcmp(image+0x979f20,"\x48\x69\xd8\xb0\x02\x00\x00",7) && !memcmp(image+0x979f2a,"\x4c\x8d\x4b\x40",4) &&
               !memcmp(image+0x979f4c,"\x49\x8b\x89\x90\x02\x00\x00",7));
        image[CAMFOL_MODE_TABLE+4]^=1;assert(!camfol_mode_pin());image[CAMFOL_MODE_TABLE+4]^=1;
        /* The refill call: UniverseFrameUpdate's camera-list loop calls 0x979ce0 with the camera in rcx; pinned. */
        assert(!memcmp(image+0x9fe570,"\x48\x8b\x0b",3) && camfol_refill_state()==0 && camfol_refill_pins());
        image[0x979d1e]^=1;assert(!camfol_refill_pins());image[0x979d1e]^=1;
        image[0x9fe580]^=1;assert(!camfol_refill_pins());image[0x9fe580]^=1;assert(camfol_refill_pins());
    }
    /* The control update's gate: skipped while +0x838 <= -0.9999 (the constructor stores -1.0 in +0x838/+0x840);
     * the skip path stores the TLS game time into +0x838 and the real time into +0x840. */
    int32_t d;memcpy(&d,image+0x97ff0b,4);double threshold;memcpy(&threshold,image+0x97ff0f+(int64_t)d,8);
    assert(!memcmp(image+0x97ff07,"\xf2\x0f\x10\x05",4) && threshold==-0.9999 && !memcmp(image+0x97ff0f,"\x66\x0f\x2f\x86\x38\x08\x00\x00\x0f\x83",10));
    assert(!memcmp(image+0x9793c0,"\x48\xb8\x00\x00\x00\x00\x00\x00\xf0\xbf\x48\x89\x86\x38\x08\x00\x00",17));
    assert(!memcmp(image+0x9822c1,"\x48\x89\x0f\xf2\x0f\x11\xb6\x40\x08\x00\x00",11));
    image[0x97ff10]^=1;assert(!camoff_pins());image[0x97ff10]^=1;camoff_verified=0;
    image[0x9822c2]^=1;assert(!camoff_pins());image[0x9822c2]^=1;camoff_verified=0;assert(camoff_pins());camoff_verified=0;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("camera offset native GetOffset pins, local PosRot application and tamper refusal verified");
}
