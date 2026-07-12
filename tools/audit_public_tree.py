from __future__ import annotations

import argparse
from pathlib import Path


REQUIRED_PATHS = (
    "README.md",
    "LICENSE",
    "NOTICE",
    "docs/architecture.md",
)

SKIPPED_PARTS = {
    ".git",
    ".pytest_cache",
    ".venv",
    "__pycache__",
    "build",
}

FORBIDDEN_FILE_SUFFIXES = (
    ".moc3",
    ".motion3.json",
    ".exp3.json",
    ".physics3.json",
    ".pose3.json",
)

FORBIDDEN_FILE_NAMES = {
    "live2dcubismcore.js",
    "live2dcubismcore.min.js",
}

FORBIDDEN_CONTENT = (
    "firmware/sparkbot-linglong-terminal",
    "firmware\\sparkbot-linglong-terminal",
    "web/vendor/live2d",
    "web\\vendor\\live2d",
    "../soul-sync",
    "..\\soul-sync",
)

TEXT_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".h",
    ".html",
    ".json",
    ".md",
    ".mjs",
    ".py",
    ".txt",
    ".yml",
    ".yaml",
}

CONTENT_SCAN_EXCLUSIONS = {
    "tests/test_public_repository_boundary.py",
    "tools/audit_public_tree.py",
}


def _is_skipped(path: Path, root: Path) -> bool:
    relative = path.relative_to(root)
    return any(part in SKIPPED_PARTS or part.startswith("build-") for part in relative.parts)


def _is_forbidden_file(path: Path) -> bool:
    name = path.name.lower()
    return name in FORBIDDEN_FILE_NAMES or name.endswith(FORBIDDEN_FILE_SUFFIXES)


def audit_tree(root: Path) -> list[str]:
    root = root.resolve()
    issues: list[str] = []

    for relative in REQUIRED_PATHS:
        if not (root / relative).is_file():
            issues.append(f"missing required file: {relative}")

    for path in sorted(root.rglob("*")):
        if not path.is_file() or _is_skipped(path, root):
            continue

        relative = path.relative_to(root).as_posix()
        if _is_forbidden_file(path):
            issues.append(f"forbidden file: {relative}")
            continue

        if relative in CONTENT_SCAN_EXCLUSIONS:
            continue

        if path.suffix.lower() not in TEXT_SUFFIXES and path.name not in {"CMakeLists.txt", "NOTICE"}:
            continue

        try:
            content = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            issues.append(f"non-UTF-8 public text file: {relative}")
            continue

        normalized = content.lower()
        for token in FORBIDDEN_CONTENT:
            if token.lower() in normalized:
                issues.append(f"forbidden content '{token}': {relative}")

    return issues


def main() -> int:
    parser = argparse.ArgumentParser(description="Audit an EL2D Lite public source tree.")
    parser.add_argument("root", type=Path, nargs="?", default=Path.cwd())
    args = parser.parse_args()
    issues = audit_tree(args.root)
    for issue in issues:
        print(issue)
    if issues:
        print(f"Public tree audit failed with {len(issues)} issue(s).")
        return 1
    print("Public tree audit passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
