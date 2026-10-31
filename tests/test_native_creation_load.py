"""ACK2 parser controls and loaded C -> two distinct Lua VM Load lifecycle.

The loaded fixture executes production gate/configuration functions on owned
memory. It does not execute an engine constructor or claim a completed build.
"""
import ctypes
import hashlib
import re
import unittest
import tempfile
import json
from pathlib import Path
import test_native_creation_rng as base

rng,evidence=base.rng,base.evidence


def config(registry=False):
    cfg=base.config();cfg['native_gate_creation_rng_ack_schema']=2
    if registry:cfg['native_gate_registry_creation_schema']=1
    return cfg


def synthetic_log(registry=False,phase=2):
    cfg=base.render_config(config(registry));data=base.log_fixture();receipt=data[4]
    digest=hashlib.sha256(rng.bootstrap_preimage(cfg).encode()).hexdigest();boot='ab'*16
    header='';common=f'boot={boot} digest={digest} slot=1'
    for ordinal,current in ((1,0),(2,phase)):
        if registry:header+='NATIVE_GATE REGISTRY_CREATION_CONFIG REGISTRY_CREATION_1;handle=0000000500000003;policy=observe\n'
        header+='NATIVE_GATE CREATION_RNG_CONFIG '+receipt+'\n'
        header+=f'NATIVE_GATE CREATION_RNG_ACK schema=2 ordinal={ordinal} {common} phase={current} registry_phase={current if registry else 0} reattach={ordinal-1} registry_acks={ordinal if registry else 0} issued=0 state=0 records=0 scopes=0 attempted=0\n'
        header+='NATIVE_GATE ARMED slot=p1 hash_scope=battle\n'
        if ordinal==1:header+='NATIVE_GATE LOAD_REQUEST snapshot='+rng.SNAPSHOT+'\n'
    body=base.render_log(data).replace('NATIVE_GATE CREATION_RNG_CONFIG '+receipt+'\n','')
    footer=f'NATIVE_GATE CREATION_RNG_ACK_STOP schema=2 acks=2 registry_acks={2 if registry else 0} {common} phase={phase} registry_phase={phase if registry else 0} failed=0\n'
    return cfg,header+body+footer,receipt


