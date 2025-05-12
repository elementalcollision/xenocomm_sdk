/**
 * @file testing_harness.hpp
 * @brief Provides the StrategyTestHarness class for testing alignment strategies.
 *
 * This file is part of the extensibility subsystem of the CommonGroundFramework.
 * It enables users to configure, execute, and validate tests for custom or plugin
 * alignment strategies. The test harness supports context setup, mock data, expected
 * results, and reporting. It can be used to test any strategy registered with the
 * framework's StrategyRegistry.
 *
 * @see StrategyRegistry
 * @see StrategyBuilder
 * @see StrategyComposer
 * @see PluginLoader
 */
#pragma once

#include <memory>
#include <string>
#include <vector>
#include "interfaces.hpp"

namespace xenocomm {
namespace common_ground {

/**
 * @class StrategyTestHarness
 * @brief Testing harness for validating custom alignment strategies.
 *
 * The StrategyTestHarness provides methods to configure test contexts, inject mock
 * data, set expected results, execute tests, validate strategies, check performance,
 * and generate/export test reports. It is designed to work with any strategy that
 * implements IAlignmentStrategy.
 *
 * @see StrategyRegistry
 * @see StrategyBuilder
 * @see StrategyComposer
 * @see PluginLoader
 */
class StrategyTestHarness {
public:
    /**
     * @brief Construct with a strategy to test.
     * @param strategy Shared pointer to the strategy to be tested.
     */
    StrategyTestHarness(std::shared_ptr<IAlignmentStrategy> strategy);

    /**
     * @brief Configure the test context.
     * @param context Alignment context to use for testing.
     */
    void withContext(const AlignmentContext& context);
    /**
     * @brief Inject mock data for testing.
     * @param key Name of the mock data field.
     * @param value Value to inject.
     */
    void withMockData(const std::string& key, const std::any& value);
    /**
     * @brief Set the expected result for the test.
     * @param result Expected AlignmentResult.
     */
    void withExpectedResult(const AlignmentResult& result);

    /**
     * @brief Run a single test case.
     * @return TestResult object with the outcome.
     */
    TestResult runTest();
    /**
     * @brief Run a suite of test cases.
     * @param suite TestSuite object containing multiple test cases.
     * @return Vector of TestResult objects.
     */
    std::vector<TestResult> runTestSuite(const TestSuite& suite);

    /**
     * @brief Validate the strategy under test.
     */
    void validateStrategy();
    /**
     * @brief Check the performance of the strategy.
     * @param criteria Performance criteria to check against.
     */
    void checkPerformance(const PerformanceCriteria& criteria);

    /**
     * @brief Generate a test report.
     * @return TestReport object summarizing the test results.
     */
    TestReport generateReport() const;
    /**
     * @brief Export the test report to a file.
     * @param path File path to export the report to.
     */
    void exportReport(const std::string& path) const;

private:
    std::shared_ptr<IAlignmentStrategy> strategy_;
    TestConfig config_;
    std::vector<TestResult> results_;

    /**
     * @brief Validate the test configuration (internal use).
     */
    void validateTestConfig() const;
    /**
     * @brief Record a test result (internal use).
     * @param result TestResult to record.
     */
    void recordTestResult(const TestResult& result);
};

} // namespace common_ground
} // namespace xenocomm
