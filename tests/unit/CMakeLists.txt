# Set test category
set(TEST_CATEGORY "unit")

# Add unit tests as they are created
# Example:
# add_xenocomm_test(test_connection_manager
#     connection_manager_test.cpp
# )

# Unit Tests Configuration

# Core module tests
file(GLOB_RECURSE CORE_TEST_SOURCES "core/*.cpp")
if(CORE_TEST_SOURCES)
    add_xenocomm_test(core_tests ${CORE_TEST_SOURCES})
endif()

# Utils module tests
file(GLOB_RECURSE UTILS_TEST_SOURCES "utils/*.cpp")
if(UTILS_TEST_SOURCES)
    add_xenocomm_test(utils_tests ${UTILS_TEST_SOURCES})
endif()

# Extensions module tests
file(GLOB_RECURSE EXTENSIONS_TEST_SOURCES "extensions/*.cpp")
if(EXTENSIONS_TEST_SOURCES)
    add_xenocomm_test(extensions_tests ${EXTENSIONS_TEST_SOURCES})
endif()

# Explicitly add EmergenceManager test as its own executable
add_xenocomm_test(test_emergence_manager extensions/test_emergence_manager.cpp) 