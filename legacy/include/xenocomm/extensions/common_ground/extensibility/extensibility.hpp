/**
 * @file extensibility.hpp
 * @defgroup Extensibility Extensibility Subsystem
 * @brief Module-level documentation for the CommonGroundFramework extensibility subsystem.
 *
 * The extensibility subsystem enables users to create, compose, load, and test custom
 * alignment strategies for the CommonGroundFramework. It provides a set of tools and
 * patterns for building flexible, pluggable, and testable strategies that can be
 * registered with the framework and used in alignment flows.
 *
 * ## Main Components
 * - StrategyBuilder: Fluent API for building custom strategies.
 * - StrategyComposer: Static methods for composing strategies (sequence, parallel, etc.).
 * - PluginLoader: Dynamic loading and management of external strategy plugins.
 * - StrategyTemplate: Base template for implementing configurable strategies.
 * - StrategyTestHarness: Tools for testing and validating strategies.
 *
 * ## Example Usage
 * @code{.cpp}
 * using namespace xenocomm::common_ground;
 *
 * // Build a custom strategy
 * auto myStrategy = StrategyBuilder()
 *     .withId("custom1")
 *     .withName("Custom Strategy")
 *     .withDescription("Checks custom alignment condition.")
 *     .withVerificationLogic([](const AlignmentContext& ctx) {
 *         // Custom logic here
 *         return AlignmentResult(true, {}, 1.0);
 *     })
 *     .build();
 *
 * // Register with the framework
 * framework.registerStrategy(myStrategy);
 *
 * // Compose strategies
 * auto composed = StrategyComposer::sequence({myStrategy, otherStrategy});
 *
 * // Load plugins
 * PluginLoader loader("./plugins");
 * loader.loadPlugin("my_plugin.so");
 *
 * // Test a strategy
 * StrategyTestHarness harness(myStrategy);
 * harness.withContext(ctx);
 * auto result = harness.runTest();
 * @endcode
 *
 * @see StrategyBuilder
 * @see StrategyComposer
 * @see PluginLoader
 * @see StrategyTemplate
 * @see StrategyTestHarness
 * @{
 */
// This file is for documentation only. No code is required here.
/** @} */ 