"""Explicit, narrow WAN allowance for the authorized VPS native gate test.

The released lane implementation remains unchanged. Its full server job endpoint
audit still runs; only the configured direct child OpenSSH command may connect to
one literal IPv4 address on TCP22. No detached sidecar or generic network bypass.
"""
import hashlib
import ipaddress
from pathlib import PureWindowsPath
import subprocess

from native_gate_ssh import ssh_command


class SSHPolicy:
    def __init__(self,host,script,command=None):
        self.address=str(ipaddress.IPv4Address(host.rsplit('@',1)[-1]))
        self.command=command or ssh_command(host,script)
        self.exe=PureWindowsPath(self.command[0])
        self.cmdline=subprocess.list2cmdline(self.command)
        self.identity=None

    def accepts(self,entry,process,owned_pids,parent):
        valid=(entry.get('proto')=='tcp' and entry.get('remote')==self.address and entry.get('rport')==22
                and entry.get('state') in ('SYN_SENT','ESTABLISHED','CLOSE_WAIT','FIN_WAIT1','FIN_WAIT2','LAST_ACK','CLOSING','TIME_WAIT')
                and process is not None and entry.get('pid')==process.pid and process.pid in owned_pids
                and process.parent==parent and process.created is not None and process.exe is not None
                and PureWindowsPath(process.exe)==self.exe and process.cmdline==self.cmdline)
        if not valid:return False
        identity=(process.pid,process.created,parent)
        if self.identity is None:self.identity=identity
        return self.identity==identity

    def description(self):
        return {'kind':'explicit_owned_ssh_only','remote_ipv4':self.address,'remote_tcp_port':22,
                'executable':str(self.exe),'command_sha256':hashlib.sha256(self.cmdline.encode()).hexdigest()}


def install(lane,host,script):
    policy=SSHPolicy(host,script)
    original=lane.Server.check_network
    def check(server):
        try:return original(server)
        except lane.NetworkViolation:
            found=server.result.get('endpoints',[])
            owned=set(server.pids())
            processes={p.pid:p for p in lane.win().processes('ssh.exe')}
            exceptions=[]
            for entry in found:
                if lane.loopback_endpoint(entry):continue
                if not policy.accepts(entry,processes.get(entry.get('pid')),owned,server.proc.pid):raise
                exceptions.append({**entry,'created':processes[entry['pid']].created})
            if not exceptions:raise
            previous=server.result.get('authorized_transport_exception',{})
            audit=previous.get('audit',[])
            if len(audit)>=1024:raise
            audit.append(exceptions)
            server.result['authorized_transport_exception']={**policy.description(),'identity':policy.identity,'audit':audit}
            server.result['violations']=[]
            return found
    lane.Server.check_network=check
