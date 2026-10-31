"""INERT SHOT capture/undo: LuaJIT doubles and an interpreter over the actual MD capture cues.

The interpreter runs State, Shield and Hull (with their Classify/Count libraries) on a Python
model of one victim callback. Native effects are modelled only as far as the rule needs them: a
restore adds the callback's delta back. Any MD syntax it does not know fails the test. This checks
the capture rule as written (A:primary damage-dedup-06), not native behaviour.
"""
import re
import unittest
import xml.etree.ElementTree as E
from pathlib import Path
from test_game_adapter import MOCK, LuaRuntime
from test_bubble_adapter import EXTRA

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / 'src/extension/x4_lan_lab/ui'
MD = ROOT / 'src/extension/x4_lan_lab/md/x4lan_shot_capture.xml'
WALL = "player.systemtime.{'%Y-%m-%dT%H:%M:%S'}"


def idx(name):
    return "S['%s']" % name if name.startswith('cap') else name


TOKENS = [
    (r'\[([^\[\]]+)\]\.min', r'min(\1)'),
    (re.escape(WALL), "P['wall']"), (r'player\.timewarp\.factor', "P['timewarp']"),
    (r'player\.age / 1s', "P['age']"), (r'player\.age', "P['age']"),
    (r'@\$arena\.\$healthProbe', 'False'),
    (r'\$capVictims\.indexof\.\{\$o\}', "P['v']"),
    (r'@\$o\.lastattacker', "P['att']"),
    (r'\$arena\.\$ships\.indexof\.\{\$att\}', "P['ai']"),
    (r'player\.controlled == \$arena\.\$ships\.\{\$capMe\}', "(P.get('controlled', S['capMe']) == S['capMe'])"),
    (r'\$o\.(shield|maxshield|hull|maxhull)\b', r"P['\1']"),
    (r'\$capCollisions\.count', "len(S['coll'])"),
    (r'\$capCollisions\.\{\$j\}\.\{(\d)\}', r"S['coll'][j-1][\1-1]"),
    (r'@?\$arena\.\$(shot|health)(\w+)\.\{\$(\w+)\}', lambda m: "S['%s%s'][%s]" % (m[1], m[2], idx(m[3]))),
    (r'@?\$arena\.\$dead\.\{\$(\w+)\}', lambda m: "S['dead'][%s]" % idx(m[1])),
    (r'@?\$arena\.\$(shotFailed|shotApplying|shotStateAt|host)\b', r"S['\1']"),
    (r'\$cap(?!ture\b)(\w+)\.\{\$(\w+)\}', lambda m: "S['cap%s'][%s]" % (m[1], idx(m[2]))),
    (r'\$cap(?!ture\b)(\w+)\b', r"S['cap\1']"),
    (r'\$a\.\$(\w+)', lambda m: "A.get('%s', 0)" % m[1]),
    (r'event\.param == event\.object', "P['self']"),
    (r'\$o\b', "'V'"),
    (r'\$(v|ai|att|collided|fresh|mine|capture|undo|amount|kind|counts|sg|vg|own|other|i)\b', r'\1'),
    (r'\b(\d+(?:\.\d+)?)s\b', r'\1'),
    (r'\bgt\b', '>'), (r'\bge\b', '>='), (r'\blt\b', '<'), (r'\ble\b', '<='),
    (r'\btrue\b', 'True'), (r'\bfalse\b', 'False'),
]


def py(expr):
    out = expr
    for pattern, repl in TOKENS:
        out = re.sub(pattern, repl, out)
    if '$' in out or 'event.' in out or 'player.' in out:
        raise AssertionError('unsupported MD expression: ' + expr)
    return out


ROOT_MD = E.parse(MD).getroot()
LIB = {l.get('name'): l.find('actions') for l in ROOT_MD.iter('library')}
CUES = {c.get('name'): c.find('actions') for c in ROOT_MD.iter('cue')}


