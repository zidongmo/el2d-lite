# EL2D Lite

[中文](#中文) | [English](#english)

## 中文

面向 ESP32-S3 等资源受限设备的轻量二维可变形网格运行时，以及与 Cubism 模型工作流兼容的离线资产转换工具。

> EL2D Lite 是独立的嵌入式二维可变形网格运行时和离线资产管线，与 Live2D Inc. 无隶属关系，也未获其背书。

### 项目状态

当前版本是 `0.1.0-dev` 技术预览，已经在 ESP32-S3 真机上完成胸像模型、多网格遮罩、状态补间、快速眨眼和口型叠加验证。运行时 API 和生成资产 ABI 在 `0.1.0` 前仍可能调整。

这不是把桌面 SDK 缩小后直接塞进 ESP32。当前可工作的工程路径是：

```text
用户自备模型与本地求值后端
          |
          v
PC 离线提取 drawable / 状态快照
          |
          v
纹理 RGB565+alpha4 转换、拓扑检查、可选减面
          |
          v
生成静态 C/C++ 资产
          |
          v
ESP32 本地补间、表情增量叠加、遮罩与 RGB565 光栅化
```

### 已实现

- 纯 C11 参数、缓动和动画曲线运行时。
- 调用方持有的 RGB565 framebuffer，不绑定 LCD 驱动或 GUI 框架。
- 稀疏 render order、透明度、共享遮罩和纹理网格渲染。
- 拓扑兼容快照之间的连续补间。
- 任意数量的通用表情增量层，可让眨眼和口型独立于身体状态过渡。
- PC 端纹理量化、静态 C 资产生成和基于 meshoptimizer 的自适应减面。
- 可选的本地官方 Cubism Web runtime 离线 drawable 提取器；仓库不包含该 runtime。
- ESP-IDF `el2d` 组件、Host CMake 构建、CTest、pytest 和公开树审计。

### 明确不包含

- 不包含 Live2D Cubism SDK、Cubism Core、官方 Web runtime 或其二进制文件。
- 不包含 Haru 或任何其他模型、纹理、动作、表情及模型派生的生成资产。
- ESP32 端目前不直接解析 `.moc3`；设备消费的是 PC 端生成的静态 C 资产。
- `.el2d` 目录当前用于转换元数据和中间交换，不是已经稳定的设备端二进制包格式。
- 不包含屏幕驱动、网络推流、机器人业务状态或产品动作调度。

### 真机参考数据

以下数据来自一个外部产品集成，不是合成 benchmark。配置为 ESP32-S3 240 MHz、8 MB Octal PSRAM、`240x216` RGB565、两张 `512x512` RGB565/alpha4 纹理、1822 个总三角形：

| 场景 | 渲染耗时 |
| --- | ---: |
| 普通非缓存重绘 P50/P95 | 55-58 ms |
| 连续口型重绘 P50/P95 | 56/57 ms |
| 应用层稳定帧缓存命中 | 4-5 ms |

缓存命中属于外部应用的调度优化；运行时本身保持确定性的本地网格渲染。实际性能受模型覆盖面积、透明混合、遮罩数量、Flash/PSRAM 配置和 LCD 传输方式影响。

### 快速开始

要求 CMake 3.16+、C/C++ 编译器和 Python 3.10+。

```powershell
python -m pip install -e ".[dev]"
cmake -S . -B build -DEL2D_BUILD_TESTS=ON -DEL2D_BUILD_TOOLS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python -m pytest -q
```

生成不依赖第三方模型的 RGB565 procedural preview：

```powershell
.\build\Release\el2d_render_preview.exe build\el2d_preview.ppm active
```

Linux 单配置生成器通常将可执行文件放在 `build/` 而不是 `build/Release/`。

### 资产转换

使用你本地、已获许可的 Cubism Web runtime 提取 drawable：

```powershell
python tools\extract_official_cubism_drawables.py path\to\Avatar.model3.json --runtime-dir local\cubism-web-runtime --output build\avatar.base.drawables.json
```

将快照和纹理转换成 ESP 可直接编译的静态资产：

```powershell
python tools\generate_el2d_mesh_asset.py --snapshot build\avatar.base.drawables.json --state-snapshot active=build\avatar.active.drawables.json --texture path\to\texture_00.png --output-header generated\avatar_asset.h --output-source generated\avatar_asset.cpp --symbol avatar_mesh_model --texture-size 512 --lod-profile adaptive --lod-target-triangles 1800 --meshoptimizer-helper build\Release\el2d_mesh_simplify.exe
```

转换器不会自动授予输入模型或官方 runtime 的使用、修改和分发权。完整步骤与自动化边界见 [资产管线](docs/asset-pipeline.md) 和 [许可与资产](docs/licensing-and-assets.md)。

### ESP-IDF 集成

仓库内的 [components/el2d](components/el2d) 是标准 ESP-IDF 组件。可以把该目录通过 `EXTRA_COMPONENT_DIRS` 加入工程，或按 [examples/esp_idf_preview](examples/esp_idf_preview) 的方式引用：

```cmake
set(EXTRA_COMPONENT_DIRS "../../components/el2d")
```

应用负责分配 framebuffer、映射产品状态、决定动画时钟和 LCD 传输节奏，并持有生成资产。运行时不认识 `listening`、`thinking` 或 `speaking` 等业务状态。

### 仓库结构

```text
include/el2d/          公共 C API
src/                   通用运行时与 RGB565 软件光栅器
components/el2d/       ESP-IDF 组件封装
converter/             .el2d 元数据/中间包转换器
tools/                 提取、静态资产生成、减面和公开树审计
third_party/           明确归属和版本的第三方源码
examples/              Host 与 ESP-IDF 示例
tests/                 C 与 Python 合成测试
docs/                  架构、资产管线和许可边界
```

架构和所有权边界见 [架构文档](docs/architecture.md)。

### PurismCore

[PurismCore](https://github.com/SakuraMotion/PurismCore) 是可选的 MIT 许可 Core 重实现。本仓库提供布局检查与编译探针，但暂不 vendoring，也尚未把它声明为默认生产提取后端。通过 `EL2D_PURISMCORE_DIR` 指向本地 checkout 可构建探针。

### 许可

项目自有代码使用 [Apache License 2.0](LICENSE)。Vendored meshoptimizer 子集保留其 [MIT License](third_party/meshoptimizer/LICENSE.md)。模型、官方 runtime 与转换产物可能受各自许可约束，不因本项目开源而自动获得再分发权。

Live2D 和 Cubism 是 Live2D Inc. 的商标或注册商标。本项目为独立工程，与 Live2D Inc. 无隶属或背书关系。

## English

A lightweight 2D deformable-mesh runtime for resource-constrained devices such as the ESP32-S3, together with offline asset conversion tools compatible with Cubism model workflows.

> EL2D Lite is an independent embedded 2D deformable-mesh runtime and offline asset pipeline. It is not affiliated with or endorsed by Live2D Inc.

### Project Status

The current version is a `0.1.0-dev` technical preview. It has been validated on physical ESP32-S3 hardware with a bust model, multi-mesh masks, state tweening, rapid blinking, and lip-sync overlays. The runtime API and generated asset ABI may still change before `0.1.0`.

This is not a desktop SDK scaled down and placed directly on an ESP32. The current working engineering flow is:

```text
User-supplied model and local evaluation backend
          |
          v
Offline extraction of drawable / state snapshots on a PC
          |
          v
RGB565+alpha4 texture conversion, topology checks, optional simplification
          |
          v
Generation of static C/C++ assets
          |
          v
Local ESP32 tweening, expression-delta layering, masks, and RGB565 rasterization
```

### Implemented

- A pure C11 runtime for parameters, easing, and animation curves.
- A caller-owned RGB565 framebuffer, with no dependency on an LCD driver or GUI framework.
- Sparse render order, opacity, shared masks, and textured-mesh rendering.
- Continuous interpolation between topology-compatible snapshots.
- Any number of general-purpose expression-delta layers, allowing blinking and lip sync to remain independent of body-state transitions.
- PC-side texture quantization, static C asset generation, and adaptive mesh simplification based on meshoptimizer.
- An optional offline drawable extractor using a locally installed official Cubism Web runtime; that runtime is not included in this repository.
- An ESP-IDF `el2d` component, host CMake builds, CTest, pytest, and public-tree auditing.

### Explicitly Not Included

- The Live2D Cubism SDK, Cubism Core, the official Web runtime, and their binaries are not included.
- Haru and all other models, textures, motions, expressions, and model-derived generated assets are not included.
- The ESP32 runtime does not currently parse `.moc3` files directly; the device consumes static C assets generated on a PC.
- The `.el2d` directory currently stores conversion metadata and intermediate interchange data. It is not a stable device-side binary package format.
- Screen drivers, network streaming, robot application states, and product action scheduling are not included.

### Hardware Reference Data

The following figures come from an external product integration, not a synthetic benchmark. The configuration was an ESP32-S3 at 240 MHz with 8 MB of Octal PSRAM, a `240x216` RGB565 framebuffer, two `512x512` RGB565/alpha4 textures, and 1822 triangles in total:

| Scenario | Render time |
| --- | ---: |
| Standard uncached redraw P50/P95 | 55-58 ms |
| Continuous lip-sync redraw P50/P95 | 56/57 ms |
| Application-level stable-frame cache hit | 4-5 ms |

Cache hits are a scheduling optimization in the external application; the runtime itself retains deterministic local mesh rendering. Actual performance depends on model coverage, alpha blending, mask count, Flash/PSRAM configuration, and the LCD transfer method.

### Quick Start

Requirements are CMake 3.16+, a C/C++ compiler, and Python 3.10+.

```powershell
python -m pip install -e ".[dev]"
cmake -S . -B build -DEL2D_BUILD_TESTS=ON -DEL2D_BUILD_TOOLS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python -m pytest -q
```

Generate an RGB565 procedural preview that does not depend on a third-party model:

```powershell
.\build\Release\el2d_render_preview.exe build\el2d_preview.ppm active
```

On Linux, single-configuration generators usually place the executable in `build/` rather than `build/Release/`.

### Asset Conversion

Use a locally installed, properly licensed Cubism Web runtime to extract drawables:

```powershell
python tools\extract_official_cubism_drawables.py path\to\Avatar.model3.json --runtime-dir local\cubism-web-runtime --output build\avatar.base.drawables.json
```

Convert snapshots and textures into static assets that can be compiled directly for the ESP:

```powershell
python tools\generate_el2d_mesh_asset.py --snapshot build\avatar.base.drawables.json --state-snapshot active=build\avatar.active.drawables.json --texture path\to\texture_00.png --output-header generated\avatar_asset.h --output-source generated\avatar_asset.cpp --symbol avatar_mesh_model --texture-size 512 --lod-profile adaptive --lod-target-triangles 1800 --meshoptimizer-helper build\Release\el2d_mesh_simplify.exe
```

The converter does not automatically grant rights to use, modify, or distribute input models or the official runtime. See the [asset pipeline](docs/asset-pipeline.md) and [licensing and assets](docs/licensing-and-assets.md) documentation for the complete workflow and automation boundaries.

### ESP-IDF Integration

The repository's [components/el2d](components/el2d) directory is a standard ESP-IDF component. Add it to a project through `EXTRA_COMPONENT_DIRS`, or reference it as shown in [examples/esp_idf_preview](examples/esp_idf_preview):

```cmake
set(EXTRA_COMPONENT_DIRS "../../components/el2d")
```

The application allocates the framebuffer, maps product states, determines the animation clock and LCD transfer cadence, and owns the generated assets. The runtime has no knowledge of business states such as `listening`, `thinking`, or `speaking`.

### Repository Layout

```text
include/el2d/          Public C API
src/                   Portable runtime and RGB565 software rasterizer
components/el2d/       ESP-IDF component wrapper
converter/             .el2d metadata/intermediate-package converter
tools/                 Extraction, static asset generation, simplification, and public-tree auditing
third_party/           Third-party source with explicit attribution and versioning
examples/              Host and ESP-IDF examples
tests/                 Synthetic C and Python tests
docs/                  Architecture, asset pipeline, and licensing boundaries
```

See the [architecture documentation](docs/architecture.md) for architecture and ownership boundaries.

### PurismCore

[PurismCore](https://github.com/SakuraMotion/PurismCore) is an optional MIT-licensed reimplementation of the Core. This repository provides layout checks and a build probe, but does not currently vendor it or designate it as the default production extraction backend. Point `EL2D_PURISMCORE_DIR` to a local checkout to build the probe.

### License

Project-owned code is licensed under the [Apache License 2.0](LICENSE). The vendored meshoptimizer subset retains its [MIT License](third_party/meshoptimizer/LICENSE.md). Models, the official runtime, and converted outputs may be subject to their own licenses; this project's open-source status does not automatically grant redistribution rights.

Live2D and Cubism are trademarks or registered trademarks of Live2D Inc. This is an independent project and is not affiliated with or endorsed by Live2D Inc.
