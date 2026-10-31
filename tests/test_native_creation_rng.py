from pathlib import Path
import copy
import hashlib
import json
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/dev'))
import native_creation_rng as rng
import native_creation_rng_evidence as evidence
import native_creation_evidence as creation
import test_native_creation_trace_v2 as fixture
from build_native_management_overlay import compose


def config():
    values={f'native_gate_{name}':bool(rng.MASK&(1<<bit)) for bit,name in enumerate(rng.OPTION_FIELDS)}
    values.update(native_gate_creation_rng_schema=1,native_gate_creation_rng_manifest=rng.manifest(),
                  native_gate_creation_trace_schema=2,native_gate_construction_test=True,
                  native_gate_snapshot_sha=rng.SNAPSHOT,native_gate_construction_plan_sha=rng.PLAN,
                  native_gate_snapshot_fixture=list(rng.FIXTURE))
    return values


def render_config(values=None):
    def value(v):
        if isinstance(v,bool):return str(v).lower()
        if isinstance(v,str):return repr(v)
        if isinstance(v,list):return '{'+','.join(map(repr,v))+'}'
        return str(v)
    return '\n'.join('X4LAN_CONFIG.'+k+' = '+value(v) for k,v in (config() if values is None else values).items())


def log_fixture():
    rows,status=fixture.fixture()
    seed=rng.parse_manifest(rng.manifest())['key_b']
    old1,_=fixture.creation.native_float_state(1001,0x3f000000)
    old2,_=fixture.creation.native_float_state(old1,0x40000000)
    new1,result1=fixture.creation.native_float_state(seed,0x3f000000)
    new2,result2=fixture.creation.native_float_state(new1,0x40000000)
    mapping={1001:seed,old1:new1,old2:new2}
    for index,record in enumerate(rows):
        if index<5:continue
        for part in (2,3):
            if not(index==5 and part==2):record[part]['rng']=mapping[record[part]['rng']] if index<=10 else 1001
        if record[0]['kind']==7:
            record[0]['result']=result1 if index==7 else result2
            for field in ('rng_before','rng_after'):record[17][field]=mapping[record[17][field]]
    trace_log=fixture.combined_fixture().split('NATIVE_GATE CREATION_TRACE ',1)[0]+fixture.render(rows,status)
    trace=creation.evidence(trace_log,required=2)
    assert trace['a_valid'] and trace['b_valid']
    parsed=rng.parse_manifest(rng.manifest())
    records=[]
    for scope,kind in ((1,1),(2,4)):
        r=next(r for r in trace['records'] if r['kind']==kind)
        common=dict(schema=1,scope=scope,part=0,creation=r['index'],epoch=1,handle=parsed['handle'],tick=6,
                    thread=r['before']['thread'],phase1=r['phase1'],enter=r['enter'],leave=r['leave'],error=0,
                    admitted=1,original=1,normal=1,closed=1,restored=1,stop_overlap=0)
        records.append(dict(common,key=parsed['key_a' if scope==1 else 'key_b'],scoped_after=r['after']['rng']))
        common['part']=1
        records.append(dict(common,tls=r['before']['tls'],address=r['before']['rng_pointer'],time=r['before']['time'],
                            caller=r['before']['rng'],restored_word=r['before']['rng'],allocation=0x40000,region=0x40000,
                            region_size=4096,protection=4,type=0x20000,child_before=[0]*12,child_after=[0]*12))
    footer=dict(schema=1,configured=1,closing=1,count=2,active=0,attempted=3,failures=0,restore_failures=0,
                restore_open=0,epoch=1,options=rng.MASK,seed_a=parsed['key_a'],seed_b=parsed['key_b'])
    receipt=rng.manifest()+f';seed_a={parsed["key_a"]:016x};seed_b={parsed["key_b"]:016x}'
    return trace_log,trace,records,footer,receipt


def render_log(data):
    trace_log,_,records,footer,receipt=data
    def fields(row):return ' '.join(k+'='+(','.join(f'{n:016x}' for n in v) if isinstance(v,list) else f'{v:x}' if k in evidence.HEX else str(v)) for k,v in row.items())
    return trace_log+'NATIVE_GATE CREATION_RNG_CONFIG '+receipt+'\n'+''.join('NATIVE_GATE CREATION_RNG '+fields(r)+'\n' for r in records)+\
        'NATIVE_GATE CREATION_RNG_STOP '+fields(footer)+'\n'


