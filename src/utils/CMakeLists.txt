# Utils module sources will be added here
# Currently all sources are listed in the parent CMakeLists.txt 

# Utils module CMakeLists.txt
target_sources(xenocomm
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/error_handling.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/logging.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/config.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/serialization.cpp
) 