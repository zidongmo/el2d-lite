# 资产管线

## 原则

资产管线必须可重复执行，不能依赖逐张手工修图或逐顶点人工调整。所有裁剪、纹理尺寸、状态采样、减面预算和误差阈值都通过命令行参数或可版本化 profile 表达。

模型授权检查是管线的第零步。仓库示例只使用测试动态生成的合成数据。

## 1. 准备输入

需要用户自行提供：

- `.model3.json` 及其引用的 `.moc3`、纹理、动作和表情。
- 一个本地可用、授权允许当前用途的 drawable 求值后端。
- 用于最终固件的状态采样列表与参数值。

官方 Web runtime 路径仅在本机读取。提取器使用只映射 model、runtime 和 host page 三个显式根目录的临时 HTTP server，不把整个工作盘暴露给浏览器。

## 2. 提取基础快照

```powershell
python tools\extract_official_cubism_drawables.py path\to\Avatar.model3.json --runtime-dir local\cubism-web-runtime --motion-initial Idle:0 --output build\avatar.base.drawables.json
```

输出包含 drawable id、顶点、UV、索引、render order、texture index、opacity、culling、blend mode 和 mask 关系。

## 3. 自动采样状态与表情

状态快照使用同一模型和同一拓扑，只改变参数求值后的顶点/透明度：

```powershell
python tools\extract_official_cubism_drawables.py path\to\Avatar.model3.json --runtime-dir local\cubism-web-runtime --parameter ParamAngleX=12 --parameter ParamBodyAngleX=4 --output build\avatar.turn.drawables.json
```

表情可从 `.model3.json` 中按名称应用，并允许在表情之后覆盖快速通道参数：

```powershell
python tools\extract_official_cubism_drawables.py path\to\Avatar.model3.json --runtime-dir local\cubism-web-runtime --expression Smile --post-parameter ParamEyeLOpen=1 --post-parameter ParamEyeROpen=1 --output build\avatar.smile.drawables.json
```

生成器会检查所有状态快照与 base 的 drawable、顶点、索引和 UV 兼容性。拓扑不一致时转换失败，不静默生成破损补间。

## 4. 纹理量化与静态资产

```powershell
python tools\generate_el2d_mesh_asset.py --snapshot build\avatar.base.drawables.json --state-snapshot turn=build\avatar.turn.drawables.json --state-snapshot smile=build\avatar.smile.drawables.json --texture path\to\texture_00.png --output-header generated\avatar_asset.h --output-source generated\avatar_asset.cpp --symbol avatar_mesh_model --texture-size 512
```

每张纹理转换为 RGB565 color plane（2 bytes/pixel）与 alpha4 plane（2 pixels/byte）。静态 C 生成物通常很大，而且可能是模型派生数据。它应由产品仓库或私有构建保存，不应默认提交到本项目。

## 5. 自适应减面

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

自适应 profile 锁定 ArtMesh 边界，并把 base 与所有状态采样的位置、UV 一起放进误差度量。若在边界保护条件下达不到预算，报告写入 `budget_met=false`，调用方应调整裁剪范围或预算，而不是接受明显轮廓破坏。

`--crop-profile bust` 是通用 drawable 级胸像裁剪启发式。它适合做首轮自动预算，但必须通过 host preview 和真机验收确认没有错误删除发型、手部或遮罩源。

## 6. 验收

每套资产至少执行：

1. 检查转换报告中的总/可见三角形、纹理字节数和 `budget_met`。
2. 在 Host 使用同一 RGB565 rasterizer 生成状态与补间预览。
3. 验证遮罩眼睛、头发边界、手部层级和透明混合。
4. 真机记录非缓存 render P50/P95、稳定帧命中、PSRAM 占用和 LCD 传输耗时。
5. 验证身体慢过渡与眨眼/口型快速增量层可以同时工作。

PC 提取和减面只生成资产参数，不生成运行时动画帧。设备每一帧仍由本地网格补间与光栅器产生。
