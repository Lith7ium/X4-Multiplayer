/* The published shared page is permanently RX. Installers validate bytes only;
 * none may change this page's protection while another stub can execute. */
#define THUNK_PAGE_SIZE 4096u
#define THUNK_MAX_SLOT_SIZE 320u
static int thunk_page_rx(const unsigned char *page) {
    MEMORY_BASIC_INFORMATION memory;
    if(!page || !VirtualQuery(page,&memory,sizeof(memory)) || memory.State!=MEM_COMMIT ||
       memory.Protect!=PAGE_EXECUTE_READ || memory.AllocationBase!=page || memory.BaseAddress!=page ||
       memory.RegionSize<THUNK_PAGE_SIZE) return 0;
    return 1;
}
static int thunk_bytes_match(unsigned offset,const void *expected,size_t size) {
    unsigned char actual[THUNK_PAGE_SIZE];
    if(!expected || !size || offset>THUNK_PAGE_SIZE || size>THUNK_PAGE_SIZE-offset || !thunk_page_rx(thunk)) return 0;
    SIZE_T copied=0;
    return ReadProcessMemory(GetCurrentProcess(),thunk+offset,actual,size,&copied) && copied==size && !memcmp(actual,expected,size);
}
static int thunk_slot_ready(unsigned offset);
