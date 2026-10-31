"""Per-script RNG census: join SCRIPT_RNG rows of two clients by script key.

native_gate_script_rng keys every AI script / MD cue execution on the sim
thread by portable identity (saved owner ID or cue name, script, entry, per-tick
ordinal). Each SCRIPT_RNG_ROW carries that key and the generator steps the
scope made (draws, -1 above the native cap). Rows are joined by key: a script
present on one client only, or with a different draw count, is a divergence
lead attributed to its script/cue name. The per-tick SCRIPT_RNG status lines
give order-independent keyset/drawset sums; the first tick where they differ
is where the clients stopped running the same scripts with the same draws.

native_gate_md_resume_rng rows (MD_RESUME_RNG_ROW) are the cue delays the
snapshot load re-draws; they join by key and must agree on delay and draws.
natural_differs counts joined delays whose native draw word differed between
the clients, i.e. delays that would have diverged without the key.

native_gate_region_rng rows (REGION_RNG_ROW) are hazard DamageField passes
(one per changed due time). They join by (region, field, n-th pass): both
clients must run the n-th pass of a field at the same tick with the same next
due time and draws. word_differs counts same-tick passes whose drawing word
differed (in observe mode: the natural word the jitter came from); the
per-tick REGION_RNG last_dueset gives the first tick whose pass set differed.

native_gate_ai_wait_rng rows (AI_WAIT_RNG_ROW) are the saved AI <wait> durations
the snapshot load re-draws (WaitAction::Reattach); they join by key and must
agree on the resulting deadline (due) and draws. pre counts generator steps
the loading thread made between two Wait restores (unkeyed draws elsewhere in
the import); rows with pre!=0 are listed per client.

SCRIPT_RNG_ROW caller= is the caller's word at scope entry: for an outermost
row the natural sim-thread word the key replaced. Joined outermost rows with a
different caller show where in the frame the natural stream first differed
(the natural section names the previous agreeing row: the unkeyed draw lies
between the two). With native_gate_script_rng_natural, outermost UNKEYED
entries (owners without a saved ID: distress drones, escape pods, spawned
ships) export state=5 rows: they run on the natural word (seed) and join by
their census key; the unkeyed section compares their tick, natural word and
draws. why=2 on a keyed row marks an unmapped owner keyed by
native_gate_script_rng_unmapped='key'. first_unkeyed_tick is the first tick
whose count of unkeyed entries differs (status lines, no option needed).

native_gate_spawn_rng rows (SPAWN_RNG_ROW) are ShipGenerator::Generate calls
with the caller's explicit generator state (job, god entry, create_ship seed).
They join by (state, n-th use of that state): both clients must generate at the
same tick and phase from the same state; entry_differs counts joined calls that
started on different words (observe mode: the loadout would differ), and in
key mode exit/draws must match.

A row present on one client whose record index the other client evicted
from its native ring before the probe drained it is not a divergence: both
clients number their keyed scopes identically until they really differ, and
the eviction window moves with the drain timing (long-L8: tick-0
NPC_State_Machines and tick-1 FactionGoal rows with equal per-tick key sums).
Such rows are counted as masked, never as one-sided.

find_order also classifies each (tick, scope) whose per-ordinal sets differ:
call_order_only when the multiset of result sets is equal (the script looped
over its inputs in another order, e.g. a group), content otherwise.

An N-client run (client1..clientN, N > 2) compares client1 with every other client.
"""
import argparse
import collections
import json
import re
from pathlib import Path

from compare_native_world import bounded_lines
from census_clients import client_slots, against_reference

EVICTED = re.compile(r'NATIVE_GATE SCRIPT_RNG_ROW index=(\d+) evicted=1')
ROW = re.compile(r'NATIVE_GATE SCRIPT_RNG_ROW index=(\d+) tick=(\d+) src=(\d+) state=(\d+) kind=(\d+) inst=(\d+) depth=(\d+) '
                 r'owner=([0-9a-f]{16}) stream=([0-9a-f]{16}) detail=([0-9a-f]{16}) ord=(\d+) key=([0-9a-f]{16}) '
                 r'parent=([0-9a-f]{16}) seeded=(\d) seed=([0-9a-f]{16}) exit=([0-9a-f]{16}) draws=(-?\d+) name=(\S+)'
                 r'(?: caller=([0-9a-f]{16}) why=(\d+))?')
