/* Read-only, pinned Windows diagnostic. Tokens, names, PCs and native RVAs are
 * observations, never semantic RNG identities. Unknown Values remain opaque. */
#define NPC_PROVENANCE_LIMIT 4096u
#define NPC_PROVENANCE_CUES 4096u
#define NPC_PROVENANCE_CURRENT 0x38f4c60u
#define NPC_PROVENANCE_DEPTH 64u
#define NPC_PROVENANCE_PARENTS 8u

typedef struct {uintptr_t exec,value,context;uint64_t id;unsigned tag;} npc_provenance_guard;
typedef struct npc_provenance_frame {
    struct npc_provenance_frame *previous;uintptr_t native_parent;uint64_t caller,caller_raw;
    npc_provenance_guard self;unsigned depth,readable,caller_source,scope_phase;
} npc_provenance_frame;
typedef struct {uintptr_t pointer;uint64_t id,token;} npc_provenance_cue;
typedef struct {uint64_t token;char name[256];} npc_provenance_link;
typedef struct {unsigned tag,kind;uint64_t value;} npc_provenance_value;
typedef struct {
    uint64_t tick,trigger,time,owner_pc,owner_type,owner_action,owner_caller,owner_caller_raw,owner_saved;
    unsigned flags,event,state,pending,block,depth,owner_kind,owner_status,ancestry_count,owner_caller_source;
    npc_provenance_link cue,links[4],ancestry[NPC_PROVENANCE_PARENTS],owner;
    npc_provenance_value values[4];
} npc_provenance_sample;
static npc_provenance_sample npc_provenance_samples[NPC_PROVENANCE_LIMIT];
static npc_provenance_cue npc_provenance_cues[NPC_PROVENANCE_CUES];
static unsigned npc_provenance_count,npc_provenance_cue_count;
static uint64_t npc_provenance_calls,npc_provenance_unresolved,npc_provenance_unowned,npc_provenance_failures,npc_provenance_overflows;
static int npc_provenance_enabled;
/* Optional pure predicate owned by the world-queue observer. It verifies its
 * explicit pre-admission configuration, open admission, UI/time bounds and
 * stable native flag/clock snapshot. NULL preserves phase3-only observation. */
static int (*npc_provenance_early_eligible)(void);
static _Thread_local npc_provenance_frame *npc_provenance_current;
static _Thread_local unsigned npc_provenance_overflow_depth,npc_provenance_denied_depth;

static int npc_provenance_guard_read(void *exec,npc_provenance_guard *g) {
    memset(g,0,sizeof(*g));g->exec=(uintptr_t)exec;
    if(!exec || !read_local((unsigned char*)exec+0x38,&g->tag,4) || !read_local((unsigned char*)exec+0x40,&g->value,8)) return 0;
    uintptr_t offset=g->tag==0x5d?0xb0:g->tag==0x66?0xa8:0;
    if(!offset) return 1; /* Known raw tag, unsupported owner type. */
    if(g->value<offset) return 0;g->context=g->value-offset;
    return read_local((void*)(g->context+8),&g->id,8) && g->id;
}
static int npc_provenance_guard_same(const npc_provenance_guard *g) {
    npc_provenance_guard now;
    return npc_provenance_guard_read((void*)g->exec,&now) && !memcmp(g,&now,sizeof(now));
}
static void npc_provenance_failure(void) {++npc_provenance_failures;InterlockedIncrement64(&interrupt_failures);}
static unsigned npc_provenance_scope_phase(void) {
    if(!(boot_options&281474976710656ULL) || !npc_provenance_enabled || !stats.active || GetCurrentThreadId()!=ui_thread) return 0;
    if(boot_phase==3) return 3;
    return boot_mode && boot_phase==2 && !stats.issued && npc_provenance_early_eligible && npc_provenance_early_eligible()?2:0;
}
static int npc_provenance_enter(npc_provenance_frame *frame,void *exec,uint64_t caller) {
    unsigned phase=npc_provenance_scope_phase();
    /* A denied same-ExecState reentry must not expose its older eligible frame
     * if a transient native read failure clears before the nested call returns. */
    if(npc_provenance_denied_depth || (!phase && npc_provenance_current && npc_provenance_current->scope_phase==2)) {
        ++npc_provenance_denied_depth;return 3;
    }
    if(!phase) return 0;
    if(npc_provenance_overflow_depth || (npc_provenance_current && npc_provenance_current->depth>=NPC_PROVENANCE_DEPTH)) {
        ++npc_provenance_overflow_depth;++npc_provenance_overflows;npc_provenance_failure();return 2;
    }
    memset(frame,0,sizeof(*frame));frame->previous=npc_provenance_current;frame->caller=frame->caller_raw=caller;
    frame->caller_source=caller?1:3; /* 1 native image,2 verified owned AI dispatch,3 unsupported caller. */
    frame->depth=frame->previous?frame->previous->depth+1:1;frame->scope_phase=phase;
    frame->readable=npc_provenance_guard_read(exec,&frame->self) &&
        read_local(image+NPC_PROVENANCE_CURRENT,&frame->native_parent,8);
    if(phase==2 && frame->self.tag!=0x66 && frame->self.tag!=0x5d) frame->readable=0;
    if(phase==3 && !caller && frame->readable && frame->self.tag==0x66) {
        uint64_t origin=ai_origin_consume(exec);if(origin) {frame->caller=origin;frame->caller_source=2;}
    }
    npc_provenance_current=frame;return 1;
}
/* Pure optional callback for other observers. A false result is unbracketed
 * coverage, not proof that the native CURRENT global has no executing owner. */
