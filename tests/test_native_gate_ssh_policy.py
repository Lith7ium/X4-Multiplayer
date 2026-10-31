from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest import mock

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/dev'))
import native_gate_ssh_policy as module


class SSHPolicyTests(unittest.TestCase):
    def policy(self):
        return module.SSHPolicy('root@192.0.2.10','/owned/bridge.py',
            [r'C:\Windows\System32\OpenSSH\ssh.exe','root@192.0.2.10','python3 /owned/bridge.py --remote'])

    def context(self,policy):
        endpoint=dict(proto='tcp',pid=20,local='192.0.2.20',lport=40000,remote='192.0.2.10',rport=22,state='ESTABLISHED')
        process=SimpleNamespace(pid=20,exe=str(policy.exe),cmdline=policy.cmdline,created=999,parent=10)
        return endpoint,process

    def test_exact_owned_command_and_destination_only(self):
        policy=self.policy();entry,process=self.context(policy)
        self.assertTrue(policy.accepts(entry,process,{10,20},10))
        self.assertEqual(policy.identity,(20,999,10))
        self.assertTrue(policy.accepts({**entry,'state':'CLOSE_WAIT'},process,{10,20},10))

    def test_every_other_destination_transport_and_unknown_identity_refused(self):
        for changes in ({'remote':'192.0.2.11'},{'rport':443},{'proto':'udp'}, {'proto':'tcp6'},{'state':'LISTEN'},{'pid':21}):
            policy=self.policy();entry,process=self.context(policy)
            self.assertFalse(policy.accepts({**entry,**changes},process,{10,20},10))
        for changes in ({'exe':r'C:\other\ssh.exe'},{'cmdline':'other command'},{'created':None},{'parent':30}):
            policy=self.policy();entry,process=self.context(policy);process.__dict__.update(changes)
            self.assertFalse(policy.accepts(entry,process,{10,20},10))
        policy=self.policy();entry,process=self.context(policy)
        self.assertFalse(policy.accepts(entry,None,{10,20},10))
        self.assertFalse(policy.accepts(entry,process,{10},10))

    def test_pid_reuse_or_replacement_process_is_not_implicitly_admitted(self):
        policy=self.policy();entry,process=self.context(policy)
        self.assertTrue(policy.accepts(entry,process,{10,20},10))
        process.created=1000
        self.assertFalse(policy.accepts(entry,process,{10,20},10))
        process.pid=21
        self.assertFalse(policy.accepts({**entry,'pid':21},process,{10,21},10))

    def test_literal_destination_required(self):
        with self.assertRaises(ValueError):module.SSHPolicy('root@not-a-literal-host','/owned/bridge.py',['ssh'])

    def test_wrapper_runs_original_audit_and_records_only_explicit_exception(self):
        policy=self.policy();entry,process=self.context(policy)
        class Violation(Exception):pass
        class Server:
            def __init__(self):self.result={};self.proc=SimpleNamespace(pid=10);self.audits=0
            def pids(self):return [10,20]
            def check_network(self):
                self.audits+=1;self.result.update(endpoints=[entry],violations=[entry]);raise Violation('outside loopback')
        lane=SimpleNamespace(Server=Server,NetworkViolation=Violation,loopback_endpoint=lambda e:e['local']=='127.0.0.1',
            win=lambda:SimpleNamespace(processes=lambda name:[process]))
        server=Server()
        with mock.patch.object(module,'SSHPolicy',return_value=policy):module.install(lane,'root@192.0.2.10','/owned/bridge.py')
        self.assertEqual(server.check_network(),[entry]);self.assertEqual(server.audits,1)
        self.assertEqual(server.result['violations'],[])
        self.assertEqual(server.result['authorized_transport_exception']['identity'],(20,999,10))
        entry['remote']='192.0.2.99'
        with self.assertRaises(Violation):server.check_network()
        self.assertEqual(server.audits,2)


if __name__=='__main__':unittest.main()
