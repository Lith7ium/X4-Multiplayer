"""Game script failures must remain visible through the native session runner."""
import ast
from pathlib import Path
import re
import unittest


class RunnerErrors(unittest.TestCase):
    def test_all_native_diagnostic_errors_fail_but_normal_stop_does_not(self):
        root=Path(__file__).resolve().parents[1]
        tree=ast.parse((root/'tools/dev/run_native_gate.py').read_text())
        pattern=next(node.value for node in ast.walk(tree)
                     if isinstance(node,ast.Constant) and isinstance(node.value,str)
                     and node.value.startswith('Error while executing'))
        for module in ('Gate','Commands','Management','Construction'):
            self.assertRegex('Error in MD cue md.X4LAN_Native'+module+'.Setup: invalid container',pattern)
        for module in ('probe','player','management','construction'):
            self.assertRegex("Error while executing the Lua script 'extensions/x4_lan_lab/ui/native_gate_"+module+".lua'",pattern)
        for text in ('NATIVE_GATE ERROR code=7','NATIVE_GATE REFUSED build', 'NATIVE_GATE STOP tick=9 reason=2 restored=1'):
            self.assertRegex(text,pattern)
        self.assertIsNone(re.search(pattern,'NATIVE_GATE STOP tick=1200 reason=1 restored=1'))


if __name__ == '__main__':unittest.main()
