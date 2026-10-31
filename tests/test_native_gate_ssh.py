import importlib.util
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/dev'))
from native_gate_ssh import remote_config,remote_packet,ssh_command


class SSHCoordinatorTests(unittest.TestCase):
    def config(self):
        return {'token':'a'*32,'ticks':12,'hold_tick':5,'hold_seconds':.3,'relative_real':True,'lifetime':30}

    def test_original_bytes_and_independent_peer_identity_survive_envelope(self):
        original=b'NGSTATE|2|'+b'a'*32+b'|2|0|'+b'0'*16+b'|'+b'0'*16+b'|'+b'b'*64+b'|8401c800'
        peer,data=remote_packet({'kind':'packet','peer':2,'hex':original.hex()})
        self.assertEqual((peer,data),(2,original))

    def test_transport_cannot_select_foreign_addresses_or_unbounded_payloads(self):
        bad=[{'kind':'packet','peer':0,'hex':''},{'kind':'packet','peer':True,'hex':''},
             {'kind':'packet','peer':33,'hex':''},{'kind':'packet','peer':1,'hex':'z0'},
             {'kind':'packet','peer':1,'hex':'a'},{'kind':'packet','peer':1,'hex':'00'*513},
             {'kind':'packet','peer':1,'hex':'','address':'elsewhere'}]
        for message in bad:
            with self.subTest(message=message):
                with self.assertRaises(ValueError):remote_packet(message)

    def test_remote_requires_bounded_exact_configuration(self):
        self.assertEqual(remote_config(self.config()).limit,12)
        for changes in ({'lifetime':0},{'lifetime':3601},{'ticks':18001},{'ticks':True},
                        {'hold_seconds':float('nan')},{'relative_real':1},{'token':'short'}, {'unknown':1}):
            with self.subTest(changes=changes):
                with self.assertRaises((ValueError,TypeError)):remote_config({**self.config(),**changes})

    def test_two_virtual_endpoints_are_still_subject_to_original_slot_pin(self):
        b=remote_config(self.config())
        def message(slot,digest='b'*64):
            return f"NGSTATE|2|{'a'*32}|{slot}|0|{'0'*16}|{'0'*16}|{digest}|00000000".encode()
        self.assertEqual(b.receive(message(1),('127.0.0.1',20001),0),[])
        self.assertEqual(b.receive(message(1),('127.0.0.1',20003),0),[])
        self.assertEqual(len(b.receive(message(2),('127.0.0.1',20002),0)),2)
        self.assertEqual(b.peers[1],('127.0.0.1',20001))

    def test_ssh_configuration_cannot_escape_owned_transport(self):
        command=ssh_command('root@192.0.2.10','/owned/bridge.py')
        self.assertEqual(command[command.index('-p')+1],'22');self.assertIn('-T',command)
        options={command[i+1] for i,value in enumerate(command) if value=='-o'}
        required={'ControlMaster=no','ControlPath=none','ControlPersist=no','ProxyCommand=none','ProxyJump=none',
                  'PermitLocalCommand=no','ClearAllForwardings=yes','ForwardAgent=no','ForwardX11=no',
                  'ForwardX11Trusted=no','Tunnel=no','RemoteCommand=none','ExitOnForwardFailure=yes'}
        self.assertTrue(required<=options)
        with self.assertRaises(ValueError):ssh_command('-v','/owned/bridge.py')


if __name__=='__main__':unittest.main()
