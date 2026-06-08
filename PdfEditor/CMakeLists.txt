cmake_minimum_required(VERSION 3.16)
project(PdfEditor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Qt 6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick)
qt_standard_project_setup()
qt_policy(SET QTP0001 NEW)

include(FetchContent)

# Setup spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.13.0
)
FetchContent_MakeAvailable(spdlog)

# Setup PDFium (using pre-built binaries for Windows x64)
# We download the archive and create an imported target
set(PDFIUM_URL "https://github.com/bblanchon/pdfium-binaries/releases/download/chromium/7869/pdfium-win-x64.tgz")
FetchContent_Declare(
    pdfium
    URL ${PDFIUM_URL}
)
FetchContent_MakeAvailable(pdfium)

# PDFium comes with precompiled libs in the `lib` folder and headers in `include`.
# We need to manually add an imported target since the tarball doesn't have CMake config.
add_library(pdfium_lib STATIC IMPORTED)
set_target_properties(pdfium_lib PROPERTIES
    IMPORTED_LOCATION "${pdfium_SOURCE_DIR}/lib/pdfium.dll.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${pdfium_SOURCE_DIR}/include"
)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering
)

# Executable
qt_add_executable(${PROJECT_NAME}
    MANUAL_FINALIZATION
    src/main.cpp
    src/core/PdfCore.cpp
    src/core/PdfTextEngine.cpp
    src/core/TextSelectionModel.cpp
    src/core/PdfExporter.cpp
    src/core/OverlayItem.cpp
    src/core/OverlayModel.cpp
    src/core/ModifyTextCommand.cpp
    src/rendering/TileCache.cpp
    src/rendering/RenderEngine.cpp
    src/ui/PdfView.cpp
    src/ui/PdfExportWorker.cpp
    src/resources.rc
)

# QML Module
qt_add_qml_module(${PROJECT_NAME}
    URI PdfEditor
    VERSION 1.0
    QML_FILES
        src/qml/Main.qml
        src/qml/TextOverlayItem.qml
        src/qml/HighlightOverlayItem.qml
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    pdfium_lib
    spdlog::spdlog
)
if(MINGW)
    target_link_options(${PROJECT_NAME} PRIVATE -static-libgcc -static-libstdc++)
endif()

# Finalize Qt executable (needed for Qt6)
qt_finalize_executable(${PROJECT_NAME})

# Copy pdfium.dll to output folder
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${pdfium_SOURCE_DIR}/bin/pdfium.dll"
    $<TARGET_FILE_DIR:${PROJECT_NAME}>
)

# Setup GoogleTest
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(PdfEditorTests
    tests/test_pdf_core.cpp
    tests/test_pdf_exporter.cpp
    src/core/PdfCore.cpp
)
target_link_libraries(PdfEditorTests PRIVATE
    GTest::gtest_main
    pdfium_lib
    spdlog::spdlog
)
if(MINGW)
    target_link_options(PdfEditorTests PRIVATE -static-libgcc -static-libstdc++)
endif()

# Copy pdfium.dll to test output folder
add_custom_command(TARGET PdfEditorTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${pdfium_SOURCE_DIR}/bin/pdfium.dll"
    $<TARGET_FILE_DIR:PdfEditorTests>
)

include(GoogleTest)
gtest_discover_tests(PdfEditorTests)
