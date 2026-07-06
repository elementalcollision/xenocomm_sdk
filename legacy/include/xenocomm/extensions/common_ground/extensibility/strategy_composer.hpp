/**
 * @file strategy_composer.hpp
 * @brief Provides the StrategyComposer class for composing alignment strategies.
 *
 * This file is part of the extensibility subsystem of the CommonGroundFramework.
 * It enables users to combine multiple alignment strategies using various composition
 * patterns such as sequence, parallel, conditional, fallback, and retry. Composed
 * strategies can be registered with the framework's StrategyRegistry and used as
 * building blocks for more complex alignment logic.
 *
 * @see StrategyRegistry
 * @see StrategyBuilder
 * @see PluginLoader
 */
#pragma once

#include <memory>
#include <vector>
#include <functional>
#include "interfaces.hpp"

namespace xenocomm {
namespace common_ground {

/**
 * @class StrategyComposer
 * @brief Provides composition patterns for combining alignment strategies.
 *
 * The StrategyComposer offers static methods to create new strategies by composing
 * existing ones. Supported patterns include sequential, parallel, conditional,
 * fallback, and retry. Composed strategies implement IAlignmentStrategy and can be
 * registered with the framework's registry.
 *
 * @see StrategyRegistry
 * @see StrategyBuilder
 */
class StrategyComposer {
public:
    /**
     * @brief Construct a new StrategyComposer instance.
     */
    StrategyComposer();

    /**
     * @brief Create a sequential composition of strategies.
     * @param strategies List of strategies to execute in order.
     * @return Composed strategy that runs each in sequence.
     */
    static std::shared_ptr<IAlignmentStrategy> sequence(
        std::vector<std::shared_ptr<IAlignmentStrategy>> strategies);

    /**
     * @brief Create a parallel composition of strategies.
     * @param strategies List of strategies to execute in parallel.
     * @param config Parallel execution configuration.
     * @return Composed strategy that runs all in parallel.
     */
    static std::shared_ptr<IAlignmentStrategy> parallel(
        std::vector<std::shared_ptr<IAlignmentStrategy>> strategies,
        ParallelExecutionConfig config = {});

    /**
     * @brief Create a conditional strategy composition.
     * @param strategy Strategy to execute if condition is true.
     * @param condition Function to determine if the strategy should run.
     * @return Composed strategy that runs conditionally.
     */
    static std::shared_ptr<IAlignmentStrategy> conditional(
        std::shared_ptr<IAlignmentStrategy> strategy,
        std::function<bool(const AlignmentContext&)> condition);

    /**
     * @brief Create a fallback composition of strategies.
     * @param strategies List of strategies to try in order until one succeeds.
     * @return Composed strategy that tries each in turn.
     */
    static std::shared_ptr<IAlignmentStrategy> fallback(
        std::vector<std::shared_ptr<IAlignmentStrategy>> strategies);

    /**
     * @brief Create a retry composition for a strategy.
     * @param strategy Strategy to retry.
     * @param config Retry configuration.
     * @return Composed strategy that retries on failure.
     */
    static std::shared_ptr<IAlignmentStrategy> withRetry(
        std::shared_ptr<IAlignmentStrategy> strategy,
        RetryConfig config);

private:
    /**
     * @brief Combine multiple AlignmentResult objects using a specified strategy.
     * @param results List of results to combine.
     * @param strategy Combination strategy.
     * @return Combined AlignmentResult.
     */
    static AlignmentResult combineResults(
        const std::vector<AlignmentResult>& results,
        ResultCombinationStrategy strategy);
};

} // namespace common_ground
} // namespace xenocomm
