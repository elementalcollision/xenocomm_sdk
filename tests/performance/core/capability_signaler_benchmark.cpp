#include "xenocomm/core/capability_signaler.h"
#include "xenocomm/core/version.h"
#include <benchmark/benchmark.h>
#include <random>
#include <string>
#include <vector>
#include <memory> // Include for std::unique_ptr

using namespace xenocomm::core;

// Define missing types (placeholder definitions - replace with actual ones if they exist)
namespace xenocomm::core {
    enum class MatchCriteria {
        EXACT, // Assuming default is exact match
        NAME_ONLY, 
        VERSION_COMPATIBLE
    };
    
    struct VersionFilter {
        bool checkMinVersion = false;
        Version minVersion;
        // Add other filter options if needed
    };
} // namespace xenocomm::core

namespace {

// Helper to generate random capabilities
class CapabilityGenerator {
public:
    CapabilityGenerator() : rng_(std::random_device{}()) {}

    Capability generate() {
        Capability cap;
        cap.name = "capability_" + std::to_string(nameDist_(rng_));
        cap.version = Version(
            versionDist_(rng_),  // major
            versionDist_(rng_),  // minor
            versionDist_(rng_)   // patch
        );
        return cap;
    }

private:
    std::mt19937 rng_;
    std::uniform_int_distribution<> nameDist_{1, 100};     // 100 different capability names
    std::uniform_int_distribution<> versionDist_{0, 5};    // Version components 0-5
};

// Benchmark fixture
class CapabilitySignalerBenchmark : public benchmark::Fixture {
protected:
    void SetUp(const benchmark::State& state) override {
        signaler_ = createInMemoryCapabilitySignaler(); // Use factory

        // Generate test data based on benchmark parameters
        const int numAgents = state.range(0);
        const int capsPerAgent = state.range(1);
        
        CapabilityGenerator generator;
        
        // Register capabilities for each agent
        for (int i = 0; i < numAgents; ++i) {
            std::string agentId = "agent_" + std::to_string(i);
            for (int j = 0; j < capsPerAgent; ++j) {
                signaler_->registerCapability(agentId, generator.generate()); // Use ->
            }
        }

        // Generate some capabilities to search for
        for (int i = 0; i < 5; ++i) {  // Always search for 5 capabilities
            searchCapabilities_.push_back(generator.generate());
        }
    }

    std::unique_ptr<CapabilitySignaler> signaler_; // Use unique_ptr to base interface
    std::vector<Capability> searchCapabilities_;
};

} // namespace

// --- Benchmarks --- 
// Note: Need to call methods on the pointer: signaler_->discoverAgents(...)
// Note: The overload discoverAgents(caps, MatchCriteria, filters) doesn't seem to exist
//       on the base CapabilitySignaler interface. We might need to cast or use a different approach
//       if we need to benchmark that specific overload of the InMemory implementation.
//       For now, benchmarking the base interface methods.

// Benchmark discovery (exact match)
BENCHMARK_DEFINE_F(CapabilitySignalerBenchmark, DiscoverAgentsExact)(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(
            signaler_->discoverAgents(searchCapabilities_) // Use ->
        );
    }
}
BENCHMARK_REGISTER_F(CapabilitySignalerBenchmark, DiscoverAgentsExact)
    ->Args({10, 5})    
    ->Args({100, 10})  
    ->Args({1000, 20}) 
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(100);

// Benchmark discovery (partial match)
BENCHMARK_DEFINE_F(CapabilitySignalerBenchmark, DiscoverAgentsPartial)(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(
            signaler_->discoverAgents(searchCapabilities_, true) // Use overload with bool
        );
    }
}
BENCHMARK_REGISTER_F(CapabilitySignalerBenchmark, DiscoverAgentsPartial)
    ->Args({10, 5})
    ->Args({100, 10})
    ->Args({1000, 20})
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(100);

// --- Removed benchmarks for non-interface methods --- 
// BENCHMARK_DEFINE_F(CapabilitySignalerBenchmark, DiscoverAgentsPartialMatch)... 
// BENCHMARK_DEFINE_F(CapabilitySignalerBenchmark, DiscoverAgentsVersionCompatible)... 

BENCHMARK_MAIN(); 