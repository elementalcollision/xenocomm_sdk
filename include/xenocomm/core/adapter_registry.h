#pragma once

#include "xenocomm/core/data_transcoder.h"
#include <memory>
#include <unordered_map>
#include <mutex>
#include <string>
#include <functional>

namespace xenocomm {
namespace core {

/**
 * @brief Factory type for creating data transcoder instances
 */
using TranscoderFactory = std::function<std::unique_ptr<DataTranscoder>()>;

/**
 * @brief Registry for managing and creating data format adapters
 * 
 * This class provides a centralized registry for format adapters with:
 * - Dynamic registration of built-in and third-party adapters
 * - Factory-based instantiation
 * - Thread-safe singleton access
 * - Caching of frequently used adapters
 */
class AdapterRegistry {
public:
    /**
     * @brief Get the singleton instance of the registry
     * 
     * @return AdapterRegistry& Singleton instance
     */
    static AdapterRegistry& getInstance();

    /**
     * @brief Register a new adapter factory for a format
     * 
     * @param format Format the adapter handles
     * @param factory Factory function to create adapter instances
     * @param description Optional description of the adapter
     * @throws std::runtime_error if format already registered
     */
    void registerAdapter(
        DataFormat format,
        TranscoderFactory factory,
        const std::string& description = "");

    /**
     * @brief Create or retrieve a cached instance of an adapter
     * 
     * @param format Format to get adapter for
     * @return std::shared_ptr<DataTranscoder> Adapter instance
     * @throws std::runtime_error if format not registered
     */
    std::shared_ptr<DataTranscoder> getAdapter(DataFormat format);

    /**
     * @brief Check if an adapter is registered for a format
     * 
     * @param format Format to check
     * @return true if adapter is registered
     * @return false if no adapter registered
     */
    bool hasAdapter(DataFormat format) const;

    /**
     * @brief Get description of registered adapter
     * 
     * @param format Format to get description for
     * @return std::string Adapter description
     * @throws std::runtime_error if format not registered
     */
    std::string getAdapterDescription(DataFormat format) const;

    /**
     * @brief Clear the adapter cache
     * 
     * Forces new instances to be created on next getAdapter() call
     */
    void clearCache();

    // Delete copy/move operations to ensure singleton
    AdapterRegistry(const AdapterRegistry&) = delete;
    AdapterRegistry& operator=(const AdapterRegistry&) = delete;
    AdapterRegistry(AdapterRegistry&&) = delete;
    AdapterRegistry& operator=(AdapterRegistry&&) = delete;

private:
    AdapterRegistry() = default;  // Private constructor for singleton

    struct AdapterInfo {
        TranscoderFactory factory;
        std::string description;
    };

    // Thread synchronization
    mutable std::mutex mutex_;

    // Registry storage
    std::unordered_map<DataFormat, AdapterInfo> registry_;
    
    // Instance cache
    std::unordered_map<DataFormat, std::weak_ptr<DataTranscoder>> cache_;

    /**
     * @brief Clean expired entries from cache
     */
    void cleanCache();
};

} // namespace core
} // namespace xenocomm 