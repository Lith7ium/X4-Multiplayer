"""One-native loopback diagnostic coordinator; never a two-player barrier."""
import argparse
import hashlib
import hmac
import json
import math
from pathlib import Path
import re
import socket
import struct
import time

CONTRACT='diagnostic-getup-1'
LIMIT=120
MAX_PACKET=512
FALSE_CLAIMS=dict(session_pass=False,parity_checked=False,independent_native_players=False,completed_release=False)

def hex_field(value,width):
    if not isinstance(value,str) or re.fullmatch(r'[0-9a-f]{'+str(width)+'}',value) is None:raise ValueError('hex field')
    return value

def integer(value,maximum):
    if not isinstance(value,str) or re.fullmatch(r'0|[1-9][0-9]{0,19}',value) is None:raise ValueError('integer grammar')
    number=int(value)
    if number>maximum:raise ValueError('integer bound')
    return number

def clock(value):
    number=struct.unpack('>d',bytes.fromhex(hex_field(value,16)))[0]
    if not math.isfinite(number) or number<0:raise ValueError('finite clock')
    return number

def header(tag,token,run,tick):
    return f'{tag}|1|{CONTRACT}|{token}|{run}|1|1|{tick}'

def decode(data,token,run):
    if not isinstance(data,bytes) or not 0<len(data)<MAX_PACKET:raise ValueError('packet capacity')
    fields=data.decode('ascii').split('|')
    if len(fields)<8 or fields[1:3]!=['1',CONTRACT] or not hmac.compare_digest(fields[3],token) or not hmac.compare_digest(fields[4],run) or fields[5:7]!=['1','1']:
        raise ValueError('contract/run/epoch/slot')
    kind=fields[0];tick=integer(fields[7],LIMIT)
    if kind=='NGDSTATE' and len(fields)==12:
        row=dict(tick=tick,game_bits=hex_field(fields[8],16),real_bits=hex_field(fields[9],16),
                 digest=hex_field(fields[10],64),proposal=hex_field(fields[11],8))
        clock(row['game_bits']);clock(row['real_bits'])
        if row['proposal']!=('87010000' if tick==29 else '00000000'):raise ValueError('proposal')
        return kind,row
    if kind in ('NGDGO','NGDSTOP') and len(fields)==9:
        if kind=='NGDGO':
            if not tick or fields[8]!=('87010000' if tick==30 else '00000000'):raise ValueError('grant command')
            return kind,dict(tick=tick,command=fields[8])
        reason=integer(fields[8],2**32-1)
        if not reason or (reason==1 and tick!=LIMIT):raise ValueError('stop reason')
        return kind,dict(tick=tick,reason=reason)
    if kind in ('NGDDONE','NGDCLOSED') and len(fields)==14:
        row=dict(zip(('reason','restored','calls','returns','unwinds','failures'),
                     (integer(value,maximum) for value,maximum in zip(fields[8:],(2**32-1,1,121,121,121,2**32-1)))))
        if not row['reason'] or row['returns']+row['unwinds']>row['calls']:raise ValueError('terminal counts')
        return kind,dict(tick=tick,**row)
    raise ValueError('message type/shape')

