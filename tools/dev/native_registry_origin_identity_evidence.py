"""Passive first-origin presence, not a lease, initialization or enrollment proof."""
import re

FIELDS = ('schema runtime order thread recorded object type actual object_state owner_getter owner owner_state '
          'controller controller_type controller_owner controller_state slots agent agent_owner agent_state '
          'reads rereads reobject reowner recontroller reagent stable').split()
HEX = set(('runtime object type actual owner_getter owner controller controller_type controller_owner slots '
           'agent agent_owner reads rereads').split())
STATES = ('object_state', 'owner_state', 'controller_state', 'agent_state')
RESTATES = ('reobject', 'reowner', 'recontroller', 'reagent')


def check_reread(bits, states):
    """The second snapshot exports branch states, not its raw pointer values."""
    obj, owner, ctrl, agent = states
    if obj != 5:
        valid = ((obj == 1 and bits == 0) or (obj == 2 and bits < 3) or
                 (obj in (3,4) and bits == 3)) and owner == ctrl == agent == 0
    else:
        valid = bits & 3 == 3
        owner_bits = bits & 12
        valid &= ((owner in (1,5) and owner_bits == 12) or (owner == 3 and owner_bits == 4) or
                  (owner == 2 and owner_bits in (0,4)))
        ctrl_bits = bits & ~15
        if ctrl == 1:
            valid &= ctrl_bits == 16 and agent == 0
        elif ctrl == 3:
            valid &= ctrl_bits == 48 and agent == 0
        elif ctrl == 2:
            valid &= (ctrl_bits in (0,16) or (ctrl_bits & 48 == 48 and ctrl_bits & 448 != 448 and not ctrl_bits & 512)) and agent == 0
        elif ctrl in (4,5):
            valid &= ctrl_bits & 496 == 496 and ((agent in (1,2) and not ctrl_bits & 512) or
                                               agent == 4 or (agent == 5 and bool(ctrl_bits & 512)))
        else:
            valid = False
    if not valid:
        raise ValueError('origin identity reread branches/states')


def check_first(row):
    """Reconstruct exactly the permitted read branches, preserving negative states."""
    bits = row['reads']
    allowed = 0
    def read(bit):
        nonlocal allowed
        allowed |= bit
        return bool(bits & bit)
    expected = [0, 0, 0, 0]
    if not row['object']:
        expected[0] = 1
    else:
        type_read, id_read = read(1), read(2)
        if not type_read or not id_read:
            expected[0] = 2
        elif row['type'] not in (0x2b7feb8, 0x2b99a88):
            expected[0] = 3
        elif row['actual'] != row['runtime']:
            expected[0] = 4
        else:
            expected[0] = 5
            if not read(4):
                expected[1] = 2
            elif row['owner_getter'] != 0x352b10:
                expected[1] = 3
            elif not read(8):
                expected[1] = 2
            else:
                expected[1] = 5 if row['owner'] else 1
            if not read(16):
                expected[2] = 2
            elif not row['controller']:
                expected[2] = 1
            elif not read(32):
                expected[2] = 2
            elif row['controller_type'] != 0x2c51158:
                expected[2] = 3
            else:
                owner_read, slots_read, agent_read = read(64), read(128), read(256)
                if not owner_read or not slots_read or not agent_read:
                    expected[2] = 2
                else:
                    expected[2] = 5 if row['controller_owner'] == row['object'] else 4
                    if not row['agent']:
                        expected[3] = 1
                    elif row['agent'] > 2**64-1-0xad0 or row['slots'] != row['agent']+0xad0:
                        expected[3] = 4
                    elif not read(512):
                        expected[3] = 2
                    else:
                        expected[3] = 5 if row['agent_owner'] == row['object'] else 4
    if bits & ~allowed or expected != [row[k] for k in STATES]:
        raise ValueError('origin identity read branches/states')
    for bit, field in [(1,'type'), (2,'actual'), (4,'owner_getter'), (8,'owner'), (16,'controller'),
                       (32,'controller_type'), (64,'controller_owner'), (128,'slots'), (256,'agent'), (512,'agent_owner')]:
        if not bits & bit and row[field]:
            raise ValueError('origin identity unread field populated')


def evidence(log, updates, required=False):
    result = {'supported': False, 'rows': [], 'scope': 'passive_presence_only', 'lifetime_exclusion_proven': False}
    lines = []
    for match in re.finditer(r'NATIVE_GATE REGISTRY_ORIGIN_IDENTITY ([^\r\n]*)', log):
        if len(lines) == 256:
            raise ValueError('origin identity global capacity')
        lines.append(match[1])
    if not lines:
        if required:
            raise ValueError('required origin identity capture missing')
        return result
    if len(lines) != len(updates):
        raise ValueError('origin identity receipt coverage')
    seen = set()
    for line, update in zip(lines, updates):
        if len(line) > 1023:
            raise ValueError('origin identity row length')
        pairs = [p.split('=', 1) for p in line.split(' ')]
        if any(len(p) != 2 for p in pairs) or [p[0] for p in pairs] != FIELDS:
            raise ValueError('origin identity row schema')
        row = {}
        for key, value in pairs:
            if not re.fullmatch(r'[0-9a-f]{1,16}' if key in HEX else r'[0-9]{1,20}', value):
                raise ValueError('origin identity numeric encoding')
            row[key] = int(value, 16 if key in HEX else 10)
            if row[key] >= 2**64:
                raise ValueError('origin identity numeric bounds')
        if row['schema'] != 1 or any(row[k] != update[k] for k in ('runtime', 'order', 'thread')) or row['runtime'] in seen:
            raise ValueError('origin identity immutable lifetime/order/thread join')
        seen.add(row['runtime'])
        if update['kind'] != 6:
            if any(row[k] for k in FIELDS if k not in ('schema','runtime','order','thread')):
                raise ValueError('origin identity attached to different first family')
        else:
            if row['recorded'] != 1 or row['reads'] > 1023 or row['rereads'] > 1023 or row['stable'] not in (1,2,3) or \
               any(row[k] > 5 for k in STATES+RESTATES):
                raise ValueError('origin identity capture encoding')
            check_first(row)
            check_reread(row['rereads'], [row[k] for k in RESTATES])
            unreadable = any(row[k] == 2 for k in STATES+RESTATES)
            if (row['stable'] == 3) != unreadable:
                raise ValueError('origin identity unreadable reread status')
            if row['stable'] == 1 and (row['reads'] != row['rereads'] or any(row[a] != row[b] for a,b in zip(STATES,RESTATES))):
                raise ValueError('origin identity false stable reread')
        result['rows'].append(row)
    result['supported'] = True
    return result