def run(cue, S, P, A=None):
    env = dict(S=S, P=P, A=A or {}, min=min, len=len)
    effects = []

    def block(nodes):
        done = None
        for node in nodes:
            tag = node.tag
            if tag in ('do_if', 'do_elseif', 'do_else'):
                if tag == 'do_if': done = False
                if done: continue
                if tag == 'do_else' or eval(py(node.get('value')), env):
                    done = True; block(list(node))
            elif tag == 'do_all':
                for j in range(1, int(eval(py(node.get('exact')), env)) + 1):
                    env[node.get('counter').lstrip('$')] = j; block(list(node))
            elif tag == 'include_actions':
                block(list(LIB[node.get('ref')]))
            elif tag == 'set_value':
                name, exact = node.get('name'), node.get('exact')
                if name == '$o': env['o'] = 'V'; continue
                if name == '$amount': env['amount'] = P['amount']; continue
                if name == '$a': continue
                m = re.fullmatch(r'\$cap(\w+)\.\{\$(\w+)\}', name)
                if m: S['cap' + m[1]][env[m[2]]] = eval(py(exact), env); continue
                m = re.fullmatch(r'\$cap(?!ture$)(\w+)', name)
                if m: S['cap' + m[1]] = eval(py(exact), env); continue
                m = re.fullmatch(r'\$(\w+)', name)
                if not m: raise AssertionError('unsupported set_value ' + name)
                env[m[1]] = eval(py(exact), env)
            elif tag == 'set_object_shield':
                effects.append('shield'); P['shield'] += P['amount']
            elif tag == 'set_object_hull':
                effects.append('hull'); P['hull'] += P['amount']
            elif tag in ('raise_lua_event', 'debug_text'):
                if tag == 'raise_lua_event': effects.append('count')
            else:
                raise AssertionError('unsupported action ' + tag)
    block(list(cue))
    return effects


def state(**kw):
    """Guest machine, piloting ship 2 under generation 4; ship 1 (host) is a remote view."""
    S = dict(shotFailed=False, shotApplying=False, shotStateAt=99.9, host=False, capFailed=False, capState=1,
             capStateAt=99.9, capMe=2, capRemote={1: True, 2: False}, capLifetime=3.8, capUndo=False,
             capShooter={i: 2 for i in range(1, 5)}, capSGen={i: 4 for i in range(1, 5)},
             capVGen={1: 3, 2: 4, 3: 0, 4: 0}, capCount={i: 0 for i in range(1, 5)}, capQuarantine={i: 0 for i in range(1, 5)},
             capCollAge=-1, coll=[], shotAuthority={1: False, 2: True}, shotStrength={1: 40, 2: 40},
             shotCurrentGen={1: 3, 2: 4}, shotVisualMode={1: 1, 2: 1}, shotVisualGen={1: 3, 2: 4},
             shotVisualAt={1: 99.9, 2: 99.9}, healthOwn={1: False, 2: True}, healthAuth={1: 0, 2: 4},
             healthClosed={1: 0, 2: 0}, healthLive={1: False, 2: False}, healthGen={1: 0, 2: 0}, dead={1: False, 2: False})
    S.update(kw); return S


def hit(S, kind, v=1, ai=2, att='A2', amount=40.0, shield=60.0, hull=900.0, age=100.0, **p):
    P = dict(v=v, ai=ai, att=att, amount=amount, shield=shield, maxshield=100.0, hull=hull, maxhull=1000.0, age=age,
             timewarp=1, wall='x', self=True); P.update(p)
    return run(CUES['Shield' if kind == 's' else 'Hull'], S, P), P


def admit(S, age, me=2, lifetime=2.0, revision=None):
    """Run the actual State cue at game time `age` (fresh window)."""
    revision = revision or S['capState'] + 1
    A = dict(revision=revision, stamp='w', **{'from': age - 0.05}, deadline=age + 1, me=me, r1=True, r2=False,
             lifetime=lifetime, undo=False)
    run(CUES['State'], S, dict(age=age, timewarp=1, wall='w'), A)
    S['shotStateAt'] = S['shotVisualAt'][2] = age


