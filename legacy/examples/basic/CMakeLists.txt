add_executable(basic_example
    main.cpp
)

target_link_libraries(basic_example
    PRIVATE
        xenocomm_core
)

target_include_directories(basic_example
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
) 