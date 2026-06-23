cmake_minimum_required(VERSION 3.20)

project(rtmp_muduo_server VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(RTMP_BUILD_EXAMPLES "Build example applications" ON)

add_library(project_warnings INTERFACE)
target_compile_options(project_warnings
    INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>
)

add_subdirectory(src)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(apps/rtmp_server)

    if(RTMP_BUILD_EXAMPLES)
        add_subdirectory(examples/echo_server)
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
