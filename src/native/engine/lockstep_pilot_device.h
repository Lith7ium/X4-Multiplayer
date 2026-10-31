/* Default-off mod-owned device sampler for the piloting prototype: the local
 * pilot record while native player controls are parked (DeactivatePlayerControls
 * removes the flight input contexts, so the processed input channels read zero).
 * Runs only at the existing capture point (gate.pilot_capture, UI thread, once
 * per admitted tick); no hook, no engine call, no engine write. It reads
 * operating-system device state only:
 *   keyboard/mouse: GetAsyncKeyState, GetCursorPos/ScreenToClient/GetClientRect,
 *     and only while this process owns the foreground window
 *     (GetForegroundWindow + GetWindowThreadProcessId == this process), so two
 *     X4 windows on one PC never react to the same keys;
 *   gamepad: XInputGetState from xinput1_4.dll (xinput9_1_0.dll as fallback),
 *     loaded with LoadLibraryW/GetProcAddress; an absent library leaves the pad
 *     off. The pad is read regardless of focus, so one human can fly with
 *     keyboard/mouse in one window while another uses a pad in the other.
 * user32 entry points are resolved at configuration time as well, so the gate
 * DLL keeps its link line. The sample is quantised into the existing record v1
 * channels exactly once per capture tick (pilot_device_map, a pure function);
 * the throttle setpoint and the mouse-steering toggle are capture-side state.
 * Every client decodes the same integers; nothing else leaves this file.
 *
 * Fixed bindings (X4 default input map libraries/inputmap.xml unless marked +):
 *   A / D strafe left/right, W / S strafe up/down, X / Z accelerate/decelerate
 *   (throttle setpoint ramp), Backspace stop (setpoint 0), Q / E roll left/right,
 *   arrow Left/Right yaw, arrow Down steer up / arrow Up steer down, Tab boost,
 *   Space fire primary (not with Shift or Ctrl), Shift+Space toggle mouse
 *   steering, hold left mouse button for mouse steering: cursor offset from the
 *   client-area centre, radius = half the smaller client dimension, 10 % radial
 *   dead zone (inputmap "deadzone" 10), linear beyond.
 *   Pad: left stick strafe x/y, right stick yaw/pitch (stick up = nose up),
 *   left trigger throttle range (absolute forward while pressed), LB full
 *   reverse while held, L3 boost, right trigger fire primary;
 *   + RB held: left stick x rolls instead of strafing, + D-pad up/down throttle
 *   setpoint ramp, + R3 stop (setpoint 0). X4's default pad has no roll and no
 *   stop; RB (fire secondary) and R3 (loot magnet) are flight actions in X4,
 *   presumably inert while its controls are parked; D-pad up/down are also
 *   X4's target lock / next target action. Face buttons, Start, Back and
 *   D-pad left/right (X4 UI, target and weapon-group actions) are not used.
 * Sign convention (assumed, see invert mask): +x right, +y up, +yaw nose right,
 * +pitch nose down, +roll left (right wing up). Flight assist is always set;
 * travel mode is never set. */
