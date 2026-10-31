"""Check every address and pin of the Linux gate core against the executables, offline.

  python tools/dev/check_lockstep_gate_linux_sites.py --linux PATH/TO/X4 [--windows PATH/TO/X4.exe] [--json OUT]

Reads (never runs) the pinned Linux X4 9.00 executable: file size and SHA-256, the
build ID, the PT_TLS and text segments, each site hash of src/native/engine/linux/
gate_sites.h, and decodes the instructions that tie every data address and static
TLS offset (src/native/engine/lockstep_tls.h) to a pinned site. With --windows it also
decodes the Windows counterparts the gate pairs them with (lockstep_gate.c,
tools/dev/lockstep_linux_known_pairs.json). Prints no executable bytes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[2]
SITES_H = ROOT / 'src/native/engine/linux/gate_sites.h'
TLS_H = ROOT / 'src/native/engine/lockstep_tls.h'
GATE_C = ROOT / 'src/native/engine/lockstep_gate.c'
KNOWN = ROOT / 'tools/dev/lockstep_linux_known_pairs.json'
XRCP_H = ROOT / 'src/native/portable_math/preload/exact_reciprocal_linux_sites.h'


def parse_sites(text=None):
    """(constants, sites) from gate_sites.h: #define LX_* values and the lx_sites rows."""
    text = text if text is not None else SITES_H.read_text(encoding='utf-8')
    constants = {}
    for name, value in re.findall(r'^#define (LX_\w+) ("[^"]*"|0x[0-9a-fA-F]+(?:u|ull)?|\d+(?:u|ull)?)', text, re.M):
        constants[name] = value.strip('"') if value.startswith('"') else int(value.rstrip('ul'), 0)
    rows = re.findall(r'\{"(\w+)",(0x[0-9a-f]+),(0x[0-9a-f]+),"([0-9a-f]{64})"\}', text)
    sites = [{'name': n, 'va': int(v, 16), 'length': int(l, 16), 'sha256': h} for n, v, l, h in rows]
    return constants, sites


def parse_tls(text=None):
    text = text if text is not None else TLS_H.read_text(encoding='utf-8')
    return {name: int(value, 0) for name, value in re.findall(r'#define GATE_TLS_(\w+) \((-?0x[0-9a-f]+)\)', text)} | \
        {name: int(value, 0) for name, value in re.findall(r'#define GATE_TLS_(WINDOWS_\w+) (0x[0-9a-f]+)', text)}


class Elf:
    def __init__(self, data):
        if data[:7] != b'\x7fELF\x02\x01\x01':
            raise ValueError('not an ELF64 little-endian file')
        self.data = data
        self.type, self.machine = struct.unpack_from('<HH', data, 16)
        phoff, = struct.unpack_from('<Q', data, 32)
        self.phentsize, self.phnum = struct.unpack_from('<HH', data, 54)
        self.segments = []
        for i in range(self.phnum):
            kind, flags, offset, vaddr, _, filesz, memsz, align = struct.unpack_from('<IIQQQQQQ', data, phoff + i * self.phentsize)
            self.segments.append({'type': kind, 'flags': flags, 'offset': offset, 'vaddr': vaddr, 'filesz': filesz,
                                  'memsz': memsz, 'align': align})

    def read(self, va, size):
        for s in self.segments:
            if s['type'] == 1 and s['vaddr'] <= va and va + size <= s['vaddr'] + s['filesz']:
                at = s['offset'] + va - s['vaddr']
                return self.data[at:at + size]
        raise ValueError(f'{va:#x} outside the file-backed segments')


class Pe:
    BASE = 0x140000000

    def __init__(self, data):
        if data[:2] != b'MZ':
            raise ValueError('not a PE file')
        self.data = data
        pe, = struct.unpack_from('<I', data, 0x3c)
        count, = struct.unpack_from('<H', data, pe + 6)
        optional, = struct.unpack_from('<H', data, pe + 20)
        self.sections = []
        for i in range(count):
            at = pe + 24 + optional + 40 * i
            vsize, va, raw_size, raw = struct.unpack_from('<IIII', data, at + 8)
            self.sections.append((va, max(vsize, raw_size), raw, raw_size))

    def read(self, rva, size):
        for va, vsize, raw, raw_size in self.sections:
            if va <= rva and rva + size <= va + min(vsize, raw_size):
                return self.data[raw + rva - va:raw + rva - va + size]
        raise ValueError(f'{rva:#x} outside the PE sections')


def rip_target(image, va, length, disp_at, prefix):
    """Target of a rip-relative operand: the instruction at va (length bytes, disp32 at disp_at) must start with prefix."""
    code = image.read(va, length)
    if not code.startswith(prefix):
        raise AssertionError(f'{va:#x}: unexpected instruction')
    disp, = struct.unpack_from('<i', code, disp_at)
    return va + length + disp


