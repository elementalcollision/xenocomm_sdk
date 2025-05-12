#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <stdexcept>
#include <string>

namespace xenocomm {
namespace core {

/**
 * @brief Error codes for compression operations
 */
enum class CompressionErrorCode : uint8_t {
    INVALID_FORMAT = 0x01,
    CHECKSUM_MISMATCH = 0x02,
    UNSUPPORTED_ALGORITHM = 0x03,
    DECOMPRESSION_FAILURE = 0x04,
    BUFFER_OVERFLOW = 0x05
};

/**
 * @brief Exception class for compression-related errors
 */
class CompressionError : public std::runtime_error {
public:
    explicit CompressionError(const std::string& message, CompressionErrorCode code) 
        : std::runtime_error(message), error_code_(code) {}
    
    CompressionErrorCode getErrorCode() const { return error_code_; }

private:
    CompressionErrorCode error_code_;
};

/**
 * @brief Interface for compression algorithms
 */
class CompressionAlgorithm {
public:
    virtual ~CompressionAlgorithm() = default;

    /**
     * @brief Compress the input data
     * @param data Input data as vector of bytes
     * @return Compressed data as a vector of bytes
     * @throws CompressionError if compression fails
     */
    virtual std::vector<uint8_t> compress(const std::vector<uint8_t>& data) = 0;

    /**
     * @brief Decompress the input data
     * @param compressed_data Compressed data
     * @return Decompressed data as a vector of bytes
     * @throws CompressionError if decompression fails
     */
    virtual std::vector<uint8_t> decompress(const std::vector<uint8_t>& compressed_data) = 0;

    /**
     * @brief Check if this algorithm is suitable for the given data
     * @param data Data to analyze
     * @return true if the algorithm is suitable
     */
    virtual bool isSuitableFor(const std::vector<uint8_t>& data) const = 0;

    /**
     * @brief Get the algorithm identifier
     * @return Algorithm ID as a string
     */
    virtual std::string getAlgorithmId() const = 0;

    /**
     * @brief Create a polymorphic copy of this algorithm instance.
     * @return A unique_ptr to a copy of the concrete algorithm.
     */
    virtual std::unique_ptr<CompressionAlgorithm> clone() const = 0;

public:
    /**
     * @brief Calculate checksum for data integrity validation
     * @param data Data to calculate checksum for
     * @return 32-bit checksum
     */
    static uint32_t calculateChecksum(const std::vector<uint8_t>& data) {
        uint32_t checksum = 0;
        for (uint8_t byte : data) {
            checksum = (checksum << 1) | (checksum >> 31); // Rotate left
            checksum ^= byte;
        }
        return checksum;
    }
};

/**
 * @brief Run-Length Encoding compression algorithm
 * Efficient for data with repeated values
 */
class RunLengthEncoding : public CompressionAlgorithm {
public:
    std::vector<uint8_t> compress(const std::vector<uint8_t>& data) override;
    std::vector<uint8_t> decompress(const std::vector<uint8_t>& compressed_data) override;
    bool isSuitableFor(const std::vector<uint8_t>& data) const override;
    std::string getAlgorithmId() const override { return "RLE"; }
    std::unique_ptr<CompressionAlgorithm> clone() const override {
        return std::make_unique<RunLengthEncoding>(*this);
    }
};

/**
 * @brief Delta Encoding compression algorithm
 * Efficient for time-series or sequential data with small changes
 */
class DeltaEncoding : public CompressionAlgorithm {
public:
    std::vector<uint8_t> compress(const std::vector<uint8_t>& data) override;
    std::vector<uint8_t> decompress(const std::vector<uint8_t>& compressed_data) override;
    bool isSuitableFor(const std::vector<uint8_t>& data) const override;
    std::string getAlgorithmId() const override { return "DELTA"; }
    std::unique_ptr<CompressionAlgorithm> clone() const override {
        return std::make_unique<DeltaEncoding>(*this);
    }
};

} // namespace core
} // namespace xenocomm 