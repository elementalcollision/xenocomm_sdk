#ifndef XENOCOMM_UTILS_SERIALIZATION_H
#define XENOCOMM_UTILS_SERIALIZATION_H

#include <vector>
#include <cstdint>
#include <xenocomm/core/capability_signaler.h> // For Capability struct

namespace xenocomm {
namespace utils {

/**
 * @brief Serializes a Capability object into a binary format.
 *
 * The format is:
 * - name: Length (uint32_t) followed by the string bytes.
 * - version: major (uint16_t), minor (uint16_t), patch (uint16_t).
 * - parameters: Count (uint32_t), followed by Key Length (uint32_t), Key Bytes,
 *   Value Length (uint32_t), Value Bytes for each parameter.
 * All multi-byte integers are stored in network byte order (big-endian).
 *
 * @param cap The Capability object to serialize.
 * @param outBuffer The vector to append the serialized bytes to. Existing contents are preserved.
 */
void serializeCapability(const core::Capability& cap, std::vector<uint8_t>& outBuffer);

/**
 * @brief Deserializes a Capability object from a binary buffer.
 *
 * Assumes the buffer contains data in the format specified by serializeCapability.
 * Performs bounds checking.
 *
 * @param data Pointer to the start of the binary data.
 * @param size The total size of the binary data available.
 * @param outCap The Capability object to populate with deserialized data.
 * @param[out] bytesRead Optional pointer to store the number of bytes consumed during deserialization.
 * @return True if deserialization was successful, false otherwise (e.g., invalid format, insufficient data).
 */
bool deserializeCapability(const uint8_t* data, size_t size, core::Capability& outCap, size_t* bytesRead = nullptr);

// TODO: Consider adding functions for serializing/deserializing multiple capabilities into a single blob.
// e.g., serializeCapabilities(const std::vector<core::Capability>&, std::vector<uint8_t>&)
//       deserializeCapabilities(const uint8_t*, size_t, std::vector<core::Capability>&) -> bool

} // namespace utils
} // namespace xenocomm

#endif // XENOCOMM_UTILS_SERIALIZATION_H 