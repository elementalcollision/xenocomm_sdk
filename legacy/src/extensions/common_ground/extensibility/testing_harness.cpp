#include "../../../../include/xenocomm/extensions/common_ground/extensibility/testing_harness.hpp"
#include <iostream>

namespace xenocomm {
namespace common_ground {

StrategyTestHarness::StrategyTestHarness(std::shared_ptr<IAlignmentStrategy> strategy)
    : strategy_(std::move(strategy)) {}

void StrategyTestHarness::withContext(const AlignmentContext&) {
    std::cout << "Configuring test context (stub)" << std::endl;
}
void StrategyTestHarness::withMockData(const std::string&, const std::any&) {
    std::cout << "Configuring mock data (stub)" << std::endl;
}
void StrategyTestHarness::withExpectedResult(const AlignmentResult&) {
    std::cout << "Setting expected result (stub)" << std::endl;
}

TestResult StrategyTestHarness::runTest() {
    std::cout << "Running test (stub)" << std::endl;
    return TestResult{};
}
std::vector<TestResult> StrategyTestHarness::runTestSuite(const TestSuite&) {
    std::cout << "Running test suite (stub)" << std::endl;
    return {};
}

void StrategyTestHarness::validateStrategy() {
    std::cout << "Validating strategy (stub)" << std::endl;
}
void StrategyTestHarness::checkPerformance(const PerformanceCriteria&) {
    std::cout << "Checking performance (stub)" << std::endl;
}

TestReport StrategyTestHarness::generateReport() const {
    std::cout << "Generating test report (stub)" << std::endl;
    return TestReport{};
}
void StrategyTestHarness::exportReport(const std::string&) const {
    std::cout << "Exporting test report (stub)" << std::endl;
}

void StrategyTestHarness::validateTestConfig() const {
    std::cout << "Validating test config (stub)" << std::endl;
}
void StrategyTestHarness::recordTestResult(const TestResult&) {
    std::cout << "Recording test result (stub)" << std::endl;
}

} // namespace common_ground
} // namespace xenocomm