#define PILOT_DEVICE_KBM 1u
#define PILOT_DEVICE_PAD 2u
#define PILOT_DEVICE_KINDS 3u
#define PILOT_INVERT_PITCH 1u
#define PILOT_INVERT_YAW 2u
#define PILOT_INVERT_ROLL 4u
#define PILOT_INVERT_STRAFE_X 8u
#define PILOT_INVERT_STRAFE_Y 16u
#define PILOT_INVERT_KNOWN 31u
#define PILOT_THROTTLE_STEP (1.0f/(float)gate_tick_hz) /* setpoint ramp per capture tick: 1 s from 0 to full at any tick rate */
#define PILOT_MOUSE_DEADZONE 0.10f
#define PILOT_PAD_LEFT_DEADZONE 7849  /* XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE */
#define PILOT_PAD_RIGHT_DEADZONE 8689 /* XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE */
#define PILOT_PAD_TRIGGER 30          /* XINPUT_GAMEPAD_TRIGGER_THRESHOLD */
#define PILOT_PAD_DPAD_UP 0x0001u
#define PILOT_PAD_DPAD_DOWN 0x0002u
#define PILOT_PAD_LEFT_THUMB 0x0040u
#define PILOT_PAD_RIGHT_THUMB 0x0080u
#define PILOT_PAD_LEFT_SHOULDER 0x0100u
#define PILOT_PAD_RIGHT_SHOULDER 0x0200u
enum {
    PILOT_KEY_STRAFE_LEFT,PILOT_KEY_STRAFE_RIGHT,PILOT_KEY_STRAFE_UP,PILOT_KEY_STRAFE_DOWN,
    PILOT_KEY_ACCELERATE,PILOT_KEY_DECELERATE,PILOT_KEY_STOP,PILOT_KEY_ROLL_LEFT,PILOT_KEY_ROLL_RIGHT,
    PILOT_KEY_YAW_LEFT,PILOT_KEY_YAW_RIGHT,PILOT_KEY_STEER_UP,PILOT_KEY_STEER_DOWN,
    PILOT_KEY_BOOST,PILOT_KEY_FIRE,PILOT_KEY_SHIFT,PILOT_KEY_CONTROL,PILOT_KEY_MOUSE_LEFT,PILOT_KEY_COUNT
};
#define PILOT_KEY(k) (1u<<(k))
static const unsigned char pilot_key_vk[PILOT_KEY_COUNT]={
    'A','D','W','S','X','Z',VK_BACK,'Q','E',VK_LEFT,VK_RIGHT,VK_DOWN,VK_UP,VK_TAB,VK_SPACE,VK_SHIFT,VK_CONTROL,VK_LBUTTON};
typedef struct {
    unsigned focused;            /* the foreground window belongs to this process */
    uint32_t keys;               /* PILOT_KEY bits, zero unless focused */
    int cursor;                  /* cursor and client rectangle valid */
    long cursor_x,cursor_y,width,height; /* client coordinates of the foreground window */
    unsigned pad;                /* XInput state valid (library present, controller connected) */
    int16_t lx,ly,rx,ry;
    unsigned char lt,rt;
    uint16_t buttons;
} pilot_device_sample;
typedef struct {unsigned kinds,pad,invert;} pilot_device_config;
typedef struct {float throttle;unsigned mouse_toggle;uint32_t previous;uint16_t previous_buttons;} pilot_device_memory;
/* XINPUT_STATE layout (xinput.h), declared here so no import library is needed. */
typedef struct {uint16_t buttons;unsigned char lt,rt;int16_t lx,ly,rx,ry;} pilot_xinput_gamepad;
typedef struct {uint32_t packet;pilot_xinput_gamepad pad;} pilot_xinput_state;
typedef DWORD (WINAPI *pilot_xinput_fn)(DWORD,pilot_xinput_state *);
typedef struct {
    HWND (WINAPI *foreground)(void);
    DWORD (WINAPI *window_thread)(HWND,LPDWORD);
    DWORD (WINAPI *self)(void);
    SHORT (WINAPI *key)(int);
    BOOL (WINAPI *cursor)(LPPOINT);
    BOOL (WINAPI *to_client)(HWND,LPPOINT);
    BOOL (WINAPI *client_rect)(HWND,LPRECT);
    pilot_xinput_fn pad_state; /* NULL: no XInput library */
} pilot_device_api;

static float pilot_device_clamp(float v) {return v>1.0f?1.0f:v<-1.0f?-1.0f:v;}
static float pilot_device_stick(int16_t raw,int dead) {
    int v=raw<-32767?-32767:raw; /* symmetric range */
    if(v<=dead && v>=-dead) return 0.0f;
    return (float)(v>0?v-dead:v+dead)/(float)(32767-dead);
}
/* The focus rule: keyboard and mouse belong to the process that owns the
 * foreground window. No window or another process means no keyboard/mouse. */
static int pilot_device_focus(HWND foreground,DWORD owner,DWORD self) {return foreground!=NULL && owner!=0 && owner==self;}
/* Pure mapping from one device snapshot (and the capture-side memory) to the
 * record v1 channels. Called once per capture tick. */
