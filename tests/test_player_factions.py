"""Player factions: factions.xml pool diff, MD schema, Lua core (LuaJIT) and relay routing.

Synthetic peers only: proves mapping, protocol and authority rules, not native X4 behaviour.
Schema checks use the read-only x4-re extraction when present (X4_RE or ../x4-re).
"""
import os
import re
import unittest
import xml.etree.ElementTree as E
from pathlib import Path

from x4lan.host import Session
from x4lan.players import PlayerRoster

try:
    from lupa.luajit21 import LuaRuntime
except ImportError:
    LuaRuntime = None
try:
    from lxml import etree
except ImportError:
    etree = None

ROOT = Path(__file__).resolve().parents[1]
EXT = ROOT / 'src/extension/x4_lan_lab'
FACTIONS = EXT / 'libraries/factions.xml'
MD = EXT / 'md/x4lan_player_factions.xml'
RE = Path(os.environ.get('X4_RE', ROOT.parent / 'x4-re'))
LIB = RE / 'gamescripts/resolved/base/libraries'
VANILLA = RE / 'work/pfaction/lib'
TOKEN = 'a' * 32
P1, P2 = ('192.0.2.1', 1000), ('192.0.2.2', 2000)


def gen_locked():
    import importlib.util
    spec = importlib.util.spec_from_file_location('gen', ROOT / 'tools/dev/build_player_factions.py')
    gen = importlib.util.module_from_spec(spec); spec.loader.exec_module(gen)
    return gen.LOCKED


