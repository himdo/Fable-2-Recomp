"""Check the hot-function override generator with fake generated code, without a game build."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("hotfunc", ROOT / "tools/make_hotfunc_overrides.py")
hotfunc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hotfunc)

REAL_OVERRIDES = ROOT / "src/core/hotfunc"
NAME = "Sample_82000000"
SOURCE_FILE = "fable_2_recomp.0.cpp"
BODY = "\tif (ctx.r3.u32) {\n\t\tctx.r4.u64 = 0;\n\t}\n\tHelper_82000010(ctx, base);\n\tctx.r3.u64 = 1;"
GENERATED = f"// generated\nDEFINE_REX_FUNC({NAME}) {{\n{BODY}\n}}\n"
ENTER = "\tREX_ENTER_GLOBAL_LOCK();\n"
LEAVE = "\tREX_LEAVE_GLOBAL_LOCK();\n"
YIELD_CALL = "\t__imp__NtYieldExecution(ctx, base);\n"
MFMSR = ("\tstd::atomic_thread_fence(std::memory_order_seq_cst);\n"
         "\tctx.r9.u64 = REX_CHECK_GLOBAL_LOCK();\n")


class HotfuncTestCase(unittest.TestCase):
    """Point the generator at a temporary folder and silence its progress output."""

    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix="fable2-hotfunc-test-")
        self.addCleanup(temp.cleanup)
        base = Path(temp.name)
        self.generated = base / "generated"
        self.generated.mkdir()
        self.overrides = base / "overrides"
        self.output = self.overrides / "frame" / f"{NAME}.cpp"
        # ROOT too: relpath() fails when the repo and temp folder are on different Windows drives.
        for patcher in (patch.object(hotfunc, "ROOT", str(base)),
                        patch.object(hotfunc, "GEN_DIR", str(self.generated)),
                        patch.object(hotfunc, "OUT_DIR", str(self.overrides)),
                        patch("builtins.print")):
            patcher.start()
            self.addCleanup(patcher.stop)

    def write_override(self, text):
        self.output.parent.mkdir(parents=True)
        self.output.write_text(text, encoding="utf-8")


class ExtractBodyTests(HotfuncTestCase):
    def test_returns_the_whole_function(self):
        (self.generated / SOURCE_FILE).write_text(GENERATED, encoding="utf-8")
        self.assertEqual(hotfunc.extract_body(NAME), (SOURCE_FILE, 2, 8, BODY))

    def test_ignores_files_that_are_not_recompiled_output(self):
        (self.generated / SOURCE_FILE).write_text(GENERATED, encoding="utf-8")
        (self.generated / "notes.cpp").write_text(GENERATED, encoding="utf-8")
        self.assertEqual(hotfunc.extract_body(NAME)[0], SOURCE_FILE)

    def test_rejects_a_missing_function(self):
        (self.generated / SOURCE_FILE).write_text("// nothing here\n", encoding="utf-8")
        with self.assertRaises(AssertionError):
            hotfunc.extract_body(NAME)

    def test_rejects_two_definitions(self):
        (self.generated / SOURCE_FILE).write_text(GENERATED, encoding="utf-8")
        (self.generated / "fable_2_recomp.1.cpp").write_text(GENERATED, encoding="utf-8")
        with self.assertRaises(AssertionError):
            hotfunc.extract_body(NAME)

    def test_rejects_a_function_with_no_closing_brace(self):
        unterminated = f"DEFINE_REX_FUNC({NAME}) {{\n\tctx.r3.u64 = 1;\n"
        (self.generated / SOURCE_FILE).write_text(unterminated, encoding="utf-8")
        with self.assertRaises(AssertionError):
            hotfunc.extract_body(NAME)


class IsHandTunedTests(HotfuncTestCase):
    def test_missing_file_is_not_hand_tuned(self):
        self.assertFalse(hotfunc.is_hand_tuned(str(self.output)))

    def test_marker_on_the_first_line_is_hand_tuned(self):
        self.write_override(f"{hotfunc.HAND_TUNED_MARKER} of {NAME}\n")
        self.assertTrue(hotfunc.is_hand_tuned(str(self.output)))

    def test_marker_on_a_later_line_is_not_hand_tuned(self):
        self.write_override(f"// generated\n{hotfunc.HAND_TUNED_MARKER}\n")
        self.assertFalse(hotfunc.is_hand_tuned(str(self.output)))

    @unittest.expectedFailure
    def test_marker_after_a_byte_order_mark_is_hand_tuned(self):
        # Known issue: a file saved with a BOM isn't recognized, so the generator would overwrite it.
        self.output.parent.mkdir(parents=True)
        self.output.write_bytes(b"\xef\xbb\xbf" + hotfunc.HAND_TUNED_MARKER.encode() + b"\n")
        self.assertTrue(hotfunc.is_hand_tuned(str(self.output)))


class EmitTests(HotfuncTestCase):
    def emit(self, active=None):
        hotfunc.emit(NAME, "frame", SOURCE_FILE, (2, 8), BODY, active or {})

    def test_writes_the_override_with_its_body(self):
        self.emit()
        text = self.output.read_text(encoding="utf-8")
        self.assertIn(f'extern "C" void {NAME}(PPCContext& __restrict ctx, uint8_t* base) {{', text)
        self.assertIn(BODY, text)

    def test_declares_the_functions_it_calls(self):
        self.emit()
        text = self.output.read_text(encoding="utf-8")
        self.assertIn('extern "C" void Helper_82000010(PPCContext& ctx, uint8_t* base);', text)

    def test_never_overwrites_a_hand_tuned_file(self):
        hand_written = f"{hotfunc.HAND_TUNED_MARKER} of {NAME}\n// careful work\n"
        self.write_override(hand_written)
        self.emit()
        self.assertEqual(self.output.read_text(encoding="utf-8"), hand_written)

    def test_replaces_an_older_generated_file(self):
        self.write_override("// old generated copy\n")
        self.emit()
        self.assertNotIn("old generated copy", self.output.read_text(encoding="utf-8"))

    def test_includes_the_yield_header_only_when_used(self):
        self.emit()
        self.assertNotIn("hotfunc_yield.h", self.output.read_text(encoding="utf-8"))
        self.emit({"batchyield": True})
        self.assertIn("hotfunc_yield.h", self.output.read_text(encoding="utf-8"))

    def test_includes_the_lock_helper_only_when_used(self):
        self.emit()
        self.assertNotIn("hotfunc_gcr_mutex", self.output.read_text(encoding="utf-8"))
        self.emit({"glock": True})
        self.assertIn("hotfunc_gcr_mutex", self.output.read_text(encoding="utf-8"))


class TransformTests(HotfuncTestCase):
    def test_nv_replaces_loads_and_stores(self):
        text = "\tREX_LOAD_U32(a);\n\tREX_STORE_U8(b, 1);\n"
        self.assertEqual(hotfunc.transform_nv(text, NAME), "\tGV32(a);\n\tSV8(b, 1);\n")

    def test_nv_replaces_every_access_size(self):
        text = ("REX_LOAD_U8(a) REX_LOAD_U16(a) REX_LOAD_U32(a) REX_LOAD_U64(a) "
                "REX_STORE_U8(a) REX_STORE_U16(a) REX_STORE_U32(a) REX_STORE_U64(a)")
        expected = "GV8(a) GV16(a) GV32(a) GV64(a) SV8(a) SV16(a) SV32(a) SV64(a)"
        self.assertEqual(hotfunc.transform_nv(text, NAME), expected)

    def test_nv_rejects_mmio_access(self):
        with self.assertRaises(AssertionError):
            hotfunc.transform_nv("\tREX_MM_LOAD_U32(a);\n", NAME)

    def test_nv_rejects_an_access_it_cannot_replace(self):
        with self.assertRaises(AssertionError):
            hotfunc.transform_nv("\tREX_LOAD_U128(a);\n", NAME)

    def test_gt_inlines_the_timebase_call(self):
        result = hotfunc.transform_gt("\tGetTimebase_8221EB58(ctx, base);\n", NAME)
        self.assertNotIn("GetTimebase_8221EB58(ctx, base);", result)
        self.assertIn(hotfunc.GT, result)

    def test_mfmsr_keeps_the_original_register(self):
        result = hotfunc.transform_mfmsr(MFMSR, NAME)
        self.assertNotIn("= REX_CHECK_GLOBAL_LOCK()", result)
        self.assertIn("ctx.r9.u64 = ppc_global_lock_count_().load(std::memory_order_acquire)", result)

    def test_mfmsr_leaves_other_code_alone(self):
        self.assertEqual(hotfunc.transform_mfmsr("\tctx.r9.u64 = 1;\n", NAME), "\tctx.r9.u64 = 1;\n")

    def test_glock_replaces_a_balanced_lock(self):
        result, used = hotfunc.transform_glock(ENTER + LEAVE, NAME)
        self.assertTrue(used)
        self.assertIn("hotfunc_gcr_mutex().lock();", result)
        self.assertIn("ppc_global_lock_count_().fetch_add(1);", result)
        self.assertIn("ppc_global_lock_count_().fetch_sub(1);", result)
        self.assertIn("hotfunc_gcr_mutex().unlock();", result)
        self.assertNotIn("REX_ENTER_GLOBAL_LOCK", result)
        self.assertNotIn("REX_LEAVE_GLOBAL_LOCK", result)

    def test_glock_rejects_an_unbalanced_lock(self):
        with self.assertRaises(AssertionError):
            hotfunc.transform_glock(ENTER, NAME)

    def test_glock_reports_when_there_is_no_lock(self):
        code = "\tctx.r3.u64 = 1;\n"
        self.assertEqual(hotfunc.transform_glock(code, NAME), (code, False))

    def test_batchyield_replaces_the_yield_and_keeps_its_return_value(self):
        result, used = hotfunc.transform_batchyield(YIELD_CALL, NAME)
        self.assertTrue(used)
        self.assertNotIn("__imp__NtYieldExecution", result)
        self.assertIn("fable2::hotfunc::BatchedYield();", result)
        self.assertIn("ctx.r3.s64 = 0;", result)

    def test_batchyield_reports_when_there_is_no_yield(self):
        code = "\tctx.r3.u64 = 1;\n"
        self.assertEqual(hotfunc.transform_batchyield(code, NAME), (code, False))

    def test_hoist_only_runs_on_the_function_it_was_written_for(self):
        with self.assertRaisesRegex(AssertionError, "hoist only defined"):
            hotfunc.transform_hoist("", NAME)


class MainTests(HotfuncTestCase):
    def run_main(self, functions, *names):
        argv = ["make_hotfunc_overrides.py", *names]
        with patch.object(hotfunc, "FUNCTIONS", functions), patch("sys.argv", argv):
            hotfunc.main()

    def generate(self, body, **config):
        """Run main() on one fake function and return the override it writes."""
        generated = f"DEFINE_REX_FUNC({NAME}) {{\n{body}\tctx.r3.u64 = 1;\n}}\n"
        (self.generated / SOURCE_FILE).write_text(generated, encoding="utf-8")
        self.run_main({NAME: dict(dir="frame", **config)})
        return self.output.read_text(encoding="utf-8")

    def test_applies_the_nv_transform(self):
        text = self.generate("\tREX_LOAD_U32(a);\n", nv=True)
        self.assertIn("\tGV32(a);", text)
        self.assertNotIn("REX_LOAD_U32", text)

    def test_applies_the_gt_transform(self):
        text = self.generate("\tGetTimebase_8221EB58(ctx, base);\n", gt=True)
        self.assertIn(hotfunc.GT, text)
        self.assertNotIn("GetTimebase_8221EB58(ctx, base);", text)

    def test_applies_the_mfmsr_transform(self):
        text = self.generate(MFMSR, mfmsr=True)
        self.assertIn("ctx.r9.u64 = ppc_global_lock_count_().load(std::memory_order_acquire)", text)

    def test_applies_the_glock_transform_and_adds_its_helper(self):
        text = self.generate(ENTER + LEAVE, glock=True)
        self.assertIn("\thotfunc_gcr_mutex().lock();", text)
        self.assertIn("static inline std::recursive_mutex& hotfunc_gcr_mutex()", text)

    def test_applies_the_batchyield_transform_and_adds_its_header(self):
        text = self.generate(YIELD_CALL, batchyield=True)
        self.assertIn("fable2::hotfunc::BatchedYield();", text)
        self.assertIn('#include "hotfunc/hotfunc_yield.h"', text)

    def test_skips_a_hand_tuned_entry(self):
        (self.generated / SOURCE_FILE).write_text(GENERATED, encoding="utf-8")
        self.run_main({NAME: dict(dir="frame", hand_tuned=True)})
        self.assertFalse(self.output.exists())

    def test_only_generates_the_named_functions(self):
        other = "Other_82000020"
        generated = f"{GENERATED}DEFINE_REX_FUNC({other}) {{\n\tctx.r3.u64 = 2;\n}}\n"
        (self.generated / SOURCE_FILE).write_text(generated, encoding="utf-8")
        self.run_main({NAME: dict(dir="frame"), other: dict(dir="frame")}, other)
        self.assertFalse(self.output.exists())
        self.assertTrue((self.overrides / "frame" / f"{other}.cpp").exists())

    @unittest.expectedFailure
    def test_unknown_function_name_is_an_error(self):
        # Known issue: a mistyped name does nothing and the run still reports success.
        (self.generated / SOURCE_FILE).write_text(GENERATED, encoding="utf-8")
        with self.assertRaises(SystemExit):
            self.run_main({NAME: dict(dir="frame")}, "Typo_Name")


def real_override_path(name):
    return REAL_OVERRIDES / hotfunc.FUNCTIONS[name]["dir"] / f"{name}.cpp"


class RealOverridesTests(unittest.TestCase):
    """Check the generator's config against the files in the repo, so no generated code is needed."""

    def test_config_agrees_with_the_hand_tuned_markers(self):
        existing = [name for name in hotfunc.FUNCTIONS if real_override_path(name).exists()]
        disagreements = [name for name in existing if bool(hotfunc.FUNCTIONS[name].get("hand_tuned"))
                         != hotfunc.is_hand_tuned(str(real_override_path(name)))]
        self.assertEqual(disagreements, [])

    @unittest.expectedFailure
    def test_generated_entries_have_their_override_file(self):
        # Known issue: LookStickProcess is in unsorted/ and RightStickLookProcess doesn't exist, so a full
        # run would add a second LookStickProcess override and one that isn't in today's build.
        generated = [name for name, config in hotfunc.FUNCTIONS.items() if not config.get("hand_tuned")]
        missing = [name for name in generated if not real_override_path(name).exists()]
        self.assertEqual(missing, [])

    @unittest.expectedFailure
    def test_hand_tuned_entries_have_their_override_file(self):
        # Known issue: the ui_rendering entries are stale. Harmless today, because they get skipped.
        hand_tuned = [name for name, config in hotfunc.FUNCTIONS.items() if config.get("hand_tuned")]
        missing = [name for name in hand_tuned if not real_override_path(name).exists()]
        self.assertEqual(missing, [])


if __name__ == "__main__":
    unittest.main()
