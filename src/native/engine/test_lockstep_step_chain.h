/* FR8 on the executable's bytes (lockstep_step_chain_win.h), in a private copy: the pins (a flipped byte refuses),
 * the six swaps and the view (raw bytes ours, hash_ok/read_local the file's, the world trace's pin of the whole step
 * still passing), then the hooks entered through their stubs: outside a watched step only counted; inside one (this
 * thread marked) recorded, with the approach helper 0xeb1db0 run on its own bytes against the reading (period 0: no
 * wrap) and asinf and the builder as stand-ins; then the exact restoration. Included by test_lockstep_gate.c. */
static float tsc_asinf(float x) {return x*0.5f;}
static void tsc_builder(void *bank,float yaw,float pitch,float roll) {
    float rows[12];for(unsigned i=0;i<12;i++) rows[i]=yaw*(float)i+pitch-roll;
    memcpy((unsigned char*)bank+0x10,rows,sizeof(rows));
}
static void tsc_jump(uint32_t rva,const void *target) {unsigned char code[14]={0xff,0x25,0,0,0,0};memcpy(code+6,&target,8);memcpy(image+rva,code,14);}
/* FR17's stand-in GetTargetPositionForWingMan: out[i] = 10*index + i + time; returns 1 in al (garbage above). */
static uint64_t tsc_fm(void *formation,void *out,uint64_t index,double time) {
    (void)formation;float o[16];for(unsigned i=0;i<16;i++) o[i]=(float)(10*index+i)+(float)time;
    memcpy(out,o,sizeof(o));return 0x7700u|1u;
}
/* FR18's stand-ins: the converter copies the point's mat4 (+0x20) to out and runs the conversion through its stub with
 * the reference object tsc_pt_ref and its frame argument; the conversion adds 100*flag to every lane. */
static void *tsc_pt_rcx;static unsigned char *tsc_pt_ref;
static uint64_t tsc_pt(void *rcx,void *out,void *point,void *frame,void *a5,void *a6) {
    (void)a5;(void)a6;tsc_pt_rcx=rcx;memcpy(out,(unsigned char*)point+0x20,0x40);
    void *r=tsc_pt_ref,*f=frame;((stc_pt_math_fn)(void*)(stc_page+stc_stub(9)))(out,NULL,&r,&f,0x7700u|1u);
    return 0x5500u|1u;
}
static uint64_t tsc_pt_math(void *out,void *velocity,void *ref,void *frame,uint64_t flag) {
    (void)velocity;(void)ref;(void)frame;float m[16];memcpy(m,out,0x40);
    for(unsigned i=0;i<16;i++) m[i]+=100.0f*(float)(flag&0xff);
    memcpy(out,m,0x40);return 0;
}
/* 0xe8c380's side of the call: push r15; mov r15,rcx; sub rsp,0x30; the caller's fifth and sixth arguments to
 * [rsp+0x20]/[rsp+0x28]; mov rax,stub; call rax; add rsp,0x30; pop r15; ret. */