TICK = re.compile(r'NATIVE_GATE SCRIPT_RNG tick=(\d+) .* last_tick=(\d+) last_entries=(\d+) last_outer=(\d+) last_child=(\d+) '
                  r'last_unkeyed=(\d+) last_draws=(\d+) last_unknown=(\d+) last_keyset=([0-9a-f]{16}) last_drawset=([0-9a-f]{16})')
STOP = re.compile(r'NATIVE_GATE SCRIPT_RNG_STOP (installed=.*)$')
RESUME = re.compile(r'NATIVE_GATE MD_RESUME_RNG_ROW index=(\d+) tick=(\d+) owner=([0-9a-f]{16}) slot=(\d+) '
                    r'activation=([0-9a-f]{16}) ord=(\d+) key=([0-9a-f]{16}) natural=([0-9a-f]{16}) exit=([0-9a-f]{16}) '
                    r'draws=(-?\d+) delay=([0-9a-f]{16}) unnamed=(\d+) thread=(\d+) name=(\S+)')
RESUME_STOP = re.compile(r'NATIVE_GATE MD_RESUME_RNG_STOP (armed=.*)$')
AI_WAIT = re.compile(r'NATIVE_GATE AI_WAIT_RNG_ROW index=(\d+) tick=(\d+) owner=([0-9a-f]{16}) script=([0-9a-f]{16}) version=(\d+) '
                     r'slot=(\d+) pc=(\d+) start=([0-9a-f]{16}) ord=(\d+) key=([0-9a-f]{16}) natural=([0-9a-f]{16}) '
                     r'exit=([0-9a-f]{16}) draws=(-?\d+) due=([0-9a-f]{16}) pre=(-?\d+) thread=(\d+) name=(\S+)')
AI_WAIT_STOP = re.compile(r'NATIVE_GATE AI_WAIT_RNG_STOP (armed=.*)$')
FIND = re.compile(r'NATIVE_GATE FIND_ORDER_ROW index=(\d+) tick=(\d+) scope=([0-9a-f]{16}) ord=(\d+) mode=(\d+) raw=(\d+) '
                  r'dups=(\d+) unmapped=(\d+) n=(\d+) moved=(\d+) set=([0-9a-f]{16}) native=([0-9a-f]{16}) name=(\S+) ids=(\S+)')
FIND_STOP = re.compile(r'NATIVE_GATE FIND_ORDER_STOP (armed=.*)$')
REGION = re.compile(r'NATIVE_GATE REGION_RNG_ROW index=(\d+) tick=(\d+) region=([0-9a-f]{16}) field=(\d+) ord=(\d+) mapped=(\d) '
                    r'seeded=(\d) key=([0-9a-f]{16}) word=([0-9a-f]{16}) exit=([0-9a-f]{16}) draws=(-?\d+) last=([0-9a-f]{16}) '
                    r'due=([0-9a-f]{16}) prior=([0-9a-f]{16})')
REGION_TICK = re.compile(r'NATIVE_GATE REGION_RNG tick=\d+ .* last_tick=(\d+) last_calls=(\d+) last_keyed=(\d+) last_passes=(\d+) '
                         r'last_draws=(\d+) last_keyset=([0-9a-f]{16}) last_dueset=([0-9a-f]{16})')
REGION_STOP = re.compile(r'NATIVE_GATE REGION_RNG_STOP (armed=.*)$')
SPAWN = re.compile(r'NATIVE_GATE SPAWN_RNG_ROW index=(\d+) tick=(\d+) phase=(\d+) ui=(\d+) mode=(\d+) normal=(\d+) '
                   r'state=([0-9a-f]{16}) key=([0-9a-f]{16}) caller=([0-9a-f]{16}) entry=([0-9a-f]{16}) exit=([0-9a-f]{16}) draws=(-?\d+)')
SPAWN_STOP = re.compile(r'NATIVE_GATE SPAWN_RNG_STOP (armed=.*)$')
SOURCES = {1: 'exec', 2: 'range', 3: 'event', 4: 'stack', 5: 'cue', 6: 'timed', 7: 'wait', 8: 'raise'}


