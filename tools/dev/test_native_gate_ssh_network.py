"""Synthetic Windows peers against an isolated Linux SSH gate coordinator.

Exercises real cross-OS transport but does not claim native X4 or Linux game
lockstep. Session secrets and packet bytes are deliberately absent from reports.
"""
import argparse
import hashlib
import json
from pathlib import Path
import secrets
import socket
import struct
import subprocess
import sys
import time


def packet(token,slot,tick,command='00000000',digest=None,version=2):
    game=struct.pack('>d',20+tick/60).hex()
    real=struct.pack('>d',slot*10+tick/60).hex()
    digest=digest or hashlib.sha256(f'checked-state-{tick}'.encode()).hexdigest()
    result=f'NGSTATE|{version}|{token}|{slot}|{tick}|{game}|{real}|{digest}'
    if version==2:result+='|'+command
    return result.encode()


def run_case(args,name):
    out=args.out/name;out.mkdir(parents=True,exist_ok=False)
    token=secrets.token_hex(16)
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as reserve:
        reserve.bind(('127.0.0.1',0));port=reserve.getsockname()[1]
    endpoint=('127.0.0.1',port)
    report=out/'native-barrier.json'
    command=[sys.executable,str(Path(__file__).with_name('native_gate_ssh.py')),'--ssh-host',args.ssh_host,
             '--remote-script',args.remote_script,'--port',str(port),'--token',token,'--ticks','12','--relative-real',
             '--hold-tick','5','--hold-seconds','.3','--lifetime','4' if name=='missing_peer_timeout' else '35','--report',str(report)]
    runner_owned=name in ('positive_retries','rejected_packets')
    command.append('--runner-owned')
    output=(out/'bridge.log').open('w')
    process=subprocess.Popen(command,stdout=output,stderr=subprocess.STDOUT)
    sockets=[socket.socket(socket.AF_INET,socket.SOCK_DGRAM) for _ in range(3)]
    for sock in sockets:sock.bind(('127.0.0.1',0));sock.settimeout(.05)
    events=[];latencies=[]
    try:
        started=time.monotonic()
        while 'RELAY READY' not in (out/'bridge.log').read_text():
            if process.poll() is not None or time.monotonic()-started>20:raise RuntimeError('bridge readiness failed')
            time.sleep(.05)
        def receive(slot,timeout=.3):
            until=time.monotonic()+timeout
            while time.monotonic()<until:
                try:data,_=sockets[slot-1].recvfrom(1024);return data.decode().split('|')
                except socket.timeout:pass
            return None
        def send(slot,tick,**kw):sockets[slot-1].sendto(packet(token,slot,tick,**kw),endpoint)
        if name=='rejected_packets':
            send(1,0)
            sockets[2].sendto(packet(token,1,0),endpoint) # Same credential, wrong pinned source.
            sockets[1].sendto(packet('0'*32,2,0),endpoint)
            send(2,2) # Future tick cannot admit tick0.
            send(2,0,version=1) # Coordinator cannot downgrade after v2 admission.
            assert receive(1,.35) is None
            events.append('foreign_source_token_future_and_version_refused')
        if name=='changed_waiting':
            send(1,0,command='81010000');time.sleep(.15)
            send(1,0,command='83010000');send(2,0)
            assert receive(1,3)[0]=='NGSTOP' and receive(2,3)[0]=='NGSTOP'
            expected='changed_state_while_waiting_tick_0'
        elif name=='remote_disconnect':
            send(1,0,command='81010000');send(2,0)
            assert receive(1,3)[0]=='NGGO' and receive(2,3)[0]=='NGGO'
            send(1,1);assert receive(1,.2) is None
            identity=json.loads((out/'ssh-coordinator.json').read_text())
            # Interrupt only this test's exact remote process, after verifying
            # executable arguments; pidfd prevents a PID-reuse race.
            code=f'''import os,signal\nfrom pathlib import Path\npid={identity['pid']}\nfd=os.pidfd_open(pid)\nargs=Path('/proc/'+str(pid)+'/cmdline').read_bytes().split(b'\\0')\nassert args[:3]==[b'python3',{args.remote_script.encode()!r},b'--remote'], 'owned process identity changed'\nsignal.pidfd_send_signal(fd,signal.SIGTERM)\nos.close(fd)\nprint('owned coordinator stopped')\n'''
            killed=subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=12',args.ssh_host,'python3 -'],input=code,text=True,capture_output=True,timeout=15)
            assert killed.returncode==0,killed.stderr
            for slot in (1,2):
                until=time.monotonic()+5
                while True:
                    response=receive(slot,.2)
                    if response and response[0]=='NGSTOP' and response[4]=='1':break
                    if time.monotonic()>until:raise TimeoutError('transport disconnect did not stop both peers')
            process.wait(timeout=10)
            assert process.returncode!=0
            expected=None;events.append('owned_linux_coordinator_disconnect_stopped_both_at_tick1')
        elif name=='missing_peer_timeout':
            send(1,0)
            assert receive(1,.3) is None
            response=receive(1,7)
            assert response and response[0]=='NGSTOP' and response[4]=='0'
            process.wait(timeout=8);assert process.returncode!=0
            expected=None;events.append('bounded_missing_peer_timeout_stopped_waiting_peer')
        elif name=='digest_mismatch':
            send(2,0,digest='c'*64);send(1,0)
            assert receive(1,3)[0]=='NGSTOP' and receive(2,3)[0]=='NGSTOP'
            expected='battle_state_mismatch_tick_0'
        else:
            lost_stop=False
            expected=None
            for tick in range(13):
                commands=('81010000' if tick==3 else '00000000','43030000' if tick==3 else '00000000')
                begin=time.monotonic();received={};last=0;lost=False
                if tick==2:
                    send(1,tick,command=commands[0]);assert receive(1,.2) is None
                    events.append('missing_peer_did_not_advance')
                while len(received)<2:
                    now=time.monotonic()
                    if now-begin>8:raise TimeoutError(f'permit missing at {tick}')
                    if now-last>.12:
                        for slot in (2,1):send(slot,tick,command=commands[slot-1])
                        last=now
                    for slot in (1,2):
                        response=receive(slot,.05)
                        if response is None:continue
                        kind=response[0];target=int(response[4])
                        if kind=='NGGO' and target!=tick+1:continue
                        if kind=='NGSTOP' and target!=tick:continue
                        if tick==4 and slot==1 and not lost:
                            lost=True;events.append('permit_deliberately_discarded_then_retried');continue
                        if tick==12 and slot==1 and not lost_stop:
                            lost_stop=True;events.append('final_stop_discarded_then_retried');continue
                        assert response[1:4]==['2',token,str(slot)]
                        if tick<12:
                            assert kind=='NGGO' and response[5:]==list(commands)
                            if tick==3:
                                recorded=json.loads(report.read_text())
                                assert [recorded['states']['3'][str(s)]['command'] for s in (1,2)]==list(commands)
                        else:assert kind=='NGSTOP'
                        received[slot]=True
                latencies.append(time.monotonic()-begin)
                if tick==5:assert latencies[-1]>=.3
            events.extend(['ordered_commands_equal','command_checkpoint_before_permit','all_13_native_clock_vectors_equal'])
        if runner_owned:
            until=time.monotonic()+8
            while 'done=True transport_closed=True' not in (out/'bridge.log').read_text():
                assert process.poll() is None,'completed bridge exited before owning runner cleanup'
                if time.monotonic()>until:raise TimeoutError('completed transport did not close')
                time.sleep(.05)
            assert process.poll() is None
            events.append('completed_bridge_waited_for_owning_runner')
            if name=='rejected_packets':
                process.wait(timeout=35);assert process.returncode==0
                events.append('completed_bridge_finite_deadline_exited')
        elif name not in ('remote_disconnect','missing_peer_timeout'):
            process.wait(timeout=8)
            assert process.returncode==0
        until=time.monotonic()+5
        while not report.exists() and time.monotonic()<until:time.sleep(.05)
        data=json.loads(report.read_text())
        assert data['failure']==expected
        assert data['verdict']==('incomplete' if name in ('remote_disconnect','missing_peer_timeout') else 'pass' if expected is None else 'fail')
        if name in ('remote_disconnect','missing_peer_timeout'):
            assert data['transport']['aborted'] and data['common_ticks']==([0] if name=='remote_disconnect' else [])
        else:
            assert data['transport']['ssh_exit_code']==0
            log=(out/'bridge.log').read_text()
            assert ('done=True transport_closed=True' in log)==(data['verdict']=='pass')
            events.append('clean_ssh_exit_persisted_before_completion')
        summary={'case':name,'passed':True,'scope':'synthetic Windows UDP peers / actual Linux coordinator via SSH',
                 'expected_failure':expected,'common_ticks':data['common_ticks'],'events':events,
                 'roundtrip_seconds':{'min':min(latencies),'max':max(latencies)} if latencies else None,
                 'coordinator':data['transport']['coordinator'],'native_linux_simulation':False,'native_windows_game':False}
        (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
        return summary
    finally:
        for sock in sockets:sock.close()
        if process.poll() is None:
            process.terminate();process.wait(timeout=10)
        output.close()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ssh-host',required=True);parser.add_argument('--remote-script',required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();results=[]
    for name in ('positive_retries','rejected_packets','changed_waiting','digest_mismatch','remote_disconnect','missing_peer_timeout'):
        result=run_case(args,name);results.append(result)
        print(name,'PASS',flush=True)
    (args.out/'summary.json').write_text(json.dumps(results,indent=2)+'\n')


if __name__=='__main__':main()
