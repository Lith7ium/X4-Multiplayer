/* Bounded copied diagnostics only. Double reads are not a lifetime lease and
 * cannot detect byte-identical ABA. No native calls, reference acquisition,
 * registry insertion, canonical identity, RNG access or engine writes. */
#define WUI_HOPS 8u
#define WUI_READS 512u
#define WUI_BYTES 16384u
#define WUI_SCRIPT_BYTES 364u
#define WUI_OBJECT_BYTES 376u
enum {WUI_INPUT=1,WUI_READ=2,WUI_BUDGET=4,WUI_STOP=8,WUI_SCRIPT=16,WUI_OWNER=32,WUI_CHANGED=64,WUI_BOUNDARY=128};
enum {WUI_REG_NONE,WUI_REG_OK,WUI_REG_READ,WUI_REG_ENTRY,WUI_REG_STATUS,WUI_REG_GENERATION,WUI_REG_POINTER,WUI_REG_ID};
enum {WUI_TYPE_NONE,WUI_TYPE_OK,WUI_TYPE_READ,WUI_TYPE_BOUNDS,WUI_TYPE_RTTI,WUI_TYPE_STUB,WUI_TYPE_CLASS};
enum {WUI_END_NONE,WUI_END_SAVED,WUI_END_NULL,WUI_END_CYCLE,WUI_END_LIMIT,WUI_END_UNSUPPORTED};
/* q: object,runtime,saved,parent,registry,page,block,object_id,vtable,COL,
 * type_descriptor,class_function,bitmap. d: fields,registry result,type result,
 * parent-read,name length. Raw immutable class/registry evidence is private. */
typedef struct {
    uint64_t q[13];uint32_t d[5];
    unsigned char name[128],code[36],bitmap[32],block[32],col[24];
} wui_object;
/* q: image,node,exec,frame,CURRENT,Value,context,lifetime,entity,runtime,
 * context vtable,descriptor,descriptor vtable,program,begin,end,PC,node vtable,
 * Execute,actual program-slot node. d: tag,version,name length,fields,error,
 * ancestry count,termination. */