def parse(lines):
    rows, ticks, status, evicted, resume, resume_status, unkeyed, evicted_index = {}, {}, None, 0, {}, None, {}, set()
    ai_wait, ai_wait_status, ai_wait_evicted = {}, None, 0
    find, find_status = {}, None
    region, region_ticks, region_status, region_evicted = [], {}, None, 0
    spawn, spawn_status, spawn_evicted = [], None, 0
    for line in lines:
        if 'NATIVE_GATE SPAWN_RNG' in line:
            r = SPAWN.search(line)
            if r: spawn.append(dict(index=int(r[1]), tick=int(r[2]), phase=int(r[3]), ui=r[4] == '1', mode=int(r[5]), normal=r[6] == '1',
                                    state=r[7], key=r[8], caller=r[9], entry=r[10], exit=r[11], draws=int(r[12])))
            elif 'SPAWN_RNG_ROW' in line and 'evicted=1' in line: spawn_evicted += 1
            else:
                st = SPAWN_STOP.search(line.rstrip())
                if st: spawn_status = st[1]
            continue
        if 'NATIVE_GATE FIND_ORDER' in line:
            r = FIND.search(line)
            if r: find[(int(r[2]), r[3], int(r[4]))] = dict(name=r[13], raw=int(r[6]), dups=int(r[7]), unmapped=int(r[8]), n=int(r[9]),
                                                            set=r[11], native=r[12], ids=r[14])
            else:
                s = FIND_STOP.search(line.rstrip())
                if s: find_status = s[1]
            continue
        if 'NATIVE_GATE REGION_RNG' in line:
            r = REGION.search(line)
            if r: region.append(dict(index=int(r[1]), tick=int(r[2]), region=r[3], field=int(r[4]), ordinal=int(r[5]),
                                     mapped=r[6] == '1', seeded=r[7] == '1', word=r[9], draws=int(r[11]), last=r[12], due=r[13]))
            elif 'REGION_RNG_ROW' in line and 'evicted=1' in line: region_evicted += 1
            else:
                t = REGION_TICK.search(line)
                if t: region_ticks[int(t[1])] = dict(calls=int(t[2]), keyed=int(t[3]), passes=int(t[4]), dueset=t[7])
                s = REGION_STOP.search(line.rstrip())
                if s: region_status = s[1]
            continue
        if 'NATIVE_GATE AI_WAIT_RNG' in line:
            r = AI_WAIT.search(line)
            if r: ai_wait[r[10]] = dict(owner=r[3], name=r[17], pc=int(r[7]), draws=int(r[13]), due=r[14], natural=r[11],
                                        pre=int(r[15]))
            elif 'AI_WAIT_RNG_ROW' in line and 'evicted=1' in line: ai_wait_evicted += 1
            else:
                s = AI_WAIT_STOP.search(line.rstrip())
                if s: ai_wait_status = s[1]
            continue
        if 'NATIVE_GATE MD_RESUME_RNG' in line:
            r = RESUME.search(line)
            if r: resume[r[7]] = dict(name=r[14], draws=int(r[10]), delay=r[11], natural=r[8], unnamed=r[12] == '1')
            else:
                s = RESUME_STOP.search(line.rstrip())
                if s: resume_status = s[1]
            continue
        if 'NATIVE_GATE SCRIPT_RNG' not in line: continue
        m = ROW.search(line)
        if m:
            row = dict(tick=int(m[2]), source=SOURCES.get(int(m[3]), m[3]), child=m[4] == '2', kind=int(m[5]),
                       owner=m[8], ordinal=int(m[11]), draws=int(m[17]), name=m[18], index=int(m[1]), seed=m[15],
                       caller=m[19], why=int(m[20]) if m[20] else 0, outer=m[4] == '1')
            (unkeyed if m[4] == '5' else rows)[m[12]] = row
            continue
        if 'SCRIPT_RNG_ROW' in line and 'evicted=1' in line:
            evicted += 1;e = EVICTED.search(line)
            if e: evicted_index.add(int(e[1]))
            continue
        t = TICK.search(line)
        if t:
            ticks[int(t[2])] = dict(entries=int(t[3]), outer=int(t[4]), child=int(t[5]), unkeyed=int(t[6]), draws=int(t[7]),
                                    unknown=int(t[8]), keyset=t[9], drawset=t[10])
            continue
        s = STOP.search(line.rstrip())
        if s: status = s[1]
    return dict(rows=rows, unkeyed=unkeyed, ticks=ticks, status=status, evicted=evicted, evicted_index=evicted_index,
                resume=resume, resume_status=resume_status,
                ai_wait=ai_wait, ai_wait_status=ai_wait_status, ai_wait_evicted=ai_wait_evicted, find=find, find_status=find_status,
                region=region, region_ticks=region_ticks, region_status=region_status, region_evicted=region_evicted,
                spawn=spawn, spawn_status=spawn_status, spawn_evicted=spawn_evicted)