static void pilot_device_map(const pilot_device_sample *s,const pilot_device_config *c,pilot_device_memory *m,pilot_record *r) {
    float x=0,y=0,pitch=0,yaw=0,roll=0,throttle_override=0;int override=0;unsigned boost=0,fire=0;
    uint32_t k=(c->kinds&PILOT_DEVICE_KBM) && s->focused?s->keys:0,pressed=k&~m->previous;
    m->previous=k;
    if(k) {
        unsigned shift=(k&PILOT_KEY(PILOT_KEY_SHIFT))!=0,control=(k&PILOT_KEY(PILOT_KEY_CONTROL))!=0;
        x+=(float)((k>>PILOT_KEY_STRAFE_RIGHT)&1)-(float)((k>>PILOT_KEY_STRAFE_LEFT)&1);
        y+=(float)((k>>PILOT_KEY_STRAFE_UP)&1)-(float)((k>>PILOT_KEY_STRAFE_DOWN)&1);
        roll+=(float)((k>>PILOT_KEY_ROLL_LEFT)&1)-(float)((k>>PILOT_KEY_ROLL_RIGHT)&1);
        yaw+=(float)((k>>PILOT_KEY_YAW_RIGHT)&1)-(float)((k>>PILOT_KEY_YAW_LEFT)&1);
        pitch+=(float)((k>>PILOT_KEY_STEER_DOWN)&1)-(float)((k>>PILOT_KEY_STEER_UP)&1);
        if(k&PILOT_KEY(PILOT_KEY_ACCELERATE)) m->throttle+=PILOT_THROTTLE_STEP;
        if(k&PILOT_KEY(PILOT_KEY_DECELERATE)) m->throttle-=PILOT_THROTTLE_STEP;
        if(k&PILOT_KEY(PILOT_KEY_STOP)) m->throttle=0.0f;
        boost|=(k>>PILOT_KEY_BOOST)&1;
        if(k&PILOT_KEY(PILOT_KEY_FIRE)) {if(!shift && !control) fire=1;}
        if((pressed&PILOT_KEY(PILOT_KEY_FIRE)) && shift && !control) m->mouse_toggle^=1u;
    }
    if((c->kinds&PILOT_DEVICE_KBM) && s->focused && (m->mouse_toggle || (k&PILOT_KEY(PILOT_KEY_MOUSE_LEFT))) &&
       s->cursor && s->width>0 && s->height>0) {
        float radius=0.5f*(float)(s->width<s->height?s->width:s->height);
        float vx=((float)s->cursor_x-0.5f*(float)s->width)/radius,vy=((float)s->cursor_y-0.5f*(float)s->height)/radius;
        float length=sqrtf(vx*vx+vy*vy);
        if(length>PILOT_MOUSE_DEADZONE) {
            float scaled=(length-PILOT_MOUSE_DEADZONE)/(1.0f-PILOT_MOUSE_DEADZONE);
            if(scaled>1.0f) scaled=1.0f;
            yaw+=vx*scaled/length;pitch+=vy*scaled/length; /* cursor below centre: nose down */
        }
    }
    if((c->kinds&PILOT_DEVICE_PAD) && s->pad) {
        uint16_t b=s->buttons,pressed_buttons=(uint16_t)(b&~m->previous_buttons);
        m->previous_buttons=b;
        float lx=pilot_device_stick(s->lx,PILOT_PAD_LEFT_DEADZONE);
        if(b&PILOT_PAD_RIGHT_SHOULDER) roll-=lx; /* RB + stick left: roll left */
        else x+=lx;
        y+=pilot_device_stick(s->ly,PILOT_PAD_LEFT_DEADZONE);
        yaw+=pilot_device_stick(s->rx,PILOT_PAD_RIGHT_DEADZONE);
        pitch-=pilot_device_stick(s->ry,PILOT_PAD_RIGHT_DEADZONE); /* stick up: nose up */
        if(b&PILOT_PAD_DPAD_UP) m->throttle+=PILOT_THROTTLE_STEP;
        if(b&PILOT_PAD_DPAD_DOWN) m->throttle-=PILOT_THROTTLE_STEP;
        if(pressed_buttons&PILOT_PAD_RIGHT_THUMB) m->throttle=0.0f;
        if(s->lt>PILOT_PAD_TRIGGER) {override=1;throttle_override=(float)(s->lt-PILOT_PAD_TRIGGER)/(float)(255-PILOT_PAD_TRIGGER);}
        if(b&PILOT_PAD_LEFT_SHOULDER) {override=1;throttle_override=-1.0f;}
        boost|=(b&PILOT_PAD_LEFT_THUMB)!=0;
        fire|=s->rt>PILOT_PAD_TRIGGER;
    } else m->previous_buttons=0;
    m->throttle=pilot_device_clamp(m->throttle);
    if(c->invert&PILOT_INVERT_PITCH) pitch=-pitch;
    if(c->invert&PILOT_INVERT_YAW) yaw=-yaw;
    if(c->invert&PILOT_INVERT_ROLL) roll=-roll;
    if(c->invert&PILOT_INVERT_STRAFE_X) x=-x;
    if(c->invert&PILOT_INVERT_STRAFE_Y) y=-y;
    float z=override?throttle_override:m->throttle;
    memset(r,0,sizeof(*r));
    r->mode=(unsigned char)(PILOT_MODE_ASSIST|(boost?PILOT_MODE_BOOST:0));
    r->q[0]=pilot_quantize(pilot_device_clamp(x));r->q[1]=pilot_quantize(pilot_device_clamp(y));r->q[2]=pilot_quantize(z);
    r->q[3]=pilot_quantize(pilot_device_clamp(pitch));r->q[4]=pilot_quantize(pilot_device_clamp(yaw));
    r->q[5]=pilot_quantize(pilot_device_clamp(roll));r->q[6]=r->q[2];
    r->buttons=(unsigned char)(fire?1u:0u); /* trigger bit 0: primary; carried, not applied */
}
/* One snapshot of the configured devices through an API table (real or fake). */
static void pilot_device_read(const pilot_device_api *api,const pilot_device_config *c,pilot_device_sample *s) {
    memset(s,0,sizeof(*s));
    if((c->kinds&PILOT_DEVICE_KBM) && api->foreground && api->window_thread && api->self) {
        HWND window=api->foreground();DWORD owner=0;
        if(window) api->window_thread(window,&owner);
        s->focused=(unsigned)pilot_device_focus(window,owner,api->self());
        if(s->focused && api->key) {
            for(unsigned i=0;i<PILOT_KEY_COUNT;i++) if(api->key(pilot_key_vk[i])&0x8000) s->keys|=PILOT_KEY(i);
            POINT point;RECT rect;
            if(api->cursor && api->to_client && api->client_rect && api->cursor(&point) && api->to_client(window,&point) &&
               api->client_rect(window,&rect)) {
                s->cursor=1;s->cursor_x=point.x;s->cursor_y=point.y;s->width=rect.right-rect.left;s->height=rect.bottom-rect.top;
            }
        }
    }
    if((c->kinds&PILOT_DEVICE_PAD) && api->pad_state) {
        pilot_xinput_state state;memset(&state,0,sizeof(state));
        if(api->pad_state(c->pad,&state)==ERROR_SUCCESS) {
            s->pad=1;s->lx=state.pad.lx;s->ly=state.pad.ly;s->rx=state.pad.rx;s->ry=state.pad.ry;
            s->lt=state.pad.lt;s->rt=state.pad.rt;s->buttons=state.pad.buttons;
        }
    }
}

