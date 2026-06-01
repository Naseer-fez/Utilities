cmake_minimum_required(VERSION 3.20)
project(LiveWallpaper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# Ensure Unicode is enabled
add_compile_definitions(UNICODE _UNICODE)

# Add source files
set(SOURCES
    src/main.cpp
    src/explorer_integration.h
    src/explorer_integration.cpp
    src/device_manager.h
    src/device_manager.cpp
    src/swap_chain_manager.h
    src/swap_chain_manager.cpp
    src/playlist_manager.h
    src/playlist_manager.cpp
    src/synchronization_manager.h
    src/synchronization_manager.cpp
    src/render_state_machine.h
    src/video_renderer.h
    src/video_renderer.cpp
    src/video_decoder.h
    src/video_decoder.cpp
    src/timer.h
    src/timer.cpp
    src/ffi_shader_bridge.h
    src/ffi_shader_bridge.cpp
    src/render_thread_controller.h
    src/render_thread_controller.cpp
    src/power_monitor.h
    src/power_monitor.cpp
    src/config.h
    src/config.cpp
    src/tray_icon.h
    src/tray_icon.cpp
    src/playlist_dialog.h
    src/playlist_dialog.cpp
    src/utils.h
    src/utils.cpp
)

add_executable(LiveWallpaper WIN32 ${SOURCES})

# Windows Specific Linker settings and library dependencies
target_link_libraries(LiveWallpaper PRIVATE
    d3d11
    dxgi
    d3dcompiler
    mf
    mfplat
    mfuuid
    mfreadwrite
    shlwapi
    dwmapi
    user32
    gdi32
    shell32
    winmm
    ole32
    uuid
)

# Compiler optimization settings for MSVC
if(MSVC)
    target_compile_options(LiveWallpaper PRIVATE 
        /W4 
        /EHsc
        /wd4100 # Unreferenced formal parameter (often common in callbacks)
    )
    # Enable multi-processor compilation
    target_compile_options(LiveWallpaper PRIVATE /MP)
else()
    # Static link GCC/Clang runtimes to ensure zero runtime dependencies
    target_link_options(LiveWallpaper PRIVATE -static -static-libgcc -static-libstdc++)
endif()

add_custom_command(
    TARGET LiveWallpaper POST_BUILD
    COMMAND cargo build --manifest-path ${CMAKE_CURRENT_SOURCE_DIR}/live_wallpaper_rust/Cargo.toml --release
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/live_wallpaper_rust/target/release/live_wallpaper_rust.dll
        $<TARGET_FILE_DIR:LiveWallpaper>/live_wallpaper_rust.dll
    COMMENT "Building Rust DLL and copying to output directory"
)

# Basic C++ Unit Test Scaffolding
add_executable(LiveWallpaperTests 
    tests/basic_test.cpp 
    src/utils.cpp 
    src/playlist_manager.cpp 
    src/synchronization_manager.cpp
)
target_link_libraries(LiveWallpaperTests gtest_main shlwapi)
if(NOT MSVC)
    target_link_options(LiveWallpaperTests PRIVATE -static -static-libgcc -static-libstdc++)
endif()
add_test(NAME LiveWallpaperTests COMMAND LiveWallpaperTests)

