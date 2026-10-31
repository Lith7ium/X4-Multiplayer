"""Offline overlay checks: boot bits and options the probe arms after the boot."""
from pathlib import Path
import importlib.util
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('check_gate_options', ROOT/'tools/dev/check_gate_options.py')
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)

BASE = 'X4LAN_CONFIG.native_gate_snapshot_sha = "%s"\n' % ('a'*64)


class BulletCadenceOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'bullet_cadence' in f['rule']]

    def test_needs_the_bullet_module_and_the_snapshot_boot(self):
        for module_line in ('X4LAN_CONFIG.native_gate_bullet_rng = true\n',
                            'X4LAN_CONFIG.native_gate_weapon_diag = true\nX4LAN_CONFIG.native_gate_bullet_diag = true\n'):
            for value in ('true', "'every'", '"observe"'):
                with self.subTest(module=module_line, value=value):
                    self.assertEqual(self.failures(BASE+module_line+'X4LAN_CONFIG.native_gate_bullet_cadence = %s\n' % value), [])
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_bullet_cadence = true\n')), 1)
        self.assertEqual(len(self.failures('X4LAN_CONFIG.native_gate_bullet_rng = true\nX4LAN_CONFIG.native_gate_bullet_cadence = true\n')), 1)

    def test_rejects_unknown_values_and_ignores_off(self):
        rng = BASE+'X4LAN_CONFIG.native_gate_bullet_rng = true\n'
        for value in ("'EVERY'", '1', "'split'"):
            with self.subTest(value=value):
                self.assertEqual(len(self.failures(rng+'X4LAN_CONFIG.native_gate_bullet_cadence = %s\n' % value)), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_bullet_cadence = false\n'), [])
        self.assertEqual(self.failures(BASE), [])



class NavCadenceOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'nav_cadence' in f['rule']]

    def test_needs_the_nav_module_and_the_snapshot_boot(self):
        for module_line in ('X4LAN_CONFIG.native_gate_nav_rng = true\n', 'X4LAN_CONFIG.native_gate_nav_diag = true\n',
                            'X4LAN_CONFIG.native_gate_nav_initial_refresh = true\n'):
            for value in ('true', "'far'", "'every'", '"observe"'):
                with self.subTest(module=module_line, value=value):
                    self.assertEqual(self.failures(BASE+module_line+'X4LAN_CONFIG.native_gate_nav_cadence = %s\n' % value), [])
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_nav_cadence = true\n')), 1)
        self.assertEqual(len(self.failures('X4LAN_CONFIG.native_gate_nav_rng = true\nX4LAN_CONFIG.native_gate_nav_cadence = true\n')), 1)

    def test_rejects_unknown_values_and_ignores_off(self):
        nav = BASE+'X4LAN_CONFIG.native_gate_nav_rng = true\n'
        for value in ("'fixed'", "'FAR'", '1', "'near'"):
            with self.subTest(value=value):
                self.assertEqual(len(self.failures(nav+'X4LAN_CONFIG.native_gate_nav_cadence = %s\n' % value)), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_nav_cadence = false\n'), [])



class ViewPointsOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'view_points' in f['rule']]

    def test_values_and_the_snapshot_boot(self):
        for value in ('true', "'players'", '"observe"'):
            with self.subTest(value=value):
                self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_view_points = %s\n' % value), [])
        for value in ("'camera'", "'PLAYERS'", '2'):
            with self.subTest(value=value):
                self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_view_points = %s\n' % value)), 1)
        self.assertEqual(len(self.failures('X4LAN_CONFIG.native_gate_view_points = true\n')), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_view_points = false\n'), [])


class NavProbeInitOptionTests(unittest.TestCase):
    NAV = 'X4LAN_CONFIG.native_gate_nav_rng = true\nX4LAN_CONFIG.native_gate_nav_cadence = \'far\'\n'

    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'nav_probe_init' in f['rule']]

    def test_needs_the_nav_module_cadence_and_snapshot(self):
        self.assertEqual(self.failures(BASE+self.NAV+'X4LAN_CONFIG.native_gate_nav_probe_init = true\n'), [])
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_nav_cadence = \'far\'\nX4LAN_CONFIG.native_gate_nav_probe_init = true\n')), 1)
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_nav_rng = true\nX4LAN_CONFIG.native_gate_nav_probe_init = true\n')), 1)
        self.assertEqual(len(self.failures(self.NAV+'X4LAN_CONFIG.native_gate_nav_probe_init = true\n')), 1)
        self.assertEqual(len(self.failures(BASE+self.NAV+'X4LAN_CONFIG.native_gate_nav_probe_init = \'yes\'\n')), 1)
        self.assertEqual(self.failures(BASE+self.NAV+'X4LAN_CONFIG.native_gate_nav_probe_init = false\n'), [])


class NavParamsInitOptionTests(unittest.TestCase):
    NAV = NavProbeInitOptionTests.NAV

    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'nav_params_init' in f['rule']]

    def test_needs_the_nav_module_cadence_and_snapshot(self):
        self.assertEqual(self.failures(BASE+self.NAV+'X4LAN_CONFIG.native_gate_nav_params_init = true\n'), [])
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_nav_cadence = \'far\'\nX4LAN_CONFIG.native_gate_nav_params_init = true\n')), 1)
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_nav_rng = true\nX4LAN_CONFIG.native_gate_nav_params_init = true\n')), 1)
        self.assertEqual(len(self.failures(self.NAV+'X4LAN_CONFIG.native_gate_nav_params_init = true\n')), 1)
        self.assertEqual(len(self.failures(BASE+self.NAV+'X4LAN_CONFIG.native_gate_nav_params_init = 1\n')), 1)
        self.assertEqual(self.failures(BASE+self.NAV+'X4LAN_CONFIG.native_gate_nav_params_init = false\n'), [])


class CommandOrderOptionTests(unittest.TestCase):
    SERIAL = 'X4LAN_CONFIG.native_gate_serial_pathing = true\n'
    TSNAP = 'X4LAN_CONFIG.native_gate_travel_snapshot = true\n'

    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'command_order' in f['rule']]

    def test_needs_serial_pathing_the_snapshot_and_for_sort_the_late_splice_join(self):
        self.assertEqual(self.failures(BASE+self.SERIAL+"X4LAN_CONFIG.native_gate_command_order = 'observe'\n"), [])
        self.assertEqual(self.failures(BASE+self.SERIAL+self.TSNAP+"X4LAN_CONFIG.native_gate_command_order = 'sort'\n"), [])
        self.assertEqual(len(self.failures(BASE+self.SERIAL+"X4LAN_CONFIG.native_gate_command_order = 'sort'\n")), 1)
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_command_order = 'observe'\n")), 1)
        self.assertEqual(len(self.failures(self.SERIAL+"X4LAN_CONFIG.native_gate_command_order = 'observe'\n")), 1)
        self.assertEqual(len(self.failures(BASE+self.SERIAL+"X4LAN_CONFIG.native_gate_command_order = 'order'\n")), 1)
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_command_order = false\n"), [])
        rows = 'X4LAN_CONFIG.native_gate_command_order_rows = true\n'
        self.assertEqual(self.failures(BASE+self.SERIAL+"X4LAN_CONFIG.native_gate_command_order = 'observe'\n"+rows), [])
        self.assertEqual(len(self.failures(BASE+self.SERIAL+rows)), 1)


class ShipSoundRngOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'ship_sound_rng' in f['rule']]

    def test_values_and_the_snapshot_boot(self):
        for value in ('true', "'split'", '"observe"'):
            with self.subTest(value=value):
                self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_ship_sound_rng = %s\n' % value), [])
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_ship_sound_rng = 'drop'\n")), 1)
        self.assertEqual(len(self.failures("X4LAN_CONFIG.native_gate_ship_sound_rng = 'split'\n")), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_ship_sound_rng = false\n'), [])


class PlayerTimerOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'player_timer' in f['rule']]

    def test_true_only_and_the_snapshot_boot(self):
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_player_timer = true\n'), [])
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_player_timer = 'rebase'\n")), 1)
        self.assertEqual(len(self.failures('X4LAN_CONFIG.native_gate_player_timer = true\n')), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_player_timer = false\n'), [])


class DrawWatchOptionTests(unittest.TestCase):
    ALL = 'X4LAN_CONFIG.native_gate_actor_rng_all = true\n'

    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'draw_watch' in f['rule']]

    def test_a_list_of_saved_ids_and_the_all_ship_rng(self):
        self.assertEqual(self.failures(BASE+self.ALL+"X4LAN_CONFIG.native_gate_draw_watch = {'00000000000ae79c'}\n"), [])
        self.assertEqual(self.failures(BASE+self.ALL+"X4LAN_CONFIG.native_gate_draw_watch = {'00000000000ae79c','0000000000092d05'}\n"), [])
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_draw_watch = {'00000000000ae79c'}\n")), 1)
        self.assertEqual(len(self.failures(BASE+self.ALL+"X4LAN_CONFIG.native_gate_draw_watch = '00000000000ae79c'\n")), 1)
        self.assertEqual(len(self.failures(BASE+self.ALL+"X4LAN_CONFIG.native_gate_draw_watch = {'ae79c'}\n")), 1)
        self.assertEqual(len(self.failures(BASE+self.ALL+"X4LAN_CONFIG.native_gate_draw_watch = {" + ','.join(["'%016x'" % i for i in range(1, 6)]) + "}\n")), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_draw_watch = nil\n'), [])

    def test_frame_watch_needs_the_seed_and_excludes_the_list(self):
        seed = "X4LAN_CONFIG.native_gate_seed_rng = true\n"
        frame = "X4LAN_CONFIG.native_gate_draw_watch_frame = true\n"
        self.assertEqual(self.failures(BASE+seed+frame), [])
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_draw_watch_frame = false\n"), [])
        self.assertEqual(len(self.failures(BASE+frame)), 1)
        self.assertEqual(len(self.failures(BASE+seed+"X4LAN_CONFIG.native_gate_draw_watch_frame = 1\n")), 1)
        self.assertEqual(len(self.failures(BASE+seed+self.ALL+frame+"X4LAN_CONFIG.native_gate_draw_watch = {'00000000000ae79c'}\n")), 1)


class ActorRngRowsOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'actor_rng_rows' in f['rule']]

    def test_needs_the_all_ship_actor_rng(self):
        rows = 'X4LAN_CONFIG.native_gate_actor_rng_rows = true\n'
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_actor_rng_all = true\n'+rows), [])
        self.assertEqual(len(self.failures(BASE+rows)), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_actor_rng_rows = false\n'), [])


class NavPlayerOptionTests(unittest.TestCase):
    NAV = NavProbeInitOptionTests.NAV

    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'nav_player' in f['rule']]

    def test_needs_the_nav_module_cadence_snapshot_and_no_player_observer(self):
        ok = BASE+self.NAV+'X4LAN_CONFIG.native_gate_nav_player = true\n'
        self.assertEqual(self.failures(ok), [])
        self.assertEqual(self.failures(ok+'X4LAN_CONFIG.native_gate_nav_probe_init = true\n'), [])
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_nav_cadence = \'far\'\nX4LAN_CONFIG.native_gate_nav_player = true\n')), 1)
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_nav_rng = true\nX4LAN_CONFIG.native_gate_nav_player = true\n')), 1)
        self.assertEqual(len(self.failures(self.NAV+'X4LAN_CONFIG.native_gate_nav_player = true\n')), 1)
        self.assertEqual(len(self.failures(BASE+self.NAV+'X4LAN_CONFIG.native_gate_nav_player = \'on\'\n')), 1)
        self.assertEqual(len(self.failures(ok+'X4LAN_CONFIG.native_gate_player_observer = true\n')), 1)
        self.assertEqual(self.failures(BASE+self.NAV+'X4LAN_CONFIG.native_gate_nav_player = false\n'), [])


class AttentionAllOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'attention_all' in f['rule']]

    def test_needs_the_attention_hook_and_snapshot(self):
        for hook in ('native_gate_attention_rng', 'native_gate_attention_initial_refresh'):
            with self.subTest(hook=hook):
                self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.%s = true\nX4LAN_CONFIG.native_gate_attention_all = true\n' % hook), [])
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_attention_all = true\n')), 1)
        self.assertEqual(len(self.failures('X4LAN_CONFIG.native_gate_attention_rng = true\nX4LAN_CONFIG.native_gate_attention_all = true\n')), 1)
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_attention_rng = true\nX4LAN_CONFIG.native_gate_attention_all = 1\n')), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_attention_all = false\n'), [])


class ViewPointsSourceOptionTests(unittest.TestCase):
    VIEW = "X4LAN_CONFIG.native_gate_view_points = 'players'\n"

    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'view_points_source' in f['rule']]

    def test_values_view_points_and_snapshot(self):
        for value in ("'ship'", '"player"'):
            with self.subTest(value=value):
                self.assertEqual(self.failures(BASE+self.VIEW+'X4LAN_CONFIG.native_gate_view_points_source = %s\n' % value), [])
        self.assertEqual(len(self.failures(BASE+self.VIEW+"X4LAN_CONFIG.native_gate_view_points_source = 'camera'\n")), 1)
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_view_points_source = 'ship'\n")), 1)
        self.assertEqual(len(self.failures(self.VIEW+"X4LAN_CONFIG.native_gate_view_points_source = 'ship'\n")), 1)
        self.assertEqual(self.failures(BASE+self.VIEW+'X4LAN_CONFIG.native_gate_view_points_source = false\n'), [])


class RegionCameraOptionTests(unittest.TestCase):
    NEEDS = "X4LAN_CONFIG.native_gate_view_points = 'players'\nX4LAN_CONFIG.native_gate_region_pass = 'sync'\n"

    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'region_camera' in f['rule']]

    def test_needs_view_points_the_synced_region_pass_and_snapshot(self):
        self.assertEqual(self.failures(BASE+self.NEEDS+"X4LAN_CONFIG.native_gate_region_camera = 'view'\n"), [])
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_view_points = true\nX4LAN_CONFIG.native_gate_region_pass = true\n"
                                       "X4LAN_CONFIG.native_gate_region_camera = 'view'\n"), [])
        self.assertEqual(len(self.failures(BASE+self.NEEDS+"X4LAN_CONFIG.native_gate_region_camera = 'camera'\n")), 1)
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_region_pass = 'sync'\nX4LAN_CONFIG.native_gate_region_camera = 'view'\n")), 1)
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_view_points = 'players'\nX4LAN_CONFIG.native_gate_region_pass = 'observe'\n"
                                           "X4LAN_CONFIG.native_gate_region_camera = 'view'\n")), 1)
        self.assertEqual(len(self.failures(self.NEEDS+"X4LAN_CONFIG.native_gate_region_camera = 'view'\n")), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_region_camera = false\n'), [])


class EngineRefreshRngOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'engine_refresh_rng' in f['rule']]

    def test_needs_the_world_identities_and_snapshot(self):
        ids = 'X4LAN_CONFIG.native_gate_world_identities = true\n'
        self.assertEqual(self.failures(BASE+ids+'X4LAN_CONFIG.native_gate_engine_refresh_rng = true\n'), [])
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_engine_refresh_rng = true\n')), 1)
        self.assertEqual(len(self.failures(BASE+ids+"X4LAN_CONFIG.native_gate_engine_refresh_rng = 'key'\n")), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_engine_refresh_rng = false\n'), [])


class PortableMathOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'portable_math' in f['rule']]

    def test_true_only_and_needs_the_snapshot_boot(self):
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_portable_math = true\n'), [])
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_portable_math = false\n'), [])
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_portable_math = 'fma3'\n")), 1)
        self.assertEqual(len(self.failures('X4LAN_CONFIG.native_gate_portable_math = true\n')), 1)


class ExactReciprocalOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'exact_reciprocal' in f['rule']]

    def test_true_only_and_needs_the_snapshot_boot(self):
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_exact_reciprocal = true\n'), [])
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_exact_reciprocal = false\n'), [])
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_exact_reciprocal = 'sqrt'\n")), 1)
        self.assertEqual(len(self.failures('X4LAN_CONFIG.native_gate_exact_reciprocal = true\n')), 1)


class FpEnvOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'fp_env' in f['rule']]

    def test_default_or_observe_and_needs_the_snapshot_boot(self):
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_fp_env = 'default'\n"), [])
        # (next to it: the cost cuts' experiment values and what each needs)
        cut = lambda text: [f['rule'] for f in module.check(text)[1] if 'path_budget' in f['rule'] or 'stride' in f['rule'] or 'fast_reads' in f['rule']]
        self.assertEqual(cut(BASE+'X4LAN_CONFIG.native_gate_fast_reads = true\n'), [])
        self.assertEqual(len(cut(BASE+'X4LAN_CONFIG.native_gate_fast_reads = 1\n')), 1)
        self.assertEqual(len(cut(BASE+'X4LAN_CONFIG.native_gate_path_budget = 100\n')), 1)
        self.assertEqual(cut(BASE+'X4LAN_CONFIG.native_gate_serial_pathing = true\nX4LAN_CONFIG.native_gate_path_budget = 100\n'), [])
        self.assertEqual(len(cut(BASE+'X4LAN_CONFIG.native_gate_serial_pathing = true\nX4LAN_CONFIG.native_gate_path_budget = 7\n')), 1)
        self.assertEqual(len(cut(BASE+'X4LAN_CONFIG.native_gate_pass_stride = 2\n')), 1)
        self.assertEqual(cut(BASE+"X4LAN_CONFIG.native_gate_mass_traffic = 'sync'\nX4LAN_CONFIG.native_gate_pass_stride = 2\n"), [])
        self.assertEqual(cut(BASE+"X4LAN_CONFIG.native_gate_region_pass = true\nX4LAN_CONFIG.native_gate_pass_stride = 2\n"), [])
        # (next to it: the tick rate takes the rates the gate and the relay know)
        rate = lambda text: [f['rule'] for f in module.check(text)[1] if 'tick_' in f['rule']]
        for hz in (60, 40, 30, 20):
            self.assertEqual(rate(BASE+f'X4LAN_CONFIG.native_gate_tick_hz = {hz}\n'), [])
        self.assertEqual(len(rate(BASE+'X4LAN_CONFIG.native_gate_tick_hz = 25\n')), 1)
        self.assertEqual(len(rate('X4LAN_CONFIG.native_gate_tick_hz = 30\n')), 1)
        self.assertEqual(rate(BASE+'X4LAN_CONFIG.native_gate_tick_hz = 30\nX4LAN_CONFIG.native_gate_tick_pace = false\n'), [])
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_fp_env = 'observe'\n"), [])
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_fp_env = false\n'), [])
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_fp_env = 'ftz'\n")), 1)
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_fp_env = true\n')), 1)
        self.assertEqual(len(self.failures("X4LAN_CONFIG.native_gate_fp_env = 'default'\n")), 1)


class DecimalOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'native_gate_decimal' in f['rule']]

    def test_true_only_and_needs_the_snapshot_boot(self):
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_decimal = true\n'), [])
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_decimal = false\n'), [])
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_decimal = 'linux'\n")), 1)
        self.assertEqual(len(self.failures('X4LAN_CONFIG.native_gate_decimal = true\n')), 1)


class EventRngOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'event_rng' in f['rule']]

    def test_needs_the_event_order_world_identities_and_snapshot(self):
        needs = 'X4LAN_CONFIG.native_gate_world_identities = true\nX4LAN_CONFIG.native_gate_event_order = true\n'
        for mode in ("'key'", "'observe'"):
            self.assertEqual(self.failures(BASE+needs+f'X4LAN_CONFIG.native_gate_event_rng = {mode}\n'), [])
            self.assertEqual(len(self.failures(needs+f'X4LAN_CONFIG.native_gate_event_rng = {mode}\n')), 1)
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_world_identities = true\nX4LAN_CONFIG.native_gate_event_rng = \'key\'\n')), 1)
        self.assertEqual(len(self.failures(BASE+needs+'X4LAN_CONFIG.native_gate_event_rng = true\n')), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_event_rng = false\n'), [])


class BuildRngAndUiCargoOptionTests(unittest.TestCase):
    def failures(self, text, name):
        return [f['rule'] for f in module.check(text)[1] if name in f['rule']]

    def test_need_the_world_identities_and_snapshot(self):
        ids = 'X4LAN_CONFIG.native_gate_world_identities = true\n'
        self.assertEqual(self.failures(BASE+ids+'X4LAN_CONFIG.native_gate_build_rng = true\n', 'build_rng'), [])
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_build_rng = true\n', 'build_rng')), 1)
        self.assertEqual(len(self.failures(BASE+ids+'X4LAN_CONFIG.native_gate_build_rng = \'key\'\n', 'build_rng')), 1)
        for mode in ("'block'", "'observe'"):
            self.assertEqual(self.failures(BASE+ids+f'X4LAN_CONFIG.native_gate_ui_cargo = {mode}\n', 'ui_cargo'), [])
            self.assertEqual(len(self.failures(ids+f'X4LAN_CONFIG.native_gate_ui_cargo = {mode}\n', 'ui_cargo')), 1)
        self.assertEqual(len(self.failures(BASE+ids+'X4LAN_CONFIG.native_gate_ui_cargo = true\n', 'ui_cargo')), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_ui_cargo = false\n', 'ui_cargo'), [])


class SupplyRowsOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'supply_rows' in f['rule']]

    def test_needs_the_world_identities_and_snapshot(self):
        ids = 'X4LAN_CONFIG.native_gate_world_identities = true\n'
        self.assertEqual(self.failures(BASE+ids+'X4LAN_CONFIG.native_gate_supply_rows = true\n'), [])
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_supply_rows = true\n')), 1)
        self.assertEqual(len(self.failures(ids+'X4LAN_CONFIG.native_gate_supply_rows = true\n')), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_supply_rows = false\n'), [])


class UiLazyOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'ui_lazy' in f['rule']]

    def test_needs_the_world_identities_and_snapshot(self):
        ids = 'X4LAN_CONFIG.native_gate_world_identities = true\n'
        for mode in ("'block'", "'observe'"):
            self.assertEqual(self.failures(BASE+ids+f'X4LAN_CONFIG.native_gate_ui_lazy = {mode}\n'), [])
            self.assertEqual(len(self.failures(ids+f'X4LAN_CONFIG.native_gate_ui_lazy = {mode}\n')), 1)
        self.assertEqual(len(self.failures(BASE+ids+'X4LAN_CONFIG.native_gate_ui_lazy = true\n')), 1)
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_ui_lazy = 'block'\n")), 1)


class LoadoutOrderOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'loadout_order' in f['rule']]

    def test_modes_and_the_snapshot(self):
        for mode in ("'observe'", "'canonical'"):
            self.assertEqual(self.failures(BASE+f'X4LAN_CONFIG.native_gate_loadout_order = {mode}\n'), [])
            self.assertEqual(len(self.failures(f'X4LAN_CONFIG.native_gate_loadout_order = {mode}\n')), 1)
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_loadout_order = 'sort'\n")), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_loadout_order = false\n'), [])


class ScriptRngWindowOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'rows_window' in f['rule']]

    def test_a_window_with_the_rows(self):
        rows = 'X4LAN_CONFIG.native_gate_script_rng_rows = true\n'
        self.assertEqual(self.failures(BASE+rows+'X4LAN_CONFIG.native_gate_script_rng_rows_window = {15940,16160}\n'), [])
        for bad in ('{16160,15940}', '{1,700}', '5'):
            self.assertEqual(len(self.failures(BASE+rows+f'X4LAN_CONFIG.native_gate_script_rng_rows_window = {bad}\n')), 1, bad)
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_script_rng_rows_window = {1,2}\n')), 1)


class EconomyLoadOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'economy_load' in f['rule']]

    def test_true_with_the_world_identities(self):
        ids = 'X4LAN_CONFIG.native_gate_world_identities = true\n'
        self.assertEqual(self.failures(BASE+ids+'X4LAN_CONFIG.native_gate_economy_load_rng = true\n'), [])
        self.assertEqual(len(self.failures(BASE+ids+"X4LAN_CONFIG.native_gate_economy_load_rng = 'key'\n")), 1)
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_economy_load_rng = true\n')), 1)


class BuildRowsOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'build_rows' in f['rule']]

    def test_true_with_the_world_identities(self):
        ids = 'X4LAN_CONFIG.native_gate_world_identities = true\n'
        self.assertEqual(self.failures(BASE+ids+'X4LAN_CONFIG.native_gate_build_rows = true\n'), [])
        self.assertEqual(len(self.failures(BASE+ids+"X4LAN_CONFIG.native_gate_build_rows = 1\n")), 1)
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_build_rows = true\n')), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_build_rows = false\n'), [])


class RuntimeCallerStrideOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'caller_stride' in f['rule']]

    def test_a_tick_count_with_the_runtime_ids(self):
        ids = 'X4LAN_CONFIG.native_gate_runtime_ids = true\n'
        self.assertEqual(self.failures(BASE+ids+'X4LAN_CONFIG.native_gate_runtime_ids_caller_stride = 1\n'), [])
        self.assertEqual(self.failures(BASE+ids+'X4LAN_CONFIG.native_gate_runtime_ids_caller_stride = nil\n'), [])
        for bad in ('0', '3601', "'x'"):
            self.assertEqual(len(self.failures(BASE+ids+f'X4LAN_CONFIG.native_gate_runtime_ids_caller_stride = {bad}\n')), 1, bad)
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_runtime_ids_caller_stride = 1\n')), 1)


class PlayModeOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'play_mode' in f['rule']]

    def test_true_and_the_snapshot(self):
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_play_mode = true\n'), [])
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_play_mode = 'on'\n")), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_play_mode = false\n'), [])


class MacroArenaOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'macro_arena' in f['rule']]

    def test_true_and_the_snapshot(self):
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_macro_arena = true\n'), [])
        self.assertEqual(len(self.failures('X4LAN_CONFIG.native_gate_macro_arena = true\n')), 1)
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_macro_arena = 'sort'\n")), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_macro_arena = false\n'), [])


class PickOrderOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'pick_order' in f['rule']]

    def test_modes_and_the_snapshot(self):
        for mode in ("'observe'", "'canonical'"):
            self.assertEqual(self.failures(BASE+f'X4LAN_CONFIG.native_gate_pick_order = {mode}\n'), [])
            self.assertEqual(len(self.failures(f'X4LAN_CONFIG.native_gate_pick_order = {mode}\n')), 1)
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_pick_order = 'sort'\n")), 1)


class OrderCanonOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'order_canon' in f['rule']]

    def test_modes_and_the_snapshot(self):
        for mode in ("'observe'", "'canonical'"):
            self.assertEqual(self.failures(BASE+f'X4LAN_CONFIG.native_gate_order_canon = {mode}\n'), [])
            self.assertEqual(len(self.failures(f'X4LAN_CONFIG.native_gate_order_canon = {mode}\n')), 1)
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_order_canon = 'sort'\n")), 1)


class WarpRngOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'warp_rng' in f['rule']]

    def test_values_need_the_world_identities_and_snapshot(self):
        ids = 'X4LAN_CONFIG.native_gate_world_identities = true\n'
        for value in ('true', "'split'", '"observe"'):
            with self.subTest(value=value):
                self.assertEqual(self.failures(BASE+ids+'X4LAN_CONFIG.native_gate_warp_rng = %s\n' % value), [])
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_warp_rng = true\n')), 1)
        self.assertEqual(len(self.failures(BASE+ids+"X4LAN_CONFIG.native_gate_warp_rng = 'key'\n")), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_warp_rng = false\n'), [])


class TransitionRowsOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'transition_rows' in f['rule']]

    def test_true_needs_the_world_identities_and_snapshot(self):
        ids = 'X4LAN_CONFIG.native_gate_world_identities = true\n'
        self.assertEqual(self.failures(BASE+ids+'X4LAN_CONFIG.native_gate_transition_rows = true\n'), [])
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_transition_rows = true\n')), 1)
        self.assertEqual(len(self.failures(BASE+ids+"X4LAN_CONFIG.native_gate_transition_rows = 'all'\n")), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_transition_rows = false\n'), [])


class NavOctreeSitesOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'nav_octree_sites' in f['rule']]

    def test_true_needs_the_deadline_or_agreed_octrees(self):
        for mode in ("'deadline'", '"agreed"'):
            with self.subTest(mode=mode):
                self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_nav_octree = %s\n' % mode+
                                               'X4LAN_CONFIG.native_gate_nav_octree_sites = true\n'), [])
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_nav_octree = 'sync'\nX4LAN_CONFIG.native_gate_nav_octree_sites = true\n")), 1)
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_nav_octree_sites = true\n')), 1)
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_nav_octree = 'agreed'\nX4LAN_CONFIG.native_gate_nav_octree_sites = 'all'\n")), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_nav_octree_sites = false\n'), [])


class GravidarPostsOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'gravidar_posts' in f['rule']]

    def test_true_needs_the_keyed_unit_apply(self):
        keyed = 'X4LAN_CONFIG.native_gate_gravidar_key_all = true\n'
        self.assertEqual(self.failures(BASE+keyed+'X4LAN_CONFIG.native_gate_gravidar_posts = true\n'), [])
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_gravidar_posts = true\n')), 1)
        self.assertEqual(len(self.failures(BASE+keyed+"X4LAN_CONFIG.native_gate_gravidar_posts = 'all'\n")), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_gravidar_posts = false\n'), [])


class GravidarWideOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'gravidar_wide' in f['rule']]

    def test_wide_needs_the_snapshot_boot_and_sort_the_gated_publish_and_identities(self):
        for value in ('true', "'wide'"):
            self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_gravidar_wide = %s\n' % value), [])
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_gravidar_wide = false\n'), [])
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_gravidar_wide = 'observe'\n")), 1)
        self.assertEqual(len(self.failures('X4LAN_CONFIG.native_gate_gravidar_wide = true\n')), 1)
        needs = 'X4LAN_CONFIG.native_gate_radar_serial = true\nX4LAN_CONFIG.native_gate_world_identities = true\n'
        self.assertEqual(self.failures(BASE+needs+"X4LAN_CONFIG.native_gate_gravidar_wide = 'sort'\n"), [])
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_gravidar_wide = 'sort'\n")), 1)
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_radar_serial = true\n'+"X4LAN_CONFIG.native_gate_gravidar_wide = 'sort'\n")), 1)


