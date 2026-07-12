from __future__ import annotations

import argparse
import json
import math
import struct
import subprocess
import tempfile
from pathlib import Path
from typing import Any

from PIL import Image


def read_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"expected JSON object: {path}")
    return data


def c_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def symbol_suffix(value: str) -> str:
    chars: list[str] = []
    previous_underscore = False
    for char in value:
        if char.isalnum():
            chars.append(char.lower())
            previous_underscore = False
        elif not previous_underscore:
            chars.append("_")
            previous_underscore = True
    suffix = "".join(chars).strip("_")
    if not suffix:
        raise ValueError(f"invalid state snapshot name: {value!r}")
    if suffix[0].isdigit():
        suffix = f"state_{suffix}"
    return suffix


def state_model_symbol(base_symbol: str, state_name: str) -> str:
    return f"{base_symbol}_{symbol_suffix(state_name)}"


def parse_state_snapshot(value: str) -> tuple[str, Path]:
    if "=" not in value:
        raise ValueError("--state-snapshot must use NAME=PATH")
    name, path = value.split("=", 1)
    name = name.strip()
    if not name:
        raise ValueError("--state-snapshot name cannot be empty")
    return name, Path(path)


def rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def wrap_values(values: list[str], indent: str = "  ", width: int = 12) -> str:
    lines: list[str] = []
    for start in range(0, len(values), width):
        lines.append(indent + ", ".join(values[start : start + width]) + ",")
    return "\n".join(lines)


def format_float(value: float) -> str:
    if math.isfinite(value):
        return f"{value:.7g}f"
    raise ValueError(f"non-finite float in drawable asset: {value}")


def texture_arrays(path: Path, texture_size: int) -> tuple[list[int], list[int], int, int]:
    image = Image.open(path).convert("RGBA")
    if texture_size > 0 and (image.width > texture_size or image.height > texture_size):
        image.thumbnail((texture_size, texture_size), Image.Resampling.LANCZOS)
    width, height = image.size
    rgb_values: list[int] = []
    alpha4_values: list[int] = []
    pending_alpha: int | None = None
    pixels = image.get_flattened_data() if hasattr(image, "get_flattened_data") else image.getdata()
    for r, g, b, a in pixels:
        rgb_values.append(rgb565(r, g, b))
        alpha = max(0, min(15, int(round(a / 17))))
        if pending_alpha is None:
            pending_alpha = alpha << 4
        else:
            alpha4_values.append(pending_alpha | alpha)
            pending_alpha = None
    if pending_alpha is not None:
        alpha4_values.append(pending_alpha)
    return rgb_values, alpha4_values, width, height


def validate_drawable(drawable: dict[str, Any], index: int) -> None:
    vertex_count = int(drawable.get("vertex_count", 0))
    index_count = int(drawable.get("index_count", 0))
    positions = drawable.get("positions", [])
    uvs = drawable.get("uvs", [])
    indices = drawable.get("indices", [])
    if not isinstance(positions, list) or len(positions) != vertex_count * 2:
        raise ValueError(f"drawable {index} positions length mismatch")
    if not isinstance(uvs, list) or len(uvs) != vertex_count * 2:
        raise ValueError(f"drawable {index} uvs length mismatch")
    if not isinstance(indices, list) or len(indices) != index_count:
        raise ValueError(f"drawable {index} indices length mismatch")


def write_header(path: Path, symbol: str, extra_symbols: list[str] | None = None) -> None:
    guard = f"{symbol.upper()}_H"
    symbols = [symbol, *(extra_symbols or [])]
    externs = "\n".join(f"extern const el2d_mesh_model {item};" for item in symbols)
    text = f"""#ifndef {guard}
#define {guard}

#include "el2d/mesh_renderer.h"

#ifdef __cplusplus
extern "C" {{
#endif

{externs}

#ifdef __cplusplus
}}
#endif

#endif
"""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def validate_snapshot(snapshot: dict[str, Any]) -> list[dict[str, Any]]:
    drawables = snapshot.get("drawables", [])
    if not isinstance(drawables, list) or not drawables:
        raise ValueError("snapshot must contain at least one drawable")
    for index, drawable in enumerate(drawables):
        if not isinstance(drawable, dict):
            raise ValueError(f"drawable {index} must be an object")
        validate_drawable(drawable, index)
    return drawables


def triangle_count(drawable: dict[str, Any]) -> int:
    return int(drawable.get("index_count", 0)) // 3


def drawable_is_rendered(drawable: dict[str, Any]) -> bool:
    if str(drawable.get("id", "")).startswith("HitArea"):
        return False
    return bool(drawable.get("visible", True)) and float(drawable.get("opacity", 1.0)) > 0.0


def snapshot_rendered_triangle_count(snapshot: dict[str, Any]) -> int:
    return sum(triangle_count(drawable) for drawable in validate_snapshot(snapshot) if drawable_is_rendered(drawable))


def snapshot_total_triangle_count(snapshot: dict[str, Any]) -> int:
    return sum(triangle_count(drawable) for drawable in validate_snapshot(snapshot))


def drawable_bounds(drawable: dict[str, Any]) -> tuple[float, float, float, float]:
    positions = [float(value) for value in drawable["positions"]]
    xs = positions[0::2]
    ys = positions[1::2]
    return min(xs), max(xs), min(ys), max(ys)


def drawable_is_bust_region(drawable: dict[str, Any]) -> bool:
    min_x, max_x, _min_y, max_y = drawable_bounds(drawable)
    center_x = (min_x + max_x) * 0.5
    if max_y >= 0.55:
        return True
    return max_y >= 0.30 and abs(center_x) >= 0.05


