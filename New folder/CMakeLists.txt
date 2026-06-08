cmake_minimum_required(VERSION 3.20)
project(LiveWallpaper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Disable RTTI and exceptions
string(REGEX REPLACE "/GR" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REGEX REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
add_compile_options(/GR- /EHs-c-)

add_executable(LiveWallpaper WIN32
    src/main.cpp
    src/wallpaper_host.cpp
    src/video_decoder.cpp
    src/renderer.cpp
    src/power_monitor.cpp
    src/config.cpp
    src/timer.cpp
    res/app.rc
)

target_include_directories(LiveWallpaper PRIVATE src)

target_link_libraries(LiveWallpaper PRIVATE
    d3d11
    dxgi
    mf
    mfplat
    mfreadwrite
    mfuuid
    shlwapi
    shell32
    user32
    gdi32
    ole32
    comctl32
    comdlg32
)

# Unicode support
target_compile_definitions(LiveWallpaper PRIVATE UNICODE _UNICODE)

# Debug configuration
target_compile_options(LiveWallpaper PRIVATE
    $<$<CONFIG:Debug>:/Od /Zi /RTC1 /MDd /D_DEBUG>
)

# Release optimization flags
target_compile_options(LiveWallpaper PRIVATE
    $<$<CONFIG:Release>:/O2 /GL /GS- /Oi /fp:fast /MT /DNDEBUG>
)
target_link_options(LiveWallpaper PRIVATE
    $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS>
)
