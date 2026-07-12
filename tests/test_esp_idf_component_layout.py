from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_esp_idf_component_declares_runtime_sources() -> None:
    cmake = ROOT / "components" / "el2d" / "CMakeLists.txt"

    text = cmake.read_text(encoding="utf-8")

    assert "idf_component_register" in text
    assert "el2d_clip.c" in text
    assert "el2d_framebuffer.c" in text
    assert "el2d_preview_renderer.c" in text
    assert "el2d_esp_preview.c" in text


def test_esp_idf_preview_example_exists() -> None:
    example = ROOT / "examples" / "esp_idf_preview"

    assert (example / "CMakeLists.txt").exists()
    assert (example / "main" / "CMakeLists.txt").exists()
    assert (example / "main" / "main.c").exists()
    assert (ROOT / "components" / "el2d" / "el2d_esp_preview.h").exists()


def test_esp_idf_preview_example_accepts_generated_config() -> None:
    main = ROOT / "examples" / "esp_idf_preview" / "main" / "main.c"

    text = main.read_text(encoding="utf-8")

    assert "el2d_generated_preview_config.h" in text
    assert "EL2D_PREVIEW_MODEL_NAME" in text
    assert "EL2D_PREVIEW_CANVAS_WIDTH" in text
    assert "EL2D_PREVIEW_MOUTH_PARAMETER_ID" in text