class FactionPoolDiff(unittest.TestCase):
    def setUp(self):
        self.tree = E.parse(FACTIONS)
        self.pool = self.tree.getroot().findall("add[@sel='/factions']/faction")

    def test_icons_are_generated_per_slot_and_never_the_player_logo(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location('gen', ROOT / 'tools/dev/build_player_factions.py')
        gen = importlib.util.module_from_spec(spec); spec.loader.exec_module(gen)
        self.assertEqual((EXT / 'libraries/icons.xml').read_text(encoding='utf-8'), gen.build_icons())
        for f in self.pool:
            index = f.get('id')[len('x4lan_player'):]
            self.assertEqual((f.find('icon').get('active'), f.find('icon').get('inactive')), (f.get('id'),) * 2)
            self.assertTrue((EXT / f'assets/textures/ui/x4lan/x4lan_player{index}.dds').is_file())
            r, g, b = (int(f.find('color').get(k)) for k in 'rgb')
            self.assertFalse(g > r + 40 and g > b + 40, f.get('id') + ' reads as the player green')
            self.assertFalse(b > r + 40 and b > 150 and g > 100 and r < 100, f.get('id') + ' reads as friendly azure')
            self.assertFalse(r > 200 and g < 110 and b < 110, f.get('id') + ' reads as hostile red')

    def test_generator_matches_committed_file(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location('gen', ROOT / 'tools/dev/build_player_factions.py')
        gen = importlib.util.module_from_spec(spec); spec.loader.exec_module(gen)
        self.assertEqual(FACTIONS.read_text(encoding='utf-8'), gen.build())

    def test_pool_ids_unique_inactive_unknown_without_economy(self):
        ids = [f.get('id') for f in self.pool]
        self.assertEqual(ids, [f'x4lan_player{i}' for i in range(1, 17)])
        self.assertEqual(len(set(ids)), 16)
        colours = set()
        for f in self.pool:
            tags = f.get('tags', '').split()
            self.assertNotIn('economic', tags)
            self.assertEqual(sorted(tags), ['claimspace', 'police'])  # as faction "player"
            self.assertEqual(f.get('policefaction'), f.get('id'))
            self.assertEqual((f.get('active'), f.get('known')), ('0', '0'))
            self.assertEqual(f.get('behaviourset'), 'default')
            self.assertFalse(re.search(r'\{\d+,\s*\d+\}', f.get('name')))
            c = f.find('color')
            colours.add((c.get('r'), c.get('g'), c.get('b')))
            self.assertIsNone(c.get('ref'))
            rel = {r.get('faction'): float(r.get('relation')) for r in f.find('relations')}
            self.assertEqual(rel.pop('player'), .75)
            # Relation-locked NPC factions cannot be changed at runtime: the player's values, static.
            self.assertEqual({k: rel.pop(k) for k in dict(gen_locked())}, {k: float(v) for k, v in gen_locked()})
            self.assertEqual(set(rel), set(ids) - {f.get('id')})  # allied to every other human
            self.assertTrue(all(v == .75 for v in rel.values()))
        for other, value in gen_locked():
            add = self.tree.getroot().find(f"add[@sel=\"/factions/faction[@id='{other}']/relations\"]")
            self.assertEqual([(r.get('faction'), r.get('relation')) for r in add], [(i, value) for i in ids])
        self.assertEqual(len(colours), 16)
        player = self.tree.getroot().find("add[@sel=\"/factions/faction[@id='player']/relations\"]")
        self.assertEqual([r.get('faction') for r in player], ids)

    def test_no_faction_logic_or_economy_references(self):
        for path in (EXT / 'md').glob('*.xml'):
            text = path.read_text(encoding='utf-8')
            self.assertNotIn('FactionLogic', text if path != MD else '')
        self.assertNotIn('economic', MD.read_text(encoding='utf-8'))


# Game-schema checks need lxml and the private read-only x4-re extraction (X4_RE or ../x4-re).
# They are collected only where both exist, so tools/dev/check.py (stdlib + lupa) does not count them
# as skipped coverage; run them on the RE machine: python -m unittest tests.test_player_factions
SCHEMAS = bool(etree and (LIB / 'md.xsd').is_file() and (LIB / 'factions.xsd').is_file())
VANILLA_FACTIONS = SCHEMAS and (VANILLA / '08__libraries_factions.xml').is_file()


class _VanillaDiffChecks:
    def test_diff_applies_to_vanilla_and_validates(self):
        base = etree.parse(str(VANILLA / '08__libraries_factions.xml'))
        vanilla_ids = {f.get('id') for f in base.getroot().iter('faction')}
        for add in etree.parse(str(FACTIONS)).getroot():
            self.assertEqual(add.tag, 'add')
            targets = base.xpath(add.get('sel'))
            self.assertEqual(len(targets), 1, add.get('sel'))
            for child in add:
                targets[0].append(child)
        ids = [f.get('id') for f in base.getroot().findall('faction')]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertFalse(vanilla_ids & {f'x4lan_player{i}' for i in range(1, 17)})
        schema = etree.XMLSchema(etree.parse(str(LIB / 'factions.xsd')))
        self.assertTrue(schema.validate(base), schema.error_log)
        if (LIB / 'diff.xsd').is_file():
            diff = etree.XMLSchema(etree.parse(str(LIB / 'diff.xsd')))
            self.assertTrue(diff.validate(etree.parse(str(FACTIONS))), diff.error_log)


if VANILLA_FACTIONS:
    class VanillaFactionsDiff(_VanillaDiffChecks, unittest.TestCase):
        pass

if SCHEMAS:
    class PlayerFactionsMdSchema(unittest.TestCase):
        def test_md_validates_against_game_schema(self):
            schema = etree.XMLSchema(etree.parse(str(LIB / 'md.xsd')))
            self.assertTrue(schema.validate(etree.parse(str(MD))), schema.error_log)


def joined():
    session = Session(TOKEN, 'build', 'bubble')
    lease1 = session.receive(f'HELLO|1|{TOKEN}|build|s_macro|bubble|p1|{"1"*32}'.encode(), P1, 0)[0].decode().split('|')[3]
    lease2 = session.receive(f'HELLO|1|{TOKEN}|build|s_macro|bubble|p2|{"2"*32}'.encode(), P2, 0)[0].decode().split('|')[3]
    return session, lease1, lease2


class RelayRoster(unittest.TestCase):
    def test_roster_relations_requests_and_sector_routing(self):
        session, l1, l2 = joined()
        r = PlayerRoster()
        name = 'Alice'.encode().hex()
        self.assertEqual(r.handle(f'PFID|1|{l1}|1|{name}|faction_player'.encode(), P1, session.peers, 0), [])
        r.handle(f'PFID|1|{l2}|1|{"426f62"}|faction_player'.encode(), P2, session.peers, 0)
        # Wrong address / lease / malformed: ignored.
        self.assertEqual(r.handle(f'PFID|1|{l1}|2|{name}|x'.encode(), P2, session.peers, 0), [])
        self.assertEqual(r.handle(f'PFID|1|{l1}|3|zz|x'.encode(), P1, session.peers, 0), [])
        rows = [p.decode() for _, p in r.snapshots(session.peers, 1)]
        self.assertIn(f'PFROSTER|1|{l2}|1|{name}|faction_player|1', rows)
        self.assertIn(f'PFROSTER|1|{l1}|2|426f62|faction_player|1', rows)
        # Only the host may author relations; forwarded to the guest only.
        self.assertEqual(r.handle(f'PFREL|1|{l2}|1|1|2|-0.5'.encode(), P2, session.peers, 1), [])
        out = r.handle(f'PFREL|1|{l1}|1|1|2|0.75'.encode(), P1, session.peers, 1)
        self.assertEqual(out, [(P2, f'PFREL|1|{l2}|1|1|2|0.7500'.encode())])
        self.assertEqual(r.handle(f'PFREL|1|{l1}|1|1|2|-1'.encode(), P1, session.peers, 1), [])  # stale rev
        self.assertEqual(r.handle(f'PFREL|1|{l1}|2|1|2|2'.encode(), P1, session.peers, 1), [])   # out of range
        # Guest relation intent goes to the host, tagged with the sender's own index.
        out = r.handle(f'PFREQ|1|{l2}|5|1|-0.5'.encode(), P2, session.peers, 1)
        self.assertEqual(out, [(P1, f'PFREQ|1|{l1}|2|1|1|-0.5000'.encode())])
        self.assertEqual(r.handle(f'PFREQ|1|{l2}|6|2|-0.5'.encode(), P2, session.peers, 1), [])  # self
        # Sectors: host rows cached and forwarded; a guest may only claim for itself.
        out = r.handle(f'PFSEC|1|{l1}|1|cluster_14_sector001_macro|S1'.encode(), P1, session.peers, 1)
        self.assertEqual(out, [(P2, f'PFSEC|1|{l2}|1|cluster_14_sector001_macro|S1'.encode())])
        self.assertEqual(r.handle(f'PFSEC|1|{l2}|1|cluster_01_sector001_macro|S1'.encode(), P2, session.peers, 1), [])
        out = r.handle(f'PFSEC|1|{l2}|2|cluster_01_sector001_macro|S2'.encode(), P2, session.peers, 1)
        self.assertEqual(out, [(P1, f'PFSEC|1|{l1}|2|cluster_01_sector001_macro|S2'.encode())])
        rows = [p.decode() for _, p in r.snapshots(session.peers, 10)]
        self.assertIn(f'PFSEC|1|{l2}|1|cluster_14_sector001_macro|S1', rows)
        self.assertIn(f'PFREL|1|{l2}|1|1|2|0.7500', rows)

    def test_new_host_lease_forgets_authority_state(self):
        session, l1, _ = joined()
        r = PlayerRoster()
        r.handle(f'PFREL|1|{l1}|1|1|2|0.75'.encode(), P1, session.peers, 0)
        session.peers['p1'].lease = 'f' * 32
        r.snapshots(session.peers, 1)
        self.assertEqual(r.relations, {})

    def test_relation_intents_ignore_duplicates_and_reordering_per_client_lease(self):
        session, l1, l2 = joined()
        r = PlayerRoster()

        def request(seq, value, lease=l2, target=1):
            return r.handle(f'PFREQ|1|{lease}|{seq}|{target}|{value}'.encode(), P2, session.peers, 0)

        self.assertTrue(request(10, '-0.5'))
        self.assertTrue(request(12, '0.75'))
        self.assertEqual(request(11, '-0.5'), [])
        self.assertEqual(request(12, '0'), [])
        # Invalid targets or values must not consume a later valid request's sequence.
        self.assertEqual(request(14, '0', target=2), [])
        self.assertEqual(request(14, '2'), [])
        self.assertTrue(request(13, '0'))
        session.peers['p2'].lease = 'e' * 32
        self.assertTrue(request(1, '0.75', lease='e' * 32))
        self.assertEqual(request(100, '-0.5'), [])  # old lease cannot poison the fence
        self.assertTrue(request(2, '0', lease='e' * 32))


@unittest.skipUnless(LuaRuntime, 'lupa.luajit21 not installed')
class LuaCore(unittest.TestCase):
    def runtime(self):
        import ctypes, lupa.luajit21
        ctypes.CDLL(lupa.luajit21.__file__, mode=ctypes.RTLD_GLOBAL)
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute((EXT / 'ui/player_factions_core.lua').read_text(encoding='utf-8'))
        lua.execute('''
            function make(slot,dedicated)
              local io={sent={},calls={},logs={}}
              io.send=function(p) io.sent[#io.sent+1]=p end
              io.md=function(a,t) io.calls[#io.calls+1]={action=a,args=t} end
              io.log=function(l) io.logs[#io.logs+1]=l end
              return X4LAN_PLAYERS_CORE.new({slot=slot,dedicated=dedicated},io),io
            end
            function split(p) local f={};for v in (p..'|'):gmatch('(.-)|') do f[#f+1]=v end;return f end
        ''')
        return lua

    def test_mapping_tokens_names_and_relations(self):
        lua = self.runtime()
        lua.execute('''
            local K=X4LAN_PLAYERS_CORE
            assert(K.index('p1')==1 and K.index('p16')==16 and not K.index('p17') and not K.index('x'))
            assert(K.factionFor(1,1)=='player' and K.factionFor(2,1)=='x4lan_player2' and K.factionFor(1,2)=='x4lan_player1')
            assert(K.tokenFor('player',2)=='S2' and K.tokenFor('x4lan_player1',2)=='S1' and K.tokenFor('argon',2)=='argon')
            assert(K.factionForToken('S2',2)=='player' and K.factionForToken('S1',2)=='x4lan_player1')
            assert(K.factionForToken('S17',1)==nil and K.factionForToken('bad id',1)==nil)
            assert(K.poolIndex('x4lan_player07')==nil and K.poolIndex('x4lan_player7')==7)
            assert(K.relation('allied')==0.75 and K.relation('hostile')==-0.5 and K.relation(2)==nil and K.relation(0/0)==nil)
            assert(K.cleanName('  Bob\\n{1001,2} ')=='Bob1001,2' and K.cleanName('\\1')==nil)
            local long=string.rep('\\195\\164',40)
            local c=K.cleanName(long);assert(#c<=48 and K.decode(K.encode(c))==c)
            assert(K.decode('7b7d')==nil and K.decode('41')=='A' and K.decode('4')==nil)
        ''')

    def test_host_assigns_guest_identity_and_publishes_default_alliance(self):
        lua = self.runtime()
        lua.execute('''
            local p,io=make('p1')
            assert(p.receive('L1',split('PFROSTER|1|L1|1|416c696365|faction_player|1'),1))
            assert(#io.calls==0)  -- own human stays `player`
            p.receive('L1',split('PFROSTER|1|L1|2|426f62|faction_argon|3'),1)
            assert(io.calls[1].action=='Assign' and io.calls[1].args.faction=='x4lan_player2' and io.calls[1].args.name=='Bob')
            assert(io.calls[1].args.icon=='faction_argon' and io.calls[1].args.index==2)
            assert(io.calls[2].action=='Relation' and io.calls[2].args.fa=='player' and io.calls[2].args.fb=='x4lan_player2')
            assert(io.calls[2].args.value==0.75)
            assert(io.sent[1]=='PFREL|1|L1|1|1|2|0.7500')
            p.receive('L1',split('PFROSTER|1|L1|2|426f62|faction_argon|4'),2)
            assert(#io.calls==2)  -- unchanged identity is not reapplied
            -- Guest intent through the relay; then a local host intent.
            p.receive('L1',split('PFREQ|1|L1|2|5|1|-0.5000'),3)
            assert(io.calls[3].action=='Relation' and io.calls[3].args.value==-0.5 and io.sent[2]=='PFREL|1|L1|2|1|2|-0.5000')
            local ok,how=p.setRelation('L1','p2','neutral')
            assert(ok and how=='applied' and io.calls[4].args.value==0 and io.sent[3]=='PFREL|1|L1|3|1|2|0.0000')
            assert(not p.setRelation('L1','p1','neutral'))
            p.receive('L1',split('PFREQ|1|L1|3|5|1|-0.5000'),3)
            assert(#io.calls==4)  -- a human not in the roster cannot change relations
        ''')

    def test_guest_mirrors_relations_in_order_and_requests_changes(self):
        lua = self.runtime()
        lua.execute('''
            local p,io=make('p2')
            p.receive('L2',split('PFROSTER|1|L2|1|416c696365|faction_player|1'),1)
            assert(io.calls[1].action=='Assign' and io.calls[1].args.faction=='x4lan_player1' and io.calls[1].args.short=='Alice')
            p.receive('L2',split('PFREL|1|L2|2|1|2|-0.5000'),1)
            assert(io.calls[2].action=='Relation' and io.calls[2].args.fa=='x4lan_player1' and io.calls[2].args.fb=='player')
            p.receive('L2',split('PFREL|1|L2|1|1|2|0.7500'),1)   -- older revision
            p.receive('L2',split('PFREL|1|L2|2|1|2|-0.5000'),1)  -- duplicate
            assert(#io.calls==2)
            local ok,how=p.setRelation('L2','p1','allied')
            assert(ok and how=='requested' and io.sent[#io.sent]:match('^PFREQ|1|L2|%d+|1|0.7500$'))
            assert(#io.calls==2)  -- a guest never applies its own intent
            p.receive('L2',split('PFREQ|1|L2|1|5|2|0.5'),1)
            assert(#io.calls==2)
        ''')

    def test_sector_ownership_mapping_both_directions(self):
        lua = self.runtime()
        lua.execute('''
            local h,hio=make('p1')
            local g,gio=make('p2')
            for _,c in ipairs({h,g}) do
              c.receive('L',split('PFROSTER|1|L|1|41|faction_player|1'),0)
              c.receive('L',split('PFROSTER|1|L|2|42|faction_player|1'),0)
            end
            hio.calls,gio.calls,hio.sent,gio.sent={},{},{},{}
            -- Host claims a sector: authority row with the host's token.
            h.localSector('L','cluster_14_sector001_macro','player')
            assert(hio.sent[1]=='PFSEC|1|L|1|cluster_14_sector001_macro|S1')
            g.receive('L',split(hio.sent[1]),1)
            assert(gio.calls[1].action=='Sector' and gio.calls[1].args.faction=='x4lan_player1')
            -- MD then reports the applied owner; a guest never re-sends authority rows.
            g.localSector('L','cluster_14_sector001_macro','x4lan_player1')
            assert(#gio.sent==0)
            -- Guest's own claim becomes an intent; the host applies it to the guest's pool faction.
            g.localSector('L','cluster_01_sector001_macro','player')
            assert(gio.sent[1]=='PFSEC|1|L|1|cluster_01_sector001_macro|S2')
            h.receive('L',split(gio.sent[1]),2)
            assert(hio.calls[1].action=='Sector' and hio.calls[1].args.faction=='x4lan_player2')
            h.localSector('L','cluster_01_sector001_macro','x4lan_player2')
            assert(hio.sent[2]=='PFSEC|1|L|2|cluster_01_sector001_macro|S2')
            g.receive('L',split(hio.sent[2]),3)
            assert(#gio.calls==1)  -- already player-owned locally: nothing to apply
            g.localSector('L','cluster_01_sector001_macro','player')
            assert(#gio.sent==1)  -- confirmed by the authority, no second claim
            -- A guest cannot claim for another human; a release reaches the guest as the NPC owner.
            h.receive('L',split('PFSEC|1|L|9|cluster_02_sector001_macro|S1'),4)
            assert(#hio.calls==1)
            h.localSector('L','cluster_14_sector001_macro','argon')
            g.receive('L',split(hio.sent[3]),5)
            assert(gio.calls[2].args.faction=='argon')
        ''')

    def test_human_factions_travel_only_as_tokens(self):
        lua = self.runtime()
        lua.execute('''
            local K=X4LAN_PLAYERS_CORE
            assert(K.factionForToken('player',2)==nil and K.factionForToken('x4lan_player1',2)==nil)
            assert(K.factionForToken('xenon',2)=='xenon')
            local g=make('p2')
            assert(g.localFaction('x4lan_player2')=='player' and g.localFaction('x4lan_player1')=='x4lan_player1')
            assert(g.localFaction('argon')=='argon')
        ''')

    def test_coop_ship_owner_follows_the_flying_human(self):
        lua = self.runtime()
        lua.execute('''
            local K=X4LAN_PLAYERS_CORE
            local h=make('p1');local g=make('p2');local s=make('p1',true)
            -- Nobody at the controls: shared `player` everywhere (handoff and docking unchanged).
            assert(h.coopOwner('0')=='player' and g.coopOwner('0')=='player' and s.coopOwner('0')=='player')
            -- Ship flown by client 2: client 2 sees its own `player`, everybody else its pool faction.
            assert(g.coopOwner('p2')=='player' and h.coopOwner('p2')=='x4lan_player2' and s.coopOwner('p2')=='x4lan_player2')
            assert(g.coopOwner('p1')=='x4lan_player1' and h.coopOwner('p1')=='player')
            assert(K.coopOwner('p99',1)=='player' and K.coopOwner(nil,1)=='player')
        ''')

    def test_server_has_no_local_human(self):
        lua = self.runtime()
        lua.execute('''
            local s,io=make('p1',true)
            assert(s.server and s.authority and s.local_==nil)
            assert(s.identity('L1','Server','faction_player')==false and #io.sent==0)
            assert(s.faction(1)=='x4lan_player1' and s.faction(2)=='x4lan_player2')
            s.receive('L1',split('PFROSTER|1|L1|2|41|faction_player|1'),1)
            s.receive('L1',split('PFROSTER|1|L1|3|42|faction_player|1'),1)
            assert(io.calls[1].action=='Assign' and io.calls[1].args.faction=='x4lan_player2')
            assert(io.calls[2].action=='Assign' and io.calls[2].args.faction=='x4lan_player3')
            -- Default alliance between the two clients, authored by the server.
            assert(io.calls[3].action=='Relation' and io.calls[3].args.fa=='x4lan_player2' and io.calls[3].args.fb=='x4lan_player3')
            assert(io.sent[1]=='PFREL|1|L1|1|2|3|0.7500')
            local ok,why=s.setRelation('L1','p2','hostile');assert(not ok and why=='no_local_human')
            -- Client 2 asks for war with client 3; the server applies and publishes it.
            s.receive('L1',split('PFREQ|1|L1|2|1|3|-0.5000'),2)
            assert(io.calls[4].args.value==-0.5 and io.sent[2]=='PFREL|1|L1|2|2|3|-0.5000')
            -- The server's own `player` (observer) is no human: never published as a sector owner.
            s.localSector('L1','cluster_14_sector001_macro','player')
            assert(#io.sent==2)
            s.localSector('L1','cluster_14_sector001_macro','x4lan_player3')
            assert(io.sent[3]=='PFSEC|1|L1|1|cluster_14_sector001_macro|S3')
            s.baseline={};assert(s.personalOwner('7')==nil)
            assert(not pcall(make,'p2',true))
        ''')

    def test_three_clients_get_pairwise_default_alliances(self):
        lua = self.runtime()
        lua.execute('''
            local h,io=make('p1')
            for i=1,3 do h.receive('L',split('PFROSTER|1|L|'..i..'|41|faction_player|1'),0) end
            local seen={}
            for _,c in ipairs(io.calls) do if c.action=='Relation' then seen[#seen+1]=c.args.a..'-'..c.args.b end end
            table.sort(seen);assert(table.concat(seen,',')=='1-2,1-3,2-3',table.concat(seen,','))
        ''')

    def test_default_or_colliding_names_and_player_icon_are_replaced(self):
        lua = self.runtime()
        lua.execute('''
            local K=X4LAN_PLAYERS_CORE
            assert(K.displayName('Multiplayer Pilot',3,'Alice')=='Player 3')
            assert(K.displayName('',2,nil)=='Player 2' and K.displayName(nil,2,nil)=='Player 2')
            assert(K.displayName('Alice',2,'Alice')=='Player 2' and K.displayName('Bob',2,'Alice')=='Bob')
            assert(K.displayIcon('faction_player',4)=='x4lan_player4' and K.displayIcon('faction_argon',4)=='faction_argon')
            -- Both clients left at the gamestart default name (live 8-player report): distinct pool identities.
            local g,io=make('p2')
            g.identity('L2','Multiplayer Pilot','faction_player')
            local mp='4d756c7469706c617965722050696c6f74'
            g.receive('L2',split('PFROSTER|1|L2|2|'..mp..'|faction_player|1'),1)
            g.receive('L2',split('PFROSTER|1|L2|3|'..mp..'|faction_player|1'),1)
            local a=io.calls[1].args
            assert(io.calls[1].action=='Assign' and a.faction=='x4lan_player3' and a.name=='Player 3' and a.short=='Player3' and a.icon=='x4lan_player3')
            -- A renamed client: reassigned under its real name.
            g.receive('L2',split('PFROSTER|1|L2|3|426f62|faction_player|2'),2)
            assert(io.calls[#io.calls].args.name=='Bob')
        ''')

    def test_host_ship_ownership_only_after_baseline(self):
        lua = self.runtime()
        lua.execute('''
            local h=make('p1');local g=make('p2')
            assert(h.personalOwner('5')==nil)
            h.baseline={['5']=true}
            assert(h.personalOwner('5')==nil and h.personalOwner('6')=='x4lan_player1')
            g.baseline={}
            assert(g.personalOwner('6')==nil)
        ''')

    def test_lua_cores_through_relay(self):
        """Two Lua cores exchange identities and a relation change through PlayerRoster."""
        lua = self.runtime()
        session, l1, l2 = joined()
        relay = PlayerRoster()
        lua.execute('h,hio=make("p1");g,gio=make("p2")')
        g = lua.globals()
        addr = {'p1': P1, 'p2': P2}
        leases = {'p1': l1, 'p2': l2}
        cores = {'p1': (g.h, g.hio), 'p2': (g.g, g.gio)}

        def pump(now):
            for slot, (core, io) in cores.items():
                sent = list(io.sent.values()); io.sent = lua.table()
                for packet in sent:
                    for target, out in relay.handle(packet.encode(), addr[slot], session.peers, now):
                        deliver(target, out, now)
            for target, out in relay.snapshots(session.peers, now):
                deliver(target, out, now)

        def deliver(target, packet, now):
            slot = 'p1' if target == P1 else 'p2'
            core = cores[slot][0]
            core.receive(leases[slot], g.split(packet.decode()), now)

        g.h.identity(l1, 'Alice', 'faction_player')
        g.g.identity(l2, 'Bob', None)
        for t in range(4):
            pump(t * 1.0)
        host_md = [(m.action, dict(m.args)) for m in g.hio.calls.values()]
        guest_md = [(m.action, dict(m.args)) for m in g.gio.calls.values()]
        self.assertIn(('Assign', {'index': 2, 'faction': 'x4lan_player2', 'name': 'Bob', 'short': 'Bob', 'icon': 'x4lan_player2'}), host_md)
        self.assertIn(('Assign', {'index': 1, 'faction': 'x4lan_player1', 'name': 'Alice', 'short': 'Alice', 'icon': 'x4lan_player1'}), guest_md)
        rel = [a for k, a in guest_md if k == 'Relation']
        self.assertEqual((rel[-1]['fa'], rel[-1]['fb'], rel[-1]['value']), ('x4lan_player1', 'player', .75))
        ok, how = g.g.setRelation(l2, 'p1', 'hostile')
        self.assertEqual((ok, how), (True, 'requested'))
        for t in range(4, 7):
            pump(t * 1.0)
        rel = [dict(m.args) for m in g.gio.calls.values() if m.action == 'Relation']
        self.assertEqual(rel[-1]['value'], -.5)
        rel = [dict(m.args) for m in g.hio.calls.values() if m.action == 'Relation']
        self.assertEqual((rel[-1]['fa'], rel[-1]['fb'], rel[-1]['value']), ('player', 'x4lan_player2', -.5))

    def test_relation_intents_survive_both_udp_hop_reordering_and_reconnects(self):
        """Actual Python relay + Lua client lease validation + Lua authority core."""
        lua = self.runtime()
        lua.execute((EXT / 'ui/client.lua').read_text(encoding='utf-8'))
        lua.execute('''
          authority,aio=make('p1')
          function attachAuthority(lease)
            local transport={send=function()end,receive=function()end}
            local cfg={slot='p1',token=string.rep('a',32),build='test',bubble='test',sector='s'}
            receiver=X4LAN_CLIENT.new(cfg,transport,string.rep('1',32))
            receiver:receive('WELCOME|1|'..string.rep('1',32)..'|'..lease..'|20',0)
            receiver.receiveExtension=function(self,f,now) return authority.receive(self.lease,f,now) end
          end
        ''')
        g = lua.globals()
        session, authority_lease, client_lease = joined()
        relay = PlayerRoster()
        g.attachAuthority(authority_lease)
        for index in (1, 2):
            g.authority.receive(authority_lease, g.split(f'PFROSTER|1|{authority_lease}|{index}|41|faction_player|1'), 0)

        def request(seq, value, target=1, lease=None):
            return relay.handle(f'PFREQ|1|{lease or client_lease}|{seq}|{target}|{value}'.encode(), P2, session.peers, 0)

        def deliver(messages):
            for address, packet in messages:
                self.assertEqual(address, P1)
                g.receiver.receive(g.receiver, packet.decode(), 1)

        def value():
            return g.authority.applied['1-2'].value

        # First hop: a delayed hostile request cannot override a newer allied request.
        deliver(request(2, '.75'))
        self.assertEqual(request(1, '-.5'), [])
        self.assertEqual(value(), .75)
        # Second hop: forward both, then deliver newer neutral before older hostile.
        older, newer = request(3, '-.5'), request(4, '0')
        deliver(newer); deliver(older); deliver(newer)
        self.assertEqual(value(), 0)
        # Invalid ingress must not consume a valid request at that sequence.
        self.assertEqual(request(5, '0', target=2), [])
        self.assertEqual(request(5, '0', target=0), [])
        self.assertEqual(request(5, '0', target=17), [])
        self.assertEqual(request(5, '2'), [])
        deliver(request(5, '.75'))
        self.assertEqual(value(), .75)
        # Receiver also rejects an invalid high-order envelope without poisoning its fence.
        deliver([(P1, f'PFREQ|1|{authority_lease}|2|999|2|-.5'.encode())])
        client_lease = 'e' * 32
        session.peers['p2'].lease = client_lease
        after_client_reconnect = request(1, '-.5')
        deliver(after_client_reconnect)
        self.assertEqual(value(), -.5)
        g.authority.receive(authority_lease, g.split(f'PFROSTER|1|{authority_lease}|2|426f62|faction_player|9'), 0)
        deliver(newer)  # identity/reconnect never resets the receiver fence
        self.assertEqual(value(), -.5)
        # New authority lease restarts relay envelope ordering; same core may survive reconnect.
        old_authority_lease = authority_lease
        authority_lease = 'f' * 32
        session.peers['p1'].lease = authority_lease
        g.attachAuthority(authority_lease)
        new_epoch = request(2, '0')
        self.assertEqual(new_epoch[0][1].decode().split('|')[4], '1')
        deliver(new_epoch)
        self.assertEqual(value(), 0)
        deliver(after_client_reconnect)  # outer client rejects previous authority lease
        self.assertEqual(value(), 0)
        deliver([(P1, f'PFREQ|1|{old_authority_lease}|2|1000|1|-.5'.encode())])
        self.assertEqual(value(), 0)
        # Different target pairs are independent, but opposite endpoints of ONE pair
        # share a fence. A newer unrelated intent must not discard an older valid pair.
        from types import SimpleNamespace
        address3, lease3 = ('192.0.2.3', 3000), 'd' * 32
        session.peers['p3'] = SimpleNamespace(address=address3, lease=lease3)
        g.authority.receive(authority_lease, g.split(f'PFROSTER|1|{authority_lease}|3|43|faction_player|1'), 0)
        different_pair_newer = request(4, '-.5', target=3)
        different_pair_older = request(3, '.75', target=1)
        self.assertTrue(different_pair_older)  # reordered FIRST hop, distinct target
        deliver(different_pair_older); deliver(different_pair_newer)  # reversed SECOND hop
        self.assertEqual(value(), .75)
        self.assertEqual(g.authority.applied['2-3'].value, -.5)
        from2 = request(5, '.75', target=3)
        from3 = relay.handle(f'PFREQ|1|{lease3}|1|2|0'.encode(), address3, session.peers, 0)
        deliver(from3); deliver(from2)
        self.assertEqual(g.authority.applied['2-3'].value, 0)


@unittest.skipUnless(LuaRuntime, 'lupa.luajit21 not installed')
class LuaGlue(unittest.TestCase):
    """player_factions.lua against stubbed engine events (no X4)."""
    def load(self, cfg, bubble=True):
        import ctypes, lupa.luajit21
        ctypes.CDLL(lupa.luajit21.__file__, mode=ctypes.RTLD_GLOBAL)
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
            logs,events,ui,sent={},{},{},{}
            function DebugError(t) logs[#logs+1]=t end
            function RegisterEvent(n,f) events[n]=f end
            function AddUITriggeredEvent(screen,action,args) ui[#ui+1]={screen=screen,action=action,args=args} end
            pcall(require('ffi').cdef,'typedef uint64_t UniverseID;')
        ''')
        lua.execute((EXT / 'ui/player_factions_core.lua').read_text(encoding='utf-8'))
        g = lua.globals()
        g.X4LAN_CONFIG = lua.table_from(cfg)
        if bubble:
            lua.execute('''X4LAN_BUBBLE_API={paused=function() return false end,
              state={active=true,ready=true,client={lease='L2'},transport={send=function(_,p) sent[#sent+1]=p end}}}''')
        lua.execute((EXT / 'ui/player_factions.lua').read_text(encoding='utf-8'))
        return lua, g

    def logs(self, g):
        return list(g.logs.values())

    def test_flag_off_is_inert(self):
        _, g = self.load(dict(enabled=True, coop=True, slot='p2', player_factions=False))
        self.assertEqual(self.logs(g), [])
        self.assertIsNone(g.X4LAN_PLAYERS)

    def test_missing_prerequisite_logs_feature_disabled(self):
        _, g = self.load(dict(enabled=True, coop=True, slot='p2', player_factions=True), bubble=False)
        self.assertIn('[X4LAN] FEATURE player_factions DISABLED reason=bubble_adapter_inactive', self.logs(g))

    def test_guest_setup_identity_and_relay_timeout(self):
        lua, g = self.load(dict(enabled=True, coop=True, slot='p2', player_factions=True))
        pf = g.X4LAN_BUBBLE_API.players
        pf.frame(0)
        self.assertEqual((g.ui[1].screen, g.ui[1].action), ('X4LAN_PlayerFactions', 'Setup'))
        g.events['X4LAN_PlayerFactions.Ready'](None, '16')
        pf.frame(1)
        pf.frame(2)
        self.assertTrue(any('PLAYER_FACTIONS READY slot=p2' in l for l in self.logs(g)))
        self.assertRegex(g.sent[1], r'^PFID\|1\|L2\|1\|' + 'LAN Player 2'.encode().hex() + r'\|faction_player$')
        ok, how = g.X4LAN_PLAYERS.setRelation('p1', 'hostile')
        self.assertEqual((ok, how), (True, 'requested'))
        pf.frame(40)
        self.assertIn('[X4LAN] FEATURE player_factions DISABLED reason=relay_unsupported', self.logs(g))

    def test_coop_pilot_changes_reach_md_and_stop_restores_player(self):
        lua, g = self.load(dict(enabled=True, coop=True, slot='p2', player_factions=True))
        lua.execute("X4LAN_BUBBLE_API.coop={pilots={'0','0'}}")
        pf = g.X4LAN_BUBBLE_API.players
        pf.frame(0); g.events['X4LAN_PlayerFactions.Ready'](None, '16'); pf.frame(1); pf.frame(2)

        def coop():
            return [(e.args.ship, e.args.faction) for e in g.ui.values() if e.action == 'CoopOwner']
        self.assertEqual(coop(), [(1, 'player'), (2, 'player')])
        lua.execute("X4LAN_BUBBLE_API.coop.pilots={'p1','p2'}")
        pf.frame(3); pf.frame(4)
        self.assertEqual(coop()[2:], [(1, 'x4lan_player1')])  # own ship 2 stays `player`
        # Any failure stops the feature loudly and hands both shared ships back to `player`.
        lua.execute("X4LAN_BUBBLE_API.players.core.receive=function() error('boom') end")
        pf.receive(lua.table('PFROSTER'), 5)
        self.assertIn('[X4LAN] FEATURE player_factions DISABLED reason=error', self.logs(g))
        self.assertEqual(coop()[3:], [(1, 'player'), (2, 'player')])

    def test_dedicated_session_switches_every_shared_ship(self):
        # Dedicated 8-player sessions share max_players-1 ships, not two (user report: every other
        # human looked like the own faction because ships 3..7 never left `player`).
        lua, g = self.load(dict(enabled=True, coop=True, slot='p2', player_factions=True))
        lua.execute("X4LAN_BUBBLE_API.coop={count=7,pilots={'0','0','0','0','0','0','0'}}")
        pf = g.X4LAN_BUBBLE_API.players
        pf.frame(0); g.events['X4LAN_PlayerFactions.Ready'](None, '16'); pf.frame(1); pf.frame(2)

        def coop():
            return [(e.args.ship, e.args.faction) for e in g.ui.values() if e.action == 'CoopOwner']
        self.assertEqual(coop(), [(i, 'player') for i in range(1, 8)])
        lua.execute("X4LAN_BUBBLE_API.coop.pilots={'0','p2','p3','0','p5','0','0'}")
        pf.frame(3)
        self.assertEqual(coop()[7:], [(3, 'x4lan_player3'), (5, 'x4lan_player5')])
        root = E.parse(MD).getroot()
        tick = E.tostring(next(c for c in root.iter('cue') if c.get('name') == 'CoopOwnerTick'), encoding='unicode')
        self.assertNotIn('count == 2', tick)
        self.assertIn('exact="$arena.$ships.count"', tick)
        # Lua numbers reach MD as floats: a datatype.integer check refused every request (8p build).
        coop_cue = E.tostring(next(c for c in root.iter('cue') if c.get('name') == 'CoopOwner'), encoding='unicode')
        self.assertNotIn('datatype.integer', coop_cue)

    def test_small_pool_disables(self):
        _, g = self.load(dict(enabled=True, coop=True, slot='p2', player_factions=True))
        pf = g.X4LAN_BUBBLE_API.players
        pf.frame(0); g.events['X4LAN_PlayerFactions.Ready'](None, '3'); pf.frame(1)
        self.assertIn('[X4LAN] FEATURE player_factions DISABLED reason=pool_missing count=3', self.logs(g))


class CoopOwnerMd(unittest.TestCase):
    def test_md_cues_and_docking_guards(self):
        root = E.parse(MD).getroot()
        names = {c.get('name') for c in root.iter('cue')}
        self.assertTrue({'RelationGuard', 'RelationMirror', 'CoopOwner', 'CoopOwnerTick'} <= names)
        tick = E.tostring(next(c for c in root.iter('cue') if c.get('name') == 'CoopOwnerTick'), encoding='unicode')
        for keep in ('player.occupiedship', '$ship.dock', '$ship.assigneddock', '$arena.$docking', '$arena.$busy'):
            self.assertIn(keep, tick)
        mirror = E.tostring(next(c for c in root.iter('cue') if c.get('name') == 'RelationMirror'), encoding='unicode')
        self.assertIn('isrelationlocked', mirror)
        # The owner guard must precede the request on the same execution path.
        for name in ('x4lan_bubble.xml', 'x4lan_coop.xml', 'x4lan_trade.xml', 'x4lan_coop_attachment.xml'):
            document = E.parse(EXT / 'md' / name).getroot()
            parents = {child: parent for parent in document.iter() for child in parent}
            requests = [
                element for element in document.iter()
                if element.tag in ('request_docking', 'request_docking_at_dock')
                and element.get('ship') == '$ship'
            ]
            self.assertTrue(requests, name)
            for request in requests:
                current = request
                guarded = False
                while current in parents:
                    siblings = list(parents[current])
                    index = siblings.index(current)
                    previous = siblings[index - 1] if index else None
                    if (
                        previous is not None
                        and previous.tag == 'do_if'
                        and previous.get('value') == '$ship.owner != faction.player'
                    ):
                        children = list(previous)
                        owner = children[0] if children else None
                        guarded = (
                            owner is not None
                            and owner.tag == 'set_owner'
                            and owner.get('object') == '$ship'
                            and owner.get('faction') == 'faction.player'
                        )
                        break
                    current = parents[current]
                self.assertTrue(guarded, name)


class FlagDefaults(unittest.TestCase):
    def test_inert_source_and_internal_packages(self):
        cfg = (EXT / 'ui/config.lua').read_text(encoding='utf-8')
        self.assertRegex(cfg, r'\n\s*player_factions = false,')
        launcher = (ROOT / 'tools/distribution/session_launcher.py').read_text(encoding='utf-8')
        self.assertIn('player_factions=True', launcher)
        glue = (EXT / 'ui/player_factions.lua').read_text(encoding='utf-8')
        self.assertIn("cfg.player_factions~=true", glue)
        self.assertIn('FEATURE player_factions DISABLED reason=', glue)
        ui = (EXT / 'ui.xml').read_text(encoding='utf-8')
        self.assertLess(ui.index('ui/bubble_game.lua'), ui.index('ui/player_factions_core.lua'))
        self.assertLess(ui.index('ui/player_factions_core.lua'), ui.index('ui/player_factions.lua'))


if __name__ == '__main__':
    unittest.main()
