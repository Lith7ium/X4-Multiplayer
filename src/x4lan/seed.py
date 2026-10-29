"""Immutable native-save transfer for the internal shared-world bootstrap.

This carries exact save bytes over a reliable stream. It does not load a game,
restore peer roles, or establish ongoing universe authority. The caller supplies
the expected session/build/content identity. No network-supplied path is used.
"""

from __future__ import annotations

import hashlib
import hmac
import json
import os
from pathlib import Path
import re
import secrets
import tempfile

MAX_SAVE = 512 * 1024 * 1024
MAX_HEADER = 16384
CHUNK = 1024 * 1024
HEX32 = re.compile(r"[a-f0-9]{32}\Z")
HEX64 = re.compile(r"[a-f0-9]{64}\Z")
ATOM = re.compile(r"[A-Za-z0-9_.-]{1,128}\Z")
# Match the launcher's reserved profiles, including dedicated clients p3-p8.
TEST_PROFILES = frozenset(str(number) for number in range(45841, 45849))


class SeedError(ValueError):
    pass


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode(
        "ascii"
    )


def _hex(value, pattern):
    return isinstance(value, str) and pattern.fullmatch(value) is not None


def _key(token):
    if not _hex(token, HEX32):
        raise SeedError("invalid session key")
    return bytes.fromhex(token)


def _mac(token, purpose, nonce, payload):
    return hmac.new(
        _key(token), purpose + b"\0" + nonce.encode("ascii") + b"\0" + payload, hashlib.sha256
    ).hexdigest()


def validate_manifest(value):
    keys = {
        "protocol",
        "seed_id",
        "sha256",
        "size",
        "game_build",
        "mod_sha256",
        "extensions",
        "format",
    }
    if (
        not isinstance(value, dict)
        or set(value) != keys
        or type(value["protocol"]) is not int
        or value["protocol"] != 1
    ):
        raise SeedError("unsupported seed manifest")
    if (
        not _hex(value["seed_id"], HEX32)
        or not _hex(value["sha256"], HEX64)
        or not _hex(value["mod_sha256"], HEX64)
    ):
        raise SeedError("invalid seed/content identity")
    if type(value["size"]) is not int or not 0 < value["size"] <= MAX_SAVE:
        raise SeedError("seed size exceeds limit")
    if not isinstance(value["game_build"], str) or not ATOM.fullmatch(value["game_build"]):
        raise SeedError("invalid game build")
    if value["format"] not in ("x4-xml", "x4-xml-gzip"):
        raise SeedError("unsupported native save format")
    extensions = value["extensions"]
    if not isinstance(extensions, list) or len(extensions) > 256:
        raise SeedError("extension manifest exceeds limit")
    names = []
    for row in extensions:
        if (
            not isinstance(row, dict)
            or set(row) != {"id", "version"}
            or not all(isinstance(row[k], str) and ATOM.fullmatch(row[k]) for k in row)
        ):
            raise SeedError("invalid extension manifest")
        names.append(row["id"])
    if names != sorted(set(names)):
        raise SeedError("extensions must be unique and sorted")
    if len(canonical(value)) > MAX_HEADER:
        raise SeedError("seed manifest exceeds header limit")
    return value


def _copy(stream, target, size):
    digest = hashlib.sha256()
    remaining = size
    while remaining:
        block = stream.read(min(remaining, CHUNK))
        if not block:
            raise SeedError("incomplete save")
        target.write(block)
        digest.update(block)
        remaining -= len(block)
    return digest.hexdigest()


def _new_directory(destination):
    path = Path(destination)
    # A caller-owned destination, never a manifest field; mkdir refuses reuse.
    path.mkdir()
    return path


