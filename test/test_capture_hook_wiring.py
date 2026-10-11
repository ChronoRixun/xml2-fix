"""Hook wiring complements the executable provenance tests (C1)."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]

def capture_body(source):
    # Mask comments/quoted literals so braces and identifiers in those cannot satisfy wiring.
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda m: ' ' * len(m[0]), source, flags=re.S)
    match = re.search(r'\bbool\s+__fastcall\s+capture_hook\s*\([^)]*\)\s*\{', masked)
    if not match:
        raise AssertionError('capture_hook definition missing')
    start = match.end(); depth = 1
    for end in range(start, len(masked)):
        depth += (masked[end] == '{') - (masked[end] == '}')
        if depth == 0:
            return masked[start:end]
    raise AssertionError('capture_hook body incomplete')

def assert_prepared_call(source):
    body = capture_body(source)
    call = re.search(r'\blcsave\s*::\s*prepared_conversation_digest\s*\(\s*tables\s*,\s*'
                     r'meta\s*\.\s*zone\s*,\s*meta\s*\.\s*conversation_digest\s*,\s*error\s*\)', body)
    if not call:
        raise AssertionError('capture_hook must obtain this zone capture digest from the prepared API')

class HookWiring(unittest.TestCase):
    def test_production_capture_calls_prepared_zone_digest(self):
        assert_prepared_call((ROOT / 'src/save_format.cpp').read_text(encoding='utf-8'))

    def test_removing_only_the_hook_call_is_detected(self):
        source = (ROOT / 'src/save_format.cpp').read_text(encoding='utf-8')
        mutated, count = re.subn(r'lcsave::prepared_conversation_digest\(tables,meta\.zone,meta\.conversation_digest,error\)',
                                'false', source)
        self.assertEqual(count, 1)
        with self.assertRaisesRegex(AssertionError, 'prepared API'):
            assert_prepared_call(mutated)

    def test_other_function_and_comment_cannot_fake_hook_wiring(self):
        source = 'bool __fastcall capture_hook(void*) { /* lcsave::prepared_conversation_digest(tables,meta.zone,meta.conversation_digest,error) */ return true; }'
        source += ' void other() { lcsave::prepared_conversation_digest(tables,meta.zone,meta.conversation_digest,error); }'
        with self.assertRaises(AssertionError):
            assert_prepared_call(source)

if __name__ == '__main__':
    unittest.main()
