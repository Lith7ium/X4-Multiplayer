"""Stock piloting (lockstep_pilot_stock.h, stop reason 155): slot 1 flies through X4's own player
control path on every client from client 1's recorded stock dispatch stream; with
native_gate_pilot_stock_slots = 'all' slot 2 too, from client 2's (lockstep_pilot_slots.h, stop
reason 161: slot 2's own player and PlayerController in a slot scope on every client).

Covers the probe (two synthetic clients under LuaJIT with the real probe and input multiplexer and
a mocked gate: arming, the 'stock' section, staging and receipts), the sandbox builder's
--controls stock block and the option checker's stock rules. The native half (record codec,
capture, staging, replay, SPACE split, cursor, settings fingerprint, pins) is tested in C:
src/native/engine/test_lockstep_pilot_stock.h through test_lockstep_gate.c."""
import math
import contextlib
import importlib.util
import io
from pathlib import Path
import re
import unittest

from lupa.luajit21 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
DEV = ROOT/'tools/dev'
MUX = (DEV/'native_gate_inputs_core.lua').read_text(encoding='utf-8')
PROBE = (DEV/'native_gate_probe.lua').read_text(encoding='utf-8')


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT/path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ref = load('native_gate_inputs_stock', 'tools/dev/native_gate_inputs.py')
sandbox = load('native_gate_sandbox_stock', 'tools/dev/native_gate_sandbox.py')
options = load('check_gate_options_stock', 'tools/dev/check_gate_options.py')

S1, S2 = '000000000000a001', '000000000000a002'
WINDOW = (5, 40)


def lua_value(value):
    if isinstance(value, bool):
        return 'true' if value else 'false'
    if value is None:
        return 'nil'
    if isinstance(value, (int, float)):
        return repr(value)
    if isinstance(value, str):
        return '"' + value + '"'
    if isinstance(value, (list, tuple)):
        return '{' + ','.join(lua_value(v) for v in value) + '}'
    return '{' + ','.join(f'[{lua_value(k)}]={lua_value(v)}' for k, v in value.items()) + '}'