def create_bundle(snapshot, destination, *, seed_id, game_build, mod_sha256, extensions):
    """Seal a completed native snapshot in a new private directory."""
    source = Path(snapshot)
    if source.is_symlink() or not source.is_file():
        raise SeedError("snapshot must be a regular file")
    before = source.stat()
    manifest = validate_manifest(
        dict(
            protocol=1,
            seed_id=seed_id,
            sha256="0" * 64,
            size=before.st_size,
            game_build=game_build,
            mod_sha256=mod_sha256,
            extensions=extensions,
            format="x4-xml-gzip" if source.suffix == ".gz" else "x4-xml",
        )
    )
    folder = _new_directory(destination)
    part = folder / "seed.part"
    try:
        with source.open("rb") as incoming, part.open("xb") as outgoing:
            manifest["sha256"] = _copy(incoming, outgoing, manifest["size"])
            if incoming.read(1):
                raise SeedError("snapshot grew during sealing")
            outgoing.flush()
            os.fsync(outgoing.fileno())
        after = source.stat()
        if (before.st_size, before.st_mtime_ns, before.st_ino) != (
            after.st_size,
            after.st_mtime_ns,
            after.st_ino,
        ):
            raise SeedError("snapshot changed during sealing")
        part.rename(folder / "seed.bin")
        (folder / "manifest.json").write_bytes(canonical(manifest) + b"\n")
        return manifest
    except Exception:
        part.unlink(missing_ok=True)
        # An incomplete directory has no manifest and is never usable.
        raise


def read_bundle(folder):
    folder = Path(folder)
    path = folder / "manifest.json"
    if path.is_symlink() or path.stat().st_size > MAX_HEADER:
        raise SeedError("invalid manifest file")
    manifest = validate_manifest(json.loads(path.read_bytes()))
    snapshot = folder / "seed.bin"
    if (
        snapshot.is_symlink()
        or not snapshot.is_file()
        or snapshot.stat().st_size != manifest["size"]
    ):
        raise SeedError("invalid sealed save")
    with snapshot.open("rb") as stream:
        digest = (
            hashlib.file_digest(stream, "sha256").hexdigest()
            if hasattr(hashlib, "file_digest")
            else _digest(stream)
        )
    if not hmac.compare_digest(digest, manifest["sha256"]):
        raise SeedError("sealed save checksum mismatch")
    return manifest, snapshot


def _digest(stream):
    digest = hashlib.sha256()
    for block in iter(lambda: stream.read(CHUNK), b""):
        digest.update(block)
    return digest.hexdigest()


def _send_header(connection, value):
    data = canonical(value)
    if len(data) > MAX_HEADER:
        raise SeedError("header exceeds limit")
    connection.sendall(len(data).to_bytes(4, "big") + data)


def _receive_exact(connection, size):
    data = bytearray()
    while len(data) < size:
        block = connection.recv(size - len(data))
        if not block:
            raise SeedError("incomplete transfer")
        data.extend(block)
    return bytes(data)


def _receive_header(connection):
    size = int.from_bytes(_receive_exact(connection, 4), "big")
    if not 0 < size <= MAX_HEADER:
        raise SeedError("header exceeds limit")
    try:
        return json.loads(_receive_exact(connection, size))
    except (UnicodeError, ValueError) as error:
        raise SeedError("invalid transfer header") from error


def send_bundle(connection, folder, token):
    """Serve one authenticated request. Caller owns listener/address/timeouts."""
    _key(token)
    manifest, snapshot = read_bundle(folder)
    nonce = secrets.token_hex(16)
    _send_header(connection, {"protocol": 1, "nonce": nonce})
    request = _receive_header(connection)
    if (
        not isinstance(request, dict)
        or set(request) != {"seed_id", "mac"}
        or request["seed_id"] != manifest["seed_id"]
        or not _hex(request["mac"], HEX64)
        or not hmac.compare_digest(
            request["mac"], _mac(token, b"request", nonce, manifest["seed_id"].encode("ascii"))
        )
    ):
        raise SeedError("seed request authentication failed")
    _send_header(
        connection,
        {"manifest": manifest, "mac": _mac(token, b"manifest", nonce, canonical(manifest))},
    )
    with snapshot.open("rb") as stream:
        for block in iter(lambda: stream.read(CHUNK), b""):
            connection.sendall(block)


