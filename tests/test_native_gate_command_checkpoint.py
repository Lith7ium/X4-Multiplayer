"""Native action failures must retain proposals before their admission reply."""
import json
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest


class CommandCheckpointTests(unittest.TestCase):
    def test_nonzero_proposals_are_durable_before_release(self):
        root=Path(__file__).resolve().parents[1]
        token='a'*32
        with tempfile.TemporaryDirectory() as directory, socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as reservation:
            reservation.bind(('127.0.0.1',0))
            port=reservation.getsockname()[1]
            reservation.close()
            report=Path(directory)/'report.json'
            process=subprocess.Popen([sys.executable,str(root/'tools/dev/native_gate_relay.py'),
                                      '--port',str(port),'--token',token,'--hold-tick','99',
                                      '--report',str(report)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            sockets=[]
            try:
                self.assertEqual(process.stdout.readline().strip(),'NATIVE_GATE RELAY READY')
                for _ in (1,2):
                    peer=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
                    peer.bind(('127.0.0.1',0));peer.settimeout(5);sockets.append(peer)
                for tick in (0,1):
                    clock=struct.pack('>d',tick/60).hex()
                    command='81010000' if tick else '00000000'
                    for slot,peer in enumerate(sockets,1):
                        peer.sendto(f'NGSTATE|2|{token}|{slot}|{tick}|{clock}|{clock}|'.encode()+b'0'*64+b'|'+command.encode(),('127.0.0.1',port))
                    for peer in sockets:
                        reply=peer.recv(1024).decode()
                        self.assertIn(f'|{tick+1}|{command}|{command}',reply)
                saved=json.loads(report.read_text())
                self.assertEqual(saved['common_ticks'],[0,1])
                self.assertEqual([saved['states']['1'][str(s)]['command'] for s in (1,2)],['81010000']*2)
                self.assertEqual(saved['verdict'],'incomplete')
            finally:
                for peer in sockets:peer.close()
                process.terminate();process.communicate(timeout=5)

    def test_input_journal_holds_each_released_tick_before_its_permit(self):
        """--input-journal: one flushed line per released tick instead of rewriting the full report per release."""
        root=Path(__file__).resolve().parents[1]
        token='a'*32
        with tempfile.TemporaryDirectory() as directory, socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as reservation:
            reservation.bind(('127.0.0.1',0))
            port=reservation.getsockname()[1]
            reservation.close()
            report,journal=Path(directory)/'report.json',Path(directory)/'inputs.jsonl'
            process=subprocess.Popen([sys.executable,str(root/'tools/dev/native_gate_relay.py'),
                                      '--port',str(port),'--token',token,'--hold-tick','99','--ticks','3',
                                      '--report',str(report),'--input-journal',str(journal)],
                                     stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            sockets=[]
            try:
                self.assertEqual(process.stdout.readline().strip(),'NATIVE_GATE RELAY READY')
                for _ in (1,2):
                    peer=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
                    peer.bind(('127.0.0.1',0));peer.settimeout(5);sockets.append(peer)
                written=[]
                for tick in range(3):
                    clock=struct.pack('>d',tick/60).hex()
                    blobs=['01%02x' % (16*slot+tick) for slot in (1,2)]
                    for slot,peer in enumerate(sockets,1):
                        peer.sendto(f'NGSTATE|3|{token}|{slot}|{tick}|{clock}|{clock}|'.encode()+b'0'*64+f'|2|{blobs[slot-1]}'.encode(),
                                    ('127.0.0.1',port))
                    for peer in sockets:
                        self.assertEqual(peer.recv(1024).decode(),f'NGGO|3|{token}|{sockets.index(peer)+1}|{tick+1}|{blobs[0]}|{blobs[1]}')
                    # The permit's inputs were on disk before it was sent.
                    lines=[json.loads(line) for line in journal.read_text().splitlines()]
                    self.assertEqual(lines[-1]['tick'],tick)
                    self.assertEqual([lines[-1]['rows'][str(s)]['blob'] for s in (1,2)],blobs)
                    written.append(json.loads(report.read_text())['common_ticks'])
                self.assertEqual([line['tick'] for line in lines],[0,1,2])
                # The full report is a checkpoint (written at the first report, then every 60 s), not per release.
                self.assertEqual(written,[[]]*3)
                # The last tick stops everyone and writes the full report.
                clock=struct.pack('>d',3/60).hex()
                for slot,peer in enumerate(sockets,1):
                    peer.sendto(f'NGSTATE|3|{token}|{slot}|3|{clock}|{clock}|'.encode()+b'0'*64+b'|2|',('127.0.0.1',port))
                for peer in sockets: self.assertTrue(peer.recv(1024).decode().startswith('NGSTOP|3|'))
                process.stdout.readline()
                for _ in range(50):
                    saved=json.loads(report.read_text())
                    if saved['verdict']=='pass': break
                    time.sleep(0.1)
                self.assertEqual(saved['common_ticks'],[0,1,2,3])
                self.assertEqual(saved['states']['2']['2']['blob'],'0122')
            finally:
                for peer in sockets:peer.close()
                process.terminate();process.communicate(timeout=5)


if __name__=='__main__':unittest.main()
