"""Build the opt-in Windows gate experiment, separately from playable packages."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'src'))
from native.engine.build import compiler_path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--zig', type=Path, required=True)
    p.add_argument('--test', action='store_true')
    p.add_argument('--native-image', type=Path, help='with --test, verify hook emission against the pinned image read-only')
    args = p.parse_args()
    if args.native_image and not args.test:p.error('--native-image requires --test')
    compiler = compiler_path(args.zig)
    out = ROOT/'.local/native-lockstep/build'
    out.mkdir(parents=True, exist_ok=True)
    common = [compiler, 'cc', '-target', 'x86_64-windows-gnu', '-mcx16', '-std=c11',
              '-O2', '-Wall', '-Wextra', '-Werror', '-fms-extensions', '-ffp-contract=off']
    source = ROOT/'src/native/engine'
    common += [str(source/'portable_rotation.c'), str(ROOT/'src/native/portable_math/portable_math.c'),
               str(ROOT/'src/native/portable_math/portable_decimal.c')]
    subprocess.run(common+['-shared', str(source/'lockstep_gate.c'), '-lbcrypt', '-lws2_32',
                          '-o', str(out/'x4lan_gate_64.dll')], check=True)
    if args.test:
        subprocess.run(common+[str(source/'test_rotation_xml_scope.c'),
                              '-o', str(out/'test_rotation_scope.exe')], check=True)
        subprocess.run([str(out/'test_rotation_scope.exe')], check=True)
        subprocess.run(common+[str(source/'test_lockstep_gate.c'), '-lbcrypt', '-lws2_32',
                              '-o', str(out/'test_gate.exe')], check=True)
        gate_env=dict(os.environ,X4LAN_XRCP_EXPORT=str(out/'xrcp-export.bin')) if args.native_image else None
        subprocess.run([str(out/'test_gate.exe')]+([str(args.native_image.resolve())] if args.native_image else []), check=True,env=gate_env)
        if args.native_image:
            try:
                import capstone  # noqa: F401  (the trampoline proof decodes with capstone)
            except ImportError:
                print('check_exact_reciprocal.py skipped: capstone not installed')
            else:
                subprocess.run([sys.executable,str(ROOT/'tools/dev/check_exact_reciprocal.py'),'--windows',str(args.native_image.resolve()),
                                '--export',str(out/'xrcp-export.bin')],check=True)
        subprocess.run(common+[str(source/'test_lockstep_world_find_rng.c'), '-lbcrypt', '-lws2_32',
                              '-o', str(out/'test_world_find_rng.exe')], check=True)
        subprocess.run([str(out/'test_world_find_rng.exe')]+([str(args.native_image.resolve())] if args.native_image else []), check=True)
        subprocess.run(common+[str(source/'test_lockstep_world_position.c'),str(source/'test_lockstep_world_position_frame.S'), '-lbcrypt', '-lws2_32',
                              '-o', str(out/'test_world_position.exe')], check=True)
        if args.native_image:
            export=out/'position-owned-export.log'
            subprocess.run([str(out/'test_world_position.exe'),str(args.native_image.resolve()),str(export)], check=True)
            subprocess.run([sys.executable,str(ROOT/'tools/dev/check_native_world_position.py'),str(export)],check=True)
        subprocess.run(common+['-DNDEBUG',str(source/'test_lockstep_world_position_rng.c'),str(source/'test_lockstep_world_position_frame.S'),
                              str(source/'test_lockstep_world_position_rng_frame.S'),'-lbcrypt','-lws2_32','-o',str(out/'test_world_position_rng.exe')],check=True)
        if args.native_image:
            export=out/'position-rng-owned-export.log'
            subprocess.run([str(out/'test_world_position_rng.exe'),str(args.native_image.resolve()),str(export)],check=True)
            subprocess.run([sys.executable,str(ROOT/'tools/dev/check_native_world_position_rng.py'),str(export)],check=True)
        subprocess.run(common+['-DNDEBUG',str(source/'test_lockstep_world_safe_position.c'),str(source/'test_lockstep_world_position_frame.S'),
                              str(source/'test_lockstep_world_safe_position_frame.S'),'-lbcrypt','-lws2_32','-o',str(out/'test_world_safe_position.exe')],check=True)
        if args.native_image:
            export=out/'safe-position-owned-export.log'
            subprocess.run([str(out/'test_world_safe_position.exe'),str(args.native_image.resolve()),str(export)],check=True)
            subprocess.run([sys.executable,str(ROOT/'tools/dev/check_native_world_safe_position.py'),str(export)],check=True)
        subprocess.run(common+[str(source/'test_lockstep_transition.c'), '-lbcrypt', '-lws2_32',
                              '-o', str(out/'test_transition.exe')], check=True)
        transition_env=dict(os.environ,X4LAN_TRANSITION_QUEUE_DIAG_TEST_LOG=str(out/'transition-queue-diag-owned.log'),
                            X4LAN_TRANSITION_PREBIND_TEST_LOG=str(out/'transition-prebind-owned.log'))
        subprocess.run([str(out/'test_transition.exe')]+([str(args.native_image.resolve())] if args.native_image else []), check=True,env=transition_env)
        subprocess.run([sys.executable,str(ROOT/'tools/dev/check_native_transition_queue_diag.py'),transition_env['X4LAN_TRANSITION_QUEUE_DIAG_TEST_LOG']],check=True)
        subprocess.run([sys.executable,str(ROOT/'tools/dev/check_native_transition_prebind.py'),transition_env['X4LAN_TRANSITION_PREBIND_TEST_LOG']],check=True)
        subprocess.run(common+[str(source/'test_lockstep_creation_forward.c'),
                              '-o', str(out/'test_creation_forward.exe')], check=True)
        subprocess.run([str(out/'test_creation_forward.exe')], check=True)
        subprocess.run(common+['-shared', str(source/'test_lockstep_creation_rng.c'), '-lbcrypt', '-lws2_32',
                              '-o', str(out/'test_creation_rng.dll')], check=True)
        if args.native_image:
            subprocess.run([sys.executable, '-c', 'import ctypes,sys; d=ctypes.CDLL(sys.argv[1]); '
                            'd.creation_rng_test.argtypes=[ctypes.c_char_p]; '
                            'sys.exit(d.creation_rng_test(sys.argv[2].encode()))',
                            str(out/'test_creation_rng.dll'), str(args.native_image.resolve())], check=True)
        subprocess.run(common+['-shared',str(source/'test_lockstep_registry_creation.c'),'-lbcrypt','-lws2_32',
                              '-o',str(out/'test_registry_creation.dll')],check=True)
        if args.native_image:
            result=subprocess.run([sys.executable,'-B','-c','import ctypes,sys; d=ctypes.CDLL(sys.argv[1]); '
                'd.registry_creation_test.argtypes=[ctypes.c_char_p]; sys.exit(d.registry_creation_test(sys.argv[2].encode()))',
                str(out/'test_registry_creation.dll'),str(args.native_image.resolve())],capture_output=True,text=True)
            (out/'registry-creation-test.txt').write_text(result.stdout+result.stderr)
            if result.returncode:print(result.stdout+result.stderr);result.check_returncode()
            print(result.stdout.rstrip().splitlines()[-1])
            subprocess.run([sys.executable,'-B',str(ROOT/'tests/test_native_registry_creation.py'),
                            '--native-output',str(out/'registry-creation-test.txt')],check=True)
            control=subprocess.run([sys.executable,'-B','-c','import ctypes,sys; ctypes.CDLL(sys.argv[1]).registry_creation_force_fail()',
                str(out/'test_registry_creation.dll')],capture_output=True,text=True)
            if control.returncode!=97:raise RuntimeError('provisional registry active-CHECK control did not fail')
        subprocess.run(common+['-shared',str(source/'test_lockstep_creation_load.c'),'-lbcrypt','-lws2_32',
                              '-o',str(out/'test_creation_load.dll')],check=True)
        if args.native_image:
            result=subprocess.run([sys.executable,'-B',str(ROOT/'tests/test_native_creation_load.py'),'--native-dll',
                str(out/'test_creation_load.dll'),str(args.native_image.resolve())],capture_output=True,text=True)
            (out/'creation-load-test.txt').write_text(result.stdout+result.stderr)
            if result.returncode:print(result.stdout+result.stderr);result.check_returncode()
            print(result.stdout.rstrip().splitlines()[-1])
            control=subprocess.run([sys.executable,'-B','-c','import ctypes,sys; ctypes.CDLL(sys.argv[1]).creation_load_force_fail()',
                str(out/'test_creation_load.dll')],capture_output=True,text=True)
            if control.returncode!=97:raise RuntimeError('construction Load active-CHECK control did not fail')
        subprocess.run(common+[str(source/'test_lockstep_actor_unwind.c'), '-lbcrypt', '-lws2_32',
                              '-o', str(out/'test_actor_unwind.exe')], check=True)
        subprocess.run([str(out/'test_actor_unwind.exe')], check=True)
        subprocess.run(common+[str(source/'test_lockstep_player_origin.c'),
                              '-o', str(out/'test_player_origin.exe')], check=True)
        subprocess.run([str(out/'test_player_origin.exe')], check=True)
        subprocess.run(common+[str(source/'test_lockstep_player_getup.c'),
                              '-o', str(out/'test_player_getup.exe')], check=True)
        subprocess.run([str(out/'test_player_getup.exe')], check=True)
        subprocess.run(common+[str(source/'test_lockstep_getup_diagnostic_wire.c'),
                              '-o', str(out/'test_getup_diagnostic_wire.exe')], check=True)
        subprocess.run([str(out/'test_getup_diagnostic_wire.exe')], check=True)
        subprocess.run(common+[str(source/'test_lockstep_getup_diagnostic_transport.c'), '-lbcrypt', '-lws2_32',
                              '-o', str(out/'test_getup_diagnostic_transport.exe')], check=True)
        subprocess.run([sys.executable,str(ROOT/'tools/dev/test_getup_diagnostic_transport.py'),
                        '--exe',str(out/'test_getup_diagnostic_transport.exe'),'--out',str(out/'getup-diagnostic-transport.json')]+
                        (['--status',str(ROOT/'.local/native-lockstep/getup-diagnostic-c-status.txt')] if args.native_image else []),check=True)
        subprocess.run(common+[str(source/'test_lockstep_portable_wire.c'),
                              '-o', str(out/'test_portable_wire.exe')], check=True)
        subprocess.run([str(out/'test_portable_wire.exe')], check=True)
        portable = source/'test_lockstep_portable_windows.c'
        if portable.exists():
            subprocess.run(common+[str(portable), '-lbcrypt', '-lws2_32',
                                  '-o', str(out/'test_portable_windows.exe')], check=True)
            subprocess.run([str(out/'test_portable_windows.exe')], check=True)
        subprocess.run(common+[str(source/'test_lockstep_portable_transport.c'), '-lbcrypt', '-lws2_32',
                              '-o', str(out/'test_portable_transport.exe')], check=True)
        subprocess.run([str(out/'test_portable_transport.exe')], check=True)
        subprocess.run(common+[str(source/'test_lockstep_rotation_win.c'), '-lbcrypt', '-lws2_32',
                              '-o', str(out/'test_rotation_win.exe')], check=True)
        subprocess.run([str(out/'test_rotation_win.exe')]+([str(args.native_image.resolve())] if args.native_image else []), check=True)
        subprocess.run(common+[str(source/'test_lockstep_world_nav.c'), '-lbcrypt', '-lws2_32',
                              '-o', str(out/'test_world_nav.exe')], check=True)
        subprocess.run([str(out/'test_world_nav.exe')]+([str(args.native_image.resolve())] if args.native_image else []), check=True)
        if args.native_image:
            # The synthetic world through the Windows world hash must still print the text the
            # Linux gate is tested against (src/native/engine/linux/test_gate_world_golden.txt).
            subprocess.run(common+[str(source/'test_lockstep_world_golden.c'), '-lbcrypt', '-lws2_32',
                                  '-o', str(out/'test_world_golden.exe')], check=True)
            golden=out/'world-golden.txt'
            subprocess.run([str(out/'test_world_golden.exe'),str(args.native_image.resolve()),str(golden)],check=True)
            # A Windows checkout (core.autocrlf) turns the file's line ends into CRLF.
            if golden.read_bytes()!=(source/'linux/test_gate_world_golden.txt').read_bytes().replace(b'\r\n',b'\n'):
                raise SystemExit('Windows world hash text differs from linux/test_gate_world_golden.txt')
    print(out/'x4lan_gate_64.dll')


if __name__ == '__main__':
    main()
