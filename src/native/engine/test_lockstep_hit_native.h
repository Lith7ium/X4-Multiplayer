/* Hit claims, native side, on fake engine memory: the real call-site patch and
 * stub executed with R14 = bullet through a hand-assembled caller, first-sight
 * keys, suppression, owner evaluation (incoming against the displayed pose,
 * outgoing shifted d ticks), proposal -> admission -> resolution with the
 * injected OnHit/cancel/kill, the keyed RNG word, an unwinding injection, the
 * stop check and restoration. Native image: pins, Kill slot, call site. */
typedef struct {unsigned char *component;float position[3];} test_hitn_body;
static test_hitn_body test_hitn_bodies[4];
static unsigned test_hitn_body_count,test_hitn_casts,test_hitn_onhits,test_hitn_cancels,test_hitn_kills,test_hitn_throw;
static float test_hitn_last_origin[4];
static float test_hitn_info[16],test_hitn_normal[4];
static void *test_hitn_hit_bullet,*test_hitn_hit_target,*test_hitn_kill_killer;
static unsigned test_hitn_cancel_code,test_hitn_kill_reason;
static double test_hitn_onhit_time,test_hitn_kill_time;
static uint64_t test_hitn_word_seen;
static volatile uint64_t test_hitn_word;
static unsigned char test_hitn_defaults[0x500];
static struct {uint64_t runtime;void *object;} test_hitn_objects[16];
static unsigned test_hitn_object_count;
/* The stack slots a5..a15 the caller passes (UpdateFlight's shape: an object,
 * 0, 0, byte 0, an unset slot, a qword, byte 1, dword, 7, dword, byte 1). Every
 * call - the original and the owner's extra casts - must see all of them. */
