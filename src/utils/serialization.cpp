#include "xenocomm/utils/serialization.h"
#include <xenocomm/core/capability_signaler.h> // Ensure Capability struct is known
#include <stdexcept> // For std::runtime_error (example)
#include <cstring>   // For memcpy
#include <arpa/inet.h> // For htonl, ntohl, htons, ntohs

namespace xenocomm {
namespace utils {

// Placeholder implementation
// TODO: Implement actual serialization functionality

// Placeholder implementation for serializeCapability
void serializeCapability(const core::Capability& cap, std::vector<uint8_t>& outBuffer) {
    // TODO: Implement actual serialization logic as per the header documentation.
    // This is a very basic placeholder that doesn't follow the specified format.

    // Example: Serialize name length and name
    uint32_t nameLen = htonl(static_cast<uint32_t>(cap.name.length()));
    outBuffer.insert(outBuffer.end(), reinterpret_cast<const uint8_t*>(&nameLen), reinterpret_cast<const uint8_t*>(&nameLen) + sizeof(nameLen));
    outBuffer.insert(outBuffer.end(), cap.name.begin(), cap.name.end());

    // Example: Serialize version (highly simplified)
    uint16_t major = htons(cap.version.major);
    uint16_t minor = htons(cap.version.minor);
    uint16_t patch = htons(cap.version.patch);
    outBuffer.insert(outBuffer.end(), reinterpret_cast<const uint8_t*>(&major), reinterpret_cast<const uint8_t*>(&major) + sizeof(major));
    outBuffer.insert(outBuffer.end(), reinterpret_cast<const uint8_t*>(&minor), reinterpret_cast<const uint8_t*>(&minor) + sizeof(minor));
    outBuffer.insert(outBuffer.end(), reinterpret_cast<const uint8_t*>(&patch), reinterpret_cast<const uint8_t*>(&patch) + sizeof(patch));

    // Example: Serialize parameter count and parameters (highly simplified)
    uint32_t paramCount = htonl(static_cast<uint32_t>(cap.parameters.size()));
    outBuffer.insert(outBuffer.end(), reinterpret_cast<const uint8_t*>(&paramCount), reinterpret_cast<const uint8_t*>(&paramCount) + sizeof(paramCount));
    for (const auto& pair : cap.parameters) {
        uint32_t keyLen = htonl(static_cast<uint32_t>(pair.first.length()));
        outBuffer.insert(outBuffer.end(), reinterpret_cast<const uint8_t*>(&keyLen), reinterpret_cast<const uint8_t*>(&keyLen) + sizeof(keyLen));
        outBuffer.insert(outBuffer.end(), pair.first.begin(), pair.first.end());

        uint32_t valLen = htonl(static_cast<uint32_t>(pair.second.length()));
        outBuffer.insert(outBuffer.end(), reinterpret_cast<const uint8_t*>(&valLen), reinterpret_cast<const uint8_t*>(&valLen) + sizeof(valLen));
        outBuffer.insert(outBuffer.end(), pair.second.begin(), pair.second.end());
    }
}

// Placeholder implementation for deserializeCapability
bool deserializeCapability(const uint8_t* data, size_t size, core::Capability& outCap, size_t* bytesRead) {
    // TODO: Implement actual deserialization logic as per the header documentation.
    // This is a very basic placeholder that doesn't follow the specified format
    // and lacks proper error handling and bounds checking.

    size_t currentOffset = 0;

    // Deserialize name
    if (size < currentOffset + sizeof(uint32_t)) return false;
    uint32_t nameLen_n;
    std::memcpy(&nameLen_n, data + currentOffset, sizeof(uint32_t));
    uint32_t nameLen = ntohl(nameLen_n);
    currentOffset += sizeof(uint32_t);
    if (size < currentOffset + nameLen) return false;
    outCap.name.assign(reinterpret_cast<const char*>(data + currentOffset), nameLen);
    currentOffset += nameLen;

    // Deserialize version
    if (size < currentOffset + sizeof(uint16_t) * 3) return false;
    uint16_t major_n, minor_n, patch_n;
    std::memcpy(&major_n, data + currentOffset, sizeof(uint16_t));
    outCap.version.major = ntohs(major_n);
    currentOffset += sizeof(uint16_t);
    std::memcpy(&minor_n, data + currentOffset, sizeof(uint16_t));
    outCap.version.minor = ntohs(minor_n);
    currentOffset += sizeof(uint16_t);
    std::memcpy(&patch_n, data + currentOffset, sizeof(uint16_t));
    outCap.version.patch = ntohs(patch_n);
    currentOffset += sizeof(uint16_t);

    // Deserialize parameters
    if (size < currentOffset + sizeof(uint32_t)) return false;
    uint32_t paramCount_n;
    std::memcpy(&paramCount_n, data + currentOffset, sizeof(uint32_t));
    uint32_t paramCount = ntohl(paramCount_n);
    currentOffset += sizeof(uint32_t);

    outCap.parameters.clear();
    for (uint32_t i = 0; i < paramCount; ++i) {
        if (size < currentOffset + sizeof(uint32_t)) return false;
        uint32_t keyLen_n;
        std::memcpy(&keyLen_n, data + currentOffset, sizeof(uint32_t));
        uint32_t keyLen = ntohl(keyLen_n);
        currentOffset += sizeof(uint32_t);
        if (size < currentOffset + keyLen) return false;
        std::string key(reinterpret_cast<const char*>(data + currentOffset), keyLen);
        currentOffset += keyLen;

        if (size < currentOffset + sizeof(uint32_t)) return false;
        uint32_t valLen_n;
        std::memcpy(&valLen_n, data + currentOffset, sizeof(uint32_t));
        uint32_t valLen = ntohl(valLen_n);
        currentOffset += sizeof(uint32_t);
        if (size < currentOffset + valLen) return false;
        std::string val(reinterpret_cast<const char*>(data + currentOffset), valLen);
        currentOffset += valLen;
        outCap.parameters[key] = val;
    }

    if (bytesRead) {
        *bytesRead = currentOffset;
    }
    return true;
}

} // namespace utils
} // namespace xenocomm 