def drawable_visible_in_any_snapshot(drawable_index: int, snapshots: list[dict[str, Any]]) -> bool:
    for snapshot in snapshots:
        drawables = validate_snapshot(snapshot)
        if drawable_index < len(drawables) and drawable_is_rendered(drawables[drawable_index]):
            return True
    return False


def drawable_matches_crop_profile(drawable_index: int, snapshots: list[dict[str, Any]], profile: str) -> bool:
    if profile == "none":
        return True
    if not drawable_visible_in_any_snapshot(drawable_index, snapshots):
        return False
    if profile == "bust":
        return any(
            drawable_index < len(validate_snapshot(snapshot)) and drawable_is_bust_region(validate_snapshot(snapshot)[drawable_index])
            for snapshot in snapshots
        )
    raise ValueError(f"unsupported crop profile: {profile}")


def apply_crop_profile(
    snapshot: dict[str, Any],
    state_snapshots: list[tuple[str, dict[str, Any]]],
    profile: str,
) -> tuple[dict[str, Any], list[tuple[str, dict[str, Any]]], dict[str, int]]:
    source_snapshots = [snapshot, *(state_snapshot for _, state_snapshot in state_snapshots)]
    base_drawables = validate_snapshot(snapshot)
    if profile == "none":
        return (
            snapshot,
            state_snapshots,
            {
                "original_drawables": len(base_drawables),
                "output_drawables": len(base_drawables),
                "original_triangles": snapshot_rendered_triangle_count(snapshot),
                "output_triangles": snapshot_rendered_triangle_count(snapshot),
                "original_total_triangles": snapshot_total_triangle_count(snapshot),
                "output_total_triangles": snapshot_total_triangle_count(snapshot),
            },
        )

    keep_indices = {
        index
        for index in range(len(base_drawables))
        if drawable_matches_crop_profile(index, source_snapshots, profile)
    }
    for snapshot_item in source_snapshots:
        drawables = validate_snapshot(snapshot_item)
        for index in list(keep_indices):
            if index >= len(drawables):
                continue
            for mask in drawables[index].get("masks", []) or []:
                mask_index = int(mask)
                if 0 <= mask_index < len(base_drawables):
                    keep_indices.add(mask_index)
    if not keep_indices:
        raise ValueError(f"crop profile {profile!r} removed all drawables")

    def transform(source: dict[str, Any]) -> dict[str, Any]:
        output = dict(source)
        output["drawables"] = [
            dict(drawable)
            for index, drawable in enumerate(validate_snapshot(source))
            if index in keep_indices
        ]
        output["drawable_count"] = len(output["drawables"])
        return output

    cropped_snapshot = transform(snapshot)
    cropped_state_snapshots = [(name, transform(state_snapshot)) for name, state_snapshot in state_snapshots]
    return (
        cropped_snapshot,
        cropped_state_snapshots,
        {
            "original_drawables": len(base_drawables),
            "output_drawables": len(validate_snapshot(cropped_snapshot)),
            "original_triangles": snapshot_rendered_triangle_count(snapshot),
            "output_triangles": snapshot_rendered_triangle_count(cropped_snapshot),
            "original_total_triangles": snapshot_total_triangle_count(snapshot),
            "output_total_triangles": snapshot_total_triangle_count(cropped_snapshot),
        },
    )


def drawable_lod_target_triangles(drawable: dict[str, Any], mask_source_indices: set[int], profile: str) -> int:
    original = triangle_count(drawable)
    if profile == "none" or original <= 12:
        return original
    if str(drawable.get("id", "")).startswith("HitArea"):
        return min(original, 2)
    order = int(drawable.get("render_order", drawable.get("index", 0)))
    if 24 <= order <= 68:
        ratio = 0.60
    elif order < 24:
        ratio = 0.40
    else:
        ratio = 0.50
    if int(drawable.get("index", -1)) in mask_source_indices or drawable.get("masks"):
        ratio = max(ratio, 0.70)
    return min(original, max(8, int(math.ceil(original * ratio))))


def barycentric_weights(
    px: float,
    py: float,
    ax: float,
    ay: float,
    bx: float,
    by: float,
    cx: float,
    cy: float,
) -> tuple[float, float, float] | None:
    denom = ((by - cy) * (ax - cx)) + ((cx - bx) * (ay - cy))
    if abs(denom) < 1e-12:
        return None
    w0 = (((by - cy) * (px - cx)) + ((cx - bx) * (py - cy))) / denom
    w1 = (((cy - ay) * (px - cx)) + ((ax - cx) * (py - cy))) / denom
    w2 = 1.0 - w0 - w1
    return w0, w1, w2


