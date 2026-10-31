/* Device sampler (pure mapping, focus rule, fake device API, once-per-tick
 * capture, wire capture integration) and camera follow (RNG word kept, ID
 * insert count, rate limit, own ship, refusals). No real device is read. */
static void pilot_device_reset(void) {
    memset(&pilot_device_cfg,0,sizeof(pilot_device_cfg));memset(&pilot_device_mem,0,sizeof(pilot_device_mem));
    memset(&pilot_device_last,0,sizeof(pilot_device_last));memset(&pilot_device_record,0,sizeof(pilot_device_record));
    pilot_device_api_active=NULL;pilot_device_armed=pilot_device_xinput=0;pilot_device_tick=pilot_device_samples=pilot_device_unfocused=0;
}
static void pilot_camera_reset(void) {
    pilot_camera_enabled=pilot_camera_held=0;pilot_camera_stage="off";
    pilot_camera_tick=pilot_camera_last_attempt=pilot_camera_attempts=pilot_camera_applied=pilot_camera_hold_ticks=0;
    pilot_camera_lost=pilot_camera_refused=pilot_camera_steps=pilot_camera_unknown=pilot_camera_ids=pilot_camera_target=0;
}
/* Fake operating-system device state. */
static HWND test_dev_window;
static DWORD test_dev_owner,test_dev_self=4242,test_dev_pad_result=ERROR_SUCCESS,test_dev_pad_index=99;
static unsigned test_dev_key_calls,test_dev_cursor_calls,test_dev_pad_calls;
static unsigned char test_dev_down[256];
static POINT test_dev_point;
static RECT test_dev_rect;
static pilot_xinput_gamepad test_dev_pad;
static HWND WINAPI test_dev_foreground(void) {return test_dev_window;}
static DWORD WINAPI test_dev_window_thread(HWND window,LPDWORD owner) {assert(window==test_dev_window);*owner=test_dev_owner;return 77;}
static DWORD WINAPI test_dev_self_id(void) {return test_dev_self;}
static SHORT WINAPI test_dev_key(int vk) {++test_dev_key_calls;return (SHORT)(test_dev_down[vk&255]?0x8000:0);}
static BOOL WINAPI test_dev_cursor(LPPOINT p) {++test_dev_cursor_calls;*p=test_dev_point;return TRUE;}
static BOOL WINAPI test_dev_to_client(HWND window,LPPOINT p) {assert(window==test_dev_window);p->x-=100;p->y-=50;return TRUE;}
static BOOL WINAPI test_dev_client_rect(HWND window,LPRECT r) {assert(window==test_dev_window);*r=test_dev_rect;return TRUE;}
static DWORD WINAPI test_dev_pad_state(DWORD index,pilot_xinput_state *state) {
    ++test_dev_pad_calls;test_dev_pad_index=index;
    if(test_dev_pad_result==ERROR_SUCCESS) state->pad=test_dev_pad;
    return test_dev_pad_result;
}
static const pilot_device_api test_dev_api={test_dev_foreground,test_dev_window_thread,test_dev_self_id,test_dev_key,test_dev_cursor,
    test_dev_to_client,test_dev_client_rect,test_dev_pad_state};
