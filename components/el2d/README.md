# ESP-IDF Component

[中文](#中文) | [English](#english)

## 中文

此组件为 ESP-IDF 封装通用运行时、计时支持和 RGB565 输出。产品专用的状态映射、显示驱动、传输和帧调度属于使用该组件的应用。

将完整 EL2D Lite 仓库放在 ESP-IDF 项目的 CMake 配置可以引用的路径，并让 `EXTRA_COMPONENT_DIRS` 指向该仓库中的 `components/el2d`。例如，当 ESP-IDF 项目与 `el2d-lite` 为同级目录时：

```cmake
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../el2d-lite/components/el2d")
```

不要只复制 `components/el2d`，因为其 `CMakeLists.txt` 会通过相对路径引用仓库根目录中的 `src/` 和 `include/`。该组件依赖 `esp_timer`；framebuffer 和显示传输由调用方负责。

## English

This component packages the generic runtime, timing support, and RGB565 output for ESP-IDF. Product-specific state mapping, display drivers, transport, and frame scheduling belong in the consuming application.

Place the complete EL2D Lite repository at a path that the ESP-IDF project's CMake configuration can reference, and point `EXTRA_COMPONENT_DIRS` to `components/el2d` within that repository. For example, when the ESP-IDF project and `el2d-lite` are sibling directories:

```cmake
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../el2d-lite/components/el2d")
```

Do not copy `components/el2d` by itself because its `CMakeLists.txt` references the repository-level `src/` and `include/` directories through relative paths. The component requires `esp_timer`; callers own the framebuffer and display transfer.
