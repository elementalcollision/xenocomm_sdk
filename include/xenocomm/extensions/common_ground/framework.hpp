#pragma once
#include "interfaces.hpp"
#include "context.hpp"
#include "result.hpp"
#include "types.hpp"
#include <memory>
#include <future>
#include <string>
#include <vector>
#include "strategy_registry.hpp"
#include "strategy_chain.hpp"
#include "strategy_invoker.hpp"
#include "strategy_hooks.hpp"
#include "strategies/knowledge_verification.hpp"
#include "strategies/goal_alignment.hpp"
#include "strategies/terminology_alignment.hpp"
#include "strategies/assumption_verification.hpp"
#include "strategies/context_synchronization.hpp"

/*
 * @file framework.hpp
 * @brief Core entry point for the CommonGroundFramework extension module.
 *
 * This file defines the CommonGroundFramework class, which manages alignment strategies
 * for establishing mutual understanding between agents. It integrates the strategy registry,
 * invocation, chaining, and hook systems, and provides the main API for alignment verification.
 *
 * Related components:
 *  - StrategyRegistry: Thread-safe management of registered alignment strategies.
 *  - StrategyHooks: Pre- and post-invocation hooks for extensibility and monitoring.
 *  - StrategyInvoker: Synchronous and asynchronous invocation of strategies.
 *  - StrategyChain: Chaining and conditional execution of multiple strategies.
 *
 *  The method registerStandardStrategies() is the recommended way to enable all
 *  standard alignment strategies in the framework.
 */

namespace xenocomm {
namespace common_ground {

// Stub Logger class
class Logger {
public:
    void info(const std::string&) {}
    void error(const std::string&) {}
};

// Stub FrameworkConfig struct
struct FrameworkConfig {
    std::string name;
    // Add more configuration fields as needed
};

// Forward declaration for StrategyRegistry
class StrategyRegistry;

/**
 * @class CommonGroundFramework
 * @brief Main class for managing alignment strategies and verification flows.
 *
 * The CommonGroundFramework provides the primary API for registering, managing,
 * and executing alignment strategies. It integrates with the NegotiationProtocol
 * and FeedbackLoop subsystems, and supports extensibility via hooks and chaining.
 */
class CommonGroundFramework {
public:
    /**
     * @brief Construct a new CommonGroundFramework instance.
     * @param config Framework configuration options.
     */
    explicit CommonGroundFramework(const FrameworkConfig& config)
        : config_(config),
          registry_(std::make_unique<StrategyRegistry>()),
          hooks_(std::make_shared<StrategyHooks>()),
          invoker_(hooks_),
          chain_() {}
    ~CommonGroundFramework() = default;

    /**
     * @brief Initialize the framework (stub).
     */
    void initialize() {}
    /**
     * @brief Shutdown the framework (stub).
     */
    void shutdown() {}

    /**
     * @brief Register an alignment strategy with optional priority.
     * @param strategy The strategy to register.
     * @param priority Priority for execution order (lower is higher priority).
     */
    void registerStrategy(std::shared_ptr<IAlignmentStrategy> strategy, int priority = 0) {
        registry_->registerStrategy(strategy, priority);
    }
    /**
     * @brief Unregister an alignment strategy by ID.
     * @param strategyId The ID of the strategy to remove.
     */
    void unregisterStrategy(const std::string& strategyId) {
        registry_->unregisterStrategy(strategyId);
    }

    /**
     * @brief Run all applicable strategies for a given context.
     * @param context The alignment context.
     * @return Results from all applicable strategies.
     */
    std::vector<AlignmentResult> runApplicableStrategies(const AlignmentContext& context) {
        auto strategies = registry_->getApplicableStrategies(context);
        return invoker_.invokeBatch(strategies, context);
    }

    /**
     * @brief Verify alignment by running all applicable strategies and aggregating results.
     * @param context The alignment context.
     * @return Aggregated alignment result (currently returns first result or default).
     */
    AlignmentResult verifyAlignment(const AlignmentContext& context) {
        auto results = runApplicableStrategies(context);
        // TODO: Aggregate results (for now, return first or default)
        if (!results.empty()) return results.front();
        return AlignmentResult(true, {}, 1.0);
    }
    /**
     * @brief Asynchronously verify alignment (stub).
     * @param context The alignment context.
     * @return Future for the aggregated alignment result.
     */
    std::future<AlignmentResult> verifyAlignmentAsync(const AlignmentContext& context) {
        // TODO: Implement async aggregation
        return std::async(std::launch::async, [this, &context]() {
            return this->verifyAlignment(context);
        });
    }

    /**
     * @brief Integrate with the NegotiationProtocol subsystem (stub).
     */
    void integrateWithNegotiationProtocol() {}
    /**
     * @brief Integrate with the FeedbackLoop subsystem (stub).
     */
    void integrateWithFeedbackLoop() {}

    /**
     * @brief Register all standard alignment strategies with default configuration.
     *
     * This method instantiates and registers the following strategies:
     *   - KnowledgeVerificationStrategy: Verifies shared knowledge between agents.
     *   - GoalAlignmentStrategy: Checks goal compatibility using a compatibility matrix.
     *   - TerminologyAlignmentStrategy: Ensures consistent terminology using mappings.
     *   - AssumptionVerificationStrategy: Surfaces and validates critical assumptions.
     *   - ContextSynchronizationStrategy: Aligns contextual parameters between agents.
     *
     * Usage:
     *   Call this method after constructing the framework instance to enable all
     *   standard strategies. You may further configure each strategy before or after
     *   registration as needed.
     */
    void registerStandardStrategies() {
        // Knowledge Verification
        auto knowledgeStrategy = std::make_shared<KnowledgeVerificationStrategy>();
        // Optionally configure knowledgeStrategy here
        registerStrategy(knowledgeStrategy);

        // Goal Alignment
        auto goalStrategy = std::make_shared<GoalAlignmentStrategy>();
        // Optionally configure goalStrategy here
        registerStrategy(goalStrategy);

        // Terminology Alignment
        auto terminologyStrategy = std::make_shared<TerminologyAlignmentStrategy>();
        // Optionally configure terminologyStrategy here
        registerStrategy(terminologyStrategy);

        // Assumption Verification
        auto assumptionStrategy = std::make_shared<AssumptionVerificationStrategy>();
        // Optionally configure assumptionStrategy here
        registerStrategy(assumptionStrategy);

        // Context Synchronization
        auto contextSyncStrategy = std::make_shared<ContextSynchronizationStrategy>();
        // Optionally configure contextSyncStrategy here
        registerStrategy(contextSyncStrategy);
    }

private:
    /**
     * @brief Thread-safe registry for alignment strategies.
     */
    std::unique_ptr<StrategyRegistry> registry_;
    /**
     * @brief Shared hooks for pre- and post-invocation extensibility.
     */
    std::shared_ptr<StrategyHooks> hooks_;
    /**
     * @brief Invoker for running strategies (sync/async).
     */
    StrategyInvoker invoker_;
    /**
     * @brief Chain for composing and conditionally executing strategies.
     */
    StrategyChain chain_;
    /**
     * @brief Framework configuration.
     */
    FrameworkConfig config_;
    /**
     * @brief Logger for framework events (stub).
     */
    Logger logger_;
};

} // namespace common_ground
} // namespace xenocomm
