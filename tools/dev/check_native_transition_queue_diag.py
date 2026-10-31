"""Check real owned-C formatter output through Lua and independent parsers.

Native export callbacks/cursors are modeled for the Lua replay. The bytes and
failed schema2 events come from the C fixture, not a native game or valid lease.
"""
import argparse
import json
from pathlib import Path
import re
from lupa.luajit21 import LuaRuntime,LuaError
import native_transition_evidence as transition
import native_transition_queue_diag as diagnostic

CONFIG='''token = "00000000000000000000000000000000"
native_gate_player_probe = "diagnostic-getup"
native_gate_diagnostic_contract = "diagnostic-getup-1"
native_gate_player_transition_observer = true
native_gate_world_trace_early = true
native_gate_world_queue_trace = true
native_gate_player_transition_manifest = "2|00000000000ae79c/p/ship_arg_s_scout_01_a_macro/player"
native_gate_player_transition_last_tick = 120
native_gate_player_transition_queue_evidence = true'''


def check(path):
    raw=path.read_text(encoding='ascii');core=transition.evidence(raw)
    copied=diagnostic.evidence(raw,CONFIG,core)
    if copied['errors'] or not core['errors'] or core['events'][2]['errors']!=48 or core['tickets']:
        raise ValueError('owned copied/strict receipt expectation failed')
    def lines(prefix):return [line.split('NATIVE_GATE ',1)[1] for line in raw.splitlines() if line.startswith('NATIVE_GATE '+prefix)]
    qrows=lines('PLAYER_TRANSITION_QUEUE_DIAG schema=');events=lines('PLAYER_TRANSITION event=')
    watch=lines('PLAYER_TRANSITION_WATCH ')[0]
    qfooter=lines('PLAYER_TRANSITION_QUEUE_DIAG_STOP ')[0].split(' ',1)[1]
    footer=lines('PLAYER_TRANSITION_STOP ')[0].split(' ',1)[1]
    def update(text,**fields):
        for key,value in fields.items():text=re.sub(r'\b'+key+r'=[^ ]+',key+'='+str(value),text)
        return text
    lua=LuaRuntime(unpack_returned_tuples=True);lua.execute('X4LAN_CONFIG={};logs={};function DebugError(s)logs[#logs+1]=s end')
    for line in CONFIG.splitlines():lua.execute('X4LAN_CONFIG.'+line)
    cfg=lua.globals();cfg.run=copied['stop']['run'];cfg.qrows=lua.table_from(qrows);cfg.events=lua.table_from(events);cfg.watch=watch
    cfg.qactive=update(qfooter,closing=0);cfg.qstop=qfooter;cfg.core_active=update(footer,enabled=1,sites=7);cfg.core_stop=footer
    lua.execute('''X4LAN_SHA256={digest=function()return run end};qcursor=0;cursor=0;watch_cursor=0;stopped=false
      gate={player_transition_watch_config=function()return 1 end,player_transition_config=function()return 1 end,
        player_transition_queue_evidence_config=function()return 1 end}
      gate.player_transition_queue_evidence_status=function()
        return (stopped and qstop or qactive):gsub('rows=%d+','rows='..qcursor) end
      gate.player_transition_queue_evidence_row=function(i)assert(i==qcursor+1);qcursor=i;return qrows[i]end
      gate.player_transition_status=function()return (stopped and core_stop or core_active):gsub(' rows=%d+',' rows='..cursor):gsub('watch_rows=%d+','watch_rows='..watch_cursor)end
      gate.player_transition_watch=function(i)assert(i==1 and watch_cursor==0);watch_cursor=1;return watch end
      gate.player_transition_row=function(i)if i==0 then return #events end;assert(i==cursor+1);cursor=i;return events[i]end
      gate.player_transition_ticket=function()error('no accepted ticket')end''')
    lua.execute((Path(__file__).with_name('native_gate_transition.lua')).read_text())
    lua.execute('X4LAN_NATIVE_TRANSITION.configure(gate);stopped=true')
    try:lua.execute('X4LAN_NATIVE_TRANSITION.drain(gate,true)')
    except LuaError as error:
        if 'transition observation failed' not in str(error):raise
    else:raise ValueError('Lua erased native transition failure')
    emitted='\n'.join(lua.globals().logs.values());errors=[]
    joined=transition.configured_evidence(emitted,CONFIG,1,[],errors)
    if not errors or joined['evidence_valid'] or not joined['queue_evidence']['evidence_valid'] or joined['tickets']:
        raise ValueError('independent configured join changed strict result')
    if [joined['queue_evidence']['rows'][i] for i in (1,2)]!=[copied['rows'][i] for i in (1,2)]:
        raise ValueError('Lua changed copied bytes')
    # The worker was paused inside the actual capture's first borrowed read.
    # Active Lua must accept that real C status. STOP must still refuse its
    # open original, including after the worker later returns successfully.
    states=dict(line.split(' ',1) for line in path.with_name(path.name+'.inflight').read_text(encoding='ascii').splitlines())
    before=diagnostic.fields(states['ACTIVE'],diagnostic.STOP)
    returned=diagnostic.fields(states['STOP_RETURNED'],diagnostic.STOP)
    if before['calls'] or before['count'] or before['open']!=1 or returned['calls']!=1 or returned['open'] or returned['restore_open']!=1:
        raise ValueError('owned in-flight schedule not exercised')
    pending=LuaRuntime(unpack_returned_tuples=True);pending.execute('X4LAN_CONFIG={};logs={};function DebugError(s)logs[#logs+1]=s end')
    for line in CONFIG.splitlines():pending.execute('X4LAN_CONFIG.'+line)
    pg=pending.globals();pg.run=copied['stop']['run'];pg.qstate=states['IDLE']
    pg.core_live=update(footer,count=0,complete=0,rows=0,failures=0,bound=0,watch_rows=0,queue_calls=1,queue_returns=0,
                        queue_unknown=0,direct_calls=0,direct_returns=0,enabled=1,sites=7,open=1)
    pg.core_stopped=update(pg.core_live,enabled=0,sites=0,restore_open=1)
    pending.execute('''X4LAN_SHA256={digest=function()return run end};stopped=false
      gate={player_transition_watch_config=function()return 1 end,player_transition_config=function()return 1 end,
        player_transition_queue_evidence_config=function()return 1 end,
        player_transition_queue_evidence_status=function()return qstate end,
        player_transition_queue_evidence_row=function()error('unpublished row consumed')end,
        player_transition_status=function()return stopped and core_stopped or core_live end,
        player_transition_row=function(i)assert(i==0);return 0 end}''')
    pending.execute((Path(__file__).with_name('native_gate_transition.lua')).read_text())
    pending.execute('X4LAN_NATIVE_TRANSITION.configure(gate)');pg.qstate=states['ACTIVE']
    pending.execute('X4LAN_NATIVE_TRANSITION.drain(gate,false)')
    pg.qstate=states['STOP_OPEN'];pending.execute('stopped=true')
    try:pending.execute('X4LAN_NATIVE_TRANSITION.drain(gate,true)')
    except LuaError as error:
        if 'queue evidence stop incomplete' not in str(error):raise
    else:raise ValueError('Lua accepted STOP during borrowed read')
    emitted='\n'.join(pg.logs.values())
    if 'PLAYER_TRANSITION_QUEUE_DIAG_STOP '+states['STOP_OPEN'] not in emitted or 'PLAYER_TRANSITION_STOP ' not in emitted:
        raise ValueError('STOP failure erased raw closure receipts')
    # Mutate only the new diagnostic row, preserving the valid native baseline.
    for before,after in (('tick=1 boot=3','tick=1 boot=0'),('order=2 candidates=1','order=18446744073709551615 candidates=1')):
        if before not in raw or diagnostic.evidence(raw.replace(before,after),CONFIG,core)['evidence_valid']:
            raise ValueError('impossible copied chronology accepted')
    return dict(owned_fixture_only=True,copied_rows=2,decoded_models=2,strict_transition_errors=48,
                accepted_tickets=0,lua_preserved_strict_failure=True,parser_pass=True,
                inflight_lua_valid=True,stop_open_refused=True,impossible_chronology_refused=True,
                native_game=False,allocation_generation_known=False,ticket_authority=False,atomic_lease=False)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('export',type=Path)
    print(json.dumps(check(parser.parse_args().export)))
