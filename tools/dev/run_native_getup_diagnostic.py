"""Explicit one-native GetUp diagnostic via the unchanged released lane runner.

This is never a fallback from paired testing and cannot produce session_pass.
"""
import argparse,json,secrets,socket,subprocess,sys,threading,time
from pathlib import Path
# Load the policy itself from source, before any local dependency import.
import importlib.util
_policy_path=Path(__file__).with_name('native_getup_source_only.py')
_policy_spec=importlib.util.spec_from_file_location('x4lan_getup_source_only',_policy_path)
_policy=importlib.util.module_from_spec(_policy_spec)
_policy_bytes=_policy_path.read_bytes();exec(compile(_policy_bytes,str(_policy_path),'exec'),_policy.__dict__)
sys.modules['x4lan_getup_source_only']=_policy;_policy.remember(_policy_path,_policy_bytes);_policy.install();_policy.record_script(__file__)
from native_getup_diagnostic import Diagnostic,serve,FALSE_CLAIMS
from native_getup_diagnostic_contract import manifest,check,configured_text,file_record

ROOT=Path(__file__).resolve().parents[2]

def command(args,contract_path):
    return [sys.executable,str(ROOT/'tools/dev/native_gate_lane.py'),'--lane-tool',str(args.lane_tool.resolve()),
        '--snapshot-in',str(args.snapshot.resolve()),'--getup-diagnostic-config',str(contract_path),'--',
        'run','--agent','native-getup-diagnostic','--label','local-getup-one-native','--lane',args.lane,
        '--collect',str(args.out.resolve()/'native'),'--base',str(args.base.resolve()),'--overlay',str(args.overlay.resolve()),
        '--start','menu','--seed','276944566','--timeout',str(args.timeout),'--wait',str(args.wait),'--settle','3',
        '--until',r'NATIVE_GATE GETUP_DIAGNOSTIC_STOP .*terminal_finished=1 closed=1 restored=1 reason=1 stage=closed ',
        '--expect','NATIVE_GATE ARMED slot=p1','--expect','NATIVE_GATE PLAYER_GETUP_STOP schema=2 ',
        '--fail-on',r'NATIVE_GATE (ERROR|REFUSED)|Error while executing.*native_gate_|Error in MD cue md\.X4LAN_Native|NATIVE_GATE .*DRAIN_ERROR']

def run(args):
    args.out=args.out.resolve();args.out.mkdir(parents=True,exist_ok=False)
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
        sock.bind(('127.0.0.1',0));token=secrets.token_hex(16)
        value=manifest(token,sock.getsockname()[1],args.lane,args.lane_tool,args.base,args.overlay,args.snapshot,args.execution_receipt)
        configured_text(value);contract_path=args.out/'diagnostic-contract.json'
        with contract_path.open('x') as out:json.dump(value,out,indent=2);out.write('\n')
        d=Diagnostic(token);stop=threading.Event();report=args.out/'diagnostic-transport.json'
        worker=threading.Thread(target=serve,args=(sock,d,report,time.monotonic()+args.wait+args.timeout+180,stop),name='getup-diagnostic-coordinator')
        worker.start();code=None;failure=None
        try:
            # Released runner owns its own timeout and process cleanup. Never
            # kill its wrapper with a second timeout that could strand X4.
            code=subprocess.call(command(args,contract_path),cwd=ROOT)
            check(value,verify_files=True)
        except Exception as error:failure=repr(error)
        finally:
            stop.set();worker.join(5)
            if worker.is_alive():failure=failure or 'coordinator did not close'
        receipt=dict(schema=1,contract=value['contract'],run=value['run'],native_clients=1,runner_exit=code,
            coordinator_joined=not worker.is_alive(),failure=failure,manifest_sha256=file_record(contract_path)['sha256'],
            execution_receipt=value['execution_receipt'],**FALSE_CLAIMS)
        (args.out/'diagnostic-runner.json').write_text(json.dumps(receipt,indent=2)+'\n')
    from validate_native_getup_diagnostic import validate
    try:result=validate(args.out)
    except (OSError,ValueError,KeyError,TypeError) as error:result=dict(local_contract_valid=False,errors=[str(error)],**FALSE_CLAIMS)
    (args.out/'diagnostic-result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2));return 0 if result['local_contract_valid'] else 1

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('lane-tool','base','overlay','snapshot','execution-receipt','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--lane',choices=('b1','b2','b3','b4'),default='b4')
    p.add_argument('--timeout',type=int,default=260);p.add_argument('--wait',type=int,default=900)
    args=p.parse_args()
    if not 120<=args.timeout<=900 or not 0<=args.wait<=900:p.error('bounded native timeout120..900 and admission wait0..900 required')
    raise SystemExit(run(args))

if __name__=='__main__':main()
