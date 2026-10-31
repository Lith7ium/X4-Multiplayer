"""Closed construction RNG manifest, initial admission and independent keys."""
import re
import native_registry_creation

MASK = 0x84FFFFFFFFFFF
SNAPSHOT = '7816351056e6040d92ae9a4619ae2e15a577fa26a19134f11e20746317e61aa8'
PLAN = 'ed86c6c938e57dd0cc15046bf49ed814f0e7b4833241f04010e016179924585c'
MACRO = 'ship_arg_xl_builder_01_a_macro'
FIXTURE = ('0000000000176a13','0000000000176a74','0000000000176ad5','0000000000176b36',
           '0000000000176b97','0000000000176bf8','0000000000176c59','0000000000176c77',
           '0000000000176c95','0000000000176cb3','0000000000176cd1','0000000000176cef')
SUFFIX = (';handle=0000000500000003;reserved_tick=5;sequence=3;authority=0;world_seed=276944566;macro='+MACRO+
          ';owner=player;snapshot='+SNAPSHOT+';plan='+PLAN+';policy=AB;schema=1')
PREFIX = 'CREATION_RNG_1;epoch='
BOOTSTRAP = 'SNAPSHOT_BYTES_1|'+SNAPSHOT+'|SNAPSHOT_FIXTURE_1|'+','.join(FIXTURE)+'|'
U64 = (1 << 64)-1
ACK_SUFFIX = '|CREATION_RNG_ACK_1'
ACK2_SUFFIX = '|CREATION_RNG_ACK_2'
OPTION_FIELDS = ('serial_pathing seed_rng flight_diag flight_rng flight_full ai_diag ai_rng agent_diag agent_rng '
                 'agent_full ship_diag ship_rng params_init weapon_diag weapon_rng params_diag nav_diag nav_rng '
                 'nav_initial_refresh velocity_diag shape_diag shape_rng shape_initial_refresh bullet_diag bullet_rng '
                 'death_diag death_rng mass_diag attention_diag attention_rng attention_initial_refresh avoidance_refresh '
                 'steering_init spline_diag spline_refresh spline_join radar_diag radar_serial radar_rng '
                 'radar_initial_refresh damage_diag interrupt_diag interrupt_rng contact_diag docking_shape_rng '
                 'docking_shape_refresh world_identities npc_rng npc_diag npc_deadline player_solver_observer '
                 'registry_md_trace player_request_observer').split()


def mix(value):
    value = ((value ^ (value >> 30))*0xbf58476d1ce4e5b9) & U64
    value = ((value ^ (value >> 27))*0x94d049bb133111eb) & U64
    return value ^ (value >> 31)


def fnv(text):
    value = 14695981039346656037
    for byte in text.encode('ascii'):
        value = ((value ^ byte)*1099511628211) & U64
    return value


def key(domain, epoch, tick=5, handle=0x500000003, sequence=3, world_seed=276944566,
        macro=MACRO, owner='player', snapshot=SNAPSHOT, plan=PLAN):
    value = mix(0x58344c5345454432 ^ domain)
    for part in (tick, handle, epoch, sequence, 1, world_seed, fnv(macro), fnv(owner)):
        value = mix(value ^ part)
    for sha in (snapshot, plan):
        if not re.fullmatch('[0-9a-f]{64}', sha):
            raise ValueError('construction RNG digest grammar')
        for offset in range(0, 64, 16):
            value = mix(value ^ int(sha[offset:offset+16], 16))
    return value


def manifest(epoch=1):
    if type(epoch) is not int or not 1 <= epoch <= 0xffffffff:
        raise ValueError('construction RNG epoch')
    return PREFIX+str(epoch)+SUFFIX