static uintptr_t test_hitn_tail[11];
static unsigned test_hitn_tail_checked;
/* The fake ray cast: hit the first body within 30 m of the origin, 5 m short of it. */
static uintptr_t test_hitn_cast(void *system,unsigned char *result,const float *origin,const float *ray,uintptr_t a5,uintptr_t a6,
                                uintptr_t a7,uintptr_t a8,uintptr_t a9,uintptr_t a10,uintptr_t a11,uintptr_t a12,uintptr_t a13,
                                uintptr_t a14,uintptr_t a15) {
    const uintptr_t tail[11]={a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15};
    assert(system==(void*)0x5157 && ray && !memcmp(tail,test_hitn_tail,sizeof(tail)));++test_hitn_tail_checked;
    ++test_hitn_casts;memcpy(test_hitn_last_origin,origin,16);
    for(unsigned i=0;i<test_hitn_body_count;i++) {
        const test_hitn_body *b=&test_hitn_bodies[i];float d2=0;
        for(unsigned k=0;k<3;k++) {float d=origin[k]-b->position[k];d2+=d*d;}
        if(d2<30.0f*30.0f) {
            float point[4]={b->position[0],b->position[1],b->position[2]-5.0f,0},normal[4]={0,0,-1,0},fraction=0.5f;
            memcpy(result,point,16);memcpy(result+0x10,normal,16);memcpy(result+0x20,&fraction,4);memcpy(result+0x28,&b->component,8);
            return 1;
        }
    }
    return 0;
}
static void test_hitn_onhit(void *bullet,void *target,const float *info,const float *normal,double time) {
    ++test_hitn_onhits;test_hitn_hit_bullet=bullet;test_hitn_hit_target=target;memcpy(test_hitn_info,info,64);memcpy(test_hitn_normal,normal,16);
    test_hitn_onhit_time=time;test_hitn_word_seen=test_hitn_word;
    if(test_hitn_throw) RaiseException(0xe0525233,0,0,NULL);
}
static void test_hitn_cancel(void *bullet,unsigned code) {assert(bullet==test_hitn_hit_bullet);++test_hitn_cancels;test_hitn_cancel_code=code;}
static void test_hitn_kill(void *bullet,unsigned reason,void *killer,double time) {
    assert(bullet==test_hitn_hit_bullet);++test_hitn_kills;test_hitn_kill_reason=reason;test_hitn_kill_killer=killer;test_hitn_kill_time=time;
}
static unsigned char *test_hitn_defaults_of(uintptr_t macro) {assert(macro==0x1234);return test_hitn_defaults;}
static void *test_hitn_find(void *map,uint64_t runtime,unsigned flags) {
    assert(!map && flags==4);
    for(unsigned i=0;i<test_hitn_object_count;i++) if(test_hitn_objects[i].runtime==runtime) return test_hitn_objects[i].object;
    return NULL;
}
typedef struct {unsigned char object[0x300],controller[0x100],slots[2*0x1a0];} test_hitn_ship;
static void test_hitn_ship_make(test_hitn_ship *s,uint64_t runtime,void *zone,float x,float z) {
    memset(s,0,sizeof(*s));unsigned char *controller=s->controller,*slots=s->slots;
    memcpy(s->object+8,&runtime,8);memcpy(s->object+0x70,&zone,8);memcpy(s->object+0x200,&controller,8);
    uintptr_t vtable=(uintptr_t)(image+HITN_NAV_VTABLE);memcpy(controller,&vtable,8);memcpy(controller+0x30,&slots,8);
    float bank[24]={x,0,z,1, 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,50,0, 0,0,0,0};memcpy(slots,bank,sizeof(bank));
    test_hitn_objects[test_hitn_object_count].runtime=runtime;test_hitn_objects[test_hitn_object_count++].object=s->object;
}
typedef struct {unsigned char object[0x300],controller[0x100];} test_hitn_bullet;
static void test_hitn_bullet_make(test_hitn_bullet *b,uint64_t runtime,void *zone,void *weapon,const float position[3],float speed,double now) {
    memset(b,0,sizeof(*b));unsigned char *controller=b->controller;uintptr_t vtable=(uintptr_t)(image+HITN_BULLET_VTABLE),linear=(uintptr_t)(image+HITN_LINEAR_VTABLE);
    uintptr_t macro=0x1234;
    memcpy(b->object,&vtable,8);memcpy(b->object+8,&runtime,8);memcpy(b->object+0x60,&macro,8);memcpy(b->object+0x70,&zone,8);
    memcpy(b->object+0x200,&controller,8);memcpy(b->object+0x2a0,&weapon,8);
    memcpy(controller,&linear,8);memcpy(controller+0x20,position,12);memcpy(controller+0x60,&now,8);
    float velocity[3]={0,0,speed};memcpy(controller+0x80,velocity,12);
    test_hitn_objects[test_hitn_object_count].runtime=runtime;test_hitn_objects[test_hitn_object_count++].object=b->object;
}
/* push r14; mov r14,imm64; sub rsp,0x80; eleven times mov rax,imm64; mov [rsp+0x20+8i],rax (the stack
 * slots a5..a15 as UpdateFlight stores them); call rel32; add rsp,0x80; pop r14; ret. rcx/rdx/r8/r9 pass
 * through from the C caller. */
