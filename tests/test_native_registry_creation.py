from pathlib import Path
import copy
import hashlib
import json
import re
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/dev'))
import native_creation_rng as rng
import native_registry_creation as contract
import native_registry_creation_evidence as evidence
import native_creation_evidence as creation
import native_registry_md_evidence as md
import test_native_creation_rng as base
import test_native_creation_rng_ack as ack


def config():
    values=ack.ack_config();values['native_gate_registry_creation_schema']=1
    return values


def text(row,names):
    return ' '.join(k+'='+(f'{row[k]:016x}' if k in evidence.FIXED else f'{row[k]:x}' if k in evidence.HEX else str(row[k])) for k in names)


def fixture():
    data=base.log_fixture();trace=data[1];status=trace['status'];records=trace['records']
    identities=[]
    for edge,(index,seq) in enumerate(((3,1),(3,2),(1,5),(12,6))):
        r=records[index-1];sample=r['before_identity'] if edge==0 else r['after_identity']
        row={k:0 for k in evidence.COMMON+evidence.SUMMARY}
        row.update(schema=1,index=edge+1,sequence=seq,md_anchor=r['enter'] if edge==0 else r['leave'],creation=index,
                   edge=edge,complete=1,closed=1,creation_enter=r['enter'],creation_leave=0 if edge==0 else r['leave'],
                   tick=6,thread=status['thread'],handle=status['handle'],object=status['object'],runtime=status['runtime'],
                   owner=status['true_owner'],macro=0x90000,lookup=2,lookup_first=status['object'],lookup_second=status['object'],
                   map_before=0xa0000,map_after=0xa0000,controller=sample['controller'],agent=sample['agent'],slots=sample['slots'],
                   admitted=int(edge<3),relationship=int(edge==3),presence_recorded=1,presence_stable=1)
        parts=[row]
        for part in (1,2):parts.append(dict({k:row[k] for k in evidence.COMMON},part=part,**{k:sample[k] for k in evidence.PRESENCE}))
        identities.append(parts)
    operations=[]
    for source,seq,leave,anchor,end,index,classification in ((2,3,4,11,13,4,1),(1,7,8,48,48,0,2)):
        r=records[3];args=r['args'];op={k:0 for k in evidence.OPERATION}
        op.update(schema=1,index=len(operations)+1,enter=seq,leave=leave,md_enter=anchor,md_leave=end,tick=6,thread=status['thread'],
                  handle=status['handle'],object=status['object'],runtime=status['runtime'],owner=status['true_owner'],kind=6,
                  source=source,caller=0x1066dc6 if source==2 else 0x106e3fb,verified=0x1066dc6 if source==2 else 0x106e3fb,
                  context=args[1],exec=args[0],program=args[2] if source==2 else 0,begin=0,end=4 if source==2 else 0,mode=1 if source==2 else 0,
                  script_hash=r['range']['script_hash'],script='orders.base',creation=index,classification=classification,
                  paired=1,captured=1,closed=1,normal=1,identity_at_entry=1,result=r['result'] if source==2 else 0)
        operations.append(op)
    footer={k:0 for k in evidence.FOOTER};footer.update(schema=1,configured=1,closing=1,sequence=8,identities=4,operations=2,peak=1,kinds=32,
                   handle=status['handle'],runtime=status['runtime'],tick=6,thread=status['thread'],identity_admitted=1)
    return identities,operations,footer,data


def local_log(parts):
    identities,operations,footer,_=parts
    return '\n'.join(['NATIVE_GATE REGISTRY_CREATION_IDENTITY '+text(p,evidence.COMMON+(evidence.SUMMARY if i==0 else evidence.PRESENCE))
        for group in identities for i,p in enumerate(group)]+['NATIVE_GATE REGISTRY_CREATION_OPERATION '+text(r,evidence.OPERATION) for r in operations]+
        ['NATIVE_GATE REGISTRY_CREATION_STOP '+text(footer,evidence.FOOTER)])+'\n'


def combined(parts=None):
    parts=parts or fixture();data=parts[3];cfg=base.render_config(config())
    digest=hashlib.sha256(rng.bootstrap_preimage(cfg).encode('ascii')).hexdigest();boot='1'*32
    header=''
    for ordinal in (1,2):
        header+=f'NATIVE_GATE REGISTRY_CREATION_CONFIG {contract.MANIFEST}\nNATIVE_GATE CREATION_RNG_CONFIG {data[4]}\n'
        header+=f'NATIVE_GATE CREATION_RNG_ACK schema=1 ordinal={ordinal} boot={boot} digest={digest} slot=1 phase=0 issued=0 state=0 records=0 scopes=0 attempted=0\n'
    old=base.render_log(data).replace('NATIVE_GATE CREATION_RNG_CONFIG '+data[4]+'\n','')
    # Production drains this observer before the RNG footer; printed rows do
    # not replace their own entry/exit order and immutable MD anchors.
    position=old.index('NATIVE_GATE CREATION_RNG_STOP ')
    log=header+old[:position]+local_log(parts)+old[position:]
    log+=f'NATIVE_GATE CREATION_RNG_ACK_STOP schema=1 acks=2 boot={boot} digest={digest} slot=1\n'
    return cfg,log


