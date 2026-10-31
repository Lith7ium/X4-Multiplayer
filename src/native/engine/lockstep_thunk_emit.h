/* Emit every reserved region before the first native entry can reach this page.
 * Prefix bytes are pinned instruction sequences, not reads from a possibly
 * already-patched entry. Feature-specific full pins still run at installation.
 * Emission does not enable a feature, acquire engine locks or register unwind. */
enum thunk_kind {THUNK_JUMP,THUNK_POINTER,THUNK_TRAMPOLINE,THUNK_PAIR,THUNK_NESTED,
    THUNK_DEADLINE,THUNK_READY,THUNK_WORLD_FORWARD,THUNK_UNWIND_TRAMPOLINE,THUNK_UNWIND_PAIR,THUNK_ROTATION_FORWARD,THUNK_POSITION_RADIUS};
typedef struct {
    unsigned offset,size,kind,entry,prefix_size,dispatch;
    void *target;unsigned char prefix[15];
} thunk_recipe;
static const thunk_recipe thunk_recipes[]={
    {0,32,THUNK_JUMP,0,0,0,(void*)gate_tick,{0}},
    {32,8,THUNK_POINTER,0,0,0,(void*)path_release,{0}},
    {128,64,THUNK_JUMP,0,0,0,(void*)ai_run,{0}},
    {RID_REMOVE_THUNK,64,THUNK_PAIR,RID_REMOVE_ENTRY,5,32,(void*)rid_remove,{0x48,0x89,0x5c,0x24,0x08}},
    {256,64,THUNK_JUMP,0,0,0,(void*)agent_update,{0}},
    {320,64,THUNK_UNWIND_TRAMPOLINE,PARAMS_ENTRY,15,0,NULL,{0x48,0x8b,0xc4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57}},
    {384,64,THUNK_PAIR,SHAPE_ENTRY,5,32,(void*)shape_move,{0x48,0x89,0x5c,0x24,0x08}},
    /* 448..511 was unowned (between shape and mass); created identities. */
    {448,64,THUNK_PAIR,CREATED_ENTRY,5,32,(void*)created_assign,{0x48,0x89,0x5c,0x24,0x10}},
    {512,64,THUNK_PAIR,MASS_ENTRY,7,32,(void*)mass_notify,{0x48,0x8b,0xc4,0x48,0x89,0x58,0x10}},
    {576,64,THUNK_UNWIND_PAIR,ATTENTION_ENTRY,6,32,(void*)attention_refresh,{0x40,0x57,0x48,0x83,0xec,0x20}},
    {640,64,THUNK_PAIR,FLIGHT_CTOR,5,32,(void*)flight_construct,{0x48,0x89,0x5c,0x24,0x18}},
    {704,32,THUNK_JUMP,0,0,0,(void*)gravidar_defer,{0}},
    {736,64,THUNK_UNWIND_PAIR,DAMAGE_ENTRY,5,32,(void*)damage_absorb,{0x48,0x8b,0xc4,0x53,0x56}},
    {800,48,THUNK_PAIR,INTERRUPT_ENTRY,7,32,(void*)interrupt_run,{0x48,0x8b,0xc4,0x48,0x89,0x58,0x20}},
    {848,48,THUNK_PAIR,CONTACT_ENTRY,5,32,(void*)contact_record,{0x4c,0x89,0x4c,0x24,0x20}},
    {896,48,THUNK_PAIR,INTERRUPT_STEP_ENTRY,5,32,(void*)interrupt_step_run,{0x48,0x89,0x5c,0x24,0x08}},
    {944,48,THUNK_PAIR,WORLD_IDENTITY_ENTRY,5,32,(void*)world_identity_import,{0x48,0x89,0x5c,0x24,0x08}},
    /* 992..1023 was unowned (between world identity and nested); census-zone Gravidar wide applies (992..1007). */
    {992,16,THUNK_JUMP,0,0,0,(void*)gwide_apply,{0}},
    {1024,64,THUNK_NESTED,0,0,0,(void*)nested_action_run,{0}},
    {1088,64,THUNK_PAIR,NPC_EXEC_ENTRY,5,32,(void*)npc_exec_run,{0x48,0x89,0x5c,0x24,0x08}},
    {1152,64,THUNK_PAIR,NPC_SPEAK_ENTRY,7,32,(void*)npc_speak_run,{0x48,0x8b,0xc4,0x48,0x89,0x58,0x10}},
    {1216,64,THUNK_PAIR,NPC_ENQUEUE_ENTRY,5,32,(void*)npc_enqueue_run,{0x44,0x88,0x4c,0x24,0x20}},
    {1280,64,THUNK_PAIR,NPC_FINISH_ENTRY,5,32,(void*)npc_finish_run,{0x48,0x89,0x5c,0x24,0x18}},
    {1344,320,THUNK_DEADLINE,0,0,0,(void*)npc_deadline_route,{0}},
    {1664,320,THUNK_READY,0,0,0,(void*)npc_ready_observe,{0}},
    {1984,32,THUNK_JUMP,0,0,0,(void*)event_order_deliver,{0}},
    {2016,16,THUNK_JUMP,0,0,0,(void*)script_rng_md_check,{0}},
    {2032,16,THUNK_JUMP,0,0,0,(void*)boot_hold_move,{0}},
    {2048,256,THUNK_PAIR,CONTACT_PROPERTIES_ENTRY,5,32,(void*)contact_properties_update,{0x48,0x89,0x5c,0x24,0x08}},
    {2304,128,THUNK_UNWIND_PAIR,SOLVER_RVA,6,64,(void*)solver_dispatch,{0x40,0x53,0x48,0x83,0xec,0x60}},
    {2432,64,THUNK_PAIR,REGISTRY_MD_RVA,5,32,(void*)registry_md_record,{0x48,0x89,0x5c,0x24,0x08}},
    {2496,64,THUNK_PAIR,REQUEST_RVA,5,32,(void*)request_dispatch,{0x48,0x89,0x5c,0x24,0x08}},
    {2560,64,THUNK_PAIR,0xeacd40,5,32,(void*)world_queue_enqueue,{0x48,0x89,0x5c,0x24,0x10}},
    {2624,64,THUNK_PAIR,0xe89e40,5,32,(void*)world_queue_append,{0x48,0x89,0x5c,0x24,0x08}},
    {2688,64,THUNK_WORLD_FORWARD,0,0,0,(void*)world_queue_dispatch,{0}},
    /* 2976..3071 was unowned (after the separately owned 2752..2975); census-zone attribution and order. */
    {2976,48,THUNK_PAIR,WR_CTOR,5,32,(void*)wr_construct,{0x48,0x89,0x5c,0x24,0x18}},
    {3024,48,THUNK_PAIR,TZ_FIND,5,32,(void*)tz_find,{0x48,0x89,0x5c,0x24,0x08}},
    {3072,64,THUNK_PAIR,0xc841b0,5,32,(void*)creation_generate,{0x48,0x89,0x5c,0x24,0x08}},
    {3136,64,THUNK_PAIR,0xc82060,5,32,(void*)creation_initialize,{0x4c,0x89,0x4c,0x24,0x20}},
    {3200,64,THUNK_PAIR,0xc7d4c0,5,32,(void*)creation_pilot,{0x48,0x89,0x5c,0x24,0x10}},
    {3264,64,THUNK_PAIR,0xea8cf0,7,32,(void*)creation_navigation,{0x48,0x8b,0xc4,0x48,0x89,0x58,0x18}},
    {3328,64,THUNK_PAIR,0xe85360,5,32,(void*)creation_agent,{0x48,0x89,0x5c,0x24,0x10}},
    {3392,64,THUNK_PAIR,0x745e00,5,32,(void*)creation_setter,{0x48,0x89,0x5c,0x24,0x08}},
    {3456,64,THUNK_UNWIND_PAIR,0x14ddc90,9,32,(void*)creation_random,{0x48,0x83,0xec,0x38,0x0f,0x29,0x74,0x24,0x20}},
    {3520,32,THUNK_JUMP,0,0,0,(void*)creation_event_query,{0}},
    {3552,32,THUNK_JUMP,0,0,0,(void*)creation_post,{0}},
    {3584,64,THUNK_PAIR,WORLD_VALUES_ASSIGN,5,32,(void*)world_values_assign,{0x48,0x89,0x5c,0x24,0x10}},
    {3648,64,THUNK_PAIR,WORLD_VALUES_CANDIDATE,5,32,(void*)world_values_candidate,{0x48,0x89,0x5c,0x24,0x10}},
    {3712,32,THUNK_JUMP,0,0,0,(void*)rotation_load_run,{0}},
    {3744,32,THUNK_ROTATION_FORWARD,0,0,0,(void*)rotation_tail_run,{0}},
    {3776,64,THUNK_PAIR,WP_OUTER,7,32,(void*)world_position_outer,{0x48,0x8b,0xc4,0x48,0x89,0x58,0x10}},
    {3840,32,THUNK_JUMP,0,0,0,(void*)world_position_transform,{0}},
    {3872,32,THUNK_POSITION_RADIUS,0,0,0,(void*)world_position_radius,{0}},
    {3904,64,THUNK_PAIR,WP_SAMPLER,5,32,(void*)world_position_sampler,{0x48,0x89,0x6c,0x24,0x10}},
    {3968,64,THUNK_PAIR,JOB_QUEUE_ENTRY,5,32,(void*)job_rng_queue,{0x48,0x89,0x5c,0x24,0x10}},
    {4032,16,THUNK_JUMP,0,0,0,(void*)md_resume_delay,{0}},
    {4048,16,THUNK_JUMP,0,0,0,(void*)travel_queue,{0}},
    {4064,16,THUNK_JUMP,0,0,0,(void*)tsnap_job,{0}},
    {4080,16,THUNK_JUMP,0,0,0,(void*)find_order_assign,{0}},
};
static void thunk_absolute_jump(unsigned char *out,void *target) {
    const unsigned char op[6]={0xff,0x25,0,0,0,0};memcpy(out,op,6);memcpy(out+6,&target,8);
}
static void thunk_r11_jump(unsigned char *out,void *target) {
    /* R11 is not an argument and has no incoming use in these pinned
     * native functions. Unlike FF25, this cannot look like a tail epilogue to
     * RtlVirtualUnwind while the copied stack adjustments are still in effect. */
    out[0]=0x49;out[1]=0xbb;memcpy(out+2,&target,8);out[10]=0x41;out[11]=0xff;out[12]=0xe3;
}
static int thunk_recipe_valid(const thunk_recipe *r) {
    if(!r || !r->size || r->size>THUNK_MAX_SLOT_SIZE) return 0;
    switch(r->kind) {
    case THUNK_JUMP:return r->size>=14;
    case THUNK_POINTER:return r->size>=8;
    case THUNK_TRAMPOLINE:case THUNK_PAIR:
        return r->prefix_size && r->prefix_size<=sizeof(r->prefix) && r->prefix_size+14<=r->size &&
            (r->kind!=THUNK_PAIR || (r->dispatch>=r->prefix_size+14 && r->dispatch<=r->size-14));
    case THUNK_UNWIND_TRAMPOLINE:case THUNK_UNWIND_PAIR: {
        const thunk_unwind_recipe *u=thunk_unwind_at(r->offset);
        return u && r->prefix_size==u->prefix_size && r->prefix_size<=sizeof(r->prefix) &&
            u->info_offset>=r->prefix_size+13 && u->info_offset<=r->size && u->info_size<=r->size-u->info_offset &&
            (r->kind!=THUNK_UNWIND_PAIR || (r->dispatch>=r->prefix_size+13 && r->dispatch<=r->size-14 &&
             (r->dispatch+14<=u->info_offset || r->dispatch>=u->info_offset+u->info_size)));
    }
    case THUNK_NESTED:return r->size>=17;
    case THUNK_ROTATION_FORWARD:return r->size>=20;
    case THUNK_POSITION_RADIUS:return r->size>=17;
    case THUNK_DEADLINE:case THUNK_READY:return r->size==320;
    case THUNK_WORLD_FORWARD:return r->size>=40;
    default:return 0;
    }
}
static int thunk_emit_slot_at(unsigned char *out,const thunk_recipe *r,unsigned char *entry) {
    if(!out || !thunk_recipe_valid(r)) return 0;
    memset(out,0,r->size);
    switch(r->kind) {
    case THUNK_JUMP:thunk_absolute_jump(out,r->target);break;
    case THUNK_POINTER:memcpy(out,&r->target,8);break;
    case THUNK_TRAMPOLINE:case THUNK_PAIR:
        memcpy(out,r->prefix,r->prefix_size);thunk_absolute_jump(out+r->prefix_size,entry+r->prefix_size);
        if(r->kind==THUNK_PAIR) thunk_absolute_jump(out+r->dispatch,r->target);
        break;
    case THUNK_UNWIND_TRAMPOLINE:case THUNK_UNWIND_PAIR: {
        const thunk_unwind_recipe *u=thunk_unwind_at(r->offset);
        memcpy(out,r->prefix,r->prefix_size);thunk_r11_jump(out+r->prefix_size,entry+r->prefix_size);
        memcpy(out+u->info_offset,u->info,u->info_size);
        if(r->kind==THUNK_UNWIND_PAIR) thunk_absolute_jump(out+r->dispatch,r->target);
        break;
    }
    case THUNK_NESTED:
        out[0]=0x48;out[1]=0x8b;out[2]=0xcf;thunk_absolute_jump(out+3,r->target);break;
    case THUNK_ROTATION_FORWARD:
        out[0]=0x4c;out[1]=0x8b;out[2]=0xc7; /* R8 = Agent RDI. */
        out[3]=0x4d;out[4]=0x8b;out[5]=0xcd; /* R9 = movement XML R13. */
        thunk_absolute_jump(out+6,r->target);break;
    case THUNK_POSITION_RADIUS:
        out[0]=0x49;out[1]=0x89;out[2]=0xc1; /* R9 = native resolver output pointer in RAX. */
        thunk_absolute_jump(out+3,r->target);break;
    case THUNK_DEADLINE:
        return npc_deadline_stub(out,r->size,r->target,image+0x563e93,image+0x563f71,image+0x563f58)==247;
    case THUNK_READY:return npc_ready_stub(out,r->size,r->target,image+0x563fbc)==144;
    case THUNK_WORLD_FORWARD: {
        const unsigned char forward[15]={0x48,0x83,0xec,0x28,0x48,0x8b,0x01,0xff,0x50,0x08,0x48,0x83,0xc4,0x28,0xc3};
        const unsigned char unwind[8]={1,4,1,0,4,0x42,0,0};
        memcpy(out,forward,sizeof(forward));memcpy(out+16,unwind,sizeof(unwind));memcpy(out+32,&r->target,8);break;
    }
    default:return 0;
    }
    return 1;
}
static int thunk_emit_slot(unsigned char *out,const thunk_recipe *r) {return thunk_emit_slot_at(out,r,image+r->entry);}
static int thunk_prepare_page(unsigned char *page) {
    if(!page) return 0;memset(page,0,THUNK_PAGE_SIZE);unsigned end=0;
    for(unsigned i=0;i<sizeof(thunk_recipes)/sizeof(*thunk_recipes);i++) {
        const thunk_recipe *r=&thunk_recipes[i];
        if(r->offset<end || r->offset>THUNK_PAGE_SIZE || r->size>THUNK_PAGE_SIZE-r->offset || !thunk_emit_slot(page+r->offset,r)) return 0;
        end=r->offset+r->size;
    }
#ifdef WORLD_WAIT_ENTRY
    world_wait_prepare_thunk(page); /* Separately owned 2752..2815. */
#endif
#ifdef TRANSITION_TRANSFER
    if(!transition_emit_page(page)) return 0; /* Separately owned 2816..2943. */
#endif
#ifdef PORIGIN_STUB
    if(!origin_emit_page(page)) return 0; /* Separately owned 2944..2975. */
#endif
    return 1;
}
static const thunk_recipe *thunk_recipe_at(unsigned offset) {
    for(unsigned i=0;i<sizeof(thunk_recipes)/sizeof(*thunk_recipes);i++) {
        const thunk_recipe *r=&thunk_recipes[i];
        if(r->offset==offset) return r;
    }
    return NULL;
}
static int thunk_slot_ready(unsigned offset) {
    unsigned char expected[THUNK_MAX_SLOT_SIZE];const thunk_recipe *r=thunk_recipe_at(offset);
    return r && thunk_emit_slot(expected,r) && thunk_bytes_match(offset,expected,r->size);
}
