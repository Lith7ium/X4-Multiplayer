/* Pure guarded reads of the exact ced70 component registry layout. This does
 * not acquire/retain the native object and is never an atomic lifetime lease. */
typedef int (*transition_reader)(const void *,void *,size_t);
typedef struct {
    uintptr_t registry,page,block,object;
    uint64_t runtime;
    unsigned index,lane,generation,status;
} transition_registry_identity;
static int transition_read_offset(transition_reader read,uintptr_t address,size_t offset,void *out,size_t length) {
    return address && offset<=UINTPTR_MAX-address && length<=UINTPTR_MAX-address-offset &&
        read((const void*)(address+offset),out,length);
}
static int transition_registry_once(transition_reader read,const void *root,uint64_t runtime,uintptr_t object,
                                    transition_registry_identity *out) {
    transition_registry_identity s={0};unsigned char block[32];uint32_t flags,generations;
    unsigned entry=(unsigned)(runtime&0x1ffffffu);
    if(!runtime || !entry || !object || !root || !read(root,&s.registry,8)) return 0;
    s.index=entry-1;unsigned page=s.index>>20,index=s.index&0xfffffu;
    if(page>=32 || !transition_read_offset(read,s.registry,0x58+page*8,&s.page,8)) return 0;
    size_t offset=(size_t)(index/3)*32;
    if(!s.page || offset>UINTPTR_MAX-s.page) return 0;s.block=s.page+offset;s.lane=index%3;
    if(!read((void*)s.block,block,sizeof(block))) return 0;
    memcpy(&s.object,block+s.lane*8,8);memcpy(&flags,block+0x1c,4);
    memcpy(&generations,block+0x18+(s.lane/2)*4,4);
    s.status=(flags>>(16+s.lane*4))&15;s.generation=(generations>>(16*(s.lane%2)))&0xffffu;
    if((s.status&5)!=5 || s.generation!=((runtime>>25)&0xffffu) || s.object!=object ||
       !transition_read_offset(read,object,8,&s.runtime,8) || s.runtime!=runtime) return 0;
    *out=s;return 1;
}
static __attribute__((unused)) int transition_registry_read(transition_reader read,const void *root,uint64_t runtime,
                                                           uintptr_t object,transition_registry_identity *out) {
    transition_registry_identity first,second;
    if(!transition_registry_once(read,root,runtime,object,&first) ||
       !transition_registry_once(read,root,runtime,object,&second) || memcmp(&first,&second,sizeof(first))) return 0;
    *out=second;return 1;
}
static __attribute__((unused)) int transition_registry_pins(void) {
    return hash_ok(image+0xced70,184,"3209f7be0616de6d0ecc20ce3cfd82fd16d0524c09635d86995210598b0a5f36");
}