def analyse_find(clients):
    """FIND_ORDER rows join by (tick, scope key, ordinal). set differs: the
    candidates differ (membership); native differs with an equal set: only
    the native order differs, which sort mode removes."""
    a, b = clients[0].get('find', {}), clients[1].get('find', {})
    out = dict(rows=[len(a), len(b)], joined=0, set_mismatch=0, order_only=0, count_mismatch=0, only=[0, 0],
               duplicates=[sum(r['dups'] > 0 for r in a.values()), sum(r['dups'] > 0 for r in b.values())],
               unmapped=[sum(r['unmapped'] > 0 for r in a.values()), sum(r['unmapped'] > 0 for r in b.values())],
               status=[c.get('find_status') for c in clients], first_set_mismatch=None, first_order_only=None, examples=[])
    for key in sorted(a.keys() | b.keys()):
        x, y = a.get(key), b.get(key)
        if not (x and y):
            out['only'][0 if x else 1] += 1;continue
        out['joined'] += 1
        if x['n'] != y['n'] or x['raw'] != y['raw']: out['count_mismatch'] += 1
        if x['set'] != y['set']:
            out['set_mismatch'] += 1
            if out['first_set_mismatch'] is None: out['first_set_mismatch'] = key[0]
            if len(out['examples']) < 8:
                ia, ib = set(x['ids'].split(',')), set(y['ids'].split(','))
                out['examples'].append(dict(tick=key[0], scope=key[1], ord=key[2], name=x['name'], n=[x['n'], y['n']], raw=[x['raw'], y['raw']],
                                            dups=[x['dups'], y['dups']], only_a=sorted(ia - ib)[:8], only_b=sorted(ib - ia)[:8]))
        elif x['native'] != y['native']:
            out['order_only'] += 1
            if out['first_order_only'] is None: out['first_order_only'] = key[0]
    scopes = collections.defaultdict(lambda: ([], []))
    for side, rows in enumerate((a, b)):
        for (tick, scope, ordinal), r in rows.items(): scopes[(tick, scope)][side].append((ordinal, r['set'], r['name']))
    out.update(scopes_differing=0, call_order_only=0, content=0, first_content_mismatch=None, scope_examples=[])
    for (tick, scope), (x, y) in sorted(scopes.items()):
        if sorted(x) == sorted(y): continue
        out['scopes_differing'] += 1
        same = collections.Counter(v[1] for v in x) == collections.Counter(v[1] for v in y)
        out['call_order_only' if same else 'content'] += 1
        if not same and out['first_content_mismatch'] is None: out['first_content_mismatch'] = tick
        if len(out['scope_examples']) < 8:
            out['scope_examples'].append(dict(tick=tick, scope=scope, name=(x or y)[0][2], calls=[len(x), len(y)],
                                              kind='call_order_only' if same else 'content'))
    return out


