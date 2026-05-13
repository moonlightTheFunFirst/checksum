cmake_minimum_required(VERSION 3.20)

project(checksumTool LANGUAGES CXX)

add_executable(checksumTool
    src/main.cpp
)

target_compile_features(checksumTool PRIVATE cxx_std_17)
set_target_properties(checksumTool PROPERTIES
    CXX_EXTENSIONS OFF
)

if (MSVC)
    target_compile_options(checksumTool PRIVATE /W4 /permissive-)
else()
    target_compile_options(checksumTool PRIVATE -Wall -Wextra -Wpedantic)
endif()
