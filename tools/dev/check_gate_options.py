"""Check a native gate config's boot option bits against lua_boot's dependency rules.

The gate refuses a start with code 10 when an option bit lacks a prerequisite
bit. This recomputes the bit mask exactly as native_gate_probe.lua does and
evaluates every rule in lockstep_gate.c's lua_boot offline, so an overlay can
be checked before two X4 instances are launched. Options the probe arms after
the boot (not boot bits) are checked against the probe's own prerequisites.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NAMED = {'REQUEST_OPTION': 1 << 52, 'SOLVER_OPTION': 1 << 50, 'REGISTRY_MD_BIT': 1 << 51}


def option_bits():
    probe = (ROOT/'tools/dev/native_gate_probe.lua').read_text()
    block = probe[probe.index('local options=(cfg.native_gate_serial_pathing'):probe.index('if diagnostic then assert(options==')]
    return [(k, int(v)) for k, v in re.findall(r'cfg\.(native_gate_\w+) and (\d+)', block)]


def rules():
    source = (ROOT/'src/native/engine/lockstep_gate.c').read_text()
    start = source.index('if(!boot_option_mask_valid(options) ||')
    end = source.index('{p_lua_pushnumber(L,result);return 1;}', start)
    lines = [line.strip().rstrip('|').strip() for line in source[start+3:end].split('\n')][1:]
    result = []
    for line in lines:
        expr = line.replace('(uint64_t)options', 'o').replace('ULL', '')
        expr = expr.replace('&&', ' and ').replace('||', ' or ').replace('!(', 'not (')
        for name, value in NAMED.items(): expr = expr.replace(name, str(value))
        expr = expr.rstrip(')').rstrip()
        while expr.count('(') > expr.count(')'): expr += ')'
        result.append((line, expr))
    return result


# Options armed by native_gate_probe.lua after the boot: accepted values and the
# boot bits whose Move wrapper they count in (any one of them).
PROBE_OPTIONS = {
    'native_gate_bullet_cadence': (('true', 'every', 'observe'), ('native_gate_bullet_rng', 'native_gate_bullet_diag'), 'bullet'),
    'native_gate_nav_cadence': (('true', 'far', 'every', 'observe'),
                                ('native_gate_nav_rng', 'native_gate_nav_diag', 'native_gate_nav_initial_refresh'), 'nav'),
    'native_gate_view_points': (('true', 'players', 'observe'), (), None),
    'native_gate_mass_traffic': (('true', 'sync', 'observe'), (), None),
    'native_gate_spawn_rng': (('key', 'observe'), (), None),
    'native_gate_region_pass': (('true', 'sync', 'observe'), (), None),
    'native_gate_view_rows': (('true',), (), None),
    'native_gate_view_triggers': (('block', 'observe'), (), None),
    'native_gate_view_points_source': (('player', 'ship'), (), None),
    'native_gate_region_camera': (('view',), (), None),
    'native_gate_region_fields': (('view',), (), None),
    'native_gate_mass_traffic_camera': (('view',), (), None),
    'native_gate_effect_camera': (('view',), (), None),
    'native_gate_highway_camera': (('view',), (), None),
    'native_gate_physics_scenes': (('viewpoints',), (), None),
    'native_gate_camera_context': (('view',), (), None),
    'native_gate_shape_refresh': (('view',), (), None),
    'native_gate_mining_camera': (('view',), (), None),
    'native_gate_xs_camera': (('view',), (), None),
    'native_gate_camera_probe': (('true',), (), None),
    'native_gate_highway_speed': (('key',), (), None),
    'native_gate_sound_rng': (('split',), (), None),
    'native_gate_view_scenes': (('true',), (), None),
    'native_gate_field_fill': (('view', 'block'), (), None),
    'native_gate_traffic_views': (('block', 'observe', 'view'), (), None),
    'native_gate_traffic_rows': (('true',), (), None),
    'native_gate_speak_timer': (('true',), (), None),
    'native_gate_cutscene_ready': (('true',), (), None),
    'native_gate_viewpoint_policy': (('sector',), (), None),
    'native_gate_effect_rows': (('true',), (), None),
    'native_gate_motion_watch_orders': (('true',), (), None),
    'native_gate_gravidar_digest': (('true',), (), None),
    'native_gate_nav_probe_init': (('true',), ('native_gate_nav_rng', 'native_gate_nav_diag', 'native_gate_nav_initial_refresh'), 'nav'),
    'native_gate_ship_sound_rng': (('true', 'split', 'observe'), (), None),
    'native_gate_player_timer': (('true',), (), None),
    'native_gate_nav_params_init': (('true',), ('native_gate_nav_rng', 'native_gate_nav_diag', 'native_gate_nav_initial_refresh'), 'nav'),
    'native_gate_nav_player': (('true',), ('native_gate_nav_rng', 'native_gate_nav_diag', 'native_gate_nav_initial_refresh'), 'nav'),
    'native_gate_attention_all': (('true',), ('native_gate_attention_rng', 'native_gate_attention_initial_refresh'), 'attention'),
}


# Probe options that need every one of the listed options (boot bits or keys).
PROBE_REQUIRES_ALL = {
    'native_gate_cutscene_ready': (('true',), ('native_gate_serial_pathing',)),
    'native_gate_actor_rng_all': (('true',), ('native_gate_world_identities', 'native_gate_agent_rng', 'native_gate_ship_rng')),
    'native_gate_created_load': (('true',), ('native_gate_world_identities', 'native_gate_created_identities')),
    'native_gate_gravidar_deadlines': (('true',), ('native_gate_world_identities', 'native_gate_gravidar_time_sync')),
    'native_gate_weapon_fire_rng': (('true',), ('native_gate_world_identities',)),
    'native_gate_warp_rng': (('true', 'split', 'observe'), ('native_gate_world_identities',)),
    'native_gate_transition_rows': (('true',), ('native_gate_world_identities',)),
    'native_gate_tempzone_order': (('true', 'order', 'observe'), ('native_gate_world_identities', 'native_gate_serial_pathing')),
    'native_gate_gravidar_posts': (('true',), ('native_gate_gravidar_key_all',)),
    'native_gate_gravidar_wide': (('true', 'wide', 'sort'), ()),
    'native_gate_shape_order': (('true', 'wait', 'sync'), ()),
    'native_gate_engine_refresh_rng': (('true',), ('native_gate_world_identities',)),
    'native_gate_portable_math': (('true',), ()),
    'native_gate_exact_reciprocal': (('true',), ()),
    'native_gate_fp_env': (('default', 'observe'), ()),
    'native_gate_decimal': (('true',), ()),
    'native_gate_tick_hz': (('60', '40', '30', '20'), ()),
    'native_gate_tick_pace': (('true',), ('native_gate_tick_hz',)),
    'native_gate_fast_reads': (('true',), ()),
    'native_gate_play_mode': (('true',), ()),
    'native_gate_build_rows': (('true',), ('native_gate_world_identities',)),
    'native_gate_economy_load_rng': (('true',), ('native_gate_world_identities',)),
    'native_gate_path_budget': (('25', '50', '100', '150', '200', '300', '500'), ('native_gate_serial_pathing',)),
    'native_gate_pass_stride': (('1', '2', '3', '4'), ()),
    'native_gate_event_rng': (('observe', 'key'), ('native_gate_world_identities', 'native_gate_event_order')),
    'native_gate_build_rng': (('true',), ('native_gate_world_identities',)),
    'native_gate_ui_cargo': (('block', 'observe'), ('native_gate_world_identities',)),
    'native_gate_supply_rows': (('true',), ('native_gate_world_identities',)),
    'native_gate_ui_lazy': (('block', 'observe'), ('native_gate_world_identities',)),
    'native_gate_loadout_order': (('observe', 'canonical'), ()),
    'native_gate_order_canon': (('observe', 'canonical'), ()),
    'native_gate_pick_order': (('observe', 'canonical'), ()),
    'native_gate_macro_arena': (('true',), ()),
    'native_gate_nav_octree': (('observe', 'sync', 'deadline', 'agreed'), ('native_gate_world_identities',)),
    'native_gate_nav_octree_snapshot': (('true',), ('native_gate_world_identities',)),
    'native_gate_body_order': (('sort', 'observe'),
                               ('native_gate_world_identities', 'native_gate_created_identities', 'native_gate_portable_created')),
    'native_gate_virtual_players': (('observe', 'construct'), ('native_gate_serial_pathing',)),
}


PREDICT_MODES = ('true', "'observe'", '"observe"', "'present'", '"present"', 'false')


def predict_rules(config_text, enabled):
    """native_gate_pilot_predict: a mode or a per-client list of modes; needs the
    piloting prototype, and 'present' needs the render RNG split's slot-10 wrapper."""
    failures = []
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_pilot_predict\s*=\s*([^\n;]+)', config_text):
        value = value.split('--')[0].strip()
        modes = [m.strip() for m in value[1:-1].split(',')] if value.startswith('{') and value.endswith('}') else [value]
        if value in ('false', 'nil'): continue
        if not modes or any(m not in PREDICT_MODES + ('nil',) for m in modes):
            failures.append(dict(rule="native_gate_pilot_predict must be true, 'observe', 'present' or a per-client list of them",
                                 bits=[value]))
            continue
        if 'native_gate_pilot_inputs' not in enabled:
            failures.append(dict(rule='native_gate_pilot_predict needs native_gate_pilot_inputs', bits=['native_gate_pilot_inputs']))
        if any('present' in m for m in modes) and 'native_gate_render_rng_split' not in enabled:
            failures.append(dict(rule="native_gate_pilot_predict 'present' needs native_gate_render_rng_split = true",
                                 bits=['native_gate_render_rng_split']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_snapshot_sha\s*=', config_text):
            failures.append(dict(rule='native_gate_pilot_predict needs the snapshot boot', bits=['native_gate_snapshot_sha']))
    return failures


HIT_POLICIES = ('true', "'shooter'", '"shooter"', "'victim'", '"victim"', "'first'", '"first"')


def hit_claim_rules(config_text, enabled):
    """native_gate_hit_claims: a player-versus-player policy (true = shooter); needs the
    piloting prototype on the wire, camera-independent bullet tests and world identities."""
    failures = []
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_hit_claims\s*=\s*([^\n;]+)', config_text):
        value = value.split('--')[0].strip()
        if value in ('false', 'nil'): continue
        if value not in HIT_POLICIES:
            failures.append(dict(rule="native_gate_hit_claims must be true, 'shooter', 'victim' or 'first'", bits=[value]))
            continue
        for name in ('native_gate_pilot_inputs', 'native_gate_world_identities'):
            if name not in enabled:
                failures.append(dict(rule=f'native_gate_hit_claims needs {name}', bits=[name]))
        if not re.search(r"X4LAN_CONFIG\.native_gate_pilot_source\s*=\s*['\"]wire['\"]", config_text):
            failures.append(dict(rule="native_gate_hit_claims needs native_gate_pilot_source = 'wire'", bits=['native_gate_pilot_source']))
        if not re.search(r"X4LAN_CONFIG\.native_gate_bullet_cadence\s*=\s*(true|['\"]every['\"])", config_text):
            failures.append(dict(rule="native_gate_hit_claims needs native_gate_bullet_cadence = 'every'", bits=['native_gate_bullet_cadence']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_snapshot_sha\s*=', config_text):
            failures.append(dict(rule='native_gate_hit_claims needs the snapshot boot', bits=['native_gate_snapshot_sha']))
    return failures


FIRE_MODES = ('true', "'record'", '"record"', "'script'", '"script"')


def pilot_fire_rules(config_text, enabled):
    """native_gate_pilot_fire: 'record' (true) or 'script'; needs the piloting prototype,
    the all-ship RNG keys (launch draws) and the 'every' bullet cadence (creation-frame test)."""
    failures = []
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_pilot_fire\s*=\s*([^\n;]+)', config_text):
        value = value.split('--')[0].strip()
        if value in ('false', 'nil'): continue
        if value not in FIRE_MODES:
            failures.append(dict(rule="native_gate_pilot_fire must be true, 'record' or 'script'", bits=[value]))
            continue
        for name in ('native_gate_pilot_inputs', 'native_gate_actor_rng_all', 'native_gate_world_identities'):
            if name not in enabled:
                failures.append(dict(rule=f'native_gate_pilot_fire needs {name}', bits=[name]))
        slots = lua_value(config_text, 'native_gate_pilot_fire_slots')
        if slots is not None and slots != 'nil' and value not in ("'script'", '"script"'):
            failures.append(dict(rule="native_gate_pilot_fire_slots needs native_gate_pilot_fire = 'script'", bits=[value]))
        if not re.search(r"X4LAN_CONFIG\.native_gate_bullet_cadence\s*=\s*(true|['\"]every['\"])", config_text):
            failures.append(dict(rule="native_gate_pilot_fire needs native_gate_bullet_cadence = 'every'", bits=['native_gate_bullet_cadence']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_snapshot_sha\s*=', config_text):
            failures.append(dict(rule='native_gate_pilot_fire needs the snapshot boot', bits=['native_gate_snapshot_sha']))
    return failures


CAPTURE_SOURCES = ("'native'", '"native"', "'device'", '"device"', "'stock'", '"stock"', 'false', 'nil')


def capture_source_rules(config_text, enabled):
    """native_gate_pilot_capture_source: 'native' (X4's own controls, stop reason 113)
    or 'device', or a per-client list; 'native' needs the piloting prototype with
    local suppression and the capture (or the wire source)."""
    failures = []
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_pilot_capture_source\s*=\s*([^\n;]+)', config_text):
        value = value.split('--')[0].strip()
        modes = [m.strip() for m in value[1:-1].split(',')] if value.startswith('{') and value.endswith('}') else [value]
        if value in ('false', 'nil'): continue
        if not modes or any(m not in CAPTURE_SOURCES for m in modes):
            failures.append(dict(rule="native_gate_pilot_capture_source must be 'native', 'device', 'stock' or a per-client list of them",
                                 bits=[value]))
            continue
        if any('stock' in m for m in modes) and 'native_gate_pilot_stock' not in enabled:
            failures.append(dict(rule="native_gate_pilot_capture_source 'stock' needs native_gate_pilot_stock = true",
                                 bits=['native_gate_pilot_stock']))
        if not any('native' in m for m in modes): continue
        for name in ('native_gate_pilot_inputs', 'native_gate_pilot_suppress_local'):
            if name not in enabled:
                failures.append(dict(rule=f"native_gate_pilot_capture_source 'native' needs {name}", bits=[name]))
        wire = re.search(r"X4LAN_CONFIG\.native_gate_pilot_source\s*=\s*['\"]wire['\"]", config_text)
        if not wire and 'native_gate_pilot_capture' not in enabled:
            failures.append(dict(rule="native_gate_pilot_capture_source 'native' needs native_gate_pilot_capture or the wire source",
                                 bits=['native_gate_pilot_capture']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_snapshot_sha\s*=', config_text):
            failures.append(dict(rule='native_gate_pilot_capture_source needs the snapshot boot', bits=['native_gate_snapshot_sha']))
    return failures


def lua_value(config_text, option):
    m = re.search(r'X4LAN_CONFIG\.' + option + r'\s*=\s*([^\n;]+)', config_text)
    return m[1].split('--')[0].strip() if m else None


def lua_modes(value):
    if value is None:
        return []
    return [m.strip().strip('\'"') for m in value[1:-1].split(',')] if value.startswith('{') and value.endswith('}') else [value.strip('\'"')]


def stock_slots(config_text):
    """The stock slots (1-based) of native_gate_pilot_stock_slots: slot 1 alone when absent,
    every pilot slot for 'all', else the listed slots; None when the value is invalid."""
    value = lua_value(config_text, 'native_gate_pilot_stock_slots')
    count = len(lua_modes(lua_value(config_text, 'native_gate_pilot_slots')))
    if value is None or value == 'nil':
        return [1]
    if value.strip('\'"') == 'all':
        return list(range(1, count+1))
    items = lua_modes(value)
    if not value.startswith('{') or not items or not all(i.isdigit() for i in items):
        return None
    slots = [int(i) for i in items]
    if 1 not in slots or len(set(slots)) != len(slots) or any(not 1 <= s <= count for s in slots):
        return None
    return sorted(slots)


def pilot_stock_rules(config_text, enabled):
    """native_gate_pilot_stock (lockstep_pilot_stock.h, stop reason 155): the stock slots fly
    through X4's own player control path on every client: slot 1 always,
    native_gate_pilot_stock_slots = 'all' or a list adds slots 2..N (lockstep_pilot_slots.h,
    stop reason 161). Needs the wire piloting prototype with local suppression and the
    snapshot; a stock slot's client captures (capture source 'stock', no device), every other
    client samples its own slot ('device' with a device); a stock slot has no pilot record, so
    its client's prediction stays off, pilot fire reads the stock record's trigger (hit claims
    are keyed by the slot ships, not by records, and may run with it)."""
    value = lua_value(config_text, 'native_gate_pilot_stock')
    if value is None or value in ('false', 'nil'):
        return []
    if value != 'true':
        return [dict(rule='native_gate_pilot_stock must be true', bits=[value])]
    failures = []
    for name in ('native_gate_pilot_inputs', 'native_gate_pilot_suppress_local'):
        if name not in enabled:
            failures.append(dict(rule=f'native_gate_pilot_stock needs {name}', bits=[name]))
    if not re.search(r"X4LAN_CONFIG\.native_gate_pilot_source\s*=\s*['\"]wire['\"]", config_text):
        failures.append(dict(rule="native_gate_pilot_stock needs native_gate_pilot_source = 'wire'", bits=['native_gate_pilot_source']))
    if not re.search(r'X4LAN_CONFIG\.native_gate_snapshot_sha\s*=', config_text):
        failures.append(dict(rule='native_gate_pilot_stock needs the snapshot boot', bits=['native_gate_snapshot_sha']))
    slots = stock_slots(config_text)
    if slots is None:
        failures.append(dict(rule="native_gate_pilot_stock_slots must be 'all' or a list of pilot slots holding 1",
                             bits=['native_gate_pilot_stock_slots']))
        slots = [1]
    sources = lua_modes(lua_value(config_text, 'native_gate_pilot_capture_source'))
    devices = lua_modes(lua_value(config_text, 'native_gate_pilot_device'))
    wanted = ['stock' if i+1 in slots else 'device' for i in range(len(sources))]
    if len(sources) < 2 or len(sources) < max(slots) or sources != wanted:
        failures.append(dict(rule="native_gate_pilot_stock needs native_gate_pilot_capture_source 'stock' exactly on the stock slots' "
                                  "clients and 'device' on the others (e.g. {'stock','device'})", bits=['native_gate_pilot_capture_source']))
    elif len(devices) < len(sources) or any((d in ('false', 'nil')) != (m == 'stock') for d, m in zip(devices, sources)):
        failures.append(dict(rule='native_gate_pilot_stock needs native_gate_pilot_device = false on the stock slots\' clients and a device '
                                  'on the others (e.g. {false,<device>})', bits=['native_gate_pilot_device']))
    predict = lua_modes(lua_value(config_text, 'native_gate_pilot_predict'))
    if any(i+1 in slots and p not in ('false', 'nil') for i, p in enumerate(predict)):
        failures.append(dict(rule='native_gate_pilot_stock excludes prediction on the stock slots\' clients', bits=['native_gate_pilot_predict']))
    script = lua_value(config_text, 'native_gate_pilot_stock_script')
    names = tuple(q + name + q for q in '\'"' for name in ('demo', 'demo2', 'demo3', 'demo4', 'aim'))
    if script is not None and script != 'nil':
        if script.startswith('{') and not script.startswith('{{'):
            if any(entry not in names+('false', 'nil') for entry in [e.strip() for e in script.strip('{}').split(',') if e.strip()]):
                failures.append(dict(rule="native_gate_pilot_stock_script per client must be {'demo'|'demo2'|'demo3'|'demo4'|'aim'|false,...}", bits=[script]))
        elif script not in names and not script.startswith('{{'):
            failures.append(dict(rule="native_gate_pilot_stock_script must be 'demo', 'demo2'..'demo4', 'aim', a list of {tick,kind,id,value} or one per client",
                                 bits=[script]))
    return failures


def pilot_present_rules(config_text, enabled):
    """native_gate_pilot_present (lockstep_pilot_present.h, stop reason 162): needs stock slots and
    the render split (slot 10 runs in the presentation scope); the camera is X4's own on the
    client's slot, so the camera follow and anchor stay off."""
    value = lua_value(config_text, 'native_gate_pilot_present')
    if value is None or value in ('false', 'nil'):
        return []
    if value not in ('true', "'camera'", '"camera"'):
        return [dict(rule='native_gate_pilot_present must be true or camera', bits=[value])]
    failures = []
    if 'native_gate_pilot_stock' not in enabled or lua_value(config_text, 'native_gate_pilot_stock_slots') in (None, 'nil'):
        failures.append(dict(rule='native_gate_pilot_present needs native_gate_pilot_stock_slots', bits=['native_gate_pilot_stock_slots']))
    if 'native_gate_render_rng_split' not in enabled:
        failures.append(dict(rule='native_gate_pilot_present needs native_gate_render_rng_split', bits=['native_gate_render_rng_split']))
    for name in ('native_gate_pilot_camera_follow', 'native_gate_camera_anchor'):
        other = lua_value(config_text, name)
        if other is not None and other not in ('false', 'nil'):
            failures.append(dict(rule=f'native_gate_pilot_present excludes {name}', bits=[name]))
    return failures


def pilot_death_rules(config_text, enabled):
    """native_gate_pilot_death (lockstep_pilot_death.h, stop reason 163): a stock session; the slot's ship
    comes back only with native_gate_pilot_respawn, a delay in whole ticks."""
    death = lua_value(config_text, 'native_gate_pilot_death')
    respawn = lua_value(config_text, 'native_gate_pilot_respawn')
    suit = lua_value(config_text, 'native_gate_pilot_death_suit')
    failures = []
    if suit is not None and suit not in ('false', 'nil'):
        if suit != 'true':
            failures.append(dict(rule='native_gate_pilot_death_suit must be true', bits=[suit]))
        if death != 'true':
            failures.append(dict(rule='native_gate_pilot_death_suit needs native_gate_pilot_death', bits=['native_gate_pilot_death']))
        if lua_value(config_text, 'native_gate_pilot_present') in (None, 'false', 'nil'):
            failures.append(dict(rule='native_gate_pilot_death_suit needs native_gate_pilot_present', bits=['native_gate_pilot_present']))
        if 'native_gate_pilot_ship_state' not in enabled:
            failures.append(dict(rule='native_gate_pilot_death_suit needs native_gate_pilot_ship_state', bits=['native_gate_pilot_ship_state']))
    if death is not None and death not in ('false', 'nil', 'true'):
        failures.append(dict(rule='native_gate_pilot_death must be true', bits=[death]))
    if death == 'true' and 'native_gate_pilot_stock' not in enabled:
        failures.append(dict(rule='native_gate_pilot_death needs native_gate_pilot_stock', bits=['native_gate_pilot_stock']))
    if respawn is not None and respawn not in ('false', 'nil'):
        if death != 'true':
            failures.append(dict(rule='native_gate_pilot_respawn needs native_gate_pilot_death', bits=['native_gate_pilot_death']))
        if not respawn.isdigit() or not 1 <= int(respawn) <= 8388607:   # PILOT_TICK_MAX, lockstep_pilot_inputs.h
            failures.append(dict(rule='native_gate_pilot_respawn must be a delay of 1..8388607 ticks', bits=[respawn]))
    return failures


UI_REPLAY_FAMILIES = ('travel', 'target', 'orders')


def ui_replay_rules(config_text, enabled):
    """native_gate_ui_replay (lockstep_ui_replay.h, stop reason 164): true or a list of travel, target, orders. Every
    client captures its own slot, so every slot is a stock slot; client k's UI must be slot k's (the presentation);
    components travel as runtime IDs, so those must be the same on every client."""
    value = lua_value(config_text, 'native_gate_ui_replay')
    failures = []
    for option in ('native_gate_ui_replay_script', 'native_gate_ui_replay_fleet'):
        if lua_value(config_text, option) not in (None, 'nil', 'false') and value in (None, 'false', 'nil'):
            failures.append(dict(rule=f'{option} needs native_gate_ui_replay', bits=['native_gate_ui_replay']))
    if value is None or value in ('false', 'nil'):
        return failures
    if value != 'true':
        names = lua_modes(value)
        if not value.startswith('{') or not names or any(n not in UI_REPLAY_FAMILIES for n in names) or len(set(names)) != len(names):
            return failures + [dict(rule='native_gate_ui_replay must be true or a list of travel, target, orders', bits=[value])]
    if lua_value(config_text, 'native_gate_pilot_stock') != 'true' or (lua_value(config_text, 'native_gate_pilot_stock_slots') or '').strip('\'"') != 'all':
        failures.append(dict(rule="native_gate_ui_replay needs native_gate_pilot_stock_slots = 'all'", bits=['native_gate_pilot_stock_slots']))
    if lua_value(config_text, 'native_gate_pilot_present') in (None, 'false', 'nil'):
        failures.append(dict(rule='native_gate_ui_replay needs native_gate_pilot_present', bits=['native_gate_pilot_present']))
    rid = lua_value(config_text, 'native_gate_runtime_ids')
    if rid is None or rid.strip('\'"') in ('false', 'nil', 'observe'):
        failures.append(dict(rule='native_gate_ui_replay needs native_gate_runtime_ids in set mode', bits=['native_gate_runtime_ids']))
    if 'native_gate_world_identities' not in enabled:
        failures.append(dict(rule='native_gate_ui_replay needs native_gate_world_identities', bits=['native_gate_world_identities']))
    return failures


def crash_note_rules(config_text, enabled):
    """native_gate_crash_note (lockstep_crash_note.h): true or absent."""
    value = lua_value(config_text, 'native_gate_crash_note')
    if value is not None and value not in ('true', 'false', 'nil'):
        return [dict(rule='native_gate_crash_note must be true', bits=[value])]
    return []


def probe_rules(config_text, enabled):
    """Prerequisites of options native_gate_probe.lua arms after the boot."""
    failures = predict_rules(config_text, enabled) + hit_claim_rules(config_text, enabled) + pilot_fire_rules(config_text, enabled) + \
        capture_source_rules(config_text, enabled) + pilot_stock_rules(config_text, enabled) + pilot_present_rules(config_text, enabled) + \
        pilot_death_rules(config_text, enabled) + ui_replay_rules(config_text, enabled) + crash_note_rules(config_text, enabled)
    if lua_value(config_text, 'native_gate_pilot_stock_script') is not None and 'native_gate_pilot_stock' not in enabled:
        failures.append(dict(rule='native_gate_pilot_stock_script needs native_gate_pilot_stock', bits=['native_gate_pilot_stock']))
    # Virtual players step 3 spike: the construction tick belongs to 'construct' and is an admitted tick 2..18000.
    due = lua_value(config_text, 'native_gate_virtual_players_construct_tick')
    if due is not None and due != 'nil':
        if (lua_value(config_text, 'native_gate_virtual_players') or '').strip('\'"') != 'construct':
            failures.append(dict(rule="native_gate_virtual_players_construct_tick needs native_gate_virtual_players = 'construct'",
                                 bits=['native_gate_virtual_players']))
        if not re.fullmatch(r'[0-9]{1,5}', due) or not 2 <= int(due) <= 18000:
            failures.append(dict(rule='native_gate_virtual_players_construct_tick must be an admitted tick 2..18000', bits=[due]))
    for option, (values, required) in PROBE_REQUIRES_ALL.items():
        for value in re.findall(r'X4LAN_CONFIG\.' + option + r'\s*=\s*([^\s;]+)', config_text):
            value = value.strip('\'"')
            if value in ('false', 'nil'): continue
            if value not in values:
                failures.append(dict(rule=f'{option} must be {", ".join(values)}', bits=[value]))
            missing = [name for name in required if name not in enabled]
            if missing:
                failures.append(dict(rule=f'{option} needs {", ".join(required)}', bits=missing))
            if not re.search(r'X4LAN_CONFIG\.native_gate_snapshot_sha\s*=', config_text):
                failures.append(dict(rule=f'{option} needs the snapshot boot', bits=['native_gate_snapshot_sha']))
    for option, (values, modules, wrapper) in PROBE_OPTIONS.items():
        for value in re.findall(r'X4LAN_CONFIG\.' + option + r'\s*=\s*([^\s;]+)', config_text):
            value = value.strip('\'"')
            if value == 'false': continue
            if value not in values:
                failures.append(dict(rule=f'{option} must be {", ".join(values)}', bits=[value]))
            if modules and not enabled & set(modules):
                failures.append(dict(rule=f'{option} counts in the {wrapper} Move wrapper', bits=[' or '.join(modules)]))
            if not re.search(r'X4LAN_CONFIG\.native_gate_snapshot_sha\s*=', config_text):
                failures.append(dict(rule=f'{option} needs the snapshot boot', bits=['native_gate_snapshot_sha']))
    if re.search(r'X4LAN_CONFIG\.native_gate_nav_probe_init\s*=\s*true\b', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_nav_cadence\s*=\s*(?!false\b|nil\b)\S', config_text):
        failures.append(dict(rule='native_gate_nav_probe_init needs native_gate_nav_cadence', bits=['native_gate_nav_cadence']))
    if re.search(r'X4LAN_CONFIG\.native_gate_nav_params_init\s*=\s*true\b', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_nav_cadence\s*=\s*(?!false\b|nil\b)\S', config_text):
        failures.append(dict(rule='native_gate_nav_params_init needs native_gate_nav_cadence', bits=['native_gate_nav_cadence']))
    if re.search(r'X4LAN_CONFIG\.native_gate_nav_octree\s*=\s*[\'"]agreed[\'"]', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_clients\s*=\s*(?:[2-8]\b|@X4LANE_CLIENTS@)', config_text):
        failures.append(dict(rule="native_gate_nav_octree = 'agreed' needs the protocol 3 input wire (native_gate_clients)",
                             bits=['native_gate_clients']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_nav_octree_sites\s*=\s*([^\s;]+)', config_text):
        value = value.strip('\'"')
        if value in ('false', 'nil'): continue
        if value != 'true':
            failures.append(dict(rule='native_gate_nav_octree_sites must be true', bits=[value]))
        if not re.search(r'X4LAN_CONFIG\.native_gate_nav_octree\s*=\s*[\'"](deadline|agreed)[\'"]', config_text):
            failures.append(dict(rule="native_gate_nav_octree_sites needs native_gate_nav_octree = 'deadline' or 'agreed'",
                                 bits=['native_gate_nav_octree']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_nav_octree_delay\s*=\s*([^\s;]+)', config_text):
        if value == 'nil': continue
        if not re.fullmatch(r'[1-9][0-9]{0,3}', value) or int(value) > 3600:
            failures.append(dict(rule='native_gate_nav_octree_delay must be a tick count 1..3600', bits=[value]))
        if not re.search(r'X4LAN_CONFIG\.native_gate_nav_octree\s*=\s*[\'"]deadline[\'"]', config_text):
            failures.append(dict(rule="native_gate_nav_octree_delay needs native_gate_nav_octree = 'deadline'", bits=['native_gate_nav_octree']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_command_order\s*=\s*([^\s;]+)', config_text):
        value = value.strip('\'"')
        if value in ('false', 'nil'): continue
        if value not in ('observe', 'sort'):
            failures.append(dict(rule='native_gate_command_order must be observe or sort', bits=[value]))
        if 'native_gate_serial_pathing' not in enabled:
            failures.append(dict(rule='native_gate_command_order needs native_gate_serial_pathing', bits=['native_gate_serial_pathing']))
        if value == 'sort' and 'native_gate_travel_snapshot' not in enabled:
            failures.append(dict(rule='native_gate_command_order sort needs native_gate_travel_snapshot (the late-splice join)',
                                 bits=['native_gate_travel_snapshot']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_snapshot_sha\s*=', config_text):
            failures.append(dict(rule='native_gate_command_order needs the snapshot boot', bits=['native_gate_snapshot_sha']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_tempzone_order\s*=\s*([^\s;]+)', config_text):
        if value.strip('\'"') in ('true', 'order') and 'native_gate_travel_snapshot' not in enabled:
            failures.append(dict(rule='native_gate_tempzone_order order needs native_gate_travel_snapshot (the late-splice join)',
                                 bits=['native_gate_travel_snapshot']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_gravidar_wide\s*=\s*([^\s;]+)', config_text):
        missing = [name for name in ('native_gate_radar_serial', 'native_gate_world_identities') if name not in enabled]
        if value.strip('\'"') == 'sort' and missing:
            failures.append(dict(rule='native_gate_gravidar_wide sort needs native_gate_radar_serial, native_gate_world_identities '
                                      '(the gated Gravidar publish, zone keys)', bits=missing))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_draw_watch\s*=\s*([^\n;]+)', config_text):
        value = value.split('--')[0].strip()
        if value in ('false', 'nil'): continue
        ids = re.findall(r"['\"]([0-9a-f]{16})['\"]", value)
        if not (value.startswith('{') and value.endswith('}')) or not 1 <= len(ids) <= 4 or \
           len(ids) != len([v for v in value[1:-1].split(',') if v.strip()]):
            failures.append(dict(rule='native_gate_draw_watch must be a list of one to four 16-digit saved IDs', bits=[value]))
        if not re.search(r'X4LAN_CONFIG\.native_gate_actor_rng_all\s*=\s*true\b', config_text):
            failures.append(dict(rule='native_gate_draw_watch needs native_gate_actor_rng_all', bits=['native_gate_actor_rng_all']))
    if re.search(r'X4LAN_CONFIG\.native_gate_draw_watch_frame\s*=\s*(?!false\b|nil\b)\S', config_text):
        if not re.search(r'X4LAN_CONFIG\.native_gate_draw_watch_frame\s*=\s*true\b', config_text):
            failures.append(dict(rule='native_gate_draw_watch_frame must be true', bits=['native_gate_draw_watch_frame']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_seed_rng\s*=\s*true\b', config_text):
            failures.append(dict(rule='native_gate_draw_watch_frame needs native_gate_seed_rng', bits=['native_gate_seed_rng']))
        if re.search(r'X4LAN_CONFIG\.native_gate_draw_watch\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule='native_gate_draw_watch_frame excludes native_gate_draw_watch', bits=['native_gate_draw_watch']))
    if re.search(r'X4LAN_CONFIG\.native_gate_actor_rng_rows\s*=\s*true\b', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_actor_rng_all\s*=\s*true\b', config_text):
        failures.append(dict(rule='native_gate_actor_rng_rows needs native_gate_actor_rng_all', bits=['native_gate_actor_rng_all']))
    if re.search(r'X4LAN_CONFIG\.native_gate_command_order_rows\s*=\s*true\b', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_command_order\s*=\s*(?!false\b|nil\b)\S', config_text):
        failures.append(dict(rule='native_gate_command_order_rows needs native_gate_command_order', bits=['native_gate_command_order']))
    if re.search(r'X4LAN_CONFIG\.native_gate_nav_player\s*=\s*true\b', config_text):
        if not re.search(r'X4LAN_CONFIG\.native_gate_nav_cadence\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule='native_gate_nav_player needs native_gate_nav_cadence', bits=['native_gate_nav_cadence']))
        if re.search(r'X4LAN_CONFIG\.native_gate_player_observer\s*=\s*true\b', config_text):
            failures.append(dict(rule='native_gate_nav_player excludes native_gate_player_observer (one owner of PlayerController slot 33)',
                                 bits=['native_gate_player_observer']))
    if re.search(r'X4LAN_CONFIG\.native_gate_viewpoint_policy\s*=\s*(?!false\b|nil\b)\S', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_(?:mass_traffic|highway)_camera\s*=\s*(?!false\b|nil\b)\S', config_text):
        failures.append(dict(rule='native_gate_viewpoint_policy needs native_gate_mass_traffic_camera or native_gate_highway_camera',
                             bits=['native_gate_mass_traffic_camera', 'native_gate_highway_camera']))
    if re.search(r'X4LAN_CONFIG\.native_gate_camera_anchor\s*=\s*(?!false\b|nil\b)\S', config_text):
        if not re.search(r'X4LAN_CONFIG\.native_gate_view_points\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule='native_gate_camera_anchor needs native_gate_view_points', bits=['native_gate_view_points']))
        if re.search(r'X4LAN_CONFIG\.native_gate_camera_offset\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule='native_gate_camera_anchor excludes native_gate_camera_offset', bits=['native_gate_camera_offset']))
        if re.search(r'X4LAN_CONFIG\.native_gate_pilot_camera_follow\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule='native_gate_camera_anchor excludes native_gate_pilot_camera_follow', bits=['native_gate_pilot_camera_follow']))
    if re.search(r'X4LAN_CONFIG\.native_gate_view_rows\s*=\s*true\b', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_view_points\s*=\s*(?!false\b|nil\b)\S', config_text):
        failures.append(dict(rule='native_gate_view_rows needs native_gate_view_points', bits=['native_gate_view_points']))
    for option in ('native_gate_region_fields', 'native_gate_mass_traffic_camera', 'native_gate_effect_camera', 'native_gate_effect_rows',
                   'native_gate_highway_camera', 'native_gate_physics_scenes', 'native_gate_camera_context',
                   'native_gate_viewpoint_policy', 'native_gate_shape_refresh', 'native_gate_mining_camera', 'native_gate_xs_camera',
                   'native_gate_camera_probe', 'native_gate_highway_speed', 'native_gate_sound_rng', 'native_gate_view_scenes', 'native_gate_field_fill',
                   'native_gate_traffic_views', 'native_gate_traffic_rows'):
        if re.search(r'X4LAN_CONFIG\.' + option + r'\s*=\s*(?!false\b|nil\b)\S', config_text) and \
           not re.search(r'X4LAN_CONFIG\.native_gate_view_points\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule=f'{option} needs native_gate_view_points', bits=['native_gate_view_points']))
    for option in ('native_gate_region_fields', 'native_gate_effect_camera', 'native_gate_effect_rows', 'native_gate_highway_camera',
                   'native_gate_physics_scenes', 'native_gate_camera_context', 'native_gate_shape_refresh', 'native_gate_mining_camera',
                   'native_gate_xs_camera', 'native_gate_camera_probe', 'native_gate_highway_speed', 'native_gate_sound_rng', 'native_gate_view_scenes',
                   'native_gate_field_fill', 'native_gate_traffic_views', 'native_gate_traffic_rows'):
        if re.search(r'X4LAN_CONFIG\.' + option + r'\s*=\s*(?!false\b|nil\b)\S', config_text) and \
           not re.search(r'X4LAN_CONFIG\.native_gate_(?:mass_traffic|region_pass)\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule=f'{option} needs native_gate_mass_traffic or native_gate_region_pass',
                                 bits=['native_gate_mass_traffic', 'native_gate_region_pass']))
    if re.search(r'X4LAN_CONFIG\.native_gate_mass_traffic_camera\s*=\s*(?!false\b|nil\b)\S', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_mass_traffic\s*=\s*(?:true\b|[\'"]sync[\'"])', config_text):
        failures.append(dict(rule="native_gate_mass_traffic_camera needs native_gate_mass_traffic = 'sync'", bits=['native_gate_mass_traffic']))
    if re.search(r'X4LAN_CONFIG\.native_gate_pass_stride\s*=\s*(?!false\b|nil\b)\S', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_(?:mass_traffic|region_pass)\s*=\s*(?:true\b|[\'"]sync[\'"])', config_text):
        failures.append(dict(rule="native_gate_pass_stride needs native_gate_mass_traffic or native_gate_region_pass = 'sync'",
                             bits=['native_gate_mass_traffic', 'native_gate_region_pass']))
    if re.search(r'X4LAN_CONFIG\.native_gate_region_camera\s*=\s*(?!false\b|nil\b)\S', config_text):
        if not re.search(r'X4LAN_CONFIG\.native_gate_view_points\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule='native_gate_region_camera needs native_gate_view_points', bits=['native_gate_view_points']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_region_pass\s*=\s*(?:true\b|[\'"]sync[\'"])', config_text):
            failures.append(dict(rule="native_gate_region_camera needs native_gate_region_pass = 'sync'", bits=['native_gate_region_pass']))
    if re.search(r'X4LAN_CONFIG\.native_gate_view_points_source\s*=\s*(?!false\b|nil\b)\S', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_view_points\s*=\s*(?!false\b|nil\b)\S', config_text):
        failures.append(dict(rule='native_gate_view_points_source needs native_gate_view_points', bits=['native_gate_view_points']))
    if re.search(r'X4LAN_CONFIG\.native_gate_view_triggers\s*=\s*(?!false\b|nil\b)\S', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_view_points\s*=\s*(?!false\b|nil\b)\S', config_text):
        failures.append(dict(rule='native_gate_view_triggers needs native_gate_view_points', bits=['native_gate_view_points']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_physics_trace\s*=\s*([^\s;]+)', config_text):
        if value == 'nil': continue
        if not re.fullmatch(r'0|[1-9][0-9]{0,6}', value) or int(value) > 1000000:
            failures.append(dict(rule='native_gate_physics_trace must be a tick count 0..1000000', bits=[value]))
        if 'native_gate_world_identities' not in enabled:
            failures.append(dict(rule='native_gate_physics_trace needs native_gate_world_identities', bits=['native_gate_world_identities']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_snapshot_sha\s*=', config_text):
            failures.append(dict(rule='native_gate_physics_trace needs the snapshot boot', bits=['native_gate_snapshot_sha']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_physics_trace_children\s*=\s*([^\s;]+)', config_text):
        if value == 'nil': continue
        if not re.fullmatch(r'[1-9][0-9]{0,3}', value) or int(value) > 3600:
            failures.append(dict(rule='native_gate_physics_trace_children must be a tick count 1..3600', bits=[value]))
        if not re.search(r'X4LAN_CONFIG\.native_gate_physics_trace\s*=\s*(?!nil\b)[0-9]', config_text):
            failures.append(dict(rule='native_gate_physics_trace_children needs native_gate_physics_trace', bits=['native_gate_physics_trace']))
    if re.search(r'X4LAN_CONFIG\.native_gate_view_scenes\s*=\s*(?!false\b|nil\b)\S', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_physics_scenes\s*=\s*(?!false\b|nil\b)\S', config_text):
        failures.append(dict(rule='native_gate_view_scenes needs native_gate_physics_scenes', bits=['native_gate_physics_scenes']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_world_hash_shields\s*=\s*([^\s;]+)', config_text):
        if value in ('nil', 'false'): continue
        if value not in ("'canonical'", '"canonical"'):
            failures.append(dict(rule="native_gate_world_hash_shields must be 'canonical'", bits=[value]))
        if not re.search(r'X4LAN_CONFIG\.native_gate_world_hash_stride\s*=\s*[0-9]', config_text):
            failures.append(dict(rule='native_gate_world_hash_shields needs native_gate_world_hash_stride', bits=['native_gate_world_hash_stride']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_viewpoint_standins\s*=\s*([^\s;]+)', config_text):
        if value in ('nil', 'false'): continue
        if value not in ('2', '3', '4'):
            failures.append(dict(rule='native_gate_viewpoint_standins must be 2, 3 or 4', bits=[value]))
        if not re.search(r"X4LAN_CONFIG\.native_gate_viewpoint_policy\s*=\s*['\"]sector['\"]", config_text):
            failures.append(dict(rule='native_gate_viewpoint_standins needs native_gate_viewpoint_policy', bits=['native_gate_viewpoint_policy']))
    if re.search(r'X4LAN_CONFIG\.native_gate_field_fill\s*=\s*(?!false\b|nil\b)\S', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_region_fields\s*=\s*(?!false\b|nil\b)\S', config_text):
        failures.append(dict(rule='native_gate_field_fill needs native_gate_region_fields', bits=['native_gate_region_fields']))
    if re.search(r'X4LAN_CONFIG\.native_gate_highway_speed\s*=\s*(?!false\b|nil\b)\S', config_text) and 'native_gate_world_identities' not in enabled:
        failures.append(dict(rule='native_gate_highway_speed needs native_gate_world_identities', bits=['native_gate_world_identities']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_runtime_ids\s*=\s*([^\s;]+)', config_text):
        if value in ('nil', 'false'): continue
        number = re.fullmatch(r'[1-9][0-9]{0,5}', value)
        if value != 'true' and value.strip('\'"') != 'observe' and not (number and int(value) <= 524288):
            failures.append(dict(rule="native_gate_runtime_ids must be a slot 1..524288, true or 'observe'", bits=[value]))
        if 'native_gate_world_identities' not in enabled:
            failures.append(dict(rule='native_gate_runtime_ids needs native_gate_world_identities', bits=['native_gate_world_identities']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_snapshot_sha\s*=', config_text):
            failures.append(dict(rule='native_gate_runtime_ids needs the snapshot boot', bits=['native_gate_snapshot_sha']))
    if re.search(r'X4LAN_CONFIG\.native_gate_runtime_ids_barrier\s*=\s*(?!false\b|nil\b)\S', config_text):
        if not re.search(r'X4LAN_CONFIG\.native_gate_runtime_ids_barrier\s*=\s*true\b', config_text):
            failures.append(dict(rule='native_gate_runtime_ids_barrier must be true', bits=['native_gate_runtime_ids_barrier']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_runtime_ids\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule='native_gate_runtime_ids_barrier needs native_gate_runtime_ids', bits=['native_gate_runtime_ids']))
    if re.search(r'X4LAN_CONFIG\.native_gate_runtime_ids_local\s*=\s*(?!false\b|nil\b)\S', config_text):
        if not re.search(r'X4LAN_CONFIG\.native_gate_runtime_ids_local\s*=\s*true\b', config_text):
            failures.append(dict(rule='native_gate_runtime_ids_local must be true', bits=['native_gate_runtime_ids_local']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_runtime_ids\s*=\s*(?!false\b|nil\b|[\'"]observe[\'"])\S', config_text):
            failures.append(dict(rule='native_gate_runtime_ids_local needs native_gate_runtime_ids set to a slot', bits=['native_gate_runtime_ids']))
        if 'native_gate_created_identities' not in enabled:
            failures.append(dict(rule='native_gate_runtime_ids_local needs native_gate_created_identities', bits=['native_gate_created_identities']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_highway_rows\s*=\s*([^\s;]+)', config_text):
        if value == 'nil': continue
        if not re.fullmatch(r'[1-9][0-9]{0,3}', value) or int(value) > 3600:
            failures.append(dict(rule='native_gate_highway_rows must be a tick count 1..3600', bits=[value]))
        if not re.search(r'X4LAN_CONFIG\.native_gate_view_points\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule='native_gate_highway_rows needs native_gate_view_points', bits=['native_gate_view_points']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_(?:mass_traffic|region_pass)\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule='native_gate_highway_rows needs native_gate_mass_traffic or native_gate_region_pass',
                                 bits=['native_gate_mass_traffic', 'native_gate_region_pass']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_highway_rows_from\s*=\s*([^\s;]+)', config_text):
        if value == 'nil': continue
        if not re.fullmatch(r'[1-9][0-9]{0,6}', value) or int(value) > 1000000:
            failures.append(dict(rule='native_gate_highway_rows_from must be a tick 1..1000000', bits=[value]))
        if not re.search(r'X4LAN_CONFIG\.native_gate_highway_rows\s*=\s*(?!nil\b)[0-9]', config_text):
            failures.append(dict(rule='native_gate_highway_rows_from needs native_gate_highway_rows', bits=['native_gate_highway_rows']))
    if re.search(r'X4LAN_CONFIG\.native_gate_physics_trace_saved\s*=\s*(?!false\b|nil\b)\S', config_text):
        if not re.search(r'X4LAN_CONFIG\.native_gate_physics_trace_saved\s*=\s*true\b', config_text):
            failures.append(dict(rule='native_gate_physics_trace_saved must be true', bits=['native_gate_physics_trace_saved']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_physics_trace\s*=\s*[1-9]', config_text):
            failures.append(dict(rule='native_gate_physics_trace_saved needs native_gate_physics_trace >= 1', bits=['native_gate_physics_trace']))
    # Diagnostic rows only: script RNG rows of a tick window.
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_script_rng_rows_window\s*=\s*([^\n;]+)', config_text):
        value = value.strip()
        if value == 'nil': continue
        match = re.fullmatch(r'\{\s*([1-9][0-9]*)\s*,\s*([1-9][0-9]*)\s*\}', value)
        if not match or int(match.group(2)) < int(match.group(1)) or int(match.group(2)) - int(match.group(1)) > 600:
            failures.append(dict(rule='native_gate_script_rng_rows_window must be {first, last} ticks at most 600 apart', bits=[value]))
        elif not re.search(r'X4LAN_CONFIG\.native_gate_script_rng_rows\s*=\s*true\b', config_text):
            failures.append(dict(rule='native_gate_script_rng_rows_window needs native_gate_script_rng_rows = true', bits=['native_gate_script_rng_rows']))
    # Diagnostic rows only: who allocates runtime IDs, every N ticks.
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_runtime_ids_caller_stride\s*=\s*([^\s;]+)', config_text):
        if value == 'nil': continue
        if not re.fullmatch(r'[1-9][0-9]{0,3}', value) or int(value) > 3600:
            failures.append(dict(rule='native_gate_runtime_ids_caller_stride must be a tick count 1..3600', bits=[value]))
        elif not re.search(r'X4LAN_CONFIG\.native_gate_runtime_ids\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule='native_gate_runtime_ids_caller_stride needs native_gate_runtime_ids', bits=['native_gate_runtime_ids']))
    # Log thinning only (held ticks are written before a stop or failure); no prerequisite.
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_status_stride\s*=\s*([^\s;]+)', config_text):
        if value == 'nil': continue
        if not re.fullmatch(r'[1-9][0-9]{0,3}', value) or int(value) > 3600:
            failures.append(dict(rule='native_gate_status_stride must be a tick count 1..3600', bits=[value]))
    # Diagnostic timing only (one PROBE_PROFILE line per 60 ticks); no prerequisite.
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_probe_profile\s*=\s*([^\s;]+)', config_text):
        if value not in ('true', 'false', 'nil'):
            failures.append(dict(rule='native_gate_probe_profile accepts only true', bits=[value]))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_gravidar_digest_rows\s*=\s*([^\s;]+)', config_text):
        if value == 'nil': continue
        if not re.fullmatch(r'[1-9][0-9]{0,6}', value) or int(value) > 1000000:
            failures.append(dict(rule='native_gate_gravidar_digest_rows must be a tick count 1..1000000', bits=[value]))
        if 'native_gate_gravidar_digest' not in enabled:
            failures.append(dict(rule='native_gate_gravidar_digest_rows needs native_gate_gravidar_digest', bits=['native_gate_gravidar_digest']))
    return failures


def check(config_text):
    bits = option_bits()
    enabled = set(re.findall(r'X4LAN_CONFIG\.(native_gate_\w+)\s*=\s*true\b', config_text))
    options = sum(value for name, value in bits if name in enabled)
    names = {value: name for name, value in bits}
    failures = []
    for line, expr in rules():
        if eval(expr, {'o': options}):
            mentioned = [names.get(int(v), v) for v in re.findall(r'&(\d+)', line.replace('ULL', ''))]
            failures.append(dict(rule=line, bits=mentioned))
    failures.extend(probe_rules(config_text, enabled))
    return options, failures


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('config', type=Path, help='config.lua or config.lua.append')
    args = p.parse_args()
    options, failures = check(args.config.read_text())
    print(f'options={options}')
    for f in failures: print('REFUSED by rule:', f['rule'], '->', ', '.join(map(str, f['bits'])))
    sys.exit(1 if failures else 0)


if __name__ == '__main__': main()