static pilot_record test_dev_map(const pilot_device_sample *s,unsigned kinds,unsigned invert,pilot_device_memory *m) {
    pilot_device_config c={kinds,0,invert};pilot_record r;pilot_device_map(s,&c,m,&r);return r;
}
static pilot_device_sample test_dev_keys(uint32_t keys) {
    pilot_device_sample s;memset(&s,0,sizeof(s));s.focused=1;s.keys=keys;return s;
}
static int test_dev_near(int16_t value,int16_t expected) {return value>=expected-1 && value<=expected+1;}
/* Camera fakes: the switch draws thread RNG and allocates IDs like the native one. */
static uint64_t test_cam_current,test_cam_word,test_cam_inserts;
static unsigned test_cam_possible=1,test_cam_accept=1,test_cam_draws=5,test_cam_new_ids=2,test_cam_scramble,test_cam_set_calls,test_cam_possible_calls,test_cam_current_calls;
static uint64_t test_cam_get(void) {++test_cam_current_calls;return test_cam_current;}
static unsigned char test_cam_is_possible(uint64_t target,unsigned char force) {
    assert(target==0xa200 && force==1);++test_cam_possible_calls;return (unsigned char)test_cam_possible;
}
static void test_cam_set(uint64_t target,unsigned char force) {
    assert(target==0xa200 && force==1);++test_cam_set_calls;
    for(unsigned i=0;i<test_cam_draws;i++) test_cam_word=render_rng_step(test_cam_word);
    if(test_cam_scramble) test_cam_word^=0x5a5a5a5a5a5a5a5aULL; /* not reachable by stepping */
    test_cam_inserts+=test_cam_new_ids;
    if(test_cam_accept) test_cam_current=target;
}
static void test_pilot_device(void) {
    pilot_device_memory m;pilot_record r,d;pilot_device_sample s;unsigned char b[PILOT_RECORD_BYTES];
    /* Focus rule: only this process's foreground window owns keyboard/mouse. */
    assert(!pilot_device_focus(NULL,4242,4242) && !pilot_device_focus((HWND)1,0,4242) && !pilot_device_focus((HWND)1,17,4242));
    assert(pilot_device_focus((HWND)1,4242,4242));
    /* Neutral: assisted, all channels zero, no target, record v1. */
    memset(&m,0,sizeof(m));memset(&s,0,sizeof(s));r=test_dev_map(&s,PILOT_DEVICE_KINDS,0,&m);
    assert(r.mode==PILOT_MODE_ASSIST && !r.buttons && !r.target);for(unsigned i=0;i<7;i++) assert(!r.q[i]);
    /* Keyboard bindings (X4 default inputmap.xml). */
    memset(&m,0,sizeof(m));s=test_dev_keys(PILOT_KEY(PILOT_KEY_STRAFE_RIGHT)|PILOT_KEY(PILOT_KEY_STRAFE_UP)|PILOT_KEY(PILOT_KEY_ROLL_LEFT)|
        PILOT_KEY(PILOT_KEY_YAW_RIGHT)|PILOT_KEY(PILOT_KEY_STEER_DOWN));
    r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);
    assert(r.q[0]==32767 && r.q[1]==32767 && r.q[5]==32767 && r.q[4]==32767 && r.q[3]==32767 && !r.q[2]);
    s=test_dev_keys(PILOT_KEY(PILOT_KEY_STRAFE_LEFT)|PILOT_KEY(PILOT_KEY_STRAFE_DOWN)|PILOT_KEY(PILOT_KEY_ROLL_RIGHT)|
        PILOT_KEY(PILOT_KEY_YAW_LEFT)|PILOT_KEY(PILOT_KEY_STEER_UP));
    r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);
    assert(r.q[0]==-32767 && r.q[1]==-32767 && r.q[5]==-32767 && r.q[4]==-32767 && r.q[3]==-32767);
    s=test_dev_keys(PILOT_KEY(PILOT_KEY_STRAFE_LEFT)|PILOT_KEY(PILOT_KEY_STRAFE_RIGHT));r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);
    assert(!r.q[0]); /* opposite keys cancel */
    /* Unfocused: keys are ignored; a pad-only client ignores the keyboard. */
    s=test_dev_keys(PILOT_KEY(PILOT_KEY_STRAFE_RIGHT));s.focused=0;r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);assert(!r.q[0]);
    s=test_dev_keys(PILOT_KEY(PILOT_KEY_STRAFE_RIGHT));r=test_dev_map(&s,PILOT_DEVICE_PAD,0,&m);assert(!r.q[0]);
    /* Throttle setpoint: ramps once per capture tick, Backspace stops, clamps. */
    memset(&m,0,sizeof(m));s=test_dev_keys(PILOT_KEY(PILOT_KEY_ACCELERATE));
    for(unsigned i=0;i<30;i++) r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);
    assert(test_dev_near(r.q[2],16384) && r.q[6]==r.q[2]);
    s=test_dev_keys(0);r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);assert(test_dev_near(r.q[2],16384)); /* setpoint persists */
    s=test_dev_keys(PILOT_KEY(PILOT_KEY_STOP));r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);assert(!r.q[2] && m.throttle==0.0f);
    s=test_dev_keys(PILOT_KEY(PILOT_KEY_DECELERATE));for(unsigned i=0;i<200;i++) r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);
    assert(r.q[2]==-32767 && m.throttle==-1.0f);
    s=test_dev_keys(PILOT_KEY(PILOT_KEY_ACCELERATE));s.focused=0;r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);assert(r.q[2]==-32767); /* unfocused: no ramp */
    /* Boost, fire, Shift+Space mouse toggle (edge, once), Ctrl+Space (assist toggle: ignored). */
    memset(&m,0,sizeof(m));s=test_dev_keys(PILOT_KEY(PILOT_KEY_BOOST)|PILOT_KEY(PILOT_KEY_FIRE));r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);
    assert(r.mode==(PILOT_MODE_ASSIST|PILOT_MODE_BOOST) && r.buttons==1);
    memset(&m,0,sizeof(m));s=test_dev_keys(PILOT_KEY(PILOT_KEY_FIRE)|PILOT_KEY(PILOT_KEY_SHIFT));
    r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);assert(!r.buttons && m.mouse_toggle==1);
    r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);assert(m.mouse_toggle==1); /* held: no second toggle */
    s=test_dev_keys(PILOT_KEY(PILOT_KEY_SHIFT));r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);
    s=test_dev_keys(PILOT_KEY(PILOT_KEY_FIRE)|PILOT_KEY(PILOT_KEY_SHIFT));r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);assert(!m.mouse_toggle);
    s=test_dev_keys(PILOT_KEY(PILOT_KEY_FIRE)|PILOT_KEY(PILOT_KEY_CONTROL));r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);
    assert(!r.buttons && !m.mouse_toggle && r.mode==PILOT_MODE_ASSIST);
    /* Mouse steering: offset from the client centre, radius = half the smaller side, 10 % dead zone. */
    memset(&m,0,sizeof(m));s=test_dev_keys(PILOT_KEY(PILOT_KEY_MOUSE_LEFT));s.cursor=1;s.width=1600;s.height=1000;
    s.cursor_x=800;s.cursor_y=500;r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);assert(!r.q[3] && !r.q[4]);
    s.cursor_x=1300;r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);assert(r.q[4]==32767 && !r.q[3]);         /* one radius right */
    s.cursor_x=1600;r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);assert(r.q[4]==32767);                    /* clamped */
    s.cursor_x=840;r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);assert(!r.q[4]);                           /* inside 10 % */
    s.cursor_x=1050;r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);assert(test_dev_near(r.q[4],pilot_quantize(0.4f/0.9f)));
    s.cursor_x=800;s.cursor_y=1000;r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);assert(r.q[3]==32767 && !r.q[4]); /* below: nose down */
    s.cursor_y=0;r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);assert(r.q[3]==-32767);
    s.keys=0;r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);assert(!r.q[3]);                                  /* button released, no toggle */
    m.mouse_toggle=1;r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);assert(r.q[3]==-32767);                  /* toggled mode */
    s.cursor=0;r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);assert(!r.q[3]);
    s.cursor=1;s.height=0;r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);assert(!r.q[3]);                    /* minimized */
    s.height=1000;s.focused=0;r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);assert(!r.q[3]);                /* unfocused */
    /* Pad (XInput): sticks with dead zones, triggers, buttons; read regardless of focus. */
    memset(&m,0,sizeof(m));memset(&s,0,sizeof(s));s.pad=1;
    s.lx=32767;s.ly=-32768;s.rx=-32767;s.ry=32767;r=test_dev_map(&s,PILOT_DEVICE_PAD,0,&m);
    assert(r.q[0]==32767 && r.q[1]==-32767 && r.q[4]==-32767 && r.q[3]==-32767); /* stick up: nose up */
    s.lx=PILOT_PAD_LEFT_DEADZONE;s.ly=-PILOT_PAD_LEFT_DEADZONE;s.rx=PILOT_PAD_RIGHT_DEADZONE;s.ry=0;r=test_dev_map(&s,PILOT_DEVICE_PAD,0,&m);
    assert(!r.q[0] && !r.q[1] && !r.q[4] && !r.q[3]);
    s.lx=(int16_t)(PILOT_PAD_LEFT_DEADZONE+(32767-PILOT_PAD_LEFT_DEADZONE)/2);r=test_dev_map(&s,PILOT_DEVICE_PAD,0,&m);assert(test_dev_near(r.q[0],16384));
    memset(&s,0,sizeof(s));s.pad=1;s.buttons=PILOT_PAD_RIGHT_SHOULDER;s.lx=-32767;r=test_dev_map(&s,PILOT_DEVICE_PAD,0,&m);
    assert(r.q[5]==32767 && !r.q[0]);                                                                     /* RB: stick left rolls left */
    s.lx=32767;r=test_dev_map(&s,PILOT_DEVICE_PAD,0,&m);assert(r.q[5]==-32767 && !r.q[0]);
    s.buttons=0;r=test_dev_map(&s,PILOT_DEVICE_PAD,0,&m);assert(!r.q[5] && r.q[0]==32767);s.lx=0;
    s.buttons=PILOT_PAD_DPAD_UP;for(unsigned i=0;i<60;i++) r=test_dev_map(&s,PILOT_DEVICE_PAD,0,&m);assert(test_dev_near(r.q[2],32767));
    s.buttons=0;s.lt=255;r=test_dev_map(&s,PILOT_DEVICE_PAD,0,&m);assert(r.q[2]==32767);
    s.lt=(unsigned char)(PILOT_PAD_TRIGGER+(255-PILOT_PAD_TRIGGER)/2);m.throttle=0;r=test_dev_map(&s,PILOT_DEVICE_PAD,0,&m);
    assert(test_dev_near(r.q[2],pilot_quantize((float)((255-PILOT_PAD_TRIGGER)/2)/(float)(255-PILOT_PAD_TRIGGER))) && m.throttle==0.0f);
    s.lt=PILOT_PAD_TRIGGER;r=test_dev_map(&s,PILOT_DEVICE_PAD,0,&m);assert(!r.q[2]);                    /* released: setpoint */
    s.lt=255;s.buttons=PILOT_PAD_LEFT_SHOULDER;r=test_dev_map(&s,PILOT_DEVICE_PAD,0,&m);assert(r.q[2]==-32767); /* full reverse wins */
    m.throttle=0.5f;s.lt=0;s.buttons=PILOT_PAD_RIGHT_THUMB;r=test_dev_map(&s,PILOT_DEVICE_PAD,0,&m);assert(!r.q[2]);
    m.throttle=0.5f;r=test_dev_map(&s,PILOT_DEVICE_PAD,0,&m);assert(m.throttle==0.5f);                  /* R3 held: one stop */
    s.buttons=PILOT_PAD_LEFT_THUMB;s.rt=200;r=test_dev_map(&s,PILOT_DEVICE_PAD,0,&m);assert(r.mode==(PILOT_MODE_ASSIST|PILOT_MODE_BOOST) && r.buttons==1);
    s.rt=PILOT_PAD_TRIGGER;s.buttons=0;r=test_dev_map(&s,PILOT_DEVICE_PAD,0,&m);assert(!r.buttons && r.mode==PILOT_MODE_ASSIST);
    s.pad=0;s.lx=32767;r=test_dev_map(&s,PILOT_DEVICE_PAD,0,&m);assert(!r.q[0]);                         /* disconnected */
    s.pad=1;r=test_dev_map(&s,PILOT_DEVICE_KBM,0,&m);assert(!r.q[0]);                                      /* kbm-only client */
    /* Both devices add and clamp; invert mask flips single axes. */
    memset(&m,0,sizeof(m));s=test_dev_keys(PILOT_KEY(PILOT_KEY_STRAFE_RIGHT)|PILOT_KEY(PILOT_KEY_YAW_RIGHT));s.pad=1;s.lx=32767;s.rx=-32767;
    r=test_dev_map(&s,PILOT_DEVICE_KINDS,0,&m);assert(r.q[0]==32767 && !r.q[4]);
    s=test_dev_keys(PILOT_KEY(PILOT_KEY_STRAFE_RIGHT)|PILOT_KEY(PILOT_KEY_STRAFE_UP)|PILOT_KEY(PILOT_KEY_ROLL_LEFT)|
        PILOT_KEY(PILOT_KEY_YAW_RIGHT)|PILOT_KEY(PILOT_KEY_STEER_DOWN));
    r=test_dev_map(&s,PILOT_DEVICE_KBM,PILOT_INVERT_KNOWN,&m);
    assert(r.q[0]==-32767 && r.q[1]==-32767 && r.q[3]==-32767 && r.q[4]==-32767 && r.q[5]==-32767);
    r=test_dev_map(&s,PILOT_DEVICE_KBM,PILOT_INVERT_PITCH,&m);assert(r.q[3]==-32767 && r.q[4]==32767);
    /* The mapped record is a valid v1 record and round-trips exactly. */
    r.capture16=0x1234;r.target=0x176ad5;pilot_encode(&r,b);assert(b[0]==1 && !b[19] && !(b[18]&0xf0));
    assert(pilot_decode(b,&d) && !memcmp(d.q,r.q,sizeof(r.q)) && d.mode==r.mode && d.buttons==r.buttons && d.capture16==0x1234);
    /* Reading through a fake API: focus rule, pad index, no key reads unfocused. */
    pilot_device_config c={PILOT_DEVICE_KINDS,2,0};
    test_dev_window=(HWND)0x1000;test_dev_owner=4242;memset(test_dev_down,0,sizeof(test_dev_down));test_dev_down['D']=1;test_dev_down[VK_SPACE]=1;
    test_dev_point.x=900;test_dev_point.y=550;test_dev_rect.left=0;test_dev_rect.top=0;test_dev_rect.right=1600;test_dev_rect.bottom=1000;
    memset(&test_dev_pad,0,sizeof(test_dev_pad));test_dev_pad.lx=1234;test_dev_pad.buttons=PILOT_PAD_RIGHT_THUMB;test_dev_pad.rt=99;
    test_dev_key_calls=test_dev_cursor_calls=test_dev_pad_calls=0;
    pilot_device_read(&test_dev_api,&c,&s);
    assert(s.focused && s.keys==(PILOT_KEY(PILOT_KEY_STRAFE_RIGHT)|PILOT_KEY(PILOT_KEY_FIRE)) && test_dev_key_calls==PILOT_KEY_COUNT);
    assert(s.cursor && s.cursor_x==800 && s.cursor_y==500 && s.width==1600 && s.height==1000);
    assert(s.pad && s.lx==1234 && s.buttons==PILOT_PAD_RIGHT_THUMB && s.rt==99 && test_dev_pad_index==2 && test_dev_pad_calls==1);
    test_dev_owner=5150;test_dev_key_calls=test_dev_cursor_calls=0;pilot_device_read(&test_dev_api,&c,&s); /* another process's window */
    assert(!s.focused && !s.keys && !s.cursor && !test_dev_key_calls && !test_dev_cursor_calls && s.pad && s.lx==1234);
    test_dev_window=NULL;pilot_device_read(&test_dev_api,&c,&s);assert(!s.focused && s.pad);
    test_dev_window=(HWND)0x1000;test_dev_owner=4242;test_dev_pad_result=ERROR_DEVICE_NOT_CONNECTED;pilot_device_read(&test_dev_api,&c,&s);
    assert(s.focused && !s.pad && !s.lx);
    pilot_device_api no_pad=test_dev_api;no_pad.pad_state=NULL;test_dev_pad_result=ERROR_SUCCESS;test_dev_pad_calls=0;
    pilot_device_read(&no_pad,&c,&s);assert(!s.pad && !test_dev_pad_calls && s.focused);              /* no XInput library */
    c.kinds=PILOT_DEVICE_PAD;test_dev_key_calls=0;pilot_device_read(&test_dev_api,&c,&s);assert(!s.focused && !test_dev_key_calls && s.pad);
    c.kinds=PILOT_DEVICE_KBM;test_dev_pad_calls=0;pilot_device_read(&test_dev_api,&c,&s);assert(s.focused && !s.pad && !test_dev_pad_calls);
    /* Arming: strict values, identical rerun only. */
    pilot_device_reset();
    assert(!pilot_device_arm_using(0,0,0,&test_dev_api,1) && !pilot_device_arm_using(4,0,0,&test_dev_api,1) &&
           !pilot_device_arm_using(1,4,0,&test_dev_api,1) && !pilot_device_arm_using(1,0,32,&test_dev_api,1) &&
           !pilot_device_arm_using(1,0,0,NULL,1) && !pilot_device_armed);
    assert(pilot_device_arm_using(PILOT_DEVICE_KINDS,1,PILOT_INVERT_ROLL,&test_dev_api,1) && pilot_device_armed && pilot_device_xinput);
    assert(pilot_device_arm_using(PILOT_DEVICE_KINDS,1,PILOT_INVERT_ROLL,&test_dev_api,1) && !pilot_device_arm_using(PILOT_DEVICE_KBM,1,PILOT_INVERT_ROLL,&test_dev_api,1));
    assert(pilot_device_arm(PILOT_DEVICE_KINDS,1,PILOT_INVERT_ROLL) && !pilot_device_arm(PILOT_DEVICE_KINDS,0,PILOT_INVERT_ROLL));
    /* Exactly one sample per capture tick: a repeated call returns the same record. */
    memset(test_dev_down,0,sizeof(test_dev_down));test_dev_down['X']=1;test_dev_pad_calls=0;memset(&test_dev_pad,0,sizeof(test_dev_pad));
    assert(!pilot_device_capture(0,&r));
    assert(pilot_device_capture(7,&r) && pilot_device_capture(7,&d) && !memcmp(&r,&d,sizeof(r)) && test_dev_pad_calls==1 && pilot_device_samples==1);
    assert(pilot_device_mem.throttle==PILOT_THROTTLE_STEP);
    assert(pilot_device_capture(8,&r) && test_dev_pad_calls==2 && pilot_device_mem.throttle==2*PILOT_THROTTLE_STEP);
    test_dev_owner=1;assert(pilot_device_capture(9,&r) && pilot_device_unfocused==1 && pilot_device_mem.throttle==2*PILOT_THROTTLE_STEP);
    test_dev_owner=4242;
    /* Wire capture through gate.pilot_capture's path: device record, capture tick, local ring. */
    memset(pilot_locals,0,sizeof(pilot_locals));pilot_armed=1;pilot_source=PILOT_SOURCE_WIRE;pilot_delay=3;pilot_frame_tick=0;
    LONG64 captures=pilot_captures;const char *why="";
    memset(test_dev_down,0,sizeof(test_dev_down));test_dev_down['D']=1;test_dev_down[VK_TAB]=1;
    assert(pilot_capture(12,b,&why) && pilot_captures==captures+1);
    assert(pilot_decode(b,&d) && d.capture16==12 && d.q[0]==32767 && d.mode==(PILOT_MODE_ASSIST|PILOT_MODE_BOOST) && !d.target);
    pilot_local *l=&pilot_locals[12%PILOT_LOCAL_RING];assert(l->used && l->capture==12 && l->apply==15 && !memcmp(l->bytes,b,PILOT_RECORD_BYTES));
    unsigned char again[PILOT_RECORD_BYTES];test_dev_down['D']=0;assert(pilot_capture(12,again,&why) && !memcmp(again,b,sizeof(b)));
    char text[1536];pilot_slot_count=0;
    assert(pilot_status(text,sizeof(text)) && strstr(text," device=both pad=1 invert=4 xinput=1 focus=1 pad_connected=1 throttle=0.033"));
    /* A menu or the map holds controls (a deactivation share above ours): the
     * record is neutral, the throttle setpoint is held, no device is sampled. */
    static uint32_t ui_counter;ui_counter=2;pilot_controls_owned=1;pilot_controls_base=0;pilot_controls_counter_ptr=&ui_counter;
    test_dev_down['D']=1;test_dev_down['X']=1;unsigned pads=test_dev_pad_calls;LONG64 neutral=pilot_neutral;float held=pilot_device_mem.throttle;
    assert(pilot_capture(14,b,&why) && pilot_decode(b,&d) && d.capture16==14 && !d.q[0] && !d.q[1] && !d.q[3] && !d.q[4] && !d.q[5] &&
           d.q[2]==pilot_quantize(held) && d.q[6]==d.q[2] && d.mode==PILOT_MODE_ASSIST && !d.buttons && pilot_neutral==neutral+1 &&
           test_dev_pad_calls==pads && pilot_device_mem.throttle==held);
    ui_counter=1;assert(pilot_capture(15,b,&why) && pilot_decode(b,&d) && d.q[0]==32767 && test_dev_pad_calls==pads+1); /* UI closed */
    pilot_controls_owned=0;pilot_controls_counter_ptr=NULL;pilot_neutral=0;
    pilot_device_api_active=NULL;assert(!pilot_capture(16,b,&why) && !strcmp(why,"device"));
    memset(pilot_locals,0,sizeof(pilot_locals));pilot_armed=0;pilot_source=pilot_delay=0;pilot_captures=captures;
    pilot_device_reset();
    printf("pilot device sampler mapping/focus/fake-api/once-per-tick/wire-capture tests PASS\n");
}
static void test_pilot_camera(void) {
    static const pilot_camera_api api={test_cam_set,test_cam_is_possible,test_cam_get};
    uint64_t saved_issued=stats.issued;unsigned saved_slot=stats.slot;
    pilot_camera_reset();
    assert(!pilot_camera_arm() && !pilot_camera_enabled); /* needs the armed prototype */
    assert(!strcmp(pilot_camera_step_using(&api,30,&test_cam_word,&test_cam_inserts),"off"));
    memset(pilot_slots,0,sizeof(pilot_slots));pilot_slot_count=2;pilot_armed=1;stats.slot=2;
    pilot_slots[0].saved=0xae79c;pilot_slots[0].runtime=0xa100;pilot_slots[0].resolved=1;pilot_slots[0].player=1;
    pilot_slots[1].saved=0x176ad5;pilot_slots[1].runtime=0xa200;pilot_slots[1].resolved=1;
    assert(pilot_camera_arm() && pilot_camera_arm() && !strcmp(pilot_camera_stage,"armed"));
    test_cam_current=0;test_cam_word=0x1234567890abcdefULL;test_cam_inserts=100;test_cam_set_calls=test_cam_possible_calls=test_cam_current_calls=0;
    assert(!strcmp(pilot_camera_step_using(&api,30,NULL,NULL),"thread") && !test_cam_current_calls);
    /* First step: switch, keep the caller's RNG word, count the draws and IDs. */
    assert(!strcmp(pilot_camera_step_using(&api,30,&test_cam_word,&test_cam_inserts),"applied"));
    assert(test_cam_word==0x1234567890abcdefULL && pilot_camera_steps==5 && pilot_camera_ids==2 && test_cam_inserts==102);
    assert(test_cam_set_calls==1 && test_cam_possible_calls==1 && pilot_camera_attempts==1 && pilot_camera_applied==1 && pilot_camera_held);
    assert(pilot_camera_target==0x176ad5);
    /* Held: one getter call, no switch. */
    unsigned gets=test_cam_current_calls;
    assert(!strcmp(pilot_camera_step_using(&api,31,&test_cam_word,&test_cam_inserts),"held") && test_cam_current_calls==gets+1 && test_cam_set_calls==1);
    assert(pilot_camera_hold_ticks==1);
    /* The game resets the view: counted as lost, retried no sooner than RETRY ticks after the last attempt. */
    test_cam_current=0;
    assert(!strcmp(pilot_camera_step_using(&api,40,&test_cam_word,&test_cam_inserts),"waiting") && pilot_camera_lost==1 && test_cam_set_calls==1);
    assert(!strcmp(pilot_camera_step_using(&api,30+PILOT_CAMERA_RETRY-1,&test_cam_word,&test_cam_inserts),"waiting"));
    assert(!strcmp(pilot_camera_step_using(&api,30+PILOT_CAMERA_RETRY,&test_cam_word,&test_cam_inserts),"applied") && test_cam_set_calls==2);
    assert(test_cam_word==0x1234567890abcdefULL && pilot_camera_steps==10 && pilot_camera_ids==4 && pilot_camera_lost==1);
    /* Refused and not-applied attempts are counted; the word is still kept. */
    test_cam_current=0;test_cam_possible=0;
    assert(!strcmp(pilot_camera_step_using(&api,200,&test_cam_word,&test_cam_inserts),"not_possible") && pilot_camera_refused==1 && test_cam_set_calls==2);
    test_cam_possible=1;test_cam_accept=0;
    assert(!strcmp(pilot_camera_step_using(&api,300,&test_cam_word,&test_cam_inserts),"not_applied") && test_cam_set_calls==3);
    assert(test_cam_word==0x1234567890abcdefULL && pilot_camera_steps==15);
    test_cam_accept=1;pilot_camera_attempts=PILOT_CAMERA_ATTEMPTS;
    assert(!strcmp(pilot_camera_step_using(&api,400,&test_cam_word,&test_cam_inserts),"attempts_exhausted") && test_cam_set_calls==3);
    /* An unknown draw count (word replaced, not stepped) is counted, not guessed; the word is still kept. */
    pilot_camera_attempts=1;pilot_camera_last_attempt=0;test_cam_scramble=1;test_cam_current=0;test_cam_word=0x1111;
    { uint64_t before=pilot_camera_unknown,steps=pilot_camera_steps;
      assert(!strcmp(pilot_camera_step_using(&api,500,&test_cam_word,&test_cam_inserts),"applied") && pilot_camera_unknown==before+1 &&
             pilot_camera_steps==steps && test_cam_word==0x1111); }
    test_cam_scramble=0;
    test_cam_draws=5;test_cam_new_ids=2;
    /* The occupied ship's client never switches; unresolved and missing slots refuse. */
    unsigned sets=test_cam_set_calls;gets=test_cam_current_calls;stats.slot=1;
    assert(!strcmp(pilot_camera_step_using(&api,600,&test_cam_word,&test_cam_inserts),"own_ship") && test_cam_set_calls==sets && test_cam_current_calls==gets);
    stats.slot=3;assert(!strcmp(pilot_camera_step_using(&api,600,&test_cam_word,&test_cam_inserts),"no_slot"));
    stats.slot=2;pilot_slots[1].resolved=0;assert(!strcmp(pilot_camera_step_using(&api,600,&test_cam_word,&test_cam_inserts),"unresolved"));
    pilot_slots[1].resolved=1;
    char text[512];assert(pilot_camera_status(text,sizeof(text)) && strstr(text,"stage=unresolved tick=600 target=0000000000176ad5"));
    /* The real entry refuses outside an active gate (no native call is made). */
    LONG active=stats.active;stats.active=0;assert(!strcmp(pilot_camera_step(700),"guard"));stats.active=active;
    pilot_camera_reset();memset(pilot_slots,0,sizeof(pilot_slots));pilot_slot_count=0;pilot_armed=0;
    stats.issued=saved_issued;stats.slot=saved_slot;
    printf("pilot camera follow word/ids/rate/own-ship/refusal tests PASS\n");
}