static pilot_device_config pilot_device_cfg;
static pilot_device_memory pilot_device_mem;
static pilot_device_api pilot_device_real;
static const pilot_device_api *pilot_device_api_active;
static pilot_device_sample pilot_device_last;
static pilot_record pilot_device_record;
static uint64_t pilot_device_tick,pilot_device_samples,pilot_device_unfocused;
static int pilot_device_armed,pilot_device_xinput;
/* Resolve the real entry points once (UI thread, before the window). */
static int pilot_device_resolve(pilot_device_api *api,int *xinput) {
    HMODULE user=GetModuleHandleW(L"user32.dll");
    if(!user) user=LoadLibraryW(L"user32.dll");
    memset(api,0,sizeof(*api));*xinput=0;
    if(!user) return 0;
#define PILOT_RESOLVE(field,name) api->field=(void *)GetProcAddress(user,name);if(!api->field) return 0
    PILOT_RESOLVE(foreground,"GetForegroundWindow");PILOT_RESOLVE(window_thread,"GetWindowThreadProcessId");
    PILOT_RESOLVE(key,"GetAsyncKeyState");PILOT_RESOLVE(cursor,"GetCursorPos");
    PILOT_RESOLVE(to_client,"ScreenToClient");PILOT_RESOLVE(client_rect,"GetClientRect");
#undef PILOT_RESOLVE
    api->self=GetCurrentProcessId;
    HMODULE pad=LoadLibraryW(L"xinput1_4.dll");
    if(!pad) pad=LoadLibraryW(L"xinput9_1_0.dll");
    if(pad) api->pad_state=(pilot_xinput_fn)(void *)GetProcAddress(pad,"XInputGetState");
    *xinput=api->pad_state!=NULL;return 1;
}
static int pilot_device_valid(unsigned kinds,unsigned pad,unsigned invert) {
    return kinds>=1 && kinds<=PILOT_DEVICE_KINDS && pad<=3 && !(invert&~PILOT_INVERT_KNOWN);
}
/* Arming with an explicit API table (tests) or the resolved real one. An
 * identical rerun after a loading-UI reload succeeds; a different one fails. */
