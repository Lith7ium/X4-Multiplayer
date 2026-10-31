"""Validate read-only construction phase provenance; this is not enrollment acceptance."""
import re
import native_registry_origin_evidence
import native_registry_origin_identity_evidence
import native_creation_evidence

FIELDS='schema order pair tick handle edge kind phase caller payload ui main thread view time ready sink reads error result pending name'.split()
HEX={'handle','caller','payload','time','sink','reads','error'}

def evidence(log,slot,errors,origin_required=False,identity_required=False,creation_required=False):
    result={'rows':[], 'first_ai_order':None, 'first_ai_thread':None, 'builder_runtime':None,
            'update_coverage':'registered_wrapper_entries_only','ancillary_callback_correlation':None,
            'first_ai_same_thread':None, 'ordering_scope':'atomic_observation_only','updates':[]}
    try:
        if creation_required and not (origin_required and identity_required):
            raise ValueError('creation schema requires mandatory origin and identity evidence')
        result['creation']=native_creation_evidence.evidence(log,required=creation_required)
        creation_orders=result['creation']['orders']
        footer=re.findall(r'NATIVE_GATE REGISTRY_MD_STOP ([^\r\n]*)',log)
        if len(footer)!=1:raise ValueError('missing unique footer')
        parsed=re.fullmatch(r'schema=2 count=(\d+) order=(\d+) updates=(\d+) failures=(\d+) restored=(\d+) open=(\d+) restore_open=(\d+) peak=(\d+)',footer[0])
        if not parsed:raise ValueError('footer schema')
        count,total,update_count,failures,restored,open_calls,restore_open,peak=map(int,parsed.groups())
        result['peak_open']=peak
        if count!=22 or not 1<=update_count<=256 or not count<=total<=count+2*update_count+len(creation_orders) or failures or restored!=1 or open_calls or restore_open or not 1<=peak<=128:
            raise ValueError('count, overflow, failed read, restoration or open original call')
        previous=0
        for line in re.findall(r'NATIVE_GATE REGISTRY_MD ([^\r\n]*)',log):
            parts=line.split();pairs=[p.split('=',1) for p in parts]
            if any(len(p)!=2 for p in pairs) or [p[0] for p in pairs]!=FIELDS:raise ValueError('row schema')
            values=dict(pairs);row={}
            for field,value in values.items():
                if field=='name':row[field]=value;continue
                if not re.fullmatch('[0-9a-f]+' if field in HEX else '[0-9]+',value):raise ValueError('number syntax')
                if field in {'handle','payload','time'} and len(value)!=16:raise ValueError('exact uint64 width')
                row[field]=int(value,16 if field in HEX else 10)
            if row['schema']!=2 or not previous<row['order']<=total or row['tick'] not in (5,6) or row['handle']!=0x500000003 or \
               row['reads']!=31 or row['error'] or row['ui'] not in (0,1) or row['main'] not in (0,1) or row['view'] not in (0,1) or not 0<row['thread']<=2**32-1 or row['pending']!=1 or \
               row['ready'] not in (0,1) or not row['sink'] or row['result'] not in (0,1):raise ValueError('row identity/coverage')
            result['rows'].append(row);previous=row['order']
        rows=result['rows']
        if len(rows)!=count:raise ValueError('row count')
        starts=[r for r in rows if r['edge']==0];ends={r['pair']:r for r in rows if r['edge']==1}
        if len(starts)!=11 or len(ends)!=11:raise ValueError('paired boundary counts')
        active=set();observed_peak=0
        for row in rows:
            if row['edge']==0:
                if row['pair'] in active:raise ValueError('duplicate open call')
                active.add(row['pair']);observed_peak=max(observed_peak,len(active))
            elif row['edge']==1:
                if row['pair'] not in active:raise ValueError('leave before entry')
                active.remove(row['pair'])
            else:raise ValueError('unknown boundary edge')
        if active or not observed_peak<=peak<=11:raise ValueError('open-call peak coverage')
        expected=[(3,0),(4,0)]+[(1,p) for p in (1,2,3)]+[(2,0)]+[(1,p) for p in (4,5,6,7,8)]
        names={1:'Boundary',2:'Builder',3:'Station',4:'Storage'}
        for index,(kind,phase) in enumerate(expected):
            start=starts[index];end=ends.get(start['pair'],{})
            if not end or start['result']!=0 or start['pair']!=start['order'] or end['order']<=start['order'] or \
               any(start[k]!=end[k] for k in ('kind','phase','handle','payload','caller','tick','thread','name')) or (start['kind'],start['phase'])!=(kind,phase):
                raise ValueError('paired original-call boundaries or MD action order')
            if start['caller']!=(0xc2095a if kind==1 else 0xc208cd) or start['name']!='X4LAN_NativeConstruction.'+names[kind]:
                raise ValueError('event identity/caller')
            if kind==1 and start['payload']!=phase:raise ValueError('marker payload')
            if kind!=1 and not start['payload']:raise ValueError('component event payload')
            if kind==2:
                result['builder_runtime']=start['payload'];result['builder_entry_order']=start['order'];result['builder_return_order']=end['order']
        if len({s['payload'] for s in starts if s['kind']!=1})!=3:raise ValueError('ancillary component identities overlap')
        if creation_required:
            status=result['creation']['status']
            phase1=next(r for r in rows if r['kind']==1 and r['phase']==1 and r['edge']==1)
            phase2=next(r for r in rows if r['kind']==1 and r['phase']==2 and r['edge']==0)
            builder=next(r for r in rows if r['kind']==2 and r['edge']==0)
            if (status['phase1'],status['phase2'],status['builder_order'],status['builder_runtime'])!=(phase1['order'],phase2['order'],builder['order'],builder['payload']) or \
               any(row['thread']!=status['thread'] for row in (phase1,phase2,builder)) or \
               phase1['tick']!=phase2['tick'] or phase1['tick']!=builder['tick']:
                raise ValueError('creation token/phase/Builder native join')
            if status['schema']==2 and status['execution_tick']!=phase1['tick']:
                raise ValueError('creation execution tick differs from immutable MD event')
        targets=re.findall(r'CONSTRUCTION_TARGET_BOUND ([^\r\n]*)',log)
        if targets:
            observed={}
            for raw in targets:
                match=re.fullmatch(r'tick=(\d+) kind=(station|storage) native_id=([0-9]+) sector_pose_supported=0',raw)
                if not match:raise ValueError('passive target callback schema')
                tick,kind,native=match.groups();native=int(native)
                if kind in observed or int(tick) not in (5,6) or not 0<native<2**64:raise ValueError('passive target callback identity')
                observed[kind]=native
            if observed!={names[s['kind']].lower():s['payload'] for s in starts if s['kind'] in (3,4)}:
                raise ValueError('ancillary payload/passive callback disagreement')
            result['ancillary_callback_correlation']=True
        bound=re.findall(r'CONSTRUCTION_BIND_BEGIN [^\r\n]*\bnative_id=([0-9]+)(?: |\r?\n|$)',log)
        if len(bound)!=1 or int(bound[0])!=result['builder_runtime']:raise ValueError('Builder payload/bind native identity')
        identities=set();seen={r['order'] for r in rows};previous_update=0
        if len(set(creation_orders))!=len(creation_orders) or any(order in seen or not 1<=order<=total for order in creation_orders):
            raise ValueError('creation shared ordinal collision')
        seen.update(creation_orders)
        for line in re.findall(r'NATIVE_GATE REGISTRY_UPDATE_FIRST ([^\r\n]*)',log):
            match=re.fullmatch(r'schema=1 runtime=([0-9a-f]{16}) tick=(\d+) kind=(\d+) order=(\d+) thread=(\d+) object=([01]) ai_order=(\d+) ai_thread=(\d+)',line)
            if not match:raise ValueError('first update receipt schema')
            values=[int(v,16 if i==0 else 10) for i,v in enumerate(match.groups())]
            update=dict(zip(('runtime','tick','kind','order','thread','object','ai_order','ai_thread'),values));order=update['order'];ai=update['ai_order']
            if not update['runtime'] or update['runtime'] in identities or not 1<=update['tick']<=18000 or not 1<=update['kind']<=17 or \
               not previous_update<order<=total or order in seen or not 0<update['thread']<=2**32-1 or \
               (ai and (not order<ai<=total or ai in seen or not 0<update['ai_thread']<=2**32-1)) or (not ai and update['ai_thread']):
                raise ValueError('first update lifetime, ordinal or thread')
            identities.add(update['runtime']);seen.add(order);previous_update=order
            if ai:seen.add(ai)
            result['updates'].append(update)
        if len(result['updates'])!=update_count:raise ValueError('first update coverage')
        builder=next((u for u in result['updates'] if u['runtime']==result['builder_runtime']),None)
        if not builder:raise ValueError('Builder first-update lifetime missing')
        result['first_update_order']=builder['order'];result['first_update_kind']=builder['kind'];result['first_update_thread']=builder['thread']
        result['first_update_same_thread']=all(r['thread']==builder['thread'] for r in rows)
        result['builder_entry_precedes_first_update']=result['builder_entry_order']<builder['order']
        result['builder_return_precedes_first_update']=result['builder_return_order']<builder['order']
        pending={tuple(map(int,m)) for m in re.findall(r'pending_tick=(\d+) pending_kind=(\d+) pending_ai=([01]) pending_order=(\d+) pending_thread=(\d+)',log)}
        if pending!={(builder['tick'],builder['kind'],int(bool(builder['ai_order'])),builder['order'],builder['thread'])}:
            raise ValueError('Builder bind/first-update provenance disagreement')
        ai_notes={tuple(map(int,m)) for m in re.findall(r'\bai_order=(\d+) ai_thread=(\d+)',log) if int(m[0])}
        # Receipts include unrelated lifetimes. Their IDs and ordinals remain
        # evidence, but they never stand in for the exact Builder lifetime.
        expected_ai={(u['ai_order'],u['ai_thread']) for u in result['updates'] if u['ai_order']}
        if ai_notes!=expected_ai:raise ValueError('AI ordinal/thread coverage')
        if builder['ai_order']:
            result['first_ai_order']=builder['ai_order'];result['first_ai_thread']=builder['ai_thread']
            result['first_ai_same_thread']=all(r['thread']==builder['ai_thread'] for r in rows)
        if result['first_update_same_thread'] and result['first_ai_same_thread'] is not False:result['ordering_scope']='same_native_thread'
        result['md_threads']=sorted({r['thread'] for r in rows})
        result['ui_view_observed']=all(r['ui']==1 and r['view']==1 for r in rows)
        if seen!=set(range(1,total+1)):raise ValueError('unexplained shared ordinal or missing first update/AI')
        result['first_origin']=native_registry_origin_evidence.evidence(log,result['updates'],required=origin_required)
        result['first_origin_identity']=native_registry_origin_identity_evidence.evidence(log,result['updates'],required=identity_required)
        if creation_required:native_creation_evidence.join_first_origin(result['creation'],result['first_origin'],result['first_origin_identity'],builder)
    except (ValueError,KeyError,IndexError) as exc:
        errors.append(f'player {slot} construction MD boundary evidence invalid: {exc}')
    return result