class StockClient:
    """One synthetic client of a two-client wire session with stock piloting: the real probe and
    input multiplexer, a mocked gate. Client 1 captures the stock record (mock: the injected
    script events of the tick), client 2 samples slot 2 with the device sampler (v1 record)."""

    def __init__(self, slot, script=None, slots_all=False, refusal=None, pins_refusal=None, present_refusal=None, death_refusal=None, prelude=None, **extra):
        self.slot = slot
        if slots_all:
            extra = {'native_gate_pilot_stock_slots': 'all', 'native_gate_pilot_capture_source': ['stock', 'stock'],
                     'native_gate_pilot_device': [False, False], **extra}
        lua = self.lua = LuaRuntime(unpack_returned_tuples=True)
        cfg = {'native_gate_test': True, 'test_profile': '45841', 'slot': f'p{slot}', 'native_gate_hash': 'clock',
               'native_gate_snapshot_sha': 'a'*64, 'port': 12345, 'token': 'c'*32, 'native_gate_clients': 2,
               'native_gate_serial_pathing': True, 'native_gate_world_identities': True, 'native_gate_pilot_inputs': True,
               'native_gate_pilot_slots': [S1, S2], 'native_gate_pilot_source': 'wire', 'native_gate_pilot_delay': 3,
               'native_gate_pilot_window': list(WINDOW), 'native_gate_pilot_suppress_local': True,
               'native_gate_pilot_capture': True, 'native_gate_pilot_stock': True,
               'native_gate_pilot_capture_source': ['stock', 'device'], 'native_gate_pilot_device': [False, 'xinput'],
               'native_gate_pilot_pad': [0, 0]}
        cfg.update(extra)
        lua.execute('X4LAN_CONFIG=' + lua_value({k: v for k, v in cfg.items() if v is not None}))
        lua.globals().CLIENTS = cfg['native_gate_clients']
        if script is not None:
            lua.execute('X4LAN_CONFIG.native_gate_pilot_stock_script=' + (repr(script) if isinstance(script, str) else lua_value(script)))
        lua.execute('''
          hashes={};logs={};proposed={};staged={};stock_staged={};injected={};calls={};loads=0;reports=0;is_menu=true
          pending={}
          state={active=1,boot_phase=0,issued=0,report_tick=0,rng='0'}
          X4LAN_SHA256={digest=function(data) hashes[#hashes+1]=data;return string.rep('b',64) end}
          local function le16(v) return string.format('%02x%02x',v%256,math.floor(v/256)%256) end
          local function le32(v) return le16(v%65536)..le16(math.floor(v/65536)) end
          local KIND={action=0,state=1,range=2}
          gate={boot=function() return 0 end,poll=function() return state end,
            load_requested=function() state.boot_phase=1;return 1 end,
            report=function(tick) reports=reports+1;state.report_tick=tick;return 1 end,stop=function() state.active=0;return 1 end,
            wire=function(version,clients) return (version==3 and clients==CLIENTS) and 1 or 0 end,
            propose=function(tick,blob) assert(tick==state.issued);proposed[tick]=blob;return 1 end,
            saved_identity=function(r) return string.rep('0',16) end,
            pilot_inputs=function(manifest,source,delay) calls[#calls+1]='inputs:'..source..'/'..delay;return 1 end,
            pilot_inputs_status=function() return 'armed=1' end,
            pilot_inputs_frame=function() return 'PILOT_1:'..state.issued end,
            pilot_apply=function(slot,record) staged[#staged+1]=state.issued..'/'..slot..':'..record;return 1 end,
            pilot_capture=function()
              assert(not STOCK_CAPTURE,'a stock capturing client has no pilot record')
              return string.format('01%02x%04x',SLOT,state.issued)..string.rep('0',48) end,
            pilot_device=function(kind,pad,invert) calls[#calls+1]='device:'..kind..'/'..pad;return 1,1 end,
            pilot_native_refusals=function() return nil end,
            pilot_stock=function(capture,slots)
              calls[#calls+1]='stock:'..capture..'/'..tostring(slots)
              if STOCK_REFUSAL then return 0,STOCK_REFUSAL end
              return 1 end,
            boot_hold=function() calls[#calls+1]='boot_hold';return 1 end,
            pilot_present=function()
              calls[#calls+1]='present'
              if PRESENT_REFUSAL then return 0,PRESENT_REFUSAL end
              return 1 end,
            crash_note=function(note,log) calls[#calls+1]='crash_note:'..note..'|'..tostring(log);return 1 end,
            pilot_death=function(delay,suit)
              calls[#calls+1]='death:'..delay..'/'..suit
              if DEATH_REFUSAL then return 0,DEATH_REFUSAL end
              return 1 end,
            pilot_death_frame=function() local e=death_events;death_events=nil;return e end,
            pilot_death_ship=function(slot,id) calls[#calls+1]='death_ship:'..slot..'/'..id;return 1 end,
            pilot_death_suit=function(slot,id) calls[#calls+1]='death_suit:'..slot..'/'..id;return 1 end,
            pilot_fire=function(mode,mask) calls[#calls+1]='fire:'..mode..'/'..tostring(mask);return 1 end,
            pilot_fire_frame=function() return 'PFIRE_1:'..state.issued end,
            pilot_fire_status=function() return 'mode=script' end,
            pilot_fire_guns=function() return nil end,
            pilot_death_status=function() return 'death=live' end,
            pilot_ship=function(slot) if ship_gone then return nil,'0' end;return 100+slot,'0' end,
            pilot_present_pause=function() calls[#calls+1]='pause';return PRESENT_OPEN and 1 or 0 end,
            pilot_present_resume=function() calls[#calls+1]='resume' end,
            pilot_stock_pins=function()
              calls[#calls+1]='stock_pins'
              if STOCK_PINS_REFUSAL then return 0,STOCK_PINS_REFUSAL end
              return 1 end,
            -- Mock record v3: version, flags, capture tick (le16), no cursor, aim or view, then the injected events.
            pilot_stock_capture=function()
              if state.issued<WINDOW_START-1 then return nil,'not_live' end
              local events=''
              for _,e in ipairs(pending) do events=events..le16(KIND[e[1]]*16384+e[2])..le32(math.floor(e[3]*1000)%4294967296) end
              local record='0301'..le16(state.issued)..string.rep('00',38)..string.format('%02x',#pending)..events
              pending={}
              return record
            end,
            pilot_stock_inject=function(kind,id,value)
              injected[#injected+1]=state.issued..'/'..kind..'/'..id..'/'..value;pending[#pending+1]={kind,id,value};return 1 end,
            pilot_stock_apply=function(slot,hex) stock_staged[#stock_staged+1]=state.issued..'/'..slot..':'..hex;return 1 end,
            pilot_stock_frame=function() return 'PSTOCK_1:'..state.issued..':'..#stock_staged end}
          package.loadlib=function(path) assert(not path:find('fixedtick'));return function() return gate end end
          ui_events={};registered={}
          AddUITriggeredEvent=function(screen,control,param) ui_events[#ui_events+1]={screen=screen,control=control,param=param} end
          ConvertStringToLuaID=function(s) return 'lua:'..s end
          ConvertIDTo64Bit=function(id) return id end
          GetComponentData=function() return 100,100 end
          local ffi={cdef=function() end,typeof=function() return true end,string=function(s) return s end,new=function() return {} end,
            C={GetObjectPositionInSector=function() return {x=1,y=2,z=3,yaw=math.pi,pitch=0,roll=0} end,IsComponentOperational=function() return true end,
              GetSaveFolderPath=function() return 'fake/45841/save' end,GetCurrentGameTime=function() return 7 end,
              IsStartmenu=function() return is_menu end,ReloadSaveList=function() end,IsSaveListLoadingComplete=function() return true end,
              GetPlayerOccupiedShipID=function() return 11 end}}
          require=function(name) assert(name=='ffi');return ffi end
          DebugError=function(s) logs[#logs+1]=s end
          RegisterEvent=function(name,fn) registered[name]=fn end
          SetScript=function(name,fn) update=fn end
          GetSaveList=function() return {{filename='save_009'}} end
          LoadGame=function() loads=loads+1 end
        ''')
        if prelude:
            lua.execute(prelude)
        lua.globals().SLOT = slot
        lua.globals().STOCK_CAPTURE = slot == 1 or slots_all
        lua.globals().STOCK_REFUSAL = refusal
        lua.globals().STOCK_PINS_REFUSAL = pins_refusal
        lua.globals().PRESENT_REFUSAL = present_refusal
        lua.globals().DEATH_REFUSAL = death_refusal
        lua.globals().WINDOW_START = WINDOW[0]
        for source in (MUX, PROBE):
            lua.execute(source)
        lua.execute('update();update();update()')                       # arms before loading
        lua.execute('state.boot_phase=3;is_menu=false;state.wire_version=3;state.clients=2')
        for source in (MUX, PROBE):                                      # the UI reloads with the game
            lua.execute(source)

    def tick(self, tick, inputs):
        self.lua.execute(f'state.issued={tick};state.inputs={lua_value(inputs)};update()')
        return self.lua.globals().proposed[tick]

    def values(self, name):
        return list(self.lua.globals()[name].values())

    def errors(self):
        return [line for line in self.values('logs') if 'ERROR' in line]


def run(clients, ticks):
    inputs, blobs = ['', ''], {}
    for tick in range(1, ticks+1):
        blobs[tick] = [c.tick(tick, inputs) for c in clients]
        inputs = blobs[tick]
    return blobs


SCRIPT = [[6, 'state', 0xd, 1], [8, 'range', 1, 0.5], [8, 'action', 0x12f, 0], [12, 'state', 0xd, 0]]


