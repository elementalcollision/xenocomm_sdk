#include <benchmark/benchmark.h>
#include "xenocomm/core/error_correction.h"
#include <random>
#include <algorithm>

namespace xenocomm {
namespace core {
namespace {

// Helper function to generate random data
std::vector<uint8_t> generateRandomData(size_t size) {
    std::vector<uint8_t> data(size);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    std::generate(data.begin(), data.end(), [&]() { return dis(gen); });
    return data;
}

// Helper function to corrupt data with bit errors
void corruptData(std::vector<uint8_t>& data, size_t numErrors) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> posDis(0, data.size() - 1);
    std::uniform_int_distribution<> bitDis(0, 7);
    
    for (size_t i = 0; i < numErrors; i++) {
        size_t pos = posDis(gen);
        uint8_t bit = 1 << bitDis(gen);
        data[pos] ^= bit; // Flip a random bit
    }
}

static void BM_CRC32_Encode(benchmark::State& state) {
    const size_t dataSize = state.range(0);
    auto data = generateRandomData(dataSize);
    CRC32ErrorDetection crc;
    
    for (auto _ : state) {
        benchmark::DoNotOptimize(crc.encode(data));
    }
    
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(dataSize));
}

static void BM_CRC32_Decode(benchmark::State& state) {
    const size_t dataSize = state.range(0);
    auto data = generateRandomData(dataSize);
    CRC32ErrorDetection crc;
    auto encoded = crc.encode(data);
    
    for (auto _ : state) {
        benchmark::DoNotOptimize(crc.decode(encoded));
    }
    
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(encoded.size()));
}

static void BM_ReedSolomon_Encode(benchmark::State& state) {
    const size_t dataSize = state.range(0);
    auto data = generateRandomData(dataSize);
    
    ReedSolomonCorrection::Config config;
    config.data_shards = 10;
    config.parity_shards = 4;
    config.enable_interleaving = false;
    ReedSolomonCorrection rs(config);
    
    for (auto _ : state) {
        benchmark::DoNotOptimize(rs.encode(data));
    }
    
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(dataSize));
}

static void BM_ReedSolomon_Decode(benchmark::State& state) {
    const size_t dataSize = state.range(0);
    auto data = generateRandomData(dataSize);
    
    ReedSolomonCorrection::Config config;
    config.data_shards = 10;
    config.parity_shards = 4;
    config.enable_interleaving = false;
    ReedSolomonCorrection rs(config);
    
    auto encoded = rs.encode(data);
    
    for (auto _ : state) {
        benchmark::DoNotOptimize(rs.decode(encoded));
    }
    
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(encoded.size()));
}

static void BM_ReedSolomon_WithInterleaving(benchmark::State& state) {
    const size_t dataSize = state.range(0);
    auto data = generateRandomData(dataSize);
    
    ReedSolomonCorrection::Config config;
    config.data_shards = 10;
    config.parity_shards = 4;
    config.enable_interleaving = true;
    ReedSolomonCorrection rs(config);
    
    auto encoded = rs.encode(data);
    corruptData(encoded, 5); // Add some errors
    
    for (auto _ : state) {
        benchmark::DoNotOptimize(rs.decode(encoded));
    }
    
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(encoded.size()));
}

// Register benchmarks with different data sizes
BENCHMARK(BM_CRC32_Encode)
    ->RangeMultiplier(4)
    ->Range(1024, 1024*1024);

BENCHMARK(BM_CRC32_Decode)
    ->RangeMultiplier(4)
    ->Range(1024, 1024*1024);

BENCHMARK(BM_ReedSolomon_Encode)
    ->RangeMultiplier(4)
    ->Range(1024, 1024*1024);

BENCHMARK(BM_ReedSolomon_Decode)
    ->RangeMultiplier(4)
    ->Range(1024, 1024*1024);

BENCHMARK(BM_ReedSolomon_WithInterleaving)
    ->RangeMultiplier(4)
    ->Range(1024, 1024*1024);

} // namespace
} // namespace core
} // namespace xenocomm

BENCHMARK_MAIN(); 