import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/dev'))
from validate_native_gate_ssh import transport_errors


class SSHAcceptanceTests(unittest.TestCase):
    def setUp(self):
        policy = dict(kind='explicit_owned_ssh_only', remote_ipv4='192.0.2.10', remote_tcp_port=22,
                      command_sha256='a'*64, executable='C:/OpenSSH/ssh.exe')
        self.expected = dict(policy=policy, source_hashes={'native_gate_ssh.py': 'b'*64, 'native_gate_relay.py': 'c'*64})
        self.result = {'server': {'pid': 10, 'authorized_transport_exception': {**policy, 'identity': [11, 12345, 10],
                       'audit': [[dict(proto='tcp', remote='192.0.2.10', rport=22, pid=11, created=12345, state='ESTABLISHED')]]}}}
        self.barrier = {'transport': dict(kind='ssh_byte_preserving', native_linux_simulation=False, ssh_exit_code=0,
                        coordinator=dict(platform='Linux-test', hashes=self.expected['source_hashes'], pid=123),
                        traffic=dict(input_datagrams=25, output_datagrams=25, bytes_to_linux=2000, bytes_from_linux=2000))}

    def check(self):return transport_errors(self.result, self.barrier, self.expected)

    def test_accepts_recorded_owned_transport(self):self.assertEqual(self.check(), [])

    def test_nonempty_endpoint_audit_is_mandatory(self):
        policy=self.result['server']['authorized_transport_exception']
        for audit in (None, [], [[]]):
            with self.subTest(audit=audit):
                policy['audit']=audit
                self.assertTrue(self.check())

    def test_cannot_substitute_endpoint_process_or_command(self):
        original=copy.deepcopy(self.result)
        for field,value in [('proto','udp'),('remote','192.0.2.11'),('rport',443),('pid',12),('created',12346),('state','LISTEN')]:
            with self.subTest(field=field):
                self.result=copy.deepcopy(original)
                self.result['server']['authorized_transport_exception']['audit'][0][0][field]=value
                self.assertTrue(self.check())
        self.result=copy.deepcopy(original)
        self.result['server']['authorized_transport_exception']['command_sha256']='d'*64
        self.assertTrue(self.check())

    def test_incomplete_or_different_remote_is_not_a_pass(self):
        original=copy.deepcopy(self.barrier)
        for key,value in [('aborted',True),('ssh_exit_code',1),('native_linux_simulation',True),('kind','local')]:
            with self.subTest(field=key):
                self.barrier=copy.deepcopy(original);self.barrier['transport'][key]=value
                self.assertTrue(self.check())
        self.barrier=copy.deepcopy(original)
        self.barrier['transport']['coordinator']['hashes']={}
        self.assertTrue(self.check())


if __name__ == '__main__':unittest.main()
