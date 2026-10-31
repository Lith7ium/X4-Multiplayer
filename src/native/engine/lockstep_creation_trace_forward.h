/* Native originals run exactly once; diagnostics retain their argument widths,
 * out-references and GP/XMM results. Do not replace a native query by a second
 * call for observation. The error slot is preserved across our Win32 readers. */
static unsigned creation_enter(unsigned,uintptr_t,const uint64_t *,unsigned);
static void creation_leave(unsigned,uint64_t,unsigned);
static void creation_abort(unsigned);
static void creation_rng_begin(unsigned,unsigned,uintptr_t,const uint64_t *,unsigned *);
static void creation_rng_end(unsigned,int);
static void creation_rng_original(unsigned);
static void registry_creation_trace(unsigned,unsigned);
static unsigned creation_call_begin(unsigned kind,uintptr_t caller,const uint64_t *args,unsigned count) {
    DWORD error=GetLastError();unsigned index=creation_enter(kind,caller,args,count);SetLastError(error);return index;
}
static void creation_call_end(unsigned index,uint64_t result,unsigned result_kind) {
    DWORD error=GetLastError();creation_leave(index,result,result_kind);SetLastError(error);
}
static void creation_call_abort(unsigned index) {
    if(!index)return;
    DWORD error=GetLastError();creation_abort(index);SetLastError(error);
}
static uintptr_t creation_generate_execute(creation_generate_fn original,uintptr_t caller,uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,
        unsigned char e,uintptr_t f,uintptr_t g,unsigned h,unsigned char i,unsigned char j) {
    uint64_t args[]={a,b,c,d,e,f,g,h,i,j};
    unsigned index=creation_call_begin(CREATION_GENERATE,caller,args,10);
    uintptr_t result=0;unsigned rng_scope=0;
    /* native_gate_spawn_rng: g is the caller's explicit generator state. */
    spawn_rng_scope spawn;DWORD spawn_error=GetLastError();spawn_rng_begin(g,&spawn);SetLastError(spawn_error);
    __try {
        registry_creation_trace(index,0);
        creation_rng_begin(index,CREATION_GENERATE,caller,args,&rng_scope);
        creation_rng_original(rng_scope);
        result=original(a,b,c,d,e,f,g,h,i,j);creation_call_end(index,result,1);
    } __finally {
        creation_rng_end(rng_scope,!AbnormalTermination());
        if(AbnormalTermination())creation_call_abort(index);
        spawn_error=GetLastError();spawn_rng_end(&spawn,!AbnormalTermination());SetLastError(spawn_error);
    }
    return result;
}
static __attribute__((noinline)) uintptr_t creation_generate(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,
        unsigned char e,uintptr_t f,uintptr_t g,unsigned h,unsigned char i,unsigned char j) {
    return creation_generate_execute((creation_generate_fn)(thunk+3072),(uintptr_t)__builtin_return_address(0),a,b,c,d,e,f,g,h,i,j);
}
static __attribute__((noinline)) void creation_initialize(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,
        uintptr_t e,unsigned char f,uintptr_t g,unsigned h,unsigned char i) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);uint64_t args[]={a,b,c,d,e,f,g,h,i};
    unsigned index=creation_call_begin(CREATION_INITIALIZE,caller,args,9);
    __try {registry_creation_trace(index,0);((creation_initialize_fn)(thunk+3136))(a,b,c,d,e,f,g,h,i);creation_call_end(index,0,0);}
    __finally {if(AbnormalTermination())creation_call_abort(index);}
}
static __attribute__((noinline)) uintptr_t creation_pilot(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,
        uintptr_t e,uintptr_t f,uintptr_t g) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);uint64_t args[]={a,b,c,d,e,f,g};
    unsigned index=creation_call_begin(CREATION_PILOT,caller,args,7);
    uintptr_t result=0;
    __try {registry_creation_trace(index,0);result=((creation_pilot_fn)(thunk+3200))(a,b,c,d,e,f,g);creation_call_end(index,result,1);}
    __finally {if(AbnormalTermination())creation_call_abort(index);}
    return result;
}
static void creation_navigation_execute(creation_navigation_fn original,uintptr_t caller,uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,
        unsigned char e,unsigned char f) {
    uint64_t args[]={a,b,c,d,e,f};
    unsigned index=creation_call_begin(CREATION_NAVIGATION,caller,args,6);
    unsigned rng_scope=0;
    __try {
        creation_rng_begin(index,CREATION_NAVIGATION,caller,args,&rng_scope);
        creation_rng_original(rng_scope);
        original(a,b,c,d,e,f);creation_call_end(index,0,0);
    } __finally {
        creation_rng_end(rng_scope,!AbnormalTermination());
        if(AbnormalTermination())creation_call_abort(index);
    }
}
static __attribute__((noinline)) void creation_navigation(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,
        unsigned char e,unsigned char f) {
    creation_navigation_execute((creation_navigation_fn)(thunk+3264),(uintptr_t)__builtin_return_address(0),a,b,c,d,e,f);
}
static __attribute__((noinline)) uintptr_t creation_agent(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);uint64_t args[]={a,b,c,d};
    unsigned index=creation_call_begin(CREATION_AGENT,caller,args,4);
    uintptr_t result=0;
    __try {result=((creation_agent_fn)(thunk+3328))(a,b,c,d);creation_call_end(index,result,1);}
    __finally {if(AbnormalTermination())creation_call_abort(index);}
    return result;
}
static __attribute__((noinline)) void creation_setter(uintptr_t a,uintptr_t b,unsigned char c) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);uint64_t args[]={a,b,c};
    unsigned index=creation_call_begin(CREATION_SETTER,caller,args,3);
    __try {((creation_setter_fn)(thunk+3392))(a,b,c);creation_call_end(index,0,0);}
    __finally {if(AbnormalTermination())creation_call_abort(index);}
}
static __attribute__((noinline)) float creation_random(uint64_t *state,float bound) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);uint32_t bound_bits;memcpy(&bound_bits,&bound,4);
    uint64_t args[]={(uintptr_t)state,bound_bits};unsigned index=creation_call_begin(CREATION_RANDOM,caller,args,2);
    float result=0;
    __try {result=((creation_random_fn)(thunk+3456))(state,bound);uint32_t bits;memcpy(&bits,&result,4);creation_call_end(index,bits,2);}
    __finally {if(AbnormalTermination())creation_call_abort(index);}
    return result;
}
static __attribute__((noinline)) unsigned char creation_event_query(uintptr_t owner,unsigned type,double minimum,
        double maximum,unsigned char flag) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);uint64_t min_bits,max_bits;
    memcpy(&min_bits,&minimum,8);memcpy(&max_bits,&maximum,8);uint64_t args[]={owner,type,min_bits,max_bits,flag};
    unsigned index=creation_call_begin(CREATION_EVENT_QUERY,caller,args,5);
    unsigned char result=0;
    __try {result=((creation_event_query_fn)(image+0x99a210))(owner,type,minimum,maximum,flag);creation_call_end(index,result,3);}
    __finally {if(AbnormalTermination())creation_call_abort(index);}
    return result;
}
static __attribute__((noinline)) void creation_post(uintptr_t owner,uintptr_t event,double due,unsigned char flag) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);uint64_t due_bits;memcpy(&due_bits,&due,8);
    uint64_t args[]={owner,event,due_bits,flag};unsigned index=creation_call_begin(CREATION_POST,caller,args,4);
    __try {((creation_post_fn)(image+0x99a3f0))(owner,event,due,flag);creation_call_end(index,0,0);}
    __finally {if(AbnormalTermination())creation_call_abort(index);}
}
