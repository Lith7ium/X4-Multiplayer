"""Build a separate, opt-in disposable-lane native-gate overlay (no installation)."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import gzip
import re
import xml.etree.ElementTree as ET
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from native_gate_wait_manifest import wait_manifest
from native_world_wait_rng import manifest_wire as wait_rng_manifest_wire
from native_transition_manifest import manifest as transition_manifest
from native_world_action import manifest_wire as action_manifest_wire
from native_world_find_rng import manifest_wire as find_manifest_wire,verify_input_files as find_verify_inputs
from native_world_position_rng import manifest_wire as position_rng_wire
import native_gate_player_orders as player_orders


def _snapshot_starts(path):
    """Stream exact XML ancestry without retaining the large universe tree."""
    stack=[]
    with gzip.open(path,'rb') as source:
        for event,element in ET.iterparse(source,events=('start','end')):
            if event=='start':
                stack.append(element)
                yield stack
            else:
                stack.pop()
                if stack:stack[-1].remove(element)
                element.clear()


def snapshot_fixture(path):
    """Resolve only the saved MD fixture's ordered Spawn.$ships list."""
    references=[]
    for nodes in _snapshot_starts(path):
        if [n.tag for n in nodes]==['savegame','md','script','cue','vars','value'] and nodes[2].get('name')=='X4LAN_NativeGate' and nodes[3].get('name')=='Spawn' and nodes[-1].get('name')=='$ships':
            value=nodes[-1]
            if nodes[3].get('state')!='complete' or value.get('type')!='list' or not re.fullmatch(r'[1-9][0-9]*',value.get('value','')):
                raise ValueError('snapshot fixture Spawn.$ships is not a completed saved list')
            references.append(value.get('value'))
    if len(references)!=1:raise ValueError('snapshot requires exactly one X4LAN_NativeGate Spawn.$ships')
    ids=[];lists=0
    for nodes in _snapshot_starts(path):
        if len(nodes)<4 or [n.tag for n in nodes[:4]]!=['savegame','script','refs','ref'] or nodes[2].get('type')!='list' or nodes[3].get('id')!=references[0]:continue
        if len(nodes)==4:lists+=1;continue
        value=nodes[-1]
        if len(nodes)!=5 or value.tag!='value' or value.get('type')!='component' or not re.fullmatch(r'\[0x[0-9a-f]{1,16}\]',value.get('value','')):
            raise ValueError('snapshot fixture list must contain direct component references')
        ids.append(int(value.get('value')[3:-1],16))
        if len(ids)>12:raise ValueError('snapshot fixture has more than twelve ships')
    if lists!=1 or len(ids)!=12 or len(set(ids))!=12 or 0 in ids:
        raise ValueError('snapshot fixture requires twelve unique nonzero saved component IDs')
    positions={saved:i for i,saved in enumerate(ids)};found=[0]*12;movement=[0]*12
    def selected(element):
        value=element.get('id','')
        return positions.get(int(value[3:-1],16)) if element.tag=='component' and re.fullmatch(r'\[0x[0-9a-f]{1,16}\]',value) else None
    for nodes in _snapshot_starts(path):
        element=nodes[-1];i=selected(element)
        if i is not None:
            faction='argon' if i<6 else 'xenon';macro='ship_arg_s_fighter_01_a_macro' if i<6 else 'ship_xen_s_fighter_01_a_macro'
            if element.get('class')!='ship_s' or element.get('macro')!=macro or element.get('owner')!=faction:
                raise ValueError('snapshot fixture component class, macro or owner mismatch')
            found[i]+=1
        if len(nodes)>1 and element.tag=='movement':
            i=selected(nodes[-2])
            if i is not None:
                if element.get('class')!='navigation':raise ValueError('snapshot fixture requires navigation controllers')
                movement[i]+=1
    if found!=[1]*12 or movement!=[1]*12:raise ValueError('snapshot fixture component identities or navigation records are incomplete/duplicated')
    return [f'{saved:016x}' for saved in ids]


