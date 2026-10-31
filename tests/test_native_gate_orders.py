"""Native order state must affect the digest without process-local order IDs."""
from pathlib import Path
import unittest
from lupa.luajit21 import LuaRuntime

SOURCE = (Path(__file__).resolve().parents[1]/'tools/dev/native_gate_orders.lua').read_text()


class NativeOrderTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute('''
          local ffi=require('ffi')
          native={};local proxy={C=native}
          for _,key in ipairs({'cdef','typeof','new','string','fill','sizeof'}) do proxy[key]=ffi[key] end
          require=function(name) assert(name=='ffi');return proxy end
          X4LAN_CONFIG={native_gate_order_state=true}
          count=1;current=1;orderdef='MoveWait';orderstate='running';displayname='localized name'
          enabled=true;override=false;priority=false;has_default=true;truncated=false
          native.GetNumOrders=function() return count end
          native.GetOrderQueueCurrentIdx=function() return current end
          native.GetOrders3=function(buffer,n)
            if truncated then return 0 end
            for i=0,n-1 do
              buffer[i].queueidx=i+1;buffer[i].orderdef=orderdef;buffer[i].state=orderstate
              buffer[i].statename=displayname;buffer[i].actualparams=4;buffer[i].enabled=enabled
              buffer[i].isoverride=override;buffer[i].ispriority=priority
            end
            return n
          end
          native.GetDefaultOrder=function(buffer)
            buffer.orderdef='Wait';buffer.state='inactive';buffer.enabled=true
            return has_default
          end
        ''')
        self.lua.execute(SOURCE)

    def sample(self):
        return self.lua.globals().X4LAN_NATIVE_ORDERS(123)

    def test_queue_state_flags_default_and_active_index_are_canonical(self):
        first = self.sample()
        self.assertIn('1,MoveWait,running,4,1,0,0,0,0,0', first)
        for change in ("current=0", "orderdef='ProtectShip'", "orderstate='inactive'",
                       'enabled=false', 'override=true', 'priority=true', 'has_default=false'):
            previous=self.sample()
            self.lua.execute(change)
            self.assertNotEqual(previous,self.sample())

    def test_display_text_does_not_enter_state_and_empty_queue_is_valid(self):
        first=self.sample()
        self.lua.execute("displayname='another language'")
        self.assertEqual(first,self.sample())
        self.lua.execute('count=0;current=0;has_default=false')
        self.assertEqual(self.sample(),'ORDERS_1;0;0;default,none')

    def test_reused_buffers_start_zeroed_like_fresh_ones(self):
        self.lua.execute('''
          local write=native.GetDefaultOrder;calls=0
          native.GetDefaultOrder=function(buffer)
            calls=calls+1
            if calls==1 then buffer.isinfinite=true;buffer.istemporder=true end
            buffer.orderdef='Wait';buffer.state='inactive'
            return true
          end
        ''')
        first,second=self.sample(),self.sample()
        self.assertTrue(first.endswith('default,0,Wait,inactive,0,0,1,0,1'))
        self.assertTrue(second.endswith('default,0,Wait,inactive,0,0,0,0,0'))  # nothing left from the first read

    def test_partial_read_and_unbounded_or_ambiguous_data_fail_closed(self):
        for change in ('truncated=true', 'count=17', 'current=-1', "orderdef='bad;delimiter'",
                       "orderstate=string.rep('x',49)"):
            with self.subTest(change=change):
                self.setUp()
                self.lua.execute(change)
                with self.assertRaises(Exception): self.sample()


if __name__ == '__main__':
    unittest.main()
