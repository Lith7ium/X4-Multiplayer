/* Fixed LE diagnostic schema. Addresses are local evidence, never shared IDs. */
#define WP_WIRE_SIZE 5320u
#define WP_ROW_BYTES 256u
#define WP_ROWS 21u
static void wp_encode_guard(unsigned char **p,const wp_guard *g) {
    for(unsigned i=0;i<19;i++)wui_put(p,g->q[i],8);for(unsigned i=0;i<9;i++)wui_put(p,g->d[i],4);
}
static void wp_encode_object(unsigned char **p,const wui_object *o) {
    for(unsigned i=0;i<13;i++)wui_put(p,o->q[i],8);for(unsigned i=0;i<5;i++)wui_put(p,o->d[i],4);
    memcpy(*p,o->name,128);*p+=128;memcpy(*p,o->code,36);*p+=36;memcpy(*p,o->bitmap,32);*p+=32;
    memcpy(*p,o->block,32);*p+=32;memcpy(*p,o->col,24);*p+=24;
}
static unsigned wp_encode(const wp_event *e,unsigned event,unsigned char out[WP_WIRE_SIZE]) {
    unsigned char *p=out;const unsigned meta[]={2,event,e->parent_action,e->depth,e->thread,e->errors,e->children,e->foreign,e->samples,e->reads,e->bytes,e->abnormal};
    for(unsigned i=0;i<12;i++)wui_put(&p,meta[i],4);for(unsigned i=0;i<4;i++)wui_put(&p,e->calls[i],4);for(unsigned i=0;i<4;i++)wui_put(&p,e->returns[i],4);
    const uint64_t ids[]={e->image_base,e->enter,e->leave,e->parent,e->result,e->caller,e->arguments[0],e->arguments[1],e->arguments[2],e->arguments[3],e->tls_address};
    for(unsigned i=0;i<11;i++)wui_put(&p,ids[i],8);
    for(unsigned i=0;i<2;i++)wp_encode_guard(&p,&e->outer[i]);for(unsigned i=0;i<2;i++)wp_encode_guard(&p,&e->transform_guard[i]);for(unsigned i=0;i<2;i++)wp_encode_guard(&p,&e->radius_guard[i]);
    const uint64_t args[]={e->space,e->reference,e->transform,e->time,e->range,e->radius_frame,e->radius_optional};
    for(unsigned i=0;i<7;i++)wui_put(&p,args[i],8);wui_put(&p,e->fifth,4);wui_put(&p,e->radius,4);for(unsigned i=0;i<2;i++)wui_put(&p,e->object_present[i],4);
    memcpy(p,e->transform_bytes,128);p+=128;memcpy(p,e->base,32);p+=32;memcpy(p,e->output,16);p+=16;
    for(unsigned i=0;i<2;i++)for(unsigned j=0;j<2;j++)wp_encode_object(&p,&e->objects[i][j]);
    for(unsigned i=0;i<WP_SAMPLES;i++) {const wp_sample *s=&e->sample[i];const uint64_t q[]={s->slot,s->rng,s->caller,s->parent_return,s->descriptor,s->frame,s->stack_limit,s->stack_base};
        const unsigned d[]={s->span,s->distribution,s->parameter,s->result,s->source,s->normal,s->abnormal};
        for(unsigned j=0;j<8;j++)wui_put(&p,q[j],8);for(unsigned j=0;j<7;j++)wui_put(&p,d[j],4);
        memcpy(p,s->difference,32);p+=32;memcpy(p,s->minimum,32);p+=32;for(unsigned j=0;j<2;j++)wp_encode_guard(&p,&s->guard[j]);
    }
    for(unsigned i=0;i<2;i++)for(unsigned j=0;j<5;j++)wui_put(&p,e->branch[i].d[j],4);
    for(unsigned i=0;i<2;i++)for(unsigned j=0;j<5;j++)wui_put(&p,e->radius_branch[i].d[j],4);
    for(unsigned i=0;i<8;i++)wui_put(&p,e->radius_witness[i],8);
    return (unsigned)(p-out);
}
static unsigned wp_ready(void) {unsigned n=0,limit=wp_reserved>WP_CAP?WP_CAP:wp_reserved;while(n<limit&&InterlockedCompareExchange(&wp_events[n].complete,0,0))++n;return n;}
static int wp_status(char *out,size_t size) {
    AcquireSRWLockShared(&wp_lock);
    int n=snprintf(out,size,"schema=2 capacity=%u sampler_capacity=%u read_limit=%u byte_limit=%u wire_size=%u row_bytes=%u reserved=%u complete=%u exported=%u rows=%u calls=%llu open=%llu peak=%llu restore_open=%llu closed=%u failures=%llu dropped=%llu owned=%u protection_pending=%u flush_pending=%u patch_failures=%llu patch_errors=%u",
        WP_CAP,WP_SAMPLES,WP_READS,WP_BYTES,WP_WIRE_SIZE,WP_ROW_BYTES,wp_reserved,wp_ready(),wp_exported,wp_ready()*WP_ROWS,
        (unsigned long long)wp_calls,(unsigned long long)wp_open,(unsigned long long)wp_peak,(unsigned long long)wp_restore_open,
        (unsigned)(wp_closing!=0),(unsigned long long)wp_failures,(unsigned long long)wp_dropped,wp_patch_mask(0),wp_patch_mask(1),wp_patch_mask(2),
        (unsigned long long)wp_patch_failures,(unsigned)wp_patch_errors);
    ReleaseSRWLockShared(&wp_lock);return n<0||(size_t)n>=size?-1:n;
}
static int wp_format(const wp_event *e,unsigned event,unsigned part,char *out,size_t size) {
    unsigned char bytes[WP_WIRE_SIZE];char hex[WP_ROW_BYTES*2+1];if(part>=WP_ROWS||wp_encode(e,event,bytes)!=WP_WIRE_SIZE)return -1;
    unsigned start=part*WP_ROW_BYTES,length=WP_WIRE_SIZE-start;if(length>WP_ROW_BYTES)length=WP_ROW_BYTES;
    world_values_hex(bytes+start,length,hex);int n=snprintf(out,size,"WORLD_POSITION schema=2 event=%u part=%u data=%s",event,part,hex);
    return n<0||(size_t)n>=size?-1:n;
}
static int lua_world_position_config(lua_State *L) {p_lua_pushnumber(L,world_position_configure());return 1;}
static int lua_world_position_cursor(lua_State *L) {if(GetCurrentThreadId()!=ui_thread||!wp_started)p_lua_pushnil(L);else p_lua_pushnumber(L,wp_exported);return 1;}
static int lua_world_position_status(lua_State *L) {
    if(GetCurrentThreadId()!=ui_thread||!wp_started){p_lua_pushnil(L);return 1;}char out[1024];int n=wp_status(out,sizeof(out));
    if(n<0)p_lua_pushnil(L);else p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_world_position_row(lua_State *L) {
    double row=p_lua_tonumber(L,1);unsigned rows=wp_ready()*WP_ROWS;
    if(GetCurrentThreadId()!=ui_thread||!wp_started||!isfinite(row)||row<0||floor(row)!=row){p_lua_pushnil(L);return 1;}
    if(!row){p_lua_pushnumber(L,rows);return 1;}if(row>rows||row!=wp_exported+1){p_lua_pushnil(L);return 1;}
    unsigned nrow=(unsigned)row-1;char out[2048];int n=wp_format(&wp_events[nrow/WP_ROWS],nrow/WP_ROWS+1,nrow%WP_ROWS,out,sizeof(out));
    if(n<0)p_lua_pushnil(L);else{++wp_exported;p_lua_pushlstring(L,out,(size_t)n);}return 1;
}
