/**
 * @file strategy_builder.hpp
 * @brief Provides the StrategyBuilder class for fluent creation of custom alignment strategies.
 *
 * This file is part of the extensibility subsystem of the CommonGroundFramework.
 * It enables users to define, configure, and register custom alignment strategies
 * using a fluent builder API. Strategies built with this class can be registered
 * with the framework's StrategyRegistry and composed with other strategies.
 *
 * @see StrategyRegistry
 * @see StrategyComposer
 * @see PluginLoader
 */
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include "interfaces.hpp"

namespace xenocomm {
namespace common_ground {

/**
 * @class StrategyBuilder
 * @brief Fluent API for building custom alignment strategies.
 *
 * The StrategyBuilder allows users to configure all aspects of a strategy,
 * including its ID, name, description, verification logic, applicability check,
 * parameters, and hooks. Use buildAndRegister() to register the built strategy
 * with the framework's StrategyRegistry.
 *
 * @see StrategyRegistry
 * @see StrategyComposer
 */
class StrategyBuilder {
public:
    /**
     * @brief Construct a new StrategyBuilder instance.
     */
    StrategyBuilder();

    /**
     * @brief Set the strategy ID.
     * @param id Unique identifier for the strategy.
     * @return Reference to this builder for chaining.
     */
    StrategyBuilder& withId(std::string id);
    /**
     * @brief Set the strategy name.
     * @param name Human-readable name for the strategy.
     * @return Reference to this builder for chaining.
     */
    StrategyBuilder& withName(std::string name);
    /**
     * @brief Set the strategy description.
     * @param description Description of the strategy's purpose.
     * @return Reference to this builder for chaining.
     */
    StrategyBuilder& withDescription(std::string description);

    /**
     * @brief Set the verification logic for the strategy.
     * @param logic Function to perform alignment verification.
     * @return Reference to this builder for chaining.
     */
    StrategyBuilder& withVerificationLogic(std::function<AlignmentResult(const AlignmentContext&)> logic);
    /**
     * @brief Set the applicability check for the strategy.
     * @param check Function to determine if the strategy is applicable.
     * @return Reference to this builder for chaining.
     */
    StrategyBuilder& withApplicabilityCheck(std::function<bool(const AlignmentContext&)> check);

    /**
     * @brief Add a parameter with a default value.
     * @tparam T Parameter type.
     * @param name Parameter name.
     * @param defaultValue Default value for the parameter.
     * @return Reference to this builder for chaining.
     */
    template<typename T>
    StrategyBuilder& withParameter(const std::string& name, T defaultValue);
    /**
     * @brief Add a required parameter.
     * @param name Parameter name.
     * @return Reference to this builder for chaining.
     */
    StrategyBuilder& withRequiredParameter(const std::string& name);

    /**
     * @brief Add a pre-verification hook.
     * @param hook Function to call before verification.
     * @return Reference to this builder for chaining.
     */
    StrategyBuilder& withPreVerificationHook(std::function<void(const AlignmentContext&)> hook);
    /**
     * @brief Add a post-verification hook.
     * @param hook Function to call after verification.
     * @return Reference to this builder for chaining.
     */
    StrategyBuilder& withPostVerificationHook(std::function<void(const AlignmentResult&)> hook);

    /**
     * @brief Build the strategy as an IAlignmentStrategy instance.
     * @return Shared pointer to the constructed strategy.
     */
    std::shared_ptr<IAlignmentStrategy> build();
    /**
     * @brief Build and register the strategy with the framework's registry.
     *
     * This method creates the strategy and registers it with the StrategyRegistry.
     * (Implementation should call registry->registerStrategy(...)).
     * @return Shared pointer to the constructed and registered strategy.
     * @see StrategyRegistry
     */
    std::shared_ptr<IAlignmentStrategy> buildAndRegister();

private:
    // Internal configuration structures (to be defined)
    // StrategyConfig config_;
    // std::vector<ParameterConfig> parameters_;
    // std::vector<HookConfig> hooks_;
};

} // namespace common_ground
} // namespace xenocomm
