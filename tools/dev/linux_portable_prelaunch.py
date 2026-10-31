"""Owned Linux startup observation/publication and read-only receipt fetch.

Only capture() uses the existing probe's retained read-only process reader.
The fetch command never opens process memory and never starts or signals a game.
"""
import argparse
from contextlib import ExitStack
import copy
import os
from pathlib import Path, PurePosixPath
import select
import stat
import sys
import time

sys.dont_write_bytecode = True
import native_portable_prelaunch as binding
import linux_portable_prepare as preparation
from native_portable import canonical_json


def probe_arguments(case, bundle, review_sha, agent_sha, config):
    case, bundle = PurePosixPath(case), PurePosixPath(bundle)
    return ['python3', '-B', str(bundle / 'bin/linux_portable_probe.py'),
        '--claim', str(case / 'claim.json'), '--agent-so', str(bundle / 'bin/libx4mp_portable.so'),
        '--agent-sha256', agent_sha, '--agent-log', str(case / 'agent.log'), '--review-sha256', review_sha,
        '--world-log', str(case / 'home/.config/EgoSoft/X4/45841/native-probe.log'),
        '--ready-file', str(case / 'ready-request'), '--run-id', case.name,
        '--report', str(case / 'probe.json'), '--contract', str(bundle / 'contract.json'),
        '--config', str(config), '--disarm-file', str(case / 'disarm')]


def command_bytes(arguments):
    return b'\0'.join(os.fsencode(item) for item in arguments) + b'\0'


def read_owned(path, uid=0, maximum=binding.MAX_RECEIPT_BYTES):
    """Bounded stable read; every parent and the target must resolve canonically."""
    path = Path(path)
    preparation.control_path(path.parent, path)
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(fd, 'rb') as stream:
        before = os.fstat(stream.fileno())
        if (not stat.S_ISREG(before.st_mode) or before.st_uid != uid or before.st_nlink != 1
                or before.st_mode & 0o077 or not 0 < before.st_size <= maximum):
            raise ValueError('prelaunch file ownership/mode/size differs')
        raw = stream.read(maximum + 1)
        if (preparation.identity(before) != preparation.identity(os.fstat(stream.fileno()))
                or preparation.identity(before) != preparation.identity(path.lstat())):
            raise ValueError('prelaunch file changed during read')
    return binding.decode(raw, maximum), raw


def process_identity(pid):
    data = Path(f'/proc/{pid}/stat').read_text(); fields = data[data.rfind(')') + 2:].split()
    uid = int(next(row for row in Path(f'/proc/{pid}/status').read_text().splitlines()
                   if row.startswith('Uid:')).split()[1])
    image = Path(f'/proc/{pid}/exe').stat()
    return dict(pid=pid, start_ticks=int(fields[19]), uid=uid, parent_pid=int(fields[1]),
        executable_identity=[image.st_dev, image.st_ino, image.st_size],
        argv_sha256=binding.sha(Path(f'/proc/{pid}/cmdline').read_bytes()))


def supervisor_command(root, bundle, config_path, config, contract, review_sha):
    prepared = config['prelaunch_request']['linux_prepared']
    raw = Path(f'/proc/{prepared["pid"]}/cmdline').read_bytes()
    argv = raw.rstrip(b'\0').split(b'\0')
    if len(argv) != 13: raise ValueError('prelaunch supervisor command shape differs')
    request_path, handoff_path = Path(os.fsdecode(argv[10])), Path(os.fsdecode(argv[12]))
    for path in (request_path, handoff_path): preparation.control_path(root, path)
    expected = command_bytes(['python3', '-B', str(bundle / 'bin/linux_native_portable_run.py'),
        '--root', str(root), '--bundle', str(bundle), '--review-sha256', review_sha,
        '--prepare-request', str(request_path), '--handoff', str(handoff_path)])
    if raw != expected: raise ValueError('prelaunch supervisor command differs')
    request, request_raw = read_owned(request_path)
    if binding.sha(request_raw) != prepared['request_sha256']: raise ValueError('prelaunch preparation request changed')
    from linux_native_portable_run import reviewed_capability
    preparation.validate_request(request, reviewed_capability(contract), config['participants']['2'], review_sha)
    expected_session = {key: item for key, item in request.items() if key not in ('schema', 'run_id', 'review_sha256')}
    if any(config.get(key) != item for key, item in expected_session.items()):
        raise ValueError('prelaunch supervisor prepared another session')
    handoff, _ = read_owned(handoff_path)
    if handoff != dict(schema='x4-portable-handoff-v1', pid=prepared['pid'], start_ticks=prepared['start_ticks'],
            request_sha256=prepared['request_sha256'], config_path=str(config_path), config_sha256=binding.object_sha(config)):
        raise ValueError('prelaunch supervisor handoff differs')
    return binding.sha(expected)


