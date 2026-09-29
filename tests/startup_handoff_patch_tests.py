import importlib.util
from pathlib import Path
import tempfile
import unittest

p=Path(__file__).resolve().parents[1]/'tools/apply_startup_handoff.py'
spec=importlib.util.spec_from_file_location('startup_patch',p)
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)
        self.a=self.root/'fable_2_recomp.1.cpp';self.b=self.root/'fable_2_recomp.2.cpp'
        self.a.write_text('DEFINE_REX_FUNC(CriticalSection_EnterOrTry_82200688) {\n}\n')
        self.b.write_text('DEFINE_REX_FUNC(sub_8236C360) {\n'+('__imp__RtlLeaveCriticalSection(ctx, base);\n'*5)+'}\nDEFINE_REX_FUNC(other) {\n__imp__RtlLeaveCriticalSection(ctx, base);\n}\n')
    def test_scope_and_idempotence(self):
        self.assertEqual(m.apply(self.root),2)
        text=self.b.read_text();self.assertEqual(text.count('fable2::startup::release('),5)
        self.assertEqual(text.count('__imp__RtlLeaveCriticalSection(ctx, base);'),1)
        self.assertEqual(m.apply(self.root),0)
        self.assertEqual(self.b.read_text(),text)
    def test_both_targets_in_one_unit(self):
        self.a.write_text(self.a.read_text()+self.b.read_text());self.b.unlink()
        self.assertEqual(m.apply(self.root),1)
        text=self.a.read_text()
        self.assertIn('startup_lock_call',text)
        self.assertEqual(text.count('fable2::startup::release('),5)
        self.assertEqual(m.apply(self.root),0)
    def test_missing_anchor_no_partial_write(self):
        before=self.a.read_text();self.b.unlink()
        with self.assertRaises(ValueError):m.apply(self.root)
        self.assertEqual(self.a.read_text(),before)
    def test_wrong_release_count_no_partial_write(self):
        before=self.a.read_text();self.b.write_text(self.b.read_text().replace('__imp__RtlLeaveCriticalSection(ctx, base);','nothing();',1))
        with self.assertRaises(ValueError):m.apply(self.root)
        self.assertEqual(self.a.read_text(),before)
    def test_duplicate_function(self):
        (self.root/'fable_2_recomp.3.cpp').write_text(self.a.read_text())
        with self.assertRaises(ValueError):m.apply(self.root)

if __name__=='__main__':unittest.main()