def analyse_region(clients):
    def passes(rows):
        out, seen = {}, collections.Counter()
        for r in sorted(rows, key=lambda r: r['index']):
            seen[(r['region'], r['field'])] += 1
            out[(r['region'], r['field'], seen[(r['region'], r['field'])])] = r
        return out
    a, b = (passes(c.get('region', [])) for c in clients)
    out = dict(rows=[len(a), len(b)], evicted=[c.get('region_evicted', 0) for c in clients], joined=0, only=[0, 0], tick_mismatch=0,
               due_mismatch=0, draw_mismatch=0, word_differs=0, unmapped=[sum(not r['mapped'] for r in x.values()) for x in (a, b)],
               first_mismatch=None, first_dueset_tick=None, status=[c.get('region_status') for c in clients], examples=[])
    for key in sorted(a.keys() | b.keys(), key=lambda k: (min(r['tick'] for r in (a.get(k), b.get(k)) if r), k)):
        x, y = a.get(key), b.get(key)
        if not (x and y):
            out['only'][0 if x else 1] += 1;bad = ['only']
        else:
            out['joined'] += 1;bad = [f for f in ('tick', 'due', 'draws') if x[f] != y[f]]
            for f in bad: out[{'tick': 'tick_mismatch', 'due': 'due_mismatch', 'draws': 'draw_mismatch'}[f]] += 1
            out['word_differs'] += x['tick'] == y['tick'] and x['word'] != y['word']
        if not bad: continue
        example = dict(region=key[0], field=key[1], n=key[2], fields=bad, tick=[x and x['tick'], y and y['tick']],
                       word=[x and x['word'], y and y['word']], due=[x and x['due'], y and y['due']])
        if out['first_mismatch'] is None: out['first_mismatch'] = example
        if len(out['examples']) < 6: out['examples'].append(example)
    ticks = sorted(clients[0].get('region_ticks', {}).keys() & clients[1].get('region_ticks', {}).keys())
    out['first_dueset_tick'] = next((t for t in ticks if clients[0]['region_ticks'][t]['dueset'] != clients[1]['region_ticks'][t]['dueset']), None)
    return out


def analyse_spawn(clients):
    def uses(rows):
        out, seen = {}, collections.Counter()
        for r in sorted(rows, key=lambda r: r['index']):
            seen[r['state']] += 1;out[(r['state'], seen[r['state']])] = r
        return out
    a, b = (uses(c.get('spawn', [])) for c in clients)
    out = dict(rows=[len(a), len(b)], evicted=[c.get('spawn_evicted', 0) for c in clients], boot=[sum(r['phase'] < 3 for r in x.values()) for x in (a, b)],
               joined=0, only=[0, 0], tick_mismatch=0, entry_differs=0, exit_mismatch=0, draw_mismatch=0, abnormal=[0, 0],
               first_mismatch=None, status=[c.get('spawn_status') for c in clients], examples=[])
    for side, x in enumerate((a, b)): out['abnormal'][side] = sum(not r['normal'] for r in x.values())
    for key in sorted(a.keys() | b.keys(), key=lambda k: ((a.get(k) or b.get(k))['index'], k)):
        x, y = a.get(key), b.get(key)
        if not (x and y):
            out['only'][0 if x else 1] += 1;bad = ['only']
        else:
            out['joined'] += 1
            bad = [f for f, n in (('tick', 'tick_mismatch'), ('exit', 'exit_mismatch'), ('draws', 'draw_mismatch')) if x[f] != y[f]]
            for f, n in (('tick', 'tick_mismatch'), ('exit', 'exit_mismatch'), ('draws', 'draw_mismatch')):
                if f in bad: out[n] += 1
            if x['entry'] != y['entry']: out['entry_differs'] += 1;bad.append('entry')
        if not bad: continue
        example = dict(state=key[0], n=key[1], fields=bad, tick=[x and x['tick'], y and y['tick']], phase=[x and x['phase'], y and y['phase']],
                       draws=[x and x['draws'], y and y['draws']], entry=[x and x['entry'], y and y['entry']])
        if out['first_mismatch'] is None: out['first_mismatch'] = example
        if len(out['examples']) < 8: out['examples'].append(example)
    return out


def analyse_resume(clients):
    a, b = clients[0].get('resume', {}), clients[1].get('resume', {})
    out = dict(rows=[len(a), len(b)], joined=0, delay_mismatch=0, draw_mismatch=0, only=[0, 0], natural_differs=0,
               status=[c.get('resume_status') for c in clients], examples=[])
    for key in sorted(a.keys() | b.keys()):
        x, y = a.get(key), b.get(key)
        if not (x and y):
            out['only'][0 if x else 1] += 1
            if len(out['examples']) < 6: out['examples'].append(dict(key=key, name=(x or y)['name'], only=0 if x else 1))
            continue
        out['joined'] += 1;out['natural_differs'] += x['natural'] != y['natural']
        bad = [f for f in ('delay', 'draws') if x[f] != y[f]]
        for f in bad: out['delay_mismatch' if f == 'delay' else 'draw_mismatch'] += 1
        if bad and len(out['examples']) < 6: out['examples'].append(dict(key=key, name=x['name'], fields=bad))
    return out