def uv_triangle_mapping(
    drawable: dict[str, Any],
    u: float,
    v: float,
    *,
    allow_nearest: bool,
) -> tuple[int, int, int, tuple[float, float, float]] | None:
    uvs = [float(value) for value in drawable["uvs"]]
    indices = [int(value) for value in drawable["indices"]]
    nearest: tuple[float, tuple[int, int, int, tuple[float, float, float]]] | None = None
    for offset in range(0, len(indices), 3):
        i0, i1, i2 = indices[offset : offset + 3]
        u0, v0 = uvs[i0 * 2], uvs[i0 * 2 + 1]
        u1, v1 = uvs[i1 * 2], uvs[i1 * 2 + 1]
        u2, v2 = uvs[i2 * 2], uvs[i2 * 2 + 1]
        weights = barycentric_weights(u, v, u0, v0, u1, v1, u2, v2)
        if weights is None:
            continue
        if weights[0] >= -1e-5 and weights[1] >= -1e-5 and weights[2] >= -1e-5:
            return i0, i1, i2, weights
        if allow_nearest:
            cu = (u0 + u1 + u2) / 3.0
            cv = (v0 + v1 + v2) / 3.0
            distance = ((u - cu) * (u - cu)) + ((v - cv) * (v - cv))
            clipped = tuple(max(0.0, value) for value in weights)
            total = sum(clipped)
            if total <= 0.0:
                clipped = (1.0, 0.0, 0.0)
            else:
                clipped = tuple(value / total for value in clipped)
            candidate = (i0, i1, i2, clipped)
            if nearest is None or distance < nearest[0]:
                nearest = (distance, candidate)
    return nearest[1] if nearest is not None else None


def sample_position_at_uv(drawable: dict[str, Any], u: float, v: float) -> tuple[float, float]:
    mapping = uv_triangle_mapping(drawable, u, v, allow_nearest=True)
    positions = [float(value) for value in drawable["positions"]]
    if mapping is None:
        best_index = 0
        best_distance = float("inf")
        uvs = [float(value) for value in drawable["uvs"]]
        for index in range(int(drawable.get("vertex_count", 0))):
            du = u - uvs[index * 2]
            dv = v - uvs[index * 2 + 1]
            distance = (du * du) + (dv * dv)
            if distance < best_distance:
                best_distance = distance
                best_index = index
        return positions[best_index * 2], positions[best_index * 2 + 1]
    i0, i1, i2, weights = mapping
    x = (
        positions[i0 * 2] * weights[0]
        + positions[i1 * 2] * weights[1]
        + positions[i2 * 2] * weights[2]
    )
    y = (
        positions[i0 * 2 + 1] * weights[0]
        + positions[i1 * 2 + 1] * weights[1]
        + positions[i2 * 2 + 1] * weights[2]
    )
    return x, y


def grid_dimensions_for_target(drawable: dict[str, Any], target_triangles: int) -> tuple[int, int]:
    uvs = [float(value) for value in drawable["uvs"]]
    us = uvs[0::2]
    vs = uvs[1::2]
    width = max(us) - min(us)
    height = max(vs) - min(vs)
    aspect = width / height if height > 1e-6 else 1.0
    target_cells = max(1, int(math.ceil(target_triangles / 2.0)))
    columns = max(1, int(round(math.sqrt(target_cells * max(aspect, 0.1)))))
    rows = max(1, int(math.ceil(target_cells / columns)))
    return columns, rows


def build_grid_lod_plan(drawable: dict[str, Any], target_triangles: int) -> dict[str, Any]:
    original_triangles = triangle_count(drawable)
    if target_triangles >= original_triangles:
        return {"mode": "keep"}
    uvs = [float(value) for value in drawable["uvs"]]
    min_u, max_u = min(uvs[0::2]), max(uvs[0::2])
    min_v, max_v = min(uvs[1::2]), max(uvs[1::2])
    if max_u - min_u <= 1e-6 or max_v - min_v <= 1e-6:
        return {"mode": "keep"}
    columns, rows = grid_dimensions_for_target(drawable, target_triangles)
    lod_uvs: list[float] = []
    vertex_by_grid: dict[tuple[int, int], int] = {}
    indices: list[int] = []

    def vertex_index(grid_x: int, grid_y: int) -> int:
        key = (grid_x, grid_y)
        existing = vertex_by_grid.get(key)
        if existing is not None:
            return existing
        u = min_u + ((max_u - min_u) * (grid_x / columns))
        v = min_v + ((max_v - min_v) * (grid_y / rows))
        index = len(lod_uvs) // 2
        vertex_by_grid[key] = index
        lod_uvs.extend([u, v])
        return index

    for y in range(rows):
        for x in range(columns):
            center_u = min_u + ((max_u - min_u) * ((x + 0.5) / columns))
            center_v = min_v + ((max_v - min_v) * ((y + 0.5) / rows))
            if uv_triangle_mapping(drawable, center_u, center_v, allow_nearest=False) is None:
                continue
            i0 = vertex_index(x, y)
            i1 = vertex_index(x + 1, y)
            i2 = vertex_index(x, y + 1)
            i3 = vertex_index(x + 1, y + 1)
            indices.extend([i0, i1, i3, i0, i3, i2])
    if not indices or len(indices) >= int(drawable.get("index_count", 0)):
        return {"mode": "keep"}
    return {"mode": "grid", "uvs": lod_uvs, "indices": indices}


def apply_lod_plan_to_drawable(drawable: dict[str, Any], plan: dict[str, Any]) -> dict[str, Any]:
    if plan.get("mode") != "grid":
        return dict(drawable)
    lod_uvs = [float(value) for value in plan["uvs"]]
    positions: list[float] = []
    for offset in range(0, len(lod_uvs), 2):
        x, y = sample_position_at_uv(drawable, lod_uvs[offset], lod_uvs[offset + 1])
        positions.extend([x, y])
    output = dict(drawable)
    output["positions"] = positions
    output["uvs"] = lod_uvs
    output["indices"] = [int(value) for value in plan["indices"]]
    output["vertex_count"] = len(lod_uvs) // 2
    output["index_count"] = len(output["indices"])
    return output


