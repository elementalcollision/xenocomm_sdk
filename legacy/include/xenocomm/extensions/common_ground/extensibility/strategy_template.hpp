/**
 * @file strategy_template.hpp
 * @brief Provides the StrategyTemplate base class for templated alignment strategies.
 *
 * This file is part of the extensibility subsystem of the CommonGroundFramework.
 * It enables users to create new alignment strategies by inheriting from a base
 * template class, parameterized by a configuration type. Derived strategies can be
 * registered with the framework's StrategyRegistry and composed with other strategies.
 *
 * @see StrategyRegistry
 * @see StrategyBuilder
 * @see StrategyComposer
 */
#pragma once

#include "interfaces.hpp"

namespace xenocomm {
namespace common_ground {

/**
 * @class StrategyTemplate
 * @brief Base template for common alignment strategy patterns.
 * @tparam ConfigType The configuration type for the strategy.
 *
 * The StrategyTemplate provides a base for implementing new alignment strategies
 * that require configuration. Derived classes must implement executeTemplate().
 *
 * @see StrategyRegistry
 * @see StrategyBuilder
 * @see StrategyComposer
 */
template<typename ConfigType>
class StrategyTemplate : public BaseAlignmentStrategy {
public:
    /**
     * @brief Construct with configuration.
     * @param config Initial configuration for the strategy.
     */
    explicit StrategyTemplate(const ConfigType& config);

    /**
     * @brief Configure the strategy.
     * @param config New configuration to apply.
     */
    void configure(const ConfigType& config);
    /**
     * @brief Get the current configuration.
     * @return Reference to the current configuration object.
     */
    const ConfigType& getConfig() const;

protected:
    /**
     * @brief Validate the configuration (override in derived classes).
     * @param config Configuration to validate.
     */
    virtual void validateConfig(const ConfigType& config) const;
    /**
     * @brief Execute the strategy template (must be implemented by derived classes).
     * @param context Alignment context for verification.
     * @param config Configuration to use for execution.
     * @return AlignmentResult of the verification.
     */
    virtual AlignmentResult executeTemplate(
        const AlignmentContext& context,
        const ConfigType& config) = 0;

    /**
     * @brief Utility: get a config value by key.
     * @tparam T Type of the value to retrieve.
     * @param key Name of the configuration field.
     * @return Value of the configuration field.
     */
    template<typename T>
    T getConfigValue(const std::string& key) const;
    /**
     * @brief Utility: set a config value by key.
     * @param key Name of the configuration field.
     * @param value Value to set.
     */
    void setConfigValue(const std::string& key, const std::any& value);

private:
    ConfigType config_;
    AlignmentResult doVerification(const AlignmentContext& context) override;
};

} // namespace common_ground
} // namespace xenocomm
