"""Publish one private final config/handoff to an already verified supervisor."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import select
import sys

sys.dont_write_bytecode = True
import linux_native_portable_run as runner
import linux_portable_prepare as preparation
from native_portable import canonical_json, exact_keys


def publish(args, value):
    if os.geteuid() != 0: raise ValueError('root-owned handoff publisher required')
    root = args.root
    if (root.resolve(strict=True) != root or root.stat().st_uid != 1004 or root.stat().st_mode & 0o077
            or not (root / '.x4mp-admission-test-root').is_file()):
        raise ValueError('not an owned private test root')
    bundle = args.bundle.resolve(strict=True)
    if not bundle.is_relative_to(root): raise ValueError('handoff bundle outside private root')
    review, contract = runner.review_bundle(bundle, args.review_sha256)
    exact_keys(value, ('prepared', 'config'), 'handoff input')
    config = value['config']; receipt = value['prepared']
    runner.validate_session(config, contract, review['participant'])
    request, request_sha = preparation.read_control(root, args.prepare_request)
    preparation.validate_request(request, runner.reviewed_capability(contract), review['participant'], args.review_sha256)
    expected = {key: item for key, item in request.items() if key not in ('schema', 'run_id', 'review_sha256')}
    if any(config.get(key) != item for key, item in expected.items()): raise ValueError('prepared/final session differs')
    preparation.control_path(root, args.config, absent=True)
    coordinator_fd = preparer_fd = None
    try:
        coordinator_fd = runner.verify_coordinator(config, bundle, root)
        preparer_fd = preparation.verify_preparer(root, bundle, args.prepare_request, args.handoff,
            receipt, args.review_sha256, contract['stock_sha256'])
        preparation.publish_handoff(root, args.config, config)
        config_sha = hashlib.sha256(canonical_json(config)).hexdigest()
        handoff = {'schema': 'x4-portable-handoff-v1', 'pid': receipt['pid'], 'start_ticks': receipt['start_ticks'],
            'request_sha256': request_sha, 'config_path': str(args.config), 'config_sha256': config_sha}
        # Recheck both exact process identities after config publication and
        # before the one-shot wake-up. PIDFDs remain held throughout.
        again = runner.verify_coordinator(config, bundle, root)
        os.close(again)
        again = preparation.verify_preparer(root, bundle, args.prepare_request, args.handoff,
            receipt, args.review_sha256, contract['stock_sha256'])
        os.close(again)
        for fd in (coordinator_fd, preparer_fd):
            poll = select.poll(); poll.register(fd, select.POLLIN)
            if poll.poll(0): raise ValueError('owned handoff participant exited')
        preparation.publish_handoff(root, args.handoff, handoff)
        return {'config_sha256': config_sha, 'handoff_sha256': hashlib.sha256(canonical_json(handoff)).hexdigest(),
            'preparing_pid': receipt['pid'], 'preparing_start_ticks': receipt['start_ticks']}
    finally:
        for fd in (coordinator_fd, preparer_fd):
            if fd is not None: os.close(fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('root', 'bundle', 'prepare-request', 'handoff', 'config'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--review-sha256', required=True)
    args = parser.parse_args()
    raw = sys.stdin.buffer.read(65537)
    if not 0 < len(raw) <= 65536: raise ValueError('bounded private handoff input required')
    # Input is deliberately never printed, including on validation failure.
    result = publish(args, preparation.decode_control(raw))
    print(json.dumps(result), flush=True)


if __name__ == '__main__': main()