class LiveIdentity:
    """A retained PIDFD plus exact initial identity; an exec cannot reset it."""
    def __init__(self, pid, start, uid, *, executable=None, argv_sha=None, parent=None):
        self.fd = os.pidfd_open(pid)
        try:
            self.poll = select.poll(); self.poll.register(self.fd, select.POLLIN)
            self.value = process_identity(pid)
            if (self.value['start_ticks'] != start or self.value['uid'] != uid
                    or executable is not None and self.value['executable_identity'] != executable
                    or argv_sha is not None and self.value['argv_sha256'] != argv_sha
                    or parent is not None and self.value['parent_pid'] != parent):
                raise ValueError('prelaunch process generation/image/command differs')
            self.verify()
        except Exception:
            os.close(self.fd); raise

    def verify(self):
        if self.poll.poll(0) or process_identity(self.value['pid']) != self.value:
            raise ValueError('prelaunch owned process exited or changed')
        return self.value

    def close(self): os.close(self.fd)
    def __enter__(self): return self
    def __exit__(self, *_): self.close()


def probe_identity(args, review, config, contract):
    expected = command_bytes(probe_arguments(args.claim.parent, args.agent_so.parent.parent,
        args.review_sha256, args.agent_sha256, args.config))
    value = process_identity(os.getpid()); prepared = config['prelaunch_request']['linux_prepared']
    if (value['uid'] != 0 or value['argv_sha256'] != binding.sha(expected)
            or value['parent_pid'] != prepared['pid']
            or process_identity(prepared['pid'])['start_ticks'] != prepared['start_ticks']):
        raise ValueError('prelaunch producer is not the owned supervisor child')
    source = binding.sha(Path(__file__).with_name('linux_portable_probe.py').read_bytes())
    if source != review['files']['bin/linux_portable_probe.py']:
        raise ValueError('prelaunch producer source changed')
    supervisor_command(args.claim.parent.parent.parent, args.agent_so.parent.parent,
                       args.config, config, contract, args.review_sha256)
    value.update(parent_start_ticks=prepared['start_ticks'], source_sha256=source)
    return value


