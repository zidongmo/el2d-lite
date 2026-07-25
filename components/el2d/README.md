# ESP-IDF Component

[中文](#中文) | [English](#english)

## 中文

此组件为 ESP-IDF 封装通用运行时、计时支持和 RGB565 输出。产品专用的状态映射、显示驱动、传输和帧调度属于使用该组件的应用。

通过 `EXTRA_COMPONENT_DIRS` 添加此目录，或将完整仓库复制到 ESP-IDF 项目的受管源码树中。该组件依赖 `esp_timer`；framebuffer 和显示传输由调用方负责。

## English

This component packages the generic runtime, timing support, and RGB565 output for ESP-IDF. Product-specific state mapping, display drivers, transport, and frame scheduling belong in the consuming application.

Add this directory through `EXTRA_COMPONENT_DIRS` or copy the complete repository into an ESP-IDF project's managed source tree. The component requires `esp_timer`; callers own the framebuffer and display transfer.
