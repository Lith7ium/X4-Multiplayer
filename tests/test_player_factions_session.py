"""Negative controls for native evidence acceptance; fixtures are synthetic."""
import json
from pathlib import Path
import tempfile
import unittest
from dev.validate_player_factions_session import analyze, analyze_client


def fixture(slot):
    other = 'p2' if slot == 'p1' else 'p1'
    faction = 'x4lan_player' + other[1:]
    ships = ('1:player/Alice/pilot=p1;2:x4lan_player2/Bob/pilot=p2' if slot == 'p1'
             else '1:x4lan_player1/Alice/pilot=p1;2:player/Bob/pilot=p2')
    name = 'Bob' if slot == 'p1' else 'Alice'
    rows = [f'PLAYER_FACTIONS READY slot={slot} local=player',
            f'PLAYER_FACTIONS IDENTITY_APPLIED slot={other} faction={faction} name="{name}"',
            'relay=connected remote=receiving']
    for value in (.75, 0, -.5, .75):
        rows.append(f'PLAYER_FACTIONS RELATION_APPLIED 1|2|{value}')
        fa, fb = ('player', faction) if slot == 'p1' else (faction, 'player')
        rows.append(f'PLAYER_FACTIONS RELATION_APPLIED {fa}->{fb}={value} {fb}->{fa}={value}')
    for value, how in ((('hostile', 'applied'), ('allied', 'applied')) if slot == 'p1' else (('neutral', 'requested'),)):
        rows.append(f'PFSESSION slot={slot} ACTION setRelation({other},{value}) -> true {how}')
    rows.append(f'PFSESSION slot={slot} t=160 other={other} faction={faction} name={name} short={name} diplomacy_list=yes:{name} uirelation=28 ships={ships}')
    rows.append(f'PFSESSION DONE slot={slot}')
    return '\n'.join('[X4LAN] ' + row for row in rows)


class SessionAcceptance(unittest.TestCase):
    def test_valid_correlated_clients(self):
        for slot in ('p1', 'p2'):
            self.assertTrue(analyze_client(fixture(slot), slot)['success'])

    def test_independent_regex_hits_do_not_prove_acceptance(self):
        source = fixture('p1')
        mutations = {
            'failed_name': source.replace('name=Bob short=', 'name=false short='),
            'wrong_client': source.replace('PFSESSION slot=p1 t=', 'PFSESSION slot=p2 t='),
            'wrong_faction': source.replace('other=p2 faction=x4lan_player2', 'other=p2 faction=x4lan_player1'),
            'wrong_pilot': source.replace('/pilot=p2', '/pilot=0'),
            'wrong_owner': source.replace('2:x4lan_player2/', '2:player/'),
            'missing_ship': source.replace('1:player/Alice/pilot=p1;', ''),
            'false_action': source.replace('-> true applied', '-> false not_ready'),
            'too_early': source.replace('t=160 ', 't=10 '),
            'no_restore': source.rsplit('PLAYER_FACTIONS RELATION_APPLIED 1|2|0.75', 1)[0] + source.rsplit('PLAYER_FACTIONS RELATION_APPLIED 1|2|0.75', 1)[1],
            'failure_after_done': source + '\n[X4LAN] FEATURE player_factions DISABLED reason=error',
            'wrong_done': source.replace('DONE slot=p1', 'DONE slot=p2'),
            'unresolved_ui': source.replace('uirelation=28', 'uirelation=false'),
            'hostile_final_ui': source.replace('uirelation=28', 'uirelation=-25'),
            'reverse_direction_stale': source.replace('x4lan_player2->player=-0.5', 'x4lan_player2->player=0.75'),
        }
        for name, text in mutations.items():
            with self.subTest(name=name):
                self.assertFalse(analyze_client(text, 'p1')['success'])

    def test_repeated_relation_readbacks_are_allowed(self):
        text = fixture('p2').replace('1|2|0\n', '1|2|0\n[X4LAN] PLAYER_FACTIONS RELATION_APPLIED 1|2|0\n')
        self.assertTrue(analyze_client(text, 'p2')['success'])

    def test_session_requires_two_successful_native_clients_and_cleanup(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            rows = []
            for index in (1, 2):
                path = root / f'client{index}'
                path.mkdir()
                (path / 'x4lan-debug.txt').write_text(fixture(f'p{index}'), encoding='utf-8')
                rows.append(dict(slot=f'p{index}', verdict='pass', x4={'pid': index}, lane_restored=True))
            result = dict(verdict='pass', clients=rows)
            output = root / 'result.json'
            output.write_text(json.dumps(result), encoding='utf-8')
            self.assertTrue(analyze(root)['success'])
            for key, value in [('slot', 'p1'), ('verdict', 'fail'), ('x4', {}), ('lane_restored', False)]:
                old = rows[1][key]
                rows[1][key] = value
                output.write_text(json.dumps(result), encoding='utf-8')
                self.assertFalse(analyze(root)['success'], key)
                rows[1][key] = old


if __name__ == '__main__':
    unittest.main()
