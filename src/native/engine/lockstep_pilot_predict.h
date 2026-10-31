/* Default-off local presentation prediction for the piloting prototype
 * (native_gate_pilot_predict; reports/LOCKSTEP-PREDICTION-20261029.txt).
 * Presentation only: this file never writes engine memory, calls engine code,
 * draws RNG or adds to the declared state. Only the predicting client runs it,
 * so the lockstep simulation is unaffected by construction.
 *
 * Model. A slot ship's simulated pose S(T) is read from its pathing read bank
 * after tick T's join (controller+0x30 slots, index 0x38f4ee4, 0x1a0 stride:
 * +0x00 position, +0x10/+0x20/+0x30 the local x/y/z axes in parent (zone)
 * coordinates, as the portable state encodes it). Body rates v(T) are the
 * finite difference S(T-1) -> S(T) in S(T-1)'s local frame: linear m/s along
 * local x/y/z, angular rad/s about local x/y/z. The h records that apply at
 * T+1..T+h (wire: this client's gate.pilot_pending ring, h = wire delay;
 * script: pilot_script, configured h) are mapped onto the ship's own Steering
 * limits exactly as the substitution maps them (pilot_target) and integrated
 * h steps with a per-axis response model
 *     dv = clamp(gain * (sign * target - v), -down, +up)
 * to P(T), the estimate of S(T+h): the ship as the simulation will have it
 * once those records apply, i.e. the local instance "a little ahead". gain,
 * up, down and sign are fitted online from the ship's own driven ticks
 * (rise/settle limit estimators and an exponential-mean gain behind
 * plausibility gates); an axis with too few samples keeps constant velocity. Every tick restarts from the latest
 * simulated state, so model error cannot accumulate. The displayed pose is
 * P(T) plus a correction that absorbs the jump between the continuation of the
 * previous display and P(T) and decays with time constant tau (snap beyond
 * PREDICT_SNAP_*).
 * Observe mode stores, for target tick T+h, P, two baselines (constant
 * velocity from S(T); no prediction = S(T) itself) and the smoothed display
 * pose, and compares each with S(T+h) when that tick is simulated. A target
 * tick whose zone changed is skipped (counted). Observe mode also checks,
 * read only, the precondition of the present mode: the render walk's pose
 * block of the ship (view cache [A] at object+0xe0+A*0x80, A = *0x38f4ee0 on
 * the MoveWorker; vc+0 cached parent, vc+0x28 pose block, PosRot with row 0
 * = translation) must hold S(T), its parent must be the ship's zone and the
 * two indices must not share one block.
 * Present mode (needs the render RNG split's slot-10 wrapper): in slot 10
 * only, and only for this client's slot ship, the pose block [A] holds the
 * displayed pose instead of S(T) while the original slot 10 runs; it is put
 * back before the wrapper returns, also when an exception unwinds, unless the
 * engine replaced the block meanwhile (then its value stands). The render
 * walk composes every object's model-view relative to the camera's cockpit
 * through this block (0x3de630), so the world is drawn as seen from the
 * predicted ship and camera; the ship's own parts keep their relative pose.
 * Simulation readers of [A] (0x3dde10 callers) run outside slot 10; other
 * threads read the other index during slot 10. Not covered (report): frustum
 * culling, LOD and world-space effects use the camera table, which stays
 * simulated. Any restore failure stops the gate with reason 101. */
#define PREDICT_RING 64u
#define PREDICT_HORIZON_MAX 30u
#define PREDICT_AXES 6u
#define PREDICT_DT gate_tick_dt()
#define PREDICT_MIN_SAMPLES 20u
#define PREDICT_OVERSHOOT 1.25
#define PREDICT_LINEAR_NOISE 0.5   /* m/s: float32 zone positions make finite differences this noisy far out */
#define PREDICT_ANGULAR_NOISE 1e-3 /* rad/s */
#define PREDICT_SNAP_POSITION 50.0 /* m */
#define PREDICT_SNAP_ANGLE 0.5     /* rad */
#define PREDICT_TAU_MAX_MS 2000u
#define PREDICT_BUCKETS 8u
#define PREDICT_STOP_REASON 101u /* 90-98 and 100 are taken */
#define PREDICT_VIEW_INDEX 0x38f4ee0u  /* view-cache index on the MoveWorker (0x3de630, 0x979ef0) */
#define PREDICT_VIEW_CACHE 0xe0u
#define PREDICT_VIEW_STRIDE 0x80u
#define PREDICT_VIEW_BLOCK 0x28u
#define PREDICT_VIEW_POSITION 0.01      /* m: a pose block that differs more is not the simulated pose */
#define PREDICT_VIEW_ROTATION 1e-4
#define PREDICT_PRESENT_POSITION 200.0  /* m: larger offsets (travel drive) are shown simulated */
#define PREDICT_PRESENT_ANGLE 0.5       /* rad */
#define PREDICT_UNIVERSE 0x3d198f8u      /* g_Universe; +0x3d0 the player camera */
#define PREDICT_PLAYER_CAMERA 0x3d0u
#define PREDICT_CAMERA_ATTACHED 0x770u   /* the component the camera is attached to */
#define PREDICT_CAMERA_BLOCKS 0x8e8u     /* view-block cache in use (camera+0x8e0 +8), cleared by 0x97a160 */
enum {PREDICT_OFF,PREDICT_OBSERVE,PREDICT_PRESENT};
enum {PREDICT_MODEL,PREDICT_DEAD,PREDICT_NONE,PREDICT_DISPLAY,PREDICT_KINDS};
typedef struct {double p[3],r[9];} predict_pose; /* r rows: local x, y, z axes in parent coordinates */
typedef struct {
    double gain[PREDICT_AXES],up[PREDICT_AXES],down[PREDICT_AXES],corr[PREDICT_AXES];
    int sign[PREDICT_AXES];
    uint64_t samples[PREDICT_AXES],refused[PREDICT_AXES],flips;
} predict_model;
typedef struct {double p[3],r[3];} predict_offset; /* parent-frame translation, local rotation vector */
typedef struct {uint64_t n;double sum,sq,max;uint64_t bucket[PREDICT_BUCKETS];} predict_stat;
typedef struct {
    uint64_t tick,frame;unsigned valid,rates_valid,driven;
    predict_pose sim;double rates[PREDICT_AXES];float native[8]; /* the bank's own velocity fields +0x40..+0x5f, frame unknown */
    unsigned predicted;uint64_t made,pframe;predict_pose made_pose[PREDICT_KINDS];
} predict_entry;
typedef struct {
    unsigned enabled,have_end,applied_known;
    predict_model model;
    predict_entry ring[PREDICT_RING];
    predict_offset correction;predict_pose end,display;double end_rates[PREDICT_AXES];uint64_t end_tick,display_tick;
    LONG64 applied_prev[2];
    predict_stat pos[PREDICT_KINDS],ang[PREDICT_KINDS];
    uint64_t samples,bad_reads,frame_changes,resets,snaps,undriven,predictions,missing_records,checked,skipped;
    double last_pos,last_ang;
    /* Present-mode precondition (read only, every tick) and this tick's handover to slot 10. */
    uint64_t view_ok,view_mismatch,view_shared,view_missing,view_parent,present_bounds;double view_deviation;
    unsigned present_ready;uint64_t present_tick;float present_sim[12],present_shown[12];
} predict_slot;
static const double predict_pos_edges[PREDICT_BUCKETS-1]={0.01,0.03,0.1,0.3,1.0,3.0,10.0};
static const double predict_ang_edges[PREDICT_BUCKETS-1]={1e-4,3e-4,1e-3,3e-3,1e-2,3e-2,1e-1};
static predict_slot predict_slots[PILOT_SLOTS];
static int predict_mode,predict_armed;
static unsigned predict_horizon,predict_tau_ms;
static uint64_t predict_frames,predict_last_tick,predict_raw_rows;
static volatile LONG64 predict_failures;
static const char *predict_stage="off";
/* Present-mode pins, checked once at arm: the render walk's cached relative
 * transform (MoveWorker view-cache index 0x38f4ee0, object+0xe0+index*0x80,
 * pose block at +0x28), the camera-table reader (same index) and the
 * MoveWorker publish that swaps 0x38f4ee0/0x2f4b71c after slot 3. The
 * slot-10 entry itself is pinned by the render RNG split. */
