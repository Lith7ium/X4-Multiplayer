"""Camera-dependent simulation census: bullet hit tests (native_gate_bullet_cadence;
native_gate_bullet_diag optional), ship navigation refreshes (native_gate_nav_cadence)
and attention evaluations (native_gate_view_points).

Per client and admitted tick it reads the BULLET_CADENCE status line: Move calls,
hit tests and how many of them scheduled the next test for the same frame or
later, for fixture-gun bullets and all other bullets apart, plus the player
camera sample (mode, anchor, cutscene camera, view-cache indices and the two
camera context caches; UniverseIDs are reduced to anchor/other/none). It reports, against client1:
- the first tick at which each client scheduled any test later than its frame
  (the switch from per-frame to the native 0.1-0.2 s far-from-camera cadence),
- the first tick at which the frame counters or the camera sample differ,
- the first tick at which the fixture BULLET rows (with native_gate_bullet_diag)
  or the fixture STATE_ROWs differ.
With native_gate_bullet_cadence='every' no test is scheduled later and the
counters should agree every tick; with 'observe' they show when and where the
camera-dependent cadence decisions part.
NAV_CADENCE lines give per tick, for fixture controllers and others apart, the
calls and per block (parameters, obstacle probes) the refreshes and their
interval class: now (camera within 1 km), mid (0.5-1 s, within 5 km), slow
(1-2 s). The report adds the first tick of a camera-near choice (now or mid)
per client and the first tick at which the nav counters differ; with 'far'
every refresh is slow, with 'every' every refresh is now. NAV_CADENCE_ROW lines
(fixture controllers and watched saved IDs) are compared per controller; the
report lists the earliest controllers whose state rows part.
VIEW_POINTS lines give per tick the attention evaluations through SetAttention,
the histogram of returned levels 1..11 and, in 'observe', how many differed
from the camera result. With 'players' the counters should agree on every
client every tick; the report gives the first tick at which they differ and
per client the first tick and total of camera-dependent results.
MASS_TRAFFIC lines (native_gate_mass_traffic) give per tick Zone::Move's
traffic updates and how many it skipped because the mass traffic pass held its
lock, the passes still running at the gate tick (busy; with 'sync' waited for),
the MassTraffic thread's RNG word after the last joined pass and its draws, and
the player camera's control state (mode and local PosRot hash). The report gives
per client the first skip, totals and the largest wait, and the first tick at
which the skips, the RNG word, the draws or the camera state differ.
REGION_PASS lines (native_gate_region_pass) give the region manager pass the
same way (joins, busy, waits, its thread's word and draws); the report adds the
first tick at which its word or draws differ.
VIEW_POINTS_ROW lines (native_gate_view_rows) record every attention level change
per object; the report lists the earliest objects whose rows part and, per slot-703
implementation (vtable), how many objects part and from which tick.
GRAVIDAR_DIGEST lines (native_gate_gravidar_digest) carry order-independent
digests of every Gravidar unit's radar factor, scan times (active units only)
and class, of the active-source lists and of the field sectors' factor-pass
deadlines and attention; the report gives the first tick at which each differs.
GRAVIDAR_DIGEST_ROW lines (native_gate_gravidar_digest_rows) are joined by tick,
kind and key; the report gives the first tick whose rows differ, with the
differing units or sectors and their raw values on each client.
GRAVIDAR_DEADLINES lines (native_gate_gravidar_deadlines) give per tick the factor passes
of the field sectors and their new deadlines (cur_dueset); the report gives per
client the re-based sectors, passes by cadence, unmapped passes, the units
re-based at admission and failures, and the first tick whose passes differ.
ATTENTION_ALL lines (native_gate_attention_all) give per tick the object
attention timer calls keyed by portable ID and the first-call due pins; the
report gives the first tick at which they differ and per client the unmapped
calls (objects without a portable ID keep the native schedule).
REGION_CAMERA lines (native_gate_region_camera) give the portable key of the
viewpoint the next region pass is centred on (0: none, the pass uses the
player camera); the report gives the first tick at which it differs and per
client the frames without a stand-in camera.
MOTION_WATCH lines (native_gate_motion_watch) carry each watched ship's pathing
state every admitted tick; the report gives per ship the first tick whose rows
differ and the fields that differ there. MOTION_ORDER (AI move points queued for
a watched ship) and MOTION_TARGET (flight model targets set for it) lines
MOTION_EVENT (boost and travel mode event dispatches for it) and MOTION_REFRESH
(flight parameter refreshes from the ship, 0xea1e80) lines
(native_gate_motion_watch_orders) compare per ship as ordered event sequences:
the report gives the first event whose fields differ (or that only one client
has), its tick on both clients and the differing fields.
EFFECT_ROW lines (native_gate_effect_rows) give per effect element handed to the
creator its owner key, delay, shot time, game time and whether it was deferred;
the report lists the first ticks whose rows differ and which fields differ.
"""
import argparse
import collections
import json
import re
from pathlib import Path

