/* Snapshot import observation only. Saved UniverseIDs identify loaded objects;
 * runtime IDs and display codes are not portable identities. Objects created
 * after loading remain unknown. No engine registry or simulation state writes. */
#define WORLD_IDENTITY_ENTRY 0x79f720u
#define WORLD_IDENTITY_CAPACITY 1048576u
#define WORLD_IDENTITY_PROBES 256u
typedef unsigned char (*world_identity_fn)(void *,void *);
typedef struct { uint64_t runtime,saved; } world_identity_entry;
static world_identity_entry *world_identities;
static SRWLOCK world_identity_lock=SRWLOCK_INIT;
static volatile LONG64 world_identity_calls,world_identity_count,world_identity_failures,world_identity_late,world_identity_sealed_writes;
/* Sealed once at the first live frame (boot phase 3): the only writer is the
 * load-time import observer, so from then on the table is immutable and
 * lookups read it without the lock. A write after the seal is refused. */
static volatile LONG world_identity_sealed;
static uint64_t world_identity_original_word,world_identity_patched_word;
static int world_identity_installed,world_identity_restored;

/* Only the canonical [0xHEX] save representation is accepted. A successful
 * native import with another representation fails this diagnostic closed. */
static int world_identity_parse(void *source,uint64_t *out) {
    unsigned char storage[32],text[21];uint64_t length,capacity;uintptr_t address;
    if(!read_local(source,storage,sizeof(storage))) return 0;
    memcpy(&length,storage+16,8);memcpy(&capacity,storage+24,8);
    if(length<5 || length>20 || capacity<length) return 0;
    if(capacity<16) address=(uintptr_t)source;
    else memcpy(&address,storage,8);
    if(!read_local((void*)address,text,(size_t)length+1) || text[0]!='[' || text[1]!='0' ||
       text[2]!='x' || text[length-1]!=']' || text[length]) return 0;
    uint64_t value=0;
    for(unsigned i=3;i<length-1;i++) {
        unsigned digit=text[i]>='0' && text[i]<='9' ? text[i]-'0' :
            text[i]>='a' && text[i]<='f' ? text[i]-'a'+10 : 16;
        if(digit>15) return 0;value=(value<<4)|digit;
    }
    if(!value) return 0;*out=value;return 1;
}
static unsigned world_identity_bucket(uint64_t runtime) {
    runtime^=runtime>>33;runtime*=0xff51afd7ed558ccdULL;runtime^=runtime>>33;
    return (unsigned)runtime&(WORLD_IDENTITY_CAPACITY-1);
}
static int world_identity_record(uint64_t runtime,uint64_t saved) {
    if(!world_identities || !runtime || !saved) return 0;
    int ok=0;unsigned bucket=world_identity_bucket(runtime);
    AcquireSRWLockExclusive(&world_identity_lock);
    if(world_identity_sealed) {++world_identity_sealed_writes;ReleaseSRWLockExclusive(&world_identity_lock);return 0;}
    for(unsigned i=0;i<WORLD_IDENTITY_PROBES;i++) {
        world_identity_entry *e=&world_identities[(bucket+i)&(WORLD_IDENTITY_CAPACITY-1)];
        if(e->runtime==runtime) {ok=e->saved==saved;break;}
        if(!e->runtime) {
            if(world_identity_count>=WORLD_IDENTITY_CAPACITY/2) break;
            e->runtime=runtime;e->saved=saved;++world_identity_count;ok=1;break;
        }
    }
    ReleaseSRWLockExclusive(&world_identity_lock);return ok;
}
static uint64_t world_identity_probe(uint64_t runtime) {
    unsigned bucket=world_identity_bucket(runtime);
    for(unsigned i=0;i<WORLD_IDENTITY_PROBES;i++) {
        const world_identity_entry *e=&world_identities[(bucket+i)&(WORLD_IDENTITY_CAPACITY-1)];
        if(e->runtime==runtime) return e->saved;
        if(!e->runtime) break;
    }
    return 0;
}
static uint64_t world_identity_lookup(uint64_t runtime) {
    if(!world_identities || !runtime) return 0;
    /* Acquire read of the seal; it was published under the exclusive lock
     * after every earlier writer released it. */
    if(*(volatile LONG*)&world_identity_sealed) return world_identity_probe(runtime);
    AcquireSRWLockShared(&world_identity_lock);uint64_t saved=world_identity_probe(runtime);
    ReleaseSRWLockShared(&world_identity_lock);return saved;
}
/* Portable keys of created objects for ordering and keying modules, DEFAULT OFF:
 * native_gate_portable_created (needs native_gate_created_identities).
 *
 * world_identity_lookup stays saved-only (non-zero means "imported object").
 * world_identity_key is what a module uses where it orders, keys or includes an
 * object by identity. Off, it is exactly the saved lookup (class SAVED or NONE)
 * and every consumer behaves as before. On, an object without a saved ID but
 * with a created identity (lockstep_world_created.h) has class CREATED and its
 * created ID.
 *
 * Total order of every ordering consumer: (class, ID) with
 *   SAVED (0) by saved UniverseID  <  CREATED (1) by created ID  <  NONE (2)
 * by the consumer's own local fallback (runtime ID or native position), so
 * saved objects keep their keys and their order relative to each other, and
 * created objects take the place among the unportable entries that they had
 * before, sorted by a portable ID. Created IDs carry bit 63 and the saved IDs
 * of X4 lie far below it, but consumers compare the class, not the bit.
 *
 * Timing. A created ID is assigned at the gate boundary after the frame that
 * created the object (main thread, MoveWorker joined). During its creation
 * frame an object is NONE on every client and CREATED from the next frame on;
 * the table changes only at that boundary, identically on every client, so
 * the switch itself is deterministic. A NONE decision in the creation frame is
 * as portable as the consumer's fallback (natural RNG stream, native order).
 * Consumers that cache a lookup compare world_identity_generation(), which
 * changes whenever the created table changes. */
