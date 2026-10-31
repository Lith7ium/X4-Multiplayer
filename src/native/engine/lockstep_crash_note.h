/* Crash note (native_gate_crash_note, default off; no stop reason: the process is ending).
 * An unhandled exception anywhere in X4 ends the process without a word in the gate's log
 * (s2-off: 0xC0000005 after tick 1599, no dump, no fault line). With the note armed, X4's
 * last-chance filter first writes one line, as present_fault does for the presentation's own
 * scopes: code, address (X4 RVA, or module+offset), access and data address, registers, the
 * unwound chain, the tick, the thread and the gate stages. Then the previous filter runs (X4's
 * crash reporter), so X4 ends exactly as it would. Nothing here runs unless the process is
 * already ending; no heap, the file opened per call and appended to (the lane profile, beside
 * the debug log; x4lan-debug.txt as well when X4 shares it). */
#define CRASH_NOTE_CHAIN 16u
static WCHAR crash_note_path[2][MAX_PATH];
static LPTOP_LEVEL_EXCEPTION_FILTER crash_note_previous;
static volatile LONG crash_note_armed,crash_note_entered;
static char crash_note_line[2048];
static void crash_note_where(uintptr_t at,char *out,size_t capacity) {
    uintptr_t base=(uintptr_t)image;HMODULE module=NULL;WCHAR name[MAX_PATH];
    if(base && at>=base && at-base<VPL_IMAGE_SPAN) {snprintf(out,capacity,"%llx",(unsigned long long)(at-base));return;}
    if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)at,&module) && module &&
       GetModuleFileNameW(module,name,MAX_PATH)) {
        const WCHAR *leaf=name;for(const WCHAR *c=name;*c;c++) if(*c=='\\' || *c=='/') leaf=c+1;
        snprintf(out,capacity,"%ls+%llx",leaf,(unsigned long long)(at-(uintptr_t)module));return;
    }
    snprintf(out,capacity,"@%llx",(unsigned long long)at);
}
/* The line, without writing it (tests call this directly). */
static int crash_note_text(const EXCEPTION_POINTERS *info,char *out,size_t capacity) {
    const EXCEPTION_RECORD *e=info->ExceptionRecord;const CONTEXT *c=info->ContextRecord;char where[96];
    DWORD thread=GetCurrentThreadId();
    crash_note_where((uintptr_t)e->ExceptionAddress,where,sizeof(where));
    int n=snprintf(out,capacity,"[X4LAN] NATIVE_GATE CRASH tick=%llu code=%08lx at=%s access=%llu data=%llx thread=%s "
        "rax=%llx rbx=%llx rcx=%llx rdx=%llx rsi=%llx rdi=%llx r8=%llx r9=%llx rsp=%llx pilot=%s chain=",
        (unsigned long long)stats.issued,(unsigned long)e->ExceptionCode,where,
        e->NumberParameters>0?(unsigned long long)e->ExceptionInformation[0]:0ull,e->NumberParameters>1?(unsigned long long)e->ExceptionInformation[1]:0ull,
        thread==ui_thread?"frame":thread==main_thread?"main":"other",
        (unsigned long long)c->Rax,(unsigned long long)c->Rbx,(unsigned long long)c->Rcx,(unsigned long long)c->Rdx,(unsigned long long)c->Rsi,
        (unsigned long long)c->Rdi,(unsigned long long)c->R8,(unsigned long long)c->R9,(unsigned long long)c->Rsp,pilot_stage);
    CONTEXT walk=*c;
    for(unsigned frame=0;frame<CRASH_NOTE_CHAIN && n>0 && (size_t)n+100<capacity && walk.Rip;frame++) {
        crash_note_where((uintptr_t)walk.Rip,where,sizeof(where));
        int more=snprintf(out+n,capacity-(size_t)n,"%s%s",frame?">":"",where);
        if(more<0) break;
        n+=more;
        DWORD64 module=0;PRUNTIME_FUNCTION fn=RtlLookupFunctionEntry(walk.Rip,&module,NULL);
        if(fn) {PVOID handler=NULL;DWORD64 frame_base=0;RtlVirtualUnwind(UNW_FLAG_NHANDLER,module,walk.Rip,fn,&walk,&handler,&frame_base,NULL);}
        else {DWORD64 back=0;if(!read_local((const void*)(uintptr_t)walk.Rsp,&back,8)) break;walk.Rip=back;walk.Rsp+=8;}
    }
    if(n>0 && (size_t)n+2<capacity) {out[n++]='\r';out[n++]='\n';out[n]=0;}
    return n>0 && (size_t)n<capacity?n:0;
}
static void crash_note_write(const char *text,int length) {
    for(unsigned i=0;i<2;i++) {
        if(!crash_note_path[i][0]) continue;
        HANDLE file=CreateFileW(crash_note_path[i],FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
        if(file==INVALID_HANDLE_VALUE) continue;
        DWORD done=0;WriteFile(file,text,(DWORD)length,&done,NULL);FlushFileBuffers(file);CloseHandle(file);
    }
}
static LONG WINAPI crash_note_filter(EXCEPTION_POINTERS *info) {
    if(InterlockedCompareExchange(&crash_note_armed,0,0) && !InterlockedExchange(&crash_note_entered,1)) {
        int n=crash_note_text(info,crash_note_line,sizeof(crash_note_line));
        if(n) crash_note_write(crash_note_line,n);
    }
    return crash_note_previous?crash_note_previous(info):EXCEPTION_CONTINUE_SEARCH;
}
/* gate.crash_note(note, log): the note file and (optional) the debug log, as UTF-8 paths. */
static int crash_note_arm(const char *note,const char *log) {
    if(!note || !*note || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,note,-1,crash_note_path[0],MAX_PATH)) return 0;
    crash_note_path[1][0]=0;
    if(log && *log && !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,log,-1,crash_note_path[1],MAX_PATH)) crash_note_path[1][0]=0;
    if(!InterlockedCompareExchange(&crash_note_armed,1,0)) {
        LPTOP_LEVEL_EXCEPTION_FILTER previous=SetUnhandledExceptionFilter(crash_note_filter);
        if(previous!=crash_note_filter) crash_note_previous=previous;   /* a UI reload arms again: keep X4's */
    }
    return 1;
}