def drawable_boundary_vertices(drawable: dict[str, Any]) -> set[int]:
    edge_counts: dict[tuple[int, int], int] = {}
    indices = [int(value) for value in drawable["indices"]]
    for offset in range(0, len(indices), 3):
        i0, i1, i2 = indices[offset : offset + 3]
        for edge in ((i0, i1), (i1, i2), (i2, i0)):
            key = tuple(sorted(edge))
            edge_counts[key] = edge_counts.get(key, 0) + 1
    return {
        vertex
        for edge, count in edge_counts.items()
        if count == 1
        for vertex in edge
    }


def drawable_adaptive_triangle_floor(drawable: dict[str, Any]) -> int:
    original = triangle_count(drawable)
    if original <= 12:
        return original
    boundary_count = len(drawable_boundary_vertices(drawable))
    return min(original, max(1, boundary_count - 2))


def allocate_adaptive_triangle_targets(drawables: list[dict[str, Any]], target_total: int) -> list[int]:
    originals = [triangle_count(drawable) for drawable in drawables]
    original_total = sum(originals)
    if target_total <= 0:
        raise ValueError("adaptive LOD target must be positive")
    if original_total <= target_total:
        return originals
    floors = [drawable_adaptive_triangle_floor(drawable) for drawable in drawables]
    minimum_total = sum(floors)
    if minimum_total > target_total:
        raise ValueError(
            f"adaptive LOD target {target_total} is below the boundary-preserving floor {minimum_total}"
        )

    def targets_for_ratio(ratio: float) -> list[int]:
        return [
            min(original, max(floor, int(math.ceil(original * ratio))))
            for original, floor in zip(originals, floors)
        ]

    low = 0.0
    high = 1.0
    selected = floors
    for _ in range(32):
        ratio = (low + high) * 0.5
        candidate = targets_for_ratio(ratio)
        if sum(candidate) <= target_total:
            selected = candidate
            low = ratio
        else:
            high = ratio

    remaining = target_total - sum(selected)
    while remaining > 0:
        candidates = [
            index
            for index, (current, original) in enumerate(zip(selected, originals))
            if current < original
        ]
        if not candidates:
            break
        candidates.sort(key=lambda index: (originals[index] - selected[index], originals[index]), reverse=True)
        for index in candidates:
            if remaining <= 0:
                break
            selected[index] += 1
            remaining -= 1
    return selected


def validate_state_drawable_topology(
    base_drawable: dict[str, Any],
    state_drawable: dict[str, Any],
    state_name: str,
) -> None:
    drawable_id = str(base_drawable.get("id", ""))
    if str(state_drawable.get("id", "")) != drawable_id:
        raise ValueError(f"state {state_name!r} drawable id mismatch: {drawable_id!r}")
    for field in ("vertex_count", "index_count"):
        if int(state_drawable.get(field, 0)) != int(base_drawable.get(field, 0)):
            raise ValueError(f"state {state_name!r} drawable {drawable_id!r} {field} mismatch")
    if [int(value) for value in state_drawable.get("indices", [])] != [
        int(value) for value in base_drawable.get("indices", [])
    ]:
        raise ValueError(f"state {state_name!r} drawable {drawable_id!r} indices mismatch")
    if [float(value) for value in state_drawable.get("uvs", [])] != [
        float(value) for value in base_drawable.get("uvs", [])
    ]:
        raise ValueError(f"state {state_name!r} drawable {drawable_id!r} UV mismatch")