enum {WORLD_KEY_SAVED=0,WORLD_KEY_CREATED=1,WORLD_KEY_NONE=2};
static volatile LONG world_portable_created;     /* armed once before the first gated frame */
static volatile LONG64 world_created_generation; /* bumped by every created table change */
static uint64_t created_lookup(uint64_t runtime); /* lockstep_world_created.h */
static int world_key_portable(void) {return *(volatile LONG*)&world_portable_created!=0;}
static uint64_t world_identity_key(uint64_t runtime,unsigned *klass) {
    uint64_t id=world_identity_lookup(runtime);
    if(id) {*klass=WORLD_KEY_SAVED;return id;}
    if(runtime && world_key_portable() && (id=created_lookup(runtime))) {*klass=WORLD_KEY_CREATED;return id;}
    *klass=WORLD_KEY_NONE;return 0;
}
/* 0 while the portable keys are off (the saved table never changes after the seal). */
static uint64_t world_identity_generation(void) {
    return world_key_portable()?(uint64_t)InterlockedCompareExchange64(&world_created_generation,0,0):0;
}
/* Waits for any in-flight import write, then closes writes for good. */
static void world_identity_seal(void) {
    if(!world_identities || world_identity_sealed) return;
    AcquireSRWLockExclusive(&world_identity_lock);InterlockedExchange(&world_identity_sealed,1);ReleaseSRWLockExclusive(&world_identity_lock);
}
static unsigned char world_identity_observe(world_identity_fn original,void *object,void *source,int loading) {
    if(!loading) {InterlockedIncrement64(&world_identity_late);return original(object,source);}
    uint64_t saved=0,runtime=0;int parsed=world_identity_parse(source,&saved);
    unsigned char result=original(object,source); /* exactly once, same arguments/result */
    InterlockedIncrement64(&world_identity_calls);
    if(!result || !parsed || !read_local((unsigned char*)object+8,&runtime,8) ||
       !world_identity_record(runtime,saved)) InterlockedIncrement64(&world_identity_failures);
    else {snapshot_import_capture(object,runtime,saved);world_trace_import_capture(object,runtime,saved);portable_import_capture(object,runtime,saved);transition_import_capture(object,runtime,saved);origin_import_capture(object,runtime,saved);}
    return result;
}
static unsigned char world_identity_import(void *object,void *source) {
    return world_identity_observe((world_identity_fn)(thunk+944),object,source,boot_mode && boot_phase>0 && boot_phase<3);
}
static int world_identity_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+WORLD_IDENTITY_ENTRY,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+WORLD_IDENTITY_ENTRY),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+WORLD_IDENTITY_ENTRY,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+WORLD_IDENTITY_ENTRY,8);return actual==expected;
}
static void world_identity_restore(void) {
    if(!world_identity_installed) return;
    world_identity_restored=world_identity_swap(world_identity_patched_word,world_identity_original_word);
    if(world_identity_restored) world_identity_installed=0;
    /* Keep the small bounded index alive until process exit: Lua diagnostics
     * may query it after stop, and an in-flight callback must retain storage. */
}
static int world_identity_install(void) {
    if(!hash_ok(image+WORLD_IDENTITY_ENTRY,160,"e4b75e8f26ba55fd42052ef36c992382fdd7a36e2c2cd14a9e61c774a09e68af")) return 0;
    intptr_t distance=(intptr_t)(thunk+976)-(intptr_t)(image+WORLD_IDENTITY_ENTRY+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!world_identities) return 0;
    if(!thunk_slot_ready(944)) return 0;
    memcpy(&world_identity_original_word,image+WORLD_IDENTITY_ENTRY,8);world_identity_patched_word=world_identity_original_word;
    unsigned char *patch=(unsigned char*)&world_identity_patched_word;patch[0]=0xe9;int32_t rel=(int32_t)distance;memcpy(patch+1,&rel,4);
    if(!world_identity_swap(world_identity_original_word,world_identity_patched_word)) return 0;
    world_identity_installed=1;return 1;
}