class ConstructionRng(unittest.TestCase):
    def test_composer_is_opt_in_and_refuses_before_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            base=Path(directory)/'base';(base/'ui').mkdir(parents=True);(base/'native').mkdir()
            values=config()
            for name in ('native_gate_creation_rng_schema','native_gate_creation_rng_manifest','native_gate_creation_trace_schema',
                         'native_gate_construction_test','native_gate_construction_plan_sha'):del values[name]
            values.update(native_gate_command_test=True,native_gate_order_state=True,native_gate_registry_origin_schema=1,
                          native_gate_registry_origin_identity_schema=1)
            (base/'ui/config.lua.append').write_text(render_config(values))
            (base/'ui/native_gate_probe.lua').write_bytes((ROOT/'tools/dev/native_gate_probe.lua').read_bytes())
            (base/'ui.xml.files').write_text('ui/native_gate_probe.lua\n')
            # Capability markers are fixture data only; no executable is built
            # or installed by this composer unit test.
            (base/'native/x4lan_gate_64.dll').write_bytes(b'creation_trace_config creation_trace_status creation_trace_row CREATION_TRACE_2;handle= '
                b'creation_rng_config creation_rng_status creation_rng_row creation_rng_ack_status CREATION_RNG_1;epoch=')
            def seal():
                payload={str(p.relative_to(base)):hashlib.sha256(p.read_bytes()).hexdigest() for p in base.rglob('*') if p.is_file() and p.name!='x4lane-manifest.json'}
                (base/'x4lane-manifest.json').write_text(json.dumps(payload));return payload
            original=seal();output=Path(directory)/'with_rng'
            compose(base,output,'construction',True,1)
            self.assertEqual(rng.bootstrap_preimage((output/'ui/config.lua.append').read_text()),rng.BOOTSTRAP+rng.manifest()+rng.ACK_SUFFIX)
            self.assertTrue((output/'ui/native_creation_rng.lua').exists());self.assertEqual(seal(),original)
            natural=Path(directory)/'natural';compose(base,natural,'construction',True)
            self.assertFalse((natural/'ui/native_creation_rng.lua').exists())
            self.assertIsNone(rng.bootstrap_plan((natural/'ui/config.lua.append').read_text()))
            for key in ('native_gate_npc_diag','native_gate_player_request_observer','native_gate_docking_shape_rng'):
                bad=dict(values);bad[key]=True;(base/'ui/config.lua.append').write_text(render_config(bad));seal()
                rejected=Path(directory)/key
                with self.subTest(option=key),self.assertRaisesRegex(ValueError,'unsupported option mask'):compose(base,rejected,'construction',True,1)
                self.assertFalse(rejected.exists())

    def test_vectors(self):
        for epoch,a,b in ((1,'8b34a17d4ae84a2f','5311c417920e6e36'),(2,'c647f830596c5aee','7fffcd27bc36f0c6'),
                          (4294967295,'b599ea48d5f7d320','3ff1cf5c69b2c218')):
            with self.subTest(epoch=epoch):
                parsed=rng.parse_manifest(rng.manifest(epoch))
                self.assertEqual((f'{parsed["key_a"]:016x}',f'{parsed["key_b"]:016x}'),(a,b))
        self.assertEqual(hashlib.sha256(rng.bootstrap_preimage(render_config()).encode()).hexdigest(),
                         'a18716e46b5bb2e320ad8551515acd59a1d0eb2344f4b3871e9cd052d74f3201')

    def test_every_fixed_field_and_epoch_grammar(self):
        for text in ('0','01','-1','+1','1.0','1 ','4294967296','1;epoch=1','1\x00','1\n'):
            with self.subTest(text=text),self.assertRaises(ValueError):rng.parse_manifest(rng.PREFIX+text+rng.SUFFIX)
        for i in range(len(rng.PREFIX)+1,len(rng.manifest())):
            text=rng.manifest();text=text[:i]+chr(ord(text[i])^1)+text[i+1:]
            with self.subTest(offset=i),self.assertRaises(ValueError):rng.parse_manifest(text)

    def test_explicit_unchanged_config(self):
        self.assertIsNone(rng.bootstrap_plan('X4LAN_CONFIG.native_gate_creation_trace_schema = 2'))
        values=config()
        for key in tuple(values):
            changed=copy.deepcopy(values);del changed[key]
            if key.startswith('native_gate_') and key[12:] in rng.OPTION_FIELDS and values[key] is False:continue
            with self.subTest(key=key),self.assertRaises(ValueError):rng.bootstrap_plan(render_config(changed))
        for bit,name in enumerate(rng.OPTION_FIELDS):
            changed=config();changed['native_gate_'+name]=not changed['native_gate_'+name]
            with self.subTest(bit=bit),self.assertRaises(ValueError):rng.bootstrap_plan(render_config(changed))
        for name in ('native_gate_player_probe','native_gate_management_test','native_gate_world_wait_rng_manifest',
                     'native_gate_world_find_rng_manifest','native_gate_player_origin_token','native_gate_player_launch_rng',
                     'native_gate_world_action_manifest','native_gate_world_values','native_gate_player_transition_observer','native_gate_npc_pilot','native_gate_world_trace_early'):
            with self.subTest(name=name),self.assertRaises(ValueError):rng.bootstrap_plan(render_config()+'\nX4LAN_CONFIG.'+name+' = true')
        for name,value in config().items():
            with self.subTest(duplicate=name),self.assertRaises(ValueError):rng.bootstrap_plan(render_config()+'\n'+render_config({name:value}))

    def test_lua_keys_bootstrap_and_abort_resumption(self):
        from lupa.luajit21 import LuaRuntime
        lua=LuaRuntime(unpack_returned_tuples=True)
        module=lua.execute((ROOT/'tools/dev/native_creation_rng.lua').read_text())
        for epoch in (1,2,4294967295):
            parsed=rng.parse_manifest(rng.manifest(epoch))
            self.assertEqual(module.parse(rng.manifest(epoch)),(epoch,f'{parsed["key_a"]:016x}',f'{parsed["key_b"]:016x}'))
        cfg=lua.table_from(config());cfg.native_gate_snapshot_fixture=lua.table_from(rng.FIXTURE)
        cfg.native_gate_creation_rng_ack_schema=1
        preimage=module.preimage(cfg,rng.BOOTSTRAP[:-1]);self.assertEqual(preimage,rng.bootstrap_preimage(render_config())+rng.ACK_SUFFIX)
        for name in rng.OPTION_FIELDS:
            field='native_gate_'+name;prior=cfg[field];cfg[field]=not prior
            with self.subTest(lua_mask=name),self.assertRaises(Exception):module.preimage(cfg,rng.BOOTSTRAP[:-1])
            cfg[field]=prior
        cfg.native_gate_player_probe=False
        with self.assertRaises(Exception):module.preimage(cfg,rng.BOOTSTRAP[:-1])
        cfg.native_gate_player_probe=None
        lua.globals().module=module;lua.globals().cfg=cfg;lua.globals().preimage=preimage
        # Fail the first config log, then fail part1 once. Actual closure resumes
        # after the last successful part and never repeats successful writes.
        result=lua.execute('''
        local logs,config_calls={},0
        local receipt=cfg.native_gate_creation_rng_manifest..';seed_a=8b34a17d4ae84a2f;seed_b=5311c417920e6e36'
        local gate={poll=function()return {creation_rng_schema=1,creation_rng_ack_schema=1}end,
         creation_rng_config=function()config_calls=config_calls+1;return receipt,'schema=1 ordinal=1 boot='..string.rep('a',32)..' digest='..string.rep('b',64)..' slot=1 phase=0 issued=0 state=0 records=0 scopes=0 attempted=0' end,
         creation_rng_ack_status=function()return 'schema=1 acks=1 end=1' end,
         creation_rng_status=function()return 'schema=1 configured=1 closing=1 count=2 active=0 end=1' end,
         creation_rng_row=function(scope,part)return 'schema=1 scope='..scope..' part='..part..' retained=1' end}
        local fail_header,fail_part=true,true
        local drain,activate=module.new(gate,cfg,preimage,function(line)
         if fail_header then fail_header=false;error('header failed') end
         if fail_part and line:find('scope=1 part=1',1,true) then fail_part=false;error('part failed') end
         logs[#logs+1]=line
        end)
        assert(not pcall(activate));assert(not pcall(drain,true));drain(true);drain(true)
        return config_calls,#logs,table.concat(logs,'\\n')
        ''')
        self.assertEqual(result[:2],(1,8));self.assertEqual(result[2].count('scope=1 part=0'),1)
        self.assertEqual(result[2].count('CREATION_RNG_CONFIG'),1)

    def test_evidence_joins_and_mutations(self):
        data=log_fixture();errors=[];result=evidence.evidence(render_log(data),render_config(),1,errors)
        self.assertEqual(errors,[]);self.assertTrue(result['valid']);self.assertFalse(result['whole_rng_supported'])
        for row in range(4):
            for name in data[2][row]:
                changed=copy.deepcopy(data)
                if isinstance(changed[2][row][name],list):changed[2][row][name][0]^=1
                else:changed[2][row][name]^=1
                errors=[]
                evidence.evidence(render_log(changed),render_config(),1,errors)
                with self.subTest(row=row,field=name):self.assertTrue(errors)
        for name in data[3]:
            changed=copy.deepcopy(data);changed[3][name]^=1;errors=[]
            evidence.evidence(render_log(changed),render_config(),1,errors)
            with self.subTest(footer=name):self.assertTrue(errors)
        errors=[];self.assertFalse(evidence.evidence('', '',1,errors)['supported']);self.assertEqual(errors,[])
        bad=render_log(data).replace('NATIVE_GATE REGISTRY_UPDATE_ORIGIN ','NATIVE_GATE OMITTED_ORIGIN ')
        errors=[];self.assertFalse(evidence.evidence(bad,render_config(),1,errors)['valid']);self.assertTrue(errors)
        lines=render_log(data).splitlines();seed=rng.parse_manifest(rng.manifest())['key_b']
        lines=[line.replace(f'rng={seed:016x}',f'rng={seed^1:016x}') if 'index=7 part=2 ' in line else line for line in lines]
        errors=[];self.assertFalse(evidence.evidence('\n'.join(lines),render_config(),1,errors)['valid']);self.assertTrue(errors)


if __name__=='__main__':unittest.main()
