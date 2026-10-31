from pathlib import Path
import sys
import unittest
from lupa.luajit21 import LuaRuntime

ROOT=Path(__file__).resolve().parents[1]
SOURCE=(ROOT/'tools/dev/native_construction_cancel.lua').read_text()
sys.path.insert(0,str(ROOT/'tools/dev'))
from native_construction_cancel_evidence import evidence


class ConstructionCancel(unittest.TestCase):
    def fixture(self):
        lua=LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
          local ffi=require('ffi');local native={};local proxy={C=native}
          for _,key in ipairs({'cdef','typeof','istype'}) do proxy[key]=ffi[key] end
          require=function() return proxy end
          retained=18446744073709551614ULL
          queue={9007199254740993ULL,retained};mutations=0;checks=0
          native.GetNumOrders=function() return #queue end
          native.GetOrderID=function(_,index) return queue[index] or 0ULL end
          native.IsComponentOperational=function() return not destroyed end
          native.RemoveOrder2=function(object,index,playercancelled,checkonly,onlyimmediate)
            assert(object==99 and index==(expected_index or 2) and playercancelled and onlyimmediate)
            if checkonly then
              checks=checks+1
              if preflight_mutates then queue[1]=9007199254740994ULL end
              return not critical
            end
            mutations=mutations+1
            if native_failure then return false end
            table.remove(queue,index)
            if removes_other then table.remove(queue,1) end
            return true
          end
          X4LAN_NATIVE_ORDERS=function()
            local rows={'ORDERS_1','1',tostring(#queue)}
            for i=1,#queue do rows[#rows+1]=i..','..(i==1 and not new_order_first and 'Wait' or 'DeployToStation')..',started,3,1' end
            rows[#rows+1]='default,none';return table.concat(rows,';')
          end
        ''')
        lua.globals().module=lua.execute(SOURCE)
        lua.execute("cancel=module.new(99,2,retained,'91-2-2-1')")
        return lua,lua.globals().cancel

    def test_queue_removal_is_pending_until_fresh_native_disengagement(self):
        lua,cancel=self.fixture()
        self.assertEqual(cancel.request(100,2,1),('PENDING','100-2-2-2'))
        self.assertEqual(lua.globals().mutations,1)
        self.assertEqual(cancel.observe(101,99,False,False),'PENDING')
        self.assertEqual(cancel.observe(111,110,True,False),'PENDING')
        self.assertEqual(cancel.observe(141,140,False,True),'PENDING')
        self.assertEqual(cancel.observe(171,170,False,False),'ACK')
        self.assertEqual(cancel.request(172,2,2),'DENIED_COMPLETE')
        self.assertEqual(lua.globals().mutations,1)
        self.assertIn('status=ACK',cancel.sample())
        self.assertNotIn('184467',cancel.sample())
        self.assertNotIn('9007199',cancel.sample())

    def test_owner_revision_critical_and_reused_index_have_no_mutation(self):
        for setup,slot,revision,denial in (
            ('',1,1,'DENIED_OWNER'),('',2,0,'STALE_REVISION'),
            ('critical=true',2,1,'DENIED_NOT_IMMEDIATE'),('destroyed=true',2,1,'DENIED_ASSET'),
            ('queue[2]=18446744073709551613ULL',2,1,'DENIED_STALE_ORDER')):
            lua,cancel=self.fixture();lua.execute(setup)
            self.assertEqual(cancel.request(100,slot,revision),denial)
            self.assertEqual(lua.globals().mutations,0)
            self.assertIn('revision=1;status=IDLE',cancel.sample())

    def test_partial_native_failures_and_mutating_preflight_poison_without_retry(self):
        for fault in ('native_failure','removes_other','preflight_mutates'):
            lua,cancel=self.fixture();lua.execute(fault+'=true')
            with self.assertRaisesRegex(Exception,'no replay'):cancel.request(100,2,1)
            self.assertIn('status=PARTIAL',cancel.sample())
            before=lua.globals().mutations
            with self.assertRaises(Exception):cancel.request(101,2,2)
            self.assertEqual(lua.globals().mutations,before)

    def test_pending_cannot_replay_and_timeout_cannot_ack(self):
        lua,cancel=self.fixture();cancel.request(100,2,1)
        self.assertEqual(cancel.request(101,2,2),'DENIED_BUSY')
        with self.assertRaisesRegex(Exception,'timeout'):cancel.observe(221,220,True,False)
        with self.assertRaises(Exception):cancel.observe(222,221,False,False)
        self.assertEqual(lua.globals().mutations,1)

    def test_retained_identity_reappearing_after_removal_is_not_completion(self):
        lua,cancel=self.fixture();cancel.request(100,2,1)
        lua.execute('queue[2]=retained')
        with self.assertRaisesRegex(Exception,'reappeared'):cancel.observe(111,110,False,False)
        self.assertIn('status=PARTIAL',cancel.sample())

    def test_float_identity_is_refused_before_native_queries(self):
        lua,_=self.fixture()
        with self.assertRaisesRegex(Exception,'exact uint64'):
            lua.execute("module.new(99,2,tonumber(retained),'91-2-2-1')")
        self.assertEqual(lua.globals().mutations,0)
        self.assertEqual(lua.globals().checks,0)

    def test_destroyed_builder_cannot_count_as_successful_disengagement(self):
        lua,cancel=self.fixture();cancel.request(100,2,1)
        lua.execute('destroyed=true')
        with self.assertRaisesRegex(Exception,'builder lost'):cancel.observe(111,110,False,False)
        self.assertIn('status=PARTIAL',cancel.sample())

    def test_regressing_or_rewritten_native_sample_cannot_ack(self):
        for md_tick in (110,140):
            lua,cancel=self.fixture();cancel.request(100,2,1)
            self.assertEqual(cancel.observe(141,140,True,True),'PENDING')
            with self.assertRaisesRegex(Exception,'sample regressed or changed'):
                cancel.observe(142,md_tick,False,False)
            self.assertIn('status=PARTIAL',cancel.sample())

    def test_capture_binds_exactly_one_new_deployment_and_preserves_prior_orders(self):
        lua,_=self.fixture()
        lua.execute("table.remove(queue,2);before=module.snapshot(99);queue[2]=retained;captured,receipt=module.capture(99,before,'91-2-2-1')")
        self.assertEqual(lua.globals().receipt,'ORDER_CAPTURE_1;ticket=91-2-2-1;queue_before=1;queue_after=2;index=2;kind=DeployToStation;new=1')
        self.assertNotIn('184467',lua.globals().receipt)
        self.assertEqual(lua.globals().captured.request(100,2,1),('PENDING','100-2-2-2'))
        self.assertEqual(lua.globals().mutations,1)

    def test_capture_rejects_replaced_prior_order_extra_order_and_wrong_builder(self):
        for change,object_id in (('queue[1]=9007199254740994ULL',99),('queue[3]=18446744073709551613ULL',99),('',100)):
            lua,_=self.fixture()
            lua.execute('table.remove(queue,2);before=module.snapshot(99);queue[2]=retained;'+change)
            with self.assertRaises(Exception):lua.globals().module.capture(object_id,lua.globals().before,'91-2-2-1')
            self.assertEqual(lua.globals().mutations,0)

    def test_capture_from_empty_queue_matches_initial_builder_fixture(self):
        lua,_=self.fixture()
        lua.execute("queue={};before=module.snapshot(99);queue={retained};new_order_first=true;expected_index=1;captured,receipt=module.capture(99,before,'91-2-2-1')")
        self.assertEqual(lua.globals().receipt,'ORDER_CAPTURE_1;ticket=91-2-2-1;queue_before=0;queue_after=1;index=1;kind=DeployToStation;new=1')
        self.assertEqual(lua.globals().captured.request(100,2,1),('PENDING','100-2-2-2'))
        self.assertEqual(lua.globals().captured.observe(111,110,False,False),'ACK')

    def test_capture_snapshot_refuses_same_count_identity_replacement_and_reorder(self):
        for change in ('queue[2]=18446744073709551613ULL','queue[1],queue[2]=queue[2],queue[1]'):
            lua,_=self.fixture()
            lua.execute('original_orders=X4LAN_NATIVE_ORDERS;reads=0;X4LAN_NATIVE_ORDERS=function(object) reads=reads+1;local result=original_orders(object);if reads==1 then '+change+' end;return result end')
            with self.assertRaisesRegex(Exception,'unstable across bounded reread'):lua.globals().module.snapshot(99)
            self.assertEqual(lua.globals().mutations,0)

    def test_capture_snapshot_refuses_definition_change_without_identity_change(self):
        lua,_=self.fixture()
        lua.execute("original_orders=X4LAN_NATIVE_ORDERS;reads=0;X4LAN_NATIVE_ORDERS=function(object) reads=reads+1;local result=original_orders(object);if reads==2 then return result:gsub('DeployToStation','Wait') end;return result end")
        with self.assertRaisesRegex(Exception,'unstable across bounded reread'):lua.globals().module.snapshot(99)
        self.assertEqual(lua.globals().mutations,0)

    def batch_fixture(self):
        lua,guard=self.fixture()
        module=lua.execute((ROOT/'tools/dev/native_construction_cancel_batch.lua').read_text())
        return lua,module.new(guard,99)

    def test_complete_batch_preflight_and_canonical_owner_refusal(self):
        lua,batch=self.batch_fixture()
        with self.assertRaises(Exception):batch.batch(100,'4b020001','invalid',None)
        self.assertEqual(lua.globals().checks,0);self.assertEqual(lua.globals().mutations,0)
        receipt,ack,state=batch.batch(100,'4b020001','4b020001',None)
        self.assertEqual(receipt,'1,75,2,1,DENIED_OWNER,1,none;2,75,2,1,PENDING,2,100-2-2-2')
        self.assertEqual(ack,'none');self.assertIn('status=PENDING',state)
        with self.assertRaises(Exception):batch.batch(100,'00000000','4b020002',None)
        self.assertEqual(lua.globals().mutations,1)

    def test_batch_ack_requires_fresh_native_md_and_is_emitted_once(self):
        lua,batch=self.batch_fixture();batch.batch(100,'00000000','4b020001',None)
        stale=lua.table_from({'tick':99,'assigned':False,'deployed':False})
        self.assertEqual(batch.batch(101,'00000000','00000000',stale)[1],'none')
        fresh=lua.table_from({'tick':101,'assigned':False,'deployed':False})
        self.assertEqual(batch.batch(102,'00000000','00000000',fresh)[1],'100-2-2-2')
        self.assertEqual(batch.batch(103,'00000000','00000000',fresh)[1],'none')
        self.assertEqual(lua.globals().mutations,1)

    def test_partial_batch_retains_prior_denial_and_refuses_retry(self):
        lua,batch=self.batch_fixture();lua.execute('native_failure=true');partial=[]
        with self.assertRaisesRegex(Exception,'no replay'):
            batch.batch(100,'4b020001','4b020001',None,lambda state,why:partial.append((state,why)))
        self.assertEqual(len(partial),1)
        self.assertIn('DENIED_OWNER,1,none;2,75,2,1,NATIVE_FAILURE,2,none',partial[0][0])
        self.assertIn('status=PARTIAL',partial[0][0])
        with self.assertRaises(Exception):batch.batch(101,'00000000','4b020002',None)
        self.assertEqual(lua.globals().mutations,1)

    def evidence_fixture(self):
        lua,_=self.fixture()
        lua.execute("table.remove(queue,2);before=module.snapshot(99);queue[2]=retained;captured,receipt,private=module.capture(99,before,'91-2-2-1')")
        guard=lua.globals().captured
        batches=lua.execute((ROOT/'tools/dev/native_construction_cancel_batch.lua').read_text()).new(guard,99)
        lines=['NATIVE_GATE CONSTRUCTION_ACK tick=99 ticket=91-2-2-1 kind=deploy result=1|DeployToStation',
               'NATIVE_GATE CONSTRUCTION_CANCEL_CAPTURE tick=99 portable='+lua.globals().receipt+' local='+lua.globals().private]
        commands={100:('4b020001','4b020001'),101:('00000000','00000000'),102:('00000000','00000000'),103:('00000000','4b020002')}
        md=None
        for tick in commands:
            if tick in (100,102):
                at=99 if tick==100 else 101;assigned=tick==100
                md=lua.table_from({'tick':at,'assigned':assigned,'deployed':assigned})
                flags='0|0|1|1|1|1|1' if assigned else '0|0|0|0|1|1|1'
                lines.append(f'NATIVE_GATE CONSTRUCTION_MD tick={at} observed={tick} state={at}|{flags}|')
            if tick==100:lines.append('NATIVE_GATE CONSTRUCTION_CANCEL_LOCAL tick=100 phase=before state='+guard.local_snapshot())
            _,ack,state=batches.batch(tick,*commands[tick],md)
            if tick==100:lines.append('NATIVE_GATE CONSTRUCTION_CANCEL_LOCAL tick=100 phase=after state='+guard.local_snapshot())
            if ack!='none':lines.append(f'NATIVE_GATE CONSTRUCTION_CANCEL_LOCAL tick={tick} phase=ack state='+guard.local_snapshot())
            lines.append(f'NATIVE_GATE CONSTRUCTION_CANCEL_STATE tick={tick} state={state}')
        return '\n'.join(lines),commands

    def test_independent_native_identity_and_fresh_ack_evidence(self):
        source,commands=self.evidence_fixture();errors=[]
        result=evidence(source,commands,errors,build_verified=True)
        self.assertEqual(errors,[]);self.assertTrue(result['completed'])
        self.assertEqual(set(result['states']),set(commands))
        self.assertNotIn('184467',result['portable_capture'])
        self.assertNotIn('184467',str(result['states']))
        errors=[];evidence(source,commands,errors,build_verified=False)
        self.assertTrue(errors)

    def test_independent_evidence_rejects_replaced_lifetime_missing_reads_and_false_ack(self):
        source,commands=self.evidence_fixture()
        variants=[source.replace(old,new) for old,new in (
            ('identity=18446744073709551614','identity=18446744073709551613'),
            ('18446744073709551614','1.8446744073709552e19'),
            ('phase=after','phase=missing'),('phase=ack','phase=missing'),
            ('DENIED_OWNER,1,none','PENDING,2,100-1-2-2'),
            ('observed=102 state=101|0|0|0|0','observed=102 state=101|0|0|1|0'),
            (';operational=1',';operational=0'))]
        variants.append(source+'\n'+next(row for row in source.splitlines() if 'phase=ack' in row))
        for bad in variants:
            errors=[];result=evidence(bad,commands,errors,build_verified=True)
            self.assertTrue(errors);self.assertFalse(result['completed'])


if __name__=='__main__':unittest.main()
