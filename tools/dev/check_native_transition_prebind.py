"""Replay actual owned-C receipt bytes through production Lua and Python."""
import argparse
import json
from pathlib import Path
import re
from lupa.luajit21 import LuaRuntime,LuaError
from native_transition_evidence import evidence,configured_evidence
from check_native_transition_queue_diag import CONFIG as BASE_CONFIG

CONFIG=BASE_CONFIG+'\nnative_gate_player_transition_prebind_receipt = true'

def check(path):
    raw=path.read_text(encoding='ascii');core=evidence(raw)
    if not core['evidence_valid'] or not core['prebind_receipt']['evidence_valid']:raise ValueError(core['errors'])
    lua=LuaRuntime(unpack_returned_tuples=True);lua.execute('X4LAN_CONFIG={};logs={};function DebugError(s)logs[#logs+1]=s end')
    for line in CONFIG.splitlines():lua.execute('X4LAN_CONFIG.'+line)
    def lines(prefix):return [line.split('NATIVE_GATE ',1)[1] for line in raw.splitlines() if line.startswith('NATIVE_GATE '+prefix)]
    def footer(prefix):return lines(prefix)[0].split(' ',1)[1]
    for name,prefix in [('p','PLAYER_TRANSITION_PREBIND schema='),('q','PLAYER_TRANSITION_QUEUE_DIAG schema='),('e','PLAYER_TRANSITION event='),
                        ('w','PLAYER_TRANSITION_WATCH '),('t','PLAYER_TRANSITION_TICKET ')]:
        lua.globals()[name+'rows']=lua.table_from(lines(prefix))
    for name,prefix in [('p','PLAYER_TRANSITION_PREBIND_STOP '),('q','PLAYER_TRANSITION_QUEUE_DIAG_STOP '),('e','PLAYER_TRANSITION_STOP ')]:
        lua.globals()[name+'footer']=footer(prefix)
    lua.globals().run=core['prebind_receipt']['stop']['run']
    lua.execute('''X4LAN_SHA256={digest=function()return run end};pcursor=0;qcursor=0;ecursor=0;wcursor=0;tcursor=0;stopped=false;order={}
      local function cfg(name)return function()order[#order+1]=name;return 1 end end
      gate={player_transition_watch_config=cfg('watch'),player_transition_prebind_config=cfg('prebind'),
        player_transition_queue_evidence_config=cfg('copied'),player_transition_config=cfg('transition')}
      gate.player_transition_prebind_status=function()return pfooter:gsub('rows=3','rows='..pcursor):gsub('closing=1','closing='..(stopped and 1 or 0))end
      gate.player_transition_queue_evidence_status=function()return qfooter:gsub('rows=2','rows='..qcursor):gsub('closing=1','closing='..(stopped and 1 or 0))end
      gate.player_transition_status=function()return efooter:gsub(' rows=%d+',' rows='..ecursor):gsub('watch_rows=%d+','watch_rows='..wcursor):gsub('ticket_rows=%d+','ticket_rows='..tcursor):gsub('sites=0','sites='..(stopped and 0 or 7)):gsub('enabled=0','enabled='..(stopped and 0 or 1))end
      gate.player_transition_prebind_row=function(i)assert(i==pcursor+1);pcursor=i;return prows[i]end
      gate.player_transition_queue_evidence_row=function(i)assert(i==qcursor+1);qcursor=i;return qrows[i]end
      gate.player_transition_row=function(i)if i==0 then return #erows end;assert(i==ecursor+1);ecursor=i;return erows[i]end
      gate.player_transition_watch=function(i)assert(i==wcursor+1);wcursor=i;return wrows[i]end
      gate.player_transition_ticket=function(i)assert(i==tcursor+1);tcursor=i;return trows[i]end''')
    source=Path(__file__).with_name('native_gate_transition.lua').read_text()
    lua.execute(source);lua.execute('X4LAN_NATIVE_TRANSITION.configure(gate);X4LAN_NATIVE_TRANSITION.drain(gate,false)')
    lua.execute(source);lua.execute('X4LAN_NATIVE_TRANSITION.configure(gate);stopped=true;X4LAN_NATIVE_TRANSITION.drain(gate,true)')
    if list(lua.globals().order.values())!=['watch','prebind','copied','transition']*2:raise ValueError('configure order')
    emitted='\n'.join(lua.globals().logs.values());errors=[];result=configured_evidence(emitted,CONFIG,1,[],errors)
    if errors or not result['evidence_valid'] or result['observed_model_completions']:raise ValueError(errors or 'initialization became user completion')
    if result['queue_evidence']['ticket_authority'] or result['prebind_receipt']['atomic_lease']:raise ValueError('authority inflation')
    # Every removed immutable phase fails against the otherwise valid C output.
    for phase in (1,2,3):
        changed='\n'.join(line for line in raw.splitlines() if not re.search(r'PLAYER_TRANSITION_PREBIND schema=1 ordinal='+str(phase)+r'\b',line))
        if evidence(changed)['evidence_valid']:raise ValueError('missing phase accepted')
    if evidence('\n'.join(line for line in raw.splitlines() if 'PLAYER_TRANSITION_PREBIND' not in line))['evidence_valid']:raise ValueError('stripped extension accepted')
    reused=path.with_name(path.name+'.reused').read_text(encoding='ascii');bad=evidence(reused)
    if bad['evidence_valid'] or not bad['prebind_receipt']['stop']['poison']:raise ValueError('retired dispatch reuse accepted')
    reused_footer=next(line.split('PLAYER_TRANSITION_PREBIND_STOP ',1)[1] for line in reused.splitlines() if 'PLAYER_TRANSITION_PREBIND_STOP ' in line)
    lua.globals().pfooter=reused_footer;lua.globals().stopped=False
    lua.execute(source)
    try:lua.execute('X4LAN_NATIVE_TRANSITION.configure(gate)')
    except LuaError as error:
        if 'prebind already closed/failed' not in str(error):raise
    else:raise ValueError('retired dispatch reuse reattached')
    states=dict(line.split(' ',1) for line in path.with_name(path.name+'.inflight').read_text(encoding='ascii').splitlines())
    pending=LuaRuntime(unpack_returned_tuples=True);pending.execute('X4LAN_CONFIG={};logs={};function DebugError(s)logs[#logs+1]=s end')
    for line in CONFIG.splitlines():pending.execute('X4LAN_CONFIG.'+line)
    def changed(text,**values):
        for k,v in values.items():text=re.sub(r'\b'+k+r'=[^ ]+',k+'='+str(v),text)
        return text
    pg=pending.globals();pg.run=core['prebind_receipt']['stop']['run'];pg.pstate=states['ACTIVE']
    pg.qstate=changed(footer('PLAYER_TRANSITION_QUEUE_DIAG_STOP '),count=0,rows=0,calls=0,closing=0,open=2)
    pg.estate=changed(footer('PLAYER_TRANSITION_STOP '),count=1,complete=0,rows=0,bound=0,watch_rows=0,tickets=0,ticket_rows=0,
                      outstanding=0,queue_calls=1,queue_returns=0,direct_calls=1,direct_returns=0,open=2,sites=7,enabled=1)
    pending.execute('''X4LAN_SHA256={digest=function()return run end};gate={
      player_transition_watch_config=function()return 1 end,player_transition_prebind_config=function()return 1 end,
      player_transition_queue_evidence_config=function()return 1 end,player_transition_config=function()return 1 end,
      player_transition_prebind_status=function()return pstate end,player_transition_queue_evidence_status=function()return qstate end,
      player_transition_status=function()return estate end,player_transition_row=function(i)assert(i==0);return 0 end}''')
    pending.execute(source);pending.execute('X4LAN_NATIVE_TRANSITION.configure(gate);X4LAN_NATIVE_TRANSITION.drain(gate,false)')
    pg.pstate=states['STOP_OPEN'];pg.qstate=changed(pg.qstate,open=0,closing=1);pg.estate=changed(pg.estate,sites=0,enabled=0,restore_open=2)
    try:pending.execute('X4LAN_NATIVE_TRANSITION.drain(gate,true)')
    except LuaError as error:
        if 'prebind incomplete' not in str(error):raise
    else:raise ValueError('pending source accepted at STOP')
    if 'PLAYER_TRANSITION_PREBIND_STOP '+states['STOP_OPEN'] not in '\n'.join(pg.logs.values()):raise ValueError('raw pending STOP erased')
    return dict(owned_fixture_only=True,source_ack_bind=3,actual_c_lua_parser=True,reload_retains_cursors=True,
                missing_phase_refused=True,active_pending_valid=True,stop_pending_refused=True,retired_dispatch_reuse_refused=True,
                native_game=False,atomic_lease=False,completed_release=False,independent_player_context=False)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('export',type=Path)
    print(json.dumps(check(parser.parse_args().export)))