static int pilot_device_arm_using(unsigned kinds,unsigned pad,unsigned invert,const pilot_device_api *api,int xinput) {
    if(!pilot_device_valid(kinds,pad,invert) || !api) return 0;
    if(pilot_device_armed) return pilot_device_cfg.kinds==kinds && pilot_device_cfg.pad==pad && pilot_device_cfg.invert==invert;
    pilot_device_cfg.kinds=kinds;pilot_device_cfg.pad=pad;pilot_device_cfg.invert=invert;
    memset(&pilot_device_mem,0,sizeof(pilot_device_mem));memset(&pilot_device_last,0,sizeof(pilot_device_last));
    pilot_device_api_active=api;pilot_device_xinput=xinput;pilot_device_tick=0;pilot_device_samples=pilot_device_unfocused=0;
    pilot_device_armed=1;return 1;
}
static int pilot_device_arm(unsigned kinds,unsigned pad,unsigned invert) {
    if(pilot_device_armed) return pilot_device_arm_using(kinds,pad,invert,pilot_device_api_active,pilot_device_xinput);
    int xinput=0;
    if(!pilot_device_valid(kinds,pad,invert) || !pilot_device_resolve(&pilot_device_real,&xinput)) return 0;
    return pilot_device_arm_using(kinds,pad,invert,&pilot_device_real,xinput);
}
/* Exactly one sample and one mapping per capture tick; a repeated call for the
 * same tick returns the same record (the throttle ramps once per tick). */
static int pilot_device_capture(uint64_t tick,pilot_record *r) {
    if(!pilot_device_armed || !pilot_device_api_active || !tick) return 0;
    if(tick!=pilot_device_tick) {
        pilot_device_read(pilot_device_api_active,&pilot_device_cfg,&pilot_device_last);
        pilot_device_map(&pilot_device_last,&pilot_device_cfg,&pilot_device_mem,&pilot_device_record);
        pilot_device_tick=tick;++pilot_device_samples;
        if((pilot_device_cfg.kinds&PILOT_DEVICE_KBM) && !pilot_device_last.focused) ++pilot_device_unfocused;
    }
    *r=pilot_device_record;return 1;
}
static int pilot_device_status(char *out,size_t capacity) {
    static const char *kinds[]={"native","kbm","xinput","both"};
    int n=snprintf(out,capacity," device=%s pad=%u invert=%u xinput=%d focus=%u pad_connected=%u throttle=%.3f mouse_toggle=%u samples=%llu unfocused=%llu",
        kinds[pilot_device_cfg.kinds&3u],pilot_device_cfg.pad,pilot_device_cfg.invert,pilot_device_xinput,pilot_device_last.focused,
        pilot_device_last.pad,(double)pilot_device_mem.throttle,pilot_device_mem.mouse_toggle,
        (unsigned long long)pilot_device_samples,(unsigned long long)pilot_device_unfocused);
    return n>0 && (size_t)n<capacity?n:0;
}
