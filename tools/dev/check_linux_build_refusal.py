"""Run owned non-game executables with a candidate preload; never starts X4."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import select
import subprocess
import sys
import tempfile

from linux_load_observer_probe import AgentEvidence


def start_ticks(pid):
    return int(Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()[19])


def check(agent):
    if sys.platform != 'linux': raise RuntimeError('Linux owned helpers required')
    agent = Path(agent).resolve(strict=True)
    # The claim used for parser replay is explicitly synthetic: this harness is
    # not a verified X4 participant. Helper PID/start/size joins below are real.
    claim = (os.getpid(), start_ticks(os.getpid()))
    executable = Path(sys.executable).resolve(strict=True)
    if not 0 < executable.stat().st_size < 60459280: raise ValueError('helper must be a small non-game executable')
    env = {key: value for key, value in os.environ.items() if not key.startswith('X4MP_') and key != 'LD_PRELOAD'}
    children = []
    with tempfile.TemporaryDirectory(prefix='x4-attributed-refusal-') as directory:
        log = Path(directory) / 'agent.log'
        env.update(LD_PRELOAD=str(agent), X4MP_AGENT_LOG=str(log))
        disabled = subprocess.run([str(executable), '-B', '-c', 'print("disabled helper")'],
                                  env=env, capture_output=True, timeout=10)
        if disabled.returncode or disabled.stderr or log.exists(): raise ValueError('default-off helper changed state')
        env['X4MP_AGENT'] = '1'
        source = '''import os,pathlib,time,sys
p=pathlib.Path(os.environ['X4MP_AGENT_LOG']);needle=' pid='+str(os.getpid())+' start_ticks='
deadline=time.monotonic()+5
while time.monotonic()<deadline:
 if p.exists() and needle in p.read_text(): break
 time.sleep(.01)
else: raise RuntimeError('own attributed refusal missing')
assert sys.stdin.buffer.read(1)==b'x'
'''
        try:
            identities = {}
            for _ in range(12):
                child = subprocess.Popen([str(executable), '-B', '-c', source], env=env,
                                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                # Retain a pidfd while independently reading this owned process.
                children.append((child, None))
                fd = os.pidfd_open(child.pid); children[-1] = (child, fd)
                identities[child.pid] = (start_ticks(child.pid), Path(f'/proc/{child.pid}/exe').stat().st_size)
            for child, fd in children:
                stdout, stderr = child.communicate(b'x', timeout=10)
                if child.returncode or stdout or stderr: raise ValueError('nongame helper failed')
                poll = select.poll(); poll.register(fd, select.POLLIN)
                if not poll.poll(0): raise ValueError('owned helper exit not observed')
            lines = log.read_text().splitlines()
            refusals = [line for line in lines if 'X4MP_AGENT REFUSED ' in line]
            if len(refusals) != 12 or any('VERIFIED' in line or 'INSTALLED' in line for line in lines):
                raise ValueError('helper verification scope changed')
            seen = set()
            for line in refusals:
                match = re.fullmatch(r'[0-9]+\.[0-9]{3} X4MP_AGENT REFUSED schema=x4-build-refusal-v1 '
                    r'pid=([1-9][0-9]*) start_ticks=([1-9][0-9]*) reason=executable_size '
                    r'executable_bytes=([1-9][0-9]*) pinned_bytes=60459280', line)
                if not match: raise ValueError('interleaved or malformed refusal')
                pid, start, size = map(int, match.groups())
                if pid in seen or identities.get(pid) != (start, size): raise ValueError('helper identity join differs')
                seen.add(pid)
            for before in (True, False):
                evidence = AgentEvidence()
                row = f'X4MP_AGENT VERIFIED build=23660954 version=9.00 (611726) sites=25 pid={claim[0]}'
                ordered = ([row] + refusals) if before else (refusals + [row])
                for line in ordered: evidence.observe(line, *claim)
                if not evidence.verified(claim[0]) or len(evidence.attributed_build_refusals) != 12:
                    raise ValueError('synthetic parser replay refused exact helpers')
            return {'native_x4': False, 'synthetic_native_verification_for_parser_replay': True,
                    'actual_owned_preload_helpers': 12, 'pidfd_exits_verified': 12,
                    'helper_pid_start_size_joins': True, 'shared_log_records_complete': True,
                    'default_off': True, 'agent_sha256': hashlib.sha256(agent.read_bytes()).hexdigest()}
        finally:
            for child, fd in children:
                if child.poll() is None:
                    child.terminate()
                    try: child.wait(timeout=5)
                    except subprocess.TimeoutExpired: child.kill(); child.wait(timeout=5)
                if fd is not None: os.close(fd)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('--agent', required=True, type=Path)
    print(json.dumps(check(parser.parse_args().agent), sort_keys=True))
