"""Protocol 3 input multiplexer: the container codec (Lua mirror vs Python reference, opt-in vs the C
functions of lockstep_hit_claims.h), the multiplexer's apply/proposal contract, the hit claim carrier,
the builders, and three synthetic clients running the real probe with pilot, orders and hit sections.

Synthetic clients only: this proves identical demultiplexing, receipts and proposals, not native X4.
"""
import contextlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

from lupa.luajit21 import LuaRuntime, LuaError

ROOT = Path(__file__).resolve().parents[1]
DEV = ROOT / 'tools/dev'
MUX = (DEV / 'native_gate_inputs_core.lua').read_text(encoding='utf-8')
PROBE = (DEV / 'native_gate_probe.lua').read_text(encoding='utf-8')
ORDERS_CORE = (DEV / 'native_gate_player_orders_core.lua').read_text(encoding='utf-8')
ORDERS_GLUE = (DEV / 'native_gate_player_orders.lua').read_text(encoding='utf-8')
HEADER = ROOT / 'src/native/engine/lockstep_hit_claims.h'
sys.path.insert(0, str(DEV))
import native_gate_inputs as ref  # noqa: E402
import native_gate_player_orders as orders_ref  # noqa: E402
import native_gate_sandbox as sandbox  # noqa: E402
import native_gate_players as players  # noqa: E402

PILOT = '01' + '00' + '0102' + 'ff7f' * 7 + '0000' + '00' * 8          # 28 bytes, version 1
ORDERS = orders_ref.encode({'slot': 2, 'tick': 9, 'seq': 1, 'commands': [{'op': 'hold', 'subject': '000000000000a002'}]})
CLAIM = {'kind': 2, 'observe16': 0xbeef, 'projectile': '0123456789abcdef', 'target': '0000000000176ad5', 'point': [-8, 32767, 8]}
CLAIM_HEX = ('12' '00' 'efbe' 'efcdab8967452301' 'd56a170000000000' 'f8ff' 'ff7f' '0800')
HITS = '01' + CLAIM_HEX
OCTREE = '05000000' + '0d' + '00' * 15                                 # nav octree report: W=5, index 6, 8 and 9 built
# Stock piloting record v3 (lockstep_pilot_stock.h): flags active, capture 0x0102, no cursor, aim or view, one state event BOOST=1.
STOCK = '03' + '01' + '0201' + '00' * 24 + '00' + '00' * 13 + '01' + '1140' + '01000000'
UI = '01' + '6400' + '01' + '0102' + '0100'   # UI replay section v1 (lockstep_ui_replay.h): capture 100, one travel start


def lua_core():
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(MUX)
    return lua, lua.globals().X4LAN_INPUT_MUX_CORE


def lua_result(value):
    """(value, None) or (None, reason) from a Lua function returning v or nil,reason."""
    if isinstance(value, tuple):
        return value
    return value, None


def lua_sections(table):
    return {k: table[k] for k in ('pilot', 'hits', 'octree', 'orders', 'stock', 'ui') if table[k] is not None}


def blob(*parts):
    return ''.join(parts)


def container(*sections):
    out = '4d01'
    for kind, payload in sections:
        out += f'{kind:02x}{len(payload) // 2:02x}' + payload
    return out


VALID = {
    'empty': ('', {}),
    'pilot': (PILOT, {'pilot': PILOT}),
    'orders': (ORDERS, {'orders': ORDERS}),
    'hits': (container((0x48, HITS)), {'hits': HITS}),
    'pilot+hits': (container((0x01, PILOT), (0x48, HITS)), {'pilot': PILOT, 'hits': HITS}),
    'pilot+orders': (container((0x01, PILOT), (0x50, ORDERS)), {'pilot': PILOT, 'orders': ORDERS}),
    'hits+orders': (container((0x48, HITS), (0x50, ORDERS)), {'hits': HITS, 'orders': ORDERS}),
    'all': (container((0x01, PILOT), (0x48, HITS), (0x50, ORDERS)), {'pilot': PILOT, 'hits': HITS, 'orders': ORDERS}),
    # The nav octree report (kind 0x4f) sits between hits and orders; alone it needs the container, like hits.
    'octree': (container((0x4f, OCTREE)), {'octree': OCTREE}),
    'pilot+octree': (container((0x01, PILOT), (0x4f, OCTREE)), {'pilot': PILOT, 'octree': OCTREE}),
    'hits+octree+orders': (container((0x48, HITS), (0x4f, OCTREE), (0x50, ORDERS)), {'hits': HITS, 'octree': OCTREE, 'orders': ORDERS}),
    'all+octree': (container((0x01, PILOT), (0x48, HITS), (0x4f, OCTREE), (0x50, ORDERS)),
                   {'pilot': PILOT, 'hits': HITS, 'octree': OCTREE, 'orders': ORDERS}),
    # The stock piloting record (kind 0x53) has no legacy form: alone it is a container, last in wire order.
    'stock': (container((0x53, STOCK)), {'stock': STOCK}),
    'pilot+stock': (container((0x01, PILOT), (0x53, STOCK)), {'pilot': PILOT, 'stock': STOCK}),
    'orders+stock': (container((0x50, ORDERS), (0x53, STOCK)), {'orders': ORDERS, 'stock': STOCK}),
    # The UI replay's section (kind 0x55) has no legacy form either: it follows stock.
    'ui': (container((0x55, UI)), {'ui': UI}),
    'stock+ui': (container((0x53, STOCK), (0x55, UI)), {'stock': STOCK, 'ui': UI}),
}
INVALID = [
    ('ABCD', 'encoding'), ('abc', 'encoding'), (PILOT[:-2], 'pilot_length'), ('33', 'channel'), ('4d', 'version'),
    ('4d02' + '480b' + HITS, 'version'), ('4d01', 'noncanonical'), (container((0x01, PILOT)), 'noncanonical'),
    (container((0x50, ORDERS)), 'noncanonical'), (container((0x01, PILOT), (0x48, HITS)) + '00', 'truncated'),
    (container((0x01, PILOT), (0x48, HITS)) + '5000', 'section_length'), ('4d01' + '4805' + HITS[:6], 'section_length'),
    (container((0x48, HITS), (0x01, PILOT)), 'order'), (container((0x48, HITS), (0x48, HITS)), 'order'),
    (container((0x02, PILOT), (0x48, HITS)), 'kind'), ('4d01' + '011d' + PILOT + '00' + '480b' + HITS, 'pilot_length'),
    (container((0x01, '02' + PILOT[2:]), (0x48, HITS)), 'pilot_length'), (container((0x48, HITS), (0x50, '51' + ORDERS[2:])), 'orders_channel'),
    ('50' * 241, 'too_long'),
    (container((0x4f, OCTREE[:-2])), 'octree_length'), (container((0x4f, OCTREE + '00')), 'octree_length'),
    (container((0x50, ORDERS), (0x4f, OCTREE)), 'order'), (container((0x4f, OCTREE), (0x48, HITS)), 'order'),
    (container((0x4f, OCTREE), (0x4f, OCTREE)), 'order'),
    (container((0x53, '02' + STOCK[2:])), 'stock_version'), (container((0x53, STOCK), (0x50, ORDERS)), 'order'),
    (container((0x53, STOCK), (0x53, STOCK)), 'order'),
    (container((0x55, '02' + UI[2:])), 'ui_version'), (container((0x55, UI), (0x53, STOCK)), 'order'),
]