def model_native_reattach(native,preimage):
    """Two actual LuaJIT VMs consume captured production C configuration returns.

    Synthetic post-configuration provenance is appended separately. This tests
    agreement/reattachment, not engine object lifetime or construction success.
    """
    from lupa.luajit21 import LuaRuntime
    outputs=[]
    for key in ('FIRST','SECOND'):
        lua=LuaRuntime(unpack_returned_tuples=True)
        registry=lua.execute((ROOT/'tools/dev/native_registry_creation.lua').read_text())
        random=lua.execute((ROOT/'tools/dev/native_creation_rng.lua').read_text())
        cfg=lua.table_from(config());calls=[];logs=[]
        gate=lua.table()
        gate.poll=lambda:lua.table_from(dict(registry_creation_schema=1,creation_rng_schema=1,creation_rng_ack_schema=1))
        gate.registry_creation_config=lambda marker,manifest,actual: (calls.append((marker,manifest,actual)) or 1)
        for name in ('registry_creation_status','registry_creation_identity','registry_creation_operation','creation_rng_status','creation_rng_row','creation_rng_ack_status'):
            gate[name]=lambda *args:None
        gate.creation_rng_config=lambda manifest,actual:(native['CONFIG'],native[key]) if actual==preimage else None
        # Lua's type() must see actual Lua functions, not Python userdata.
        wrap=lua.eval('function(f)return function(...)return f(...)end end')
        for name in list(gate):gate[name]=wrap(gate[name])
        logger=wrap(lambda line:logs.append('NATIVE_GATE '+line))
        _,activate_registry=registry.new(gate,cfg,preimage,logger);activate_registry()
        _,activate_random=random.new(gate,cfg,preimage,logger);activate_random()
        assert calls==[(contract.MANIFEST,rng.manifest(),preimage)]
        outputs.extend(logs)
    cfg,log=combined()
    body=log[log.index('NATIVE_GATE REGISTRY_MD '):]
    body=re.sub(r'NATIVE_GATE CREATION_RNG_ACK_STOP [^\n]+','NATIVE_GATE CREATION_RNG_ACK_STOP '+native['STOP'],body)
    errors=[];result=evidence.evidence('\n'.join(outputs)+'\n'+body,cfg,1,errors)
    assert errors==[] and result['valid'],errors