static int npc_provenance_current_matches(uintptr_t native_current) {
    return !npc_provenance_overflow_depth && !npc_provenance_denied_depth && npc_provenance_current && npc_provenance_current->readable &&
        npc_provenance_current->scope_phase && npc_provenance_scope_phase()==npc_provenance_current->scope_phase &&
        npc_provenance_current->self.exec==native_current && npc_provenance_guard_same(&npc_provenance_current->self);
}
static void npc_provenance_leave(npc_provenance_frame *frame,int entered) {
    if(entered==3) {if(npc_provenance_denied_depth) --npc_provenance_denied_depth;else npc_provenance_failure();return;}
    if(entered==2) {if(npc_provenance_overflow_depth) --npc_provenance_overflow_depth;else npc_provenance_failure();return;}
    if(!entered) return;
    uintptr_t parent=0;
    if(npc_provenance_current!=frame || !read_local(image+NPC_PROVENANCE_CURRENT,&parent,8) || parent!=frame->native_parent)
        npc_provenance_failure();
    npc_provenance_current=frame->previous;
}
static int npc_provenance_cue_read(uintptr_t cue,npc_provenance_link *link) {
    memset(link,0,sizeof(*link));strcpy(link->name,"none");if(!cue) return 1;
    uint64_t id=0,again=0;unsigned char exec[0x50]={0};
    *(unsigned*)(exec+0x38)=0x5d;*(uintptr_t*)(exec+0x40)=cue+0xb0;
    if(!read_local((void*)(cue+8),&id,8) || !id || npc_cue_name_ex(exec,link->name,0)!=1 ||
       !read_local((void*)(cue+8),&again,8) || id!=again) {strcpy(link->name,"unreadable");return 0;}
    for(unsigned i=0;i<npc_provenance_cue_count;i++) {
        npc_provenance_cue *entry=&npc_provenance_cues[i];
        if(entry->pointer==cue && entry->id==id) {link->token=entry->token;return 1;}
        /* IDs must be unique while their observed pointer remains live. */
        if(entry->id==id && entry->pointer!=cue) return 0;
    }
    if(npc_provenance_cue_count==NPC_PROVENANCE_CUES) {++npc_provenance_overflows;return 0;}
    npc_provenance_cue *entry=&npc_provenance_cues[npc_provenance_cue_count++];
    entry->pointer=cue;entry->id=id;entry->token=npc_provenance_cue_count;link->token=entry->token;return 1;
}
/* Only already observed, still-live cue IDs may become LOCAL correlation tokens. */
static uint64_t npc_provenance_cue_token(uint64_t id) {
    if(!id) return 0;
    for(unsigned i=0;i<npc_provenance_cue_count;i++) if(npc_provenance_cues[i].id==id) {
        uint64_t now=0;npc_provenance_cue *e=&npc_provenance_cues[i];
        return read_local((void*)(e->pointer+8),&now,8) && now==id?e->token:0;
    }
    return 0;
}
/* kind0 unresolved,1 saved object,2 unmapped object,3 absent source,
 * 4 local cue reference,5 unresolved cue reference. Never emit opaque payload bits. */