def parse_manifest(text):
    if not isinstance(text, str) or len(text) >= 512:
        raise ValueError('construction RNG manifest bounds')
    match = re.fullmatch(re.escape(PREFIX)+r'([1-9][0-9]{0,9})'+re.escape(SUFFIX), text)
    if not match or int(match[1]) > 0xffffffff:
        raise ValueError('construction RNG manifest grammar')
    epoch = int(match[1])
    return {'epoch': epoch, 'handle': 0x500000003, 'reserved_tick': 5, 'sequence': 3, 'authority': 0,
            'world_seed': 276944566, 'snapshot': SNAPSHOT, 'plan': PLAN, 'macro': MACRO, 'owner': 'player',
            'key_a': key(0x43524541545f4131, epoch), 'key_b': key(0x43524541545f4231, epoch)}


def assignment(config, name, required=True):
    values = re.findall(r'(?m)^\s*X4LAN_CONFIG\.'+re.escape(name)+r'\s*=\s*([^\r\n]+)\s*$', config)
    if not values and not required:
        return None
    if len(values) != 1:
        raise ValueError('construction RNG duplicate/missing '+name)
    return values[0].strip()


def quoted(value):
    if not value or len(value) < 2 or value[0] not in "'\"" or value[-1] != value[0] or any(c in value[1:-1] for c in "\\\r\n\x00'\""):
        raise ValueError('construction RNG exact quoted string')
    return value[1:-1]


def bootstrap_plan(config):
    """Return the one construction suffix, without '|'; never infer a mode."""
    schema = assignment(config, 'native_gate_creation_rng_schema', False)
    raw = assignment(config, 'native_gate_creation_rng_manifest', False)
    ack = assignment(config, 'native_gate_creation_rng_ack_schema', False)
    native_registry_creation.plan(config,assignment)
    if ack not in (None, '1', '2') or (ack is not None and schema is None):
        raise ValueError('construction RNG acknowledgement schema')
    if schema is None and raw is None:
        return None
    if schema != '1' or raw is None:
        raise ValueError('construction RNG explicit schema/manifest pair')
    text = quoted(raw)
    parse_manifest(text)
    options = 0
    for bit, name in enumerate(OPTION_FIELDS):
        value = assignment(config, 'native_gate_'+name, False)
        if value not in (None, 'false', 'true'):
            raise ValueError('construction RNG option grammar '+name)
        if value == 'true':
            options |= 1 << bit
    if options != MASK:
        raise ValueError('construction RNG unsupported option mask')
    for name in ('native_gate_player_probe','native_gate_player_origin_token','native_gate_management_test',
                 'native_gate_world_wait_manifest','native_gate_world_wait_rng_manifest',
                 'native_gate_world_find_rng_manifest','native_gate_player_launch_rng','native_gate_world_action_manifest',
                 'native_gate_world_values','native_gate_player_transition_observer','native_gate_npc_pilot','native_gate_world_trace_early'):
        if assignment(config, name, False) is not None:
            raise ValueError('construction RNG unsupported composition '+name)
    for name in ('native_gate_construction_test','native_gate_registry_md_trace'):
        if assignment(config, name) != 'true':
            raise ValueError('construction RNG prerequisite '+name)
    if assignment(config, 'native_gate_creation_trace_schema') != '2' or \
       quoted(assignment(config, 'native_gate_snapshot_sha')) != SNAPSHOT or \
       quoted(assignment(config, 'native_gate_construction_plan_sha')) != PLAN:
        raise ValueError('construction RNG scenario/schema differs')
    fixture = assignment(config, 'native_gate_snapshot_fixture')
    if not re.fullmatch(r"\{\s*['\"][0-9a-f]{16}['\"](?:\s*,\s*['\"][0-9a-f]{16}['\"]){11}\s*\}", fixture) or \
       tuple(re.findall(r"['\"]([0-9a-f]{16})['\"]", fixture)) != FIXTURE:
        raise ValueError('construction RNG imported fixture differs')
    return text


def bootstrap_agreement(config):
    text = bootstrap_plan(config)
    ack=assignment(config,'native_gate_creation_rng_ack_schema',False)
    return None if text is None else text+({'1':ACK_SUFFIX,'2':ACK2_SUFFIX}.get(ack,''))+\
        (native_registry_creation.SUFFIX if native_registry_creation.plan(config,assignment) else '')


def bootstrap_preimage(config):
    text = bootstrap_agreement(config)
    return None if text is None else BOOTSTRAP+text