class CodecTests(unittest.TestCase):
    def test_valid_blobs_decode_and_reencode_identically_in_lua_and_python(self):
        lua, K = lua_core()
        self.assertEqual(CLAIM_HEX, ref.encode_hits([CLAIM])[2:])
        for name, (text, sections) in VALID.items():
            with self.subTest(blob=name):
                self.assertEqual(ref.decode(text), sections)
                self.assertEqual(ref.encode(sections), text)
                decoded, why = lua_result(K.decode(text))
                self.assertIsNone(why)
                self.assertEqual(lua_sections(decoded), sections)
                self.assertEqual(K.encode(lua.table_from(sections)), text)
                self.assertEqual(K.size(lua.table_from(sections)), len(text) // 2)
                # Legacy single forms stay byte-identical to what the exclusive adapters sent before.
                if name in ('pilot', 'orders'):
                    self.assertEqual(text, sections[name])

    def test_invalid_blobs_refuse_with_the_same_reason_in_lua_and_python(self):
        lua, K = lua_core()
        for text, reason in INVALID:
            with self.subTest(reason=reason, blob=text[:24]):
                with self.assertRaises(ref.Refused) as caught:
                    ref.decode(text)
                self.assertEqual(str(caught.exception), reason)
                self.assertEqual(lua_result(K.decode(text))[1], reason)

    def test_encoder_refuses_what_does_not_fit_or_is_malformed(self):
        lua, K = lua_core()
        big = '50' * 200
        cases = [({'pilot': PILOT, 'orders': big, 'hits': HITS}, 'too_long'), ({'pilot': PILOT[:-2]}, 'pilot_length'),
                 ({'orders': '51' + ORDERS[2:]}, 'orders_channel'), ({'hits': HITS[:-2]}, 'length'), ({'other': PILOT}, 'kind'),
                 ({'orders': '50' * 241}, 'too_long'), ({'octree': OCTREE[:-2]}, 'octree_length'),
                 ({'octree': OCTREE, 'orders': '50' * 230}, 'too_long'), ({'stock': '02' + STOCK[2:]}, 'stock_version'),
                 ({'stock': STOCK, 'orders': '50' * 220}, 'too_long')]
        for sections, reason in cases:
            with self.subTest(reason=reason):
                with self.assertRaises(ref.Refused) as caught:
                    ref.encode(sections)
                self.assertEqual(str(caught.exception), reason)
                self.assertEqual(lua_result(K.encode(lua.table_from(sections)))[1], reason)

    def test_hit_claims_round_trip_and_refuse_identically(self):
        lua, K = lua_core()
        claims = [CLAIM, dict(CLAIM, kind=1, projectile='00000000000000ff', point=[0, -32767, 1], observe16=0)]
        text = ref.encode_hits(claims)
        self.assertEqual(ref.decode_hits(text), claims)
        lua_claims = lua.eval(json_to_lua(claims))
        self.assertEqual(K.hits_encode(lua_claims), text)
        decoded = K.hits_decode(text)
        self.assertEqual([(c.kind, c.observe16, c.projectile, c.target, list(c.point.values())) for c in decoded.values()],
                         [(c['kind'], c['observe16'], c['projectile'], c['target'], c['point']) for c in claims])
        bad_payloads = [('00', 'length'), ('09' + CLAIM_HEX * 9, 'length'), ('01' + CLAIM_HEX[:-2], 'length'),
                        ('01' + '13' + CLAIM_HEX[2:], 'claim'), ('01' + '22' + CLAIM_HEX[2:], 'claim'), ('01' + '1201' + CLAIM_HEX[4:], 'claim'),
                        ('01' + CLAIM_HEX[:40] + '0080' + CLAIM_HEX[44:], 'point'), ('01' + CLAIM_HEX[:8] + '00' * 8 + CLAIM_HEX[24:], 'identity'),
                        ('02' + CLAIM_HEX + CLAIM_HEX, 'duplicate')]
        for text, reason in bad_payloads:
            with self.subTest(reason=reason, payload=text[:12]):
                with self.assertRaises(ref.Refused) as caught:
                    ref.decode_hits(text)
                self.assertEqual(str(caught.exception), reason)
                self.assertEqual(lua_result(K.hits_decode(text))[1], reason)
        for claims, reason in (([], 'count'), ([CLAIM] * 9, 'count'), ([dict(CLAIM, kind=3)], 'claim'),
                               ([dict(CLAIM, projectile='0' * 16)], 'identity'), ([CLAIM, CLAIM], 'duplicate'),
                               ([dict(CLAIM, point=[0, 0, -32768])], 'point')):
            with self.subTest(reason=reason):
                with self.assertRaises(ref.Refused) as caught:
                    ref.encode_hits(claims)
                self.assertEqual(str(caught.exception), reason)
                self.assertEqual(lua_result(K.hits_encode(lua.eval(json_to_lua(claims))))[1], reason)

    def test_observe_tick_reconstruction_matches_hit_stage(self):
        lua, K = lua_core()
        for observe16, r, expected in ((99, 100, 99), (91, 100, 91), (90, 100, None), (100, 100, None), (65535, 65537, 65535),
                                       (65534, 65540, 65534), (65530, 65540, None), (2, 65540, 65538), (0, 1, None), (0, 2, 0)):
            with self.subTest(observe16=observe16, r=r):
                if expected is None:
                    self.assertRaises(ref.Refused, ref.hit_observe, observe16, r)
                    self.assertEqual(lua_result(K.hit_observe(observe16, r))[1], 'far')
                else:
                    self.assertEqual(ref.hit_observe(observe16, r), expected)
                    self.assertEqual(K.hit_observe(observe16, r), expected)

    # The C functions themselves (opt-in: set X4LAN_ZIG to a zig executable).
    @unittest.skipUnless(os.environ.get('X4LAN_ZIG') and Path(os.environ.get('X4LAN_ZIG', '')).is_file(),
                         'set X4LAN_ZIG to the zig compiler to cross-check the C container codec')
    def test_c_container_codec_agrees_with_the_mirrors(self):
        cases = [text for text, _ in VALID.values()] + [text for text, _ in INVALID if re.fullmatch(r'(?:[0-9a-f]{2})*', text)]
        with tempfile.TemporaryDirectory() as d:
            harness = Path(d) / 'mux.c'
            harness.write_text(c_harness())
            exe = Path(d) / 'mux.exe'
            subprocess.run([os.environ['X4LAN_ZIG'], 'cc', '-std=c11', '-O1', '-o', str(exe), str(harness)], check=True)
            out = subprocess.run([str(exe)], input='\n'.join(cases + ['H' + HITS, 'H' + '01' + CLAIM_HEX[:40] + '0080' + CLAIM_HEX[44:]]) + '\n',
                                 capture_output=True, text=True, check=True).stdout.splitlines()
        for text, line in zip(cases, out):
            with self.subTest(blob=text[:24]):
                try:
                    sections = ref.decode(text)
                    self.assertEqual(line, 'OK ' + ' '.join(sections.get(k, '-') for k in ('pilot', 'hits', 'octree', 'orders', 'stock', 'ui')) + ' ' +
                                     (ref.encode(sections) or '-'))
                except ref.Refused as error:
                    self.assertEqual(line, 'NO ' + str(error))
        self.assertEqual(out[len(cases)], 'HOK 1')
        self.assertEqual(out[len(cases) + 1], 'HNO')


def json_to_lua(value):
    if isinstance(value, bool):
        return 'true' if value else 'false'
    if isinstance(value, (int, float)):
        return repr(value)
    if isinstance(value, str):
        return '"' + value + '"'
    if isinstance(value, list):
        return '{' + ','.join(json_to_lua(v) for v in value) + '}'
    return '{' + ','.join(f'[{json_to_lua(k)}]={json_to_lua(v)}' for k, v in value.items()) + '}'


def c_harness():
    """The container and hit payload functions copied verbatim from the header, with a line driver."""
    text = HEADER.read_text(encoding='utf-8')
    defines = '\n'.join(line for line in text.splitlines() if re.match(r'#define (INPUT_|HIT_CLAIM|HIT_CLAIMS_MAX|HIT_INCOMING|HIT_OUTGOING)', line))
    typedefs = '\n'.join(line for line in text.splitlines() if line.startswith('typedef struct {const unsigned char *pilot') or
                         line.startswith('typedef struct {unsigned kind,flags;uint16_t observe16'))
    mux = text[text.index('/* ---- blob multiplexing ---- */'):text.index('/* ---- claims ---- */')]
    claims = text[text.index('static void hit_claim_encode'):text.index('/* Admission at tick r')]
    return ('#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n#define PILOT_RECORD_BYTES 28u\n#define PILOT_RECORD_VERSION 1u\n'
            + defines + '\n' + typedefs + '\n' + mux + '\n' + claims + r'''
static void hex(const unsigned char *b,size_t n){if(!n){fputs("-",stdout);return;}for(size_t i=0;i<n;i++)printf("%02x",b[i]);}
int main(void){char line[2048];
 while(fgets(line,sizeof line,stdin)){
  size_t n=strcspn(line,"\r\n");line[n]=0;int h=line[0]=='H';const char *p=line+h;size_t m=strlen(p)/2;unsigned char b[1024];
  for(size_t i=0;i<m;i++){unsigned v;sscanf(p+2*i,"%2x",&v);b[i]=(unsigned char)v;}
  if(h){hit_claim c[HIT_CLAIMS_MAX];unsigned k=0;if(hit_payload_decode(b,(unsigned)m,c,&k))printf("HOK %u\n",k);else puts("HNO");continue;}
  input_sections s;const char *why;
  if(!input_mux_decode(b,m,&s,&why)){printf("NO %s\n",why);continue;}
  unsigned char again[512];size_t r=input_mux_encode(&s,again,sizeof again);
  fputs("OK ",stdout);hex(s.pilot,s.pilot_len);putchar(' ');hex(s.hits,s.hits_len);putchar(' ');hex(s.octree,s.octree_len);putchar(' ');
  hex(s.orders,s.orders_len);putchar(' ');hex(s.stock,s.stock_len);putchar(' ');hex(s.ui,s.ui_len);putchar(' ');hex(again,r);putchar('\n');
 }
 return 0;}
''')


class MuxTests(unittest.TestCase):
    def setUp(self):
        self.lua, self.K = lua_core()
        self.lua.execute('''
          logs={};seen={};refused={}
          M=X4LAN_INPUT_MUX_CORE.mux(function(s) logs[#logs+1]=s end)
          function section(name,payloads)
            return {apply=function(tick,inputs,clients,slot) seen[#seen+1]=name..'@'..tick..':'..table.concat(inputs,',')..'/'..clients..'/'..slot
                      return name=='orders' and 'PORDERS_1|'..tick or nil end,
                    proposal=function(tick,budget) seen[#seen+1]=name..'?'..tick..':'..budget;return payloads[tick] end,
                    refused=function(tick,why) refused[#refused+1]=name..'@'..tick..':'..why end}
          end
        ''')

    def logs(self):
        return list(self.lua.globals().logs.values())

    def test_apply_hands_every_section_its_slot_list_and_folds_one_receipt(self):
        self.lua.execute(f'''
          M.register('pilot','pilot',section('pilot',{{}}))
          M.register('orders','orders',section('orders',{{}}))
          M.register('hits','hits',section('hits',{{}}))
          receipt=M.apply(7,{{'{VALID["all"][0]}','{PILOT}','','{ORDERS}','33'}},5,2)
        ''')
        g = self.lua.globals()
        self.assertEqual(list(g.seen.values()), [
            f'pilot@7:{PILOT},{PILOT},,,/5/2', f'hits@7:{HITS},,,,/5/2', f'orders@7:{ORDERS},,,{ORDERS},/5/2'])
        self.assertEqual(g.receipt, f'INPUTS_1|7|1:pilot+hits+orders,2:pilot,3:-,4:orders,5:refused_channel|pilot:2/56|-|hits:1/27|-|'
                                    f'orders:2/{len(ORDERS)}|PORDERS_1|7')
        self.assertIn('INPUT_MUX_REFUSED tick=7 slot=5 reason=channel', self.logs())

    def test_a_section_nobody_registered_refuses_that_blob_on_every_client(self):
        self.lua.execute(f"M.register('pilot','pilot',section('pilot',{{}}));receipt=M.apply(3,{{'{VALID['pilot+hits'][0]}','{PILOT}'}},2,1)")
        g = self.lua.globals()
        self.assertEqual(list(g.seen.values()), [f'pilot@3:,{PILOT}/2/1'])
        self.assertTrue(g.receipt.startswith('INPUTS_1|3|1:refused_unregistered_hits,2:pilot|pilot:1/28|-'))
        with self.assertRaises(LuaError):
            self.lua.execute("M.apply(3,{'',''},2,1)")  # ticks must increase

    def test_proposal_packs_by_priority_within_the_budget_and_refuses_overflow(self):
        hits8 = ref.encode_hits([dict(CLAIM, projectile=f'{i + 1:016x}') for i in range(8)])
        hits3 = ref.encode_hits([dict(CLAIM, projectile=f'{i + 1:016x}') for i in range(3)])
        big_orders = '50' + 'ab' * 119
        self.lua.execute(f'''
          M.register('hits','hits',section('hits',{{[1]='{hits8}',[2]='{hits3}',[4]='{hits8}'}}))
          M.register('orders','orders',section('orders',{{[1]='{big_orders}',[2]='{big_orders}',[3]='{ORDERS}'}}))
          M.register('pilot','pilot',section('pilot',{{[1]='{PILOT}',[2]='{PILOT}',[3]='{PILOT}'}}))
          p1=M.proposal(1);p1b=M.proposal(1);p2=M.proposal(2);p3=M.proposal(3);p4=M.proposal(4)
        ''')
        g = self.lua.globals()
        # Tick 1: pilot 30 + orders 122 leave 84 bytes for hits: eight claims (211) are refused, not truncated.
        self.assertEqual(g.p1, container((0x01, PILOT), (0x50, big_orders)))
        self.assertEqual(g.p1b, g.p1)  # idempotent per tick: no second call to the sections
        self.assertEqual(list(g.refused.values()), ['hits@1:overflow'])
        self.assertIn('INPUT_MUX_OVERFLOW tick=1 section=hits bytes=209 budget=84', self.logs())
        self.assertEqual(g.p2, container((0x01, PILOT), (0x48, hits3), (0x50, big_orders)))
        self.assertEqual(g.p3, container((0x01, PILOT), (0x50, ORDERS)))
        self.assertEqual(g.p4, container((0x48, hits8)))
        seen = list(g.seen.values())
        self.assertEqual(seen[:3], ['pilot?1:472', 'orders?1:412', 'hits?1:168'])  # budgets in hex characters
        self.assertEqual(seen.count('pilot?1:472'), 1)
        for tick, proposals in ((1, {'pilot': PILOT, 'orders': big_orders, 'hits': hits8}), (2, {'pilot': PILOT, 'orders': big_orders, 'hits': hits3}),
                                (4, {'hits': hits8})):
            text, dropped = ref.pack(proposals)
            self.assertEqual(text, g['p%d' % tick])
            self.assertEqual(dropped, ['hits'] if tick == 1 else [])
        self.lua.execute("M.set_priority({'hits','pilot','orders'})")
        self.lua.execute(f"M.sections.hits.adapter=section('hits',{{[5]='{hits8}'}});M.sections.pilot.adapter=section('pilot',{{[5]='{PILOT}'}})")
        self.lua.execute(f"M.sections.orders.adapter=section('orders',{{[5]='{big_orders}'}});p5=M.proposal(5)")
        # Hits first: eight claims leave 25 bytes, so the pilot record (28) and the orders are refused.
        self.assertEqual(g.p5, container((0x48, hits8)))
        self.assertEqual(ref.pack({'pilot': PILOT, 'orders': big_orders, 'hits': hits8}, ('hits', 'pilot', 'orders')), (g.p5, ['pilot', 'orders']))

    def test_the_octree_report_is_offered_last_and_waits_for_room(self):
        big_orders = '50' + 'ab' * 119
        hits3 = ref.encode_hits([dict(CLAIM, projectile=f'{i + 1:016x}') for i in range(3)])
        self.lua.execute(f'''
          M.register('octree','octree',section('octree',{{[1]='{OCTREE}',[2]='{OCTREE}',[3]='{OCTREE}'}}))
          M.register('orders','orders',section('orders',{{[1]='{ORDERS}',[2]='{big_orders}'}}))
          M.register('pilot','pilot',section('pilot',{{[1]='{PILOT}',[2]='{PILOT}'}}))
          M.register('hits','hits',section('hits',{{[2]='{hits3}'}}))
          p1=M.proposal(1);p2=M.proposal(2);p3=M.proposal(3)
          receipt=M.apply(4,{{p1,p2,p3}},3,1)
        ''')
        g = self.lua.globals()
        # Wire order is kind order (hits 0x48 < octree 0x4f < orders 0x50); the report is offered after the others.
        self.assertEqual(g.p1, container((0x01, PILOT), (0x4f, OCTREE), (0x50, ORDERS)))
        self.assertEqual(g.p2, container((0x01, PILOT), (0x48, hits3), (0x50, big_orders)))  # full: the report waits a tick
        self.assertIn('INPUT_MUX_OVERFLOW tick=2 section=octree bytes=20 budget=3', self.logs())
        self.assertIn('octree@2:overflow', list(g.refused.values()))
        self.assertEqual(g.p3, container((0x4f, OCTREE)))
        seen = list(g.seen.values())
        self.assertEqual(seen[:4], ['pilot?1:472', 'orders?1:412', 'hits?1:' + str(2 * (240 - 2 - 30 - 2 - len(ORDERS) // 2 - 2)),
                                    'octree?1:' + str(2 * (240 - 2 - 30 - 2 - len(ORDERS) // 2 - 2))])
        self.assertIn(f'octree@4:{OCTREE},,{OCTREE}/3/1', seen)
        self.assertIn('|octree:2/40|', g.receipt)
        for tick, proposals in ((1, {'pilot': PILOT, 'orders': ORDERS, 'octree': OCTREE}),
                                (2, {'pilot': PILOT, 'orders': big_orders, 'hits': hits3, 'octree': OCTREE}), (3, {'octree': OCTREE})):
            text, dropped = ref.pack(proposals)
            self.assertEqual(text, g['p%d' % tick])
            self.assertEqual(dropped, ['octree'] if tick == 2 else [])
        # A priority list from before the section keeps working (the report goes last); one naming it is taken as is.
        self.lua.execute("M.set_priority({'hits','pilot','orders'});a=table.concat(M.priority,',')")
        self.lua.execute("M.set_priority({'octree','hits','pilot','orders'});b=table.concat(M.priority,',')")
        self.assertEqual((g.a, g.b), ('stock,ui,hits,pilot,orders,octree', 'stock,ui,octree,hits,pilot,orders'))
        # Stock may be listed too; unlisted it is offered first (it flies a slot).
        self.lua.execute("M.set_priority({'pilot','stock','orders','hits'});c=table.concat(M.priority,',')")
        self.assertEqual(g.c, 'pilot,stock,ui,orders,hits,octree')

    def test_registration_owner_tags_and_malformed_proposals_fail_closed(self):
        self.lua.execute("M.register('orders','orders',section('orders',{}))")
        for bad in ("M.register('orders','someone',section('orders',{}))", "M.register('chat','chat',section('chat',{}))",
                    "M.register('hits','Hits',section('hits',{}))", "M.register('hits','hits',{apply=1})",
                    "M.set_priority({'pilot','pilot','orders'})", "M.set_priority({'pilot','orders'})"):
            with self.subTest(bad=bad), self.assertRaises(LuaError):
                self.lua.execute(bad)
        self.lua.execute("M.register('orders','orders',section('orders',{[1]='4d01'}))")  # same owner replaces (UI reload)
        with self.assertRaises(LuaError):
            self.lua.execute('M.proposal(1)')  # an orders payload must start with 0x50
        self.assertEqual(self.lua.eval('M.names()'), 'orders/orders')


class HitCarrierTests(unittest.TestCase):
    def test_scripted_claims_ride_the_wire_and_fold_into_the_receipt(self):
        lua, K = lua_core()
        lua.execute('''
          logs={}
          A=X4LAN_INPUT_MUX_CORE.hit_carrier({slot=2,log=function(s) logs[#logs+1]=s end,script={[2]={
            [10]={{kind=2,back=1,projectile='00000000000000a1',target='0000000000176c95',point={0,0,8}},
                  {kind=1,back=0,projectile='00000000000000a2',target='0000000000176ad5',point={-8,16,0}}},
            [11]={{kind=2,back=8,projectile='00000000000000a3',target='0000000000176c95',point={1,2,3}}}}}})
          p9=A.proposal(9,472);p10=A.proposal(10,56);q10=A.queued();p11=A.proposal(11,472)
        ''')
        g = lua.globals()
        self.assertIsNone(g.p9)
        claims = ref.decode_hits(g.p10)  # budget 56 hex = 28 bytes: one claim, one stays queued
        self.assertEqual([(c['kind'], c['observe16'], c['projectile']) for c in claims], [(2, 9, '00000000000000a1')])
        self.assertEqual(g.q10, 1)
        claims = ref.decode_hits(g.p11)
        self.assertEqual([(c['kind'], c['observe16'], c['projectile']) for c in claims], [(1, 10, '00000000000000a2'), (2, 3, '00000000000000a3')])
        lua.execute(f"r=A.apply(13,{{'', '{g.p11}', '{'01' + CLAIM_HEX[:40] + '0080' + CLAIM_HEX[44:]}'}})")
        self.assertEqual(g.r, 'HITS_1|13|2/1/in/10/00000000000000a2/0000000000176ad5/-8,16,0/carried;'
                              '2/2/out/-/00000000000000a3/0000000000176c95/1,2,3/refused_far;3:refused_point')
        self.assertIn('HIT_CLAIM tick=13 2/1/in/10/00000000000000a2/0000000000176ad5/-8,16,0/carried', list(g.logs.values()))
        for bad in ("{[2]={[10]={{kind=2,back=9,projectile='00000000000000a1',target='0000000000176c95',point={0,0,8}}}}}",
                    "{[2]={[10]={{kind=3,projectile='00000000000000a1',target='0000000000176c95',point={0,0,8}}}}}",
                    "{[9]={[10]={}}}"):
            with self.subTest(script=bad), self.assertRaises(LuaError):
                lua.execute('X4LAN_INPUT_MUX_CORE.hit_carrier({slot=2,script=' + bad + '})')


S1, S2, S3 = '000000000000a001', '000000000000a002', '000000000000a003'


class ProbeClient:
    """One synthetic client: the real probe, input multiplexer, orders core/glue and hit carrier under
    LuaJIT; a mocked gate with wire piloting (protocol 3, three clients)."""

    def __init__(self, slot, octree=False):
        self.slot = slot
        lua = self.lua = LuaRuntime(unpack_returned_tuples=True)
        cfg = {'native_gate_test': True, 'test_profile': '45841', 'slot': f'p{slot}', 'native_gate_hash': 'clock',
               'native_gate_snapshot_sha': 'a' * 64, 'port': 12345, 'token': 'c' * 32, 'native_gate_clients': 3,
               'native_gate_serial_pathing': True, 'native_gate_world_identities': True, 'native_gate_pilot_inputs': True,
               'native_gate_pilot_slots': [S1, S2, S3], 'native_gate_pilot_source': 'wire', 'native_gate_pilot_delay': 3,
               'native_gate_pilot_suppress_local': True,
               'native_gate_player_orders': True, 'native_gate_player_orders_owners': {S1: 1, S2: 2, S3: 3},
               'native_gate_player_orders_players': 3,
               'native_gate_player_orders_script': {2: {2: [{'op': 'hold', 'subject': S2}]}, 1: {3: [{'op': 'hold', 'subject': S2}]}},
               'native_gate_hit_claims': 'carry'}
        if octree:
            cfg['native_gate_nav_octree'] = 'agreed'
        lua.execute('X4LAN_CONFIG=' + json_to_lua(cfg))
        lua.execute('''X4LAN_CONFIG.native_gate_hit_claims_script={
          [2]={[2]={{kind=2,back=0,projectile='00000000000000b1',target='000000000000a003',point={0,0,8}}}},
          [3]={[3]={{kind=1,back=1,projectile='00000000000000b2',target='000000000000a003',point={0,4,0}}}}}''')
        lua.execute('''
          hashes={};logs={};proposed={};staged={};events={};loads=0;reports=0;is_menu=true
          state={active=1,boot_phase=0,issued=0,report_tick=0,rng='0'}
          X4LAN_SHA256={digest=function(data) hashes[#hashes+1]=data;return string.rep('b',64) end}
          local ids={['000000000000a001']='11',['000000000000a002']='22',['000000000000a003']='33'}
          local saved={};for s,r in pairs(ids) do saved[r]=s end
          gate={boot=function() return 0 end,poll=function() return state end,
            load_requested=function() state.boot_phase=1;return 1 end,
            report=function(tick) reports=reports+1;state.report_tick=tick;return 1 end,stop=function() state.active=0;return 1 end,
            wire=function(version,clients) return (version==3 and clients==3) and 1 or 0 end,
            propose=function(tick,blob) assert(tick==state.issued);proposed[tick]=blob;return 1 end,
            saved_identity=function(r) return saved[r] or string.rep('0',16) end,
            pilot_inputs=function(manifest,source,delay) assert(source=='wire' and delay==3);return 1 end,
            pilot_inputs_status=function() return 'armed=1' end,
            pilot_inputs_frame=function() return 'PILOT_1:'..state.issued end,
            pilot_apply=function(slot,record) staged[#staged+1]=state.issued..'/'..slot..':'..record;return 1 end,
            pilot_capture=function() return string.format('01%02x%04x',SLOT,state.issued)..string.rep('0',48) end}
          package.loadlib=function(path) assert(not path:find('fixedtick'));return function() return gate end end
          local factions={'player'}
          local ffi={cdef=function() end,typeof=function() return true end,string=function(s) return s end,new=function() return {} end,
            C={GetSaveFolderPath=function() return 'fake/45841/save' end,GetCurrentGameTime=function() return 7 end,
              IsStartmenu=function() return is_menu end,ReloadSaveList=function() end,IsSaveListLoadingComplete=function() return true end,
              GetNumAllFactions=function() return 1 end,GetAllFactions=function(buf) buf[0]='player';return 1 end,
              GetNumAllFactionShips=function() return 3 end,
              GetAllFactionShips=function(buf) buf[0]=11;buf[1]=22;buf[2]=33;return 3 end,
              GetNumAllFactionStations=function() return 0 end,GetAllFactionStations=function() return 0 end,
              IsComponentClass=function() return true end}}
          require=function(name) assert(name=='ffi');return ffi end
          DebugError=function(s) logs[#logs+1]=s end
          RegisterEvent=function() end
          AddUITriggeredEvent=function(screen,control,args) events[#events+1]=screen..'/'..control..'/'..tostring(args.head) end
          ConvertStringToLuaID=function(s) return 'L'..s end
          ConvertIDTo64Bit=function(x) return tonumber((tostring(x):gsub('^L',''))) end
          GetClusters=function() return {} end
          SetScript=function(name,fn) update=fn end
          GetSaveList=function() return {{filename='save_009'}} end
          LoadGame=function() loads=loads+1 end
        ''')
        lua.globals().SLOT = slot
        # Nav octree 'agreed' (mocked gate): this client reports W = its slot + tick (little endian u32, empty bitmap);
        # the agreement returns a receipt of exactly what it was handed, so equal receipts mean equal admitted reports.
        lua.execute('''
          octree_agreed={};octree_armed=nil
          gate.nav_octree=function(mode,delay) octree_armed=mode..':'..tostring(delay);return 1 end
          gate.nav_octree_status=function() return 'mode=agreed installed=1' end
          gate.nav_octree_row=function() return nil end
          gate.nav_octree_report=function() return string.format('%02x000000',SLOT+state.issued)..string.rep('0',32) end
          gate.nav_octree_agree=function(tick,n,payloads) octree_agreed[#octree_agreed+1]=tick..'/'..n..'/'..payloads
            return 'OCT_1|'..tick..'|'..payloads end
        ''')
        for source in (MUX, ORDERS_CORE, ORDERS_GLUE, PROBE):
            lua.execute(source)
        lua.execute('update();update();update()')                       # arms before loading
        lua.execute('state.boot_phase=3;is_menu=false;state.wire_version=3;state.clients=3')
        for source in (MUX, ORDERS_CORE, ORDERS_GLUE, PROBE):            # the UI reloads with the game
            lua.execute(source)

    def tick(self, tick, inputs):
        self.lua.execute(f'state.issued={tick};state.inputs={json_to_lua(inputs)};update()')
        return self.lua.globals().proposed[tick]

    def values(self, name):
        return list(self.lua.globals()[name].values())


class ProbeThreeClientTests(unittest.TestCase):
    def test_nav_octree_reports_ride_the_blob_between_hits_and_orders_and_agree_identically(self):
        clients = [ProbeClient(k, octree=True) for k in (1, 2, 3)]
        inputs, blobs = ['', '', ''], {}
        for tick in range(1, 6):
            blobs[tick] = [c.tick(tick, inputs) for c in clients]
            inputs = blobs[tick]
        for c in clients:
            logs = c.values('logs')
            self.assertFalse(any('ERROR' in line for line in logs), [line for line in logs if 'ERROR' in line])
            self.assertTrue(any('INPUT_SECTION ARMED section=octree owner=octree' in line for line in logs))
            self.assertTrue(any(line.endswith('NAV_OCTREE_ARMED mode=agreed') for line in logs))
            self.assertEqual(c.lua.globals().octree_armed, 'agreed:0')
        # Slot 2's tick-2 blob: pilot, hits, the octree report, orders, in kind order.
        self.assertEqual(list(ref.decode(blobs[2][1])), ['pilot', 'hits', 'octree', 'orders'])
        self.assertEqual(blobs[2][1][:4] + blobs[2][1][4:6], '4d0101')
        self.assertEqual(ref.decode(blobs[2][1])['octree'], '04000000' + '0' * 32)
        self.assertEqual(ref.decode(blobs[4][0]), {'pilot': '01010004' + '0' * 48, 'octree': '05000000' + '0' * 32})
        # Every client handed the gate the same admitted reports and folded the same receipt into its digest.
        agreed = [c.values('octree_agreed') for c in clients]
        self.assertTrue(all(a == agreed[0] for a in agreed))
        self.assertEqual(agreed[0][1], '2/3/' + ','.join(f'{k + 1:02x}000000' + '0' * 32 for k in (1, 2, 3)))
        hashes = [[h for h in c.values('hashes') if h.startswith('CLOCK_ONLY_1')] for c in clients]
        self.assertTrue(all(h == hashes[0] for h in hashes))
        self.assertIn('|octree:3/60|OCT_1|2|', hashes[0][1])


    def test_pilot_orders_and_hit_claims_share_one_blob_per_slot_on_three_clients(self):
        clients = [ProbeClient(k) for k in (1, 2, 3)]
        inputs, blobs = ['', '', ''], {}
        for tick in range(1, 8):
            blobs[tick] = [c.tick(tick, inputs) for c in clients]
            inputs = blobs[tick]
        for c in clients:
            logs = c.values('logs')
            self.assertFalse(any('ERROR' in line for line in logs), [line for line in logs if 'ERROR' in line])
            self.assertTrue(any('INPUT_SECTION ARMED section=pilot owner=pilot' in line for line in logs))
            self.assertTrue(any('INPUT_SECTION ARMED section=orders owner=orders' in line for line in logs))
            self.assertTrue(any('INPUT_SECTION ARMED section=hits owner=hits' in line for line in logs))
        # Tick 2: slot 2's blob holds all three sections; slot 3's tick 3 pilot + hits; the rest pilot alone.
        self.assertEqual(sorted(ref.decode(blobs[2][1])), ['hits', 'orders', 'pilot'])
        self.assertEqual(sorted(ref.decode(blobs[3][0])), ['orders', 'pilot'])
        self.assertEqual(sorted(ref.decode(blobs[3][2])), ['hits', 'pilot'])
        self.assertEqual(ref.decode(blobs[4][0]), {'pilot': '01010004' + '0' * 48})  # legacy pilot form
        # Every client hashed the same declared data every tick and staged every slot's record.
        hashes = [[h for h in c.values('hashes') if h.startswith('CLOCK_ONLY_1')] for c in clients]
        self.assertEqual(len(hashes[0]), 7)
        self.assertTrue(all(h == hashes[0] for h in hashes))
        self.assertIn('INPUTS_1|3|1:pilot,2:pilot+hits+orders,3:pilot|pilot:3/84|-|hits:1/27|HITS_1|3|2/1/out/2/00000000000000b1/'
                      '000000000000a003/0,0,8/carried|orders:1/', hashes[0][2])
        staged = [c.values('staged') for c in clients]
        self.assertTrue(all(s == staged[0] for s in staged))
        self.assertIn('3/2:01020002' + '0' * 48, staged[0])
        # Orders: slot 2 holds its own ship (applied at 2+3), slot 1 holds slot 2's ship (refused at 3+3).
        for c in clients:
            self.assertEqual(c.values('events'), [f'X4LAN_PlayerOrders/Apply/tick=5 slot=2 op=hold subject={S2} target=-'])
            self.assertTrue(any(f'PLAYER_ORDER_APPLIED tick=6 slot=1 op=hold subject={S2} target=- result=refused_not_owner' in line
                                for line in c.values('logs')))
            self.assertTrue(any('HIT_CLAIM tick=4 3/1/in/2/00000000000000b2/000000000000a003/0,4,0/carried' in line for line in c.values('logs')))


class BuilderTests(unittest.TestCase):
    BASE = '''X4LAN_CONFIG.enabled = false
X4LAN_CONFIG.native_gate_test = true
X4LAN_CONFIG.native_gate_hash = "battle"
X4LAN_CONFIG.slot = "@X4LANE_SLOT@"
X4LAN_CONFIG.native_gate_snapshot_sha = "%s"
X4LAN_CONFIG.native_gate_snapshot_fixture = {'0000000000176a13'}
X4LAN_CONFIG.native_gate_serial_pathing = true
X4LAN_CONFIG.native_gate_world_identities = true
X4LAN_CONFIG.native_gate_bullet_rng = true
X4LAN_CONFIG.native_gate_nav_rng = true
X4LAN_CONFIG.native_gate_agent_rng = true
X4LAN_CONFIG.native_gate_ship_rng = true
X4LAN_CONFIG.native_gate_clients = @X4LANE_CLIENTS@
''' % ('7' * 64)

    def make_base(self, root, extra='', files=('ui/native_gate_probe.lua',)):
        base = root / 'base'
        for d in ('ui', 'native', 'md'):
            (base / d).mkdir(parents=True, exist_ok=True)
        (base / 'ui/config.lua.append').write_text(self.BASE + extra)
        (base / 'ui/native_gate_probe.lua').write_text('-- probe\n')
        (base / 'native/x4lan_gate_64.dll').write_bytes(b'dll')
        (base / 'ui.xml.files').write_text('\n'.join(files) + '\n')
        return base

    def test_sandbox_keeps_a_scripted_orders_block_next_to_wire_piloting(self):
        orders = ('X4LAN_CONFIG.native_gate_player_orders = true\nX4LAN_CONFIG.native_gate_player_orders_players = 2\n'
                  'X4LAN_CONFIG.native_gate_player_orders_owners = {["00000000000ae79c"]=1}\n')
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            base = self.make_base(root, orders, ('ui/native_gate_player_orders_core.lua', 'ui/native_gate_player_orders.lua', 'ui/native_gate_probe.lua'))
            with contextlib.redirect_stdout(io.StringIO()):
                sandbox.main(['overlay', '--base', str(base), '--out', str(root / 'out')])
            text = (root / 'out/ui/config.lua.append').read_text()
            self.assertIn('X4LAN_CONFIG.native_gate_player_orders = true', text)
            self.assertIn("X4LAN_CONFIG.native_gate_pilot_source = 'wire'", text)
            self.assertEqual((root / 'out/ui.xml.files').read_text().split()[0], 'ui/native_gate_inputs_core.lua')
            self.assertTrue((root / 'out/ui/native_gate_inputs_core.lua').is_file())
            for extra, files, message in (
                    (orders + 'X4LAN_CONFIG.native_gate_player_orders_capture = true\n', None, 'capture'),
                    (orders.replace('players = 2', 'players = 3'), None, '"players": 2'),
                    (orders, ('ui/native_gate_probe.lua',), 'native_gate_player_orders.lua')):
                with self.subTest(message=message), tempfile.TemporaryDirectory() as e:
                    bad = self.make_base(Path(e), extra, files or ('ui/native_gate_player_orders.lua', 'ui/native_gate_probe.lua'))
                    with self.assertRaises(ValueError) as caught:
                        sandbox.build_overlay(sandbox.build_parser().parse_args(['overlay', '--base', str(bad), '--out', str(Path(e) / 'o')]))
                    self.assertIn(message, str(caught.exception))

    def test_main_builder_adds_the_multiplexer_to_protocol_3_overlays(self):
        builder = __import__('build_native_gate_overlay')
        from unittest import mock
        with tempfile.TemporaryDirectory() as d:
            dll = Path(d) / 'gate.dll'
            dll.write_bytes(b'dll')
            for extra, expected in (([], False), (['--session-clients'], True)):
                out = Path(d) / f'out{len(extra)}'
                with mock.patch('sys.argv', ['build', '--gate-dll', str(dll), '--mode', 'clock', '--fixedtick-dll', str(dll), '--out', str(out), *extra]):
                    builder.main()
                files = (out / 'ui.xml.files').read_text().split()
                self.assertEqual('ui/native_gate_inputs_core.lua' in files, expected)
                self.assertEqual((out / 'ui/native_gate_inputs_core.lua').is_file(), expected)
                if expected:
                    self.assertLess(files.index('ui/native_gate_inputs_core.lua'), files.index('ui/native_gate_probe.lua'))

    def test_players_builder_runs_orders_with_human_pilots(self):
        args = players.defaults(players.build_parser().parse_args(['config', '--players', '3', '--mode', 'factions', '--orders', 'demo']))
        lines, plan, orders = players.config_block(args)
        self.assertIn("X4LAN_CONFIG.native_gate_pilot_source = 'wire'", lines)
        self.assertIn('X4LAN_CONFIG.native_gate_player_orders = true', lines)
        self.assertEqual(json.loads(json.dumps(orders))['ownership'], 'faction')


if __name__ == '__main__':
    unittest.main()