class StockProbeTests(unittest.TestCase):
    def test_slot_one_rides_the_stock_section_and_every_client_stages_it_identically(self):
        clients = [StockClient(1, SCRIPT), StockClient(2, SCRIPT)]
        blobs = run(clients, 14)
        for c in clients:
            self.assertEqual(c.errors(), [], c.values('logs')[-3:])
            logs = c.values('logs')
            self.assertTrue(any('INPUT_SECTION ARMED section=stock owner=stock' in line for line in logs))
        # Client 1 arms the stock capture without a device; client 2 replays and samples slot 2 with its pad.
        calls1, calls2 = clients[0].values('calls'), clients[1].values('calls')
        self.assertIn('stock:1/1', calls1)
        self.assertFalse(any(call.startswith('device:') for call in calls1))
        self.assertIn('stock:0/1', calls2)
        self.assertIn('device:xinput/0', calls2)
        self.assertTrue(any('PILOT_STOCK_ARMED slot=1 capture=1 slots=1 script=list' in line for line in clients[0].values('logs')))
        self.assertTrue(any('PILOT_STOCK_ARMED slot=2 capture=0 slots=1 script=none' in line for line in clients[1].values('logs')))
        # Client 1's blob holds only the stock section (no pilot record for slot 1); client 2's is its legacy pilot record.
        for tick in range(WINDOW[0]-1, 15):
            self.assertEqual(list(ref.decode(blobs[tick][0])), ['stock'], tick)
            self.assertEqual(list(ref.decode(blobs[tick][1])), ['pilot'], tick)
        self.assertEqual(blobs[2][0], '')     # before the window: nothing to propose
        # The scripted events of tick 8 ride that tick's record: range 1 = 0.5 and action 0x12f.
        stock8 = ref.decode(blobs[8][0])['stock']
        self.assertEqual(stock8[:8], '03010800')
        self.assertEqual(stock8[84:86], '02')
        self.assertEqual(stock8[86:], '0180' + 'f4010000' + '2f01' + '00000000')
        self.assertEqual(clients[0].values('injected'), ['6/state/13/1', '8/range/1/0.5', '8/action/303/0', '12/state/13/0'])
        self.assertEqual(clients[1].values('injected'), [])
        self.assertTrue(any(line.endswith('PILOT_STOCK_CAPTURE tick=8 record='+stock8) for line in clients[0].values('logs')))
        # Every client staged the same stock records (slot 1 only) and the same pilot records (slot 2 only).
        stock = [c.values('stock_staged') for c in clients]
        self.assertEqual(stock[0], stock[1])
        self.assertIn('9/1:'+stock8, stock[0])
        self.assertTrue(all(entry.split('/')[1].startswith('1:') for entry in stock[0]))
        staged = [c.values('staged') for c in clients]
        self.assertEqual(staged[0], staged[1])
        self.assertTrue(staged[0] and all(entry.split('/')[1].startswith('2:') for entry in staged[0]))
        # The declared state carries the stock receipt; both clients hashed the same data every tick.
        hashes = [[h for h in c.values('hashes') if h.startswith('CLOCK_ONLY_1')] for c in clients]
        self.assertEqual(hashes[0], hashes[1])
        self.assertTrue(any('|PSTOCK_1:9:' in h and '|stock:1/' in h for h in hashes[0]), hashes[0][8])

    def test_demo_script_is_relative_to_the_window_start(self):
        client = StockClient(1, 'demo')
        run([client, StockClient(2, 'demo')], WINDOW[0]+11)
        self.assertEqual(client.errors(), [])
        self.assertEqual(client.values('injected'), [f'{WINDOW[0]+10}/state/13/1'])
        self.assertTrue(any('PILOT_STOCK_ARMED slot=1 capture=1 slots=1 script=demo' in line for line in client.values('logs')))

    def test_every_stock_slot_rides_its_own_section_from_its_own_client(self):
        # Slots 'all': both clients capture their own slot (no device), every client stages both
        # records under their senders' slots; client 2's demo2 is relative to the window start.
        clients = [StockClient(1, ['demo', False], slots_all=True), StockClient(2, ['demo', 'demo2'], slots_all=True)]
        blobs = run(clients, WINDOW[0]+16)
        for c in clients:
            self.assertEqual(c.errors(), [], c.values('logs')[-3:])
            self.assertFalse(any(call.startswith('device:') for call in c.values('calls')))
        self.assertIn('stock:1/3', clients[0].values('calls'))
        self.assertIn('stock:1/3', clients[1].values('calls'))
        self.assertTrue(any('PILOT_STOCK_ARMED slot=1 capture=1 slots=3 script=demo' in line for line in clients[0].values('logs')))
        self.assertTrue(any('PILOT_STOCK_ARMED slot=2 capture=1 slots=3 script=demo2' in line for line in clients[1].values('logs')))
        self.assertEqual(clients[0].values('injected'), [f'{WINDOW[0]+10}/state/13/1'])
        self.assertEqual(clients[1].values('injected'), [f'{WINDOW[0]+15}/state/13/1'])
        for tick in range(WINDOW[0]-1, WINDOW[0]+16):
            self.assertEqual(list(ref.decode(blobs[tick][0])), ['stock'], tick)
            self.assertEqual(list(ref.decode(blobs[tick][1])), ['stock'], tick)
        stock = [c.values('stock_staged') for c in clients]
        self.assertEqual(stock[0], stock[1])
        senders = {entry.split('/')[1].split(':')[0] for entry in stock[0]}
        self.assertEqual(senders, {'1', '2'})
        self.assertEqual([c.values('staged') for c in clients], [[], []])   # no pilot records at all
        hashes = [[h for h in c.values('hashes') if h.startswith('CLOCK_ONLY_1')] for c in clients]
        self.assertEqual(hashes[0], hashes[1])

    def test_slots_pins_are_checked_before_the_modules_that_patch_x4(self):
        client = StockClient(2, slots_all=True, native_gate_boot_hold=True)
        self.assertEqual(client.errors(), [], client.values('logs')[-3:])
        calls = client.values('calls')
        self.assertLess(calls.index('stock_pins'), calls.index('inputs:wire/3'))
        self.assertLess(calls.index('stock_pins'), calls.index('boot_hold'))
        logs = client.values('logs')
        at = lambda text: next(i for i, line in enumerate(logs) if text in line)
        self.assertLess(at('PILOT_STOCK_PINS ok=1'), at('BOOT_HOLD_ARMED'))
        self.assertLess(at('BOOT_HOLD_ARMED'), at('PILOT_STOCK_ARMED'))
        # Slot 1 only: no extra pin check.
        self.assertNotIn('stock_pins', StockClient(1).values('calls'))

    def test_the_aim_script_steers_at_the_other_ship(self):
        client = StockClient(2, slots_all=True, native_gate_pilot_stock_script='aim')
        self.assertEqual(client.errors(), [], client.values('logs')[-3:])
        self.assertTrue(any('script=aim' in line for line in client.values('logs')))
        axes = client.lua.globals().X4LAN_STOCK_AIM_AXES
        pose = client.lua.eval('function(x,y,z,yaw,pitch) return {x=x,y=y,z=z,yaw=yaw,pitch=pitch} end')
        ahead = axes(pose(0, 0, 0, 0, 0), pose(0, 0, 800, 0, 0))
        self.assertEqual(ahead, (0, 0))
        self.assertEqual(axes(pose(0, 0, 800, math.pi, 0), pose(0, 0, 0, 0, 0)), (0, 0))   # facing -z, target behind in z: ahead
        right = axes(pose(0, 0, 0, 0, 0), pose(100, 0, 800, 0, 0))
        self.assertGreater(right[0], 0)
        above = axes(pose(0, 0, 0, 0, 0), pose(0, 100, 800, 0, 0))
        self.assertLess(above[1], 0)     # negative is nose up
        behind = axes(pose(0, 0, 0, 0, 0), pose(0, 0, -800, 0, 0))
        self.assertEqual(abs(behind[0]), 0.5)   # clamped
        # Across the wrap: heading +3.1, target at heading -3.1 is a short turn to the right.
        wrap = axes(pose(0, 0, 0, 3.1, 0), pose(800 * math.sin(-3.1), 0, 800 * math.cos(-3.1), 0, 0))
        self.assertGreater(wrap[0], 0)
        self.assertLess(wrap[0], 0.5)

    def test_four_clients_arm_every_slot_and_script(self):
        ships = [S1, S2, '0000000000176b36', '0000000000176b97']
        for slot in (1, 2, 3, 4):
            with self.subTest(slot=slot):
                client = StockClient(slot, slots_all=True, native_gate_clients=4, native_gate_pilot_slots=ships,
                                     native_gate_pilot_capture_source=['stock'] * 4, native_gate_pilot_device=[False] * 4,
                                     native_gate_pilot_pad=[0] * 4, native_gate_pilot_present=True,
                                     native_gate_pilot_stock_script=['demo', 'demo2', 'demo3', 'demo4'])
                self.assertEqual(client.errors(), [], client.values('logs')[-3:])
                self.assertIn('stock:1/15', client.values('calls'))
                self.assertTrue(any(f'PILOT_STOCK_ARMED slot={slot} capture=1 slots=15 script={"demo" if slot == 1 else f"demo{slot}"}' in line
                                    for line in client.values('logs')))
        errors = StockClient(3, slots_all=True, native_gate_clients=4, native_gate_pilot_slots=ships,
                             native_gate_pilot_capture_source=['stock'] * 4, native_gate_pilot_device=[False] * 4,
                             native_gate_pilot_stock_script='demo9').errors()
        self.assertTrue(errors and 'pilot stock script must be demo, demo2, demo3, demo4 or a list' in errors[0], errors)

    def test_present_arms_after_the_stock_slots_and_the_probe_frame_runs_canonical(self):
        client = StockClient(2, slots_all=True, native_gate_pilot_present=True)
        self.assertEqual(client.errors(), [], client.values('logs')[-3:])
        calls = client.values('calls')
        self.assertLess(calls.index('stock:1/3'), calls.index('present'))
        self.assertTrue(any('PILOT_PRESENT_ARMED slot=2 mode=true' in line for line in client.values('logs')))
        self.assertTrue(any('PILOT_PRESENT_ARMED slot=2 mode=camera' in line
                            for line in StockClient(2, slots_all=True, native_gate_pilot_present='camera').values('logs')))
        # The frame runs canonical: paused before it, resumed after it, only when the scope was open.
        client.lua.execute('PRESENT_OPEN=true;calls={}')
        client.tick(1, ['', ''])
        calls = client.values('calls')
        self.assertEqual((calls[0], calls[-1]), ('pause', 'resume'))
        client.lua.execute('PRESENT_OPEN=false;calls={}')
        client.tick(2, ['', ''])
        self.assertNotIn('resume', client.values('calls'))
        # Refusals name themselves.
        errors = StockClient(2, slots_all=True, native_gate_pilot_present=True, present_refusal='pins_present:97de40').errors()
        self.assertTrue(errors and 'pilot present refused: pins_present:97de40' in errors[0], errors)
        for extra, why in ((dict(native_gate_pilot_present='yes'), 'must be true or camera'),
                           (dict(native_gate_pilot_present=True, native_gate_pilot_stock_slots=None), 'needs stock slots')):
            errors = StockClient(1, **extra).errors()
            self.assertTrue(errors and why in errors[0], errors)
        errors = StockClient(2, slots_all=True, native_gate_pilot_present=True, native_gate_pilot_camera_follow=[False, True]).errors()
        self.assertTrue(errors and 'excludes camera follow' in errors[0], errors)

    def test_death_is_off_unless_asked_and_arms_after_the_stock_slots(self):
        client = StockClient(2, slots_all=True)
        self.assertFalse(any(call.startswith('death') for call in client.values('calls')))
        self.assertFalse(any('PILOT_DEATH' in line for line in client.values('logs')))
        client = StockClient(2, slots_all=True, native_gate_pilot_death=True)
        self.assertEqual(client.errors(), [], client.values('logs')[-3:])
        calls = client.values('calls')
        self.assertLess(calls.index('stock:1/3'), calls.index('death:0/0'))
        self.assertTrue(any('PILOT_DEATH_ARMED slot=2 respawn=off suit=false' in line for line in client.values('logs')))
        for extra, why in ((dict(native_gate_pilot_death='yes'), 'must be true'),
                           (dict(native_gate_pilot_respawn=60), 'needs native_gate_pilot_death'),
                           (dict(native_gate_pilot_death=True, native_gate_pilot_respawn=1.5), 'whole number'),
                           (dict(native_gate_pilot_death=True, native_gate_pilot_respawn=0), 'whole number'),
                           (dict(native_gate_pilot_death_suit=True), 'needs native_gate_pilot_death'),
                           (dict(native_gate_pilot_death=True, native_gate_pilot_death_suit=True), 'needs native_gate_pilot_present')):
            with self.subTest(extra):
                errors = StockClient(2, slots_all=True, **extra).errors()
                self.assertTrue(any(why in line for line in errors), errors[-2:])
        errors = StockClient(2, slots_all=True, native_gate_pilot_death=True, death_refusal='death_eject_off').errors()
        self.assertTrue(any('pilot death refused: death_eject_off' in line for line in errors), errors[-2:])

    def test_respawn_raises_the_md_events_at_the_ticks_the_gate_names(self):
        client = StockClient(1, slots_all=True, native_gate_pilot_death=True, native_gate_pilot_respawn=60)
        self.assertEqual(client.errors(), [], client.values('logs')[-3:])
        inputs = ['', '']
        for tick in range(1, WINDOW[0]+1):
            client.tick(tick, inputs)
        lua = client.lua
        # The window start: what MD keeps per slot, and the start pose in the probe.
        notes = lua.eval('(function() local t={};for _,e in ipairs(ui_events) do t[#t+1]=e.control..":"..e.param.slotname..":"..tostring(e.param.object) end;return table.concat(t,",") end)()')
        self.assertEqual(notes, 'Note:1:lua:101,Note:2:lua:102')
        # die, create: the MD event with the start pose, in degrees, a seed of its own per slot and ship.
        lua.execute("death_events='die:1'")
        client.tick(WINDOW[0]+1, inputs)
        self.assertTrue(any(line.endswith(f'PILOT_DEATH tick={WINDOW[0]+1} die:1') for line in client.values('logs')))
        self.assertEqual(lua.eval('#ui_events'), 2)
        lua.execute("death_events='create:1'")
        client.tick(WINDOW[0]+2, inputs)
        event = lua.eval('ui_events[3]')
        self.assertEqual((event.control, event.param.slotname, event.param.seed, event.param.x, event.param.z, event.param.yaw), ('Respawn', '1', 7101, 1, 3, 180))
        lua.execute("death_events='create:1'")
        client.tick(WINDOW[0]+3, inputs)
        self.assertEqual(lua.eval('ui_events[4].param.seed'), 7102)
        # The MD answer goes to the gate as a decimal string.
        lua.execute("registered['X4LAN_PilotRespawn.Ship1'](nil,12345)")
        self.assertIn('death_ship:1/12345', client.values('calls'))

    def test_a_dead_slot_gets_its_suit_at_its_last_pose(self):
        client = StockClient(2, slots_all=True, native_gate_pilot_death=True, native_gate_pilot_death_suit=True, native_gate_pilot_present=True,
                             native_gate_pilot_ship_state=True)
        self.assertEqual(client.errors(), [], client.values('logs')[-3:])
        self.assertIn('death:0/1', client.values('calls'))
        inputs, lua = ['', ''], client.lua
        for tick in range(1, WINDOW[0]+1):
            client.tick(tick, inputs)
        self.assertEqual(lua.eval('#ui_events'), 2)        # what MD keeps per slot, without a respawn too
        lua.execute("death_events='die:2,suit:2'")
        client.tick(WINDOW[0]+1, inputs)
        event = lua.eval('ui_events[3]')
        self.assertEqual((event.control, event.param.slotname, event.param.seed, event.param.x, event.param.y, event.param.z, event.param.yaw),
                         ('Suit', '2', 8201, 1, 102, 3, 180))
        # Rare rows are written at once, not held by the status stride.
        self.assertTrue(any(line.endswith(f'PILOT_DEATH tick={WINDOW[0]+1} die:2,suit:2') for line in client.values('logs')))
        lua.execute("registered['X4LAN_PilotRespawn.Suit2'](nil,777)")
        self.assertIn('death_suit:2/777', client.values('calls'))
        self.assertTrue(any('PILOT_RESPAWN_SUIT slot=2 accepted=1' in line for line in client.values('logs')))

    def test_scripted_fire_can_be_one_slot(self):
        lua = StockClient(1, slots_all=True).lua
        mask = lua.eval('function(list,fire) return X4LAN_PILOT_FIRE_MASK(list and {list} or nil,fire,2) end')
        self.assertEqual(mask(None, 'script'), 0)    # every slot, as ever
        self.assertEqual(mask(1, 'script'), 1)
        self.assertEqual(mask(2, 'script'), 2)
        ok = lua.eval('function(text) return pcall(function() return X4LAN_PILOT_FIRE_MASK(loadstring("return "..text)(),"script",2) end) end')
        self.assertEqual(ok('{1,2}')[:2], (True, 3))
        for text, why in (('{3}', 'fire_slots invalid'), ('{1,1}', 'fire_slots invalid')):
            result = ok(text)
            self.assertFalse(result[0])
            self.assertIn(why, result[1])
        record = lua.eval('function() return pcall(function() return X4LAN_PILOT_FIRE_MASK({1},"record",2) end) end')()
        self.assertFalse(record[0])
        self.assertIn('for script fire', record[1])

    def test_the_crash_note_is_off_unless_asked_and_writes_beside_the_log(self):
        self.assertFalse(any(c.startswith('crash_note') for c in StockClient(1).values('calls')))
        client = StockClient(1, native_gate_crash_note=True)
        self.assertEqual(client.errors(), [], client.values('logs')[-3:])
        self.assertIn('crash_note:fake/45841/x4lan-crash.txt|fake/45841/x4lan-debug.txt', client.values('calls'))
        self.assertTrue(any('CRASH_NOTE_ARMED file=45841/x4lan-crash.txt' in line for line in client.values('logs')))
        self.assertIn('native_gate_crash_note must be true', stock_failures(stock_config(native_gate_crash_note="'yes'")))
        self.assertEqual(stock_failures(stock_config(native_gate_crash_note='true')), [])

    def test_a_refusal_names_itself(self):
        # The gate's reason reaches the error row, from the early pin check and from the arming.
        for slot in (1, 2):
            errors = StockClient(slot, slots_all=True, pins_refusal='pins_slots:9fe2a9').errors()
            self.assertTrue(errors and 'pilot stock refused: pins_slots:9fe2a9' in errors[0], errors)
            errors = StockClient(slot, slots_all=True, refusal='capture_not_a_stock_slot').errors()
            self.assertTrue(errors and 'pilot stock refused: capture_not_a_stock_slot' in errors[0], errors)
        # The probe's own conditions name themselves too.
        named = (
            (1, dict(native_gate_pilot_source='script', native_gate_pilot_delay=0), 'pilot source must be wire'),
            (2, dict(native_gate_pilot_capture_source=['stock', 'stock'], native_gate_pilot_device=[False, False]),
             'capture source stock exactly on the stock slots'),
            (1, dict(native_gate_pilot_predict=['observe', False]), 'excludes prediction'),
            (1, dict(native_gate_pilot_device=['kbm', 'xinput']), 'no device on a capturing client'),
        )
        for slot, extra, why in named:
            with self.subTest(why):
                errors = StockClient(slot, **extra).errors()
                self.assertTrue(errors and 'pilot stock refused: '+why in errors[0], errors)

    def test_a_stock_record_from_a_slot_outside_the_stock_slots_stops_the_session(self):
        clients = [StockClient(1), StockClient(2)]
        run(clients, 6)
        forged = ref.encode({'pilot': '01'+'00'+'0102'+'ff7f'*7+'0000'+'00'*8, 'stock': '03'+'01'+'0700'+'00'*38+'00'})
        clients[1].lua.execute(f'state.issued=7;state.inputs={lua_value(["", forged])};update()')
        self.assertTrue(any('stock record from slot 2' in line for line in clients[1].values('logs')))

    def test_refusals_before_loading(self):
        refused, source = 'pilot stock refused', 'pilot capture source refused'
        cases = (
            ('no wire', (1, 2), dict(native_gate_pilot_source='script', native_gate_pilot_delay=0), refused),
            ('capture on client 2', (1, 2), dict(native_gate_pilot_capture_source=['device', 'stock'], native_gate_pilot_device=['kbm', False]),
             refused),
            ('device on client 1', (1,), dict(native_gate_pilot_device=['kbm', 'xinput']), refused),
            ('no device on client 2', (2,), dict(native_gate_pilot_device=[False, False]), 'pilot capture source device needs'),
            ('stock not true', (1,), dict(native_gate_pilot_stock='yes'), source),
            ('stock not true', (2,), dict(native_gate_pilot_stock='yes'), refused),
            ('prediction on client 1', (1,), dict(native_gate_pilot_predict=['observe', False]), refused),
            ('stock source without stock', (1,), dict(native_gate_pilot_stock=None), source),
            ('bad script kind', (1,), dict(script=[[6, 'axis', 1, 0]]), 'invalid pilot stock script event'),
            ('bad script id', (1,), dict(script=[[6, 'state', 600, 1]]), 'invalid pilot stock script event'),
            ('bad script tick', (1,), dict(script=[[6.5, 'state', 1, 1]]), 'invalid pilot stock script event'),
            ('slots all, device on client 2', (2,), dict(slots_all=True, native_gate_pilot_capture_source=['stock', 'device'],
                                                       native_gate_pilot_device=[False, 'kbm']), refused),
            ('slots without slot 1', (1, 2), dict(native_gate_pilot_stock_slots=[2]), 'pilot stock slots must hold slot 1'),
            ('slots out of range', (1, 2), dict(native_gate_pilot_stock_slots=[1, 3]), 'pilot stock slots invalid'),
            ('slots twice', (1, 2), dict(native_gate_pilot_stock_slots=[1, 1]), 'pilot stock slots invalid'),
            ('slots not a list', (1, 2), dict(native_gate_pilot_stock_slots='some'), 'pilot stock slots must be all or a list'),
            ('prediction on client 2 of slots all', (2,), dict(slots_all=True, native_gate_pilot_predict=[False, 'observe']), refused),
        )
        for name, slots, extra, why in cases:
            for slot in slots:
                with self.subTest(name, slot=slot):
                    extra = dict(extra)
                    script = extra.pop('script', None)
                    errors = StockClient(slot, script, **extra).errors()
                    self.assertTrue(errors and why in errors[0], errors)
        # A configuration valid for that client arms it (the other client refuses on its own).
        self.assertEqual(StockClient(2, native_gate_pilot_device=['kbm', 'xinput']).errors(), [])
        self.assertEqual(StockClient(2, native_gate_pilot_predict=['observe', False]).errors(), [])


