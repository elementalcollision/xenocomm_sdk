#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <stdexcept>

namespace xenocomm {
namespace core {

/**
 * @brief Enumeration of supported data formats for encoding/decoding
 */
enum class DataFormat {
    VECTOR_FLOAT32,    ///< Vector of 32-bit floating point values
    VECTOR_INT8,       ///< Vector of 8-bit integer values (quantized)
    COMPRESSED_STATE,  ///< Compressed state representation
    BINARY_CUSTOM,     ///< Custom binary serialization format
    GGWAVE_FSK         ///< Audio-based FSK encoding format
};

/**
 * @brief Exception class for data transcoding errors
 */
class TranscodingError : public std::runtime_error {
public:
    explicit TranscodingError(const std::string& message) 
        : std::runtime_error(message) {}
};

/**
 * @brief Metadata structure for encoded data
 */
struct TranscodingMetadata {
    DataFormat format;                    ///< Format of the encoded data
    std::vector<size_t> dimensions;       ///< Dimensions of the data (if applicable)
    float scale_factor;                   ///< Scale factor for quantized formats
    std::string compression_algorithm;     ///< Name of compression algorithm used (if any)
    uint32_t version;                     ///< Version of the encoding format
    size_t element_count;                 ///< Number of elements in the data
    size_t element_size;                  ///< Size of each element in bytes
    
    TranscodingMetadata() 
        : format(DataFormat::VECTOR_FLOAT32)
        , scale_factor(1.0f)
        , version(1)
        , element_count(0)
        , element_size(0) {}
};

/**
 * @brief Abstract base class for data transcoding operations
 * 
 * The DataTranscoder class provides an interface for encoding and decoding data
 * between different formats, with support for various data types and compression
 * algorithms.
 */
class DataTranscoder {
public:
    virtual ~DataTranscoder() = default;

    /**
     * @brief Encode data into the specified format
     * 
     * @param data Raw input data as bytes
     * @param size Size of input data in bytes
     * @param format Target format for encoding
     * @return std::vector<uint8_t> Encoded data
     * @throws TranscodingError if encoding fails
     */
    virtual std::vector<uint8_t> encode(
        const void* data,
        size_t size,
        DataFormat format) = 0;

    /**
     * @brief Decode data from the specified format
     * 
     * @param encoded_data Encoded input data
     * @param source_format Format of the encoded data
     * @return std::vector<uint8_t> Decoded data
     * @throws TranscodingError if decoding fails
     */
    virtual std::vector<uint8_t> decode(
        const std::vector<uint8_t>& encoded_data,
        DataFormat source_format) = 0;

    /**
     * @brief Validate if data matches format requirements
     * 
     * @param data Data to validate
     * @param size Size of data in bytes
     * @param format Format to validate against
     * @return true if data is valid for the format
     * @return false if data is invalid for the format
     */
    virtual bool isValidFormat(
        const void* data,
        size_t size,
        DataFormat format) const = 0;

    /**
     * @brief Get metadata from encoded data
     * 
     * @param encoded_data Encoded data to extract metadata from
     * @return TranscodingMetadata Metadata structure
     * @throws TranscodingError if metadata extraction fails
     */
    virtual TranscodingMetadata getMetadata(
        const std::vector<uint8_t>& encoded_data) const = 0;

protected:
    /**
     * @brief Helper method to validate input parameters
     * 
     * @param data Input data pointer
     * @param size Size of input data
     * @throws TranscodingError if validation fails
     */
    void validateInput(const void* data, size_t size) const {
        if (data == nullptr) {
            throw TranscodingError("Input data pointer is null");
        }
        if (size == 0) {
            throw TranscodingError("Input data size is 0");
        }
    }
};

} // namespace core
} // namespace xenocomm 