/* Diagnostic identity is not operational readiness. No actor publication API. */
#ifndef X4LAN_REGISTRY_CREATION_CONTRACT_H
#define X4LAN_REGISTRY_CREATION_CONTRACT_H
#define REGISTRY_CREATION_MANIFEST "REGISTRY_CREATION_1;handle=0000000500000003;policy=observe"
#define REGISTRY_CREATION_SUFFIX "|" REGISTRY_CREATION_MANIFEST
#define REGISTRY_CREATION_OPERATIONS 256u
#define REGISTRY_CREATION_IDENTITIES 4u
typedef struct {unsigned configured,mode,acks,phase,closing;} registry_creation_attachment;
enum registry_creation_error {
    RC_READ=1,RC_IDENTITY=2,RC_LOOKUP=4,RC_THREAD=8,RC_SCOPE=16,
    RC_OVERFLOW=32,RC_STOP=64,RC_UNWIND=128,RC_UNSUPPORTED=256,RC_LIFETIME=512,RC_DUPLICATE=1024
};
#endif
