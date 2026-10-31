"""Reliable save transfer and staging; these are not native load/authority tests."""
import hashlib
import json
from pathlib import Path
import socket
import tempfile
import threading
import unittest

from x4lan import seed

TOKEN = "a" * 32
IDENTITY = dict(seed_id="b" * 32, game_build="world1-9.00-steam23660954",
                mod_sha256="c" * 64,
                extensions=[{"id": "ego_dlc_timelines", "version": "900"}])


class SeedTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.payload = b"<savegame>" + bytes(range(256)) * 9000 + b"</savegame>"
        self.snapshot = self.root / "source.xml"
        self.snapshot.write_bytes(self.payload)
        self.host = self.root / "host"
        self.manifest = seed.create_bundle(self.snapshot, self.host, **IDENTITY)

    def transfer(self, sender=None, token=TOKEN, identity=None):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(3)
        self.addCleanup(listener.close)
        errors = []

        def serve():
            try:
                connection, _ = listener.accept()
                with connection:
                    connection.settimeout(3)
                    (sender or (lambda s: seed.send_bundle(s, self.host, TOKEN)))(connection)
            except Exception as error:
                errors.append(error)

        thread = threading.Thread(target=serve, daemon=True)
        thread.start()
        try:
            with socket.create_connection(listener.getsockname(), timeout=3) as connection:
                result = seed.receive_bundle(connection, self.root / "guest", token, **(identity or IDENTITY))
        finally:
            thread.join(4)
            self.assertFalse(thread.is_alive(), "seed server did not finish")
        return result, errors

    def test_exact_bytes_over_real_tcp_then_independent_atomic_save_copy(self):
        result, errors = self.transfer()
        self.assertFalse(errors)
        self.assertEqual(result, self.manifest)
        guest = self.root / "guest"
        self.assertEqual((guest / "seed.bin").read_bytes(), self.payload)
        profile = self.root / "45841"
        profile.mkdir()
        target = seed.stage_test_save(guest, profile)
        self.assertEqual(target.name, "save_010.xml")
        self.assertEqual(target.read_bytes(), self.payload)
        target.write_bytes(b"later engine write")
        self.assertEqual((guest / "seed.bin").read_bytes(), self.payload,
                         "game save must not share an inode with the sealed bundle")
        with self.assertRaises(seed.SeedError):
            seed.stage_test_save(guest, profile)
        self.assertEqual(target.read_bytes(), b"later engine write")

    def test_wrong_key_and_incompatible_content_do_not_create_guest_bundle(self):
        with self.assertRaises(seed.SeedError):
            self.transfer(token="d" * 32)
        self.assertFalse((self.root / "guest").exists())
        for field, wrong in [("seed_id", "e" * 32), ("game_build", "different"),
                             ("mod_sha256", "f" * 64), ("extensions", [])]:
            with self.subTest(field=field), self.assertRaises(seed.SeedError):
                self.transfer(identity={**IDENTITY, field: wrong})
            self.assertFalse((self.root / "guest").exists())

    def test_reserved_multiplayer_profiles_stage_without_overwriting_existing_saves(self):
        for number in range(45841, 45849):
            with self.subTest(profile=number):
                profile = self.root / str(number)
                saves = profile / "save"
                saves.mkdir(parents=True)
                cloud = saves / "steam_autocloud.vdf"
                cloud.write_bytes(b"existing cloud metadata")
                target = seed.stage_test_save(self.host, profile)
                self.assertEqual(target.read_bytes(), self.payload)
                self.assertEqual(cloud.read_bytes(), b"existing cloud metadata")
                target.write_bytes(b"later player save")
                with self.assertRaises(seed.SeedError):
                    seed.stage_test_save(self.host, profile)
                self.assertEqual(target.read_bytes(), b"later player save")
                self.assertEqual((self.host / "seed.bin").read_bytes(), self.payload)

    def test_multiplayer_staging_refuses_normal_missing_and_linked_profiles(self):
        for name in ("45840", "45849", "70000001", "45843-extra"):
            profile = self.root / name
            profile.mkdir()
            with self.subTest(profile=name), self.assertRaises(seed.SeedError):
                seed.stage_test_save(self.host, profile)
            self.assertEqual(list(profile.iterdir()), [])
        profile = self.root / "45843"
        with self.assertRaises(seed.SeedError):
            seed.stage_test_save(self.host, profile)
        self.assertFalse(profile.exists())
        normal = self.root / "70000001"
        profile.symlink_to(normal, target_is_directory=True)
        with self.assertRaises(seed.SeedError):
            seed.stage_test_save(self.host, profile)
        profile.unlink()
        profile.mkdir()
        (profile / "save").symlink_to(normal, target_is_directory=True)
        with self.assertRaises(seed.SeedError):
            seed.stage_test_save(self.host, profile)
        self.assertEqual(list(normal.iterdir()), [])

    def test_content_mismatch_names_requirements_without_publishing_a_save(self):
        with self.assertRaisesRegex(seed.SeedError, "Host: ego_dlc_timelines@900"):
            self.transfer(identity={**IDENTITY, "extensions": []})
        self.assertFalse((self.root / "guest").exists())

    def _synthetic_sender(self, payload, *, signed=True, manifest=None):
        value = manifest or self.manifest
        def send(connection):
            nonce = "1" * 32
            seed._send_header(connection, {"protocol": 1, "nonce": nonce})
            seed._receive_header(connection)
            mac = seed._mac(TOKEN, b"manifest", nonce, seed.canonical(value)) if signed else "0" * 64
            seed._send_header(connection, {"manifest": value, "mac": mac})
            connection.sendall(payload)
        return send

    def test_truncation_and_corruption_never_publish_completed_manifest(self):
        for payload in [self.payload[:-1], b"!" + self.payload[1:]]:
            with self.subTest(length=len(payload)), self.assertRaises(seed.SeedError):
                self.transfer(self._synthetic_sender(payload))
            guest = self.root / "guest"
            self.assertFalse((guest / "manifest.json").exists())
            self.assertFalse((guest / "seed.bin").exists())
            self.assertFalse((guest / "seed.part").exists())
            guest.rmdir()

    def test_forged_manifest_and_network_path_are_rejected_before_writing(self):
        with self.assertRaises(seed.SeedError):
            self.transfer(self._synthetic_sender(b"", signed=False))
        with self.assertRaises(seed.SeedError):
            self.transfer(self._synthetic_sender(b"", manifest={**self.manifest, "filename": "../../save.xml"}))
        self.assertFalse((self.root / "guest").exists())

    def test_sealed_bytes_are_checked_and_existing_directories_preserved(self):
        before = (self.host / "seed.bin").read_bytes()
        with self.assertRaises(FileExistsError):
            seed.create_bundle(self.snapshot, self.host, **IDENTITY)
        self.assertEqual((self.host / "seed.bin").read_bytes(), before)
        (self.host / "seed.bin").write_bytes(b"x" + before[1:])
        with self.assertRaises(seed.SeedError):
            seed.read_bundle(self.host)

    def test_manifest_bounds_and_profile_guards(self):
        for change in [{"size": True}, {"size": seed.MAX_SAVE + 1}, {"size": 0},
                       {"protocol": True}, {"seed_id": "../bad"},
                       {"extensions": IDENTITY["extensions"] * 2},
                       {"extensions": [{"id": "a", "version": 1}]}]:
            with self.subTest(change=change), self.assertRaises(seed.SeedError):
                seed.validate_manifest({**self.manifest, **change})
        normal = self.root / "normal"
        normal.mkdir()
        with self.assertRaises(seed.SeedError):
            seed.stage_test_save(self.host, normal)
        profile = self.root / "45841"
        profile.mkdir()
        (profile / "save").symlink_to(normal, target_is_directory=True)
        with self.assertRaises(seed.SeedError):
            seed.stage_test_save(self.host, profile)
        self.assertEqual(list(normal.iterdir()), [])

    def test_huge_header_rejected_without_allocation_or_destination(self):
        with self.assertRaises(seed.SeedError):
            self.transfer(lambda connection: connection.sendall((seed.MAX_HEADER + 1).to_bytes(4, "big")))
        self.assertFalse((self.root / "guest").exists())


if __name__ == "__main__":
    unittest.main()
