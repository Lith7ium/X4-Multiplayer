/* Opt-in observer only. GetUp is never invoked here. No getters, ownership
 * acquisition, context writes or raw-watch rebinding. Exceptions propagate. */
#include "lockstep_player_origin.h"
#define PORIGIN_WORD 0x1abb00u
#define PORIGIN_SLOT 0x2a85288u
#define PORIGIN_STUB 2944u
static porigin_ledger origin_ledger;
static SRWLOCK origin_lock=SRWLOCK_INIT;
static unsigned origin_started,origin_closing,origin_sites,origin_imports,origin_first,origin_last;
static volatile LONG origin_bound;
static uint64_t origin_player_saved,origin_ship_saved,origin_runtime;
static uintptr_t origin_player;
static uint64_t origin_calls,origin_returns,origin_open,origin_peak,origin_restore_open,origin_prebind,origin_failures;
static uint64_t origin_unwinds;
static unsigned origin_ticket_rows,origin_edge_rows;
static char origin_token[80];
static unsigned __int128 origin_original_word,origin_patched_word;
static uint64_t origin_original_slot,origin_patched_slot;
static uint64_t origin_post(void *,void *,double,unsigned char);
static uint64_t origin_delivery(void *,void *,void *,double);
#include "lockstep_player_origin_pins.h"
static int origin_window(void) {
    return InterlockedCompareExchange(&origin_bound,0,0) && stats.active && boot_mode && boot_phase==3 &&
        stats.issued>=origin_first && stats.issued<=origin_last;
}
static int origin_enter(void) {
    AcquireSRWLockExclusive(&origin_lock);int ok=origin_started && !origin_closing && stats.active;
    if(ok) {++origin_calls;if(++origin_open>origin_peak) origin_peak=origin_open;if(!origin_bound) ++origin_prebind;}
    ReleaseSRWLockExclusive(&origin_lock);return ok;
}
static void origin_leave(int admitted,int abnormal) {
    if(!admitted) return;AcquireSRWLockExclusive(&origin_lock);
    if(abnormal) {++origin_unwinds;++origin_failures;} else ++origin_returns;
    if(!origin_open) ++origin_failures;else --origin_open;ReleaseSRWLockExclusive(&origin_lock);
}
static void origin_import_capture(void *object,uint64_t runtime,uint64_t saved) {
    AcquireSRWLockExclusive(&origin_lock);
    if(origin_started && !origin_closing && stats.active && saved==origin_player_saved) {
        if(origin_imports || origin_bound || !object || !runtime || !boot_mode || boot_phase<1 || boot_phase>=3) ++origin_failures;
        else {origin_player=(uintptr_t)object;origin_runtime=runtime;origin_imports=1;}
    }
    ReleaseSRWLockExclusive(&origin_lock);
}
typedef struct {
    uintptr_t universe,player,path[16];uint64_t runtime,path_runtime[16];unsigned count;
} origin_player_view;
static int origin_player_read(origin_player_view *out) {
    origin_player_view s={0};uintptr_t type;transition_registry_identity registry;
    if(!read_local(image+0x3d198f8,&s.universe,8) ||
       !transition_read_offset(read_local,s.universe,0x230,&s.player,8) || s.player!=origin_player ||
       !read_local((void*)s.player,&type,8) || type!=(uintptr_t)(image+0x2b329a8) ||
       !transition_read_offset(read_local,s.player,8,&s.runtime,8) || s.runtime!=origin_runtime ||
       world_identity_lookup(s.runtime)!=origin_player_saved ||
       !transition_registry_read(read_local,image+0x6cf1500,s.runtime,s.player,&registry)) return 0;
    uintptr_t object=s.player;
    for(unsigned i=0;i<16;i++) {
        if(!transition_read_offset(read_local,object,0x70,&object,8) || !object || object==s.player) return 0;
        for(unsigned j=0;j<i;j++) if(s.path[j]==object) return 0;
        s.path[i]=object;s.count=i+1;
        if(!transition_read_offset(read_local,object,8,&s.path_runtime[i],8) || !world_identity_lookup(s.path_runtime[i]) ||
           !transition_registry_read(read_local,image+0x6cf1500,s.path_runtime[i],object,&registry)) return 0;
        if(world_identity_lookup(s.path_runtime[i])==origin_ship_saved) {*out=s;return 1;}
    }
    return 0;
}
static int origin_identity(porigin_sample *sample) {
    origin_player_view a,b;transition_state ship;
    if(!origin_player_read(&a) || !transition_read((void*)a.path[a.count-1],NULL,&ship) ||
       ship.saved!=origin_ship_saved || strcmp(ship.owner,"player") || !ship.owns_agent ||
       !origin_player_read(&b) || memcmp(&a,&b,sizeof(a))) return 0;
    *sample=(porigin_sample){.binding={origin_player_saved,ship.saved,a.runtime,ship.runtime,a.player,
        ship.object,ship.agent,ship.faction},.controller=ship.controller,.tick=stats.issued};return 1;
}
static int origin_clock(const void *teb,uint64_t *bits) {
    uintptr_t slots,slot;unsigned char initialized;double time;
    if(!transition_read_offset(read_local,(uintptr_t)teb,0x58,&slots,8) || !read_local((void*)slots,&slot,8) ||
       !transition_read_offset(read_local,slot,0x10,&initialized,1) || initialized!=1 ||
       !transition_read_offset(read_local,slot,0x300,bits,8)) return 0;
    memcpy(&time,bits,8);return isfinite(time) && time>=0;
}
static int origin_integrity(void) {
    unsigned __int128 word;uint64_t slot;
    return read_local(image+PORIGIN_WORD,&word,16) && word==origin_patched_word &&
        read_local(image+PORIGIN_SLOT,&slot,8) && slot==origin_patched_slot;
}
static int origin_capture(void *teb,unsigned stage,const porigin_arguments *a,porigin_sample *out) {
    unsigned char event[24],again[24];uintptr_t type;uint64_t counter,reference;uint64_t tick=stats.issued;
    if(!origin_window() || (GetCurrentThreadId()!=main_thread && GetCurrentThreadId()!=ui_thread) || !origin_integrity() ||
       !read_local(a->event,event,sizeof(event))) return 0;
    memcpy(&type,event,8);memcpy(&counter,event+8,8);memcpy(&reference,event+16,8);
    if(type!=(uintptr_t)(image+0x2a85278) || (stage==PO_POST && (counter || reference)) ||
       !origin_identity(out) || !origin_clock(teb,&out->clock_bits) || !read_local(a->event,again,sizeof(again))) return 0;
    if(stage!=PO_AFTER) {
        uintptr_t controller_type;unsigned char player;
        if(!read_local((void*)out->controller,&controller_type,8) || controller_type!=(uintptr_t)(image+0x2c39488) ||
           !transition_read_offset(read_local,out->binding.agent,0xf79,&player,1) || player!=1) return 0;
    }
    uintptr_t final_type;memcpy(&final_type,again,8);
    out->transition_order=(uint64_t)InterlockedCompareExchange64(&transition_order,0,0);
    /* Native reference counters can change after posting. Only construction
     * preimage is compared byte-for-byte; delivery requires the exact type. */
    return final_type==type && (stage!=PO_POST || !memcmp(event,again,sizeof(event))) && origin_window() && stats.issued==tick;
}
static uint64_t origin_observe_post(porigin_post_fn original,void *receiver,void *event,double due,unsigned char flags,
                                    uintptr_t caller,void *teb) {
    int admitted=origin_enter();unsigned rva=caller>=(uintptr_t)image && caller<(uintptr_t)(image+0x6fa9000)?(unsigned)(caller-(uintptr_t)image):0;
    uint64_t result=0;
    __try {result=porigin_observe_post(&origin_ledger,origin_capture,teb,original,receiver,event,due,flags,rva);}
    __finally {origin_leave(admitted,AbnormalTermination());}
    return result;
}
static uint64_t origin_observe_delivery(porigin_delivery_fn original,void *event,void *receiver,void *source,double due,
                                        uintptr_t caller,void *teb) {
    int admitted=origin_enter();unsigned rva=caller>=(uintptr_t)image && caller<(uintptr_t)(image+0x6fa9000)?(unsigned)(caller-(uintptr_t)image):0;
    uint64_t result=0;
    __try {result=porigin_observe_delivery(&origin_ledger,origin_capture,teb,original,event,receiver,source,due,rva);}
    __finally {origin_leave(admitted,AbnormalTermination());}
    return result;
}
static uint64_t origin_post(void *receiver,void *event,double due,unsigned char flags) {
    return origin_observe_post((porigin_post_fn)(image+0x99a3f0),receiver,event,due,flags,
        (uintptr_t)__builtin_return_address(0),NtCurrentTeb());
}
static uint64_t origin_delivery(void *event,void *receiver,void *source,double due) {
    return origin_observe_delivery((porigin_delivery_fn)(image+0x152870),event,receiver,source,due,
        (uintptr_t)__builtin_return_address(0),NtCurrentTeb());
}
static void origin_transition_edge(uint64_t event) {
    if(origin_started) (void)porigin_link_transition(&origin_ledger,event);
}
static void origin_emit(unsigned char out[32],void *target) {
    memset(out,0,32);out[0]=0xff;out[1]=0x25;memcpy(out+6,&target,8);
}
static int origin_emit_page(unsigned char *page) {origin_emit(page+PORIGIN_STUB,(void*)origin_post);return 1;}
static int origin_encode(unsigned __int128 *original,unsigned __int128 *patched,const unsigned char *word,
                         const unsigned char *stub) {
    static const unsigned char expected[16]={0x0f,0x58,0x15,0x09,0x2b,0xb1,0x02,0xe8,0xe4,0xe8,0x7e,0x00,0x48,0x8b,0x74,0x24};
    intptr_t distance=(intptr_t)stub-(intptr_t)(word+12);
    if(memcmp(word,expected,16) || distance<INT32_MIN || distance>INT32_MAX) return 0;
    memcpy(original,word,16);*patched=*original;int32_t relative=(int32_t)distance;
    memcpy((unsigned char*)patched+8,&relative,4);return 1;
}
/* changed=1 records ownership even if restoring page protection/flush fails. */
static int origin_swap(unsigned site,int installing,int *changed) {
    *changed=0;DWORD old,ignored;unsigned char *address=image+(site?PORIGIN_SLOT:PORIGIN_WORD);size_t length=site?8:16;
    if(site>1 || !VirtualProtect(address,length,PAGE_EXECUTE_READWRITE,&old)) return 0;
    if(site) {
        uint64_t expected=installing?origin_original_slot:origin_patched_slot,value=installing?origin_patched_slot:origin_original_slot;
        *changed=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)address,(LONG64)value,(LONG64)expected)==expected;
    } else {
        unsigned __int128 expected=installing?origin_original_word:origin_patched_word,value=installing?origin_patched_word:origin_original_word;
        *changed=__sync_val_compare_and_swap((unsigned __int128*)address,expected,value)==expected;
    }
    int protect=VirtualProtect(address,length,old,&ignored),flush=FlushInstructionCache(GetCurrentProcess(),address,length);
    return *changed && protect && flush;
}
static void origin_restore_locked(void) {
    origin_closing=1;porigin_stop(&origin_ledger);if(origin_open>origin_restore_open) origin_restore_open=origin_open;
    for(unsigned site=0;site<2;site++) if(origin_sites&(1u<<site)) {
        int changed=0;int ok=origin_swap(site,0,&changed);if(changed) origin_sites&=~(1u<<site);if(!ok) ++origin_failures;
    }
}
static void origin_restore(void) {AcquireSRWLockExclusive(&origin_lock);origin_restore_locked();ReleaseSRWLockExclusive(&origin_lock);}
static int origin_prepare(void) {
    unsigned char stub[32];origin_emit(stub,(void*)origin_post);
    if(!IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128) || !origin_pins() || !transition_saved_pins() ||
       !thunk_bytes_match(PORIGIN_STUB,stub,sizeof(stub)) ||
       !origin_encode(&origin_original_word,&origin_patched_word,image+PORIGIN_WORD,thunk+PORIGIN_STUB)) return 0;
    origin_original_slot=(uint64_t)(uintptr_t)(image+0x152870);origin_patched_slot=(uint64_t)(uintptr_t)origin_delivery;return 1;
}
static int origin_parse(const char *text,size_t length,uint64_t *player,uint64_t *ship,unsigned *first,unsigned *last) {
    if(!text || length<39 || length>43 || memchr(text,0,length) || text[0]!='1' || text[1]!='|' || text[18]!='|' || text[35]!='|') return 0;
    uint64_t ids[2]={0};
    for(unsigned j=0;j<2;j++) for(unsigned i=0;i<16;i++) {
        char c=text[2+j*17+i];unsigned d=c>='0' && c<='9'?(unsigned)(c-'0'):c>='a' && c<='f'?(unsigned)(c-'a'+10):16;
        if(d>15) return 0;ids[j]=(ids[j]<<4)|d;
    }
    size_t at=36;unsigned ticks[2]={0};
    for(unsigned j=0;j<2;j++) {
        unsigned digits=0;if(at>=length || text[at]<'1' || text[at]>'9') return 0;
        while(at<length && text[at]!='|') {
            char c=text[at++];if(c<'0' || c>'9' || ++digits>3) return 0;
            ticks[j]=ticks[j]*10+(unsigned)(c-'0');if(ticks[j]>120) return 0;
        }
        if(!j) {if(at==length || text[at++]!='|') return 0;} else if(at!=length) return 0;
    }
    *player=ids[0];*ship=ids[1];*first=ticks[0];*last=ticks[1];
    return *player && *ship && *player!=*ship && *first<=*last;
}
static int origin_configure_transaction(const char *text,size_t length,int (*prepare)(void),int (*swap)(unsigned,int,int*)) {
    uint64_t player,ship;unsigned first,last;if(!origin_parse(text,length,&player,&ship,&first,&last)) return 0;
    int ok=0;AcquireSRWLockExclusive(&origin_lock);
    if(origin_closing || !stats.active || GetCurrentThreadId()!=ui_thread) goto done;
    if(origin_started) {ok=origin_sites==3 && strlen(origin_token)==length && !memcmp(text,origin_token,length);goto done;}
    if(!boot_mode || boot_phase || stats.issued || !world_identity_installed || !transition_enabled || transition_sites!=7 ||
       transition_saved_count!=1 || transition_saved_watches[0].saved!=ship || transition_saved_watches[0].initial_type!=0x2c39488 ||
       strcmp(transition_saved_watches[0].owner,"player") || last>transition_saved_last || !prepare() || !stats.active) goto done;
    origin_player_saved=player;origin_ship_saved=ship;origin_first=first;origin_last=last;
    memcpy(origin_token,text,length);origin_token[length]=0;origin_started=1;origin_ledger.window=origin_window;
    for(unsigned site=0;site<2;site++) {int changed=0;int result=swap(site,1,&changed);if(changed) origin_sites|=1u<<site;if(!result) goto failed;}
    if(!stats.active) goto failed;ok=1;goto done;
failed:origin_restore_locked();
done:ReleaseSRWLockExclusive(&origin_lock);return ok;
}
static int origin_configure(const char *text,size_t length) {return origin_configure_transaction(text,length,origin_prepare,origin_swap);}
static int origin_bind(void) {
    if(!origin_started) return 1;int ok=1;AcquireSRWLockExclusive(&origin_lock);
    if(origin_bound) {ok=!origin_failures;goto done;}
    porigin_sample sample={0};transition_saved_watch w;
    ok=!origin_closing && stats.active && boot_phase==3 && !stats.issued && GetCurrentThreadId()==main_thread && origin_imports==1 &&
        transition_saved_copy(0,&w) && w.saved==origin_ship_saved && origin_identity(&sample) &&
        sample.controller==w.initial_controller && sample.binding.agent==w.agent && porigin_configure(&origin_ledger,&sample.binding);
    if(ok) InterlockedExchange(&origin_bound,1);else ++origin_failures;
done:ReleaseSRWLockExclusive(&origin_lock);return ok;
}
static int origin_failed(void) {
    AcquireSRWLockShared(&origin_lock);int bad=origin_started && (origin_failures || origin_sites!=3 || origin_closing);
    ReleaseSRWLockShared(&origin_lock);AcquireSRWLockShared(&origin_ledger.lock);bad|=origin_ledger.failures!=0;
    ReleaseSRWLockShared(&origin_ledger.lock);return bad;
}