def capture(*, args, config, contract, review, process, coordinator, provenance, request, installed,
            native, load, rotation, loader, first_endpoint, bridge_identity, agent_log, world_log):
    """Return None while evidence is incomplete; faults never become readiness."""
    import linux_portable_probe as probe
    if (request is None or installed is None or first_endpoint is None or not provenance.verified(process.pid)
            or not probe.prelaunch_status(native) or not probe.observer.provisional_status(load or {})
            or 'LOAD_REQUEST' not in loader.markers): return None
    manager = process.rotation_manager(); table = process.word(probe.observer.VBI_GLOBAL)
    regions = process.rotation_restoration()
    if not rotation.prelaunch(native, load, table, manager, regions): return None
    # Appends may be completed by the next ordinary parsing pass. Rewrites,
    # replacements, malformed/fatal records and oversized files are never deferred.
    try:
        cutoff = dict(agent=agent_log.require_drained(), world=world_log.require_drained())
    except ValueError as error:
        if str(error) in ('terminal_log_unterminated_record', 'terminal_log_unread_bytes',
                          'terminal_log_changed_during_cutoff'): return None
        raise
    coordinator.verify(); process.verify()
    report, report_raw = read_owned(config['coordinator']['report_path'])
    frame_sha, source = binding.first_linux_report(report, contract, config['participants'], ticks=config['ticks'])
    endpoint = process.endpoint(source, config['coordinator']['port'])
    bridge_path = args.claim.parent / 'extensions/x4_lan_lab/native/x4mp_load_probe.so'
    bridge = process.bridge_mapping(bridge_path, review['files']['fixture/native/x4mp_load_probe.so'])
    mapping = process.verify_agent_mapping()
    clocks_before = process.clocks()
    if endpoint != first_endpoint or bridge != bridge_identity:
        raise ValueError('prelaunch native socket or bridge changed')
    producer = probe_identity(args, review, config, contract)
    # Rehash the small Python closure; native library identity is independently
    # mapped and pinned by the existing retained reader, never reattached here.
    bundle = args.agent_so.parent.parent
    sources = {key: value for key, value in review['files'].items() if key.startswith('bin/') and key.endswith('.py')}
    if any(binding.sha((bundle / name).read_bytes()) != digest for name, digest in sources.items()):
        raise ValueError('prelaunch producer dependency changed')
    clocks_after = process.clocks()
    if (manager != process.rotation_manager() or table != process.word(probe.observer.VBI_GLOBAL)
            or regions != process.rotation_restoration() or mapping != process.verify_agent_mapping()
            or endpoint != process.endpoint(source, config['coordinator']['port'])
            or bridge != process.bridge_mapping(bridge_path, review['files']['fixture/native/x4mp_load_probe.so'])
            or producer != probe_identity(args, review, config, contract)):
        raise ValueError('prelaunch observation changed')
    coordinator.verify(); process.verify()
    if read_owned(config['coordinator']['report_path'])[1] != report_raw:
        raise ValueError('prelaunch coordinator report changed')
    try:
        final_cutoff = dict(agent=agent_log.require_drained(), world=world_log.require_drained())
    except ValueError as error:
        if str(error) in ('terminal_log_unterminated_record', 'terminal_log_unread_bytes',
                          'terminal_log_changed_during_cutoff'): return None
        raise
    if cutoff != final_cutoff: raise ValueError('prelaunch parsed log cutoff changed')
    for row in cutoff.values(): row['scope'] = 'observed startup cutoff; later appends are not covered'
    wait = config['prelaunch_request']
    value = dict(schema=binding.RECEIPT_SCHEMA, scope='Linux joined-main startup boundary only', native_pair_verified=False,
        request=wait, request_raw_sha256=binding.object_sha(wait), request_canonical_sha256=binding.object_sha(wait),
        linux_config_raw_sha256=binding.object_sha(config), linux_config_canonical_sha256=binding.object_sha(config),
        review_sha256=args.review_sha256, contract_sha256=config['contract_sha256'], participants=config['participants'],
        run=args.run_id, case=str(args.claim.parent), bundle=str(bundle),
        claim=dict(pid=process.pid, start_ticks=process.start, uid=process.uid), producer=producer,
        native_executable_identity=list(process.executable_identity), agent_mapping=mapping, bridge_mapping=bridge,
        python_sources=sources, coordinator_lifetime=copy.deepcopy(coordinator.evidence), coordinator_report=report,
        coordinator_report_raw_sha256=binding.sha(report_raw), coordinator_report_canonical_sha256=binding.object_sha(report),
        frame0_sha256=frame_sha, native_endpoint_first=first_endpoint, native_endpoint_current=endpoint,
        native_status=native, load_status=load, rotation_status=rotation.latest, rotation_regions=regions,
        rotation_table=table, rotation_manager=manager, clocks_before=clocks_before, clocks_after=clocks_after,
        ready_request=request, installed=installed, markers=loader.markers, log_cutoff=cutoff,
        observed_monotonic_ns=time.monotonic_ns())
    binding.validate_receipt(value, config, contract, review)
    # The handoff configuration is canonical and token-bearing; never echo it.
    if read_owned(args.config)[1] != canonical_json(config): raise ValueError('prelaunch private config changed')
    preparation.publish_handoff(args.claim.parent, args.claim.parent / 'prelaunch.json', value)
    return dict(path=str(args.claim.parent / 'prelaunch.json'), raw_sha256=binding.object_sha(value),
        canonical_sha256=binding.object_sha(value), observed_monotonic_ns=value['observed_monotonic_ns'],
        frame0_sha256=frame_sha, native_pair_verified=False)


