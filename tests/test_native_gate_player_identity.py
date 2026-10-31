"""Lockstep player identity: plan validation (Lua core vs Python reference), setup steps through the real
glue on three synthetic clients with distinct runtime IDs, MD structure, the probe hook, the orders slot
faction table and the N-player session builder.

LuaJIT runs the real Lua; MD is replaced by a small Python model of its results. Synthetic clients only:
this proves identical plans, steps, events and hashed summaries, not native X4 behaviour.
"""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

from lupa.luajit21 import LuaRuntime, LuaError

ROOT = Path(__file__).resolve().parents[1]
DEV = ROOT / 'tools/dev'
CORE = (DEV / 'native_gate_player_identity_core.lua').read_text(encoding='utf-8')
GLUE = (DEV / 'native_gate_player_identity.lua').read_text(encoding='utf-8')
ORDERS_CORE = (DEV / 'native_gate_player_orders_core.lua').read_text(encoding='utf-8')
ORDERS_GLUE = (DEV / 'native_gate_player_orders.lua').read_text(encoding='utf-8')
MUX = (DEV / 'native_gate_inputs_core.lua').read_text(encoding='utf-8')
PROBE = (DEV / 'native_gate_probe.lua').read_text(encoding='utf-8')
MD = DEV / 'native_gate_player_identity.xml'
sys.path.insert(0, str(DEV))


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


identity = load('player_identity_reference', DEV / 'native_gate_player_identity.py')
players = load('native_gate_players_builder', DEV / 'native_gate_players.py')
orders = load('player_orders_reference_identity', DEV / 'native_gate_player_orders.py')
try:
    from lxml import etree
except ImportError:
    etree = None
RE = Path(os.environ.get('X4_RE', ROOT.parent / 'x4-re'))
MD_XSD = RE / 'gamescripts/resolved/base/libraries/md.xsd'
L = identity.lua_literal

S1, S2, S3, S4 = identity.DEFAULT_SHIPS
SECTOR = '0000000000000729'
WORLD = {'player': [S1], 'argon': [S2, S3, S4, '0000000000176a13'], 'xenon': ['0000000000176c59']}
CREATE = {'macro': 'ship_arg_s_fighter_01_a_macro', 'seed': 4803, 'sector': 'player', 'x': 0, 'y': 100000, 'z': -600, 'yaw': 90}
XENON = '0000000000176c59'
STARTS = [{'object': S2, 'sector': SECTOR, 'x': 0, 'y': 30000, 'z': 0, 'yaw': 0},
          {'object': XENON, 'sector': SECTOR, 'x': -10000000, 'y': 30000, 'z': 1500, 'yaw': -180}]


def lua_core():
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(CORE)
    return lua, lua.globals().X4LAN_PLAYER_IDENTITY_CORE


def lua_validate(lua, K, plan):
    """(normalized plan, None) or (None, reason) from the Lua core."""
    result = K.validate(lua.eval(L(plan)) if not isinstance(plan, type(None)) else None)
    return result if isinstance(result, tuple) else (result, None)


def plans():
    out = []
    for n in (2, 3, 4):
        for mode in ('factions', 'coop'):
            out.append(identity.build_plan(n, mode))
    out.append(identity.build_plan(3, 'factions', relation='hostile', overrides={(1, 2): 'allied'}, occupied='pool', reputation='keep', tick=2))
    out.append(identity.build_plan(3, 'factions', creates={3: CREATE}, tick=29))
    out.append(identity.build_plan(2, 'coop', creates={2: dict(CREATE, sector=SECTOR, x=-10000000, yaw=-180)}))
    out.append(identity.build_plan(2, 'factions', starts=STARTS))
    out.append(identity.build_plan(2, 'coop', starts=[dict(STARTS[0], sector='cluster_01_sector001_macro')]))
    out.append(identity.build_plan(3, 'coop', creates={3: CREATE}, starts=[dict(STARTS[0], object=S1)]))
    return out


class PlanTests(unittest.TestCase):
    def test_lua_and_python_agree_on_canonical_text_digest_and_step_heads(self):
        lua, K = lua_core()
        for plan in plans():
            with self.subTest(plan=identity.canonical(identity.validate(plan))):
                P = identity.validate(plan)
                lua_plan, why = lua_validate(lua, K, plan)
                self.assertIsNone(why)
                text = K.canonical(lua_plan)
                self.assertEqual(text, identity.canonical(P))
                self.assertEqual(hashlib.sha256(text.encode()).hexdigest(), identity.digest(P))
                lua_steps = K.steps(lua_plan, identity.digest(P))
                heads = [lua_steps[i].head for i in range(1, len(lua_steps) + 1)]
                self.assertEqual(heads, [s['head'] for s in identity.steps(P)])
                self.assertEqual(lua_steps[1].step, 'begin')
                self.assertEqual(lua_steps[len(lua_steps)].step, 'end')

    def test_starts_extend_the_plan_text_and_add_one_step_each_after_ownership(self):
        P = identity.validate(identity.build_plan(2, 'factions'))
        self.assertNotIn('starts', identity.canonical(P))  # plans without starts keep their earlier digest
        P = identity.validate(identity.build_plan(2, 'factions', starts=STARTS))
        self.assertTrue(identity.canonical(P).endswith(
            f'|starts={S2}@{SECTOR}/0,30000,0/0,{XENON}@{SECTOR}/-10000000,30000,1500/-180'))
        steps = identity.steps(P)
        self.assertEqual([s['step'] for s in steps], ['begin', 'faction', 'ship', 'ship', 'start', 'start', 'relation', 'end'])
        self.assertEqual(steps[4]['head'], f'tick=20 ordinal=5 step=start slot=0 start=1 object={S2} sector={SECTOR} at=0,30000,0/0')

    def test_default_plans_follow_the_documented_slot_factions(self):
        P = identity.validate(identity.build_plan(4, 'factions'))
        self.assertEqual([r['faction'] for r in P['slots']], ['player', 'x4lan_player2', 'x4lan_player3', 'x4lan_player4'])
        self.assertEqual([r['ship'] for r in P['slots']], list(identity.DEFAULT_SHIPS))
        self.assertEqual(len(P['relations']), 6)
        P = identity.validate(identity.build_plan(3, 'factions', occupied='pool'))
        self.assertEqual([r['faction'] for r in P['slots']], ['x4lan_player1', 'x4lan_player2', 'x4lan_player3'])
        P = identity.validate(identity.build_plan(3, 'coop'))
        self.assertEqual({r['faction'] for r in P['slots']}, {'player'})
        self.assertEqual(P['relations'], [])
        # 3 factions: begin, 2 pool factions, 3 ships, 3 relations, end; coop: begin, ships, end.
        self.assertEqual(len(identity.steps(identity.validate(identity.build_plan(3, 'factions')))), 10)
        self.assertEqual(len(identity.steps(identity.validate(identity.build_plan(3, 'coop')))), 5)

    def test_invalid_plans_refuse_with_the_same_reason_in_lua_and_python(self):
        lua, K = lua_core()
        base = identity.build_plan(3, 'factions')
        coop = identity.build_plan(2, 'coop')

        def changed(plan, **fields):
            out = json.loads(json.dumps(plan))
            out.update(fields)
            return out

        def slot(plan, index, **fields):
            out = json.loads(json.dumps(plan))
            out['slots'][index].update(fields)
            for key in [k for k, v in fields.items() if v is None]:
                del out['slots'][index][key]
            return out

        bad = [(changed(base, version=2), 'version'), (changed(base, mode='pvp'), 'mode'), (changed(base, tick=1), 'tick'),
               (changed(base, tick=30), 'tick'), (changed(base, occupied='nobody'), 'occupied'), (changed(base, reputation='x'), 'reputation'),
               (changed(base, extra=1), 'key_unknown'), (changed(base, slots=[]), 'slots'),
               (changed(base, slots=base['slots'] * 6), 'slots'),
               (slot(base, 1, create=CREATE), 'slot_source'), (slot(base, 1, ship=None), 'slot_source'),
               (slot(base, 1, ship='0' * 16), 'ship_invalid'), (slot(base, 1, ship=S1.upper()), 'ship_invalid'),
               (slot(base, 1, ship=S1), 'ship_duplicate'), (slot(base, 1, faction='x4lan_player3'), 'faction_invalid'),
               (slot(base, 1, faction='player'), 'faction_duplicate'), (slot(base, 1, colour=1), 'key_unknown'),
               (slot(coop, 1, faction='x4lan_player2'), 'faction_mode'),
               (slot(base, 2, ship=None, create=dict(CREATE, macro='Ship')), 'macro_invalid'),
               (slot(base, 2, ship=None, create=dict(CREATE, seed=-1)), 'seed_invalid'),
               (slot(base, 2, ship=None, create=dict(CREATE, sector='zone')), 'sector_invalid'),
               (slot(base, 2, ship=None, create=dict(CREATE, z=10000001)), 'coordinate_invalid'),
               (slot(base, 2, ship=None, create=dict(CREATE, yaw=181)), 'rotation_invalid'),
               (slot(base, 2, ship=None, create=dict(CREATE, pilot=1)), 'key_unknown'),
               (changed(coop, relations=[{'a': 1, 'b': 2, 'relation': 'allied'}]), 'relations_mode'),
               (changed(base, relations=base['relations'][:2]), 'relation_missing'),
               (changed(base, relations=base['relations'][:2] + [{'a': 2, 'b': 2, 'relation': 'allied'}]), 'relation_pair'),
               (changed(base, relations=base['relations'][:2] + [{'a': 3, 'b': 4, 'relation': 'allied'}]), 'relation_pair'),
               (changed(base, relations=base['relations'][:2] + [{'a': 2, 'b': 3, 'relation': 'war'}]), 'relation_invalid'),
               (changed(base, relations=base['relations'][:2] + [base['relations'][0]]), 'relation_duplicate'),
               (changed(base, starts={'object': S2}), 'starts'), (changed(base, starts=STARTS * 9), 'starts'),
               (changed(base, starts=[dict(STARTS[0], pitch=1)]), 'key_unknown'),
               (changed(base, starts=[dict(STARTS[0], object='0' * 16)]), 'start_object_invalid'),
               (changed(base, starts=[STARTS[0], dict(STARTS[1], object=S2)]), 'start_duplicate'),
               (changed(base, starts=[dict(STARTS[0], sector='player')]), 'sector_invalid'),
               (changed(base, starts=[dict(STARTS[0], sector='Cluster_01_macro')]), 'sector_invalid'),
               (changed(base, starts=[dict(STARTS[0], y=10000001)]), 'coordinate_invalid'),
               (changed(base, starts=[dict(STARTS[0], yaw=-181)]), 'rotation_invalid')]
        for plan, reason in bad:
            with self.subTest(reason=reason):
                with self.assertRaises(identity.Refused) as caught:
                    identity.validate(plan)
                self.assertEqual(str(caught.exception), reason)
                self.assertEqual(lua_validate(lua, K, plan)[1], reason)
        self.assertEqual(lua_validate(lua, K, None)[1], 'plan_invalid')

    def test_result_rows_fail_closed_on_count_order_and_refusals(self):
        lua, K = lua_core()
        P = lua_validate(lua, K, identity.build_plan(2, 'coop'))[0]
        steps = K.steps(P, 'd')
        rows = ['1|applied|pool=16 occupied=true', '2|unchanged|owner=player', '3|applied|owner=player', '4|applied|player=player']
        text = K.results(steps, lua.table_from(rows))
        self.assertEqual(text, 'PIDENT_MD_1|' + ';'.join(rows))
        self.assertEqual(K.results(steps, lua.table_from(rows[:3]))[1], 'result_count:3/4')
        self.assertEqual(K.results(steps, lua.table_from([rows[1], rows[0]] + rows[2:]))[1], 'result_order:1')
        self.assertEqual(K.results(steps, lua.table_from(rows[:2] + ['3|refused_occupied_ship|-'] + rows[3:]))[1],
                         'refused:3:refused_occupied_ship')