static const struct {unsigned rva,size;const char *sha;} predict_pin_list[3]={
    {0x3de630u,0x7e5u,"c76f237791494750478ffd518026d62295ce3177c1d958b85782b8cd9b08220f"},
    {0x979ef0u,0x264u,"81de8348fb8fc02d3c34c7a8a35b225a0c29b0a1b2ac668597f57da80ecd88ca"},
    {0xef41e0u,0x117u,"7447a23fa34bdce6a2c01762d8ad55f4642fcd8445e8cb4c1976899dd4beb70b"}};
static int predict_pins_checked; /* 0 unchecked, 1 verified, -1 refused */
static int predict_pins(void) {
    if(!predict_pins_checked) {
        predict_pins_checked=1;
        for(unsigned i=0;i<sizeof(predict_pin_list)/sizeof(*predict_pin_list);i++)
            if(!hash_ok(image+predict_pin_list[i].rva,predict_pin_list[i].size,predict_pin_list[i].sha)) predict_pins_checked=-1;
    }
    return predict_pins_checked==1;
}
/* Open slot-10 substitution (MoveWorker only). */
static unsigned char *predict_present_block;
static float predict_present_saved[16],predict_present_written[16];
static int predict_present_open;
/* Read-only camera diagnostics in slot 6 (stage 2 and view questions). */
static uint64_t predict_camera_samples,predict_camera_cached,predict_camera_on_ship,predict_camera_unreadable;
static uint64_t predict_present_calls,predict_present_applied,predict_present_restored,predict_present_foreign,
    predict_present_stale,predict_present_refused;
static volatile LONG64 predict_present_failures;