def owned_paths(root, case, bundle, config):
    for path in (root, case, bundle, config.parent):
        if not path.is_absolute() or path.resolve(strict=True) != path or path.stat().st_mode & 0o077:
            raise ValueError('prelaunch noncanonical or public owned path')
    if (case.parent != root / 'cases' or not case.name.startswith('portable-native-')
            or not bundle.is_relative_to(root) or config.parent != root or case.stat().st_uid != 1004
            or root.stat().st_uid != 1004): raise ValueError('prelaunch path outside owned test namespace')


def verify_mappings(pid, case, bundle, receipt, review):
    with Path(f'/proc/{pid}/maps').open('rb') as stream: raw = stream.read(32 * 1024 * 1024 + 1)
    if len(raw) > 32 * 1024 * 1024: raise ValueError('prelaunch native mappings exceed bound')
    lines = [line.split(maxsplit=5) for line in raw.decode().splitlines()]
    for key, path, expected in (
        ('agent_mapping', bundle / 'bin/libx4mp_portable.so', review['participant']['agent_sha256']),
        ('bridge_mapping', case / 'extensions/x4_lan_lab/native/x4mp_load_probe.so',
         review['files']['fixture/native/x4mp_load_probe.so'])):
        before = path.lstat()
        if path.resolve(strict=True) != path or not stat.S_ISREG(before.st_mode) or binding.sha(path.read_bytes()) != expected:
            raise ValueError('prelaunch mapped artifact bytes changed')
        row = receipt[key]
        if any(row.get(name) != value for name, value in (('device',before.st_dev),('inode',before.st_ino),('bytes',before.st_size))):
            raise ValueError('prelaunch mapped artifact file identity changed')
        ranges = []
        for fields in lines:
            if len(fields) != 6 or fields[1] != 'r-xp': continue
            major, minor = (int(part,16) for part in fields[3].split(':'))
            if os.makedev(major,minor) != before.st_dev or int(fields[4]) != before.st_ino: continue
            if key == 'agent_mapping':
                if fields[5] != str(path): raise ValueError('prelaunch agent mapping path differs')
                ranges.append(fields[0])
            else: ranges.append(dict(path=fields[5],range=fields[0]))
        if ranges != row['executable_ranges'] or preparation.identity(before) != preparation.identity(path.lstat()):
            raise ValueError('prelaunch executable mapping changed')


