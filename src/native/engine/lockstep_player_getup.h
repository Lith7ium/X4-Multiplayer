/* Exclusive opt-in GetUp fixture. Persistent claim precedes public native call.
 * This core neither changes a controller nor establishes a native lifetime lease. */
#ifndef LOCKSTEP_PLAYER_GETUP_H
#define LOCKSTEP_PLAYER_GETUP_H
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#define PGETUP_TICK 30u
#define PGETUP_LAST 120u
typedef bool (*pgetup_original)(void);
typedef int (*pgetup_guard)(void *,uint64_t,int);
typedef struct {
    SRWLOCK lock;
    char run[65];uint64_t epoch,processed;
    unsigned configured,closing,claimed,accepted;
    unsigned phase;uint64_t publication;uintptr_t published_controller;
    uint64_t calls,returns,unwinds,open,stop_open,failures;
} pgetup_ledger;
typedef struct pgetup_scope {struct pgetup_scope *parent;pgetup_ledger *ledger;} pgetup_scope;
static _Thread_local pgetup_scope *pgetup_current;
static int pgetup_run(const char *run) {
    if(!run) return 0;
    for(unsigned i=0;i<64;i++) if(!((run[i]>='0'&&run[i]<='9')||(run[i]>='a'&&run[i]<='f'))) return 0;
    return !run[64];
}
static inline int pgetup_configure(pgetup_ledger *l,const char *run,uint64_t epoch) {
    if(!pgetup_run(run) || epoch!=1) return 0;
    int ok=0;AcquireSRWLockExclusive(&l->lock);
    if(!l->closing && !l->configured) {memcpy(l->run,run,65);l->epoch=epoch;l->configured=1;ok=1;}
    else if(l->configured && !l->closing) ok=l->epoch==epoch && !strcmp(l->run,run);
    ReleaseSRWLockExclusive(&l->lock);return ok;
}
static void pgetup_stop(pgetup_ledger *l) {
    AcquireSRWLockExclusive(&l->lock);l->closing=1;if(l->open>l->stop_open) l->stop_open=l->open;
    ReleaseSRWLockExclusive(&l->lock);
}
static uint64_t pgetup_request(pgetup_ledger *l) {
    return pgetup_current && pgetup_current->ledger==l?1:0;
}
static int pgetup_words(uint64_t tick,const char *a,const char *b) {
    return tick>=1 && tick<=PGETUP_LAST && a && b && !strcmp(b,"00000000") &&
        !strcmp(a,tick==PGETUP_TICK?"87010000":"00000000");
}
static int pgetup_batch(pgetup_ledger *l,const char *run,uint64_t epoch,uint64_t tick,
                         const char *a,const char *b,pgetup_guard guard,void *user,pgetup_original original) {
    int action=tick==PGETUP_TICK,ok=0;
    /* The guard must only read the actual admitted batch/identity. Every failure
     * poisons this fixture. A later retry cannot repair an uncertain request. */
    int preflight=pgetup_run(run) && pgetup_words(tick,a,b) && original && guard && guard(user,tick,action);
    AcquireSRWLockExclusive(&l->lock);
    if(!preflight || !l->configured || l->closing || l->failures || l->epoch!=epoch ||
       strcmp(l->run,run) || tick!=l->processed+1 || (action && l->claimed)) ++l->failures;
    else {
        l->processed=tick;ok=1;
        if(action) {l->claimed=1;++l->calls;++l->open;}
    }
    ReleaseSRWLockExclusive(&l->lock);
    if(!ok || !action) return ok;
    pgetup_scope scope={pgetup_current,l};pgetup_current=&scope;bool accepted=false;
    __try {accepted=original();}
    __finally {
        AcquireSRWLockExclusive(&l->lock);
        if(AbnormalTermination()) {++l->unwinds;++l->failures;}
        else {++l->returns;l->accepted=accepted?1:0;if(!accepted) ++l->failures;}
        if(!l->open) ++l->failures;else --l->open;
        ok=!l->failures && !l->closing;
        ReleaseSRWLockExclusive(&l->lock);pgetup_current=scope.parent;
    }
    return ok;
}
#endif
