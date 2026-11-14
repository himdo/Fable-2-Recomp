"""Check the hot-function override generator with fake generated code, without a game build."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import pytest

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


@pytest.fixture
def folders(tmp_path, monkeypatch):
    """Point the generator at a temporary folder, and return its generated/ and overrides/ paths."""
    generated = tmp_path / "generated"
    generated.mkdir()
    overrides = tmp_path / "overrides"
    # ROOT too: relpath() fails when the repo and temp folder are on different Windows drives.
    monkeypatch.setattr(hotfunc, "ROOT", str(tmp_path))
    monkeypatch.setattr(hotfunc, "GEN_DIR", str(generated))
    monkeypatch.setattr(hotfunc, "OUT_DIR", str(overrides))
    return SimpleNamespace(generated=generated, overrides=overrides,
                           output=overrides / "frame" / f"{NAME}.cpp")


def write_override(folders, text):
    folders.output.parent.mkdir(parents=True)
    folders.output.write_text(text, encoding="utf-8")


def emit(active=None):
    hotfunc.emit(NAME, "frame", SOURCE_FILE, (2, 8), BODY, active or {})


def run_main(functions, *names):
    argv = ["make_hotfunc_overrides.py", *names]
    with patch.object(hotfunc, "FUNCTIONS", functions), patch("sys.argv", argv):
        hotfunc.main()


def generate(folders, body, **config):
    """Run main() on one fake function and return the override it writes."""
    generated = f"DEFINE_REX_FUNC({NAME}) {{\n{body}\tctx.r3.u64 = 1;\n}}\n"
    (folders.generated / SOURCE_FILE).write_text(generated, encoding="utf-8")
    run_main({NAME: dict(dir="frame", **config)})
    return folders.output.read_text(encoding="utf-8")


def real_override_path(name):
    return REAL_OVERRIDES / hotfunc.FUNCTIONS[name]["dir"] / f"{name}.cpp"


def describe_extract_body():
    def test_should_return_the_whole_function(folders):
        (folders.generated / SOURCE_FILE).write_text(GENERATED, encoding="utf-8")
        assert hotfunc.extract_body(NAME) == (SOURCE_FILE, 2, 8, BODY)

    def test_should_ignore_files_that_are_not_recompiled_output(folders):
        (folders.generated / SOURCE_FILE).write_text(GENERATED, encoding="utf-8")
        (folders.generated / "notes.cpp").write_text(GENERATED, encoding="utf-8")
        assert hotfunc.extract_body(NAME)[0] == SOURCE_FILE

    def test_should_reject_a_missing_function(folders):
        (folders.generated / SOURCE_FILE).write_text("// nothing here\n", encoding="utf-8")
        with pytest.raises(AssertionError):
            hotfunc.extract_body(NAME)

    def test_should_reject_two_definitions(folders):
        (folders.generated / SOURCE_FILE).write_text(GENERATED, encoding="utf-8")
        (folders.generated / "fable_2_recomp.1.cpp").write_text(GENERATED, encoding="utf-8")
        with pytest.raises(AssertionError):
            hotfunc.extract_body(NAME)

    def test_should_reject_a_function_with_no_closing_brace(folders):
        unterminated = f"DEFINE_REX_FUNC({NAME}) {{\n\tctx.r3.u64 = 1;\n"
        (folders.generated / SOURCE_FILE).write_text(unterminated, encoding="utf-8")
        with pytest.raises(AssertionError):
            hotfunc.extract_body(NAME)


def describe_is_hand_tuned():
    def test_should_not_count_a_missing_file(folders):
        assert not hotfunc.is_hand_tuned(str(folders.output))

    def test_should_count_a_marker_on_the_first_line(folders):
        write_override(folders, f"{hotfunc.HAND_TUNED_MARKER} of {NAME}\n")
        assert hotfunc.is_hand_tuned(str(folders.output))

    def test_should_not_count_a_marker_on_a_later_line(folders):
        write_override(folders, f"// generated\n{hotfunc.HAND_TUNED_MARKER}\n")
        assert not hotfunc.is_hand_tuned(str(folders.output))

    # --- Known issues -----------------------------------------------------

    @pytest.mark.xfail(strict=True, reason="a file saved with a BOM isn't recognized, so the generator would overwrite it")
    def test_should_count_a_marker_after_a_byte_order_mark(folders):
        folders.output.parent.mkdir(parents=True)
        folders.output.write_bytes(b"\xef\xbb\xbf" + hotfunc.HAND_TUNED_MARKER.encode() + b"\n")
        assert hotfunc.is_hand_tuned(str(folders.output))


def describe_emit():
    def test_should_write_the_override_with_its_body(folders):
        emit()
        text = folders.output.read_text(encoding="utf-8")
        assert f'extern "C" void {NAME}(PPCContext& __restrict ctx, uint8_t* base) {{' in text
        assert BODY in text

    def test_should_declare_the_functions_it_calls(folders):
        emit()
        text = folders.output.read_text(encoding="utf-8")
        assert 'extern "C" void Helper_82000010(PPCContext& ctx, uint8_t* base);' in text

    def test_should_never_overwrite_a_hand_tuned_file(folders):
        hand_written = f"{hotfunc.HAND_TUNED_MARKER} of {NAME}\n// careful work\n"
        write_override(folders, hand_written)
        emit()
        assert folders.output.read_text(encoding="utf-8") == hand_written

    def test_should_replace_an_older_generated_file(folders):
        write_override(folders, "// old generated copy\n")
        emit()
        assert "old generated copy" not in folders.output.read_text(encoding="utf-8")

    def test_should_include_the_yield_header_only_when_used(folders):
        emit()
        assert "hotfunc_yield.h" not in folders.output.read_text(encoding="utf-8")
        emit({"batchyield": True})
        assert "hotfunc_yield.h" in folders.output.read_text(encoding="utf-8")

    def test_should_include_the_lock_helper_only_when_used(folders):
        emit()
        assert "hotfunc_gcr_mutex" not in folders.output.read_text(encoding="utf-8")
        emit({"glock": True})
        assert "hotfunc_gcr_mutex" in folders.output.read_text(encoding="utf-8")


def describe_transform_nv():
    def test_should_replace_loads_and_stores():
        text = "\tREX_LOAD_U32(a);\n\tREX_STORE_U8(b, 1);\n"
        assert hotfunc.transform_nv(text, NAME) == "\tGV32(a);\n\tSV8(b, 1);\n"

    def test_should_replace_every_access_size():
        text = ("REX_LOAD_U8(a) REX_LOAD_U16(a) REX_LOAD_U32(a) REX_LOAD_U64(a) "
                "REX_STORE_U8(a) REX_STORE_U16(a) REX_STORE_U32(a) REX_STORE_U64(a)")
        expected = "GV8(a) GV16(a) GV32(a) GV64(a) SV8(a) SV16(a) SV32(a) SV64(a)"
        assert hotfunc.transform_nv(text, NAME) == expected

    def test_should_reject_mmio_access():
        with pytest.raises(AssertionError):
            hotfunc.transform_nv("\tREX_MM_LOAD_U32(a);\n", NAME)

    def test_should_reject_an_access_it_cannot_replace():
        with pytest.raises(AssertionError):
            hotfunc.transform_nv("\tREX_LOAD_U128(a);\n", NAME)


def describe_transform_gt():
    def test_should_inline_the_timebase_call():
        result = hotfunc.transform_gt("\tGetTimebase_8221EB58(ctx, base);\n", NAME)
        assert "GetTimebase_8221EB58(ctx, base);" not in result
        assert hotfunc.GT in result


def describe_transform_mfmsr():
    def test_should_keep_the_original_register():
        result = hotfunc.transform_mfmsr(MFMSR, NAME)
        assert "= REX_CHECK_GLOBAL_LOCK()" not in result
        assert "ctx.r9.u64 = ppc_global_lock_count_().load(std::memory_order_acquire)" in result

    def test_should_leave_other_code_alone():
        assert hotfunc.transform_mfmsr("\tctx.r9.u64 = 1;\n", NAME) == "\tctx.r9.u64 = 1;\n"


def describe_transform_glock():
    def test_should_replace_a_balanced_lock():
        result, used = hotfunc.transform_glock(ENTER + LEAVE, NAME)
        assert used
        assert "hotfunc_gcr_mutex().lock();" in result
        assert "ppc_global_lock_count_().fetch_add(1);" in result
        assert "ppc_global_lock_count_().fetch_sub(1);" in result
        assert "hotfunc_gcr_mutex().unlock();" in result
        assert "REX_ENTER_GLOBAL_LOCK" not in result
        assert "REX_LEAVE_GLOBAL_LOCK" not in result

    def test_should_reject_an_unbalanced_lock():
        with pytest.raises(AssertionError):
            hotfunc.transform_glock(ENTER, NAME)

    def test_should_report_when_there_is_no_lock():
        code = "\tctx.r3.u64 = 1;\n"
        assert hotfunc.transform_glock(code, NAME) == (code, False)


def describe_transform_batchyield():
    def test_should_replace_the_yield_and_keep_its_return_value():
        result, used = hotfunc.transform_batchyield(YIELD_CALL, NAME)
        assert used
        assert "__imp__NtYieldExecution" not in result
        assert "fable2::hotfunc::BatchedYield();" in result
        assert "ctx.r3.s64 = 0;" in result

    def test_should_report_when_there_is_no_yield():
        code = "\tctx.r3.u64 = 1;\n"
        assert hotfunc.transform_batchyield(code, NAME) == (code, False)


def describe_transform_hoist():
    def test_should_only_run_on_the_function_it_was_written_for():
        with pytest.raises(AssertionError, match="hoist only defined"):
            hotfunc.transform_hoist("", NAME)


def describe_main():
    # --- Happy flow -------------------------------------------------------

    def test_should_apply_the_nv_transform(folders):
        text = generate(folders, "\tREX_LOAD_U32(a);\n", nv=True)
        assert "\tGV32(a);" in text
        assert "REX_LOAD_U32" not in text

    def test_should_apply_the_gt_transform(folders):
        text = generate(folders, "\tGetTimebase_8221EB58(ctx, base);\n", gt=True)
        assert hotfunc.GT in text
        assert "GetTimebase_8221EB58(ctx, base);" not in text

    def test_should_apply_the_mfmsr_transform(folders):
        text = generate(folders, MFMSR, mfmsr=True)
        assert "ctx.r9.u64 = ppc_global_lock_count_().load(std::memory_order_acquire)" in text

    def test_should_apply_the_glock_transform_and_add_its_helper(folders):
        text = generate(folders, ENTER + LEAVE, glock=True)
        assert "\thotfunc_gcr_mutex().lock();" in text
        assert "static inline std::recursive_mutex& hotfunc_gcr_mutex()" in text

    def test_should_apply_the_batchyield_transform_and_add_its_header(folders):
        text = generate(folders, YIELD_CALL, batchyield=True)
        assert "fable2::hotfunc::BatchedYield();" in text
        assert '#include "hotfunc/hotfunc_yield.h"' in text

    # --- Edge cases -------------------------------------------------------

    def test_should_skip_a_hand_tuned_entry(folders):
        (folders.generated / SOURCE_FILE).write_text(GENERATED, encoding="utf-8")
        run_main({NAME: dict(dir="frame", hand_tuned=True)})
        assert not folders.output.exists()

    def test_should_only_generate_the_named_functions(folders):
        other = "Other_82000020"
        generated = f"{GENERATED}DEFINE_REX_FUNC({other}) {{\n\tctx.r3.u64 = 2;\n}}\n"
        (folders.generated / SOURCE_FILE).write_text(generated, encoding="utf-8")
        run_main({NAME: dict(dir="frame"), other: dict(dir="frame")}, other)
        assert not folders.output.exists()
        assert (folders.overrides / "frame" / f"{other}.cpp").exists()

    # --- Known issues -----------------------------------------------------

    @pytest.mark.xfail(strict=True, reason="a mistyped name does nothing and the run still reports success")
    def test_should_fail_on_an_unknown_function_name(folders):
        (folders.generated / SOURCE_FILE).write_text(GENERATED, encoding="utf-8")
        with pytest.raises(SystemExit):
            run_main({NAME: dict(dir="frame")}, "Typo_Name")


def describe_the_real_overrides():
    """Check the generator's config against the files in the repo, so no generated code is needed."""

    def test_should_agree_with_the_hand_tuned_markers():
        existing = [name for name in hotfunc.FUNCTIONS if real_override_path(name).exists()]
        disagreements = [name for name in existing if bool(hotfunc.FUNCTIONS[name].get("hand_tuned"))
                         != hotfunc.is_hand_tuned(str(real_override_path(name)))]
        assert disagreements == []

    # --- Known issues -----------------------------------------------------

    @pytest.mark.xfail(strict=True, reason="LookStickProcess is in unsorted/ and RightStickLookProcess doesn't exist")
    def test_should_have_a_file_for_each_generated_entry():
        # A full run would add a second LookStickProcess override, and one that isn't in today's build.
        generated = [name for name, config in hotfunc.FUNCTIONS.items() if not config.get("hand_tuned")]
        missing = [name for name in generated if not real_override_path(name).exists()]
        assert missing == []

    @pytest.mark.xfail(strict=True, reason="the ui_rendering entries are stale")
    def test_should_have_a_file_for_each_hand_tuned_entry():
        # Harmless today, because hand-tuned entries get skipped.
        hand_tuned = [name for name, config in hotfunc.FUNCTIONS.items() if config.get("hand_tuned")]
        missing = [name for name in hand_tuned if not real_override_path(name).exists()]
        assert missing == []
