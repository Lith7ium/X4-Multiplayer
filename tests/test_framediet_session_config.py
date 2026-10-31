"""Installed session configs carry the frame-diet / RNG-split flags per platform and role."""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

from lupa.luajit21 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('framediet_session_launcher', ROOT / 'tools/distribution/session_launcher.py')
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)
SESSION = {'token': 't' * 32, 'bubble': 'bubble-01', 'build': 'world1-test-np1-9.00-steam23660954'}


def flags(platform, slot, dedicated):
    with patch.object(launcher.sys, 'platform', platform):
        text = launcher.session_config(SESSION, slot, '127.0.0.1', dedicated,
                                       Path('control.txt') if dedicated and slot == 'p1' else None)
    lua = LuaRuntime()
    lua.execute(text)
    cfg = lua.globals().X4LAN_CONFIG
    return cfg.frame_diet, cfg.rng_split


class FrameDietSessionConfigTests(unittest.TestCase):
    def test_windows_roles(self):
        self.assertEqual(flags('win32', 'p1', True), (True, True))     # dedicated server
        self.assertEqual(flags('win32', 'p2', True), (False, True))    # client of a dedicated server
        self.assertEqual(flags('win32', 'p1', False), (False, True))   # playing host keeps its picture
        self.assertEqual(flags('win32', 'p2', False), (False, True))

    def test_linux_roles_leave_both_off(self):
        for slot, dedicated in (('p1', True), ('p2', True), ('p1', False), ('p2', False)):
            self.assertEqual(flags('linux', slot, dedicated), (False, False))


if __name__ == '__main__':
    unittest.main()
