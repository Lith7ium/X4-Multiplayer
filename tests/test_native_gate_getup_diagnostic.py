"""Closed one-native contract tests. No game or staging."""
from copy import deepcopy
import hashlib
import math
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/dev'))
from native_getup_diagnostic import Diagnostic,CONTRACT,decode,header
from native_gate_relay import Barrier

TOKEN='a'*32
RUN=hashlib.sha256(TOKEN.encode()).hexdigest()
PEER=('127.0.0.1',62000)

def state(tick,game=None,real=None):
    bits=lambda value:struct.pack('>d',value).hex()
    return (header('NGDSTATE',TOKEN,RUN,tick)+'|'+bits(20+tick/60 if game is None else game)+'|'+
            bits(80+tick/60 if real is None else real)+'|'+'b'*64+'|'+('87010000' if tick==29 else '00000000')).encode()

def done(tick=120,tail='1|1|121|121|0|0'):
    return (header('NGDDONE',TOKEN,RUN,tick)+'|'+tail).encode()

class DiagnosticTests(unittest.TestCase):
    def completed(self):
        d=Diagnostic(TOKEN)
        for tick in range(121):
            replies=d.receive(state(tick),PEER,float(tick));self.assertEqual(len(replies),1)
            kind,row=decode(replies[0][1],TOKEN,RUN)
            self.assertEqual(kind,'NGDSTOP' if tick==120 else 'NGDGO')
            self.assertEqual(row['tick'],tick if tick==120 else tick+1)
            if tick==29:self.assertEqual(row['command'],'87010000')
        self.assertIsNone(d.failed);return d

    def test_one_real_state_stream_and_terminal_ack_cannot_claim_parity(self):
        d=self.completed();reply=d.receive(done(),PEER,121)[0][1]
        self.assertEqual(decode(reply,TOKEN,RUN)[0],'NGDCLOSED')
        summary=d.summary();self.assertTrue(summary['local_transport_valid'])
        for key in ('session_pass','parity_checked','independent_native_players','completed_release'):self.assertIs(summary[key],False)
        self.assertEqual(summary['native_clients'],1);self.assertEqual(len(summary['states']),121)
        self.assertTrue(all('2' not in row for row in summary['states'].values()))
        self.assertEqual(d.receive(done(),PEER,122),[(PEER,reply)])
        self.assertEqual(d.receive(state(29),PEER,123),[])

    def test_all_local_tags_refuse_in_paired_barrier_and_ordinary_tags_refuse_local(self):
        packets=[state(0),done(),header('NGDGO',TOKEN,RUN,30).encode()+b'|87010000',
                 header('NGDSTOP',TOKEN,RUN,120).encode()+b'|1',done().replace(b'NGDDONE',b'NGDCLOSED')]
        for packet in packets:
            with self.subTest(packet=packet.split(b'|')[0]):
                b=Barrier(TOKEN);self.assertEqual(b.receive(packet,PEER,0),[]);self.assertEqual(b.states,{})
        for kind in ('NGSTATE','NGGO','NGSTOP','NGDONE','NGCLOSED'):
            d=Diagnostic(TOKEN);self.assertEqual(d.receive(state(0).replace(b'NGDSTATE',kind.encode()),PEER,0),[])
            self.assertEqual(d.failed,'invalid_packet');self.assertFalse(d.summary()['local_transport_valid'])

    def test_canonical_typed_contract_and_clock_mutations_refuse(self):
        parts=state(0).decode().split('|')
        mutations={1:['0','01','2'],2:['agreed-getup','diagnostic-getup-2'],3:['c'*32],4:['d'*64],
                   5:['0','2','01'],6:['0','2','01'],7:['-1','+0','00','121','18446744073709551616'],
                   8:[struct.pack('>d',x).hex() for x in (math.nan,math.inf,-1)],9:['0'*15,'F'*16],
                   10:['b'*63,'B'*64],11:['87010000','0000000','000000000']}
        for index,values in mutations.items():
            for value in values:
                with self.subTest(index=index,value=value):
                    f=parts.copy();f[index]=value;d=Diagnostic(TOKEN)
                    self.assertEqual(d.receive('|'.join(f).encode(),PEER,0),[]);self.assertIsNotNone(d.failed)
        for value in (state(0)+b'|0',state(0)+b'\x00',state(0)+b'\n',b'x'*512):
            with self.assertRaises(ValueError):decode(value,TOKEN,RUN)

    def test_extra_endpoint_held_mutation_gap_and_nonfinite_clocks_fail(self):
        d=Diagnostic(TOKEN);d.receive(state(0),PEER,0)
        self.assertEqual(d.receive(state(0),('127.0.0.1',62001),1),[]);self.assertEqual(d.failed,'extra_endpoint')
        d=Diagnostic(TOKEN);d.receive(state(0),PEER,0)
        reply=d.receive(state(0).replace(b'b'*64,b'c'*64),PEER,1)
        self.assertEqual(decode(reply[0][1],TOKEN,RUN)[1]['reason'],76)
        for packet in (state(2),state(1,game=21),state(1,real=81)):
            d=Diagnostic(TOKEN);d.receive(state(0),PEER,0);d.receive(packet,PEER,1);self.assertIsNotNone(d.failed)

    def test_missing_and_changed_terminal_counts_never_pass(self):
        baseline=self.completed();self.assertFalse(baseline.summary()['local_transport_valid'])
        for tail in ('1|0|121|121|0|0','76|1|121|121|0|0','1|1|120|120|0|0','1|1|121|120|1|0','1|1|121|121|0|1'):
            d=deepcopy(baseline);d.receive(done(tail=tail),PEER,121);self.assertEqual(d.failed,'terminal_incomplete')
        baseline.receive(done(),PEER,121);baseline.receive(done(tail='1|0|121|121|0|0'),PEER,122)
        self.assertEqual(baseline.failed,'changed_terminal')
        d=Diagnostic(TOKEN);d.receive(done(),PEER,0);self.assertEqual(d.failed,'initial_state_missing')

if __name__=='__main__':unittest.main()
