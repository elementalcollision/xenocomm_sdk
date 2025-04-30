#pragma once

#include "xenocomm/core/data_transcoder.h"
#include <memory>
#include <string>
#include <vector>

namespace xenocomm {
namespace core {

/**
 * @brief Custom binary serialization adapter for complex data structures
 * 
 * This adapter provides schema-based serialization with versioning support
 * and type checking for complex data structures.
 */
class BinaryCustomAdapter : public DataTranscoder {
public:
    BinaryCustomAdapter();
    ~BinaryCustomAdapter() override = default;

    /**
     * @brief Encode data using custom binary serialization
     * 
     * @param data Raw input data as bytes
     * @param size Size of input data in bytes
     * @param format Target format for encoding (must be BINARY_CUSTOM)
     * @return std::vector<uint8_t> Encoded data with schema and version info
     * @throws TranscodingError if encoding fails or format is invalid
     */
    std::vector<uint8_t> encode(
        const void* data,
        size_t size,
        DataFormat format) override;

    /**
     * @brief Decode data from custom binary format
     * 
     * @param encoded_data Encoded input data with schema
     * @param source_format Format of the encoded data (must be BINARY_CUSTOM)
     * @return std::vector<uint8_t> Decoded data
     * @throws TranscodingError if decoding fails or format is invalid
     */
    std::vector<uint8_t> decode(
        const std::vector<uint8_t>& encoded_data,
        DataFormat source_format) override;

    /**
     * @brief Validate if data matches binary custom format requirements
     * 
     * @param data Data to validate
     * @param size Size of data in bytes
     * @param format Format to validate against (must be BINARY_CUSTOM)
     * @return true if data is valid for the format
     * @return false if data is invalid for the format
     */
    bool isValidFormat(
        const void* data,
        size_t size,
        DataFormat format) const override;

    /**
     * @brief Get metadata from encoded binary custom data
     * 
     * @param encoded_data Encoded data to extract metadata from
     * @return TranscodingMetadata Metadata including schema version and type info
     * @throws TranscodingError if metadata extraction fails
     */
    TranscodingMetadata getMetadata(
        const std::vector<uint8_t>& encoded_data) const override;

private:
    static constexpr uint32_t SCHEMA_VERSION = 1;
    static constexpr uint32_t MAGIC_NUMBER = 0xBC5A4D2E;  // "BC" for Binary Custom

    struct SchemaHeader {
        uint32_t magic;
        uint32_t version;
        uint32_t data_size;
        uint32_t checksum;
    };

    /**
     * @brief Calculate checksum for data validation
     * 
     * @param data Data to calculate checksum for
     * @param size Size of data in bytes
     * @return uint32_t Calculated checksum
     */
    uint32_t calculateChecksum(const void* data, size_t size) const;

    /**
     * @brief Validate schema header
     * 
     * @param header Schema header to validate
     * @param data_size Expected data size
     * @throws TranscodingError if header is invalid
     */
    void validateHeader(const SchemaHeader& header, size_t data_size) const;
};

} // namespace core
} // namespace xenocomm 