typedef uint64_t (*tsc_pt_caller_fn)(void *agent,void *out,void *point,void *frame,void *a5,void *a6);
static unsigned char *tsc_pt_caller(void *stub) {
    static const unsigned char head[]={0x41,0x57,0x49,0x89,0xcf,0x48,0x83,0xec,0x30,0x48,0x8b,0x44,0x24,0x60,0x48,0x89,0x44,0x24,0x20,
        0x48,0x8b,0x44,0x24,0x68,0x48,0x89,0x44,0x24,0x28,0x48,0xb8},tail[]={0xff,0xd0,0x48,0x83,0xc4,0x30,0x41,0x5f,0xc3};
    unsigned char *p=VirtualAlloc(NULL,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(p);
    memcpy(p,head,sizeof(head));memcpy(p+sizeof(head),&stub,8);memcpy(p+sizeof(head)+8,tail,sizeof(tail));
    FlushInstructionCache(GetCurrentProcess(),p,0x1000);return p;
}
/* The helper as read: |cur-t| < 1e-4 -> t; cur >= t -> max(cur - rate*dt, t); else min(rate*dt + cur, t). */
static float tsc_approach(float cur,float t,float rate,float dt) {
    if(!(fabsf(cur-t)>=1e-4f)) return t;
    volatile float step=rate*dt;
    if(cur>=t) {const float x=cur-step;return x>t?x:t;}
    const float x=step+cur;return x<t?x:t;
}
static void test_step_chain_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && !fseek(file,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        if(section.SizeOfRawData) {assert(!fseek(file,section.PointerToRawData,SEEK_SET) && fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);}
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    unsigned char *saved_page=stc_page;stc_page=NULL;stc_done=0;st_chain_installed=0;st_chain_restored=0;
    static unsigned char block[STC_BLOCK_SIZE],seen[STC_BLOCK_SIZE];memcpy(block,image+STC_BLOCK,STC_BLOCK_SIZE);
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) assert(stc_site_state(i)==0);
    image[STC_BLOCK+0x20]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[STC_BLOCK+0x20]^=1;
    image[STC_HELPER+0x30]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[STC_HELPER+0x30]^=1;
    image[stc_sites[3].site+2]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[stc_sites[3].site+2]^=1;
    image[STC_FM+0x70]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[STC_FM+0x70]^=1;
    image[STC_FM_WRAPPER+0x10]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[STC_FM_WRAPPER+0x10]^=1;
    image[stc_sites[7].site+3]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[stc_sites[7].site+3]^=1;
    image[STC_PT+0x100]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[STC_PT+0x100]^=1;
    image[STC_PT_CONVERT+0x20]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[STC_PT_CONVERT+0x20]^=1;
    image[STC_PT_CALLER+0x10]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[STC_PT_CALLER+0x10]^=1;
    image[stc_sites[8].site+2]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[stc_sites[8].site+2]^=1;
    image[stc_sites[9].site+4]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[stc_sites[9].site+4]^=1;
    assert(!stc_page && !st_chain_installed);
    /* The stand-ins (called, never read: not pinned) and the helper executable in this copy. */
    DWORD old;
    assert(VirtualProtect(image+(STC_ASINF&~0xfffu),0x2000,PAGE_EXECUTE_READWRITE,&old) && VirtualProtect(image+(STC_BUILDER&~0xfffu),0x2000,PAGE_EXECUTE_READWRITE,&old));
    tsc_jump(STC_ASINF,(void*)tsc_asinf);tsc_jump(STC_BUILDER,(void*)tsc_builder);
    assert(VirtualProtect(image+(STC_HELPER&~0xfffu),0x1000,PAGE_EXECUTE_READWRITE,&old));
    FlushInstructionCache(GetCurrentProcess(),image,nt.OptionalHeader.SizeOfImage);
    assert(st_chain_arm() && st_chain_installed && stc_done==(1u<<ST_CHAIN_SITE_COUNT)-1u && !strcmp(st_chain_stage,"armed") && stc_page);
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) assert(stc_site_state(i)==1);
    assert(memcmp(image+STC_BLOCK,block,STC_BLOCK_SIZE));                                  /* raw: ours */
    assert(hash_ok(image+STC_BLOCK,STC_BLOCK_SIZE,STC_BLOCK_SHA));                          /* the view: the file's */
    assert(read_local(image+STC_BLOCK,seen,STC_BLOCK_SIZE) && !memcmp(seen,block,STC_BLOCK_SIZE));
    assert(hash_ok(image+0xeb27c0,5212,"7920e6e4ff99bf0d0c348eeab202b0a21f5e9d69c4005ba75d302eac7ea0c8a4")); /* the world trace's pin */
    assert(st_chain_owned() && !memcmp(stc_page+stc_stub(8),"\x4c\x89\xf9\xff\x25\0\0\0\0",9) && !memcmp(stc_page+stc_stub(9),"\xff\x25\0\0\0\0",6));
    assert(stc_stub(7)==0x70 && stc_stub(8)==0x80 && stc_stub(9)==0xa0 && STC_CODE_BYTES==0xc0);
    /* Outside a watched step: the original's result, counted, nothing recorded. */
    static st_record_t rec;memset(&rec,0,sizeof(rec));rec.chain[0]=ST_CHAIN_TAG;
    const LONG64 hits_pitch=st_chain_hits[3],recorded=st_chain_recorded;
    float a=0.25f;((stc_approach_fn)(void*)(stc_page+0x30))(&a,0.1f,0.5f,0.02f,0.0f);
    assert(a==tsc_approach(0.25f,0.1f,0.5f,0.02f) && st_chain_hits[3]==hits_pitch+1 && st_chain_recorded==recorded && !st_live());
    /* Inside: the approach helper on its own bytes against the reading, 20,000 inputs through the pitch stub. */
    st_record_t *was=NULL;const int live=st_live_begin(&rec,&was);assert(live>=0 && st_live()==&rec && !was);
    float first_in=0,first_t=0,first_out=0;unsigned at_target=0;
    for(unsigned i=0;i<20000;i++) {
        const float cur=tfr_range(-1.5f,1.5f),t=(i%5==0)?cur+tfr_range(-2e-4f,2e-4f):tfr_range(-1.5f,1.5f);
        const float rate=tfr_range(0.0f,3.0f),dt=tfr_range(0.0f,0.05f);
        float x=cur;((stc_approach_fn)(void*)(stc_page+0x30))(&x,t,rate,dt,0.0f);
        const float want=tsc_approach(cur,t,rate,dt);
        if(fr_bits(x)!=fr_bits(want)) {printf("step chain approach %u: cur=%08x t=%08x rate=%08x dt=%08x native %08x read %08x\n",i,fr_bits(cur),fr_bits(t),fr_bits(rate),fr_bits(dt),fr_bits(x),fr_bits(want));assert(0);}
        if(x==t) ++at_target;
        if(!i) {first_in=cur;first_t=t;first_out=x;}
    }
    float y=-0.5f;((stc_approach_fn)(void*)(stc_page+0x10))(&y,-0.25f,1.0f,0.1f,0.0f);           /* yaw */
    float r=0.75f;((stc_approach_fn)(void*)(stc_page+0x40))(&r,0.70f,2.0f,0.01f,0.0f);           /* roll */
    assert(((stc_asin_fn)(void*)stc_page)(0.5f)==0.25f && ((stc_asin_fn)(void*)(stc_page+0x20))(-0.2f)==-0.1f);
    unsigned char bank[0x60];memset(bank,0,sizeof(bank));((stc_builder_fn)(void*)(stc_page+0x50))(bank,1.0f,2.0f,3.0f);
    st_live_end(live,was);assert(!st_live());
    const uint32_t *w=rec.chain,*s=w+ST_CHAIN_HEAD;
    assert(w[0]==ST_CHAIN_TAG && w[1]==1 && w[2]==fr_bits(1.0f) && w[3]==fr_bits(2.0f) && w[4]==fr_bits(3.0f) && !memcmp(w+5,bank+0x10,0x30) && !w[17] && !w[18]);
    assert(s[0]==1 && s[1]==fr_bits(-0.5f) && s[2]==fr_bits(-0.25f) && s[3]==fr_bits(1.0f) && s[4]==fr_bits(0.1f) && !s[5] && s[6]==fr_bits(y) && y==tsc_approach(-0.5f,-0.25f,1.0f,0.1f));
    assert(s[7]==20000 && s[8]==fr_bits(first_in) && s[9]==fr_bits(first_t) && s[13]==fr_bits(first_out));
    assert(s[14]==1 && s[15]==fr_bits(0.75f) && s[20]==fr_bits(r) && r==tsc_approach(0.75f,0.70f,2.0f,0.01f));
    assert(s[21]==1 && s[22]==fr_bits(0.5f) && s[27]==fr_bits(0.25f) && s[28]==1 && s[29]==fr_bits(-0.2f) && s[34]==fr_bits(-0.1f));
    assert(at_target>3000 && st_chain_recorded==recorded+20005);
    /* FR17: GetTargetPositionForWingMan behind a stand-in (written after the pins), entered through its two stubs with a
     * formation of two agents (the leader unwatched, the wingman watched) in tick 2 of 3, read bank 1. */
    {assert(VirtualProtect(image+(STC_FM&~0xfffu),0x2000,PAGE_EXECUTE_READWRITE,&old));tsc_jump(STC_FM,(void*)tsc_fm);
     FlushInstructionCache(GetCurrentProcess(),image+STC_FM,14);
     world_identity_entry *prior_identities=world_identities;const LONG64 prior_count=world_identity_count;
     world_identities=VirtualAlloc(NULL,sizeof(world_identity_entry)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
     assert(world_identities);world_identity_count=0;assert(world_identity_record(9101,0x7dcd0) && world_identity_record(9102,0x7dd7c));
     const unsigned prior_ids=st_id_count;const uint64_t prior_ticks=st_ticks,prior_issued=stats.issued;const int prior_active=stats.active,prior_phase=boot_phase;
     const DWORD prior_ui=ui_thread;
     st_id_count=1;st_ids[0]=0x7dd7c;st_ticks=3;stats.active=1;boot_phase=3;stats.issued=2;ui_thread=GetCurrentThreadId();
     memset(st_fm,0,sizeof(st_fm));st_fm_kept=0;
     static __declspec(align(16)) unsigned char ships[2][0x20],agents[2][0x1000],formation[0x80],shape[0x80],out[0x40];
     static void *members[2];
     for(unsigned i=0;i<2;i++) {
         memset(agents[i],0x11*(i+1),sizeof(agents[i]));const uint64_t runtime=9101+i;memcpy(ships[i]+8,&runtime,8);
         void *ship=ships[i];memcpy(agents[i]+ST_AGENT_OWNER,&ship,8);members[i]=agents[i];
     }
     const uint64_t b=(uintptr_t)members,e=b+sizeof(members);memset(formation,0,sizeof(formation));
     memcpy(formation+ST_FM_MEMBERS,&b,8);memcpy(formation+ST_FM_MEMBERS+8,&e,8);
     for(unsigned i=0;i<sizeof(shape);i++) shape[i]=(unsigned char)(0x40+i);
     void *vt=image+0x2c4f610u,*sp=shape;memcpy(shape,&vt,8);memcpy(formation+ST_FM_SHAPE,&sp,8);
     float pose[31];for(unsigned i=0;i<31;i++) pose[i]=1.0f+(float)i;memcpy(agents[0]+ST_AGENT_POSE+ST_BANK_STRIDE,pose,sizeof(pose));
     const uint32_t prior_bank=*(uint32_t*)ST_BANK_READ;*(uint32_t*)ST_BANK_READ=1;
     const LONG64 hits6=st_chain_hits[6],hits7=st_chain_hits[7];
     memset(out,0x5a,sizeof(out));
     assert((((st_fm_fn)(void*)(stc_page+0x60))(formation,out,1,12.5)&0xff)==1);
     assert((((st_fm_fn)(void*)(stc_page+0x70))(formation,out,0,12.5)&0xff)==1); /* the leader: not kept */
     assert((((st_fm_fn)(void*)(stc_page+0x70))(formation,out,1,13.5)&0xff)==1);
     stats.issued=4;assert((((st_fm_fn)(void*)(stc_page+0x60))(formation,out,1,14.5)&0xff)==1); /* past the window */
     assert(st_chain_hits[6]==hits6+2 && st_chain_hits[7]==hits7+2 && st_fm_kept==2 && st_fm[0].calls==2);
     static st_record_t fr;st_fm_take(0,&fr);assert(!st_fm[0].calls);
     const uint32_t *f=fr.formation,*g=f+ST_FM_WORDS;float want[16];uint64_t k;double t;
     assert(f[0]==ST_FM_TAG && f[1]==2 && f[2]==0 && f[3]==1 && f[4]==2 && f[7]==1 && f[8]==0 && f[9]==1 && f[14]==0x2c4f610u && f[15]==0 && f[127]==2);
     memcpy(&k,f+10,8);assert(k==0x7dd7c);memcpy(&k,f+12,8);assert(k==0x7dcd0);memcpy(&t,f+5,8);assert(t==12.5);
     for(unsigned i=0;i<16;i++) want[i]=1.0f+(float)i;
     assert(!memcmp(f+16,want,0x40) && !memcmp(f+34,shape+8,0x78) && f[112]==st_bits(17.0f) && f[126]==st_bits(31.0f));
     for(unsigned i=0;i<16;i++) want[i]=(float)(10+i)+12.5f;
     assert(!memcmp(f+64,want,0x40) && f[80]==0x5a5a5a5au && f[96]==0x22222222u);
     for(unsigned i=0;i<16;i++) want[i]=(float)(10+i)+13.5f;
     assert(g[0]==ST_FM_TAG && g[2]==1 && g[15]==1 && g[127]==2 && !memcmp(g+64,want,0x40));
     for(unsigned i=0;i<ST_FM_WORDS;i++) assert(!f[2*ST_FM_WORDS+i]);
     /* FR18: the converter and the conversion behind stand-ins, entered as 0xe8c380 does with the agent in r15: the
      * watched wingman's call kept complete with its conversion; the other agent's, a conversion outside any call and a
      * call past the window not. */
     assert(VirtualProtect(image+(STC_PT&~0xfffu),0x2000,PAGE_EXECUTE_READWRITE,&old) && VirtualProtect(image+(STC_PT_MATH&~0xfffu),0x1000,PAGE_EXECUTE_READWRITE,&old));
     tsc_jump(STC_PT,(void*)tsc_pt);tsc_jump(STC_PT_MATH,(void*)tsc_pt_math);FlushInstructionCache(GetCurrentProcess(),image,nt.OptionalHeader.SizeOfImage);
     assert(world_identity_record(9103,0x1234) && world_identity_record(9104,0x5678));
     static __declspec(align(16)) unsigned char point[0x80],ref[0x300],frame[0x300],ref_controller[0x60],frame_controller[0x60],pt_out[0x40];
     const uint64_t ref_runtime=9103,frame_runtime=9104;void *rc=ref_controller,*fc=frame_controller,*parent=ships[0];
     for(unsigned i=0;i<sizeof(point);i++) point[i]=(unsigned char)(0x30+i);
     float m[16];for(unsigned i=0;i<16;i++) m[i]=0.5f*(float)i-3.0f;memcpy(point+0x20,m,0x40);memcpy(point+ST_PT_REF,&ref_runtime,8);
     memset(ref,0,sizeof(ref));memset(frame,0,sizeof(frame));memcpy(ref+8,&ref_runtime,8);memcpy(frame+8,&frame_runtime,8);
     memcpy(ref+ST_OBJ_CONTROLLER,&rc,8);memcpy(frame+ST_OBJ_CONTROLLER,&fc,8);memcpy(ref+ST_OBJ_PARENT,&parent,8);
     for(unsigned i=0;i<0x60;i++) {ref_controller[i]=(unsigned char)(0x80+i);frame_controller[i]=(unsigned char)(0xe0-i);}
     tsc_pt_ref=ref;memset(st_fm,0,sizeof(st_fm));st_pt_kept=st_pt_conversions=st_pt_no_slot=0;
     const LONG64 hits8=st_chain_hits[8],hits9=st_chain_hits[9];
     unsigned char *caller=tsc_pt_caller(stc_page+stc_stub(8));
     stats.issued=2;memset(pt_out,0x5a,sizeof(pt_out));
     assert((((tsc_pt_caller_fn)(void*)caller)(agents[1],pt_out,point,frame,ships[0],NULL)&0xff)==1 && tsc_pt_rcx==agents[1]);
     assert((((tsc_pt_caller_fn)(void*)caller)(agents[0],pt_out,point,frame,NULL,NULL)&0xff)==1 && tsc_pt_rcx==agents[0]); /* not watched */
     {void *r=ref,*fr=frame;((stc_pt_math_fn)(void*)(stc_page+stc_stub(9)))(pt_out,NULL,&r,&fr,1);} /* outside any call */
     stats.issued=4;assert((((tsc_pt_caller_fn)(void*)caller)(agents[1],pt_out,point,frame,NULL,NULL)&0xff)==1); /* past the window */
     assert(st_chain_hits[8]==hits8+3 && st_chain_hits[9]==hits9+4 && st_pt_kept==1 && st_pt_conversions==1 && !st_pt_no_slot && st_fm[0].pt_calls==1);
     assert(!st_pt_open && !st_pt_live());
     static st_record_t pr;st_fm_take(0,&pr);assert(!st_fm[0].pt_calls && !st_fm[0].lock);
     const uint32_t *p=pr.path;float plus[16];for(unsigned i=0;i<16;i++) plus[i]=m[i]+100.0f;
     assert(p[0]==ST_PT_TAG && p[1]==2 && p[2]==0 && p[3]==1 && p[4]==0 && p[5]==1 && p[16]==1 && p[17]==0 && p[18]==0 && p[191]==1);
     memcpy(&k,p+6,8);assert(k==0x7dd7c);memcpy(&k,p+8,8);assert(k==0x1234);memcpy(&k,p+10,8);assert(k==0x5678);
     memcpy(&k,p+12,8);assert(k==0x7dcd0);memcpy(&k,p+14,8);assert(!k);memcpy(&k,p+19,8);assert(k==0x1234);memcpy(&k,p+21,8);assert(k==0x5678);
     memcpy(&k,p+168,8);assert(k==0x7dcd0);memcpy(&k,p+170,8);assert(!k);
     assert(!memcmp(p+24,point,0x80) && !memcmp(p+56,plus,0x40) && !memcmp(p+72,m,0x40) && !memcmp(p+88,plus,0x40));
     assert(!memcmp(p+104,ref_controller,0x60) && !memcmp(p+128,frame_controller,0x60) && p[152]==0x22222222u && !p[172] && !p[190]);
     for(unsigned i=ST_PT_WORDS;i<ST_PT_ENTRIES*ST_PT_WORDS;i++) assert(!p[i]);
     assert(VirtualFree(caller,0,MEM_RELEASE));st_pt_kept=st_pt_conversions=0;
     /* FR19: SetTarget's slot holds 0xeaf620 in the file; relocated as the game would, a flipped pinned byte refuses,
      * then armed; the real SetTarget runs on its own bytes, called through a caller written into the int3 padding after
      * it (sub rsp,0x28; call [slot]; add rsp,0x28; ret: returns to 0xeaf65b): the watched ship's call kept, the other
      * ship's and one past the window not; then the slot restored. */
     {uint64_t file_slot;memcpy(&file_slot,image+ST_SG_SLOT,8);assert(file_slot==nt.OptionalHeader.ImageBase+ST_SG_FN);
      for(unsigned i=0x31;i<0x40;i++) assert(image[ST_SG_FN+i]==0xcc);
      void *fn=image+ST_SG_FN;memcpy(image+ST_SG_SLOT,&fn,8);
      image[ST_SG_FN+0x25]^=1;assert(!st_sg_arm() && !st_sg_installed);image[ST_SG_FN+0x25]^=1;
      assert(VirtualProtect(image+(ST_SG_FN&~0xfffu),0x1000,PAGE_EXECUTE_READWRITE,&old));
      assert(st_sg_arm() && st_sg_installed && *(void**)(image+ST_SG_SLOT)==(void*)st_settarget && st_sg_native==fn);
      unsigned char sg_code[15]={0x48,0x83,0xec,0x28,0xff,0x15,0,0,0,0,0x48,0x83,0xc4,0x28,0xc3};
      const int32_t disp=(int32_t)(ST_SG_SLOT-(ST_SG_FN+0x31+10));memcpy(sg_code+6,&disp,4);memcpy(image+ST_SG_FN+0x31,sg_code,15);
      FlushInstructionCache(GetCurrentProcess(),image+ST_SG_FN,0x60);
      typedef void (*tsg_fn)(void *,const void *,void *,void *);const tsg_fn set=(tsg_fn)(void*)(image+ST_SG_FN+0x31);
      static __declspec(align(16)) unsigned char sg_models[2][0x200];
      for(unsigned i=0;i<2;i++) {memset(sg_models[i],0,0x200);void *a=agents[i];memcpy(sg_models[i]+ST_MODEL_AGENT,&a,8);}
      void *pp=point;memcpy(agents[1]+ST_AGENT_POINT,&pp,8);
      memset((void*)st_fm,0,sizeof(st_fm));st_sg_calls=st_sg_kept=0;
      float target[16];for(unsigned i=0;i<16;i++) target[i]=7.0f*(float)i-1.5f;
      stats.issued=2;set(sg_models[1],target,ships[0],(void*)1);set(sg_models[0],target,NULL,NULL);
      stats.issued=4;set(sg_models[1],target,NULL,NULL);
      assert(st_sg_calls==3 && st_sg_kept==1 && st_fm[0].sg_calls==1 && !memcmp(sg_models[0]+ST_MODEL_TARGET,target,0x40));
      void *a3;memcpy(&a3,sg_models[1]+0x60,8);assert(a3==NULL && sg_models[1][0x6c]==0 && sg_models[1][0x6d]==1); /* the last call's */
      static st_record_t sr;st_fm_take(0,&sr);assert(!st_fm[0].sg_calls && !st_fm[0].lock);
      const uint32_t *g=sr.settarget;
      assert(g[0]==ST_SG_TAG && g[1]==2 && g[2]==ST_SG_FN+0x31+10 && g[3]==0 && g[4]==1 && g[5]==0 && g[14]==0 && g[127]==1);
      memcpy(&k,g+6,8);assert(k==0x7dd7c);memcpy(&k,g+8,8);assert(k==(uint64_t)(uintptr_t)ships[0]);memcpy(&k,g+10,8);assert(k==0x7dcd0);
      memcpy(&k,g+12,8);assert(k==1);
      assert(!memcmp(g+16,target,0x40) && !g[32] && !g[47] && !memcmp(g+52,point,0x80) && g[84]==0x22222222u && !memcmp(g+100,target,0x40));
      for(unsigned i=ST_SG_WORDS;i<ST_SG_ENTRIES*ST_SG_WORDS;i++) assert(!g[i]);
      st_sg_restore();assert(!st_sg_installed && *(void**)(image+ST_SG_SLOT)==fn);st_sg_calls=st_sg_kept=0;}
     *(uint32_t*)ST_BANK_READ=prior_bank;st_id_count=prior_ids;st_ticks=prior_ticks;stats.issued=prior_issued;stats.active=prior_active;boot_phase=prior_phase;
     ui_thread=prior_ui;st_fm_kept=0;
     VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior_identities;world_identity_count=prior_count;}
    /* Ownership, then the exact restoration. */
    image[stc_sites[5].site+1]^=1;assert(!st_chain_owned());image[stc_sites[5].site+1]^=1;assert(st_chain_owned());
    st_chain_restore();
    assert(!st_chain_installed && st_chain_restored && !stc_done && !memcmp(image+STC_BLOCK,block,STC_BLOCK_SIZE));
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) assert(stc_site_state(i)==0);
    assert(VirtualFree(stc_page,0,MEM_RELEASE));stc_page=saved_page;st_chain_restored=0;st_chain_stage="off";
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) st_chain_hits[i]=0;
    st_chain_recorded=0;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    printf("step chain native image: pins, ten swaps, view, approach helper 0xeb1db0 = the reading on 20000 (%u at the target), rows, "
           "FR17 formation calls kept (watched wingman, tick window, leader not kept), FR18 path point call kept complete "
           "(agent through r15, conversion inside), FR19 SetTarget slot (pin, real bytes, caller 0xeaf65b) kept, restoration PASS\n",at_target);
}
