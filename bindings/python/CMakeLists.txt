cmake_minimum_required(VERSION 3.15)
project(xenocomm_python)

# Find Python and pybind11
find_package(Python COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

# Add the bindings library
pybind11_add_module(xenocomm 
    src/bindings.cpp
    src/connection_manager.cpp
    src/capability_signaler.cpp
    src/negotiation_protocol.cpp
    src/data_transcoder.cpp
    src/transmission_manager.cpp
    src/feedback_loop.cpp
)

# Link against the main xenocomm library
target_link_libraries(xenocomm PRIVATE xenocomm_lib)

# Include directories
target_include_directories(xenocomm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Set output directory
set_target_properties(xenocomm PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/xenocomm"
    OUTPUT_NAME "_core"  # This will create _core.so/.pyd
) 