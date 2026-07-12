set(EL2D_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

idf_component_register(
    SRCS
        "${EL2D_ROOT}/src/el2d_clip.c"
        "${EL2D_ROOT}/src/el2d_easing.c"
        "${EL2D_ROOT}/src/el2d_framebuffer.c"
        "${EL2D_ROOT}/src/el2d_mesh_rasterizer.c"
        "${EL2D_ROOT}/src/el2d_mesh_renderer.c"
        "${EL2D_ROOT}/src/el2d_model.c"
        "${EL2D_ROOT}/src/el2d_preview_renderer.c"
        "el2d_esp_preview.c"
    INCLUDE_DIRS
        "${EL2D_ROOT}/include"
        "."
    REQUIRES
        esp_timer
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE EL2D_RENDER_DETAILED_STATS=0)
target_compile_options(${COMPONENT_LIB} PRIVATE -O3)