class TempzoneOrderOptionTests(unittest.TestCase):
    NEEDS = ('X4LAN_CONFIG.native_gate_world_identities = true\n'
             'X4LAN_CONFIG.native_gate_serial_pathing = true\n')
    TSNAP = 'X4LAN_CONFIG.native_gate_travel_snapshot = true\n'

    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'tempzone_order' in f['rule']]

    def test_needs_identities_serial_pathing_snapshot_and_for_order_the_late_splice_join(self):
        for value in ('true', "'order'"):
            with self.subTest(value=value):
                self.assertEqual(self.failures(BASE+self.NEEDS+self.TSNAP+'X4LAN_CONFIG.native_gate_tempzone_order = %s\n' % value), [])
                self.assertEqual(len(self.failures(BASE+self.NEEDS+'X4LAN_CONFIG.native_gate_tempzone_order = %s\n' % value)), 1)
        self.assertEqual(self.failures(BASE+self.NEEDS+"X4LAN_CONFIG.native_gate_tempzone_order = 'observe'\n"), [])
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_tempzone_order = 'observe'\n")), 1)
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_world_identities = true\n'+
                                           "X4LAN_CONFIG.native_gate_tempzone_order = 'observe'\n")), 1)
        self.assertEqual(len(self.failures(self.NEEDS+"X4LAN_CONFIG.native_gate_tempzone_order = 'observe'\n")), 1)
        self.assertEqual(len(self.failures(BASE+self.NEEDS+self.TSNAP+"X4LAN_CONFIG.native_gate_tempzone_order = 'sort'\n")), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_tempzone_order = false\n'), [])


class WeaponFireRngOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'weapon_fire_rng' in f['rule']]

    def test_needs_the_world_identities_and_snapshot(self):
        ids = 'X4LAN_CONFIG.native_gate_world_identities = true\n'
        self.assertEqual(self.failures(BASE+ids+'X4LAN_CONFIG.native_gate_weapon_fire_rng = true\n'), [])
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_weapon_fire_rng = true\n')), 1)
        self.assertEqual(len(self.failures(BASE+ids+"X4LAN_CONFIG.native_gate_weapon_fire_rng = 'key'\n")), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_weapon_fire_rng = false\n'), [])


class SimulationCameraOptionTests(unittest.TestCase):
    VIEW = "X4LAN_CONFIG.native_gate_view_points = 'players'\n"

    def failures(self, text, key):
        return [f['rule'] for f in module.check(text)[1] if key in f['rule']]

    def test_fields_need_view_points_a_pass_join_and_snapshot(self):
        key = 'native_gate_region_fields'
        join = "X4LAN_CONFIG.native_gate_region_pass = 'sync'\n"
        self.assertEqual(self.failures(BASE+self.VIEW+join+"X4LAN_CONFIG.native_gate_region_fields = 'view'\n", key), [])
        self.assertEqual(self.failures(BASE+self.VIEW+"X4LAN_CONFIG.native_gate_mass_traffic = 'observe'\n"
                                       "X4LAN_CONFIG.native_gate_region_fields = 'view'\n", key), [])
        self.assertEqual(len(self.failures(BASE+self.VIEW+"X4LAN_CONFIG.native_gate_region_fields = 'view'\n", key)), 1)
        self.assertEqual(len(self.failures(BASE+self.VIEW+join+"X4LAN_CONFIG.native_gate_region_fields = true\n", key)), 1)
        self.assertEqual(len(self.failures(BASE+join+"X4LAN_CONFIG.native_gate_region_fields = 'view'\n", key)), 1)
        self.assertEqual(len(self.failures(self.VIEW+join+"X4LAN_CONFIG.native_gate_region_fields = 'view'\n", key)), 1)

    def test_effects_need_view_points_a_pass_join_and_snapshot(self):
        key = 'native_gate_effect_camera'
        join = "X4LAN_CONFIG.native_gate_mass_traffic = 'sync'\n"
        self.assertEqual(self.failures(BASE+self.VIEW+join+"X4LAN_CONFIG.native_gate_effect_camera = 'view'\n", key), [])
        self.assertEqual(len(self.failures(BASE+self.VIEW+"X4LAN_CONFIG.native_gate_effect_camera = 'view'\n", key)), 1)
        self.assertEqual(len(self.failures(BASE+join+"X4LAN_CONFIG.native_gate_effect_camera = 'view'\n", key)), 1)
        self.assertEqual(len(self.failures(BASE+self.VIEW+join+"X4LAN_CONFIG.native_gate_effect_camera = true\n", key)), 1)
        self.assertEqual(len(self.failures(self.VIEW+join+"X4LAN_CONFIG.native_gate_effect_camera = 'view'\n", key)), 1)
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_effect_camera = false\n", key), [])

    def test_camera_anchor_needs_view_points_and_excludes_the_offset(self):
        key = 'native_gate_camera_anchor'
        follow = "X4LAN_CONFIG.native_gate_camera_anchor = {client=2, slot=2}\n"
        self.assertEqual(self.failures(BASE+self.VIEW+follow, key), [])
        self.assertEqual(len(self.failures(BASE+follow, key)), 1)
        self.assertEqual(len(self.failures(BASE+self.VIEW+follow+"X4LAN_CONFIG.native_gate_camera_offset = {client=2, z=1}\n", key)), 1)
        self.assertEqual(len(self.failures(BASE+self.VIEW+follow+"X4LAN_CONFIG.native_gate_pilot_camera_follow = true\n", key)), 1)
        own = "X4LAN_CONFIG.native_gate_camera_anchor = 'own'\n"
        self.assertEqual(self.failures(BASE+self.VIEW+own, key), [])
        self.assertEqual(len(self.failures(BASE+own, key)), 1)

    def test_physics_scenes_need_view_points_a_pass_join_and_snapshot(self):
        key = 'native_gate_physics_scenes'
        join = "X4LAN_CONFIG.native_gate_mass_traffic = 'sync'\n"
        self.assertEqual(self.failures(BASE+self.VIEW+join+"X4LAN_CONFIG.native_gate_physics_scenes = 'viewpoints'\n", key), [])
        self.assertEqual(len(self.failures(BASE+self.VIEW+"X4LAN_CONFIG.native_gate_physics_scenes = 'viewpoints'\n", key)), 1)
        self.assertEqual(len(self.failures(BASE+join+"X4LAN_CONFIG.native_gate_physics_scenes = 'viewpoints'\n", key)), 1)
        self.assertEqual(len(self.failures(BASE+self.VIEW+join+"X4LAN_CONFIG.native_gate_physics_scenes = 'view'\n", key)), 1)
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_physics_scenes = false\n", key), [])

    def test_camera_context_needs_view_points_a_pass_join_and_snapshot(self):
        key = 'native_gate_camera_context'
        join = "X4LAN_CONFIG.native_gate_region_pass = 'sync'\n"
        self.assertEqual(self.failures(BASE+self.VIEW+join+"X4LAN_CONFIG.native_gate_camera_context = 'view'\n", key), [])
        self.assertEqual(len(self.failures(BASE+self.VIEW+"X4LAN_CONFIG.native_gate_camera_context = 'view'\n", key)), 1)
        self.assertEqual(len(self.failures(BASE+join+"X4LAN_CONFIG.native_gate_camera_context = 'view'\n", key)), 1)
        self.assertEqual(len(self.failures(BASE+self.VIEW+join+"X4LAN_CONFIG.native_gate_camera_context = 'viewpoints'\n", key)), 1)
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_camera_context = false\n", key), [])

    def test_runtime_ids_need_identities_the_snapshot_and_a_valid_slot(self):
        key = 'native_gate_runtime_ids'
        ids = "X4LAN_CONFIG.native_gate_world_identities = true\n"
        for value in ('true', "'observe'", '65536', '524288'):
            self.assertEqual(self.failures(BASE+ids+"X4LAN_CONFIG.native_gate_runtime_ids = "+value+"\n", key), [], value)
        for value in ('0', '524289', "'set'"):
            self.assertEqual(len(self.failures(BASE+ids+"X4LAN_CONFIG.native_gate_runtime_ids = "+value+"\n", key)), 1, value)
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_runtime_ids = true\n", key)), 1)
        self.assertEqual(len(self.failures(ids+"X4LAN_CONFIG.native_gate_runtime_ids = true\n", key)), 1)
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_runtime_ids = false\n", key), [])
        self.assertEqual(self.failures(BASE+ids+"X4LAN_CONFIG.native_gate_runtime_ids = true\nX4LAN_CONFIG.native_gate_runtime_ids_barrier = true\n", key), [])
        self.assertEqual(len(self.failures(BASE+ids+"X4LAN_CONFIG.native_gate_runtime_ids_barrier = true\n", key)), 1)
        self.assertEqual(len(self.failures(BASE+ids+"X4LAN_CONFIG.native_gate_runtime_ids = true\nX4LAN_CONFIG.native_gate_runtime_ids_barrier = 1\n", key)), 1)

    def test_runtime_ids_local_needs_a_set_slot_and_the_created_hook(self):
        key = 'native_gate_runtime_ids'
        ids = "X4LAN_CONFIG.native_gate_world_identities = true\n"
        hook = "X4LAN_CONFIG.native_gate_created_identities = true\n"
        local = "X4LAN_CONFIG.native_gate_runtime_ids_local = true\n"
        self.assertEqual(self.failures(BASE+ids+hook+"X4LAN_CONFIG.native_gate_runtime_ids = true\n"+local, key), [])
        self.assertEqual(self.failures(BASE+ids+hook+"X4LAN_CONFIG.native_gate_runtime_ids = 65536\n"+local, key), [])
        self.assertEqual(self.failures(BASE+ids+hook+"X4LAN_CONFIG.native_gate_runtime_ids = true\nX4LAN_CONFIG.native_gate_runtime_ids_local = false\n", key), [])
        self.assertEqual(len(self.failures(BASE+ids+hook+local, key)), 1)
        self.assertEqual(len(self.failures(BASE+ids+hook+"X4LAN_CONFIG.native_gate_runtime_ids = 'observe'\n"+local, key)), 1)
        self.assertEqual(len(self.failures(BASE+ids+"X4LAN_CONFIG.native_gate_runtime_ids = true\n"+local, key)), 1)
        self.assertEqual(len(self.failures(BASE+ids+hook+"X4LAN_CONFIG.native_gate_runtime_ids = true\nX4LAN_CONFIG.native_gate_runtime_ids_local = 1\n", key)), 1)

    def test_highway_speed_needs_view_points_a_pass_join_and_identities(self):
        key = 'native_gate_highway_speed'
        view = "X4LAN_CONFIG.native_gate_view_points = 'players'\n"
        join = "X4LAN_CONFIG.native_gate_region_pass = 'sync'\n"
        ids = "X4LAN_CONFIG.native_gate_world_identities = true\n"
        on = "X4LAN_CONFIG.native_gate_highway_speed = 'key'\n"
        self.assertEqual(self.failures(BASE+view+join+ids+on, key), [])
        self.assertEqual(len(self.failures(BASE+join+ids+on, key)), 1)
        self.assertEqual(len(self.failures(BASE+view+ids+on, key)), 1)
        self.assertEqual(len(self.failures(BASE+view+join+on, key)), 1)
        self.assertEqual(len(self.failures(BASE+view+join+ids+"X4LAN_CONFIG.native_gate_highway_speed = 'view'\n", key)), 1)
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_highway_speed = false\n", key), [])

    def test_view_scenes_need_the_scenes_group(self):
        key = 'native_gate_view_scenes'
        view = "X4LAN_CONFIG.native_gate_view_points = 'players'\n"
        join = "X4LAN_CONFIG.native_gate_region_pass = 'sync'\n"
        scenes = "X4LAN_CONFIG.native_gate_physics_scenes = 'viewpoints'\n"
        on = "X4LAN_CONFIG.native_gate_view_scenes = true\n"
        self.assertEqual(self.failures(BASE+view+join+scenes+on, key), [])
        self.assertEqual(len(self.failures(BASE+view+join+on, key)), 1)
        self.assertEqual(len(self.failures(BASE+join+scenes+on, key)), 1)
        self.assertEqual(len(self.failures(BASE+view+scenes+on, key)), 1)
        self.assertEqual(len(self.failures(BASE+view+join+scenes+"X4LAN_CONFIG.native_gate_view_scenes = 'all'\n", key)), 1)
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_view_scenes = false\n", key), [])

    def test_field_fill_needs_the_fields_group(self):
        key = 'native_gate_field_fill'
        view = "X4LAN_CONFIG.native_gate_view_points = 'players'\n"
        join = "X4LAN_CONFIG.native_gate_region_pass = 'sync'\n"
        fields = "X4LAN_CONFIG.native_gate_region_fields = 'view'\n"
        for mode in ('view', 'block'):
            on = "X4LAN_CONFIG.native_gate_field_fill = '%s'\n" % mode
            self.assertEqual(self.failures(BASE+view+join+fields+on, key), [])
            self.assertEqual(len(self.failures(BASE+view+join+on, key)), 1)
            self.assertEqual(len(self.failures(BASE+join+fields+on, key)), 1)
            self.assertEqual(len(self.failures(BASE+view+fields+on, key)), 1)
        self.assertEqual(len(self.failures(BASE+view+join+fields+"X4LAN_CONFIG.native_gate_field_fill = 'all'\n", key)), 1)
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_field_fill = false\n", key), [])

    def test_world_hash_shields(self):
        key = 'world_hash_shields'
        stride = "X4LAN_CONFIG.native_gate_world_hash_stride = 300\n"
        self.assertEqual(self.failures(BASE+stride+"X4LAN_CONFIG.native_gate_world_hash_shields = 'canonical'\n", key), [])
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_world_hash_shields = 'canonical'\n", key)), 1)
        self.assertEqual(len(self.failures(BASE+stride+"X4LAN_CONFIG.native_gate_world_hash_shields = 'raw'\n", key)), 1)
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_world_hash_shields = false\n", key), [])

    def test_speak_timer_values(self):
        key = 'native_gate_speak_timer'
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_speak_timer = true\n", key), [])
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_speak_timer = 'audio'\n", key)), 1)
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_speak_timer = false\n", key), [])

    def test_viewpoint_standins(self):
        key = 'viewpoint_standins'
        policy = "X4LAN_CONFIG.native_gate_viewpoint_policy = 'sector'\n"
        for n in ('2', '3', '4'):
            self.assertEqual(self.failures(BASE+policy+"X4LAN_CONFIG.native_gate_viewpoint_standins = %s\n" % n, key), [])
        self.assertEqual(len(self.failures(BASE+policy+"X4LAN_CONFIG.native_gate_viewpoint_standins = 5\n", key)), 1)
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_viewpoint_standins = 3\n", key)), 1)
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_viewpoint_standins = nil\n", key), [])

    def test_traffic_rows_need_view_points_and_a_pass_join(self):
        key = 'native_gate_traffic_rows'
        view = "X4LAN_CONFIG.native_gate_view_points = 'players'\n"
        join = "X4LAN_CONFIG.native_gate_mass_traffic = 'sync'\n"
        on = "X4LAN_CONFIG.native_gate_traffic_rows = true\n"
        self.assertEqual(self.failures(BASE+view+join+on, key), [])
        self.assertEqual(len(self.failures(BASE+join+on, key)), 1)
        self.assertEqual(len(self.failures(BASE+view+on, key)), 1)
        self.assertEqual(len(self.failures(BASE+view+join+"X4LAN_CONFIG.native_gate_traffic_rows = 'all'\n", key)), 1)
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_traffic_rows = false\n", key), [])

    def test_traffic_views_need_view_points_and_a_pass_join(self):
        key = 'native_gate_traffic_views'
        view = "X4LAN_CONFIG.native_gate_view_points = 'players'\n"
        join = "X4LAN_CONFIG.native_gate_mass_traffic = 'sync'\n"
        on = "X4LAN_CONFIG.native_gate_traffic_views = 'block'\n"
        self.assertEqual(self.failures(BASE+view+join+on, key), [])
        self.assertEqual(len(self.failures(BASE+join+on, key)), 1)
        self.assertEqual(len(self.failures(BASE+view+on, key)), 1)
        for mode in ('observe', 'view'):
            self.assertEqual(self.failures(BASE+view+join+"X4LAN_CONFIG.native_gate_traffic_views = '%s'\n" % mode, key), [])
        self.assertEqual(len(self.failures(BASE+view+join+"X4LAN_CONFIG.native_gate_traffic_views = 'all'\n", key)), 1)
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_traffic_views = false\n", key), [])

    def test_sound_rng_needs_view_points_and_a_pass_join(self):
        key = 'native_gate_sound_rng'
        view = "X4LAN_CONFIG.native_gate_view_points = 'players'\n"
        join = "X4LAN_CONFIG.native_gate_region_pass = 'sync'\n"
        on = "X4LAN_CONFIG.native_gate_sound_rng = 'split'\n"
        self.assertEqual(self.failures(BASE+view+join+on, key), [])
        self.assertEqual(len(self.failures(BASE+join+on, key)), 1)
        self.assertEqual(len(self.failures(BASE+view+on, key)), 1)
        self.assertEqual(len(self.failures(BASE+view+join+"X4LAN_CONFIG.native_gate_sound_rng = 'key'\n", key)), 1)
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_sound_rng = false\n", key), [])

    def test_highway_rows_need_view_points_a_pass_join_and_tick_counts(self):
        key = 'native_gate_highway_rows'
        ok = "X4LAN_CONFIG.native_gate_view_points = 'players'\nX4LAN_CONFIG.native_gate_region_pass = 'sync'\n"
        self.assertEqual(self.failures(BASE+ok+"X4LAN_CONFIG.native_gate_highway_rows = 600\nX4LAN_CONFIG.native_gate_highway_rows_from = 1090\n", key), [])
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_region_pass = 'sync'\nX4LAN_CONFIG.native_gate_highway_rows = 6\n", key)), 1)
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_view_points = 'players'\nX4LAN_CONFIG.native_gate_highway_rows = 6\n", key)), 1)
        for bad in ('0', '3601', 'true', "'x'"):
            self.assertEqual(len(self.failures(BASE+ok+"X4LAN_CONFIG.native_gate_highway_rows = "+bad+"\n", key)), 1, bad)
        for bad in ('0', '1000001', 'true'):
            self.assertEqual(len(self.failures(BASE+ok+"X4LAN_CONFIG.native_gate_highway_rows = 6\nX4LAN_CONFIG.native_gate_highway_rows_from = "+bad+"\n", key)), 1, bad)
        self.assertEqual(len(self.failures(BASE+ok+"X4LAN_CONFIG.native_gate_highway_rows_from = 10\n", key)), 1)

    def test_physics_trace_children_needs_the_trace_and_a_tick_count(self):
        key = 'native_gate_physics_trace_children'
        trace = "X4LAN_CONFIG.native_gate_physics_trace = 0\n"
        self.assertEqual(self.failures(BASE+trace+"X4LAN_CONFIG.native_gate_physics_trace_children = 3\n", key), [])
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_physics_trace_children = 3\n", key)), 1)
        for bad in ('0', '3601', 'true', "'3'"):
            self.assertEqual(len(self.failures(BASE+trace+"X4LAN_CONFIG.native_gate_physics_trace_children = "+bad+"\n", key)), 1, bad)

    def test_physics_trace_saved_needs_body_rows(self):
        key = 'native_gate_physics_trace_saved'
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_physics_trace = 900\nX4LAN_CONFIG.native_gate_physics_trace_saved = true\n", key), [])
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_physics_trace = 0\nX4LAN_CONFIG.native_gate_physics_trace_saved = true\n", key)), 1)
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_physics_trace_saved = true\n", key)), 1)
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_physics_trace = 900\nX4LAN_CONFIG.native_gate_physics_trace_saved = 1\n", key)), 1)
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_physics_trace_saved = false\n", key), [])

    def test_mining_xs_and_probe_need_view_points_and_a_pass_join(self):
        join = "X4LAN_CONFIG.native_gate_mass_traffic = 'sync'\n"
        for key, good, bad in (('native_gate_mining_camera', "'view'", "'sector'"), ('native_gate_xs_camera', "'view'", 'true'),
                               ('native_gate_camera_probe', 'true', "'view'")):
            line = 'X4LAN_CONFIG.%s = %s\n'
            self.assertEqual(self.failures(BASE+self.VIEW+join+line % (key, good), key), [], key)
            self.assertEqual(len(self.failures(BASE+self.VIEW+line % (key, good), key)), 1, key)
            self.assertEqual(len(self.failures(BASE+join+line % (key, good), key)), 1, key)
            self.assertEqual(len(self.failures(BASE+self.VIEW+join+line % (key, bad), key)), 1, key)
            self.assertEqual(self.failures(BASE+line % (key, 'false'), key), [], key)

    def test_shape_refresh_needs_view_points_and_a_pass_join(self):
        key = 'native_gate_shape_refresh'
        join = "X4LAN_CONFIG.native_gate_mass_traffic = 'sync'\n"
        self.assertEqual(self.failures(BASE+self.VIEW+join+"X4LAN_CONFIG.native_gate_shape_refresh = 'view'\n", key), [])
        self.assertEqual(len(self.failures(BASE+self.VIEW+"X4LAN_CONFIG.native_gate_shape_refresh = 'view'\n", key)), 1)
        self.assertEqual(len(self.failures(BASE+join+"X4LAN_CONFIG.native_gate_shape_refresh = 'view'\n", key)), 1)
        self.assertEqual(len(self.failures(BASE+self.VIEW+join+"X4LAN_CONFIG.native_gate_shape_refresh = 'sync'\n", key)), 1)
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_shape_refresh = false\n", key), [])

    def test_viewpoint_policy_needs_view_points_and_the_traffic_or_highway_group(self):
        key = 'native_gate_viewpoint_policy'
        join = "X4LAN_CONFIG.native_gate_region_pass = 'sync'\n"
        highway = "X4LAN_CONFIG.native_gate_highway_camera = 'view'\n"
        traffic = "X4LAN_CONFIG.native_gate_mass_traffic = 'sync'\nX4LAN_CONFIG.native_gate_mass_traffic_camera = 'view'\n"
        policy = "X4LAN_CONFIG.native_gate_viewpoint_policy = 'sector'\n"
        self.assertEqual(self.failures(BASE+self.VIEW+join+highway+policy, key), [])
        self.assertEqual(self.failures(BASE+self.VIEW+traffic+policy, key), [])
        self.assertEqual(len(self.failures(BASE+self.VIEW+join+policy, key)), 1)
        self.assertEqual(len(self.failures(BASE+join+highway+policy, key)), 1)
        self.assertEqual(len(self.failures(BASE+self.VIEW+join+highway+"X4LAN_CONFIG.native_gate_viewpoint_policy = 'two'\n", key)), 1)
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_viewpoint_policy = false\n", key), [])

    def test_highway_needs_view_points_a_pass_join_and_snapshot(self):
        key = 'native_gate_highway_camera'
        join = "X4LAN_CONFIG.native_gate_region_pass = 'sync'\n"
        self.assertEqual(self.failures(BASE+self.VIEW+join+"X4LAN_CONFIG.native_gate_highway_camera = 'view'\n", key), [])
        self.assertEqual(len(self.failures(BASE+self.VIEW+"X4LAN_CONFIG.native_gate_highway_camera = 'view'\n", key)), 1)
        self.assertEqual(len(self.failures(BASE+join+"X4LAN_CONFIG.native_gate_highway_camera = 'view'\n", key)), 1)
        self.assertEqual(len(self.failures(BASE+self.VIEW+join+"X4LAN_CONFIG.native_gate_highway_camera = true\n", key)), 1)
        self.assertEqual(len(self.failures(self.VIEW+join+"X4LAN_CONFIG.native_gate_highway_camera = 'view'\n", key)), 1)
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_highway_camera = false\n", key), [])

    def test_traffic_needs_view_points_the_synced_pass_and_snapshot(self):
        key = 'native_gate_mass_traffic_camera'
        sync = "X4LAN_CONFIG.native_gate_mass_traffic = 'sync'\n"
        self.assertEqual(self.failures(BASE+self.VIEW+sync+"X4LAN_CONFIG.native_gate_mass_traffic_camera = 'view'\n", key), [])
        self.assertEqual(self.failures(BASE+self.VIEW+"X4LAN_CONFIG.native_gate_mass_traffic = true\n"
                                       "X4LAN_CONFIG.native_gate_mass_traffic_camera = 'view'\n", key), [])
        self.assertEqual(len(self.failures(BASE+self.VIEW+"X4LAN_CONFIG.native_gate_mass_traffic = 'observe'\n"
                                           "X4LAN_CONFIG.native_gate_mass_traffic_camera = 'view'\n", key)), 1)
        self.assertEqual(len(self.failures(BASE+sync+"X4LAN_CONFIG.native_gate_mass_traffic_camera = 'view'\n", key)), 1)
        self.assertEqual(len(self.failures(BASE+self.VIEW+sync+"X4LAN_CONFIG.native_gate_mass_traffic_camera = 'on'\n", key)), 1)
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_mass_traffic_camera = false\n", key), [])


class ViewRowsOptionTests(unittest.TestCase):
    VIEW = "X4LAN_CONFIG.native_gate_view_points = 'players'\n"

    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'view_rows' in f['rule']]

    def test_needs_view_points_and_the_snapshot(self):
        self.assertEqual(self.failures(BASE+self.VIEW+'X4LAN_CONFIG.native_gate_view_rows = true\n'), [])
        self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_view_rows = true\n')), 1)
        self.assertEqual(len(self.failures(BASE+self.VIEW+'X4LAN_CONFIG.native_gate_view_rows = 2\n')), 1)
        self.assertEqual(len(self.failures(self.VIEW+'X4LAN_CONFIG.native_gate_view_rows = true\n')), 1)

    def test_triggers_values_and_view_points(self):
        rules = lambda text: [f['rule'] for f in module.check(text)[1] if 'view_triggers' in f['rule']]
        for value in ("'block'", "'observe'"):
            self.assertEqual(rules(BASE+self.VIEW+'X4LAN_CONFIG.native_gate_view_triggers = %s\n' % value), [])
        self.assertEqual(len(rules(BASE+self.VIEW+"X4LAN_CONFIG.native_gate_view_triggers = 'drop'\n")), 1)
        self.assertEqual(len(rules(BASE+"X4LAN_CONFIG.native_gate_view_triggers = 'block'\n")), 1)


class GravidarDigestOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'gravidar_digest' in f['rule']]

    def test_values_and_the_snapshot(self):
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_gravidar_digest = true\n'), [])
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_gravidar_digest = 'on'\n")), 1)
        self.assertEqual(len(self.failures('X4LAN_CONFIG.native_gate_gravidar_digest = true\n')), 1)

    def test_rows_are_a_tick_count_and_need_the_digest(self):
        on = BASE+'X4LAN_CONFIG.native_gate_gravidar_digest = true\n'
        for value in ('1', '1500', '1000000', 'nil'):
            self.assertEqual(self.failures(on+'X4LAN_CONFIG.native_gate_gravidar_digest_rows = %s\n' % value), [], value)
        for value in ('0', '-1', '1.5', '1000001', 'true', "'8'", '08'):
            self.assertEqual(len(self.failures(on+'X4LAN_CONFIG.native_gate_gravidar_digest_rows = %s\n' % value)), 1, value)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_gravidar_digest_rows = 8\n'),
                         ['native_gate_gravidar_digest_rows needs native_gate_gravidar_digest'])


class GravidarDeadlinesOptionTests(unittest.TestCase):
    NEEDS = 'X4LAN_CONFIG.native_gate_world_identities = true\nX4LAN_CONFIG.native_gate_gravidar_time_sync = true\n'
    ON = 'X4LAN_CONFIG.native_gate_gravidar_deadlines = true\n'

    def failures(self, text):
        return [f for f in module.check(text)[1] if 'gravidar_deadlines' in f['rule']]

    def test_needs_identities_the_time_sync_and_the_snapshot_boot(self):
        self.assertEqual(self.failures(BASE+self.NEEDS+self.ON), [])
        for line in self.NEEDS.splitlines():
            with self.subTest(missing=line):
                found = self.failures(BASE+self.NEEDS.replace(line+'\n', '')+self.ON)
                self.assertEqual([f['bits'] for f in found], [[line.split('.')[1].split(' ')[0]]])
        self.assertEqual(len(self.failures(self.NEEDS+self.ON)), 1)
        for value in ("'on'", '1', "'key'"):
            self.assertEqual(len(self.failures(BASE+self.NEEDS+'X4LAN_CONFIG.native_gate_gravidar_deadlines = %s\n' % value)), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_gravidar_deadlines = false\n'), [])


class MassTrafficOptionTests(unittest.TestCase):
    def failures(self, text, key='mass_traffic'):
        return [f['rule'] for f in module.check(text)[1] if key in f['rule']]

    def test_values_and_the_snapshot_boot(self):
        for key in ('mass_traffic', 'region_pass'):
            for value in ('true', "'sync'", '"observe"'):
                with self.subTest(key=key, value=value):
                    self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_%s = %s\n' % (key, value), key), [])
            for value in ("'off'", "'SYNC'", '2', "'disable'"):
                with self.subTest(key=key, value=value):
                    self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_%s = %s\n' % (key, value), key)), 1)
            self.assertEqual(len(self.failures('X4LAN_CONFIG.native_gate_%s = true\n' % key, key)), 1)
            self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_%s = false\n' % key, key), [])


class PilotFireOptionTests(unittest.TestCase):
    READY = (BASE+'X4LAN_CONFIG.native_gate_pilot_inputs = true\nX4LAN_CONFIG.native_gate_world_identities = true\n'
             'X4LAN_CONFIG.native_gate_agent_rng = true\nX4LAN_CONFIG.native_gate_ship_rng = true\n'
             'X4LAN_CONFIG.native_gate_actor_rng_all = true\nX4LAN_CONFIG.native_gate_bullet_rng = true\n'
             "X4LAN_CONFIG.native_gate_bullet_cadence = 'every'\n")

    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'pilot_fire' in f['rule']]

    def test_modes_and_prerequisites(self):
        for value in ('true', "'record'", '"script"'):
            with self.subTest(value=value):
                self.assertEqual(self.failures(self.READY+'X4LAN_CONFIG.native_gate_pilot_fire = %s\n' % value), [])
        for value in ("'always'", '1'):
            with self.subTest(value=value):
                self.assertEqual(len(self.failures(self.READY+'X4LAN_CONFIG.native_gate_pilot_fire = %s\n' % value)), 1)
        for drop in ('X4LAN_CONFIG.native_gate_actor_rng_all = true\n', "X4LAN_CONFIG.native_gate_bullet_cadence = 'every'\n",
                     'X4LAN_CONFIG.native_gate_pilot_inputs = true\n'):
            with self.subTest(drop=drop):
                self.assertEqual(len(self.failures(self.READY.replace(drop, '')+'X4LAN_CONFIG.native_gate_pilot_fire = true\n')), 1)
        self.assertEqual(self.failures(self.READY+'X4LAN_CONFIG.native_gate_pilot_fire = false\n'), [])


