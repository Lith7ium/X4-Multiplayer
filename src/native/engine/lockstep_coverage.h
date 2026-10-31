/* Diagnostic, DEFAULT OFF: which functions of a list run in gated ticks 1..N (native_gate_coverage = N, at most
 * 6000; the list is coverage.txt next to the gate binary: one address per line, hexadecimal, Windows image RVAs or
 * Linux virtual addresses, at most COV_MAX). Option B evidence E1 (reports/LOCKSTEP-LINUX-PORT-PLAN-20261030.txt):
 * the floating-point inventory's simulation functions, to count those that actually run.
 *
 * At the start of tick 1 (an admitted frame, on the MoveWorker) each listed function's first byte is saved and
 * replaced by int3, page by page; an entry already starting with a jmp (0xe9) or an int3 is another hook's and is
 * skipped (counted). The first execution of a marked function traps on whatever thread runs it: the handler (a
 * signal handler on Linux, a first vectored handler on Windows) finds the address in its table, puts the byte back,
 * notes the tick and resumes at the entry, so each function traps at most once and then runs unmodified. At the end
 * of tick N, and at stop, every byte not yet put back is restored. A trap not at a marked address passes on.
 * Run it with as few other modules as possible: a module that hashes code containing a marked entry every tick
 * would see the int3 until the function first runs. COVERAGE_ROW: the functions that ran, in list order, with the
 * tick of their first run. */
#define COV_MAX 8192u
#ifndef COV_BASE
#define COV_BASE 0u /* listed addresses are image RVAs; Linux lists virtual addresses (COV_BASE 0x400000) */
#endif
#define COV_TABLE 16384u
typedef struct {uintptr_t address;volatile LONG64 tick;unsigned char saved;volatile LONG state;} cov_site_t; /* state 0 off, 1 marked, 2 ran, 3 restored */
static cov_site_t cov_sites[COV_MAX];
static unsigned cov_table[COV_TABLE]; /* site index + 1, open addressing on the address */
static unsigned cov_count,cov_ticks,cov_marked,cov_skipped,cov_invalid,cov_armed,cov_open,cov_done;
static volatile LONG64 cov_hits,cov_foreign,cov_failures;
static const char *cov_stage="idle";
/* Platform: the page holding address made writable and executable for the window (idempotent per page), all such
 * pages back to read and execute, inside the executable's code. The pages stay writable while marks are out, so a
 * handler on any thread puts a byte back with a plain store: no protection change races another one. */
