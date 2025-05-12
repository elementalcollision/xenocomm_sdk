cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0148 NEW)

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
    # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Werror") # Temporarily disable -Werror
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
elseif(MSVC)
    # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /WX") # Temporarily disable /WX
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
    set(CMAKE_CXX_FLAGS_DEBUG "/Od /Zi")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2")
endif()

# Add this line to disable gcc compatibility warnings for Abseil
add_compile_options(-Wno-gcc-compat)
# Ignore unused/unknown compiler arguments (e.g., -msse4.1 on ARM)
add_compile_options(-Qunused-arguments)

# Options
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(BUILD_TESTING "Build tests" ON)
option(BUILD_EXAMPLES "Build examples" ON)
option(BUILD_DOCS "Build documentation" OFF)
option(ENABLE_COVERAGE "Enable coverage reporting" OFF)
option(BUILD_PYTHON_BINDINGS "Build Python bindings" ON)

# Include dependency management
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake)

# Explicitly Fetch Abseil FIRST
# FetchContent_Declare(
#     absl
#     GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
#     GIT_TAG 20240722.0 # Use a recent tag compatible with Protobuf v25.3
# )
# FetchContent_MakeAvailable(absl)

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

# add_subdirectory(bindings/python)

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

# Install nlohmann_json and add it to the export set
install(TARGETS nlohmann_json
    EXPORT XenoCommTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    # INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} # Typically header-only, might not be needed
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
    
    # Patch Abseil to disable HWAES on ARM (May not be needed if fetched separately, but keep for now)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
        FetchContent_GetProperties(protobuf)
        if(NOT protobuf_POPULATED)
            FetchContent_Populate(protobuf)
            # We might need to check if absl source dir exists from the explicit fetch before patching?
            # Let's assume patching here is okay for now.
            file(WRITE "${protobuf_SOURCE_DIR}/third_party/abseil-cpp/absl/random/internal/randen_hwaes.cc"
                "// HWAES disabled on ARM\n"
                "#include \"absl/random/internal/randen_hwaes.h\"\n"
                "namespace absl {\n"
                "namespace random_internal {\n"
                "void RandenHwAes::Generate(void*, const void*, void*) {}\n"
                "bool HasRandenHwAes() { return false; }\n"
                "} // namespace random_internal\n"
                "} // namespace absl\n"
            )
        endif()
    endif()
    
    # Force-clear Abseil HWAES flags (May not be needed if fetched separately)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
        set(ABSL_RANDOM_HWAES_ARM64_FLAGS "" CACHE STRING "" FORCE)
    endif()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
        set(ABSL_RANDOM_HWAES_X64_FLAGS "" CACHE STRING "" FORCE)
    endif()
    
    FetchContent_MakeAvailable(protobuf)
    
    # Removed BUILD_SHARED_LIBS restore

endif()

# Disable specific warnings for Protobuf/Abseil targets when built via FetchContent
if(NOT USE_SYSTEM_PROTOBUF)
    # Note: Warning suppressions for dependencies are handled globally via
    # CMAKE_CXX_FLAGS or add_compile_options(-Wno-...) earlier in this file.
    # Setting target-specific properties for FetchContent targets is unreliable here.
endif()

# Add the generated proto files to xenocomm library
set(FEEDBACK_PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/feedback_data.proto
)

# Add zlib dependency
if(USE_SYSTEM_ZLIB)
    target_link_libraries(xenocomm_core PRIVATE ZLIB::ZLIB)
else()
    target_link_libraries(xenocomm_core PRIVATE zlibstatic)
endif() 