class CaptureRuleTests(unittest.TestCase):
    def test_shield_hit_counts_once_and_its_overflow_does_not(self):
        S = state()
        self.assertEqual(hit(S, 's')[0], ['shield', 'count'])
        effects, P = hit(S, 'h', shield=60.0 + 40.0)                 # overflow: restored shield is up
        self.assertEqual(effects, ['hull'])
        self.assertEqual(S['capCount'][1], 1)

    def test_hull_hit_counts_when_the_shield_is_zero(self):
        S = state()
        self.assertEqual(hit(S, 'h', shield=0.0)[0], ['hull', 'count'])
        self.assertEqual(S['capCount'][1], 1)

    def test_every_collision_pair_of_the_frame_is_excluded_from_capture_and_undo(self):
        S = state(capCollAge=100.0, coll=[['X', 'Y'], ['V', 'A2'], ['A2', 'V']])
        self.assertEqual(hit(S, 's')[0], [])
        S2 = state(capCollAge=99.5, coll=[['V', 'A2']])                # an older frame: not this hit
        self.assertEqual(hit(S2, 's')[0], ['shield', 'count'])
        S3 = state(capCollAge=100.0, coll=[['V', 'A1']], capUndo=True)
        self.assertEqual(hit(S3, 's', v=2, ai=1, att='A1')[0], [])   # undo excluded too

    def test_nothing_while_applying_failed_or_stale(self):
        for kw in (dict(shotApplying=True), dict(capFailed=True), dict(shotFailed=True), dict(capStateAt=99.0), dict(shotStateAt=99.0)):
            self.assertEqual(hit(state(**kw), 's')[0], [], kw)

    def test_only_my_own_ship_on_a_remote_authority_view_is_captured(self):
        self.assertEqual(hit(state(), 's', ai=0, att=None)[0], [])
        self.assertEqual(hit(state(shotAuthority={1: True, 2: True}), 's')[0], [])
        self.assertEqual(hit(state(), 's', v=2, ai=2)[0], [])
        self.assertEqual(hit(state(shotStrength={1: 40, 2: 0}), 's')[0], [])

    def test_no_original_bullet_proof_no_capture(self):
        for kw in (dict(shotVisualMode={1: 1, 2: 2}), dict(shotVisualMode={1: 1, 2: 0}),
                   dict(shotVisualGen={1: 3, 2: 3}), dict(shotVisualAt={1: 99.9, 2: 99.5})):
            self.assertEqual(hit(state(**kw), 's')[0], [], kw)

    def test_native_health_fences_before_any_restore(self):
        # Guest shooter: its own HEALTH authority must be owned, current and not closed natively.
        for kw in (dict(healthOwn={1: False, 2: False}), dict(healthAuth={1: 0, 2: 3}), dict(healthClosed={1: 0, 2: 4})):
            self.assertEqual(hit(state(**kw), 's')[0], [], kw)
        # Host shooter on the guest's ship: the victim's live generation must be current and open.
        host = dict(host=True, capMe=1, shotAuthority={1: True, 2: False}, shotVisualMode={1: 1, 2: 1},
                    shotVisualGen={1: 3, 2: 4}, capShooter={i: 1 for i in range(1, 5)}, capSGen={i: 3 for i in range(1, 5)},
                    capVGen={1: 3, 2: 4, 3: 0, 4: 0}, healthLive={1: False, 2: True}, healthGen={1: 0, 2: 4})
        self.assertEqual(hit(state(**host), 's', v=2, ai=1, att='A1')[0], ['shield', 'count'])
        for kw in (dict(healthLive={1: False, 2: False}), dict(healthGen={1: 0, 2: 3}), dict(healthClosed={1: 0, 2: 4})):
            self.assertEqual(hit(state(**dict(host, **kw)), 's', v=2, ai=1, att='A1')[0], [], kw)

    def test_closed_generations_fence_both_sides_before_any_restore(self):
        # A:primary repros: guest on a closed host-victim gen; host shooter on its own closed gen.
        self.assertEqual(hit(state(healthClosed={1: 3, 2: 0}), 's')[0], [])
        host = dict(host=True, capMe=1, shotAuthority={1: True, 2: False}, capShooter={i: 1 for i in range(1, 5)},
                    capSGen={i: 3 for i in range(1, 5)}, healthLive={1: False, 2: True}, healthGen={1: 0, 2: 4})
        self.assertEqual(hit(state(**host), 's', v=2, ai=1, att='A1')[0], ['shield', 'count'])
        self.assertEqual(hit(state(**dict(host, healthClosed={1: 3, 2: 0})), 's', v=2, ai=1, att='A1')[0], [])

    def test_only_the_natively_controlled_ship_captures_or_undoes(self):
        # Lua still says capMe=2, but the player left the controls (or is a passenger).
        for c in (1, None):
            self.assertEqual(hit(state(), 's', controlled=c)[0], [], c)
            self.assertEqual(hit(state(capUndo=True), 's', v=2, ai=1, att='A1', controlled=c)[0], [], c)

    def test_undo_is_off_by_default(self):
        self.assertEqual(hit(state(), 's', v=2, ai=1, att='A1')[0], [])

    def test_diagnostic_undo_restores_replayed_remote_hits_without_counting(self):
        S = state(capUndo=True)
        effects, P = hit(S, 's', v=2, ai=1, att='A1')
        self.assertEqual((effects, P['shield']), (['shield'], 100.0))
        self.assertEqual(hit(S, 'h', v=2, ai=1, att='A1', shield=100.0)[0], ['hull'])
        self.assertEqual(sum(S['capCount'].values()), 0)
        self.assertEqual(hit(state(capRemote={1: False, 2: False}, capUndo=True), 's', v=2, ai=1, att='A1')[0], [])

    def test_quarantine_is_armed_at_admission_not_at_the_first_hit(self):
        # A:primary: a first hit long after startup must count.
        S = state(capShooter={i: 0 for i in range(1, 5)}, capSGen={i: 0 for i in range(1, 5)}, capVGen={i: 0 for i in range(1, 5)})
        admit(S, 50.0)
        self.assertAlmostEqual(S['capQuarantine'][1], 52.25)
        S['capStateAt'] = 99.9; S['shotStateAt'] = S['shotVisualAt'][2] = 99.9
        self.assertEqual(hit(S, 's', age=100.0)[0], ['shield', 'count'])
        # A victim generation change re-keys at the next admission and quarantines in-flight bullets.
        S['shotCurrentGen'][1] = 5; S['shotVisualGen'][2] = 4
        self.assertEqual(hit(S, 's', age=100.05)[0], ['shield'])       # old key: restored, not counted
        admit(S, 100.1)
        self.assertEqual((S['capVGen'][1], S['capCount'][1]), (5, 0))
        S['capStateAt'] = S['shotStateAt'] = S['shotVisualAt'][2] = 100.9
        self.assertEqual(hit(S, 's', age=101.0)[0], ['shield'])        # inside the quarantine
        S['capStateAt'] = S['shotStateAt'] = S['shotVisualAt'][2] = 102.4
        self.assertEqual(hit(S, 's', age=102.5)[0], ['shield', 'count'])

    def test_a_shooter_change_never_credits_the_old_key(self):
        # A:primary: after source 2 -> source 1 with the same gen, old counts must not move.
        S = state(); hit(S, 's')
        S['capMe'] = 1                                                 # Lua's next state, before admission
        self.assertEqual(hit(S, 's', ai=1, att='A1')[0], [])           # no own authority for ship 1 here
        self.assertEqual(S['capCount'][1], 1)

    def test_child_hull_events_are_ignored(self):
        self.assertEqual(hit(state(), 'h', shield=0.0, self=False)[0], [])