def config_args(*extra):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        sandbox.main(['config', *extra])
    return [line for line in out.getvalue().splitlines() if not line.startswith(('-- relay', '-- coordinator', '-- desync watcher'))]


class StockBuilderTests(unittest.TestCase):
    def test_stock_controls_replace_the_device_lines(self):
        stock, default = config_args('--controls', 'stock'), config_args()
        self.assertEqual([line for line in default if line not in stock], ["X4LAN_CONFIG.native_gate_pilot_device = {'kbm','xinput'}"])
        self.assertEqual([line for line in stock if line not in default], [
            "-- stock piloting: lockstep_pilot_stock.h (stop reason 155); slot 1 flies through X4's own controls on every client",
            'X4LAN_CONFIG.native_gate_pilot_stock = true',
            "X4LAN_CONFIG.native_gate_pilot_capture_source = {'stock','device'}",
            "X4LAN_CONFIG.native_gate_pilot_device = {false,'xinput'}"])
        # Client 1 has no device: its pad entry is always 0.
        self.assertIn('X4LAN_CONFIG.native_gate_pilot_pad = {0,2}', config_args('--controls', 'stock', '--pad1', '3', '--pad2', '2'))
        self.assertIn("X4LAN_CONFIG.native_gate_pilot_stock_script = 'demo'", config_args('--controls', 'stock', '--stock-script', 'demo'))
        self.assertIn("X4LAN_CONFIG.native_gate_pilot_device = {false,'both'}", config_args('--controls', 'stock', '--device2', 'both'))
        notes = sandbox.session_notes(sandbox.build_parser().parse_args(['config', '--controls', 'stock', '--window', '30', '600']))
        self.assertTrue(notes[0].startswith('relay ticks: 603 (pilot window 30-600, controls restored at 602)'))

    def test_stock_slots_all_capture_on_both_clients(self):
        lines = config_args('--controls', 'stock', '--stock-slots', 'all', '--stock-script', 'demo', '--stock-script2', 'demo2')
        self.assertIn("X4LAN_CONFIG.native_gate_pilot_stock_slots = 'all'", lines)
        self.assertIn("X4LAN_CONFIG.native_gate_pilot_capture_source = {'stock','stock'}", lines)
        self.assertIn('X4LAN_CONFIG.native_gate_pilot_device = {false,false}', lines)
        self.assertIn("X4LAN_CONFIG.native_gate_pilot_stock_script = {'demo','demo2'}", lines)
        self.assertFalse(any('native_gate_pilot_pad' in line for line in lines))
        self.assertIn("X4LAN_CONFIG.native_gate_pilot_stock_script = {false,'demo2'}",
                      config_args('--controls', 'stock', '--stock-slots', 'all', '--stock-script2', 'demo2'))
        self.assertIn('X4LAN_CONFIG.native_gate_pilot_device = {false,false}', config_args('--controls', 'stock', '--stock-slots', 'all',
                                                                                         '--device2', 'none'))
        for extra in (['--stock-slots', 'all'], ['--controls', 'stock', '--stock-script2', 'demo2'],
                      ['--controls', 'stock', '--stock-slots', 'all', '--predict2', 'observe'],
                      ['--controls', 'stock', '--stock-slots', '2']):
            with self.assertRaises(SystemExit, msg=extra), contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
                sandbox.main(['config', *extra])
        present = config_args('--controls', 'stock', '--stock-slots', 'all', '--present')
        self.assertIn('X4LAN_CONFIG.native_gate_pilot_present = true', present)
        self.assertIn("X4LAN_CONFIG.native_gate_pilot_present = 'camera'",
                      config_args('--controls', 'stock', '--stock-slots', 'all', '--present', 'camera'))
        for extra in (['--controls', 'stock', '--present'], ['--present'],
                      ['--controls', 'stock', '--stock-slots', 'all', '--present', '--camera-follow']):
            with self.assertRaises(SystemExit, msg=extra), contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
                sandbox.main(['config', *extra])
        # The block arms both clients in the probe and passes the option checker.
        lines = [line.replace('@X4LANE_CLIENTS@', '2') for line in lines]
        for slot in (1, 2):
            lua = LuaRuntime(unpack_returned_tuples=True)
            lua.execute('X4LAN_CONFIG={}')
            lua.execute('\n'.join(line for line in lines if line.startswith('X4LAN_CONFIG.')))
            cfg = lua.globals().X4LAN_CONFIG
            self.assertEqual(cfg.native_gate_pilot_capture_source[slot], 'stock')
            self.assertFalse(cfg.native_gate_pilot_device[slot])

    def test_stock_controls_refuse_what_needs_a_slot_one_pilot_record(self):
        for extra in (['--stock-script', 'demo'], ['--controls', 'stock', '--script-pilots'],
                      ['--controls', 'stock', '--predict1', 'observe'],
                      ['--controls', 'stock', '--device2', 'none'], ['--controls', 'stock', '--no-capture-log'],
                      ['--controls', 'stock', '--stock-script', 'other']):
            with self.assertRaises(SystemExit, msg=extra), contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
                sandbox.main(['config', *extra])
        # Client 2's prediction stays possible: it predicts its own slot 2; hit claims combine with stock.
        self.assertTrue(any('native_gate_pilot_predict' in line for line in config_args('--controls', 'stock', '--predict2', 'observe')))
        self.assertIn("X4LAN_CONFIG.native_gate_hit_claims = 'victim'", config_args('--controls', 'stock', '--hit-claims', 'victim'))

    def test_the_generated_block_passes_the_probe(self):
        lines = [line.replace('@X4LANE_CLIENTS@', '2') for line in config_args('--controls', 'stock', '--stock-script', 'demo')]
        for slot in (1, 2):
            lua = LuaRuntime(unpack_returned_tuples=True)
            lua.execute('X4LAN_CONFIG={}')
            lua.execute('\n'.join(line for line in lines if line.startswith('X4LAN_CONFIG.')))
            cfg = lua.globals().X4LAN_CONFIG
            self.assertEqual(cfg.native_gate_pilot_capture_source[slot], 'stock' if slot == 1 else 'device')
            self.assertEqual(bool(cfg.native_gate_pilot_device[slot]), slot != 1)