from census_clients import client_slots, against_reference

CADENCE = re.compile(r'NATIVE_GATE BULLET_CADENCE tick=(\d+) mode=(\w+) .*?frame=(\d+) fixture=([\d/]+) other=([\d/]+) '
                     r'.*?first_later=(\d+) unreadable=(\d+) camera=(\S+)')
BULLET = re.compile(r'NATIVE_GATE BULLET (id=\d+ slot=\d+ tick=(\d+) .*)$')
STATE = re.compile(r'NATIVE_GATE STATE_ROW tick=(\d+) id=(\d+) data=(\S+)')
NAV_ROW = re.compile(r'NATIVE_GATE NAV_CADENCE_ROW index=\d+ (tick=(\d+) id=(\d+) saved=([0-9a-f]{16}) .*)$')
VIEW = re.compile(r'NATIVE_GATE VIEW_POINTS tick=(\d+) mode=(\w+) .*?frame=(\d+) source=(\w+) viewpoints=(\d+) ids=\S+ '
                  r'calls=(\d+) evaluations=(\d+) levels=([\d/]+) differ=(\d+)')
MASS = re.compile(r'NATIVE_GATE MASS_TRAFFIC tick=(\d+) mode=(\w+) .*?frame=(\d+) tries=(\d+) skips=(\d+) .*?first_skip=(\d+) '
                  r'joins=(\d+) busy=(\d+) unknown=(\d+) .*?wait_us=(\d+) max_wait_us=(\d+) .*?word=([0-9a-f]{16}) draws=(\d+) '
                  r'total_draws=(\d+)(?:.*? camera=(\S+))?')
REGION = re.compile(r'NATIVE_GATE REGION_PASS tick=(\d+) mode=(\w+) .*?joins=(\d+) busy=(\d+) unknown=(\d+) .*?max_wait_us=(\d+) '
                    r'.*?word=([0-9a-f]{16}) draws=(\d+)')
VIEW_ROW = re.compile(r'NATIVE_GATE VIEW_POINTS_ROW index=\d+ (tick=(\d+) key=([0-9a-f]{16}) klass=(\d+) vtable=([0-9a-f]{8}) .*)$')
GDIG = re.compile(r'NATIVE_GATE GRAVIDAR_DIGEST tick=(\d+) tick=\d+ units=(\d+) unmapped=(\d+) factors=([0-9a-f]{16}) times=([0-9a-f]{16}) '
                  r'classes=([0-9a-f]{16}) active=(\d+)/(\d+) active_digest=([0-9a-f]{16})/([0-9a-f]{16}) active_unmapped=(\d+)/(\d+) stage=(\w+)'
                  r'(?: passive=(\d+) sectors=(\d+) sector_unmapped=(\d+) deadlines=([0-9a-f]{16}) attention=([0-9a-f]{16}))?')
ATTENTION_ALL = re.compile(r'NATIVE_GATE ATTENTION_ALL tick=(\d+) armed=\d+ installed=\d+ frame=(\d+) calls=(\d+) inits=(\d+) '
                           r'total_calls=\d+ total_inits=\d+ ids=(\d+) unmapped=(\d+)')
REGION_CAMERA = re.compile(r'NATIVE_GATE REGION_CAMERA tick=(\d+) mode=(\w+) installed=\d+ frame=(\d+) viewpoint=[0-9a-f]+ '
                           r'key=([0-9a-f]{16}) updates=(\d+) native_frames=(\d+)')
