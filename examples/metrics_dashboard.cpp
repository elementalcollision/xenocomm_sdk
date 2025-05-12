#include "xenocomm/extensions/common_ground/metrics/alignment_metrics.hpp"
#include "xenocomm/extensions/common_ground/metrics/visualization.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <ctime>
#include <cstdlib>

using namespace xenocomm::extensions::common_ground::metrics;

// Helper to generate sample data for demonstration
void generateSampleData(AlignmentMetrics& metrics, int numSamples) {
    std::cout << "Generating " << numSamples << " sample alignment attempts..." << std::endl;
    std::vector<std::string> strategies = {"clarification", "negotiation", "explanation", "reformulation"};
    for (int i = 0; i < numSamples; ++i) {
        AlignmentContext context;
        context.agentId = "agent-" + std::to_string(rand() % 5);
        context.targetId = "target-" + std::to_string(rand() % 3);
        context.domainContext = "domain-" + std::to_string(rand() % 2);
        context.initialAlignmentScores = {{"overall", 0.3 + (rand() % 40) / 100.0}};
        AlignmentResult result;
        bool success = (rand() % 100) < 70; // 70% success rate
        result.outcome = success ? AlignmentOutcome::Success : AlignmentOutcome::Failure;
        result.alignmentScore = success ? 0.7 + (rand() % 30) / 100.0 : 0.2 + (rand() % 40) / 100.0;
        result.convergenceTime = std::chrono::milliseconds(50 + rand() % 450);
        result.dimensionalScores = {
            {"understanding", 0.5 + (rand() % 50) / 100.0},
            {"agreement", 0.4 + (rand() % 60) / 100.0}
        };
        if (!success) {
            std::vector<std::string> reasons = {"Conflicting goals", "Misunderstanding", "Timeout", "Resource constraints"};
            result.failureReason = reasons[rand() % reasons.size()];
        }
        AlignmentMetadata metadata;
        metadata.sessionId = "session-" + std::to_string(i / 10);
        auto now = std::chrono::system_clock::now();
        auto variance = std::chrono::seconds(rand() % 3600);
        metadata.timestamp = now - variance;
        int numStrategies = 1 + rand() % 3;
        metadata.appliedStrategies.clear();
        for (int j = 0; j < numStrategies; ++j) {
            metadata.appliedStrategies.push_back(strategies[rand() % strategies.size()]);
        }
        metrics.recordAlignmentAttempt(context, result, metadata);
        if (i % 10 == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    std::cout << "Sample data generation complete." << std::endl;
}

int main(int argc, char** argv) {
    srand(static_cast<unsigned int>(time(nullptr)));
    MetricsConfig config;
    config.enablePersistence = false; // In-memory for demo
    AlignmentMetrics metrics(config);
    MetricVisualizer visualizer;
    int numSamples = 0;
    if (argc > 1) {
        numSamples = std::atoi(argv[1]);
    }
    if (numSamples > 0) {
        generateSampleData(metrics, numSamples);
    }
    // Create time range for last 24 hours
    TimeRange range;
    range.start = std::chrono::system_clock::now() - std::chrono::hours(24);
    range.end = std::chrono::system_clock::now();
    // Render and display dashboard
    AlignmentTrends trends = metrics.analyzeTrends(range);
    std::string trendsReport = visualizer.renderTrends(trends);
    std::vector<std::string> strategies;
    for (const auto& [strategy, _] : trends.strategyPerformance) {
        strategies.push_back(strategy);
    }
    std::string strategyReport;
    if (!strategies.empty()) {
        StrategyComparison comparison = metrics.compareStrategies(strategies);
        strategyReport = visualizer.renderStrategyComparison(comparison);
    }
    std::cout << "\n==================== ALIGNMENT METRICS DASHBOARD ====================\n";
    std::cout << trendsReport;
    if (!strategyReport.empty()) {
        std::cout << strategyReport;
    }
    std::cout << "====================================================================\n";
    std::cout << "Usage: " << argv[0] << " [num_samples]\n";
    std::cout << "  num_samples: Number of random alignment attempts to generate\n";
    return 0;
} 