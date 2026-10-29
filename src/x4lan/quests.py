"""Authenticated, monotonic snapshots of host-X4 cooperative objectives.

Only the host game's mission script can advance objectives or award money.
The relay stores a display snapshot; it does not infer kills or grant rewards.
"""

from .economy import integer
from .host import sequence


def parse_progress(fields):
    if len(fields) != 5:
        raise ValueError('quest fields')
    revision, stage, delivered, destroyed, paid = (
        sequence(fields[0]),
        integer(fields[1], 3),
        integer(fields[2], 20),
        integer(fields[3], 2),
        integer(fields[4], 1),
    )
    if stage < 1:
        raise ValueError('quest stage')
    if stage == 1 and (delivered == 20 or paid):
        raise ValueError('delivery state')
    if stage == 2 and (delivered != 20 or destroyed == 2 or paid):
        raise ValueError('combat state')
    if stage == 3 and (delivered, destroyed, paid) != (20, 2, 1):
        raise ValueError('completion state')
    return revision, stage, delivered, destroyed, paid


class QuestState:
    def __init__(self):
        self.owner = None
        self.progress = None
        self.next_send = 0

    def receive(self, lease, fields):
        progress = parse_progress(fields)
        old = self.progress if self.owner == lease else None
        if old and (progress[0] <= old[0] or any(a < b for a, b in zip(progress[1:], old[1:]))):
            return
        self.owner, self.progress = lease, progress

    def snapshots(self, peers, now):
        host = peers.get('p1')
        if not host or self.owner != host.lease or self.progress is None or now < self.next_send:
            return []
        self.next_send = now + 0.5
        payload = '|'.join(map(str, self.progress))
        return [
            (p.address, f'QUEST|1|{p.lease}|{host.lease}|{payload}'.encode())
            for p in peers.values()
        ]
