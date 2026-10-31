"""Private, copied unknown-owner diagnostics. Never supplies canonical identities.

Registry validation applies to Component/Entity only. Context lifetime is a
separate native field, not an asserted Component registry key. Repeated reads
cannot establish a reference lease or exclude byte-identical ABA.
"""
import hashlib
import math
import re
import struct

IMAGE_SIZE = 0x6fa9000
STUB_SHA = '95552e650460a140f624748f17306613061330a17be9794f9e1e1d21abfb2fa0'
ROW_KEYS = set('schema sample part reads bytes error stopped boundary before after'.split())
STOP_KEYS = set('schema samples complete rows exported open closed read_limit byte_limit hop_limit'.split())
Q_SCRIPT = 'image node exec frame current value context lifetime entity runtime context_vtable descriptor descriptor_vtable program begin end pc node_vtable execute actual_node'.split()
Q_OBJECT = 'object runtime saved parent registry page block_address object_id vtable col_address type_descriptor class_function bitmap_address'.split()


def fields(body, stop=False):
    if not body.isascii() or len(body) + 55 >= 2048:
        raise ValueError('row bound')
    out = {}
    for token in body.split():
        key, value = token.split('=', 1)
        if key in out:
            raise ValueError('duplicate field')
        if key in ('before', 'after'):
            if not re.fullmatch('[0-9a-f]+', value) or len(value) not in (728, 752):
                raise ValueError('copied record length/hex')
            out[key] = bytes.fromhex(value)
        else:
            if not re.fullmatch('[0-9]+', value) or int(value) >= 1 << 32:
                raise ValueError('integer bound')
            out[key] = int(value)
    if set(out) != (STOP_KEYS if stop else ROW_KEYS) or out['schema'] != 1:
        raise ValueError('unknown schema/fields')
    return out


def text(raw, length):
    if not 1 <= length < len(raw) or any(raw[length:]) or any(x < 33 or x > 126 for x in raw[:length]):
        raise ValueError('bounded text/NUL')
    return raw[:length].decode('ascii')


def script(raw):
    if len(raw) != 364:
        raise ValueError('script size')
    out = dict(zip(Q_SCRIPT, struct.unpack_from('<20Q', raw)))
    out.update(zip('tag version name_length fields error ancestors termination'.split(), struct.unpack_from('<7I', raw, 160)))
    out.update(name_raw=raw[188:284], storage=raw[284:316], flags=struct.unpack_from('<4I', raw, 316), clocks=struct.unpack_from('<4Q', raw, 332))
    if out['fields'] > 7 or out['ancestors'] > 8 or out['termination'] > 5 or out['error'] not in (0, 16, 32):
        raise ValueError('script flags/counts')
    return out


def obj(raw):
    if len(raw) != 376:
        raise ValueError('object size')
    out = dict(zip(Q_OBJECT, struct.unpack_from('<13Q', raw)))
    out.update(zip('fields registry_result type_result parent_read name_length'.split(), struct.unpack_from('<5I', raw, 104)))
    out.update(name_raw=raw[124:252], code=raw[252:288], bitmap=raw[288:320], block=raw[320:352], col=raw[352:376])
    if out['fields'] > 4095 or out['registry_result'] > 7 or out['type_result'] > 6 or out['parent_read'] > 1 or out['name_length'] > 127:
        raise ValueError('object flags/counts')
    return out


def inside(image, pointer, size=1):
    return image > 0 and image <= (1 << 64)-IMAGE_SIZE and image <= pointer <= image+IMAGE_SIZE-size


