cmake_minimum_required(VERSION 3.15)

# Project version
project(XenoComm
    VERSION 0.1.0
    DESCRIPTION "High-performance communication framework for AI agents"
    LANGUAGES CXX
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Generate compile_commands.json for tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Define build types
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo;MinSizeRel")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Werror")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
elseif(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /WX")
    set(CMAKE_CXX_FLAGS_DEBUG "/Od /Zi")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2")
endif()

# Add this line to disable gcc compatibility warnings for Abseil
add_compile_options(-Wno-gcc-compat)

# Options
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(BUILD_TESTING "Build tests" ON)
option(BUILD_EXAMPLES "Build examples" ON)
option(BUILD_DOCS "Build documentation" OFF)
option(ENABLE_COVERAGE "Enable coverage reporting" OFF)
option(BUILD_PYTHON_BINDINGS "Build Python bindings" ON)

# Include dependency management
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake)

# Version header configuration
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/xenocomm/version.hpp
    @ONLY
)

# Add subdirectories
add_subdirectory(src)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(BUILD_DOCS)
    add_subdirectory(docs)
endif()

if(BUILD_PYTHON_BINDINGS)
    add_subdirectory(bindings/python)
endif()

# Installation configuration
include(GNUInstallDirs)
set(INSTALL_CONFIGDIR ${CMAKE_INSTALL_LIBDIR}/cmake/XenoComm)

install(
    DIRECTORY include/xenocomm
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN "*.hpp.in" EXCLUDE
)

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/include/xenocomm/version.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xenocomm
)

# Export targets
install(
    EXPORT XenoCommTargets
    FILE XenoCommTargets.cmake
    NAMESPACE XenoComm::
    DESTINATION ${INSTALL_CONFIGDIR}
)

# Create and install config file
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/XenoCommConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/XenoCommConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/XenoCommConfig.cmake
    INSTALL_DESTINATION ${INSTALL_CONFIGDIR}
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/XenoCommConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/XenoCommConfigVersion.cmake
    DESTINATION ${INSTALL_CONFIGDIR}
)

# Configure protobuf for feedback data
if(USE_SYSTEM_PROTOBUF)
    find_package(Protobuf REQUIRED)
else()
    set(protobuf_BUILD_TESTS OFF CACHE BOOL "Build tests" FORCE)
    set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "Build examples" FORCE)
    set(protobuf_WITH_ZLIB ON CACHE BOOL "Build with zlib support" FORCE)
    FetchContent_MakeAvailable(protobuf)
endif()

# Add the generated proto files to xenocomm library
set(FEEDBACK_PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/feedback_data.proto
)

protobuf_generate(LANGUAGE cpp TARGET xenocomm PROTOS ${FEEDBACK_PROTO_FILES})

target_include_directories(xenocomm
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}  # For generated protobuf files
)

# Add zlib dependency
if(USE_SYSTEM_ZLIB)
    target_link_libraries(xenocomm PRIVATE ZLIB::ZLIB)
else()
    target_link_libraries(xenocomm PRIVATE zlibstatic)
endif() 