@unittest.skipUnless(LuaRuntime, 'LuaJIT unavailable')
class CaptureAdapterTests(unittest.TestCase):
    def make(self, slot='p2', npc=False):
        l = LuaRuntime(unpack_returned_tuples=True); l.execute(MOCK + EXTRA)
        cfg = l.globals().X4LAN_CONFIG
        cfg.slot, cfg.coop, cfg.pilot_health, cfg.shot_claims, cfg.shot_capture = slot, True, True, True, True
        cfg.npc_combat = npc
        l.execute('''
         sectors={};ConvertIDTo64Bit=function(v) return tonumber(v) end;ConvertStringTo64Bit=function(v) return v end
         GetComponentData=function() return 'cluster_14_sector001_macro' end
         P='weapon_ter_s_plasma_01_mk1_macro'
         entries={[P]={hullshielddpshot=40,isbeamweapon=0,shieldonlydpshot=0,hullonlydpshot=0,hullnoshielddpshot=0,
          hullshieldareadpshot=0,shieldonlyareadpshot=0,hullonlyareadpshot=0,hullnoshieldareadpshot=0,bulletspeed=2300}}
         GetMacroData=function(m,k) return k=='infolibrary' and 'weapons' or nil end
         GetLibraryEntry=function(lib,m) return entries[m] end
         api.GetInstalledWeaponMod=function() return false end
         local operational=api.IsComponentOperational
         api.IsComponentOperational=function(id) return id==0x11 or operational(id) end   -- the equipped gun
        ''')
        for file in ('bubble_game.lua', 'motion.lua', 'coop_game.lua', 'health_game.lua', 'shot_game.lua', 'shot_capture.lua'):
            l.execute((UI / file).read_text())
        l.execute('''events['X4LAN.Loaded']();now=10;frames.onUpdate();events['X4LAN_Bubble.Ready']();frames.onUpdate();events['X4LAN_Coop.Ready']()
         client:receiveExtension({'AUTH','1',client.lease,'1','p1','1','p1','p2','1','p2'},now)
         now=10.1;frames.onUpdate();events['X4LAN_Health.Ready']();frames.onUpdate();events['X4LAN_Shot.Ready']()
         frames.onUpdate();events['X4LAN_ShotCapture.Ready']()
         client:receiveExtension({'HGEN','1',client.lease,'1','3','4'},now)
         events['X4LAN_Health.Own'](nil,'50|2026-10-24T03:50:00|0|0|0|0')
         events['X4LAN_Shot.Loadout'](nil,'2|0|0x11,'..P..';')
         events['X4LAN_ShotCapture.Range'](nil,'2|3910')               -- native runtime laser range
         function last(control) local r;for _,q in ipairs(requests) do if q[1]=='X4LAN_ShotCapture' and q[2]==control then r=q[3] end end;return r end
         function tick(t) now=t;frames.onUpdate() end
        ''')
        return l

    def test_state_carries_pilot_remote_ships_and_native_lifetime(self):
        out = self.make().execute('''
         client:receiveExtension({'HBASE','1',client.lease,'2','4','64.5','30'},now);events['X4LAN_Health.Based'](nil,'2|4')
         tick(10.2);local st=last('State')
         return st.me,tostring(st.r1),tostring(st.r2),st.lifetime,st.stamp,st.deadline,tostring(st.undo)''')
        me, r1, r2, life, stamp, deadline, undo = out
        self.assertEqual((me, r1, r2, stamp, deadline, undo), (2, 'true', 'false', '2026-10-24T03:50:00', 51, 'false'))
        self.assertAlmostEqual(life, 1.5 * 3910 / 2300)              # margin * native range / library speed

    def test_npc_refresh_retries_inside_native_freshness_window(self):
        for enabled in (False, True):
            before,after=self.make(npc=enabled).execute('''
             tick(10.2);local first=last('State').revision
             tick(10.31);return first,last('State').revision
            ''')
            self.assertEqual(after-before,1 if enabled else 0)

    def test_no_native_range_or_speed_refuses_capture(self):
        for setup in ("events['X4LAN_ShotCapture.Range'](nil,'2|null')", 'entries[P].bulletspeed=nil'):
            out = self.make().execute(setup + '''
             client:receiveExtension({'HBASE','1',client.lease,'2','4','64.5','30'},now);events['X4LAN_Health.Based'](nil,'2|4')
             tick(10.2);return last('State').me,last('State').lifetime''')
            self.assertEqual(tuple(out), (0, 0), setup)

    def test_native_cumulative_counts_feed_totals_only_for_their_own_shooter(self):
        out = self.make().execute('''
         client:receiveExtension({'HBASE','1',client.lease,'2','4','64.5','30'},now);events['X4LAN_Health.Based'](nil,'2|4')
         local S=X4LAN_BUBBLE_API.shot
         events['X4LAN_ShotCapture.Count'](nil,'1|2|4|3|2');events['X4LAN_ShotCapture.Count'](nil,'1|2|4|3|1')
         events['X4LAN_ShotCapture.Count'](nil,'1|1|4|3|7')             -- admitted for another shooter
         events['X4LAN_ShotCapture.Count'](nil,'1|2|4|9|5')             -- not the current victim generation
         events['X4LAN_ShotCapture.Count'](nil,'2|2|4|4|5')             -- my own ship
         events['X4LAN_ShotCapture.Count'](nil,'1|4|3|2')               -- old 4-field format: ignored
         local t={};for k,v in pairs(S.totals) do t[#t+1]=k..'='..v end
         return table.concat(t,',')''')
        self.assertEqual(out, '2|4|1|3=2')

    def test_a_failure_stops_capture_natively(self):
        out = self.make().execute('''
         X4LAN_BUBBLE_API.shotCapture.frame=function() error('induced') end;tick(10.3)
         return X4LAN_BUBBLE_API.shotCapture.failed,last('Stop')~=nil''')
        self.assertEqual(tuple(out), (True, True))

    def test_inert_without_its_flag(self):
        l = LuaRuntime(unpack_returned_tuples=True); l.execute(MOCK + EXTRA)
        cfg = l.globals().X4LAN_CONFIG; cfg.coop, cfg.pilot_health, cfg.shot_claims = True, True, True
        for file in ('bubble_game.lua', 'motion.lua', 'coop_game.lua', 'health_game.lua', 'shot_game.lua', 'shot_capture.lua'):
            l.execute((UI / file).read_text())
        self.assertIsNone(l.eval('X4LAN_BUBBLE_API.shotCapture'))
        self.assertTrue('shot_capture = false' in (UI / 'config.lua').read_text())
        self.assertTrue('shot_undo = false' in (UI / 'config.lua').read_text())


