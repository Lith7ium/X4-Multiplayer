"""Add an owned native snapshot slot to the released lane runner, in this process only.

The released runner retains admission, locks, launch, process ownership and cleanup.
No release file is edited. Never stages a save in a protected or ordinary profile.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import sys

if any(arg=='--getup-diagnostic-config' or arg.startswith('--getup-diagnostic-config=') for arg in sys.argv[1:]):
    _policy_path=Path(__file__).with_name('native_getup_source_only.py')
    _policy_spec=importlib.util.spec_from_file_location('x4lan_getup_source_only',_policy_path)
    _policy=importlib.util.module_from_spec(_policy_spec)
    _policy_bytes=_policy_path.read_bytes();exec(compile(_policy_bytes,str(_policy_path),'exec'),_policy.__dict__)
    sys.modules['x4lan_getup_source_only']=_policy;_policy.remember(_policy_path,_policy_bytes);_policy.install();_policy.record_script(__file__)

PROFILES = {'45850', '45851', '45853', '45854', '45855', '45856', '45857', '45858'}
SLOT = 'save_009'


def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


class SnapshotSlot:
    def __init__(self, profile, profile_dir, core):
        self.root, self.core = Path(profile_dir), core
        if profile not in PROFILES or self.root.name != profile or core.is_link(self.root):
            raise ValueError('snapshot requires a disposable lane profile')
        self.directory = self.root/'save'
        core.assert_inside(self.directory, self.root)
        if core.is_link(self.directory):
            raise ValueError('snapshot save directory is linked')
        self.directory.mkdir(exist_ok=True)
        if list(self.directory.glob(SLOT+'*')):
            raise ValueError('snapshot slot already exists; refusing overwrite')
        self.archived = {}

    def stage(self, source):
        source = Path(source).resolve()
        if source.suffixes[-2:] != ['.xml', '.gz']:
            raise ValueError('snapshot must be a native compressed .xml.gz save')
        before = digest(source)
        target = self.directory/(SLOT+'.xml.gz')
        self.core.assert_inside(target, self.root)
        # Exclusive create: never replace a slot that appeared after admission.
        with source.open('rb') as inp, target.open('xb') as out:
            shutil.copyfileobj(inp, out)
        if digest(target) != before or digest(source) != before:
            raise ValueError('snapshot changed during staging')
        return {'sha256': before, 'bytes': target.stat().st_size}

    def collect(self, outdir):
        outdir = Path(outdir)/'native-snapshot'
        outdir.mkdir(exist_ok=True)
        files = []
        for name in (SLOT+'.xml.gz', SLOT+'.xml'):
            source = self.directory/name
            if not source.exists():
                continue
            self.core.assert_inside(source, self.root)
            self.core.check_tree_unlinked(self.directory)
            target = outdir/name
            with source.open('rb') as inp, target.open('xb') as out:
                shutil.copyfileobj(inp, out)
            sha = digest(source)
            if digest(target) != sha:
                raise ValueError('snapshot archive failed verification')
            self.archived[source] = sha
            files.append({'file': name, 'sha256': sha, 'bytes': source.stat().st_size})
        (outdir/'manifest.json').write_text(json.dumps(files, indent=2)+'\n')
        return files

    def restore(self):
        for source, sha in self.archived.items():
            if not source.exists():
                continue
            if digest(source) != sha:
                raise ValueError('owned snapshot changed after collection; preserving it')
            self.core.safe_unlink(source, self.root)
        remaining = list(self.directory.glob(SLOT+'*'))
        if remaining:
            raise ValueError('unarchived snapshot output remains; preserving it')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--lane-tool', type=Path, required=True)
    mode = p.add_mutually_exclusive_group(required=True)
    mode.add_argument('--capture-snapshot', action='store_true')
    mode.add_argument('--snapshot-in', type=Path)
    p.add_argument('--ssh-host')
    p.add_argument('--ssh-remote-script')
    p.add_argument('--getup-diagnostic-config',type=Path,help='explicit closed one-native NGD manifest; never a paired fallback')
    p.add_argument('lane_args', nargs=argparse.REMAINDER)
    args = p.parse_args()
    if args.getup_diagnostic_config and 'x4lan_getup_source_only' not in sys.modules:p.error('diagnostic requires the explicit --getup-diagnostic-config option')
    lane_args = args.lane_args[1:] if args.lane_args[:1] == ['--'] else args.lane_args
    if not lane_args or lane_args[0] not in ('run', 'session'):
        p.error('only run/session are supported')
    if args.capture_snapshot and lane_args[0] != 'run':
        p.error('capture uses exactly one disposable lane')
    if '--no-repin' in lane_args:
        p.error('snapshot tests require lane profile repinning')
    if lane_args[0] == 'run' and '--repin-profile' not in lane_args:
        lane_args.append('--repin-profile')
    diagnostic=None
    if args.getup_diagnostic_config:
        if not args.snapshot_in or args.ssh_host or args.ssh_remote_script:p.error('diagnostic requires local snapshot run')
        from native_getup_diagnostic_contract import check,install_lane,file_record
        diagnostic=json.loads(args.getup_diagnostic_config.read_text())
        check(diagnostic,verify_files=True)
        if file_record(args.lane_tool)!=diagnostic['inputs']['lane_tool']:p.error('diagnostic lane tool identity differs')
    spec = importlib.util.spec_from_file_location('native_snapshot_x4lane', args.lane_tool.resolve())
    lane = importlib.util.module_from_spec(spec)
    if diagnostic is not None:
        policy=sys.modules.get('x4lan_getup_source_only')
        if policy is None:raise ValueError('diagnostic source policy absent')
        policy.execute(lane,args.lane_tool.resolve(),diagnostic['inputs']['lane_tool']['sha256'])
    else:spec.loader.exec_module(lane)
    if diagnostic is not None:
        install_lane(lane,diagnostic,lane_args,args.snapshot_in)
    if bool(args.ssh_host)!=bool(args.ssh_remote_script):p.error('both explicit SSH transport arguments are required')
    if args.ssh_host:
        if not args.snapshot_in or lane_args[0]!='session':p.error('SSH transport allowance requires a snapshot session')
        from native_gate_ssh_policy import install
        install(lane,args.ssh_host,args.ssh_remote_script)
    prepare, collect, restore = lane.Run.prepare, lane.Run.collect, lane.Run.restore

    def prepare_with_snapshot(run):
        prepare(run)
        run.native_snapshot = SnapshotSlot(run.lane.profile, run.lane.profile_dir, lane.core)
        run.result['native_snapshot'] = {'slot': SLOT, 'mode': 'load' if args.snapshot_in else 'capture'}
        if args.snapshot_in:
            config = (run.lane.lab/'ui/config.lua').read_text(encoding='utf-8')
            if 'native_gate_snapshot_sha = "'+digest(args.snapshot_in)+'"' not in config:
                raise ValueError('overlay snapshot identity differs from staged bytes')
            run.result['native_snapshot']['staged'] = run.native_snapshot.stage(args.snapshot_in)
            # Only this run's disposable settings are changed. The snapshot UI
            # loads the reserved slot; the lane retains timeout/PID cleanup.
            settings_path = run.lane.extensions/lane.core.AUTOSTART_ID/'ui/settings.lua'
            settings = settings_path.read_text(encoding='utf-8')
            if 'enabled = true' not in settings:
                raise ValueError('unexpected lane autostart settings')
            settings = settings.replace('enabled = true', 'enabled = false', 1)
            lane.core.safe_write(settings_path, settings, run.lane.dir)
            shutil.copy2(settings_path, run.outdir/'installed/autostart-settings.lua')

    def collect_snapshot(run):
        collect(run)
        slot = getattr(run, 'native_snapshot', None)
        if slot:
            if run.proc is not None and run.proc.poll() is None:
                raise ValueError('cannot collect a live snapshot writer')
            run.result['native_snapshot']['files'] = slot.collect(run.outdir)

    def restore_snapshot(run):
        slot = getattr(run, 'native_snapshot', None)
        if slot:
            if run.proc is not None and run.proc.poll() is None:
                raise ValueError('cannot restore a live snapshot slot')
            slot.restore()
            run.result['native_snapshot']['restored'] = True
        restore(run)

    lane.Run.prepare, lane.Run.collect, lane.Run.restore = prepare_with_snapshot, collect_snapshot, restore_snapshot
    raise SystemExit(lane.main(lane_args))


if __name__ == '__main__':
    main()