class HitClaimOptionTests(unittest.TestCase):
    READY = (BASE+'X4LAN_CONFIG.native_gate_pilot_inputs = true\nX4LAN_CONFIG.native_gate_world_identities = true\n'
             "X4LAN_CONFIG.native_gate_pilot_source = 'wire'\nX4LAN_CONFIG.native_gate_bullet_rng = true\n"
             "X4LAN_CONFIG.native_gate_bullet_cadence = 'every'\n")

    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'hit_claims' in f['rule']]

    def test_policies_and_prerequisites(self):
        for value in ('true', "'shooter'", '"victim"', "'first'"):
            with self.subTest(value=value):
                self.assertEqual(self.failures(self.READY+'X4LAN_CONFIG.native_gate_hit_claims = %s\n' % value), [])
        for value in ("'always'", '1', "'SHOOTER'"):
            with self.subTest(value=value):
                self.assertEqual(len(self.failures(self.READY+'X4LAN_CONFIG.native_gate_hit_claims = %s\n' % value)), 1)
        for drop in ("X4LAN_CONFIG.native_gate_pilot_source = 'wire'\n", "X4LAN_CONFIG.native_gate_bullet_cadence = 'every'\n",
                     'X4LAN_CONFIG.native_gate_world_identities = true\n', 'X4LAN_CONFIG.native_gate_pilot_inputs = true\n'):
            with self.subTest(drop=drop):
                self.assertEqual(len(self.failures(self.READY.replace(drop, '')+'X4LAN_CONFIG.native_gate_hit_claims = true\n')), 1)
        self.assertEqual(self.failures(self.READY+'X4LAN_CONFIG.native_gate_hit_claims = false\n'), [])
        self.assertEqual(self.failures(self.READY), [])