GDIG_ROW = re.compile(r'NATIVE_GATE GRAVIDAR_DIGEST_ROW index=\d+ (?:(evicted)=1|tick=(\d+) kind=(\w) key=(\S+) (.*))$')
GDL = re.compile(r'NATIVE_GATE GRAVIDAR_DEADLINES tick=(\d+) armed=(-?\d+) .*? first=(\d+) rebased=(\d+) due=(\d+) keyed=(\d+) unmapped=(\d+) '
                   r'native=(\d+) unscoped=(\d+) passes=(\d+) low=(\d+) high=(\d+) .*?cur_tick=(\d+) cur_calls=(\d+) '
                   r'cur_passes=(\d+) cur_keyed=(\d+) cur_dueset=([0-9a-f]{16}) .*?admitted=(-?\d+) units_active=(\d+) units_rebased=(\d+) failures=(-?\d+) stage=(\w+)')
MOTION_WATCH = re.compile(r'NATIVE_GATE MOTION_WATCH index=\d+ tick=(\d+) saved=([0-9a-f]{16}) (.*)$')
MOTION_EVENT = re.compile(r'NATIVE_GATE MOTION_(ORDER|TARGET|EVENT|REFRESH) index=\d+ tick=(\d+) saved=([0-9a-f]{16}) (.*)$')
EFFECT_ROW = re.compile(r'NATIVE_GATE EFFECT_ROW index=\d+ tick=(\d+) (key=\S+ klass=\d+ delay=\S+ time=\S+ now=\S+ deferred=\d+ unreadable=\d+)')
NAV = re.compile(r'NATIVE_GATE NAV_CADENCE tick=(\d+) mode=(\w+) .*?frame=(\d+) fixture=([\d/]+) other=([\d/]+) '
                 r'.*?first_now=(\d+) first_mid=(\d+) unreadable=(\d+)')


def portable_camera(sample):
    """Camera sample without process-local UniverseIDs: each cached attached
    component is written as anchor/other/none relative to the camera anchor."""
    parts = sample.split(':')
    if len(parts) < 7: return sample
    anchor = parts[1]
    caches = []
    for part in parts[5:]:
        component, _, count = part.partition('/')
        caches.append(('none' if component == '0' else 'anchor' if component == anchor else 'other')+'/'+count)
    return ':'.join([parts[0], 'anchor' if anchor != '0' else 'none', parts[2], parts[3], parts[4]]+caches)


def lines(path):
    with Path(path).open('rb') as source:
        for raw in source: yield raw.decode('utf-8', 'replace').rstrip('\r\n')


