cmake_minimum_required(VERSION 3.20)
project(LiveWallpaper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ensure Unicode is enabled
add_compile_definitions(UNICODE _UNICODE)

# Add source files
set(SOURCES
    src/main.cpp
    src/wallpaper_host.h
    src/wallpaper_host.cpp
    src/renderer.h
    src/renderer.cpp
    src/video_decoder.h
    src/video_decoder.cpp
    src/timer.h
    src/timer.cpp
    src/render_engine.h
    src/render_engine.cpp
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
        /WX 
        /EHsc
        /wd4100 # Unreferenced formal parameter (often common in callbacks)
    )
    # Enable multi-processor compilation
    target_compile_options(LiveWallpaper PRIVATE /MP)
else()
    # Static link GCC/Clang runtimes to ensure zero runtime dependencies
    target_link_options(LiveWallpaper PRIVATE -static -static-libgcc -static-libstdc++)
endif()
