"""Run the production Windows NGD transport against Python on real loopback UDP.

The executable owns synthetic engine memory; this never starts X4 or stages files.
"""
import argparse
import json
from pathlib import Path
import socket
import subprocess
import threading
import time
from native_getup_diagnostic import Diagnostic

def run(executable,case):
    d=Diagnostic('a'*32);stop=threading.Event();worker_errors=[]
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
        sock.bind(('127.0.0.1',0));sock.settimeout(.05)
        def peer():
            try:
                while not stop.is_set():
                    try:data,address=sock.recvfrom(512)
                    except socket.timeout:continue
                    replies=d.receive(data,address,time.monotonic())
                    for endpoint,packet in replies:
                        if case==6:continue
                        if case==7 and packet.startswith(b'NGDGO|'):
                            f=packet.decode().split('|');f[7]=str(int(f[7])+1);packet='|'.join(f).encode()
                        if case==8 and packet.startswith(b'NGDCLOSED|'):packet=packet[:-1]+b'1'
                        sock.sendto(packet,endpoint)
            except BaseException as error:worker_errors.append(repr(error))
        worker=threading.Thread(target=peer);worker.start()
        try:result=subprocess.run([str(executable),str(sock.getsockname()[1]),str(case)],capture_output=True,text=True,timeout=15)
        finally:stop.set();worker.join(2)
    if worker.is_alive() or worker_errors:raise AssertionError(f'peer failure {worker_errors}')
    if result.returncode:raise AssertionError(f'case{case} exit{result.returncode}: {result.stdout}\n{result.stderr}\n{json.dumps(d.summary())[:1600]}')
    if case==0 and not d.summary()['local_transport_valid']:raise AssertionError(d.summary())
    if case not in (0,8) and d.summary()['local_transport_valid']:raise AssertionError('negative produced valid local transport')
    if case==8 and 'closed=0' not in result.stdout:raise AssertionError('changed CLOSED accepted by native')
    return dict(case=case,output=result.stdout.strip(),local_transport=d.summary()['local_transport_valid'],failure=d.failed)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--status',type=Path);a=p.parse_args()
    if a.status:
        from lupa.luajit21 import LuaRuntime
        lua=LuaRuntime(unpack_returned_tuples=True);lua.globals().raw=a.status.read_text()
        lua.execute("""X4LAN_CONFIG={slot='p1',native_gate_player_probe='diagnostic-getup',native_gate_diagnostic_contract='diagnostic-getup-1',token=string.rep('a',32)}
          X4LAN_SHA256={digest=function() return '3ba3f5f43b92602683c19aee62a20342b084dd5971ddd33808d81a328879a547' end}
          gate={player_getup_diagnostic_status=function() return raw end};DebugError=function() error('unfinished status must not log a terminal') end""")
        lua.execute((Path(__file__).parent/'native_gate_getup_diagnostic.lua').read_text())
        assert lua.eval('X4LAN_NATIVE_DIAGNOSTIC.drain(gate,false)') is False
        print('Production C diagnostic formatter -> actual LuaJIT closed parser PASS (unfinished synthetic capture)')
    results=[run(a.exe.resolve(),case) for case in (0,1,2,3,4,5,6,7,8,10,11,12,13,14,15)]
    if a.out:a.out.write_text(json.dumps(results,indent=2)+'\n')
    for row in results:print(row['output'])
    print('NGD production transport/Python loopback: 15 owned-memory cases PASS; native gameplay untested')

if __name__=='__main__':main()
