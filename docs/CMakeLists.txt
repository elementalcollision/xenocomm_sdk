# Find Doxygen package
find_package(Doxygen)

if(DOXYGEN_FOUND)
    # Configure Doxyfile
    set(DOXYGEN_INPUT_DIR ${CMAKE_SOURCE_DIR}/include)
    set(DOXYGEN_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/api)
    set(DOXYGEN_INDEX_FILE ${DOXYGEN_OUTPUT_DIR}/html/index.html)
    set(DOXYFILE_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    # Replace variables in Doxyfile.in
    configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} @ONLY)

    # Add documentation targets
    add_custom_command(
        OUTPUT ${DOXYGEN_INDEX_FILE}
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
        MAIN_DEPENDENCY ${DOXYFILE_OUT} ${DOXYFILE_IN}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
    add_custom_target(docs ALL DEPENDS ${DOXYGEN_INDEX_FILE})

    # Install documentation
    install(
        DIRECTORY ${DOXYGEN_OUTPUT_DIR}/html
        DESTINATION ${CMAKE_INSTALL_DOCDIR}
    )
else()
    message(STATUS "Doxygen not found, documentation will not be generated")
endif() 