def parse(stream):
    ticks, nav, view, bullets, states = {}, {}, {}, collections.defaultdict(list), collections.defaultdict(list)
    nav_rows = collections.defaultdict(list)
    mass, region = {}, {}
    view_rows = collections.defaultdict(list)
    gdig, gdig_rows, gdl = {}, collections.defaultdict(dict), {}
    gdig_evicted = 0
    attention = {}
    region_camera = {}
    effect_rows = collections.defaultdict(list)
    motion = collections.defaultdict(dict)
    motion_events = {k: collections.defaultdict(list) for k in ('ORDER', 'TARGET', 'EVENT', 'REFRESH')}
    for line in stream:
        if 'NAV_CADENCE_ROW ' in line and (m := NAV_ROW.search(line)):
            nav_rows[f'{m[3]}/{m[4]}'].append(m[1])
        elif 'MASS_TRAFFIC tick=' in line and (m := MASS.search(line)):
            mass[int(m[1])] = dict(mode=m[2], frame=int(m[3]), tries=int(m[4]), skips=int(m[5]), first_skip=int(m[6]),
                                   joins=int(m[7]), busy=int(m[8]), unknown=int(m[9]), wait_us=int(m[10]),
                                   max_wait_us=int(m[11]), word=m[12], draws=int(m[13]), total_draws=int(m[14]),
                                   camera=m[15])
        elif 'GRAVIDAR_DIGEST_ROW ' in line and (m := GDIG_ROW.search(line)):
            if m[1]: gdig_evicted += 1
            else: gdig_rows[int(m[2])][f'{m[3]}/{m[4]}'] = m[5]
        elif 'GRAVIDAR_DIGEST tick=' in line and (m := GDIG.search(line)):
            gdig[int(m[1])] = dict(units=(int(m[2]), int(m[3])), factors=m[4], times=m[5], classes=m[6],
                                   active=(int(m[7]), int(m[8]), m[9], m[10], int(m[11]), int(m[12])), stage=m[13],
                                   passive=m[14] and int(m[14]), sectors=m[15] and (int(m[15]), int(m[16]), m[17]), sector_attention=m[18])
        elif 'GRAVIDAR_DEADLINES tick=' in line and (m := GDL.search(line)):
            gdl[int(m[1])] = dict(first=int(m[3]), rebased=int(m[4]), due=int(m[5]), keyed=int(m[6]), unmapped=int(m[7]),
                                    native=int(m[8]), unscoped=int(m[9]), passes=int(m[10]), low=int(m[11]), high=int(m[12]),
                                    cur_tick=int(m[13]), cur_calls=int(m[14]), cur_passes=int(m[15]), cur_keyed=int(m[16]),
                                    cur_dueset=m[17], admitted=int(m[18]), units_active=int(m[19]), units_rebased=int(m[20]),
                                    failures=int(m[21]), stage=m[22])
        elif 'MOTION_WATCH ' in line and (m := MOTION_WATCH.search(line)):
            motion[m[2]][int(m[1])] = dict(f.split('=', 1) for f in m[3].split())
        elif ('MOTION_ORDER ' in line or 'MOTION_TARGET ' in line or 'MOTION_EVENT ' in line or 'MOTION_REFRESH ' in line) and \
                (m := MOTION_EVENT.search(line)):
            motion_events[m[1]][m[3]].append(dict(tick=m[2], **dict(f.split('=', 1) for f in m[4].split())))
        elif 'EFFECT_ROW ' in line and (m := EFFECT_ROW.search(line)):
            effect_rows[int(m[1])].append(m[2])
        elif 'REGION_CAMERA tick=' in line and (m := REGION_CAMERA.search(line)):
            region_camera[int(m[1])] = dict(mode=m[2], frame=int(m[3]), key=m[4], updates=int(m[5]), native_frames=int(m[6]))
        elif 'ATTENTION_ALL tick=' in line and (m := ATTENTION_ALL.search(line)):
            attention[int(m[1])] = dict(frame=int(m[2]), counts=(int(m[3]), int(m[4])), ids=int(m[5]), unmapped=int(m[6]))
        elif 'VIEW_POINTS_ROW ' in line and (m := VIEW_ROW.search(line)):
            if m[3] != '0000000000000000': view_rows[f'{m[3]}/{m[5]}'].append(m[1])
        elif 'REGION_PASS tick=' in line and (m := REGION.search(line)):
            region[int(m[1])] = dict(mode=m[2], joins=int(m[3]), busy=int(m[4]), unknown=int(m[5]), max_wait_us=int(m[6]),
                                     word=m[7], draws=int(m[8]))
        elif 'VIEW_POINTS tick=' in line and (m := VIEW.search(line)):
            view[int(m[1])] = dict(mode=m[2], frame=int(m[3]), source=m[4], viewpoints=int(m[5]),
                                   counts=[int(m[6]), int(m[7])]+list(map(int, m[8].split('/'))), differ=int(m[9]))
        elif 'NAV_CADENCE tick=' in line and (m := NAV.search(line)):
            nav[int(m[1])] = dict(mode=m[2], frame=int(m[3]), fixture=list(map(int, m[4].split('/'))),
                                  other=list(map(int, m[5].split('/'))), unreadable=int(m[8]))
        elif 'BULLET_CADENCE tick=' in line and (m := CADENCE.search(line)):
            fixture = list(map(int, m[4].split('/')));other = list(map(int, m[5].split('/')))
            ticks[int(m[1])] = dict(mode=m[2], frame=int(m[3]), fixture=fixture, other=other,
                                    first_later=int(m[6]), unreadable=int(m[7]), camera=portable_camera(m[8]))
        elif 'NATIVE_GATE BULLET id=' in line and (m := BULLET.search(line)):
            bullets[int(m[2])].append(m[1])
        elif 'STATE_ROW tick=' in line and (m := STATE.search(line)):
            states[int(m[1])].append((int(m[2]), m[3]))
    return dict(ticks=ticks, nav=nav, view=view, bullets=dict(bullets), states=dict(states), nav_rows=dict(nav_rows), mass=mass, region=region, view_rows=dict(view_rows), gdig=gdig,
                gdig_rows=dict(gdig_rows), gdig_evicted=gdig_evicted, gdl=gdl, attention=attention, region_camera=region_camera, effect_rows=dict(effect_rows), motion=dict(motion),
                motion_orders=dict(motion_events['ORDER']), motion_targets=dict(motion_events['TARGET']),
                motion_events=dict(motion_events['EVENT']), motion_refreshes=dict(motion_events['REFRESH']))


