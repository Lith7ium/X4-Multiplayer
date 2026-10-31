"""Adversarial sequences against the actual MD fence expressions of md/x4lan_health.xml.

A deliberately tiny interpreter runs the do_if/do_elseif/do_else chain and set_value actions of
one cue on a Python state model; native effects (set_object_*, debug_text, events) are recorded,
not executed. Any MD syntax it does not know fails the test, so the model cannot silently drift
from the script. This checks the fence logic as written, not native MD behaviour.
"""
import re
import unittest
import xml.etree.ElementTree as E
from pathlib import Path

MD = Path(__file__).resolve().parents[1] / 'src/extension/x4_lan_lab/md/x4lan_health.xml'
WALL = "player.systemtime.{'%Y-%m-%dT%H:%M:%S'}"
TOKENS = [
    (r'\[([^\[\]]+)\]\.max', r'max(\1)'),   # before any bracketed rewrite
    (re.escape(WALL), "P['wall']"),
    (r'player\.timewarp\.factor', "P['timewarp']"),
    (r'player\.age / 1s', "P['age']"), (r'player\.age', "P['age']"),
    (r'@\$arena\.\$healthProbe', "S.get('Probe', False)"),
    (r'@?\$arena\.\$shot(\w+)\.\{\$k\}', lambda m: "S['shot%s'][k]" % m[1]),
    (r'@?\$arena\.\$shot(\w+)\.\{\$a\.\$(\w+)\}', lambda m: "S['shot%s'][A.get('%s', 0)]" % (m[1], m[2])),
    (r'@?\$arena\.\$dead\.\{\$a\.\$(\w+)\}', lambda m: "S['dead'][A.get('%s', 0)]" % m[1]),
    (r'@?\$arena\.\$health(\w+)\.\{\$a\.\$(\w+)\}', lambda m: "S['%s'][A.get('%s', 0)]" % (m[1], m[2])),
    (r'@?\$arena\.\$shot(\w+)\.\{(\d+)\}', lambda m: "S['shot%s'][%s]" % (m[1], m[2])),
    (r'\$arena\.\$shot(\w+)(?![\w.{])', lambda m: "S['shot%s']" % m[1]),
    (r'\$arena\.\$host', "P.get('host', True)"),
    (r'@\$(victim|attacker)\.isoperational', "P['operational']"),
    (r'(?<![.\w])\$(k|dead)\b', r'\1'),                 # MD locals of the shot Apply cue (not $arena.$dead)
    (r'@?\$arena\.\$health(\w+)\.\{(\$\w+|\d+)\}', lambda m: "S['%s'][%s]" % (m[1], m[2].lstrip('$') if m[2].startswith('$') else m[2])),
    (r'@?\$arena\.\$dead\.\{(\$\w+)\}', lambda m: "S['dead'][%s]" % m[1].lstrip('$')),
    (r'@\$ship\.isoperational', "P['operational']"),
    (r'\$ship\.(hull|shield)percentage', lambda m: "P.get('%s', -1000)" % m[1]),  # change-only writes (8543b6e)
    (r'@?\$a\.\$(\w+)', lambda m: "A.get('%s', 0)" % m[1]),          # MD null: falsy, never greater
    (r'event\.param3\.\$(\w+)', lambda m: "A.get('%s', 0)" % m[1]),
    (r'\b(\d+(?:\.\d+)?)s\b', r'\1'),
    (r'\bgt\b', '>'), (r'\bge\b', '>='), (r'\blt\b', '<'), (r'\ble\b', '<='),
    (r'\btrue\b', 'True'), (r'\bfalse\b', 'False'),
    (r"(S\['Value'\]\[\w+\])\.\{(\d)\}", lambda m: '%s[%d]' % (m[1], int(m[2]) - 1)),  # MD lists are 1-based
]


def py(expr):
    out = expr
    for pattern, repl in TOKENS:
        out = re.sub(pattern, repl, out)
    if '$' in out or 'player.' in out or 'event.' in out:
        raise AssertionError('unsupported MD expression: ' + expr)
    return out


def cue(name):
    found = [c for c in E.parse(MD).iter('cue') if c.get('name') == name]
    assert len(found) == 1, name
    return found[0].find('actions')


