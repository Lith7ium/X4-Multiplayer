/* Presentation prediction: rotation math, the response model and its online
 * fit on a synthetic ship, bounded prediction error, display smoothing, the
 * pending-record parser and the observe layer end to end on fake engine memory. */
static unsigned char test_predict_object[0x300],test_predict_controller[0x60],test_predict_agent[0x1000],test_predict_slots[2*0x1a0];
static unsigned char test_predict_zone[2][16];
typedef struct {double gain[6],up[6],down[6];int sign[6];} test_predict_truth;
static void test_predict_truth_step(const test_predict_truth *t,double v[6],const double raw[6]) {
    for(unsigned i=0;i<6;i++) {
        double dv=t->gain[i]*(t->sign[i]*raw[i]-v[i]);
        if(dv>t->up[i]) dv=t->up[i];
        if(dv<-t->down[i]) dv=-t->down[i];
        v[i]+=dv;
    }
}
static void test_predict_write_bank(const predict_pose *pose) {
    float bank[16]={0};
    for(unsigned i=0;i<3;i++) {bank[i]=(float)pose->p[i];for(unsigned j=0;j<3;j++) bank[4*(i+1)+j]=(float)pose->r[3*i+j];}
    bank[3]=1.0f;memcpy(test_predict_slots,bank,sizeof(bank));
}
static void test_predict_math(void) {
    /* exp/log round trip, including near-zero and large angles. */
    const double ws[][3]={{0,0,0},{1e-9,-2e-9,3e-9},{0.01,0.02,-0.03},{0.5,-0.25,0.75},{-1.2,0.3,0.1},{0,0,3.0}};
    for(unsigned i=0;i<sizeof(ws)/sizeof(*ws);i++) {
        double e[9],w[3],t[9];predict_exp(ws[i],e);predict_log(e,w);
        for(unsigned k=0;k<3;k++) assert(fabs(w[k]-ws[i][k])<1e-9);
        predict_mul_transposed(e,e,t); /* orthonormal */
        for(unsigned k=0;k<9;k++) assert(fabs(t[k]-((k%4)?0.0:1.0))<1e-12);
    }
    /* Small yaw about local y turns local z toward local x (row convention). */
    double e[9];predict_exp((const double[3]){0,0.1,0},e);assert(e[6]>0.09 && fabs(e[7])<1e-12 && e[8]>0.99);
    /* rates(advance(pose, v)) == v on a rotated, translated pose. */
    predict_pose pose={{10,-20,30},{1,0,0,0,1,0,0,0,1}},moved;predict_exp((const double[3]){0.3,-0.2,0.6},pose.r);
    const double v[6]={12.5,-3.0,250.0,0.4,-0.7,1.1};double back[6];
    moved=pose;predict_advance(&moved,v,PREDICT_DT);predict_rates(&pose,&moved,PREDICT_DT,back);
    for(unsigned k=0;k<6;k++) assert(fabs(back[k]-v[k])<1e-9*(1+fabs(v[k])));
    assert(fabs(predict_distance(&pose,&moved)-sqrt(12.5*12.5+9+250.0*250.0)*PREDICT_DT)<1e-9);
    assert(fabs(predict_angle(moved.r,pose.r)-sqrt(0.16+0.49+1.21)*PREDICT_DT)<1e-12);
    /* Bank parsing: identity accepted, NaN, scale and skew refused. */
    float bank[16]={1,2,3,1, 1,0,0,0, 0,1,0,0, 0,0,1,0};predict_pose parsed;
    assert(predict_pose_from_bank(bank,&parsed) && parsed.p[2]==3.0 && parsed.r[0]==1.0 && parsed.r[8]==1.0);
    bank[3]=NAN;assert(predict_pose_from_bank(bank,&parsed)); /* w is not read */
    bank[3]=1;bank[5]=NAN;assert(!predict_pose_from_bank(bank,&parsed));bank[5]=0;
    bank[4]=1.01f;assert(!predict_pose_from_bank(bank,&parsed));bank[4]=1;
    bank[8]=0.01f;assert(!predict_pose_from_bank(bank,&parsed));bank[8]=0; /* skewed axes */
    /* Orthonormalization keeps a left-handed basis left-handed. */
    double left[9]={-1,0,0, 0,1.001,0.002, 0,0,1};predict_orthonormalize(left);
    assert(left[0]<-0.999 && fabs(predict_dot3(left+3,left+6))<1e-12 && fabs(predict_dot3(left+3,left+3)-1)<1e-12);
}
static void test_predict_model(void) {
    /* Synthetic ship: the true response has the model's form, pitch inverted. */
    test_predict_truth truth={{0.25,0.3,0.2,0.35,0.3,0.4},{1.5,1.2,2.5,0.02,0.03,0.05},{1.5,1.2,1.5,0.02,0.03,0.05},{1,1,1,-1,1,1}};
    const float limits[8]={300,0,-100,50,60,0.5f,0.75f,0.875f};
    predict_model m;predict_model_reset(&m);
    double v[6]={0},raw[6];pilot_record r;
    /* Uncalibrated: constant velocity. */
    double c[6]={1,2,3,0.1,0.2,0.3},target[6]={100,100,100,1,1,1};predict_step(&m,c,target);
    assert(c[0]==1 && c[3]==0.1 && c[5]==0.3);
    for(uint64_t tick=30;tick<30+1200;tick++) {
        pilot_script(1+(unsigned)(tick/600%2),tick,30,&r);predict_targets(&r,limits,raw);
        double before[6];memcpy(before,v,sizeof(v));test_predict_truth_step(&truth,v,raw);
        predict_calibrate(&m,before,v,raw);
    }
    assert(m.sign[3]==-1 && m.flips>=1 && m.sign[0]==1 && m.sign[2]==1 && m.sign[4]==1);
    for(unsigned i=0;i<6;i++) {
        if(!predict_axis_ready(&m,i)) continue;
        assert(fabs(m.gain[i]-truth.gain[i])<1e-6*truth.gain[i]+1e-9);
    }
    /* Limits settle within a few percent (large gain samples pull them down slowly). */
    assert(predict_axis_ready(&m,2) && fabs(m.up[2]-2.5)<0.05 && fabs(m.down[2]-1.5)<0.03);
    assert(predict_axis_ready(&m,4) && fabs(m.up[4]-0.03)<6e-4);
    /* Bounded error: the fitted model reproduces the truth over 3 steps. */
    predict_pose base={{0,0,0},{1,0,0,0,1,0,0,0,1}},truth_pose,model_pose;
    double tv[6]={0,0,120,0,0.1,0},raws[3][6];
    for(unsigned k=0;k<3;k++) {pilot_script(1,150+k,30,&r);predict_targets(&r,limits,raws[k]);}
    truth_pose=base;double t2[6];memcpy(t2,tv,sizeof(t2));
    for(unsigned k=0;k<3;k++) {test_predict_truth_step(&truth,t2,raws[k]);predict_advance(&truth_pose,t2,PREDICT_DT);}
    predict_run(&m,&base,tv,(const double (*)[6])raws,3,&model_pose,NULL);
    assert(predict_distance(&model_pose,&truth_pose)<0.01 && predict_angle(model_pose.r,truth_pose.r)<1e-4);
    /* External impulses and overshoot are refused, not fitted. */
    uint64_t refused=m.refused[2];double before[6]={0,0,10,0,0,0},after[6]={0,0,-40,0,0,0},hold[6]={0,0,300,0,0,0};
    double down=m.down[2],up=m.up[2];
    predict_calibrate(&m,before,after,hold);assert(m.refused[2]==refused+1 && m.down[2]==down);
    after[2]=400;predict_calibrate(&m,before,after,hold);assert(m.refused[2]==refused+2 && m.up[2]==up);
    /* Smoothing: a jump decays geometrically; beyond the bounds it snaps. */
    predict_offset off;memset(&off,0,sizeof(off));
    predict_pose a=base,b=base;a.p[0]=1.0;double lambda=exp(-1000.0*PREDICT_DT/100.0);
    assert(!predict_smooth(&off,&a,&b,lambda) && fabs(off.p[0]-lambda)<1e-12);
    for(unsigned k=0;k<30;k++) assert(!predict_smooth(&off,&b,&b,lambda));
    assert(fabs(off.p[0]-pow(lambda,31))<1e-12 && off.p[0]<0.01);
    predict_pose shown;predict_offset_apply(&b,&off,&shown);assert(fabs(shown.p[0]-off.p[0])<1e-12);
    a.p[0]=1000;assert(predict_smooth(&off,&a,&b,lambda) && off.p[0]==0);
    a.p[0]=0;predict_exp((const double[3]){0,0.8,0},a.r);assert(predict_smooth(&off,&a,&b,1.0));
}
static void test_predict_pending(void) {
    pilot_record r,out[4];unsigned char present[4],bytes[PILOT_RECORD_BYTES];char text[512],hex[2*PILOT_RECORD_BYTES+1];
    memset(&r,0,sizeof(r));r.mode=PILOT_MODE_ASSIST;r.capture16=41;r.q[2]=32767;pilot_encode(&r,bytes);pilot_hex(bytes,PILOT_RECORD_BYTES,hex);
    snprintf(text,sizeof(text),"frame_tick=40 41:44:%s",hex);
    assert(predict_pending_parse(text,42,3,out,present) && !present[0] && !present[1] && present[2] && out[2].q[2]==32767);
    assert(predict_pending_parse("frame_tick=40",41,3,out,present) && !present[0] && !present[1] && !present[2]);
    snprintf(text,sizeof(text),"frame_tick=40 42:44:%s",hex);assert(!predict_pending_parse(text,41,3,out,present)); /* capture16 differs */
    snprintf(text,sizeof(text),"frame_tick=40 41:44:%sx",hex);assert(!predict_pending_parse(text,41,3,out,present));
    assert(!predict_pending_parse("frame=40",41,3,out,present) && !predict_pending_parse(NULL,41,3,out,present));
}
/* Observe layer on fake engine memory: script source, horizon 3, a truth ship
 * that answers the applied records; then a zone change and a failed read. */
