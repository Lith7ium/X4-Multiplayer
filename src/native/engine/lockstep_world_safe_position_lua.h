/* Separate passive schema; the common prefix codec is not Position admission. */
#define GPS_WIRE_SIZE 6184u
#define GPS_ROW_BYTES 256u
#define GPS_ROWS 25u
static unsigned gps_encode(const gps_record *r,unsigned number,unsigned char out[GPS_WIRE_SIZE]){
    if(wp_encode(&r->e,number,out)!=WP_WIRE_SIZE)return 0;unsigned char *p=out+WP_WIRE_SIZE;
    for(unsigned phase=0;phase<2;phase++)for(unsigned i=0;i<7;i++)wui_put(&p,r->witness[phase][i],8);
    for(unsigned phase=0;phase<2;phase++)wp_encode_object(&p,&r->reference[phase]);
    return (unsigned)(p-out);
}
static unsigned gps_ready(void){unsigned n=0,limit=gps_reserved>GPS_CAP?GPS_CAP:gps_reserved;while(n<limit&&InterlockedCompareExchange(&gps_records[n].e.complete,0,0))++n;return n;}
static int gps_status(char *out,size_t size){
    /* Common restoration is sampled under its owning lock; never take it from
     * a passive callback or while holding gps_lock across an original. */
    AcquireSRWLockShared(&wp_lock);AcquireSRWLockShared(&gps_lock);
    int n=snprintf(out,size,"schema=1 capacity=%u read_limit=%u byte_limit=%u wire_size=%u row_bytes=%u reserved=%u complete=%u exported=%u rows=%u calls=%llu open=%llu peak=%llu restore_open=%llu closed=%u failures=%llu dropped=%llu owned=%u protection_pending=%u flush_pending=%u patch_failures=%llu patch_errors=%u",
        GPS_CAP,WP_READS,WP_BYTES,GPS_WIRE_SIZE,GPS_ROW_BYTES,gps_reserved,gps_ready(),gps_exported,gps_ready()*GPS_ROWS,
        (unsigned long long)gps_calls,(unsigned long long)gps_open,(unsigned long long)gps_peak,(unsigned long long)gps_restore_open,
        (unsigned)(gps_closing!=0),(unsigned long long)gps_failures,(unsigned long long)gps_dropped,wp_patch_mask(0),wp_patch_mask(1),wp_patch_mask(2),
        (unsigned long long)wp_patch_failures,(unsigned)wp_patch_errors);
    ReleaseSRWLockShared(&gps_lock);ReleaseSRWLockShared(&wp_lock);return n<0||(size_t)n>=size?-1:n;
}
static int gps_format(const gps_record *r,unsigned number,unsigned part,char *out,size_t size){
    unsigned char bytes[GPS_WIRE_SIZE];char hex[GPS_ROW_BYTES*2+1];
    if(part>=GPS_ROWS||gps_encode(r,number,bytes)!=GPS_WIRE_SIZE)return -1;
    unsigned start=part*GPS_ROW_BYTES,length=GPS_WIRE_SIZE-start;if(length>GPS_ROW_BYTES)length=GPS_ROW_BYTES;
    world_values_hex(bytes+start,length,hex);
    int n=snprintf(out,size,"WORLD_SAFE_POSITION schema=1 event=%u part=%u data=%s",number,part,hex);
    return n<0||(size_t)n>=size?-1:n;
}
static int lua_world_safe_position_config(lua_State *L){p_lua_pushnumber(L,world_safe_position_configure());return 1;}
static int lua_world_safe_position_cursor(lua_State *L){if(GetCurrentThreadId()!=ui_thread||!gps_started)p_lua_pushnil(L);else p_lua_pushnumber(L,gps_exported);return 1;}
static int lua_world_safe_position_status(lua_State *L){
    if(GetCurrentThreadId()!=ui_thread||!gps_started){p_lua_pushnil(L);return 1;}char out[1024];int n=gps_status(out,sizeof(out));
    if(n<0)p_lua_pushnil(L);else p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_world_safe_position_row(lua_State *L){
    double row=p_lua_tonumber(L,1);unsigned rows=gps_ready()*GPS_ROWS;
    if(GetCurrentThreadId()!=ui_thread||!gps_started||!isfinite(row)||row<0||floor(row)!=row){p_lua_pushnil(L);return 1;}
    if(!row){p_lua_pushnumber(L,rows);return 1;}
    if(row>rows||row!=gps_exported+1){p_lua_pushnil(L);return 1;}
    unsigned index=(unsigned)row-1;char out[2048];int n=gps_format(&gps_records[index/GPS_ROWS],index/GPS_ROWS+1,index%GPS_ROWS,out,sizeof(out));
    if(n<0)p_lua_pushnil(L);else{++gps_exported;p_lua_pushlstring(L,out,(size_t)n);}return 1;
}