static npc_provenance_value npc_provenance_decode(unsigned event,unsigned slot,unsigned tag,uint64_t payload) {
    npc_provenance_value value={tag,0,0};
    if(slot==0 && event==0x580 && tag==0xa) {
        value.value=world_identity_lookup(payload);value.kind=value.value?1:2;
    } else if(slot==0 && (event==0x580 || event==0x637) && tag==0) value.kind=3;
    else if(slot==0 && event==0x637 && tag==0x12) {
        value.value=npc_provenance_cue_token(payload);value.kind=value.value?4:5;
    }
    return value;
}
static void npc_provenance_owner(npc_provenance_sample *s,npc_provenance_frame *frame) {
    strcpy(s->owner.name,"none");if(!frame->native_parent) return;
    npc_provenance_guard owner;
    if(frame->native_parent==frame->self.exec || !npc_provenance_guard_read((void*)frame->native_parent,&owner)) {s->flags|=8;return;}
    s->owner_status=2; /* Native owner not covered by our b58bb0 TLS bracket. */
    if(!npc_provenance_denied_depth && frame->previous && frame->previous->scope_phase==3 && frame->previous->self.exec==frame->native_parent) {
        if(!frame->previous->readable || memcmp(&frame->previous->self,&owner,sizeof(owner))) {s->flags|=16;return;}
        s->owner_status=1;s->owner_caller=frame->previous->caller;s->owner_caller_raw=frame->previous->caller_raw;
        s->owner_caller_source=frame->previous->caller_source;
    }
    s->owner_kind=owner.tag==0x5d?1:owner.tag==0x66?2:3;
    if(owner.tag==0x5d) {if(!npc_provenance_cue_read(owner.context,&s->owner)) s->flags|=8;}
    else if(owner.tag==0x66) {
        char script[96];uintptr_t entity=0,again=0;uint64_t id=0,id_again=0;
        if(!ai_script((void*)owner.context,script) || !npc_encode(script,s->owner.name,sizeof(s->owner.name)) ||
           !read_local((void*)(owner.context+0x120),&entity,8) || !entity || !read_local((void*)(entity+8),&id,8) || !id ||
           !read_local((void*)(owner.context+0x120),&again,8) || again!=entity || !read_local((void*)(entity+8),&id_again,8) || id_again!=id)
            s->flags|=8;
        else s->owner_saved=world_identity_lookup(id); /* Missing map is explicit, never a runtime-ID fallback. */
    } else strcpy(s->owner.name,"unsupported");
    uintptr_t program=0,bounds[2]={0},node=0;uint64_t pc=0;
    if(!read_local((void*)(owner.exec+0x18),&pc,8) || pc>1048576) s->flags|=8;
    else {
        s->owner_pc=pc;
        /* Native interpreter increments PC before calling the owning action. */
        if(pc && read_local((void*)(owner.exec+0x10),&program,8) && read_local((void*)program,bounds,16) && bounds[1]>=bounds[0] &&
           !((bounds[1]-bounds[0])&7) && pc<=(bounds[1]-bounds[0])/8 && read_local((void*)(bounds[0]+(pc-1)*8),&node,8)) {
            nested_action_sample action={0};
            if(nested_action_read((void*)node,(void*)owner.exec,&action)) {s->owner_pc=action.pc;s->owner_type=action.type;s->owner_action=action.action;}
            else s->flags|=8;
        } else s->flags|=8;
    }
    uint64_t pc_after=0;uintptr_t program_after=0;
    if(!npc_provenance_guard_same(&owner) || !read_local((void*)(owner.exec+0x18),&pc_after,8) || pc_after!=pc ||
       !read_local((void*)(owner.exec+0x10),&program_after,8) || program_after!=program) s->flags|=16;
}
static void npc_provenance_capture(void *exec,uint64_t tick,uint64_t trigger) {
    if(!npc_provenance_enabled) return;
    ++npc_provenance_calls;
    if(npc_provenance_count==NPC_PROVENANCE_LIMIT) {++npc_provenance_overflows;npc_provenance_failure();return;}
    npc_provenance_sample *s=&npc_provenance_samples[npc_provenance_count++];memset(s,0,sizeof(*s));s->tick=tick;s->trigger=trigger;
    strcpy(s->cue.name,"unreadable");strcpy(s->owner.name,"none");
    for(unsigned i=0;i<4;i++) strcpy(s->links[i].name,"none");
    npc_provenance_frame *frame=npc_provenance_current;
    if(!frame || npc_provenance_overflow_depth || !frame->readable || frame->self.exec!=(uintptr_t)exec || frame->self.tag!=0x5d) s->flags|=32;
    else {
        s->depth=frame->depth;uintptr_t cue=frame->self.context,links[5]={0},parents[NPC_PROVENANCE_PARENTS]={0};
        unsigned char event[0x48]={0},after[0x48]={0};
        if(!npc_provenance_cue_read(cue,&s->cue) || !read_local((void*)(cue+0x18),event,sizeof(event)) ||
           !read_local((void*)(cue+0x158),links,sizeof(links)) || !read_local((void*)(cue+0x180),&s->state,4) ||
           !read_local((void*)(cue+0x184),&s->pending,4) || !read_local((void*)(cue+0x1f8),&s->time,8) ||
           !read_local((void*)(cue+0x200),&s->block,4)) s->flags|=1;
        else {
            memcpy(&s->event,event,4);
            const unsigned indices[4]={0,2,3,4};
            for(unsigned i=0;i<4;i++) if(!npc_provenance_cue_read(links[indices[i]],&s->links[i])) s->flags|=2;
            uintptr_t parent=links[0];
            while(parent && s->ancestry_count<NPC_PROVENANCE_PARENTS) {
                unsigned index=s->ancestry_count;int cycle=parent==cue;
                for(unsigned j=0;j<index;j++) if(parents[j]==parent) cycle=1;
                if(cycle) {s->flags|=4;break;}parents[index]=parent;
                if(!npc_provenance_cue_read(parent,&s->ancestry[index]) || !read_local((void*)(parent+0x158),&parent,8)) {s->flags|=2;break;}
                ++s->ancestry_count;
            }
            if(parent && s->ancestry_count==NPC_PROVENANCE_PARENTS) s->flags|=4;
            npc_provenance_owner(s,frame);
            for(unsigned i=0;i<4;i++) {
                unsigned tag;uint64_t payload;memcpy(&tag,event+8+i*16,4);memcpy(&payload,event+16+i*16,8);
                s->values[i]=npc_provenance_decode(s->event,i,tag,payload);
            }
            uintptr_t links_after[5],native_parent=0;
            if(!read_local((void*)(cue+0x18),after,sizeof(after)) || memcmp(event,after,sizeof(event)) ||
               !read_local((void*)(cue+0x158),links_after,sizeof(links_after)) || memcmp(links,links_after,sizeof(links)) ||
               !read_local(image+NPC_PROVENANCE_CURRENT,&native_parent,8) || native_parent!=frame->native_parent ||
               !npc_provenance_current_matches((uintptr_t)exec)) s->flags|=16;
        }
    }
    for(unsigned i=0;i<4;i++) if(s->values[i].kind==0 || s->values[i].kind==2 || s->values[i].kind==5) ++npc_provenance_unresolved;
    if(s->owner_status==2 || s->owner_kind==3 || (s->owner_kind==2 && !s->owner_saved)) ++npc_provenance_unowned;
    if(s->flags) npc_provenance_failure();
}
static int npc_provenance_pinned(void) {
    return npc_layout_pinned() &&
        hash_ok(image+0x1111a40,905,"bde488a4463be31fa0a9d096c61c1b5922d2b1e3191a939bf11302905e688ee4") &&
        hash_ok(image+0x111c290,712,"5f9346c838fde113044bdca972ea8c18f13dadf33fb48dde50e69664ac5688d1") &&
        hash_ok(image+0x111c5f0,1514,"712156da1741677dc92f13fba5e861756cf6d7dc084992f0cdc79e4f3d653805") &&
        hash_ok(image+0x111d360,1087,"f790b20b409157ab3f6acf4b5dbcd7b6ac005df6e4627b1035f7f9584c108437") &&
        hash_ok(image+0x111e370,520,"d0662b4ba856fe88f7f78f5a188c19a929606a772b12a940022f841c6f007e5b") &&
        hash_ok(image+0x111c560,143,"3b55edbc3ac1c5c0291e0aec87b1081e756c0834940443650eec053ee4c8cea7") &&
        hash_ok(image+0x1121f80,394,"1a73fd8bd775643c09f921a2b0e956b23ff8d934f524067466f34b93d3e2b8d1") &&
        hash_ok(image+0x1122ae0,317,"76023adb28e78e57d244d98ad62d4b833e7eedcb0d7b0aae51b8dcf06f85550e") &&
        hash_ok(image+0x1123610,615,"bde93a061287442fbea87795c10b3f0be68d246372776caeb8c850dd012fc989") &&
        hash_ok(image+0xcaab00,113,"802ab0758a5c93826e270bebed55ee45c50c9567f370faed8c6d582433a13e65") &&
        hash_ok(image+0xc58820,142,"1d710dec2d324f2c99327b1bcd6f4e4ede7116fbcad8077d883a9090bf2bda81") &&
        hash_ok(image+0x9cb6f0,355,"29f3c41b691dd93d4139994545cf6eb518ce9dbc910606334033b218c6792ea9") &&
        hash_ok(image+0x9cb930,56,"e3fca8abd083a032a00ee00f73c6498d94c0ef11f8792b9950bf51c416c1244a") &&
        hash_ok(image+0xcb7590,201,"0fc3e88ffa953d75812c8cbb58cc470eb9aa38e78ecfdfe4fdf2a9ed676808ba") &&
        hash_ok(image+0x10631b0,472,"b5468455eb7534da2330706d124b3e70c1aedf137869208687e6686adfb2917d") &&
        hash_ok(image+0x1066ba0,613,"ab00b7598d2b6cce00d9d0135844f47bd2c9a94d9a9fcc1cf9508a994a8d9434") &&
        hash_ok(image+0x106d5b1,40,"98cec6c522690356d88f9c9b8be8ef9e0cd59054dc9c2ea9db26b123bcd5b154") &&
        *(uintptr_t*)(image+0x2ba7028)==(uintptr_t)(image+0x9cb930);
}
