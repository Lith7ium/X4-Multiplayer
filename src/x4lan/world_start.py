"""Explicit numeric generation seed; a startup recipe, never a saved universe.

Used by development tools only. Does not edit a profile, launch X4, or admit a
peer. Equal recipes are necessary, not sufficient, evidence of equal stations.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import secrets
import xml.etree.ElementTree as ET

from . import seed


def validate(recipe):
    keys = {
        'protocol',
        'world_id',
        'generation_seed',
        'gamestart',
        'game_build',
        'mod_sha256',
        'extensions',
    }
    if not isinstance(recipe, dict) or set(recipe) != keys:
        raise seed.SeedError('invalid world-start recipe')
    if type(recipe['protocol']) is not int or recipe['protocol'] != 1:
        raise seed.SeedError('unsupported world-start protocol')
    if (
        type(recipe['generation_seed']) is not int
        or not 1 <= recipe['generation_seed'] <= 4294967295
    ):
        raise seed.SeedError('generation seed must be a nonzero unsigned 32-bit integer')
    if not isinstance(recipe['gamestart'], str) or not seed.ATOM.fullmatch(recipe['gamestart']):
        raise seed.SeedError('invalid gamestart')
    seed.validate_manifest(
        dict(
            protocol=1,
            seed_id=recipe['world_id'],
            sha256='0' * 64,
            size=1,
            format='x4-xml',
            **content(recipe),
        )
    )
    return recipe


def content(recipe):
    return {k: recipe[k] for k in ('game_build', 'mod_sha256', 'extensions')}


def create(
    *,
    game_build,
    mod_sha256,
    extensions,
    generation_seed=None,
    world_id=None,
    gamestart='custom_creative',
):
    return validate(
        dict(
            protocol=1,
            world_id=secrets.token_hex(16) if world_id is None else world_id,
            generation_seed=secrets.randbelow(2147483647) + 1
            if generation_seed is None
            else generation_seed,
            gamestart=gamestart,
            game_build=game_build,
            mod_sha256=mod_sha256,
            extensions=json.loads(json.dumps(extensions)),
        )
    )


def fingerprint(recipe):
    return hashlib.sha256(seed.canonical(validate(recipe))).hexdigest()


def arguments(recipe, profile, *, observed_content):
    """Read-only plan for a NEW isolated world; existing seed overrides reject.

    Content must be independently measured by the caller, not copied from the
    invitation. The native station comparison must follow before any admission.
    """
    validate(recipe)
    if recipe['generation_seed'] > 2147483647:
        raise seed.SeedError(
            'native save seed is valid but outside the verified command-line seed range'
        )
    if observed_content != content(recipe):
        raise seed.SeedError('game/build/extension content differs from host recipe')
    profile = Path(profile)
    if (
        profile.name != '45841'
        or profile.is_symlink()
        or profile.resolve().name != '45841'
        or not (profile / 'config.xml').is_file()
    ):
        raise seed.SeedError('requires the existing isolated test profile')
    config = ET.parse(profile / 'config.xml').getroot()
    if config.tag != 'root' or config.findtext('autosave') != 'false':
        raise seed.SeedError('requires an autosave-disabled test configuration')
    # A config seed can override the command-line switch. Do not silently edit it.
    values = config.findall('seed')
    if values and (len(values) != 1 or values[0].text != str(recipe['generation_seed'])):
        raise seed.SeedError('profile seed conflicts with the host generation seed')
    if any(config.find(k) is not None for k in ('load', 'module', 'mpar', 'usedefaultconfig')):
        raise seed.SeedError('profile contains a load/module override; fresh-world plan refused')
    return [
        '-personalfolderid',
        '45841',
        '-module',
        recipe['gamestart'],
        '-seed:' + str(recipe['generation_seed']),
        '-nocputhrottle',
        '-noworkshopsync',
    ]
