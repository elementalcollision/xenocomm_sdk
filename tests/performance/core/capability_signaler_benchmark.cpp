#include "xenocomm/core/capability_signaler.h"
#include "xenocomm/core/version.h"
#include <benchmark/benchmark.h>
#include <random>
#include <string>
#include <vector>

using namespace xenocomm::core;

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
        // Generate test data based on benchmark parameters
        const int numAgents = state.range(0);
        const int capsPerAgent = state.range(1);
        
        CapabilityGenerator generator;
        
        // Register capabilities for each agent
        for (int i = 0; i < numAgents; ++i) {
            std::string agentId = "agent_" + std::to_string(i);
            for (int j = 0; j < capsPerAgent; ++j) {
                signaler_.registerCapability(agentId, generator.generate());
            }
        }

        // Generate some capabilities to search for
        for (int i = 0; i < 5; ++i) {  // Always search for 5 capabilities
            searchCapabilities_.push_back(generator.generate());
        }
    }

    InMemoryCapabilitySignaler signaler_;
    std::vector<Capability> searchCapabilities_;
};

} // namespace

// Benchmark discovery with varying numbers of agents and capabilities per agent
BENCHMARK_DEFINE_F(CapabilitySignalerBenchmark, DiscoverAgents)(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(
            signaler_.discoverAgents(searchCapabilities_)
        );
    }
}

// Test with different combinations of agents and capabilities
BENCHMARK_REGISTER_F(CapabilitySignalerBenchmark, DiscoverAgents)
    ->Args({10, 5})     // 10 agents, 5 capabilities each
    ->Args({100, 10})   // 100 agents, 10 capabilities each
    ->Args({1000, 20})  // 1000 agents, 20 capabilities each
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(100);

// Benchmark partial matching
BENCHMARK_DEFINE_F(CapabilitySignalerBenchmark, DiscoverAgentsPartialMatch)(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(
            signaler_.discoverAgents(searchCapabilities_, MatchCriteria::NAME_ONLY)
        );
    }
}

BENCHMARK_REGISTER_F(CapabilitySignalerBenchmark, DiscoverAgentsPartialMatch)
    ->Args({10, 5})
    ->Args({100, 10})
    ->Args({1000, 20})
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(100);

// Benchmark version compatibility matching
BENCHMARK_DEFINE_F(CapabilitySignalerBenchmark, DiscoverAgentsVersionCompatible)(benchmark::State& state) {
    std::vector<VersionFilter> filters(searchCapabilities_.size());
    for (auto& filter : filters) {
        filter.checkMinVersion = true;
        filter.minVersion = Version(1, 0, 0);
    }

    for (auto _ : state) {
        benchmark::DoNotOptimize(
            signaler_.discoverAgents(searchCapabilities_, MatchCriteria::VERSION_COMPATIBLE, filters)
        );
    }
}

BENCHMARK_REGISTER_F(CapabilitySignalerBenchmark, DiscoverAgentsVersionCompatible)
    ->Args({10, 5})
    ->Args({100, 10})
    ->Args({1000, 20})
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(100);

BENCHMARK_MAIN(); 