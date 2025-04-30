# Configure protobuf
if(USE_SYSTEM_PROTOBUF)
    find_package(Protobuf REQUIRED)
else()
    set(protobuf_BUILD_TESTS OFF CACHE BOOL "Build tests" FORCE)
    set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "Build examples" FORCE)
    set(protobuf_WITH_ZLIB ON CACHE BOOL "Build with zlib support" FORCE)
    FetchContent_MakeAvailable(protobuf)
endif()

# Add library target
add_library(xenocomm
    # Core module sources
    core/connection_manager.cpp
    core/capability_signaler.cpp
    core/negotiation_protocol.cpp
    core/data_transcoder.cpp
    core/transmission_manager.cpp
    core/feedback_loop.cpp
    core/protocol_variant.cpp

    # Utils module sources
    utils/error_handling.cpp
    utils/logging.cpp
    utils/config.cpp
    utils/serialization.cpp

    # Extensions module sources
    extensions/emergence_manager.cpp
    extensions/common_ground.cpp
    extensions/boundary_gateway.cpp
    extensions/compatibility_checker.cpp
    extensions/rollback_manager.cpp
)

# Add alias target for use in other parts of the build
add_library(XenoComm::xenocomm ALIAS xenocomm)

# Set target properties
target_compile_features(xenocomm PRIVATE cxx_std_17)

target_include_directories(xenocomm
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Set target compile options
target_compile_options(xenocomm
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
target_link_libraries(xenocomm
    PUBLIC
        protobuf::libprotobuf
        nlohmann_json
    PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
        ZLIB::ZLIB
)

# Set properties
set_target_properties(xenocomm PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
)

# Add subdirectories for modules
add_subdirectory(core)
add_subdirectory(utils)
add_subdirectory(extensions)

# Install targets
install(
    TARGETS xenocomm
    EXPORT XenoCommTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
) 