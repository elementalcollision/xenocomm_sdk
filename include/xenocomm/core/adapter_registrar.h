#pragma once

#include "xenocomm/core/adapter_registry.h"
#include <type_traits>

namespace xenocomm {
namespace core {

/**
 * @brief Helper class for automatic adapter registration
 * 
 * This template class helps register adapters automatically when included.
 * Usage:
 *   static AdapterRegistrar<MyAdapter> registrar(DataFormat::MY_FORMAT, "My adapter description");
 * 
 * @tparam T Adapter class type (must inherit from DataTranscoder)
 */
template<typename T>
class AdapterRegistrar {
    static_assert(std::is_base_of<DataTranscoder, T>::value,
        "Adapter must inherit from DataTranscoder");

public:
    /**
     * @brief Construct and register an adapter
     * 
     * @param format Format the adapter handles
     * @param description Optional description of the adapter
     */
    AdapterRegistrar(DataFormat format, const std::string& description = "") {
        AdapterRegistry::getInstance().registerAdapter(
            format,
            []() { return std::make_unique<T>(); },
            description
        );
    }
};

} // namespace core
} // namespace xenocomm 