from __future__ import annotations

import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CHECKER_PATH = ROOT / "tools" / "check_purismcore_layout.py"


def load_checker_module():
    spec = importlib.util.spec_from_file_location("check_purismcore_layout", CHECKER_PATH)
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_purismcore_layout_reports_missing_dependency(tmp_path: Path) -> None:
    checker = load_checker_module()

    report = checker.inspect_purismcore_layout(tmp_path / "missing")

    assert report["available"] is False
    assert report["missing"] == ["root"]


def test_purismcore_layout_accepts_source_checkout_shape(tmp_path: Path) -> None:
    checker = load_checker_module()
    root = tmp_path / "PurismCore"
    (root / "include").mkdir(parents=True)
    (root / "src").mkdir()
    (root / "include" / "PurismCore.h").write_text("", encoding="utf-8")
    (root / "include" / "Live2DCubismCore.h").write_text("", encoding="utf-8")
    for name in ["core.c", "moc3.c", "model.c", "render.c"]:
        (root / "src" / name).write_text("", encoding="utf-8")

    report = checker.inspect_purismcore_layout(root)

    assert report["available"] is True
    assert report["missing"] == []
    assert report["mode"] == "source_checkout"
    assert report["include_dir"].endswith("include")
    assert len(report["source_files"]) == 4
