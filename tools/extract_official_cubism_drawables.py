from __future__ import annotations

import argparse
import functools
import json
import os
import threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path, PurePosixPath
from typing import Any
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parents[1]
HOST_PAGE = ROOT / "tools" / "official_cubism_extractor.html"
COMMON_BROWSER_EXECUTABLES = (
    Path(os.environ.get("PROGRAMFILES", "")) / "Google" / "Chrome" / "Application" / "chrome.exe",
    Path(os.environ.get("PROGRAMFILES(X86)", "")) / "Google" / "Chrome" / "Application" / "chrome.exe",
    Path(os.environ.get("LOCALAPPDATA", "")) / "Google" / "Chrome" / "Application" / "chrome.exe",
    Path(os.environ.get("PROGRAMFILES", "")) / "Microsoft" / "Edge" / "Application" / "msedge.exe",
    Path(os.environ.get("PROGRAMFILES(X86)", "")) / "Microsoft" / "Edge" / "Application" / "msedge.exe",
)


def resolve_mapped_path(url: str, mappings: dict[str, Path]) -> Path | None:
    request_path = unquote(urlsplit(url).path)
    for prefix, root in sorted(mappings.items(), key=lambda item: len(item[0]), reverse=True):
        if request_path != prefix and not request_path.startswith(prefix + "/"):
            continue
        relative = request_path[len(prefix) :].lstrip("/")
        parts = PurePosixPath(relative).parts
        if any(part in {"", ".", ".."} for part in parts):
            return None
        resolved_root = root.resolve()
        candidate = resolved_root.joinpath(*parts).resolve()
        try:
            candidate.relative_to(resolved_root)
        except ValueError:
            return None
        return candidate
    return None


class QuietHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args: object, mappings: dict[str, Path], **kwargs: object) -> None:
        self.mappings = mappings
        super().__init__(*args, **kwargs)

    def log_message(self, format: str, *args: object) -> None:
        return

    def translate_path(self, path: str) -> str:
        mapped = resolve_mapped_path(path, self.mappings)
        if mapped is None:
            return str(ROOT / ".el2d-not-found")
        return str(mapped)


