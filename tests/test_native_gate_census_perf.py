"""census_perf native breakdown: FRAME_TIMING plus the script RNG self-time."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/dev'))
from census_perf import agree,native


def status(tick,last,entries,self_us):
    return (f'[X4LAN] NATIVE_GATE SCRIPT_RNG tick={tick} installed=1 active=1 mode=1 mask=3 cur_tick={tick} cur_entries=5 '
            f'last_tick={last} last_entries={entries} last_outer=2 last_child=1 last_unkeyed=0 last_draws=9 last_unknown=0 '
            f'last_keyset=0000000000000001 last_drawset=0000000000000002 rows=0 self_us=999 cur_self_us=3 last_self_us={self_us} '
            f'ai_hits=10 ai_misses=1 identity_sealed=1')


class CensusPerfTests(unittest.TestCase):
    def test_a_status_field_is_compared_tick_by_tick_between_the_clients(self):
        with tempfile.TemporaryDirectory() as root:
            run=Path(root)
            for slot,values in ((1,{5:'aa',6:'bb',7:'cc',8:'dd'}),(2,{5:'aa',6:'bb',7:'ee',9:'ff'})):
                client=run/f'client{slot}';(client/'installed').mkdir(parents=True)
                (client/'installed/x4_lan_lab-config.lua').write_text('')
                (client/'engine-output.log').write_text(''.join(
                    f'[X4LAN] NATIVE_GATE PATH_DETERMINISM tick={tick} armed=1 walk={value}\n' for tick,value in values.items()))
            self.assertEqual(agree(run,'PATH_DETERMINISM','walk'),dict(row='PATH_DETERMINISM',field='walk',compared=3,differing=1,
                first=7,values=dict(client1='cc',client2='ee')))
            self.assertEqual(agree(run,'PATH_DETERMINISM','armed')['differing'],0)

    def test_script_rng_self_time_is_the_median_of_completed_ticks(self):
        with tempfile.TemporaryDirectory() as root:
            run=Path(root);client=run/'client1';(client/'installed').mkdir(parents=True)
            (client/'installed/x4_lan_lab-config.lua').write_text('X4LAN_CONFIG.native_gate_world_hash_stride = 10\n')
            lines=[f'[X4LAN] NATIVE_GATE FRAME_TIMING tick={t} frames=1 wait_us=1000 frame_us=20000 outside_us=500' for t in range(1,13)]
            lines+=[status(4,3,3000,150),status(5,4,3100,170),status(6,5,2900,160),status(11,10,9999,90000),status(12,11,9999,90000),
                    status(7,6,2800,140)]
            (client/'engine-output.log').write_text('\n'.join(lines)+'\n')
            result=native(run)['client1']
            self.assertEqual(result['script_rng_entries'],2950)
            self.assertEqual(result['script_rng_ms'],0.155)
            self.assertEqual(result['frametick_ms'],20.0)
            self.assertNotIn('pre_ms',result)

    def test_gate_pre_work_and_reads_when_the_gate_reports_them(self):
        with tempfile.TemporaryDirectory() as root:
            run=Path(root);client=run/'client1';(client/'installed').mkdir(parents=True)
            (client/'installed/x4_lan_lab-config.lua').write_text('')
            lines=[f'[X4LAN] NATIVE_GATE FRAME_TIMING tick={t} frames=1 wait_us=100 frame_us=2000 outside_us=30000 sum_wait_us=0 '
                   f'sum_frame_us=0 sum_outside_us=0 pre_us={3000+t} created_us=400 join_us=2500 sum_pre_us=0 reads={900+t} probe_us=1500'
                   for t in range(3,8)]
            (client/'engine-output.log').write_text('\n'.join(lines)+'\n')
            result=native(run)['client1']
            self.assertEqual((result['pre_ms'],result['created_ms'],result['join_ms'],result['reads']),(3.0,0.4,2.5,905))
            self.assertEqual(result['rest_ms'],25.5)  # outside - probe - pre


if __name__=='__main__':
    unittest.main()
