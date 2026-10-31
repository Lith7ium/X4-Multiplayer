"""Keep live installers from regaining write access to shared executable stubs."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]/'src/native/engine'
INSTALLERS = {
    'agent':256, 'ai':128, 'attention':576, 'contact':848, 'contact_properties':2048,
    'damage':736, 'flight':640, 'gravidar':704, 'interrupt':800, 'interrupt_step':896,
    'mass':512, 'nested_action':1024, 'npc_deadline':1344, 'npc_enqueue':1216,
    'npc_finish':1280, 'npc_ready':1664, 'npc_rng':1088, 'npc_speak':1152,
    'params':320, 'registry_md':2432, 'request':2496, 'shape':384, 'solver':2304,
    'world_identity':944, 'world_created':448,
}


class ThunkPageSourceTests(unittest.TestCase):
    def test_all_preexisting_live_writers_are_checks(self):
        for stem, offset in INSTALLERS.items():
            text = (ROOT/f'lockstep_{stem}.h').read_text()
            with self.subTest(stem=stem):
                self.assertIn(f'if(!thunk_slot_ready({offset})) return 0;', text)
        self.assertIn('if(!thunk_slot_ready(32)) return 0;', (ROOT/'lockstep_gate.c').read_text())
        self.assertIn('if(!thunk_slot_ready(2560+i*64)) return 0;', (ROOT/'lockstep_world_queue.h').read_text())

    def test_no_live_thunk_protection_or_write_operations(self):
        for path in ROOT.glob('lockstep*'):
            if path.suffix not in ('.h','.c'):
                continue
            text = path.read_text()
            with self.subTest(path=path.name):
                self.assertNotRegex(text, r'VirtualProtect\s*\(\s*thunk\b')
                self.assertNotRegex(text, r'(?:memcpy|memset)\s*\(\s*thunk\s*(?:\+|,)')
                self.assertNotRegex(text, r'thunk\s*\[[^\]]+\]\s*=')

    def test_single_private_emission_and_checked_seal_before_return(self):
        gate = (ROOT/'lockstep_gate.c').read_text()
        self.assertEqual(gate.count('thunk_prepare_page(p)'), 1)
        start = gate.index('static unsigned char *make_thunk_using(')
        end = gate.index('static unsigned char *make_thunk(void)', start)
        body = gate[start:end]
        order = [body.index(text) for text in ('allocate(', 'thunk_prepare_page(p)',
                 'protect(p,THUNK_PAGE_SIZE,PAGE_EXECUTE_READ', 'flush(GetCurrentProcess(),p,THUNK_PAGE_SIZE)',
                 'thunk_page_rx(p)', 'return p;')]
        self.assertEqual(order, sorted(order))
        self.assertIn('release(p,0,MEM_RELEASE);return NULL;', body)
        self.assertNotIn('RtlAddFunctionTable', body)
        emit = (ROOT/'lockstep_thunk_emit.h').read_text()
        self.assertNotRegex(emit, r'VirtualProtect|RtlAddFunctionTable|\b\w+_swap\s*\(')
        # Wait stays independently owned and is emitted only in the private phase.
        self.assertEqual(emit.count('world_wait_prepare_thunk(page)'), 1)
        self.assertEqual(emit.count('transition_emit_page(page)'), 1)

    def test_prefix_unwind_registration_precedes_only_its_feature_publication(self):
        for stem, offset in (('params', 320), ('attention', 576), ('damage', 736), ('solver', 2304)):
            text = (ROOT/f'lockstep_{stem}.h').read_text()
            body = text[text.index(f'static int {stem}_install('):]
            with self.subTest(stem=stem):
                check = body.index(f'if(!thunk_slot_ready({offset})) return 0;')
                register = body.index(f'if(!thunk_unwind_prepare({offset})) return 0;')
                publish = body.index(f'if(!{stem}_swap(')
                self.assertLess(check, register)
                self.assertLess(register, publish)
                self.assertNotIn('thunk_unwind_prepare', text[:text.index(f'static int {stem}_install(')])
        unwind = (ROOT/'lockstep_thunk_unwind.h').read_text()
        self.assertNotIn('RtlDeleteFunctionTable', unwind)
        self.assertNotIn('VirtualFree', unwind)
        emit = (ROOT/'lockstep_thunk_emit.h').read_text()
        self.assertNotIn('thunk_unwind_prepare(', emit)
        self.assertNotIn('RtlAddFunctionTable', emit)


if __name__ == '__main__':
    unittest.main()