def fetch(root, case, bundle, config_path, review_sha):
    """Read-only fetch with retained process generations and socket joins."""
    import linux_native_portable_run as runner
    import linux_portable_probe as probe
    owned_paths(root, case, bundle, config_path)
    review, contract = runner.review_bundle(bundle, review_sha)
    config, config_raw = read_owned(config_path)
    runner.validate_session(config, contract, review['participant'])
    if config_raw != canonical_json(config): raise ValueError('prelaunch configuration is not canonical')
    try: receipt, receipt_raw = read_owned(case / 'prelaunch.json')
    except FileNotFoundError: return dict(schema=binding.FETCH_SCHEMA, ready=False)
    binding.validate_receipt(receipt, config, contract, review)
    if (receipt_raw != canonical_json(receipt) or receipt['case'] != str(case) or receipt['bundle'] != str(bundle)
            or receipt['review_sha256'] != review_sha): raise ValueError('prelaunch receipt source path or bytes differ')
    prepared = config['prelaunch_request']['linux_prepared']; producer = receipt['producer']; claim = receipt['claim']
    supervisor_argv_sha = supervisor_command(root, bundle, config_path, config, contract, review_sha)
    argv = command_bytes(probe_arguments(case, bundle, review_sha, review['participant']['agent_sha256'], config_path))
    if producer['argv_sha256'] != binding.sha(argv): raise ValueError('prelaunch producer argv differs')
    py = Path(sys.executable).stat(); python_id = [py.st_dev, py.st_ino, py.st_size]
    with ExitStack() as stack:
        supervisor = stack.enter_context(LiveIdentity(prepared['pid'], prepared['start_ticks'], 0,
            executable=python_id, argv_sha=supervisor_argv_sha))
        producer_live = stack.enter_context(LiveIdentity(producer['pid'], producer['start_ticks'], 0,
            executable=producer['executable_identity'], argv_sha=binding.sha(argv), parent=prepared['pid']))
        if producer['executable_identity'] != python_id: raise ValueError('prelaunch producer interpreter differs')
        native = stack.enter_context(LiveIdentity(claim['pid'], claim['start_ticks'], claim['uid'],
            executable=receipt['native_executable_identity']))
        cofd = runner.verify_coordinator(config, bundle, root); stack.callback(os.close, cofd)
        coordinator = probe.CoordinatorLifetime(cofd, config)
        def verify():
            supervisor.verify(); producer_live.verify(); native.verify(); coordinator.verify()
            if os.path.lexists(case / 'disarm') or os.path.lexists(case / 'probe.json'):
                raise ValueError('prelaunch probe cancelled or already finished')
            verify_mappings(claim['pid'], case, bundle, receipt, review)
            if coordinator.socket != receipt['coordinator_lifetime']['endpoint']:
                raise ValueError('prelaunch coordinator endpoint changed')
            # Use the existing fd/inode join without constructing a reader or
            # opening /proc/PID/mem. Its endpoint method only calls verify().
            adapter = type('SocketOwner', (), {})()
            adapter.pid, adapter.start, adapter.uid, adapter.verify = claim['pid'], claim['start_ticks'], claim['uid'], native.verify
            actual = probe.PortableProcess.endpoint(adapter, receipt['native_endpoint_current']['source'], config['coordinator']['port'])
            if actual != receipt['native_endpoint_current']: raise ValueError('prelaunch native endpoint changed')
        verify()
        report, report_raw = read_owned(config['coordinator']['report_path'])
        if (report != receipt['coordinator_report'] or binding.sha(report_raw) != receipt['coordinator_report_raw_sha256']
                or read_owned(case / 'prelaunch.json')[1] != receipt_raw or read_owned(config_path)[1] != config_raw):
            raise ValueError('prelaunch receipt/config/report changed')
        if runner.review_bundle(bundle, review_sha) != (review, contract): raise ValueError('prelaunch source closure changed')
        if supervisor_command(root, bundle, config_path, config, contract, review_sha) != supervisor_argv_sha:
            raise ValueError('prelaunch supervisor preparation changed')
        verify(); now = time.monotonic_ns(); age = binding.receipt_age(receipt['observed_monotonic_ns'], now)
        return dict(schema=binding.FETCH_SCHEMA, ready=True, receipt=receipt,
            receipt_raw_sha256=binding.sha(receipt_raw), receipt_canonical_sha256=binding.object_sha(receipt),
            report_raw_hex=report_raw.hex(), fetched_monotonic_ns=now, remote_age_ns=age,
            generations_verified=True, sockets_verified=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('root', 'case', 'bundle', 'config'): parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--review-sha256', required=True); args = parser.parse_args()
    value = fetch(args.root, args.case, args.bundle, args.config, args.review_sha256)
    raw = canonical_json(value)
    if len(raw) > binding.MAX_MESSAGE_BYTES: raise ValueError('prelaunch fetch envelope too large')
    print(raw.decode('ascii'), flush=True)


if __name__ == '__main__': main()