class Client:
    """One synthetic client: real identity core/glue under LuaJIT, a mocked engine with process-local
    runtime IDs (different on every client) and a Python stand-in for the MD results."""

    def __init__(self, slot, plan, clients=3, cfg=None):
        self.slot = slot
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        lua = self.lua
        lua.execute('stream={};events={};handlers={};owners={};alive={};created={};sectors={};macros={}')
        config = {'slot': f'p{slot}', 'native_gate_player_identity': True, 'native_gate_world_identities': True}
        config.update(cfg or {})
        lua.execute('X4LAN_CONFIG=' + L(config))
        lua.execute('X4LAN_CONFIG.native_gate_player_identity_plan=' + L(plan))
        self.runtime = {saved: str(int(saved, 16) * 10 + slot) for saved in [*sum(WORLD.values(), []), SECTOR]}
        lua.execute('runtime_of=' + L(self.runtime))
        lua.execute('saved_of={};for s,r in pairs(runtime_of) do saved_of[r]=s end')
        lua.execute('world=' + L(WORLD) + ';sector_id="' + self.runtime[SECTOR] + '"')
        for faction, ships in WORLD.items():
            for saved in ships:
                lua.execute(f'owners["{self.runtime[saved]}"]="{faction}";alive["{self.runtime[saved]}"]=true')
        g = lua.globals()
        g.X4LAN_SHA256 = lua.table(digest=lambda text: hashlib.sha256(text.encode()).hexdigest())
        lua.execute('''
          DebugError=function(s) stream[#stream+1]=s end
          RegisterEvent=function(name,fn) handlers[name]=fn end
          AddUITriggeredEvent=function(screen,control,args)
            assert(screen=='X4LAN_PlayerIdentity' and control=='Step');events[#events+1]=args
          end
          ConvertStringToLuaID=function(s) assert(s:match('^%d+$'));return 'L'..s end
          ConvertIDTo64Bit=function(x) return tonumber((tostring(x):gsub('^L',''))) end
          ConvertStringTo64Bit=function(s) return tonumber(s) end
          GetComponentData=function(id,key)
            local runtime=(tostring(id):gsub('^L',''))
            if key=='owner' then return owners[runtime] elseif key=='sectorid' then return sectors[runtime]
            elseif key=='macro' then return macros[runtime] end
            error('unexpected component data '..key)
          end
          GetClusters=function() return {'Lcluster'} end
          GetSectors=function(cluster) return {'L'..sector_id} end
          local factions={};for f in pairs(world) do factions[#factions+1]=f end;table.sort(factions,function(a,b) return a>b end)
          local function fill(list,buf) for i,s in ipairs(list or {}) do buf[i-1]=tonumber(runtime_of[s]) end;return #(list or {}) end
          local ffi={cdef=function() end,typeof=function() return true end,new=function() return {} end,string=function(s) return s end,
            C={GetNumAllFactions=function() return #factions end,
               GetAllFactions=function(buf,n) for i,f in ipairs(factions) do buf[i-1]=f end;return #factions end,
               GetNumAllFactionShips=function(f) return #(world[f] or {}) end,
               GetAllFactionShips=function(buf,n,f) return fill(world[f],buf) end,
               IsComponentOperational=function(id) return alive[string.format('%.0f',id)]==true end}}
          require=function(name) assert(name=='ffi');return ffi end
          gate={saved_identity=function(runtime) assert(type(runtime)=='string' and runtime:match('^%d+$'));return saved_of[runtime] or string.rep('0',16) end,
                created_identity=function(runtime) return created[runtime] or string.rep('0',16) end}
        ''')
        lua.execute(CORE)
        lua.execute(GLUE)
        self.adapter = g.X4LAN_NATIVE_PLAYER_IDENTITY
        self.adapter.bind(g.gate, clients)
        self.seen = 0
        self.move = True

    def frame(self, tick):
        return self.adapter.frame(tick)

    def md(self, refuse=None):
        """Apply the raised steps like the MD script would: owners, created ships, one result row each."""
        g = self.lua.globals()
        events = list(g.events.values())
        for a in events[self.seen:]:
            ordinal = int(a.ordinal)
            result, detail = 'applied', '-'
            if a.step == 'begin':
                detail = 'pool=16 occupied=true'
            elif a.step == 'faction':
                detail = 'known=true active=true mirrored=3 xenon=-1 argon=0'
            elif a.step == 'ship':
                if a.source == 'create':
                    runtime = str(900000 + int(a.slotname) * 10 + self.slot)
                    self.lua.execute(f'owners["{runtime}"]="{a.faction}";alive["{runtime}"]=true')
                    g.handlers['X4LAN_PlayerIdentity.Ship' + a.slotname](None, 'L' + runtime)
                else:
                    runtime = str(a.object)[1:]
                    if g.owners[runtime] == a.faction:
                        result = 'unchanged'
                    self.lua.execute(f'owners["{runtime}"]="{a.faction}"')
                detail = f'owner={a.faction}'
            elif a.step == 'start' and self.move:
                # MD warps the ship; a macro sector stands for a sector object of that macro.
                runtime = str(a.object)[1:]
                if a.hassector == 1:
                    self.lua.execute(f'sectors["{runtime}"]="{a.sector}"')
                else:
                    self.lua.execute(f'sectors["{runtime}"]="Lsector_{a.sectormacro}";macros["sector_{a.sectormacro}"]="{a.sectormacro}"')
            elif a.step == 'relation':
                detail = f'ab={a.value} ba={a.value}'
            if refuse == ordinal:
                result = 'refused_ship_missing'
            self.lua.execute('stream[#stream+1]=...', f'[X4LAN] PLAYER_IDENTITY_APPLIED {a.head} result={result} {detail} time=21')
            g.handlers['X4LAN_PlayerIdentity.Result'](None, f'{a.ordname}|{result}|{detail}')
        self.seen = len(events)
        return events

    def log(self):
        return '\n'.join(self.lua.globals().stream.values())


def run(clients, ticks, md_tick=None, refuse=None):
    """Every client: frame(t); MD answers in the frame after the setup tick. Returns per-tick sets of states."""
    md_tick = md_tick or {}
    states = []
    for tick in range(1, ticks + 1):
        states.append({c.frame(tick) for c in clients})
        for c in clients:
            if md_tick.get(c.slot, None) == tick or (c.slot not in md_tick and tick == c.adapter.plan.tick + 1):
                c.md(refuse)
    return states