def analyse_ai_wait(clients):
    a, b = clients[0].get('ai_wait', {}), clients[1].get('ai_wait', {})
    out = dict(rows=[len(a), len(b)], evicted=[c.get('ai_wait_evicted', 0) for c in clients], joined=0, drawn=0, due_mismatch=0,
               draw_mismatch=0, only=[0, 0], natural_differs=0, pre_nonzero=[sum(r['pre'] != 0 for r in c.values()) for c in (a, b)],
               status=[c.get('ai_wait_status') for c in clients], examples=[], pre_examples=[])
    for key in sorted(a.keys() | b.keys()):
        x, y = a.get(key), b.get(key)
        if not (x and y):
            out['only'][0 if x else 1] += 1
            r = x or y
            if len(out['examples']) < 6: out['examples'].append(dict(key=key, owner=r['owner'], name=r['name'], pc=r['pc'], only=0 if x else 1))
            continue
        out['joined'] += 1;out['drawn'] += x['draws'] > 0;out['natural_differs'] += x['natural'] != y['natural']
        bad = [f for f in ('due', 'draws') if x[f] != y[f]]
        for f in bad: out['due_mismatch' if f == 'due' else 'draw_mismatch'] += 1
        if bad and len(out['examples']) < 6: out['examples'].append(dict(key=key, owner=x['owner'], name=x['name'], pc=x['pc'], fields=bad))
    for side, rows in enumerate((a, b)):
        for key, r in rows.items():
            if r['pre'] != 0 and len(out['pre_examples']) < 8:
                out['pre_examples'].append(dict(client=side+1, key=key, owner=r['owner'], name=r['name'], pc=r['pc'], pre=r['pre']))
    return out


def analyse_natural(clients):
    """Joined outermost keyed rows by caller word, located in client 1's
    execution order; unkeyed natural rows joined by census key."""
    a, b = clients[0]['rows'], clients[1]['rows']
    ua, ub = clients[0].get('unkeyed', {}), clients[1].get('unkeyed', {})
    joined = [k for k in a.keys() & b.keys() if a[k]['outer'] and a[k]['caller'] and b[k]['caller']]
    bad = {k for k in joined if a[k]['caller'] != b[k]['caller']}
    out = dict(joined=len(joined), caller_mismatch=len(bad), first_tick=min((a[k]['tick'] for k in bad), default=None),
               locations=[], unmapped_keyed=[sum(r['why'] == 2 for r in c['rows'].values()) for c in clients])
    stream = sorted([(r['index'], k, 'keyed') for k, r in a.items() if r['outer'] and r['caller']] +
                    [(r['index'], k, 'unkeyed') for k, r in ua.items()])
    previous, located = {}, set()
    for _, key, kind in stream:
        r = (a if kind == 'keyed' else ua)[key];t = r['tick']
        if kind == 'unkeyed':
            y = ub.get(key);previous[t] = (r, kind, y is not None and y['seed'] == r['seed']);continue
        if key not in b: continue
        if key in bad and t not in located:
            located.add(t)
            if len(out['locations']) < 8:
                p = previous.get(t)
                out['locations'].append(dict(tick=t, first=dict(source=r['source'], name=r['name'], owner=r['owner']),
                                             caller=[r['caller'], b[key]['caller']],
                                             after=p and dict(source=p[0]['source'], name=p[0]['name'], kind=p[1], agreed=p[2])))
        previous[t] = (r, kind, key not in bad)
    u = dict(rows=[len(ua), len(ub)], joined=0, only=[0, 0], seed_mismatch=0, draw_mismatch=0, first_tick=None,
             first_mismatch=None, by_name={}, examples=[])
    names = collections.defaultdict(lambda: dict(rows=[0, 0], only=[0, 0], seed_mismatch=0, first_tick=None))
    for key in sorted(ua.keys() | ub.keys(), key=lambda k: ((ua.get(k) or ub.get(k))['tick'], k)):
        x, y = ua.get(key), ub.get(key);r = x or y;g = names[f"{r['source']}/{r['name']}"]
        for i, row in enumerate((x, y)):
            if row: g['rows'][i] += 1
        if x and y:
            u['joined'] += 1;fields = [f for f in ('seed', 'draws') if x[f] != y[f]]
            u['seed_mismatch'] += 'seed' in fields;u['draw_mismatch'] += 'draws' in fields;g['seed_mismatch'] += 'seed' in fields
        else:
            u['only'][0 if x else 1] += 1;g['only'][0 if x else 1] += 1;fields = ['only']
        if not fields: continue
        if g['first_tick'] is None: g['first_tick'] = r['tick']
        example = dict(tick=r['tick'], source=r['source'], name=r['name'], owner=r['owner'], why=r['why'], fields=fields,
                       seed=[x and x['seed'], y and y['seed']], draws=[x and x['draws'], y and y['draws']])
        if u['first_tick'] is None: u['first_tick'] = r['tick'];u['first_mismatch'] = example
        if len(u['examples']) < 8: u['examples'].append(example)
    u['by_name'] = dict(sorted(names.items(), key=lambda kv: (kv[1]['first_tick'] is None, kv[1]['first_tick'] or 0)))
    return out, u


