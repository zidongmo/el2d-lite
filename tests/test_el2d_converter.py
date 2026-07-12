from __future__ import annotations

import json
import struct
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = ROOT
CONVERTER = ROOT / "converter" / "el2d_converter.py"


def write_png_stub(path: Path, width: int, height: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + struct.pack(">I", 13) + b"IHDR" + struct.pack(">II", width, height) + b"\x08\x06\x00\x00\x00")


def write_json(path: Path, data: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def make_synthetic_model(tmp_path: Path) -> Path:
    model_dir = tmp_path / "synthetic"
    model_dir.mkdir(parents=True, exist_ok=True)
    (model_dir / "Synthetic.moc3").write_bytes(b"MOC3" + b"\0" * 28)
    write_png_stub(model_dir / "textures" / "texture_00.png", 1024, 512)
    write_json(
        model_dir / "motions" / "idle.motion3.json",
        {
            "Meta": {"Duration": 1.0, "Fps": 30.0},
            "Curves": [
                {"Target": "Parameter", "Id": "ParamMouthOpenY", "Segments": [0, 0, 1, 1]},
                {"Target": "Parameter", "Id": "ParamAngleX", "Segments": [0, 0, 1, 0.2]},
            ],
        },
    )
    write_json(
        model_dir / "expressions" / "happy.exp3.json",
        {
            "Type": "Live2D Expression",
            "Parameters": [
                {"Id": "ParamEyeLOpen", "Value": 1.0},
                {"Id": "ParamEyeROpen", "Value": 1.0},
            ],
        },
    )
    write_json(
        model_dir / "Synthetic.model3.json",
        {
            "Version": 3,
            "Groups": [
                {"Target": "Parameter", "Name": "EyeBlink", "Ids": ["ParamEyeLOpen", "ParamEyeROpen"]},
                {"Target": "Parameter", "Name": "LipSync", "Ids": ["ParamMouthOpenY"]},
            ],
            "FileReferences": {
                "Moc": "Synthetic.moc3",
                "Textures": ["textures/texture_00.png"],
                "Expressions": [{"Name": "happy", "File": "expressions/happy.exp3.json"}],
                "Motions": {"Idle": [{"File": "motions/idle.motion3.json"}]},
            },
        },
    )
    return model_dir / "Synthetic.model3.json"


def run_converter(model3: Path, output: Path, extra_args: list[str] | None = None) -> None:
    result = subprocess.run(
        [sys.executable, str(CONVERTER), str(model3), "--output", str(output), *(extra_args or [])],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    assert result.returncode == 0, result.stderr


def write_drawable_snapshot(path: Path, model_name: str = "Synthetic") -> None:
    write_json(
        path,
        {
            "format": "el2d-lite.drawable-snapshot",
            "format_version": 1,
            "backend": "official_cubism_web_runtime",
            "model_name": model_name,
            "coordinate_space": "cubism_normalized",
            "drawable_count": 1,
            "drawables": [
                {
                    "index": 0,
                    "id": "D_Test",
                    "texture_index": 0,
                    "render_order": 0,
                    "opacity": 1.0,
                    "culling": False,
                    "blend_mode": 0,
                    "visible": True,
                    "vertex_count": 3,
                    "index_count": 3,
                    "positions": [0.0, 0.0, 1.0, 0.0, 0.0, 1.0],
                    "uvs": [0.0, 0.0, 1.0, 0.0, 0.0, 1.0],
                    "indices": [0, 1, 2],
                    "masks": [],
                }
            ],
        },
    )


def run_converter_with_header(model3: Path, output: Path, header: Path) -> None:
    result = subprocess.run(
        [sys.executable, str(CONVERTER), str(model3), "--output", str(output), "--preview-header", str(header)],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    assert result.returncode == 0, result.stderr


def test_converter_emits_el2d_manifest_for_synthetic_model(tmp_path: Path) -> None:
    output = tmp_path / "synthetic.el2d"

    run_converter(make_synthetic_model(tmp_path), output)

    manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
    report = json.loads((output / "report.json").read_text(encoding="utf-8"))

    assert manifest["format"] == "el2d-lite.el2d"
    assert manifest["format_version"] == 1
    assert manifest["source"]["model_name"] == "Synthetic"
    assert manifest["target"]["canvas"] == {"width": 240, "height": 216}
    assert manifest["controls"]["mouth_open"]["parameter_ids"] == ["ParamMouthOpenY"]
    assert manifest["controls"]["eye_open"]["parameter_ids"] == ["ParamEyeLOpen", "ParamEyeROpen"]
    assert {item["id"] for item in manifest["parameters"]} >= {"ParamMouthOpenY", "ParamAngleX", "ParamEyeLOpen", "ParamEyeROpen"}
    assert manifest["textures"][0]["target"]["width"] == 512
    assert manifest["textures"][0]["target"]["format"] == "rgb565"
    assert manifest["drawable_extraction"]["status"] == "deferred"
    assert report["warnings"] == []


def test_converter_embeds_drawable_snapshot_for_local_runtime_package(tmp_path: Path) -> None:
    output = tmp_path / "synthetic.el2d"
    snapshot = tmp_path / "synthetic.drawables.json"
    write_drawable_snapshot(snapshot)

    run_converter(make_synthetic_model(tmp_path), output, ["--drawable-snapshot", str(snapshot)])

    manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
    metadata = json.loads((output / "metadata.json").read_text(encoding="utf-8"))
    drawables = json.loads((output / "drawables.json").read_text(encoding="utf-8"))

    assert manifest["drawable_extraction"]["status"] == "ready"
    assert manifest["drawable_extraction"]["backend"] == "official_cubism_web_runtime"
    assert manifest["drawable_extraction"]["drawable_count"] == 1
    assert manifest["drawable_extraction"]["file"] == "drawables.json"
    assert manifest["feature_flags"]["metadata_only"] is False
    assert manifest["feature_flags"]["mesh_deforms"] == "snapshot"
    assert metadata["generated_files"] == ["manifest.json", "report.json", "metadata.json", "drawables.json"]
    assert drawables["drawables"][0]["id"] == "D_Test"
    assert drawables["drawables"][0]["positions"] == [0.0, 0.0, 1.0, 0.0, 0.0, 1.0]


def test_converter_can_emit_preview_config_header_for_esp_idf(tmp_path: Path) -> None:
    model = make_synthetic_model(tmp_path)
    output = tmp_path / "synthetic.el2d"
    header = tmp_path / "el2d_generated_preview_config.h"

    run_converter_with_header(model, output, header)

    text = header.read_text(encoding="utf-8")
    assert "#define EL2D_PREVIEW_MODEL_NAME \"Synthetic\"" in text
    assert "#define EL2D_PREVIEW_CANVAS_WIDTH 240" in text
    assert "#define EL2D_PREVIEW_CANVAS_HEIGHT 216" in text
    assert "#define EL2D_PREVIEW_TEXTURE_BYTES_RGB565" in text
    assert "#define EL2D_PREVIEW_MOUTH_PARAMETER_ID \"ParamMouthOpenY\"" in text
    assert "#define EL2D_PREVIEW_LEFT_EYE_PARAMETER_ID \"ParamEyeLOpen\"" in text
    assert "#define EL2D_PREVIEW_RIGHT_EYE_PARAMETER_ID \"ParamEyeROpen\"" in text
