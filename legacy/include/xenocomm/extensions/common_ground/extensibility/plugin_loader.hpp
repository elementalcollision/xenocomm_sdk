/**
 * @file plugin_loader.hpp
 * @brief Provides the PluginLoader class for dynamic loading of strategy plugins.
 *
 * This file is part of the extensibility subsystem of the CommonGroundFramework.
 * It enables users to load, manage, and access external alignment strategy plugins
 * at runtime. Loaded strategies can be registered with the framework's StrategyRegistry
 * and used alongside built-in and custom strategies.
 *
 * @see StrategyRegistry
 * @see StrategyBuilder
 * @see StrategyComposer
 */
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include "interfaces.hpp"

namespace xenocomm {
namespace common_ground {

/**
 * @class PluginLoader
 * @brief Plugin architecture for loading external strategy implementations.
 *
 * The PluginLoader provides methods to load and unload plugins, access strategies
 * provided by plugins, and retrieve plugin metadata. Loaded strategies can be
 * registered with the framework's registry for use in alignment flows.
 *
 * @see StrategyRegistry
 * @see StrategyBuilder
 * @see StrategyComposer
 */
class PluginLoader {
public:
    /**
     * @brief Construct a PluginLoader for a given plugin directory.
     * @param pluginDir Directory to search for plugins.
     */
    PluginLoader(const std::string& pluginDir);

    /**
     * @brief Load a plugin from the specified path.
     * @param pluginPath Path to the plugin file.
     */
    void loadPlugin(const std::string& pluginPath);
    /**
     * @brief Unload a plugin by its ID.
     * @param pluginId Identifier of the plugin to unload.
     */
    void unloadPlugin(const std::string& pluginId);
    /**
     * @brief Check if a plugin is loaded.
     * @param pluginId Identifier of the plugin.
     * @return True if the plugin is loaded, false otherwise.
     */
    bool isPluginLoaded(const std::string& pluginId) const;

    /**
     * @brief Get all strategies provided by loaded plugins.
     * @return List of shared pointers to plugin strategies.
     */
    std::vector<std::shared_ptr<IAlignmentStrategy>> getPluginStrategies() const;
    /**
     * @brief Get a specific strategy from a plugin.
     * @param pluginId Identifier of the plugin.
     * @param strategyId Identifier of the strategy.
     * @return Shared pointer to the requested strategy, or nullptr if not found.
     */
    std::shared_ptr<IAlignmentStrategy> getStrategyFromPlugin(
        const std::string& pluginId,
        const std::string& strategyId) const;

    /**
     * @brief Get metadata for a specific plugin.
     * @param pluginId Identifier of the plugin.
     * @return PluginMetadata object.
     */
    PluginMetadata getPluginMetadata(const std::string& pluginId) const;
    /**
     * @brief Get metadata for all loaded plugins.
     * @return List of PluginMetadata objects.
     */
    std::vector<PluginMetadata> getLoadedPlugins() const;

private:
    std::string pluginDir_;
    std::unordered_map<std::string, PluginHandle> loadedPlugins_;

    /**
     * @brief Validate a plugin handle (internal use).
     * @param handle Plugin handle to validate.
     */
    void validatePlugin(const PluginHandle& handle) const;
    /**
     * @brief Register strategies provided by a plugin (internal use).
     * @param handle Plugin handle.
     */
    void registerPluginStrategies(const PluginHandle& handle);
};

} // namespace common_ground
} // namespace xenocomm
