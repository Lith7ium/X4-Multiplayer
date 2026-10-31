"""Closed opt-in provisional identity evidence; never operational enrollment."""
MANIFEST='REGISTRY_CREATION_1;handle=0000000500000003;policy=observe'
SUFFIX='|'+MANIFEST


def plan(config,assignment):
    schema=assignment(config,'native_gate_registry_creation_schema',False)
    if schema is None:return None
    if schema!='1' or assignment(config,'native_gate_creation_rng_schema',False)!='1' or \
       assignment(config,'native_gate_creation_rng_ack_schema',False) not in ('1','2') or \
       assignment(config,'native_gate_creation_trace_schema',False)!='2' or \
       assignment(config,'native_gate_construction_test',False)!='true':
        raise ValueError('provisional registry evidence requires explicit construction/RNG/ACK mode')
    return MANIFEST
