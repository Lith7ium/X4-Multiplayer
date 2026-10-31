/* Static cue name, cached per (definition, module): the tree walk is long. */
static unsigned script_rng_md(script_rng_identity *id) {
    uintptr_t cue=id->local,vtable=0,definition=0,module=0;
    if(!script_rng_read((void*)cue,&vtable,8)) return SR_UNREADABLE;
    if(vtable!=(uintptr_t)(image+SCRIPT_RNG_CUE_VT)) return SR_FOREIGN;
    if(!script_rng_read((void*)(cue+0x160),&definition,8) || !definition ||
       !script_rng_read((void*)(definition+0x150),&module,8) || !module) return SR_UNREADABLE;
    id->instance=definition!=cue;id->script=SCRIPT_RNG_MD_SCRIPT;
    unsigned first=(unsigned)(lockstep_mix(definition)%SCRIPT_RNG_NAMES);script_rng_name *slot=NULL;
    for(unsigned n=0;n<SCRIPT_RNG_PROBES;n++) {
        script_rng_name *e=&script_rng_names[(first+n)%SCRIPT_RNG_NAMES];
        if(e->definition==definition) {
            if(e->module==module) {id->owner=e->hash;memcpy(id->name,e->name,sizeof(id->name));return SR_OK;}
            slot=e;break; /* Same address, other module: a replaced definition. */
        }
        if(!e->definition) {slot=e;break;}
    }
    unsigned char exec[0x50]={0};char full[256];
    *(uint32_t*)(exec+0x38)=0x5d;*(uintptr_t*)(exec+0x40)=cue+0xb0;
    /* A miss walks the module's cue tree (thousands of nodes for FactionLogic):
     * read it with the guarded copy, not one system call per node. */
    if(npc_cue_name_with(script_rng_read,exec,full,0)!=1) return SR_UNNAMED;
    uint64_t hash=14695981039346656037ULL;
    for(size_t i=0;full[i];i++) hash=(hash^(unsigned char)full[i])*1099511628211ULL;
    size_t n=strlen(full);if(n>=sizeof(id->name)) n=sizeof(id->name)-1;memcpy(id->name,full,n);id->name[n]=0;
    id->owner=hash;
    if(slot) {
        if(!slot->definition) ++script_rng_name_count;
        slot->definition=definition;slot->module=module;slot->hash=hash;memcpy(slot->name,id->name,sizeof(slot->name));
    }
    return SR_OK;
}
