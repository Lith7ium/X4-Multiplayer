"""Desync indicator for a lockstep sandbox session (observe-mode relay); read only.

Follows either the relay's console log (the lane runner keeps it as <collect>/server.log)
or the relay's --status-file JSON, and alerts the people at the PC when the per-tick
declared-state digests stop matching:
  * a banner on this console and the console title "X4 lockstep: DESYNC tick=N";
  * optional --beep (Windows MessageBeep) and --toast (Windows toast notification through
    Windows PowerShell; a toast does not take the keyboard focus from an X4 window);
  * optional --status-out FILE: one short line for the coordinator ("OK tick=N" /
    "DESYNC tick=N first=N mismatches=N slots=..." / "END ...").
It never opens a socket, never touches the games, the relay or any save. No wire change:
it only reads the relay's own NATIVE_GATE DESYNC / RESYNC / status lines.

  python tools/dev/native_gate_desync_watch.py --log <collect>/server.log --beep --toast
  python tools/dev/native_gate_desync_watch.py --status <collect>/desync-status.json
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import time

DESYNC = re.compile(r'NATIVE_GATE DESYNC tick=(\d+) slots=([\d,]+) first=(\d+) mismatches=(\d+)')
RESYNC = re.compile(r'NATIVE_GATE RESYNC tick=(\d+) mismatches=(\d+)')
STATUS = re.compile(r'NATIVE_GATE tick=(-?\d+) failure=(\S+) done=(True|False)')
# PowerShell's own AppUserModelID: lets a script show a toast without registering an app.
TOAST_APP = '{1AC14E77-02E7-4E5D-B744-2EB1AE5198B7}\\WindowsPowerShell\\v1.0\\powershell.exe'


class Watch:
    """State machine over relay lines or status snapshots; returns events to alert on."""
    def __init__(self):
        self.tick, self.desync, self.diverged, self.first, self.count, self.slots = -1, False, False, None, 0, ''
        self.ended, self.failure = False, None

    def line(self, text):
        events = []
        m = DESYNC.search(text)
        if m:
            tick, slots, first, count = int(m[1]), m[2], int(m[3]), int(m[4])
            self.desync, self.diverged, self.first, self.count, self.slots = True, True, first, count, slots
            self.tick = max(self.tick, tick)
            events.append(('desync', tick, slots, first, count))
        m = RESYNC.search(text)
        if m:
            self.diverged, self.count = False, int(m[2])
            events.append(('resync', int(m[1]), int(m[2])))
        m = STATUS.search(text)
        if m:
            self.tick = max(self.tick, int(m[1]))
            if (m[2] != 'none' or m[3] == 'True') and not self.ended:
                self.ended, self.failure = True, None if m[2] == 'none' else m[2]
                events.append(('end', int(m[1]), self.failure))
        return events

    def snapshot(self, status):
        """One --status-file JSON object (see native_gate_relay.Barrier.status)."""
        events = []
        tick = status.get('tick', -1)
        self.tick = max(self.tick, tick if isinstance(tick, int) else -1)
        count = status.get('mismatches', 0)
        if isinstance(count, int) and count > self.count:
            last = status.get('last_mismatch')
            slots = ','.join(str(s) for s in status.get('slots', []) if isinstance(s, int))
            if not self.diverged:
                events.append(('desync', last if isinstance(last, int) else self.tick, slots,
                               status.get('first_mismatch'), count))
            self.desync, self.diverged, self.first, self.count, self.slots = True, True, status.get('first_mismatch'), count, slots
        if self.diverged and status.get('diverged_now') is False:
            self.diverged = False
            events.append(('resync', self.tick, self.count))
        if (status.get('failure') or status.get('done')) and not self.ended:
            self.ended, self.failure = True, status.get('failure') or None
            events.append(('end', self.tick, self.failure))
        return events

    def summary(self):
        if self.ended:
            verdict = f'failure={self.failure}' if self.failure else ('diverged' if self.desync else 'pass')
            return f'END tick={self.tick} verdict={verdict} mismatches={self.count}'
        if self.desync:
            return f'DESYNC tick={self.tick} first={self.first} mismatches={self.count} slots={self.slots}' + \
                ('' if self.diverged else ' resynced')
        return f'OK tick={self.tick}'


def describe(event):
    kind = event[0]
    if kind == 'desync':
        _, tick, slots, first, count = event
        return f'DESYNC at tick {tick}: client(s) {slots} differ from client 1 (first {first}, {count} differing ticks)'
    if kind == 'resync':
        return f'RESYNC at tick {event[1]}: digests agree again ({event[2]} differing ticks so far)'
    return f'SESSION END at tick {event[1]}' + (f': relay failure {event[2]}' if event[2] else '')


def toast_command(title, text):
    """Windows PowerShell command for one toast. Callers pass only fixed text and integers."""
    if any(c in title+text for c in '\'"`$;\r\n'):
        raise ValueError('toast text must be plain')
    script = ('[Windows.UI.Notifications.ToastNotificationManager,Windows.UI.Notifications,ContentType=WindowsRuntime]>$null;'
              '$t=[Windows.UI.Notifications.ToastNotificationManager]::GetTemplateContent('
              '[Windows.UI.Notifications.ToastTemplateType]::ToastText02);'
              '$x=$t.GetElementsByTagName(\'text\');'
              f'$x.Item(0).AppendChild($t.CreateTextNode(\'{title}\'))>$null;'
              f'$x.Item(1).AppendChild($t.CreateTextNode(\'{text}\'))>$null;'
              '[Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier('
              f'\'{TOAST_APP}\').Show([Windows.UI.Notifications.ToastNotification]::new($t))')
    return ['powershell.exe', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-Command', script]


class Alerts:
    def __init__(self, beep=False, toast=False, out=sys.stdout, runner=None, beeper=None, title=None):
        self.beep, self.toast, self.out = beep, toast, out
        self.runner = runner or (lambda command: subprocess.Popen(
            command, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0)))
        self.beeper, self.title = beeper, title

    def __call__(self, event):
        text = describe(event)
        if event[0] == 'desync':
            self.out.write('\n' + '*'*72 + f'\n*** X4 LOCKSTEP {text}\n' + '*'*72 + '\n')
        else:
            self.out.write(f'--- {text}\n')
        self.out.flush()
        if self.title:
            self.title('X4 lockstep: ' + (f'DESYNC tick={event[1]}' if event[0] == 'desync' else text.split(':')[0]))
        if self.beep and event[0] == 'desync' and self.beeper:
            self.beeper()
        if self.toast and event[0] in ('desync', 'end'):
            headline = 'X4 lockstep DESYNC' if event[0] == 'desync' else 'X4 lockstep session ended'
            try:
                self.runner(toast_command(headline, text.replace(':', ' -').replace('(', '').replace(')', '')))
            except OSError as error:
                self.out.write(f'--- toast unavailable: {error}\n')


def windows_beeper():
    try:
        import winsound
    except ImportError:
        return None
    return lambda: winsound.MessageBeep(0x30)  # MB_ICONEXCLAMATION


def windows_title():
    try:
        import ctypes
        set_title = ctypes.windll.kernel32.SetConsoleTitleW
    except (ImportError, AttributeError, OSError):
        return None
    return lambda text: set_title(text)


class LogFollower:
    """Complete new lines of a growing log; tolerates a file that does not exist yet."""
    def __init__(self, path):
        self.path, self.offset, self.partial = Path(path), 0, b''

    def lines(self):
        try:
            with self.path.open('rb') as f:
                f.seek(self.offset)
                data = f.read()
        except FileNotFoundError:
            return []
        self.offset += len(data)
        data = self.partial + data
        *complete, self.partial = data.split(b'\n')
        return [line.decode('utf-8', 'replace').rstrip('\r') for line in complete]


def read_status(path):
    try:
        return json.loads(Path(path).read_text())
    except (FileNotFoundError, ValueError):
        return None


def run(args, alerts, sleep=time.sleep, clock=time.monotonic):
    watch, follower = Watch(), LogFollower(args.log) if args.log else None
    deadline = clock()+args.timeout if args.timeout else None
    last_summary = None
    while True:
        events = []
        if follower:
            for line in follower.lines():
                events += watch.line(line)
        else:
            status = read_status(args.status)
            if status:
                events += watch.snapshot(status)
        for event in events:
            alerts(event)
        summary = watch.summary()
        if args.status_out and summary != last_summary:
            temporary = Path(args.status_out).with_suffix('.tmp')
            temporary.write_text(summary+'\n')
            temporary.replace(args.status_out)
            last_summary = summary
        if watch.ended or args.once or (deadline is not None and clock() >= deadline):
            return watch
        sleep(args.interval)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    source = p.add_mutually_exclusive_group(required=True)
    source.add_argument('--log', help='relay console log (lane runner: <collect>/server.log)')
    source.add_argument('--status', help='relay --status-file JSON')
    p.add_argument('--status-out', help='write one summary line for the coordinator')
    p.add_argument('--beep', action='store_true', help='Windows MessageBeep on DESYNC')
    p.add_argument('--toast', action='store_true', help='Windows toast notification on DESYNC and at the end')
    p.add_argument('--interval', type=float, default=0.5)
    p.add_argument('--timeout', type=float, default=0, help='stop after this many seconds (0: until the session ends)')
    p.add_argument('--once', action='store_true', help='read once and exit (prints the summary)')
    args = p.parse_args(argv)
    if args.interval <= 0:
        p.error('--interval must be positive')
    alerts = Alerts(beep=args.beep, toast=args.toast, beeper=windows_beeper(), title=windows_title())
    watch = run(args, alerts)
    print(watch.summary(), flush=True)
    return 1 if watch.desync else 0


if __name__ == '__main__':
    sys.exit(main())
