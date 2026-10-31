"""Controller half of the opt-in one-shot Linux-first owner channel.

The caller already owns both supervisors. This module starts no game or server;
its only subprocess operation fetches a bounded read-only Linux receipt over the
caller's reviewed SSH command. Any fault leaves the Windows gate closed.
"""
import ctypes
from ctypes import wintypes as wt
import json
import os
from pathlib import Path
import subprocess
import time

import native_portable_prelaunch as binding
from native_portable import canonical_json


def owned_runner(process):
    from native_portable_windows_identity import WindowsAPI, stamp
    api=WindowsAPI();handle=int(process._handle)
    if process.poll() is not None or api.kernel.WaitForSingleObject(handle,0)!=258 or api.kernel.GetProcessId(handle)!=process.pid:
        raise ValueError('prelaunch Windows runner no longer owned and alive')
    fields=[wt.FILETIME() for _ in range(4)]
    api.require(api.kernel.GetProcessTimes(handle,*(ctypes.byref(field) for field in fields)),
                'prelaunch owned runner creation unavailable')
    return dict(pid=process.pid,created=stamp(fields[0]))


def read_local(path, maximum=binding.MAX_MESSAGE_BYTES):
    path=Path(path)
    if path.is_symlink() or path.resolve(strict=True)!=path:raise ValueError('linked prelaunch controller input')
    with path.open('rb') as stream:
        before=os.fstat(stream.fileno());raw=stream.read(maximum+1);after=os.fstat(stream.fileno())
        if (before.st_dev,before.st_ino,before.st_size,before.st_mtime_ns)!=(after.st_dev,after.st_ino,after.st_size,after.st_mtime_ns):
            raise ValueError('prelaunch controller input changed')
    return binding.decode(raw,maximum),raw


def live(process,linux):
    if process.poll() is not None:raise RuntimeError('Windows runner exited before its native launch grant')
    if linux.poll() is not None:raise RuntimeError('Linux supervisor exited before the Windows native launch grant')


def request_config(*,out,windows,process,linux,contract,participants,prepared,remote_script,deadline):
    """Wait for the runner-created challenge, then derive the exact handoff."""
    path=windows/'portable-prelaunch-request.json'
    while not path.exists():
        live(process,linux)
        if time.monotonic()>deadline:raise TimeoutError('bounded Windows prelaunch challenge wait expired')
        time.sleep(.05)
    live(process,linux)
    request,request_raw=read_local(path,binding.MAX_RECEIPT_BYTES)
    session,session_raw=read_local(windows/'portable-session.json',binding.MAX_RECEIPT_BYTES)
    ready,ready_raw=read_local(windows/'portable-barrier-remote.json',binding.MAX_RECEIPT_BYTES)
    saved,saved_raw=read_local(out/'linux-prepared.json',binding.MAX_RECEIPT_BYTES)
    binding.validate_wait(request,contract,participants,session_config=session,ready=ready)
    if (request_raw!=canonical_json(request) or request['runner']!=owned_runner(process) or saved!=prepared
            or request['linux_prepared']!=prepared or request['session_config_raw_sha256']!=binding.sha(session_raw)
            or request['coordinator_ready_raw_sha256']!=binding.sha(ready_raw)
            or request['linux_prepared_raw_sha256']!=binding.sha(saved_raw)
            or time.monotonic_ns()>=request['deadline_monotonic_ns']):
        raise ValueError('prelaunch controller/runner/source binding differs')
    # The owned runner creates the session/client IDs in this one-shot request;
    # final result.json is written only at shutdown, never assumed available now.
    return binding.linux_config(session,ready,remote_script,request)


def release_windows(*,plan,out,windows,process,linux,config,contract,review,remote_command,case_from_log):
    request=config['prelaunch_request'];heartbeat=0
    grant_path=out/'windows-prelaunch-grant.json'
    if grant_path.exists():raise ValueError('prelaunch owner grant is one-shot')
    while time.monotonic_ns()<request['deadline_monotonic_ns']:
        live(process,linux)
        text=(out/'linux-runner.log').read_bytes()
        if len(text)>1024*1024:raise ValueError('prelaunch Linux runner output exceeds bound')
        text=text.decode('utf-8',errors='strict')
        if not any(line.startswith('OWNED_NATIVE_READY ') for line in text.splitlines()):
            if time.monotonic()>heartbeat:
                print('MIXED_WAIT Linux_native_claim_before_Windows_game',flush=True);heartbeat=time.monotonic()+30
            time.sleep(.1);continue
        case=case_from_log(text,plan['remote_root'])
        started=time.monotonic_ns()
        remaining=(request['deadline_monotonic_ns']-started)/1_000_000_000
        if remaining<=0:break
        command=remote_command(['python3','-B',plan['linux_bundle']+'/bin/linux_portable_prelaunch.py',
            '--root',plan['remote_root'],'--case',case,'--bundle',plan['linux_bundle'],
            '--config',plan['remote_config'],'--review-sha256',plan['linux_review_sha256']])
        fetched=subprocess.run(command,capture_output=True,timeout=min(8,remaining))
        live(process,linux)
        if fetched.returncode or fetched.stderr or len(fetched.stdout)>binding.MAX_MESSAGE_BYTES:
            raise ValueError('owned read-only Linux prelaunch fetch refused')
        envelope=binding.decode(fetched.stdout)
        if envelope==dict(schema=binding.FETCH_SCHEMA,ready=False):
            if time.monotonic()>heartbeat:
                print('MIXED_WAIT Linux_joined_main_frame0_before_Windows_game',flush=True);heartbeat=time.monotonic()+30
            time.sleep(.25);continue
        grant=dict(schema='x4-portable-launch-grant-v1',request_sha256=binding.object_sha(request),
                   fetch_started_monotonic_ns=started,fetch=envelope)
        receipt=binding.validate_grant(grant,request,config,contract,review,time.monotonic_ns())
        if (receipt['case']!=case or receipt['bundle']!=plan['linux_bundle']
                or request['runner']!=owned_runner(process)):
            raise ValueError('prelaunch actual case/bundle/Windows generation differs')
        with (out/'linux-prelaunch-fetch.json').open('xb') as stream:stream.write(fetched.stdout)
        wire=canonical_json(grant)+b'\n'
        with grant_path.open('xb') as stream:stream.write(wire);stream.flush();os.fsync(stream.fileno())
        live(process,linux)
        binding.validate_grant(grant,request,config,contract,review,time.monotonic_ns())
        process.stdin.write(wire);process.stdin.flush()
        print('MIXED_WINDOWS_NATIVE_GRANTED linux_frame0='+receipt['frame0_sha256'],flush=True)
        # The caller must keep this pipe open through all actual launch checks.
        return grant
    raise TimeoutError('absolute prelaunch startup deadline expired')


def collect_rejection(log, run, agent_sha, destination):
    """Diagnostic evidence remains separate and can never make a session pass."""
    from native_portable_rotation_rejection import evidence
    try:value=evidence(log.splitlines(),run,agent_sha)
    except Exception as error:value=dict(acceptance=False,error=type(error).__name__+': '+str(error))
    if value.get('acceptance') is not False:raise ValueError('rejection diagnostic claimed acceptance')
    Path(destination).write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')
    return value
