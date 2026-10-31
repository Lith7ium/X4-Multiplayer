"""Versioned pre-activity reattachment evidence; legacy logs stay strict."""
import hashlib
import re
import unittest
import test_native_creation_rng as base

rng, evidence = base.rng, base.evidence


def ack_config():
    result=base.config();result['native_gate_creation_rng_ack_schema']=1
    return result


def native_fixture():
    digest=hashlib.sha256(rng.bootstrap_preimage(base.render_config(ack_config())).encode()).hexdigest()
    common=f'boot={"ab"*16} digest={digest} slot=1'
    receipt=base.log_fixture()[4]
    return dict(CONFIG=receipt,FIRST=f'schema=1 ordinal=1 {common} phase=0 issued=0 state=0 records=0 scopes=0 attempted=0',
                SECOND=f'schema=1 ordinal=2 {common} phase=0 issued=0 state=0 records=0 scopes=0 attempted=0',
                STOP=f'schema=1 acks=2 {common}')


def model_reload(native=None):
    """Also accepts captured production C Lua-return strings from native tests."""
    from lupa.luajit21 import LuaRuntime
    native=native_fixture() if native is None else native
    lua=LuaRuntime(unpack_returned_tuples=True)
    module=lua.execute((base.ROOT/'tools/dev/native_creation_rng.lua').read_text())
    values=ack_config();cfg=lua.table_from(values);cfg.native_gate_snapshot_fixture=lua.table_from(rng.FIXTURE)
    data=base.log_fixture();log=base.render_log(data)
    scope_rows=re.findall(r'NATIVE_GATE CREATION_RNG ([^\n]+)',log)
    footer=re.search(r'NATIVE_GATE CREATION_RNG_STOP ([^\n]+)',log)[1]
    g=lua.globals();g.module=module;g.cfg=cfg;g.native=lua.table_from(native)
    g.preimage=rng.bootstrap_preimage(base.render_config(values));g.rows=lua.table_from(scope_rows);g.footer=footer
    g.trace=data[0]
    result=lua.execute('''
      local logs,calls,active={},0,false
      local fail_ack,fail_footer=true,true
      local gate={poll=function()return {creation_rng_schema=1,creation_rng_ack_schema=1}end,
       creation_rng_config=function(manifest,preimage_actual)
        assert(manifest==cfg.native_gate_creation_rng_manifest and preimage_actual==preimage)
        calls=calls+1;assert(calls<=2);return native.CONFIG,calls==1 and native.FIRST or native.SECOND
       end,
       creation_rng_status=function()return active and footer or 'schema=1 configured=1 closing=0 count=0 active=0 end=1' end,
       creation_rng_ack_status=function()return native.STOP end,
       creation_rng_row=function(scope,part)if active then return rows[(scope-1)*2+part+1]end end}
      local function log(line)
       if fail_ack and line:find('CREATION_RNG_ACK ',1,true) then fail_ack=false;error('ack log failure')end
       if fail_footer and line:find('CREATION_RNG_ACK_STOP ',1,true) then fail_footer=false;error('ack footer failure')end
       logs[#logs+1]='NATIVE_GATE '..line..'\\n'
      end
      local drain,activate=module.new(gate,cfg,preimage,log)
      assert(not pcall(activate));drain(false);assert(not pcall(activate))
      logs[#logs+1]='NATIVE_GATE ARMED slot=p1 hash_scope=battle\\nNATIVE_GATE LOAD_REQUEST snapshot=fixture\\n'
      -- Discard the first Lua state exactly as the observed reload did. Native
      -- configuration and ordinal survive; no scope or footer was emitted.
      drain,activate=module.new(gate,cfg,preimage,log);activate()
      logs[#logs+1]='NATIVE_GATE ARMED slot=p1 hash_scope=battle\\n'..trace
      active=true;assert(not pcall(drain,true));drain(true);drain(true)
      return table.concat(logs),calls
    ''')
    assert result[1]==2
    errors=[];parsed=evidence.evidence(result[0],base.render_config(values),1,errors)
    assert errors==[],errors
    assert parsed['valid'] and parsed['acknowledgements']['count']==2
    return result[0]