class CanonicalFloorTests(unittest.TestCase):
    """B3: under a live pilot value only the canonical value decides death; the proxy never does."""
    LIFE = [(r'@?\$arena\.\$healthLive\.\{\$i\}', "S['live']"), (r'\$arena\.\$healthValue\.\{\$i\}\.\{1\}', "S['value']"),
            (r'@?\$arena\.\$healthProxyLost\.\{\$i\}', "S['lost']"), (r'\$ship\.isoperational', "P['op']"),
            (r'@?\$ship\.hullpercentage', "P['hull']"), (r'\ble\b', '<='), (r'\btrue\b', 'True')]

    def tick(self, S, P):
        node = [n for n in E.parse(ROOT / 'src/extension/x4_lan_lab/md/x4lan_life.xml').iter('do_if')
                if n.get('value') == '$arena.$host and not $arena.$dead.{$i}'][0]
        logs = []

        def ev(expr):
            for pattern, repl in self.LIFE: expr = re.sub(pattern, repl, expr)
            if '$' in expr: raise AssertionError('unsupported: ' + expr)
            return eval(expr, dict(S=S, P=P))

        def block(nodes):
            done = None
            for n in nodes:
                if n.tag in ('do_if', 'do_elseif', 'do_else'):
                    if n.tag == 'do_if': done = False
                    if done: continue
                    if n.tag == 'do_else' or ev(n.get('value')): done = True; block(list(n))
                elif n.tag == 'set_value':
                    name = {'$arena.$dead.{$i}': 'dead', '$arena.$healthProxyLost.{$i}': 'lost'}[n.get('name')]
                    S[name] = ev(n.get('exact'))
                elif n.tag == 'debug_text': logs.append(n.get('text').split(' ')[1])
                else: raise AssertionError('unsupported action ' + n.tag)
        block(list(node))
        return logs

    def test_live_pilot_value_alone_decides_death(self):
        S = dict(live=True, value=50, lost=False, dead=False)
        self.assertEqual((self.tick(S, dict(op=True, hull=0.5)), S['dead']), ([], False))      # proxy hull: a view
        S['value'] = 1.0
        self.assertEqual((self.tick(S, dict(op=True, hull=80)), S['dead']), (["SHIP_LETHAL"], True))

    def test_a_lost_proxy_is_latched_and_never_becomes_a_death(self):
        # A:primary 04:00: healthy canonical 50 + lost local proxy must not kill the live player later.
        S = dict(live=True, value=50, lost=False, dead=False)
        self.assertEqual(self.tick(S, dict(op=False, hull=0)), ['HEALTH_PROXY_LOST'])
        self.assertEqual((self.tick(S, dict(op=False, hull=0)), S['dead'], S['lost']), ([], False, True))
        S['live'] = False                                            # even if the live value ends
        self.assertEqual((self.tick(S, dict(op=False, hull=0)), S['dead']), ([], False))
        S['live'], S['value'] = True, 1.0                            # a canonical lethal value still kills
        self.assertEqual((self.tick(S, dict(op=False, hull=0)), S['dead']), (["SHIP_LETHAL"], True))

    def test_without_a_live_value_the_proxy_decides_as_before(self):
        for hull, dead in ((1.0, True), (50, False)):
            S = dict(live=False, value=0, lost=False, dead=False)
            self.tick(S, dict(op=True, hull=hull))
            self.assertEqual(S['dead'], dead)
        health = (ROOT / 'src/extension/x4_lan_lab/md/x4lan_health.xml').read_text()
        self.assertTrue('set_object_min_hull object="$ship" exact="2"' not in health)
        self.assertTrue('<append_to_list name="$arena.$healthProxyLost" exact="false"/>' in health)


if __name__ == '__main__':
    unittest.main()
