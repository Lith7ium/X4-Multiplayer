/* Only relocated stack-changing prefixes have entries here. These
 * records describe copied instructions, never the remainder of the native
 * prologue. They have no exception handlers and do not promise observer cleanup
 * when an engine C++ exception crosses a C wrapper. */
typedef struct {
    unsigned offset,prefix_size,info_offset,info_size;
    unsigned char info[20];
} thunk_unwind_recipe;
static const thunk_unwind_recipe thunk_unwind_recipes[]={
    {320,15,32,20,{1,15,8,0,15,0xf0,13,0xe0,11,0xd0,9,0xc0,7,0x70,6,0x60,5,0x30,4,0x50}},
    {576,6,48,8,{1,6,2,0,6,0x32,2,0x70}},
    {736,5,48,8,{1,5,2,0,5,0x60,4,0x30}},
    {2304,6,32,8,{1,6,2,0,6,0xb2,2,0x30}},
    {3456,9,48,12,{1,9,3,0,9,0x68,2,0,4,0x62,0,0}},
};
/* Static storage is pinned with the DLL. A registered table and the sealed page
 * remain resident even if the subsequent native CAS fails or STOP restores it. */
static RUNTIME_FUNCTION thunk_unwind_functions[]={
    {320,348,352},{576,595,624},{736,754,784},{2304,2323,2336},
    {3456,3478,3504},
};
#define THUNK_UNWIND_COUNT (sizeof(thunk_unwind_recipes)/sizeof(*thunk_unwind_recipes))
_Static_assert(sizeof(thunk_unwind_functions)/sizeof(*thunk_unwind_functions)==THUNK_UNWIND_COUNT,"unwind recipe/table count");
static DWORD64 thunk_unwind_bases[THUNK_UNWIND_COUNT];
static SRWLOCK thunk_unwind_lock=SRWLOCK_INIT;
typedef BOOLEAN (WINAPI *thunk_unwind_add_fn)(PRUNTIME_FUNCTION,DWORD,DWORD64);
static const thunk_unwind_recipe *thunk_unwind_at(unsigned offset) {
    for(unsigned i=0;i<THUNK_UNWIND_COUNT;i++) if(thunk_unwind_recipes[i].offset==offset) return &thunk_unwind_recipes[i];
    return NULL;
}
static int thunk_unwind_prepare_using(unsigned offset,thunk_unwind_add_fn add) {
    const thunk_unwind_recipe *r=thunk_unwind_at(offset);if(!r || !add) return 0;
    unsigned i=(unsigned)(r-thunk_unwind_recipes);int ok=0;
    AcquireSRWLockExclusive(&thunk_unwind_lock);
    RUNTIME_FUNCTION *f=&thunk_unwind_functions[i];DWORD64 base=(DWORD64)(uintptr_t)thunk;
    if(!thunk_slot_ready(offset) || f->BeginAddress!=offset || f->EndAddress!=offset+r->prefix_size+13 ||
       f->UnwindData!=offset+r->info_offset || (thunk_unwind_bases[i] && thunk_unwind_bases[i]!=base)) goto done;
    if(!thunk_unwind_bases[i]) {
        if(!add(f,1,base)) goto done;
        thunk_unwind_bases[i]=base; /* Never revoke a successful registration. */
    }
    DWORD64 first_base=0,last_base=0;
    ok=RtlLookupFunctionEntry(base+f->BeginAddress,&first_base,NULL)==f && first_base==base &&
       RtlLookupFunctionEntry(base+f->EndAddress-1,&last_base,NULL)==f && last_base==base;
done:
    ReleaseSRWLockExclusive(&thunk_unwind_lock);return ok;
}
static int thunk_unwind_prepare(unsigned offset) {return thunk_unwind_prepare_using(offset,RtlAddFunctionTable);}