def write_json(path: Path, data: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def model_name_from_model3(path: Path) -> str:
    suffix = ".model3.json"
    if path.name.endswith(suffix):
        return path.name[: -len(suffix)]
    return path.stem


def read_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8-sig") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"expected JSON object: {path}")
    return data


def motion_initial_parameters(model3_path: Path, motion_spec: str) -> dict[str, float]:
    return motion_initial_values(model3_path, motion_spec, "Parameter")


def motion_initial_part_opacities(model3_path: Path, motion_spec: str) -> dict[str, float]:
    return motion_initial_values(model3_path, motion_spec, "PartOpacity")


def motion_initial_values(model3_path: Path, motion_spec: str, target: str) -> dict[str, float]:
    if not motion_spec:
        return {}
    group, _, index_text = motion_spec.partition(":")
    if not group:
        raise ValueError("motion spec must be GROUP or GROUP:INDEX")
    index = int(index_text) if index_text else 0
    model3 = read_json(model3_path)
    motions = model3.get("FileReferences", {}).get("Motions", {})
    group_motions = motions.get(group)
    if not isinstance(group_motions, list) or index < 0 or index >= len(group_motions):
        raise ValueError(f"motion not found in model3: {motion_spec}")
    motion_file = group_motions[index].get("File")
    if not isinstance(motion_file, str) or not motion_file:
        raise ValueError(f"motion file missing for {motion_spec}")

    motion_path = (model3_path.parent / motion_file).resolve()
    motion = read_json(motion_path)
    values: dict[str, float] = {}
    for curve in motion.get("Curves", []):
        if not isinstance(curve, dict) or curve.get("Target") != target:
            continue
        value_id = curve.get("Id")
        segments = curve.get("Segments")
        if not isinstance(value_id, str) or not isinstance(segments, list) or len(segments) < 2:
            continue
        values[value_id] = float(segments[1])
    return values


def expression_parameters(model3_path: Path, expression_name: str) -> list[dict[str, Any]]:
    if not expression_name:
        return []
    model3 = read_json(model3_path)
    expressions = model3.get("FileReferences", {}).get("Expressions", [])
    if not isinstance(expressions, list):
        raise ValueError("model3 Expressions must be a list")
    expression_file: str | None = None
    for expression in expressions:
        if not isinstance(expression, dict):
            continue
        if expression.get("Name") == expression_name:
            file_value = expression.get("File")
            if isinstance(file_value, str) and file_value:
                expression_file = file_value
            break
    if expression_file is None:
        raise ValueError(f"expression not found in model3: {expression_name}")
    expression = read_json((model3_path.parent / expression_file).resolve())
    parameters: list[dict[str, Any]] = []
    for item in expression.get("Parameters", []):
        if not isinstance(item, dict):
            continue
        parameter_id = item.get("Id")
        if not isinstance(parameter_id, str):
            continue
        parameters.append(
            {
                "Id": parameter_id,
                "Value": float(item.get("Value", 0.0)),
                "Blend": str(item.get("Blend", "Add")),
            }
        )
    return parameters


def find_local_browser_executable(candidates: tuple[Path, ...] = COMMON_BROWSER_EXECUTABLES) -> str | None:
    for candidate in candidates:
        if candidate and candidate.exists():
            return str(candidate)
    return None


def start_static_server(mappings: dict[str, Path]) -> tuple[ThreadingHTTPServer, str]:
    handler = functools.partial(QuietHandler, mappings=mappings)
    server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    host, port = server.server_address
    return server, f"http://{host}:{port}"


def extract_drawables(
    model3_path: Path,
    runtime_dir: Path,
    parameters: dict[str, float] | None = None,
    part_opacities: dict[str, float] | None = None,
    expressions: list[dict[str, Any]] | None = None,
    post_parameters: dict[str, float] | None = None,
) -> dict[str, Any]:
    """Extract drawable mesh data with the official Web runtime for offline conversion only.

    The extractor does not copy or redistribute the official runtime. Users provide
    their own licensed runtime through --runtime-dir. The embedded runtime consumes
    only generated EL2D asset data.
    """

    try:
        from playwright.sync_api import sync_playwright
    except ImportError as exc:
        raise RuntimeError("python package playwright is required for official drawable extraction") from exc

    model3_path = model3_path.resolve()
    runtime_dir = runtime_dir.resolve()
    mappings = {
        "/__el2d_model__": model3_path.parent,
        "/__el2d_runtime__": runtime_dir,
        "/__el2d_host__": HOST_PAGE.parent,
    }
    model_url_path = f"/__el2d_model__/{model3_path.name}"
    runtime_url_path = "/__el2d_runtime__"
    host_url_path = f"/__el2d_host__/{HOST_PAGE.name}"
    server, base_url = start_static_server(mappings)
    try:
        with sync_playwright() as playwright:
            launch_args: dict[str, Any] = {"headless": True}
            executable = find_local_browser_executable()
            if executable:
                launch_args["executable_path"] = executable
            browser = playwright.chromium.launch(**launch_args)
            try:
                page = browser.new_page(viewport={"width": 64, "height": 64})
                page.goto(f"{base_url}{host_url_path}?runtimeDir={runtime_url_path}", wait_until="load")
                page.wait_for_function("window.el2dExtractorReady === true", timeout=15000)
                return page.evaluate(
                    """async ({ modelUrl, modelName, parameters, partOpacities, expressions, postParameters }) => {
                        return await window.el2dExtractOfficialCubismDrawables({ modelUrl, modelName, parameters, partOpacities, expressions, postParameters });
                    }""",
                    {
                        "modelUrl": f"{base_url}{model_url_path}",
                        "modelName": model_name_from_model3(model3_path),
                        "parameters": parameters or {},
                        "partOpacities": part_opacities or {},
                        "expressions": expressions or [],
                        "postParameters": post_parameters or {},
                    },
                )
            finally:
                browser.close()
    finally:
        server.shutdown()
        server.server_close()


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Extract Cubism drawable mesh data into an EL2D Lite snapshot. "
            "This is an offline converter backend, not a frame streaming path."
        )
    )
    parser.add_argument("model3", type=Path, help="Path to a Cubism .model3.json file.")
    parser.add_argument("--runtime-dir", type=Path, required=True, help="Local official Cubism Web runtime directory.")
    parser.add_argument("--output", type=Path, required=True, help="Output drawable snapshot JSON.")
    parser.add_argument(
        "--parameter",
        action="append",
        default=[],
        metavar="ID=VALUE",
        help="Optional Cubism parameter override for this snapshot.",
    )
    parser.add_argument(
        "--motion-initial",
        metavar="GROUP[:INDEX]",
        help="Seed Cubism parameters from the initial keyframe of a model3 motion, for example Idle:0.",
    )
    parser.add_argument(
        "--expression",
        metavar="NAME",
        help="Apply a model3 expression after base parameters, for example F03.",
    )
    parser.add_argument(
        "--post-parameter",
        action="append",
        default=[],
        metavar="ID=VALUE",
        help="Optional Cubism parameter override applied after the expression.",
    )
    args = parser.parse_args()

    parameters: dict[str, float] = motion_initial_parameters(args.model3, args.motion_initial) if args.motion_initial else {}
    part_opacities: dict[str, float] = motion_initial_part_opacities(args.model3, args.motion_initial) if args.motion_initial else {}
    expressions = expression_parameters(args.model3, args.expression) if args.expression else []
    for item in args.parameter:
        if "=" not in item:
            raise SystemExit(f"invalid --parameter value: {item}")
        name, value = item.split("=", 1)
        parameters[name] = float(value)

    post_parameters: dict[str, float] = {}
    for item in args.post_parameter:
        if "=" not in item:
            raise SystemExit(f"invalid --post-parameter value: {item}")
        name, value = item.split("=", 1)
        post_parameters[name] = float(value)

    snapshot = extract_drawables(
        args.model3,
        args.runtime_dir,
        parameters,
        part_opacities,
        expressions,
        post_parameters,
    )
    write_json(args.output, snapshot)
    print(json.dumps({"output": args.output.as_posix(), "drawable_count": snapshot["drawable_count"]}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
