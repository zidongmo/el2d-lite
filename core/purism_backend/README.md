# PurismCore Backend

[中文](#中文) | [English](#english)

## 中文

此可选后端边界用于检查本地 PurismCore checkout 能否由主机或 ESP-IDF Xtensa 工具链构建。生产级提取器还必须将模型求值输出与可信的本地参考输出进行比较。

仓库不 vendoring 任何 PurismCore 源码。默认构建和转换器均不依赖 PurismCore。

### 主机构建探针

使用本地 PurismCore 源码 checkout，但不要将其提交到仓库：

```powershell
cmake -S . -B build-purism -DEL2D_PURISMCORE_DIR=local/purismcore
cmake --build build-purism
build-purism\Debug\el2d_purism_probe.exe
```

默认的 `build` 不需要 PurismCore。

在 Windows 上，默认的 Visual Studio C 编译器无法编译当前 PurismCore 源码 checkout，因为该代码使用 C99 变长数组。实际执行 Core 探针时，请使用 MinGW、ESP-IDF Xtensa GCC 工具链，或 `PurismCoreBundle.h` 发布产物。

### ESP32-S3 编译说明

本机的 ESP-IDF 5.4.4 Xtensa GCC 可以使用以下选项编译当前 PurismCore 源码 checkout：

```text
-std=c99 -Wno-incompatible-pointer-types
```

需要此抑制选项，是因为在 Xtensa 工具链上 `psm__i32` 解析为 `long int`，而与 Cubism 兼容的公共 API 将多个数组声明为 `const int *`。在此目标平台上两者均为 32 位，但应在 ESP-IDF 组件中明确处理这一类型不匹配，而不能将其隐藏在应用代码中。

## English

This optional backend boundary checks whether a local PurismCore checkout can be built by the host or ESP-IDF Xtensa toolchain. A production extractor must additionally compare model evaluation output against a trusted local reference.

No PurismCore source is vendored. The default build and converter do not require it.

### Host Build Probe

Use a local PurismCore source checkout without committing it:

```powershell
cmake -S . -B build-purism -DEL2D_PURISMCORE_DIR=local/purismcore
cmake --build build-purism
build-purism\Debug\el2d_purism_probe.exe
```

The default `build` does not require PurismCore.

On Windows, the default Visual Studio C compiler cannot compile the current
PurismCore source checkout because that code uses C99 variable-length arrays.
Use MinGW, the ESP-IDF Xtensa GCC path, or a `PurismCoreBundle.h` release
artifact for the actual Core probe.

### ESP32-S3 Compilation Notes

The ESP-IDF 5.4.4 Xtensa GCC on this machine can compile the current
PurismCore source checkout with:

```text
-std=c99 -Wno-incompatible-pointer-types
```

The suppression is needed because `psm__i32` resolves to `long int` on the
Xtensa toolchain while the Cubism-compatible public API exposes several arrays
as `const int *`. Both are 32-bit on this target, but the type mismatch must be
handled deliberately in the ESP-IDF component rather than hidden in application
code.
