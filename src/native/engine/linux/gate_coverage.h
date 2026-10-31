/* Function coverage on the Linux X4 9.00 executable (lockstep_coverage.h, Option B evidence E1): the list is
 * coverage.txt next to this module (virtual addresses), the marks are int3 in the PT_LOAD R E segment, a SIGTRAP
 * handler takes them (chained to the one before it for any other trap), and the pages stay r-w-x for the window.
 * Diagnostic, default off, no stop reason. */
#define COV_PAGES 2048u
static uintptr_t cov_pages[COV_PAGES];
static unsigned cov_page_count;
static struct sigaction cov_prior;
static int cov_handler_installed;
#define COV_BASE LX_IMAGE_BASE
#include "lockstep_coverage.h"
static int cov_in_text(uintptr_t address) {return address>=(uintptr_t)LX(LX_TEXT_VA) && address<(uintptr_t)LX(LX_TEXT_END_VA);}
static void cov_flush(uintptr_t address) {(void)address;} /* x86 instruction fetch is coherent with stores */
static int cov_page_open(uintptr_t address) {
    const uintptr_t page=address&~(uintptr_t)(GATE_PAGE-1);
    for(unsigned i=0;i<cov_page_count;i++) if(cov_pages[i]==page) return 1;
    if(cov_page_count>=COV_PAGES || mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC)) return 0;
    cov_pages[cov_page_count++]=page;return 1;
}
static int cov_pages_close(void) {
    int ok=1;
    for(unsigned i=0;i<cov_page_count;i++) if(mprotect((void*)cov_pages[i],GATE_PAGE,PROT_READ|PROT_EXEC)) ok=0;
    cov_page_count=0;return ok;
}
static void cov_signal(int sig,siginfo_t *info,void *context) {
    ucontext_t *uc=context;const uintptr_t rip=(uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
    if(rip && cov_trap(rip-1)) {uc->uc_mcontext.gregs[REG_RIP]=(greg_t)(rip-1);return;}
    if(cov_prior.sa_flags&SA_SIGINFO) {if(cov_prior.sa_sigaction) {cov_prior.sa_sigaction(sig,info,context);return;}}
    else if(cov_prior.sa_handler!=SIG_DFL && cov_prior.sa_handler!=SIG_IGN) {cov_prior.sa_handler(sig);return;}
    if(cov_prior.sa_handler==SIG_IGN) return;
    struct sigaction fallback;memset(&fallback,0,sizeof(fallback));fallback.sa_handler=SIG_DFL;sigemptyset(&fallback.sa_mask);
    sigaction(SIGTRAP,&fallback,NULL);raise(SIGTRAP);
}
static int cov_install_handler(void) {
    if(cov_handler_installed) return 1;
    struct sigaction action;memset(&action,0,sizeof(action));action.sa_sigaction=cov_signal;action.sa_flags=SA_SIGINFO|SA_RESTART;sigemptyset(&action.sa_mask);
    if(sigaction(SIGTRAP,&action,&cov_prior)) return 0;
    cov_handler_installed=1;return 1;
}
/* coverage.txt next to this module. */
static int cov_load(void) {
    Dl_info info;char path[PATH_MAX];
    if(!dladdr((void*)cov_load,&info) || !info.dli_fname) return 0;
    const char *slash=strrchr(info.dli_fname,'/');size_t dir=slash?(size_t)(slash-info.dli_fname)+1:0;
    if(dir+sizeof("coverage.txt")>sizeof(path)) return 0;
    memcpy(path,info.dli_fname,dir);memcpy(path+dir,"coverage.txt",sizeof("coverage.txt"));
    FILE *f=fopen(path,"rb");if(!f) return 0;
    static char text[COV_MAX*20];size_t n=fread(text,1,sizeof(text),f);const int whole=feof(f);fclose(f);
    return whole && cov_load_text(text,n);
}
static int lua_coverage(lua_State *L) {
    double ticks=p_lua_tonumber(L,1);int ok=isfinite(ticks) && ticks>=1 && ticks<=6000 && floor(ticks)==ticks;
    if(ok && !cov_armed && !cov_count && !cov_load()) {cov_stage="list";ok=0;}
    p_lua_pushnumber(L,ok && cov_arm((unsigned)ticks));return 1;
}
static int lua_coverage_frame(lua_State *L) {
    double tick=p_lua_tonumber(L,1);
    if(accept_ui() && isfinite(tick) && tick>=1 && floor(tick)==tick) cov_frame((uint64_t)tick);
    p_lua_pushnumber(L,1);return 1;
}
static int lua_coverage_status(lua_State *L) {
    char out[384];int n=accept_ui()?cov_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_coverage_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[128];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && floor(index)==index) n=cov_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