static void test_predict_observe(void) {
    unsigned saved_index=*(unsigned*)(image+0x38f4ee4),saved_slot=stats.slot;uint64_t saved_issued=stats.issued;int saved_active=stats.active;
    unsigned char *o=test_predict_object,*k=test_predict_controller,*a=test_predict_agent,*slots=test_predict_slots,*zone=test_predict_zone[0];
    memset(o,0,sizeof(test_predict_object));memset(k,0,sizeof(test_predict_controller));memset(a,0,sizeof(test_predict_agent));
    memcpy(o+0x70,&zone,8);memcpy(o+0x200,&k,8);memcpy(k+0x30,&slots,8);*(unsigned*)(image+0x38f4ee4)=0;
    const float limits[8]={300,0,-100,50,60,0.5f,0.75f,0.875f};memcpy(a+PILOT_LIMITS,limits,sizeof(limits));
    memset(pilot_slots,0,sizeof(pilot_slots));pilot_slot_count=1;pilot_source=PILOT_SOURCE_SCRIPT;pilot_delay=0;pilot_start=30;pilot_stop=18000;
    pilot_slots[0].saved=0x176ad5;pilot_slots[0].resolved=1;pilot_slots[0].object=o;pilot_slots[0].agent=a;pilot_slots[0].solver=a;
    /* Refusals before arming. */
    pilot_armed=0;stats.active=1;assert(!predict_arm(PREDICT_OBSERVE,3,100));
    pilot_armed=1;assert(!predict_arm(PREDICT_OBSERVE,0,100) && !predict_arm(PREDICT_PRESENT,3,100) && !predict_arm(PREDICT_OBSERVE,31,100));
    assert(!predict_arm(PREDICT_OBSERVE,3,PREDICT_TAU_MAX_MS+1));
    pilot_source=PILOT_SOURCE_WIRE;pilot_delay=3;stats.slot=1;assert(!predict_arm(PREDICT_OBSERVE,4,100));pilot_source=PILOT_SOURCE_SCRIPT;pilot_delay=0;
    assert(predict_arm(PREDICT_OBSERVE,3,100) && predict_arm(PREDICT_OBSERVE,3,100) && !predict_arm(PREDICT_OBSERVE,4,100));
    assert(!predict_frame(5)); /* the pilot frame table is not at this tick */
    test_predict_truth truth={{0.25,0.3,0.2,0.35,0.3,0.4},{1.5,1.2,2.5,0.02,0.03,0.05},{1.5,1.2,1.5,0.02,0.03,0.05},{1,1,1,1,-1,1}};
    predict_pose pose={{100,-50,200},{1,0,0,0,1,0,0,0,1}};double v[6]={0};
    for(uint64_t tick=20;tick<1260;tick++) {
        pilot_record r;
        if(tick>=pilot_start) pilot_script(1,tick,pilot_start,&r);else {memset(&r,0,sizeof(r));r.mode=PILOT_MODE_ASSIST;}
        double raw[6];predict_targets(&r,limits,raw);test_predict_truth_step(&truth,v,raw);predict_advance(&pose,v,PREDICT_DT);
        test_predict_write_bank(&pose);
        pilot_slots[0].record=r;pilot_slots[0].driving=tick>=pilot_start;pilot_frame_tick=tick;
        if(pilot_slots[0].driving) {pilot_slots[0].applied[0]+=3;pilot_slots[0].applied[1]+=3;}
        assert(predict_frame(tick));
        if(tick==630) {memset(predict_slots[0].pos,0,sizeof(predict_slots[0].pos));memset(predict_slots[0].ang,0,sizeof(predict_slots[0].ang));}
    }
    predict_slot *p=&predict_slots[0];char text[4096];
    assert(p->checked>=1230 && !p->bad_reads && !p->frame_changes && p->model.sign[4]==-1);
    assert(p->pos[PREDICT_MODEL].n==629 && p->pos[PREDICT_MODEL].max<0.03 && p->ang[PREDICT_MODEL].max<1e-3);
    assert(p->pos[PREDICT_DEAD].max>0.2 && p->pos[PREDICT_MODEL].sum*20<p->pos[PREDICT_DEAD].sum);
    assert(p->pos[PREDICT_NONE].max>10.0 && p->pos[PREDICT_DISPLAY].max<0.05);
    assert(predict_row(text,sizeof(text)) && !strncmp(text,"tick=1259 horizon=3 s1=",23));
    assert(predict_status(text,sizeof(text)) && strstr(text,"mode=observe armed=1 horizon=3 tau_ms=100") && strstr(text," s1: samples=") &&
           strstr(text," a4=-1/"));
    assert(predict_raw(1,text,sizeof(text)) && strstr(text,"slot=1 tick=1259 driven=1 rates=1 pose=") && strstr(text," record=01"));
    /* Zone change: the history restarts and the prediction made before it is skipped. */
    unsigned char *other=test_predict_zone[1];memcpy(o+0x70,&other,8);
    uint64_t checked=p->checked,skipped=p->skipped;
    for(uint64_t tick=1260;tick<1270;tick++) {
        pilot_frame_tick=tick;pilot_slots[0].applied[0]+=3;pilot_slots[0].applied[1]+=3;
        predict_advance(&pose,v,PREDICT_DT);test_predict_write_bank(&pose);assert(predict_frame(tick));
    }
    assert(p->frame_changes==1 && p->checked>checked && p->checked<checked+10 && p->skipped==skipped);
    /* A failed read resets the history and is counted; never a stop. */
    memset(o+0x200,0,8);pilot_frame_tick=1270;assert(predict_frame(1270) && p->bad_reads==1);
    assert(!predict_frame(1270)); /* ticks only advance */
    /* Wire source: the local slot's pending records drive the horizon. */
    memcpy(o+0x200,&k,8);memset(predict_slots,0,sizeof(predict_slots));predict_armed=0;
    pilot_source=PILOT_SOURCE_WIRE;pilot_delay=3;stats.slot=1;memset(pilot_locals,0,sizeof(pilot_locals));
    assert(predict_arm(PREDICT_OBSERVE,0,0) && predict_horizon==3 && predict_slots[0].enabled);
    pilot_record r;memset(&r,0,sizeof(r));r.mode=PILOT_MODE_ASSIST;
    for(uint64_t t=1281;t<=1283;t++) {
        pilot_local *l=&pilot_locals[t%PILOT_LOCAL_RING];l->capture=t;l->apply=t+3;l->used=1;
        r.capture16=(uint16_t)t;r.q[2]=(int16_t)(t==1281?32767:-32767);pilot_encode(&r,l->bytes);
    }
    pilot_frame_tick=1283;double raw[PREDICT_HORIZON_MAX][6];
    assert(predict_future(1,1283,3,limits,NULL,raw)==0 && raw[0][2]==300.0 && raw[1][2]==-100.0 && raw[2][2]==-100.0);
    pilot_locals[1282%PILOT_LOCAL_RING].used=0; /* apply 1285 missing: the previous step's record holds */
    assert(predict_future(1,1283,3,limits,NULL,raw)==1 && raw[0][2]==300.0 && raw[1][2]==300.0 && raw[2][2]==-100.0);
    pilot_record applied=r;applied.q[2]=0;pilot_locals[1281%PILOT_LOCAL_RING].used=0;
    assert(predict_future(1,1283,3,limits,&applied,raw)==2 && raw[0][2]==0.0 && raw[1][2]==0.0 && raw[2][2]==-100.0);
    pilot_locals[1281%PILOT_LOCAL_RING].used=1;pilot_frame_tick=1284;
    assert(predict_future(1,1283,3,limits,NULL,raw)==2); /* a record already applied is not pending */
    /* Reset to default off. */
    memset(predict_slots,0,sizeof(predict_slots));predict_armed=0;predict_mode=PREDICT_OFF;predict_horizon=predict_tau_ms=0;
    predict_frames=predict_last_tick=predict_raw_rows=0;predict_failures=0;predict_stage="off";
    memset(pilot_slots,0,sizeof(pilot_slots));memset(pilot_locals,0,sizeof(pilot_locals));
    pilot_armed=0;pilot_slot_count=pilot_source=pilot_delay=0;pilot_start=pilot_stop=pilot_frame_tick=0;
    *(unsigned*)(image+0x38f4ee4)=saved_index;stats.slot=saved_slot;stats.issued=saved_issued;stats.active=saved_active;
}
/* Present mode: the slot-10 substitution through the render RNG split's real
 * wrapper (render_rng_execute) on fake view caches. */
