/* The static "module.cue" name of an MD cue, from the pinned cue-name tree. Shared by the NPC trace
 * and the cue identity (lockstep_cue_identity.h), on both platforms. */
/* Every read goes through read: read_local (a system call per read) for the
 * trace modules, or a caller's cheaper guarded copy on a per-entry hot path. */
typedef int (*npc_reader)(const void *,void *,size_t);
#ifdef NPC_STRING_LIBSTDCXX
static int npc_string_with(npc_reader read,uintptr_t address,char *out,size_t capacity) {
    uintptr_t data=0;uint64_t size=0; /* libstdc++: pointer, size */
    if(!read((void*)address,&data,8) || !read((void*)(address+8),&size,8)) return 0;
    if(!size || size>=capacity || size>1048576 || !data || !read((void*)data,out,(size_t)size)) return 0;
    for(size_t i=0;i<size;i++) if(!out[i]) return 0;
    out[size]=0;return 1;
}
#else
static int npc_string_with(npc_reader read,uintptr_t address,char *out,size_t capacity) {
    unsigned char str[32];uintptr_t data;uint64_t size,cap;
    if(!read((void*)address,str,32)) return 0;
    memcpy(&size,str+16,8);memcpy(&cap,str+24,8);
    if(!size || size>=capacity || cap<size || cap>1048576) return 0;
    if(cap<16) data=address;else memcpy(&data,str,8);
    if(!data || !read((void*)data,out,(size_t)size)) return 0;
    for(size_t i=0;i<size;i++) if(!out[i]) return 0;
    out[size]=0;return 1;
}
#endif
static int npc_encode(const char *raw,char *out,size_t capacity) {
    static const char hex[]="0123456789abcdef";size_t used=0;
    for(size_t i=0;raw[i];i++) {
        unsigned char c=(unsigned char)raw[i];
        if((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_' || c=='.' || c=='-') {
            if(used+1>=capacity) return 0;out[used++]=(char)c;
        } else {
            if(used+3>=capacity) return 0;out[used++]='%';out[used++]=hex[c>>4];out[used++]=hex[c&15];
        }
    }
    out[used]=0;return 1;
}
/* Return 1 for a named NPC module cue, 2 for a known different module/context,
 * and 0 for unreadable identity. A missing name must never silently drop a cue. */
static int npc_cue_name_with(npc_reader read,void *exec,char out[256],int npc_only) {
    uint32_t tag;uintptr_t value,definition,module;
    if(!read((unsigned char*)exec+0x38,&tag,4)) return 0;
    if(tag!=0x5d) return 2;
    if(!read((unsigned char*)exec+0x40,&value,8) || value<0xb0 ||
       !read((void*)(value-0xb0+0x160),&definition,8) || !definition ||
       !read((void*)(definition+0x150),&module,8) || !module) return 0;
    char module_name[96];if(!npc_string_with(read,module+0x18,module_name,sizeof(module_name))) return 0;
    int cache=!strcmp(module_name,"NPC_UseCases");
    if(npc_only && !cache) return 2;
    unsigned start=(unsigned)((definition>>4)%NPC_NAME_LIMIT),slot=start;
    for(unsigned count=0;cache && count<NPC_NAME_LIMIT;count++,slot=(slot+1)%NPC_NAME_LIMIT) {
        if(npc_names[slot].definition==definition) {strcpy(out,npc_names[slot].cue);return 1;}
        if(!npc_names[slot].definition) break;
        if(count+1==NPC_NAME_LIMIT) return 0;
    }
    uintptr_t sentinel=module+0x60,current;
    if(!read((void*)(sentinel+0x20),&current,8)) return 0;
    unsigned budget=8192;
    while(current!=sentinel) {
        uintptr_t candidate,right;
        if(!current || !budget-- || !read((void*)(current+0x40),&candidate,8)) return 0;
        if(candidate==definition) {
            char raw[192],encoded[224];if(!npc_string_with(read,current+0x20,raw,sizeof(raw)) || !npc_encode(raw,encoded,sizeof(encoded))) return 0;
            char encoded_module[288];if(!npc_encode(module_name,encoded_module,sizeof(encoded_module))) return 0;
            int n=snprintf(out,256,"%s.%s",encoded_module,encoded);if(n<0 || n>=256) return 0;
            if(cache) {npc_names[slot].definition=definition;strcpy(npc_names[slot].cue,out);}return 1;
        }
        if(!read((void*)(current+0x10),&right,8)) return 0;
        if(right) {
            current=right;
            for(;;) {
                uintptr_t left;if(!current || !budget-- || !read((void*)(current+8),&left,8)) return 0;
                if(!left) break;current=left;
            }
        } else {
            uintptr_t parent,left;
            if(!read((void*)current,&parent,8)) return 0;
            for(;;) {
                if(!parent || !budget-- || !read((void*)(parent+8),&left,8)) return 0;
                if(left==current) {current=parent;break;}
                current=parent;if(!read((void*)current,&parent,8)) return 0;
            }
        }
    }
    return 0;
}
