from __future__ import annotations

import importlib.util
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXTRACTOR = ROOT / "tools" / "extract_official_cubism_drawables.py"
HOST_PAGE = ROOT / "tools" / "official_cubism_extractor.html"


def load_extractor_module():
    spec = importlib.util.spec_from_file_location("extract_official_cubism_drawables", EXTRACTOR)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_official_drawable_extractor_is_offline_converter_backend_not_streaming_path() -> None:
    extractor = EXTRACTOR.read_text(encoding="utf-8")
    host = HOST_PAGE.read_text(encoding="utf-8")
    combined = extractor + "\n" + host

    assert "Live2DModel.from" in combined
    assert "getDrawableVertexPositions" in combined
    assert "getDrawableVertexUvs" in combined
    assert "getDrawableVertexIndices" in combined
    assert "getDrawableTextureIndices" in combined
    assert "getDrawableRenderOrders" in combined
    assert "setPartOpacityById" in combined
    assert "addParameterValueById" in combined
    assert "multiplyParameterValueById" in combined
    assert "el2d-lite.drawable-snapshot" in combined
    assert "avatar-frame-raw" not in combined
    assert "captureRgb565Frame" not in combined
    assert "captureStreamFrame" not in combined


def test_official_drawable_extractor_documents_no_runtime_core_redistribution() -> None:
    extractor = EXTRACTOR.read_text(encoding="utf-8")

    assert "offline conversion only" in extractor
    assert "does not copy or redistribute the official runtime" in extractor
    assert "--runtime-dir" in extractor


def test_static_server_maps_only_explicit_public_roots(tmp_path: Path) -> None:
    module = load_extractor_module()
    model_root = tmp_path / "model"
    runtime_root = tmp_path / "runtime"
    model_root.mkdir()
    runtime_root.mkdir()
    mappings = {
        "/__el2d_model__": model_root,
        "/__el2d_runtime__": runtime_root,
    }

    assert module.resolve_mapped_path("/__el2d_model__/textures/00.png", mappings) == model_root / "textures" / "00.png"
    assert module.resolve_mapped_path("/__el2d_runtime__/core.js", mappings) == runtime_root / "core.js"
    assert module.resolve_mapped_path("/__el2d_model__/../secret.txt", mappings) is None
    assert module.resolve_mapped_path("/unmapped/file.txt", mappings) is None


def test_motion_initial_parameters_are_read_from_model3_motion_reference(tmp_path: Path) -> None:
    module = load_extractor_module()
    model_dir = tmp_path / "model"
    motion_dir = model_dir / "motions"
    motion_dir.mkdir(parents=True)
    model3 = model_dir / "Tiny.model3.json"
    motion = motion_dir / "idle.motion3.json"
    model3.write_text(
        json.dumps(
            {
                "FileReferences": {
                    "Motions": {
                        "Idle": [
                            {
                                "File": "motions/idle.motion3.json",
                            }
                        ]
                    }
                }
            }
        ),
        encoding="utf-8",
    )
    motion.write_text(
        json.dumps(
            {
                "Curves": [
                    {"Target": "Parameter", "Id": "ParamArmLA", "Segments": [0, 1, 1, 2, 1]},
                    {"Target": "Parameter", "Id": "ParamArmRA", "Segments": [0, 0.75, 1, 2, 0.75]},
                    {"Target": "PartOpacity", "Id": "Part01ArmLA001", "Segments": [0, 1, 2, 2, 1]},
                ]
            }
        ),
        encoding="utf-8",
    )

    assert module.motion_initial_parameters(model3, "Idle:0") == {
        "ParamArmLA": 1.0,
        "ParamArmRA": 0.75,
    }
    assert module.motion_initial_part_opacities(model3, "Idle:0") == {
        "Part01ArmLA001": 1.0,
    }
    assert "--motion-initial" in EXTRACTOR.read_text(encoding="utf-8")


def test_expression_parameters_are_read_from_model3_expression_reference(tmp_path: Path) -> None:
    module = load_extractor_module()
    model_dir = tmp_path / "model"
    expression_dir = model_dir / "expressions"
    expression_dir.mkdir(parents=True)
    model3 = model_dir / "Tiny.model3.json"
    expression = expression_dir / "angry.exp3.json"
    model3.write_text(
        json.dumps(
            {
                "FileReferences": {
                    "Expressions": [
                        {
                            "Name": "angry",
                            "File": "expressions/angry.exp3.json",
                        }
                    ]
                }
            }
        ),
        encoding="utf-8",
    )
    expression.write_text(
        json.dumps(
            {
                "Parameters": [
                    {"Id": "ParamBrowLY", "Value": -1, "Blend": "Add"},
                    {"Id": "ParamEyeLOpen", "Value": 0.8, "Blend": "Multiply"},
                ]
            }
        ),
        encoding="utf-8",
    )

    assert module.expression_parameters(model3, "angry") == [
        {"Id": "ParamBrowLY", "Value": -1.0, "Blend": "Add"},
        {"Id": "ParamEyeLOpen", "Value": 0.8, "Blend": "Multiply"},
    ]
    assert "--expression" in EXTRACTOR.read_text(encoding="utf-8")


def test_post_parameters_are_applied_after_expression_before_core_update() -> None:
    extractor = EXTRACTOR.read_text(encoding="utf-8")
    host = HOST_PAGE.read_text(encoding="utf-8")

    assert '"--post-parameter"' in extractor
    assert 'post_parameters: dict[str, float] | None = None' in extractor
    assert '"postParameters": post_parameters or {}' in extractor
    expression_at = host.index("for (const expression of options.expressions || [])")
    post_at = host.index("for (const [id, value] of Object.entries(options.postParameters || {}))")
    update_at = host.index("core.update()")
    assert expression_at < post_at < update_at
