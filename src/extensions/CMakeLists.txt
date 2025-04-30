# Extensions module sources will be added here
# Currently all sources are listed in the parent CMakeLists.txt 

# Extensions module CMakeLists.txt
target_sources(xenocomm
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/emergence_manager.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/common_ground.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/boundary_gateway.cpp
) 