class GlueTests(unittest.TestCase):
    def test_three_clients_raise_identical_steps_and_hash_identical_state(self):
        plan = identity.build_plan(3, 'factions', relation='hostile')
        P = identity.validate(plan)
        clients = [Client(k, plan) for k in (1, 2, 3)]
        # MD answers arrive at different ticks per client (UI event delivery may lag); the fold tick is fixed.
        states = run(clients, 35, md_tick={1: 21, 2: 22, 3: 29})
        self.assertTrue(all(len(s) == 1 for s in states), [s for s in states if len(s) != 1])
        digest = identity.digest(P)
        self.assertEqual(states[0], {f'PIDENT_1:{digest}:pending'})
        self.assertEqual(states[19], {f'PIDENT_1:{digest}:setup:1/{S1},2/{S2},3/{S3}'})
        self.assertEqual(states[20], {f'PIDENT_1:{digest}:applying'})
        final = states[34].pop()
        self.assertTrue(final.endswith(f':1/{S1}/player/player,2/{S2}/x4lan_player2/x4lan_player2,3/{S3}/x4lan_player3/x4lan_player3'), final)
        heads = [s['head'] for s in identity.steps(P)]
        for client in clients:
            events = list(client.lua.globals().events.values())
            self.assertEqual([e.head for e in events], heads)
            ship = [e for e in events if e.step == 'ship']
            # Each client hands MD its own process-local runtime IDs; nothing runtime-local is in a head.
            self.assertEqual([e.object for e in ship], ['L' + client.runtime[s] for s in (S1, S2, S3)])
            self.assertEqual([e.faction for e in ship], ['player', 'x4lan_player2', 'x4lan_player3'])
            relation = [e for e in events if e.step == 'relation']
            self.assertEqual([(e.fa, e.fb, e.value) for e in relation],
                             [('player', 'x4lan_player2', -0.5), ('player', 'x4lan_player3', -0.5), ('x4lan_player2', 'x4lan_player3', -0.5)])
            faction = [e for e in events if e.step == 'faction']
            self.assertEqual([(e.faction, e.name, e.short, e.mirror) for e in faction],
                             [('x4lan_player2', 'Player 2', 'P2', 1), ('x4lan_player3', 'Player 3', 'P3', 1)])
            log = client.log()
            self.assertIn(f'PLAYER_IDENTITY ARMED slot=p{client.slot} mode=factions slots=3 tick=20 steps=10 occupied=player reputation=mirror plan={digest}', log)
            self.assertIn('PLAYER_IDENTITY READY tick=30 steps=10 md=', log)
            for runtime in client.runtime.values():
                self.assertNotIn(runtime, ' '.join(heads))
        ok, details = identity.compare([c.log() for c in clients], plan)
        self.assertTrue(ok, details)

    def test_created_slot_ship_reports_its_object_and_created_identity(self):
        plan = identity.build_plan(3, 'coop', creates={3: CREATE})
        clients = [Client(k, plan) for k in (1, 2, 3)]
        states = run(clients, 31)
        self.assertEqual(len(states[29]), 1)
        self.assertTrue(states[29].pop().endswith(',3/created3/player/player'))
        for client in clients:  # the gate assigns the same created ID on every client from tick 31
            client.lua.execute(f'created["{900030 + client.slot}"]="80000000000a0001"')
        self.assertEqual(len({c.frame(32) for c in clients}), 1)
        self.assertTrue(clients[0].frame(33).endswith(',3/80000000000a0001/player/player'))
        create = [e for e in clients[0].lua.globals().events.values() if e.source == 'create'][0]
        self.assertEqual((create.macro, create.seed, create.x, create.y, create.z, create.yaw, create.hassector),
                         ('ship_arg_s_fighter_01_a_macro', 4803, 0, 100000, -600, 90, 0))
        self.assertIn('PLAYER_IDENTITY_STATE tick=30 1/', clients[0].log())

    def test_start_steps_hand_md_each_clients_own_ship_and_sector_objects(self):
        plan = identity.build_plan(2, 'factions', starts=STARTS)
        clients = [Client(k, plan, clients=2) for k in (1, 2)]
        states = run(clients, 31)
        self.assertTrue(all(len(s) == 1 for s in states), [s for s in states if len(s) != 1])
        for client in clients:
            starts = [e for e in client.lua.globals().events.values() if e.step == 'start']
            self.assertEqual([(e.object, e.sector, e.x, e.y, e.z, e.yaw) for e in starts],
                             [('L' + client.runtime[S2], 'L' + client.runtime[SECTOR], 0, 30000, 0, 0),
                              ('L' + client.runtime[XENON], 'L' + client.runtime[SECTOR], -10000000, 30000, 1500, -180)])
        ok, details = identity.compare([c.log() for c in clients], plan)
        self.assertTrue(ok, details)
        self.assertIn(f'PLAYER_IDENTITY START tick=30 start=2 object={XENON} sector={SECTOR}', clients[0].log())
        # A ship that did not move fails closed at the fold tick.
        client = Client(1, plan, clients=2)
        client.move = False
        with self.assertRaises(LuaError) as caught:
            run([client], 30)
        self.assertIn(f'start 1 ship {S2} is in none, not {SECTOR}', str(caught.exception))
        client = Client(1, identity.build_plan(2, 'factions', starts=[dict(STARTS[0], sector='0000000000000451')]), clients=2)
        with self.assertRaises(LuaError) as caught:
            run([client], 20)
        self.assertIn('unmapped slot sector 0000000000000451', str(caught.exception))
        # A sector macro goes to MD find_sector as a name (live st-S1: the Lua scan sees known clusters only).
        clients = [Client(k, identity.build_plan(2, 'coop', starts=[dict(STARTS[0], sector='cluster_01_sector001_macro')]), clients=2)
                   for k in (1, 2)]
        run(clients, 31)
        for client in clients:
            start = [e for e in client.lua.globals().events.values() if e.step == 'start'][0]
            self.assertEqual((start.object, start.sectormacro, start.hassector, start.sector),
                             ('L' + client.runtime[S2], 'cluster_01_sector001_macro', 0, None))
            self.assertIn('PLAYER_IDENTITY START tick=30 start=1 object=' + S2 + ' sector=cluster_01_sector001_macro', client.log())

    def test_a_dead_slot_ship_and_a_divergent_owner_change_the_hashed_state(self):
        plan = identity.build_plan(2, 'coop')
        clients = [Client(k, plan, clients=2) for k in (1, 2)]
        run(clients, 30)
        clients[1].lua.execute(f'owners["{clients[1].runtime[S2]}"]="argon"')
        self.assertEqual(len({c.frame(31) for c in clients}), 2)
        clients[0].lua.execute(f'alive["{clients[0].runtime[S2]}"]=false')
        self.assertTrue(clients[0].frame(32).endswith(f'2/{S2}/dead/player'))

    def test_missing_late_refused_or_unmapped_results_fail_closed(self):
        plan = identity.build_plan(2, 'factions')
        cases = {'missing': dict(md_tick={1: 99}), 'refused': dict(refuse=3), 'late': dict(md_tick={1: 30})}
        for name, kw in cases.items():
            with self.subTest(case=name):
                client = Client(1, plan, clients=2)
                with self.assertRaises(LuaError) as caught:
                    run([client], 31, **kw)
                self.assertIn('player identity', str(caught.exception))
        client = Client(1, plan, clients=2)
        client.lua.execute(f'saved_of["{client.runtime[S2]}"]=nil')
        with self.assertRaises(LuaError) as caught:
            run([client], 20)
        self.assertIn('unmapped slot ship ' + S2, str(caught.exception))
        client = Client(1, plan, clients=2)
        run([client], 19)
        with self.assertRaises(LuaError) as caught:
            client.frame(21)
        self.assertIn('setup tick 20 was not admitted', str(caught.exception))
        with self.assertRaises(LuaError):
            client.frame(21)  # ticks must increase

    def test_plan_digest_waits_for_the_base_mods_sha256(self):
        # Live pf3: the base mod's SHA-256 loads after the overlay adapters, so the digest is taken at the
        # first gated tick, never at bind.
        plan = identity.build_plan(2, 'coop')
        client = Client(1, plan, clients=2)
        g = client.lua.globals()
        sha = g.X4LAN_SHA256
        g.X4LAN_SHA256 = None
        with self.assertRaises(LuaError) as caught:
            client.frame(1)
        self.assertIn('needs X4LAN_SHA256', str(caught.exception))
        client = Client(1, plan, clients=2)
        self.assertIn('PLAYER_IDENTITY BOUND slot=p1 mode=coop slots=2 tick=20', client.log())
        self.assertNotIn('ARMED', client.log())
        client.frame(1)
        self.assertIn('plan=' + identity.digest(identity.validate(plan)), client.log())
        self.assertIsNotNone(sha)

    def test_flag_off_is_inert_and_bad_configuration_refuses_before_arming(self):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute("X4LAN_CONFIG={slot='p1'}")
        lua.execute(CORE)
        lua.execute(GLUE)
        self.assertIsNone(lua.globals().X4LAN_NATIVE_PLAYER_IDENTITY)
        plan = identity.build_plan(3, 'factions')
        for cfg in ({'native_gate_world_identities': False}, {'player_factions': True}):
            with self.subTest(cfg=cfg), self.assertRaises(LuaError):
                Client(1, plan, cfg=cfg)
        with self.assertRaises(LuaError):
            Client(1, plan, clients=2)  # three slots for two clients
        broken = json.loads(json.dumps(plan))
        broken['tick'] = 40
        with self.assertRaises(LuaError) as caught:
            Client(1, broken)
        self.assertIn('invalid player identity plan: tick', str(caught.exception))

    def test_order_block_refuses_stock_order_entry_points_locally(self):
        plan = identity.build_plan(2, 'coop')
        client = Client(1, plan, clients=2, cfg={'native_gate_player_identity_block_orders': True})
        client.lua.execute('''
          calls={}
          local function original(name) return function() calls[#calls+1]=name;return 7 end end
          interact={name='InteractMenu'};map={name='MapMenu'}
          for _,n in ipairs({'orderAttack','orderMoveWait','buttonRemoveAllOrders','buttonInfo'}) do interact[n]=original(n) end
          for _,n in ipairs({'orderFollow','buttonConfirmTradeLoop','buttonShowInfo'}) do map[n]=original(n) end
          Menus={interact,map}
        ''')
        client.frame(1)
        client.lua.execute("interact.orderAttack();interact.buttonRemoveAllOrders();interact.buttonInfo();map.orderFollow();map.buttonConfirmTradeLoop();map.buttonShowInfo()")
        self.assertEqual(list(client.lua.globals().calls.values()), ['buttonInfo', 'buttonShowInfo'])
        log = client.log()
        self.assertIn('PLAYER_IDENTITY ORDER_BLOCK menu=InteractMenu blocked=3', log)
        self.assertIn('PLAYER_IDENTITY_ORDER_BLOCKED menu=MapMenu entry=buttonConfirmTradeLoop', log)
        self.assertEqual(client.adapter.state.blocked(), 4)


