cmake_minimum_required(VERSION 3.16)

project(el2d_lite VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)

option(EL2D_BUILD_TESTS "Build EL2D Lite tests" ON)
option(EL2D_BUILD_TOOLS "Build EL2D Lite host tools" ON)
set(EL2D_PURISMCORE_DIR "" CACHE PATH "Optional path to a local PurismCore source checkout")

add_library(el2d_runtime STATIC
    src/el2d_clip.c
    src/el2d_easing.c
    src/el2d_framebuffer.c
    src/el2d_mesh_rasterizer.c
    src/el2d_mesh_renderer.c
    src/el2d_model.c
    src/el2d_preview_renderer.c
)
add_library(el2d::runtime ALIAS el2d_runtime)

target_compile_features(el2d_runtime PUBLIC c_std_11)
target_include_directories(el2d_runtime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(MSVC)
    target_compile_options(el2d_runtime PRIVATE /W4)
else()
    target_compile_options(el2d_runtime PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(el2d_runtime PUBLIC m)
endif()

if(EL2D_BUILD_TOOLS)
    add_executable(el2d_render_preview tools/el2d_render_preview.c)
    target_link_libraries(el2d_render_preview PRIVATE el2d::runtime)

    add_library(el2d_meshoptimizer STATIC
        third_party/meshoptimizer/allocator.cpp
        third_party/meshoptimizer/simplifier.cpp
    )
    target_compile_features(el2d_meshoptimizer PUBLIC cxx_std_17)
    target_include_directories(el2d_meshoptimizer PUBLIC third_party/meshoptimizer)

    add_executable(el2d_mesh_simplify tools/el2d_mesh_simplify.cpp)
    target_compile_features(el2d_mesh_simplify PRIVATE cxx_std_17)
    target_link_libraries(el2d_mesh_simplify PRIVATE el2d_meshoptimizer)
    if(MSVC)
        target_compile_options(el2d_mesh_simplify PRIVATE /W4)
    else()
        target_compile_options(el2d_mesh_simplify PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(EL2D_BUILD_TESTS)
    enable_testing()
    add_executable(test_el2d_runtime tests/test_el2d_runtime.c)
    target_include_directories(test_el2d_runtime PRIVATE src)
    target_link_libraries(test_el2d_runtime PRIVATE el2d::runtime)
    add_test(NAME el2d_runtime COMMAND test_el2d_runtime)

    if(EL2D_BUILD_TOOLS)
        add_test(
            NAME el2d_preview_smoke
            COMMAND el2d_render_preview ${CMAKE_CURRENT_BINARY_DIR}/el2d_preview.ppm active
        )
    endif()
endif()

if(EL2D_PURISMCORE_DIR)
    if(NOT EXISTS "${EL2D_PURISMCORE_DIR}/include/PurismCore.h")
        message(FATAL_ERROR "EL2D_PURISMCORE_DIR does not contain include/PurismCore.h: ${EL2D_PURISMCORE_DIR}")
    endif()

    file(GLOB EL2D_PURISMCORE_SOURCES CONFIGURE_DEPENDS "${EL2D_PURISMCORE_DIR}/src/*.c")
    if(NOT EL2D_PURISMCORE_SOURCES)
        message(FATAL_ERROR "EL2D_PURISMCORE_DIR does not contain source files under src/")
    endif()

    add_library(el2d_purismcore STATIC ${EL2D_PURISMCORE_SOURCES})
    target_compile_features(el2d_purismcore PUBLIC c_std_99)
    target_include_directories(el2d_purismcore
        PUBLIC "${EL2D_PURISMCORE_DIR}/include"
        PRIVATE "${EL2D_PURISMCORE_DIR}/src"
    )
    if(NOT MSVC)
        target_compile_options(el2d_purismcore PRIVATE -Wall -Wextra)
    endif()

    if(EL2D_BUILD_TOOLS)
        add_executable(el2d_purism_probe tools/el2d_purism_probe.c)
        target_link_libraries(el2d_purism_probe PRIVATE el2d_purismcore)
    endif()
endif()

install(TARGETS el2d_runtime EXPORT el2dLiteTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT el2dLiteTargets NAMESPACE el2d:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/el2d-lite)