class Diagnostic:
    def __init__(self,token):
        self.token=hex_field(token,32);self.run=hashlib.sha256(token.encode('ascii')).hexdigest()
        self.peer=None;self.states={};self.grants={};self.terminal=None;self.closed=False;self.failed=None
        self.transcript=[];self.rejected=0

    def packet(self,tag,tick,*parts):return (header(tag,self.token,self.run,tick)+''.join('|'+str(p) for p in parts)).encode('ascii')

    def fail(self,reason):
        self.failed=self.failed or reason

    def receive(self,data,address,now):
        if len(self.transcript)>4094:self.fail('transcript_capacity');return []
        if not isinstance(address,tuple) or len(address)!=2 or address[0]!='127.0.0.1' or type(address[1]) is not int or not 1024<=address[1]<=65535:
            self.rejected+=1;return []
        try:kind,row=decode(data,self.token,self.run)
        except (ValueError,UnicodeError,TypeError):
            self.rejected+=1;self.fail('invalid_packet');return []
        self.transcript.append(dict(direction='in',at=now,endpoint=list(address),packet=data.decode('ascii')))
        if self.peer is not None and address!=self.peer:self.fail('extra_endpoint');return []
        if kind not in ('NGDSTATE','NGDDONE'):self.fail('unexpected_direction');return []
        tick=row['tick']
        if self.peer is None:
            if kind!='NGDSTATE' or tick!=0:self.fail('initial_state_missing');return []
            self.peer=address
        replies=[]
        if kind=='NGDSTATE':
            if self.terminal is not None:
                if self.states.get(tick)!=row:self.fail('state_after_terminal')
                return []  # No grant can be replayed after native terminal receipt.
            previous=self.states.get(tick)
            if previous is not None and previous!=row:self.fail(f'changed_held_state_{tick}')
            if previous is None:
                if tick!=len(self.states) or (tick and tick not in self.grants) or self.terminal:self.fail('state_sequence')
                else:
                    if tick:
                        for field in ('game_bits','real_bits'):
                            if abs(clock(row[field])-clock(self.states[tick-1][field])-1/60)>1e-8:self.fail(f'local_{field}_delta_{tick}')
                    self.states[tick]=row
            if self.failed:replies=[self.packet('NGDSTOP',tick,76)]
            elif tick==LIMIT:replies=[self.packet('NGDSTOP',tick,1)]
            else:
                grant=self.packet('NGDGO',tick+1,row['proposal'])
                self.grants.setdefault(tick+1,grant.decode('ascii'));replies=[grant]
        else:
            if self.terminal is not None and self.terminal!=row:self.fail('changed_terminal')
            if (tick!=LIMIT or len(self.states)!=LIMIT+1 or len(self.grants)!=LIMIT or row['reason']!=1 or
                row['restored']!=1 or row['calls']!=121 or row['returns']!=121 or row['unwinds'] or row['failures']):self.fail('terminal_incomplete')
            self.terminal=row
            # Failure closure is still acknowledged and retained; it never passes.
            replies=[self.packet('NGDCLOSED',tick,*(row[k] for k in ('reason','restored','calls','returns','unwinds','failures')))]
            self.closed=True
        for reply in replies:self.transcript.append(dict(direction='out',at=now,endpoint=list(address),packet=reply.decode('ascii')))
        return [(address,reply) for reply in replies]

    def summary(self):
        return dict(schema=1,contract=CONTRACT,run=self.run,epoch=1,native_clients=1,physical_slot=1,
                    action_scope='local_singleton',input2_origin='constant_internal_zero',category='local_action_diagnostic',
                    **FALSE_CLAIMS,closed=self.closed,failed=self.failed,rejected=self.rejected,
                    endpoint=list(self.peer) if self.peer else None,states=self.states,grants=self.grants,
                    terminal=self.terminal,transcript=self.transcript,
                    local_transport_valid=self.closed and self.failed is None)

def serve(sock,diagnostic,report,deadline,stop=None):
    """Use an already bound socket; the caller owns admission and process cleanup."""
    sock.settimeout(.1);closed_at=None
    try:
        while time.monotonic()<deadline and not (stop and stop.is_set()):
            try:data,address=sock.recvfrom(MAX_PACKET)
            except socket.timeout:
                if closed_at is not None and time.monotonic()-closed_at>=2:break
                continue
            for target,packet in diagnostic.receive(data,address,time.monotonic()):sock.sendto(packet,target)
            if diagnostic.closed and closed_at is None:closed_at=time.monotonic()
    finally:
        if not diagnostic.closed:diagnostic.fail('coordinator_incomplete')
        report=Path(report);temporary=report.with_suffix(report.suffix+'.tmp')
        temporary.write_text(json.dumps(diagnostic.summary(),indent=2)+'\n');temporary.replace(report)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port',type=int,required=True)
    p.add_argument('--token',required=True);p.add_argument('--report',type=Path,required=True)
    p.add_argument('--lifetime',type=int,default=1200);a=p.parse_args()
    if not 1024<=a.port<=65535 or not 1<=a.lifetime<=3600:p.error('bounded loopback port/lifetime required')
    d=Diagnostic(a.token)
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
        sock.bind(('127.0.0.1',a.port));print('diagnostic_getup_ready native_clients=1 parity_checked=false',flush=True)
        serve(sock,d,a.report,time.monotonic()+a.lifetime)
    print(f'diagnostic_getup_closed={d.closed} failure={d.failed or "none"} session_pass=false',flush=True)
    raise SystemExit(0 if d.closed and not d.failed else 1)

if __name__=='__main__':main()
