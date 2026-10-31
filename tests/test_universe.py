"""W01 fault injection and cross-language interoperability; no game simulation."""
import random
from pathlib import Path
import unittest

from x4lan.universe import REPEAT_BUDGET, Baseline, Receiver, Sender, UniverseSession, record
from test_game_adapter import LuaRuntime
from test_host import TOKEN, P1, P2, FOREIGN

UI = Path(__file__).resolve().parents[1] / 'src/extension/x4_lan_lab/ui'
EPOCH = 'a' * 32
SECTOR = 'cluster_14_sector001_macro'
OTHER = 'cluster_01_sector001_macro'


def row(i=1, kind='ship'):
    return f'{i},{kind},ship_arg_s_scout_01_a_macro,argon,{i},2,3,0,.1,-.1,' + ('none,0' if kind == 'ship' else 'energycells,20')


def fields(b, i):
    return b.packet('b' * 32, i).decode().split('|')


def commit(receiver, baseline, now=0):
    result = None
    for i in range(len(baseline.rows) + 2):
        _, result = receiver.receive(fields(baseline, i), now)
    return result


class UniverseProtocolTests(unittest.TestCase):
    def test_partial_and_early_commit_never_replace_visible_world(self):
        r = Receiver(); old = Baseline.create(EPOCH, 1, SECTOR, [row()]); commit(r, old)
        new = Baseline.create(EPOCH, 2, SECTOR, [row(2), row(3)])
        r.receive(fields(new, 0), 1); r.receive(fields(new, 2), 1)
        self.assertEqual(r.receive(fields(new, 3), 1), (None, None))
        self.assertEqual(r.latest, old)
        r.receive(fields(new, 1), 1)
        self.assertEqual(r.receive(fields(new, 3), 1)[1], new)

    def test_committed_empty_sector_is_distinct_from_missing_data(self):
        r = Receiver(); commit(r, Baseline.create(EPOCH, 1, SECTOR, [row()]))
        empty = Baseline.create(EPOCH, 2, SECTOR, [])
        r.receive(fields(empty, 0), 1); self.assertEqual(len(r.latest.rows), 1)
        self.assertEqual(r.receive(fields(empty, 1), 1)[1], empty)

    def test_replay_and_epoch_change_cannot_resurrect_removed_objects(self):
        r = Receiver(); old = Baseline.create(EPOCH, 1, SECTOR, [row()])
        commit(r, old); gone = Baseline.create(EPOCH, 2, SECTOR, []); commit(r, gone)
        commit(r, old); commit(r, Baseline.create('c' * 32, 3, SECTOR, [row()]))
        self.assertEqual(r.latest, gone)
        self.assertEqual(r.receive(fields(gone, 1), 3), (1, None))

    def test_corruption_conflicting_fragments_and_duplicate_ids_request_restart(self):
        for mode in ('checksum', 'conflict', 'duplicate'):
            with self.subTest(mode=mode):
                r = Receiver(); b = Baseline.create(EPOCH, 1, SECTOR, [row(), row(2)])
                r.receive(fields(b, 0), 0); r.receive(fields(b, 1), 0)
                f = fields(b, 2)
                if mode == 'checksum': f[-1] = row(3)
                if mode == 'duplicate': f[-1] = row()
                r.receive(f, 0)
                if mode == 'conflict':
                    f = fields(b, 1); f[-1] = row(4)
                    result = r.receive(f, 0)
                else: result = r.receive(fields(b, 3), 0)
                self.assertEqual(result, (-1, None)); self.assertIsNone(r.latest)

    def test_pending_transfer_cannot_be_evicted_and_expires(self):
        r = Receiver(); b = Baseline.create(EPOCH, 1, SECTOR, [row()])
        newer = Baseline.create(EPOCH, 2, OTHER, [])
        r.receive(fields(b, 0), 0)
        self.assertEqual(r.receive(fields(newer, 0), 1), (None, None))
        self.assertEqual(r.receive(fields(b, 1), 16), (-1, None))
        self.assertEqual(commit(r, newer, 17), newer)

    def test_invalid_records_and_overflow_fail_without_truncation(self):
        for bad in [row().replace(',1,2,3,', ',nan,2,3,'), row().replace(',1,2,3,', ',50000001,2,3,'),
                    row().replace('argon', 'bad owner'), row().replace('none,0', 'energycells,1'),
                    row(0), row(kind='drop').replace('energycells,20', 'none,0')]:
            with self.subTest(bad=bad), self.assertRaises(ValueError): record(bad)
        for rows in [[row(2), row(1)], [row(), row()], [row(i + 1) for i in range(4097)]]:
            with self.assertRaises(ValueError): Baseline.create(EPOCH, 1, SECTOR, rows)
        b = Baseline.create(EPOCH, 1, SECTOR, [row(kind='drop')])
        self.assertEqual(commit(Receiver(), b), b)

    def test_loss_reordering_duplicates_and_lost_final_ack_converge(self):
        for seed in range(8):
            rng = random.Random(seed); b = Baseline.create(EPOCH, 1, SECTOR, [row(i) for i in range(1, 65)])
            sender, receiver = Sender(b), Receiver(); queue=[]; commits=0; lost_final=False
            for tick in range(1800):
                now=tick*.05
                for p in sender.packets('b'*32, now):
                    if rng.random() > .25: queue.append((tick+rng.randrange(8), p))
                ready=[p for at,p in queue if at<=tick]; queue=[(at,p) for at,p in queue if at>tick]
                rng.shuffle(ready)
                for p in ready:
                    f=p.decode().split('|'); ack, done=receiver.receive(f,now)
                    commits += done is not None
                    if ack==65 and not lost_final: lost_final=True;continue
                    if ack is not None and rng.random()>.25:
                        sender.acknowledge(f[3],int(f[4]),ack)
                if sender.done: break
            self.assertTrue(sender.done,seed); self.assertEqual(receiver.latest,b); self.assertEqual(commits,1)

    def test_unsent_or_foreign_ack_cannot_finish_sender(self):
        b=Baseline.create(EPOCH,1,SECTOR,[row()]); s=Sender(b)
        s.acknowledge(EPOCH,1,2); self.assertFalse(s.done)
        s.packets('b'*32,0); s.acknowledge('f'*32,1,0)
        self.assertFalse(s.acked)
        s.acknowledge(EPOCH,1,0); self.assertEqual(len(s.packets('b'*32,1)),1)