def analyse(clients):
    a, b = clients[0]['rows'], clients[1]['rows']
    scripts = collections.defaultdict(lambda: dict(rows=[0, 0], joined=0, draw_mismatch=0, only=[0, 0], draws=[0, 0],
                                                   first_mismatch_tick=None, examples=[]))
    masked = [0, 0]
    for key in sorted(a.keys() | b.keys(), key=lambda k: ((a.get(k) or b.get(k))['tick'], k)):
        x, y = a.get(key), b.get(key)
        r = x or y
        if not (x and y) and r.get('index') is not None and r['index'] in clients[1 if x else 0].get('evicted_index', ()):
            masked[0 if x else 1] += 1;continue
        g = scripts[f"{r['source']}/{r['name']}"]
        for i, row in enumerate((x, y)):
            if row: g['rows'][i] += 1;g['draws'][i] += max(row['draws'], 0)
        if x and y:
            g['joined'] += 1
            if x['draws'] == y['draws']: continue
            g['draw_mismatch'] += 1
        else: g['only'][0 if x else 1] += 1
        if g['first_mismatch_tick'] is None: g['first_mismatch_tick'] = r['tick']
        if len(g['examples']) < 4:
            g['examples'].append(dict(tick=r['tick'], key=key, owner=r['owner'], ord=r['ordinal'], child=r['child'],
                                      draws=[x['draws'] if x else None, y['draws'] if y else None]))
    ranked = sorted(scripts.items(), key=lambda kv: (kv[1]['first_mismatch_tick'] is None, kv[1]['first_mismatch_tick'] or 0,
                                                     -kv[1]['draw_mismatch'] - sum(kv[1]['only'])))
    ticks = sorted(clients[0]['ticks'].keys() & clients[1]['ticks'].keys())
    first_keyset = next((t for t in ticks if clients[0]['ticks'][t]['keyset'] != clients[1]['ticks'][t]['keyset']), None)
    first_drawset = next((t for t in ticks if clients[0]['ticks'][t]['drawset'] != clients[1]['ticks'][t]['drawset']), None)
    first_unkeyed = next((t for t in ticks if clients[0]['ticks'][t]['unkeyed'] != clients[1]['ticks'][t]['unkeyed']), None)
    natural, unkeyed = analyse_natural(clients)
    return dict(rows=[len(a), len(b)], evicted=[c['evicted'] for c in clients], masked=masked, status=[c['status'] for c in clients],
                ticks_compared=len(ticks), first_keyset_tick=first_keyset, first_drawset_tick=first_drawset,
                first_unkeyed_tick=first_unkeyed, natural=natural, unkeyed=unkeyed,
                per_tick={t: [clients[0]['ticks'][t], clients[1]['ticks'][t]] for t in ticks[:8]},
                md_resume=analyse_resume(clients), ai_wait=analyse_ai_wait(clients), find_order=analyse_find(clients), region=analyse_region(clients),
                spawn=analyse_spawn(clients),
                scripts=dict(ranked))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('run', type=Path)
    p.add_argument('--out', type=Path)
    args = p.parse_args()
    slots = client_slots(args.run)
    clients = [parse(bounded_lines(args.run/f'client{s}/engine-output.log')) for s in slots]
    result = analyse(clients) if len(slots) == 2 else against_reference(slots, lambda k: analyse([clients[0], clients[k-1]]))
    text = json.dumps(result, indent=2)
    if args.out: args.out.write_text(text+'\n')
    print(text)


if __name__ == '__main__': main()
