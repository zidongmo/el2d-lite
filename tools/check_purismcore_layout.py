from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


REQUIRED_HEADERS = [
    Path("include") / "PurismCore.h",
    Path("include") / "Live2DCubismCore.h",
]

REQUIRED_SOURCE_FILES = [
    Path("src") / "core.c",
    Path("src") / "moc3.c",
    Path("src") / "model.c",
    Path("src") / "render.c",
]


def inspect_purismcore_layout(root_path: str | Path) -> dict[str, Any]:
    root = Path(root_path).resolve()
    if not root.exists():
        return {
            "available": False,
            "root": root.as_posix(),
            "mode": "missing",
            "missing": ["root"],
            "include_dir": None,
            "source_files": [],
        }

    bundle = root / "PurismCoreBundle.h"
    if bundle.exists():
        return {
            "available": True,
            "root": root.as_posix(),
            "mode": "single_file_bundle",
            "missing": [],
            "include_dir": root.as_posix(),
            "source_files": [bundle.as_posix()],
        }

    missing = [
        item.as_posix()
        for item in [*REQUIRED_HEADERS, *REQUIRED_SOURCE_FILES]
        if not (root / item).exists()
    ]
    return {
        "available": len(missing) == 0,
        "root": root.as_posix(),
        "mode": "source_checkout",
        "missing": missing,
        "include_dir": (root / "include").as_posix(),
        "source_files": [(root / item).as_posix() for item in REQUIRED_SOURCE_FILES if (root / item).exists()],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Check a PurismCore dependency checkout for ESP Live2D Lite.")
    parser.add_argument("path", type=Path, help="Path to a PurismCore source checkout or bundle directory.")
    parser.add_argument("--output", type=Path, help="Optional JSON report path.")
    args = parser.parse_args()

    report = inspect_purismcore_layout(args.path)
    text = json.dumps(report, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n", encoding="utf-8")
    else:
        print(text)
    return 0 if report["available"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