typedef struct {
    uint64_t q[20];uint32_t d[7];unsigned char name[96],storage[32];
} wui_script;
typedef struct {wui_script script;wui_object owner,ancestors[WUI_HOPS];} wui_snapshot;
typedef struct {
    wui_snapshot before,after;unsigned reads,bytes,error,stopped,boundary;
    uint32_t phase_flags[2][4];uint64_t phase_clocks[2][4];
} wui_result;
typedef struct {
    void *context;
    int (*read)(void *,uint64_t,void *,size_t);
    uint64_t (*saved)(void *,uint64_t);
    int (*stopped)(void *);
    int (*class_stub)(void *,const unsigned char[36]);
} wui_api;
typedef struct {const wui_api *api;wui_result *result;uint64_t image;} wui_reader;
static uint32_t wui_u32(const void *p) {uint32_t x;memcpy(&x,p,4);return x;}
static uint64_t wui_u64(const void *p) {uint64_t x;memcpy(&x,p,8);return x;}
static int wui_read(wui_reader *r,uint64_t address,void *out,size_t size) {
    if(r->api->stopped(r->api->context)) {r->result->stopped=1;r->result->error|=WUI_STOP;return 0;}
    if(!address || !size || size-1>UINT64_MAX-address) {r->result->error|=WUI_READ;return 0;}
    if(r->result->reads>=WUI_READS || size>WUI_BYTES-r->result->bytes) {r->result->error|=WUI_BUDGET;return 0;}
    ++r->result->reads;r->result->bytes+=(unsigned)size;
    if(!r->api->read(r->api->context,address,out,size)) {r->result->error|=WUI_READ;return 0;}return 1;
}
static int wui_at(wui_reader *r,uint64_t base,size_t offset,void *out,size_t size) {
    if(!base || offset>UINT64_MAX-base) {r->result->error|=WUI_READ;return 0;}
    return wui_read(r,base+offset,out,size);
}
static int wui_image(const wui_reader *r,uint64_t address,size_t size) {
    return address>=r->image && size<=0x6fa9000u && address-r->image<=0x6fa9000u-size;
}
static int wui_name(wui_reader *r,uint64_t address,unsigned char text[128],unsigned *length) {
    for(unsigned at=0;at<128;at+=16) {
        if(!wui_image(r,address+at,16) || !wui_read(r,address+at,text+at,16)) return 0;
        for(unsigned j=at;j<at+16;j++) {
            if(!text[j]) {if(!j) return 0;*length=j;memset(text+j,0,128-j);return 1;}
            if(text[j]<33 || text[j]>126) return 0;
        }
    }
    return 0;
}
static unsigned wui_registry(wui_reader *r,wui_object *o) {
    unsigned entry=(unsigned)(o->q[1]&0x1ffffffu);
    if(!o->q[1] || !entry || !o->q[0]) return WUI_REG_ENTRY;
    if(!wui_at(r,r->image,0x6cf1500,&o->q[4],8)) return WUI_REG_READ;o->d[0]|=2;
    unsigned index=entry-1,page=index>>20,part=index&0xfffffu,lane=part%3;
    if(page>=32) return WUI_REG_ENTRY;
    if(!wui_at(r,o->q[4],0x58+page*8,&o->q[5],8)) return WUI_REG_READ;o->d[0]|=4;
    size_t offset=(size_t)(part/3)*32;
    if(!o->q[5] || offset>UINT64_MAX-o->q[5]) return WUI_REG_READ;o->q[6]=o->q[5]+offset;
    if(!wui_read(r,o->q[6],o->block,32)) return WUI_REG_READ;o->d[0]|=8;
    unsigned flags=wui_u32(o->block+28),generation=(wui_u32(o->block+24+(lane/2)*4)>>(16*(lane%2)))&0xffff;
    if(((flags>>(16+lane*4))&5)!=5) return WUI_REG_STATUS;
    if(generation!=((o->q[1]>>25)&0xffff)) return WUI_REG_GENERATION;
    if(wui_u64(o->block+lane*8)!=o->q[0]) return WUI_REG_POINTER;
    if(!wui_at(r,o->q[0],8,&o->q[7],8)) return WUI_REG_READ;o->d[0]|=16;
    return o->q[7]==o->q[1]?WUI_REG_OK:WUI_REG_ID;
}
static unsigned wui_type(wui_reader *r,wui_object *o,int entity) {
    if(!wui_read(r,o->q[0],&o->q[8],8)) return WUI_TYPE_READ;o->d[0]|=32;
    if(o->q[8]%8 || !wui_image(r,o->q[8],0x11e8) || o->q[8]<r->image+8) return WUI_TYPE_BOUNDS;
    if(!wui_read(r,o->q[8]-8,&o->q[9],8) || !wui_image(r,o->q[9],24) ||
       !wui_read(r,o->q[9],o->col,24)) return WUI_TYPE_READ;
    o->d[0]|=64;
    uint32_t td=wui_u32(o->col+12),hierarchy=wui_u32(o->col+16);
    if(wui_u32(o->col)!=1 || wui_u32(o->col+4) || wui_u32(o->col+8) ||
       wui_u32(o->col+20)!=o->q[9]-r->image || !td || !hierarchy || td>0x6fa9000u-144 || hierarchy>0x6fa9000u-16) return WUI_TYPE_RTTI;
    o->q[10]=r->image+td;
    if(!wui_name(r,o->q[10]+16,o->name,&o->d[4]) || o->name[0]!='.' || o->name[1]!='?' || o->name[2]!='A') return WUI_TYPE_RTTI;
    o->d[0]|=128;
    if(!wui_at(r,o->q[8],0x11e0,&o->q[11],8) || !wui_image(r,o->q[11],36) ||
       !wui_read(r,o->q[11],o->code,36)) return WUI_TYPE_READ;
    o->d[0]|=256;
    unsigned char normalized[36];memcpy(normalized,o->code,36);int32_t displacement;memcpy(&displacement,normalized+6,4);memset(normalized+6,0,4);
    if(!r->api->class_stub(r->api->context,normalized)) return WUI_TYPE_STUB;
    int64_t relative=(int64_t)(o->q[11]-r->image)+10+displacement;
    if(relative<0 || relative>0x6fa9000-32) return WUI_TYPE_BOUNDS;o->q[12]=r->image+(uint64_t)relative;
    if(!wui_read(r,o->q[12],o->bitmap,32)) return WUI_TYPE_READ;o->d[0]|=512;
    unsigned component=(wui_u32(o->bitmap+4*(22/16))>>(2*(22%16)))&3;
    unsigned is_entity=(wui_u32(o->bitmap+4*(44/16))>>(2*(44%16)))&3;
    return component && (!entity || is_entity)?WUI_TYPE_OK:WUI_TYPE_CLASS;
}
static int wui_object_read(wui_reader *r,wui_object *o,uint64_t object,int entity) {
    o->q[0]=object;if(!wui_at(r,object,8,&o->q[1],8)) return 0;o->d[0]|=1;
    o->d[1]=wui_registry(r,o);o->d[2]=wui_type(r,o,entity);
    if(o->d[1]!=WUI_REG_OK || o->d[2]!=WUI_TYPE_OK) return 0;
    o->q[2]=r->api->saved(r->api->context,o->q[1]);o->d[0]|=2048;
    if(!wui_at(r,object,0x70,&o->q[3],8)) return 0;o->d[0]|=1024;o->d[3]=1;return 1;
}
static int wui_script_read(wui_reader *r,wui_script *s,uint64_t node,uint64_t exec,uint64_t frame) {
    uint64_t *q=s->q;q[0]=r->image;q[1]=node;q[2]=exec;q[3]=frame;
    if(!node || !exec || exec>UINT64_MAX-0x38 || frame!=exec+0x38) return 0;
    if(!wui_at(r,r->image,0x38f4c60,&q[4],8) || q[4]!=exec ||
       !wui_at(r,exec,0x38,&s->d[0],4) || s->d[0]!=0x66 || !wui_at(r,exec,0x40,&q[5],8) || q[5]<0xa8) return 0;
    q[6]=q[5]-0xa8;
    if(!wui_at(r,q[6],8,&q[7],8) || !q[7] || !wui_read(r,q[6],&q[10],8) || q[10]!=r->image+0x2c6a270 ||
       !wui_at(r,q[6],0x120,&q[8],8) || !wui_at(r,q[8],8,&q[9],8) || !q[9]) return 0;
    s->d[3]|=1;
    if(!wui_at(r,q[6],0xd8,&q[11],8) || !wui_read(r,q[11],&q[12],8) || q[12]!=r->image+0x2c69030 ||
       !wui_at(r,q[11],0x58,&s->d[1],4) || !wui_at(r,q[11],0x10,s->storage,32)) return 0;
    uint64_t length=wui_u64(s->storage+16),capacity=wui_u64(s->storage+24),address;
    if(!length || length>95 || capacity<length || capacity>4096) return 0;
    address=capacity<16?q[11]+0x10:wui_u64(s->storage);
    if(!wui_read(r,address,s->name,(size_t)length+1) || s->name[length]) return 0;
    for(unsigned i=0;i<length;i++) if(s->name[i]<33 || s->name[i]>126) return 0;
    s->d[2]=(unsigned)length;s->d[3]|=2;
    if(!wui_at(r,exec,0x10,&q[13],8) || !wui_read(r,q[13],&q[14],8) || !wui_at(r,q[13],8,&q[15],8) ||
       !wui_at(r,exec,0x18,&q[16],8) || !q[14] || q[14]%8 || q[15]<q[14] || (q[15]-q[14])%8 ||
       (q[15]-q[14])/8>4096 || !q[16] || q[16]>(q[15]-q[14])/8 ||
       !wui_at(r,q[14],(size_t)(q[16]-1)*8,&q[19],8) || q[19]!=node ||
       !wui_read(r,node,&q[17],8) || !wui_image(r,q[17],0x20) ||
       !wui_at(r,q[17],0x18,&q[18],8) || !wui_image(r,q[18],1)) return 0;
    s->d[3]|=4;return 1;
}
static void wui_capture(wui_reader *r,wui_snapshot *s,uint64_t node,uint64_t exec,uint64_t frame) {
    if(!wui_script_read(r,&s->script,node,exec,frame)) {s->script.d[4]=WUI_SCRIPT;r->result->error|=WUI_SCRIPT;return;}
    if(!wui_object_read(r,&s->owner,s->script.q[8],1) || s->owner.q[1]!=s->script.q[9] || s->owner.q[2]) {
        s->script.d[4]=WUI_OWNER;s->script.d[6]=WUI_END_UNSUPPORTED;r->result->error|=WUI_OWNER;return;
    }
    uint64_t parent=s->owner.q[3];
    for(unsigned i=0;parent && i<WUI_HOPS;i++) {
        for(unsigned j=0;j<=i;j++) if(parent==(j?s->ancestors[j-1].q[0]:s->owner.q[0])) {s->script.d[6]=WUI_END_CYCLE;return;}
        s->script.d[5]=i+1;wui_object *a=&s->ancestors[i];
        if(!wui_object_read(r,a,parent,0)) {s->script.d[6]=WUI_END_UNSUPPORTED;return;}
        if(a->q[2]) {s->script.d[6]=WUI_END_SAVED;return;}parent=a->q[3];
    }
    s->script.d[6]=parent?WUI_END_LIMIT:WUI_END_NULL;
}
static void wui_observe(const wui_api *api,uint64_t image,uint64_t node,uint64_t exec,uint64_t frame,wui_result *out) {
    memset(out,0,sizeof(*out));
    if(!api || !api->read || !api->saved || !api->stopped || !api->class_stub || !image || image>UINT64_MAX-0x6fa9000u) {out->error=WUI_INPUT;return;}
    wui_reader reader={api,out,image};
    wui_capture(&reader,&out->before,node,exec,frame);wui_capture(&reader,&out->after,node,exec,frame);
    if(memcmp(&out->before,&out->after,sizeof(out->before))) out->error|=WUI_CHANGED;
    if(api->stopped(api->context)) {out->error|=WUI_STOP;out->stopped=1;}
}
/* Explicit little-endian serialization; native struct padding is never emitted. */
static void wui_put(unsigned char **p,uint64_t value,unsigned size) {for(unsigned i=0;i<size;i++) *(*p)++=(unsigned char)(value>>(8*i));}
static unsigned wui_encode(const wui_result *r,unsigned phase,unsigned part,unsigned char out[WUI_OBJECT_BYTES]) {
    const wui_snapshot *s=phase?&r->after:&r->before;unsigned char *p=out;
    if(!part) {
        for(unsigned i=0;i<20;i++) wui_put(&p,s->script.q[i],8);
        for(unsigned i=0;i<7;i++) wui_put(&p,s->script.d[i],4);
        memcpy(p,s->script.name,96);p+=96;memcpy(p,s->script.storage,32);p+=32;
        for(unsigned i=0;i<4;i++) wui_put(&p,r->phase_flags[phase][i],4);
        for(unsigned i=0;i<4;i++) wui_put(&p,r->phase_clocks[phase][i],8);
    } else {
        if(part>WUI_HOPS+1) return 0;
        const wui_object *o=part==1?&s->owner:&s->ancestors[part-2];
        for(unsigned i=0;i<13;i++) wui_put(&p,o->q[i],8);
        for(unsigned i=0;i<5;i++) wui_put(&p,o->d[i],4);
        memcpy(p,o->name,128);p+=128;memcpy(p,o->code,36);p+=36;memcpy(p,o->bitmap,32);p+=32;
        memcpy(p,o->block,32);p+=32;memcpy(p,o->col,24);p+=24;
    }
    return (unsigned)(p-out);
}
