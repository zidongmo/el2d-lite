from __future__ import annotations

import json
import struct
import subprocess
import sys
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = ROOT
GENERATOR = ROOT / "tools" / "generate_el2d_mesh_asset.py"


def find_mesh_simplifier() -> Path:
    executable = "el2d_mesh_simplify.exe" if sys.platform == "win32" else "el2d_mesh_simplify"
    candidates = (
        ROOT / "build" / executable,
        ROOT / "build" / "Release" / executable,
        ROOT / "build" / "Debug" / executable,
    )
    return next((path for path in candidates if path.is_file()), candidates[0])


MESH_SIMPLIFIER = find_mesh_simplifier()


def write_json(path: Path, data: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def dense_grid_drawable(*, state_offset: float = 0.0) -> dict[str, object]:
    columns = 5
    rows = 5
    positions: list[float] = []
    uvs: list[float] = []
    for y in range(rows + 1):
        for x in range(columns + 1):
            u = x / columns
            v = y / rows
            positions.extend([-1.0 + (2.0 * u) + state_offset, -1.0 + (2.0 * v)])
            uvs.extend([u, v])
    indices: list[int] = []
    for y in range(rows):
        for x in range(columns):
            i0 = y * (columns + 1) + x
            i1 = i0 + 1
            i2 = i0 + columns + 1
            i3 = i2 + 1
            indices.extend([i0, i1, i3, i0, i3, i2])
    return {
        "index": 0,
        "id": "D_Dense",
        "texture_index": 0,
        "render_order": 30,
        "opacity": 1.0,
        "culling": False,
        "blend_mode": 0,
        "visible": True,
        "vertex_count": len(positions) // 2,
        "index_count": len(indices),
        "positions": positions,
        "uvs": uvs,
        "indices": indices,
        "masks": [],
    }


def boundary_vertex_indices(drawable: dict[str, object]) -> set[int]:
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


def write_meshoptimizer_request(path: Path, drawable: dict[str, object], target_triangles: int) -> None:
    vertex_count = int(drawable["vertex_count"])
    indices = [int(value) for value in drawable["indices"]]
    positions_2d = [float(value) for value in drawable["positions"]]
    positions_3d = [
        component
        for index in range(vertex_count)
        for component in (positions_2d[index * 2], positions_2d[index * 2 + 1], 0.0)
    ]
    attributes = [float(value) for value in drawable["uvs"]]
    boundary = boundary_vertex_indices(drawable)
    locks = bytes(1 if index in boundary else 0 for index in range(vertex_count))
    payload = bytearray(
        struct.pack(
            "<8sIIIIIf",
            b"EL2DMO1\0",
            vertex_count,
            len(indices),
            2,
            target_triangles * 3,
            1 << 6,
            3.402823e38,
        )
    )
    payload.extend(struct.pack(f"<{len(positions_3d)}f", *positions_3d))
    payload.extend(struct.pack(f"<{len(attributes)}f", *attributes))
    payload.extend(struct.pack("<2f", 20.0, 20.0))
    payload.extend(locks)
    payload.extend(struct.pack(f"<{len(indices)}I", *indices))
    path.write_bytes(payload)


def read_meshoptimizer_response(path: Path) -> tuple[list[int], float]:
    payload = path.read_bytes()
    magic, index_count, result_error = struct.unpack_from("<8sIf", payload)
    assert magic == b"EL2DMR1\0"
    indices = list(struct.unpack_from(f"<{index_count}I", payload, struct.calcsize("<8sIf")))
    return indices, result_error


def test_meshoptimizer_helper_reduces_dense_grid_and_preserves_boundary(tmp_path: Path) -> None:
    drawable = dense_grid_drawable()
    request = tmp_path / "request.bin"
    response = tmp_path / "response.bin"
    write_meshoptimizer_request(request, drawable, target_triangles=32)

    result = subprocess.run(
        [str(MESH_SIMPLIFIER), str(request), str(response)],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    assert result.returncode == 0, result.stderr
    indices, result_error = read_meshoptimizer_response(response)
    assert 0 < len(indices) // 3 <= 32
    assert max(indices) < int(drawable["vertex_count"])
    assert boundary_vertex_indices(drawable) <= set(indices)
    assert result_error >= 0.0


def rectangular_drawable(
    drawable_id: str,
    *,
    index: int,
    render_order: int,
    min_x: float,
    max_x: float,
    min_y: float,
    max_y: float,
    visible: bool = True,
    opacity: float = 1.0,
    masks: list[int] | None = None,
) -> dict[str, object]:
    return {
        "index": index,
        "id": drawable_id,
        "texture_index": 0,
        "render_order": render_order,
        "opacity": opacity,
        "culling": False,
        "blend_mode": 0,
        "visible": visible,
        "vertex_count": 4,
        "index_count": 6,
        "positions": [min_x, min_y, max_x, min_y, max_x, max_y, min_x, max_y],
        "uvs": [0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0],
        "indices": [0, 1, 2, 0, 2, 3],
        "masks": masks or [],
    }


def snapshot_with_drawable(drawable: dict[str, object]) -> dict[str, object]:
    return {
        "format": "el2d-lite.drawable-snapshot",
        "format_version": 1,
        "backend": "test",
        "model_name": "Tiny",
        "coordinate_space": "cubism_normalized",
        "drawable_count": 1,
        "drawables": [drawable],
    }


def snapshot_with_drawables(drawables: list[dict[str, object]]) -> dict[str, object]:
    return {
        "format": "el2d-lite.drawable-snapshot",
        "format_version": 1,
        "backend": "test",
        "model_name": "Tiny",
        "coordinate_space": "cubism_normalized",
        "drawable_count": len(drawables),
        "drawables": drawables,
    }


def drawable_entry_counts(source_text: str, drawable_id: str) -> list[tuple[int, int]]:
    counts: list[tuple[int, int]] = []
    marker = f'{{ "{drawable_id}",'
    for line in source_text.splitlines():
        if marker not in line:
            continue
        parts = [part.strip() for part in line.split(",")]
        counts.append((int(parts[5].removesuffix("u")), int(parts[6].removesuffix("u"))))
    return counts


def drawable_entry_ids(source_text: str) -> list[str]:
    ids: list[str] = []
    for line in source_text.splitlines():
        stripped = line.strip()
        if not stripped.startswith('{ "'):
            continue
        ids.append(stripped.split('"', 2)[1])
    return ids


def test_mesh_asset_generator_emits_rgb565_alpha4_mesh_model(tmp_path: Path) -> None:
    texture = tmp_path / "texture_00.png"
    Image.new("RGBA", (1, 1), (255, 0, 0, 255)).save(texture)
    snapshot = tmp_path / "drawables.json"
    write_json(
        snapshot,
        {
            "format": "el2d-lite.drawable-snapshot",
            "format_version": 1,
            "backend": "test",
            "model_name": "Tiny",
            "coordinate_space": "cubism_normalized",
            "drawable_count": 1,
            "drawables": [
                {
                    "index": 0,
                    "id": "D_Tiny",
                    "texture_index": 0,
                    "render_order": 0,
                    "opacity": 1.0,
                    "culling": False,
                    "blend_mode": 0,
                    "visible": True,
                    "vertex_count": 3,
                    "index_count": 3,
                    "positions": [-1.0, -1.0, 1.0, -1.0, -1.0, 1.0],
                    "uvs": [0.0, 0.0, 1.0, 0.0, 0.0, 1.0],
                    "indices": [0, 1, 2],
                    "masks": [],
                }
            ],
        },
    )
    header = tmp_path / "tiny_asset.h"
    source = tmp_path / "tiny_asset.cpp"

    result = subprocess.run(
        [
            sys.executable,
            str(GENERATOR),
            "--snapshot",
            str(snapshot),
            "--texture",
            str(texture),
            "--output-header",
            str(header),
            "--output-source",
            str(source),
            "--symbol",
            "tiny_mesh_model",
            "--texture-size",
            "1",
        ],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    assert result.returncode == 0, result.stderr
    header_text = header.read_text(encoding="utf-8")
    source_text = source.read_text(encoding="utf-8")
    assert "extern const el2d_mesh_model tiny_mesh_model;" in header_text
    assert '#include "el2d/mesh_renderer.h"' in header_text
    assert "static const uint16_t tiny_mesh_model_texture_00_rgb565[]" in source_text
    assert "0xf800" in source_text
    assert "static const uint8_t tiny_mesh_model_texture_00_alpha4[]" in source_text
    assert "static const float tiny_mesh_model_drawable_00_positions[]" in source_text
    assert "static const uint8_t tiny_mesh_model_drawable_00_triangle_alpha_modes[]" in source_text
    assert "EL2D_TRIANGLE_ALPHA_OPAQUE" in source_text
    assert "static const el2d_mesh_drawable tiny_mesh_model_drawables[]" in source_text
    assert "const el2d_mesh_model tiny_mesh_model" in source_text
    assert "avatar-frame-raw" not in source_text


def test_mesh_asset_generator_can_emit_multiple_state_models_with_shared_textures(tmp_path: Path) -> None:
    texture = tmp_path / "texture_00.png"
    Image.new("RGBA", (1, 1), (255, 0, 0, 255)).save(texture)
    base_snapshot = tmp_path / "idle.drawables.json"
    happy_snapshot = tmp_path / "happy.drawables.json"
    snapshot_data = {
        "format": "el2d-lite.drawable-snapshot",
        "format_version": 1,
        "backend": "test",
        "model_name": "Tiny",
        "coordinate_space": "cubism_normalized",
        "drawable_count": 1,
        "drawables": [
            {
                "index": 0,
                "id": "D_Tiny",
                "texture_index": 0,
                "render_order": 0,
                "opacity": 1.0,
                "culling": False,
                "blend_mode": 0,
                "visible": True,
                "vertex_count": 3,
                "index_count": 3,
                "positions": [-1.0, -1.0, 1.0, -1.0, -1.0, 1.0],
                "uvs": [0.0, 0.0, 1.0, 0.0, 0.0, 1.0],
                "indices": [0, 1, 2],
                "masks": [],
            }
        ],
    }
    write_json(base_snapshot, snapshot_data)
    snapshot_data["drawables"][0]["positions"] = [-0.8, -1.0, 1.0, -1.0, -1.0, 1.0]
    write_json(happy_snapshot, snapshot_data)
    header = tmp_path / "tiny_asset.h"
    source = tmp_path / "tiny_asset.cpp"

    result = subprocess.run(
        [
            sys.executable,
            str(GENERATOR),
            "--snapshot",
            str(base_snapshot),
            "--state-snapshot",
            f"happy={happy_snapshot}",
            "--texture",
            str(texture),
            "--output-header",
            str(header),
            "--output-source",
            str(source),
            "--symbol",
            "tiny_mesh_model",
            "--texture-size",
            "1",
        ],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    assert result.returncode == 0, result.stderr
    header_text = header.read_text(encoding="utf-8")
    source_text = source.read_text(encoding="utf-8")
    assert "extern const el2d_mesh_model tiny_mesh_model;" in header_text
    assert "extern const el2d_mesh_model tiny_mesh_model_happy;" in header_text
    assert source_text.count("texture_00_rgb565[]") == 1
    assert "const el2d_mesh_model tiny_mesh_model =" in source_text
    assert "const el2d_mesh_model tiny_mesh_model_happy =" in source_text


def test_mesh_asset_generator_preserves_drawable_masks(tmp_path: Path) -> None:
    texture = tmp_path / "texture_00.png"
    Image.new("RGBA", (1, 1), (255, 255, 255, 255)).save(texture)
    snapshot = tmp_path / "masked.drawables.json"
    base_drawable = {
        "texture_index": 0,
        "visible": True,
        "opacity": 1.0,
        "culling": False,
        "blend_mode": 0,
        "vertex_count": 3,
        "index_count": 3,
        "positions": [-1.0, -1.0, 1.0, -1.0, -1.0, 1.0],
        "uvs": [0.0, 0.0, 1.0, 0.0, 0.0, 1.0],
        "indices": [0, 1, 2],
    }
    write_json(
        snapshot,
        {
            "format": "el2d-lite.drawable-snapshot",
            "format_version": 1,
            "backend": "test",
            "model_name": "Tiny",
            "coordinate_space": "cubism_normalized",
            "drawable_count": 2,
            "drawables": [
                {**base_drawable, "index": 10, "id": "D_Mask", "render_order": 0, "masks": []},
                {**base_drawable, "index": 20, "id": "D_Eye", "render_order": 1, "masks": [10]},
            ],
        },
    )
    header = tmp_path / "tiny_asset.h"
    source = tmp_path / "tiny_asset.cpp"

    result = subprocess.run(
        [
            sys.executable,
            str(GENERATOR),
            "--snapshot",
            str(snapshot),
            "--texture",
            str(texture),
            "--output-header",
            str(header),
            "--output-source",
            str(source),
            "--symbol",
            "tiny_mesh_model",
            "--texture-size",
            "1",
        ],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    assert result.returncode == 0, result.stderr
    source_text = source.read_text(encoding="utf-8")
    assert "static const uint16_t tiny_mesh_model_drawable_01_masks[] = {" in source_text
    assert "0u," in source_text
    assert "tiny_mesh_model_drawable_01_masks" in source_text


def test_mesh_asset_generator_lod_medium_reduces_dense_drawable_topology(tmp_path: Path) -> None:
    texture = tmp_path / "texture_00.png"
    Image.new("RGBA", (8, 8), (255, 255, 255, 255)).save(texture)
    snapshot = tmp_path / "dense.drawables.json"
    write_json(snapshot, snapshot_with_drawable(dense_grid_drawable()))
    header = tmp_path / "dense_asset.h"
    source = tmp_path / "dense_asset.cpp"

    result = subprocess.run(
        [
            sys.executable,
            str(GENERATOR),
            "--snapshot",
            str(snapshot),
            "--texture",
            str(texture),
            "--output-header",
            str(header),
            "--output-source",
            str(source),
            "--symbol",
            "dense_mesh_model",
            "--texture-size",
            "8",
            "--lod-profile",
            "medium",
        ],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    assert result.returncode == 0, result.stderr
    assert "DeprecationWarning" not in result.stderr
    report = json.loads(result.stdout)
    assert report["lod_profile"] == "medium"
    assert report["lod"]["original_triangles"] == 50
    assert 20 <= report["lod"]["output_triangles"] < 50
    source_text = source.read_text(encoding="utf-8")
    assert drawable_entry_counts(source_text, "D_Dense") == [(25, 96)]


def test_mesh_asset_generator_lod_medium_keeps_state_topology_compatible(tmp_path: Path) -> None:
    texture = tmp_path / "texture_00.png"
    Image.new("RGBA", (8, 8), (255, 255, 255, 255)).save(texture)
    base_snapshot = tmp_path / "base.drawables.json"
    happy_snapshot = tmp_path / "happy.drawables.json"
    write_json(base_snapshot, snapshot_with_drawable(dense_grid_drawable()))
    write_json(happy_snapshot, snapshot_with_drawable(dense_grid_drawable(state_offset=0.25)))
    header = tmp_path / "dense_asset.h"
    source = tmp_path / "dense_asset.cpp"

    result = subprocess.run(
        [
            sys.executable,
            str(GENERATOR),
            "--snapshot",
            str(base_snapshot),
            "--state-snapshot",
            f"happy={happy_snapshot}",
            "--texture",
            str(texture),
            "--output-header",
            str(header),
            "--output-source",
            str(source),
            "--symbol",
            "dense_mesh_model",
            "--texture-size",
            "8",
            "--lod-profile",
            "medium",
        ],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    assert result.returncode == 0, result.stderr
    source_text = source.read_text(encoding="utf-8")
    entries = [
        line
        for line in source_text.splitlines()
        if '"D_Dense", 0u, 30, 1u, 255u,' in line
    ]
    assert len(entries) == 2
    assert drawable_entry_counts(source_text, "D_Dense") == [(25, 96), (25, 96)]


def test_mesh_asset_generator_adaptive_lod_meets_budget_and_preserves_state_topology(tmp_path: Path) -> None:
    texture = tmp_path / "texture_00.png"
    Image.new("RGBA", (8, 8), (255, 255, 255, 255)).save(texture)
    base_snapshot = tmp_path / "base.drawables.json"
    happy_snapshot = tmp_path / "happy.drawables.json"
    base_drawable = dense_grid_drawable()
    happy_drawable = dense_grid_drawable(state_offset=0.25)
    write_json(base_snapshot, snapshot_with_drawable(base_drawable))
    write_json(happy_snapshot, snapshot_with_drawable(happy_drawable))
    header = tmp_path / "adaptive_asset.h"
    source = tmp_path / "adaptive_asset.cpp"
    optimized_dir = tmp_path / "optimized"

    result = subprocess.run(
        [
            sys.executable,
            str(GENERATOR),
            "--snapshot",
            str(base_snapshot),
            "--state-snapshot",
            f"happy={happy_snapshot}",
            "--texture",
            str(texture),
            "--output-header",
            str(header),
            "--output-source",
            str(source),
            "--symbol",
            "adaptive_mesh_model",
            "--texture-size",
            "8",
            "--lod-profile",
            "adaptive",
            "--lod-target-triangles",
            "32",
            "--meshoptimizer-helper",
            str(MESH_SIMPLIFIER),
            "--optimized-snapshot-dir",
            str(optimized_dir),
        ],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    assert result.returncode == 0, result.stderr
    report = json.loads(result.stdout)
    assert report["lod_profile"] == "adaptive"
    assert report["lod"]["target_triangles"] == 32
    assert report["lod"]["original_total_triangles"] == 50
    assert 0 < report["lod"]["output_total_triangles"] <= 32
    assert report["lod"]["state_attribute_count"] == 4
    assert report["lod"]["budget_met"] is True

    optimized_base = json.loads((optimized_dir / base_snapshot.name).read_text(encoding="utf-8"))
    optimized_happy = json.loads((optimized_dir / happy_snapshot.name).read_text(encoding="utf-8"))
    base_output = optimized_base["drawables"][0]
    happy_output = optimized_happy["drawables"][0]
    assert base_output["vertex_count"] == happy_output["vertex_count"]
    assert base_output["index_count"] == happy_output["index_count"]
    assert base_output["uvs"] == happy_output["uvs"]
    assert base_output["indices"] == happy_output["indices"]
    assert max(base_output["indices"]) < base_output["vertex_count"]

    source_positions = [float(value) for value in base_drawable["positions"]]
    source_uvs = [float(value) for value in base_drawable["uvs"]]
    boundary_signatures = {
        (
            source_positions[index * 2],
            source_positions[index * 2 + 1],
            source_uvs[index * 2],
            source_uvs[index * 2 + 1],
        )
        for index in boundary_vertex_indices(base_drawable)
    }
    output_signatures = {
        (
            base_output["positions"][index * 2],
            base_output["positions"][index * 2 + 1],
            base_output["uvs"][index * 2],
            base_output["uvs"][index * 2 + 1],
        )
        for index in range(base_output["vertex_count"])
    }
    assert boundary_signatures <= output_signatures


def test_mesh_asset_generator_adaptive_lod_rejects_kept_state_topology_mismatch(tmp_path: Path) -> None:
    texture = tmp_path / "texture_00.png"
    Image.new("RGBA", (8, 8), (255, 255, 255, 255)).save(texture)
    base_snapshot = tmp_path / "base.drawables.json"
    state_snapshot = tmp_path / "state.drawables.json"
    base_drawable = rectangular_drawable(
        "D_Small",
        index=0,
        render_order=0,
        min_x=-0.1,
        max_x=0.1,
        min_y=0.6,
        max_y=0.8,
    )
    state_drawable = dict(base_drawable)
    state_drawable["indices"] = [0, 2, 1, 0, 3, 2]
    write_json(base_snapshot, snapshot_with_drawable(base_drawable))
    write_json(state_snapshot, snapshot_with_drawable(state_drawable))

    result = subprocess.run(
        [
            sys.executable,
            str(GENERATOR),
            "--snapshot",
            str(base_snapshot),
            "--state-snapshot",
            f"changed={state_snapshot}",
            "--texture",
            str(texture),
            "--output-header",
            str(tmp_path / "asset.h"),
            "--output-source",
            str(tmp_path / "asset.cpp"),
            "--lod-profile",
            "adaptive",
            "--lod-target-triangles",
            "10",
            "--meshoptimizer-helper",
            str(MESH_SIMPLIFIER),
        ],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    assert result.returncode != 0
    assert "indices mismatch" in result.stderr


def test_mesh_asset_generator_bust_crop_keeps_upper_body_and_side_hands(tmp_path: Path) -> None:
    texture = tmp_path / "texture_00.png"
    Image.new("RGBA", (8, 8), (255, 255, 255, 255)).save(texture)
    snapshot = tmp_path / "bust.drawables.json"
    write_json(
        snapshot,
        snapshot_with_drawables(
            [
                rectangular_drawable("D_Legs", index=0, render_order=0, min_x=-0.2, max_x=0.2, min_y=-0.8, max_y=0.1),
                rectangular_drawable("D_Chest", index=1, render_order=1, min_x=-0.1, max_x=0.1, min_y=0.35, max_y=0.58),
                rectangular_drawable("D_Face", index=2, render_order=2, min_x=-0.1, max_x=0.1, min_y=0.6, max_y=0.85),
                rectangular_drawable("D_LeftHand", index=3, render_order=3, min_x=-0.18, max_x=-0.11, min_y=0.25, max_y=0.34),
                rectangular_drawable("D_HiddenUnused", index=4, render_order=4, min_x=0.0, max_x=0.1, min_y=0.7, max_y=0.8, visible=False, opacity=0.0),
            ]
        ),
    )
    header = tmp_path / "bust_asset.h"
    source = tmp_path / "bust_asset.cpp"

    result = subprocess.run(
        [
            sys.executable,
            str(GENERATOR),
            "--snapshot",
            str(snapshot),
            "--texture",
            str(texture),
            "--output-header",
            str(header),
            "--output-source",
            str(source),
            "--symbol",
            "bust_mesh_model",
            "--texture-size",
            "8",
            "--crop-profile",
            "bust",
        ],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    assert result.returncode == 0, result.stderr
    report = json.loads(result.stdout)
    assert report["crop_profile"] == "bust"
    assert report["crop"]["original_triangles"] == 8
    assert report["crop"]["output_triangles"] == 6
    source_text = source.read_text(encoding="utf-8")
    assert drawable_entry_ids(source_text) == ["D_Chest", "D_Face", "D_LeftHand"]


def test_mesh_asset_generator_bust_crop_keeps_masks_used_by_remaining_drawables(tmp_path: Path) -> None:
    texture = tmp_path / "texture_00.png"
    Image.new("RGBA", (8, 8), (255, 255, 255, 255)).save(texture)
    snapshot = tmp_path / "masked_bust.drawables.json"
    write_json(
        snapshot,
        snapshot_with_drawables(
            [
                rectangular_drawable("D_Legs", index=0, render_order=0, min_x=-0.2, max_x=0.2, min_y=-0.8, max_y=0.1),
                rectangular_drawable("D_EyeMask", index=1, render_order=1, min_x=-0.1, max_x=0.1, min_y=0.62, max_y=0.72, visible=False, opacity=0.0),
                rectangular_drawable("D_Eye", index=2, render_order=2, min_x=-0.08, max_x=0.08, min_y=0.63, max_y=0.71, masks=[1]),
            ]
        ),
    )
    header = tmp_path / "masked_bust_asset.h"
    source = tmp_path / "masked_bust_asset.cpp"

    result = subprocess.run(
        [
            sys.executable,
            str(GENERATOR),
            "--snapshot",
            str(snapshot),
            "--texture",
            str(texture),
            "--output-header",
            str(header),
            "--output-source",
            str(source),
            "--symbol",
            "masked_bust_mesh_model",
            "--texture-size",
            "8",
            "--crop-profile",
            "bust",
        ],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    assert result.returncode == 0, result.stderr
    source_text = source.read_text(encoding="utf-8")
    assert drawable_entry_ids(source_text) == ["D_EyeMask", "D_Eye"]
    assert "static const uint16_t masked_bust_mesh_model_drawable_01_masks[] = {" in source_text
