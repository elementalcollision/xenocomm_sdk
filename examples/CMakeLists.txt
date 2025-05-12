# Function to add example executable
function(add_xenocomm_example EXAMPLE_NAME EXAMPLE_SOURCES)
    add_executable(${EXAMPLE_NAME} ${EXAMPLE_SOURCES})
    target_link_libraries(${EXAMPLE_NAME}
        PRIVATE
            XenoComm::xenocomm_core
    )
    target_compile_features(${EXAMPLE_NAME} PRIVATE cxx_std_17)
    
    # Add example to CTest
    add_test(NAME ${EXAMPLE_NAME}_test
        COMMAND ${EXAMPLE_NAME} --test
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    set_tests_properties(${EXAMPLE_NAME}_test
        PROPERTIES
        LABELS "example"
    )
    
    # Install example
    install(TARGETS ${EXAMPLE_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/examples
    )
endfunction()

# Add example subdirectories
add_subdirectory(basic)
add_xenocomm_example(metrics_dashboard metrics_dashboard.cpp) 