static int cov_page_open(uintptr_t address);
static int cov_pages_close(void);
static int cov_in_text(uintptr_t address);
static void cov_flush(uintptr_t address);
static int cov_code_write(uintptr_t address,unsigned char value) {*(volatile unsigned char*)address=value;cov_flush(address);return 1;}
static unsigned cov_slot(uintptr_t address) {return (unsigned)((address*0x9e3779b97f4a7c15ull)>>50)&(COV_TABLE-1u);}
static cov_site_t *cov_find(uintptr_t address) {
    for(unsigned n=0,h=cov_slot(address);n<COV_TABLE;n++,h=(h+1)&(COV_TABLE-1u)) {
        const unsigned i=cov_table[h];
        if(!i) return NULL;
        if(cov_sites[i-1].address==address) return &cov_sites[i-1];
    }
    return NULL;
}
/* One listed address (relative to the image); before arming. Duplicates are dropped. */
static int cov_add(uint64_t relative) {
    if(cov_armed || cov_count>=COV_MAX) return 0;
    if(relative<COV_BASE) {++cov_invalid;return 1;}
    const uintptr_t address=(uintptr_t)image+(uintptr_t)(relative-COV_BASE);
    if(!cov_in_text(address)) {++cov_invalid;return 1;}
    if(cov_find(address)) return 1;
    for(unsigned h=cov_slot(address);;h=(h+1)&(COV_TABLE-1u)) if(!cov_table[h]) {cov_table[h]=cov_count+1;break;}
    cov_sites[cov_count].address=address;cov_sites[cov_count].state=0;cov_sites[cov_count].tick=0;cov_count++;return 1;
}
/* The handler's half: 1 when the trap was ours and the entry is restored (the caller resumes at the entry). */
static int cov_trap(uintptr_t address) {
    cov_site_t *s=cov_find(address);
    if(!s) {InterlockedIncrement64(&cov_foreign);return 0;}
    const LONG state=InterlockedCompareExchange(&s->state,0,0);
    if(state==1 && InterlockedCompareExchange(&s->state,2,1)==1) {
        InterlockedExchange64(&s->tick,(LONG64)stats.issued);InterlockedIncrement64(&cov_hits);
        cov_code_write(address,s->saved);
        return 1;
    }
    /* Another thread is putting it back, or did: the byte is (or is about to be) the original. */
    for(unsigned spin=0;spin<1000000 && *(volatile unsigned char*)address==0xcc;spin++) {}
    return *(volatile unsigned char*)address!=0xcc;
}
static int cov_install_handler(void); /* platform */
/* At the start of tick 1, on the MoveWorker. */
static void cov_mark(void) {
    if(!cov_armed || cov_open || cov_done) return;
    cov_open=1;
    if(!cov_install_handler()) {cov_stage="handler";InterlockedIncrement64(&cov_failures);return;}
    for(unsigned i=0;i<cov_count;i++) {
        cov_site_t *s=&cov_sites[i];const unsigned char first=*(volatile unsigned char*)s->address;
        if(first==0xe9 || first==0xcc) {++cov_skipped;continue;}
        if(!cov_page_open(s->address)) {InterlockedIncrement64(&cov_failures);continue;}
        s->saved=first;InterlockedExchange(&s->state,1);
        cov_code_write(s->address,0xcc);
        ++cov_marked;
    }
    cov_stage="marked";
}
/* After tick N, and at stop: every mark not yet hit goes back. */
static void cov_unmark(void) {
    if(!cov_open || cov_done) return;
    for(unsigned i=0;i<cov_count;i++) {
        cov_site_t *s=&cov_sites[i];
        if(InterlockedCompareExchange(&s->state,3,1)==1) cov_code_write(s->address,s->saved);
    }
    if(!cov_pages_close()) InterlockedIncrement64(&cov_failures);
    cov_done=1;cov_stage="done";
}
/* Per admitted frame on the MoveWorker, at its start. */
static void cov_frame(uint64_t tick) {
    if(!cov_armed || cov_done) return;
    if(tick==1 && !cov_open) cov_mark();
    if(tick>cov_ticks) cov_unmark();
}
static int cov_arm(unsigned ticks) {
    if(cov_armed) return stats.active && ticks==cov_ticks;
    if(!ticks || ticks>6000 || !cov_count || !stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) {cov_stage="refused";return 0;}
    cov_ticks=ticks;cov_armed=1;cov_stage="armed";return 1;
}
static int cov_status(char *out,size_t cap) {
    int n=snprintf(out,cap,"armed=%u ticks=%u listed=%u invalid=%u marked=%u skipped=%u hits=%lld foreign=%lld failures=%lld stage=%s",
        cov_armed,cov_ticks,cov_count,cov_invalid,cov_marked,cov_skipped,(long long)cov_hits,(long long)cov_foreign,(long long)cov_failures,cov_stage);
    return n>0 && (size_t)n<cap?n:0;
}
/* Row i: the i-th listed function that ran (after the marks are off), with its first tick. */
static unsigned cov_row_cursor,cov_row_site;
static int cov_row(uint64_t index,char *out,size_t cap) {
    if(!cov_done) return 0;
    if(index<cov_row_cursor) {cov_row_cursor=0;cov_row_site=0;}
    for(;cov_row_site<cov_count;cov_row_site++) {
        const cov_site_t *s=&cov_sites[cov_row_site];
        if(s->state!=2) continue;
        if(cov_row_cursor==index) {
            int n=snprintf(out,cap,"index=%llu address=%llx tick=%lld",(unsigned long long)index,
                (unsigned long long)(s->address-(uintptr_t)image+COV_BASE),(long long)s->tick);
            ++cov_row_cursor;++cov_row_site;
            return n>0 && (size_t)n<cap?n:0;
        }
        ++cov_row_cursor;
    }
    return 0;
}
/* The list file: hexadecimal addresses, one per line, '#' comments. */
static int cov_load_text(const char *text,size_t size) {
    size_t i=0;
    while(i<size) {
        uint64_t value=0;int digits=0;
        if(text[i]=='#') {while(i<size && text[i]!='\n') i++;continue;}
        if(i+1<size && text[i]=='0' && (text[i+1]=='x' || text[i+1]=='X')) i+=2;
        for(;i<size;i++) {
            const char c=text[i];int d=c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:c>='A' && c<='F'?c-'A'+10:-1;
            if(d<0) break;
            value=value*16+(uint64_t)d;if(++digits>16) return 0;
        }
        if(digits && !cov_add(value)) return 0;
        while(i<size && text[i]!='\n') {if(text[i]!='\r' && text[i]!=' ' && text[i]!='\t') return 0;i++;}
        i++;
    }
    return 1;
}
