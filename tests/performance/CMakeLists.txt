# Set test category
set(TEST_CATEGORY "performance")

# Only proceed if benchmarks are enabled
if(BUILD_BENCHMARKS)
    # Function to add performance test
    function(add_xenocomm_benchmark BENCH_NAME)
        add_executable(${BENCH_NAME} ${ARGN})
        target_link_libraries(${BENCH_NAME} PRIVATE
            benchmark::benchmark
            benchmark::benchmark_main
            xenocomm_core
        )
        target_include_directories(${BENCH_NAME} PRIVATE
            ${PROJECT_SOURCE_DIR}/include
            ${PROJECT_SOURCE_DIR}/src
        )
    endfunction()

    # Core performance tests
    file(GLOB_RECURSE CORE_BENCH_SOURCES "core/*.cpp")
    if(CORE_BENCH_SOURCES)
        add_xenocomm_benchmark(core_benchmarks ${CORE_BENCH_SOURCES})
    endif()

    # Network performance tests
    file(GLOB_RECURSE NETWORK_BENCH_SOURCES "network/*.cpp")
    if(NETWORK_BENCH_SOURCES)
        add_xenocomm_benchmark(network_benchmarks ${NETWORK_BENCH_SOURCES})
    endif()

    # Protocol performance tests
    file(GLOB_RECURSE PROTOCOL_BENCH_SOURCES "protocol/*.cpp")
    if(PROTOCOL_BENCH_SOURCES)
        add_xenocomm_benchmark(protocol_benchmarks ${PROTOCOL_BENCH_SOURCES})
    endif()
endif() 