def first_later(ticks, kind):
    """First tick whose own frame (frame == tick) scheduled a test later than that frame."""
    return next((t for t in sorted(ticks) if ticks[t]['frame'] == t and ticks[t][kind][3] > 0), None)


def first_near(nav, kind):
    """First own-frame tick with a camera-near nav choice: now or mid in either block."""
    near = lambda c: c[2]+c[3]+c[6]+c[7]
    return next((t for t in sorted(nav) if nav[t]['frame'] == t and near(nav[t][kind]) > 0), None)


def first_row_differences(a, b, limit=8):
    """Per controller (fixture id / watched saved ID): the first state row that differs, earliest first."""
    out = []
    for key in sorted(set(a) | set(b)):
        x, y = a.get(key, []), b.get(key, [])
        for i in range(max(len(x), len(y))):
            u, v = (x[i] if i < len(x) else None), (y[i] if i < len(y) else None)
            if u != v:
                tick = min(int(r.split()[0][5:]) for r in (u, v) if r)
                out.append(dict(controller=key, row=i, tick=tick, client1=u, client2=v));break
    return sorted(out, key=lambda d: d['tick'])[:limit]


def own_frames(mass):
    """MASS_TRAFFIC rows whose frame counters belong to their own tick."""
    return {t: r for t, r in mass.items() if r['frame'] == t}


def view_row_vtables(a, b):
    """Per slot-703 implementation (vtable RVA): objects whose attention rows differ, the earliest tick and
    the triggers (callers of SetAttention) seen in the first differing rows."""
    out = collections.defaultdict(lambda: dict(objects=0, first_tick=None, triggers=collections.Counter()))
    for d in first_row_differences(a, b, limit=1 << 30):
        e = out[d['controller'].split('/')[1]];e['objects'] += 1
        e['first_tick'] = d['tick'] if e['first_tick'] is None else min(e['first_tick'], d['tick'])
        for row in (d['client1'], d['client2']):
            m = row and re.search(r' trigger=([0-9a-f]{8})', row)
            if m: e['triggers'][m[1]] += 1
    return {k: dict(v, triggers=dict(v['triggers'])) for k, v in sorted(out.items(), key=lambda kv: kv[1]['first_tick'])}


def first_gravidar_rows(a, b, limit=16):
    """The first tick whose GRAVIDAR_DIGEST_ROW sets differ: the differing kind/key entries
    (sorted, at most limit) with each client's row (None: no row on that client)."""
    for tick in sorted(set(a) | set(b)):
        x, y = a.get(tick, {}), b.get(tick, {})
        keys = sorted(k for k in set(x) | set(y) if x.get(k) != y.get(k))
        if keys: return dict(tick=tick, differing=len(keys), rows=[dict(key=k, client1=x.get(k), client2=y.get(k)) for k in keys[:limit]])
    return None


def own_passes(gdl):
    """GRAVIDAR_DEADLINES lines whose current tally belongs to their own tick (no field Move yet: no tally)."""
    return {t: dict(passes=r['cur_passes'], dueset=r['cur_dueset']) if r['cur_tick'] == t else dict(passes=0, dueset=None)
            for t, r in gdl.items()}


def gdl_summary(gdl):
    if not gdl: return None
    last = gdl[max(gdl)]
    return {k: last[k] for k in ('first', 'rebased', 'due', 'keyed', 'unmapped', 'native', 'unscoped', 'passes', 'low', 'high',
                                 'admitted', 'units_active', 'units_rebased', 'failures', 'stage')}


def motion_differences(a, b):
    """Per watched ship: the first tick whose MOTION_WATCH fields differ and the differing fields."""
    out = {}
    for saved in sorted(set(a) | set(b)):
        x, y = a.get(saved, {}), b.get(saved, {})
        for tick in sorted(set(x) | set(y)):
            u, v = x.get(tick), y.get(tick)
            if u != v:
                fields = sorted(k for k in set(u or {}) | set(v or {}) if (u or {}).get(k) != (v or {}).get(k))
                out[saved] = dict(tick=tick, fields=fields)
                break
    return out


