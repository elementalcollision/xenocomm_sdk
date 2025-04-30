#include "xenocomm/core/adapter_registry.h"
#include <stdexcept>

namespace xenocomm {
namespace core {

AdapterRegistry& AdapterRegistry::getInstance() {
    static AdapterRegistry instance;
    return instance;
}

void AdapterRegistry::registerAdapter(
    DataFormat format,
    TranscoderFactory factory,
    const std::string& description) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (registry_.find(format) != registry_.end()) {
        throw std::runtime_error("Adapter already registered for format");
    }

    registry_[format] = AdapterInfo{
        std::move(factory),
        description
    };
}

std::shared_ptr<DataTranscoder> AdapterRegistry::getAdapter(DataFormat format) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check if format is registered
    auto it = registry_.find(format);
    if (it == registry_.end()) {
        throw std::runtime_error("No adapter registered for format");
    }

    // Clean expired cache entries
    cleanCache();

    // Check cache for existing instance
    auto cache_it = cache_.find(format);
    if (cache_it != cache_.end()) {
        if (auto adapter = cache_it->second.lock()) {
            return adapter;  // Return cached instance
        }
        cache_.erase(cache_it);  // Remove expired entry
    }

    // Create new instance
    auto adapter = std::shared_ptr<DataTranscoder>(
        it->second.factory());
    
    // Cache the instance
    cache_[format] = adapter;
    
    return adapter;
}

bool AdapterRegistry::hasAdapter(DataFormat format) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return registry_.find(format) != registry_.end();
}

std::string AdapterRegistry::getAdapterDescription(DataFormat format) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = registry_.find(format);
    if (it == registry_.end()) {
        throw std::runtime_error("No adapter registered for format");
    }
    
    return it->second.description;
}

void AdapterRegistry::clearCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
}

void AdapterRegistry::cleanCache() {
    // Remove all expired weak pointers
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (it->second.expired()) {
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace core
} // namespace xenocomm 