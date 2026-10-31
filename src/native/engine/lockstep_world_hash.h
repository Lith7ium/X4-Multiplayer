/* Read-only, same-tick census of imported UniverseIDs. This deliberately does
 * not claim coverage of new objects, arbitrary controllers, AI, cargo or MD.
 * Each object/domain has SHA-256, and sorted object hashes form domain roots.
 * Unsupported fields are reported explicitly, never hashed as raw pointers. */
#include <stdlib.h>
#include "lockstep_world_nav.h"
/* Optional created identities (lockstep_world_created.h, default off): their
 * rows follow every saved row (high bit set) and references resolve to them. */
static uint64_t world_identity_portable(uint64_t runtime);
static int created_enabled(void);
#define WORLD_HASH_CREATED_MAX 524288u /* CREATED_LIVE */
#define WORLD_HASH_DOMAINS 4u
#define WORLD_HASH_PAGE 12u
#define WORLD_TIMING_BYTES 96u
typedef struct {
    uint64_t runtime,saved,parent;
    unsigned type,flags;
    unsigned char hash[WORLD_HASH_DOMAINS][32];
    unsigned char timing[WORLD_TIMING_BYTES],timing_length;
    unsigned char nav_raw[WORLD_NAV_RAW_BYTES],nav_raw_length;
} world_hash_item;
typedef struct {unsigned char data[1024];unsigned length;} world_hash_data;
#define WORLD_HASH_WATCH_MAX 64u
typedef struct {uint64_t saved,runtime;void *object;world_hash_data data[WORLD_HASH_DOMAINS];} world_hash_watch;
static world_hash_watch world_hash_watches[WORLD_HASH_WATCH_MAX];
static unsigned world_hash_watch_count;
typedef struct {uintptr_t vtable;unsigned flags;} world_class_cache;
/* Every capture read goes through world_hash_reader: the guarded copy (sample_read) by default.
 * A capture reads each of the ~522k imported objects some ten times (Nav objects some 75), and a
 * ReadProcessMemory call each made a census capture take ~3.5 s. Tests compare it with read_local. */
static int (*world_hash_reader)(const void *,void *,size_t)=sample_read;
static world_hash_item *world_hash_items,*world_hash_created;
static unsigned *world_hash_changed;
static unsigned world_hash_count,world_hash_created_count,world_hash_changes,world_hash_alive,world_hash_class_unknown;
static unsigned world_hash_motion_unknown,world_hash_parent_unknown;
static uint64_t world_hash_tick,world_hash_scans,world_hash_failures;
static uint64_t world_hash_failure_id;
static unsigned world_hash_schema=1;
static const char *world_hash_stage="not_started";
static world_class_cache world_classes[512];
static unsigned world_class_count;
static char world_hash_summary[768],world_hash_state[384];
static BCRYPT_ALG_HANDLE world_sha_algorithm;
static BCRYPT_HASH_HANDLE world_sha_row,world_sha_roots[WORLD_HASH_DOMAINS];

/* The native bank number can differ after a different number of load/menu
 * swaps. Record it without changing either selector or any controller cursor. */
typedef struct {uint32_t read,write;} world_bank_phase;
static world_bank_phase world_banks_before,world_banks_after;
static int world_banks_read(world_bank_phase *phase) {
    return world_hash_reader(image+0x38f4ee4,&phase->read,4) &&
           world_hash_reader(image+0x2f4b6f4,&phase->write,4) &&
           phase->read<=1 && phase->write<=1 && phase->read+phase->write==1;
}

