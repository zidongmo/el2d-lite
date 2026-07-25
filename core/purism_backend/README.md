# PurismCore Backend

[中文](#中文) | [English](#english)

## 中文

此可选后端边界用于对本地 PurismCore 源码 checkout 执行主机构建探针。任何 ESP-IDF Xtensa 集成都必须针对所选 checkout 和工具链组合单独验证。生产级提取器还必须将模型求值输出与可信的本地参考输出进行比较。

本仓库不随附任何 PurismCore 源码。默认构建和转换器均不依赖 PurismCore。

### 主机构建探针

MSVC 不支持 C99 变长数组。如果所选 PurismCore 源码 checkout 使用变长数组，默认的 Visual Studio generator 无法构建该探针。安装 MinGW 并确保工具链位于 `PATH` 后，可使用 MinGW generator 和单配置产物路径：

```powershell
cmake -S . -B build-purism -G "MinGW Makefiles" -DEL2D_PURISMCORE_DIR=local/purismcore
cmake --build build-purism --target el2d_purism_probe
.\build-purism\el2d_purism_probe.exe
```

默认的 `build` 不需要 PurismCore。

当前 CMake 构建路径要求 `EL2D_PURISMCORE_DIR` 中同时存在 `include/PurismCore.h` 和 `src/*.c`，因此不能使用仅包含 `PurismCoreBundle.h` 的目录构建探针。在本仓库中，Bundle 仅受布局检查支持：

```powershell
python tools\check_purismcore_layout.py local\purismcore-bundle
```

### ESP32-S3 工具链说明

`el2d_purismcore` CMake target 通过 `c_std_99` 请求 C99。本仓库未固定 PurismCore revision，也未记录已验证的 ESP-IDF 或 Xtensa 工具链版本，因此不保证任意 checkout 与工具链组合均可编译。

如果所选 Xtensa GCC 工具链报告 `psm__i32 *` 与 Cubism 兼容公共 API 中的 `const int *` 不兼容，应先在目标工具链上验证类型宽度和 ABI，并在相应的 ESP-IDF 适配层明确处理类型转换。只有确认 ABI 兼容后，才可考虑用 `-Wno-incompatible-pointer-types` 抑制诊断；该选项本身不能证明或修复兼容性。

## English

This optional backend boundary runs a host build probe against a local PurismCore source checkout. Any ESP-IDF Xtensa integration must be verified separately for the selected checkout and toolchain combination. A production extractor must additionally compare model evaluation output against a trusted local reference.

No PurismCore source is vendored. The default build and converter do not require it.

### Host Build Probe

MSVC does not support C99 variable-length arrays. If the selected PurismCore source checkout uses variable-length arrays, the default Visual Studio generator cannot build the probe. After installing MinGW and ensuring that its toolchain is on `PATH`, use the MinGW generator and its single-configuration artifact path:

```powershell
cmake -S . -B build-purism -G "MinGW Makefiles" -DEL2D_PURISMCORE_DIR=local/purismcore
cmake --build build-purism --target el2d_purism_probe
.\build-purism\el2d_purism_probe.exe
```

The default `build` does not require PurismCore.

The current CMake build path requires both `include/PurismCore.h` and `src/*.c` under `EL2D_PURISMCORE_DIR`, so a directory containing only `PurismCoreBundle.h` cannot build the probe. In this repository, a bundle is supported only by the layout check:

```powershell
python tools\check_purismcore_layout.py local\purismcore-bundle
```

### ESP32-S3 Toolchain Notes

The `el2d_purismcore` CMake target requests C99 through `c_std_99`. This repository does not pin a PurismCore revision or record a verified ESP-IDF or Xtensa toolchain version, so it does not guarantee that every checkout and toolchain combination will compile.

If the selected Xtensa GCC toolchain reports an incompatibility between `psm__i32 *` and `const int *` in the Cubism-compatible public API, first verify the type widths and ABI on the target toolchain, then handle the conversion explicitly in the corresponding ESP-IDF adapter layer. Only after confirming ABI compatibility should `-Wno-incompatible-pointer-types` be considered to suppress the diagnostic; the option itself neither proves nor fixes compatibility.