def run(actions, S, A, P, e):
    env = dict(S=S, A=A, P=P, e=e, max=max)
    effects = []

    def block(nodes):
        chain_done = None
        for node in nodes:
            tag = node.tag
            if tag in ('do_if', 'do_elseif', 'do_else'):
                if tag == 'do_if': chain_done = False
                if chain_done: continue
                if tag == 'do_else' or eval(py(node.get('value')), env):
                    chain_done = True; block(list(node))
            elif tag == 'do_all':
                exact = node.get('exact'); counter = (node.get('counter') or '$_').lstrip('$')
                for i in range(1, int(exact if exact.isdigit() else eval(py(exact), env)) + 1):
                    env[counter] = i; block(list(node))
            elif tag == 'set_value':
                name = node.get('name')
                if name in ('$k', '$dead'): env[name[1:]] = eval(py(node.get('exact')), env); continue
                if name in ('$a', '$e', '$ship', '$s', '$d', '$o', '$attacker', '$victim'): continue      # local aliases
                scalar = re.fullmatch(r'\$arena\.\$(shot\w+)', name)
                if scalar: S[scalar[1]] = eval(py(node.get('exact')), env); continue
                target = re.fullmatch(r'\$arena\.\$(health|shot)(\w+)\.\{(\$\w+|\d+)\}', name)
                if not target: raise AssertionError('unsupported set_value ' + name)
                S[target[2] if target[1] == 'health' else 'shot' + target[2]][env[target[3][1:]] if target[3].startswith('$') else int(target[3])] = eval(py(node.get('exact')), env)
            elif tag in ('set_object_hull', 'set_object_shield', 'set_object_min_hull', 'debug_text', 'raise_lua_event', 'apply_attackstrength'):
                effects.append((tag, node.get('exact')))
            else:
                raise AssertionError('unsupported action ' + tag)
    block(list(actions))
    return effects


def guest_state():
    return dict(Auth={1: 0, 2: 0}, Own={1: False, 2: False}, Closed={1: 0, 2: 0}, Top={1: 0, 2: 0},
                KeptAt={1: 0, 2: 0}, Based={1: 0, 2: 0}, dead={1: False, 2: False})


FRESH_P = dict(timewarp=1, wall='2026-10-24T00:50:00', age=100.2, operational=True)
WINDOW = dict(stamp='2026-10-24T00:50:00', **{'from': 100.0}, deadline=101.0)


class AuthorityFenceTests(unittest.TestCase):
    def authority(self, S, **a):
        run(cue('Authority'), S, dict(entity=2, **a), dict(FRESH_P), 2)

    def test_reordered_old_start_never_downgrades_the_current_generation(self):
        S = guest_state(); S['Auth'][2], S['Top'][2], S['Closed'][2] = 5, 5, 2
        self.authority(S, gen=3, own=False)                       # A:primary: G3 after G5, closed G2
        self.assertEqual((S['Auth'][2], S['Own'][2], S['Top'][2]), (5, False, 5))
        self.authority(S, gen=6, own=False)
        self.assertEqual((S['Auth'][2], S['Top'][2]), (6, 6))

    def test_revocation_names_its_generation_and_a_late_old_one_is_refused(self):
        S = guest_state(); S['Auth'][2], S['Top'][2], S['Own'][2] = 5, 5, True
        self.authority(S, gen=0, own=False, ended=3)              # late revoke of an older generation
        self.assertEqual((S['Auth'][2], S['Own'][2], S['Closed'][2]), (5, True, 0))
        self.authority(S, gen=0, own=False)                       # no generation named: refused
        self.assertEqual(S['Auth'][2], 5)
        self.authority(S, gen=0, own=False, ended=5)
        self.assertEqual((S['Auth'][2], S['Own'][2], S['Closed'][2]), (0, False, 5))
        self.authority(S, gen=5, own=False)                       # the ended generation never restarts
        self.authority(S, gen=5, own=True, **WINDOW)
        self.assertEqual((S['Auth'][2], S['Own'][2]), (0, False))

    def test_own_needs_its_own_generation_above_the_fence_and_a_fresh_window(self):
        S = guest_state()
        self.authority(S, gen=7, own=True, **WINDOW)              # before its start: refused
        self.assertFalse(S['Own'][2])
        self.authority(S, gen=7, own=False)
        for bad in (dict(WINDOW, stamp='2026-10-24T00:49:59'), dict(WINDOW, deadline=100.1), {}):
            self.authority(S, gen=7, own=True, **bad)
            self.assertFalse(S['Own'][2], bad)
        P = dict(FRESH_P, timewarp=0)
        run(cue('Authority'), S, dict(entity=2, gen=7, own=True, **WINDOW), P, 2)   # paused
        self.assertFalse(S['Own'][2])
        self.authority(S, gen=7, own=True, **WINDOW)
        self.assertTrue(S['Own'][2])

    def test_a_fresh_start_without_own_never_sets_own(self):
        # A:primary: start G5 own=false, then the same G5 own=false WITH a fresh window set Own.
        S = guest_state()
        self.authority(S, gen=5, own=False); self.authority(S, gen=5, own=False, **WINDOW)
        self.assertEqual((S['Auth'][2], S['Own'][2]), (5, False))
        self.authority(S, gen=5, own=True, **WINDOW)
        self.assertTrue(S['Own'][2])

    def test_keepalive_watchdog_closes_natively_before_a_queued_own(self):
        S = guest_state(); S['Auth'][2], S['Top'][2], S['Own'][2] = 8, 8, True
        S['KeptAt'][2] = 0; P = dict(FRESH_P, age=3.0)
        run(cue('KeepWatchdog'), S, {}, P, None)
        self.assertEqual((S['Auth'][2], S['Own'][2], S['Closed'][2]), (0, False, 8))
        self.authority(S, gen=8, own=True, **WINDOW)              # the queued old own
        self.authority(S, gen=8, own=False)
        self.assertEqual((S['Auth'][2], S['Own'][2]), (0, False))