typedef struct {unsigned rva,length;const char *sha;} world_nav_pin;
static int world_nav_live_prefixes(void);
static const world_nav_pin world_nav_pins[]={
    {0xea2a20u,537u,"d301bea94b279ddce92ced403bafdd17a90ed2eb83c5cc5944cf2ae600fc4a4f"},
    {0xea35e0u,1319u,"55ee34c5ef450f0f308b7c55a920eb84e65c03d6c5f8c84b0079d914eaec691c"},
    {0xea4e90u,925u,"866c1a72a063e9f56e225683d74d234b724b7e3c696bbb1bcc4e8b90282d4f50"},
    {0xea4a00u,375u,"c55b28f9e7b1f10a740d2436077b498e29a6940031da6d5d0c04afeeb8fba117"},
    /* Existing Transition/creation hooks own only the first eight bytes.
     * world_nav_live_prefixes verifies original or exact resident thunk forms. */
    {0xea4c08u,646u,"3bb6417ac6b4c5a53fb99185b685b514f7d431f64ea1bacfd035c9957e6e9ea7"},
    {0xeac530u,582u,"7b02058e82f6d8acf92e189f5982eae449af06dc045fcb41be1d182ea374267c"},
    {0xeac380u,421u,"f2175c1111b65cb926d637d544d2ab5b3c20626de01588df983387c797935865"},
    {0xe85368u,1360u,"4ffffa48ec9049508f62919f72914d5c88149a1e4a1f291a3b9fd0433a2e2e32"},
    {0xe8f250u,10613u,"ae0d8b729a39a75f83b1c3d783585f587de1cc2e5eff846620f2f0c482504f52"},
    {0xeac170u,123u,"8487d9a8922f92b790f68759933ac3011a10746f828a634ffc5fcb68ab24bd36"}
};
/* The travel snapshot rewrites one displacement byte inside Agent_UpdatePathing;
 * pin the native layout with the original byte substituted, as world trace does. */
static int tsnap_unpatched_hash(unsigned rva,ULONG length,const char *expected);
static int world_nav_layout_pins(void) {
    if(!world_nav_live_prefixes()) return 0;
    for(unsigned i=0;i<sizeof(world_nav_pins)/sizeof(*world_nav_pins);i++)
        if(!tsnap_unpatched_hash(world_nav_pins[i].rva,world_nav_pins[i].length,world_nav_pins[i].sha)) return 0;
    return 1;
}

static int world_hash_watch_add(const char *text,size_t length) {
    if(!text || length!=16 || world_hash_scans || world_hash_watch_count==WORLD_HASH_WATCH_MAX) return 0;
    uint64_t saved=0;
    for(size_t i=0;i<length;i++) {
        unsigned digit=(unsigned)(text[i]>='0' && text[i]<='9'?text[i]-'0':text[i]>='a' && text[i]<='f'?text[i]-'a'+10:16);
        if(digit>15) return 0;saved=(saved<<4)|digit;
    }
    if(!saved) return 0;
    unsigned pos=0;while(pos<world_hash_watch_count && world_hash_watches[pos].saved<saved) ++pos;
    if(pos<world_hash_watch_count && world_hash_watches[pos].saved==saved) return 0;
    memmove(world_hash_watches+pos+1,world_hash_watches+pos,(world_hash_watch_count-pos)*sizeof(*world_hash_watches));
    memset(world_hash_watches+pos,0,sizeof(*world_hash_watches));world_hash_watches[pos].saved=saved;
    ++world_hash_watch_count;return 1;
}

static int world_hash_add(world_hash_data *b,const void *data,unsigned length) {
    if(length>sizeof(b->data)-b->length) return 0;
    memcpy(b->data+b->length,data,length);b->length+=length;return 1;
}
static int world_hash_read(world_hash_data *b,const void *source,unsigned length) {
    if(length>sizeof(b->data)-b->length || !world_hash_reader(source,b->data+b->length,length)) return 0;
    b->length+=length;return 1;
}
/* Class bitmap of a vtable: two bits per class ID (component 22, destructible
 * 36, object 73, positional 77, ...). 1 only for a verified pure stub. */
