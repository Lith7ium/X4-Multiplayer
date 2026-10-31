"""Per-frame wall time of a gated run from the relay's release timestamps.

released_at[tick] is the relay's monotonic time when tick+1 was permitted (older relays on
Windows: a clock of 15.6 ms steps, so only the mean and the seconds mean anything), so
the difference between consecutive releases is the slower client's frame cost
plus the relay round trip. With --lookahead k (protocol 4) it is the time the
reports of tick released the permit for tick+1+k; the difference is then the
frame period, the round trip hidden while k frames cover it. World-capture
ticks are reported separately because census captures are diagnostic, not
part of the determinism fixes. The native breakdown covers every clientK
directory of an N-client run.
"""
import argparse
import json
import re
import statistics
from pathlib import Path

from census_clients import client_slots


def stride(run):
    config = (run/'client1/installed/x4_lan_lab-config.lua').read_text()
    m = re.search(r'native_gate_world_hash_stride\s*=\s*(\d+)', config)
    return int(m[1]) if m else 0


def tick_hz(run):
    config = (run/'client1/installed/x4_lan_lab-config.lua').read_text()
    m = re.search(r'native_gate_tick_hz\s*=\s*(\d+)', config)
    return int(m[1]) if m else 60


def analyse(run):
    b = json.loads((run/'native-barrier.json').read_text())
    at = {int(k): v for k, v in (b.get('released_at') or {}).items()}
    ticks = sorted(at)
    every = stride(run)
    frames, captures = [], []
    for prev, tick in zip(ticks, ticks[1:]):
        ms = (at[tick]-at[prev])*1000
        captured = every and (tick == 1 or tick % every == 0)
        (captures if captured else frames).append(ms)
    def summary(values):
        if not values: return None
        values = sorted(values)
        return dict(n=len(values), median=round(statistics.median(values), 2), mean=round(statistics.fmean(values), 2),
                    p90=round(values[int(len(values)*0.9)-1], 2), max=round(values[-1], 2))
    span = at[ticks[-1]]-at[ticks[0]] if len(ticks) > 1 else 0
    return dict(verdict=b.get('verdict'), ticks=len(ticks), stride=every, frames=summary(frames),
                capture_frames=summary(captures), seconds=round(span, 2),
                simulated_per_wall=round((len(ticks)-1)/tick_hz(run)/span, 3) if span else None)


FRAME = re.compile(r'FRAME_TIMING tick=(\d+) frames=\d+ wait_us=(\d+) frame_us=(\d+) outside_us=(\d+)(?:.*probe_us=(-?\d+))?')
# Gate builds from census-perf 2026-10-29 on: the gate's own work before the permit wait (pre:
# checks, created identities, joins) and the ReadProcessMemory calls of the frame interval.
GATE_PRE = re.compile(r'pre_us=(\d+) created_us=(\d+) join_us=(\d+) sum_pre_us=\d+ reads=(\d+)')
# Completed-tick script RNG self-time (scope entry/exit only, TSC via QPC).
SCRIPT = re.compile(r'SCRIPT_RNG tick=\d+ .* last_tick=(\d+) last_entries=(\d+) .* last_self_us=(\d+)')


def native(run):
    """Median native breakdown (QPC) from FRAME_TIMING lines; on relays older than
    2026-10-30 the clock above is GetTickCount-based on Windows, quantised to ~15.6 ms."""
    every = stride(run)
    result = {}
    for client in (f'client{k}' for k in client_slots(run, strict=False)):
        rows, scripts, pre = [], {}, []
        log = run/client/'engine-output.log'
        if not log.is_file(): continue
        skip = lambda tick: tick <= 2 or tick == 11 or (every and tick % every == 0)  # captures, hold, start
        with log.open(encoding='utf-8', errors='replace') as lines:
            for line in lines:
                if 'SCRIPT_RNG tick=' in line:
                    s = SCRIPT.search(line)
                    if s and not skip(int(s[1])): scripts[int(s[1])] = (int(s[2]), int(s[3]))
                    continue
                if 'FRAME_TIMING tick=' not in line: continue
                m = FRAME.search(line)
                if not m: continue
                tick = int(m[1])
                if skip(tick): continue
                rows.append([int(m[i]) if m[i] is not None else -1 for i in range(2, 6)])
                g = GATE_PRE.search(line)
                if g: pre.append([int(v) for v in g.groups()]+[rows[-1][2]-rows[-1][3]-int(g[1])])
        if not rows: continue
        med = lambda i: round(statistics.median(r[i] for r in rows)/1000, 2)
        result[client] = dict(frames=len(rows), wait_ms=med(0), frametick_ms=med(1), outside_ms=med(2),
                              probe_ms=med(3), total_ms=round(med(0)+med(1)+med(2), 2))
        if pre:
            # rest = outside - probe - pre: the engine frame and the UI outside the probe.
            mid = lambda i: statistics.median(r[i] for r in pre)
            result[client].update(pre_ms=round(mid(0)/1000, 2), created_ms=round(mid(1)/1000, 3), join_ms=round(mid(2)/1000, 2),
                                  reads=mid(3), rest_ms=round(mid(4)/1000, 2))
        if scripts:
            result[client].update(script_rng_ms=round(statistics.median(v[1] for v in scripts.values())/1000, 3),
                                  script_rng_entries=statistics.median(v[0] for v in scripts.values()))
    return result


def agree(run, row, field):
    """Does a status field agree between the clients, tick by tick? PATH_DETERMINISM walk for
    native_gate_path_budget: the first tick it differs, the budget's cheap agents differ."""
    pattern = re.compile(r'\b' + re.escape(row) + r' tick=(\d+) .*?\b' + re.escape(field) + r'=(\S+)')
    ticks = {}
    for client in (f'client{k}' for k in client_slots(run, strict=False)):
        log = run/client/'engine-output.log'
        if not log.is_file(): continue
        with log.open(encoding='utf-8', errors='replace') as lines:
            for line in lines:
                m = pattern.search(line) if row in line else None
                if m: ticks.setdefault(int(m[1]), {})[client] = m[2]
    shared = {tick: values for tick, values in ticks.items() if len(values) > 1}
    differing = sorted(tick for tick, values in shared.items() if len(set(values.values())) > 1)
    return dict(row=row, field=field, compared=len(shared), differing=len(differing),
                first=differing[0] if differing else None, values=shared[differing[0]] if differing else None)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('runs', type=Path, nargs='+')
    p.add_argument('--native', action='store_true', help='also report the QPC FRAME_TIMING breakdown')
    p.add_argument('--agree', metavar='ROW:FIELD', help='compare a status field across the clients, e.g. PATH_DETERMINISM:walk')
    args = p.parse_args()
    for run in args.runs:
        print(run.name, json.dumps(analyse(run)))
        if args.native: print(run.name, 'native', json.dumps(native(run)))
        if args.agree: print(run.name, 'agree', json.dumps(agree(run, *args.agree.split(':', 1))))


if __name__ == '__main__': main()
