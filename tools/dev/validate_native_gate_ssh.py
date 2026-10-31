"""Add mandatory Linux transport provenance to the ordinary native validator."""
import argparse
import importlib.util
import json
from pathlib import Path, PureWindowsPath


def transport_errors(result, barrier, expected):
    errors = []
    server = result.get('server', {})
    policy = server.get('authorized_transport_exception', {})
    wanted = expected['policy']
    for key in ('kind', 'remote_ipv4', 'remote_tcp_port', 'command_sha256'):
        if policy.get(key) != wanted[key]:errors.append('SSH policy differs: '+key)
    if PureWindowsPath(policy.get('executable', '')) != PureWindowsPath(wanted['executable']):
        errors.append('SSH executable differs')
    identity = policy.get('identity')
    valid_identity = (isinstance(identity, list) and len(identity) == 3
                      and all(type(n) is int and n > 0 for n in identity)
                      and identity[2] == server.get('pid'))
    if not valid_identity:errors.append('SSH owned process identity missing or invalid')
    audit = policy.get('audit')
    if not isinstance(audit, list) or not audit or any(not isinstance(check, list) or not check for check in audit):
        errors.append('SSH authorized endpoint audit missing or empty')
    elif valid_identity:
        for check in audit:
            for endpoint in check:
                if (endpoint.get('proto') != 'tcp' or endpoint.get('remote') != wanted['remote_ipv4']
                    or endpoint.get('rport') != 22 or endpoint.get('pid') != identity[0]
                    or endpoint.get('created') != identity[1]
                    or endpoint.get('state') not in ('SYN_SENT', 'ESTABLISHED', 'CLOSE_WAIT', 'FIN_WAIT1', 'FIN_WAIT2', 'LAST_ACK', 'CLOSING', 'TIME_WAIT')):
                    errors.append('SSH audit contains an unauthorized endpoint or process identity')
    transport = barrier.get('transport', {})
    coordinator = transport.get('coordinator', {})
    if (transport.get('kind') != 'ssh_byte_preserving' or transport.get('native_linux_simulation') is not False
        or transport.get('aborted') or transport.get('ssh_exit_code') != 0):
        errors.append('SSH transport did not complete cleanly in its declared scope')
    if (not coordinator.get('platform', '').startswith('Linux')
        or coordinator.get('hashes') != expected['source_hashes']
        or type(coordinator.get('pid')) is not int or coordinator['pid'] <= 0):
        errors.append('Linux coordinator source/platform/process provenance differs')
    traffic = transport.get('traffic', {})
    if any(type(traffic.get(k)) is not int or traffic[k] <= 0
           for k in ('input_datagrams', 'output_datagrams', 'bytes_to_linux', 'bytes_from_linux')):
        errors.append('SSH bidirectional native packet evidence missing')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('--manifest', type=Path, required=True, help='trusted source/policy manifest frozen before launch')
    parser.add_argument('--native-validator', type=Path, required=True, help='ordinary full native validator for this fixture')
    parser.add_argument('--scope', choices=('clock', 'battle'), required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('native_validator', args.native_validator)
    module = importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    result = module.validate(args.run, args.scope)
    errors = transport_errors(json.loads((args.run/'result.json').read_text()),
                              json.loads((args.run/'native-barrier.json').read_text()),
                              json.loads(args.manifest.read_text()))
    result['ssh_transport_valid'] = not errors
    result['native_linux_simulation'] = False
    result['errors'].extend(errors)
    result['session_pass'] = result['session_pass'] and not errors
    result['evidence_valid'] = result['evidence_valid'] and not errors
    args.out.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({key:result[key] for key in ('session_pass', 'evidence_valid', 'ssh_transport_valid',
                     'native_linux_simulation', 'ticks_observed', 'errors')}, indent=2))
    raise SystemExit(0 if result['session_pass'] else 1)


if __name__ == '__main__':main()