static int world_class_words(uintptr_t vtable,uint32_t words[8]) {
    uintptr_t function=0,bitmap;unsigned char code[36];int32_t relative;
    if(vtable>=(uintptr_t)image && vtable<=(uintptr_t)(image+0x6fa9000-0x11e8) &&
       world_hash_reader((void*)(vtable+0x11e0),&function,8) && function>=(uintptr_t)image &&
       function<=(uintptr_t)(image+0x6fa9000-sizeof(code)) && world_hash_reader((void*)function,code,sizeof(code))) {
        /* All verified pure IsRealClass stubs share this normalized body.
         * Decode its bitmap instead of invoking any engine getter. */
        memcpy(&relative,code+6,4);memset(code+6,0,4);bitmap=function+10+relative;
        if(hash_ok(code,sizeof(code),"95552e650460a140f624748f17306613061330a17be9794f9e1e1d21abfb2fa0") &&
           bitmap>=(uintptr_t)image && bitmap<=(uintptr_t)(image+0x6fa9000-32))
            return world_hash_reader((void*)bitmap,words,8*sizeof(*words));
    }
    return 0;
}
static int world_class_has(const uint32_t words[8],unsigned klass) {
    return klass<128 && ((words[klass/16]>>(2*(klass%16)))&3);
}
static unsigned world_class_flags(uintptr_t vtable) {
    for(unsigned i=0;i<world_class_count;i++) if(world_classes[i].vtable==vtable) return world_classes[i].flags;
    unsigned flags=0x80000000u;uint32_t words[8];
    if(world_class_words(vtable,words)) {
        const unsigned classes[3]={36,77,73};flags=0;
        for(unsigned i=0;i<3;i++) if(world_class_has(words,classes[i])) flags|=1u<<i;
    }
    if(world_class_count<512) world_classes[world_class_count++]=(world_class_cache){vtable,flags};
    return flags;
}
static uint64_t world_hash_reference(uint64_t runtime,unsigned *flags) {
    uint64_t saved=world_identity_portable(runtime);
    if(runtime && !saved) *flags|=16;return saved;
}
static int world_hash_vectors(world_hash_data *b,const void *source,unsigned vectors) {
    uint32_t words[24];if(vectors>6 || !world_hash_reader(source,words,vectors*16)) return 0;
    for(unsigned i=0;i<vectors;i++) if(!world_hash_add(b,words+i*4,12)) return 0;
    return 1;
}
static int world_hash_motion(void *object,unsigned char *controller,world_hash_data *motion,world_hash_data *timing,unsigned *flags,world_hash_item *item) {
    world_hash_stage="controller";
    if(!controller) {unsigned zero=0;*flags|=8;return world_hash_add(motion,&zero,4);}
    uintptr_t vtable;unsigned kind;
    if(!world_hash_reader(controller,&vtable,8)) return 0;
    vtable=pslot_canonical_vtable(vtable); /* a stock slot's PlayerController is a PlayerController */
    if(vtable<(uintptr_t)image || vtable>=(uintptr_t)(image+0x6fa9000)) return 0;
    kind=(unsigned)(vtable-(uintptr_t)image);if(!world_hash_add(motion,&kind,4)) return 0;
    if(kind==0x2c51158) {
        if(world_hash_schema==2) {
            world_nav_snapshot captured;unsigned char canonical[WORLD_NAV_TIMING_BYTES],movement[WORLD_NAV_MOTION_BYTES];
            world_hash_stage="nav_identity_missing";if(!item) return 0;
            if(!world_nav_capture(world_hash_reader,world_identity_portable,(uintptr_t)image,(uintptr_t)object,
                                  (uintptr_t)controller,item->runtime,&captured,&world_hash_stage)) return 0;
            world_hash_stage="nav_census_phase_changed";
            if(captured.links.read!=world_banks_before.read || captured.links.write!=world_banks_before.write) return 0;
            world_hash_stage="nav_attention_changed";
            if(timing->length!=4 || memcmp(timing->data,&captured.links.attention,4)) return 0;
            world_hash_stage="nav_object_type_changed";
            if(captured.links.object_vtable!=(uintptr_t)image+item->type) return 0;
            world_nav_encode(&captured,canonical,movement,item->nav_raw,flags);item->nav_raw_length=WORLD_NAV_RAW_BYTES;
            return world_hash_add(timing,canonical+4,sizeof(canonical)-4) && world_hash_add(motion,movement+4,sizeof(movement)-4);
        }
        world_hash_stage="navigation";
        unsigned char *slots;unsigned index;
        if(!world_hash_reader(controller+0x30,&slots,8) || !slots || !world_hash_reader(image+0x38f4ee4,&index,4) || index>1 ||
           !world_hash_read(timing,controller+0xc0,12) || !world_hash_read(timing,controller+0xd0,9)) return 0;
        for(unsigned i=0;i<2;i++) {
            unsigned char *state=slots+(i?1-index:index)*0x1a0;uint64_t refs[2];
            if(!world_hash_vectors(motion,state,6) || !world_hash_reader(state+0xb0,refs,16)) return 0;
            for(unsigned j=0;j<2;j++) refs[j]=world_hash_reference(refs[j],flags);
            if(!world_hash_add(motion,refs,16) || !world_hash_read(motion,state+0xcc,4) ||
               !world_hash_read(motion,state+0x15b,1) || !world_hash_read(timing,state+0x150,8) ||
               !world_hash_read(timing,state+0x159,2)) return 0;
        }
        return 1;
    }
    if(kind==DOCK_CONTROLLER_VTABLE || kind==UNDOCK_CONTROLLER_VTABLE) {
        world_hash_stage="dock_controller";
        char dock[512];return dock_controller_state(controller,object,dock) && world_hash_add(motion,dock,(unsigned)strlen(dock));
    }
    uintptr_t getter;
    if(!world_hash_reader((void*)(vtable+0x30),&getter,8)) return 0;
    if(getter==(uintptr_t)(image+0x10ba20)) return world_hash_vectors(motion,controller+0x10,4);
    if(getter==(uintptr_t)(image+0x46ff70)) return world_hash_vectors(motion,controller+0x20,4);
    /* Unknown controller identity is still visible; its state is not claimed. */
    *flags|=8;return 1;
}
static int world_hash_record(world_hash_item *item,world_hash_data data[WORLD_HASH_DOMAINS],unsigned char *object) {
    world_hash_stage="topology";
    item->type=item->flags=0;item->parent=0;item->nav_raw_length=0;
    if(object) {
        uint64_t id;uintptr_t vtable;unsigned char *parent;unsigned classes,mode;
        if(!world_hash_reader(object,&vtable,8) || vtable<(uintptr_t)image || vtable>=(uintptr_t)(image+0x6fa9000) ||
           !world_hash_reader(object+8,&id,8) || id!=item->runtime || !world_hash_reader(object+0x70,&parent,8) ||
           !world_hash_reader(object+0x68,&mode,4)) return 0;
        item->type=(unsigned)(vtable-(uintptr_t)image);item->flags=1;classes=world_class_flags(vtable);
        if(classes&0x80000000u) {item->flags|=32;++world_hash_class_unknown;}
        if(parent) {
            uint64_t parent_id;if(!world_hash_reader(parent+8,&parent_id,8)) return 0;
            item->parent=world_hash_reference(parent_id,&item->flags);
        }
        if(!world_hash_add(&data[0],&mode,4) || !world_hash_read(&data[0],object+0xd3,1) ||
           !world_hash_read(&data[3],object+0x1ec,4)) return 0;
        if(classes&1) {
            world_hash_stage="health";
            char shield[192];item->flags|=2;
            if(!world_hash_read(&data[1],object+0x280,8) || !shield_state_at(object,shield,shield_canonical?&shield_canonical_now:NULL) ||
               !world_hash_add(&data[1],shield,(unsigned)strlen(shield))) return 0;
        }
        if(classes&2) {
            unsigned char *controller;item->flags|=4;
            if(!world_hash_reader(object+0x200,&controller,8) || !world_hash_motion(object,controller,&data[2],&data[3],&item->flags,item)) return 0;
        }
        world_hash_stage="identity_recheck";
        if(!world_hash_reader(object+8,&id,8) || id!=item->runtime) return 0;
        ++world_hash_alive;
        if(item->flags&8) ++world_hash_motion_unknown;
        if(item->flags&16) ++world_hash_parent_unknown;
    }
    /* Absence, class and reference coverage are part of every domain. */
    for(unsigned domain=0;domain<WORLD_HASH_DOMAINS;domain++)
        if(!world_hash_add(&data[domain],&item->type,4) || !world_hash_add(&data[domain],&item->flags,4) ||
           !world_hash_add(&data[domain],&item->parent,8)) return 0;
    return 1;
}
static int world_hash_compare(const void *a,const void *b) {
    uint64_t x=((const world_hash_item*)a)->saved,y=((const world_hash_item*)b)->saved;
    return x<y?-1:x>y;
}
static int world_hash_init(void) {
    if(!world_identities || world_identity_count<=0 || world_identity_count>WORLD_IDENTITY_CAPACITY/2 ||
       !hash_ok(image+0xced70,32,"de126e38d2532250fc386f851f355cdb4812bf6b4507ace0549728e27505f268") || !dock_state_pins()) return 0;
    unsigned count=(unsigned)world_identity_count;
    world_hash_items=VirtualAlloc(NULL,sizeof(*world_hash_items)*count,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    world_hash_changed=VirtualAlloc(NULL,sizeof(*world_hash_changed)*(count+WORLD_HASH_CREATED_MAX),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!world_hash_items || !world_hash_changed) return 0;
    for(unsigned i=0;i<WORLD_IDENTITY_CAPACITY;i++) if(world_identities[i].runtime) {
        if(world_hash_count==count) return 0;
        world_hash_items[world_hash_count++]=(world_hash_item){.runtime=world_identities[i].runtime,.saved=world_identities[i].saved};
    }
    if(world_hash_count!=count) return 0;
    qsort(world_hash_items,count,sizeof(*world_hash_items),world_hash_compare);
    for(unsigned i=1;i<count;i++) if(world_hash_items[i-1].saved==world_hash_items[i].saved) return 0;
    if(created_enabled()) {
        /* Created rows sort after every saved row: the high bit is theirs. */
        world_hash_stage="created_saved_range";
        if(world_hash_items[count-1].saved>>63) return 0;
        world_hash_created=VirtualAlloc(NULL,sizeof(*world_hash_created)*WORLD_HASH_CREATED_MAX,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        if(!world_hash_created) return 0;
    }
    if(BCryptOpenAlgorithmProvider(&world_sha_algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)<0 ||
       BCryptCreateHash(world_sha_algorithm,&world_sha_row,NULL,0,NULL,0,BCRYPT_HASH_REUSABLE_FLAG)<0) return 0;
    for(unsigned i=0;i<WORLD_HASH_DOMAINS;i++) if(BCryptCreateHash(world_sha_algorithm,&world_sha_roots[i],NULL,0,NULL,0,BCRYPT_HASH_REUSABLE_FLAG)<0) return 0;
    return 1;
}
typedef void *(*world_hash_find_fn)(void *,uint64_t,unsigned);
static int created_capture_rows(world_hash_find_fn,void *,world_hash_item *,unsigned *,unsigned);
/* Saved rows first, then created rows; both runs are sorted by ID. */
static world_hash_item *world_hash_item_at(unsigned index) {
    return world_hash_created_count && index>=world_hash_count?&world_hash_created[index-world_hash_count]:&world_hash_items[index];
}
static int world_hash_capture_with(uint64_t tick,unsigned schema,world_hash_find_fn find) {
    world_hash_stage="schema";
    if((schema!=1 && schema!=2) || (world_hash_scans && schema!=world_hash_schema)) return 0;
    world_hash_schema=schema;
    if(shield_canonical) {
        world_hash_stage="shield_time";
        void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
        if(!tls || !tls[0] || !read_local((unsigned char*)tls[0]+0x300,&shield_canonical_now,8) || !isfinite(shield_canonical_now)) return 0;
    }
    world_hash_stage="nav_layout_pins";
    if(schema==2 && !world_nav_layout_pins()) return 0;
    world_hash_stage="initialization";
    if(!world_hash_items && !world_hash_init()) return 0;
    void *map=*(void**)(image+0x6cf1500);if(!map || world_hash_failures || (world_hash_scans && tick<=world_hash_tick)) return 0;
    if(world_hash_created) {
        world_hash_stage="created_rows";
        if(!created_capture_rows(find,map,world_hash_created,&world_hash_created_count,WORLD_HASH_CREATED_MAX)) return 0;
    }
    const unsigned total=world_hash_count+world_hash_created_count;
    world_hash_stage="bank_phase_before";
    if(!world_banks_read(&world_banks_before)) return 0;
    world_hash_changes=world_hash_alive=world_hash_class_unknown=world_hash_motion_unknown=world_hash_parent_unknown=0;
    /* Aggregate buffers contain only savedID + per-domain SHA, little endian. */
    unsigned char aggregate[WORLD_HASH_DOMAINS][40960];unsigned used=0,watch=0;
    for(unsigned i=0;i<total;i++) {
        world_hash_item *item=world_hash_item_at(i);unsigned char previous[WORLD_HASH_DOMAINS][32];
        unsigned char previous_raw[WORLD_NAV_RAW_BYTES],previous_raw_length=item->nav_raw_length;
        memcpy(previous_raw,item->nav_raw,sizeof(previous_raw));
        world_hash_failure_id=item->saved;
        memcpy(previous,item->hash,sizeof(previous));world_hash_data data[WORLD_HASH_DOMAINS]={0};
        unsigned char *object=find(map,item->runtime,4);
        if(!world_hash_record(item,data,object)) return 0;
        if(data[3].length<16 || data[3].length-16>WORLD_TIMING_BYTES) {world_hash_stage="timing_schema";return 0;}
        item->timing_length=(unsigned char)(data[3].length-16);
        memcpy(item->timing,data[3].data,item->timing_length);
        /* Retain the exact canonical bytes used in this scan, never a later
         * reread which could diagnose a different state than the SHA record. */
        if(watch<world_hash_watch_count && world_hash_watches[watch].saved==item->saved) {
            world_hash_watches[watch].runtime=item->runtime;world_hash_watches[watch].object=object;
            memcpy(world_hash_watches[watch].data,data,sizeof(data));++watch;
        }
        for(unsigned d=0;d<WORLD_HASH_DOMAINS;d++) {
            if(BCryptHashData(world_sha_row,data[d].data,data[d].length,0)<0 || BCryptFinishHash(world_sha_row,item->hash[d],32,0)<0) return 0;
            memcpy(aggregate[d]+used,&item->saved,8);memcpy(aggregate[d]+used+8,item->hash[d],32);
        }
        used+=40;
        if(!world_hash_scans || memcmp(previous,item->hash,sizeof(previous)) || previous_raw_length!=item->nav_raw_length ||
           memcmp(previous_raw,item->nav_raw,item->nav_raw_length)) world_hash_changed[world_hash_changes++]=i;
        if(used==sizeof(aggregate[0]) || i+1==total) {
            for(unsigned d=0;d<WORLD_HASH_DOMAINS;d++) if(BCryptHashData(world_sha_roots[d],aggregate[d],used,0)<0) return 0;
            used=0;
        }
    }
    if(watch!=world_hash_watch_count) {world_hash_failure_id=world_hash_watches[watch].saved;world_hash_stage="watch_identity_missing";return 0;}
    world_hash_stage="bank_phase_after";
    if(!world_banks_read(&world_banks_after) || memcmp(&world_banks_before,&world_banks_after,sizeof(world_banks_before))) return 0;
    char roots[WORLD_HASH_DOMAINS][65];unsigned char raw[32];
    for(unsigned d=0;d<WORLD_HASH_DOMAINS;d++) {if(BCryptFinishHash(world_sha_roots[d],raw,32,0)<0) return 0;hex32(raw,roots[d]);}
    world_hash_tick=tick;++world_hash_scans;world_hash_failure_id=0;world_hash_stage="complete";
    /* objects counts saved plus created rows; created= appears only when
     * created identities are enabled, so default captures are unchanged. */
    char created[32]="";if(world_hash_created) snprintf(created,sizeof(created)," created=%u",world_hash_created_count);
    snprintf(world_hash_state,sizeof(world_hash_state),"WORLD_IMPORTED_%u:%llu:%u:%s:%s:%s:%s",schema,(unsigned long long)tick,total,roots[0],roots[1],roots[2],roots[3]);
    snprintf(world_hash_summary,sizeof(world_hash_summary),"tick=%llu objects=%u present=%u changed=%u unknown_class=%u unknown_motion=%u unknown_references=%u%s state=%s",
        (unsigned long long)tick,total,world_hash_alive,world_hash_changes,world_hash_class_unknown,world_hash_motion_unknown,world_hash_parent_unknown,created,world_hash_state);
    return 1;
}
static int world_hash_capture(uint64_t tick,unsigned schema) {
    return world_hash_capture_with(tick,schema,(world_hash_find_fn)(image+0xced70));
}