/* ---- rotation helpers (row convention: a parent vector = sum of local components times the rows) ---- */
static double predict_dot3(const double *a,const double *b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static void predict_mul(const double a[9],const double b[9],double out[9]) {
    double t[9];
    for(unsigned i=0;i<3;i++) for(unsigned j=0;j<3;j++) t[3*i+j]=a[3*i]*b[j]+a[3*i+1]*b[3+j]+a[3*i+2]*b[6+j];
    memcpy(out,t,sizeof(t));
}
static void predict_mul_transposed(const double a[9],const double b[9],double out[9]) { /* a * b^T */
    double t[9];
    for(unsigned i=0;i<3;i++) for(unsigned j=0;j<3;j++) t[3*i+j]=predict_dot3(a+3*i,b+3*j);
    memcpy(out,t,sizeof(t));
}
/* Rotation vector of E (the rotation that takes R0 to R1 = E * R0, in R0's local frame). */
static void predict_log(const double e[9],double w[3]) {
    double s[3]={(e[5]-e[7])*0.5,(e[6]-e[2])*0.5,(e[1]-e[3])*0.5},c=(e[0]+e[4]+e[8]-1.0)*0.5;
    double n=sqrt(predict_dot3(s,s));
    if(c>1.0) c=1.0;
    if(c<-1.0) c=-1.0;
    double theta=atan2(n,c),k=n>1e-12?theta/n:1.0;
    for(unsigned i=0;i<3;i++) w[i]=s[i]*k;
}
static void predict_exp(const double w[3],double e[9]) {
    double theta=sqrt(predict_dot3(w,w));
    if(theta<1e-12) {
        static const double identity[9]={1,0,0,0,1,0,0,0,1};
        memcpy(e,identity,sizeof(identity));
        e[1]=w[2];e[2]=-w[1];e[3]=-w[2];e[5]=w[0];e[6]=w[1];e[7]=-w[0];return;
    }
    double k[3]={w[0]/theta,w[1]/theta,w[2]/theta},c=cos(theta),s=sin(theta),v=1.0-c;
    /* Row i: the local basis vector e_i rotated by theta about k (Rodrigues). */
    e[0]=c+k[0]*k[0]*v;      e[1]=k[2]*s+k[0]*k[1]*v;  e[2]=-k[1]*s+k[0]*k[2]*v;
    e[3]=-k[2]*s+k[1]*k[0]*v; e[4]=c+k[1]*k[1]*v;      e[5]=k[0]*s+k[1]*k[2]*v;
    e[6]=k[1]*s+k[2]*k[0]*v;  e[7]=-k[0]*s+k[2]*k[1]*v; e[8]=c+k[2]*k[2]*v;
}
static void predict_cross(const double *a,const double *b,double *out) {
    double t[3]={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};memcpy(out,t,sizeof(t));
}
/* Gram-Schmidt from z (forward) and y (up); x keeps the basis' handedness. */
static void predict_orthonormalize(double r[9]) {
    double *x=r,*y=r+3,*z=r+6,c[3];
    predict_cross(y,z,c);
    double handed=predict_dot3(x,c)<0?-1.0:1.0,n=sqrt(predict_dot3(z,z));
    for(unsigned i=0;i<3;i++) z[i]/=n;
    double d=predict_dot3(y,z);
    for(unsigned i=0;i<3;i++) y[i]-=d*z[i];
    n=sqrt(predict_dot3(y,y));
    for(unsigned i=0;i<3;i++) y[i]/=n;
    predict_cross(y,z,c);
    for(unsigned i=0;i<3;i++) x[i]=handed*c[i];
}
static double predict_angle(const double a[9],const double b[9]) {
    double e[9],w[3];predict_mul_transposed(a,b,e);predict_log(e,w);return sqrt(predict_dot3(w,w));
}
static double predict_distance(const predict_pose *a,const predict_pose *b) {
    double d[3]={a->p[0]-b->p[0],a->p[1]-b->p[1],a->p[2]-b->p[2]};return sqrt(predict_dot3(d,d));
}
/* A bank's first four float vectors: position, then the three axes. */
static int predict_pose_from_bank(const float bank[16],predict_pose *out) {
    for(unsigned i=0;i<16;i++) if((i&3)!=3 && !isfinite(bank[i])) return 0;
    for(unsigned i=0;i<3;i++) {out->p[i]=bank[i];for(unsigned j=0;j<3;j++) out->r[3*i+j]=bank[4*(i+1)+j];}
    for(unsigned i=0;i<3;i++) {
        double n=predict_dot3(out->r+3*i,out->r+3*i);
        if(fabs(n-1.0)>2e-3) return 0;
        for(unsigned j=i+1;j<3;j++) if(fabs(predict_dot3(out->r+3*i,out->r+3*j))>2e-3) return 0;
    }
    return 1;
}
static void predict_rates(const predict_pose *from,const predict_pose *to,double dt,double v[PREDICT_AXES]) {
    double d[3]={to->p[0]-from->p[0],to->p[1]-from->p[1],to->p[2]-from->p[2]},e[9];
    for(unsigned i=0;i<3;i++) v[i]=predict_dot3(d,from->r+3*i)/dt;
    predict_mul_transposed(to->r,from->r,e);predict_log(e,v+3);
    for(unsigned i=3;i<6;i++) v[i]/=dt;
}
static void predict_advance(predict_pose *pose,const double v[PREDICT_AXES],double dt) {
    double w[3]={v[3]*dt,v[4]*dt,v[5]*dt},e[9];
    for(unsigned i=0;i<3;i++) pose->p[i]+=(v[0]*pose->r[i]+v[1]*pose->r[3+i]+v[2]*pose->r[6+i])*dt;
    predict_exp(w,e);predict_mul(e,pose->r,pose->r);predict_orthonormalize(pose->r);
}

/* ---- response model ---- */
static void predict_model_reset(predict_model *m) {
    memset(m,0,sizeof(*m));
    for(unsigned i=0;i<PREDICT_AXES;i++) m->sign[i]=1;
}
static void predict_axis_reset(predict_model *m,unsigned i) {m->gain[i]=m->up[i]=m->down[i]=0;m->samples[i]=0;}
/* Record -> the six raw Steering targets, in model axis order: linear x y z,
 * then angular solver axes 0 1 2 (pitch, yaw, roll). */
static void predict_targets(const pilot_record *r,const float limits[8],double out[PREDICT_AXES]) {
    for(unsigned i=0;i<3;i++) {out[i]=(double)pilot_target(r,limits,1,i,NULL);out[3+i]=(double)pilot_target(r,limits,0,i,NULL);}
}
static double predict_noise(unsigned axis) {return axis<3?PREDICT_LINEAR_NOISE:PREDICT_ANGULAR_NOISE;}
/* One driven tick: v_prev -> v_now under target raw (the record applied in
 * that tick). A sample whose change reaches the current limit estimate
 * (PREDICT_SATURATED of it) is an acceleration sample: the limit rises fast
 * towards larger changes and settles slowly towards smaller ones. Any other
 * sample is a gain sample (exponential mean, unbiased under position noise).
 * Samples with a small error, moving away from the target or overshooting it
 * (impulses, other flight models) are not fitted. */
#define PREDICT_SATURATED 0.9
#define PREDICT_RISE 0.5
#define PREDICT_SETTLE 0.05
#define PREDICT_GAIN_ALPHA 0.05
static void predict_calibrate(predict_model *m,const double v_prev[PREDICT_AXES],const double v_now[PREDICT_AXES],const double raw[PREDICT_AXES]) {
    for(unsigned i=0;i<PREDICT_AXES;i++) {
        double dv=v_now[i]-v_prev[i],noise=predict_noise(i);
        if(!isfinite(dv) || !isfinite(raw[i]) || !isfinite(v_prev[i])) {++m->refused[i];continue;}
        if(fabs(raw[i])>noise && fabs(dv)>noise) {
            m->corr[i]+=raw[i]*dv;
            int sign=m->corr[i]<0?-1:1;
            if(sign!=m->sign[i] && fabs(m->corr[i])>noise*noise*PREDICT_MIN_SAMPLES) {m->sign[i]=sign;predict_axis_reset(m,i);++m->flips;}
        }
        double e=m->sign[i]*raw[i]-v_prev[i];
        if(fabs(e)<4.0*noise) continue;                              /* no information */
        double ratio=dv/e;
        if(ratio<=0 || ratio>PREDICT_OVERSHOOT) {++m->refused[i];continue;} /* external force or another controller */
        double *limit=dv>0?&m->up[i]:&m->down[i],change=fabs(dv);
        if(change>=PREDICT_SATURATED**limit) *limit=*limit==0?change:*limit+(change>*limit?PREDICT_RISE:PREDICT_SETTLE)*(change-*limit);
        else m->gain[i]=m->gain[i]==0?ratio:m->gain[i]+PREDICT_GAIN_ALPHA*(ratio-m->gain[i]);
        ++m->samples[i];
    }
}
static int predict_axis_ready(const predict_model *m,unsigned i) {
    return m->samples[i]>=PREDICT_MIN_SAMPLES && (m->up[i]>0 || m->down[i]>0);
}
/* Without a gain sample the axis is limit-only (reaches the target when within
 * one step); an unseen direction borrows the other direction's limit. */
static void predict_step(const predict_model *m,double v[PREDICT_AXES],const double raw[PREDICT_AXES]) {
    for(unsigned i=0;i<PREDICT_AXES;i++) {
        if(!predict_axis_ready(m,i)) continue; /* constant velocity until fitted */
        double gain=m->gain[i]>0?m->gain[i]:1.0,up=m->up[i]>0?m->up[i]:m->down[i],down=m->down[i]>0?m->down[i]:m->up[i];
        double dv=gain*(m->sign[i]*raw[i]-v[i]);
        if(dv>up) dv=up;
        if(dv<-down) dv=-down;
        v[i]+=dv;
    }
}
/* h steps from (pose, v); raw[k] is the target of the k-th step. */
static void predict_run(const predict_model *m,const predict_pose *base,const double rates[PREDICT_AXES],
                        const double (*raw)[PREDICT_AXES],unsigned steps,predict_pose *out,double out_rates[PREDICT_AXES]) {
    double v[PREDICT_AXES];*out=*base;memcpy(v,rates,sizeof(v));
    for(unsigned k=0;k<steps;k++) {predict_step(m,v,raw[k]);predict_advance(out,v,PREDICT_DT);}
    if(out_rates) memcpy(out_rates,v,sizeof(v));
}

/* ---- display smoothing ---- */
static void predict_offset_apply(const predict_pose *pose,const predict_offset *c,predict_pose *out) {
    double e[9];*out=*pose;
    for(unsigned i=0;i<3;i++) out->p[i]+=c->p[i];
    predict_exp(c->r,e);predict_mul(e,out->r,out->r);
}
/* New prediction 'next' versus the continuation of the previous display path:
 * correction = lambda * (correction + jump); a jump beyond the snap bounds
 * (teleport, zone change, mispredicted impulse) clears it instead. */
static int predict_smooth(predict_offset *c,const predict_pose *continuation,const predict_pose *next,double lambda) {
    double e[9],w[3],d[3];
    for(unsigned i=0;i<3;i++) d[i]=continuation->p[i]-next->p[i];
    predict_mul_transposed(continuation->r,next->r,e);predict_log(e,w);
    for(unsigned i=0;i<3;i++) {c->p[i]=lambda*(c->p[i]+d[i]);c->r[i]=lambda*(c->r[i]+w[i]);}
    if(sqrt(predict_dot3(c->p,c->p))>PREDICT_SNAP_POSITION || sqrt(predict_dot3(c->r,c->r))>PREDICT_SNAP_ANGLE) {
        memset(c,0,sizeof(*c));return 1;
    }
    return 0;
}
static void predict_stat_add(predict_stat *s,double value,const double edges[PREDICT_BUCKETS-1]) {
    unsigned b=0;while(b<PREDICT_BUCKETS-1 && value>=edges[b]) ++b;
    ++s->n;s->sum+=value;s->sq+=value*value;if(value>s->max) s->max=value;++s->bucket[b];
}

/* ---- records ---- */
/* gate.pilot_pending text: "frame_tick=N capture:apply:hex ..." -> records for
 * apply ticks first..first+count-1; present[k] marks the ones found. */
static int predict_pending_parse(const char *text,uint64_t first,unsigned count,pilot_record *out,unsigned char *present) {
    unsigned long long frame=0,capture,apply;int used=0;
    memset(present,0,count);
    if(!text || sscanf(text,"frame_tick=%llu%n",&frame,&used)!=1) return 0;
    for(const char *p=text+used;*p;) {
        char hex[2*PILOT_RECORD_BYTES+1];unsigned char bytes[PILOT_RECORD_BYTES];pilot_record r;int n=0;
        if(*p!=' ' || sscanf(p," %llu:%llu:%56[0-9a-f]%n",&capture,&apply,hex,&n)!=3 || n<=0) return 0;
        p+=n;
        if(strlen(hex)!=2*PILOT_RECORD_BYTES || !pilot_unhex(hex,strlen(hex),bytes) || !pilot_decode(bytes,&r) ||
           r.capture16!=(uint16_t)capture || apply<=capture) return 0;
        if(apply>=first && apply<first+count) {out[apply-first]=r;present[apply-first]=1;}
    }
    return 1;
}
/* Targets of the next h steps for one slot; a missing record holds the previous target. */
static unsigned predict_future(unsigned slot,uint64_t tick,unsigned h,const float limits[8],const pilot_record *applied,
                               double (*raw)[PREDICT_AXES]) {
    pilot_record records[PREDICT_HORIZON_MAX];unsigned char present[PREDICT_HORIZON_MAX];unsigned missing=0;
    memset(records,0,sizeof(records));memset(present,0,sizeof(present));
    if(pilot_source==PILOT_SOURCE_SCRIPT) {
        for(unsigned k=0;k<h;k++) {
            uint64_t t=tick+1+k;
            if(t>=pilot_start && t<=pilot_stop) pilot_script(slot,t,pilot_start,&records[k]);
            else {memset(&records[k],0,sizeof(records[k]));records[k].mode=PILOT_MODE_ASSIST;}
            present[k]=1;
        }
    } else {
        char text[2*PILOT_LOCAL_RING*(2*PILOT_RECORD_BYTES+48)];
        if(!pilot_pending(text,sizeof(text)) || !predict_pending_parse(text,tick+1,h,records,present)) memset(present,0,sizeof(present));
    }
    pilot_record last=applied?*applied:records[0];
    for(unsigned k=0;k<h;k++) {
        if(!present[k]) {records[k]=last;++missing;}
        last=records[k];predict_targets(&records[k],limits,raw[k]);
    }
    return missing;
}

/* ---- native sampling (MoveWorker, slot 6; guarded reads only) ---- */
static int predict_sample(const pilot_slot *s,predict_pose *pose,uint64_t *frame,float limits[8],float native[8],float sim[12]) {
    unsigned char *controller=NULL,*slots=NULL,*parent=NULL;unsigned index=2;float bank[24];
    if(!s->resolved || !s->object || !s->agent) return 0;
    if(!read_local(s->object+0x70,&parent,8) || !read_local(s->object+0x200,&controller,8) || !controller ||
       !read_local(controller+0x30,&slots,8) || !slots || !read_local(image+0x38f4ee4,&index,4) || index>1 ||
       !read_local(slots+index*0x1a0,bank,sizeof(bank)) || !predict_pose_from_bank(bank,pose) ||
       !read_local(s->agent+PILOT_LIMITS,limits,8*sizeof(float)) || !pilot_limits_valid(limits)) return 0;
    memcpy(native,bank+16,8*sizeof(float));*frame=(uint64_t)(uintptr_t)parent;
    for(unsigned i=0;i<4;i++) memcpy(sim+3*i,bank+4*i,3*sizeof(float));
    return 1;
}
static int predict_write(void *destination,const void *source,size_t bytes) {
    SIZE_T done=0;return destination && WriteProcessMemory(GetCurrentProcess(),destination,source,bytes,&done) && done==bytes;
}
/* The render walk's view cache [A] of a component (MoveWorker index). */
static int predict_view_block(unsigned char *object,unsigned char **block,unsigned char **other,uint64_t *parent) {
    unsigned index=2;unsigned char *cached=NULL;
    if(!object || !read_local(image+PREDICT_VIEW_INDEX,&index,4) || index>1) return 0;
    unsigned char *vc=object+PREDICT_VIEW_CACHE+index*PREDICT_VIEW_STRIDE,*vo=object+PREDICT_VIEW_CACHE+(1-index)*PREDICT_VIEW_STRIDE;
    if(!read_local(vc,&cached,8) || !read_local(vc+PREDICT_VIEW_BLOCK,block,8) || !read_local(vo+PREDICT_VIEW_BLOCK,other,8)) return 0;
    *parent=(uint64_t)(uintptr_t)cached;return 1;
}
/* Largest difference between a pose block's xyz and the given floats:
 * [0] position, [1] rotation rows. */
static void predict_view_deviation(const float block[16],const float xyz[12],double deviation[2]) {
    deviation[0]=deviation[1]=0;
    for(unsigned i=0;i<4;i++) for(unsigned j=0;j<3;j++) {
        double d=fabs((double)block[4*i+j]-(double)xyz[3*i+j]);
        if(!(d<=1e30)) d=1e30; /* NaN */
        if(d>deviation[i?1:0]) deviation[i?1:0]=d;
    }
}
static int predict_view_check(predict_slot *p,const pilot_slot *s,const float sim[12],uint64_t frame) {
    unsigned char *block=NULL,*other=NULL;uint64_t parent=0;float b[16];double deviation[2];
    if(!predict_view_block(s->object,&block,&other,&parent) || !block || !read_local(block,b,sizeof(b))) {++p->view_missing;return 0;}
    if(parent!=frame) {++p->view_parent;return 0;}
    if(block==other) {++p->view_shared;return 0;}
    predict_view_deviation(b,sim,deviation);
    if(deviation[0]>p->view_deviation) p->view_deviation=deviation[0];
    if(deviation[0]>PREDICT_VIEW_POSITION || deviation[1]>PREDICT_VIEW_ROTATION) {++p->view_mismatch;return 0;}
    ++p->view_ok;return 1;
}
static void predict_floats(const predict_pose *pose,float out[12]) {
    for(unsigned i=0;i<3;i++) {out[i]=(float)pose->p[i];for(unsigned j=0;j<3;j++) out[3*(i+1)+j]=(float)pose->r[3*i+j];}
}
static void predict_slot_reset(predict_slot *p) {
    memset(p->ring,0,sizeof(p->ring));memset(&p->correction,0,sizeof(p->correction));p->have_end=0;++p->resets;
}
static void predict_check(predict_slot *p,const predict_entry *now) {
    if(!now->predicted || now->made+predict_horizon!=now->tick) return;
    if(now->pframe!=now->frame) {++p->skipped;return;}
    for(unsigned k=0;k<PREDICT_KINDS;k++) {
        double dp=predict_distance(&now->made_pose[k],&now->sim),da=predict_angle(now->made_pose[k].r,now->sim.r);
        predict_stat_add(&p->pos[k],dp,predict_pos_edges);predict_stat_add(&p->ang[k],da,predict_ang_edges);
        if(k==PREDICT_MODEL) {p->last_pos=dp;p->last_ang=da;}
    }
    ++p->checked;
}
/* One admitted tick for one slot: sample, fit, check the prediction made h
 * ticks ago, predict h ticks ahead and update the display path. */
static void predict_slot_frame(unsigned index,uint64_t tick) {
    predict_slot *p=&predict_slots[index];pilot_slot *s=&pilot_slots[index];
    predict_entry *now=&p->ring[tick%PREDICT_RING],*before=&p->ring[(tick-1)%PREDICT_RING];
    predict_pose pose;uint64_t frame=0;float limits[8],native[8],sim[12];
    unsigned keep=now->tick==tick && now->predicted;predict_entry saved=*now;
    memset(now,0,sizeof(*now));now->tick=tick;
    if(keep) {now->predicted=1;now->made=saved.made;now->pframe=saved.pframe;memcpy(now->made_pose,saved.made_pose,sizeof(now->made_pose));}
    LONG64 applied[2]={InterlockedCompareExchange64(&s->applied[0],0,0),InterlockedCompareExchange64(&s->applied[1],0,0)};
    now->driven=p->applied_known && s->driving && applied[0]-p->applied_prev[0]==3 && applied[1]-p->applied_prev[1]==3;
    p->applied_prev[0]=applied[0];p->applied_prev[1]=applied[1];p->applied_known=1;
    p->present_ready=0;
    if(!predict_sample(s,&pose,&frame,limits,native,sim)) {++p->bad_reads;predict_slot_reset(p);return;}
    int view=predict_view_check(p,s,sim,frame);
    now->valid=1;now->sim=pose;now->frame=frame;memcpy(now->native,native,sizeof(native));++p->samples;
    if(before->valid && before->tick==tick-1) {
        if(before->frame!=frame) {++p->frame_changes;predict_slot_reset(p);*now=(predict_entry){0};now->tick=tick;now->valid=1;now->sim=pose;now->frame=frame;return;}
        predict_rates(&before->sim,&pose,PREDICT_DT,now->rates);now->rates_valid=1;
        if(before->rates_valid && now->driven) {
            double raw[PREDICT_AXES];predict_targets(&s->record,limits,raw);
            predict_calibrate(&p->model,before->rates,now->rates,raw);
        } else if(!now->driven) ++p->undriven;
    }
    predict_check(p,now);
    if(!now->rates_valid) return;
    /* Predict S(T+h). */
    double raw[PREDICT_HORIZON_MAX][PREDICT_AXES],end_rates[PREDICT_AXES],constant[PREDICT_HORIZON_MAX][PREDICT_AXES];
    unsigned h=predict_horizon;predict_pose model,dead,display;
    p->missing_records+=predict_future(index+1,tick,h,limits,s->driving?&s->record:NULL,raw);
    predict_run(&p->model,&pose,now->rates,(const double (*)[PREDICT_AXES])raw,h,&model,end_rates);
    predict_model frozen;predict_model_reset(&frozen);memset(constant,0,sizeof(constant));
    predict_run(&frozen,&pose,now->rates,(const double (*)[PREDICT_AXES])constant,h,&dead,NULL);
    /* Display: continue the previous path one step with the newest target. */
    if(p->have_end && p->end_tick+1==tick+h) {
        predict_pose continuation=p->end;double v[PREDICT_AXES];memcpy(v,p->end_rates,sizeof(v));
        predict_step(&p->model,v,raw[h-1]);predict_advance(&continuation,v,PREDICT_DT);
        double lambda=predict_tau_ms?exp(-1000.0*PREDICT_DT/(double)predict_tau_ms):0.0;
        if(predict_smooth(&p->correction,&continuation,&model,lambda)) ++p->snaps;
    } else memset(&p->correction,0,sizeof(p->correction));
    predict_offset_apply(&model,&p->correction,&display);
    p->end=model;memcpy(p->end_rates,end_rates,sizeof(end_rates));p->end_tick=tick+h;p->have_end=1;
    p->display=display;p->display_tick=tick;
    if(predict_mode==PREDICT_PRESENT && view && index+1==stats.slot) {
        if(predict_distance(&display,&pose)>PREDICT_PRESENT_POSITION || predict_angle(display.r,pose.r)>PREDICT_PRESENT_ANGLE) ++p->present_bounds;
        else {memcpy(p->present_sim,sim,sizeof(p->present_sim));predict_floats(&display,p->present_shown);p->present_tick=tick;p->present_ready=1;}
    }
    predict_entry *target=&p->ring[(tick+h)%PREDICT_RING];
    memset(target,0,sizeof(*target));target->tick=tick+h;target->predicted=1;target->made=tick;target->pframe=frame;
    target->made_pose[PREDICT_MODEL]=model;target->made_pose[PREDICT_DEAD]=dead;target->made_pose[PREDICT_NONE]=pose;
    target->made_pose[PREDICT_DISPLAY]=display;++p->predictions;
}
/* Once per tick: is the player camera attached under this client's slot ship
 * (then stage 1 moves camera and world together), and has its view-block
 * cache already been filled before slot 10 (then a camera-table rewrite in
 * slot 10 would need the cache cleared)? Guarded reads only. */
static void predict_camera_sample(void) {
    unsigned char *universe=NULL,*camera=NULL,*attached=NULL;uint64_t cached=0;
    if(stats.slot<1 || stats.slot>pilot_slot_count) return;
    const pilot_slot *s=&pilot_slots[stats.slot-1];
    if(!read_local(image+PREDICT_UNIVERSE,&universe,8) || !universe || !read_local(universe+PREDICT_PLAYER_CAMERA,&camera,8) || !camera ||
       !read_local(camera+PREDICT_CAMERA_BLOCKS,&cached,8) || !read_local(camera+PREDICT_CAMERA_ATTACHED,&attached,8)) {
        ++predict_camera_unreadable;return;
    }
    ++predict_camera_samples;if(cached) ++predict_camera_cached;
    for(unsigned depth=0;attached && s->object && depth<4;depth++) {
        if(attached==s->object) {++predict_camera_on_ship;break;}
        if(!read_local(attached+0x70,&attached,8)) break;
    }
}
/* Before the window: needs the armed piloting prototype. Wire source predicts
 * this client's slot with the wire delay as horizon (horizon must be 0 or
 * equal); script source predicts every slot with horizon 1..30. */
static int predict_arm(int mode,unsigned horizon,unsigned tau_ms) {
    if(predict_armed) return mode==predict_mode && (horizon==predict_horizon || (!horizon && pilot_source==PILOT_SOURCE_WIRE)) &&
        tau_ms==predict_tau_ms && stats.active;
    if((mode!=PREDICT_OBSERVE && mode!=PREDICT_PRESENT) || !pilot_armed || !stats.active || tau_ms>PREDICT_TAU_MAX_MS) return 0;
    if(mode==PREDICT_PRESENT && (!render_rng_installed || render_rng_mode!=RENDER_RNG_SPLIT || stats.slot<1 || stats.slot>pilot_slot_count ||
       !predict_pins())) {predict_stage="present_refused";return 0;}
    if(pilot_source==PILOT_SOURCE_WIRE) {
        if(horizon && horizon!=pilot_delay) return 0;
        horizon=pilot_delay;
        if(stats.slot<1 || stats.slot>pilot_slot_count) return 0;
    } else if(pilot_source!=PILOT_SOURCE_SCRIPT || !horizon) return 0;
    if(horizon<1 || horizon>PREDICT_HORIZON_MAX || horizon>=PREDICT_RING/2) return 0;
    memset(predict_slots,0,sizeof(predict_slots));
    for(unsigned i=0;i<pilot_slot_count;i++) {
        predict_slots[i].enabled=pilot_source==PILOT_SOURCE_SCRIPT || i+1==stats.slot;
        predict_model_reset(&predict_slots[i].model);
    }
    predict_mode=mode;predict_horizon=horizon;predict_tau_ms=tau_ms;predict_frames=0;predict_last_tick=0;
    predict_present_open=0;predict_present_block=NULL;
    predict_camera_samples=predict_camera_cached=predict_camera_on_ship=predict_camera_unreadable=0;
    predict_armed=1;predict_stage="armed";return 1;
}
/* Once per admitted tick on the UI thread (slot 6), after the pilot frame. */
static int predict_frame(uint64_t tick) {
    if(!predict_armed) return 0;
    if(!tick || tick<=predict_last_tick || pilot_frame_tick!=tick) {InterlockedIncrement64(&predict_failures);predict_stage="tick";return 0;}
    for(unsigned i=0;i<pilot_slot_count;i++) if(predict_slots[i].enabled) predict_slot_frame(i,tick);
    predict_camera_sample();
    predict_last_tick=tick;++predict_frames;predict_stage="live";return 1;
}
/* Slot 10 on the MoveWorker, from the render RNG split's wrapper, before the
 * original: put this tick's displayed pose into the ship's pose block [A]
 * after re-checking that the block still is the one slot 6 verified and holds
 * S(T). Returns 1 when substituted; predict_present_leave must follow. */
static int predict_present_enter(void) {
    if(!predict_armed || predict_mode!=PREDICT_PRESENT || !stats.active || stats.slot<1 || stats.slot>pilot_slot_count) return 0;
    ++predict_present_calls;
    if(predict_present_open) {InterlockedIncrement64(&predict_present_failures);predict_stage="present_nested";return 0;}
    predict_slot *p=&predict_slots[stats.slot-1];const pilot_slot *s=&pilot_slots[stats.slot-1];
    if(!p->present_ready || p->present_tick!=stats.issued || pilot_frame_tick!=stats.issued) {++predict_present_stale;return 0;}
    p->present_ready=0; /* one render per admitted tick */
    unsigned char *block=NULL,*other=NULL,*parent=NULL;uint64_t cached=0;float now[16],shown[16];double deviation[2];
    if(!predict_view_block(s->object,&block,&other,&cached) || !block || block==other || !read_local(s->object+0x70,&parent,8) ||
       cached!=(uint64_t)(uintptr_t)parent || !read_local(block,now,sizeof(now))) {++predict_present_refused;return 0;}
    predict_view_deviation(now,p->present_sim,deviation);
    if(deviation[0]>PREDICT_VIEW_POSITION || deviation[1]>PREDICT_VIEW_ROTATION) {++predict_present_refused;return 0;}
    memcpy(shown,now,sizeof(shown));
    for(unsigned i=0;i<4;i++) memcpy(shown+4*i,p->present_shown+3*i,3*sizeof(float)); /* w components stay */
    memcpy(predict_present_saved,now,sizeof(now));memcpy(predict_present_written,shown,sizeof(shown));
    if(!predict_write(block,shown,sizeof(shown))) {++predict_present_refused;return 0;}
    predict_present_block=block;predict_present_open=1;++predict_present_applied;return 1;
}
/* After the original (also on unwind): restore S(T) unless the engine wrote
 * the block meanwhile. A block left holding the displayed pose is a failure. */
static void predict_present_leave(int entered) {
    if(!entered) return;
    float current[16];
    if(!predict_present_open || !read_local(predict_present_block,current,sizeof(current))) {
        InterlockedIncrement64(&predict_present_failures);predict_stage="present_unreadable";
    } else if(!memcmp(current,predict_present_written,sizeof(current))) {
        if(predict_write(predict_present_block,predict_present_saved,sizeof(predict_present_saved))) ++predict_present_restored;
        else {InterlockedIncrement64(&predict_present_failures);predict_stage="present_restore";}
    } else ++predict_present_foreign;
    predict_present_open=0;predict_present_block=NULL;
}
/* Main-thread gate tick: any present failure stops the gate (reason 101). */
static int predict_check_live(void) {
    return !predict_armed || predict_mode!=PREDICT_PRESENT || !InterlockedCompareExchange64(&predict_present_failures,0,0);
}
static double predict_mean(const predict_stat *s) {return s->n?s->sum/(double)s->n:0.0;}
static double predict_rms(const predict_stat *s) {return s->n?sqrt(s->sq/(double)s->n):0.0;}
/* Per tick row: last model error and running means per slot (metres / radians). */
static int predict_row(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"tick=%llu horizon=%u",(unsigned long long)predict_last_tick,predict_horizon);
    for(unsigned i=0;n>0 && (size_t)n<capacity && i<pilot_slot_count;i++) {
        const predict_slot *p=&predict_slots[i];
        if(!p->enabled) continue;
        n+=snprintf(out+n,capacity-(size_t)n," s%u=%.4g/%.4g/%.4g/%.4g/%.4g/%.4g/%llu",i+1,p->last_pos,p->last_ang,
            predict_mean(&p->pos[PREDICT_MODEL]),predict_mean(&p->pos[PREDICT_DEAD]),predict_mean(&p->pos[PREDICT_NONE]),
            predict_mean(&p->pos[PREDICT_DISPLAY]),(unsigned long long)p->checked);
    }
    return n>0 && (size_t)n<capacity?n:0;
}
static int predict_status(char *out,size_t capacity) {
    static const char *kinds[PREDICT_KINDS]={"model","dead","none","display"};
    int n=snprintf(out,capacity,"mode=%s armed=%d horizon=%u tau_ms=%u frames=%llu tick=%llu stage=%s failures=%lld "
        "present_calls=%llu present_applied=%llu present_restored=%llu present_foreign=%llu present_stale=%llu present_refused=%llu "
        "present_failures=%lld camera_samples=%llu camera_on_ship=%llu camera_cached=%llu camera_unreadable=%llu",
        predict_mode==PREDICT_OBSERVE?"observe":predict_mode==PREDICT_PRESENT?"present":"off",predict_armed,predict_horizon,predict_tau_ms,
        (unsigned long long)predict_frames,(unsigned long long)predict_last_tick,predict_stage,(long long)predict_failures,
        (unsigned long long)predict_present_calls,(unsigned long long)predict_present_applied,(unsigned long long)predict_present_restored,
        (unsigned long long)predict_present_foreign,(unsigned long long)predict_present_stale,(unsigned long long)predict_present_refused,
        (long long)predict_present_failures,(unsigned long long)predict_camera_samples,(unsigned long long)predict_camera_on_ship,
        (unsigned long long)predict_camera_cached,(unsigned long long)predict_camera_unreadable);
    for(unsigned i=0;n>0 && (size_t)n<capacity && i<pilot_slot_count;i++) {
        const predict_slot *p=&predict_slots[i];
        if(!p->enabled) continue;
        n+=snprintf(out+n,capacity-(size_t)n," s%u: samples=%llu checked=%llu skipped=%llu predictions=%llu bad_reads=%llu "
            "frame_changes=%llu resets=%llu undriven=%llu missing=%llu snaps=%llu flips=%llu",i+1,(unsigned long long)p->samples,
            (unsigned long long)p->checked,(unsigned long long)p->skipped,(unsigned long long)p->predictions,(unsigned long long)p->bad_reads,
            (unsigned long long)p->frame_changes,(unsigned long long)p->resets,(unsigned long long)p->undriven,
            (unsigned long long)p->missing_records,(unsigned long long)p->snaps,(unsigned long long)p->model.flips);
        if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," view_ok=%llu view_mismatch=%llu view_shared=%llu "
            "view_missing=%llu view_parent=%llu view_deviation=%.6g present_bounds=%llu",(unsigned long long)p->view_ok,
            (unsigned long long)p->view_mismatch,(unsigned long long)p->view_shared,(unsigned long long)p->view_missing,
            (unsigned long long)p->view_parent,p->view_deviation,(unsigned long long)p->present_bounds);
        for(unsigned k=0;n>0 && (size_t)n<capacity && k<PREDICT_KINDS;k++)
            n+=snprintf(out+n,capacity-(size_t)n," %s=%.4g/%.4g/%.4g,%.4g/%.4g/%.4g",kinds[k],predict_mean(&p->pos[k]),
                predict_rms(&p->pos[k]),p->pos[k].max,predict_mean(&p->ang[k]),predict_rms(&p->ang[k]),p->ang[k].max);
        if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," hist=");
        for(unsigned b=0;n>0 && (size_t)n<capacity && b<PREDICT_BUCKETS;b++)
            n+=snprintf(out+n,capacity-(size_t)n,"%s%llu",b?",":"",(unsigned long long)p->pos[PREDICT_MODEL].bucket[b]);
        for(unsigned a=0;n>0 && (size_t)n<capacity && a<PREDICT_AXES;a++)
            n+=snprintf(out+n,capacity-(size_t)n," a%u=%d/%.4g/%.4g/%.4g/%llu/%llu",a,p->model.sign[a],p->model.gain[a],
                p->model.up[a]/PREDICT_DT,p->model.down[a]/PREDICT_DT,(unsigned long long)p->model.samples[a],
                (unsigned long long)p->model.refused[a]);
    }
    return n>0 && (size_t)n<capacity?n:0;
}
/* Raw sample of one slot for offline fitting: tick, driven, pose (12 floats as
 * read, hex bits), the finite-difference rates, the bank's own velocity
 * fields (hex bits) and the record applied this tick. */
