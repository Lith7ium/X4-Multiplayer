"""Closed, optional Windows first-rejection evidence. Never grants acceptance."""
import re

HEADER = 'schema enabled present readout_drained capture_scope context_scope stage predicate helper_known helper_result operands_known actual expected serial role class accounted owner thread phase scope_open load_open primary_failure_at_entry selected0 selected1 load_calls load_returns image_base basis xml agent movement bank records bytes truncated failed_reads attempted_reads dropped_records'.split()
READ = 'schema index address requested_bytes copied_bytes ok data'.split()
DONE = 'schema return_valid raw_return unwind primary_failure_after scope_open load_open role0_returns role1_returns'.split()
HEX = set('actual expected image_base basis xml agent movement bank'.split())
U64 = set('serial selected0 selected1 load_calls load_returns'.split())
FLAGS = set('enabled present readout_drained helper_known operands_known accounted truncated'.split())
PREDICATES = ('none scope_depth count stopped configured owner_thread phase load_calls load_returns scope_open '
    'table_read table_null client_read client_null client_type_read client_type client_entry_read client_entry '
    'module_read module_null module_type_read module_type module_entry_read module_entry module_state_read '
    'manager_read manager_null loading_read loading table_entry_read table_matches host_changed manager_flag_read '
    'manager_flag readers_read readers app_read app selector_read selector other_read other patch_owned patch_debt '
    'patch_read patch_bytes manager_changed agent_null agent_align agent_overflow basis call_serial call_selector '
    'call_host call_manager guard_changed arguments index_full index_join lifetime_identity lifetime_class '
    'role_duplicate bank_duplicate capacity limit helper math nonfinite address read accounted writable bank_changed '
    'plan_word write_changed unwind already_failed inactive').split()
STAGES = 'none admission pre_guard capture claim_guard lifetime class_count evaluate post_guard replay plan publish_replay publish_guard original prior'.split()
SCALAR_PREDICATES = set(('scope_depth count stopped configured owner_thread load_calls load_returns scope_open '
    'client_type client_entry module_type module_entry loading table_matches manager_flag readers other '
    'manager_changed basis call_serial call_selector call_manager index_join lifetime_identity lifetime_class '
    'bank_duplicate capacity limit plan_word already_failed').split())
GUARDS = set(PREDICATES[3:56]) | {'already_failed', 'inactive', 'unwind'}
PAIRS = {
    'admission': {'scope_depth', 'count'}, 'pre_guard': GUARDS, 'claim_guard': GUARDS,
    'post_guard': GUARDS, 'capture': {'helper','stopped','already_failed','inactive','unwind'},
    'lifetime': {'arguments','index_full','index_join','lifetime_identity','lifetime_class','role_duplicate','bank_duplicate','capacity','unwind'},
    'class_count': {'limit','unwind'}, 'evaluate': {'math','unwind'},
    'replay': {'helper','stopped','already_failed','inactive','unwind'},
    'publish_replay': {'helper','stopped','already_failed','inactive','unwind'},
    'plan': {'nonfinite','address','read','stopped','already_failed','inactive','unwind'},
    'publish_guard': GUARDS | {'accounted','writable','read','bank_changed','plan_word','write_changed'},
    'original': {'unwind'}, 'prior': {'already_failed'},
}


def uint(text, high=0xffffffff):
    if not isinstance(text,str) or not re.fullmatch(r'0|[1-9][0-9]*',text) or len(text)>20 or int(text)>high:
        raise ValueError('rejection unsigned integer')
    return int(text)


def row(text, schema, keys):
    if not isinstance(text,str) or not 0<len(text)<=2047 or not text.isascii():
        raise ValueError('rejection row bound')
    parts=[item.split('=',1) for item in text.split(' ')]
    if any(len(p)!=2 for p in parts) or [p[0] for p in parts]!=keys:
        raise ValueError('rejection closed fields/order')
    result=dict(parts)
    if result['schema']!=schema:raise ValueError('rejection schema')
    return result