class MdScriptTests(unittest.TestCase):
    def test_md_handles_every_step_and_writes_one_line_and_one_result_per_step(self):
        text = MD.read_text(encoding='utf-8')
        root = ET.fromstring(text)
        self.assertEqual(root.get('name'), 'X4LAN_PlayerIdentity')
        self.assertEqual([c.get('name') for c in root.iter('cue')], ['State', 'Step'])
        for step in ('begin', 'faction', 'ship', 'start', 'relation', 'end'):
            self.assertIn(f"$a.$step == '{step}'", text)
        # Start placements: a fixed point (no safe-position search), read back by sector.
        self.assertEqual([w.get('object') for w in root.iter('warp')], ['$ship'])
        self.assertEqual([f.get('macro') for f in root.iter('find_sector')], ['macro.{$a.$sectormacro}'])
        self.assertIsNone(root.find('.//find_closest_safe_pos'))
        self.assertNotIn('refused_start_readback', text)  # the move applies after the block: Lua checks it
        self.assertEqual(text.count('<debug_text '), 1)
        self.assertIn("'[X4LAN] PLAYER_IDENTITY_APPLIED %s result=%s %s time=%s'", text)
        events = [e.get('name') for e in root.iter('raise_lua_event')]
        self.assertEqual(events, ["'X4LAN_PlayerIdentity.Ship' + $a.$slotname", "'X4LAN_PlayerIdentity.Result'"])
        # The occupied ship never gets overridenpc (it would re-own the player entity).
        owners = list(root.iter('set_owner'))
        self.assertEqual([o.get('overridenpc') for o in owners], [None, 'true'])
        self.assertIn('$ship == player.occupiedship', text)
        # Faction ids only through lookup.faction: no static reference to a pool faction.
        self.assertNotIn('faction.x4lan', text)
        results = set(re.findall(r"'(refused_[a-z_]+|applied|unchanged)'", text))
        self.assertTrue({'applied', 'unchanged', 'refused_md_sequence', 'refused_occupied_ship', 'refused_pool_missing'} <= results)

    # Compiling the game schema takes about two minutes: opt in with X4LAN_MD_SCHEMA=1 (lxml + x4-re).
    @unittest.skipUnless(os.environ.get('X4LAN_MD_SCHEMA') == '1' and etree is not None and MD_XSD.is_file(),
                         'set X4LAN_MD_SCHEMA=1 with lxml and the read-only x4-re md.xsd')
    def test_md_validates_against_game_schema(self):
        schema = etree.XMLSchema(etree.parse(str(MD_XSD)))
        self.assertTrue(schema.validate(etree.parse(str(MD))), schema.error_log)


class ProbeHookTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute('''
          X4LAN_CONFIG={native_gate_test=true,test_profile='45841',slot='p2',native_gate_hash='battle',
            native_gate_snapshot_sha=string.rep('a',64),port=12345,token=string.rep('c',32),native_gate_player_identity=true,
            native_gate_clients=3}
          X4LAN_SHA256={digest=function(data) hashed=data;return string.rep('b',64) end}
          logs={};reports=0;frames={};ui={}
          state={active=1,boot_phase=3,issued=1,report_tick=0,rng='0',wire_version=3,clients=3,inputs={'','',''}}
          gate={boot=function() return 0 end,poll=function() return state end,
            report=function(tick) reports=reports+1;state.report_tick=tick;return 1 end,
            propose=function() return 1 end,saved_identity=function() return string.rep('0',16) end,
            wire=function(version,clients) return (version==3 and clients==3) and 1 or 0 end,stop=function() state.active=0;return 1 end}
          package.loadlib=function(path) assert(not path:find('fixedtick'));return function() return gate end end
          local ffi={cdef=function() end,typeof=function() return true end,string=function(s) return s end,
            C={GetSaveFolderPath=function() return 'fake/45841/save' end,GetCurrentGameTime=function() return 7 end,
              IsStartmenu=function() return false end,ReloadSaveList=function() end,IsSaveListLoadingComplete=function() return true end}}
          require=function(name) assert(name=='ffi');return ffi end
          DebugError=function(s) logs[#logs+1]=s end
          RegisterEvent=function() end
          AddUITriggeredEvent=function(screen,control) ui[#ui+1]=screen..'/'..control end
          SetScript=function(name,fn) update=fn end
          X4LAN_NATIVE_PLAYER_IDENTITY={bind=function(g,clients) assert(g==gate);bound=clients end,
            frame=function(tick) frames[#frames+1]=tick;ui[#ui+1]='identity';return 'PIDENT_1:test:'..tick end}
        ''')

    def test_probe_binds_with_the_client_count_and_hashes_the_identity_state_after_fixture_events(self):
        self.lua.execute(PROBE)
        self.lua.execute('update()')
        g = self.lua.globals()
        self.assertFalse(any('ERROR' in line for line in g.logs.values()), list(g.logs.values())[-3:])
        self.assertEqual(g.bound, 3)
        self.assertEqual(list(g.frames.values()), [1])
        self.assertEqual(list(g.ui.values()), ['X4LAN_NativeGate/RestoreActors', 'identity'])
        self.assertIn('|PIDENT_1:test:1', g.hashed)
        self.assertEqual(g.reports, 1)

    def test_probe_refuses_identity_without_battle_snapshot_or_adapter(self):
        for change in ("X4LAN_CONFIG.native_gate_hash='clock'", 'X4LAN_NATIVE_PLAYER_IDENTITY=nil', 'X4LAN_CONFIG.native_gate_snapshot_sha=nil'):
            with self.subTest(change=change):
                self.setUp()
                self.lua.execute(change)
                with self.assertRaises(LuaError):
                    self.lua.execute(PROBE)


class OrdersFactionTests(unittest.TestCase):
    def test_slot_faction_list_reaches_md_and_validates(self):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute(ORDERS_CORE)
        K = lua.globals().X4LAN_PLAYER_ORDERS_CORE
        S = K.session(lua.eval('{ownership="faction",players=3,factions={"player","x4lan_player2","x4lan_player3"}}'))
        self.assertEqual((K.faction(S, 1), K.faction(S, 3)), ('player', 'x4lan_player3'))
        self.assertTrue(S.owner_rows.endswith(';factions=player,x4lan_player2,x4lan_player3'))
        self.assertEqual(K.faction(K.session(lua.eval('{ownership="faction"}')), 2), 'x4lan_player2')
        self.assertEqual(K.session(lua.eval('{owners={["000000000000a001"]=1}}')).owner_rows, '000000000000a001=1')
        for bad in ('{"x4lan_player2"}', '{"player","player"}', '{"player","x4lan_player3"}', '{}'):
            with self.subTest(bad=bad), self.assertRaises(LuaError):
                K.session(lua.eval('{ownership="faction",factions=' + bad + '}'))
        ref = lambda text: text  # noqa: E731
        plan = {'players': 2, 'ownership': 'faction', 'factions': ['player', 'x4lan_player2']}
        self.assertIn('X4LAN_CONFIG.native_gate_player_orders_factions = {"player","x4lan_player2"}', orders.overlay_config(plan, ref))
        for bad in (dict(plan, ownership='table', owners={S1: 1}), dict(plan, factions=['x4lan_player1', 'x4lan_player1']),
                    dict(plan, factions=['player', 'player2']), dict(plan, factions=['player', 'x4lan_player2', 'x4lan_player3'])):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                orders.overlay_config(bad, ref)


BASE_CONFIG = '''X4LAN_CONFIG.enabled = false
X4LAN_CONFIG.native_gate_test = true
X4LAN_CONFIG.native_gate_hash = "battle"
X4LAN_CONFIG.slot = "@X4LANE_SLOT@"
X4LAN_CONFIG.native_gate_snapshot_sha = "%s"
X4LAN_CONFIG.native_gate_snapshot_fixture = {'0000000000176a13'}
X4LAN_CONFIG.native_gate_serial_pathing = true
X4LAN_CONFIG.native_gate_world_identities = true
X4LAN_CONFIG.native_gate_world_hash_stride = 60
X4LAN_CONFIG.native_gate_world_hash_schema = 'WORLD_IMPORTED_2'
X4LAN_CONFIG.native_gate_bullet_rng = true
X4LAN_CONFIG.native_gate_nav_rng = true
X4LAN_CONFIG.native_gate_agent_rng = true
X4LAN_CONFIG.native_gate_ship_rng = true
X4LAN_CONFIG.native_gate_script_rng_rows = true
X4LAN_CONFIG.native_gate_clients = @X4LANE_CLIENTS@
''' % ('7' * 64)


