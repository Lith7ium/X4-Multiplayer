"""Offline per-script RNG census join over synthetic SCRIPT_RNG rows."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/dev'))
from census_scripts import analyse,parse


def row(index,tick,key,draws,name,src=1,state=1,owner='00000000000000a1',caller=None,why=0,seed=None):
    tail=f' caller={caller} why={why}' if caller else ''
    return (f'[X4LAN] NATIVE_GATE SCRIPT_RNG_ROW index={index} tick={tick} src={src} state={state} kind=1 inst=0 depth=1 '
            f'owner={owner} stream=1111111111111111 detail=0000000000000004 ord=1 key={key} parent=0000000000000000 '
            f'seeded=1 seed={seed or key} exit=2222222222222222 draws={draws} name={name}{tail}')


def tick(n,last,keyset,drawset,unkeyed=0):
    return (f'[X4LAN] NATIVE_GATE SCRIPT_RNG tick={n} installed=1 active=1 mode=1 mask=3 cur_tick={n} cur_entries=1 '
            f'last_tick={last} last_entries=3 last_outer=2 last_child=1 last_unkeyed={unkeyed} last_draws=9 last_unknown=0 '
            f'last_keyset={keyset} last_drawset={drawset}')


class CensusScriptsTests(unittest.TestCase):
    def test_rows_join_by_key_and_rank_first_divergent_script(self):
        a=[row(0,1,'a'*16,3,'move.idle'),row(1,1,'b'*16,2,'Other.Cue1',src=5),row(2,2,'c'*16,4,'fight.attack',state=2),
           tick(2,1,'1'*16,'2'*16),tick(3,2,'3'*16,'4'*16),'[X4LAN] NATIVE_GATE SCRIPT_RNG_ROW index=9 evicted=1',
           '[X4LAN] NATIVE_GATE SCRIPT_RNG_STOP installed=1 failures=0']
        b=[row(0,1,'a'*16,3,'move.idle'),row(1,1,'b'*16,5,'Other.Cue1',src=5),row(2,2,'d'*16,4,'fight.attack',state=2),
           tick(2,1,'1'*16,'5'*16),tick(3,2,'6'*16,'7'*16)]
        clients=[parse(a),parse(b)]
        self.assertEqual(clients[0]['evicted'],1);self.assertEqual(clients[0]['status'],'installed=1 failures=0')
        self.assertTrue(clients[0]['rows']['c'*16]['child'])
        result=analyse(clients)
        self.assertEqual(result['rows'],[3,3]);self.assertEqual(result['first_keyset_tick'],2);self.assertEqual(result['first_drawset_tick'],1)
        scripts=list(result['scripts'].items())
        self.assertEqual(scripts[0][0],'cue/Other.Cue1');self.assertEqual(scripts[0][1]['draw_mismatch'],1)
        self.assertEqual(scripts[0][1]['examples'][0]['draws'],[2,5])
        self.assertEqual(result['scripts']['exec/fight.attack']['only'],[1,1])
        self.assertEqual(result['scripts']['exec/move.idle']['first_mismatch_tick'],None)
        self.assertEqual(result['scripts']['exec/move.idle']['joined'],1)

    def test_natural_caller_words_locate_the_first_unkeyed_difference(self):
        # Tick 5: keyed A agrees, unkeyed drone D runs on a different natural
        # word on client 2, keyed B after it sees a different caller word.
        drone='7f554e4d41505044'
        a=[row(0,5,'a'*16,1,'move.idle',caller='1'*16),row(1,5,'d'*16,2,'move.escapepod',state=5,owner=drone,caller='3'*16,why=2,seed='3'*16),
           row(2,5,'b'*16,1,'fight.attack',caller='4'*16),row(3,6,'e'*16,0,'move.escapepod',state=5,owner=drone,caller='6'*16,why=2,seed='6'*16),
           row(4,6,'c'*16,1,'order.dock',caller='5'*16,why=2,owner=drone),tick(6,5,'1'*16,'2'*16,unkeyed=1)]
        b=[row(0,5,'a'*16,1,'move.idle',caller='1'*16),row(1,5,'d'*16,3,'move.escapepod',state=5,owner=drone,caller='9'*16,why=2,seed='9'*16),
           row(2,5,'b'*16,1,'fight.attack',caller='8'*16),row(4,6,'c'*16,1,'order.dock',caller='5'*16,why=2,owner=drone),
           tick(6,5,'1'*16,'2'*16,unkeyed=0)]
        clients=[parse(a),parse(b)]
        self.assertEqual(sorted(clients[0]['unkeyed']),['d'*16,'e'*16]);self.assertNotIn('d'*16,clients[0]['rows'])
        self.assertEqual(clients[0]['rows']['b'*16]['caller'],'4'*16);self.assertEqual(clients[0]['rows']['c'*16]['why'],2)
        result=analyse(clients)
        self.assertEqual(result['rows'],[3,3]);self.assertEqual(result['first_unkeyed_tick'],5)
        natural=result['natural']
        self.assertEqual((natural['joined'],natural['caller_mismatch'],natural['first_tick']),(3,1,5))
        self.assertEqual(natural['unmapped_keyed'],[1,1])
        self.assertEqual(natural['locations'][0],dict(tick=5,first=dict(source='exec',name='fight.attack',owner='00000000000000a1'),
                         caller=['4'*16,'8'*16],after=dict(source='exec',name='move.escapepod',kind='unkeyed',agreed=False)))
        unkeyed=result['unkeyed']
        self.assertEqual(unkeyed['rows'],[2,1]);self.assertEqual(unkeyed['joined'],1);self.assertEqual(unkeyed['only'],[1,0])
        self.assertEqual((unkeyed['seed_mismatch'],unkeyed['draw_mismatch'],unkeyed['first_tick']),(1,1,5))
        self.assertEqual(unkeyed['first_mismatch']['fields'],['seed','draws']);self.assertEqual(unkeyed['first_mismatch']['why'],2)
        self.assertEqual(unkeyed['by_name']['exec/move.escapepod'],dict(rows=[2,1],only=[1,0],seed_mismatch=1,first_tick=5))
        # Old rows without caller/why still parse and stay out of the natural join.
        old=parse([row(0,5,'a'*16,1,'move.idle')]);self.assertIsNone(old['rows']['a'*16]['caller'])
        self.assertEqual(analyse([old,old])['natural']['joined'],0)

    def test_find_scopes_separate_call_order_from_content(self):
        def find(index,tick,ord,set_,ids,scope='420342c014014929',name='FactionLogic.MaintainMinefields'):
            return (f'[X4LAN] NATIVE_GATE FIND_ORDER_ROW index={index} tick={tick} scope={scope} ord={ord} mode=2 raw=1 '
                    f'dups=0 unmapped=0 n=1 moved=0 set={set_} native={set_} name={name} ids={ids}')
        a=[find(0,807,1,'1'*16,'112'),find(1,807,2,'2'*16,'9b'),find(2,900,1,'3'*16,'2d68a',scope='0'*16,name='move.escapepod')]
        b=[find(0,807,1,'2'*16,'9b'),find(1,807,2,'1'*16,'112')]
        result=analyse([parse(a),parse(b)])['find_order']
        self.assertEqual(result['set_mismatch'],2);self.assertEqual(result['first_set_mismatch'],807)
        self.assertEqual((result['scopes_differing'],result['call_order_only'],result['content']),(2,1,1))
        self.assertEqual(result['first_content_mismatch'],900)
        self.assertEqual([e['kind'] for e in result['scope_examples']],['call_order_only','content'])
        self.assertEqual(result['scope_examples'][1]['calls'],[1,0])

    def test_spawn_generations_join_by_explicit_state_and_use(self):
        def spawn(index,tick,state,entry,exit_,draws,phase=3,normal=1):
            return (f'[X4LAN] NATIVE_GATE SPAWN_RNG_ROW index={index} tick={tick} phase={phase} ui=1 mode=0 normal={normal} '
                    f'state={state} key={"a"*16} caller={"c"*16} entry={entry} exit={exit_} draws={draws}')
        a=[spawn(0,0,'1'*16,'2'*16,'3'*16,40,phase=2),spawn(1,0,'1'*16,'4'*16,'5'*16,38,phase=2),spawn(2,7,'6'*16,'7'*16,'8'*16,12),
           '[X4LAN] NATIVE_GATE SPAWN_RNG_ROW index=9 evicted=1','[X4LAN] NATIVE_GATE SPAWN_RNG_STOP armed=1 failures=0']
        b=[spawn(0,0,'1'*16,'9'*16,'d'*16,41,phase=2),spawn(1,0,'1'*16,'4'*16,'5'*16,38,phase=2),spawn(2,8,'6'*16,'7'*16,'8'*16,12),
           spawn(3,9,'e'*16,'7'*16,'8'*16,12,normal=0)]
        clients=[parse(a),parse(b)]
        self.assertEqual(clients[0]['spawn_status'],'armed=1 failures=0');self.assertEqual(clients[0]['spawn_evicted'],1)
        self.assertEqual(clients[0]['rows'],{})
        result=analyse(clients)['spawn']
        self.assertEqual(result['rows'],[3,4]);self.assertEqual(result['boot'],[2,2]);self.assertEqual(result['joined'],3)
        self.assertEqual(result['only'],[0,1]);self.assertEqual(result['entry_differs'],1);self.assertEqual(result['exit_mismatch'],1)
        self.assertEqual(result['draw_mismatch'],1);self.assertEqual(result['tick_mismatch'],1);self.assertEqual(result['abnormal'],[0,1])
        self.assertEqual(result['first_mismatch'],dict(state='1'*16,n=1,fields=['exit','draws','entry'],tick=[0,0],phase=[2,2],
                                                       draws=[40,41],entry=['2'*16,'9'*16]))
        self.assertEqual([e['fields'] for e in result['examples']],[['exit','draws','entry'],['tick'],['only']])

    def test_rows_evicted_on_the_other_client_are_masked_not_one_sided(self):
        a=[row(5,0,'a'*16,0,'NPC_State_Machines.Base',src=5),row(6,0,'b'*16,0,'NPC_State_Machines.Base',src=5),row(7,1,'c'*16,1,'move.idle')]
        b=['[X4LAN] NATIVE_GATE SCRIPT_RNG_ROW index=5 evicted=1','[X4LAN] NATIVE_GATE SCRIPT_RNG_ROW index=6 evicted=1',
           row(7,1,'c'*16,1,'move.idle'),row(8,1,'d'*16,2,'move.idle')]
        clients=[parse(a),parse(b)]
        self.assertEqual(clients[1]['evicted_index'],{5,6});self.assertEqual(clients[1]['evicted'],2)
        result=analyse(clients)
        self.assertEqual(result['masked'],[2,0])
        self.assertNotIn('cue/NPC_State_Machines.Base',result['scripts'])
        self.assertEqual(result['scripts']['exec/move.idle']['only'],[0,1])

    def test_direct_md_check_sources_are_named(self):
        lines=[row(0,0,'e'*16,1,'Other.Cue1',src=6),row(1,1,'f'*16,0,'Other.Cue1',src=7),row(2,1,'9'*16,2,'Other.Cue2',src=8,state=2)]
        rows=parse(lines)['rows']
        self.assertEqual([rows[k]['source'] for k in ('e'*16,'f'*16,'9'*16)],['timed','wait','raise'])
        self.assertIn('raise/Other.Cue2',analyse([parse(lines),parse(lines[:2])])['scripts'])

    def test_load_time_md_delays_join_by_key(self):
        def resume(index,key,natural,draws,delay,name='Other.Cue1'):
            return (f'[X4LAN] NATIVE_GATE MD_RESUME_RNG_ROW index={index} tick=0 owner=00000000000000a1 slot=0 '
                    f'activation=401c54fdf3b645a2 ord=1 key={key} natural={natural} exit=3333333333333333 draws={draws} '
                    f'delay={delay} unnamed=0 thread=0 name={name}')
        a=[resume(0,'a'*16,'1'*16,1,'4'*16),resume(1,'b'*16,'2'*16,0,'5'*16),resume(2,'c'*16,'2'*16,1,'6'*16),
           '[X4LAN] NATIVE_GATE MD_RESUME_RNG_STOP armed=1 failures=0']
        b=[resume(0,'a'*16,'7'*16,1,'4'*16),resume(1,'b'*16,'2'*16,1,'8'*16),resume(2,'d'*16,'2'*16,1,'6'*16,'Other.Cue2')]
        clients=[parse(a),parse(b)]
        self.assertEqual(clients[0]['resume_status'],'armed=1 failures=0');self.assertEqual(clients[0]['rows'],{})
        result=analyse(clients)['md_resume']
        self.assertEqual(result['rows'],[3,3]);self.assertEqual(result['joined'],2);self.assertEqual(result['only'],[1,1])
        self.assertEqual(result['natural_differs'],1);self.assertEqual(result['delay_mismatch'],1);self.assertEqual(result['draw_mismatch'],1)
        self.assertEqual(result['examples'][0],dict(key='b'*16,name='Other.Cue1',fields=['delay','draws']))


    def test_load_time_ai_waits_join_by_key(self):
        def wait(index,key,natural,draws,due,pre=0,owner='0000000000080d54',name='move.idle'):
            return (f'[X4LAN] NATIVE_GATE AI_WAIT_RNG_ROW index={index} tick=0 owner={owner} script=c5cc1374daaf1dd8 version=4 '
                    f'slot=70 pc=71 start=401baaab8a5ce5b5 ord=1 key={key} natural={natural} exit=3333333333333333 '
                    f'draws={draws} due={due} pre={pre} thread=0 name={name}')
        a=[wait(0,'a'*16,'1'*16,1,'4'*16),wait(1,'b'*16,'2'*16,0,'5'*16,pre=3),wait(2,'c'*16,'2'*16,1,'6'*16),
           '[X4LAN] NATIVE_GATE AI_WAIT_RNG_ROW index=3 evicted=1','[X4LAN] NATIVE_GATE AI_WAIT_RNG_STOP armed=1 failures=0']
        b=[wait(0,'a'*16,'7'*16,1,'4'*16),wait(1,'b'*16,'2'*16,1,'8'*16),
           wait(2,'d'*16,'2'*16,1,'6'*16,pre=-1,owner='000000000011fd90',name='fight.attack.object.fighter')]
        clients=[parse(a),parse(b)]
        self.assertEqual(clients[0]['ai_wait_status'],'armed=1 failures=0');self.assertEqual(clients[0]['rows'],{})
        self.assertEqual(clients[0]['ai_wait_evicted'],1);self.assertEqual(clients[0]['resume'],{})
        result=analyse(clients)['ai_wait']
        self.assertEqual(result['rows'],[3,3]);self.assertEqual(result['evicted'],[1,0]);self.assertEqual(result['joined'],2)
        self.assertEqual(result['only'],[1,1]);self.assertEqual(result['drawn'],1);self.assertEqual(result['natural_differs'],1)
        self.assertEqual(result['due_mismatch'],1);self.assertEqual(result['draw_mismatch'],1);self.assertEqual(result['pre_nonzero'],[1,1])
        self.assertIn(dict(key='b'*16,owner='0000000000080d54',name='move.idle',pc=71,fields=['due','draws']),result['examples'])
        self.assertIn(dict(key='d'*16,owner='000000000011fd90',name='fight.attack.object.fighter',pc=71,only=1),result['examples'])
        self.assertEqual([(e['client'],e['pre']) for e in result['pre_examples']],[(1,3),(2,-1)])
    def test_find_order_rows_separate_membership_from_order(self):
        def find(index,tick,ord,set_,native,n,ids,dups=0):
            return (f'[X4LAN] NATIVE_GATE FIND_ORDER_ROW index={index} tick={tick} scope=420342c014014929 ord={ord} mode=1 raw={n+dups} '
                    f'dups={dups} unmapped=0 n={n} moved=1 set={set_} native={native} name=FactionSubgoal_Recon.Evaluate ids={ids}')
        a=[find(0,63,1,'1'*16,'2'*16,2,'100a,1014'),find(1,63,2,'3'*16,'4'*16,3,'100a,1014,101e'),find(2,63,3,'5'*16,'6'*16,1,'100a'),
           find(3,64,1,'7'*16,'8'*16,1,'1001'),'[X4LAN] NATIVE_GATE FIND_ORDER_STOP armed=1 failures=0']
        b=[find(0,63,1,'1'*16,'9'*16,2,'100a,1014'),find(1,63,2,'a'*16,'4'*16,4,'100a,1014,101e,1028',dups=1),find(2,63,3,'5'*16,'6'*16,1,'100a')]
        clients=[parse(a),parse(b)]
        self.assertEqual(clients[0]['find_status'],'armed=1 failures=0')
        result=analyse(clients)['find_order']
        self.assertEqual(result['rows'],[4,3]);self.assertEqual(result['joined'],3);self.assertEqual(result['only'],[1,0])
        self.assertEqual(result['order_only'],1);self.assertEqual(result['set_mismatch'],1);self.assertEqual(result['count_mismatch'],1)
        self.assertEqual(result['first_set_mismatch'],63);self.assertEqual(result['duplicates'],[0,1])
        self.assertEqual(result['examples'][0]['only_b'],['1028']);self.assertEqual(result['examples'][0]['n'],[3,4])



    def test_region_passes_join_by_region_field_and_rank(self):
        def region(index,tick,word,due,draws=1,field=0,reg='00000000000007ce',mapped=1):
            return (f'[X4LAN] NATIVE_GATE REGION_RNG_ROW index={index} tick={tick} region={reg} field={field} ord=1 mapped={mapped} '
                    f'seeded=0 key=aaaaaaaaaaaaaaaa word={word} exit=3333333333333333 draws={draws} last=4024000000000000 '
                    f'due={due} prior=4023cccccccccccd')
        def status(tick,dueset):
            return (f'[X4LAN] NATIVE_GATE REGION_RNG tick={tick+1} armed=1 installed=1 active=1 mode=0 cur_tick={tick+1} '
                    f'last_tick={tick} last_calls=359 last_keyed=0 last_passes=1 last_draws=1 last_keyset=0000000000000000 '
                    f'last_dueset={dueset}')
        a=[region(0,1,'1'*16,'4'*16),status(1,'a'*16),region(1,64,'2'*16,'5'*16),status(64,'b'*16),region(2,127,'6'*16,'7'*16),
           region(3,127,'6'*16,'7'*16,field=2,reg='0000000000000000',mapped=0),
           '[X4LAN] NATIVE_GATE REGION_RNG_ROW index=9 evicted=1','[X4LAN] NATIVE_GATE REGION_RNG_STOP armed=1 failures=0']
        b=[region(0,1,'1'*16,'4'*16),status(1,'a'*16),region(1,64,'8'*16,'9'*16),status(64,'c'*16),region(2,130,'6'*16,'d'*16)]
        clients=[parse(a),parse(b)]
        self.assertEqual(clients[0]['region_status'],'armed=1 failures=0');self.assertEqual(clients[0]['region_evicted'],1)
        self.assertEqual(clients[0]['rows'],{});self.assertEqual(clients[0]['region_ticks'][64]['dueset'],'b'*16)
        result=analyse(clients)['region']
        self.assertEqual(result['rows'],[4,3]);self.assertEqual(result['joined'],3);self.assertEqual(result['only'],[1,0])
        self.assertEqual(result['tick_mismatch'],1);self.assertEqual(result['due_mismatch'],2);self.assertEqual(result['word_differs'],1)
        self.assertEqual(result['unmapped'],[1,0]);self.assertEqual(result['first_dueset_tick'],64)
        self.assertEqual(result['first_mismatch'],dict(region='00000000000007ce',field=0,n=2,fields=['due'],tick=[64,64],
                                                       word=['2'*16,'8'*16],due=['5'*16,'9'*16]))
        self.assertEqual(result['examples'][1]['fields'],['only']);self.assertEqual(result['examples'][1]['tick'],[127,None])
        self.assertEqual(result['examples'][2]['fields'],['tick','due']);self.assertEqual(result['examples'][2]['tick'],[127,130])

if __name__=='__main__':
    unittest.main()
