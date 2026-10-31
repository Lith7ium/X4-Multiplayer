"""Client directories of a lane session run: client1..clientN, compared against client1.

A two-client run keeps each census tool's original report. With N > 2 clients
a tool reports client1 against every other client:
{"clients": N, "reference": "client1", "pairs": {"client2": ..., "clientN": ...}}
where each pair entry has the tool's two-client report format.
"""
import re
from pathlib import Path

CLIENT = re.compile(r'client([1-9][0-9]*)')


def client_slots(run, strict=True):
    """Sorted slots of run/clientK directories. strict: exactly client1..clientN with N >= 2."""
    slots = sorted(int(m[1]) for p in Path(run).iterdir() if p.is_dir() and (m := CLIENT.fullmatch(p.name)))
    if strict and (len(slots) < 2 or slots != list(range(1, len(slots)+1))):
        raise ValueError(f'{run}: need client1..clientN directories with N >= 2, found {[f"client{s}" for s in slots]}')
    return slots


def against_reference(slots, pair):
    """N > 2 report: pair(k) compares client1 with clientK for every other slot."""
    return dict(clients=len(slots), reference='client1', pairs={f'client{k}': pair(k) for k in slots[1:]})
