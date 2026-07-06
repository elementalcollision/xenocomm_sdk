#include "../../../../include/xenocomm/extensions/common_ground/extensibility/plugin_loader.hpp"
#include <iostream>

namespace xenocomm {
namespace common_ground {

PluginLoader::PluginLoader(const std::string& pluginDir) : pluginDir_(pluginDir) {
    std::cout << "PluginLoader created for dir: " << pluginDir << std::endl;
}

void PluginLoader::loadPlugin(const std::string& pluginPath) {
    std::cout << "Simulating loading plugin: " << pluginPath << std::endl;
    // Simulate plugin handle
    loadedPlugins_[pluginPath] = PluginHandle{};
}

void PluginLoader::unloadPlugin(const std::string& pluginId) {
    std::cout << "Simulating unloading plugin: " << pluginId << std::endl;
    loadedPlugins_.erase(pluginId);
}

bool PluginLoader::isPluginLoaded(const std::string& pluginId) const {
    return loadedPlugins_.count(pluginId) > 0;
}

std::vector<std::shared_ptr<IAlignmentStrategy>> PluginLoader::getPluginStrategies() const {
    std::cout << "Returning dummy plugin strategies (empty)" << std::endl;
    return {};
}

std::shared_ptr<IAlignmentStrategy> PluginLoader::getStrategyFromPlugin(const std::string& pluginId, const std::string& strategyId) const {
    std::cout << "Returning dummy strategy for plugin: " << pluginId << ", strategy: " << strategyId << std::endl;
    return nullptr;
}

PluginMetadata PluginLoader::getPluginMetadata(const std::string& pluginId) const {
    std::cout << "Returning dummy metadata for plugin: " << pluginId << std::endl;
    return PluginMetadata{};
}

std::vector<PluginMetadata> PluginLoader::getLoadedPlugins() const {
    std::cout << "Returning dummy loaded plugin metadata (empty)" << std::endl;
    return {};
}

void PluginLoader::validatePlugin(const PluginHandle&) const {
    std::cout << "Validating plugin (stub)" << std::endl;
}
void PluginLoader::registerPluginStrategies(const PluginHandle&) {
    std::cout << "Registering plugin strategies (stub)" << std::endl;
}

} // namespace common_ground
} // namespace xenocomm
