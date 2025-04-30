# Core module sources will be added here
# Currently all sources are listed in the parent CMakeLists.txt 

# Generate protobuf files
protobuf_generate_cpp(FEEDBACK_PROTO_SRCS FEEDBACK_PROTO_HDRS
    ${CMAKE_CURRENT_SOURCE_DIR}/feedback_data.proto
)

# Core module CMakeLists.txt
target_sources(xenocomm
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/capability_signaler.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/connection_manager.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/data_transcoder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/feedback_loop.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/negotiation_protocol.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/transmission_manager.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/capability_index.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/capability_cache.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/in_memory_capability_signaler.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/error_correction.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/security_manager.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/secure_transport_wrapper.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/authentication_manager.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/certificate_auth_provider.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/token_auth_provider.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/feedback_integration.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/strategy_adapter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/compressed_state_adapter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/compression_algorithms.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/adapter_registry.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ggwave_fsk_adapter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/binary_custom_adapter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/data_adapters.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/parameter_fallback.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/timeout_negotiation_protocol.cpp
        ${FEEDBACK_PROTO_SRCS}
)

# Add include directories for generated protobuf files
target_include_directories(xenocomm
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
)

# Link against required libraries
target_link_libraries(xenocomm
    PRIVATE
        reed_solomon_erasure
        protobuf::libprotobuf
        ZLIB::ZLIB
        OpenSSL::SSL
        OpenSSL::Crypto
) 