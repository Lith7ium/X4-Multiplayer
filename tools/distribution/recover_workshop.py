"""Guarded rollback of Fix-1 and a local list for manual Steam resubscription.

Never launches X4, calls Steam APIs, or writes Steam/normal-profile/mod files.
The old helper recorded affected IDs, NOT a subscription snapshot.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import gzip
import hashlib
import html
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from isolate_test_mods import ensure_closed


def digest(raw):
    return hashlib.sha256(raw).hexdigest() if raw is not None else None


def linked(path):
    return path.is_symlink() or getattr(path, 'is_junction', lambda: False)()


def read_json(path):
    if linked(path):
        raise RuntimeError('Refusing a linked record: ' + str(path))
    return json.loads(path.read_text(encoding='utf-8-sig'))


def workshop_id(value):
    if isinstance(value, str) and re.fullmatch(r'ws_[1-9][0-9]{0,19}', value):
        number = int(value[3:])
        if number < 2**64:
            return str(number)
    return None


def prepare(root):
    package = read_json(root / 'package.json')
    state = read_json(root / 'installation.json')
    if (
        package.get('channel') != 'internal'
        or package.get('slot') not in ('p1', 'p2')
        or not package.get('package_id')
        or state.get('package_id') != package['package_id']
        or package.get('version') != state.get('version')
    ):
        raise RuntimeError('Use the original configured internal package folder.')
    profile = Path(state['profile'])
    if (
        not profile.is_absolute()
        or profile.name != '45841'
        or linked(profile)
        or profile.resolve().name != '45841'
        or not (profile / 'config.xml').is_file()
    ):
        raise RuntimeError('Unexpected test profile; no recovery changes made.')
    target = profile / 'content.xml'
    if linked(target) or linked(root / 'backups'):
        raise RuntimeError('Refusing a linked selection or backup folder.')
    records = []
    candidates = set()
    for folder in sorted((root / 'backups').glob('test-mod-isolation-*')):
        if linked(folder):
            raise RuntimeError('Refusing a linked backup folder.')
        record = read_json(folder / 'change.json')
        if Path(record['profile']).resolve() != profile.resolve():
            raise RuntimeError('Backup profile mismatch; preserve records for review.')
        if type(record.get('before_exists')) is not bool:
            raise RuntimeError('Invalid backup record.')
        before_file = folder / 'content.xml'
        if linked(before_file):
            raise RuntimeError('Refusing a linked backup file.')
        if record['before_exists']:
            before = before_file.read_bytes()
            if ET.fromstring(before).tag != 'content':
                raise RuntimeError('Unexpected backed-up selection format.')
        else:
            if before_file.exists() or not (folder / 'BEFORE-WAS-ABSENT.txt').is_file():
                raise RuntimeError('Missing or inconsistent absent-file backup marker.')
            before = None
        if digest(before) != record.get('before_sha256'):
            raise RuntimeError('Backup checksum mismatch; no profile changes made.')
        if not re.fullmatch(r'[0-9a-f]{64}', str(record.get('after_sha256', ''))):
            raise RuntimeError('Invalid after checksum.')
        if not isinstance(record.get('disabled_ids'), list):
            raise RuntimeError('Invalid affected-ID list.')
        candidates.update(
            item_id for value in record['disabled_ids'] if (item_id := workshop_id(value))
        )
        records.append((folder, record, before))
    if not records:
        raise RuntimeError(
            'No Fix-1 backups found. Keep the original package and its backups; '
            'do not guess a former subscription list.'
        )
    current = target.read_bytes() if target.exists() else None
    original = records[0][2]
    chain_valid = all(
        digest(records[i][2]) == records[i - 1][1]['after_sha256'] for i in range(1, len(records))
    )
    if current == original:
        status = 'already_original'
    elif chain_valid and digest(current) == records[-1][1]['after_sha256']:
        status = 'ready'
    else:
        status = 'manual_review_required'
    prior = {}
    if original is not None:
        for entry in ET.fromstring(original).findall('extension'):
            number = workshop_id(entry.get('id'))
            if number:
                prior.setdefault(number, []).append(entry.get('enabled', 'unspecified'))
    # Read only matching installed manifests for display names. They do not prove
    # a former subscription. Do not enumerate other profiles or inspect saves.
    names = {}
    warnings = []
    game = Path(state['game'])
    if not game.is_absolute():
        raise RuntimeError('Unexpected game path.')
    for base in (game / 'extensions', profile / 'extensions', profile.parent / 'extensions'):
        if not base.is_dir() or linked(base):
            continue
        for folder in sorted(base.iterdir()):
            if not folder.is_dir() or linked(folder):
                continue
            manifest = folder / 'content.xml'
            if not manifest.is_file():
                manifest = folder / 'content.xml.gz'
            if not manifest.is_file() or linked(manifest):
                continue
            try:
                opener = gzip.open if manifest.suffix == '.gz' else open
                with opener(manifest, 'rb') as stream:
                    raw = stream.read(2_000_001)
                if len(raw) > 2_000_000:
                    raise ValueError('Manifest exceeds size limit')
                entry = ET.fromstring(raw)
                number = workshop_id(entry.get('id'))
                if entry.tag == 'content' and number in candidates:
                    names[number] = entry.get('name') or folder.name
            except (OSError, ValueError, ET.ParseError):
                warnings.append('Could not read a display name from ' + str(manifest))
    return dict(
        target=target,
        current=current,
        original=original,
        status=status,
        records=records,
        ids=sorted(candidates, key=int),
        prior=prior,
        names=names,
        warnings=warnings,
    )


def page(plan, status):
    rows = []
    for number in plan['ids']:
        old = ', '.join(plan['prior'].get(number, ['unknown']))
        name = plan['names'].get(number, 'Workshop item ' + number)
        rows.append(
            '<tr><td><input type="checkbox" aria-label="Checked in Steam"></td>'
            '<td>'
            + html.escape(name)
            + '</td><td>'
            + number
            + '</td><td>'
            + html.escape(old)
            + '</td><td><a target="_blank" rel="noopener noreferrer" '
            'href="https://steamcommunity.com/sharedfiles/filedetails/?id='
            + number
            + '">Open Workshop item</a></td></tr>'
        )
    return (
        '''<!doctype html><html lang="en"><meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'">
<title>X4 Workshop recovery</title><style>body{font:17px system-ui;max-width:1100px;margin:40px auto;padding:20px}
td,th{padding:10px;text-align:left;border-bottom:1px solid #ccc}a{color:#1257a6}</style>
<h1>X4 Workshop recovery</h1><p><b>Steam subscriptions have NOT been changed by this tool.</b></p>
<p>These are Workshop IDs the withdrawn helper listed as affected. This is not a verified
historical subscription list. An installed or formerly enabled mod is not proof of an account
subscription. Review each item before subscribing. Previously disabled items may be intentional.</p>
<ol><li>Keep X4 closed. Do not resume the affected multiplayer test profile.</li>
<li>Open the Workshop links below. Sign in only on the official Steam website, using the
affected Steam account. Click Subscribe for mods you want back. Already subscribed items
need no action. Do not click Unsubscribe.</li>
<li>Check <a href="https://steamcommunity.com/my/myworkshopfiles/?appid=392160&amp;browsesort=mysubscriptions"
target="_blank" rel="noopener noreferrer">your X4 subscriptions in Steam</a> afterwards.</li></ol>
<p>Local test selection: <b>'''
        + html.escape(status)
        + '''</b>. Rollback of a local file does not
restore account subscriptions. If manual review is required, the current file was left untouched.</p>
<p>Checkboxes are only temporary notes, not Steam status; reloading this page resets them.
If an item is unavailable/private, check the logged-in account and retain this report.</p>
<table><tr><th>Checked</th><th>Installed name</th><th>Workshop ID</th><th>Original local enabled value</th>
<th>Steam</th></tr>'''
        + ''.join(rows)
        + '''</table><p>No items listed means no valid ws_ IDs were
found in the helper records, not that there were no subscriptions.</p></html>'''
    )


def recover(root, plan, restore=True):
    target = plan['target']
    # Fail before any mutation if a game/user changed the file after inspection.
    if linked(target) or (target.read_bytes() if target.exists() else None) != plan['current']:
        raise RuntimeError('Selection changed during preparation; rerun with X4 closed.')
    output_base = root / 'workshop-recovery'
    if linked(output_base):
        raise RuntimeError('Refusing a linked output folder.')
    output = output_base / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S-%fZ')
    output.mkdir(parents=True, exist_ok=False)
    if plan['current'] is not None:
        (output / 'content-before-recovery.xml').write_bytes(plan['current'])
    if plan['original'] is not None:
        (output / 'content-original.xml').write_bytes(plan['original'])
    record = dict(
        profile=str(target.parent),
        status='prepared',
        before_sha256=digest(plan['current']),
        original_sha256=digest(plan['original']),
        candidates=plan['ids'],
        subscription_status='not_checked_or_modified',
        warnings=plan['warnings'],
        source_records=[str(folder / 'change.json') for folder, _, _ in plan['records']],
    )
    journal = output / 'recovery.json'
    journal.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    # Always leave usable links even if local rollback fails later.
    report = output / 'WORKSHOP-RECOVERY.html'
    report.write_text(page(plan, 'not restored yet'), encoding='utf-8')
    status = plan['status']
    if restore and status == 'ready':
        ensure_closed()
        if linked(target) or (target.read_bytes() if target.exists() else None) != plan['current']:
            raise RuntimeError('Selection changed; links and evidence retained in ' + str(output))
        if plan['original'] is None:
            target.unlink()
        else:
            temporary = None
            try:
                with tempfile.NamedTemporaryFile(
                    dir=target.parent, prefix='content-recovery-', suffix='.tmp', delete=False
                ) as stream:
                    temporary = Path(stream.name)
                    stream.write(plan['original'])
                os.replace(temporary, target)
            finally:
                if temporary is not None and temporary.exists():
                    temporary.unlink()
        if (target.read_bytes() if target.exists() else None) != plan['original']:
            raise RuntimeError('Rollback readback failed; preserve ' + str(output))
        status = 'original_local_selection_restored'
    elif not restore:
        status = 'report_only_no_profile_changes'
    record['status'] = status
    journal.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    report.write_text(page(plan, status), encoding='utf-8')
    return report, status


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--report-only',
        action='store_true',
        help='Create links without rolling back the test selection',
    )
    parser.add_argument('--no-open', action='store_true', help='Do not open the local HTML report')
    args = parser.parse_args()
    try:
        ensure_closed()
        root = Path(__file__).resolve().parent
        plan = prepare(root)
        report, status = recover(root, plan, restore=not args.report_only)
        print('Local selection:', status)
        print('Workshop candidates:', len(plan['ids']))
        print('Report:', report)
        print(
            'Steam subscriptions were NOT changed. Review the links and click Subscribe in Steam.'
        )
        print('Keep X4 closed and do not resume the affected multiplayer test profile.')
        if not args.no_open:
            try:
                os.startfile(report)
            except OSError:
                print('Open the HTML report above manually.')
        return 0
    except (
        OSError,
        ValueError,
        RuntimeError,
        KeyError,
        TypeError,
        ET.ParseError,
        subprocess.SubprocessError,
    ) as exc:
        print('ERROR:', exc, file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
