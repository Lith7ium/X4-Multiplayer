"""Run two disposable native X4 gates through the released lane runner."""
import argparse
from pathlib import Path
import subprocess
import sys


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--lane-tool', type=Path, required=True)
    p.add_argument('--base', type=Path, required=True, help='frozen inert extension')
    p.add_argument('--overlay', type=Path, required=True)
    p.add_argument('--snapshot', type=Path)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--ticks', type=int, default=120)
    p.add_argument('--timeout', type=int, default=360)
    p.add_argument('--wait', type=int, default=180, help='seconds to queue for safe lane admission')
    p.add_argument('--live-inputs', action='store_true', help='enable loopback tactical inputs for a live-plan overlay')
    p.add_argument('--ssh-host', help='opt-in isolated Linux coordinator over authenticated SSH')
    p.add_argument('--ssh-remote-script', help='absolute remote path to the identical native_gate_ssh.py')
    p.add_argument('--tick-hz', type=int, default=60, choices=(60, 40, 30, 20),
                   help="the overlay's native_gate_tick_hz: the relay's timestep and relative-real checks")
    args = p.parse_args()
    if not 12 <= args.ticks <= 18000:
        p.error('ticks must be 12..18000')
    root = Path(__file__).resolve().parents[2]
    server_args = ['{python}', str(root/'tools/dev/native_gate_relay.py'),
                                     '--port', '{port}', '--token', '{token}', '--ticks', str(args.ticks),
                                     '--report', '{outdir}/native-barrier.json']
    if args.snapshot:
        server_args.append('--relative-real')
    if args.tick_hz != 60:
        server_args += ['--tick-hz', str(args.tick_hz)]
    if bool(args.ssh_host) != bool(args.ssh_remote_script):
        p.error('--ssh-host and --ssh-remote-script must be supplied together')
    if args.ssh_host:
        if not args.snapshot:p.error('SSH coordinator requires an explicit snapshot session')
        if args.live_inputs:p.error('SSH coordinator does not expose the local tactical input inbox')
        server_args[1] = str(root/'tools/dev/native_gate_ssh.py')
        server_args += ['--ssh-host',args.ssh_host,'--remote-script',args.ssh_remote_script,'--lifetime',str(min(3600,args.timeout+120)), '--runner-owned']
    if args.live_inputs:
        config = (args.overlay/'ui/config.lua.append').read_text()
        if 'native_gate_command_plan = "live"' not in config:
            p.error('--live-inputs requires an overlay with --command-plan live')
        server_args.append('--live-inputs')
    server = subprocess.list2cmdline(server_args)
    cmd = [sys.executable, str(args.lane_tool.resolve()), 'session', '--agent', 'native-lockstep',
           '--label', 'native-gate', '--clients', '2', '--timeout', str(args.timeout), '--wait', str(args.wait),
           '--settle', '3', '--base', str(args.base.resolve()), '--overlay', str(args.overlay.resolve()),
           '--gamestart', 'custom_creative', '--seed', '276944566', '--build', 'nativegate1-test',
           '--bubble', 'native-gate-test', '--server-cmd', server, '--server-cwd', str(root),
           '--server-until', 'done=True', '--server-fail-on', 'Traceback|failure=(?!none)[a-z]',
           '--until', 'NATIVE_GATE STOP .*reason=1 restored=1', '--until-mode', 'all',
           '--expect', 'NATIVE_GATE ARMED',
           '--fail-on', r'Error while executing.*native_(?:gate|construction|management)_[a-z_]+|Error in MD cue md\.X4LAN_Native(?:Gate|Commands|Management|Construction)|NATIVE_GATE (ERROR|REFUSED)|NATIVE_GATE STOP .*reason=(?!1 )[0-9]',
           '--collect', str(args.out.resolve())]
    if args.snapshot:
        wrapper = [sys.executable, str(root/'tools/dev/native_gate_lane.py'), '--lane-tool', str(args.lane_tool.resolve()),
                   '--snapshot-in', str(args.snapshot.resolve())]
        if args.ssh_host:wrapper+=['--ssh-host',args.ssh_host,'--ssh-remote-script',args.ssh_remote_script]
        cmd = wrapper+['--']+cmd[2:]+['--start','menu']
    raise SystemExit(subprocess.call(cmd, cwd=root))


if __name__ == '__main__':
    main()