def decode(rows):
    if not isinstance(rows,list) or not 1<=len(rows)<=194:raise ValueError('rejection row count')
    h=row(rows[0],'x4-rotation-reject-v1',HEADER)
    for key in HEX:
        if not re.fullmatch('[0-9a-f]{16}',h[key]):raise ValueError('rejection hex')
    for key in set(HEADER)-HEX-{'schema','capture_scope','context_scope','helper_result'}:
        h[key]=uint(h[key],2**64-1 if key in U64 else 0xffffffff)
    if any(h[k]>1 for k in FLAGS):raise ValueError('rejection flag')
    if h['capture_scope']!='failing_stage' or h['context_scope']!='predicate_only' or h['readout_drained']!=1:
        raise ValueError('rejection scope')
    if not re.fullmatch(r'0|1|-[1-9]|-10',h['helper_result']):raise ValueError('rejection helper result')
    h['helper_result']=int(h['helper_result'])
    if not h['helper_known'] and h['helper_result']!=0:raise ValueError('unset rejection helper')
    if h['stage']>=len(STAGES) or h['predicate']>=len(PREDICATES) or h['role']>1 or h['class']>2 or h['phase']>5:
        raise ValueError('rejection enum')
    if h['primary_failure_at_entry']>14 or h['records']>192 or h['bytes']>8192 or h['attempted_reads']!=h['records']+h['dropped_records']:
        raise ValueError('rejection count')
    if h['dropped_records'] and h['records']!=192:raise ValueError('rejection premature record overflow')
    if not h['present']:
        zero=set(HEADER)-HEX-{'schema','capture_scope','context_scope','enabled','readout_drained','class','helper_result'}
        if len(rows)!=1 or any(h[k]!=0 for k in zero) or any(h[k]!='0'*16 for k in HEX) or h['class']!=2:
            raise ValueError('empty rejection entry')
        return {'header':h,'reads':[],'completion':None,'scope':'no_rejection_recorded','acceptance':False}
    if h['enabled']!=1 or not h['thread'] or h['image_base']=='0'*16 or h['stage']==0 or h['predicate']==0:
        raise ValueError('rejection entry identity')
    stage,predicate=STAGES[h['stage']],PREDICATES[h['predicate']]
    if predicate not in PAIRS[stage]:raise ValueError('rejection stage/predicate')
    if bool(h['operands_known']) != (predicate in SCALAR_PREDICATES):
        raise ValueError('rejection predicate operand encoding')
    if predicate=='helper' and (not h['helper_known'] or not h['helper_result'] or
                                h['actual']!='0'*16 or h['expected']!='0'*16):
        raise ValueError('rejection helper attribution')
    if h['accounted'] and h['class']==2:raise ValueError('rejection accounted unknown class')
    if len(rows)!=h['records']+2:raise ValueError('rejection missing completion or reads')
    reads=[];copied=failed=0;incomplete=bool(h['dropped_records'])
    for index,text in enumerate(rows[1:-1],1):
        r=row(text,'x4-rotation-reject-read-v1',READ)
        for key in ('index','requested_bytes','copied_bytes','ok'):r[key]=uint(r[key],2**64-1 if key=='requested_bytes' else 0xffffffff)
        if r['index']!=index or not re.fullmatch('[0-9a-f]{16}',r['address']) or not r['requested_bytes'] or r['copied_bytes']>512 or r['ok']>1:
            raise ValueError('rejection read bounds')
        if r['copied_bytes']:
            if not r['ok'] or r['copied_bytes']!=r['requested_bytes'] or not re.fullmatch('[0-9a-f]{%d}'%(2*r['copied_bytes']),r['data']):
                raise ValueError('rejection successful bytes')
        elif r['data']!='-':raise ValueError('rejection absent bytes')
        if not r['ok']:failed+=1
        elif not r['copied_bytes']:
            if r['requested_bytes']<=512 and copied+r['requested_bytes']<=8192:
                raise ValueError('rejection unexplained byte overflow')
            incomplete=True
        copied+=r['copied_bytes'];reads.append(r)
    if copied!=h['bytes'] or not failed<=h['failed_reads']<=failed+h['dropped_records'] or bool(h['truncated'])!=incomplete:
        raise ValueError('rejection aggregate')
    c=row(rows[-1],'x4-rotation-reject-completion-v1',DONE)
    for key in set(DONE)-{'schema','raw_return'}:c[key]=uint(c[key],2**64-1 if key.startswith('role') else 0xffffffff)
    if c['return_valid']>1 or c['unwind']>1 or c['primary_failure_after']>14 or not c['primary_failure_after']:
        raise ValueError('rejection completion flags')
    if not c['return_valid'] and not c['unwind']:raise ValueError('rejection missing original outcome')
    if h['primary_failure_at_entry'] and c['primary_failure_after']!=h['primary_failure_at_entry']:
        raise ValueError('rejection changed primary failure')
    if (c['return_valid'] and not re.fullmatch('[0-9a-f]{16}',c['raw_return'])) or (not c['return_valid'] and c['raw_return']!='-'):
        raise ValueError('rejection opaque return')
    return {'header':h,'reads':reads,'completion':c,'scope':'passive_first_refusal_only','acceptance':False}


def evidence(lines,run,agent):
    if not isinstance(run,str) or not re.fullmatch(r'[a-z0-9_-]{1,80}',run) or \
            not isinstance(agent,str) or not re.fullmatch(r'[0-9a-f]{64}',agent):
        raise ValueError('rejection expected run/agent identity')
    prefix=f'[X4LAN] PORTABLE_ROTATION_REJECT slot=1 run={run} agent={agent} '
    rows=[]
    for line in lines:
        if not isinstance(line,str):raise ValueError('rejection log line')
        if 'PORTABLE_ROTATION_REJECT_ERROR ' in line:raise ValueError('rejection export error')
        if 'PORTABLE_ROTATION_REJECT ' not in line:continue
        start=line.find('[X4LAN] PORTABLE_ROTATION_REJECT ')
        text=line[start:]
        if not text.startswith(prefix):raise ValueError('rejection run/agent binding')
        rows.append(text[len(prefix):])
    return decode(rows)