def check_linux(path, constants, sites, tls):
    data = Path(path).read_bytes()
    findings = []

    def ok(name, condition, detail=''):
        if not condition:
            raise AssertionError(f'{name} {detail}'.strip())
        findings.append(name)
    c = constants
    ok('file_size', len(data) == c['LX_FILE_SIZE'], f'{len(data)}')
    ok('file_sha256', hashlib.sha256(data).hexdigest() == c['LX_FILE_SHA'])
    elf = Elf(data)
    ok('elf_exec_x86_64', elf.type == 2 and elf.machine == 62 and elf.phentsize == 56 and elf.phnum == 12)
    tls_segment = [s for s in elf.segments if s['type'] == 7]
    ok('pt_tls', len(tls_segment) == 1 and (tls_segment[0]['vaddr'], tls_segment[0]['memsz'], tls_segment[0]['align']) ==
       (c['LX_TLS_VADDR'], c['LX_TLS_MEMSZ'], c['LX_TLS_ALIGN']))
    ok('pt_load_text', any(s['type'] == 1 and s['vaddr'] == c['LX_TEXT_VA'] and s['flags'] == 5 for s in elf.segments))
    ok('build_id', elf.read(0x4002fc + 16, 20).hex() == c['LX_BUILD_ID'])
    for site in sites:
        ok(f"site:{site['name']}", hashlib.sha256(elf.read(site['va'], site['length'])).hexdigest() == site['sha256'])
    by_name = {s['name']: s for s in sites}

    def inside(site, va, size):
        s = by_name[site]
        return s['va'] <= va and va + size <= s['va'] + s['length']
    # The FrameTick call and its 16-byte compare-exchange window.
    call, window = c['LX_CALL_VA'], c['LX_CALL_WINDOW']
    ok('call_rel32_to_frame_tick', rip_target(elf, call, 5, 1, b'\xe8') == c['LX_TICK_VA'] and inside('main_loop_frame_tick_call', call, 5))
    ok('call_window', window % 16 == 0 and window <= call + 1 and call + 5 <= window + 16 and inside('main_loop_frame_tick_call', window, 16))
    ok('frame_tick_head_is_pinned_entry', by_name['frame_tick_head']['va'] == c['LX_TICK_VA'])
    # Data addresses, each referenced rip-relative from a pinned site.
    ok('in_flight_set_after_call', rip_target(elf, call + 5, 7, 2, b'\xc6\x05') == c['LX_IN_FLIGHT_VA'] and elf.read(call + 11, 1) == b'\x01')
    ok('in_flight_checked', rip_target(elf, 0x72f638, 7, 2, b'\x80\x3d') == c['LX_IN_FLIGHT_VA'] and inside('main_loop_in_flight_check', 0x72f638, 7))
    ok('in_flight_cleared_on_sim_done', rip_target(elf, 0x72f7aa, 7, 2, b'\xc6\x05') == c['LX_IN_FLIGHT_VA'] and elf.read(0x72f7b0, 1) == b'\x00')
    vbi = by_name['move_worker_vbi_dispatch']['va']
    ok('vbi_table_global', rip_target(elf, vbi, 7, 3, b'\x48\x8b\x1d') == c['LX_VBI_TABLE_VA'])
    ok('vbi_loading_flag', elf.read(vbi + 7, 6) == b'\x8b\xab' + struct.pack('<I', c['LX_VBI_LOADING']))
    ok('timer_speed', rip_target(elf, 0x2025296, 8, 4, b'\xf2\x0f\x10\x05') == c['LX_SPEED_VA'] and inside('frame_tick_time_advance', 0x2025296, 8))
    ok('timer_game_add', rip_target(elf, 0x20252a5, 8, 4, b'\xf2\x0f\x58\x05') == c['LX_GAME_VA'])
    ok('timer_game_store', rip_target(elf, 0x20252ad, 8, 4, b'\xf2\x0f\x11\x05') == c['LX_GAME_VA'])
    ok('timer_real_add', rip_target(elf, 0x20252d8, 8, 4, b'\xf2\x0f\x58\x0d') == c['LX_REAL_VA'] and inside('frame_tick_time_advance', 0x20252e3, 8))
    ok('timer_real_store', rip_target(elf, 0x20252e3, 8, 4, b'\xf2\x0f\x11\x0d') == c['LX_REAL_VA'])
    movie = by_name['frame_tick_movie_branch']['va']
    ok('movie_flag', rip_target(elf, movie, 7, 2, b'\x80\x3d') == c['LX_MOVIE_FLAG_VA'])
    ok('movie_frame_time', rip_target(elf, movie + 7, 8, 4, b'\xf2\x0f\x10\x0d') == c['LX_MOVIE_DT_VA'])
    ok('movie_branch_to_dt_path', rip_target(elf, movie + 15, 6, 2, b'\x0f\x84') == c['LX_DT_LOAD_VA'])
    # Fixed dt: the original clamp load, the patched load to 1/60, the minsd and its NOP.
    load, minsd = c['LX_DT_LOAD_VA'], c['LX_DT_MIN_VA']
    ok('dt_qwords_aligned_in_site', load % 8 == 0 and minsd == load + 8 and load // 4096 == (minsd + 7) // 4096 and
       inside('frame_tick_time_advance', load, 16))
    ok('dt_load_original', struct.unpack('<Q', elf.read(load, 8))[0] == c['LX_DT_LOAD_ORIGINAL'])
    ok('dt_min_original', struct.unpack('<Q', elf.read(minsd, 8))[0] == c['LX_DT_MIN_ORIGINAL'])
    clamp = rip_target(elf, load, 8, 4, b'\xf2\x0f\x10\x0d')
    ok('dt_clamp_is_one', struct.unpack('<d', elf.read(clamp, 8))[0] == 1.0)
    ok('one_sixtieth', struct.unpack('<d', elf.read(c['LX_ONE_SIXTIETH_VA'], 8))[0] == 1.0 / 60.0 and
       by_name['rodata_one_sixtieth']['va'] == c['LX_ONE_SIXTIETH_VA'])
    disp = c['LX_ONE_SIXTIETH_VA'] - (load + 8)
    ok('dt_patch_reach', -2**31 <= disp < 2**31)
    patched = struct.pack('<Q', c['LX_DT_MIN_PATCHED'])
    ok('dt_min_patch', patched[:4] == b'\x0f\x1f\x40\x00' and patched[4:] == elf.read(minsd + 4, 4))
    # Static TLS: the offsets of lockstep_tls.h in the code that uses them, inside PT_TLS.
    seed = by_name['thread_rng_seed']['va']
    code = elf.read(seed, by_name['thread_rng_seed']['length'])
    ok('tls_rng_store', b'\x64\x48\x89\x3c\x25' + struct.pack('<i', tls['LINUX_RNG']) in code)
    entry = by_name['move_worker_run_entry']
    code = elf.read(entry['va'], entry['length'])
    ok('tls_view_set_to_one', b'\x64\xc7\x04\x25' + struct.pack('<i', tls['LINUX_VIEW']) + b'\x01\x00\x00\x00' in code)
    start = by_name['move_worker_frame_start']
    code = elf.read(start['va'], start['length'])
    ok('tls_time_read', b'\x64\xf2\x0f\x10\x04\x25' + struct.pack('<i', tls['LINUX_TIME']) in code)
    ok('tls_previous_time_store', b'\x64\xf2\x0f\x11\x04\x25' + struct.pack('<i', tls['LINUX_PREVIOUS_TIME']) in code)
    ok('tls_time_sync_store', b'\x64\x48\x89\x2c\x25' + struct.pack('<i', tls['LINUX_TIME']) in code)
    ok('tls_offsets_in_block', all(-c['LX_TLS_MEMSZ'] <= tls[k] and tls[k] + 8 <= 0 for k in tls if k.startswith('LINUX_')))
    # World identities and world hash (gate_world.h): the hooked map insert, the UniverseID
    # import map, the component registry and find, the bank selectors, the shield source.
    entry = c['LX_IMPORT_INSERT_VA']
    ok('import_insert_entry', by_name['import_map_insert']['va'] == entry and entry % 16 == 0 and
       struct.unpack('<Q', elf.read(entry, 8))[0] == c['LX_IMPORT_ENTRY_ORIGINAL'] and
       elf.read(entry, c['LX_IMPORT_PROLOGUE']) == b'\x41\x56\x41\x55\x41\x54')
    site = by_name['import_universe_site']['va']
    ok('import_universe_map', rip_target(elf, site, 7, 3, b'\x48\x8d\x3d') == c['LX_IMPORT_MAP_VA'])
    ok('import_universe_insert_call', rip_target(elf, site + 11, 5, 1, b'\xe8') == entry)
    ok('import_universe_value_store', elf.read(site + 0x1e, 4) == b'\x4c\x89\x78\x28')  # mov [rax+0x28], r15 (runtime ID)
    # The load function frees the import map through the clear (at two places).
    clear = c['LX_IMPORT_CLEAR_VA']
    ok('import_clear_entry', by_name['import_map_clear']['va'] == clear and clear % 16 == 0 and
       struct.unpack('<Q', elf.read(clear, 8))[0] == c['LX_IMPORT_CLEAR_ORIGINAL'] and
       elf.read(clear, c['LX_IMPORT_CLEAR_PROLOGUE']) == b'\x53\x48\x83\xec\x40')
    ok('import_clear_frees_nodes', rip_target(elf, clear + 0xa0, 5, 1, b'\xe8') == 0x2c52010)  # aligned operator delete of the blocks
    for name in ('import_clear_load_a', 'import_clear_load_b'):
        at = by_name[name]['va']
        ok(name + '_map', rip_target(elf, at, 7, 3, b'\x48\x8d\x3d') == c['LX_IMPORT_MAP_VA'])
        ok(name + '_call', rip_target(elf, at + 7, 5, 1, b'\xe8') == clear)
    use = by_name['component_find_use']['va']
    ok('component_registry', rip_target(elf, use, 7, 3, b'\x48\x8b\x3d') == c['LX_COMPONENT_MAP_VA'])
    ok('component_find_call', rip_target(elf, use + 14, 5, 1, b'\xe8') == c['LX_FIND_VA'] and by_name['component_find']['va'] == c['LX_FIND_VA'])
    ok('bank_write_selector', rip_target(elf, 0x13e35c4, 6, 2, b'\x2b\x05') == c['LX_BANK_WRITE_VA'] and inside('move_bank_swap', 0x13e35c4, 6))
    ok('bank_read_selector', rip_target(elf, 0x13e35ca, 6, 2, b'\x2b\x15') == c['LX_BANK_READ_VA'] and inside('move_bank_swap', 0x13e35ca, 6))
    ok('nav_select_reads_read_selector', rip_target(elf, 0x144990a, 6, 2, b'\x8b\x05') == c['LX_BANK_READ_VA'] and inside('nav_select', 0x144990a, 6))
    ok('shield_source_vtable', rip_target(elf, 0x17be40f, 7, 3, b'\x48\x8d\x3d') == c['LX_SHIELD_SOURCE_VTABLE'] and
       inside('shield_group_construct', 0x17be40f, 7))
    # Serial pathing and path determinism (gate_path.h): the release, the join, the batches, the budget.
    release = c['LX_PATH_RELEASE_SITE']
    ok('path_release_call', rip_target(elf, release, 5, 1, b'\xe8') == c['LX_SEM_RELEASE_VA'] and inside('move_worker_frame_start', release, 5))
    ok('path_release_word', struct.unpack('<Q', elf.read(release & ~7, 8))[0] == c['LX_PATH_RELEASE_ORIGINAL'] and release % 8 + 5 <= 8)
    ok('path_release_manager', rip_target(elf, 0x12125c5, 7, 3, b'\x48\x8b\x3d') == c['LX_PATH_MANAGER_VA'] and
       elf.read(0x12125d8, 7) == b'\xc6\x87\x98\x01\x00\x00\x00' and elf.read(0x12125df, 4) == b'\x48\x83\xc7\x70')
    ok('path_late_join', elf.read(0x13e3574, 6) == b'\x8b\x87\x94\x01\x00\x00' and elf.read(0x13e3588, 4) == b'\x48\x8d\x7f\x10' and
       inside('move_bank_swap', 0x13e3588, 4))
    ok('path_batch_vectors', elf.read(0x13e3d9a, 7) == b'\x4c\x8b\xb3\xb8\x02\x00\x00' and elf.read(0x13e3da1, 7) == b'\x4c\x8b\xbb\xb0\x02\x00\x00' and
       elf.read(0x13e3e18, 7) == b'\x4c\x8b\xb3\xd0\x02\x00\x00' and elf.read(0x13e3e1f, 7) == b'\x4c\x8b\xbb\xc8\x02\x00\x00' and
       elf.read(0x13e3e0f, 4) == b'\x49\x83\xc7\x20' and inside('path_run_batches', 0x13e3d9a, 0x8c))
    ok('path_batch_job', rip_target(elf, 0x13e3dad, 7, 3, b'\x4c\x8d\x2d') == 0x13e32f0 and by_name['path_update_batch']['va'] == 0x13e32f0)
    ok('path_budget_read', elf.read(0x13e3416, 9) == b'\x66\x41\x0f\x2f\x87\xa8\x03\x00\x00' and inside('path_update_batch', 0x13e3416, 9))
    # gate_path_budget.h: the clock block's first 16 bytes, 16-aligned, the cheap byte it stores, its two exits.
    ok('path_budget_block', elf.read(0x13e33d0, 16) == bytes.fromhex('4d8b97a0030000488d742420bf040000') and 0x13e33d0 % 16 == 0 and
       elf.read(0x13e33e1, 4) == b'\x88\x54\x24\x1f' and elf.read(0x13e342d, 5) == b'\x0f\xb6\x6c\x24\x1f' and
       elf.read(0x13e3374, 4) == b'\x49\x83\xc5\x01' and elf.read(0x13e3425, 4) == b'\x49\x8b\x46\x18' and
       inside('path_update_batch', 0x13e33d0, 0x60))
    ok('path_budget_native', rip_target(elf, 0x13e92a7, 7, 3, b'\x48\x8b\x05') == 0x2e2b8e8 and
       struct.unpack('<Q', elf.read(0x2e2b8e8, 8))[0] == 0x3f50624dd2f1a9fc and elf.read(0x13e92b9, 7) == b'\x48\x89\x83\xa8\x03\x00\x00')
    sync = c['LX_PATH_SYNC_VA']
    ok('path_sync_time', by_name['path_sync_time']['va'] == sync and elf.read(sync + 0x44, 10) == b'\x64\xf2\x0f\x11\x04\x25\xd8\xd8\xff\xff' and
       elf.read(sync + 0x19, 10) == b'\x64\xf2\x0f\x11\x04\x25\xd8\xd9\xff\xff')
    ok('path_refresh', by_name['nav_select']['va'] == c['LX_PATH_REFRESH_VA'] and elf.read(c['LX_PATH_REFRESH_VA'] + 0x22, 10) == b'\x64\xf2\x0f\x10\x04\x25\xd8\xd8\xff\xff')
    ok('semaphores', by_name['sem_release']['va'] == c['LX_SEM_RELEASE_VA'] and by_name['sem_timed_wait']['va'] == c['LX_SEM_TIMED_WAIT_VA'])
    # Hazard region updates (gate_region.h): the Move slot, the field vector, the pass times.
    move = c['LX_REGION_MOVE_VA']
    ok('region_move_slot', c['LX_REGION_SLOT_VA'] == c['LX_REGION_VTABLE'] + 0x1558 and
       struct.unpack('<Q', elf.read(c['LX_REGION_SLOT_VA'], 8))[0] == move and by_name['region_move']['va'] == move)
    ok('region_field_vector', elf.read(move + 0x16, 7) == b'\x4c\x8b\xa5\x88\x02\x00\x00' and elf.read(move + 0x1d, 7) == b'\x48\x8b\x9d\x80\x02\x00\x00' and
       elf.read(move + 0x3a, 3) == b'\xff\x50\x48')
    update = c['LX_DAMAGE_FIELD_UPDATE_VA']
    ok('region_field_slot', struct.unpack('<Q', elf.read(c['LX_DAMAGE_FIELD_VTABLE'] + 0x48, 8))[0] == update and by_name['region_field_update']['va'] == update)
    ok('region_field_times', elf.read(update + 0x3b, 8) == b'\x66\x0f\x2f\x85\xf8\x10\x00\x00' and elf.read(update + 0x65, 8) == b'\xf2\x0f\x5c\x85\xf0\x10\x00\x00')
    # Both vtable slots the gate swaps lie in RELRO: gate_data_swap64 leaves its page read-only.
    (phoff, phentsize, phnum) = (struct.unpack_from('<Q', data, 32)[0], struct.unpack_from('<H', data, 54)[0], struct.unpack_from('<H', data, 56)[0])
    relro = [struct.unpack_from('<IIQQQQQQ', data, phoff + i * phentsize) for i in range(phnum)]
    relro = [(h[3], h[3] + h[6]) for h in relro if h[0] == 0x6474e552]
    for name in ('LX_REGION_SLOT_VA', 'LX_AI_WAIT_SLOT_VA', 'LX_SECTOR_MOVE_SLOT_VA', 'LX_GRAVIDAR_UNIT_SLOT_VA'):
        ok(f'{name}_in_relro', any(low <= c[name] and c[name] + 8 <= high for low, high in relro), str(relro))
    # Runtime IDs (gate_runtime_ids.h): the map, Remove with its one append to the free list, the fields by bytes.
    remove, assign = c['LX_ID_REMOVE_VA'], c['LX_ID_ASSIGN_VA']
    ok('id_remove_entry', elf.read(remove, 8) == struct.pack('<Q', c['LX_ID_REMOVE_ENTRY_ORIGINAL']) and elf.read(remove, c['LX_ID_REMOVE_PROLOGUE']) == bytes.fromhex('415741564155') and remove % 8 == 0 and
       by_name['id_remove']['va'] == remove and elf.read(remove + 8, 4) == bytes.fromhex('4c8d6710'))
    ok('id_remove_fields', elf.read(0xee3882, 5) == bytes.fromhex('488b4cd560') and elf.read(0xee3958, 3) == bytes.fromhex('8b4d5c') and elf.read(0xee3966, 3) == bytes.fromhex('897558') and inside('id_remove', 0xee3966, 3))
    ok('id_assign_map', rip_target(elf, assign + 0xf, 7, 3, b'\x4c\x8b\x2d') == c['LX_ID_MAP_VA'] and by_name['id_assign']['va'] == assign)
    ok('id_component_return', rip_target(elf, c['LX_ID_COMPONENT_RETURN_VA'] - 5, 5, 1, b'\xe8') == assign)
    ok('id_assign_fields', elf.read(0xeed892, 8) == bytes.fromhex('41817d4cff8f0100') and elf.read(0xeed8db, 5) == bytes.fromhex('bfc0aaaa00') and elf.read(0xeed9e4, 4) == bytes.fromhex('458b4d58') and
       elf.read(0xeeda05, 5) == bytes.fromhex('4d8b44d560') and elf.read(0xeeda72, 4) == bytes.fromhex('41897d5c') and elf.read(0xeedaa5, 5) == bytes.fromhex('41836d4c01') and inside('id_assign', 0xeedaa5, 5))
    # Travel snapshot (gate_travel_snapshot.h): the wingman's read, the padding, the job and its two LEAs.
    read_va, word_va = c['LX_TRAVEL_READ_VA'], c['LX_TRAVEL_READ_WORD_VA']
    ok('travel_read', elf.read(read_va, 7) == bytes.fromhex('80ba9b0f000000') and word_va == read_va - 3 and word_va % 8 == 0 and inside('travel_wingman_read', read_va, 7) and
       elf.read(0x1446b2e, 7) == bytes.fromhex('80ba990f000000') and elf.read(0x14458df, 7) == bytes.fromhex('80bb9b0f000000') and elf.read(0x14458e8, 7) == bytes.fromhex('488b9340 0f0000'.replace(' ', '')) and
       inside('travel_block', 0x14458e8, 7))
    uses = [m.start() for m in re.finditer(bytes.fromhex('9b0f0000'), elf.read(0x656000, 0x261d0a9))]
    readers = [0x656000 + at - 2 for at in uses if elf.read(0x656000 + at - 2, 1) == b'\x80']
    ok('travel_flag_readers', sorted(readers) == sorted([read_va, 0x14458df]), str([hex(a) for a in readers]))
    ok('travel_agent_size', elf.read(0x13d1e3b, 5) == bytes.fromhex('bfb00f0000') and elf.read(0x13d1e40, 1) == b'\xe8' and inside('nav_agent_construct', 0x13d1e3b, 5) and
       elf.read(0x13d26b1, 11) == bytes.fromhex('48c783a00f000000000000') and inside('nav_agent_construct', 0x13d26b1, 11))
    job = c['LX_PATH_JOB_VA']
    for name, base, disp, at in (('a', c['LX_PATH_JOB_LEA_A_VA'], c['LX_PATH_JOB_LEA_A_DISP'], 0x13e3db9), ('b', c['LX_PATH_JOB_LEA_B_VA'], c['LX_PATH_JOB_LEA_B_DISP'], 0x13e3e2d)):
        ok(f'travel_job_lea_{name}', base % 16 == 0 and base + disp == at + 3 and disp + 4 <= 16 and elf.read(at, 3) == b'\x4c\x8d\x25' and at + 7 + struct.unpack('<i', elf.read(at + 3, 4))[0] == job and inside('path_run_batches', at, 7))
    ok('travel_fresh_list', elf.read(0x13e37bd, 7) == bytes.fromhex('4c8ba3a8020000') and elf.read(0x13e37c4, 5) == bytes.fromhex('4d8b6c2408') and inside('path_fresh_list', 0x13e37bd, 7) and elf.read(0x13e37e0, 4) == bytes.fromhex('498b7510'))
    # Gravidar deadlines (gate_gravidar_deadlines.h): the slot, its only holder, the fields of Sector::Move.
    sector_move = c['LX_SECTOR_MOVE_VA']
    needle = struct.pack('<Q', sector_move)
    holders = [at for at in range(0, len(data) - 8, 8) if data[at:at + 8] == needle]
    ok('sector_move_slot', c['LX_SECTOR_MOVE_SLOT_VA'] == c['LX_SECTOR_VTABLE'] + 0x1558 and struct.unpack('<Q', elf.read(c['LX_SECTOR_MOVE_SLOT_VA'], 8))[0] == sector_move and len(holders) == 1 and
       by_name['sector_move_head']['va'] == sector_move, str(holders))
    ok('sector_vtable', elf.read(struct.unpack('<Q', elf.read(struct.unpack('<Q', elf.read(c['LX_SECTOR_VTABLE'] - 8, 8))[0] + 8, 8))[0], 12) == b'N1U6SectorE\0' and struct.unpack('<q', elf.read(c['LX_SECTOR_VTABLE'] - 16, 8))[0] == 0)
    ok('sector_move_fields', elf.read(sector_move + 0x11, 7) == bytes.fromhex('83bfec01000004') and elf.read(0x127cf5a, 7) == bytes.fromhex('80bb3a04000000') and elf.read(0x127cf7e, 8) == bytes.fromhex('660f2f8340040000') and
       elf.read(0x127d0f5, 8) == bytes.fromhex('f20f118340040000') and elf.read(0x127d141, 8) == bytes.fromhex('f20f118340040000') and inside('sector_move_head', 0x127cf7e, 8) and inside('sector_move_schedule', 0x127d141, 8))
    # The Gravidar (gate_gravidar.h): the publish call in its qword, the unit slot, the worker's fields by bytes.
    gcall = c['LX_GRAVIDAR_CALL_VA']
    ok('gravidar_call', elf.read(gcall, 1) == b'\xe8' and gcall + 5 + struct.unpack('<i', elf.read(gcall + 1, 4))[0] == c['LX_GRAVIDAR_PUBLISH_VA'] and
       c['LX_GRAVIDAR_CALL_WORD_VA'] == gcall - 1 and (gcall - 1) % 8 == 0 and elf.read(gcall - 2, 2) == b'\x31\xf6' and
       rip_target(elf, gcall - 9, 7, 3, b'\x48\x8b\x3d') == c['LX_GRAVIDAR_WORKER_VA'] and inside('move_worker_frame_start', gcall - 9, 14))
    apply_va = c['LX_GRAVIDAR_UNIT_APPLY_VA']
    needle = struct.pack('<Q', apply_va)
    holders = [at for at in range(0, len(data) - 8, 8) if data[at:at + 8] == needle]
    ok('gravidar_unit_slot', c['LX_GRAVIDAR_UNIT_SLOT_VA'] == c['LX_GRAVIDAR_UNIT_VTABLE'] + 0x40 and struct.unpack('<Q', elf.read(c['LX_GRAVIDAR_UNIT_SLOT_VA'], 8))[0] == apply_va and len(holders) == 1, str(holders))
    ok('gravidar_vtables', all(elf.read(struct.unpack('<Q', elf.read(struct.unpack('<Q', elf.read(vt - 8, 8))[0] + 8, 8))[0], len(name)) == name and struct.unpack('<q', elf.read(vt - 16, 8))[0] == 0
                               for vt, name in ((c['LX_GRAVIDAR_VTABLE'], b'N2AI8GravidarE\0'), (c['LX_GRAVIDAR_UNIT_VTABLE'], b'N2AI18ActiveGravidarUnitE\0'), (c['LX_MACRO_VTABLE'], b'N1U5MacroE\0'))))
    ok('gravidar_run_slot', struct.unpack('<Q', elf.read(c['LX_GRAVIDAR_VTABLE'] + 0x10, 8))[0] == c['LX_GRAVIDAR_RUN_VA'] and by_name['gravidar_run_head']['va'] == c['LX_GRAVIDAR_RUN_VA'])
    run = c['LX_GRAVIDAR_RUN_VA']
    ok('gravidar_run_frame', elf.read(run, 4) == bytes.fromhex('554889e5') and elf.read(0x1106f03, 4) == bytes.fromhex('488d4340') and elf.read(0x1106f0c, 7) == bytes.fromhex('48898560ffffff') and
       elf.read(0x1106f1f, 11) == bytes.fromhex('48c78510ffffff00000000') and inside('gravidar_run_head', 0x1106f1f, 11) and rip_target(elf, 0x1106ef0, 7, 3, b'\x48\x8d\x3d') == 0x2d44783 and elf.read(0x2d44783, 9) == b'Gravidar\0')
    ok('gravidar_time', rip_target(elf, 0x1106f87, 7, 3, b'\x48\x8d\x3d') == c['LX_GAME_TIME_LOCK_VA'] and rip_target(elf, 0x1106f9d, 8, 4, b'\xf2\x0f\x10\x2d') == c['LX_GAME_TIME_GLOBAL_VA'] and inside('gravidar_run_head', 0x1106f9d, 8))
    ok('gravidar_ready', elf.read(0x1107020, 7) == bytes.fromhex('4c8db368710000') and elf.read(0x110702f, 7) == bytes.fromhex('c6839871000001') and inside('gravidar_run_ready', 0x110702f, 7) and
       elf.read(0x110c808, 7) == bytes.fromhex('80bb9871000000') and inside('gravidar_init_kick', 0x110c808, 7))
    ok('gravidar_round_robin', elf.read(0x1108bf7, 7) == bytes.fromhex('483983d8700000') and elf.read(0x1108c23, 7) == bytes.fromhex('480383c0700000') and elf.read(0x1108c6b, 7) == bytes.fromhex('48898510ffffff') and
       inside('gravidar_round_robin', 0x1108bf7, 7) and inside('gravidar_round_robin', 0x1108c6b, 7))
    ok('gravidar_list_lock', elf.read(0x11087ca, 7) == bytes.fromhex('4c8dab90700000') and elf.read(0x11087c3, 7) == bytes.fromhex('488dbb98700000') and inside('gravidar_list_lock', 0x11087ca, 7))
    ok('x4_mutex', elf.read(0x1a23652, 4) == bytes.fromhex('4883c708') and elf.read(0x1a236a2, 4) == bytes.fromhex('4883c708') and elf.read(0x1a23532, 5) == bytes.fromhex('be01000000'))
    ok('macro_getter', elf.read(c['LX_MACRO_NAME_GETTER_VA'], 5) == bytes.fromhex('488d4720c3') and struct.unpack('<Q', elf.read(c['LX_MACRO_VTABLE'] + 19 * 8, 8))[0] == c['LX_MACRO_NAME_GETTER_VA'])
    ok('text_end', next(s['vaddr'] + s['filesz'] for s in elf.segments if s['type'] == 1 and s['flags'] & 1) == c['LX_TEXT_END_VA'])
    # Event order (gate_event_order.h): the delivery call and the slots it reads; the word it ends.
    call_va, word_va = c['LX_EVENT_CALL_VA'], c['LX_EVENT_CALL_WORD_VA']
    ok('event_call', elf.read(call_va, 1) == b'\xe8' and call_va + 5 + struct.unpack('<i', elf.read(call_va + 1, 4))[0] == c['LX_EVENT_DELIVER_VA'] and
       word_va == call_va - 3 and word_va % 8 == 0 and elf.read(word_va, 8) == struct.pack('<Q', c['LX_EVENT_CALL_WORD_ORIGINAL']))
    ok('event_slots', elf.read(0x16c6b50, 4) == bytes.fromhex('498b7d18') and elf.read(0x16c6b59, 6) == bytes.fromhex('f2410f104530') and elf.read(0x16c6b5f, 4) == bytes.fromhex('498b7528') and
       elf.read(0x16c6b35, 8) == bytes.fromhex('488d04dd00000000') and elf.read(0x16c6b3d, 3) == bytes.fromhex('4829d8') and elf.read(0x16c6b40, 4) == bytes.fromhex('4c8d2cc6') and
       inside('event_delivery_loop', 0x16c6b35, 0x30))
    ok('event_batch', rip_target(elf, 0x16c6b1b, 7, 3, b'\x48\x8b\x2d') == c['LX_EVENT_COUNT_VA'] and rip_target(elf, 0x16c6b2e, 7, 3, b'\x48\x8d\x35') == c['LX_EVENT_BATCH_VA'] and
       inside('event_delivery_loop', 0x16c6b1b, 7) and by_name['event_deliver_head']['va'] == c['LX_EVENT_DELIVER_VA'])
    ok('event_source_scan_clears_source', rip_target(elf, 0x16bb2ec, 7, 3, b'\x48\x8b\x15') == c['LX_EVENT_COUNT_VA'] and rip_target(elf, 0x16bb300, 7, 3, b'\x48\x8d\x05') == c['LX_EVENT_BATCH_VA'] and
       elf.read(0x16bb319, 4) == bytes.fromhex('483b5818') and inside('event_source_scan', 0x16bb319, 4))
    # MD cue delays (gate_md_resume.h): the call with its two cue fields, and the rel32 in one window.
    site = c['LX_MD_RESUME_SITE_VA']
    ok('md_resume_call', elf.read(site - 9, 9) == bytes.fromhex('8bb500020000') + bytes.fromhex('4889ef') and elf.read(site, 1) == b'\xe8' and
       site + 5 + struct.unpack('<i', elf.read(site + 1, 4))[0] == c['LX_MD_RESUME_DELAY_VA'] and elf.read(site + 13, 8) == bytes.fromhex('f20f5885f8010000') and
       inside('md_resume_caller', site, 5) and by_name['md_resume_caller']['va'] == c['LX_MD_RESUME_CALLER_VA'] and by_name['md_resume_delay']['va'] == c['LX_MD_RESUME_DELAY_VA'])
    ok('md_resume_window', (site & ~15) + 16 >= site + 5 and (site & 15) <= 11)
    # The cue name layout: definition, module, tree, node name and the module's name, by bytes.
    ok('cue_name_fields', elf.read(0xf68af4, 14) == bytes.fromhex('488bb6600100004c8b8650010000') and elf.read(0xf68b0b, 7) == bytes.fromhex('498b9080000000') and
       elf.read(0xf68b12, 4) == bytes.fromhex('498d7860') and elf.read(0xf68b20, 4) == bytes.fromhex('483b7240') and elf.read(0xf68cd8, 4) == bytes.fromhex('4c8b6a20') and
       elf.read(0xf48cc2, 4) == bytes.fromhex('488d7518') and inside('cue_name_walk', 0xf68af4, 14) and inside('cue_name_walk', 0xf68b20, 4) and
       inside('cue_name_walk_up', 0xf68cd8, 4) and inside('cue_module_name', 0xf48cc2, 4))
    typeinfo = struct.unpack('<Q', elf.read(c['LX_CUE_VTABLE'] - 8, 8))[0]
    name = struct.unpack('<Q', elf.read(typeinfo + 8, 8))[0]
    ok('cue_vtable_typeinfo', elf.read(name, 10) == b'N2MD3CueE\0' and struct.unpack('<q', elf.read(c['LX_CUE_VTABLE'] - 16, 8))[0] == 0)
    # The boot hold (gate_boot_hold.h): the only caller of Move, the director it loads, the prologue.
    move, call = c['LX_BOOT_HOLD_MOVE_VA'], c['LX_BOOT_HOLD_CALL_VA']
    ok('boot_hold_call', elf.read(call, 1) == b'\xe8' and call + 5 + struct.unpack('<i', elf.read(call + 1, 4))[0] == move and
       rip_target(elf, call - 7, 7, 3, b'\x48\x8b\x3d') == c['LX_BOOT_HOLD_DIRECTOR_VA'] and inside('boot_hold_call', call - 7, 12))
    text = next(s for s in elf.segments if s['type'] == 1 and s['flags'] & 1)
    code = elf.read(text['vaddr'], text['filesz'])
    callers = [text['vaddr'] + m.start() for m in re.finditer(rb'[\xe8\xe9]', code)
               if m.start() + 5 <= len(code) and text['vaddr'] + m.start() + 5 + struct.unpack_from('<i', code, m.start() + 1)[0] == move]
    ok('boot_hold_only_caller', callers == [call], str([hex(a) for a in callers]))
    ok('boot_hold_prologue', elf.read(move, 8) == struct.pack('<Q', c['LX_BOOT_HOLD_ENTRY_ORIGINAL']) and elf.read(move, c['LX_BOOT_HOLD_PROLOGUE']) == bytes.fromhex('554889e54157') and
       move % 8 == 0 and by_name['boot_hold_move_head']['va'] == move and inside('boot_hold_move_head', move + 0x30, 8))
    # AI Wait restores (gate_ai_wait.h): the slot, its only holder, the importer's call.
    restore = c['LX_AI_WAIT_RESTORE_VA']
    ok('ai_wait_slot', c['LX_AI_WAIT_SLOT_VA'] == 0x3b2a650 + 0x30 and struct.unpack('<Q', elf.read(c['LX_AI_WAIT_SLOT_VA'], 8))[0] == restore and
       by_name['ai_wait_restore']['va'] == restore)
    needle = struct.pack('<Q', restore)
    holders = [at for at in range(0, len(data) - 8, 8) if data[at:at + 8] == needle]
    ok('ai_wait_slot_only_holder', len(holders) == 1, str(holders))
    ok('ai_wait_import_call', elf.read(c['LX_AI_WAIT_RETURN_VA'] - 3, 3) == b'\xff\x50\x30' and elf.read(c['LX_AI_WAIT_RETURN_VA'] - 13, 4) == b'\x49\x8d\x56\x38' and
       inside('ai_wait_import_call', c['LX_AI_WAIT_RETURN_VA'] - 3, 3))
    # The script RNG (gate_script_rng.h): the entries, the slots and their only holders, the three calls, the layouts.
    run_va, range_va = c['LX_EXEC_RUN_VA'], c['LX_EXEC_RANGE_VA']
    ok('script_exec_entries', elf.read(run_va, 8) == struct.pack('<Q', c['LX_EXEC_RUN_ENTRY_ORIGINAL']) and elf.read(run_va, c['LX_EXEC_RUN_PROLOGUE']) == bytes.fromhex('55534889fb') and run_va % 8 == 0 and
       elf.read(range_va, 8) == struct.pack('<Q', c['LX_EXEC_RANGE_ENTRY_ORIGINAL']) and elf.read(range_va, c['LX_EXEC_RANGE_PROLOGUE']) == bytes.fromhex('415741564155') and range_va % 8 == 0 and
       by_name['script_exec_run']['va'] == run_va and by_name['script_exec_range']['va'] == range_va)
    # Exec layout: the running Exec's global, program +0x10, PC +0x18, the action call; the range's arguments.
    ok('script_exec_layout', elf.read(run_va + 0x10, 4) == bytes.fromhex('c6473000') and elf.read(run_va + 0x4a, 4) == bytes.fromhex('488b4310') and elf.read(run_va + 0x4e, 4) == bytes.fromhex('488b5318') and
       elf.read(range_va + 8, 3) == bytes.fromhex('4989fc') and elf.read(range_va + 0xc, 3) == bytes.fromhex('4889cd') and elf.read(range_va + 0x10, 3) == bytes.fromhex('4889d3'))
    for label, slot, vtable, handler, pin in (('ai', c['LX_AI_EVENT_SLOT_VA'], c['LX_AI_CONTEXT_VTABLE'], c['LX_AI_EVENT_VA'], 'script_ai_event'),
                                              ('stack', c['LX_STACK_EVENT_SLOT_VA'], c['LX_STACK_VTABLE'], c['LX_STACK_EVENT_VA'], 'script_stack_event'),
                                              ('cue', c['LX_CUE_EVENT_SLOT_VA'], c['LX_CUE_VTABLE'], c['LX_CUE_EVENT_VA'], 'script_cue_event')):
        needle = struct.pack('<Q', handler)
        holders = [at for at in range(0, len(data) - 8, 8) if data[at:at + 8] == needle]
        ok(f'script_{label}_slot', slot == vtable + 566 * 8 and elf.read(slot, 8) == needle and by_name[pin]['va'] == handler)
        ok(f'script_{label}_only_holder', len(holders) == 1, str(holders))
    check = c['LX_CUE_CHECK_VA']
    ok('script_cue_tail', elf.read(c['LX_CUE_EVENT_VA'], 1) == b'\xe9' and c['LX_CUE_EVENT_VA'] + 5 + struct.unpack('<i', elf.read(c['LX_CUE_EVENT_VA'] + 1, 4))[0] == check)
    text = elf.read(c['LX_TEXT_VA'], c['LX_TEXT_END_VA'] - c['LX_TEXT_VA'])
    wanted = check - c['LX_TEXT_VA'] - 5
    sites_seen = [c['LX_TEXT_VA'] + m.start() for m in re.finditer(b'\xe8', text)
                  if m.start() + 5 <= len(text) and m.start() + struct.unpack_from('<i', text, m.start() + 1)[0] == wanted]
    ok('script_cue_calls', sites_seen == sorted([c['LX_CUE_TIMED_SITE_VA'], c['LX_CUE_WAIT_SITE_VA'], c['LX_CUE_RAISE_SITE_VA']]), str([hex(a) for a in sites_seen]))
    # Cue_EnterWaiting's call crosses a 16-byte boundary, so the jump goes over the mov before it; the others fit.
    wait = c['LX_CUE_WAIT_SITE_VA']
    ok('script_cue_wait_site', wait & 15 == 13 and elf.read(wait - 3, 3) == bytes.fromhex('4c89f7'), hex(wait))
    ok('script_cue_blocks', c['LX_CUE_TIMED_SITE_VA'] & 15 <= 11 and c['LX_CUE_RAISE_SITE_VA'] & 15 <= 11 and
       inside('script_cue_wait_site', wait - 3, 8) and inside('script_cue_timed_site', c['LX_CUE_TIMED_SITE_VA'], 5) and inside('script_cue_raise_site', c['LX_CUE_RAISE_SITE_VA'], 5))
    # Layouts: the cue's Value at +0xa0, the stack context's owner at +0xc8, the script descriptor at +0xd0, the event record at +0x18.
    ok('script_layouts', elf.read(0xf5b3e8, 7) == bytes.fromhex('4881c3a0000000') and elf.read(0xf5b407, 8) == bytes.fromhex('c74424105d000000') and inside('script_cue_value', 0xf5b407, 8) and
       elf.read(c['LX_STACK_EVENT_VA'] + 0xe, 7) == bytes.fromhex('488b87c8000000') and elf.read(c['LX_AI_EVENT_VA'] + 0x39, 7) == bytes.fromhex('488b83d0000000') and
       elf.read(c['LX_AI_EVENT_VA'] + 0x47, 4) == bytes.fromhex('4c8d7b18'))
    # The navigation deadlines (gate_nav.h): the slots, the fields and both scheduling blocks around their branches.
    nav_move = c['LX_NAV_MOVE_VA']
    needle = struct.pack('<Q', nav_move)
    holders = [at for at in range(0, len(data) - 8, 8) if data[at:at + 8] == needle]
    ok('nav_slots', len(holders) == 2 and elf.read(c['LX_NAV_MOVE_SLOT_A_VA'], 8) == needle and elf.read(c['LX_NAV_MOVE_SLOT_B_VA'], 8) == needle and
       c['LX_NAV_MOVE_SLOT_A_VA'] == 0x3b697e8 + 40 * 8 and c['LX_NAV_MOVE_SLOT_B_VA'] == 0x3b94bb0 + 40 * 8, str(holders))
    ok('nav_fields', elf.read(nav_move + 0x24, 7) == bytes.fromhex('80bbd800000000') and elf.read(nav_move + 0x39, 8) == bytes.fromhex('f20f7fbbd0000000'.replace('f20f7f', '660f2f')) and
       by_name['nav_move_head']['va'] == nav_move and elf.read(0x144aaa4, 8) == bytes.fromhex('f20f118bd0000000') and elf.read(0x144ad66, 8) == bytes.fromhex('f20f1188e0030000') and
       inside('nav_params_block', 0x144aaa4, 8) and inside('nav_probe_block', 0x144ad66, 8) and elf.read(0x144ad55, 4) == bytes.fromhex('488b4340'))
    ok('nav_branches', elf.read(c['LX_NAV_PARAMS_NEAR_VA'], 2) == bytes.fromhex('772e') and c['LX_NAV_PARAMS_NEAR_VA'] + 2 + 0x2e == 0x144aa9c and
       elf.read(c['LX_NAV_PARAMS_FAR_VA'], 6) == bytes.fromhex('0f8671080000') and c['LX_NAV_PARAMS_FAR_VA'] + 6 + 0x871 == 0x144b2f0 and
       elf.read(c['LX_NAV_PROBE_NEAR_VA'], 2) == bytes.fromhex('772e') and c['LX_NAV_PROBE_NEAR_VA'] + 2 + 0x2e == 0x144ad4d and
       elf.read(c['LX_NAV_PROBE_FAR_VA'], 6) == bytes.fromhex('0f8648080000') and c['LX_NAV_PROBE_FAR_VA'] + 6 + 0x848 == 0x144b578 and
       all(va & 15 <= 16 - size for va, size in ((c['LX_NAV_PARAMS_NEAR_VA'], 2), (c['LX_NAV_PARAMS_FAR_VA'], 6), (c['LX_NAV_PROBE_NEAR_VA'], 2), (c['LX_NAV_PROBE_FAR_VA'], 6))))
    # Created identities (gate_world_created.h): the constructor's return, and the class stub the gate decodes.
    stub = struct.unpack('<Q', elf.read(0x3d23cd0 + 0x11e8, 8))[0]
    stub_code = elf.read(stub, 32)
    bitmap = stub + 10 + struct.unpack_from('<i', stub_code, 6)[0]
    ship_words = struct.unpack('<8I', elf.read(bitmap, 32))
    ok('created_class_stub', stub_code[:6] == bytes.fromhex('4863c6488d15') and stub_code[10:] == bytes.fromhex('83e60f48c1e804488d0c368b0482d3e8a8030f95c0c3') and (ship_words[22 // 16] >> (2 * (22 % 16))) & 3 != 0 and
       all((ship_words[k // 16] >> (2 * (k % 16))) & 3 == 0 for k in (11, 32, 42, 46, 58, 61, 97, 105)))
    # Ship loadouts (gate_spawn_rng.h): Generate's entry, the state as its first stack argument at a job spawn, its core.
    generate = c['LX_GENERATE_VA']
    ok('spawn_generate_entry', elf.read(generate, 8) == struct.pack('<Q', c['LX_GENERATE_ENTRY_ORIGINAL']) and elf.read(generate, c['LX_GENERATE_PROLOGUE']) == bytes.fromhex('554889e54157') and generate % 8 == 0 and
       by_name['spawn_generate']['va'] == generate)
    ok('spawn_generate_state', elf.read(0x1593f61, 7) == bytes.fromhex('41ffb598050000') and rip_target(elf, 0x1593f70, 5, 1, b'\xe8') == generate and rip_target(elf, 0x1594140, 5, 1, b'\xe8') == generate and
       rip_target(elf, 0x162a5ec, 5, 1, b'\xe8') == generate and rip_target(elf, 0xdc7ebd, 5, 1, b'\xe8') == generate)
    ok('spawn_generate_core', rip_target(elf, generate + 0x112, 5, 1, b'\xe8') == 0xd28fc0 and inside('spawn_generate', generate + 0x112, 5) and rip_target(elf, 0xd290a0, 5, 1, b'\xe8') == 0xd029a0)
    # The Gravidar's zone groups (gate_gravidar_wide.h): the group's fields and its neighbour indices in the scan.
    ok('gravidar_group_fields', elf.read(0x10ff743, 5) == bytes.fromhex('41c6435000') and elf.read(0x10ff757, 5) == bytes.fromhex('41 0f 11 43 38'.replace(' ', '')) and elf.read(0x10ff75c, 5) == bytes.fromhex('4983421058') and
       inside('gravidar_group_init', 0x10ff743, 5) and inside('gravidar_group_init', 0x10ff75c, 5))
    ok('gravidar_group_neighbours', elf.read(0x1caf016, 3) == bytes.fromhex('498b17') and elf.read(0x1caf029, 8) == bytes.fromhex('488d0492488d0442') and elf.read(0x1caf031, 4) == bytes.fromhex('488d34c1') and
       elf.read(0x1caf035, 4) == bytes.fromhex('807e5000') and inside('gravidar_scan_groups', 0x1caf016, 3) and inside('gravidar_scan_groups', 0x1caf035, 4))
    # find_* candidate lists (gate_find_order.h): the action's Execute, its one call of the store, the arguments.
    find_site = c['LX_FIND_ORDER_SITE_VA']
    ok('find_order_execute', elf.read(c['LX_FIND_ORDER_VTABLE'] + 4 * 8, 8) == struct.pack('<Q', c['LX_FIND_ORDER_EXECUTE_VA']) and by_name['find_order_execute']['va'] == c['LX_FIND_ORDER_EXECUTE_VA'])
    ok('find_order_site', rip_target(elf, find_site, 5, 1, b'\xe8') == c['LX_FIND_ORDER_ASSIGN_VA'] and find_site & 7 == 3 and inside('find_order_execute', find_site, 5) and
       elf.read(find_site - 3, 3) == bytes.fromhex('4c89ee') and elf.read(find_site - 6, 3) == bytes.fromhex('4c89e2') and elf.read(find_site - 13, 7) == bytes.fromhex('488dbbf8000000'))
    # The Linear step trace (gate_step_trace.h): the vtable entry holds the step, and nothing else in the file does.
    ok('step_trace_entry', elf.read(c['LX_ST_SLOT_VA'], 8) == struct.pack('<Q', c['LX_ST_STEP_VA']) and c['LX_ST_SLOT_VA'] & 7 == 0)
    # FR8 (gate_step_trace.h): the step's three calls (asinf of the target and the current pitch, the rotation builder),
    # each rel32 inside one 16-byte line, inside the step.
    for site, target in ((c['LX_SC_ASIN_TARGET_SITE_VA'], c['LX_SC_ASINF_VA']), (c['LX_SC_ASIN_CUR_SITE_VA'], c['LX_SC_ASINF_VA']),
                         (c['LX_SC_BUILDER_SITE_VA'], c['LX_SC_BUILDER_VA'])):
        ok('step_chain_site_%x' % site, rip_target(elf, site, 5, 1, b'\xe8') == target and (site + 1) >> 4 == (site + 4) >> 4 and
           c['LX_ST_STEP_VA'] < site < c['LX_ST_STEP_VA'] + 0x2000)
    # Objects' attention timer (gate_attention.h): the prologue moved to the trampoline, the due read and written at
    # +0x2d8, the thread's time and word, the tail to SetAttention.
    attention = c['LX_ATTENTION_VA']
    ok('attention_timer', elf.read(attention, 8) == struct.pack('<Q', c['LX_ATTENTION_ENTRY_ORIGINAL']) and attention & 7 == 0 and by_name['attention_timer']['va'] == attention and
       elf.read(attention + 0x29, 8) == bytes.fromhex('f20f1083d8020000') and elf.read(attention + 0xd0, 8) == bytes.fromhex('f20f1183d8020000') and
       elf.read(attention + 0x47, 9) == bytes.fromhex('64f20f100c25d8d8ff') and elf.read(attention + 0x8a, 9) == bytes.fromhex('64480faf0425e0d8ff') and
       c['LX_ATTENTION_DUE'] == 0x2d8 and c['LX_ATTENTION_PROLOGUE'] == 5)
    # Stations' loadout targets (gate_loadout_order.h): each call reaches its target, its rel32 lies in one aligned
    # 8-byte word; each rest loop starts with the 5 bytes the stub replaces, inside one aligned 16-byte block, and is
    # entered there or just before it (the two jumps found); the stubs' stack slots are read by the native loops.
    for name in ('wares', 'ammo', 'deployable', 'unit', 'existing', 'pick'):
        site, target = c[f'LX_LL_{name.upper()}_SITE_VA'], c[f'LX_LL_{name.upper()}_VA']
        ok(f'll_{name}_call', rip_target(elf, site, 5, 1, b'\xe8') == target and ((site + 1) & 7) <= 4 and by_name[f'll_{name}_call']['va'] == site)
    ok('ll_wanted_calls', all(c['LX_LL_WANTED_VA'] < c[f'LX_LL_{n}_SITE_VA'] < c['LX_LL_WANTED_VA'] + 0x1310 for n in ('WARES', 'AMMO', 'DEPLOYABLE', 'UNIT')) and
       c['LX_LL_AMMO_VA'] < c['LX_LL_EXISTING_SITE_VA'] < c['LX_LL_AMMO_VA'] + 0x560 and c['LX_LL_EXISTING_VA'] < c['LX_LL_PICK_SITE_VA'] < c['LX_LL_EXISTING_VA'] + 0xb70)
    unit, deployable = c['LX_LL_REST_UNIT_VA'], c['LX_LL_REST_DEPLOYABLE_VA']
    ok('ll_rest_unit', elf.read(unit, 5) == bytes.fromhex('4c8b4c2418') and (unit & 15) + 5 <= 16 and inside('ll_rest_unit', unit, 5) and
       elf.read(unit - 3, 3) == bytes.fromhex('4189ef') and rip_target(elf, 0x1e9e617, 6, 2, b'\x0f\x84') == unit - 3 and rip_target(elf, 0x1e9e898, 6, 2, b'\x0f\x85') == unit and
       elf.read(0x1e9e7b5, 11) == bytes.fromhex('4489f831d24589faf7f3') + b'\x49' and elf.read(0x1e9e87e, 5) == bytes.fromhex('488b7c2460') and
       elf.read(c['LX_LL_REST_UNIT_EXIT_VA'], 5) == bytes.fromhex('4885ff740d') and inside('ll_rest_unit_exit', c['LX_LL_REST_UNIT_EXIT_VA'], 5))
    ok('ll_rest_deployable', elf.read(deployable, 5) == bytes.fromhex('4c8b5c2440') and (deployable & 15) + 5 <= 16 and inside('ll_rest_deployable', deployable, 5) and
       elf.read(deployable - 12, 12) == bytes.fromhex('8b44244c85c00f849d000000') and rip_target(elf, 0x1df832c, 5, 1, b'\xe9') == deployable - 12 and
       elf.read(0x1df81a5, 5) == bytes.fromhex('448b54244c') and elf.read(c['LX_LL_REST_DEPLOYABLE_EXIT_VA'], 3) == bytes.fromhex('4d85f6') and
       inside('ll_rest_deployable', c['LX_LL_REST_DEPLOYABLE_EXIT_VA'], 3))
    # The exact-reciprocal preload (src/native/portable_math/preload) rewrites these windows:
    # no pin may cover one and no gate patch may touch one.
    windows = [(int(a, 16), int(n)) for a, n in re.findall(r'\{0x([0-9a-f]+)u,(\d+),', XRCP_H.read_text(encoding='utf-8'))]
    ok('exact_reciprocal_windows_read', len(windows) == 765)
    ok('no_pin_in_exact_reciprocal_window', not any(a < s['va'] + s['length'] and s['va'] < a + n for s in sites for a, n in windows))
    patches = [(c['LX_CALL_WINDOW'], 16), (c['LX_DT_LOAD_VA'], 16), (entry, 8), (clear, 8), (release & ~7, 8),
               (c['LX_MD_RESUME_SITE_VA'] & ~15, 16), (c['LX_BOOT_HOLD_MOVE_VA'], 8), (c['LX_EVENT_CALL_WORD_VA'], 8), (c['LX_GRAVIDAR_CALL_WORD_VA'], 8),
               (c['LX_TRAVEL_READ_WORD_VA'], 8), (c['LX_PATH_JOB_LEA_A_VA'], 16), (c['LX_PATH_JOB_LEA_B_VA'], 16),
               (c['LX_ID_REMOVE_VA'], 8), (c['LX_ID_ASSIGN_VA'], 8), (c['LX_EXEC_RUN_VA'], 8), (c['LX_EXEC_RANGE_VA'], 8),
               (c['LX_CUE_TIMED_SITE_VA'] & ~15, 16), (c['LX_CUE_WAIT_SITE_VA'] & ~15, 16), (c['LX_CUE_RAISE_SITE_VA'] & ~15, 16), (c['LX_FIND_ORDER_SITE_VA'] & ~7, 8), (c['LX_GENERATE_VA'], 8), (c['LX_NAV_PARAMS_NEAR_VA'] & ~15, 16), (c['LX_NAV_PARAMS_FAR_VA'] & ~15, 16), (c['LX_NAV_PROBE_NEAR_VA'] & ~15, 16), (c['LX_NAV_PROBE_FAR_VA'] & ~15, 16)]
    patches += [(c['LX_ATTENTION_VA'], 8)]
    patches += [((c[f'LX_LL_{n}_SITE_VA'] + 1) & ~7, 8) for n in ('WARES', 'AMMO', 'DEPLOYABLE', 'UNIT', 'EXISTING', 'PICK')] + \
               [(c['LX_LL_REST_UNIT_VA'] & ~15, 16), (c['LX_LL_REST_DEPLOYABLE_VA'] & ~15, 16)]
    ok('no_patch_in_exact_reciprocal_window', not any(a < v + n2 and v < a + n for v, n2 in patches for a, n in windows))
    # The same preload's decimal patch jumps from these two readers to portable_decimal.c.
    readers = [(0x1a3b1a0, 0x39f), (0x1a3ac40, 0x3bf)]
    ok('no_pin_in_decimal_reader', not any(a < s['va'] + s['length'] and s['va'] < a + n for s in sites for a, n in readers))
    ok('no_patch_in_decimal_reader', not any(a < v + n2 and v < a + n for v, n2 in patches for a, n in readers))
    return findings


def gate_constants(text=None):
    text = text if text is not None else GATE_C.read_text(encoding='utf-8')
    return {name: int(value, 16) for name, value in re.findall(r'#define (\w+_RVA) (0x[0-9a-f]+)u', text)}


# The Windows gate constants the Linux gate core replaces (lockstep_gate.c -> gate_sites.h).
PAIRS = [('CALL_RVA', 'LX_CALL_VA', 'site'), ('TICK_RVA', 'LX_TICK_VA', 'function'), ('GAME_RVA', 'LX_GAME_VA', 'data'),
         ('REAL_RVA', 'LX_REAL_VA', 'data'), ('SPEED_RVA', 'LX_SPEED_VA', 'data'), ('TABLE_RVA', 'LX_VBI_TABLE_VA', 'data')]
EXTRA_PAIRS = [(0x6adfc58, 'LX_MOVIE_FLAG_VA', 'data'), (0x6adfc60, 'LX_MOVIE_DT_VA', 'data'), (0xfa3302, 0x2025135, 'site'),
               (0x31c20cc, 'LX_IN_FLIGHT_VA', 'data'),
               # World identities and world hash (lockstep_world_identity.h/lockstep_world_hash.h -> gate_world.h).
               (0x4795a0, 'LX_IMPORT_INSERT_VA', 'function'), (0x31c2110, 'LX_IMPORT_MAP_VA', 'data'),
               (0x942540, 'LX_IMPORT_CLEAR_VA', 'function'),
               (0xced70, 'LX_FIND_VA', 'function'), (0x6cf1500, 'LX_COMPONENT_MAP_VA', 'data'),
               (0x38f4ee4, 'LX_BANK_READ_VA', 'data'), (0x2f4b6f4, 'LX_BANK_WRITE_VA', 'data'),
               (0x2b19700, 'LX_SHIELD_SOURCE_VTABLE', 'data'),
               # Path determinism (lockstep_path_determinism.h -> gate_path.h).
               (0x6cf21c8, 'LX_PATH_MANAGER_VA', 'data'), (0xea2a20, 'LX_PATH_REFRESH_VA', 'function'), (0x14b8e20, 'LX_PATH_SYNC_VA', 'function'),
               # Hazard region updates (lockstep_region_rng.h -> gate_region.h).
               (0x767410, 'LX_REGION_MOVE_VA', 'function'), (0xee16a0, 'LX_DAMAGE_FIELD_UPDATE_VA', 'function'),
               (0x2c4b2f0, 'LX_DAMAGE_FIELD_VTABLE', 'data'),
               # AI Wait restores (lockstep_ai_wait_rng.h -> gate_ai_wait.h).
               (0x10a50d0, 'LX_AI_WAIT_RESTORE_VA', 'function'), (0x1069bfc, 'LX_AI_WAIT_RETURN_VA', 'site'), (0x2c6a270, 'LX_AI_CONTEXT_VTABLE', 'data'),
               # MD cue delays and the boot hold.
               (0x1115960, 'LX_MD_RESUME_CALLER_VA', 'function'), (0x1115bad, 'LX_MD_RESUME_SITE_VA', 'site'), (0x111f8c0, 'LX_MD_RESUME_DELAY_VA', 'function'),
               (0x2c75170, 'LX_CUE_VTABLE', 'data'), (0x9fe093, 'LX_BOOT_HOLD_CALL_VA', 'site'), (0x106d250, 'LX_BOOT_HOLD_MOVE_VA', 'function'),
               (0x6cf15f0, 'LX_BOOT_HOLD_DIRECTOR_VA', 'data'),
               # Event order.
               (0x99c470, 'LX_EVENT_DELIVER_VA', 'function'), (0x39943e0, 'LX_EVENT_BATCH_VA', 'data'), (0x38f4bc8, 'LX_EVENT_COUNT_VA', 'data'),
               (0x99c3d1, 'LX_EVENT_CALL_VA', 'site'), (0x2b6cc38, 'LX_UI_ELEMENT_VTABLE', 'data'),
               # The Gravidar.
               (0x6cf1650, 'LX_GRAVIDAR_WORKER_VA', 'data'), (0x2c68860, 'LX_GRAVIDAR_VTABLE', 'data'), (0x10b12c0, 'LX_GRAVIDAR_PUBLISH_VA', 'function'),
               (0xef45d9, 'LX_GRAVIDAR_CALL_VA', 'site'), (0x10acd70, 'LX_GRAVIDAR_UNIT_APPLY_VA', 'function'), (0x2c67610, 'LX_GRAVIDAR_UNIT_VTABLE', 'data'),
               (0x10ae0e0, 'LX_GRAVIDAR_RUN_VA', 'function'), (0x6b60e10, 'LX_GAME_TIME_LOCK_VA', 'data'), (0x6b60e38, 'LX_GAME_TIME_GLOBAL_VA', 'data'),
               (0x2ba5430, 'LX_MACRO_VTABLE', 'data'), (0xefb10, 'LX_MACRO_NAME_GETTER_VA', 'function'),
               # Gravidar deadlines.
               (0x2b1ea48, 'LX_SECTOR_VTABLE', 'data'), (0x784230, 'LX_SECTOR_MOVE_VA', 'function'),
               # Travel snapshot.
               (0xe918d5, 'LX_TRAVEL_READ_VA', 'site'), (0xf29cd0, 'LX_PATH_JOB_VA', 'function'),
               # Runtime IDs.
               (0x6cf1500, 'LX_ID_MAP_VA', 'data'), (0xcbe70, 'LX_ID_REMOVE_VA', 'function'), (0xcc220, 'LX_ID_ASSIGN_VA', 'function'),
               # The script RNG.
               (0xb58bb0, 'LX_EXEC_RUN_VA', 'function'), (0xb58ce0, 'LX_EXEC_RANGE_VA', 'function'), (0x1066750, 'LX_AI_EVENT_VA', 'function'),
               (0x1062f70, 'LX_STACK_EVENT_VA', 'function'), (0x111f990, 'LX_CUE_EVENT_VA', 'function'), (0x111c290, 'LX_CUE_CHECK_VA', 'function'),
               (0x2c690b8, 'LX_STACK_VTABLE', 'data'), (0x112208b, 'LX_CUE_TIMED_SITE_VA', 'site'), (0x111d296, 'LX_CUE_WAIT_SITE_VA', 'site'), (0x111e505, 'LX_CUE_RAISE_SITE_VA', 'site'),
               # find_* candidate lists.
               (0x2c0f3c8, 'LX_FIND_ORDER_VTABLE', 'data'), (0xb87a30, 'LX_FIND_ORDER_EXECUTE_VA', 'function'), (0xb87ae3, 'LX_FIND_ORDER_SITE_VA', 'site'), (0xd2ab50, 'LX_FIND_ORDER_ASSIGN_VA', 'function'),
               # Ship loadouts.
               (0xc841b0, 'LX_GENERATE_VA', 'function'),
               # The navigation deadlines.
               (0xea77f0, 'LX_NAV_MOVE_VA', 'function'),
               # The Linear step trace.
               (0x2c4f680, 'LX_ST_SLOT_VA', 'data'), (0xeb27c0, 'LX_ST_STEP_VA', 'function'),
               # The attention timer.
               (0x6d1ff0, 'LX_ATTENTION_VA', 'function'),
               # Stations' loadout targets.
               (0x429ad0, 'LX_LL_WANTED_VA', 'function'), (0x316f60, 'LX_LL_WARES_VA', 'function'), (0x429c64, 'LX_LL_WARES_SITE_VA', 'site'),
               (0x505730, 'LX_LL_AMMO_VA', 'function'), (0x42a40e, 'LX_LL_AMMO_SITE_VA', 'site'), (0x5046b0, 'LX_LL_DEPLOYABLE_VA', 'function'),
               (0x42a6ee, 'LX_LL_DEPLOYABLE_SITE_VA', 'site'), (0x503c40, 'LX_LL_UNIT_VA', 'function'), (0x42a9a8, 'LX_LL_UNIT_SITE_VA', 'site'),
               (0x504970, 'LX_LL_EXISTING_VA', 'function'), (0x505ef8, 'LX_LL_EXISTING_SITE_VA', 'site'), (0x5012f0, 'LX_LL_PICK_VA', 'function'),
               (0x504e20, 'LX_LL_PICK_SITE_VA', 'site'), (0x503349, 'LX_LL_REST_UNIT_VA', 'site'), (0x5043ce, 'LX_LL_REST_DEPLOYABLE_VA', 'site')]


def check_pairs(constants):
    known = json.loads(KNOWN.read_text(encoding='utf-8'))['pairs']
    table = {(int(p['win'], 16), int(p['lin'], 16), p['kind']) for p in known}
    win = gate_constants()
    rows = [(win[w], constants[l], k) for w, l, k in PAIRS] + \
           [(w, constants[l] if isinstance(l, str) else l, k) for w, l, k in EXTRA_PAIRS]
    missing = [f'{w:#x}->{l:#x} {k}' for w, l, k in rows if (w, l, k) not in table]
    if missing:
        raise AssertionError(f'pairs missing from {KNOWN.name}: {missing}')
    return [f'{w:#x}->{l:#x}' for w, l, _ in rows]


def check_windows(path):
    pe = Pe(Path(path).read_bytes())
    win = gate_constants()
    findings = []
    target = rip_target(pe, win['CALL_RVA'], 5, 1, b'\xe8')
    assert target == win['TICK_RVA'], 'Windows CALL_RVA does not call TICK_RVA'
    findings.append('windows_call_to_frame_tick')
    assert rip_target(pe, 0xfa32fb, 7, 3, b'\x44\x38\x25') == 0x6adfc58, 'Windows movie flag'
    assert pe.read(0xfa3302, 2) == b'\x74\x08', 'Windows movie branch'
    assert rip_target(pe, 0xfa3304, 8, 4, b'\xf2\x0f\x10\x35') == 0x6adfc60, 'Windows movie frame time'
    findings.append('windows_movie_capture_path')
    assert rip_target(pe, win['CALL_RVA'] + 5, 7, 2, b'\xc6\x05') == 0x31c20cc and pe.read(win['CALL_RVA'] + 11, 1) == b'\x01'
    findings.append('windows_in_flight_after_call')
    return findings


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--linux', type=Path, required=True)
    parser.add_argument('--windows', type=Path)
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    constants, sites = parse_sites()
    result = {'linux': check_linux(args.linux, constants, sites, parse_tls()), 'pairs': check_pairs(constants)}
    if args.windows:
        result['windows'] = check_windows(args.windows)
    text = json.dumps(result, indent=2)
    if args.json:
        args.json.write_text(text + '\n')
    print(text)
    print('LINUX GATE SITES PASS', sum(len(v) for v in result.values()), 'checks')


if __name__ == '__main__':
    main()
