/* Compare the production guard to the real pinned ced70 code executing only
 * on owned synthetic memory. No game process is loaded or modified. */
static unsigned char *identity_page,*identity_object,*identity_registry;
static uintptr_t identity_root;
static unsigned identity_reads,identity_change;
static uint64_t identity_runtime;
static void identity_xor32(unsigned char *p,unsigned mask) {unsigned value;memcpy(&value,p,4);value^=mask;memcpy(p,&value,4);}
static void identity_generation(unsigned char *b,unsigned lane,unsigned generation) {
    unsigned value,offset=0x18+(lane/2)*4,shift=16*(lane%2);memcpy(&value,b+offset,4);
    value=(value&~(0xffffu<<shift))|(generation<<shift);memcpy(b+offset,&value,4);
}
static int identity_reader(const void *p,void *out,size_t n) {
    if(p==&identity_root && ++identity_reads==2) {
        if(identity_change==1) *(uint64_t*)(identity_object+8)^=1ULL<<25;
        if(identity_change==2) *(uintptr_t*)identity_page=(uintptr_t)identity_object+16;
        if(identity_change==3) identity_xor32(identity_page+0x18,1);
    }
    return read_local(p,out,n);
}
static void test_transition_registry_identity(int actual_image) {
    identity_registry=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    identity_page=VirtualAlloc(NULL,0xb00000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    identity_object=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(identity_registry && identity_page && identity_object);identity_root=(uintptr_t)identity_registry;
    typedef void *(*native_lookup_fn)(void *,uint64_t,unsigned);
    native_lookup_fn native=(native_lookup_fn)(image+0xced70);DWORD protection=0;
    if(actual_image) {assert(transition_registry_pins());assert(VirtualProtect((void*)native,184,PAGE_EXECUTE_READWRITE,&protection));FlushInstructionCache(GetCurrentProcess(),(void*)native,184);}
    const unsigned indices[]={0,1,2,3,0xffffe,0xfffff,0x100000,0x1fffffe};
    unsigned checked=0;
    const unsigned generations[]={0,1,0x7fff,0x8000,0xffff};
    for(unsigned i=0;i<sizeof(indices)/sizeof(indices[0]);i++) for(unsigned g=0;g<sizeof(generations)/sizeof(generations[0]);g++) {
        unsigned generation=generations[g];
        unsigned index=indices[i],part=index&0xfffff,lane=part%3;unsigned char *b=identity_page+(part/3)*32;
        memset(b,0,32);*(void**)(identity_registry+0x58+(index>>20)*8)=identity_page;
        identity_runtime=((uint64_t)generation<<25)|(index+1);*(uint64_t*)(identity_object+8)=identity_runtime;
        *(void**)(b+lane*8)=identity_object;identity_generation(b,lane,generation);
        identity_xor32(b+0x1c,5u<<(16+lane*4));transition_registry_identity r;
        assert(transition_registry_read(read_local,&identity_root,identity_runtime,(uintptr_t)identity_object,&r));
        assert(r.lane==lane && r.generation==generation && r.object==(uintptr_t)identity_object);
        if(actual_image) assert(native(identity_registry,identity_runtime,4)==identity_object);
        /* Each independently rejects the actual native ID/type/generation. */
        const unsigned masks[]={1,4,5};
        for(unsigned j=0;j<3;j++) {
            identity_xor32(b+0x1c,masks[j]<<(16+lane*4));
            assert(!transition_registry_read(read_local,&identity_root,identity_runtime,(uintptr_t)identity_object,&r));
            if(actual_image) assert(!native(identity_registry,identity_runtime,4));
            identity_xor32(b+0x1c,masks[j]<<(16+lane*4));
        }
        identity_xor32(b+0x18+(lane/2)*4,1u<<(16*(lane%2)));
        assert(!transition_registry_read(read_local,&identity_root,identity_runtime,(uintptr_t)identity_object,&r));
        if(actual_image) assert(!native(identity_registry,identity_runtime,4));
        identity_xor32(b+0x18+(lane/2)*4,1u<<(16*(lane%2)));
        /* Native Find ignores high bits; the observer additionally requires
         * the full object ID, not merely a matching 16-bit generation. */
        *(uint64_t*)(identity_object+8)^=1ULL<<48;
        assert(!transition_registry_read(read_local,&identity_root,identity_runtime,(uintptr_t)identity_object,&r));
        if(actual_image) assert(native(identity_registry,identity_runtime,4)==identity_object);
        ++checked;
    }
    transition_registry_identity r;assert(!transition_registry_read(read_local,&identity_root,0,(uintptr_t)identity_object,&r));
    assert(!transition_registry_read(read_local,&identity_root,1ULL<<25,(uintptr_t)identity_object,&r));
    for(identity_change=1;identity_change<=3;identity_change++) {
        identity_reads=0;memset(identity_page,0,32);*(void**)(identity_registry+0x58)=identity_page;
        identity_runtime=(77ULL<<25)|1;*(uint64_t*)(identity_object+8)=identity_runtime;
        *(void**)identity_page=identity_object;identity_generation(identity_page,0,77);identity_xor32(identity_page+0x1c,5u<<16);
        assert(!transition_registry_read(identity_reader,&identity_root,identity_runtime,(uintptr_t)identity_object,&r));
        assert(identity_reads==2);
    }
    identity_change=0;DWORD old;assert(VirtualProtect(identity_page,4096,PAGE_NOACCESS,&old));
    assert(!transition_registry_read(read_local,&identity_root,identity_runtime,(uintptr_t)identity_object,&r));
    assert(VirtualProtect(identity_page,4096,old,&old));
    uintptr_t invalid=UINTPTR_MAX-8;assert(!transition_registry_read(read_local,&invalid,identity_runtime,(uintptr_t)identity_object,&r));
    if(actual_image) {DWORD ignored;assert(VirtualProtect((void*)native,184,protection,&ignored));}
    VirtualFree(identity_object,0,MEM_RELEASE);VirtualFree(identity_page,0,MEM_RELEASE);VirtualFree(identity_registry,0,MEM_RELEASE);
    printf("transition identity %u lane/page/generation cases, native Find comparison=%d, replacement/partial-read refusals PASS\n",checked,actual_image);
}
