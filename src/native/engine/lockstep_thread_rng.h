/* Census diagnostic, read-only. Every engine thread keeps its RNG word at
 * TLS slot0+0x318. Per-frame seeding covers only the simulation thread, while
 * c002 showed navigation refreshes landing on different frames. Export every
 * thread's word so two clients can be compared thread by thread. Threads are
 * labelled by start address (image RVA when inside X4) and creation order;
 * the words are read while their threads may run, so a row is a sample, not
 * an atomic snapshot. */
#include <tlhelp32.h>
typedef LONG (WINAPI *thread_rng_query_fn)(HANDLE,int,void *,ULONG,ULONG *);
typedef LONG (WINAPI *thread_rng_name_fn)(HANDLE,PWSTR *);
typedef struct {uint64_t start,created,word;DWORD id;char name[40];} thread_rng_row;
typedef struct {LONG status;void *teb;uintptr_t process,thread;uintptr_t affinity;LONG priority,base;} thread_rng_basic;
static int thread_rng_compare(const void *a,const void *b) {
    const thread_rng_row *x=a,*y=b;
    if(x->start!=y->start) return x->start<y->start?-1:1;
    int named=strcmp(x->name,y->name);if(named) return named;
    if(x->created!=y->created) return x->created<y->created?-1:1;
    return x->id<y->id?-1:x->id>y->id;
}
static int thread_rng_census(char *out,size_t capacity) {
    static thread_rng_query_fn query;
    if(!query) query=(thread_rng_query_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationThread");
    static thread_rng_name_fn describe;
    if(!describe) describe=(thread_rng_name_fn)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetThreadDescription");
    if(!query || !image) return 0;
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(snap==INVALID_HANDLE_VALUE) return 0;
    static thread_rng_row rows[512];unsigned count=0,skipped=0;DWORD self=GetCurrentProcessId();
    THREADENTRY32 entry;entry.dwSize=sizeof(entry);
    for(BOOL ok=Thread32First(snap,&entry);ok;ok=Thread32Next(snap,&entry)) {
        if(entry.th32OwnerProcessID!=self) continue;
        HANDLE thread=OpenThread(THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);
        if(!thread) {++skipped;continue;}
        thread_rng_basic basic;void *start=NULL;FILETIME created,exited,kernel,user;
        int got=query(thread,0,&basic,sizeof(basic),NULL)>=0 && query(thread,9,&start,sizeof(start),NULL)>=0 &&
                GetThreadTimes(thread,&created,&exited,&kernel,&user);
        CloseHandle(thread);
        void **slots=NULL,*slot=NULL;uint64_t word=0;
        if(!got || !read_local((unsigned char*)basic.teb+0x58,&slots,8) || !slots || !read_local(slots,&slot,8) || !slot ||
           !read_local((unsigned char*)slot+0x318,&word,8)) {++skipped;continue;}
        /* X4 names its threads (SetThreadDescription); the name pairs roles across processes. */
        char name[40]="-";HANDLE named=OpenThread(THREAD_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ThreadID);PWSTR wide=NULL;
        if(named && describe && describe(named,&wide)>=0 && wide) {
            unsigned k=0;for(;wide[k] && k<sizeof(name)-1;k++) {WCHAR c=wide[k];name[k]=(c>='0'&&c<='9')||(c>='A'&&c<='Z')||(c>='a'&&c<='z')?(char)c:'_';}
            name[k]=0;if(!k) strcpy(name,"-");
        }
        if(wide) LocalFree(wide);
        if(named) CloseHandle(named);
        if(count==512) {++skipped;continue;}
        uintptr_t s=(uintptr_t)start,base=(uintptr_t)image;
        rows[count].start=s>=base && s<base+0x6fa9000?s-base:(1ULL<<63)|(s&0xffff);
        rows[count].created=((uint64_t)created.dwHighDateTime<<32)|created.dwLowDateTime;
        rows[count].word=word;strcpy(rows[count].name,name);rows[count].id=entry.th32ThreadID;++count;
    }
    CloseHandle(snap);
    qsort(rows,count,sizeof(rows[0]),thread_rng_compare);
    int n=snprintf(out,capacity,"threads=%u skipped=%u ui=%lu main=%lu rows=",count,skipped,(unsigned long)ui_thread,(unsigned long)main_thread);
    for(unsigned i=0;i<count && n>0 && (size_t)n<capacity;i++)
        n+=snprintf(out+n,capacity-(size_t)n,"%s%llx.%s:%lu:%016llx",i?",":"",(unsigned long long)rows[i].start,rows[i].name,
                    (unsigned long)rows[i].id,(unsigned long long)rows[i].word);
    return n>0 && (size_t)n<capacity?n:0;
}
