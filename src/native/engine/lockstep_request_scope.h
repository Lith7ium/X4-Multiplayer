/* Observation scopes only, not pilot authority or native controller contexts. */
typedef struct request_agent_scope {
    struct request_agent_scope *previous;
    void *agent;
    uint64_t tick;
    DWORD thread;
    unsigned actor,record,active,requests;
} request_agent_scope;
static void request_agent_begin(request_agent_scope *,unsigned,void *);
static void request_agent_end(request_agent_scope *);
