cmake_minimum_required(VERSION 3.20)
project(EasyQRTests LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(QUIRC_SOURCES
    ../vendor/quirc/quirc.c
    ../vendor/quirc/decode.c
    ../vendor/quirc/identify.c
    ../vendor/quirc/version_db.c
)

set(BUILD_READERS ON CACHE BOOL "" FORCE)
set(BUILD_WRITERS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_BLACKBOX_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_PYTHON_MODULE OFF CACHE BOOL "" FORCE)
set(BUILD_C_API OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(../vendor/zxing-cpp zxing-cpp EXCLUDE_FROM_ALL)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ZXing PRIVATE -w -ffunction-sections -fdata-sections)
endif()

add_executable(url_tests url_tests.cpp ../src/url.cpp)
target_include_directories(url_tests PRIVATE ../src)

add_executable(qr_probe qr_probe.cpp ../src/qr.cpp ${QUIRC_SOURCES})
target_include_directories(qr_probe PRIVATE ../src ../vendor/quirc)
target_link_libraries(qr_probe PRIVATE ZXing)
target_compile_options(qr_probe PRIVATE -w)