class HostFenceTests(unittest.TestCase):
    def host(self):
        return dict(Gen={1: 0, 2: 0}, Closed={1: 0, 2: 0}, Live={1: False, 2: False}, Value={1: [0, 0], 2: [0, 0]},
                    At={1: 0, 2: 0}, dead={1: False, 2: False})

    def apply(self, S, gen, hull=80):
        return run(cue('Apply'), S, dict(entity=2, gen=gen, hull=hull, shield=50, **WINDOW), dict(FRESH_P), 2)

    def test_closed_or_older_generations_never_apply_after_release(self):
        S = self.host()
        self.assertTrue(self.apply(S, 4)); self.assertEqual((S['Gen'][2], S['Live'][2]), (4, True))
        run(cue('Release'), S, dict(entity=2, gen=4, closed=True), dict(FRESH_P), 2)
        self.assertEqual((S['Closed'][2], S['Live'][2]), (4, False))
        self.assertEqual(self.apply(S, 4), []); self.assertEqual(self.apply(S, 3), [])
        self.assertTrue(self.apply(S, 5)); self.assertEqual(S['Gen'][2], 5)
        run(cue('Release'), S, dict(entity=2, gen=4, closed=True), dict(FRESH_P), 2)   # late old release
        self.assertEqual((S['Gen'][2], S['Live'][2], S['Closed'][2]), (5, True, 4))

    def test_a_lost_proxy_keeps_canonical_authority_without_physical_writes(self):
        S = self.host()
        effects = run(cue('Apply'), S, dict(entity=2, gen=4, hull=60, shield=20, **WINDOW), dict(FRESH_P, operational=False), 2)
        self.assertEqual([tag for tag, _ in effects if tag.startswith('set_object')], [])
        self.assertEqual((S['Gen'][2], S['Live'][2], S['Value'][2], S['At'][2]), (4, True, [60, 20], FRESH_P['age']))

    def test_stale_window_or_pause_refuses_an_apply(self):
        S = self.host()
        self.assertEqual(run(cue('Apply'), S, dict(entity=2, gen=4, hull=80, shield=50, **WINDOW), dict(FRESH_P, timewarp=0), 2), [])
        self.assertEqual(run(cue('Apply'), S, dict(entity=2, gen=4, hull=80, shield=50, **dict(WINDOW, stamp='x')), dict(FRESH_P), 2), [])
        self.assertFalse(S['Live'][2])


if __name__ == '__main__':
    unittest.main()