def motion_event_differences(a, b):
    """Per watched ship: the first MOTION_ORDER/MOTION_TARGET event whose fields differ
    (by position in the ship's sequence), its tick on both clients and the fields."""
    out = {}
    for saved in sorted(set(a) | set(b)):
        x, y = a.get(saved, []), b.get(saved, [])
        for i in range(max(len(x), len(y))):
            u, v = (x[i] if i < len(x) else None), (y[i] if i < len(y) else None)
            if u != v:
                fields = sorted(k for k in set(u or {}) | set(v or {}) if (u or {}).get(k) != (v or {}).get(k))
                out[saved] = dict(event=i, ticks=[int(u['tick']) if u else None, int(v['tick']) if v else None], fields=fields)
                break
    return out


def effect_row_differences(a, b, limit=4):
    """The first ticks whose EFFECT_ROW multisets differ: rows only one client has (count-aware)."""
    out = []
    for tick in sorted(set(a) | set(b)):
        x, y = collections.Counter(a.get(tick, [])), collections.Counter(b.get(tick, []))
        if x != y:
            out.append(dict(tick=tick, client1_only=sorted((x - y).elements())[:8], client2_only=sorted((y - x).elements())[:8]))
            if len(out) >= limit: break
    return out


def first_difference(a, b, field=None):
    for tick in sorted(set(a) | set(b)):
        x, y = a.get(tick), b.get(tick)
        if field: x, y = x and x[field], y and y[field]
        if x != y: return dict(tick=tick, client1=x, client2=y)
    return None