static int predict_raw(unsigned slot,char *out,size_t capacity) {
    if(!predict_armed || slot<1 || slot>pilot_slot_count || !predict_slots[slot-1].enabled) return 0;
    const predict_slot *p=&predict_slots[slot-1];const predict_entry *e=&p->ring[predict_last_tick%PREDICT_RING];
    if(e->tick!=predict_last_tick || !e->valid) return 0;
    int n=snprintf(out,capacity,"slot=%u tick=%llu driven=%u rates=%u pose=",slot,(unsigned long long)e->tick,e->driven,e->rates_valid);
    double values[12];memcpy(values,e->sim.p,sizeof(e->sim.p));memcpy(values+3,e->sim.r,sizeof(e->sim.r));
    for(unsigned i=0;n>0 && (size_t)n<capacity && i<12;i++) {float f=(float)values[i];uint32_t b;memcpy(&b,&f,4);n+=snprintf(out+n,capacity-(size_t)n,"%s%08x",i?",":"",b);}
    if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," rates=");
    for(unsigned i=0;n>0 && (size_t)n<capacity && i<PREDICT_AXES;i++) n+=snprintf(out+n,capacity-(size_t)n,"%s%.6g",i?",":"",e->rates[i]);
    if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," native=");
    for(unsigned i=0;n>0 && (size_t)n<capacity && i<8;i++) {uint32_t b;memcpy(&b,&e->native[i],4);n+=snprintf(out+n,capacity-(size_t)n,"%s%08x",i?",":"",b);}
    if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," record=");
    if(n>0 && (size_t)n<capacity && (size_t)n+2*PILOT_RECORD_BYTES<capacity) {
        unsigned char bytes[PILOT_RECORD_BYTES];pilot_encode(&pilot_slots[slot-1].record,bytes);pilot_hex(bytes,PILOT_RECORD_BYTES,out+n);
        n+=2*PILOT_RECORD_BYTES;
    }
    if(n>0 && (size_t)n<capacity) ++predict_raw_rows;
    return n>0 && (size_t)n<capacity?n:0;
}