def run_main(*argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        code = players.main(list(argv))
    return code, out.getvalue()


class PlayersBuilderTests(unittest.TestCase):
    def block(self, *argv):
        args = players.defaults(players.build_parser().parse_args(['config', *argv]))
        return players.config_block(args)

    def test_script_factions_block_pilots_every_slot_and_checks_ownership_by_faction(self):
        lines, plan, orders_plan = self.block('--players', '3', '--mode', 'factions', '--source', 'script')
        text = '\n'.join(lines)
        self.assertIn("native_gate_pilot_slots = {'00000000000ae79c','0000000000176ad5','0000000000176b36'}", text)
        self.assertIn("native_gate_pilot_source = 'script'", text)
        self.assertIn('native_gate_pilot_window = {30,600}', text)
        self.assertNotIn('native_gate_pilot_device', text)
        self.assertNotIn('native_gate_pilot_delay', text)
        self.assertIn('native_gate_player_orders_ownership = "faction"', text)
        self.assertIn('native_gate_player_orders_factions = {"player","x4lan_player2","x4lan_player3"}', text)
        self.assertIn('native_gate_world_hash_stride = 300', text)
        self.assertNotIn('block_orders', text)
        # Attack the slot ship's battle counterpart: an Attack order keeps the Steering model a pilot slot needs.
        self.assertEqual(orders_plan['script']['3'], {'40': [{'op': 'attack', 'subject': S2, 'target': '0000000000176c95'}]})
        self.assertEqual(orders_plan['script']['1'], {'40': [{'op': 'attack', 'subject': S3, 'target': '0000000000176cb3'}]})
        self.assertEqual([s['faction'] for s in plan['slots']], ['player', 'x4lan_player2', 'x4lan_player3'])

    def test_start_moves_slot_ships_and_counterparts_to_their_own_sector(self):
        lines, plan, orders_plan = self.block('--source', 'script', '--start', '2=cluster_01_sector001_macro@0,30000,0/90',
                                              '--start', '1=00000000000006e0@100,30000,-200', '--start-counterparts')
        self.assertEqual(plan['starts'], [
            {'object': S2, 'sector': 'cluster_01_sector001_macro', 'x': 0, 'y': 30000, 'z': 0, 'yaw': 90},
            {'object': '0000000000176c95', 'sector': 'cluster_01_sector001_macro', 'x': 0, 'y': 30000, 'z': 1500, 'yaw': 90},
            {'object': S1, 'sector': '00000000000006e0', 'x': 100, 'y': 30000, 'z': -200, 'yaw': 0}])
        self.assertIn('["starts"]=', '\n'.join(lines))
        # The orders demo still attacks the (moved) counterpart.
        self.assertEqual(orders_plan['script']['2'], {'40': [{'op': 'attack', 'subject': S2, 'target': '0000000000176c95'}]})
        args = players.build_parser().parse_args(['config', '--source', 'script', '--start', '2=0000000000000451@0,30000,0/90',
                                                  '--start-counterparts'])
        self.assertEqual(players.echo_args(players.defaults(args))[-3:],
                         ['--start', '2=0000000000000451@0,30000,0/90', '--start-counterparts'])

    def test_moved_slot_ships_order_their_own_ship_after_the_start(self):
        lines, plan, orders_plan = self.block('--third-sector', '--players', '3', '--source', 'script', '--start', '2=cluster_01_sector001_macro@0,30000,0',
                                              '--start', '3=cluster_13_sector001_macro@0,30000,0', '--start-counterparts')
        self.assertEqual(orders_plan['script']['3'], {'40': [{'op': 'attack', 'subject': S2, 'target': '0000000000176c95'},
                                                             {'op': 'attack', 'subject': S3, 'target': '0000000000176cb3'}]})
        args = players.defaults(players.build_parser().parse_args(['config', '--third-sector', '--players', '3', '--source', 'script', '--start',
                                                                   '3=cluster_13_sector001_macro@0,30000,0', '--start-counterparts']))
        self.assertIn(f'tick=43 slot=3 op=attack subject={S3} target=0000000000176cb3 result=applied',
                      players.expected_orders(args, plan))
        # Without starts the demo is unchanged.
        lines, plan, orders_plan = self.block('--players', '3', '--source', 'script')
        self.assertEqual(len(orders_plan['script']['3']['40']), 1)

    def test_script_coop_block_uses_the_own_ship_table(self):
        lines, plan, orders_plan = self.block('--players', '4', '--mode', 'coop', '--source', 'script')
        text = '\n'.join(lines)
        self.assertIn('native_gate_player_orders_ownership = "table"', text)
        self.assertEqual(orders_plan['owners'], {S1: 1, S2: 2, S3: 3, S4: 4})
        self.assertNotIn('native_gate_player_orders_factions', text)
        self.assertIn('"faction"]="player",["ship"]="0000000000176b97"', text)

    def test_wire_block_is_the_human_session_with_devices_and_blocked_menus(self):
        lines, plan, orders_plan = self.block('--players', '2', '--mode', 'factions', '--relations', 'hostile')
        text = '\n'.join(lines)
        self.assertIsNone(orders_plan)
        self.assertIn("native_gate_pilot_source = 'wire'", text)
        self.assertIn('native_gate_pilot_delay = 3', text)
        self.assertIn('native_gate_pilot_window = {30,18000}', text)
        self.assertIn("native_gate_pilot_device = {'kbm','xinput'}", text)
        self.assertIn('native_gate_pilot_pad = {0,0}', text)
        self.assertIn('native_gate_pilot_capture = true', text)
        self.assertIn('native_gate_pilot_block_orders = true', text)
        self.assertNotIn('native_gate_player_identity_block_orders', text)
        self.assertIn("native_gate_view_points = 'players'", text)
        self.assertIn('["relation"]="hostile"', text)
        self.assertNotIn('native_gate_player_orders =', text)
        self.assertNotIn('native_gate_battle_engage', text)  # engage: the slot fixtures keep a Steering solver
        # Orders ride the input multiplexer next to the pilot records: wire sessions may script them too.
        lines, _, orders_plan = self.block('--players', '2', '--mode', 'coop', '--orders', 'demo')
        self.assertIn("X4LAN_CONFIG.native_gate_pilot_source = 'wire'", lines)
        self.assertIn('X4LAN_CONFIG.native_gate_player_orders = true', lines)
        self.assertIn('X4LAN_CONFIG.native_gate_pilot_block_orders = true', lines)
        self.assertEqual(orders_plan['owners'], {S1: 1, S2: 2})
        lines, _, _ = self.block('--players', '3', '--camera-follow', '--devices', 'kbm,xinput,xinput', '--pads', '0,0,1')
        self.assertIn('X4LAN_CONFIG.native_gate_pilot_camera_follow = {false,true,true}', lines)

    def test_invalid_sessions_are_refused(self):
        bad = [('--players', '5'), ('--players', '1'), ('--source', 'script', '--ships', S1 + ',0000000000176c95'),
               ('--source', 'script', '--create', '2=ship_arg_s_fighter_01_a_macro@0,100000,-600'),
               ('--source', 'script', '--camera-follow'), ('--setup-tick', '26', '--source', 'script'),
               ('--players', '3', '--devices', 'kbm,xinput'), ('--relation', '1-3=allied'),
               ('--mode', 'coop', '--relation', '1-2=hostile'), ('--ships', S1 + ',' + S1)]
        for argv in bad:
            with self.subTest(argv=argv), self.assertRaises((ValueError, SystemExit)), contextlib.redirect_stderr(io.StringIO()):
                self.block(*argv)
        for argv in (('--start', '2=0000000000000451@0,30000'), ('--start', '3=0000000000000451@0,30000,0'),
                     ('--start-counterparts',), ('--start', '2=0000000000000451@0,0,0', '--start', f'{S2}=0000000000000451@0,0,0')):
            with self.subTest(argv=argv), self.assertRaises((ValueError, SystemExit)), contextlib.redirect_stderr(io.StringIO()):
                self.block('--source', 'script', *argv)
        # Identity only: created ships are allowed without piloting.
        lines, plan, orders_plan = self.block('--source', 'none', '--create', '2=ship_arg_s_fighter_01_a_macro@0,100000,-600/90#4802')
        self.assertEqual(plan['slots'][1], {'create': {'macro': 'ship_arg_s_fighter_01_a_macro', 'seed': 4802, 'sector': 'player',
                                                       'x': 0, 'y': 100000, 'z': -600, 'yaw': 90}, 'faction': 'x4lan_player2'})
        self.assertFalse(any('pilot' in line for line in lines))

    def make_base(self, root, config=BASE_CONFIG):
        base = root / 'base'
        (base / 'ui').mkdir(parents=True)
        (base / 'native').mkdir()
        (base / 'md').mkdir()
        (base / 'ui/config.lua.append').write_text(config)
        (base / 'ui/native_gate_probe.lua').write_text('-- old probe\n')
        (base / 'native/x4lan_gate_64.dll').write_bytes(b'old dll')
        (base / 'md/native_gate_battle.xml').write_text('<mdscript/>\n')
        (base / 'ui.xml.files').write_text('ui/native_gate_orders.lua\nui/native_gate_probe.lua\n')
        return base

    def test_overlay_adds_adapters_before_the_probe_and_rebuilds_idempotently(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            base = self.make_base(root)
            out = root / 'out'
            code, printed = run_main('overlay', '--base', str(base), '--out', str(out), '--probe', str(DEV / 'native_gate_probe.lua'),
                                     '--players', '3', '--mode', 'factions', '--source', 'script')
            self.assertEqual(code, 0)
            self.assertIn('coordinator: .local/census/runN.sh <name> ' + str(out) + ' b1,b2,b3 602 3', printed)
            self.assertEqual((out / 'ui.xml.files').read_text().split(), [
                'ui/native_gate_inputs_core.lua', 'ui/native_gate_player_identity_core.lua', 'ui/native_gate_player_identity.lua',
                'ui/native_gate_player_orders_core.lua', 'ui/native_gate_player_orders.lua', 'ui/native_gate_orders.lua',
                'ui/native_gate_probe.lua'])
            for name in ('md/native_gate_player_identity.xml', 'md/native_gate_player_orders.xml', 'ui/native_gate_player_identity.lua',
                         'ui/native_gate_player_orders_core.lua', 'ui/native_gate_inputs_core.lua', 'player-identity-plan.json'):
                self.assertTrue((out / name).is_file(), name)
            text = (out / 'ui/config.lua.append').read_text()
            self.assertEqual(text.count('native_gate_clients'), 1)
            self.assertEqual(text.count('native_gate_player_identity_plan'), 1)
            self.assertNotIn('script_rng_rows', text)
            manifest = json.loads((out / 'x4lane-manifest.json').read_text())
            self.assertEqual(manifest['ui/config.lua.append'], hashlib.sha256((out / 'ui/config.lua.append').read_bytes()).hexdigest())
            # Rebuilding from the built overlay replaces the players block instead of stacking it.
            code, _ = run_main('overlay', '--base', str(out), '--out', str(root / 'again'), '--players', '3', '--mode', 'coop',
                               '--source', 'script', '--full-logs')
            again = (root / 'again/ui/config.lua.append').read_text()
            self.assertEqual(again.count('native_gate_player_identity_plan'), 1)
            self.assertEqual(again.count('native_gate_player_orders_ownership'), 1)
            self.assertIn('native_gate_player_orders_ownership = "table"', again)
            self.assertEqual((root / 'again/ui.xml.files').read_text().split().count('ui/native_gate_player_orders.lua'), 1)

    def test_four_clients_a_fourth_slot_ship_identity_plan_and_orders(self):
        lines, plan, orders_plan = self.block('--third-sector', '--players', '4', '--source', 'script', '--window', '30', '3000', '--start-counterparts',
                                              '--start', '2=cluster_01_sector001_macro@0,30000,0', '--start', '4=cluster_13_sector001_macro@0,30000,0')
        text = '\n'.join(lines)
        self.assertEqual([s['ship'] for s in plan['slots']], ['00000000000ae79c', '0000000000176ad5', '0000000000176b36', '0000000000176b97'])
        self.assertEqual([s['faction'] for s in plan['slots']], ['player', 'x4lan_player2', 'x4lan_player3', 'x4lan_player4'])
        self.assertEqual(len(plan['relations']), 6)
        self.assertIn("native_gate_pilot_slots = {'00000000000ae79c','0000000000176ad5','0000000000176b36','0000000000176b97'}", text)
        self.assertIn('native_gate_player_orders_players = 4', text)
        # Slot 4 was moved, so it orders its own ship to attack its moved counterpart.
        self.assertIn('4', orders_plan['script'])
        code, out = run_main('config', '--players', '4', '--source', 'script')
        self.assertEqual(code, 0)
        self.assertIn('runN4.sh <name> <overlay> b1,b2,b3,b4 602 4', out)

    def test_stock_source_every_slot_through_the_stock_path(self):
        lines, plan, _ = self.block('--players', '4', '--source', 'stock', '--stock-script', 'demo', '--present')
        text = '\n'.join(lines)
        for wanted in ("native_gate_pilot_source = 'wire'", 'native_gate_pilot_delay = 3', 'native_gate_pilot_window = {40,640}',
                       'native_gate_pilot_stock = true', "native_gate_pilot_stock_slots = 'all'",
                       "native_gate_pilot_capture_source = {'stock','stock','stock','stock'}", 'native_gate_pilot_device = {false,false,false,false}',
                       "native_gate_pilot_stock_script = {'demo','demo2','demo3','demo4'}", 'native_gate_pilot_present = true',
                       'native_gate_pilot_capture = true', 'native_gate_pilot_block_orders = true', 'native_gate_world_hash_stride = 1800'):
            self.assertIn(wanted, text)
        self.assertNotIn('native_gate_player_orders', text)
        text = '\n'.join(self.block('--players', '3', '--source', 'stock', '--present', 'camera')[0])
        self.assertIn("native_gate_pilot_present = 'camera'", text)
        self.assertNotIn('native_gate_pilot_stock_script', text)
        # The option checker's rules hold for what the builder writes.
        checker = load('check_gate_options_for_players', DEV / 'check_gate_options.py')
        full = text.replace('@X4LANE_CLIENTS@', '3') + '\nX4LAN_CONFIG.native_gate_snapshot_sha = "' + 'a' * 64 + '"\n'
        self.assertEqual(checker.pilot_stock_rules(full, set(re.findall(r'X4LAN_CONFIG\.(native_gate_\w+)\s*=\s*true\b', full))), [])
        self.assertEqual(checker.pilot_present_rules(full, set(re.findall(r'X4LAN_CONFIG\.(native_gate_\w+)\s*=\s*true\b', full))), [])
        for argv, why in ((['--source', 'script', '--present'], '--present needs --source stock'),
                          (['--source', 'script', '--stock-script', 'demo'], '--stock-script needs --source stock'),
                          (['--source', 'stock', '--present', '--camera-follow'], '--present excludes --camera-follow'),
                          (['--source', 'stock', '--invert', 'pitch'], '--invert needs the device sampler'),
                          (['--source', 'stock', '--window', '30', '600'], 'the identity must be ready')):
            with self.subTest(argv=argv), self.assertRaises(ValueError) as caught:
                self.block(*argv)
            self.assertIn(why, str(caught.exception))

    def _sectors(self, *argv):
        args = players.defaults(players.build_parser().parse_args(['plan', '--players', '4'] + list(argv)))
        players.plan_of(args)
        return args

    def test_default_starts_stay_in_the_covered_sectors_for_every_source(self):
        for source in ('wire', 'script', 'stock', 'none'):
            args = self._sectors('--source', source)
            self.assertEqual(args.sector_warnings, [], source)
        args = self._sectors('--source', 'stock')
        moved = {s['target'] for s in args.start}
        self.assertEqual(moved, {2, 3, 4})
        self.assertEqual({s['sector'] for s in args.start}, {identity.STOCK_SECTOR})

    def test_a_partial_stock_start_brings_the_other_slots_to_slot_one(self):
        args = self._sectors('--source', 'stock', '--start', '2=cluster_01_sector001_macro@0,30000,0')
        self.assertEqual(args.sector_warnings, [])
        by_slot = {s['target']: s['sector'] for s in args.start}
        self.assertEqual(by_slot, {2: 'cluster_01_sector001_macro', 3: identity.STOCK_SECTOR, 4: identity.STOCK_SECTOR})

    def test_a_third_sector_is_refused_by_name_or_warned_with_the_flag(self):
        third = ['--source', 'stock', '--start', '2=cluster_01_sector001_macro@0,30000,0', '--start', '3=cluster_13_sector001_macro@0,30000,0']
        with self.assertRaises(ValueError) as refused:
            self._sectors(*third)
        text = str(refused.exception)
        self.assertIn('slot 3 (cluster_13_sector001_macro)', text)
        self.assertIn('no physics scene', text)
        self.assertIn('--third-sector', text)
        self.assertNotIn('slot 2', text)
        args = self._sectors(*third, '--third-sector')
        self.assertEqual(len(args.sector_warnings), 1)
        self.assertTrue(args.sector_warnings[0].startswith('WARNING slot 3 (cluster_13_sector001_macro)'))

    def test_view_scenes_and_standins_cover_up_to_four_sectors(self):
        four = ['--source', 'stock', '--start', '2=cluster_01_sector001_macro@0,30000,0', '--start', '3=cluster_13_sector001_macro@0,30000,0',
                '--start', '4=cluster_02_sector001_macro@0,30000,0']
        keys = ['--set', 'native_gate_view_scenes=true', '--set', 'native_gate_viewpoint_policy=\'sector\'']
        args = self._sectors(*four, *keys, '--set', 'native_gate_viewpoint_standins=4')
        self.assertEqual(args.sector_warnings, [])
        with self.assertRaises(ValueError) as refused:
            self._sectors(*four, *keys, '--set', 'native_gate_viewpoint_standins=3')
        text = str(refused.exception)
        self.assertIn('slot 4 (cluster_02_sector001_macro): beyond the 3 covered sectors', text)
        self.assertIn('native_gate_viewpoint_standins=N', text)
        # Stand-ins without view scenes, or view scenes alone, still cover two.
        for partial in (['--set', 'native_gate_viewpoint_standins=4'], ['--set', 'native_gate_view_scenes=true']):
            with self.assertRaises(ValueError) as refused:
                self._sectors(*four, *partial)
            self.assertIn('beyond the 2 covered sectors', str(refused.exception))

    def test_the_two_names_of_the_start_sector_count_once(self):
        args = self._sectors('--source', 'stock', '--start', '2=cluster_14_sector001_macro@0,100000,500', '--start', '3=cluster_01_sector001_macro@0,30000,0',
                             '--start', '4=cluster_01_sector001_macro@0,30000,900')
        self.assertEqual(args.sector_warnings, [])
        # Moving slot 1 away makes the start sector one of the two.
        with self.assertRaises(ValueError):
            self._sectors('--source', 'stock', '--start', '1=cluster_13_sector001_macro@0,30000,0', '--start', '3=cluster_01_sector001_macro@0,30000,0')

    def test_a_ship_the_builder_has_no_sector_for_is_warned_not_refused(self):
        args = self._sectors('--source', 'none', '--ships', '00000000000ae79c,0000000000176ad5,0000000000176b36,00000000000a214c')
        self.assertEqual(len(args.sector_warnings), 1)
        self.assertIn('slot 4', args.sector_warnings[0])
        self.assertIn('VIEW_POINTS_UNCOVERED', args.sector_warnings[0])

    def test_stock_defaults_are_ships_with_a_cockpit_moved_near_slot_one(self):
        scouts = ['00000000000ae79c', '000000000007a8fe', '000000000007a8dc', '000000000007a931']
        for players in (2, 3, 4):
            lines, plan, _ = self.block('--players', str(players), '--source', 'stock', '--stock-script', 'demo')
            self.assertEqual([s['ship'] for s in plan['slots']], scouts[:players])
            self.assertEqual([s['object'] for s in plan['starts']], scouts[1:players])
            self.assertTrue(all(s['sector'] == '0000000000000729' and s['y'] == 100000 for s in plan['starts']))
        # An explicit choice replaces them; other sources keep the fixtures.
        lines, plan, _ = self.block('--source', 'stock', '--ships', '00000000000ae79c,0000000000176ad5')
        self.assertEqual(plan.get('starts', []), [])
        lines, plan, _ = self.block('--source', 'stock', '--start', '2=0000000000000729@1,100000,2/3')
        self.assertEqual([(s['object'], s['x']) for s in plan['starts']], [('000000000007a8fe', 1)])
        lines, plan, _ = self.block('--source', 'script')
        self.assertEqual(plan['slots'][1]['ship'], '0000000000176ad5')

    def test_death_and_respawn_are_off_unless_asked(self):
        text = '\n'.join(self.block('--source', 'stock', '--stock-script', 'aim')[0])
        self.assertNotIn('native_gate_pilot_death', text)
        self.assertNotIn('native_gate_pilot_respawn', text)
        text = '\n'.join(self.block('--source', 'stock', '--stock-script', 'aim', '--death')[0])
        self.assertIn('X4LAN_CONFIG.native_gate_pilot_death = true', text)
        self.assertNotIn('native_gate_pilot_respawn', text)
        text = '\n'.join(self.block('--source', 'stock', '--stock-script', 'aim', '--death', '--respawn', '300')[0])
        self.assertIn('X4LAN_CONFIG.native_gate_pilot_respawn = 300', text)
        for argv, why in ((['--source', 'script', '--death'], '--death needs --source stock'),
                          (['--source', 'stock', '--respawn', '300'], '--respawn needs --death'),
                          (['--source', 'stock', '--death', '--respawn', '0'], '--respawn is a delay'),
                          (['--source', 'stock', '--death', '--respawn', '8388608'], '--respawn is a delay')):
            with self.subTest(argv=argv), self.assertRaises(ValueError) as caught:
                self.block(*argv)
            self.assertIn(why, str(caught.exception))

    def test_the_crash_note_is_asked_for(self):
        self.assertIn('X4LAN_CONFIG.native_gate_crash_note = true', '\n'.join(self.block('--source', 'stock', '--crash-note')[0]))
        self.assertNotIn('native_gate_crash_note', '\n'.join(self.block('--source', 'stock')[0]))

    def test_one_shooter_and_a_suit_are_asked_for(self):
        text = '\n'.join(self.block('--source', 'stock', '--stock-script', 'aim', '--fire', 'script', '--fire-slots', '1', '--present', '--death', '--death-suit')[0])
        self.assertIn('X4LAN_CONFIG.native_gate_pilot_fire_slots = {1}', text)
        self.assertIn('X4LAN_CONFIG.native_gate_pilot_death_suit = true', text)
        text = '\n'.join(self.block('--source', 'stock', '--stock-script', 'aim', '--fire', 'script')[0])
        self.assertNotIn('native_gate_pilot_fire_slots', text)
        self.assertNotIn('native_gate_pilot_death_suit', text)
        for argv, why in ((['--source', 'stock', '--fire-slots', '1'], '--fire-slots needs --fire script'),
                          (['--source', 'stock', '--fire', 'record', '--fire-slots', '1'], '--fire-slots needs --fire script'),
                          (['--source', 'stock', '--fire', 'script', '--fire-slots', '3'], '--fire-slots is a list'),
                          (['--source', 'stock', '--fire', 'script', '--fire-slots', '1,1'], '--fire-slots is a list'),
                          (['--source', 'stock', '--death', '--death-suit'], '--death-suit needs --death and --present'),
                          (['--source', 'stock', '--present', '--death-suit'], '--death-suit needs --death and --present')):
            with self.subTest(argv=argv), self.assertRaises(ValueError) as caught:
                self.block(*argv)
            self.assertIn(why, str(caught.exception))

    def test_only_a_respawn_or_suit_overlay_has_the_md_file(self):
        checker = load('check_gate_options_for_players', DEV / 'check_gate_options.py')
        for extra, present in ((('--death',), False), (('--death', '--respawn', '300'), True), (('--death', '--present', '--death-suit'), True)):
            with tempfile.TemporaryDirectory() as d:
                root = Path(d)
                base = self.make_base(root)
                out = root / 'out'
                code, _ = run_main('overlay', '--base', str(base), '--out', str(out), '--probe', str(DEV / 'native_gate_probe.lua'),
                                   '--players', '2', '--source', 'stock', '--stock-script', 'aim', *extra)
                self.assertEqual(code, 0)
                self.assertEqual((out / 'md/native_gate_pilot_respawn.xml').is_file(), present)
                full = (out / 'ui/config.lua.append').read_text().replace('@X4LANE_CLIENTS@', '2')
                self.assertEqual(checker.pilot_death_rules(full, set(re.findall(r'X4LAN_CONFIG\.(native_gate_\w+)\s*=\s*true\b', full))), [])

    def test_the_respawn_md_uses_the_stock_actions_and_is_driven_by_the_probe(self):
        text = (DEV / 'native_gate_pilot_respawn.xml').read_text(encoding='utf-8')
        root = ET.fromstring(text)
        self.assertEqual(root.get('name'), 'X4LAN_PilotRespawn')
        controls = [cue.find('conditions/event_ui_triggered').get('control') for cue in root.find('cues') if cue.find('conditions') is not None]
        self.assertEqual(controls, ["'Note'", "'Suit'", "'Respawn'"])
        creates = root.findall('.//create_ship')
        self.assertEqual([c.get('name') for c in creates], ['$suit', '$ship', '$ship'])
        self.assertEqual(creates[0].get('macro'), 'player.entity.spacesuitmacro')   # stock's way to an entity's suit
        for create in creates:
            self.assertIsNone(create.find('safepos')) # a search draws randomness
        # Slot 1's ship has a free seat (teleport_player refuses a ship with a pilot); slots 2..N's ships have
        # their pilot, as the saved ships they replace (an empty seat is drawn in front of the presented eye).
        self.assertIsNone(creates[0].find('pilot'))
        self.assertIsNone(creates[1].find('pilot'))
        self.assertEqual(creates[2].find('pilot/select').get('tags'), 'tag.aipilot')
        respawn = [c for c in root.find('cues') if c.get('name') == 'Respawn'][0]
        self.assertEqual(respawn.find('.//create_order').get('id'), "'Wait'")
        self.assertEqual(root.find('.//set_object_min_hull').get('exact'), '100')   # as X4's own eject
        self.assertEqual(root.find('.//pause_spacesuit_oxygen').get('object'), '$suit')
        teleport = root.find('.//teleport_player')
        self.assertEqual((teleport.get('control'), teleport.get('instant'), teleport.get('force')), ('true', 'true', 'true'))
        probe = (DEV / 'native_gate_probe.lua').read_text(encoding='utf-8')
        for name in ("'X4LAN_PilotRespawn','Note'", "'X4LAN_PilotRespawn','Respawn'", "'X4LAN_PilotRespawn','Suit'", "'X4LAN_PilotRespawn.Ship'", "'X4LAN_PilotRespawn.Suit'"):
            self.assertIn(name, probe)

    def test_hit_claims_choice_replaces_the_base(self):
        self.assertIn('native_gate_hit_claims = false', '\n'.join(self.block('--source', 'stock', '--hit-claims', 'off')[0]))
        self.assertIn("native_gate_hit_claims = 'victim'", '\n'.join(self.block('--source', 'stock', '--hit-claims', 'victim')[0]))
        self.assertNotIn('native_gate_hit_claims', '\n'.join(self.block('--source', 'stock')[0]))
        text = '\n'.join(self.block('--source', 'stock', '--stock-script', 'aim', '--fire', 'script')[0])
        self.assertIn("native_gate_pilot_stock_script = 'aim'", text)
        self.assertIn('native_gate_pilot_window = {40,640}', text)

    def test_a_session_for_people(self):
        args = ['--players', '2', '--source', 'stock', '--present', '--fire', 'record', '--battle', 'calm', '--relations', 'hostile']
        text = '\n'.join(self.block(*args)[0])
        for wanted in ('native_gate_pilot_window = {40,72000}', 'native_gate_world_hash_stride = 18000', "native_gate_pilot_fire = 'record'",
                       'native_gate_pilot_present = true', 'native_gate_battle_engage = false', 'native_gate_pilot_stock = true'):
            self.assertIn(wanted, text)
        self.assertNotIn('native_gate_pilot_stock_script', text)
        code, out = run_main('config', *args)
        self.assertEqual(code, 0)
        self.assertIn('runPlay.sh <name> <overlay> b1,b2 72002 2', out)
        with self.assertRaises(ValueError):
            self.block('--source', 'none', '--fire', 'record')
        # The default stays 72000; a longer session asks for it, up to the gate's bound (lockstep_pilot_inputs.h).
        self.assertIn('native_gate_pilot_window = {40,648000}', '\n'.join(self.block('--source', 'stock', '--window', '40', '648000')[0]))
        with self.assertRaises(ValueError):
            self.block('--source', 'stock', '--window', '40', '8388608')

    def test_census_keys_replace_the_base_lines(self):
        text = '\n'.join(self.block('--players', '4', '--source', 'script', '--tick187', 'observe', '--fp', 'default',
                                    '--set', 'native_gate_order_canon=\'canonical\'')[0])
        for wanted in ("native_gate_ui_lazy = 'observe'", 'native_gate_ui_lazy_rows = {150,200}', 'native_gate_supply_wants = {150,200}',
                       "native_gate_loadout_order = 'observe'", 'native_gate_loadout_order_rows = {150,200}',
                       'native_gate_portable_math = true', 'native_gate_exact_reciprocal = true', 'native_gate_decimal = true',
                       "native_gate_fp_env = 'default'", "native_gate_order_canon = 'canonical'"):
            self.assertIn(wanted, text)
        text = '\n'.join(self.block('--source', 'script', '--tick187', 'canonical')[0])
        self.assertIn("native_gate_loadout_order = 'canonical'", text)
        self.assertIn('native_gate_loadout_order_rows = nil', text)
        for bad in ('native_gate_order_canon', 'other=1', 'native_gate_fp_env=1'):
            with self.subTest(bad), self.assertRaises(ValueError):
                self.block('--source', 'script', '--fp', 'default', '--set', bad)
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            base = self.make_base(root, BASE_CONFIG + "X4LAN_CONFIG.native_gate_loadout_order = 'canonical'\nX4LAN_CONFIG.native_gate_keep = 1\n")
            code, _ = run_main('overlay', '--base', str(base), '--out', str(root / 'o'), '--probe', str(DEV / 'native_gate_probe.lua'),
                               '--players', '4', '--source', 'script', '--tick187', 'observe')
            text = (root / 'o/ui/config.lua.append').read_text()
            self.assertEqual(code, 0)
            self.assertEqual(text.count('native_gate_loadout_order ='), 1)
            self.assertIn("native_gate_loadout_order = 'observe'", text)
            self.assertIn('native_gate_keep = 1', text)
            # --present takes a play base's chase camera out; the checker accepts the result.
            anchored = self.make_base(root / 'b3', BASE_CONFIG + "X4LAN_CONFIG.native_gate_camera_anchor = 'own'\n")
            code, _ = run_main('overlay', '--base', str(anchored), '--out', str(root / 'p'), '--probe', str(DEV / 'native_gate_probe.lua'),
                               '--players', '4', '--source', 'stock', '--present')
            text = (root / 'p/ui/config.lua.append').read_text()
            self.assertEqual(code, 0)
            self.assertNotIn("native_gate_camera_anchor = 'own'", text)
            checker = load('check_gate_options_for_present', DEV / 'check_gate_options.py')
            self.assertEqual(checker.pilot_present_rules(text, set(re.findall(r'X4LAN_CONFIG\.(native_gate_\w+)\s*=\s*true\b', text))), [])
            # A 3-player overlay as the base of a 4-player one: its players block is replaced, nothing of the plan stays.
            code, _ = run_main('overlay', '--base', str(root / 'o'), '--out', str(root / 'three'), '--probe', str(DEV / 'native_gate_probe.lua'),
                               '--players', '3', '--source', 'script')
            code, _ = run_main('overlay', '--base', str(root / 'three'), '--out', str(root / 'four'), '--probe', str(DEV / 'native_gate_probe.lua'),
                               '--players', '4', '--source', 'script')
            text = (root / 'four/ui/config.lua.append').read_text()
            self.assertEqual(code, 0)
            self.assertEqual(text.count('native_gate_player_identity_plan'), 1)
            self.assertEqual(text.count('native_gate_pilot_slots'), 1)
            self.assertIn('native_gate_player_orders_players = 4', text)
            self.assertEqual(len(json.loads((root / 'four/player-identity-plan.json').read_text())['slots']), 4)

    def test_overlay_refuses_a_probe_without_the_hook_or_a_base_with_its_own_orders(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            base = self.make_base(root)
            args = players.defaults(players.build_parser().parse_args(['overlay', '--base', str(base), '--out', str(root / 'o1')]))
            with self.assertRaises(ValueError) as caught:
                players.build_overlay(args)
            self.assertIn('no player identity hook', str(caught.exception))
            # The builder sets the attention viewpoints itself, so camera follow needs nothing from the base.
            code, _ = run_main('overlay', '--base', str(base), '--out', str(root / 'o4'), '--probe', str(DEV / 'native_gate_probe.lua'),
                               '--players', '3', '--camera-follow', '--devices', 'kbm,xinput,both')
            text = (root / 'o4/ui/config.lua.append').read_text()
            self.assertEqual(text.count('native_gate_view_points'), 1)
            self.assertIn('native_gate_pilot_camera_follow = {false,true,true}', text)
            self.assertIn("native_gate_pilot_device = {'kbm','xinput','both'}", text)
            self.assertIn('native_gate_pilot_pad = {0,0,1}', text)
            base2 = self.make_base(root / 'b2', BASE_CONFIG + 'X4LAN_CONFIG.native_gate_player_orders = true\n')
            args = players.defaults(players.build_parser().parse_args(['overlay', '--base', str(base2), '--out', str(root / 'o3'),
                                                                      '--probe', str(DEV / 'native_gate_probe.lua')]))
            with self.assertRaises(ValueError):
                players.build_overlay(args)

    def test_generated_block_arms_the_real_adapters_and_pilot_on_each_client(self):
        for mode in ('factions', 'coop'):
            lines, plan, _ = self.block('--players', '3', '--mode', mode, '--source', 'script')
            for slot in (1, 3):
                with self.subTest(mode=mode, slot=slot):
                    lua = LuaRuntime(unpack_returned_tuples=True)
                    lua.execute('''
                      X4LAN_CONFIG={native_gate_test=true,test_profile='45841',slot='p%d',native_gate_hash='battle',
                        native_gate_snapshot_sha=string.rep('a',64),port=12345,token=string.rep('c',32),
                        native_gate_serial_pathing=true,native_gate_world_identities=true,native_gate_bullet_rng=true,native_gate_nav_rng=true,
                        native_gate_agent_rng=true,native_gate_ship_rng=true}
                      X4LAN_SHA256={digest=function(data) hashed=data;return string.rep('b',64) end}
                      logs={};calls={}
                      state={active=1,boot_phase=0,issued=0,report_tick=0,rng='0'}
                      gate={boot=function() return 0 end,poll=function() return state end,load_requested=function() state.boot_phase=1;return 1 end,
                        report=function() return 1 end,stop=function() state.active=0;return 1 end,saved_identity=function() return string.rep('0',16) end}
                      gate.pilot_inputs=function(...) calls[#calls+1]='inputs:'..table.concat({...},'/');return 1 end
                      gate.render_rng_split=function(mode) return mode=='split' and 1 or 0 end
                      gate.render_rng_split_status=function() return 'mode=split' end
                      gate.bullet_cadence=function(mode) return mode=='every' and 1 or 0 end
                      gate.bullet_cadence_status=function() return 'mode=every' end
                      gate.nav_cadence=function(mode) return mode=='far' and 1 or 0 end
                      gate.nav_cadence_status=function() return 'mode=far' end
                      gate.view_points=function(mode) return mode=='players' and 1 or 0 end
                      gate.view_points_frame=function() return 1 end
                      gate.view_points_status=function() return 'mode=players' end
                      gate.actor_rng_all=function() return 1 end
                      gate.actor_rng_all_status=function() return 'armed=1' end
                      gate.pilot_ship=function() return nil,'0000000000000000' end
                      gate.propose=function() return 1 end
                      gate.wire=function(version,clients) return (version==3 and clients==3) and 1 or 0 end
                      gate.world_capture_nav_v2=function(tick) return 'tick='..tick..' state=WORLD_IMPORTED_2:'..tick end
                      package.loadlib=function() return function() return gate end end
                      local ffi={cdef=function() end,typeof=function() return true end,string=function(s) return s end,
                        C={GetSaveFolderPath=function() return 'fake/45841/save' end,IsStartmenu=function() return true end,
                          ReloadSaveList=function() end,IsSaveListLoadingComplete=function() return true end}}
                      require=function() return ffi end
                      DebugError=function(s) logs[#logs+1]=s end
                      RegisterEvent=function() end
                      SetScript=function(name,fn) update=fn end
                      GetSaveList=function() return {{filename='save_009'}} end
                      LoadGame=function() end
                    ''' % slot)
                    lua.execute('\n'.join(line.replace('@X4LANE_CLIENTS@', '3') for line in lines) +
                                "\nX4LAN_CONFIG.native_gate_world_hash_schema='WORLD_IMPORTED_2'")
                    for source in (MUX, CORE, GLUE, ORDERS_CORE, ORDERS_GLUE, PROBE):
                        lua.execute(source)
                    lua.execute('update();update()')
                    logs = list(lua.globals().logs.values())
                    self.assertFalse(any("ERROR" in line for line in logs), [line for line in logs if "ERROR" in line])
                    self.assertTrue(any(f'PLAYER_IDENTITY BOUND slot=p{slot} mode={mode} slots=3 tick=20' in line for line in logs))
                    self.assertTrue(any('PLAYER_ORDERS ARMED slot=%d delay=3 ownership=%s' % (slot, 'faction' if mode == 'factions' else 'table')
                                        in line for line in logs))
                    self.assertEqual(list(lua.globals().calls.values())[0],
                                     'inputs:00000000000ae79c,0000000000176ad5,0000000000176b36/script/0/30/600/1')

    def test_compare_accepts_the_live_line_shapes_of_pf3_and_pc3(self):
        # Engine prefixes, MD readback fields and time=, the raw canonical plan logged before the digest fix,
        # and Lua's immediate table refusals ahead of the MD line of the same tick (coop).
        for mode in ('factions', 'coop'):
            args = ['--players', '3', '--mode', mode, '--source', 'script']
            _, plan, _ = self.block(*args)
            P = identity.validate(plan)
            raw = identity.canonical(P)
            log = []
            for i, s in enumerate(identity.steps(P)):
                head = s['head'].replace('plan=' + identity.digest(P), 'plan=' + raw)
                log.append(f'*** Context:md.X4LAN_PlayerIdentity.Step<inst:14ea{i:02x}>: [X4LAN] PLAYER_IDENTITY_APPLIED {head} '
                           f'result={"unchanged" if s["step"] == "ship" and s["slot"] == 1 else "applied"} known=1 active=1 time=7.433')
            log.append('[X4LAN] PLAYER_IDENTITY READY tick=30 steps=%d md=%s' % (len(identity.steps(P)), 'c' * 64))
            cmd = players.defaults(players.build_parser().parse_args(['compare', 'x', *args]))
            rows = players.expected_orders(cmd, plan)
            if mode == 'coop':
                rows = [rows[0], rows[2], rows[1]]
            log += ['*** Context:md.X4LAN_PlayerOrders.Apply<inst:14ed61>: [X4LAN] PLAYER_ORDER_APPLIED ' + line +
                    ' seq=1 index=1 flags=0 due=43 received=41 time=7.81633' for line in rows]
            with tempfile.TemporaryDirectory() as d, self.subTest(mode=mode):
                paths = []
                for k in (1, 2, 3):
                    path = Path(d) / f'c{k}.log'
                    path.write_text(chr(10).join(log))
                    paths.append(str(path))
                code, printed = run_main('compare', *paths, *args)
                self.assertEqual(code, 0, printed)

    def test_compare_checks_identity_and_orders_lines_against_the_plan(self):
        args = ['--players', '3', '--mode', 'factions', '--source', 'script']
        lines, plan, _ = self.block(*args)
        P = identity.validate(plan)
        md = 'b' * 64
        log = [f'[X4LAN] PLAYER_IDENTITY_APPLIED {s["head"]} result=applied - time=21' for s in identity.steps(P)]
        log.append(f'[X4LAN] PLAYER_IDENTITY READY tick=30 steps=10 md={md}')
        cmd = players.defaults(players.build_parser().parse_args(['compare', 'x', *args]))
        log += ['[X4LAN] PLAYER_ORDER_APPLIED ' + line + ' seq=1 index=1 flags=0 due=43 received=41' for line in players.expected_orders(cmd, plan)]
        text = '\n'.join(log)
        with tempfile.TemporaryDirectory() as d:
            paths = []
            for k, content in enumerate((text, text, text), 1):
                path = Path(d) / f'c{k}.log'
                path.write_text(content)
                paths.append(str(path))
            code, printed = run_main('compare', *paths, *args)
            self.assertEqual(code, 0, printed)
            self.assertIn('PASS identity: 10 identical steps on 3 clients', printed)
            Path(paths[2]).write_text(text.replace('result=applied - time=21', 'result=refused_ship_missing - time=21', 1))
            self.assertEqual(run_main('compare', *paths, *args)[0], 1)
            Path(paths[2]).write_text(text.replace('result=applied seq', 'result=refused_not_owner seq'))
            code, printed = run_main('compare', *paths, *args)
            self.assertEqual(code, 1)
            self.assertIn('FAIL orders', printed)


if __name__ == '__main__':
    unittest.main()
