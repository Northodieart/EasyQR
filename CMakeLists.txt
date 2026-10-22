cmake_minimum_required(VERSION 3.20)
project(EasyQR VERSION 1.4.1 LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "EasyQR targets Windows x64. Configure with -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MINGW)
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} --codepage=65001")
endif()

set(BUILD_READERS ON CACHE BOOL "" FORCE)
set(BUILD_WRITERS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_BLACKBOX_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_PYTHON_MODULE OFF CACHE BOOL "" FORCE)
set(BUILD_C_API OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/zxing-cpp EXCLUDE_FROM_ALL)
if(MINGW)
    target_compile_options(ZXing PRIVATE -w -ffunction-sections -fdata-sections)
endif()

add_executable(EasyQR WIN32
    src/main.cpp
    src/overlay.cpp
    src/render.cpp
    src/capture.cpp
    src/qr.cpp
    src/url.cpp
    src/util.cpp
    src/settings.cpp
    src/update.cpp
    src/resources.rc
    vendor/quirc/quirc.c
    vendor/quirc/decode.c
    vendor/quirc/identify.c
    vendor/quirc/version_db.c
)

target_include_directories(EasyQR PRIVATE src vendor/quirc)
set_source_files_properties(
    vendor/quirc/quirc.c
    vendor/quirc/decode.c
    vendor/quirc/identify.c
    vendor/quirc/version_db.c
    PROPERTIES COMPILE_OPTIONS "-w"
)
target_compile_definitions(EasyQR PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
)
target_compile_options(EasyQR PRIVATE
    -O2
    -Wall
    -Wextra
    -Wno-unused-parameter
    -ffunction-sections
    -fdata-sections
    -finput-charset=UTF-8
)
target_link_options(EasyQR PRIVATE
    -municode
    -static
    -Wl,--gc-sections
    -s
)
target_link_libraries(EasyQR PRIVATE
    d2d1
    dwrite
    dwmapi
    d3d11
    dxgi
    dcomp
    winhttp
    shell32
    user32
    gdi32
    ole32
    uuid
    advapi32
    ZXing
)

set_target_properties(EasyQR PROPERTIES OUTPUT_NAME "EasyQR")
