include(FetchContent)

# Fetch and configure Google Test
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Fetch and configure Catch2
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.2 # Using a recent v3 tag
)
FetchContent_MakeAvailable(Catch2)

# Enable testing for this directory and below
enable_testing()

# Function to add test executable with common settings (GTest)
function(add_xenocomm_test TEST_NAME)
  add_executable(${TEST_NAME} ${ARGN})
  target_link_libraries(${TEST_NAME} PRIVATE
    GTest::gtest_main
    GTest::gmock_main
    xenocomm_core
  )
  target_include_directories(${TEST_NAME} PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
  )
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# Function to add Catch2 test executable with common settings
function(add_xenocomm_catch2_test TEST_NAME)
  add_executable(${TEST_NAME} ${ARGN})
  target_link_libraries(${TEST_NAME} PRIVATE
    Catch2::Catch2WithMain
    xenocomm_core
  )
  target_include_directories(${TEST_NAME} PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    # Catch2 itself doesn't usually need explicit include dirs if using target_link_libraries
  )
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# Add test subdirectories
add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(performance)

# Enable test coverage if requested
if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(xenocomm PRIVATE --coverage)
        target_link_options(xenocomm PRIVATE --coverage)
        
        # Add coverage target
        add_custom_target(coverage
            COMMAND gcovr -r ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR}
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating code coverage report..."
        )
    endif()
endif() 