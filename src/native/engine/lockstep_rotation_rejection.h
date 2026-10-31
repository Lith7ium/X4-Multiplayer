/* Optional Windows-only, passive first-refusal evidence. No source-memory
 * reread, engine call, retry, acceptance decision, or output write lives here.
 * Publication and completion require rotation_lock. Records survive STOP. */
#include <limits.h>
#define ROTATION_REJECT_RECORDS 192u
#define ROTATION_REJECT_BYTES 8192u
#define ROTATION_REJECT_READ_MAX 512u
enum rotation_reject_stage {
    RR_NONE,RR_ADMISSION,RR_PRE_GUARD,RR_CAPTURE,RR_CLAIM_GUARD,RR_LIFETIME,
    RR_CLASS_COUNT,RR_EVALUATE,RR_POST_GUARD,RR_REPLAY,RR_PLAN,RR_PUBLISH_REPLAY,
    RR_PUBLISH_GUARD,RR_ORIGINAL,RR_PRIOR
};
enum rotation_reject_predicate {
    RP_NONE,RP_SCOPE_DEPTH,RP_COUNT,RP_STOPPED,RP_CONFIGURED,RP_OWNER_THREAD,RP_PHASE,
    RP_LOAD_CALLS,RP_LOAD_RETURNS,RP_SCOPE_OPEN,RP_TABLE_READ,RP_TABLE_NULL,
    RP_CLIENT_READ,RP_CLIENT_NULL,RP_CLIENT_TYPE_READ,RP_CLIENT_TYPE,
    RP_CLIENT_ENTRY_READ,RP_CLIENT_ENTRY,RP_MODULE_READ,RP_MODULE_NULL,
    RP_MODULE_TYPE_READ,RP_MODULE_TYPE,RP_MODULE_ENTRY_READ,RP_MODULE_ENTRY,
    RP_MODULE_STATE_READ,RP_MANAGER_READ,RP_MANAGER_NULL,RP_LOADING_READ,RP_LOADING,
    RP_TABLE_ENTRY_READ,RP_TABLE_MATCHES,RP_HOST_CHANGED,RP_MANAGER_FLAG_READ,
    RP_MANAGER_FLAG,RP_READERS_READ,RP_READERS,RP_APP_READ,RP_APP,
    RP_SELECTOR_READ,RP_SELECTOR,RP_OTHER_READ,RP_OTHER,RP_PATCH_OWNED,
    RP_PATCH_DEBT,RP_PATCH_READ,RP_PATCH_BYTES,RP_MANAGER_CHANGED,RP_AGENT_NULL,
    RP_AGENT_ALIGN,RP_AGENT_OVERFLOW,RP_BASIS,RP_CALL_SERIAL,RP_CALL_SELECTOR,
    RP_CALL_HOST,RP_CALL_MANAGER,RP_GUARD_CHANGED,RP_ARGUMENTS,RP_INDEX_FULL,
    RP_INDEX_JOIN,RP_LIFETIME_IDENTITY,RP_LIFETIME_CLASS,RP_ROLE_DUPLICATE,
    RP_BANK_DUPLICATE,RP_CAPACITY,RP_LIMIT,RP_HELPER,RP_MATH,RP_NONFINITE,
    RP_ADDRESS,RP_READ,RP_ACCOUNTED,RP_WRITABLE,RP_BANK_CHANGED,RP_PLAN_WORD,
    RP_WRITE_CHANGED,RP_UNWIND,RP_ALREADY_FAILED,RP_INACTIVE
};
typedef struct {
    uint64_t address,requested;
    unsigned offset,copied,ok;
} rotation_reject_read;
typedef struct {
    unsigned stage,predicate,helper_known,operands_known;int helper_result;
    uint64_t actual,expected;
    unsigned records,bytes,truncated,failed_reads,attempted_reads,dropped_records;
    rotation_reject_read reads[ROTATION_REJECT_RECORDS];
    unsigned char data[ROTATION_REJECT_BYTES];
} rotation_reject_trace;
typedef struct {
    unsigned present,stage,predicate,helper_known,operands_known;int helper_result;
    uint64_t actual,expected,serial,selected[2],load_calls,load_returns;
    uintptr_t image_base,basis,xml,agent,movement,bank;
    unsigned role,class_id,accounted,owner,thread,phase,scope_open,load_open,primary_failure;
    rotation_reject_trace trace;
} rotation_reject_entry;
typedef struct {
    unsigned present,return_valid,unwind,primary_failure,scope_open,load_open;
    uint64_t raw_return,role_returns[2];
} rotation_reject_completion;
/* -1 is unbound. The first configuration fixes 0 or 1 for this boot. */
static LONG rotation_reject_mode=-1;
static rotation_reject_entry rotation_reject_first;
static rotation_reject_completion rotation_reject_done;
static int rotation_reject_pred(rotation_reject_trace *d,unsigned predicate,uint64_t actual,uint64_t expected) {
    if(d && !d->predicate) {
        d->predicate=predicate;d->actual=actual;d->expected=expected;
        /* Only these predicates have a defined scalar actual/expected pair.
         * Compound checks retain context but explicitly mark operands unknown. */
        switch(predicate) {
        case RP_SCOPE_DEPTH:case RP_COUNT:case RP_STOPPED:case RP_CONFIGURED:
        case RP_OWNER_THREAD:case RP_LOAD_CALLS:case RP_LOAD_RETURNS:case RP_SCOPE_OPEN:
        case RP_CLIENT_TYPE:case RP_CLIENT_ENTRY:case RP_MODULE_TYPE:case RP_MODULE_ENTRY:
        case RP_LOADING:case RP_TABLE_MATCHES:case RP_MANAGER_FLAG:case RP_READERS:
        case RP_OTHER:case RP_MANAGER_CHANGED:case RP_BASIS:case RP_CALL_SERIAL:
        case RP_CALL_SELECTOR:case RP_CALL_MANAGER:case RP_INDEX_JOIN:
        case RP_LIFETIME_IDENTITY:case RP_LIFETIME_CLASS:case RP_BANK_DUPLICATE:
        case RP_CAPACITY:case RP_LIMIT:case RP_PLAN_WORD:case RP_ALREADY_FAILED:
            d->operands_known=1;break;
        default:d->operands_known=0;break;
        }
    }
    return 0;
}
static void rotation_reject_stage_begin(rotation_reject_trace *d,unsigned stage) {
    if(d) {memset(d,0,sizeof(*d));d->stage=stage;}
}
static void rotation_reject_helper(rotation_reject_trace *d,int result) {
    if(d) {d->helper_known=1;d->helper_result=result;if(result && !d->predicate)d->predicate=RP_HELPER;}
}
static void rotation_reject_copied(rotation_reject_trace *d,uintptr_t address,
        const void *bytes,size_t requested,int ok) {
    if(!d)return;
    if(d->attempted_reads!=UINT_MAX)++d->attempted_reads;else d->truncated=1;
    if(!ok && d->failed_reads!=UINT_MAX)++d->failed_reads;
    if(d->records==ROTATION_REJECT_RECORDS) {
        d->truncated=1;if(d->dropped_records!=UINT_MAX)++d->dropped_records;return;
    }
    rotation_reject_read *r=&d->reads[d->records++];
    *r=(rotation_reject_read){address,requested,d->bytes,0,(unsigned)(ok!=0)};
    if(ok) {
        if(requested>ROTATION_REJECT_READ_MAX || requested>ROTATION_REJECT_BYTES-d->bytes) {d->truncated=1;return;}
        memcpy(d->data+d->bytes,bytes,requested);r->copied=(unsigned)requested;d->bytes+=(unsigned)requested;
    }
}
static int rotation_observed_read(rotation_reject_trace *d,const void *source,void *out,size_t length) {
    int ok=read_local(source,out,length);
    rotation_reject_copied(d,(uintptr_t)source,out,length,ok);return ok;
}
