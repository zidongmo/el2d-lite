[中文](#中文) | [English](#english)

# 中文

## 资产管线（中文）

### 原则（中文）

资产管线必须可重复执行，不能依赖逐张手工修图或逐顶点人工调整。所有裁剪、纹理尺寸、状态采样、减面预算和误差阈值都通过命令行参数或可版本化 profile 表达。

模型授权检查是管线的第零步。仓库示例只使用测试动态生成的合成数据。

### 1. 准备输入（中文）

需要用户自行提供：

- `.model3.json` 及其引用的 `.moc3`、纹理、动作和表情。
- 一个本地可用、授权允许当前用途的 drawable 求值后端。
- 用于最终固件的状态采样列表与参数值。

`Playwright` 未列入本项目 `pyproject.toml` 的依赖。运行提取器前必须显式安装 Python 包及 Chromium：

```powershell
python -m pip install playwright
python -m playwright install chromium
```

`--runtime-dir` 目录必须直接包含以下三个精确文件名：

- `live2dcubismcore.min.js`：用户本地提供的官方 Cubism Core for Web 文件，来源于官方 Cubism SDK for Web 包。
- `pixi.min.js`：用户本地提供的 PixiJS 浏览器 bundle。
- `pixi-live2d-display-cubism4.min.js`：用户本地提供的 pixi-live2d-display Cubism 4 浏览器 bundle。

`tools/official_cubism_extractor.html` 按上述字面文件名加载脚本，不会自动发现其他名称；本文不指定版本。`--runtime-dir` 仅在本机读取。提取器使用只映射 model、runtime 和 host page 三个显式根目录的临时 HTTP server，不把整个工作盘暴露给浏览器。

### 2. 提取基础快照（中文）

```powershell
python tools\extract_official_cubism_drawables.py path\to\Avatar.model3.json --runtime-dir local\cubism-web-runtime --motion-initial Idle:0 --output build\avatar.base.drawables.json
```

输出包含 drawable id、顶点、UV、索引、render order、texture index、opacity、culling、blend mode 和 mask 关系。

### 3. 自动采样状态与表情（中文）

状态快照使用同一模型和同一拓扑，只改变参数求值后的顶点/透明度：

```powershell
python tools\extract_official_cubism_drawables.py path\to\Avatar.model3.json --runtime-dir local\cubism-web-runtime --parameter ParamAngleX=12 --parameter ParamBodyAngleX=4 --output build\avatar.turn.drawables.json
```

表情可从 `.model3.json` 中按名称应用，并允许在表情之后覆盖快速通道参数：

```powershell
python tools\extract_official_cubism_drawables.py path\to\Avatar.model3.json --runtime-dir local\cubism-web-runtime --expression Smile --post-parameter ParamEyeLOpen=1 --post-parameter ParamEyeROpen=1 --output build\avatar.smile.drawables.json
```

生成器的 `adaptive` 路径会在减面前显式检查 state 与 base 的 drawable 数量和顺序（通过对应 id）、顶点/索引数量、索引数组及 UV，但该检查不比较遮罩关系。`none` 路径不做这些跨快照兼容性检查。调用方必须在生成前保证未由所选路径检查的项目以及 runtime 要求的遮罩关系兼容，否则可能生成无法正确补间的资产。

### 4. 纹理量化与静态资产（中文）

```powershell
python tools\generate_el2d_mesh_asset.py --snapshot build\avatar.base.drawables.json --state-snapshot turn=build\avatar.turn.drawables.json --state-snapshot smile=build\avatar.smile.drawables.json --texture path\to\texture_00.png --output-header generated\avatar_asset.h --output-source generated\avatar_asset.cpp --symbol avatar_mesh_model --texture-size 512 --lod-profile none
```

上例显式选择 `--lod-profile none`，因此不提供上述跨快照防护，只适用于调用方已经完成兼容性检查的输入。若要由工具执行上述 `adaptive` 兼容性检查，先构建 host helper，再改用第 5 节的 `adaptive` 参数；遮罩关系仍由调用方检查。

每张纹理转换为 RGB565 color plane（2 bytes/pixel）与 alpha4 plane（2 pixels/byte）。静态 C 生成物通常很大，而且可能是模型派生数据。它应由产品仓库或私有构建保存，不应默认提交到本项目。

### 5. 自适应减面（中文）

先构建 host helper：

```powershell
cmake --build build --config Release --target el2d_mesh_simplify
```

然后给生成器加入以下参数：

```text
--lod-profile adaptive
--lod-target-triangles 1800
--meshoptimizer-helper build/Release/el2d_mesh_simplify.exe
--optimized-snapshot-dir build/optimized
```

自适应 profile 锁定 ArtMesh 边界，并把 base 与所有状态采样的位置、UV 一起放进误差度量。若在边界保护条件下无法达到预算，生成器会抛出 `ValueError` 并在输出最终 JSON 报告前停止。调用方应提高 `--lod-target-triangles`，或调整输入 drawable 或裁剪策略（包括选择合适的 `--crop-profile`）后重试，而不是接受明显轮廓破坏。

`--crop-profile bust` 是通用 drawable 级胸像裁剪启发式。它适合做首轮自动预算，但必须通过 host preview 和真机验收确认没有错误删除发型、手部或遮罩源。

### 6. 验收（中文）

每套资产至少执行：

1. 检查成功转换报告中的总/可见三角形。纹理内存可按每张转换后纹理的 `width × height × (2 + 0.5)` bytes 求和估算（RGB565 为 2 bytes/pixel，alpha4 为 0.5 bytes/pixel），或从生成资产中的 RGB565/alpha4 数组统计。
2. 在 Host 使用同一 RGB565 rasterizer 生成状态与补间预览。
3. 验证遮罩眼睛、头发边界、手部层级和透明混合。
4. 真机记录非缓存 render P50/P95、稳定帧命中、PSRAM 占用和 LCD 传输耗时。
5. 验证身体慢过渡与眨眼/口型快速增量层可以同时工作。

PC 提取和减面只生成资产参数，不生成运行时动画帧。设备每一帧仍由本地网格补间与光栅器产生。

# English

## Asset Pipeline (English)

### Principles (English)

The asset pipeline must be reproducible and must not depend on manual retouching of individual images or manual adjustment of individual vertices. All cropping, texture dimensions, state sampling, mesh-reduction budgets, and error thresholds are expressed through command-line parameters or versionable profiles.

Model license review is step zero of the pipeline. Repository examples use only synthetic data generated dynamically by tests.

### 1. Prepare Inputs (English)

Users must provide:

- The `.model3.json` file and its referenced `.moc3`, textures, motions, and expressions.
- A locally available drawable-evaluation backend whose license permits the intended use.
- The list of state samples and parameter values for the final firmware.

`Playwright` is not listed as a dependency in this project's `pyproject.toml`. Before running the extractor, explicitly install the Python package and Chromium:

```powershell
python -m pip install playwright
python -m playwright install chromium
```

The `--runtime-dir` directory must directly contain these three exact filenames:

- `live2dcubismcore.min.js`: the user-provided official Cubism Core for Web file from the official Cubism SDK for Web package.
- `pixi.min.js`: the user-provided PixiJS browser bundle.
- `pixi-live2d-display-cubism4.min.js`: the user-provided pixi-live2d-display Cubism 4 browser bundle.

`tools/official_cubism_extractor.html` loads scripts by these literal filenames and does not discover alternative names automatically; this document does not specify versions. `--runtime-dir` is read only from the local machine. The extractor uses a temporary HTTP server that maps only three explicit roots for the model, runtime, and host page; it does not expose the entire working drive to the browser.

### 2. Extract the Base Snapshot (English)

```powershell
python tools\extract_official_cubism_drawables.py path\to\Avatar.model3.json --runtime-dir local\cubism-web-runtime --motion-initial Idle:0 --output build\avatar.base.drawables.json
```

The output contains drawable IDs, vertices, UVs, indices, render order, texture index, opacity, culling, blend mode, and mask relationships.

### 3. Sample States and Expressions Automatically (English)

State snapshots use the same model and topology and change only the vertices and opacity produced by parameter evaluation:

```powershell
python tools\extract_official_cubism_drawables.py path\to\Avatar.model3.json --runtime-dir local\cubism-web-runtime --parameter ParamAngleX=12 --parameter ParamBodyAngleX=4 --output build\avatar.turn.drawables.json
```

Expressions can be applied by name from `.model3.json`, with fast-path parameters overridden after the expression:

```powershell
python tools\extract_official_cubism_drawables.py path\to\Avatar.model3.json --runtime-dir local\cubism-web-runtime --expression Smile --post-parameter ParamEyeLOpen=1 --post-parameter ParamEyeROpen=1 --output build\avatar.smile.drawables.json
```

The generator's `adaptive` path explicitly checks each state against the base before simplification for drawable count and order (through corresponding IDs), vertex and index counts, index arrays, and UVs, but this check does not compare mask relationships. The `none` path does not perform these cross-snapshot compatibility checks. Before generation, the caller must guarantee that items not checked by the selected path and the mask relationships required by the runtime are compatible; otherwise, the generated assets may not interpolate correctly.

### 4. Quantize Textures and Generate Static Assets (English)

```powershell
python tools\generate_el2d_mesh_asset.py --snapshot build\avatar.base.drawables.json --state-snapshot turn=build\avatar.turn.drawables.json --state-snapshot smile=build\avatar.smile.drawables.json --texture path\to\texture_00.png --output-header generated\avatar_asset.h --output-source generated\avatar_asset.cpp --symbol avatar_mesh_model --texture-size 512 --lod-profile none
```

The example explicitly selects `--lod-profile none`, so it does not provide the cross-snapshot safeguards described above and is suitable only for inputs whose compatibility the caller has already checked. To have the tool perform the `adaptive` compatibility checks described above, first build the host helper and then use the `adaptive` parameters in Section 5; mask relationships remain the caller's responsibility.

Each texture is converted into an RGB565 color plane (2 bytes/pixel) and an alpha4 plane (2 pixels/byte). Generated static C assets are usually large and may contain model-derived data. They should be stored in the product repository or a private build and should not be committed to this project by default.

### 5. Apply Adaptive Mesh Simplification (English)

First build the host helper:

```powershell
cmake --build build --config Release --target el2d_mesh_simplify
```

Then add the following parameters to the generator:

```text
--lod-profile adaptive
--lod-target-triangles 1800
--meshoptimizer-helper build/Release/el2d_mesh_simplify.exe
--optimized-snapshot-dir build/optimized
```

The adaptive profile locks ArtMesh boundaries and includes positions and UVs from the base and all state samples in the error metric. If the budget cannot be reached while preserving the boundaries, the generator raises `ValueError` and stops before emitting the final JSON report. The caller should raise `--lod-target-triangles` or revise the input drawables or cropping strategy, including selecting an appropriate `--crop-profile`, and retry instead of accepting visible silhouette damage.

`--crop-profile bust` is a general-purpose, drawable-level heuristic for bust cropping. It is suitable for a first automated budgeting pass, but host preview and on-device acceptance testing must confirm that it has not incorrectly removed hair, hands, or mask sources.

### 6. Acceptance Testing (English)

For every asset set, perform at least the following checks:

1. Inspect the total and visible triangle counts in the successful conversion report. Estimate texture memory by summing `width × height × (2 + 0.5)` bytes for each converted texture (RGB565 at 2 bytes/pixel and alpha4 at 0.5 bytes/pixel), or derive it from the RGB565/alpha4 arrays in the generated asset.
2. On the host, use the same RGB565 rasterizer to generate previews of states and interpolated transitions.
3. Verify masked eyes, hair boundaries, hand layering, and alpha blending.
4. On the device, record uncached render P50/P95, stable-frame cache hits, PSRAM usage, and LCD transfer time.
5. Verify that slow body transitions and fast additive blink/mouth-shape layers can operate simultaneously.

PC extraction and mesh simplification generate only asset parameters, not runtime animation frames. Every device frame is still produced by local mesh interpolation and rasterization.
