/* Stack-owned, thread-local observation brackets; never an engine context. */
typedef struct solver_scope {
    struct solver_scope *previous;
    void *self,*agent,*slot;
    uint64_t tick;
    DWORD thread;
    unsigned kind,actor,active;
} solver_scope;
static void solver_scope_begin(solver_scope *,unsigned,void *,void *,void *,unsigned);
static void solver_scope_end(solver_scope *);