def compare(a, b):
    summary = lambda p: dict(ticks=len(p['ticks']), modes=sorted({t['mode'] for t in p['ticks'].values()}),
                             first_later_fixture=first_later(p['ticks'], 'fixture'),
                             first_later_other=first_later(p['ticks'], 'other'),
                             unreadable=max((t['unreadable'] for t in p['ticks'].values()), default=0),
                             nav_ticks=len(p['nav']), nav_modes=sorted({t['mode'] for t in p['nav'].values()}),
                             first_nav_near_fixture=first_near(p['nav'], 'fixture'),
                             first_nav_near_other=first_near(p['nav'], 'other'),
                             nav_unreadable=max((t['unreadable'] for t in p['nav'].values()), default=0),
                             view_ticks=len(p['view']), view_modes=sorted({t['mode'] for t in p['view'].values()}),
                             view_sources=sorted({t['source'] for t in p['view'].values()}),
                             first_view_differ=next((t for t in sorted(p['view']) if p['view'][t]['differ']), None),
                             view_differ=sum(t['differ'] for t in p['view'].values()),
                             mass_ticks=len(p['mass']), mass_modes=sorted({t['mode'] for t in p['mass'].values()}),
                             mass_first_skip=next((t['first_skip'] for t in p['mass'].values() if t['first_skip']), None),
                             mass_skips=sum(r['skips'] for r in own_frames(p['mass']).values()),
                             mass_busy=max((t['busy'] for t in p['mass'].values()), default=0),
                             mass_unknown=max((t['unknown'] for t in p['mass'].values()), default=0),
                             mass_max_wait_us=max((t['max_wait_us'] for t in p['mass'].values()), default=0),
                             region_ticks=len(p['region']), region_modes=sorted({t['mode'] for t in p['region'].values()}),
                             region_busy=max((t['busy'] for t in p['region'].values()), default=0),
                             region_max_wait_us=max((t['max_wait_us'] for t in p['region'].values()), default=0),
                             region_draws=sum(t['draws'] for t in p['region'].values()),
                             attention_all_ticks=len(p['attention']),
                             attention_all_unmapped=max((t['unmapped'] for t in p['attention'].values()), default=0),
                             region_camera_ticks=len(p['region_camera']),
                             region_camera_native_frames=max((t['native_frames'] for t in p['region_camera'].values()), default=0),
                             gravidar_rows=sum(len(r) for r in p['gdig_rows'].values()), gravidar_rows_evicted=p['gdig_evicted'],
                             gravidar_deadlines=gdl_summary(p['gdl']))
    return dict(clients=[summary(a), summary(b)],
                first_fixture_counts=first_difference(a['ticks'], b['ticks'], 'fixture'),
                first_other_counts=first_difference(a['ticks'], b['ticks'], 'other'),
                first_camera=first_difference(a['ticks'], b['ticks'], 'camera'),
                first_nav_fixture_counts=first_difference(a['nav'], b['nav'], 'fixture'),
                first_nav_other_counts=first_difference(a['nav'], b['nav'], 'other'),
                first_view_counts=first_difference(a['view'], b['view'], 'counts'),
                first_mass_skips=first_difference(own_frames(a['mass']), own_frames(b['mass']), 'skips'),
                first_mass_word=first_difference(a['mass'], b['mass'], 'word'),
                first_mass_draws=first_difference(a['mass'], b['mass'], 'draws'),
                first_mass_camera=first_difference(a['mass'], b['mass'], 'camera'),
                first_region_word=first_difference(a['region'], b['region'], 'word'),
                first_gravidar_units=first_difference(a['gdig'], b['gdig'], 'units'),
                first_gravidar_factors=first_difference(a['gdig'], b['gdig'], 'factors'),
                first_gravidar_times=first_difference(a['gdig'], b['gdig'], 'times'),
                first_gravidar_classes=first_difference(a['gdig'], b['gdig'], 'classes'),
                first_gravidar_active=first_difference(a['gdig'], b['gdig'], 'active'),
                first_gravidar_sectors=first_difference(a['gdig'], b['gdig'], 'sectors'),
                first_gravidar_sector_attention=first_difference(a['gdig'], b['gdig'], 'sector_attention'),
                first_gravidar_rows=first_gravidar_rows(a['gdig_rows'], b['gdig_rows']),
                first_gravidar_deadline_passes=first_difference(own_passes(a['gdl']), own_passes(b['gdl'])),
                first_region_draws=first_difference(a['region'], b['region'], 'draws'),
                first_attention_all=first_difference(a['attention'], b['attention'], 'counts'),
                first_region_camera=first_difference(a['region_camera'], b['region_camera'], 'key'),
                first_effect_rows=effect_row_differences(a['effect_rows'], b['effect_rows']),
                first_motion_watch=motion_differences(a['motion'], b['motion']),
                first_motion_orders=motion_event_differences(a['motion_orders'], b['motion_orders']),
                first_motion_targets=motion_event_differences(a['motion_targets'], b['motion_targets']),
                first_motion_events=motion_event_differences(a['motion_events'], b['motion_events']),
                first_motion_refreshes=motion_event_differences(a['motion_refreshes'], b['motion_refreshes']),
                first_attention_all_ids=first_difference(a['attention'], b['attention'], 'ids'),
                first_nav_rows=first_row_differences(a['nav_rows'], b['nav_rows']),
                first_view_rows=first_row_differences(a['view_rows'], b['view_rows'], limit=16),
                view_row_vtables=view_row_vtables(a['view_rows'], b['view_rows']),
                first_bullet_rows=first_difference(a['bullets'], b['bullets']),
                first_state_rows=first_difference(a['states'], b['states']))


def client_logs(run):
    return [Path(run)/f'client{slot}'/'engine-output.log' for slot in client_slots(run)]


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('run', type=Path, nargs='?', help='run directory with client1/..clientN/engine-output.log')
    p.add_argument('--logs', type=Path, nargs='+', metavar='LOG', help='explicit engine-output.log of client1, client2[, ...]')
    p.add_argument('--out', type=Path)
    args = p.parse_args()
    if bool(args.run) == bool(args.logs): p.error('give a run directory or --logs CLIENT1 CLIENT2 [...]')
    if args.logs and len(args.logs) < 2: p.error('--logs needs at least two clients')
    paths = args.logs or client_logs(args.run)
    parsed = [parse(lines(path)) for path in paths]
    pair = lambda k: compare(parsed[0], parsed[k-1])
    result = pair(2) if len(paths) == 2 else against_reference(list(range(1, len(paths)+1)), pair)
    text = json.dumps(result, indent=2)
    if args.out: args.out.write_text(text+'\n')
    print(text)


if __name__ == '__main__': main()
