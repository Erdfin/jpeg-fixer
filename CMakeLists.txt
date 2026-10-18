cmake_minimum_required(VERSION 3.20)

project(Main LANGUAGES CXX)

add_executable(main main.cpp)

# C++23
set_target_properties(main PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

# OpenMP
find_package(OpenMP REQUIRED COMPONENTS CXX)
target_link_libraries(main PRIVATE OpenMP::OpenMP_CXX)

# Compiler-specific optimizations
if(MSVC)
    # MSVC (Visual Studio)
    target_compile_options(main PRIVATE
        /O2
        /arch:AVX2
    )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC
    target_compile_options(main PRIVATE
        -O3
        -march=native
        -mtune=native
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Clang
    target_compile_options(main PRIVATE
        -O3
        -march=native
        -mtune=native
    )
endif()