def replay_plan(path):
    report=json.loads(path.read_text())
    if report.get('protocol_version') != 2:
        raise ValueError('replay needs a native protocol 2 report')
    plans={1:{},2:{}}
    for text,rows in report['states'].items():
        tick=int(text)
        if str(tick)!=text or not 0<=tick<=18000:
            raise ValueError('invalid replay tick')
        for slot in (1,2):
            command=rows[str(slot)]['command']
            if len(command)!=8 or any(c not in '0123456789abcdef' for c in command):
                raise ValueError('invalid replay command')
            if command!='00000000':
                if tick==0: raise ValueError('bootstrap cannot propose gameplay commands')
                plans[slot][tick]=command
    if not all(plans.values()): raise ValueError('replay must include inputs from both clients')
    return '{'+','.join('['+str(slot)+']={'+','.join('['+str(tick)+']="'+command+'"' for tick,command in sorted(plans[slot].items()))+'}' for slot in (1,2))+'}'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    gate = p.add_mutually_exclusive_group(required=True)
    gate.add_argument('--gate-dll', type=Path, help='Windows gate x4lan_gate_64.dll')
    gate.add_argument('--gate-so', type=Path, help='Linux gate core x4lan_gate.so (src/native/engine/linux): snapshot clock runs, '
                      'optionally --session-clients and --seed-rng; every other option needs a module not ported yet')
    p.add_argument('--fixedtick-dll', type=Path)
    p.add_argument('--snapshot-prebind', action='store_true', help='validate and prebind the saved twelve-ship fixture before the first admitted native frame')
    p.add_argument('--snapshot', type=Path, help='same owned native save staged by the lane wrapper')
    p.add_argument('--mode', choices=('clock', 'battle'), required=True)
    p.add_argument('--command-test', choices=('pvp', 'coop'), help='scripted commands from both clients, applied through native MD orders')
    p.add_argument('--command-plan', choices=('combat', 'maneuvers', 'travel', 'withdraw', 'docking', 'fleet', 'live', 'replay'), default='combat', help='scripted combat/maneuvers/travel/withdraw/docking/fleet, local live input, or recorded command timing')
    p.add_argument('--docking-state', action='store_true', help='create two disposable dock anchors and hash native docking/occupancy state')
    p.add_argument('--docking-exit', choices=('diagonal','above'), default='diagonal', help='docking fixture departure destination; above tests clearance one kilometre above the anchor')
    p.add_argument('--boost-diag', action='store_true', help='sample native boost activity, charge and speed; automatic for withdraw plan')
    p.add_argument('--travel-diag', action='store_true', help='sample native travel/charge/speed state; automatically enabled for travel plan')
    p.add_argument('--command-replay', type=Path, help='native-barrier.json to reproduce command proposals with --command-plan replay')
    p.add_argument('--player-orders', type=Path, metavar='PLAN', help='default-off lockstep player orders from a JSON plan (owners, script, delay); needs the variable-length gate input wire')
    p.add_argument('--order-state', action='store_true', help='also compare native order queue/default state through exported UI queries')
    p.add_argument('--fleet-state', action='store_true', help='also compare native commander identity, assignment and subordinate group')
    p.add_argument('--serial-pathing', action='store_true')
    p.add_argument('--runtime-ids', action='store_true', help='the Linux gate: native_gate_runtime_ids, the same runtime IDs on every client (set mode at the default base; needs --world-identities)')
    p.add_argument('--script-rng', choices=('observe', 'key'), help='the Linux gate: native_gate_script_rng, AI scripts and MD cues on per-script streams (needs --world-identities)')
    p.add_argument('--script-rng-mask', type=int, choices=range(1, 8), metavar='1..7', help='with --script-rng: 1 AI scripts, 2 MD cues, 4 also boot phase 2 (default 3)')
    p.add_argument('--script-rng-rows', type=int, metavar='TICKS', help='with --script-rng: SCRIPT_RNG_ROW lines for the first TICKS ticks')
    p.add_argument('--script-rng-natural', action='store_true', help='with --script-rng-rows: rows of outermost unkeyed scopes too')
    p.add_argument('--script-rng-unmapped', action='store_true', help='with --script-rng: AI owners without a portable ID are keyed by their rank')
    p.add_argument('--md-timed-rng', action='store_true', help='the Linux gate: direct MD cue checks on the per-cue streams (needs --script-rng with the MD bit)')
    p.add_argument('--nav-cadence', choices=('far', 'observe'), help='the Linux gate: nav refresh intervals no longer depend on the camera distance')
    p.add_argument('--nav-params-init', action='store_true', help='the Linux gate: parameter deadlines reset at the first call (needs --nav-cadence)')
    p.add_argument('--nav-key', action='store_true', help="the Linux gate: the nav refresh's draws keyed per ship (needs --nav-cadence and --world-identities)")
    p.add_argument('--nav-probe-init', action='store_true', help='the Linux gate: obstacle-probe deadlines reset at the first call (needs --nav-cadence)')
    p.add_argument('--created-identities', action='store_true', help='the Linux gate: portable IDs and census rows for components made after the load (needs --world-identities)')
    p.add_argument('--created-load', action='store_true', help='the Linux gate: also for those the load makes after the import (needs --created-identities)')
    p.add_argument('--portable-created', action='store_true', help='the Linux gate: ordering and keying modules use created IDs (needs --created-identities)')
    p.add_argument('--gravidar-wide', choices=('sort',), help='the Linux gate: the Gravidar\'s zone groups in portable order for the pass (needs --gravidar)')
    p.add_argument('--spawn-rng', choices=('observe', 'key'), help='the Linux gate: ship loadouts on the generator\'s explicit state')
    p.add_argument('--find-order', choices=('observe', 'sort'), help='the Linux gate: find_* candidate lists on saved IDs (needs --world-identities)')
    p.add_argument('--loadout-order', choices=('observe', 'canonical'), help="the Linux gate: what a station wants to restock does not depend on the macros' heap addresses (native_gate_loadout_order)")
    p.add_argument('--attention-all', action='store_true', help="the Linux gate: every portable object's attention timer re-evaluates at its first admitted call and draws from (tick, ID) (native_gate_attention_all with the hook of native_gate_attention_initial_refresh; needs --world-identities)")
    p.add_argument('--step-trace', nargs='+', metavar='SAVED_ID', help="the Linux gate: the Linear flight model's step, entry and exit, for up to four saved IDs (diagnostic; needs --world-identities)")
    p.add_argument('--step-trace-ticks', type=int, default=3, help='ticks 1..N of the step trace (default 3, at most 600)')
    p.add_argument('--fp-redirect', choices=('shadow', 'writer', 'redirect'), help='both gates: the flight parameter refresh recomputed by strict code next to the native one, ulp per field (Option B slice 1; shadow writes nothing, writer writes the writer\'s +0x0..+0xc, redirect also the spline segment length where Windows inlines BezierSpline::Set)')
    p.add_argument('--fp-redirect-watch', nargs='+', metavar='SAVED_ID', help='FP_REDIRECT_ROWs (inputs and results) for up to four saved IDs (needs --fp-redirect and --world-identities)')
    p.add_argument('--fp-redirect-ticks', type=int, default=3, help='ticks 1..N of the FP_REDIRECT_ROWs (default 3, at most 600)')
    p.add_argument('--coverage', type=int, metavar='TICKS', help='the Linux gate: which listed functions run in ticks 1..TICKS (diagnostic; needs --coverage-list)')
    p.add_argument('--coverage-list', type=Path, help='the function list (virtual addresses, one per line), copied next to the gate as coverage.txt')
    p.add_argument('--loadout-order-rows', type=int, nargs=2, metavar=('FROM', 'TO'), help='LOADOUT_ROWs in this tick window, at most 600 ticks (needs --loadout-order and --world-identities)')
    p.add_argument('--runtime-id-callers', action='store_true', help='the Linux gate: the table of who allocates runtime IDs, RUNTIME_IDS_CALLER rows (needs --runtime-ids)')
    p.add_argument('--travel-snapshot', action='store_true', help='the Linux gate: the wingman travel decision reads a per-pass snapshot of the leader flag (needs --serial-pathing and path determinism bit 2)')
    p.add_argument('--gravidar-deadlines', action='store_true', help='the Linux gate: factor-pass deadlines rebased at the first gated Move, low-attention passes keyed (needs --gravidar)')
    p.add_argument('--gravidar', action='store_true', help='the Linux gate: the gated Gravidar pass with keyed units, time sync and the sector round-robin in saved order (needs --serial-pathing, --world-identities, --gate-so)')
    p.add_argument('--event-rows', type=int, metavar='TICKS', help='native_gate_event_rows: one EVENT_ROW per due event in the boot passes and the first TICKS gated ticks, up to 600 (20000 on the Linux gate; needs --event-order)')
    p.add_argument('--event-order', action='store_true', help='native_gate_event_order: due events delivered in (time, saved ID) source order (needs --world-identities)')
    p.add_argument('--md-resume-rng', action='store_true', help='native_gate_md_resume_rng: load-time MD cue delays on per-cue streams (needs the snapshot boot)')
    p.add_argument('--boot-hold', action='store_true', help='native_gate_boot_hold: the first AIDirector::Move after the load runs in tick 1 (needs the snapshot boot)')
    p.add_argument('--ai-wait-rng', action='store_true', help='native_gate_ai_wait_rng: load-time AI Wait durations on per-wait streams (needs --world-identities)')
    p.add_argument('--region-rng', choices=('key', 'observe'), help='native_gate_region_rng: hazard region updates on per-region streams (needs --world-identities)')
    p.add_argument('--path-determinism', type=int, choices=(1, 2, 3), help='native_gate_path_determinism mask: 1 stamps the nav refresh per frame (needs --serial-pathing), 2 lifts the pathing budget')
    p.add_argument('--seed-rng', action='store_true', help='test-only MoveWorker frame seed')
    p.add_argument('--actor-diag', action='store_true', help='read-only raw navigation-state diagnostics')
    p.add_argument('--flight-diag', action='store_true', help='read-only per-fixture steering update inputs/outputs')
    p.add_argument('--flight-rng', action='store_true', help='isolate RNG within each fixture steering update')
    p.add_argument('--flight-full', action='store_true', help='use full updates for fixture steering irrespective of wall-time budget')
    p.add_argument('--avoidance-refresh', action='store_true', help='refresh native fixture avoidance geometry every steering call (requires --flight-full)')
    p.add_argument('--steering-init', action='store_true', help='initialize fixture steering constructor random angles deterministically (requires --flight-rng)')
    p.add_argument('--trace-stride', type=int, default=1, help='log verbose per-actor diagnostics every N frames; canonical state and event traces remain complete')
    p.add_argument('--trace-window', type=int, nargs=2, metavar=('FIRST', 'LAST'), help='also log every update in this inclusive diagnostic frame window')
    p.add_argument('--ai-diag', action='store_true', help='observe fixture AI interpreter runs')
    p.add_argument('--registry-md-trace', action='store_true', help='read-only native evaluated construction MD boundary trace')
    p.add_argument('--ai-rng', action='store_true', help='isolate RNG within fixture AI interpreter runs')
    p.add_argument('--interrupt-rng', action='store_true', help='scope fixture native interrupt interpreter RNG, restoring its caller stream')
    p.add_argument('--npc-rng', action='store_true', help='KNOWN-FAILING legacy ordered_invocation_v1 RNG experiment; same-tick ambient calls can shift fixture seeds')
    p.add_argument('--npc-deadline', action='store_true', help='EXPERIMENT: route exact owned audio-ready NPC completion through the native deadline; requires strict lifecycle evidence')
    p.add_argument('--npc-diag', action='store_true', help='read-only named NPC lifecycle and actual native speech selection; implies saved world identities')
    p.add_argument('--npc-pilot', action='store_true', help='read-only saved pilot976ab execution/event trace; implies NPC diagnostics')
    p.add_argument('--interrupt-diag', action='store_true', help='read-only native fixture AI events and interrupt execution')
    p.add_argument('--radar-diag', action='store_true', help='read-only fixture AI radar scan event delivery trace')
    p.add_argument('--radar-serial', action='store_true', help='publish and join native radar work after frame admission, before pathing')
    p.add_argument('--radar-rng', action='store_true', help='isolate native fixture radar scan RNG; requires serial pathing')
    p.add_argument('--radar-initial-refresh', action='store_true', help='align fixture radar refresh and notification timestamps once; requires --radar-rng')
    p.add_argument('--damage-diag', action='store_true', help='read-only native fixture shield absorption and caller trace')
    p.add_argument('--contact-diag', action='store_true', help='read-only native collision manifolds and settings involving fixture ships')
    p.add_argument('--world-identities', action='store_true', help='read-only saved-to-runtime UniverseID import capture; snapshot boot only')
    p.add_argument('--player-probe', choices=('observe','native-local-negative','agreed-singleton','agreed-throttle','authority-negative','agreed-fire','agreed-getup','diagnostic-getup'), help='hash native singleton; local/authority negative actions or admitted p1-only native controls (never two independent players)')
    p.add_argument('--player-observer', action='store_true', help='read-only native update/trigger context trace; first admitted UI onward')
    p.add_argument('--player-weapon-rng', action='store_true', help='default-off saved-player/gun native launch RNG scope; bullet Move RNG stays native')
    p.add_argument('--player-request-observer', action='store_true', help='read-only native request/Agent queue observer; no pilot authority or input writes')
    p.add_argument('--player-transition-observer', action='store_true', help='read-only controller transfer and deferred model receipts; optional separate manifest supports initial Nav/Player watches; requires observe-only player probe and early queue trace')
    p.add_argument('--player-transition-manifest', type=Path, help='separate schema2 saved Nav/Player watch JSON; read-only, requires transition observer')
    p.add_argument('--player-transition-queue-evidence', action='store_true', help='copied early/unmatched queue diagnostics for the exclusive local GetUp fixture; never accepted tickets')
    p.add_argument('--player-transition-prebind-receipt', action='store_true', help='prospective ordinary-return/bind proof for the one selected initialization model order; requires copied queue diagnostics')
    p.add_argument('--player-solver-observer', action='store_true', help='read-only existing player/12 registered fixture solver calls; no additional native control context')
    p.add_argument('--world-hash-stride', type=int, help='hash every imported component at tick1 and every N ticks; read-only, requires --world-identities')
    p.add_argument('--world-hash-schema', choices=('WORLD_IMPORTED_1','WORLD_IMPORTED_2'), default='WORLD_IMPORTED_1', help='opt-in enriched Nav timing/owned pending state; legacy raw timing remains the default')
    p.add_argument('--world-hash-watch', action='append', default=[], metavar='SAVED_ID', help='retain canonical hash bytes for up to64 saved IDs (16 lowercase hex digits); requires --world-hash-stride')
    p.add_argument('--world-trace-ticks', type=int, help='read-only native motion trace for watched imported ships through tick2..12; requires serial pathing and all motion diagnostic hooks')
    p.add_argument('--world-trace-early', action='store_true', help='configure watched identities before loading and observe postload/first-pathing/AI producer boundaries')
    p.add_argument('--world-queue-trace', action='store_true', help='read-only manager enqueue and Agent pending append provenance; requires early world trace')
    p.add_argument('--world-wait-manifest', type=Path, help='read-only selected saved Wait restoration observer; snapshot-bound JSON manifest, requires early world trace')
    p.add_argument('--world-wait-rng-manifest', type=Path, help='EXPERIMENT: isolate RNG only for exact selected saved Wait restores; requires matching schema2 observation manifest')
    p.add_argument('--world-action-manifest', type=Path, help='read-only selected early script actions; requires snapshot, early queue trace and explicit --npc-diag')
    p.add_argument('--world-find-rng-manifest', type=Path, help='explicit single-use FindComponent RNG experiment; requires exact reviewed Wait/Action manifests and typed Values')
    p.add_argument('--world-values', action='store_true', help='read-only typed outputs and final component candidates inside the selected early Actions')
    p.add_argument('--world-safe-position', action='store_true', help='passive selected GetSafePos candidate and final Value receipts')
    p.add_argument('--world-position', action='store_true', help='read-only post-evaluation CreatePosition inputs inside selected early Actions')
    p.add_argument('--world-position-rng-manifest', type=Path, help='explicit single patrol CreatePosition RNG scope; requires reviewed Position and Find composition')
    p.add_argument('--docking-shape-rng', action='store_true', help='scope native shape refresh for the two disposable dock modules')
    p.add_argument('--docking-shape-refresh', action='store_true', help='refresh the two fixture dock modules every native body update; requires --docking-shape-rng')
    p.add_argument('--agent-diag', action='store_true', help='observe complete fixture pathing-agent updates')
    p.add_argument('--spline-diag', action='store_true', help='read-only fixture spline flight controller trace')
    p.add_argument('--spline-refresh', action='store_true', help='refresh native spline obstacle geometry each full update; requires --spline-diag and --agent-full')
    p.add_argument('--spline-state', action='store_true', help='also hash native spline progress and three speed-limit factors')
    p.add_argument('--spline-join', action='store_true', help='wait up to one second for the fixture spline request before consuming its native result')
    p.add_argument('--agent-rng', action='store_true', help='isolate RNG within complete fixture pathing-agent updates')
    p.add_argument('--agent-full', action='store_true', help='force full updates before fixture pathing-agent behaviour selection')
    p.add_argument('--ship-diag', action='store_true', help='observe fixture ship native Move and recursive child walk')
    p.add_argument('--ship-rng', action='store_true', help='isolate RNG within fixture ship Move and child walk')
    p.add_argument('--params-init', action='store_true', help='recompute fixture movement parameters once after snapshot load')
    p.add_argument('--weapon-diag', action='store_true', help='observe fixture guns by physical upgrade slot')
    p.add_argument('--weapon-rng', action='store_true', help='isolate RNG within each fixture gun native Move')
    p.add_argument('--params-diag', action='store_true', help='observe native movement-parameter builder callers')
    p.add_argument('--nav-diag', action='store_true', help='observe fixture navigation upkeep and refresh deadlines')
    p.add_argument('--nav-rng', action='store_true', help='isolate RNG within fixture navigation upkeep')
    p.add_argument('--nav-initial-refresh', action='store_true', help='make the first registered native navigation update refresh parameters')
    p.add_argument('--strict-state', action='store_true', help='hash raw hull/shield state and native pose/velocities as well as UI state')
    p.add_argument('--shield-refresh', action='store_true', help='invoke the native shield getter at the simulation clock before strict cache reads')
    p.add_argument('--strict-physics', action='store_true', help='also hash native attention and semantic physics state, including wrecks (requires --strict-state)')
    p.add_argument('--velocity-diag', action='store_true', help='observe fixture native velocity feedback writes')
    p.add_argument('--body-diag', action='store_true', help='read frame-end fixture physics body state')
    p.add_argument('--shape-diag', action='store_true', help='observe fixture physics shape refresh calls')
    p.add_argument('--shape-rng', action='store_true', help='isolate fixture physics shape refresh RNG')
    p.add_argument('--shape-initial-refresh', action='store_true', help='align first refresh for each fixture compound holder')
    p.add_argument('--bullet-diag', action='store_true', help='trace fixture bullet creation and native Move')
    p.add_argument('--bullet-rng', action='store_true', help='isolate fixture bullet Move RNG through native impact callbacks')
    p.add_argument('--death-diag', action='store_true', help='trace fixture native deferred KillEvent handling')
    p.add_argument('--death-rng', action='store_true', help='isolate fixture native KillEvent RNG including wreck creation')
    p.add_argument('--mass-diag', action='store_true', help='observe native shape notifications and mass property changes')
    p.add_argument('--attention-diag', action='store_true', help='trace native fixture attention refresh')
    p.add_argument('--attention-rng', action='store_true', help='isolate native fixture attention refresh RNG')
    p.add_argument('--attention-initial-refresh', action='store_true', help='align first native fixture attention refresh deadline')
    p.add_argument('--session-clients', action='store_true',
                   help='native input wire 3: N=2..8 clients from the lane session (@X4LANE_CLIENTS@); relay needs --clients {clients}')
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    if args.session_clients and (args.command_test or args.player_probe or args.docking_state or args.docking_shape_rng):
        p.error('--session-clients excludes the two-player command, player and docking fixtures')
    if args.player_probe:
        if not args.snapshot or args.mode!='battle':p.error('--player-probe requires a battle snapshot')
        if (args.player_probe.startswith('agreed-') or args.player_probe=='authority-negative') and args.command_test:p.error('native singleton and tactical command plans are exclusive')
        args.world_identities = True
    if args.player_probe=='agreed-fire':
        if not (args.player_observer and (args.weapon_rng or args.weapon_diag) and (args.bullet_rng or args.bullet_diag) and args.damage_diag):
            p.error('--player-probe agreed-fire requires player/weapon/bullet/damage observers')
    if args.player_weapon_rng and args.player_probe!='agreed-fire':p.error('--player-weapon-rng requires agreed-fire')
    if args.player_request_observer and not (args.player_solver_observer and (args.agent_diag or args.agent_rng or args.agent_full)):
        p.error('--player-request-observer requires --player-solver-observer and an Agent observer')
    if args.player_solver_observer and not (args.player_observer and args.serial_pathing and (args.flight_diag or args.flight_rng or args.flight_full)):
        p.error('--player-solver-observer requires --serial-pathing, --player-observer and a flight observer')
    if args.player_observer and args.player_probe not in ('observe','agreed-throttle','agreed-fire'):
        p.error('--player-observer requires observe, agreed-throttle or agreed-fire player probe')
    if args.registry_md_trace and (not args.world_identities or not args.serial_pathing or not (args.ai_diag or args.ai_rng) or not (args.interrupt_diag or args.interrupt_rng)):
        p.error('--registry-md-trace requires --world-identities, --serial-pathing, --ai-diag or --ai-rng, and --interrupt-diag or --interrupt-rng')
    if args.snapshot_prebind and (not args.snapshot or args.mode!='battle' or not args.world_identities or not args.serial_pathing):
        p.error('--snapshot-prebind requires a battle snapshot, --world-identities and --serial-pathing')
    fixture=None;snapshot_checksum=None
    if args.snapshot_prebind:
        try:
            snapshot_checksum=hashlib.sha256(args.snapshot.read_bytes()).hexdigest()
            fixture=snapshot_fixture(args.snapshot)
            if hashlib.sha256(args.snapshot.read_bytes()).hexdigest()!=snapshot_checksum:
                raise ValueError('snapshot changed while validating the fixture')
        except (ValueError,OSError,ET.ParseError) as error:p.error(str(error))
    player_order_lines=None
    if args.player_orders:
        if args.mode!='battle' or not args.snapshot:p.error('--player-orders requires a battle snapshot')
        if args.command_test or (args.player_probe and args.player_probe!='observe'):p.error('--player-orders excludes tactical and native player command plans')
        if not args.session_clients:p.error('--player-orders needs --session-clients (native input wire 3)')
        args.world_identities=True
        actors={}
        def reference(text):
            match=re.fullmatch(r'@(sector:)?([1-9][0-9]?)',text)
            if not match or int(match.group(2))>12:raise ValueError(f'invalid fixture reference {text!r}')
            if 'ids' not in actors:actors['ids']=fixture or snapshot_fixture(args.snapshot)
            saved=actors['ids'][int(match.group(2))-1]
            return player_orders.fixture_sector(args.snapshot,saved,_snapshot_starts) if match.group(1) else saved
        try:player_order_lines=player_orders.overlay_config(json.loads(args.player_orders.read_text()),reference)
        except (ValueError,TypeError,KeyError,OSError,ET.ParseError) as error:p.error(str(error))
    if args.world_identities and (not args.snapshot or (args.mode!='battle' and not args.gate_so)):
        p.error('--world-identities requires a battle snapshot (or the Linux gate with a snapshot)')
    if args.world_hash_stride is not None and (not args.world_identities or not 1<=args.world_hash_stride<=18000):
        p.error('--world-hash-stride requires --world-identities and stride1..18000')
    if args.world_hash_schema=='WORLD_IMPORTED_2' and args.world_hash_stride is None:
        p.error('--world-hash-schema WORLD_IMPORTED_2 requires --world-hash-stride')
    if args.world_hash_watch and (args.world_hash_stride is None or len(args.world_hash_watch)>64 or len(set(args.world_hash_watch))!=len(args.world_hash_watch) or any(len(s)!=16 or any(c not in '0123456789abcdef' for c in s) or int(s,16)==0 for s in args.world_hash_watch)):
        p.error('--world-hash-watch requires stride,1..64 unique nonzero16-digit lowercase hex IDs')
    if args.world_trace_ticks is not None:
        if not 2<=args.world_trace_ticks<=12 or not args.world_hash_watch or args.world_hash_stride!=1 or not args.serial_pathing:
            p.error('--world-trace-ticks requires ticks2..12, world watches, hash stride1, and serial pathing')
        args.ship_diag=args.nav_diag=args.agent_diag=args.flight_diag=args.spline_diag=True
    if args.world_trace_early:
        if args.world_trace_ticks is None:p.error('--world-trace-early requires --world-trace-ticks')
        args.ai_diag=True
    if args.world_queue_trace and not args.world_trace_early:p.error('--world-queue-trace requires --world-trace-early')
    if args.player_transition_observer and (args.player_probe not in ('observe','agreed-getup','diagnostic-getup') or not args.world_queue_trace or args.command_test):
        p.error('--player-transition-observer requires observe/agreed-getup, --world-queue-trace and no tactical command plan')
    transition_wire=None;transition_last=None
    if args.player_transition_manifest:
        if not args.player_transition_observer or not args.snapshot:p.error('--player-transition-manifest requires snapshot and transition observer')
        try:transition_wire,transition_last=transition_manifest(json.loads(args.player_transition_manifest.read_text()),args.snapshot,_snapshot_starts)
        except (ValueError,TypeError,KeyError,OSError,ET.ParseError) as error:p.error(str(error))
    if args.player_transition_queue_evidence and (args.player_probe!='diagnostic-getup' or not args.player_transition_manifest or not args.player_transition_observer):
        p.error('--player-transition-queue-evidence requires the explicit diagnostic-getup selected watch')
    if args.player_transition_prebind_receipt and not args.player_transition_queue_evidence:
        p.error('--player-transition-prebind-receipt requires --player-transition-queue-evidence')
    if args.player_probe in ('agreed-getup','diagnostic-getup'):
        if (not args.player_transition_observer or args.world_hash_stride!=1 or
            transition_wire!='2|00000000000ae79c/p/ship_arg_s_scout_01_a_macro/player' or transition_last!=120 or
            hashlib.sha256(args.snapshot.read_bytes()).hexdigest()!='7816351056e6040d92ae9a4619ae2e15a577fa26a19134f11e20746317e61aa8'):
            p.error('agreed-getup requires the pinned snapshot, exact controlled-ship watch120, and strict world hash stride1')
    if args.player_probe=='diagnostic-getup':
        allowed={'serial_pathing','world_identities','flight_diag','ai_diag','agent_diag','ship_diag','nav_diag','spline_diag',
                 'world_trace_early','world_queue_trace','player_transition_observer','player_transition_queue_evidence','player_transition_prebind_receipt'}
        forbidden=[name for name,value in vars(args).items() if value is True and name not in allowed]
        if forbidden or any(getattr(args,key,None) for key in ('world_wait_manifest','world_wait_rng_manifest','world_action_manifest',
            'world_find_rng_manifest','command_test','command_replay')):
            p.error('diagnostic-getup permits only its explicit passive observation contract: '+','.join(forbidden))
        if not args.serial_pathing or not args.world_identities or not args.world_trace_early or not args.world_queue_trace:
            p.error('diagnostic-getup requires serial pathing, saved identities and complete configured passive trace dependencies')
    action_wire=None
    if args.world_values and not args.world_action_manifest:p.error('--world-values requires --world-action-manifest')
    if args.world_safe_position and not args.world_position:p.error('--world-safe-position requires --world-position and its Action/Values dependencies')
    if args.world_position and not (args.world_values and args.world_action_manifest):p.error('--world-position requires --world-values and --world-action-manifest')
    if args.world_action_manifest:
        if not args.snapshot or not args.world_queue_trace or not args.world_trace_early or not args.npc_diag:
            p.error('--world-action-manifest requires snapshot, early queue trace and explicit --npc-diag')
        try:action_wire=action_manifest_wire(json.loads(args.world_action_manifest.read_text()),args.snapshot,_snapshot_starts)
        except (ValueError,TypeError,KeyError,OSError,ET.ParseError) as error:p.error(str(error))
    wait_wire=None;wait_rng_wire=None;wait_document=None
    if args.world_wait_manifest:
        if not args.world_trace_early or not args.snapshot:p.error('--world-wait-manifest requires snapshot and early world trace')
        try:
            wait_document=json.loads(args.world_wait_manifest.read_text());wait_wire=wait_manifest(wait_document,args.snapshot,_snapshot_starts)
        except (ValueError,TypeError,KeyError,OSError,ET.ParseError) as error:p.error(str(error))
    if args.world_wait_rng_manifest:
        if not wait_document:p.error('--world-wait-rng-manifest requires --world-wait-manifest')
        try:wait_rng_wire=wait_rng_manifest_wire(json.loads(args.world_wait_rng_manifest.read_text()),wait_document)
        except (ValueError,TypeError,KeyError,OSError) as error:p.error(str(error))
    find_wire=None
    if args.world_find_rng_manifest:
        if not args.world_values or not args.world_action_manifest or not args.world_wait_manifest or not args.world_wait_rng_manifest or not args.snapshot:
            p.error('--world-find-rng-manifest requires typed Values, snapshot and Wait/Wait-RNG/Action manifests')
        try:
            find_document=json.loads(args.world_find_rng_manifest.read_text());find_wire=find_manifest_wire(find_document)
            find_verify_inputs(find_document,args.snapshot,args.world_wait_manifest,args.world_wait_rng_manifest,args.world_action_manifest)
        except (ValueError,TypeError,KeyError,OSError) as error:p.error(str(error))
    position_rng_manifest=None
    if args.world_position_rng_manifest:
        if not args.world_position or not find_wire:p.error('--world-position-rng-manifest requires --world-position and explicit --world-find-rng-manifest')
        try:
            position_rng_document=json.loads(args.world_position_rng_manifest.read_text())
            position_rng_manifest=position_rng_wire(position_rng_document)
            if position_rng_document['expected_find_composition']!=find_document['schema']:raise ValueError('Position RNG Find composition mismatch')
        except (ValueError,TypeError,KeyError,OSError) as error:p.error(str(error))
    if args.command_plan == 'docking':args.docking_state = True
    if args.docking_exit!='diagonal' and args.command_plan!='docking':p.error('--docking-exit above requires the docking command plan')
    if args.docking_shape_refresh and not args.docking_shape_rng:p.error('--docking-shape-refresh requires --docking-shape-rng')
    if args.docking_state and not (args.command_test and args.fleet_state):p.error('--docking-state requires --command-test and --fleet-state')
    if args.docking_shape_rng:
        if not args.docking_state or not args.shape_rng or not args.shape_initial_refresh:p.error('--docking-shape-rng requires docking state, --shape-rng and --shape-initial-refresh')
    if args.damage_diag:
        if not args.snapshot:p.error('--damage-diag requires snapshot boot')
        args.actor_diag = True
    if args.contact_diag:
        if not args.snapshot:p.error('--contact-diag requires snapshot boot')
        args.actor_diag = True
    if args.npc_rng or args.npc_deadline or args.npc_pilot:args.npc_diag = True
    if args.npc_diag:
        if not args.snapshot or args.mode!='battle':p.error('--npc-diag requires a battle snapshot')
        args.world_identities = True
        args.interrupt_diag = True
    if args.interrupt_rng:args.interrupt_diag = True
    if args.interrupt_diag:
        args.ai_diag = True
    if args.radar_initial_refresh and not args.radar_rng:p.error('--radar-initial-refresh requires --radar-rng')
    if args.radar_rng:args.radar_serial = True
    if args.radar_serial:
        if not args.serial_pathing:p.error('--radar-serial requires --serial-pathing')
        args.radar_diag = True
    if args.radar_diag:
        args.ai_diag = True
    if args.command_test and (args.mode != 'battle' or not args.snapshot):
        p.error('--command-test requires a battle snapshot')
    if args.command_plan != 'combat' and not args.command_test:
        p.error('--command-plan requires --command-test')
    if args.boost_diag and not args.command_test:
        p.error('--boost-diag requires --command-test')
    if args.travel_diag and not args.command_test:
        p.error('--travel-diag requires --command-test')
    if bool(args.command_replay) != (args.command_plan == 'replay'):
        p.error('--command-replay and --command-plan replay require each other')
    replay = replay_plan(args.command_replay) if args.command_replay else None
    if args.order_state and not (args.strict_physics and args.steering_init):
        p.error('--order-state requires --strict-physics and --steering-init')
    if args.spline_refresh and not (args.spline_diag and args.agent_full):
        p.error('--spline-refresh requires --spline-diag and --agent-full')
    if args.spline_state and not (args.spline_diag and args.order_state):
        p.error('--spline-state requires --spline-diag and --order-state')
    if args.spline_join and not (args.spline_diag and args.agent_full and args.serial_pathing):
        p.error('--spline-join requires --spline-diag, --agent-full and --serial-pathing')
    if args.fleet_state and not args.spline_state:
        p.error('--fleet-state requires --spline-state')
    if args.command_plan=='fleet' and not args.fleet_state:
        p.error('--command-plan fleet requires --fleet-state')
    if not 1 <= args.trace_stride <= 600:
        p.error('--trace-stride must be 1..600')
    if args.trace_window and not 0 <= args.trace_window[0] <= args.trace_window[1] <= 18000:
        p.error('--trace-window must satisfy 0 <= FIRST <= LAST <= 18000')
    if args.avoidance_refresh and not args.flight_full:
        p.error('--avoidance-refresh requires --flight-full')
    if args.steering_init and not args.flight_rng:
        p.error('--steering-init requires --flight-rng')
    if args.strict_physics and not args.strict_state:
        p.error('--strict-physics requires --strict-state')
    if args.strict_state and args.mode != 'battle':
        p.error('--strict-state requires battle mode')
    if args.shield_refresh and (not args.strict_state or not args.snapshot):
        p.error('--shield-refresh requires a strict-state battle snapshot')
    if args.gate_so:
        # The Linux gate core owns fixed dt itself (snapshot boot) and has no fixture modules yet;
        # world identities and the world hash are ported (src/native/engine/linux/gate_world.h).
        if not args.snapshot or args.mode != 'clock':
            p.error('--gate-so supports the snapshot boot with --mode clock only')
        allowed = {'gate_so', 'snapshot', 'mode', 'session_clients', 'seed_rng', 'out', 'serial_pathing', 'path_determinism', 'region_rng', 'ai_wait_rng', 'md_resume_rng', 'boot_hold', 'event_order', 'event_rows', 'gravidar', 'gravidar_deadlines', 'travel_snapshot', 'runtime_ids', 'runtime_id_callers', 'script_rng', 'script_rng_mask', 'script_rng_rows', 'script_rng_natural', 'script_rng_unmapped', 'md_timed_rng', 'find_order', 'loadout_order', 'loadout_order_rows', 'attention_all', 'nav_key', 'step_trace', 'step_trace_ticks', 'fp_redirect', 'fp_redirect_watch', 'fp_redirect_ticks', 'coverage', 'coverage_list', 'gravidar_wide', 'spawn_rng', 'created_identities', 'created_load', 'portable_created', 'nav_cadence', 'nav_params_init', 'nav_probe_init',
                   'world_identities', 'world_hash_stride', 'world_hash_schema', 'world_hash_watch'}
        unported = sorted(k for k, v in vars(args).items() if k not in allowed and v != p.get_default(k))
        if unported:
            p.error('not ported to the Linux gate: ' + ', '.join('--' + k.replace('_', '-') for k in unported))
    if not args.snapshot and not args.fixedtick_dll:
        p.error('ordinary gate requires --fixedtick-dll')
    agent = args.agent_diag or args.agent_rng or args.agent_full or args.spline_diag
    ship = args.ship_diag or args.ship_rng
    attention = args.attention_diag or args.attention_rng or args.attention_initial_refresh
    death = args.death_diag or args.death_rng
    bullet = args.bullet_diag or args.bullet_rng
    weapon = args.weapon_diag or args.weapon_rng or bullet
    nav = args.nav_diag or args.nav_rng or args.nav_initial_refresh
    shape = args.shape_diag or args.shape_rng or args.shape_initial_refresh
    if (args.flight_diag or args.flight_rng or args.flight_full or args.ai_diag or args.ai_rng or agent or ship or weapon or nav or shape or death or attention or args.mass_diag or args.params_init or args.params_diag or args.velocity_diag) and not args.snapshot:
        p.error('native fixture experiments require snapshot boot without the fixedtick DLL')
    if args.out.exists():
        p.error('output must be a new directory so previous native inputs remain frozen')
    root = Path(__file__).resolve().parents[2]
    args.out.mkdir(parents=True)
    for directory in ('ui', 'native', 'md'):
        (args.out/directory).mkdir()
    inputs = [(args.gate_dll, 'native/x4lan_gate_64.dll') if args.gate_dll else (args.gate_so, 'native/x4lan_gate.so'),
              (root/'tools/dev/native_gate_probe.lua', 'ui/native_gate_probe.lua')]
    if not args.snapshot:
        inputs.append((args.fixedtick_dll, 'native/x4lan_fixedtick_64.dll'))
    for source, target in inputs:
        shutil.copy2(source, args.out/target)
    if args.mode == 'battle':
        shutil.copy2(root/'tools/dev/native_gate_battle.xml', args.out/'md/native_gate_battle.xml')
    ui_files = 'ui/native_gate_probe.lua\n'
    if args.player_probe:
        shutil.copy2(root/'tools/dev/native_gate_player.lua',args.out/'ui/native_gate_player.lua')
        ui_files = 'ui/native_gate_player.lua\n'+ui_files
    if args.player_transition_observer:
        shutil.copy2(root/'tools/dev/native_gate_transition.lua',args.out/'ui/native_gate_transition.lua')
        ui_files = 'ui/native_gate_transition.lua\n'+ui_files
    if args.player_probe in ('agreed-getup','diagnostic-getup'):
        shutil.copy2(root/'tools/dev/native_gate_player_origin.lua',args.out/'ui/native_gate_player_origin.lua')
        ui_files = 'ui/native_gate_player_origin.lua\n'+ui_files
    if args.player_probe=='diagnostic-getup':
        shutil.copy2(root/'tools/dev/native_gate_getup_diagnostic.lua',args.out/'ui/native_gate_getup_diagnostic.lua')
        ui_files = 'ui/native_gate_getup_diagnostic.lua\n'+ui_files
    if args.order_state:
        shutil.copy2(root/'tools/dev/native_gate_orders.lua', args.out/'ui/native_gate_orders.lua')
        ui_files = 'ui/native_gate_orders.lua\n'+ui_files
    if args.fleet_state:
        shutil.copy2(root/'tools/dev/native_gate_fleet.lua', args.out/'ui/native_gate_fleet.lua')
        ui_files = 'ui/native_gate_fleet.lua\n'+ui_files
    if args.docking_state:
        (args.out/'libraries').mkdir()
        shutil.copy2(root/'tools/dev/native_gate_dock_plan.xml',args.out/'libraries/constructionplans.xml')
        shutil.copy2(root/'tools/dev/native_gate_docking.lua',args.out/'ui/native_gate_docking.lua')
        ui_files = 'ui/native_gate_docking.lua\n'+ui_files
    if args.command_test:
        shutil.copy2(root/'tools/dev/native_gate_commands.lua', args.out/'ui/native_gate_commands.lua')
        shutil.copy2(root/'tools/dev/native_gate_commands.xml', args.out/'md/native_gate_commands.xml')
        ui_files = 'ui/native_gate_commands.lua\n'+ui_files
        if args.command_plan == 'live':
            shutil.copy2(root/'tools/dev/native_gate_live.lua', args.out/'ui/native_gate_live.lua')
            ui_files = 'ui/native_gate_live.lua\n'+ui_files
    if args.player_orders:
        for name in ('native_gate_player_orders.lua','native_gate_player_orders_core.lua'):
            shutil.copy2(root/'tools/dev'/name,args.out/'ui'/name)
        shutil.copy2(root/'tools/dev/native_gate_player_orders.xml',args.out/'md/native_gate_player_orders.xml')
        ui_files = 'ui/native_gate_player_orders_core.lua\nui/native_gate_player_orders.lua\n'+ui_files
    if args.session_clients:
        # Protocol 3 input multiplexer (pilot, orders and hit claim sections share one blob).
        shutil.copy2(root/'tools/dev/native_gate_inputs_core.lua',args.out/'ui/native_gate_inputs_core.lua')
        ui_files = 'ui/native_gate_inputs_core.lua\n'+ui_files
    (args.out/'ui.xml.files').write_text(ui_files)
    (args.out/'ui/config.lua.append').write_text('''X4LAN_CONFIG.enabled = false
X4LAN_CONFIG.native_gate_test = true
X4LAN_CONFIG.native_gate_hash = "%s"
X4LAN_CONFIG.native_gate_start_time = 5
X4LAN_CONFIG.test_profile = "45841"
X4LAN_CONFIG.host = "127.0.0.1"
X4LAN_CONFIG.port = @X4LANE_PORT@
X4LAN_CONFIG.token = "@X4LANE_TOKEN@"
X4LAN_CONFIG.slot = "@X4LANE_SLOT@"
''' % args.mode)
    if args.session_clients:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_clients = @X4LANE_CLIENTS@\n')
    if args.command_test:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write(f'X4LAN_CONFIG.native_gate_command_test = "{args.command_test}"\n')
            config.write(f'X4LAN_CONFIG.native_gate_command_plan = "{args.command_plan}"\n')
            if replay:
                config.write('X4LAN_CONFIG.native_gate_command_replay = '+replay+'\n')
            if args.docking_state:
                config.write('X4LAN_CONFIG.native_gate_docking_state = true\n')
                config.write(f'X4LAN_CONFIG.native_gate_docking_exit = "{args.docking_exit}"\n')
            if args.boost_diag or args.command_plan == 'withdraw':
                config.write('X4LAN_CONFIG.native_gate_boost_diag = true\n')
            if args.travel_diag or args.command_plan == 'travel':
                config.write('X4LAN_CONFIG.native_gate_travel_diag = true\n')
    if player_order_lines:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write(player_order_lines)
    if args.order_state:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_order_state = true\n')
    if args.fleet_state:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_fleet_state = true\n')
    if args.trace_stride != 1:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write(f'X4LAN_CONFIG.native_gate_trace_stride = {args.trace_stride}\n')
    if args.trace_window:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_trace_window = {'+','.join(map(str,args.trace_window))+'}\n')
    if any((args.seed_rng, args.flight_rng, args.ai_rng, args.agent_rng, args.ship_rng,
            args.weapon_rng, args.nav_rng, args.shape_rng, args.bullet_rng, args.death_rng, args.attention_rng,args.npc_rng)):
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_rng_schema = 2\n')
    if args.snapshot:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_snapshot_sha = "'+(snapshot_checksum or hashlib.sha256(args.snapshot.read_bytes()).hexdigest())+'"\n')
            if fixture:config.write('X4LAN_CONFIG.native_gate_snapshot_fixture = {'+','.join(repr(saved) for saved in fixture)+'}\n')
    if args.serial_pathing:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_serial_pathing = true\n')
    if args.runtime_ids:
        if not args.gate_so or not args.snapshot or not args.world_identities:
            p.error('--runtime-ids needs --gate-so, the snapshot boot and --world-identities')
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_runtime_ids = true\n')
    if args.script_rng:
        if not args.gate_so or not args.snapshot or not args.world_identities:
            p.error('--script-rng needs --gate-so, the snapshot boot and --world-identities')
        mask = args.script_rng_mask if args.script_rng_mask is not None else 3
        if args.script_rng_natural and not args.script_rng_rows:
            p.error('--script-rng-natural needs --script-rng-rows')
        if args.script_rng_unmapped and not mask & 1:
            p.error('--script-rng-unmapped needs the AI bit of the mask')
        if not mask & 3:
            p.error('--script-rng-mask needs the AI or the MD bit')
        if args.md_timed_rng and not mask & 2:
            p.error('--md-timed-rng needs the MD bit of the mask')
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write(f"X4LAN_CONFIG.native_gate_script_rng = '{args.script_rng}'\n")
            config.write(f'X4LAN_CONFIG.native_gate_script_rng_mask = {mask}\n')
            if args.script_rng_rows:
                config.write('X4LAN_CONFIG.native_gate_script_rng_rows = true\n')
                config.write(f'X4LAN_CONFIG.native_gate_script_rng_rows_ticks = {args.script_rng_rows}\n')
            if args.script_rng_natural:
                config.write('X4LAN_CONFIG.native_gate_script_rng_natural = true\n')
            if args.script_rng_unmapped:
                config.write("X4LAN_CONFIG.native_gate_script_rng_unmapped = 'key'\n")
            if args.md_timed_rng:
                config.write('X4LAN_CONFIG.native_gate_md_timed_rng = true\n')
    elif args.script_rng_mask is not None or args.script_rng_rows or args.script_rng_natural or args.script_rng_unmapped or args.md_timed_rng:
        p.error('these options need --script-rng')
    if args.nav_cadence:
        if not args.gate_so or not args.snapshot or not args.world_identities:
            p.error('--nav-cadence needs --gate-so, the snapshot boot and --world-identities')
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write(f"X4LAN_CONFIG.native_gate_nav_cadence = '{args.nav_cadence}'\n")
            if args.nav_params_init:
                config.write('X4LAN_CONFIG.native_gate_nav_params_init = true\n')
            if args.nav_key:
                if not args.world_identities:
                    p.error('--nav-key needs --world-identities')
                config.write('X4LAN_CONFIG.native_gate_nav_key = true\n')
            if args.nav_probe_init:
                config.write('X4LAN_CONFIG.native_gate_nav_probe_init = true\n')
    elif args.nav_params_init or args.nav_probe_init or args.nav_key:
        p.error('--nav-params-init, --nav-probe-init and --nav-key need --nav-cadence')
    if args.created_identities:
        if not args.gate_so or not args.snapshot or not args.world_identities:
            p.error('--created-identities needs --gate-so, the snapshot boot and --world-identities')
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_created_identities = true\n')
            if args.created_load:
                config.write('X4LAN_CONFIG.native_gate_created_load = true\n')
            if args.portable_created:
                config.write('X4LAN_CONFIG.native_gate_portable_created = true\n')
    elif args.created_load or args.portable_created:
        p.error('--created-load and --portable-created need --created-identities')
    if args.gravidar_wide:
        if not args.gravidar:
            p.error('--gravidar-wide needs --gravidar')
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write(f"X4LAN_CONFIG.native_gate_gravidar_wide = '{args.gravidar_wide}'\n")
    if args.spawn_rng:
        if not args.gate_so or not args.snapshot:
            p.error('--spawn-rng needs --gate-so and the snapshot boot')
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write(f"X4LAN_CONFIG.native_gate_spawn_rng = '{args.spawn_rng}'\n")
    if args.find_order:
        if not args.gate_so or not args.snapshot or not args.world_identities:
            p.error('--find-order needs --gate-so, the snapshot boot and --world-identities')
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write(f"X4LAN_CONFIG.native_gate_find_order = '{args.find_order}'\n")
    if args.attention_all:
        if not args.gate_so or not args.snapshot or not args.world_identities:
            p.error('--attention-all needs --gate-so, the snapshot boot and --world-identities')
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_attention_initial_refresh = true\n')
            config.write('X4LAN_CONFIG.native_gate_attention_all = true\n')
    if args.step_trace:
        ids = [i.lower() for i in args.step_trace]
        if not args.gate_so or not args.snapshot or not args.world_identities or len(ids) > 4 or any(len(i) != 16 or any(c not in '0123456789abcdef' for c in i) for i in ids):
            p.error('--step-trace needs --gate-so, the snapshot boot, --world-identities and one to four 16-digit saved IDs')
        if not 1 <= args.step_trace_ticks <= 600:
            p.error('--step-trace-ticks: 1..600')
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_step_trace = {' + ', '.join(f"'{i}'" for i in ids) + '}\n')
            config.write(f'X4LAN_CONFIG.native_gate_step_trace_ticks = {args.step_trace_ticks}\n')
    if args.fp_redirect_watch and not args.fp_redirect:
        p.error('--fp-redirect-watch needs --fp-redirect')
    if args.fp_redirect:
        ids = [i.lower() for i in args.fp_redirect_watch or []]
        if len(ids) > 4 or any(len(i) != 16 or any(c not in '0123456789abcdef' for c in i) for i in ids) or (ids and not args.world_identities):
            p.error('--fp-redirect-watch: up to four 16-digit saved IDs, with --world-identities')
        if not 1 <= args.fp_redirect_ticks <= 600:
            p.error('--fp-redirect-ticks: 1..600')
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write(f"X4LAN_CONFIG.native_gate_fp_redirect = '{args.fp_redirect}'\n")
            if ids:
                config.write('X4LAN_CONFIG.native_gate_fp_redirect_watch = {' + ', '.join(f"'{i}'" for i in ids) + '}\n')
            config.write(f'X4LAN_CONFIG.native_gate_fp_redirect_ticks = {args.fp_redirect_ticks}\n')
    if args.coverage or args.coverage_list:
        if not args.gate_so or not args.coverage or not args.coverage_list or not 1 <= args.coverage <= 6000 or not args.coverage_list.is_file():
            p.error('--coverage needs --gate-so, 1..6000 ticks and an existing --coverage-list')
        (args.out / 'native').mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.coverage_list, args.out / 'native' / 'coverage.txt')
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write(f'X4LAN_CONFIG.native_gate_coverage = {args.coverage}\n')
    if args.loadout_order_rows and not args.loadout_order:
        p.error('--loadout-order-rows needs --loadout-order')
    if args.loadout_order:
        rows = args.loadout_order_rows
        if not args.gate_so or not args.snapshot or (rows and not args.world_identities):
            p.error('--loadout-order needs --gate-so and the snapshot boot (its rows --world-identities)')
        if rows and not (1 <= rows[0] <= rows[1] and rows[1] - rows[0] <= 600):
            p.error('--loadout-order-rows: 1 <= FROM <= TO, at most 600 ticks')
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write(f"X4LAN_CONFIG.native_gate_loadout_order = '{args.loadout_order}'\n")
            if rows:
                config.write(f'X4LAN_CONFIG.native_gate_loadout_order_rows = {{{rows[0]}, {rows[1]}}}\n')
    if args.runtime_id_callers:
        if not args.runtime_ids:
            p.error('--runtime-id-callers needs --runtime-ids')
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_runtime_ids_callers = true\n')
    if args.travel_snapshot:
        if not args.gate_so or not args.snapshot or not args.serial_pathing or not args.path_determinism or not args.path_determinism & 2:
            p.error('--travel-snapshot needs --gate-so, the snapshot boot, --serial-pathing and path determinism bit 2')
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_travel_snapshot = true\n')
    if args.gravidar_deadlines and not args.gravidar:
        p.error('--gravidar-deadlines needs --gravidar')
    if args.gravidar:
        if not args.gate_so or not args.snapshot or not args.world_identities or not args.serial_pathing:
            p.error('--gravidar needs --gate-so, the snapshot boot, --world-identities and --serial-pathing')
        with (args.out/'ui/config.lua.append').open('a') as config:
            for key in ('radar_serial', 'gravidar_key_all', 'gravidar_time_sync', 'ai_schedule', 'gravidar_load_pass', 'gravidar_sector_order'):
                config.write(f'X4LAN_CONFIG.native_gate_{key} = true\n')
            if args.gravidar_deadlines:
                config.write('X4LAN_CONFIG.native_gate_gravidar_deadlines = true\n')
    event_row_ticks = 20000 if args.gate_so else 600
    if args.event_rows is not None and (not args.event_order or not 0 <= args.event_rows <= event_row_ticks):
        p.error(f'--event-rows needs --event-order and 0..{event_row_ticks} ticks')
    if args.event_order:
        if not args.snapshot or not args.world_identities:
            p.error('--event-order needs the snapshot boot and --world-identities')
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_event_order = true\n')
            if args.event_rows is not None:
                config.write(f'X4LAN_CONFIG.native_gate_event_rows = {args.event_rows}\n')
    if args.md_resume_rng or args.boot_hold:
        if not args.snapshot:
            p.error('--md-resume-rng and --boot-hold need the snapshot boot')
        with (args.out/'ui/config.lua.append').open('a') as config:
            if args.md_resume_rng:
                config.write('X4LAN_CONFIG.native_gate_md_resume_rng = true\n')
            if args.boot_hold:
                config.write('X4LAN_CONFIG.native_gate_boot_hold = true\n')
    if args.ai_wait_rng:
        if not args.snapshot or not args.world_identities:
            p.error('--ai-wait-rng needs the snapshot boot and --world-identities')
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_ai_wait_rng = true\n')
    if args.region_rng:
        if not args.snapshot or not args.world_identities:
            p.error('--region-rng needs the snapshot boot and --world-identities')
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write(f"X4LAN_CONFIG.native_gate_region_rng = '{args.region_rng}'\n")
    if args.path_determinism:
        if not args.snapshot or (args.path_determinism & 1 and not args.serial_pathing):
            p.error('--path-determinism needs the snapshot boot, and mask bit 1 needs --serial-pathing')
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write(f'X4LAN_CONFIG.native_gate_path_determinism = {args.path_determinism}\n')
    if args.seed_rng:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_seed_rng = true\n')
    if args.interrupt_diag:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_interrupt_diag = true\n')
            config.write('X4LAN_CONFIG.native_gate_interrupt_steps = true\n')
            config.write('X4LAN_CONFIG.native_gate_nested_actions = true\n')
            config.write('X4LAN_CONFIG.native_gate_npc_lifecycle = true\n')
            if args.interrupt_rng:config.write('X4LAN_CONFIG.native_gate_interrupt_rng = true\n')
            if args.npc_rng:config.write('X4LAN_CONFIG.native_gate_npc_rng = true\n')
            if args.npc_deadline:config.write('X4LAN_CONFIG.native_gate_npc_deadline = true\n')
            if args.npc_diag:
                config.write('X4LAN_CONFIG.native_gate_npc_diag = true\n')
                config.write('X4LAN_CONFIG.native_gate_npc_finish = true\n')
                config.write('X4LAN_CONFIG.native_gate_npc_trigger = true\n')
                config.write('X4LAN_CONFIG.native_gate_npc_provenance = true\n')
                config.write('X4LAN_CONFIG.native_gate_npc_provenance_origin = true\n')
                if args.npc_pilot:config.write('X4LAN_CONFIG.native_gate_npc_pilot = 620203\nX4LAN_CONFIG.native_gate_npc_pilot_origin = true\n')
    if args.world_identities:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_world_identities = true\n')
            if args.world_hash_stride is not None:
                config.write(f'X4LAN_CONFIG.native_gate_world_hash_stride = {args.world_hash_stride}\n')
                config.write(f'X4LAN_CONFIG.native_gate_world_hash_schema = {args.world_hash_schema!r}\n')
                config.write('X4LAN_CONFIG.native_gate_world_timing_fields = true\n')
            if args.world_hash_watch:config.write('X4LAN_CONFIG.native_gate_world_hash_watch = {'+','.join(repr(s) for s in args.world_hash_watch)+'}\n')
            if args.world_trace_ticks is not None:config.write(f'X4LAN_CONFIG.native_gate_world_trace_ticks = {args.world_trace_ticks}\n')
            if args.world_trace_early:config.write('X4LAN_CONFIG.native_gate_world_trace_early = true\n')
            if args.world_queue_trace:config.write('X4LAN_CONFIG.native_gate_world_queue_trace = true\n')
            if wait_wire:config.write('X4LAN_CONFIG.native_gate_world_wait_manifest = '+json.dumps(wait_wire)+'\n')
            if wait_rng_wire:config.write('X4LAN_CONFIG.native_gate_world_wait_rng_manifest = '+json.dumps(wait_rng_wire)+'\n')
            if action_wire:config.write('X4LAN_CONFIG.native_gate_world_action_manifest = '+json.dumps(action_wire)+'\n')
            if args.world_values:config.write('X4LAN_CONFIG.native_gate_world_values = true\n')
            if args.world_safe_position:config.write('X4LAN_CONFIG.native_gate_world_safe_position = true\n')
            if args.world_position:config.write('X4LAN_CONFIG.native_gate_world_position = true\n')
            if find_wire:config.write('X4LAN_CONFIG.native_gate_world_find_rng_manifest = '+json.dumps(find_wire)+'\n')
            if position_rng_manifest:config.write('X4LAN_CONFIG.native_gate_world_position_rng_manifest = '+json.dumps(position_rng_manifest)+'\n')
            if args.player_probe:config.write(f'X4LAN_CONFIG.native_gate_player_probe = "{args.player_probe}"\n')
            if args.player_probe=='diagnostic-getup':config.write('X4LAN_CONFIG.native_gate_diagnostic_contract = "diagnostic-getup-1"\n')
            if args.player_probe in ('agreed-getup','diagnostic-getup'):
                config.write('X4LAN_CONFIG.native_gate_player_origin_token = "1|0000000000011e12|00000000000ae79c|1|120"\n')
            if args.player_observer:config.write('X4LAN_CONFIG.native_gate_player_observer = true\n')
            if args.player_request_observer:config.write('X4LAN_CONFIG.native_gate_player_request_observer = true\n')
            if args.player_transition_observer:config.write('X4LAN_CONFIG.native_gate_player_transition_observer = true\n')
            if args.player_transition_queue_evidence:config.write('X4LAN_CONFIG.native_gate_player_transition_queue_evidence = true\n')
            if args.player_transition_prebind_receipt:config.write('X4LAN_CONFIG.native_gate_player_transition_prebind_receipt = true\n')
            if transition_wire:
                config.write('X4LAN_CONFIG.native_gate_player_transition_manifest = '+json.dumps(transition_wire)+'\n')
                config.write('X4LAN_CONFIG.native_gate_player_transition_last_tick = '+str(transition_last)+'\n')
            if args.player_solver_observer:config.write('X4LAN_CONFIG.native_gate_player_solver_observer = true\n')
            if args.player_weapon_rng:config.write('X4LAN_CONFIG.native_gate_player_weapon_rng = true\n')
    if args.registry_md_trace:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_registry_md_trace = true\n')
            config.write('X4LAN_CONFIG.native_gate_registry_origin_schema = 1\n')
            config.write('X4LAN_CONFIG.native_gate_registry_origin_identity_schema = 1\n')
    if args.damage_diag:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_damage_diag = true\n')
    if args.contact_diag:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_contact_diag = true\n')
            config.write('X4LAN_CONFIG.native_gate_contact_order = true\n')
            config.write('X4LAN_CONFIG.native_gate_contact_properties = true\n')
    if args.docking_shape_rng:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_docking_shape_rng = true\n')
            config.write('X4LAN_CONFIG.native_gate_docking_extra_shapes = true\n')
            if args.docking_shape_refresh:config.write('X4LAN_CONFIG.native_gate_docking_shape_refresh = true\n')
    if args.actor_diag or args.flight_diag or args.flight_rng or args.flight_full or args.ai_diag or args.ai_rng or agent or ship or weapon or nav or shape or death or attention or args.mass_diag or args.params_init or args.params_diag or args.strict_state or args.velocity_diag or args.body_diag:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_actor_diag = true\n')
    if args.flight_diag or args.flight_rng or args.flight_full:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_flight_diag = true\n')
            if args.flight_rng:
                config.write('X4LAN_CONFIG.native_gate_flight_rng = true\n')
            if args.flight_full:
                config.write('X4LAN_CONFIG.native_gate_flight_full = true\n')
            if args.avoidance_refresh:
                config.write('X4LAN_CONFIG.native_gate_avoidance_refresh = true\n')
            if args.steering_init:
                config.write('X4LAN_CONFIG.native_gate_steering_init = true\n')
                config.write('X4LAN_CONFIG.native_gate_steering_schema = 2\n')
    if args.ai_diag or args.ai_rng:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_ai_diag = true\n')
            if args.radar_diag:
                config.write('X4LAN_CONFIG.native_gate_radar_diag = true\n')
            if args.radar_serial:
                config.write('X4LAN_CONFIG.native_gate_radar_serial = true\n')
                config.write('X4LAN_CONFIG.native_gate_radar_contacts = true\n')
            if args.radar_rng:
                config.write('X4LAN_CONFIG.native_gate_radar_rng = true\n')
            if args.radar_initial_refresh:
                config.write('X4LAN_CONFIG.native_gate_radar_initial_refresh = true\n')
            if args.ai_rng:
                config.write('X4LAN_CONFIG.native_gate_ai_rng = true\n')
    if args.spline_diag:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_spline_diag = true\n')
            if args.spline_refresh:
                config.write('X4LAN_CONFIG.native_gate_spline_refresh = true\n')
            if args.spline_state:
                config.write('X4LAN_CONFIG.native_gate_spline_state = true\n')
            if args.spline_join:
                config.write('X4LAN_CONFIG.native_gate_spline_join = true\n')
    if agent:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_agent_diag = true\n')
            if args.agent_rng:
                config.write('X4LAN_CONFIG.native_gate_agent_rng = true\n')
            if args.agent_full:
                config.write('X4LAN_CONFIG.native_gate_agent_full = true\n')
    if ship:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_ship_diag = true\n')
            if args.ship_rng:
                config.write('X4LAN_CONFIG.native_gate_ship_rng = true\n')
    if args.params_init:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_params_init = true\n')
    if weapon:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_weapon_diag = true\n')
            if args.weapon_rng:
                config.write('X4LAN_CONFIG.native_gate_weapon_rng = true\n')
    if args.params_diag:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_params_diag = true\n')
    if nav:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_nav_diag = true\n')
            if args.nav_rng:
                config.write('X4LAN_CONFIG.native_gate_nav_rng = true\n')
            if args.nav_initial_refresh:
                config.write('X4LAN_CONFIG.native_gate_nav_initial_refresh = true\n')
    if args.strict_state:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_strict_state = true\n')
            if args.shield_refresh:
                config.write('X4LAN_CONFIG.native_gate_shield_refresh = true\n')
            if args.strict_physics:
                config.write('X4LAN_CONFIG.native_gate_strict_physics = true\n')
    if attention:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_attention_diag = true\n')
            if args.attention_rng:
                config.write('X4LAN_CONFIG.native_gate_attention_rng = true\n')
            if args.attention_initial_refresh:
                config.write('X4LAN_CONFIG.native_gate_attention_initial_refresh = true\n')
    if args.mass_diag:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_mass_diag = true\n')
    if death:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_death_diag = true\n')
            if args.death_rng:
                config.write('X4LAN_CONFIG.native_gate_death_rng = true\n')
    if bullet:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_bullet_diag = true\n')
            if args.bullet_rng:
                config.write('X4LAN_CONFIG.native_gate_bullet_rng = true\n')
    if args.velocity_diag:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_velocity_diag = true\n')
    if args.body_diag:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_body_diag = true\n')
    if shape:
        with (args.out/'ui/config.lua.append').open('a') as config:
            config.write('X4LAN_CONFIG.native_gate_shape_diag = true\n')
            config.write('X4LAN_CONFIG.native_gate_shape_lifetimes = true\n')
            config.write('X4LAN_CONFIG.native_gate_shape_lifetime_ids = true\n')
            if args.shape_rng:
                config.write('X4LAN_CONFIG.native_gate_shape_rng = true\n')
            if args.shape_initial_refresh:
                config.write('X4LAN_CONFIG.native_gate_shape_initial_refresh = true\n')
    manifest = {str(f.relative_to(args.out)): hashlib.sha256(f.read_bytes()).hexdigest()
                for f in args.out.rglob('*') if f.is_file()}
    (args.out/'x4lane-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')


if __name__ == '__main__':
    main()
