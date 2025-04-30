# Add library target
add_library(xenocomm
    # Core module sources
    core/connection_manager.cpp
    core/capability_signaler.cpp
    core/negotiation_protocol.cpp
    core/data_transcoder.cpp
    core/transmission_manager.cpp
    core/feedback_loop.cpp

    # Utils module sources
    utils/error_handling.cpp
    utils/logging.cpp
    utils/config.cpp
    utils/serialization.cpp

    # Extensions module sources
    extensions/emergence_manager.cpp
    extensions/common_ground.cpp
    extensions/boundary_gateway.cpp
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