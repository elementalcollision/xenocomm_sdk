# Set test category
set(TEST_CATEGORY "integration")

# Add integration tests as they are created
# Example:
# add_xenocomm_test(test_full_communication
#     full_communication_test.cpp
# )

# Integration Tests Configuration

# System integration tests
file(GLOB_RECURSE SYSTEM_TEST_SOURCES "system/*.cpp")
if(SYSTEM_TEST_SOURCES)
    add_xenocomm_test(system_integration_tests ${SYSTEM_TEST_SOURCES})
endif()

# Network integration tests
file(GLOB_RECURSE NETWORK_TEST_SOURCES "network/*.cpp")
if(NETWORK_TEST_SOURCES)
    add_xenocomm_test(network_integration_tests ${NETWORK_TEST_SOURCES})
endif()

# Protocol integration tests
file(GLOB_RECURSE PROTOCOL_TEST_SOURCES "protocol/*.cpp")
if(PROTOCOL_TEST_SOURCES)
    add_xenocomm_test(protocol_integration_tests ${PROTOCOL_TEST_SOURCES})
endif() 