typedef uintptr_t (*test_hitn_caller_fn)(void *,void *,const float *,const float *);
static test_hitn_caller_fn test_hitn_caller(unsigned char *code,void *bullet,void *zone,unsigned char *target) {
    unsigned n=0;
    const uintptr_t tail[11]={(uintptr_t)zone,0,0,0x1100,0xa9a9a9a9a9a9a9a9ULL,(uintptr_t)0x7ffd12345678ULL,0x2201,
                              0xffffffff00000c11ULL,7,0x00000000ffffffffULL,0x3301};
    memcpy(test_hitn_tail,tail,sizeof(tail));
    code[n++]=0x41;code[n++]=0x56;code[n++]=0x49;code[n++]=0xbe;memcpy(code+n,&bullet,8);n+=8;
    code[n++]=0x48;code[n++]=0x81;code[n++]=0xec;code[n++]=0x80;code[n++]=0;code[n++]=0;code[n++]=0;
    for(unsigned i=0;i<11;i++) {
        code[n++]=0x48;code[n++]=0xb8;memcpy(code+n,&tail[i],8);n+=8;
        code[n++]=0x48;code[n++]=0x89;code[n++]=0x44;code[n++]=0x24;code[n++]=(unsigned char)(0x20+8*i);
    }
    code[n++]=0xe8;int32_t rel=(int32_t)((intptr_t)target-(intptr_t)(code+n+4));memcpy(code+n,&rel,4);n+=4;
    code[n++]=0x48;code[n++]=0x81;code[n++]=0xc4;code[n++]=0x80;code[n++]=0;code[n++]=0;code[n++]=0;
    code[n++]=0x41;code[n++]=0x5e;code[n++]=0xc3;
    FlushInstructionCache(GetCurrentProcess(),code,n);
    return (test_hitn_caller_fn)code;
}
static void test_hitn_native_image(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;IMAGE_SECTION_HEADER sections[32];
    assert(fread(&dos,1,sizeof(dos),f)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && !fseek(f,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),f)==sizeof(nt) && nt.FileHeader.TimeDateStamp==0x6a1eaded && nt.FileHeader.NumberOfSections<=32);
    assert(fread(sections,sizeof(*sections),nt.FileHeader.NumberOfSections,f)==nt.FileHeader.NumberOfSections);
    static unsigned char bytes[0x200];
    #define TEST_HITN_READ(rva,size) do { unsigned offset_=0; \
        for(unsigned j_=0;j_<nt.FileHeader.NumberOfSections;j_++) \
            if((rva)>=sections[j_].VirtualAddress && (rva)+(size)<=sections[j_].VirtualAddress+sections[j_].SizeOfRawData) \
                offset_=sections[j_].PointerToRawData+(rva)-sections[j_].VirtualAddress; \
        assert(offset_ && !fseek(f,(long)offset_,SEEK_SET) && fread(bytes,1,(size),f)==(size)); } while(0)
    for(unsigned i=0;i<sizeof(hitn_pins_list)/sizeof(*hitn_pins_list);i++) {
        assert(hitn_pins_list[i].size<=sizeof(bytes));TEST_HITN_READ(hitn_pins_list[i].rva,hitn_pins_list[i].size);
        assert(hash_ok(bytes,hitn_pins_list[i].size,hitn_pins_list[i].sha));
        bytes[0]^=1;assert(!hash_ok(bytes,hitn_pins_list[i].size,hitn_pins_list[i].sha));
    }
    TEST_HITN_READ(HITN_BLOCK,16);assert(!memcmp(bytes,hitn_block_original,16) && bytes[1]==0xe8);
    int32_t rel;memcpy(&rel,bytes+2,4);assert(HITN_SITE+5+(unsigned)rel==HITN_CAST);
    TEST_HITN_READ(0xd4f292,3);assert(bytes[0]==0x4c && bytes[1]==0x8b && bytes[2]==0xf1); /* mov r14, rcx */
    TEST_HITN_READ(HITN_BULLET_VTABLE+HITN_KILL_SLOT,8);uint64_t kill;memcpy(&kill,bytes,8);
    assert(kill==nt.OptionalHeader.ImageBase+HITN_KILL);
    #undef TEST_HITN_READ
    fclose(f);
    puts("pinned native image hit claims: cast/OnHit/cancel/Kill/defaults/Find pins, call block, R14 setup and Kill slot verified");
}
static void test_hit_native(void) {
    /* Fake engine entry points (absolute jumps on executable fake-image bytes). */
    const struct {unsigned rva;void *target;} entries[]={{HITN_CAST,(void*)test_hitn_cast},{HITN_ONHIT,(void*)test_hitn_onhit},
        {HITN_CANCEL,(void*)test_hitn_cancel},{HITN_KILL,(void*)test_hitn_kill},{HITN_DEFAULTS,(void*)test_hitn_defaults_of}};
    DWORD protection;
    for(unsigned i=0;i<sizeof(entries)/sizeof(*entries);i++) {
        assert(VirtualProtect(image+entries[i].rva,16,PAGE_EXECUTE_READWRITE,&protection));thunk_absolute_jump(image+entries[i].rva,entries[i].target);
    }
    assert(VirtualProtect(image+0x1000,0x200,PAGE_EXECUTE_READWRITE,&protection));
    memcpy(image+HITN_BLOCK,hitn_block_original,16);
    uintptr_t kill=(uintptr_t)(image+HITN_KILL);memcpy(image+HITN_BULLET_VTABLE+HITN_KILL_SLOT,&kill,8);
    memset(test_hitn_defaults,0,sizeof(test_hitn_defaults));unsigned limit=1;memcpy(test_hitn_defaults+0x354,&limit,4);
    hitn_find_override=test_hitn_find;hitn_now_override=100.0;hitn_word_override=&test_hitn_word;
    unsigned saved_index=*(unsigned*)(image+0x38f4ee4),saved_slot=stats.slot;uint64_t saved_issued=stats.issued;int saved_active=stats.active;
    DWORD saved_ui=ui_thread;int saved_identity=world_identity_installed;
    *(unsigned*)(image+0x38f4ee4)=0;ui_thread=GetCurrentThreadId();stats.active=1;stats.issued=0;stats.slot=1;world_identity_installed=1;
    int owned_identities=!world_identities;
    if(owned_identities) world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    /* Zone, three ships (own = slot 1, slot 2, an NPC), two weapons, a station part. */
    static unsigned char zone[0x100],weapon_npc[0x100],weapon_own[0x100],station[0x300];
    static test_hitn_ship own,other,npc;test_hitn_object_count=0;
    test_hitn_ship_make(&own,0xa001,zone,0,0);test_hitn_ship_make(&other,0xa002,zone,500,0);test_hitn_ship_make(&npc,0xa003,zone,0,1000);
    unsigned char *npc_object=npc.object,*own_object=own.object,*zone_=zone;
    memcpy(weapon_npc+8,&(uint64_t){0xc001},8);memcpy(weapon_npc+0x70,&npc_object,8);
    memcpy(weapon_own+8,&(uint64_t){0xc002},8);memcpy(weapon_own+0x70,&own_object,8);
    memcpy(station+8,&(uint64_t){0xd001},8);memcpy(station+0x70,&zone_,8);
    assert(world_identity_record(0xa001,0x5001) && world_identity_record(0xa002,0x5002) && world_identity_record(0xa003,0x5003) &&
           world_identity_record(0xc001,0x7001) && world_identity_record(0xc002,0x7002));
    memset(pilot_slots,0,sizeof(pilot_slots));pilot_slot_count=2;pilot_source=PILOT_SOURCE_WIRE;pilot_delay=3;pilot_armed=1;
    pilot_slots[0]=(pilot_slot){.saved=0x5001,.runtime=0xa001,.object=own.object,.resolved=1};
    pilot_slots[1]=(pilot_slot){.saved=0x5002,.runtime=0xa002,.object=other.object,.resolved=1};
    test_hitn_body_count=0;
    test_hitn_bodies[test_hitn_body_count++]=(test_hitn_body){own.object,{0,0,0}};
    test_hitn_bodies[test_hitn_body_count++]=(test_hitn_body){npc.object,{0,0,1000}};
    test_hitn_bodies[test_hitn_body_count++]=(test_hitn_body){station,{2000,0,0}};
    /* Arming: refused without the pins (fake image), then armed with the seam. */
    hitn_pins_checked=0;assert(!hitn_arm(HIT_POLICY_SHOOTER) && hitn_pins_checked==-1 && !hitn_installed);
    hitn_pins_checked=1;assert(!hitn_arm(0) && hitn_arm(HIT_POLICY_SHOOTER) && hitn_installed && hitn_live && hitn_owned() && hitn_check());
    assert(hitn_arm(HIT_POLICY_SHOOTER) && !hitn_arm(HIT_POLICY_VICTIM)); /* identical rerun only */
    unsigned char now[16];memcpy(now,image+HITN_BLOCK,16);int32_t rel;memcpy(&rel,now+2,4);
    assert(now[1]==0xe8 && image+HITN_SITE+5+rel==hitn_page && !memcmp(now+6,hitn_block_original+6,10) && now[0]==0xc8);
    assert(hitn_page[0]==0x4c && hitn_page[1]==0x89 && hitn_page[2]==0x35 && hitn_page[7]==0xff && hitn_page[8]==0x25 && hitn_stub_intact());
    unsigned char *stub_target=image+HITN_SITE+5+rel;
    /* Tick 10: an NPC bullet 20 m from the own ship hits it natively: suppressed on
     * every client; the owner (no prediction armed) claims it. */
    stats.issued=10;static test_hitn_bullet b1,b2,b3,b4;
    test_hitn_bullet_make(&b1,0xb001,zone,weapon_npc,(const float[3]){0,0,20},-1200.0f,100.0);
    float origin[4]={0,0,20,1},ray[4]={0,0,-25,0};unsigned char result[0x40];
    memset(result,0,sizeof(result));*(float*)(result+0x20)=-1.0f;
    test_hitn_caller_fn call=test_hitn_caller(image+0x1000,b1.object,zone,stub_target);
    unsigned casts=test_hitn_casts,tails=test_hitn_tail_checked;
    call((void*)0x5157,result,origin,ray);
    assert(test_hitn_casts==casts+2 && *(float*)(result+0x20)==-1.0f && !*(void**)(result+0x28)); /* native + owner evaluation */
    /* Both saw all fifteen arguments unchanged (the H1 crash: only eight were forwarded). */
    assert(test_hitn_tail_checked==tails+2);
    assert(hitn_casts==1 && hitn_keyed==1 && hitn_suppressed_in==1 && hitn_claims_in==1 && hitn_queue_count==1);
    hitn_projectile *p1=hitn_by_key(hitn_queue[0].claim.projectile);
    assert(p1 && p1->object==b1.object && p1->shooter==0x7001 && !p1->shooter_slot && p1->key==hit_projectile_key(10,0x7001,0,0));
    assert(hitn_queue[0].claim.kind==HIT_INCOMING && hitn_queue[0].claim.target==0x5001 &&
           hitn_queue[0].claim.point[2]==-40 && !hitn_queue[0].claim.point[0]); /* 5 m in front, 1/8 m */
    /* A second bullet of the same weapon in the same tick: ordinal 1. An NPC bullet hitting the NPC ship is not touched. */
    test_hitn_bullet_make(&b2,0xb002,zone,weapon_npc,(const float[3]){0,0,1010},-1200.0f,100.0);
    float origin2[4]={0,0,1010,1};memset(result,0,sizeof(result));*(float*)(result+0x20)=-1.0f;
    call=test_hitn_caller(image+0x1000,b2.object,zone,stub_target);call((void*)0x5157,result,origin2,ray);
    assert(*(float*)(result+0x20)==0.5f && *(void**)(result+0x28)==npc.object && hitn_suppressed_in==1);
    assert(hitn_by_key(hit_projectile_key(10,0x7001,0,1))->object==b2.object);
    /* The claimed bullet is cast again later in the tick: suppressed again, never claimed twice. */
    memset(result,0,sizeof(result));*(float*)(result+0x20)=-1.0f;casts=test_hitn_casts;
    call=test_hitn_caller(image+0x1000,b1.object,zone,stub_target);call((void*)0x5157,result,origin,ray);
    assert(test_hitn_casts==casts+1 && hitn_suppressed_in==2 && hitn_claims_in==1 && hitn_queue_count==1);
    /* The own ship's bullet 80 m short of the NPC ship: no native hit, but the
     * owner's cast shifted by 3 ticks of flight (60 m) hits: an outgoing claim. */
    test_hitn_bullet_make(&b3,0xb003,zone,weapon_own,(const float[3]){0,0,920},1200.0f,100.0);
    float origin3[4]={0,0,920,1},ray3[4]={0,0,25,0};memset(result,0,sizeof(result));*(float*)(result+0x20)=-1.0f;
    call=test_hitn_caller(image+0x1000,b3.object,zone,stub_target);call((void*)0x5157,result,origin3,ray3);
    assert(*(float*)(result+0x20)==-1.0f && fabsf(test_hitn_last_origin[2]-980.0f)<1e-3f && hitn_claims_out==1 && hitn_queue_count==2);
    assert(hitn_queue[1].claim.kind==HIT_OUTGOING && hitn_queue[1].claim.target==0x5003 && hitn_queue[1].claim.point[2]==-40);
    /* The own ship's bullet hitting a station part natively: not claimable, stays native. */
    test_hitn_bullet_make(&b4,0xb004,zone,weapon_own,(const float[3]){2000,0,10},1200.0f,100.0);
    float origin4[4]={2000,0,10,1};memset(result,0,sizeof(result));*(float*)(result+0x20)=-1.0f;
    call=test_hitn_caller(image+0x1000,b4.object,zone,stub_target);call((void*)0x5157,result,origin4,ray3);
    assert(*(void**)(result+0x28)==station && hitn_suppressed_out==0);
    /* Proposal, then admission on the next tick: incoming applies at 11, outgoing at 13. */
    unsigned char payload[INPUT_MUX_MAX];size_t length=0;
    assert(hitn_proposal(10,payload,sizeof(payload),&length) && length==1+2*HIT_CLAIM_BYTES && payload[0]==2 && !hitn_queue_count);
    assert(hitn_proposal(10,payload+100,sizeof(payload)-100,&length) && !length); /* drained */
    /* A budget for one claim proposes the oldest; the other waits; an old claim expires. */
    int16_t q[3]={0,0,8};
    hitn_candidate_add(HIT_INCOMING,b1.object,0x9001,0x5001,q,10);hitn_candidate_add(HIT_INCOMING,b2.object,0x9002,0x5001,q,10);
    hitn_candidate_add(HIT_INCOMING,b2.object,0x9002,0x5001,q,10); /* the same projectile once */
    unsigned char small[1+HIT_CLAIM_BYTES];assert(hitn_queue_count==2);
    assert(hitn_proposal(10,small,HIT_CLAIM_BYTES,&length) && !length && hitn_queue_count==2); /* no room */
    assert(hitn_proposal(10,small,sizeof(small),&length) && length==sizeof(small) && small[0]==1 && hitn_queue_count==1);
    hit_claim first;assert(hit_claim_decode(small+1,&first) && first.projectile==0x9001);
    assert(hitn_proposal(19,small,sizeof(small),&length) && !length && !hitn_queue_count && hitn_queue_expired==1); /* 10+8 < 19 */
    assert(hitn_claims_in==3);hitn_claims_in=1; /* the two queued by hand */
    hitn_proposal(10,payload,sizeof(payload),&length);
    length=1+2*HIT_CLAIM_BYTES;hit_claim decoded[HIT_CLAIMS_MAX];unsigned count=0;
    assert(hit_payload_decode(payload,(unsigned)length,decoded,&count) && count==2 && decoded[0].observe16==10);
    stats.issued=11;hitn_now_override=100.0+1.0/60.0;
    assert(hitn_admit(1,payload,(unsigned)length,11) && hitn_admitted==2 && !hitn_admit(3,payload,(unsigned)length,11) && hitn_admit_refused==1);
    unsigned char broken[8]={3,1,2,3};assert(!hitn_admit(1,broken,sizeof(broken),11) && hitn_admit_refused==2);
    char receipt[4096];test_hitn_word=0x1234;unsigned onhits=test_hitn_onhits;
    assert(hitn_resolve(11,receipt,sizeof(receipt)) && !strncmp(receipt,"HITS_1:11:1:",12) && strstr(receipt,"/applied"));
    assert(test_hitn_onhits==onhits+1 && test_hitn_hit_bullet==b1.object && test_hitn_hit_target==own.object && test_hitn_word==0x1234 &&
           test_hitn_word_seen==lockstep_seed(HIT_DOMAIN,11,decoded[0].projectile,0x5001,1,0x494e4a));
    assert(test_hitn_info[0]==0 && test_hitn_info[2]==-5.0f && test_hitn_info[4]==1.0f && test_hitn_info[9]==1.0f && test_hitn_info[14]==1.0f);
    assert(test_hitn_normal[2]==1.0f && test_hitn_onhit_time==100.0+1.0/60.0); /* against the bullet's flight (-z) */
    int hits=0;memcpy(&hits,b1.object+0x280,4);
    assert(hits==1 && test_hitn_cancels==1 && test_hitn_cancel_code==0xeb && test_hitn_kills==1 && test_hitn_kill_reason==2 &&
           test_hitn_kill_killer==own.object && test_hitn_kill_time==test_hitn_onhit_time && hitn_applied==1);
    assert(hitn_resolve(12,receipt,sizeof(receipt)) && !strncmp(receipt,"HITS_1:12:0:0000000000000000",28));
    /* Tick 13: the outgoing claim; the bullet has flown 3 ticks (60 m) to the claimed point. */
    stats.issued=13;hitn_now_override=100.0+3.0/60.0;
    assert(hitn_resolve(13,receipt,sizeof(receipt)) && strstr(receipt,"HITS_1:13:1:") && strstr(receipt,"/applied"));
    assert(test_hitn_hit_bullet==b3.object && test_hitn_hit_target==npc.object && hitn_applied==2);
    /* A claim for a projectile the engine no longer finds is refused; nothing is applied. */
    hit_claim stale=decoded[0];stale.observe16=14;stale.projectile=hit_projectile_key(10,0x7001,0,1);
    test_hitn_objects[4].runtime=0; /* b2 gone */
    unsigned char single[1+HIT_CLAIM_BYTES];assert(hit_payload_encode(&stale,1,single,sizeof(single)));
    stats.issued=15;assert(hitn_admit(1,single,sizeof(single),15) && hitn_resolve(15,receipt,sizeof(receipt)) && strstr(receipt,"/projectile"));
    assert(hitn_applied==2 && test_hitn_onhits==onhits+2);
    /* With the prediction armed, the owner tests against the displayed pose: a bullet
     * 45 m from the body (no native hit) hits the ship shown 45 m further on. */
    predict_armed=1;memset(predict_slots,0,sizeof(predict_slots));predict_slots[0].enabled=1;predict_slots[0].display_tick=19;
    predict_slots[0].display=(predict_pose){{45,0,0},{1,0,0,0,1,0,0,0,1}};
    stats.issued=20;hitn_now_override=101.0;static test_hitn_bullet b5;
    test_hitn_bullet_make(&b5,0xb005,zone,weapon_npc,(const float[3]){45,0,20},-1200.0f,101.0);
    float origin5[4]={45,0,20,1};memset(result,0,sizeof(result));*(float*)(result+0x20)=-1.0f;
    call=test_hitn_caller(image+0x1000,b5.object,zone,stub_target);call((void*)0x5157,result,origin5,ray);
    assert(*(float*)(result+0x20)==-1.0f && fabsf(test_hitn_last_origin[0])<1e-3f && hitn_claims_in==2 && hitn_queue_count==1);
    predict_armed=0;memset(predict_slots,0,sizeof(predict_slots));hitn_queue_count=0;
    /* An injection that unwinds restores the word and stops the gate (reason 111). */
    static test_hitn_bullet b6;stats.issued=30;hitn_now_override=102.0;
    test_hitn_bullet_make(&b6,0xb006,zone,weapon_npc,(const float[3]){0,0,20},-1200.0f,102.0);
    memset(result,0,sizeof(result));*(float*)(result+0x20)=-1.0f;
    call=test_hitn_caller(image+0x1000,b6.object,zone,stub_target);call((void*)0x5157,result,origin,ray);
    assert(hitn_proposal(30,payload,sizeof(payload),&length) && length);
    stats.issued=31;assert(hitn_admit(1,payload,(unsigned)length,31));
    test_hitn_throw=1;test_hitn_word=0x77;int caught=0;
    __try {hitn_resolve(31,receipt,sizeof(receipt));} __except(EXCEPTION_EXECUTE_HANDLER) {caught=1;}
    test_hitn_throw=0;assert(caught && test_hitn_word==0x77 && hitn_failures==1 && !hitn_check());
    hitn_failures=0;assert(hitn_check());
    /* A changed call block or handler stops the gate; restore puts the bytes back. */
    char status[2048];assert(hitn_status(status,sizeof(status)) && strstr(status,"armed=1 installed=1") && strstr(status," applied=2") &&
                             strstr(status,"policy=shooter"));
    image[HITN_BLOCK+12]^=1;assert(!hitn_check() && !strcmp(hitn_stage,"site_changed"));image[HITN_BLOCK+12]^=1;hitn_failures=0;
    hitn_restore();assert(!hitn_installed && hitn_restored && !memcmp(image+HITN_BLOCK,hitn_block_original,16) && !hitn_live);
    call=test_hitn_caller(image+0x1000,b1.object,zone,stub_target);casts=hitn_casts;
    call((void*)0x5157,result,origin,ray);assert(hitn_casts==casts && hitn_foreign>0); /* a late call through the stub passes through */
    /* Reset to default off. */
    hitn_armed=0;hitn_policy=0;hitn_pins_checked=0;hitn_stage="off";hit_reset();memset(hitn_table,0,sizeof(hitn_table));
    hitn_casts=hitn_foreign=hitn_keyed=hitn_unkeyed=hitn_full=hitn_suppressed_in=hitn_suppressed_out=hitn_eval_in=hitn_eval_out=0;
    hitn_claims_in=hitn_claims_out=hitn_candidates_dropped=hitn_proposals=hitn_admitted=hitn_admit_refused=hitn_resolved=hitn_applied=0;
    hitn_frames=hitn_receipt=hitn_last_tick=hitn_queue_expired=0;hitn_queue_count=0;hitn_find_override=NULL;hitn_now_override=0;hitn_word_override=NULL;
    memset(pilot_slots,0,sizeof(pilot_slots));pilot_slot_count=pilot_source=pilot_delay=0;pilot_armed=0;
    memset(image+HITN_BLOCK,0,16);
    for(unsigned i=0;i<sizeof(entries)/sizeof(*entries);i++) memset(image+entries[i].rva,0,16);
    *(unsigned*)(image+0x38f4ee4)=saved_index;stats.slot=saved_slot;stats.issued=saved_issued;stats.active=saved_active;
    ui_thread=saved_ui;world_identity_installed=saved_identity;
    if(owned_identities) {VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;}
    printf("hit claims native: call-site stub with R14, keys, suppression, owner evaluation (incoming/predicted/outgoing), "
           "proposal/admission/resolution, keyed injection, unwind, stop check and restore tests PASS\n");
}
