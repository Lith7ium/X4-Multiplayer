"""Execute the production NPC Apply/Check XML against controlled native readbacks.

This catches queued-write races and duplicate application; X4 behaviour is still
verified separately by retained native captures.
"""
import copy
import re
import unittest
import xml.etree.ElementTree as E
from pathlib import Path

MD=Path(__file__).resolve().parents[1]/'src/extension/x4_lan_lab/md/x4lan_npc_combat.xml'
TREE=E.parse(MD)
ACTIONS={e.get('name'):e.find('actions') for e in TREE.iter() if e.tag in ('cue','library')}


def evaluate(expr, env):
    text=expr.replace('Session.', '')
    replacements=[
        (re.escape("player.systemtime.{'%Y-%m-%dT%H:%M:%S'}"), "P['wall']"),
        (r'player.timewarp.factor', "P['warp']"),
        (r'player.age / 1s', "P['age']"), (r'player.age', "P['age']"),
        (r'player\.(controlled|occupiedship|container)', r"P['\1']"),
        (r'\$arena\.\$ships.indexof.\{\$o\}', "P['coopIndex']"),
        (r'@?\$arena\.\$ships\.\{\$e\}\.isoperational', "P['attackerOperational']"),
        (r'\$arena\.\$ships\.\{\$e\}', '200'),
        (r'@?\$arena\.\$(\w+)\.\{\$e\}', r"S['\1'][e]"),
        (r'@?\$arena\.\$(\w+)', r"S['\1']"),
        (r'@?\$capture\.\$capFailed', 'False'),
        (r'\$byObject\.\{\$o\}\?', 'True'),
        (r'@?\$rows\.\{\$a\.\$id\}', 'R'),
        (r'\$r\.\$(\w+)\.\{\$e\}', r"R['\1'][e]"),
        (r'@?\$r\.\$(\w+)', r"R['\1']"),
        (r'\$a\.\$(\w+)', r"A['\1']"),
        (r'faction\.\{([^{}]+)\}', r'\1'), (r'faction.player', "'player'"),
        (r'@?\$o\.([a-zA-Z_.]+)', lambda m: "O['%s']"%m[1]),
        (r'\$(failed|applying)', r"S['\1']"),
        (r'\$o\b', '100'), (r'\$r\b', 'R'), (r'\$(ok|e|admitted|amount|counts)\b', r'\1'),
        (r'event.param == event.object', 'True'), (r'event.param3', '40'),
        (r'\bnull\b', 'None'),
        (r'\b([0-9.]+)s\b', r'\1'),
        (r'\bgt\b','>'), (r'\bge\b','>='), (r'\blt\b','<'), (r'\ble\b','<='),
        (r'\btrue\b','True'), (r'\bfalse\b','False'),
    ]
    for pattern, replace in replacements:text=re.sub(pattern,replace,text)
    if '$' in text or 'player.' in text:raise AssertionError('Unsupported expression '+expr)
    return eval(text,{},env)


