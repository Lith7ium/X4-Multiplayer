"""Lockstep player orders: codec, refusals, deterministic application and a 3-player mock gate.

LuaJIT runs the real Lua core/glue/probe; the Python reference codec is cross-checked against it.
Synthetic clients only: proves encoding, scheduling and decision equality, not native X4 behaviour.
"""
import contextlib
import gzip
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock
import xml.etree.ElementTree as ET

from lupa.luajit21 import LuaRuntime, LuaError

ROOT = Path(__file__).resolve().parents[1]
DEV = ROOT / 'tools/dev'
CORE = (DEV / 'native_gate_player_orders_core.lua').read_text(encoding='utf-8')
GLUE = (DEV / 'native_gate_player_orders.lua').read_text(encoding='utf-8')
MUX = (DEV / 'native_gate_inputs_core.lua').read_text(encoding='utf-8')
PROBE = (DEV / 'native_gate_probe.lua').read_text(encoding='utf-8')
MD = DEV / 'native_gate_player_orders.xml'
sys.path.insert(0, str(DEV))
spec = importlib.util.spec_from_file_location('player_orders_reference', DEV / 'native_gate_player_orders.py')
orders = importlib.util.module_from_spec(spec)
spec.loader.exec_module(orders)
spec = importlib.util.spec_from_file_location('player_orders_builder', DEV / 'build_native_gate_overlay.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
try:
    from lxml import etree
except ImportError:
    etree = None
RE = Path(os.environ.get('X4_RE', ROOT.parent / 'x4-re'))
MD_XSD = RE / 'gamescripts/resolved/base/libraries/md.xsd'

S1, S2, S3 = '000000000000a001', '000000000000a002', '000000000000a003'
S2B = '000000000000a012'
NPC, STATION, SECTOR = '000000000000b001', '0000000000000d01', '0000000000000c01'
OWNERS = {S1: 1, S2: 2, S3: 3, S2B: 2}
L = orders.lua_literal


def core():
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(CORE)
    return lua, lua.globals().X4LAN_PLAYER_ORDERS_CORE


def lua_value(lua, value):
    return lua.eval(L(value))


SAMPLES = [
    {'op': 'move', 'flags': 1, 'subject': S2, 'sector': SECTOR, 'x': -1600.25, 'y': 100200.0, 'z': 0.1},
    {'op': 'attack', 'flags': 2, 'subject': S2, 'target': NPC},
    {'op': 'follow', 'subject': S2, 'target': S1},
    {'op': 'protect', 'flags': 3, 'subject': S2, 'target': S1},
    {'op': 'dock', 'subject': S2, 'target': STATION},
    {'op': 'hold', 'flags': 7, 'subject': S2},
    {'op': 'buy', 'subject': S2, 'target': STATION, 'ware': 'energycells', 'amount': 100, 'price': 1550},
    {'op': 'sell', 'subject': S2, 'target': STATION, 'ware': 'hullparts', 'amount': 0, 'price': 0},
    {'op': 'assign', 'subject': S2B, 'target': S2, 'assignment': 'interception', 'group': 10},
    {'op': 'assign', 'subject': S2B},
]


class CodecTests(unittest.TestCase):
    def test_every_op_round_trips_identically_in_lua_and_python(self):
        lua, K = core()
        for command in SAMPLES:
            with self.subTest(op=command['op']):
                blob = {'slot': 2, 'tick': 77, 'seq': 3, 'commands': [command]}
                text = orders.encode(blob)
                self.assertEqual(K.encode(lua_value(lua, blob)), text)
                decoded = orders.decode(text, 2)
                self.assertEqual(orders.encode(decoded), text)
                lua_blob = K.decode(text, 2)
                self.assertEqual(K.encode(lua_blob), text)
                c = lua_blob.commands[1]
                for key in ('op', 'subject', 'target', 'sector', 'ware', 'amount', 'price', 'assignment', 'group'):
                    self.assertEqual(c[key], decoded['commands'][0].get(key), key)
        # One blob with all commands still fits the per-slot budget.
        mixed = {'slot': 2, 'tick': 9, 'seq': 1, 'commands': SAMPLES[:5]}
        self.assertLessEqual(len(orders.encode(mixed)), orders.MAX_HEX)
        self.assertEqual(K.encode(lua_value(lua, mixed)), orders.encode(mixed))

    def test_saved_ids_are_minimal_big_endian_and_runtime_ids_never_appear(self):
        text = orders.encode({'slot': 1, 'tick': 1, 'seq': 1, 'commands': [{'op': 'attack', 'subject': '00000000000000ff', 'target': 'ffffffffffffffff'}]})
        self.assertEqual(text, '5001010000000100010102' + '00' + '11ff' + '18ffffffffffffffff')

    def test_float32_bits_are_exact_with_round_to_nearest_even(self):
        lua, K = core()
        for value in (0.1, -1234.5678, 1e-40, 1.5e-45, 123456.789, 1e7, 2.5, 0.5 + 2 ** -24, 1 + 2 ** -24,
                      1 + 3 * 2 ** -24, -3.0000001, 9999999.5, 0.0, -0.0, 1e-46):
            with self.subTest(value=value):
                expected = struct.unpack('>I', struct.pack('>f', value))[0]
                expected = 0 if expected == 0x80000000 else expected
                self.assertEqual(int(K.f32_bits(value)), expected)
                self.assertEqual(orders.f32_bits(value), expected)
                self.assertEqual(K.f32_value(expected), struct.unpack('>f', struct.pack('>I', expected))[0])
        command = {'op': 'move', 'subject': S1, 'sector': SECTOR, 'x': 0.1, 'y': -0.0, 'z': 1e7}
        decoded = orders.decode(orders.encode({'slot': 1, 'tick': 1, 'seq': 1, 'commands': [command]}), 1)['commands'][0]
        self.assertEqual((decoded['xbits'], decoded['ybits'], decoded['zbits']), (0x3dcccccd, 0, 0x4b189680))
        self.assertEqual(decoded['x'], struct.unpack('>f', struct.pack('>f', 0.1))[0])

    def test_encoder_refuses_invalid_commands_identically(self):
        lua, K = core()
        bad = [({'op': 'warp', 'subject': S1}, 'op_unknown'),
               ({'op': 'attack', 'subject': S1, 'target': NPC, 'flags': 4}, 'flags_invalid'),
               ({'op': 'attack', 'subject': S1}, 'reference_missing'),
               ({'op': 'attack', 'subject': '0' * 16, 'target': NPC}, 'reference_invalid'),
               ({'op': 'attack', 'subject': S1.upper(), 'target': NPC}, 'reference_invalid'),
               ({'op': 'move', 'subject': S1, 'sector': SECTOR, 'x': float('nan'), 'y': 0, 'z': 0}, 'coordinate_nonfinite'),
               ({'op': 'move', 'subject': S1, 'sector': SECTOR, 'x': 1e8, 'y': 0, 'z': 0}, 'coordinate_range'),
               ({'op': 'buy', 'subject': S1, 'target': STATION, 'ware': 'Energy', 'amount': 1, 'price': 1}, 'ware_invalid'),
               ({'op': 'buy', 'subject': S1, 'target': STATION, 'ware': 'e' * 41, 'amount': 1, 'price': 1}, 'ware_invalid'),
               ({'op': 'buy', 'subject': S1, 'target': STATION, 'ware': 'ore', 'amount': 0, 'price': 1}, 'amount_invalid'),
               ({'op': 'sell', 'subject': S1, 'target': STATION, 'ware': 'ore', 'amount': 1, 'price': -1}, 'price_invalid'),
               ({'op': 'assign', 'subject': S1, 'target': S2, 'assignment': 'patrol'}, 'assignment_invalid'),
               ({'op': 'assign', 'subject': S1, 'assignment': 'defence'}, 'assignment_invalid'),
               ({'op': 'assign', 'subject': S1, 'target': S2, 'assignment': 'defence', 'group': 11}, 'group_invalid')]
        for command, reason in bad:
            with self.subTest(reason=reason, command=command['op']):
                with self.assertRaises(orders.Refused) as caught:
                    orders.encode_command(command)
                self.assertEqual(str(caught.exception), reason)
                if command.get('x') == command.get('x'):  # NaN has no Lua literal here
                    self.assertEqual(K.command_hex(lua_value(lua, command))[1], reason)
        too_many = {'slot': 1, 'tick': 1, 'seq': 1, 'commands': [{'op': 'hold', 'subject': S1}] * 16}
        self.assertEqual(K.encode(lua_value(lua, too_many))[1], 'count_invalid')
        too_long = {'slot': 1, 'tick': 1, 'seq': 1, 'commands': [SAMPLES[6]] * 4}
        self.assertEqual(K.encode(lua_value(lua, too_long))[1], 'too_long')
        with self.assertRaises(orders.Refused):
            orders.encode(too_long)

    def test_decoder_refusals_are_strict_and_identical_in_lua_and_python(self):
        lua, K = core()
        base = orders.encode({'slot': 2, 'tick': 10, 'seq': 1, 'commands': [{'op': 'attack', 'subject': S2, 'target': NPC}]})
        self.assertEqual(base, '5001020000000a000101' + '0200' + '12a002' + '12b001')
        move = orders.encode({'slot': 2, 'tick': 10, 'seq': 1, 'commands': [SAMPLES[0]]})
        trade = orders.encode({'slot': 2, 'tick': 10, 'seq': 1, 'commands': [SAMPLES[6]]})
        assign = orders.encode({'slot': 2, 'tick': 10, 'seq': 1, 'commands': [SAMPLES[9]]})
        x = move.index('c4c80800')  # -1600.25 as float32
        vectors = [
            (base.upper(), 2, 'encoding'), (base + '0', 2, 'encoding'), ('aa' * 121, 2, 'too_long'),
            ('51' + base[2:], 2, 'channel'), ('5002' + base[4:], 2, 'version'), (base, 1, 'slot_mismatch'),
            (base[:4] + '00' + base[6:], 0, 'slot_invalid'), (base[:4] + '11' + base[6:], 17, 'slot_invalid'),
            (base[:6] + '00000000' + base[14:], 2, 'tick_invalid'), (base[:14] + '0000' + base[18:], 2, 'seq_invalid'),
            (base[:18] + '00' + base[20:], 2, 'count_invalid'), (base[:18] + '10' + base[20:], 2, 'count_invalid'),
            (base[:18] + '02' + base[20:], 2, 'truncated'), (base[:-2], 2, 'truncated'), (base + '00', 2, 'trailing'),
            (base[:20] + '00' + base[22:], 2, 'op_unknown'), (base[:20] + '0a' + base[22:], 2, 'op_unknown'),
            (base[:22] + '08' + base[24:], 2, 'flags_invalid'), (base[:22] + '04' + base[24:], 2, 'flags_invalid'),
            (base[:24] + '22' + base[26:], 2, 'reference_kind'), (base[:24] + '10' + base[26:], 2, 'reference_length'),
            (base[:24] + '19' + base[26:], 2, 'reference_length'), (base[:24] + '1300a002' + base[30:], 2, 'reference_noncanonical'),
            (base[:24] + '00' + base[30:], 2, 'reference_missing'),
            (move[:x] + '7fc00000' + move[x + 8:], 2, 'coordinate_nonfinite'),
            (move[:x] + '80000000' + move[x + 8:], 2, 'coordinate_negative_zero'),
            (move[:x] + '4cbebc20' + move[x + 8:], 2, 'coordinate_range'),
            (trade.replace('0b656e', '0b456e'), 2, 'ware_invalid'), (trade.replace('0b656e', '00656e'), 2, 'ware_invalid'),
            (trade.replace('00000064', '00000000'), 2, 'amount_invalid'),
            (assign[:-4] + '0100', 2, 'assignment_invalid'), (assign[:-4] + '0001', 2, 'assignment_invalid'),
            (orders.encode({'slot': 2, 'tick': 10, 'seq': 1, 'commands': [SAMPLES[8]]})[:-4] + '0b00', 2, 'assignment_invalid'),
            (orders.encode({'slot': 2, 'tick': 10, 'seq': 1, 'commands': [SAMPLES[8]]})[:-2] + '0b', 2, 'group_invalid'),
        ]
        for text, wire, reason in vectors:
            with self.subTest(reason=reason, text=text[:40]):
                with self.assertRaises(orders.Refused) as caught:
                    orders.decode(text, wire)
                self.assertEqual(str(caught.exception), reason)
                blob, why = K.decode(text, wire)
                self.assertIsNone(blob)
                self.assertEqual(why, reason)


def session(lua, K, **extra):
    params = {'owners': OWNERS, 'players': 3}
    params.update(extra)
    return K.session(lua_value(lua, params))


def blob(slot, tick, seq, *commands):
    return orders.encode({'slot': slot, 'tick': tick, 'seq': seq, 'commands': list(commands)})


class SchedulingTests(unittest.TestCase):
    def setUp(self):
        self.lua, self.K = core()
        self.lua.execute('''resolve_map={}
          function resolve(saved) local r=resolve_map[saved];if r==false then return nil,'ambiguous' end;if not r then return nil,'unmapped' end;return r end''')
        for i, saved in enumerate((S1, S2, S3, S2B, NPC, STATION, SECTOR)):
            self.lua.globals().resolve_map[saved] = f'{100 + i}'

    def decide(self, S, tick, inputs):
        rows, summary = self.K.decide(S, tick, self.lua.table_from(inputs), self.lua.globals().resolve)
        return [(r.head, r.result) for r in rows.values()], summary

    def test_commands_apply_exactly_at_their_admitted_tick(self):
        S = session(self.lua, self.K)
        attack = {'op': 'attack', 'subject': S2, 'target': NPC}
        self.assertEqual(self.decide(S, 5, ['', blob(2, 8, 1, attack), ''])[0], [])
        for tick in (6, 7):
            self.assertEqual(self.decide(S, tick, ['', '', ''])[0], [])
        rows, summary = self.decide(S, 8, ['', '', ''])
        self.assertEqual(rows, [(f'tick=8 slot=2 op=attack subject={S2} target={NPC}', 'queued')])
        self.assertIn('seq=1 index=1 flags=0 due=8 received=5', summary)
        self.assertEqual(self.decide(S, 9, ['', '', ''])[0], [])

    def test_canonical_order_is_slot_seq_index_regardless_of_arrival(self):
        S = session(self.lua, self.K)
        hold = lambda s: {'op': 'hold', 'subject': s}
        self.decide(S, 5, ['', blob(2, 9, 1, hold(S2), hold(S2B)), blob(3, 9, 1, hold(S3))])
        self.decide(S, 6, [blob(1, 9, 1, hold(S1)), blob(2, 9, 2, hold(S2B)), ''])
        rows, _ = self.decide(S, 9, ['', '', ''])
        self.assertEqual([h.split(' subject=')[0].split(' ', 1)[1] + ' ' + h.split('subject=')[1][:16] for h, _ in rows],
                         [f'slot=1 op=hold {S1}', f'slot=2 op=hold {S2}', f'slot=2 op=hold {S2B}',
                          f'slot=2 op=hold {S2B}', f'slot=3 op=hold {S3}'])

    def test_late_far_duplicate_backlog_and_bad_blobs_refuse_deterministically(self):
        S = session(self.lua, self.K)
        hold = {'op': 'hold', 'subject': S1}
        rows, _ = self.decide(S, 5, [blob(1, 4, 1, hold), blob(2, 70, 1, {'op': 'hold', 'subject': S2}), 'zz'])
        self.assertEqual([r for _, r in rows], ['refused_late', 'refused_far', 'refused_encoding'])
        rows, _ = self.decide(S, 6, [blob(1, 8, 1, hold), blob(1, 8, 2, hold)[:-2] + 'ff', ''])
        self.assertEqual([r for _, r in rows], ['refused_duplicate', 'refused_slot_mismatch'])
        self.K.MAX_PENDING = 2
        rows, _ = self.decide(S, 7, [blob(1, 9, 2, hold, hold), blob(2, 9, 2, {'op': 'hold', 'subject': S2}), ''])
        self.assertEqual([r for _, r in rows], ['refused_backlog'])
        rows, _ = self.decide(S, 9, ['', '', ''])
        self.assertEqual([r for _, r in rows], ['queued', 'queued'])

    def test_semantic_refusals_use_saved_ids_and_the_agreed_owner_table(self):
        S = session(self.lua, self.K)
        self.lua.globals().resolve_map['000000000000bbbb'] = False
        cases = [({'op': 'attack', 'subject': S3, 'target': NPC}, 'refused_not_owner'),
                 ({'op': 'attack', 'subject': '000000000000ffff', 'target': NPC}, 'refused_unmapped_subject'),
                 ({'op': 'attack', 'subject': S2, 'target': '000000000000ffff'}, 'refused_unmapped_target'),
                 ({'op': 'attack', 'subject': S2, 'target': '000000000000bbbb'}, 'refused_ambiguous_target'),
                 ({'op': 'attack', 'subject': S2, 'target': S2}, 'refused_self_target'),
                 ({'op': 'attack', 'subject': S2, 'target': S2B}, 'refused_own_target'),
                 ({'op': 'attack', 'subject': S2, 'target': S3}, 'queued'),
                 ({'op': 'follow', 'subject': S2, 'target': S2B}, 'queued'),
                 ({'op': 'assign', 'subject': S2B, 'target': S1, 'assignment': 'defence'}, 'refused_commander_not_owned'),
                 ({'op': 'assign', 'subject': S2B, 'target': S2, 'assignment': 'defence'}, 'queued'),
                 ({'op': 'move', 'subject': S2, 'sector': '000000000000ffff', 'x': 1, 'y': 2, 'z': 3}, 'refused_unmapped_sector')]
        for tick, (command, result) in enumerate(cases, 10):
            with self.subTest(result=result):
                self.decide(S, 2 * tick, ['', blob(2, 2 * tick + 1, tick, command), ''])
                rows, _ = self.decide(S, 2 * tick + 1, ['', '', ''])
                self.assertEqual([r for _, r in rows], [result])
        faction = session(self.lua, self.K, ownership='faction', owners={})
        self.decide(faction, 1, ['', '', blob(3, 2, 1, {'op': 'attack', 'subject': S2, 'target': S3})])
        self.assertEqual([r for _, r in self.decide(faction, 2, ['', '', ''])[0]], ['queued'])  # MD checks the faction

    def test_wire_and_session_errors_fail_closed(self):
        S = session(self.lua, self.K)
        self.decide(S, 3, ['', '', ''])
        for tick, inputs in ((3, ['', '', '']), (4, ['', '']), (5, ['', 1, ''])):
            with self.assertRaises(LuaError):
                self.decide(S, tick, inputs)
        for bad in ({'owners': {'A' * 16: 1}}, {'owners': {S1: 17}}, {'ownership': 'player'}, {'players': 0}):
            with self.assertRaises(LuaError):
                self.K.session(lua_value(self.lua, bad))

    def test_outbox_proposals_are_idempotent_bounded_and_sequenced(self):
        O = self.K.outbox(2)
        self.assertEqual(self.K.propose(O, 1, 3), '')
        trade = SAMPLES[6]
        for _ in range(4):
            self.assertTrue(self.K.submit(O, lua_value(self.lua, trade)))
        first, b1 = self.K.propose(O, 2, 3)
        self.assertEqual(self.K.propose(O, 2, 3)[0], first)
        self.assertEqual((b1.tick, b1.seq, len(b1.commands)), (5, 1, 3))
        self.assertLessEqual(len(first), self.K.MAX_HEX)
        second, b2 = self.K.propose(O, 3, 3)
        self.assertEqual((b2.tick, b2.seq, len(b2.commands)), (6, 2, 1))
        self.assertEqual(self.K.propose(O, 4, 3), '')
        self.assertEqual(self.K.submit(O, lua_value(self.lua, {'op': 'attack', 'subject': S2, 'target': S2})), (None, 'self_target'))
        with self.assertRaises(LuaError):
            self.K.propose(O, 4 - 1, 3)


WORLD = {'argon': [S1, S2, S3, S2B], 'xenon': [NPC], 'ownerless': [], 'player': []}
STATIONS = {'argon': [STATION]}


class Client:
    """One synthetic client: real core/glue under LuaJIT, mocked engine, distinct runtime IDs."""

    def __init__(self, slot, script=None, capture=False, players=3, delay=3):
        self.slot = slot
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute('stream={};events={};original={}')
        cfg = {'slot': f'p{slot}', 'native_gate_player_orders': True, 'native_gate_player_orders_delay': delay,
               'native_gate_world_identities': True,
               'native_gate_player_orders_owners': OWNERS, 'native_gate_player_orders_players': players}
        if script:
            cfg['native_gate_player_orders_script'] = script
        if capture:
            cfg['native_gate_player_orders_capture'] = True
        self.lua.execute('X4LAN_CONFIG=' + L(cfg))
        # Runtime IDs differ per client by construction: they must never reach the wire.
        runtime = {saved: str(int(saved, 16) * 10 + slot) for saved in [*sum(WORLD.values(), []), STATION, SECTOR]}
        self.runtime = runtime
        self.lua.execute('runtime_of=' + L({k: v for k, v in runtime.items()}))
        self.lua.execute('saved_of={};for s,r in pairs(runtime_of) do saved_of[r]=s end')
        self.lua.execute('world=' + L(WORLD) + ';stations=' + L(STATIONS) + ';sector_id="' + runtime[SECTOR] + '"')
        self.lua.execute('''
          DebugError=function(s) stream[#stream+1]=s end
          AddUITriggeredEvent=function(screen,control,args)
            assert(screen=='X4LAN_PlayerOrders' and control=='Apply')
            events[#events+1]=args
            stream[#stream+1]='[X4LAN] PLAYER_ORDER_APPLIED '..args.head..' result=applied '..args.tail
          end
          ConvertStringToLuaID=function(s) assert(s:match('^%d+$'));return 'L'..s end
          ConvertIDTo64Bit=function(x) return tonumber((tostring(x):gsub('^L',''))) end
          ConvertStringTo64Bit=function(s) return tonumber(s) end
          X4LAN_SHA256={digest=function(s) local h=0;for i=1,#s do h=(h*31+s:byte(i))%4294967296 end;return string.format('%08x',h) end}
          GetClusters=function() return {'Lcluster'} end
          GetSectors=function(cluster) assert(cluster=='Lcluster');return {'L'..sector_id} end
          GetWareData=function(ware,key) assert(key=='avgprice');return 15.5 end
          local factions={};for f in pairs(world) do factions[#factions+1]=f end;table.sort(factions,function(a,b) return a>b end)
          local function fill(list,buf) for i,s in ipairs(list or {}) do buf[i-1]=tonumber(runtime_of[s]) end;return #(list or {}) end
          local ffi={cdef=function() end,typeof=function() return true end,new=function() return {} end,string=function(s) return s end,
            C={GetNumAllFactions=function() return #factions end,
               GetAllFactions=function(buf,n) for i,f in ipairs(factions) do buf[i-1]=f end;return #factions end,
               GetNumAllFactionShips=function(f) return #(world[f] or {}) end,
               GetAllFactionShips=function(buf,n,f) return fill(world[f],buf) end,
               GetNumAllFactionStations=function(f) return #(stations[f] or {}) end,
               GetAllFactionStations=function(buf,n,f) return fill(stations[f],buf) end,
               IsComponentClass=function(id,class) return class=='sector' and tostring(id)==sector_id or class=='controllable' end,
               GetPlayerOccupiedShipID=function() return tonumber(runtime_of[\'''' + S2B + '''\']) end}}
          require=function(name) assert(name=='ffi');return ffi end
          gate={saved_identity=function(runtime) assert(type(runtime)=='string' and runtime:match('^%d+$'));return saved_of[runtime] or string.rep('0',16) end}
        ''')
        self.lua.execute(CORE)
        self.lua.execute(GLUE)
        self.adapter = self.lua.globals().X4LAN_NATIVE_PLAYER_ORDERS
        self.adapter.bind(self.lua.globals().gate)

    def apply(self, tick, inputs):
        return self.adapter.apply(tick, self.lua.table_from(inputs))

    def proposal(self, tick):
        return self.adapter.proposal(tick)

    def log(self):
        return '\n'.join(self.lua.globals().stream.values())

    def events(self):
        return list(self.lua.globals().events.values())


def run(clients, ticks, between=None):
    """Mock N-slot relay: proposals reported with tick t are delivered to every client at t+1."""
    inputs, summaries = [''] * len(clients), []
    for tick in range(1, ticks + 1):
        summaries.append({c.apply(tick, inputs) for c in clients})
        if between:
            between(tick)
        inputs = [c.proposal(tick) for c in clients]
    return summaries


SCRIPT = {
    2: {5: [{'op': 'move', 'flags': 1, 'subject': S2, 'sector': SECTOR, 'x': -1600.25, 'y': 100200, 'z': 1200.5}],
        9: [{'op': 'attack', 'subject': S2, 'target': '00000000000fffff'},
            {'op': 'buy', 'subject': S2B, 'target': STATION, 'ware': 'energycells', 'amount': 100, 'price': 1550}]},
    3: {5: [{'op': 'attack', 'flags': 2, 'subject': S3, 'target': NPC}],
        9: [{'op': 'follow', 'subject': S2, 'target': S3}]},
    1: {7: [{'op': 'hold', 'flags': 4, 'subject': S1}],
        9: [{'op': 'assign', 'subject': S1, 'target': S2, 'assignment': 'defence', 'group': 1}]},
}


class ThreePlayerMockGateTests(unittest.TestCase):
    def test_three_players_apply_identical_orders_at_the_same_tick(self):
        clients = [Client(slot, SCRIPT) for slot in (1, 2, 3)]
        summaries = run(clients, 14)
        self.assertTrue(all(len(s) == 1 for s in summaries), 'decision summaries diverged between clients')
        ok, details = orders.compare([c.log() for c in clients])
        self.assertTrue(ok, details)
        lines = orders.applied_lines(clients[0].log())
        self.assertEqual([line.split(' seq=')[0] for line in lines], [
            f'tick=8 slot=2 op=move subject={S2} target={SECTOR}@c4c80800,47c3b400,44961000 result=applied',
            f'tick=8 slot=3 op=attack subject={S3} target={NPC} result=applied',
            f'tick=10 slot=1 op=hold subject={S1} target=- result=applied',
            f'tick=12 slot=1 op=assign subject={S1} target={S2}/defence/1 result=refused_commander_not_owned',
            f'tick=12 slot=2 op=attack subject={S2} target=00000000000fffff result=refused_unmapped_target',
            f'tick=12 slot=2 op=buy subject={S2B} target={STATION}/energycells/100/1550 result=applied',
            f'tick=12 slot=3 op=follow subject={S2} target={S3} result=refused_not_owner'])
        for client in clients:
            events = client.events()
            self.assertEqual([(e.tick, e.slot, e.ordinal, e.op) for e in events],
                             [(8, 2, 1, 'move'), (8, 3, 2, 'attack'), (10, 1, 3, 'hold'), (12, 2, 4, 'buy')])
            move, attack, hold, buy = events
            # Each client resolves the same saved IDs to its own process-local runtime IDs.
            self.assertEqual((move.subject, move.sector), ('L' + client.runtime[S2], 'L' + client.runtime[SECTOR]))
            self.assertEqual((move.x, move.y, move.z, move.clear, move.immediate), (-1600.25, 100200, 1200.5, 1, 0))
            self.assertEqual((attack.target, attack.immediate, hold.holdfire), ('L' + client.runtime[NPC], 1, 1))
            self.assertEqual((buy.ware, buy.amount, buy.price, buy.target), ('energycells', 100, 1550, 'L' + client.runtime[STATION]))
            self.assertIn('PLAYER_ORDERS INDEX tick=1 objects=7 mapped=7 duplicates=0', client.log())
        proposals = [line for line in clients[1].log().splitlines() if 'PLAYER_ORDER_PROPOSED' in line]
        self.assertEqual([p.split(' hex=')[0] for p in proposals],
                         ['[X4LAN] PLAYER_ORDER_PROPOSED tick=5 slot=2 seq=1 apply=8 count=1',
                          '[X4LAN] PLAYER_ORDER_PROPOSED tick=9 slot=2 seq=2 apply=12 count=2'])
        for client in clients:  # runtime IDs never appear in proposals
            for p in proposals:
                self.assertNotIn(client.runtime[S2], p.split(' hex=')[1])

    def test_a_divergent_index_changes_the_hashed_decision_summary(self):
        clients = [Client(slot, SCRIPT) for slot in (1, 2, 3)]
        clients[2].lua.execute(f"saved_of[runtime_of['{NPC}']]=nil")
        summaries = run(clients, 9)
        self.assertEqual(len(summaries[7]), 2)  # tick 8: client 3 refuses the unmapped attack target

    def test_ui_capture_never_executes_locally_and_blocks_other_order_paths(self):
        clients = [Client(slot, capture=(slot == 2)) for slot in (1, 2, 3)]
        lua = clients[1].lua
        lua.execute('''
          calls={}
          local function original(name) return function(...) calls[#calls+1]=name;return 7 end end
          interact={name='InteractMenu',componentSlot={},selectedplayerships={},data={}}
          for _,n in ipairs({'orderAttack','orderMoveWait','orderMining','orderStopAndHoldFire','orderAssignCommander','orderDock',
                             'buttonRemoveAllOrders','buttonAssignCommander','buttonInfo','onCloseElement'}) do interact[n]=original(n) end
          map={name='MapMenu',contextMenuData={}}
          for _,n in ipairs({'orderAttack','orderFollow','orderWithdrawFromCombat','buttonConfirmTradeLoop','buttonConfirmTrade','closeContextMenu'}) do map[n]=original(n) end
          Menus={interact,map}
        ''')
        r = clients[1].runtime

        def act(tick):
            if tick == 2:
                lua.execute(f'''
                  interact.orderAttack({r[S2]},'L{r[NPC]}',true,true)
                  interact.orderMoveWait({r[S2B]},{r[SECTOR]},{{x=10.5,y=-2,z=3}},{r[SECTOR]},false,false)
                  interact.orderMining({r[S2]})
                  interact.orderStopAndHoldFire({r[S2B]},false,false)
                  interact.buttonRemoveAllOrders()
                  interact.priorityOrderMode=true;interact.orderAttack({r[S2]},'L{r[NPC]}',false,false);interact.priorityOrderMode=nil
                  interact.orderDock({r[S2]},{r[STATION]},false,'Lventure')
                  interact.buttonInfo()
                  map.orderAttack({r[S2]},{r[S3]},false,'both')
                  map.orderFollow({r[S2]},{r[S1]},false)
                  map.orderWithdrawFromCombat({r[S2]})
                  map.contextMenuData={{loop='SingleSell',currentShip={r[S2]},component={r[STATION]},ware='ore',amount=5}}
                  map.buttonConfirmTradeLoop()
                  map.buttonConfirmTrade()
                  interact.orderAttack({r[S2]},999999,false,false)
                ''')
            if tick == 4:
                lua.execute(f'''
                  interact.componentSlot.component={r[S2]};interact.showPlayerInteractions=true
                  interact.buttonAssignCommander('defence',2)
                ''')
        run(clients, 10, act)
        self.assertEqual(list(lua.globals().calls.values()), ['buttonInfo', 'closeContextMenu', 'onCloseElement'])
        log = clients[1].log()
        self.assertIn('PLAYER_ORDERS CAPTURE menu=InteractMenu captured=6 blocked=2', log)
        self.assertIn('PLAYER_ORDERS CAPTURE menu=MapMenu captured=3 blocked=2', log)
        for reason in ('entry=orderMining reason=not_lockstep_order', 'entry=buttonRemoveAllOrders reason=not_lockstep_order',
                       'entry=orderAttack reason=priority_order', 'entry=orderDock reason=venture_or_showroom',
                       'menu=MapMenu entry=orderAttack reason=priority_order', 'entry=orderWithdrawFromCombat reason=not_lockstep_order',
                       'entry=buttonConfirmTrade reason=not_lockstep_order', 'entry=attack reason=unmapped_target'):
            self.assertIn(reason, log)
        ok, details = orders.compare([c.log() for c in clients])
        self.assertTrue(ok, details)
        lines = [line.split(' seq=')[0] for line in orders.applied_lines(clients[0].log())]
        self.assertEqual(lines, [
            f'tick=5 slot=2 op=attack subject={S2} target={NPC} result=applied',
            f'tick=5 slot=2 op=move subject={S2B} target={SECTOR}@41280000,c0000000,40400000 result=applied',
            f'tick=5 slot=2 op=hold subject={S2B} target=- result=applied',
            f'tick=5 slot=2 op=follow subject={S2} target={S1} result=applied',
            f'tick=5 slot=2 op=sell subject={S2} target={STATION}/ore/5/1550 result=applied',
            f'tick=7 slot=2 op=assign subject={S2} target={S2B}/defence/2 result=applied'])
        attack = clients[0].events()[0]
        self.assertEqual((attack.clear, attack.immediate), (1, 1))

    def test_flag_off_is_inert_and_invalid_configuration_refuses_before_arming(self):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute("X4LAN_CONFIG={slot='p1'}")
        lua.execute(CORE)
        lua.execute(GLUE)
        self.assertIsNone(lua.globals().X4LAN_NATIVE_PLAYER_ORDERS)
        for change in ({'slot': 'p0'}, {'slot': 'p17'}, {'native_gate_player_orders_delay': 0}, {'native_gate_world_identities': False},
                       {'native_gate_player_orders_delay': 65}, {'native_gate_player_orders_ownership': 'player'},
                       {'native_gate_player_orders_script': {2: {5: [{'op': 'attack', 'subject': S2, 'target': S2}]}}},
                       {'native_gate_player_orders_script': {2: {5: [{'op': 'teleport', 'subject': S2}]}}}):
            with self.subTest(change=change), self.assertRaises(Exception):
                cfg = {'slot': 'p1', 'native_gate_player_orders': True, 'native_gate_player_orders_owners': OWNERS,
                       'native_gate_world_identities': True}
                cfg.update(change)
                lua = LuaRuntime(unpack_returned_tuples=True)
                lua.execute('X4LAN_CONFIG=' + L(cfg))
                lua.execute(CORE)
                lua.execute(GLUE)


class ProbeIntegrationTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute('''
          X4LAN_CONFIG={native_gate_test=true,test_profile='45841',slot='p1',native_gate_hash='clock',
            native_gate_snapshot_sha=string.rep('a',64),port=12345,token=string.rep('c',32),native_gate_player_orders=true,
            native_gate_clients=3}
          X4LAN_SHA256={digest=function(data) hashed=data;return string.rep('b',64) end}
          logs={};reports=0;proposed={};applied={};bound=0
          state={active=1,boot_phase=3,issued=1,report_tick=0,rng='0',wire_version=3,clients=3,inputs={'', '50cd', ''}}
          gate={boot=function() return 0 end,poll=function() return state end,
            report=function(tick) reports=reports+1;state.report_tick=tick;return 1 end,
            propose=function(tick,hex) assert(tick==state.issued);proposed[#proposed+1]=hex;return 1 end,
            wire=function(version,clients) return (version==3 and clients==3) and 1 or 0 end,stop=function() state.active=0;return 1 end}
          package.loadlib=function(path) assert(not path:find('fixedtick'));return function() return gate end end
          local ffi={cdef=function() end,typeof=function() return true end,string=function(s) return s end,
            C={GetSaveFolderPath=function() return 'fake/45841/save' end,GetCurrentGameTime=function() return 7 end,
              IsStartmenu=function() return false end,ReloadSaveList=function() end,IsSaveListLoadingComplete=function() return true end}}
          require=function(name) assert(name=='ffi');return ffi end
          DebugError=function(s) logs[#logs+1]=s end
          RegisterEvent=function() end
          SetScript=function(name,fn) update=fn end
          X4LAN_NATIVE_PLAYER_ORDERS={bind=function(g) assert(g==gate);bound=bound+1 end,
            apply=function(tick,inputs) applied[#applied+1]=tick..':'..table.concat(inputs,',');return 'PORDERS_1|'..tick end,
            proposal=function(tick) return string.format('50%02x',tick) end}
        ''')

    def test_probe_applies_admitted_inputs_hashes_decisions_and_proposes(self):
        self.lua.execute(MUX)
        self.lua.execute(PROBE)
        self.lua.execute('update()')
        g = self.lua.globals()
        self.assertEqual(g.bound, 1)
        # Through the input multiplexer: the orders section of every slot, the legacy orders-only blob.
        self.assertEqual(list(g.applied.values()), ['1:,50cd,'])
        self.assertEqual(list(g.proposed.values()), ['5001'])
        self.assertEqual(g.reports, 1)
        self.assertIn('|PORDERS_1|1', g.hashed)
        self.assertFalse(any('ERROR' in line for line in g.logs.values()))

    def test_probe_refuses_without_the_variable_length_wire_or_proposal_api(self):
        for change in ('state.inputs=nil', 'gate.propose=nil', 'gate.propose=function() return 0 end',
                       'X4LAN_CONFIG.native_gate_command_test="coop"', 'X4LAN_CONFIG.native_gate_clients=nil',
                       'gate.wire=nil'):
            with self.subTest(change=change):
                self.setUp()
                self.lua.execute(change)
                try:
                    self.lua.execute(MUX)
                    self.lua.execute(PROBE)
                    self.lua.execute('update()')
                except LuaError:
                    continue  # refused while arming
                self.assertEqual(self.lua.globals().reports, 0)
                self.assertTrue(any('ERROR' in line or 'REFUSED' in line for line in self.lua.globals().logs.values()))


class MdScriptTests(unittest.TestCase):
    def test_md_dispatches_every_op_with_expected_order_ids(self):
        text = MD.read_text(encoding='utf-8')
        root = ET.fromstring(text)
        self.assertEqual(root.get('name'), 'X4LAN_PlayerOrders')
        self.assertEqual([c.get('name') for c in root.iter('cue')], ['State', 'Apply'])
        _, K = core()
        for op in K.OPS.values():
            self.assertIn(f"$a.$op == '{op.name}'", text)
        ids = {c.get('id') for c in root.iter('create_order')}
        self.assertEqual(ids, {"'MoveWait'", "'Attack'", "'Follow'", "'ProtectShip'", "'DockAndWait'", "'Wait'", '$expect', "'AssignCommander'"})
        self.assertEqual(text.count('<debug_text '), 1)
        self.assertIn("'[X4LAN] PLAYER_ORDER_APPLIED %s result=%s %s time=%s'", text)
        for assignment in K.ASSIGNMENTS.values():
            self.assertIn(f"assignment.{assignment}", text)

    # Compiling the game schema takes about two minutes: opt in with X4LAN_MD_SCHEMA=1 (lxml + x4-re).
    @unittest.skipUnless(os.environ.get('X4LAN_MD_SCHEMA') == '1' and etree is not None and MD_XSD.is_file(),
                         'set X4LAN_MD_SCHEMA=1 with lxml and the read-only x4-re md.xsd')
    def test_md_validates_against_game_schema(self):
        schema = etree.XMLSchema(etree.parse(str(MD_XSD)))
        self.assertTrue(schema.validate(etree.parse(str(MD))), schema.error_log)


class BuilderTests(unittest.TestCase):
    def snapshot(self, directory):
        ships = ''.join(f'<component id="[0x{0xa000 + i:x}]" class="ship_s" owner="{"argon" if i <= 6 else "xenon"}" '
                        f'macro="{"ship_arg_s_fighter_01_a_macro" if i <= 6 else "ship_xen_s_fighter_01_a_macro"}">'
                        '<movement class="navigation"/></component>' for i in range(1, 13))
        values = ''.join(f'<value type="component" value="[0x{0xa000 + i:x}]"/>' for i in range(1, 13))
        xml = (f'<savegame><universe><component class="cluster" id="[0x5]"><component class="sector" id="[0xc01]">{ships}'
               '</component></component></universe><script><refs type="list"><ref id="77">' + values + '</ref></refs></script>'
               '<md><script name="X4LAN_NativeGate"><cue name="Spawn" state="complete"><vars><value name="$ships" type="list" value="77"/>'
               '</vars></cue></script></md></savegame>')
        path = Path(directory) / 'save.xml.gz'
        with gzip.open(path, 'wt') as stream:
            stream.write(xml)
        return path

    def build(self, directory, plan, *extra):
        dll = Path(directory) / 'gate.dll'
        dll.write_bytes(b'dll')
        plan_path = Path(directory) / 'plan.json'
        plan_path.write_text(json.dumps(plan))
        out = Path(directory) / 'out'
        argv = ['build', '--gate-dll', str(dll), '--snapshot', str(self.snapshot(directory)), '--mode', 'battle',
                '--player-orders', str(plan_path), '--out', str(out), *(extra or ('--session-clients',))]
        with mock.patch('sys.argv', argv), contextlib.redirect_stderr(io.StringIO()):
            builder.main()
        return out

    PLAN = {'players': 3, 'delay': 2, 'owners': {'@1': 1, '@2': 2, '@7': 3},
            'script': {'2': {'30': [{'op': 'move', 'subject': '@2', 'sector': '@sector:2', 'x': -1600, 'y': 100200, 'z': 1200}]},
                       '3': {'45': [{'op': 'attack', 'subject': '@7', 'target': '@1'}]}}}

    def test_overlay_copies_adapter_and_writes_resolved_plan(self):
        with tempfile.TemporaryDirectory() as directory:
            out = self.build(directory, self.PLAN)
            files = (out / 'ui.xml.files').read_text().splitlines()
            self.assertEqual(files[:3], ['ui/native_gate_inputs_core.lua', 'ui/native_gate_player_orders_core.lua',
                                         'ui/native_gate_player_orders.lua'])
            self.assertEqual(files[-1], 'ui/native_gate_probe.lua')
            self.assertTrue((out / 'md/native_gate_player_orders.xml').is_file())
            config = (out / 'ui/config.lua.append').read_text()
            self.assertIn('X4LAN_CONFIG.native_gate_player_orders = true', config)
            self.assertIn('X4LAN_CONFIG.native_gate_player_orders_delay = 2', config)
            self.assertIn('X4LAN_CONFIG.native_gate_player_orders_owners = {["000000000000a001"]=1,["000000000000a002"]=2,["000000000000a007"]=3}', config)
            self.assertIn('[30]={{["op"]="move",["sector"]="0000000000000c01",["subject"]="000000000000a002"', config)
            self.assertIn('X4LAN_CONFIG.native_gate_world_identities = true', config)
            # The generated configuration arms the real glue.
            lua = LuaRuntime(unpack_returned_tuples=True)
            lua.execute("X4LAN_CONFIG={slot='p2'}\n" + config.replace('@X4LANE_PORT@', '1').replace('@X4LANE_CLIENTS@', '3').replace('"@X4LANE_SLOT@"', '"p2"'))
            lua.execute('require=function() return {cdef=function() end,typeof=function() return true end,C={}} end')
            lua.execute(CORE)
            lua.execute(GLUE)
            self.assertIsNotNone(lua.globals().X4LAN_NATIVE_PLAYER_ORDERS)

    def test_overlay_refuses_bad_plans_and_exclusive_command_paths(self):
        bad = [dict(self.PLAN, owners={}), dict(self.PLAN, delay=0), dict(self.PLAN, extra=1),
               dict(self.PLAN, owners={'@13': 1}), dict(self.PLAN, owners={'@1': 4}),
               dict(self.PLAN, script={'2': {'30': [{'op': 'attack', 'subject': '@2', 'target': '@2', 'x': 1}]}}),
               dict(self.PLAN, script={'2': {'30': [{'op': 'move', 'subject': '@2', 'sector': '@sector:2', 'x': 1e9, 'y': 0, 'z': 0}]}})]
        for plan in bad:
            with self.subTest(plan=plan), tempfile.TemporaryDirectory() as directory, self.assertRaises(SystemExit):
                self.build(directory, plan)
        with tempfile.TemporaryDirectory() as directory, self.assertRaises(SystemExit):
            self.build(directory, self.PLAN, '--session-clients', '--command-test', 'coop')
        with tempfile.TemporaryDirectory() as directory, self.assertRaises(SystemExit):
            self.build(directory, self.PLAN, '--mode', 'battle')  # orders need the protocol 3 wire

    def test_committed_three_player_live_plan_builds(self):
        plan = json.loads((DEV / 'native_gate_player_orders_plan_3p.json').read_text())
        with tempfile.TemporaryDirectory() as directory:
            config = (self.build(directory, plan) / 'ui/config.lua.append').read_text()
        self.assertIn('X4LAN_CONFIG.native_gate_player_orders_players = 3', config)
        self.assertEqual(config.count('["op"]='), 10)

    def test_compare_reports_the_first_divergent_line(self):
        a = 'x [X4LAN] PLAYER_ORDER_APPLIED tick=8 slot=2 op=hold subject=s target=- result=applied seq=1\n'
        self.assertEqual(orders.compare([a, 'other prefix ' + a])[0], True)
        ok, details = orders.compare([a, a.replace('applied', 'refused_not_owner')])
        self.assertFalse(ok)
        self.assertIn('client 2 differs at line 1', details)
        self.assertFalse(orders.compare([a, ''])[0])


if __name__ == '__main__':
    unittest.main()
