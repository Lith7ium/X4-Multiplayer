#include "lockstep_seed_core.h"
/* Schema 2 stays byte-identical for fixtures and dock anchors. Dynamic ships
 * use the complete creation handle in a disjoint high-bit namespace. Handles
 * are bounded below 2^63; neither storage slots nor native IDs enter the key. */
static uint64_t actor_seed(uint64_t domain,uint64_t tick,unsigned actor,
                           uint64_t stream,uint64_t ordinal,uint64_t detail) {
    return lockstep_seed(domain,tick,actor_rng_identity(actor),stream,ordinal,detail);
}