class PilotPredictOptionTests(unittest.TestCase):
    PILOT = BASE+'X4LAN_CONFIG.native_gate_pilot_inputs = true\n'

    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'pilot_predict' in f['rule']]

    def test_modes_per_client_lists_and_prerequisites(self):
        split = 'X4LAN_CONFIG.native_gate_render_rng_split = true\n'
        for value in ('true', "'observe'", '"observe"', "{false,'observe'}", "{'observe',false}", "{nil,'present'}", "'present'"):
            with self.subTest(value=value):
                self.assertEqual(self.failures(self.PILOT+split+'X4LAN_CONFIG.native_gate_pilot_predict = %s\n' % value), [])
        self.assertEqual(self.failures(self.PILOT+"X4LAN_CONFIG.native_gate_pilot_predict = 'observe' -- this client only\n"), [])
        self.assertEqual(len(self.failures(self.PILOT+"X4LAN_CONFIG.native_gate_pilot_predict = {false,'present'}\n")), 1)  # no split
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_pilot_predict = 'observe'\n")), 1)  # no pilot inputs
        self.assertEqual(len(self.failures("X4LAN_CONFIG.native_gate_pilot_inputs = true\nX4LAN_CONFIG.native_gate_pilot_predict = true\n")), 1)
        for value in ("'fast'", '1', "{'observe','always'}", "'PRESENT'"):
            with self.subTest(value=value):
                self.assertEqual(len(self.failures(self.PILOT+split+'X4LAN_CONFIG.native_gate_pilot_predict = %s\n' % value)), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_pilot_predict = false\n'), [])
        self.assertEqual(self.failures(BASE), [])


class PilotCaptureSourceOptionTests(unittest.TestCase):
    PILOT = BASE+'X4LAN_CONFIG.native_gate_pilot_inputs = true\nX4LAN_CONFIG.native_gate_pilot_suppress_local = true\n'

    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'capture_source' in f['rule']]

    def test_native_needs_parking_and_a_capture_device_is_free(self):
        capture = 'X4LAN_CONFIG.native_gate_pilot_capture = true\n'
        wire = "X4LAN_CONFIG.native_gate_pilot_source = 'wire'\n"
        for value in ("'native'", "{'native','device'}", "{nil,'native'}", "'device'", "{'device',false}"):
            with self.subTest(value=value):
                self.assertEqual(self.failures(self.PILOT+capture+'X4LAN_CONFIG.native_gate_pilot_capture_source = %s\n' % value), [])
        self.assertEqual(self.failures(self.PILOT+wire+"X4LAN_CONFIG.native_gate_pilot_capture_source = 'native'\n"), [])
        self.assertEqual(len(self.failures(self.PILOT+"X4LAN_CONFIG.native_gate_pilot_capture_source = 'native'\n")), 1)  # no capture
        self.assertEqual(len(self.failures(BASE+capture+"X4LAN_CONFIG.native_gate_pilot_capture_source = 'native'\n")), 2)
        self.assertEqual(len(self.failures('X4LAN_CONFIG.native_gate_pilot_inputs = true\nX4LAN_CONFIG.native_gate_pilot_suppress_local = true\n'
                                           + capture + "X4LAN_CONFIG.native_gate_pilot_capture_source = 'native'\n")), 1)  # no snapshot
        for value in ("'keys'", '1', "{'native','pad'}", "'NATIVE'"):
            with self.subTest(value=value):
                self.assertEqual(len(self.failures(self.PILOT+capture+'X4LAN_CONFIG.native_gate_pilot_capture_source = %s\n' % value)), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_pilot_capture_source = false\n'), [])
        self.assertEqual(self.failures(BASE), [])


class ActorRngAllOptionTests(unittest.TestCase):
    NEEDS = ('X4LAN_CONFIG.native_gate_world_identities = true\nX4LAN_CONFIG.native_gate_agent_rng = true\n'
             'X4LAN_CONFIG.native_gate_ship_rng = true\n')

    def failures(self, text):
        return [f for f in module.check(text)[1] if 'actor_rng_all' in f['rule']]

    def test_needs_identities_seeded_agent_and_ship_modules_and_the_snapshot_boot(self):
        self.assertEqual(self.failures(BASE+self.NEEDS+'X4LAN_CONFIG.native_gate_actor_rng_all = true\n'), [])
        for line in self.NEEDS.splitlines():
            with self.subTest(missing=line):
                found = self.failures(BASE+self.NEEDS.replace(line+'\n', '')+'X4LAN_CONFIG.native_gate_actor_rng_all = true\n')
                self.assertEqual(len(found), 1)
                self.assertEqual(found[0]['bits'], [line.split('.')[1].split(' ')[0]])
        self.assertEqual(len(self.failures(self.NEEDS+'X4LAN_CONFIG.native_gate_actor_rng_all = true\n')), 1)

    def test_rejects_other_values_and_ignores_off(self):
        for value in ("'key'", '1', "'observe'"):
            with self.subTest(value=value):
                self.assertEqual(len(self.failures(BASE+self.NEEDS+'X4LAN_CONFIG.native_gate_actor_rng_all = %s\n' % value)), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_actor_rng_all = false\n'), [])


class SpawnRngOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'spawn_rng' in f['rule']]

    def test_known_modes_need_the_snapshot_boot(self):
        for value in ("'key'", '"observe"'):
            self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_spawn_rng = %s\n' % value), [])
        self.assertEqual(len(self.failures("X4LAN_CONFIG.native_gate_spawn_rng = 'key'\n")), 1)
        for value in ('true', "'Key'", '1'):
            self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_spawn_rng = %s\n' % value)), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_spawn_rng = false\n'), [])


class StatusStrideOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'status_stride' in f['rule']]

    def test_tick_counts_one_to_3600_and_no_prerequisite(self):
        for value in ('1', '60', '600', '3600', 'nil'):
            with self.subTest(value=value):
                self.assertEqual(self.failures('X4LAN_CONFIG.native_gate_status_stride = %s\n' % value), [])
        for value in ('0', '-1', '1.5', '3601', '06', 'true', "'60'", '{60}'):
            with self.subTest(value=value):
                self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_status_stride = %s\n' % value)), 1)


class ProbeProfileOptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in module.check(text)[1] if 'probe_profile' in f['rule']]

    def test_true_or_off(self):
        for value in ('true', 'false', 'nil'):
            self.assertEqual(self.failures('X4LAN_CONFIG.native_gate_probe_profile = %s\n' % value), [])
        for value in ('1', "'on'", '60'):
            self.assertEqual(len(self.failures(BASE+'X4LAN_CONFIG.native_gate_probe_profile = %s\n' % value)), 1)


class CreatedLoadOptionTests(unittest.TestCase):
    NEEDS = 'X4LAN_CONFIG.native_gate_world_identities = true\nX4LAN_CONFIG.native_gate_created_identities = true\n'
    ON = 'X4LAN_CONFIG.native_gate_created_load = true\n'

    def failures(self, text):
        return [f for f in module.check(text)[1] if 'created_load' in f['rule']]

    def test_needs_saved_and_created_identities_and_the_snapshot_boot(self):
        self.assertEqual(self.failures(BASE+self.NEEDS+self.ON), [])
        for line in self.NEEDS.splitlines():
            with self.subTest(missing=line):
                found = self.failures(BASE+self.NEEDS.replace(line+'\n', '')+self.ON)
                self.assertEqual([f['bits'] for f in found], [[line.split('.')[1].split(' ')[0]]])
        self.assertEqual(len(self.failures(self.NEEDS+self.ON)), 1)
        for value in ("'on'", '1'):
            self.assertEqual(len(self.failures(BASE+self.NEEDS+'X4LAN_CONFIG.native_gate_created_load = %s\n' % value)), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_created_load = false\n'), [])


if __name__ == '__main__':
    unittest.main()