class CreationAck(unittest.TestCase):
    def test_lua_reload_pair_and_resume(self):
        log=model_reload()
        for marker,count in [('CREATION_RNG_CONFIG ',2),('CREATION_RNG_ACK ',2),('CREATION_RNG_STOP ',1),('CREATION_RNG_ACK_STOP ',1)]:
            self.assertEqual(log.count(marker),count)

    def test_ack_mode_is_in_initial_agreement_and_legacy_unchanged(self):
        old=base.render_config();new=base.render_config(ack_config())
        self.assertEqual(rng.bootstrap_preimage(new),rng.bootstrap_preimage(old)+rng.ACK_SUFFIX)
        self.assertEqual(hashlib.sha256(rng.bootstrap_preimage(new).encode()).hexdigest(),
                         '1ce03def6a724e38f50bb9f32096f7829a91f0f47b32147153b1d2eeca1a79ba')
        self.assertEqual(rng.bootstrap_plan(old),rng.bootstrap_plan(new))
        for mode in ('0','3','true','01','1\nX4LAN_CONFIG.native_gate_creation_rng_ack_schema = 1'):
            with self.subTest(mode=mode),self.assertRaises(ValueError):
                rng.bootstrap_preimage(old+'\nX4LAN_CONFIG.native_gate_creation_rng_ack_schema = '+mode)
        data=base.log_fixture();legacy=base.render_log(data);errors=[]
        self.assertTrue(evidence.evidence(legacy,old,1,errors)['valid']);self.assertEqual(errors,[])
        duplicate=legacy+'NATIVE_GATE CREATION_RNG_CONFIG '+data[4]+'\n';errors=[]
        self.assertFalse(evidence.evidence(duplicate,old,1,errors)['valid'])
        self.assertIn('missing/duplicate/different manifest and key receipt',errors[0])

    def test_missing_conflicting_activity_and_identity_refusals(self):
        log=model_reload();cfg=base.render_config(ack_config());lines=log.splitlines(True)
        acklines=[i for i,line in enumerate(lines) if 'CREATION_RNG_ACK ' in line]
        mutations={}
        for i in acklines:
            for field in evidence.ACK:
                row=evidence.fields(lines[i].split('CREATION_RNG_ACK ',1)[1].strip(),evidence.ACK)
                before=str(row[field]);after=('cd'*16 if field=='boot' else 'ef'*32 if field=='digest' else str(row[field]+1))
                changed=lines[:];changed[i]=changed[i].replace(field+'='+before,field+'='+after)
                mutations[f'ack{i}:{field}']=''.join(changed)
            mutations[f'missing{i}']=''.join(lines[:i]+lines[i+1:])
        mutations['missing_config']=log.replace('NATIVE_GATE CREATION_RNG_CONFIG ','NATIVE_GATE OMITTED ',1)
        mutations['different_manifest']=log.replace(';epoch=1',';epoch=2',1)
        mutations['different_key']=log.replace('seed_a=8b34','seed_a=8b35',1)
        mutations['after_activity']=log[:log.index(lines[acklines[1]-1])]+'NATIVE_GATE MASS id=1\n'+log[log.index(lines[acklines[1]-1]):]
        swapped=lines[:];swapped[acklines[1]],swapped[acklines[1]-1]=swapped[acklines[1]-1],swapped[acklines[1]]
        mutations['swapped']=''.join(swapped)
        mutations['extra_scope']=log+next(line for line in lines if 'CREATION_RNG schema=1 scope=1 part=0 ' in line)
        mutations['missing_footer']=log.replace('CREATION_RNG_ACK_STOP ','OMITTED ')
        footer_line=next(line for line in lines if 'CREATION_RNG_ACK_STOP ' in line)
        footer=evidence.fields(footer_line.split('CREATION_RNG_ACK_STOP ',1)[1].strip(),evidence.ACK_STOP)
        for field,value in footer.items():
            changed='cd'*16 if field=='boot' else 'ef'*32 if field=='digest' else str(value+1)
            mutations['footer:'+field]=log.replace(footer_line,footer_line.replace(field+'='+str(value),field+'='+changed))
        mutations['late_duplicate']=log+lines[acklines[1]-1]+lines[acklines[1]]
        mutations['mixed_boot_restart']=lines[0]+lines[1]+log
        mutations['too_many']=(''.join(lines[:2]))*9+log
        for name,changed in mutations.items():
            errors=[]
            with self.subTest(name=name):self.assertFalse(evidence.evidence(changed,cfg,1,errors)['valid']);self.assertTrue(errors)


if __name__=='__main__':
    import sys
    if len(sys.argv)==3 and sys.argv[1]=='--native-output':
        raw=base.Path(sys.argv[2]).read_bytes()
        text=raw.decode('utf-16' if raw.startswith((b'\xff\xfe',b'\xfe\xff')) else 'utf-8')
        pairs=re.findall(r'^ACK_TEST_(CONFIG|FIRST|SECOND|STOP) ([^\r\n]+)',text,re.M)
        assert len(pairs)==4 and len(dict(pairs))==4
        model_reload(dict(pairs))
        print('Production C Lua-return strings -> two Lua states -> independent parser: PASS')
    else:unittest.main()
