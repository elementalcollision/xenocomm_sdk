include(FetchContent)

# Options for dependency management
option(USE_SYSTEM_GTEST "Use system-installed Google Test" OFF)
option(USE_SYSTEM_PROTOBUF "Use system-installed Protocol Buffers" OFF)
option(USE_SYSTEM_OPENSSL "Use system-installed OpenSSL" ON)
option(USE_SYSTEM_PYBIND11 "Use system-installed pybind11" OFF)
option(USE_SYSTEM_BENCHMARK "Use system-installed Google Benchmark" OFF)
option(BUILD_BENCHMARKS "Build performance benchmarks" ON)

# Required dependencies
find_package(Threads REQUIRED)
find_package(OpenSSL 1.1.1 REQUIRED)

# Google Test
if(BUILD_TESTING)
    if(USE_SYSTEM_GTEST)
        find_package(GTest REQUIRED)
    else()
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.14.0
        )
        # Prevent overriding parent project's compiler/linker settings on Windows
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()
endif()

# Protocol Buffers (for configuration and metadata)
if(USE_SYSTEM_PROTOBUF)
    find_package(Protobuf REQUIRED)
else()
    FetchContent_Declare(
        protobuf
        GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
        GIT_TAG v25.3
    )
    set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(protobuf)
endif()

# pybind11 (for Python bindings)
if(BUILD_PYTHON_BINDINGS)
    if(USE_SYSTEM_PYBIND11)
        find_package(pybind11 REQUIRED)
    else()
        FetchContent_Declare(
            pybind11
            GIT_REPOSITORY https://github.com/pybind/pybind11.git
            GIT_TAG v2.11.1
        )
        FetchContent_MakeAvailable(pybind11)
    endif()
endif()

# Google Benchmark
if(BUILD_BENCHMARKS)
    if(USE_SYSTEM_BENCHMARK)
        find_package(benchmark REQUIRED)
    else()
        FetchContent_Declare(
            benchmark
            GIT_REPOSITORY https://github.com/google/benchmark.git
            GIT_TAG v1.8.3
        )
        set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
        set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(benchmark)
    endif()
endif()

# Reed-Solomon error correction library
option(USE_SYSTEM_REED_SOLOMON "Use system-installed Reed-Solomon library" OFF)
if(USE_SYSTEM_REED_SOLOMON)
    find_package(reed_solomon_erasure REQUIRED)
else()
    FetchContent_Declare(
        reed_solomon_erasure
        GIT_REPOSITORY https://github.com/klayoutmatthias/reed-solomon-erasure.git
        GIT_TAG v1.2.3
    )
    set(BUILD_REED_SOLOMON_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(reed_solomon_erasure)
endif()

# zlib (for data compression)
option(USE_SYSTEM_ZLIB "Use system-installed zlib" ON)
if(USE_SYSTEM_ZLIB)
    find_package(ZLIB REQUIRED)
else()
    FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3
    )
    FetchContent_MakeAvailable(zlib)
endif()

# Function to check version compatibility
function(check_min_version package_name min_version)
    if(${${package_name}_VERSION} VERSION_LESS ${min_version})
        message(FATAL_ERROR "${package_name} version ${${package_name}_VERSION} found, but version ${min_version} or higher is required")
    endif()
endfunction()

# Function to find system package with fallback to FetchContent
function(find_package_or_fetch package_name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG
        ""
        "VERSION;REPOSITORY;TAG"
        ""
    )
    
    if(USE_SYSTEM_${package_name})
        find_package(${package_name} ${ARG_VERSION})
        if(${package_name}_FOUND)
            message(STATUS "Using system ${package_name} version ${${package_name}_VERSION}")
            return()
        endif()
    endif()
    
    message(STATUS "Fetching ${package_name} from repository")
    FetchContent_Declare(
        ${package_name}
        GIT_REPOSITORY ${ARG_REPOSITORY}
        GIT_TAG ${ARG_TAG}
    )
    FetchContent_MakeAvailable(${package_name})
endfunction() 