def valid_object(o, image, entity=False):
    """Derive typed support from raw evidence; producer success flags do not suffice."""
    if o['fields'] != 4095 or o['registry_result'] != 1 or o['type_result'] != 1 or o['parent_read'] != 1:
        return False
    runtime = o['runtime']; entry = runtime & 0x1ffffff
    if not runtime or not entry or not o['object'] or o['object_id'] != runtime or not o['registry'] or not o['page']:
        return False
    part = (entry-1) & 0xfffff; lane = part % 3
    block = o['block']; flags = struct.unpack_from('<I', block, 28)[0]
    generation = struct.unpack_from('<I', block, 24+(lane//2)*4)[0] >> (16*(lane % 2)) & 0xffff
    if o['block_address'] != o['page']+(part//3)*32 or ((flags >> (16+lane*4)) & 5) != 5 or generation != (runtime >> 25) & 0xffff or struct.unpack_from('<Q', block, lane*8)[0] != o['object']:
        return False
    if o['vtable'] % 8 or not inside(image, o['vtable'], 0x11e8) or o['vtable'] < image+8 or not inside(image, o['col_address'], 24):
        return False
    signature, offset, ctor, td, hierarchy, self_rva = struct.unpack('<6I', o['col'])
    if (signature, offset, ctor, self_rva) != (1, 0, 0, o['col_address']-image) or not td or not hierarchy or td > IMAGE_SIZE-144 or hierarchy > IMAGE_SIZE-16 or o['type_descriptor'] != image+td:
        return False
    try:
        if not text(o['name_raw'], o['name_length']).startswith('.?A'):
            return False
    except ValueError:
        return False
    if not inside(image, o['class_function'], 36) or not inside(image, o['bitmap_address'], 32):
        return False
    code = bytearray(o['code']); displacement = struct.unpack_from('<i', code, 6)[0]; code[6:10] = bytes(4)
    if hashlib.sha256(code).hexdigest() != STUB_SHA or o['bitmap_address'] != o['class_function']+10+displacement:
        return False
    def class_bit(n):
        return struct.unpack_from('<I', o['bitmap'], (n//16)*4)[0] >> (2*(n % 16)) & 3
    return bool(class_bit(22) and (not entity or class_bit(44)))


def valid_script(s, old):
    image = s['image']; length = s['name_length']; storage = s['storage']
    if s['fields'] != 7 or s['error'] or s['tag'] != 0x66 or s['context_vtable'] != image+0x2c6a270 or s['descriptor_vtable'] != image+0x2c69030:
        return False
    if not s['exec'] or s['frame'] != s['exec']+0x38 or s['current'] != s['exec'] or s['value'] != s['context']+0xa8 or not s['lifetime'] or not s['descriptor'] or not s['program']:
        return False
    if any(s[k] != old[k] for k in ('exec','current','context','lifetime','entity','runtime')) or s['tag'] != old['tag'] or s['value'] != old['raw_value']:
        return False
    if (old['reason'], old['reads'], old['owned'], old['errors'], old['ui'], old['boot'], old['tick'], old['saved'], old['eligible']) != (6,31,1,0,1,2,0,0,1):
        return False
    if old.get('phase_status') != 1 or not old.get('thread') or not 0 <= old.get('elapsed', -1) <= 120000 or old.get('raw_tag') != 0x66:
        return False
    try:
        text(s['name_raw'], length)
    except ValueError:
        return False
    stored_length, capacity = struct.unpack_from('<QQ', storage, 16)
    if stored_length != length or not length <= capacity <= 4096 or (capacity < 16 and storage[:length+1] != s['name_raw'][:length+1]) or (capacity >= 16 and not int.from_bytes(storage[:8], 'little')):
        return False
    count, remainder = divmod(s['end']-s['begin'], 8)
    if not s['begin'] or s['begin'] % 8 or remainder or not 1 <= s['pc'] <= count <= 4096 or s['node'] != s['actual_node'] or not s['node'] or not inside(image, s['node_vtable'], 32) or not inside(image, s['execute']):
        return False
    if s['flags'] != tuple(old[k] for k in ('app','vbi','view','main')) or s['clocks'] != tuple(old[k] for k in ('tls','game','real','saved_time')):
        return False
    return s['flags'][:2] in ((0x20,0),(0x26,1)) and s['flags'][2:] == (1,0) and s['clocks'][0] == s['clocks'][1] == s['clocks'][3] and all(math.isfinite(struct.unpack('<d',x.to_bytes(8,'little'))[0]) for x in s['clocks'])


def evidence(lines, unknown, errors, label='unknown identity'):
    rows = {}; stop = None; order = []; local = []; samples = []
    for line in lines:
        if 'NATIVE_GATE WORLD_UNKNOWN_IDENTITY' not in line:
            continue
        try:
            name, body = line.rstrip().split('NATIVE_GATE ', 1)[1].split(' ', 1)
            if name == 'WORLD_UNKNOWN_IDENTITY_STOP':
                if stop is not None:
                    raise ValueError('duplicate STOP')
                stop = fields(body, True)
            elif name == 'WORLD_UNKNOWN_IDENTITY':
                if stop is not None:
                    raise ValueError('row after STOP')
                row = fields(body); key = row['sample'], row['part']
                if key in rows or not 1 <= key[0] <= 28 or key[1] > 9 or row['reads'] > 512 or row['bytes'] > 16384 or row['error'] > 255 or row['stopped'] > 1 or row['boundary'] > 3:
                    raise ValueError('duplicate/bounded sample')
                rows[key] = row; order.append(key)
            else:
                raise ValueError('unknown record')
        except (ValueError, KeyError) as exc:
            local.append(f'{label}: {exc}')
    if not rows and stop is None and not local:
        return {'available': False, 'samples': [], 'coverage_complete': False, 'canonical_identity': False}
    expected = []
    for old in unknown:
        index = old['index']; result = {'sample': index, 'supported': False, 'canonical_identity': False, 'context_registry_supported': False, 'ancestry_complete': False, 'validation_errors': []}
        try:
            first = rows[index,0]; a = script(first['before']); b = script(first['after'])
            parts = 2+max(a['ancestors'], b['ancestors']); expected.extend((index, p) for p in range(parts))
            group = [rows[index,p] for p in range(parts)]
            common = ('schema','sample','reads','bytes','error','stopped','boundary')
            if any(any(row[k] != first[k] for k in common) for row in group):
                raise ValueError('inconsistent sample headers')
            owners = [obj(group[1][which]) for which in ('before','after')]
            ancestors = [[obj(row[which]) for row in group[2:]] for which in ('before','after')]
            result['copied_rows'] = [{k:(v.hex() if isinstance(v,bytes) else v) for k,v in row.items()} for row in group]
            equal = all(row['before'] == row['after'] for row in group)
            if not equal and not first['error'] & 64:
                raise ValueError('changed bytes without refusal')
            if first['stopped'] and not first['error'] & 8:
                raise ValueError('STOP without refusal')
            # Any failed/partial capture remains raw data, never a typed null.
            if first['error'] or first['stopped'] or first['boundary'] != 3 or not equal:
                result['refused'] = True
            else:
                if first['reads'] < 64 or first['bytes'] < first['reads']*4:
                    raise ValueError('impossible complete read budget')
                if not valid_script(a, old) or not valid_object(owners[0], a['image'], True) or owners[0]['object'] != a['entity'] or owners[0]['runtime'] != a['runtime'] or owners[0]['saved']:
                    raise ValueError('unsupported/forged owner or script success')
                current = owners[0]; seen = {current['object']}; terminal = None
                for index_in_chain, ancestor in enumerate(ancestors[0]):
                    if terminal is not None:
                        raise ValueError('ancestry continues after native terminal')
                    if not current['parent'] or current['parent'] != ancestor['object'] or ancestor['object'] in seen:
                        raise ValueError('ancestry link/cycle mismatch')
                    seen.add(ancestor['object'])
                    if not valid_object(ancestor, a['image']):
                        terminal = 5
                    elif ancestor['saved']:
                        terminal = 1
                    elif not ancestor['parent']:
                        terminal = 2
                    elif index_in_chain == 7:
                        terminal = 4
                    elif ancestor['parent'] in seen:
                        terminal = 3
                    current = ancestor
                termination = a['termination']
                if not ancestors[0]:
                    terminal = 2 if not current['parent'] else 3 if current['parent'] in seen else None
                if terminal != termination:
                    raise ValueError('ancestry termination mismatch')
                result.update(supported=True, script_name=text(a['name_raw'],a['name_length']), script_version=a['version'], stored_pc=a['pc'], observed_node=a['node'], actual_program_slot=a['pc']-1, entity_class=text(owners[0]['name_raw'],owners[0]['name_length']), context_lifetime=a['lifetime'], entity_runtime=a['runtime'], ancestry_complete=termination in (1,2), ancestry_termination=termination, saved_ancestor=current['saved'] if termination == 1 else None)
        except (ValueError, KeyError, IndexError, struct.error) as exc:
            message = f'{label} sample {index}: {exc}'; local.append(message); result['validation_errors'].append(message)
        samples.append(result)
    if order != expected:
        local.append(f'{label}: missing/extra/reordered sample parts')
    if stop is None or stop['samples'] != len(unknown) or stop['complete'] != len(unknown) or stop['rows'] != len(rows) or stop['exported'] != len(rows) or stop['open'] or stop['closed'] != 1 or (stop['read_limit'],stop['byte_limit'],stop['hop_limit']) != (512,16384,8):
        local.append(f'{label}: missing/incomplete STOP')
    errors.extend(local)
    return {'available': True, 'stop': stop, 'samples': samples, 'coverage_complete': not local and all(s['supported'] and s['ancestry_complete'] for s in samples), 'canonical_identity': False, 'byte_identical_aba_excluded': False}