def receive_bundle(connection, destination, token, *, seed_id, game_build, mod_sha256, extensions):
    """Receive into a NEW staging directory; never writes an X4 profile."""
    _key(token)
    if not _hex(seed_id, HEX32):
        raise SeedError("invalid expected seed ID")
    hello = _receive_header(connection)
    if (
        not isinstance(hello, dict)
        or set(hello) != {"protocol", "nonce"}
        or type(hello["protocol"]) is not int
        or hello["protocol"] != 1
        or not _hex(hello["nonce"], HEX32)
    ):
        raise SeedError("invalid seed challenge")
    nonce = hello["nonce"]
    _send_header(
        connection,
        {"seed_id": seed_id, "mac": _mac(token, b"request", nonce, seed_id.encode("ascii"))},
    )
    response = _receive_header(connection)
    if (
        not isinstance(response, dict)
        or set(response) != {"manifest", "mac"}
        or not _hex(response["mac"], HEX64)
    ):
        raise SeedError("invalid signed manifest")
    manifest = validate_manifest(response["manifest"])
    if not hmac.compare_digest(
        response["mac"], _mac(token, b"manifest", nonce, canonical(manifest))
    ):
        raise SeedError("seed manifest authentication failed")
    expected = dict(
        seed_id=seed_id, game_build=game_build, mod_sha256=mod_sha256, extensions=extensions
    )
    if any(manifest[k] != expected[k] for k in ("seed_id", "game_build", "mod_sha256")):
        raise SeedError("session, game build or multiplayer scripts differ")
    if manifest["extensions"] != extensions:
        describe = lambda rows: (
            ", ".join(row["id"] + "@" + row["version"] for row in rows) or "(none)"
        )
        raise SeedError(
            "Enabled DLC/mods differ. Host: "
            + describe(manifest["extensions"])
            + ". This computer: "
            + describe(extensions)
            + ". No content settings were changed"
        )
    folder = _new_directory(destination)
    part = folder / "seed.part"
    try:
        digest = hashlib.sha256()
        remaining = manifest["size"]
        with part.open("xb") as stream:
            while remaining:
                block = connection.recv(min(remaining, CHUNK))
                if not block:
                    raise SeedError("incomplete transfer")
                stream.write(block)
                digest.update(block)
                remaining -= len(block)
            stream.flush()
            os.fsync(stream.fileno())
        if not hmac.compare_digest(digest.hexdigest(), manifest["sha256"]):
            raise SeedError("transferred save checksum mismatch")
        part.rename(folder / "seed.bin")
        (folder / "manifest.json").write_bytes(canonical(manifest) + b"\n")
        return manifest
    except Exception:
        part.unlink(missing_ok=True)
        raise


def stage_test_save(folder, profile):
    """Copy a verified seed into the reserved slot of an empty isolated profile.

    This does not launch X4. The caller must ensure the game is closed. A private
    temporary file is atomically linked into a previously absent destination;
    existing saves and the immutable bundle cannot be overwritten.
    """
    profile = Path(profile)
    if profile.name not in TEST_PROFILES or profile.is_symlink() or not profile.is_dir():
        raise SeedError("requires existing isolated test profile 45841..45848")
    saves = profile / "save"
    if saves.is_symlink():
        raise SeedError("linked save directory")
    saves.mkdir(exist_ok=True)
    if any(p.name != "steam_autocloud.vdf" for p in saves.iterdir()):
        raise SeedError("isolated save directory must be empty")
    manifest, source = read_bundle(folder)
    suffix = ".xml.gz" if manifest["format"] == "x4-xml-gzip" else ".xml"
    destination = saves / ("save_010" + suffix)
    fd, temporary = tempfile.mkstemp(prefix=".x4lan-seed-", dir=saves)
    try:
        with os.fdopen(fd, "wb") as outgoing, source.open("rb") as incoming:
            digest = _copy(incoming, outgoing, manifest["size"])
            outgoing.flush()
            os.fsync(outgoing.fileno())
        if not hmac.compare_digest(digest, manifest["sha256"]):
            raise SeedError("seed changed during staging")
        os.link(temporary, destination)
    finally:
        Path(temporary).unlink(missing_ok=True)
    return destination
