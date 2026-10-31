/* Private first-entry provenance. These scopes do not change native state or
 * decide whether an observed entry is safe for enrollment. */
enum registry_origin_source {REGISTRY_ORIGIN_UNKNOWN,REGISTRY_ORIGIN_AI,
    REGISTRY_ORIGIN_RANGE,REGISTRY_ORIGIN_EVENT,REGISTRY_ORIGIN_RADAR};
/* Presence is a passive snapshot, never a lifetime lease or enrollment verdict.
 * PRESENT means only that this component's listed reads/relationships matched. */
enum registry_presence_state {PRESENCE_UNOBSERVED,PRESENCE_ABSENT,PRESENCE_UNREADABLE,
    PRESENCE_UNSUPPORTED,PRESENCE_PARTIAL,PRESENCE_PRESENT};
typedef struct {
    uint64_t object,type,actual,owner_getter,owner,controller,controller_type,controller_owner,slots,agent,agent_owner;
    uint64_t raw_type,raw_owner_getter,raw_controller_type;
    unsigned reads,object_state,owner_state,controller_state,agent_state,reserved;
} registry_presence_sample;
typedef struct {registry_presence_sample first,second;unsigned recorded,stable;} registry_origin_identity;
typedef struct {
    uint64_t caller,verified_caller,context,exec,program,begin,end,pc,context_pc;
    uint64_t type,entity,rng_before,rng_after,time,script_hash,result;
    unsigned recorded,source,caller_source,reads,error,complete,mode,depth,ui,view;
    char script[96];
    registry_origin_identity identity;
} registry_pending_origin;
typedef struct registry_origin_scope {
    struct registry_origin_scope *previous;
    registry_pending_origin *notes[16];
    void *context,*exec,*program,*tls;
    uint64_t *rng;
    uintptr_t caller;
    uint64_t begin,end;
    unsigned source,mode,count,admitted,creation_index,closed;
    /* Indices into separately resident diagnostic storage, not actor slots. */
    unsigned creation_notes[16],creation_note_count,creation_owner_thread;
} registry_origin_scope;
static unsigned creation_range_enter(const registry_origin_scope *);
static void creation_range_leave(unsigned,uint64_t);
static void creation_range_abort(unsigned);
static void *registry_origin_tls(void);
static uint64_t *registry_origin_rng(void *);
static void registry_origin_enter(registry_origin_scope *,unsigned,uintptr_t,void *,void *,void *,uint64_t,uint64_t,unsigned,void *,uint64_t *);
static void registry_origin_leave(registry_origin_scope *,uint64_t);
static void registry_origin_abort(registry_origin_scope *);
static void registry_origin_capture(registry_pending_origin *,void *,uint64_t);
static int registry_origin_enable(void);
static void registry_origin_close(void);