class ProvisionalRegistry(unittest.TestCase):
    def test_complete_capture_is_never_ready(self):
        cfg,log=combined();errors=[];result=evidence.evidence(log,cfg,1,errors)
        self.assertEqual(errors,[]);self.assertTrue(result['valid']);self.assertTrue(result['identity_admitted'])
        self.assertFalse(result['ready']);self.assertFalse(result['publication']);self.assertTrue(result['provenance_joined'])

    def test_native_output_decode(self):
        # A committed synthetic fixture is mandatory; loaded C output is an
        # additional exact production-format check via --native-output.
        result=evidence.decode(local_log(fixture()))
        self.assertEqual(len(result['identities']),4)

    def test_provenance_and_presence_mutants(self):
        mutations=[lambda p:p[0][0][0].update(admitted=1,lookup=1),
          lambda p:p[0][0][0].update(map_after=0),lambda p:p[0][0][0].update(creation=2),
          lambda p:p[0][0][0].update(creation_enter=8),lambda p:p[0][0][0].update(md_anchor=8),
          lambda p:p[0][3][0].update(admitted=1),lambda p:p[0][3][0].update(controller=0),
          lambda p:p[0][3][0].update(open_scopes=1),lambda p:p[0][0][1].update(owner_state=99),
          lambda p:p[0][0][2].update(actual=p[0][0][2]['actual']+1),
          lambda p:p[1][0].update(kind=2),lambda p:p[1][0].update(creation=2),
          lambda p:p[1][0].update(context=0),lambda p:p[1][0].update(result=999),
          lambda p:p[1][0].update(classification=0),lambda p:p[1][0].update(leave=3),
          lambda p:p[1][1].update(closed=0),lambda p:p[1][1].update(enter=3),
          lambda p:p[2].update(ready=1),lambda p:p[2].update(publication=1),lambda p:p[2].update(operations=1),
          lambda p:p[2].update(open=1),lambda p:p[2].update(kinds=0),lambda p:p[2].update(peak=2),
          lambda p:p[2].update(sequence=2**64-1)]
        for mutate in mutations:
            with self.subTest(mutate=mutations.index(mutate)):
                parts=fixture();mutate(parts);cfg,log=combined(parts);errors=[]
                self.assertFalse(evidence.evidence(log,cfg,1,errors)['valid']);self.assertTrue(errors)

    def test_lookup_absence_is_retained_not_admitted(self):
        parts=fixture();parts[2].update(failures=2,identity_admitted=0)
        for group in parts[0]:
            group[0]['admitted']=0
        for row in parts[0][0]:row['error']=4
        parts[0][0][0].update(lookup=1,lookup_first=0,lookup_second=0)
        cfg,log=combined(parts);errors=[];result=evidence.evidence(log,cfg,1,errors)
        self.assertEqual(errors,[]);self.assertTrue(result['valid']);self.assertFalse(result['identity_admitted'])
        self.assertEqual(result['identities'][0]['lookup'],1)

    def test_reordered_observation_cannot_precede_admitted_pilot(self):
        parts=fixture()
        for row in parts[0][0]:row['sequence']=3
        for row in parts[0][1]:row['sequence']=4
        parts[1][0].update(enter=1,leave=2)
        cfg,log=combined(parts);errors=[]
        self.assertFalse(evidence.evidence(log,cfg,1,errors)['valid']);self.assertTrue(errors)
        # Even with admission explicitly refused, the contradictory local/MD
        # ordering must not become valid retained causal evidence.
        parts[0][0][0]['admitted']=0;parts[0][1][0]['admitted']=0
        cfg,log=combined(parts);errors=[]
        self.assertFalse(evidence.evidence(log,cfg,1,errors)['valid']);self.assertTrue(errors)

    def test_producer_consistency_refusals(self):
        parts=fixture();parts[1][0]['tick']+=1
        with self.assertRaisesRegex(ValueError,'tick refusal'):evidence.decode(local_log(parts))
        parts[1][0]['error']=16;parts[2].update(failures=1,identity_admitted=0)
        self.assertFalse(evidence.decode(local_log(parts))['identity_admitted'])
        parts=fixture();parts[0][2][0]['owner']+=1;parts[0][2][0]['admitted']=0
        with self.assertRaisesRegex(ValueError,'immutable identity'):evidence.decode(local_log(parts))
        for row in parts[0][2]:row['error']=512
        parts[2].update(failures=1,identity_admitted=0)
        self.assertFalse(evidence.decode(local_log(parts))['identity_admitted'])
        parts=fixture();parts[2].update(sequence=108,dropped=1,failures=1,identity_admitted=0)
        with self.assertRaisesRegex(ValueError,'sequence gap'):evidence.decode(local_log(parts))
        parts[2]['sequence']=9
        self.assertFalse(evidence.decode(local_log(parts))['capture_complete'])

    def test_bootstrap_and_attachment_refusals(self):
        cfg,log=combined()
        self.assertEqual(len(rng.bootstrap_preimage(cfg)),702)
        self.assertEqual(hashlib.sha256(rng.bootstrap_preimage(cfg).encode()).hexdigest(),'91b30261372f883f52286bd23a19ccbabd2c03b60b7d6627de662c731af6bf85')
        for bad in (log.replace('REGISTRY_CREATION_CONFIG','REGISTRY_CREATION_WRONG',1),
                    log.replace(contract.MANIFEST,contract.MANIFEST+';policy=observe',1),
                    log.replace('ordinal=2','ordinal=1'),log.replace('boot='+'1'*32,'boot='+'2'*32,1)):
            errors=[];self.assertFalse(evidence.evidence(bad,cfg,1,errors)['valid'])
        for name in ('native_gate_creation_rng_schema','native_gate_creation_rng_ack_schema','native_gate_creation_trace_schema'):
            bad=config();bad[name]=0
            with self.assertRaises(ValueError):rng.bootstrap_preimage(base.render_config(bad))

    def test_legacy_has_no_new_required_records(self):
        errors=[];result=evidence.evidence('',base.render_config(ack.ack_config()),1,errors)
        self.assertFalse(result['supported']);self.assertEqual(errors,[])

    def test_lua_preimage_matches_and_drain_resumes(self):
        from lupa.luajit21 import LuaRuntime
        lua=LuaRuntime(unpack_returned_tuples=True);values=config();cfg=lua.table_from(values)
        rng_lua=lua.execute((ROOT/'tools/dev/native_creation_rng.lua').read_text())
        self.assertEqual(rng_lua.preimage(cfg,rng.BOOTSTRAP[:-1]),rng.bootstrap_preimage(base.render_config(values)))
        module=lua.execute((ROOT/'tools/dev/native_registry_creation.lua').read_text());rows=local_log(fixture()).splitlines()
        captured=lua.table_from([r.split(' ',2)[2] for r in rows[:12]])
        operations=lua.table_from([r.split(' ',2)[2] for r in rows[12:14]])
        status=rows[-1].split(' ',2)[2]
        run=lua.eval(r'''function(module,cfg,data,ids,ops,status)
          local output,fail={},true
          local gate={poll=function()return{registry_creation_schema=1}end,
           registry_creation_config=function(marker,manifest,preimage)assert(marker==module.manifest and manifest==cfg.native_gate_creation_rng_manifest and preimage==data);return 1 end,
           registry_creation_status=function()return status end,
           registry_creation_identity=function(i,p)return ids[(i-1)*3+p+1]end,
           registry_creation_operation=function(i)return ops[i]end}
          local drain,activate=module.new(gate,cfg,data,function(line)
            if fail and line:match('index=2 part=1 ')then fail=false;error('injected logger failure')end
            output[#output+1]=line
          end)
          activate();assert(not pcall(drain,true));drain(true);drain(true)
          assert(#output==16);return table.concat(output,'\n')
        end''')
        output=run(module,cfg,rng.bootstrap_preimage(base.render_config(values)),captured,operations,status)
        decoded=evidence.decode('\n'.join('NATIVE_GATE '+x for x in output.splitlines()))
        self.assertFalse(decoded['ready'])

    def test_composer_capability_and_immutable_source(self):
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'source';(source/'ui').mkdir(parents=True);(source/'native').mkdir()
            values=base.config()
            for key in ('native_gate_creation_rng_schema','native_gate_creation_rng_manifest','native_gate_creation_trace_schema',
                        'native_gate_construction_test','native_gate_construction_plan_sha'):del values[key]
            values.update(native_gate_command_test=True,native_gate_order_state=True,native_gate_registry_origin_schema=1,native_gate_registry_origin_identity_schema=1)
            (source/'ui/config.lua.append').write_text(base.render_config(values))
            (source/'ui/native_gate_probe.lua').write_bytes((ROOT/'tools/dev/native_gate_probe.lua').read_bytes())
            (source/'ui.xml.files').write_text('ui/native_gate_probe.lua\n')
            capability=b'creation_trace_config creation_trace_status creation_trace_row CREATION_TRACE_2;handle= creation_rng_config creation_rng_status creation_rng_row creation_rng_ack_status CREATION_RNG_1;epoch='
            dll=source/'native/x4lan_gate_64.dll';dll.write_bytes(capability)
            def seal():
                payload={str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest() for p in source.rglob('*') if p.is_file() and p.name!='x4lane-manifest.json'}
                (source/'x4lane-manifest.json').write_text(json.dumps(payload));return payload
            seal();out=Path(directory)/'missing'
            with self.assertRaisesRegex(ValueError,'provisional registry requires'):base.compose(source,out,'construction',True,1,True)
            self.assertFalse(out.exists())
            dll.write_bytes(capability+b' registry_creation_config registry_creation_status registry_creation_identity registry_creation_operation REGISTRY_CREATION_1;handle=')
            original=seal();out=Path(directory)/'enabled';base.compose(source,out,'construction',True,1,True)
            self.assertEqual(seal(),original);self.assertTrue((out/'ui/native_registry_creation.lua').exists())
            self.assertEqual(rng.bootstrap_preimage((out/'ui/config.lua.append').read_text()),rng.bootstrap_preimage(base.render_config(config())))
            with self.assertRaisesRegex(ValueError,'overlap'):base.compose(source,source/'child','construction',True,1,True)
            self.assertFalse((source/'child').exists())


if __name__=='__main__':
    if len(sys.argv)==3 and sys.argv[1]=='--native-output':
        raw=Path(sys.argv[2]).read_text();log=raw.split('RC_TEST_BEGIN\n',1)[1].split('RC_TEST_END',1)[0]
        result=evidence.decode(log)
        assert len(result['identities'])==4 and result['identities'][3]['open_scopes']==1 and not result['ready']
        native={key:re.search('RC_ACK_'+key+r' ([^\n]*)',raw)[1] for key in ('CONFIG','FIRST','SECOND','STOP')}
        preimage=re.search(r'RC_PREIMAGE ([^\n]*)',raw)[1]
        assert preimage==rng.bootstrap_preimage(base.render_config(config()))
        model_native_reattach(native,preimage)
        print('Loaded C provisional registry rows decoded; open candidate retained; READY unavailable')
    else:unittest.main()
