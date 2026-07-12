# Converter

`el2d_converter.py` 把 model3 包的元数据和可选 drawable snapshot 写入 `.el2d` 目录。它输出：

- `manifest.json`：模型、纹理、控制参数、动作/表情摘要与目标 profile。
- `report.json`：源/目标内存预算和转换警告。
- `metadata.json`：格式版本与生成文件清单。
- 可选 `drawables.json`：离线后端提取的网格快照。
- 可选 preview config header：ESP-IDF procedural example 的配置桥。

当前 `.el2d` 是中间交换目录，不是设备端可直接加载的稳定二进制格式。生产固件使用 `tools/generate_el2d_mesh_asset.py` 生成的静态 C/C++ 资产。

```powershell
python converter\el2d_converter.py path\to\Avatar.model3.json --output build\avatar.el2d --drawable-snapshot build\avatar.base.drawables.json
```

如果没有 snapshot，drawable extraction 会标记为 `deferred`。未来 PurismCore 等后端可以输出相同 snapshot schema，而无需进入 ESP runtime。