class UniverseRelayTests(unittest.TestCase):
    def setUp(self):
        self.s=UniverseSession(TOKEN,'world1-test','test')
        self.a=self.join('p1',P1,'1'*32); self.b=self.join('p2',P2,'2'*32)

    def join(self,slot,address,nonce):
        packet=f'HELLO|1|{TOKEN}|world1-test|{SECTOR}|test|{slot}|{nonce}'.encode()
        return self.s.receive(packet,address,0)[0].decode().split('|')[3]

    def publish(self,b):
        for i in range(len(b.rows)+2): self.s.receive(b.packet(self.a,i),P1,.1)

    def test_only_host_epoch_and_endpoint_can_publish(self):
        b=Baseline.create(self.a,1,SECTOR,[row()])
        for i in range(3):
            self.s.receive(b.packet(self.b,i),P2,.1); self.s.receive(b.packet(self.a,i),FOREIGN,.1)
        self.assertIsNone(self.s.universe.latest)
        self.publish(b); self.assertEqual(self.s.universe.latest,b)
        self.assertFalse(self.s.receive(b'UBEGIN|1|'+b'x'*800,P1,.2))

    def test_late_guest_gets_latest_baseline_and_sector_change_is_scoped(self):
        first=Baseline.create(self.a,1,SECTOR,[row()]); self.publish(first)
        other=Baseline.create(self.a,2,OTHER,[row(2)]); self.publish(other)
        self.s.receive(f'UINTEREST|1|{self.b}|1|{SECTOR}'.encode(),P2,.2)
        packets=self.s.snapshots(.3)
        self.assertEqual(self.s.delivery.baseline,first)
        self.assertTrue(any(a==P1 and p.startswith(b'UINTEREST') for a,p in packets))
        self.s.receive(f'UINTEREST|1|{self.b}|2|{OTHER}'.encode(),P2,.4)
        self.s.snapshots(.5);self.assertIsNone(self.s.delivery)
        fresh=Baseline.create(self.a,3,OTHER,[row(2)]);self.publish(fresh)
        self.s.snapshots(.6);self.assertEqual(self.s.delivery.baseline,fresh)

    def test_returning_to_a_sector_waits_for_fresh_capture_instead_of_deadlocking_on_cache(self):
        receiver=Receiver()
        def visit(seq,sector,baseline,now):
            self.s.receive(f'UINTEREST|1|{self.b}|{seq}|{sector}'.encode(),P2,now)
            self.s.snapshots(now);self.assertIsNone(self.s.delivery)
            self.publish(baseline)
            for i in range(12):
                for address,packet in self.s.snapshots(now+i*.05):
                    if address!=P2 or not packet.startswith((b'UBEGIN|',b'UOBJECT|',b'UCOMMIT|')):continue
                    f=packet.decode().split('|');ack,_=receiver.receive(f,now+i*.05)
                    if ack is not None:self.s.receive(f'UACK|1|{self.b}|{f[3]}|{f[4]}|{ack}'.encode(),P2,now+i*.05)
            self.assertEqual(receiver.latest,baseline)
            self.assertEqual(self.s.delivered,baseline.revision)
        visit(1,SECTOR,Baseline.create(self.a,1,SECTOR,[row()]),1)
        visit(2,OTHER,Baseline.create(self.a,2,OTHER,[row(2)]),2)
        visit(3,SECTOR,Baseline.create(self.a,3,SECTOR,[row(3)]),3)

    def test_motion_cannot_create_objects_or_cross_authority(self):
        b=Baseline.create(self.a,1,SECTOR,[row()]);self.publish(b)
        def position(lease,id=1,seq=1): return f'UPOS|1|{lease}|{self.a}|{id}|{seq}|{SECTOR}|10|0|0|0|0|0'.encode()
        self.s.receive(position(self.b),P2,.2);self.s.receive(position(self.a,99),P1,.2)
        self.assertFalse(self.s.positions)
        self.s.receive(position(self.a),P1,.2);self.assertIn(1,self.s.positions)
        self.s.receive(f'UINTEREST|1|{self.b}|1|{SECTOR}'.encode(),P2,.2)
        self.assertTrue(any(p.startswith(b'UPOS') for a,p in self.s.snapshots(.3) if a==P2))
        self.assertFalse(any(p.startswith(b'UPOS') for a,p in self.s.snapshots(1.1) if a==P2))
        self.publish(Baseline.create(self.a,2,SECTOR,[]));self.assertFalse(self.s.positions)

    def test_world_build_is_separate(self):
        with self.assertRaises(ValueError): UniverseSession(TOKEN,'test','test')

    def test_velocity_motion_validates_authority_clock_bounds_and_removal(self):
        self.publish(Baseline.create(self.a,1,SECTOR,[row()]))
        self.s.receive(f'UINTEREST|1|{self.b}|1|{SECTOR}'.encode(),P2,.2)
        def packet(lease,seq=1,time='10',vx='100'):
            return f'UMOVE|1|{lease}|{self.a}|1|{seq}|{SECTOR}|{time}|10|0|0|0|0|0|{vx}|0|0|0|0|0'.encode()
        for p,addr in [(packet(self.b),P2),(packet(self.a),FOREIGN),
                       (packet(self.a,vx='nan'),P1),(packet(self.a,time='inf'),P1),
                       (packet(self.a,vx='100001'),P1)]:
            self.s.receive(p,addr,.2)
        self.assertFalse(self.s.positions)
        self.s.receive(packet(self.a),P1,.2)
        forwarded=[p for a,p in self.s.snapshots(.3) if a==P2 and p.startswith(b'UMOVE')]
        self.assertEqual(forwarded,[packet(self.b)])
        self.s.receive(packet(self.a,2,time='9'),P1,.4)
        self.assertEqual(self.s.positions[1][0],1)
        self.publish(Baseline.create(self.a,2,SECTOR,[]))
        self.assertFalse(self.s.positions)

    def test_dense_sector_forwards_new_motion_before_repeats(self):
        # NPC flicker: a blind round robin over 200 live entities repeated unchanged samples
        # while new ones waited up to 200/640 s; fresh samples now go first.
        self.publish(Baseline.create(self.a,1,SECTOR,[row(i) for i in range(1,201)]))
        self.s.receive(f'UINTEREST|1|{self.b}|1|{SECTOR}'.encode(),P2,.2)
        def packet(entity,seq,time):
            return f'UMOVE|1|{self.a}|{self.a}|{entity}|{seq}|{SECTOR}|{time}|10|0|0|0|0|0|100|0|0|0|0|0'.encode()
        def forwarded(now):
            return [int(p.decode().split('|')[4]) for a,p in self.s.snapshots(now) if a==P2 and p.startswith(b'UMOVE')]
        for i in range(1,201): self.s.receive(packet(i,i,1),P1,.2)
        seen=set()
        for tick in range(7): seen.update(forwarded(.25+tick*.05))
        self.assertEqual(seen,set(range(1,201)))  # each fresh sample exactly once first
        for n,entity in enumerate((150,7,93)): self.s.receive(packet(entity,300+n,2),P1,.6)
        self.assertEqual(sorted(forwarded(.61)[:3]),[7,93,150])
        repeats=forwarded(.66)  # nothing fresh: a small budget repeats latest samples for loss
        self.assertEqual(len(repeats),REPEAT_BUDGET)
        later=forwarded(.71)  # round robin, not the same entities every tick
        self.assertEqual(len(later),REPEAT_BUDGET);self.assertFalse(set(later)&set(repeats))