def run(A,S,R,O,P,action="Apply",nodes=None):
    env=dict(A=A,S=S,R=R,O=O,P=P);effects=[]
    def execute(nodes, env=env):
        chain=False
        for node in nodes:
            tag=node.tag
            if tag in ('do_if','do_elseif','do_else'):
                if tag=='do_if':chain=False
                if not chain and (tag=='do_else' or evaluate(node.get('value'),env)):
                    chain=True;execute(node,env)
            elif tag=='include_actions':execute(ACTIONS[node.get('ref')],env)
            elif tag=='set_value':
                name=node.get('name').replace('Session.', '')
                if name in ('$a','$o'):continue
                if name=='$r':
                    env['R']=None if node.get('exact')=='null' else R
                    continue
                value=evaluate(node.get('exact'),env)
                if name.startswith('$r.$'):
                    field=name[4:]
                    if '.{$e}' in field:R[field.split('.')[0]][env['e']]=value
                    else:R[field]=value
                elif name in ('$failed','$applying'):S[name[1:]]=value
                else:env[name[1:]]=value
            elif tag=='do_all':
                for _ in range(int(evaluate(node.get('exact'),env))):execute(node,env)
            elif tag=='apply_attackstrength':
                effects.append('attack');O['hp']-=A['strength']
                # apply_attackstrength synchronously dispatches damage callbacks.
                # X4's nearest namespace owns $r: missing per-instance scope would
                # let the Classify library erase Apply's live reference.
                shield=next(c for c in TREE.iter('cue') if c.get('name')=='Shield')
                nested=dict(env) if shield.get('namespace')=='this' else env
                execute(ACTIONS['Shield'],nested)
                if O['hp']<=0:O['isoperational']=False
            elif tag in ('set_object_min_hull','destroy_object','set_object_hull','set_object_shield'):effects.append(tag)
            elif tag=='raise_lua_event':effects.append(node.get('name'))
            elif tag=='debug_text':pass
            else:raise AssertionError('Unsupported action '+tag)
    execute(ACTIONS[action] if nodes is None else nodes);return effects


def fixture():
    A=dict(id=1,gen=1,serial=1,object=100,code='NPC-101',macro='ship_macro',owner='argon',sector='far_macro',
           shooter=2,sgen=4,state=3,base=0,count=3,strength=40,stamp='wall',deadline=101,**{'from':100})
    S=dict(host=True,failed=False,applying=False,shotFailed=False,shotState=3,shotStateAt=100,
           shotCurrentGen={2:4},healthClosed={2:0},healthLive={2:True},healthGen={2:4},dead={2:False},shotStrength={2:40})
    R=dict(id=1,object=100,serial=1,gen=1,dead=False,retired=False,at=100,gens={1:0,2:0},counts={1:0,2:0})
    O={'isoperational':True,'idcode':'NPC-101','macro.id':'ship_macro','owner':'argon','sector.macro.id':'far_macro',
       'isclass.ship_s':True,'isclass.ship_m':False,'dock':False,'isinternallystored':False,'hp':1000}
    P=dict(wall='wall',age=100.1,warp=1,controlled=999,occupiedship=999,container=999,coopIndex=0,attackerOperational=True)
    return A,S,R,O,P


class NpcNativeFenceTests(unittest.TestCase):
    def test_terminal_replica_destruction_is_fenced_and_cannot_repeat(self):
        a,s,r,o,p=fixture();s['host']=False;a['sequence']=2;r['seq']=1
        self.assertIn('destroy_object',run(a,s,r,o,p,'Destroy'))
        self.assertNotIn('destroy_object',run(a,s,r,o,p,'Destroy'))
        a,s,r,o,p=fixture();s['host']=False;a['sequence']=2;r['seq']=1;p['occupiedship']=100
        self.assertNotIn('destroy_object',run(a,s,r,o,p,'Destroy'))

    def test_namespace_regression_fixture_reproduces_nested_callback_corruption(self):
        shield=next(c for c in TREE.iter('cue') if c.get('name')=='Shield')
        original=shield.get('namespace')
        self.assertEqual(original,'this')
        try:
            shield.attrib.pop('namespace')
            with self.assertRaises(TypeError):run(*fixture())
        finally:shield.set('namespace',original)

    def test_lost_native_ack_and_reordered_claim_apply_each_hit_once(self):
        a,s,r,o,p=fixture()
        self.assertEqual(run(a,s,r,o,p).count('attack'),3)
        self.assertEqual(run(a,s,r,o,p).count('attack'),0)
        a['base'],a['count']=3,5
        self.assertEqual(run(a,s,r,o,p).count('attack'),2)
        a['base'],a['count']=0,3
        self.assertEqual(run(a,s,r,o,p).count('attack'),0)
        self.assertEqual((o['hp'],r['counts'][2]),(800,5))

    def test_queued_action_rechecks_native_target_changes(self):
        for field,value in [('idcode','NEW-101'),('owner','player'),('sector.macro.id','other_macro'),
            ('macro.id','different_macro'),('dock',True),('isinternallystored',True),('isoperational',False)]:
            with self.subTest(field=field):
                a,s,r,o,p=fixture();o[field]=value
                self.assertNotIn('attack',run(a,s,r,o,p));self.assertEqual(r['counts'][2],0)

    def test_paused_stale_occupied_or_closed_shooter_never_writes(self):
        for field,value in [('warp',0),('wall','later'),('age',102),('controlled',100),('occupiedship',100),('container',100)]:
            with self.subTest(field=field):
                a,s,r,o,p=fixture();p[field]=value
                self.assertNotIn('attack',run(a,s,r,o,p))
        a,s,r,o,p=fixture();s['healthClosed'][2]=4
        self.assertNotIn('attack',run(a,s,r,o,p))

    def test_owner_or_sector_roundtrip_cannot_reopen_a_retired_identity(self):
        a,s,r,o,p=fixture();r['retired']=True
        self.assertNotIn('attack',run(a,s,r,o,p))
        self.assertEqual(r['counts'][2],0)

    def test_death_stops_remaining_hits_and_does_not_repeat(self):
        a,s,r,o,p=fixture();o['hp']=50
        self.assertEqual(run(a,s,r,o,p).count('attack'),2)
        self.assertTrue(r['dead']);self.assertEqual(r['counts'][2],3)
        self.assertNotIn('attack',run(a,s,r,o,p))