def drawable_uv_weight(drawable: dict[str, Any]) -> float:
    uvs = [float(value) for value in drawable["uvs"]]
    indices = [int(value) for value in drawable["indices"]]
    total_area = 0.0
    for offset in range(0, len(indices), 3):
        i0, i1, i2 = indices[offset : offset + 3]
        u0, v0 = uvs[i0 * 2], uvs[i0 * 2 + 1]
        u1, v1 = uvs[i1 * 2], uvs[i1 * 2 + 1]
        u2, v2 = uvs[i2 * 2], uvs[i2 * 2 + 1]
        total_area += abs(((u1 - u0) * (v2 - v0)) - ((v1 - v0) * (u2 - u0))) * 0.5
    average_area = total_area / max(1, len(indices) // 3)
    if average_area <= 1e-12:
        return 20.0
    return min(100.0, max(10.0, 1.0 / math.sqrt(average_area)))


def build_adaptive_meshoptimizer_input(
    base_drawable: dict[str, Any],
    state_drawables: list[tuple[str, dict[str, Any]]],
) -> tuple[list[float], list[float], list[float], bytes]:
    vertex_count = int(base_drawable["vertex_count"])
    base_positions = [float(value) for value in base_drawable["positions"]]
    positions_3d = [
        component
        for index in range(vertex_count)
        for component in (base_positions[index * 2], base_positions[index * 2 + 1], 0.0)
    ]
    for state_name, state_drawable in state_drawables:
        validate_state_drawable_topology(base_drawable, state_drawable, state_name)

    uvs = [float(value) for value in base_drawable["uvs"]]
    attributes: list[float] = []
    for index in range(vertex_count):
        attributes.extend([uvs[index * 2], uvs[index * 2 + 1]])
        for _state_name, state_drawable in state_drawables:
            state_positions = state_drawable["positions"]
            attributes.extend(
                [float(state_positions[index * 2]), float(state_positions[index * 2 + 1])]
            )
    uv_weight = drawable_uv_weight(base_drawable)
    state_weight = 1.0 / math.sqrt(max(1, len(state_drawables)))
    weights = [uv_weight, uv_weight] + [state_weight] * (len(state_drawables) * 2)
    boundary = drawable_boundary_vertices(base_drawable)
    locks = bytes(1 if index in boundary else 0 for index in range(vertex_count))
    return positions_3d, attributes, weights, locks


def run_meshoptimizer_helper(
    helper: Path,
    drawable: dict[str, Any],
    state_drawables: list[tuple[str, dict[str, Any]]],
    target_triangles: int,
) -> tuple[list[int], float]:
    if not helper.is_file():
        raise ValueError(f"meshoptimizer helper does not exist: {helper}")
    positions, attributes, weights, locks = build_adaptive_meshoptimizer_input(drawable, state_drawables)
    vertex_count = int(drawable["vertex_count"])
    indices = [int(value) for value in drawable["indices"]]
    attribute_count = len(weights)
    if len(attributes) != vertex_count * attribute_count:
        raise ValueError(f"drawable {drawable.get('id')!r} adaptive attribute length mismatch")

    payload = bytearray(
        struct.pack(
            "<8sIIIIIf",
            b"EL2DMO1\0",
            vertex_count,
            len(indices),
            attribute_count,
            target_triangles * 3,
            1 << 6,
            3.402823e38,
        )
    )
    payload.extend(struct.pack(f"<{len(positions)}f", *positions))
    payload.extend(struct.pack(f"<{len(attributes)}f", *attributes))
    payload.extend(struct.pack(f"<{len(weights)}f", *weights))
    payload.extend(locks)
    payload.extend(struct.pack(f"<{len(indices)}I", *indices))

    with tempfile.TemporaryDirectory(prefix="el2d-meshopt-") as temporary:
        request_path = Path(temporary) / "request.bin"
        response_path = Path(temporary) / "response.bin"
        request_path.write_bytes(payload)
        result = subprocess.run(
            [str(helper), str(request_path), str(response_path)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if result.returncode != 0:
            message = result.stderr.strip() or result.stdout.strip() or f"exit code {result.returncode}"
            raise ValueError(f"meshoptimizer helper failed for {drawable.get('id')!r}: {message}")
        response = response_path.read_bytes()

    header_size = struct.calcsize("<8sIf")
    if len(response) < header_size:
        raise ValueError("meshoptimizer helper returned a truncated response")
    magic, output_index_count, result_error = struct.unpack_from("<8sIf", response)
    if magic != b"EL2DMR1\0" or output_index_count == 0 or output_index_count % 3 != 0:
        raise ValueError("meshoptimizer helper returned an invalid response header")
    expected_size = header_size + (output_index_count * 4)
    if len(response) != expected_size:
        raise ValueError("meshoptimizer helper returned an invalid response size")
    output_indices = list(struct.unpack_from(f"<{output_index_count}I", response, header_size))
    if max(output_indices) >= vertex_count:
        raise ValueError("meshoptimizer helper returned an out-of-range vertex index")
    return output_indices, float(result_error)


def compact_drawable_to_indices(drawable: dict[str, Any], indices: list[int]) -> dict[str, Any]:
    used_vertices = sorted(set(indices))
    remap = {source: output for output, source in enumerate(used_vertices)}
    positions = [float(value) for value in drawable["positions"]]
    uvs = [float(value) for value in drawable["uvs"]]
    output = dict(drawable)
    output["positions"] = [
        component
        for index in used_vertices
        for component in (positions[index * 2], positions[index * 2 + 1])
    ]
    output["uvs"] = [
        component
        for index in used_vertices
        for component in (uvs[index * 2], uvs[index * 2 + 1])
    ]
    output["indices"] = [remap[index] for index in indices]
    output["vertex_count"] = len(used_vertices)
    output["index_count"] = len(indices)
    return output


def apply_adaptive_lod_profile(
    snapshot: dict[str, Any],
    state_snapshots: list[tuple[str, dict[str, Any]]],
    target_triangles: int,
    helper: Path,
) -> tuple[dict[str, Any], list[tuple[str, dict[str, Any]]], dict[str, Any]]:
    base_drawables = validate_snapshot(snapshot)
    state_drawable_lists = [
        (name, validate_snapshot(state_snapshot))
        for name, state_snapshot in state_snapshots
    ]
    for state_name, state_drawables in state_drawable_lists:
        if len(state_drawables) != len(base_drawables):
            raise ValueError(f"state {state_name!r} drawable count mismatch")

    targets = allocate_adaptive_triangle_targets(base_drawables, target_triangles)
    plans: list[dict[str, Any]] = []
    for index, (drawable, target) in enumerate(zip(base_drawables, targets)):
        original = triangle_count(drawable)
        state_drawables = [
            (state_name, drawables[index])
            for state_name, drawables in state_drawable_lists
        ]
        for state_name, state_drawable in state_drawables:
            validate_state_drawable_topology(drawable, state_drawable, state_name)
        if target >= original:
            output_indices = [int(value) for value in drawable["indices"]]
            result_error = 0.0
        else:
            output_indices, result_error = run_meshoptimizer_helper(
                helper,
                drawable,
                state_drawables,
                target,
            )
        plans.append(
            {
                "indices": output_indices,
                "original_triangles": original,
                "target_triangles": target,
                "output_triangles": len(output_indices) // 3,
                "result_error": result_error,
            }
        )

    def transform(source: dict[str, Any]) -> dict[str, Any]:
        output = dict(source)
        output["drawables"] = [
            compact_drawable_to_indices(drawable, plans[index]["indices"])
            for index, drawable in enumerate(validate_snapshot(source))
        ]
        output["drawable_count"] = len(output["drawables"])
        return output

    lod_snapshot = transform(snapshot)
    lod_state_snapshots = [(name, transform(state_snapshot)) for name, state_snapshot in state_snapshots]
    output_total = snapshot_total_triangle_count(lod_snapshot)
    report_drawables = [
        {
            "id": str(drawable.get("id", "")),
            "original_triangles": plan["original_triangles"],
            "target_triangles": plan["target_triangles"],
            "output_triangles": plan["output_triangles"],
            "result_error": plan["result_error"],
            "boundary_vertices": len(drawable_boundary_vertices(drawable)),
        }
        for drawable, plan in zip(base_drawables, plans)
    ]
    report: dict[str, Any] = {
        "original_triangles": snapshot_rendered_triangle_count(snapshot),
        "output_triangles": snapshot_rendered_triangle_count(lod_snapshot),
        "original_total_triangles": snapshot_total_triangle_count(snapshot),
        "output_total_triangles": output_total,
        "target_triangles": target_triangles,
        "state_attribute_count": 2 + (len(state_snapshots) * 2),
        "budget_met": output_total <= target_triangles,
        "drawables": report_drawables,
    }
    if not report["budget_met"]:
        raise ValueError(
            f"adaptive LOD output {output_total} exceeds target {target_triangles} while preserving boundaries"
        )
    return lod_snapshot, lod_state_snapshots, report


def apply_lod_profile(
    snapshot: dict[str, Any],
    state_snapshots: list[tuple[str, dict[str, Any]]],
    profile: str,
    *,
    target_triangles: int = 0,
    meshoptimizer_helper: Path | None = None,
) -> tuple[dict[str, Any], list[tuple[str, dict[str, Any]]], dict[str, Any]]:
    if profile == "none":
        triangles = snapshot_rendered_triangle_count(snapshot)
        return snapshot, state_snapshots, {"original_triangles": triangles, "output_triangles": triangles}
    if profile == "adaptive":
        if meshoptimizer_helper is None:
            raise ValueError("adaptive LOD requires --meshoptimizer-helper")
        return apply_adaptive_lod_profile(
            snapshot,
            state_snapshots,
            target_triangles,
            meshoptimizer_helper,
        )
    base_drawables = validate_snapshot(snapshot)
    mask_source_indices = {
        int(mask)
        for drawable in base_drawables
        for mask in drawable.get("masks", []) or []
    }
    plans: dict[int, dict[str, Any]] = {}
    for index, drawable in enumerate(base_drawables):
        target = drawable_lod_target_triangles(drawable, mask_source_indices, profile)
        plans[index] = build_grid_lod_plan(drawable, target)

    def transform(source: dict[str, Any]) -> dict[str, Any]:
        output = dict(source)
        output["drawables"] = [
            apply_lod_plan_to_drawable(drawable, plans[index])
            for index, drawable in enumerate(validate_snapshot(source))
        ]
        output["drawable_count"] = len(output["drawables"])
        return output

    lod_snapshot = transform(snapshot)
    lod_state_snapshots = [(name, transform(state_snapshot)) for name, state_snapshot in state_snapshots]
    return (
        lod_snapshot,
        lod_state_snapshots,
        {
            "original_triangles": snapshot_rendered_triangle_count(snapshot),
            "output_triangles": snapshot_rendered_triangle_count(lod_snapshot),
        },
    )


def triangle_intersects_texel_cell(
    triangle: list[tuple[float, float]], texel_x: int, texel_y: int
) -> bool:
    half_extent = 0.5001
    axes = [
        (1.0, 0.0),
        (0.0, 1.0),
        (-(triangle[1][1] - triangle[0][1]), triangle[1][0] - triangle[0][0]),
        (-(triangle[2][1] - triangle[1][1]), triangle[2][0] - triangle[1][0]),
        (-(triangle[0][1] - triangle[2][1]), triangle[0][0] - triangle[2][0]),
    ]
    for axis_x, axis_y in axes:
        projections = [point_x * axis_x + point_y * axis_y for point_x, point_y in triangle]
        cell_center = texel_x * axis_x + texel_y * axis_y
        cell_radius = half_extent * (abs(axis_x) + abs(axis_y))
        if max(projections) < cell_center - cell_radius or min(projections) > cell_center + cell_radius:
            return False
    return True


def alpha4_at(alpha4: list[int], width: int, x: int, y: int) -> int:
    pixel_index = y * width + x
    pair = alpha4[pixel_index // 2]
    return pair >> 4 if pixel_index % 2 == 0 else pair & 0x0F


def triangle_alpha_modes(
    drawable: dict[str, Any], textures: list[tuple[list[int], int, int]]
) -> list[str]:
    texture_index = int(drawable.get("texture_index", 0))
    if texture_index < 0 or texture_index >= len(textures):
        return ["EL2D_TRIANGLE_ALPHA_MIXED"] * (int(drawable.get("index_count", 0)) // 3)
    alpha4, width, height = textures[texture_index]
    max_x = float(width - 1)
    max_y = float(height - 1)
    uvs = [float(value) for value in drawable["uvs"]]
    indices = [int(value) for value in drawable["indices"]]
    modes: list[str] = []
    for offset in range(0, len(indices), 3):
        triangle_indices = indices[offset : offset + 3]
        if len(triangle_indices) != 3 or any(index * 2 + 1 >= len(uvs) for index in triangle_indices):
            modes.append("EL2D_TRIANGLE_ALPHA_MIXED")
            continue
        triangle = [
            (uvs[index * 2] * max_x, (1.0 - uvs[index * 2 + 1]) * max_y)
            for index in triangle_indices
        ]
        x0 = max(0, min(width - 1, math.floor(min(point[0] for point in triangle) - 0.5)))
        x1 = max(0, min(width - 1, math.ceil(max(point[0] for point in triangle) + 0.5)))
        y0 = max(0, min(height - 1, math.floor(min(point[1] for point in triangle) - 0.5)))
        y1 = max(0, min(height - 1, math.ceil(max(point[1] for point in triangle) + 0.5)))
        sampled_alpha = [
            alpha4_at(alpha4, width, x, y)
            for y in range(y0, y1 + 1)
            for x in range(x0, x1 + 1)
            if triangle_intersects_texel_cell(triangle, x, y)
        ]
        if sampled_alpha and all(alpha == 0 for alpha in sampled_alpha):
            modes.append("EL2D_TRIANGLE_ALPHA_TRANSPARENT")
        elif sampled_alpha and all(alpha == 15 for alpha in sampled_alpha):
            modes.append("EL2D_TRIANGLE_ALPHA_OPAQUE")
        else:
            modes.append("EL2D_TRIANGLE_ALPHA_MIXED")
    return modes


def append_model(
    lines: list[str],
    symbol: str,
    snapshot: dict[str, Any],
    texture_array_symbol: str,
    texture_count_expr: str,
    texture_alpha_data: list[tuple[list[int], int, int]],
) -> None:
    drawables = validate_snapshot(snapshot)
    xs: list[float] = []
    ys: list[float] = []
    drawable_entries: list[str] = []
    sorted_drawables = sorted(drawables, key=lambda item: int(item.get("render_order", item.get("index", 0))))
    source_index_to_output = {
        int(drawable.get("index", out_index)): out_index
        for out_index, drawable in enumerate(sorted_drawables)
    }
    for out_index, drawable in enumerate(sorted_drawables):
        positions = [float(value) for value in drawable["positions"]]
        uvs = [float(value) for value in drawable["uvs"]]
        indices = [int(value) for value in drawable["indices"]]
        xs.extend(positions[0::2])
        ys.extend(positions[1::2])
        prefix = f"{symbol}_drawable_{out_index:02d}"
        lines.append(f"static const float {prefix}_positions[] = {{")
        lines.append(wrap_values([format_float(value) for value in positions], width=8))
        lines.append("};")
        lines.append("")
        lines.append(f"static const float {prefix}_uvs[] = {{")
        lines.append(wrap_values([format_float(value) for value in uvs], width=8))
        lines.append("};")
        lines.append("")
        lines.append(f"static const uint16_t {prefix}_indices[] = {{")
        lines.append(wrap_values([f"{value}u" for value in indices], width=16))
        lines.append("};")
        lines.append("")
        alpha_modes = triangle_alpha_modes(drawable, texture_alpha_data)
        lines.append(f"static const uint8_t {prefix}_triangle_alpha_modes[] = {{")
        lines.append(wrap_values(alpha_modes, width=8))
        lines.append("};")
        lines.append("")
        raw_masks = drawable.get("masks", [])
        mask_indices: list[int] = []
        if isinstance(raw_masks, list):
            for mask in raw_masks:
                mask_index = source_index_to_output.get(int(mask))
                if mask_index is not None:
                    mask_indices.append(mask_index)
        mask_pointer = "0"
        if mask_indices:
            lines.append(f"static const uint16_t {prefix}_masks[] = {{")
            lines.append(wrap_values([f"{value}u" for value in mask_indices], width=16))
            lines.append("};")
            lines.append("")
            mask_pointer = f"{prefix}_masks"
        opacity = max(0, min(255, int(round(float(drawable.get("opacity", 1.0)) * 255))))
        visible = 1 if bool(drawable.get("visible", True)) else 0
        drawable_entries.append(
            "  { "
            f"\"{c_string(str(drawable.get('id', f'drawable_{out_index}')))}\", "
            f"{int(drawable.get('texture_index', 0))}u, "
            f"{int(drawable.get('render_order', out_index))}, "
            f"{visible}u, "
            f"{opacity}u, "
            f"{int(drawable.get('vertex_count', 0))}u, "
            f"{int(drawable.get('index_count', 0))}u, "
            f"{prefix}_positions, {prefix}_uvs, {prefix}_indices, "
            f"{len(mask_indices)}u, {mask_pointer}, {prefix}_triangle_alpha_modes"
            " },"
        )

    lines.append(f"static const el2d_mesh_drawable {symbol}_drawables[] = {{")
    lines.extend(drawable_entries)
    lines.append("};")
    lines.append("")
    lines.append(f"const el2d_mesh_model {symbol} = {{")
    lines.append(f"  {texture_array_symbol},")
    lines.append(f"  {texture_count_expr},")
    lines.append(f"  {symbol}_drawables,")
    lines.append(f"  sizeof({symbol}_drawables) / sizeof({symbol}_drawables[0]),")
    lines.append(f"  {format_float(min(xs))},")
    lines.append(f"  {format_float(max(xs))},")
    lines.append(f"  {format_float(min(ys))},")
    lines.append(f"  {format_float(max(ys))},")
    lines.append("};")
    lines.append("")


def write_source(
    path: Path,
    header_name: str,
    symbol: str,
    snapshot: dict[str, Any],
    texture_paths: list[Path],
    texture_size: int,
    state_snapshots: list[tuple[str, dict[str, Any]]] | None = None,
) -> None:
    validate_snapshot(snapshot)
    for _, state_snapshot in state_snapshots or []:
        validate_snapshot(state_snapshot)

    lines: list[str] = [
        f'#include "{header_name}"',
        "",
        "#include <stddef.h>",
        "",
    ]

    texture_entries: list[str] = []
    texture_alpha_data: list[tuple[list[int], int, int]] = []
    texture_array_symbol = f"{symbol}_textures"
    for index, texture_path in enumerate(texture_paths):
        rgb_values, alpha_values, width, height = texture_arrays(texture_path, texture_size)
        rgb_name = f"{symbol}_texture_{index:02d}_rgb565"
        alpha_name = f"{symbol}_texture_{index:02d}_alpha4"
        lines.append(f"static const uint16_t {rgb_name}[] = {{")
        lines.append(wrap_values([f"0x{value:04x}" for value in rgb_values], width=12))
        lines.append("};")
        lines.append("")
        lines.append(f"static const uint8_t {alpha_name}[] = {{")
        lines.append(wrap_values([f"0x{value:02x}" for value in alpha_values], width=16))
        lines.append("};")
        lines.append("")
        texture_entries.append(f"  {{ {rgb_name}, {alpha_name}, {width}u, {height}u }},")
        texture_alpha_data.append((alpha_values, width, height))

    lines.append(f"static const el2d_mesh_texture {texture_array_symbol}[] = {{")
    lines.extend(texture_entries)
    lines.append("};")
    lines.append("")
    texture_count_expr = f"sizeof({texture_array_symbol}) / sizeof({texture_array_symbol}[0])"
    append_model(lines, symbol, snapshot, texture_array_symbol, texture_count_expr, texture_alpha_data)
    for state_name, state_snapshot in state_snapshots or []:
        append_model(lines, state_model_symbol(symbol, state_name), state_snapshot, texture_array_symbol, texture_count_expr, texture_alpha_data)

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate a C mesh asset for EL2D Lite from a drawable snapshot.")
    parser.add_argument("--snapshot", type=Path, required=True, help="Drawable snapshot JSON.")
    parser.add_argument("--texture", type=Path, action="append", required=True, help="Texture PNG path, in Cubism texture index order.")
    parser.add_argument("--state-snapshot", action="append", default=[], help="Additional state mesh snapshot as NAME=PATH.")
    parser.add_argument("--output-header", type=Path, required=True, help="Generated C header.")
    parser.add_argument("--output-source", type=Path, required=True, help="Generated C/C++ source.")
    parser.add_argument("--symbol", default="el2d_mesh_model_asset", help="Exported el2d_mesh_model symbol.")
    parser.add_argument("--texture-size", type=int, default=512, help="Maximum texture side length; 0 keeps source size.")
    parser.add_argument(
        "--lod-profile",
        choices=["none", "medium", "adaptive"],
        default="none",
        help="Optional PC-side mesh LOD profile. State snapshots keep topology compatible with the base model.",
    )
    parser.add_argument(
        "--lod-target-triangles",
        type=int,
        default=1850,
        help="Total triangle budget for the adaptive LOD profile.",
    )
    parser.add_argument(
        "--meshoptimizer-helper",
        type=Path,
        help="Path to the host el2d_mesh_simplify executable required by adaptive LOD.",
    )
    parser.add_argument(
        "--optimized-snapshot-dir",
        type=Path,
        help="Optional directory for the cropped and optimized base/state snapshots.",
    )
    parser.add_argument(
        "--crop-profile",
        choices=["none", "bust"],
        default="none",
        help="Optional drawable-level crop profile applied before LOD. The bust profile keeps upper body and side hands.",
    )
    args = parser.parse_args()

    snapshot = read_json(args.snapshot)
    parsed_state_paths = [parse_state_snapshot(item) for item in args.state_snapshot]
    state_snapshots = [(name, read_json(path)) for name, path in parsed_state_paths]
    snapshot, state_snapshots, crop_report = apply_crop_profile(snapshot, state_snapshots, args.crop_profile)
    snapshot, state_snapshots, lod_report = apply_lod_profile(
        snapshot,
        state_snapshots,
        args.lod_profile,
        target_triangles=args.lod_target_triangles,
        meshoptimizer_helper=args.meshoptimizer_helper,
    )
    if args.optimized_snapshot_dir is not None:
        args.optimized_snapshot_dir.mkdir(parents=True, exist_ok=True)
        (args.optimized_snapshot_dir / args.snapshot.name).write_text(
            json.dumps(snapshot, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        for (_state_name, state_path), (_output_name, state_snapshot) in zip(parsed_state_paths, state_snapshots):
            (args.optimized_snapshot_dir / state_path.name).write_text(
                json.dumps(state_snapshot, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
    state_symbols = [state_model_symbol(args.symbol, name) for name, _ in parsed_state_paths]
    write_header(args.output_header, args.symbol, state_symbols)
    write_source(args.output_source, args.output_header.name, args.symbol, snapshot, args.texture, args.texture_size, state_snapshots)
    print(
        json.dumps(
            {
                "header": args.output_header.as_posix(),
                "source": args.output_source.as_posix(),
                "symbol": args.symbol,
                "state_symbols": state_symbols,
                "crop_profile": args.crop_profile,
                "crop": crop_report,
                "lod_profile": args.lod_profile,
                "lod": lod_report,
            },
            ensure_ascii=False,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
