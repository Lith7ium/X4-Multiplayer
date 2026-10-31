/* native_gate_path_budget on the Linux X4 9.00 executable, the twin of lockstep_path_budget.h:
 * N full agent updates per batch and pass, the rest cheap, stock's round robin by count instead
 * of by clock. On a loaded machine the pass is the frame's largest item: the VPS's serial
 * pathing wait is about 15 ms with every agent on a full update. Included by gate_linux.c only.
 *
 * UpdateBatch's clock block starts at 0x13e33d0, 16-aligned, so one compare-exchange of its
 * first 16 bytes replaces it: keep the cheap flag where the block's cheap exit reads it
 * ([rsp+0x1f]), compare the loop index r13 with N-1, leave by the block's own exits (stay full
 * at 0x13e3374, turn cheap at 0x13e3425). The rest of the block is no longer reached.
 *
 * Armed on the UI thread, after the frame's pathing pass has been joined by the engine, so no
 * batch runs; restored only while no pass is open (a stuck pass leaves it patched and
 * stats.restored=0). The pin of the whole function is lx_sites' path_update_batch; these 16 bytes
 * are compared again before the swap, and a foreign value is never overwritten. */
#define LX_PBUD_SITE 0x13e33d0u
#define LX_PBUD_NEXT 0x13e3374u
#define LX_PBUD_CHEAP 0x13e3425u
#define LX_PBUD_MAX 100000u
static const unsigned char lx_pbud_native[16]={0x4d,0x8b,0x97,0xa0,0x03,0x00,0x00,0x48,0x8d,0x74,0x24,0x20,0xbf,0x04,0x00,0x00};
static unsigned __int128 pbud_ours;
static unsigned pbud_n;
static int pbud_restored;
static const char *pbud_stage="idle";
/* mov [rsp+0x1f],dl; cmp r13,N-1; jb next; jmp cheap; int3 */
static void pbud_emit(unsigned char out[16],unsigned n) {
    static const unsigned char head[7]={0x88,0x54,0x24,0x1f,0x49,0x81,0xfd};
    int32_t v=(int32_t)(n-1u);memcpy(out,head,7);memcpy(out+7,&v,4);
    out[11]=0x72;out[12]=(unsigned char)(int8_t)((int64_t)LX_PBUD_NEXT-(int64_t)(LX_PBUD_SITE+13));
    out[13]=0xeb;out[14]=(unsigned char)(int8_t)((int64_t)LX_PBUD_CHEAP-(int64_t)(LX_PBUD_SITE+15));
    out[15]=0xcc;
}
static int pbud_arm(unsigned n) {
    if(pbud_installed) return stats.active && n==pbud_n;
    if(!n || n>LX_PBUD_MAX || !stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread ||
       !path_installed || !(path_options&1)) {pbud_stage="state";return 0;}
    unsigned char *site=LX(LX_PBUD_SITE),code[16];unsigned __int128 native;
    if(memcmp(site,lx_pbud_native,16)) {pbud_stage="pins";return 0;}
    memcpy(&native,lx_pbud_native,16);pbud_emit(code,n);memcpy(&pbud_ours,code,16);
    if(!gate_code_swap128(site,native,pbud_ours)) {pbud_stage="swap";return 0;}
    pbud_n=n;pbud_restored=0;pbud_installed=1;pbud_stage="armed";return 1;
}
static int pbud_owned(void) {return !pbud_installed || !memcmp(LX(LX_PBUD_SITE),&pbud_ours,16);}
static void pbud_restore(void) {
    if(!pbud_installed) return;
    if(InterlockedCompareExchange(&path_pass_open,0,0)) {pbud_stage="pass_open";return;}
    unsigned __int128 native;memcpy(&native,lx_pbud_native,16);
    if(!gate_code_swap128(LX(LX_PBUD_SITE),pbud_ours,native)) {pbud_stage="restore_foreign";return;}
    pbud_installed=0;pbud_restored=1;pbud_stage="restored";
}
static int pbud_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"installed=%d n=%u restored=%d stage=%s",pbud_installed,pbud_n,pbud_restored,pbud_stage);
    return n>0 && (size_t)n<capacity?n:0;
}
static int lua_path_budget(lua_State *L) {
    double n=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(n) && n>=1 && n<=LX_PBUD_MAX && floor(n)==n && pbud_arm((unsigned)n));return 1;
}
static int lua_path_budget_status(lua_State *L) {
    char out[160];int n=accept_ui()?pbud_status(out,sizeof(out)):0;
    if(n>0) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
