# ESP-IDF Component

This component packages the generic runtime, timing support, and RGB565 output for ESP-IDF. Product-specific state mapping, display drivers, transport, and frame scheduling belong in the consuming application.

Add this directory through `EXTRA_COMPONENT_DIRS` or copy the complete repository into an ESP-IDF project's managed source tree. The component requires `esp_timer`; callers own the framebuffer and display transfer.