def stock_config(**override):
    lines = [line.replace('@X4LANE_CLIENTS@', '2') for line in config_args('--controls', 'stock')]
    # The base overlay options the sandbox block relies on (merged_config checks them).
    base = [f'X4LAN_CONFIG.{name} = true' for name in ('native_gate_world_identities', 'native_gate_agent_rng', 'native_gate_ship_rng',
                                                       'native_gate_bullet_rng', 'native_gate_nav_rng', 'native_gate_serial_pathing')]
    text = '\n'.join(base + lines) + '\nX4LAN_CONFIG.native_gate_snapshot_sha = "' + 'a'*64 + '"\n'
    for key, value in override.items():
        text = '\n'.join(line for line in text.splitlines() if not line.startswith(f'X4LAN_CONFIG.{key} ')) + '\n'
        if value is not None:
            text += f'X4LAN_CONFIG.{key} = {value}\n'
    return text


def stock_failures(text):
    return [f['rule'] for f in options.probe_rules(text, set(re.findall(r'X4LAN_CONFIG\.(native_gate_\w+)\s*=\s*true\b', text)))]


class StockOptionTests(unittest.TestCase):
    def test_the_sandbox_block_has_no_stock_failures(self):
        self.assertEqual(stock_failures(stock_config()), [])
        self.assertEqual(stock_failures(stock_config(native_gate_pilot_stock_script="'demo'")), [])

    def test_pilot_fire_is_allowed_with_stock_slots(self):
        for fire in ("'record'", "'script'"):
            self.assertEqual(stock_failures(stock_config(native_gate_pilot_fire=fire, native_gate_actor_rng_all='true')), [])

    def test_stock_rules(self):
        cases = {
            'native_gate_pilot_capture_source \'stock\' needs native_gate_pilot_stock = true': dict(native_gate_pilot_stock=None),
            "native_gate_pilot_stock needs native_gate_pilot_source = 'wire'": dict(native_gate_pilot_source="'script'"),
            'native_gate_pilot_stock needs native_gate_pilot_suppress_local': dict(native_gate_pilot_suppress_local=None),
            'native_gate_pilot_stock needs native_gate_pilot_inputs': dict(native_gate_pilot_inputs=None),
            'native_gate_pilot_stock needs the snapshot boot': dict(native_gate_snapshot_sha=None),
            "native_gate_pilot_stock needs native_gate_pilot_capture_source 'stock' exactly on the stock slots' clients and 'device' on the "
            "others (e.g. {'stock','device'})": dict(native_gate_pilot_capture_source="{'device','stock'}"),
            "native_gate_pilot_stock needs native_gate_pilot_device = false on the stock slots' clients and a device on the others "
            "(e.g. {false,<device>})": dict(native_gate_pilot_device="{'kbm','xinput'}"),
            "native_gate_pilot_stock excludes prediction on the stock slots' clients": dict(native_gate_pilot_predict="{'observe',false}"),
            "native_gate_pilot_stock_script must be 'demo', 'demo2'..'demo4', 'aim', a list of {tick,kind,id,value} or one per client":
                dict(native_gate_pilot_stock_script="'other'"),
            "native_gate_pilot_stock_script per client must be {'demo'|'demo2'|'demo3'|'demo4'|'aim'|false,...}": dict(native_gate_pilot_stock_script="{'demo','x'}"),
            "native_gate_pilot_stock_slots must be 'all' or a list of pilot slots holding 1": dict(native_gate_pilot_stock_slots='{2}'),
            'native_gate_pilot_stock must be true': dict(native_gate_pilot_stock="'yes'"),
        }
        for rule, override in cases.items():
            with self.subTest(rule):
                self.assertIn(rule, stock_failures(stock_config(**override)))
        self.assertIn('native_gate_pilot_stock_script needs native_gate_pilot_stock',
                      stock_failures(stock_config(native_gate_pilot_stock=None, native_gate_pilot_stock_script="'demo'")))
        # Prediction on client 2 only is fine; so are hit claims (they need the pilot slots, not records).
        self.assertEqual(stock_failures(stock_config(native_gate_pilot_predict="{false,'observe'}")), [])
        # Slots 'all': both clients capture, no device, scripts per client; prediction on client 2 is then refused.
        both = dict(native_gate_pilot_stock_slots="'all'", native_gate_pilot_capture_source="{'stock','stock'}",
                    native_gate_pilot_device='{false,false}', native_gate_pilot_pad=None)
        self.assertEqual(stock_failures(stock_config(**both, native_gate_pilot_stock_script="{'demo','demo2'}")), [])
        self.assertEqual(stock_failures(stock_config(**both, native_gate_pilot_stock_script="{false,'demo2'}")), [])
        self.assertTrue(stock_failures(stock_config(**both, native_gate_pilot_predict="{false,'observe'}")))
        self.assertTrue(stock_failures(stock_config(**dict(both, native_gate_pilot_capture_source="{'stock','device'}"))))
        self.assertEqual(stock_failures(stock_config(**dict(both, native_gate_pilot_stock_slots='{1,2}'))), [])
        # Cutscene ready on the game clock: true, with serial pathing (the sandbox has it).
        self.assertEqual(stock_failures(stock_config(native_gate_cutscene_ready='true')), [])
        self.assertIn('native_gate_cutscene_ready needs native_gate_serial_pathing',
                      stock_failures(stock_config(native_gate_cutscene_ready='true', native_gate_serial_pathing=None)))
        # Ship death: a stock session; the respawn delay needs it and is a whole number of ticks.
        self.assertEqual(stock_failures(stock_config(**both, native_gate_pilot_death='true')), [])
        self.assertEqual(stock_failures(stock_config(**both, native_gate_pilot_death='true', native_gate_pilot_respawn='300')), [])
        self.assertIn('native_gate_pilot_death must be true', stock_failures(stock_config(**both, native_gate_pilot_death="'yes'")))
        self.assertIn('native_gate_pilot_respawn needs native_gate_pilot_death', stock_failures(stock_config(**both, native_gate_pilot_respawn='300')))
        for value in ('0', '8388608', "'soon'"):
            self.assertIn('native_gate_pilot_respawn must be a delay of 1..8388607 ticks',
                          stock_failures(stock_config(**both, native_gate_pilot_death='true', native_gate_pilot_respawn=value)))
        self.assertIn('native_gate_pilot_death needs native_gate_pilot_stock',
                      stock_failures(stock_config(native_gate_pilot_stock=None, native_gate_pilot_death='true')))
        self.assertEqual(stock_failures(stock_config(**both, native_gate_pilot_death='true', native_gate_pilot_death_suit='true', native_gate_pilot_present='true',
                                                     native_gate_pilot_ship_state='true')), [])
        for rule, extra in (('native_gate_pilot_death_suit needs native_gate_pilot_death', dict(native_gate_pilot_death_suit='true', native_gate_pilot_present='true', native_gate_pilot_ship_state='true')),
                            ('native_gate_pilot_death_suit needs native_gate_pilot_present', dict(native_gate_pilot_death='true', native_gate_pilot_death_suit='true', native_gate_pilot_ship_state='true', native_gate_pilot_present=None)),
                            ('native_gate_pilot_death_suit needs native_gate_pilot_ship_state', dict(native_gate_pilot_death='true', native_gate_pilot_death_suit='true', native_gate_pilot_present='true', native_gate_pilot_ship_state=None)),
                            ('native_gate_pilot_death_suit must be true', dict(native_gate_pilot_death='true', native_gate_pilot_death_suit="'yes'", native_gate_pilot_present='true', native_gate_pilot_ship_state='true'))):
            self.assertIn(rule, stock_failures(stock_config(**both, **extra)))
        # Presentation: stock slots and the render split, no camera follow or anchor.
        self.assertEqual(stock_failures(stock_config(**dict(both, native_gate_pilot_stock_script="{'demo3','demo4'}"))), [])
        self.assertEqual(stock_failures(stock_config(**both, native_gate_pilot_present='true')), [])
        self.assertEqual(stock_failures(stock_config(**both, native_gate_pilot_present="'camera'")), [])
        for rule, override in (('native_gate_pilot_present must be true or camera', dict(both, native_gate_pilot_present="'yes'")),
                               ('native_gate_pilot_present needs native_gate_pilot_stock_slots', dict(native_gate_pilot_present='true')),
                               ('native_gate_pilot_present needs native_gate_render_rng_split',
                                dict(both, native_gate_pilot_present='true', native_gate_render_rng_split=None)),
                               ('native_gate_pilot_present excludes native_gate_pilot_camera_follow',
                                dict(both, native_gate_pilot_present='true', native_gate_pilot_camera_follow='{false,true}')),
                               ('native_gate_pilot_present excludes native_gate_camera_anchor',
                                dict(both, native_gate_pilot_present='true', native_gate_camera_anchor='{client=2,slot=2}'))):
            with self.subTest(rule):
                self.assertIn(rule, stock_failures(stock_config(**override)))
        self.assertFalse([f for f in stock_failures(stock_config(native_gate_hit_claims="'victim'")) if 'stock' in f])


if __name__ == '__main__':
    unittest.main()