def bind_write():
    """The guest's queued host-health write inside Bind (identity checks are covered above)."""
    return next(n for n in ACTIONS['Bind'].iter('do_if') if n.get('value')=='$a.$sequence gt $r.$seq')


class NpcReplicaHealthWriteTests(unittest.TestCase):
    def write(self,hull,shield,local_hull,local_shield,sequence=2):
        a,s,r,o,p=fixture();s['host']=False
        a.update(hull=hull,shield=shield,sequence=sequence);r['seq']=1
        o.update(hullpercentage=local_hull,shieldpercentage=local_shield)
        return run(a,s,r,o,p,nodes=[bind_write()]),r,s

    def test_unchanged_host_readback_never_rewrites_shield(self):
        # The host bumps the sequence on every 10 Hz readback. Rewriting the same value
        # re-shows the HUD shield bar of every bound replica, which made it flash.
        for local in (100,99.6,100.4):
            with self.subTest(local=local):
                effects,r,s=self.write(100,100,local,local)
                self.assertNotIn('set_object_shield',effects);self.assertNotIn('set_object_hull',effects)
                self.assertEqual(r['seq'],2);self.assertFalse(s['applying'])

    def test_real_host_change_is_still_written(self):
        effects,r,_=self.write(80,40,100,100)
        self.assertEqual((effects.count('set_object_hull'),effects.count('set_object_shield')),(1,1))
        effects,_,_=self.write(100,40,100,100)
        self.assertEqual(effects,['set_object_shield'])
        effects,_,_=self.write(50,100,100,100)
        self.assertEqual(effects,['set_object_hull'])

    def test_old_sequence_writes_nothing(self):
        effects,r,_=self.write(10,0,100,100,sequence=1)
        self.assertEqual(effects,[]);self.assertEqual(r['seq'],1)

    def test_every_guest_shield_write_is_guarded_or_a_callback_restore(self):
        # Periodic writers must compare first; only damage-callback restores may write directly.
        for name in ('x4lan_npc_combat.xml','x4lan_bubble.xml','x4lan_health.xml'):
            text=(MD.parent/name).read_text(encoding='utf-8')
            for m in re.finditer(r'<set_object_shield object="([^"]+)" exact="([^"]+)"',text):
                with self.subTest(file=name,write=m[0]):
                    before=text[max(0,m.start()-400):m.start()]
                    restore='$o.shield + $amount' in m[2]
                    self.assertTrue(restore or before.rstrip().endswith('gt 0.25">'),m[0])
