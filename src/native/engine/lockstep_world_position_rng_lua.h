#define WPR_WIRE_SIZE 576u
#define WPR_ROWS 3u
static unsigned wpr_ready(void){unsigned n=0,limit=wpr_reserved>WPR_CAP?WPR_CAP:wpr_reserved;while(n<limit&&InterlockedCompareExchange(&wpr_events[n].complete,0,0))++n;return n;}
static void wpr_encode_identity(unsigned char **p,const world_find_identity *i){
    const uint64_t q[]={i->context,i->entity,i->script,i->exec,i->program,i->order,i->backlink,i->registry,i->group,i->context_id,i->runtime,i->saved,i->order_id};
    const unsigned d[]={i->state,i->version,i->flags,i->generation};
    for(unsigned n=0;n<13;n++)wui_put(p,q[n],8);for(unsigned n=0;n<4;n++)wui_put(p,d[n],4);
}
static unsigned wpr_encode(const wpr_event *e,unsigned number,unsigned char bytes[WPR_WIRE_SIZE]){
    unsigned char *p=bytes;const unsigned d[]={1,number,e->errors,e->thread,e->position,e->action,e->occurrence,e->samples,
        e->prewrite_refused,e->write_attempted,e->applied,e->restore_required,e->restored,e->debt,e->abnormal,e->normal,e->reads,e->bytes,e->composition,e->find_event};
    const uint64_t q[]={e->caller_before,e->seed,e->scoped_before,e->sampler_after,e->scoped_after,e->caller_after,e->tls,e->address,
        e->sampler_slot,e->image,e->raw_outer_before,e->raw_sampler_before,e->frame,e->descriptor,e->anchor_wait,e->find_candidate};
    for(unsigned i=0;i<20;i++)wui_put(&p,d[i],4);for(unsigned i=0;i<16;i++)wui_put(&p,q[i],8);
    for(unsigned i=0;i<2;i++)wpr_encode_identity(&p,&e->identity[i]);
    memcpy(p,e->maximum,32);p+=32;memcpy(p,e->scratch,32);p+=32;
    for(unsigned i=0;i<2;i++)for(unsigned j=0;j<2;j++)wui_put(&p,e->math[i][j],4);
    memcpy(p,e->output,16);p+=16;memcpy(p,e->digest,32);p+=32;return(unsigned)(p-bytes);
}
static int wpr_format(const wpr_event *e,unsigned number,unsigned part,char *out,size_t capacity){
    unsigned char bytes[WPR_WIRE_SIZE];char hex[513];if(part>=WPR_ROWS||wpr_encode(e,number,bytes)!=WPR_WIRE_SIZE)return -1;
    unsigned start=part*256,size=WPR_WIRE_SIZE-start;if(size>256)size=256;world_values_hex(bytes+start,size,hex);
    int n=snprintf(out,capacity,"WORLD_POSITION_RNG schema=1 event=%u part=%u data=%s",number,part,hex);return n<0||(size_t)n>=capacity?-1:n;
}
static int wpr_status(char *out,size_t size){
    char digest[65];hex32(wpr_digest,digest);AcquireSRWLockShared(&wpr_lock);
    int n=snprintf(out,size,"schema=1 composition=%u capacity=%u reserved=%u complete=%u exported=%u rows=%u calls=%llu open=%llu peak=%llu restore_open=%llu closed=%u failures=%llu dropped=%llu debt=%llu used=%u seed=%016llx digest=%s",
        wpr_composition,WPR_CAP,wpr_reserved,wpr_ready(),wpr_exported,wpr_ready()*WPR_ROWS,(unsigned long long)wpr_calls,
        (unsigned long long)wpr_open,(unsigned long long)wpr_peak,(unsigned long long)wpr_restore_open,wpr_closed,
        (unsigned long long)wpr_failures,(unsigned long long)wpr_dropped,(unsigned long long)wpr_debt,wpr_used,(unsigned long long)wpr_seed,digest);
    ReleaseSRWLockShared(&wpr_lock);return n<0||(size_t)n>=size?-1:n;
}
static int lua_world_position_rng_config(lua_State *L){size_t length=0;const char *text=p_lua_tolstring(L,1,&length);p_lua_pushnumber(L,world_position_rng_configure(text,length));return 1;}
static int lua_world_position_rng_cursor(lua_State *L){if(GetCurrentThreadId()!=ui_thread||!wpr_started)p_lua_pushnil(L);else p_lua_pushnumber(L,wpr_exported);return 1;}
static int lua_world_position_rng_status(lua_State *L){
    if(GetCurrentThreadId()!=ui_thread||!wpr_started){p_lua_pushnil(L);return 1;}char out[1024];int n=wpr_status(out,sizeof(out));
    if(n<0)p_lua_pushnil(L);else p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_world_position_rng_row(lua_State *L){
    double row=p_lua_tonumber(L,1);unsigned rows=wpr_ready()*WPR_ROWS;
    if(GetCurrentThreadId()!=ui_thread||!wpr_started||!isfinite(row)||row<0||floor(row)!=row){p_lua_pushnil(L);return 1;}
    if(!row){p_lua_pushnumber(L,rows);return 1;}if(row>rows||row!=wpr_exported+1){p_lua_pushnil(L);return 1;}
    unsigned number=(unsigned)row-1;char out[1024];int n=wpr_format(&wpr_events[number/WPR_ROWS],number/WPR_ROWS+1,number%WPR_ROWS,out,sizeof(out));
    if(n<0)p_lua_pushnil(L);else{++wpr_exported;p_lua_pushlstring(L,out,(size_t)n);}return 1;
}
