/* ACK2 prepared boot identity. This is not successful registry/RNG attachment
 * and never changes seed material, native objects or observer cursors.
 * All accesses require creation_lock. Valid is published last, once. */
typedef struct {
    unsigned valid,slot,port,thread,epoch,registry;
    uint64_t started,options;
    uintptr_t image;
    char secret[33],digest[65],nonce[33];
    char manifest[CREATION_RNG_MANIFEST_CAP],preimage[1024];
} creation_attach_binding;
static creation_attach_binding creation_attach_boot;

static int creation_attach_matches(const char *manifest,size_t length,const char *preimage,size_t preimage_length,
        uint32_t epoch,unsigned registry) {
    const creation_attach_binding *b=&creation_attach_boot;
    return b->valid && !creation_attach_rejected && b->epoch==epoch && b->registry==registry && b->slot==stats.slot && b->port==remote_port &&
        b->thread==ui_thread && b->thread==GetCurrentThreadId() && b->started==boot_started &&
        b->options==boot_options && b->image==(uintptr_t)image && valid_hex(b->nonce,32) &&
        !strcmp(b->secret,secret) && !strcmp(b->digest,initial_state_digest) &&
        strlen(b->manifest)==length && !memcmp(b->manifest,manifest,length) &&
        strlen(b->preimage)==preimage_length && !memcmp(b->preimage,preimage,preimage_length);
}
static int creation_attach_random(unsigned char bytes[16]) {
    return BCryptGenRandom(NULL,bytes,16,BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0;
}
static int creation_attach_prepare_using(const char *manifest,size_t length,const char *preimage,size_t preimage_length,
        uint32_t epoch,unsigned registry,int (*random)(unsigned char[16])) {
    if(creation_attach_boot.valid)return creation_attach_matches(manifest,length,preimage,preimage_length,epoch,registry);
    if(!stats.active || !used || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread ||
       !creation_enabled || creation_closing || creation_failures || creation_count || creation_open || creation_selected.state ||
       creation_rng_started || creation_rng_closing || creation_rng_count || creation_rng_active || creation_rng_attempted ||
       creation_rng_failures || creation_rng_restore_failures || creation_rng_restore_open ||
       !wire_pair() || stats.slot<1 || stats.slot>2 || registry>1 || !valid_hex(secret,32) || !valid_hex(initial_state_digest,64) ||
       !manifest || length>=sizeof(creation_attach_boot.manifest) || !preimage || preimage_length>=sizeof(creation_attach_boot.preimage))return 0;
    creation_attach_binding prepared={0};unsigned char nonce[16];
    if(!random(nonce))return 0;
    if(!stats.active || creation_closing || boot_phase || stats.issued)return 0;
    prepared.slot=stats.slot;prepared.port=remote_port;prepared.thread=ui_thread;prepared.epoch=epoch;prepared.registry=registry;
    prepared.started=boot_started;prepared.options=boot_options;prepared.image=(uintptr_t)image;
    strcpy(prepared.secret,secret);strcpy(prepared.digest,initial_state_digest);
    memcpy(prepared.manifest,manifest,length);memcpy(prepared.preimage,preimage,preimage_length);
    for(unsigned i=0;i<16;i++)snprintf(prepared.nonce+i*2,3,"%02x",nonce[i]);
    creation_attach_boot=prepared;creation_attach_boot.valid=1;
    return 1;
}
static int creation_attach_prepare(const char *manifest,size_t length,const char *preimage,size_t preimage_length,
        uint32_t epoch,unsigned registry) {
    return creation_attach_prepare_using(manifest,length,preimage,preimage_length,epoch,registry,creation_attach_random);
}