class LoadAcknowledgements(unittest.TestCase):
    def test_cross_stage_phase_and_load_before_every_activity(self):
        cfg,log,receipt=synthetic_log(True,3)
        log=log.replace('phase=3 registry_phase=3','phase=3 registry_phase=1')
        self.assertEqual(evidence.acknowledgements(log,cfg,1,receipt)['registry_phases'],[0,1])
        rows=log.splitlines(keepends=True)
        second=next(i for i,line in enumerate(rows) if 'CREATION_RNG_ACK schema=2 ordinal=2 ' in line)
        block=''.join(rows[second-2:second+2])
        third=block.replace('ordinal=2 ','ordinal=3 ').replace('registry_acks=2 ','registry_acks=3 ').replace('registry_phase=1 ','registry_phase=2 ')
        rows.insert(second+2,third)
        impossible=''.join(rows).replace('schema=2 acks=2 registry_acks=2','schema=2 acks=3 registry_acks=3').replace(
            'slot=1 phase=3 registry_phase=1 failed=0','slot=1 phase=3 registry_phase=2 failed=0')
        with self.assertRaisesRegex(ValueError,'registry phase'):evidence.acknowledgements(impossible,cfg,1,receipt)
        possible=impossible.replace('registry_phase=2 ','registry_phase=3 ')
        self.assertEqual(evidence.acknowledgements(possible,cfg,1,receipt)['phases'],[0,3,3])
        # The first registry half of reattachment must follow Load, too.
        rows=log.splitlines(keepends=True)
        registry_index=next(i for i,line in enumerate(rows) if i>0 and line.startswith('NATIVE_GATE REGISTRY_CREATION_CONFIG '))
        header=rows.pop(registry_index);load_index=next(i for i,line in enumerate(rows) if 'NATIVE_GATE LOAD_REQUEST ' in line)
        rows.insert(load_index,header)
        with self.assertRaisesRegex(ValueError,'before Load'):evidence.acknowledgements(''.join(rows),cfg,1,receipt)
        # A native run may have only one ACK if no UI reload occurs.
        cfg,log,receipt=synthetic_log(False,2);rows=log.splitlines(keepends=True)
        second=next(i for i,line in enumerate(rows) if 'CREATION_RNG_ACK schema=2 ordinal=2 ' in line)
        del rows[second-1:second+2]
        single=''.join(rows).replace('schema=2 acks=2 registry_acks=0','schema=2 acks=1 registry_acks=0').replace(
            'slot=1 phase=2 registry_phase=0 failed=0','slot=1 phase=0 registry_phase=0 failed=0')
        errors=[];self.assertTrue(evidence.evidence(single,cfg,1,errors)['valid']);self.assertEqual(errors,[])
        load=next(line for line in single.splitlines(keepends=True) if 'NATIVE_GATE LOAD_REQUEST ' in line)
        without=single.replace(load,'')
        for marker in ('NATIVE_GATE CREATION_RNG_STOP ', 'NATIVE_GATE CREATION_RNG_ACK_STOP ',None):
            moved=without+load if marker is None else without.replace(marker,load+marker)
            with self.subTest(marker=marker),self.assertRaisesRegex(ValueError,'before Load'):
                evidence.acknowledgements(moved,cfg,1,receipt)

    def test_production_probe_retains_both_drains_before_registry_refusal(self):
        from lupa.luajit21 import LuaRuntime
        source=(base.ROOT/'tools/dev/native_gate_probe.lua').read_text()
        setup=source[source.index('  local registry_activate,rng_activate'):source.index('  if rng_activate then rng_activate() end')+len('  if rng_activate then rng_activate() end')]
        abort=source[source.index("SetScript('onUpdate',function()") :]
        lua=LuaRuntime();lua.execute('''
          cfg={native_gate_registry_creation_schema=1,native_gate_creation_rng_schema=1,native_gate_player_observer=true}
          logs={};stops=0;done=false;armed=false;native_owned=true
          log=function(s)logs[#logs+1]=s end
          gate={stop=function()stops=stops+1 end,poll=function()return {active=0,restored=1,issued=0}end}
          X4LAN_NATIVE_REGISTRY_CREATION={new=function()return function(final)assert(final);log('REGISTRY_DRAIN')end,
             function()error('actual registry attachment refused')end end}
          X4LAN_NATIVE_CREATION_RNG={new=function()return function(final)assert(final);log('RNG_DRAIN')end,
             function()error('must not configure RNG after registry refusal')end end}
          SetScript=function(_,callback)update=callback end
        ''')
        lua.execute('frame=function()if done then return end;'+setup+' end')
        lua.execute(abort);lua.execute('update();update()')
        rows=list(lua.globals().logs.values())
        self.assertEqual(lua.globals().stops,1)
        self.assertEqual(rows.count('REGISTRY_DRAIN'),1);self.assertEqual(rows.count('RNG_DRAIN'),1)
        self.assertTrue(any('actual registry attachment refused' in row for row in rows))

    def test_explicit_composer_capability_and_source_preservation(self):
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'source';(source/'ui').mkdir(parents=True);(source/'native').mkdir()
            values=base.config()
            for key in ('native_gate_creation_rng_schema','native_gate_creation_rng_manifest','native_gate_creation_trace_schema',
                        'native_gate_construction_test','native_gate_construction_plan_sha'):del values[key]
            values.update(native_gate_command_test=True,native_gate_order_state=True,native_gate_registry_origin_schema=1,native_gate_registry_origin_identity_schema=1)
            (source/'ui/config.lua.append').write_text(base.render_config(values))
            (source/'ui/native_gate_probe.lua').write_bytes((base.ROOT/'tools/dev/native_gate_probe.lua').read_bytes())
            (source/'ui.xml.files').write_text('ui/native_gate_probe.lua\n')
            dll=source/'native/x4lan_gate_64.dll'
            capability=b'creation_trace_config creation_trace_status creation_trace_row CREATION_TRACE_2;handle= creation_rng_config creation_rng_status creation_rng_row creation_rng_ack_status CREATION_RNG_1;epoch= registry_creation_config registry_creation_status registry_creation_identity registry_creation_operation REGISTRY_CREATION_1;handle='
            def seal():
                payload={str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest() for p in source.rglob('*') if p.is_file() and p.name!='x4lane-manifest.json'}
                (source/'x4lane-manifest.json').write_text(json.dumps(payload));return payload
            dll.write_bytes(capability);original=seal();out=Path(directory)/'missing'
            with self.assertRaisesRegex(ValueError,'ACK2'):base.compose(source,out,'construction',True,1,True,2)
            self.assertFalse(out.exists());self.assertEqual(seal(),original)
            dll.write_bytes(capability+b' creation_rng_ack2_schema');original=seal();out=Path(directory)/'enabled'
            base.compose(source,out,'construction',True,1,True,2)
            self.assertEqual(seal(),original)
            self.assertEqual(rng.bootstrap_preimage((out/'ui/config.lua.append').read_text()),rng.bootstrap_preimage(base.render_config(config(True))))

    def test_explicit_agreement_and_unchanged_keys(self):
        for registry,digest in ((False,'792966722d11de00bbd39f887142ad17e73e9ff8c4cbd248c6c29f13c1c0286a'),
                               (True,'12674fe96e5dd2db165934cb30fc50d88ee5047c5036cbba38738ef127e29586')):
            cfg=config(registry);new=rng.bootstrap_preimage(base.render_config(cfg))
            self.assertEqual(hashlib.sha256(new.encode()).hexdigest(),digest)
            cfg['native_gate_creation_rng_ack_schema']=1
            self.assertEqual(new,rng.bootstrap_preimage(base.render_config(cfg)).replace('|CREATION_RNG_ACK_1','|CREATION_RNG_ACK_2'))
            self.assertEqual(rng.parse_manifest(rng.manifest())['key_a'],0x8b34a17d4ae84a2f)
        for mode in ('0','3','02','true','2\nX4LAN_CONFIG.native_gate_creation_rng_ack_schema = 2'):
            cfg=base.render_config(base.config())+'\nX4LAN_CONFIG.native_gate_creation_rng_ack_schema = '+mode
            with self.assertRaises(ValueError):rng.bootstrap_preimage(cfg)

    def test_synthetic_phase_and_pair_controls(self):
        for registry in (False,True):
            for phase in (1,2,3):
                cfg,log,receipt=synthetic_log(registry,phase)
                parsed=evidence.acknowledgements(log,cfg,1,receipt)
                self.assertEqual(parsed['phases'],[0,phase]);self.assertEqual(parsed['registry_acks'],2 if registry else 0)
        cfg,log,receipt=synthetic_log();errors=[]
        self.assertTrue(evidence.evidence(log,cfg,1,errors)['valid']);self.assertEqual(errors,[])

    def test_every_field_and_load_mutants_refuse(self):
        for registry in (False,True):
            cfg,log,receipt=synthetic_log(registry)
            mutations={}
            for index,line in enumerate(re.findall(r'NATIVE_GATE CREATION_RNG_ACK ([^\n]+)',log)):
                fields=evidence.fields(line,evidence.ACK2)
                for name,value in fields.items():
                    changed='cd'*16 if name=='boot' else 'ef'*32 if name=='digest' else str(value+4)
                    mutations[f'ack{index}:{name}']=log.replace(line,line.replace(name+'='+str(value),name+'='+changed),1)
            footer=re.search(r'NATIVE_GATE CREATION_RNG_ACK_STOP ([^\n]+)',log)[1]
            for name,value in evidence.fields(footer,evidence.ACK2_STOP).items():
                changed='cd'*16 if name=='boot' else 'ef'*32 if name=='digest' else str(value+4)
                mutations['footer:'+name]=log.replace(footer,footer.replace(name+'='+str(value),name+'='+changed))
            mutations['no_load']=log.replace('NATIVE_GATE LOAD_REQUEST','NATIVE_GATE OMITTED')
            mutations['wrong_load']=log.replace('LOAD_REQUEST snapshot='+rng.SNAPSHOT,'LOAD_REQUEST snapshot='+'0'*64)
            mutations['double_load']=log.replace('NATIVE_GATE LOAD_REQUEST',f'NATIVE_GATE LOAD_REQUEST snapshot={rng.SNAPSHOT}\nNATIVE_GATE LOAD_REQUEST')
            mutations['phase0_reattach']=log.replace('phase=2 registry_phase=', 'phase=0 registry_phase=')
            mutations['activity_before_reload']=log.replace('NATIVE_GATE LOAD_REQUEST','NATIVE_GATE MASS id=1\nNATIVE_GATE LOAD_REQUEST')
            for name,changed in mutations.items():
                with self.subTest(registry=registry,name=name),self.assertRaises(ValueError):
                    evidence.acknowledgements(changed,cfg,1,receipt)