static float test_predict_blocks[2][16],test_predict_seen[16],test_predict_foreign[16];
static unsigned test_predict_renders,test_predict_overwrite,test_predict_throw;
static void test_predict_render(void *self) {
    assert(self==(void*)&test_predict_renders);++test_predict_renders;
    memcpy(test_predict_seen,test_predict_blocks[0],sizeof(test_predict_seen));
    if(test_predict_overwrite) memcpy(test_predict_blocks[0],test_predict_foreign,sizeof(test_predict_foreign)); /* the engine rewrites it */
    if(test_predict_throw) RaiseException(0xe0525232,0,0,NULL);
}
static int test_predict_slot10(void) {
    uint64_t word=0x77;int caught=0;
    __try {render_rng_execute(test_predict_render,&test_predict_renders,&word,NULL);}
    __except(EXCEPTION_EXECUTE_HANDLER) {caught=1;}
    assert(word==0x77);return caught;
}
static void test_predict_present(void) {
    unsigned saved_index=*(unsigned*)(image+0x38f4ee4),saved_view=*(unsigned*)(image+PREDICT_VIEW_INDEX),saved_slot=stats.slot;
    uint64_t saved_issued=stats.issued;int saved_active=stats.active,saved_mode=render_rng_mode,saved_installed=render_rng_installed;
    unsigned char *o=test_predict_object,*k=test_predict_controller,*a=test_predict_agent,*slots=test_predict_slots,*zone=test_predict_zone[0];
    memset(o,0,sizeof(test_predict_object));memset(k,0,sizeof(test_predict_controller));memset(a,0,sizeof(test_predict_agent));
    memcpy(o+0x70,&zone,8);memcpy(o+0x200,&k,8);memcpy(k+0x30,&slots,8);*(unsigned*)(image+0x38f4ee4)=0;
    /* View caches: [0] and [1] with their own pose blocks, cached parent = the zone. */
    unsigned char *b0=(unsigned char*)test_predict_blocks[0],*b1=(unsigned char*)test_predict_blocks[1];
    memcpy(o+PREDICT_VIEW_CACHE,&zone,8);memcpy(o+PREDICT_VIEW_CACHE+PREDICT_VIEW_BLOCK,&b0,8);
    memcpy(o+PREDICT_VIEW_CACHE+PREDICT_VIEW_STRIDE,&zone,8);memcpy(o+PREDICT_VIEW_CACHE+PREDICT_VIEW_STRIDE+PREDICT_VIEW_BLOCK,&b1,8);
    *(unsigned*)(image+PREDICT_VIEW_INDEX)=0;
    const float limits[8]={300,0,-100,50,60,0.5f,0.75f,0.875f};memcpy(a+PILOT_LIMITS,limits,sizeof(limits));
    memset(pilot_slots,0,sizeof(pilot_slots));pilot_slot_count=1;pilot_source=PILOT_SOURCE_SCRIPT;pilot_delay=0;pilot_start=30;pilot_stop=18000;
    pilot_slots[0].saved=0x176ad5;pilot_slots[0].resolved=1;pilot_slots[0].object=o;pilot_slots[0].agent=a;pilot_slots[0].solver=a;
    pilot_armed=1;stats.active=1;stats.slot=1;memset(predict_slots,0,sizeof(predict_slots));predict_armed=0;
    /* Present needs the pins and the split wrapper. */
    render_rng_installed=1;render_rng_mode=RENDER_RNG_SPLIT;predict_pins_checked=0;
    assert(!predict_arm(PREDICT_PRESENT,3,100) && predict_pins_checked==-1); /* the fake image is not X4 */
    predict_pins_checked=1;render_rng_installed=0;assert(!predict_arm(PREDICT_PRESENT,3,100));
    render_rng_installed=1;render_rng_mode=RENDER_RNG_OBSERVE;assert(!predict_arm(PREDICT_PRESENT,3,100));
    render_rng_mode=RENDER_RNG_SPLIT;render_rng_state=1;render_rng_frame=UINT64_MAX;assert(predict_arm(PREDICT_PRESENT,3,100));
    test_predict_truth truth={{0.25,0.3,0.2,0.35,0.3,0.4},{1.5,1.2,2.5,0.02,0.03,0.05},{1.5,1.2,1.5,0.02,0.03,0.05},{1,1,1,1,1,1}};
    predict_pose pose={{100,-50,200},{1,0,0,0,1,0,0,0,1}};double v[6]={0};unsigned presented=0;
    for(uint64_t tick=20;tick<400;tick++) {
        pilot_record r;
        if(tick>=pilot_start) pilot_script(1,tick,pilot_start,&r);else {memset(&r,0,sizeof(r));r.mode=PILOT_MODE_ASSIST;}
        double raw[6];predict_targets(&r,limits,raw);test_predict_truth_step(&truth,v,raw);predict_advance(&pose,v,PREDICT_DT);
        test_predict_write_bank(&pose);memcpy(test_predict_blocks[0],test_predict_slots,64);memcpy(test_predict_blocks[1],test_predict_slots,64);
        test_predict_blocks[0][3]=7.0f; /* w components are not ours */
        pilot_slots[0].record=r;pilot_slots[0].driving=tick>=pilot_start;pilot_frame_tick=tick;stats.issued=tick;
        if(pilot_slots[0].driving) {pilot_slots[0].applied[0]+=3;pilot_slots[0].applied[1]+=3;}
        assert(predict_frame(tick));
        uint64_t applied=predict_present_applied;float sim[16];memcpy(sim,test_predict_blocks[0],sizeof(sim));
        assert(!test_predict_slot10());
        assert(!memcmp(test_predict_blocks[0],sim,sizeof(sim))); /* S(T) is back after slot 10 */
        if(predict_present_applied==applied+1) {
            ++presented;
            for(unsigned i=0;i<4;i++) for(unsigned j=0;j<3;j++) assert(test_predict_seen[4*i+j]==predict_slots[0].present_shown[3*i+j]);
            assert(test_predict_seen[3]==7.0f);
            if(tick>=60 && tick<150) assert(test_predict_seen[2]>sim[2]+1.0f); /* flying forward, model fitted: the shown ship is ahead */
        } else assert(!memcmp(test_predict_seen,sim,sizeof(sim)));
    }
    predict_slot *p=&predict_slots[0];
    assert(presented>=375 && predict_present_restored==presented && p->view_ok>=379 && !p->view_mismatch && !predict_present_failures);
    assert(predict_check_live());
    /* A second render in the same tick is stale. */
    uint64_t stale=predict_present_stale;assert(!test_predict_slot10() && predict_present_stale==stale+1);
    /* The engine rewrites the block during slot 10: its value stands. */
    uint64_t tick=400;stats.issued=pilot_frame_tick=tick;predict_advance(&pose,v,PREDICT_DT);test_predict_write_bank(&pose);
    memcpy(test_predict_blocks[0],test_predict_slots,64);memcpy(test_predict_blocks[1],test_predict_slots,64);assert(predict_frame(tick));
    for(unsigned i=0;i<16;i++) test_predict_foreign[i]=(float)i;
    test_predict_overwrite=1;assert(!test_predict_slot10());test_predict_overwrite=0;
    assert(predict_present_foreign==1 && !memcmp(test_predict_blocks[0],test_predict_foreign,64));
    /* An exception in slot 10 still restores. */
    ++tick;stats.issued=pilot_frame_tick=tick;predict_advance(&pose,v,PREDICT_DT);test_predict_write_bank(&pose);
    memcpy(test_predict_blocks[0],test_predict_slots,64);memcpy(test_predict_blocks[1],test_predict_slots,64);assert(predict_frame(tick));
    uint64_t restored=predict_present_restored;float sim[16];memcpy(sim,test_predict_blocks[0],sizeof(sim));
    test_predict_throw=1;assert(test_predict_slot10());test_predict_throw=0;
    assert(predict_present_restored==restored+1 && !memcmp(test_predict_blocks[0],sim,sizeof(sim)) && !predict_present_open);
    render_rng_failures=0;render_rng_stage="idle";
    /* Refusals: a shared block, a block that is not S(T), a foreign parent. */
    ++tick;stats.issued=pilot_frame_tick=tick;predict_advance(&pose,v,PREDICT_DT);test_predict_write_bank(&pose);
    memcpy(test_predict_blocks[0],test_predict_slots,64);memcpy(o+PREDICT_VIEW_CACHE+PREDICT_VIEW_STRIDE+PREDICT_VIEW_BLOCK,&b0,8);
    uint64_t shared=p->view_shared;stale=predict_present_stale;assert(predict_frame(tick) && p->view_shared==shared+1);
    assert(!test_predict_slot10() && predict_present_stale==stale+1);
    memcpy(o+PREDICT_VIEW_CACHE+PREDICT_VIEW_STRIDE+PREDICT_VIEW_BLOCK,&b1,8);
    ++tick;stats.issued=pilot_frame_tick=tick;predict_advance(&pose,v,PREDICT_DT);test_predict_write_bank(&pose);
    memcpy(test_predict_blocks[0],test_predict_slots,64);test_predict_blocks[0][0]+=1.0f;
    uint64_t mismatch=p->view_mismatch;assert(predict_frame(tick) && p->view_mismatch==mismatch+1 && p->view_deviation>=0.99);
    ++tick;stats.issued=pilot_frame_tick=tick;predict_advance(&pose,v,PREDICT_DT);test_predict_write_bank(&pose);
    memcpy(test_predict_blocks[0],test_predict_slots,64);unsigned char *elsewhere=test_predict_zone[1];memcpy(o+PREDICT_VIEW_CACHE,&elsewhere,8);
    uint64_t foreign_parent=p->view_parent;assert(predict_frame(tick) && p->view_parent==foreign_parent+1);
    memcpy(o+PREDICT_VIEW_CACHE,&zone,8);
    /* The block changes between slot 6 and slot 10: refused at slot 10. */
    ++tick;stats.issued=pilot_frame_tick=tick;predict_advance(&pose,v,PREDICT_DT);test_predict_write_bank(&pose);
    memcpy(test_predict_blocks[0],test_predict_slots,64);assert(predict_frame(tick) && p->present_ready);
    test_predict_blocks[0][1]+=0.5f;uint64_t refused=predict_present_refused;
    assert(!test_predict_slot10() && predict_present_refused==refused+1 && !memcmp(test_predict_seen,test_predict_blocks[0],64));
    char text[4096];assert(predict_status(text,sizeof(text)) && strstr(text,"mode=present") && strstr(text," view_ok=") && strstr(text,"present_failures=0"));
    /* Camera diagnostics: attached under the slot ship (two levels up), view-block cache in use. */
    assert(predict_camera_unreadable>0 && !predict_camera_samples); /* no universe in the fake image */
    static unsigned char universe[0x400],camera[0x900],seat[0x100];unsigned char *u_=universe,*c_=camera,*seat_=seat;
    memcpy(image+PREDICT_UNIVERSE,&u_,8);memcpy(universe+PREDICT_PLAYER_CAMERA,&c_,8);memcpy(camera+PREDICT_CAMERA_ATTACHED,&seat_,8);
    memcpy(seat+0x70,&o,8);*(uint64_t*)(camera+PREDICT_CAMERA_BLOCKS)=2;
    predict_camera_sample();assert(predict_camera_samples==1 && predict_camera_on_ship==1 && predict_camera_cached==1);
    memset(seat+0x70,0,8);*(uint64_t*)(camera+PREDICT_CAMERA_BLOCKS)=0;
    predict_camera_sample();assert(predict_camera_samples==2 && predict_camera_on_ship==1 && predict_camera_cached==1);
    memset(image+PREDICT_UNIVERSE,0,8);
    /* Nesting or an unreadable block on leave is a failure: the gate stops with reason 101. */
    predict_present_open=1;assert(!predict_present_enter() && !predict_check_live());predict_present_open=0;predict_present_failures=0;
    predict_present_open=1;predict_present_block=NULL;predict_present_leave(1);assert(!predict_check_live() && !predict_present_open);
    uint32_t saved_reason=stats.reason;stats.restored=1;render_rng_installed=0;finish(PREDICT_STOP_REASON);
    assert(stats.reason==PREDICT_STOP_REASON && !stats.active && !predict_present_enter()); /* nothing presents after a stop */
    stats.reason=saved_reason;predict_present_failures=0;
    /* Reset to default off. */
    memset(predict_slots,0,sizeof(predict_slots));predict_armed=0;predict_mode=PREDICT_OFF;predict_horizon=predict_tau_ms=0;
    predict_frames=predict_last_tick=predict_raw_rows=0;predict_failures=0;predict_stage="off";predict_pins_checked=0;
    predict_present_calls=predict_present_applied=predict_present_restored=predict_present_foreign=predict_present_stale=predict_present_refused=0;
    predict_camera_samples=predict_camera_cached=predict_camera_on_ship=predict_camera_unreadable=0;
    memset(pilot_slots,0,sizeof(pilot_slots));pilot_armed=0;pilot_slot_count=pilot_source=pilot_delay=0;pilot_start=pilot_stop=pilot_frame_tick=0;
    render_rng_mode=saved_mode;render_rng_installed=saved_installed;render_rng_calls=render_rng_steps=render_rng_unknown=render_rng_ids=0;
    render_rng_frame=UINT64_MAX;render_rng_stage="idle";
    *(unsigned*)(image+0x38f4ee4)=saved_index;*(unsigned*)(image+PREDICT_VIEW_INDEX)=saved_view;
    stats.slot=saved_slot;stats.issued=saved_issued;stats.active=saved_active;
}
/* Present-mode pins against the actual X4.exe (file sections, as loaded). */
static void test_predict_native_image(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;IMAGE_SECTION_HEADER sections[32];
    assert(fread(&dos,1,sizeof(dos),f)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && !fseek(f,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),f)==sizeof(nt) && nt.FileHeader.TimeDateStamp==0x6a1eaded && nt.FileHeader.NumberOfSections<=32);
    assert(fread(sections,sizeof(*sections),nt.FileHeader.NumberOfSections,f)==nt.FileHeader.NumberOfSections);
    static unsigned char bytes[0x800];
    for(unsigned i=0;i<sizeof(predict_pin_list)/sizeof(*predict_pin_list);i++) {
        unsigned offset=0,rva=predict_pin_list[i].rva,size=predict_pin_list[i].size;
        assert(size<=sizeof(bytes));
        for(unsigned j=0;j<nt.FileHeader.NumberOfSections;j++)
            if(rva>=sections[j].VirtualAddress && rva+size<=sections[j].VirtualAddress+sections[j].SizeOfRawData)
                offset=sections[j].PointerToRawData+rva-sections[j].VirtualAddress;
        assert(offset && !fseek(f,(long)offset,SEEK_SET) && fread(bytes,1,size,f)==size && hash_ok(bytes,size,predict_pin_list[i].sha));
        bytes[size/2]^=1;assert(!hash_ok(bytes,size,predict_pin_list[i].sha)); /* tampered */
    }
    /* The view-cache index loads the present mode relies on, in the pinned bodies. */
    const struct {unsigned at;unsigned char op[3];unsigned target;} loads[]={
        {0x3de677,{0x44,0x8b,0x15},PREDICT_VIEW_INDEX},{0x979f19,{0x0f,0x45,0x05},PREDICT_VIEW_INDEX},{0xef4205,{0x8b,0x0d,0},PREDICT_VIEW_INDEX}};
    for(unsigned i=0;i<sizeof(loads)/sizeof(*loads);i++) {
        unsigned offset=0;unsigned char code[8];
        for(unsigned j=0;j<nt.FileHeader.NumberOfSections;j++)
            if(loads[i].at>=sections[j].VirtualAddress && loads[i].at+8<=sections[j].VirtualAddress+sections[j].SizeOfRawData)
                offset=sections[j].PointerToRawData+loads[i].at-sections[j].VirtualAddress;
        assert(offset && !fseek(f,(long)offset,SEEK_SET) && fread(code,1,8,f)==8);
        unsigned length=loads[i].op[2]?3:2;int32_t displacement;memcpy(&displacement,code+length,4);
        assert(!memcmp(code,loads[i].op,length) && loads[i].at+length+4+(unsigned)displacement==loads[i].target);
    }
    fclose(f);
    puts("pinned native image prediction present pins (render transform, camera-table reader, view-cache publish) and index loads verified");
}
/* Worst-width status and row: four slots, every counter and statistic extreme. */
static void test_predict_widths(void) {
    unsigned saved_count=pilot_slot_count;char status[8192],row[512];
    pilot_slot_count=PILOT_SLOTS;predict_armed=1;predict_mode=PREDICT_PRESENT;predict_horizon=PREDICT_HORIZON_MAX;predict_tau_ms=PREDICT_TAU_MAX_MS;
    predict_stage="present_unreadable";predict_frames=predict_last_tick=UINT64_MAX;predict_failures=predict_present_failures=INT64_MIN;
    predict_present_calls=predict_present_applied=predict_present_restored=predict_present_foreign=predict_present_stale=predict_present_refused=UINT64_MAX;
    predict_camera_samples=predict_camera_cached=predict_camera_on_ship=predict_camera_unreadable=UINT64_MAX;
    for(unsigned i=0;i<PILOT_SLOTS;i++) {
        predict_slot *p=&predict_slots[i];memset(p,0xff,sizeof(*p));p->enabled=1;
        p->last_pos=p->last_ang=p->view_deviation=-1.2345678e-300;
        for(unsigned k=0;k<PREDICT_KINDS;k++) {p->pos[k].n=p->ang[k].n=1;p->pos[k].sum=p->ang[k].sum=-1.2345678e300;
            p->pos[k].sq=p->ang[k].sq=1.2345678e300;p->pos[k].max=p->ang[k].max=-1.2345678e-300;}
        for(unsigned a=0;a<PREDICT_AXES;a++) {p->model.sign[a]=-1;p->model.gain[a]=p->model.up[a]=p->model.down[a]=-1.2345678e-300;}
    }
    int n=predict_status(status,sizeof(status)),m=predict_row(row,sizeof(row));
    assert(n>0 && n<(int)sizeof(status) && m>0 && m<(int)sizeof(row));
    printf("pilot predict worst-case status %d bytes, row %d bytes\n",n,m);
    memset(predict_slots,0,sizeof(predict_slots));predict_armed=0;predict_mode=PREDICT_OFF;predict_horizon=predict_tau_ms=0;predict_stage="off";
    predict_frames=predict_last_tick=0;predict_failures=predict_present_failures=0;
    predict_present_calls=predict_present_applied=predict_present_restored=predict_present_foreign=predict_present_stale=predict_present_refused=0;
    predict_camera_samples=predict_camera_cached=predict_camera_on_ship=predict_camera_unreadable=0;pilot_slot_count=saved_count;
}
static void test_pilot_predict(void) {
    test_predict_math();test_predict_model();test_predict_pending();test_predict_observe();test_predict_present();test_predict_widths();
    printf("pilot predict rotation math/model fit/bounded error/smoothing/pending parse/observe layer/present substitution tests PASS\n");
}
