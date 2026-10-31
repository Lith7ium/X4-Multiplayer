"""Runner/manifest policy on owned files, with no game launch or staging."""
from copy import deepcopy
import hashlib,importlib.util,json,shutil,sys,tempfile,types,unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/dev'))
import native_getup_diagnostic_contract as contract
import validate_native_getup_diagnostic as validator
import run_native_getup_diagnostic as runner
import native_gate_lane as wrapper
import native_getup_diagnostic_execution as execution
from test_native_gate_getup_diagnostic_evidence import fixture
import test_native_gate_getup_diagnostic_evidence as evidence_fixture


class DiagnosticRunnerTests(unittest.TestCase):
    def setUp(self):
        policy=sys.modules['x4lan_getup_source_only'];old_bound,old_roots,old_failure=policy.bound,policy.bound_roots,policy.failure
        policy.bound=None;policy.bound_roots=[];policy.failure=None
        self.addCleanup(lambda:setattr(policy,'failure',old_failure))
        self.addCleanup(lambda:setattr(policy,'bound',old_bound));self.addCleanup(lambda:setattr(policy,'bound_roots',old_roots))
        config=fixture()[1]
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.path=Path(self.temp.name)
        self.snapshot=self.path/'save.xml.gz';self.snapshot.write_bytes(b'owned synthetic snapshot pin')
        self.pin=hashlib.sha256(self.snapshot.read_bytes()).hexdigest()
        # Pin substitution is only the owned filesystem policy test. Native and
        # production validator constants remain the real pristine snapshot.
        for target,key in ((contract,'SNAPSHOT'),(validator,'SNAPSHOT')):
            p=patch.object(target,key,self.pin);p.start();self.addCleanup(p.stop)
        p=patch.dict(contract.FIXED,snapshot_sha=self.pin);p.start();self.addCleanup(p.stop)
        self.source_root=self.path/'frozen-source';self.lane_root=self.path/'release'
        for root,names in ((self.source_root/'tools/dev',execution.REQUIRED_SOURCE),(self.lane_root,execution.REQUIRED_LANE|{'dev/lane/x4lane_autostart/content.xml'})):
            for name in names:
                path=root/name;path.parent.mkdir(parents=True,exist_ok=True)
                original=ROOT/'tools/dev'/name
                if root==self.source_root/'tools/dev':path.write_bytes(original.read_bytes())
                else:path.write_text('# owned source fixture, never executed')
        # Model a frozen module inventory using byte-identical source copies.
        # Actual source-loader execution/caches are exercised separately.
        aliases={}
        for name in execution.REQUIRED_SOURCE:
            module=sys.modules.get(name[:-3])
            if module is not None:
                copied=types.ModuleType(name[:-3]);copied.__dict__.update(module.__dict__)
                copied.__file__=str(self.source_root/'tools/dev'/name);aliases[name[:-3]]=copied
                policy.compiled[str(Path(copied.__file__).resolve())]=hashlib.sha256(Path(copied.__file__).read_bytes()).hexdigest()
        p=patch.dict(sys.modules,aliases);p.start();self.addCleanup(p.stop)
        def forget_owned_compilation():
            for path in list(policy.compiled):
                if Path(path).is_relative_to(self.path):del policy.compiled[path]
        self.addCleanup(forget_owned_compilation)
        self.tool=self.lane_root/'dev/x4lane.py'
        self.x4lock=self.path/'x4lock.exe';self.x4lock.write_bytes(b'owned helper, never executed')
        self.execution_path=self.path/'execution.json';self.execution_path.write_text(json.dumps(execution.capture(self.source_root,self.lane_root,self.x4lock)))
        p=patch.object(execution,'CURRENT_ROOT',self.source_root);p.start();self.addCleanup(p.stop)
        self.base=self.path/'base';self.base.mkdir();(self.base/'config.lua').write_text('enabled=false')
        self.overlay=self.path/'overlay';(self.overlay/'ui').mkdir(parents=True);(self.overlay/'native').mkdir()
        (self.overlay/'native/x4lan_gate_64.dll').write_bytes(b'owned fake DLL, never loaded')
        # fixture reads its imported original SNAPSHOT, so replace that too.
        config=config.replace('7816351056e6040d92ae9a4619ae2e15a577fa26a19134f11e20746317e61aa8',self.pin)
        (self.overlay/'ui/config.lua.append').write_text(config)
        self.value=contract.manifest('a'*32,42000,'b4',self.tool,self.base,self.overlay,self.snapshot,self.execution_path)
        self.args=types.SimpleNamespace(lane_tool=self.tool,base=self.base,overlay=self.overlay,snapshot=self.snapshot,execution_receipt=self.execution_path,out=self.path/'out',lane='b4',timeout=260,wait=900)
        full=runner.command(self.args,self.path/'diagnostic-contract.json');self.lane_args=full[full.index('--')+1:]+['--repin-profile']

    def test_separate_run_command_has_no_pair_or_direct_game_path(self):
        cmd=runner.command(self.args,self.path/'contract.json')
        self.assertIn('--getup-diagnostic-config',cmd);self.assertIn('run',cmd)
        for forbidden in ('session','--clients','--server-cmd','--live-inputs','--lua'):self.assertNotIn(forbidden,cmd)
        self.assertEqual(cmd[cmd.index('--collect')+1],str((self.args.out/'native').resolve()))
        self.assertEqual(cmd[cmd.index('--lane')+1],'b4')

    def test_closed_manifest_and_changed_inputs_refuse_before_policy_install(self):
        self.assertEqual(contract.configured_text(self.value).count('slot = "p1"'),1)
        for field,value in [('native_clients',2),('physical_slot',2),('session_pass',True),('port',True),('host','0.0.0.0'),('run','b'*64),('lane','protected')]:
            item=deepcopy(self.value);item[field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):contract.check(item,True)
        item=deepcopy(self.value);item['extra']=1
        with self.assertRaises(ValueError):contract.check(item)
        (self.overlay/'native/x4lan_gate_64.dll').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError,'input changed'):contract.check(self.value,True)

    def test_queue_evidence_manifest_exactly_joins_the_immutable_config(self):
        config=self.overlay/'ui/config.lua.append'
        old=config.read_text();self.assertNotIn('queue_evidence',self.value)
        # Adding only a manifest marker cannot select an unconfigured stream.
        with self.assertRaisesRegex(ValueError,'queue evidence'):contract.configured_text(dict(self.value,queue_evidence=1))
        config.write_text(old+'\nnative_gate_player_transition_queue_evidence = true\n')
        value=contract.manifest('a'*32,42000,'b4',self.tool,self.base,self.overlay,self.snapshot,self.execution_path)
        self.assertEqual(value['queue_evidence'],1);contract.configured_text(value)
        missing=deepcopy(value);del missing['queue_evidence']
        with self.assertRaisesRegex(ValueError,'queue evidence'):contract.configured_text(missing)
        for marker in (True,False,0,2,'1'):
            with self.subTest(marker=marker),self.assertRaises(ValueError):contract.check(dict(value,queue_evidence=marker))
        for flag in ('false','1','true\nnative_gate_player_transition_queue_evidence = true'):
            config.write_text(old+'\nnative_gate_player_transition_queue_evidence = '+flag+'\n')
            with self.assertRaises(ValueError):contract.manifest('a'*32,42000,'b4',self.tool,self.base,self.overlay,self.snapshot,self.execution_path)

    def test_narrow_loopback_seam_preserves_prepare_and_owned_dll_proof(self):
        calls=[];outer=self
        installed=self.path/'prepared';(installed/'native').mkdir(parents=True);(installed/'ui').mkdir()
        shutil.copyfile(self.overlay/'native/x4lan_gate_64.dll',installed/'native/x4lan_gate_64.dll')
        (installed/'ui/config.lua').write_text(contract.configured_text(self.value))
        paths=types.SimpleNamespace(repo=self.lane_root,autostart_source=self.lane_root/'dev/lane/x4lane_autostart',x4lock_exe=self.x4lock)
        class Run:
            def __init__(self,*args):self.paths=paths;self.network='offline';self.extra_tokens={};self.result={};self.lane=types.SimpleNamespace(lab=installed);calls.append('init')
            def prepare(self):calls.append(('prepare',self.network));self.result['released_prepare']=True
        def original_lock(current,build=True):self.assertIs(build,False);return current.x4lock_exe
        lane=types.SimpleNamespace(Run=Run,ROOT=self.lane_root,ensure_x4lock=original_lock,
            core=types.SimpleNamespace(__file__=str(self.lane_root/'dev/lane/lanecore.py'),Paths=types.SimpleNamespace(from_env=lambda root:paths)))
        contract.install_lane(lane,self.value,self.lane_args,self.snapshot)
        instance=Run();instance.prepare()
        self.assertEqual(lane.ensure_x4lock(paths),self.x4lock)
        self.assertEqual(calls,['init',('prepare','loopback')]);self.assertTrue(instance.result['released_prepare'])
        self.assertEqual(instance.extra_tokens,{'@X4LANE_PORT@':'42000','@X4LANE_TOKEN@':'a'*32,'@X4LANE_SLOT@':'p1'})
        self.assertEqual(instance.result['getup_diagnostic']['installed_gate_sha256'],hashlib.sha256(b'owned fake DLL, never loaded').hexdigest())
        (installed/'ui/config.lua').write_text(contract.configured_text(self.value).replace('world_trace_ticks = 2','world_trace_ticks = 12'))
        with self.assertRaisesRegex(ValueError,'differs from immutable overlay'):instance.prepare()

    def test_extra_payload_sessions_and_overlay_replacement_are_refused(self):
        for change in ('session','extra','overlay','slot','start'):
            args=list(self.lane_args);item=deepcopy(self.value)
            if change=='session':args[0]='session'
            if change=='extra':args+=['--lua','danger=true']
            if change=='overlay':args[args.index('--overlay')+1]=str(self.base)
            if change=='slot':item['physical_slot']=2
            if change=='start':args[args.index('--start')+1]='module'
            with self.subTest(change=change),self.assertRaises(ValueError):contract.install_lane(None,item,args,self.snapshot)

    def test_changed_lane_tool_refuses_before_its_import_executes(self):
        sentinel=self.path/'import-sentinel'
        self.tool.write_text('from pathlib import Path\nPath('+repr(str(sentinel))+').write_text("must not execute")\n')
        path=self.path/'contract.json';path.write_text(json.dumps(self.value))
        argv=runner.command(self.args,path)[1:]
        with patch.object(sys,'argv',argv),self.assertRaisesRegex(ValueError,'input changed: lane_tool'):wrapper.main()
        self.assertFalse(sentinel.exists())

    def test_complete_filesystem_receipt_and_missing_cleanup_have_explicit_failed_result(self):
        with patch.object(evidence_fixture,'SNAPSHOT',self.pin):log,config,transport,lane,_=fixture()
        run=self.path/'run';installed=run/'native/installed';installed.mkdir(parents=True)
        # A harmless base p1 default is not a second physical proposal. The
        # explicit appended p1 assignment still has to occur exactly once.
        config='X4LAN_CONFIG = {slot = "p1", token = ""}\n'+config
        config_path=installed/'x4_lan_lab-config.lua';config_path.write_text(config)
        (run/'native/engine-output.log').write_text(log)
        (run/'diagnostic-transport.json').write_text(json.dumps(transport))
        contract_path=run/'diagnostic-contract.json';contract_path.write_text(json.dumps(self.value))
        lane.update(profile='45856',lane='b4',install={'sha256':{'ui/config.lua':contract.file_record(config_path)['sha256']}},
            native_snapshot=dict(mode='load',slot='save_009',restored=True,staged={k:self.value['inputs']['snapshot'][k] for k in ('sha256','bytes')}))
        lane['getup_diagnostic']['installed_gate_sha256']=self.value['inputs']['overlay']['files']['native/x4lan_gate_64.dll']['sha256']
        lane_path=run/'native/result.json';lane_path.write_text(json.dumps(lane))
        receipt=dict(schema=1,contract=self.value['contract'],run=self.value['run'],native_clients=1,runner_exit=0,coordinator_joined=True,
            failure=None,manifest_sha256=contract.file_record(contract_path)['sha256'],execution_receipt=self.value['execution_receipt'],**runner.FALSE_CLAIMS)
        (run/'diagnostic-runner.json').write_text(json.dumps(receipt))
        result=validator.validate(run);self.assertEqual(result['errors'],[]);self.assertTrue(result['local_contract_valid'])
        json.dumps(result)  # Actual command result must remain serializable.
        # A self-consistent changed installed hash/raw footer cannot replace the
        # intended immutable observer window. This is the review counterexample.
        changed=config.replace('world_trace_ticks = 2','world_trace_ticks = 12');config_path.write_text(changed)
        (run/'native/engine-output.log').write_text(log.replace('last=2 capacity=2048','last=12 capacity=2048'))
        changed_lane=deepcopy(lane);changed_lane['install']['sha256']['ui/config.lua']=contract.file_record(config_path)['sha256']
        lane_path.write_text(json.dumps(changed_lane));result=validator.validate(run)
        self.assertFalse(result['local_contract_valid']);self.assertIn('immutable overlay',result['errors'][0])
        config_path.write_text(config);(run/'native/engine-output.log').write_text(log);lane_path.write_text(json.dumps(lane))
        for field,value in [('path',str(self.path/'other-execution.json')),('sha256','0'*64)]:
            changed_receipt=deepcopy(receipt);changed_receipt['execution_receipt'][field]=value
            (run/'diagnostic-runner.json').write_text(json.dumps(changed_receipt))
            self.assertFalse(validator.validate(run)['local_contract_valid'])
        (run/'diagnostic-runner.json').write_text(json.dumps(receipt))
        for field,value in [('profile','45841'),('native_snapshot',{}),('lane_restored',False)]:
            broken=dict(lane);broken[field]=value;lane_path.write_text(json.dumps(broken))
            self.assertFalse(validator.validate(run)['local_contract_valid'])
        lane_path.write_text(json.dumps(lane));(run/'diagnostic-runner.json').unlink()
        result=validator.validate(run);self.assertFalse(result['local_contract_valid']);self.assertTrue(result['errors'])
        for key in runner.FALSE_CLAIMS:self.assertIs(result[key],False)

if __name__=='__main__':unittest.main()