def loaded_lifecycle(dll_path,image_path):
    """Actual production C returns are passed through two independent Lua VMs."""
    from lupa.luajit21 import LuaRuntime
    dll=ctypes.CDLL(str(Path(dll_path).resolve()))
    dll.creation_load_open.argtypes=[ctypes.c_char_p];dll.creation_load_fixture.argtypes=[ctypes.c_uint,ctypes.c_uint]
    dll.creation_load_call.argtypes=[ctypes.c_uint];dll.creation_load_output.argtypes=[ctypes.c_uint];dll.creation_load_output.restype=ctypes.c_char_p
    dll.creation_load_inputs.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.c_char_p]
    dll.creation_load_boot_inputs.argtypes=[ctypes.c_uint,ctypes.c_uint,ctypes.c_char_p,ctypes.c_char_p,ctypes.c_uint64]
    if dll.creation_load_open(str(Path(image_path).resolve()).encode())!=0:raise AssertionError('loaded C matrix failed')
    results=[]
    scenarios=[(registry,phase,None) for registry in (False,True) for phase in (1,2,3)]+[(True,1,2),(True,2,3)]
    for registry,phase,advance in scenarios:
        dll.creation_load_fixture(int(registry),2)
        values=config(registry);cfg_text=base.render_config(values);preimage=rng.bootstrap_preimage(cfg_text)
        digest=hashlib.sha256(preimage.encode()).hexdigest();logs=[]
        def new_vm(progress=None):
            lua=LuaRuntime(unpack_returned_tuples=True);wrap=lua.eval('function(f)return function(...)return f(...)end end')
            cfg=lua.table_from(values);cfg.native_gate_snapshot_fixture=lua.table_from(rng.FIXTURE)
            g=lua.globals();g.cfg=cfg;calls={'native':0,'ack':0,'footer':0}
            def record(line):
                key='ack' if line.startswith('CREATION_RNG_ACK ') else 'footer' if line.startswith('CREATION_RNG_ACK_STOP ') else None
                if key and calls[key]==0:
                    calls[key]+=1
                    raise RuntimeError('deliberate '+key+' logging interruption')
                logs.append('NATIVE_GATE '+line+'\n')
            g.log=wrap(record)
            g.rng_module=lua.execute((base.ROOT/'tools/dev/native_creation_rng.lua').read_text())
            g.trace_module=lua.execute((base.ROOT/'tools/dev/native_creation_trace.lua').read_text())
            g.registry_module=lua.execute((base.ROOT/'tools/dev/native_registry_creation.lua').read_text())
            def inputs(m,p):dll.creation_load_inputs(m.encode(),p.encode(),b'REGISTRY_CREATION_1;handle=0000000500000003;policy=observe')
            def configure_rng(m,p):
                calls['native']+=1
                assert calls['native']==1, 'logging retry repeated native attachment'
                inputs(m,p)
                if not dll.creation_load_call(3):return None
                return dll.creation_load_output(0).decode(),dll.creation_load_output(1).decode()
            def configure_registry(marker,m,p):
                dll.creation_load_inputs(m.encode(),p.encode(),marker.encode());accepted=dll.creation_load_call(2)
                if accepted and progress is not None:assert dll.creation_load_call(5 if progress==2 else 6)==progress
                return accepted
            def boot():
                dll.creation_load_boot_inputs(1,22529,b'0123456789abcdef0123456789abcdef',digest.encode(),rng.MASK)
                return dll.creation_load_call(1)
            gate={'boot':wrap(boot),'poll':wrap(lambda:lua.table_from({'creation_trace_schema':2,'creation_rng_schema':1,'creation_rng_ack_schema':1,
                    'creation_rng_ack2_schema':2,'registry_creation_schema':1})),
                'creation_rng_config':wrap(configure_rng),'registry_creation_config':wrap(configure_registry),
                'creation_rng_status':wrap(lambda:dll.creation_load_output(3).decode()),
                'creation_rng_ack_status':wrap(lambda:dll.creation_load_output(2).decode()),
                'registry_creation_status':wrap(lambda:dll.creation_load_output(4).decode()),
                'creation_trace_config':wrap(lambda marker:dll.creation_load_call(8)),
                'creation_trace_status':wrap(lambda:dll.creation_load_output(5).decode()),
                'load_requested':wrap(lambda:dll.creation_load_call(4))}
            for name in ('creation_rng_row','registry_creation_identity','registry_creation_operation','creation_trace_row'):gate[name]=wrap(lambda *args:None)
            g.gate=lua.table_from(gate);g.bootstrap=rng.BOOTSTRAP[:-1]
            lua.execute('''
              assert(gate.boot()==0)
              data=rng_module.preimage(cfg,bootstrap)
              trace=trace_module.new(gate,cfg,log)
              if cfg.native_gate_registry_creation_schema then registry,registry_activate=registry_module.new(gate,cfg,data,log)end
              rng,rng_activate=rng_module.new(gate,cfg,data,log)
              if registry_activate then registry_activate()end
              assert(not pcall(rng_activate));rng(false)
              assert(not pcall(rng_activate));log('ARMED slot=p1 hash_scope=battle')
            ''')
            return lua
        first=new_vm()
        first.execute("assert(gate.load_requested()==1);log('LOAD_REQUEST snapshot='..cfg.native_gate_snapshot_sha)")
        if phase>=2:assert dll.creation_load_call(5)==2
        if phase==3:assert dll.creation_load_call(6)==3
        second=new_vm(advance);assert second is not first
        assert dll.creation_load_call(7)==1
        second.execute('if registry then registry(true)end; assert(not pcall(rng,true));rng(true);rng(true);trace(true)')
        log=''.join(logs);receipt=re.search(r'NATIVE_GATE CREATION_RNG_CONFIG ([^\n]+)',log)[1]
        decoded=evidence.acknowledgements(log,cfg_text,1,receipt)
        assert decoded['phases']==[0,advance or phase] and decoded['registry_acks']==(2 if registry else 0)
        assert decoded['registry_phases']==[0,phase if registry else 0]
        # This fixture intentionally performs no constructors: whole RNG
        # evidence must remain invalid despite successful attachment proof.
        errors=[];whole=evidence.evidence(log,cfg_text,1,errors)
        assert not whole['valid'] and errors
        results.append({'registry':registry,'phases':decoded['phases'],'two_distinct_lua_vms':True,
            'registry_phases':decoded['registry_phases'],'actual_load_and_boot_next':True,
            'acknowledgements_valid':True,'constructor_evidence_valid':False})
    return results


if __name__=='__main__':
    import json,sys
    if len(sys.argv)==4 and sys.argv[1]=='--native-dll':
        print(json.dumps(loaded_lifecycle(sys.argv[2],sys.argv[3]),indent=2))
    else:unittest.main()
