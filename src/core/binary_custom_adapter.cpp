#include "xenocomm/core/binary_custom_adapter.h"
#include <cstring>
#include <algorithm>
#include <numeric>

namespace xenocomm {
namespace core {

BinaryCustomAdapter::BinaryCustomAdapter() = default;

std::vector<uint8_t> BinaryCustomAdapter::encode(
    const void* data,
    size_t size,
    DataFormat format) {
    // Validate input parameters
    validateInput(data, size);
    if (format != DataFormat::BINARY_CUSTOM) {
        throw TranscodingError("Invalid format for BinaryCustomAdapter::encode");
    }

    // Calculate checksum
    uint32_t checksum = calculateChecksum(data, size);

    // Create header
    SchemaHeader header{
        MAGIC_NUMBER,
        SCHEMA_VERSION,
        static_cast<uint32_t>(size),
        checksum
    };

    // Create output buffer with space for header and data
    std::vector<uint8_t> encoded;
    encoded.reserve(sizeof(SchemaHeader) + size);

    // Write header
    const uint8_t* header_bytes = reinterpret_cast<const uint8_t*>(&header);
    encoded.insert(encoded.end(), header_bytes, header_bytes + sizeof(SchemaHeader));

    // Write data
    const uint8_t* data_bytes = static_cast<const uint8_t*>(data);
    encoded.insert(encoded.end(), data_bytes, data_bytes + size);

    return encoded;
}

std::vector<uint8_t> BinaryCustomAdapter::decode(
    const std::vector<uint8_t>& encoded_data,
    DataFormat source_format) {
    if (source_format != DataFormat::BINARY_CUSTOM) {
        throw TranscodingError("Invalid format for BinaryCustomAdapter::decode");
    }

    if (encoded_data.size() < sizeof(SchemaHeader)) {
        throw TranscodingError("Encoded data too small to contain header");
    }

    // Extract header
    const SchemaHeader* header = reinterpret_cast<const SchemaHeader*>(encoded_data.data());
    
    // Validate header
    validateHeader(*header, encoded_data.size() - sizeof(SchemaHeader));

    // Extract data
    const uint8_t* data_start = encoded_data.data() + sizeof(SchemaHeader);
    std::vector<uint8_t> decoded(data_start, data_start + header->data_size);

    // Verify checksum
    uint32_t computed_checksum = calculateChecksum(decoded.data(), decoded.size());
    if (computed_checksum != header->checksum) {
        throw TranscodingError("Checksum verification failed");
    }

    return decoded;
}

bool BinaryCustomAdapter::isValidFormat(
    const void* data,
    size_t size,
    DataFormat format) const {
    if (format != DataFormat::BINARY_CUSTOM || !data || size < sizeof(SchemaHeader)) {
        return false;
    }

    try {
        const SchemaHeader* header = static_cast<const SchemaHeader*>(data);
        validateHeader(*header, size - sizeof(SchemaHeader));
        return true;
    } catch (const TranscodingError&) {
        return false;
    }
}

TranscodingMetadata BinaryCustomAdapter::getMetadata(
    const std::vector<uint8_t>& encoded_data) const {
    if (encoded_data.size() < sizeof(SchemaHeader)) {
        throw TranscodingError("Encoded data too small to contain header");
    }

    const SchemaHeader* header = reinterpret_cast<const SchemaHeader*>(encoded_data.data());
    if (header->magic != MAGIC_NUMBER) {
        throw TranscodingError("Invalid magic number in header");
    }

    TranscodingMetadata metadata;
    metadata.format = DataFormat::BINARY_CUSTOM;
    metadata.version = header->version;
    metadata.element_count = header->data_size;
    metadata.element_size = 1;  // Raw bytes

    return metadata;
}

uint32_t BinaryCustomAdapter::calculateChecksum(const void* data, size_t size) const {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    return std::accumulate(bytes, bytes + size, static_cast<uint32_t>(0),
        [](uint32_t sum, uint8_t byte) {
            return sum + byte;
        });
}

void BinaryCustomAdapter::validateHeader(
    const SchemaHeader& header,
    size_t data_size) const {
    if (header.magic != MAGIC_NUMBER) {
        throw TranscodingError("Invalid magic number in header");
    }

    if (header.version != SCHEMA_VERSION) {
        throw TranscodingError("Unsupported schema version");
    }

    if (header.data_size != data_size) {
        throw TranscodingError("Data size mismatch in header");
    }
}

} // namespace core
} // namespace xenocomm 