@unittest.skipUnless(LuaRuntime,'LuaJIT unavailable')
class UniverseLuaTests(unittest.TestCase):
    def setUp(self):
        self.lua=LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute((UI/'universe_protocol.lua').read_text())
        self.p=self.lua.globals().X4LAN_UNIVERSE_PROTOCOL

    def test_python_sender_to_lua_receiver_with_loss(self):
        b=Baseline.create(EPOCH,1,SECTOR,[row(i,'drop' if i%2 else 'ship') for i in range(1,50)])
        s=Sender(b);r=self.p.receiver();rng=random.Random(912);commits=0
        for i in range(1000):
            for packet in s.packets('b'*32,i*.05):
                if rng.random()<.2: continue
                result=r.receive(r,self.lua.table_from(packet.decode().split('|')),i*.05)
                ack,done=result if isinstance(result,tuple) else (result,None)
                commits+=done is not None
                if ack is not None and rng.random()>.2:s.acknowledge(EPOCH,1,int(ack))
            if s.done:break
        self.assertTrue(s.done);self.assertEqual(commits,1)
        self.assertEqual(tuple(r.latest.rows.values()),b.rows);self.assertEqual(r.latest.digest,b.digest)

    def test_lua_sender_to_python_receiver_and_maximum_size(self):
        rows=[row(i) for i in range(1,4097)]
        b=self.p.baseline(EPOCH,1,SECTOR,self.lua.table_from(rows));s=self.p.sender(b);r=Receiver()
        for i in range(2000):
            for packet in s.packets(s,'b'*32,i*.05).values():
                self.assertLessEqual(len(packet),768)
                f=packet.split('|');ack,done=r.receive(f,i*.05)
                if ack is not None:s.ack(s,EPOCH,1,ack)
            if s.done:break
        self.assertTrue(s.done);self.assertEqual(r.latest.rows,tuple(rows))

    def test_lua_empty_commit_replay_and_wrong_epoch(self):
        self.lua.execute('''
         local P=X4LAN_UNIVERSE_PROTOCOL
         local b=P.baseline(string.rep('a',32),1,'sector_macro',{})
         local r=P.receiver()
         r:receive(P.split(P.packet(b,string.rep('b',32),0),'|'),0)
         assert(r.latest==nil)
         local ack,done=r:receive(P.split(P.packet(b,string.rep('b',32),1),'|'),0)
         assert(ack==1 and done and #r.latest.rows==0)
         ack,done=r:receive(P.split(P.packet(b,string.rep('b',32),1),'|'),1)
         assert(ack==1 and done==nil)
         b.epoch=string.rep('c',32);b.revision=2
         assert(r:receive(P.split(P.packet(b,string.rep('b',32),0),'|'),2)==nil)
        ''')
