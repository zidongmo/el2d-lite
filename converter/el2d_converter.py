from __future__ import annotations

import argparse
import json
import math
import shutil
import struct
from pathlib import Path
from typing import Any


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
DEFAULT_PROFILE: dict[str, Any] = {
    "canvas": {"width": 240, "height": 216},
    "texture": {"max_size": 512, "format": "rgb565"},
    "drawable_extraction": {"backend": "purismcore", "status": "deferred"},
}


def read_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"expected JSON object: {path}")
    return data


def write_json(path: Path, data: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def read_png_size(path: Path) -> tuple[int, int]:
    with path.open("rb") as handle:
        signature = handle.read(8)
        if signature != PNG_SIGNATURE:
            raise ValueError(f"not a PNG file: {path}")
        length = handle.read(4)
        chunk_type = handle.read(4)
        if len(length) != 4 or chunk_type != b"IHDR":
            raise ValueError(f"missing PNG IHDR: {path}")
        header = handle.read(8)
        if len(header) != 8:
            raise ValueError(f"truncated PNG IHDR: {path}")
        return struct.unpack(">II", header)


def model_name_from_model3(path: Path) -> str:
    suffix = ".model3.json"
    if path.name.endswith(suffix):
        return path.name[: -len(suffix)]
    return path.stem


def as_relative(path: Path, root: Path) -> str:
    return path.resolve().relative_to(root.resolve()).as_posix()


def merge_profile(profile_path: Path | None) -> dict[str, Any]:
    profile = json.loads(json.dumps(DEFAULT_PROFILE))
    if profile_path is None:
        return profile

    override = read_json(profile_path)
    for key, value in override.items():
        if isinstance(value, dict) and isinstance(profile.get(key), dict):
            profile[key].update(value)
        else:
            profile[key] = value
    return profile


def fit_size(width: int, height: int, max_size: int) -> tuple[int, int]:
    if width <= max_size and height <= max_size:
        return width, height
    scale = min(max_size / width, max_size / height)
    return max(1, math.floor(width * scale)), max(1, math.floor(height * scale))


def collect_groups(model3: dict[str, Any]) -> dict[str, list[str]]:
    groups: dict[str, list[str]] = {}
    for group in model3.get("Groups", []):
        if not isinstance(group, dict) or group.get("Target") != "Parameter":
            continue
        name = str(group.get("Name", ""))
        ids = [str(item) for item in group.get("Ids", [])]
        if name and ids:
            groups[name] = ids
    return groups


def collect_expression_ids(root: Path, expressions: list[Any]) -> tuple[list[dict[str, Any]], set[str]]:
    reports: list[dict[str, Any]] = []
    parameter_ids: set[str] = set()
    for expression in expressions:
        if not isinstance(expression, dict):
            continue
        file_name = str(expression.get("File", ""))
        if not file_name:
            continue
        expression_path = root / file_name
        expression_data = read_json(expression_path)
        ids: list[str] = []
        for parameter in expression_data.get("Parameters", []):
            if not isinstance(parameter, dict) or "Id" not in parameter:
                continue
            parameter_id = str(parameter["Id"])
            ids.append(parameter_id)
            parameter_ids.add(parameter_id)
        reports.append(
            {
                "name": str(expression.get("Name", expression_path.stem)),
                "file": file_name,
                "parameter_ids": ids,
            }
        )
    return reports, parameter_ids


def collect_motion_ids(root: Path, motions: dict[str, Any]) -> tuple[dict[str, Any], set[str]]:
    groups: dict[str, Any] = {}
    parameter_ids: set[str] = set()
    total = 0
    for group_name, entries in motions.items():
        if not isinstance(entries, list):
            continue
        group_entries: list[dict[str, Any]] = []
        for entry in entries:
            if not isinstance(entry, dict):
                continue
            file_name = str(entry.get("File", ""))
            if not file_name:
                continue
            motion_path = root / file_name
            motion_data = read_json(motion_path)
            ids: list[str] = []
            for curve in motion_data.get("Curves", []):
                if not isinstance(curve, dict) or curve.get("Target") != "Parameter" or "Id" not in curve:
                    continue
                parameter_id = str(curve["Id"])
                ids.append(parameter_id)
                parameter_ids.add(parameter_id)
            meta = motion_data.get("Meta", {}) if isinstance(motion_data.get("Meta", {}), dict) else {}
            group_entries.append(
                {
                    "file": file_name,
                    "duration": float(meta.get("Duration", 0.0)),
                    "fps": float(meta.get("Fps", 0.0)),
                    "parameter_ids": sorted(set(ids)),
                    "fade_in": float(entry.get("FadeInTime", 0.0)),
                    "fade_out": float(entry.get("FadeOutTime", 0.0)),
                }
            )
            total += 1
        groups[str(group_name)] = group_entries
    return {"group_count": len(groups), "total": total, "groups": groups}, parameter_ids


def texture_manifest(root: Path, textures: list[Any], profile: dict[str, Any]) -> tuple[list[dict[str, Any]], dict[str, int]]:
    max_size = int(profile["texture"]["max_size"])
    texture_format = str(profile["texture"]["format"])
    output: list[dict[str, Any]] = []
    source_rgb565 = 0
    source_rgba8888 = 0
    target_rgb565 = 0
    for index, texture in enumerate(textures):
        file_name = str(texture)
        texture_path = root / file_name
        width, height = read_png_size(texture_path)
        target_width, target_height = fit_size(width, height, max_size)
        source_rgb565 += width * height * 2
        source_rgba8888 += width * height * 4
        target_rgb565 += target_width * target_height * 2
        output.append(
            {
                "index": index,
                "source": {
                    "file": file_name,
                    "bytes": texture_path.stat().st_size,
                    "width": width,
                    "height": height,
                },
                "target": {
                    "file": f"textures/texture_{index:02d}.{texture_format}.bin",
                    "width": target_width,
                    "height": target_height,
                    "format": texture_format,
                    "bytes": target_width * target_height * 2 if texture_format == "rgb565" else target_width * target_height * 4,
                },
            }
        )
    return output, {
        "source_texture_bytes_rgb565": source_rgb565,
        "source_texture_bytes_rgba8888": source_rgba8888,
        "target_texture_bytes_rgb565": target_rgb565,
    }


def parameter_manifest(groups: dict[str, list[str]], expression_ids: set[str], motion_ids: set[str]) -> list[dict[str, Any]]:
    sources: dict[str, set[str]] = {}
    for group_name, ids in groups.items():
        for parameter_id in ids:
            sources.setdefault(parameter_id, set()).add(f"group:{group_name}")
    for parameter_id in expression_ids:
        sources.setdefault(parameter_id, set()).add("expression")
    for parameter_id in motion_ids:
        sources.setdefault(parameter_id, set()).add("motion")
    return [{"id": parameter_id, "sources": sorted(source_names)} for parameter_id, source_names in sorted(sources.items())]


def control_manifest(groups: dict[str, list[str]]) -> dict[str, Any]:
    return {
        "mouth_open": {
            "parameter_ids": groups.get("LipSync", []),
        },
        "eye_open": {
            "parameter_ids": groups.get("EyeBlink", []),
        },
    }


def normalize_drawable_summary(snapshot: dict[str, Any]) -> list[dict[str, Any]]:
    drawables = snapshot.get("drawables", [])
    if not isinstance(drawables, list) or not drawables:
        raise ValueError("drawable snapshot must contain at least one drawable")

    summary: list[dict[str, Any]] = []
    for index, drawable in enumerate(drawables):
        if not isinstance(drawable, dict):
            raise ValueError(f"drawable snapshot item must be an object: {index}")
        positions = drawable.get("positions", [])
        uvs = drawable.get("uvs", [])
        indices = drawable.get("indices", [])
        vertex_count = int(drawable.get("vertex_count", len(positions) // 2))
        index_count = int(drawable.get("index_count", len(indices)))
        if not isinstance(positions, list) or len(positions) != vertex_count * 2:
            raise ValueError(f"drawable {index} positions length does not match vertex_count")
        if not isinstance(uvs, list) or len(uvs) != vertex_count * 2:
            raise ValueError(f"drawable {index} uvs length does not match vertex_count")
        if not isinstance(indices, list) or len(indices) != index_count:
            raise ValueError(f"drawable {index} indices length does not match index_count")
        masks = drawable.get("masks", [])
        summary.append(
            {
                "index": int(drawable.get("index", index)),
                "id": str(drawable.get("id", f"drawable_{index}")),
                "texture_index": int(drawable.get("texture_index", -1)),
                "render_order": int(drawable.get("render_order", index)),
                "vertex_count": vertex_count,
                "index_count": index_count,
                "opacity": float(drawable.get("opacity", 1.0)),
                "mask_count": len(masks) if isinstance(masks, list) else 0,
            }
        )
    return summary


def load_drawable_snapshot(path: Path, model_name: str) -> dict[str, Any]:
    snapshot = read_json(path)
    snapshot_model_name = str(snapshot.get("model_name", model_name))
    if snapshot_model_name != model_name:
        raise ValueError(f"drawable snapshot model_name {snapshot_model_name!r} does not match {model_name!r}")
    normalize_drawable_summary(snapshot)
    return snapshot


def build_manifest(
    model3_path: Path,
    profile_path: Path | None = None,
    drawable_snapshot_path: Path | None = None,
) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any] | None]:
    model3_path = model3_path.resolve()
    root = model3_path.parent
    model3 = read_json(model3_path)
    profile = merge_profile(profile_path)
    references = model3.get("FileReferences", {})
    if not isinstance(references, dict):
        raise ValueError(f"missing FileReferences: {model3_path}")

    moc_file = str(references.get("Moc", ""))
    if not moc_file:
        raise ValueError(f"missing Moc reference: {model3_path}")
    moc_path = root / moc_file

    expressions = references.get("Expressions", [])
    if not isinstance(expressions, list):
        expressions = []
    motions = references.get("Motions", {})
    if not isinstance(motions, dict):
        motions = {}
    textures = references.get("Textures", [])
    if not isinstance(textures, list):
        textures = []

    groups = collect_groups(model3)
    expression_reports, expression_ids = collect_expression_ids(root, expressions)
    motion_reports, motion_ids = collect_motion_ids(root, motions)
    texture_reports, texture_budget = texture_manifest(root, textures, profile)
    parameters = parameter_manifest(groups, expression_ids, motion_ids)
    warnings: list[str] = []
    model_name = model_name_from_model3(model3_path)
    drawable_snapshot = load_drawable_snapshot(drawable_snapshot_path, model_name) if drawable_snapshot_path else None
    if drawable_snapshot is None:
        drawable_extraction = {
            "backend": str(profile["drawable_extraction"]["backend"]),
            "status": str(profile["drawable_extraction"]["status"]),
            "reason": "MOC3 drawable and mesh extraction is reserved for the PurismCore-backed converter gate.",
            "drawables": [],
        }
        metadata_only: bool | str = True
        mesh_deforms: bool | str = False
        deferred = [drawable_extraction["reason"]]
    else:
        drawable_summary = normalize_drawable_summary(drawable_snapshot)
        drawable_extraction = {
            "backend": str(drawable_snapshot.get("backend", "unknown")),
            "status": "ready",
            "file": "drawables.json",
            "coordinate_space": str(drawable_snapshot.get("coordinate_space", "cubism_normalized")),
            "drawable_count": len(drawable_summary),
            "drawables": drawable_summary,
        }
        metadata_only = False
        mesh_deforms = "snapshot"
        deferred = []

    manifest = {
        "format": "el2d-lite.el2d",
        "format_version": 1,
        "source": {
            "model_name": model_name,
            "model3": as_relative(model3_path, root),
            "root": root.as_posix(),
        },
        "target": {
            "canvas": {
                "width": int(profile["canvas"]["width"]),
                "height": int(profile["canvas"]["height"]),
            }
        },
        "moc": {
            "file": moc_file,
            "bytes": moc_path.stat().st_size,
        },
        "textures": texture_reports,
        "parameters": parameters,
        "controls": control_manifest(groups),
        "expressions": {
            "count": len(expression_reports),
            "items": expression_reports,
        },
        "motions": motion_reports,
        "drawable_extraction": drawable_extraction,
        "budget": {
            "moc_bytes": moc_path.stat().st_size,
            "texture_bytes_rgb565": texture_budget["target_texture_bytes_rgb565"],
            "texture_count": len(texture_reports),
        },
        "feature_flags": {
            "metadata_only": metadata_only,
            "texture_repack_plan": True,
            "mesh_deforms": mesh_deforms,
        },
    }
    report = {
        "model_name": manifest["source"]["model_name"],
        "warnings": warnings,
        "source_asset_budget": {
            "moc_bytes": moc_path.stat().st_size,
            "texture_bytes_rgb565": texture_budget["source_texture_bytes_rgb565"],
            "texture_bytes_rgba8888": texture_budget["source_texture_bytes_rgba8888"],
        },
        "target_budget": manifest["budget"],
        "deferred": deferred,
    }
    return manifest, report, drawable_snapshot


def convert_model(
    model3_path: Path,
    output: Path,
    profile_path: Path | None = None,
    drawable_snapshot_path: Path | None = None,
) -> dict[str, Any]:
    manifest, report, drawable_snapshot = build_manifest(model3_path, profile_path, drawable_snapshot_path)
    if output.exists():
        if output.is_dir():
            shutil.rmtree(output)
        else:
            output.unlink()
    output.mkdir(parents=True, exist_ok=True)
    write_json(output / "manifest.json", manifest)
    write_json(output / "report.json", report)
    generated_files = ["manifest.json", "report.json", "metadata.json"]
    if drawable_snapshot is not None:
        write_json(output / "drawables.json", drawable_snapshot)
        generated_files.append("drawables.json")
    write_json(
        output / "metadata.json",
        {
            "format": "el2d-lite.el2d.metadata",
            "format_version": 1,
            "model_name": manifest["source"]["model_name"],
            "generated_files": generated_files,
        },
    )
    return manifest


def c_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def first_parameter(manifest: dict[str, Any], control_name: str, index: int = 0) -> str:
    parameters = manifest["controls"].get(control_name, {}).get("parameter_ids", [])
    if not isinstance(parameters, list) or len(parameters) <= index:
        return ""
    return str(parameters[index])


def write_preview_header(path: Path, manifest: dict[str, Any]) -> None:
    model_name = c_string(str(manifest["source"]["model_name"]))
    mouth_parameter = c_string(first_parameter(manifest, "mouth_open"))
    left_eye_parameter = c_string(first_parameter(manifest, "eye_open", 0))
    right_eye_parameter = c_string(first_parameter(manifest, "eye_open", 1))
    canvas = manifest["target"]["canvas"]
    budget = manifest["budget"]
    text = f"""#ifndef EL2D_GENERATED_PREVIEW_CONFIG_H
#define EL2D_GENERATED_PREVIEW_CONFIG_H

#define EL2D_PREVIEW_MODEL_NAME \"{model_name}\"
#define EL2D_PREVIEW_CANVAS_WIDTH {int(canvas["width"])}
#define EL2D_PREVIEW_CANVAS_HEIGHT {int(canvas["height"])}
#define EL2D_PREVIEW_TEXTURE_BYTES_RGB565 {int(budget["texture_bytes_rgb565"])}
#define EL2D_PREVIEW_MOUTH_PARAMETER_ID \"{mouth_parameter}\"
#define EL2D_PREVIEW_LEFT_EYE_PARAMETER_ID \"{left_eye_parameter}\"
#define EL2D_PREVIEW_RIGHT_EYE_PARAMETER_ID \"{right_eye_parameter}\"

#endif
"""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert a Cubism model3 package into an EL2D Lite .el2d package.")
    parser.add_argument("model3", type=Path, help="Path to a model3.json file.")
    parser.add_argument("--output", type=Path, required=True, help="Output .el2d directory.")
    parser.add_argument("--profile", type=Path, help="Optional conversion profile JSON.")
    parser.add_argument("--drawable-snapshot", type=Path, help="Optional drawable snapshot generated by an extraction backend.")
    parser.add_argument("--preview-header", type=Path, help="Optional C header for the ESP-IDF preview example.")
    args = parser.parse_args()

    manifest = convert_model(args.model3, args.output, args.profile, args.drawable_snapshot)
    if args.preview_header:
        write_preview_header(args.preview_header, manifest)
    print(json.dumps({"output": args.output.as_posix(), "model_name": manifest["source"]["model_name"]}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
