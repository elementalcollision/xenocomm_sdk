cmake_minimum_required(VERSION 3.15)
project(xenocomm_python)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find Python and pybind11
find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development)
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

# Link the Python module library (xenocomm) against pybind11 and the core C++ library
target_link_libraries(xenocomm PRIVATE pybind11::module xenocomm_core)

# Include directories (pybind11_add_module handles Python/pybind11 headers)
# Core library headers are needed
target_include_directories(xenocomm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include 
    # ${CMAKE_CURRENT_SOURCE_DIR}/include # This might not be needed if already in core?
)

# Set output directory for the Python module
set_target_properties(xenocomm PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python" # Output to build/python
    OUTPUT_NAME "_xenocomm"  # Create _xenocomm.so/.pyd
)

# Ensure the core library is built before the Python module
add_dependencies(xenocomm xenocomm_core)

# Install the Python module relative to the project install prefix
install(TARGETS xenocomm
        LIBRARY DESTINATION "." COMPONENT python 
        RUNTIME DESTINATION "." COMPONENT python
)

# Debugging output for pybind11 headers include path
get_target_property(PYBIND11_HEADERS_INCLUDES pybind11::headers INTERFACE_INCLUDE_DIRECTORIES)
message(STATUS "pybind11::headers INTERFACE_INCLUDE_DIRECTORIES: ${PYBIND11_HEADERS_INCLUDES}") 