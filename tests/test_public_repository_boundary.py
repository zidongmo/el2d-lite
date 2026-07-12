from __future__ import annotations

import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
AUDIT_TOOL = ROOT / "tools" / "audit_public_tree.py"


def load_audit_tree():
    spec = importlib.util.spec_from_file_location("audit_public_tree", AUDIT_TOOL)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.audit_tree


def test_repository_has_no_private_product_or_model_payloads() -> None:
    audit_tree = load_audit_tree()

    assert audit_tree(ROOT) == []


def test_audit_rejects_parent_repo_and_proprietary_payloads(tmp_path: Path) -> None:
    audit_tree = load_audit_tree()
    for required in ("README.md", "LICENSE", "NOTICE", "docs/architecture.md"):
        path = tmp_path / required
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("public\n", encoding="utf-8")

    coupled = tmp_path / "src" / "product_adapter.c"
    coupled.parent.mkdir(parents=True)
    coupled.write_text(
        '#include "../../soul-sync/firmware/sparkbot-linglong-terminal/main/private.h"\n',
        encoding="utf-8",
    )
    (tmp_path / "assets").mkdir()
    (tmp_path / "assets" / "Haru.moc3").write_bytes(b"private model")

    issues = audit_tree(tmp_path)

    assert any("forbidden content" in issue for issue in issues)
    assert any("forbidden file" in issue for issue in issues)
