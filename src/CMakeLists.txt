# Configure protobuf
# Use find_package for libraries, explicitly find protoc executable
# find_package(Protobuf REQUIRED)

# Define PROTOC_EXECUTABLE based on whether we used FetchContent or system
# Check if the protobuf::protoc target exists (created by FetchContent)
if(TARGET protobuf::protoc)
    set(PROTOC_EXECUTABLE $<TARGET_FILE:protobuf::protoc>)
    set(PROTOC_TARGET protobuf::protoc) # Store target for dependency
    message(STATUS "Using protoc from FetchContent target: ${PROTOC_EXECUTABLE}")
else()
    # Fallback to find_program if not using FetchContent or target doesn't exist
    find_program(PROTOC_EXECUTABLE NAMES protoc HINTS ENV Protobuf_ROOT ${Protobuf_RUNTIME_LIBRARY_DIRS})
    if(NOT PROTOC_EXECUTABLE)
        message(FATAL_ERROR "protoc executable not found.")
    endif()
    set(PROTOC_TARGET ${PROTOC_EXECUTABLE}) # Use executable path as dependency (less ideal)
    message(STATUS "Using system protoc: ${PROTOC_EXECUTABLE}")
endif()

# Define proto source and output directory
set(PROTO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/core/feedback_data.proto)
set(PROTO_OUT_DIR ${CMAKE_BINARY_DIR}/generated_proto) # Use CMAKE_BINARY_DIR
file(MAKE_DIRECTORY ${PROTO_OUT_DIR})

# Define generated source files
set(GENERATED_PROTO_SRCS
    ${PROTO_OUT_DIR}/feedback_data.pb.cc
)
set(GENERATED_PROTO_HDRS
    ${PROTO_OUT_DIR}/feedback_data.pb.h
)

# Custom command to generate protobuf files
add_custom_command(
    OUTPUT ${GENERATED_PROTO_SRCS} ${GENERATED_PROTO_HDRS}
    COMMAND ${PROTOC_EXECUTABLE} # Use the determined executable path/target file
    ARGS --cpp_out=${PROTO_OUT_DIR} -I ${CMAKE_CURRENT_SOURCE_DIR}/core ${PROTO_SRC}
    DEPENDS ${PROTO_SRC} ${PROTOC_TARGET} # Depend on the target or executable
    COMMENT "Generating C++ protocol buffer code from ${PROTO_SRC}"
    VERBATIM
)

# Custom target to ensure generation happens
add_custom_target(feedback_proto_gen ALL DEPENDS ${GENERATED_PROTO_SRCS} ${GENERATED_PROTO_HDRS})

# Add library target with ALL sources listed here
add_library(xenocomm_core SHARED
    # Core module sources
    core/connection_manager.cpp
    core/capability_signaler.cpp
    core/negotiation_protocol.cpp
    core/data_transcoder.cpp
    core/transmission_manager.cpp
    core/feedback_loop.cpp
    core/protocol_variant.cpp
    core/base64_transcoder.cpp
    core/capability_cache.cpp
    core/capability_index.cpp
    core/error_correction.cpp
    core/parameter_fallback.cpp
    core/udp_transport.cpp
    core/secure_transport_wrapper.cpp
    core/security_manager.cpp
    core/metrics_collector.cpp
    core/canary_deployment.cpp
    core/circuit_breaker.cpp
    core/key_manager.cpp
    core/token_auth_provider.cpp
    core/certificate_auth_provider.cpp
    core/authentication_manager.cpp
    core/feedback_integration.cpp
    core/strategy_adapter.cpp
    core/compressed_state_adapter.cpp
    core/compression_algorithms.cpp
    core/adapter_registry.cpp
    core/ggwave_fsk_adapter.cpp
    core/binary_custom_adapter.cpp
    core/data_adapters.cpp
    core/timeout_negotiation_protocol.cpp
    core/in_memory_capability_signaler.cpp
    core/tcp_transport.cpp

    # Utils module sources
    utils/error_handling.cpp
    utils/logging.cpp
    utils/config.cpp
    utils/serialization.cpp
    # Add ALL utils sources here

    # Extensions module sources
    # extensions/emergence_manager.cpp // Temporarily commented out
    extensions/common_ground.cpp
    extensions/boundary_gateway.cpp
    extensions/compatibility_checker.cpp
    extensions/rollback_manager.cpp
    # Add ALL extensions sources here

    # Add generated sources here
    ${GENERATED_PROTO_SRCS}
)

# Add alias target
add_library(XenoComm::xenocomm_core ALIAS xenocomm_core)

# Add dependency on protobuf generation
add_dependencies(xenocomm_core feedback_proto_gen)

# Set target compile features
target_compile_features(xenocomm_core PRIVATE cxx_std_17)

# Include directories - PRIVATE first for internal build
target_include_directories(xenocomm_core 
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include 
        ${CMAKE_BINARY_DIR}/include # For version.hpp
        ${PROTO_OUT_DIR} 
        ${OpenSSL_INCLUDE_DIR}
)
# PUBLIC/INTERFACE for consumers
target_include_directories(xenocomm_core
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Set target compile options
target_compile_options(xenocomm_core
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:
            -Wall
            -Wextra
            -Wpedantic
            -Werror
        >
        $<$<CXX_COMPILER_ID:MSVC>:
            /W4
            /WX
        >
)

# Link dependencies
target_link_libraries(xenocomm_core
    PUBLIC
        protobuf::libprotobuf
        nlohmann_json
        OpenSSL::SSL
        OpenSSL::Crypto
    PRIVATE
        $<$<BOOL:${USE_SYSTEM_ZLIB}>:ZLIB::ZLIB>
        $<$<NOT:$<BOOL:${USE_SYSTEM_ZLIB}>>:zlibstatic>
        # Add the list from protobuf_ABSL_USED_TARGETS again
        absl::absl_check
        absl::absl_log
        absl::algorithm
        absl::base
        absl::bind_front
        absl::bits
        absl::btree
        absl::cleanup
        absl::cord
        absl::core_headers
        absl::debugging
        absl::die_if_null
        absl::dynamic_annotations
        absl::flags
        absl::flat_hash_map
        absl::flat_hash_set
        absl::function_ref
        absl::hash
        absl::layout
        absl::log_initialize
        absl::log_severity
        absl::memory
        absl::node_hash_map
        absl::node_hash_set
        absl::optional
        absl::span
        absl::status
        absl::statusor
        absl::strings
        absl::synchronization
        absl::time
        absl::type_traits
        absl::utility
        absl::variant
)

# Set properties
set_target_properties(xenocomm_core PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
)

# Install targets
install(
    TARGETS xenocomm_core
    EXPORT XenoCommTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
) 