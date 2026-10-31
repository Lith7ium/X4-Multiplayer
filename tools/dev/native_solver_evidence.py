"""Independent checks for existing native solver calls, not independent players."""
import re

SIZES = {'c0': 504, 'c1': 504, 'm0': 160, 'm1': 160,
         'source': 96, 'configuration': 8, 'published': 96}
NUMBERS = ('index', 'tick', 'kind', 'actor', 'ordinal', 'role', 'bracket',
           'result', 'valid', 'solve_tick', 'solve_ordinal')
FIELDS = set(NUMBERS) | {'owner', 'caller'} | set(SIZES)


def evidence(log, slot, context, last_tick, errors):
    if not isinstance(context,(tuple,list)) or len(context)!=4 or any(
            not isinstance(value,str) or not re.fullmatch('[0-9a-f]{16}',value) for value in context):
        errors.append(f'player {slot} solver context missing or invalid')
        context=None
    if re.findall(r'NATIVE_GATE PLAYER_SOLVER_ENABLED (.*)', log) != [
            'tick=1 schema=1 early_coverage=0 input_changes=0']:
        errors.append(f'player {slot} solver declaration missing')
    fixtures = dict(re.findall(r'NATIVE_GATE WORLD_ACTOR id=(\d+) saved=([0-9a-f]{16})', log))
    rows = []; solves = {}; ordinals = {}; published = set(); matches = set(); owners = {}
    for raw in re.findall(r'NATIVE_GATE PLAYER_SOLVER_EVENT ([^\r\n]*)', log):
        try:
            parts = raw.split(); row = dict(part.split('=', 1) for part in parts)
            assert len(parts) == len(row) and set(row) == FIELDS
            assert all(re.fullmatch(r'\d+', row[k]) for k in NUMBERS)
            row.update({k: int(row[k]) for k in NUMBERS})
            assert all(re.fullmatch('[0-9a-f]{'+str(size*2)+'}', row[k]) for k,size in SIZES.items())
            assert re.fullmatch('[0-9a-f]{16}', row['owner']) and re.fullmatch('[0-9a-f]{8}', row['caller'])
            actor = row['actor']; owner = row['owner']; tick = row['tick']
            assert row['index'] == len(rows)+1 and 1 <= tick <= last_tick and row['valid'] == 1
            assert 0 <= actor <= 12 and row['role'] in (1,2,3) and row['kind'] in (1,2)
            assert context and owner == (context[3] if actor == 0 else fixtures.get(str(actor))) and owner != '0'*16
            assert owners.get(owner,actor) == actor
            owners[owner] = actor
            if row['kind'] == 1:
                key = (owner,tick); ordinal = row['ordinal']
                assert ordinal == ordinals.get(key,0)+1 and row['result'] in (0,1)
                assert row['caller'] == ('00d93559' if actor == 0 else '00ec92c5')
                assert row['bracket'] in ((0,1) if actor == 0 else (2,))
                assert actor != 0 or row['role'] == 1
                assert row['solve_tick'] == row['solve_ordinal'] == 0 and row['published'] == '0'*192
                ordinals[key] = ordinal; solves[owner,tick,ordinal] = row
            else:
                key = (owner,row['solve_tick'],row['solve_ordinal'])
                assert key in solves and key not in published and row['solve_tick'] <= tick
                assert row['ordinal'] == row['result'] == 0 and row['caller'] == '00000000'
                assert row['bracket'] == (1 if actor == 0 else 2)
                assert all(row[k] == '0'*(size*2) for k,size in SIZES.items() if k != 'published')
                published.add(key)
                # The first64 bytes are pose, next32 velocity, in both records.
                if row['published'] == solves[key]['m1'][128:320]: matches.add(owner)
            rows.append(row)
        except (AssertionError,ValueError,KeyError,IndexError):
            errors.append(f'player {slot} malformed/invalid native solver event {len(rows)+1}')
    stops = re.findall(r'NATIVE_GATE PLAYER_SOLVER_STOP schema=1 count=(\d+) failures=(\d+) excluded=(\d+) open=(\d+) restored=(\d+) active=(\d+) early_coverage=0 emitted=(\d+)',log)
    summary = tuple(map(int,stops[0])) if len(stops)==1 else None
    if not summary or summary[0] != len(rows) or summary[1] or summary[3:6] != (0,1,0) or summary[6] != len(rows):
        errors.append(f'player {slot} solver capture/restore incomplete')
    player = context[3] if context else None
    if player not in matches or not any(owner != player for owner in matches):
        errors.append(f'player {slot} solver/publication proof needs the player and a distinct saved fixture')
    # Allocation order across worker threads is local. Saved owner/tick/ordinal
    # and all captured native values remain compared, including publication.
    canonical = sorted(tuple((k,row[k]) for k in sorted(row) if k != 'index') for row in rows)
    return canonical, {'events':len(rows), 'solves':len(solves), 'publications':len(published),
                       'matching_published_owners':sorted(matches), 'excluded_calls':summary[2] if summary else None,
                       'independent_player_contexts':False, 'early_coverage':False}
