"""Bounded station identity/pose admission for matching startup worlds.

This is not a station mover or a continuous economy synchronizer. A display code
alone is not unique; pose is checked AFTER identity matching, never used to pick
an object. Runtime handles may change across processes and saved-game loads.
"""

from __future__ import annotations

import math
import re

from . import seed, world_start

MAX_STATIONS = 20000
POSITION_TOLERANCE_M = 0.05
ANGLE_TOLERANCE_RAD = 0.00001
ATOM = re.compile(r'[A-Za-z0-9_.-]{1,128}\Z')


def validate(catalog):
    """Reject malformed catalogs and ambiguous local identities."""
    if (
        not isinstance(catalog, dict)
        or set(catalog) != {'protocol', 'recipe_sha256', 'stations'}
        or type(catalog['protocol']) is not int
        or catalog['protocol'] != 1
        or not seed._hex(catalog['recipe_sha256'], seed.HEX64)
    ):
        raise seed.SeedError('invalid station catalog header')
    rows = catalog['stations']
    if not isinstance(rows, list) or not 1 <= len(rows) <= MAX_STATIONS:
        raise seed.SeedError('station catalog size outside bounds')
    identities = set()
    handles = set()
    for row in rows:
        if not isinstance(row, dict) or set(row) != {
            'object',
            'code',
            'macro',
            'owner',
            'sector',
            'pose',
        }:
            raise seed.SeedError('invalid station row')
        if (
            type(row['object']) is not int
            or not 1 <= row['object'] < 2**64
            or row['object'] in handles
        ):
            raise seed.SeedError('invalid or aliased local station handle')
        values = [row[field] for field in ('code', 'macro', 'owner', 'sector')]
        if any(
            not isinstance(value, str)
            or not ATOM.fullmatch(value)
            or value in ('nil', 'unknown', 'null')
            for value in values
        ):
            raise seed.SeedError('unresolved station identity')
        identity = tuple(values)
        if identity in identities:
            raise seed.SeedError('ambiguous station identity; pose matching is forbidden')
        pose = row['pose']
        if (
            not isinstance(pose, list)
            or len(pose) != 6
            or any(
                type(value) not in (float, int) or not math.isfinite(value) or abs(value) > 1e12
                for value in pose
            )
        ):
            raise seed.SeedError('invalid station pose')
        identities.add(identity)
        handles.add(row['object'])
    return catalog


def create(recipe, stations):
    """Build and validate a catalog tied to the world recipe."""
    return validate(
        dict(protocol=1, recipe_sha256=world_start.fingerprint(recipe), stations=stations)
    )


def match(host, guest):
    """All-or-nothing local station bindings plus measured layout errors.

    Only call with catalogs actually captured from the two native worlds; equal
    recipes alone cannot grant admission. No guessing or partial binding occurs.
    """
    validate(host)
    validate(guest)
    if host['recipe_sha256'] != guest['recipe_sha256']:
        raise seed.SeedError('different generation/content/world recipes')

    def index(catalog):
        return {
            tuple(row[field] for field in ('code', 'macro', 'owner', 'sector')): row
            for row in catalog['stations']
        }

    source = index(host)
    target = index(guest)
    missing = sorted(source.keys() - target.keys())
    extra = sorted(target.keys() - source.keys())
    mismatches = []
    bindings = {}
    max_position = 0.0
    max_angle = 0.0
    for identity in sorted(source.keys() & target.keys()):
        host_station = source[identity]
        guest_station = target[identity]
        position = math.dist(host_station['pose'][:3], guest_station['pose'][:3])
        angle = max(
            abs((host_angle - guest_angle + math.pi) % (2 * math.pi) - math.pi)
            for host_angle, guest_angle in zip(host_station['pose'][3:], guest_station['pose'][3:])
        )
        max_position = max(max_position, position)
        max_angle = max(max_angle, angle)
        if position > POSITION_TOLERANCE_M or angle > ANGLE_TOLERANCE_RAD:
            mismatches.append(
                dict(identity=list(identity), position_error_m=position, angle_error_rad=angle)
            )
        bindings[str(host_station['object'])] = guest_station['object']
    accepted = not (missing or extra or mismatches)
    return dict(
        station_layout_matches=accepted,
        host_count=len(source),
        guest_count=len(target),
        missing=[list(identity) for identity in missing],
        extra=[list(identity) for identity in extra],
        pose_mismatches=mismatches,
        max_position_error_m=max_position,
        max_angle_error_rad=max_angle,
        local_bindings=bindings if accepted else {},
        identity_key=['code', 'macro', 'owner', 'sector'],
        ongoing_world_authority=False,
        save_resume_acceptance=False,
    )
