#pragma once

#include "xenocomm/core/data_transcoder.h"
#include "xenocomm/core/compression_algorithms.h"
#include <memory>
#include <chrono>
#include <string>

namespace xenocomm {
namespace core {

/**
 * @brief Adapter for handling compressed state data using various compression algorithms
 */
class CompressedStateAdapter : public DataTranscoder {
public:
    /**
     * @brief Constructor
     * @param algorithm Initial compression algorithm (defaults to RLE)
     */
    explicit CompressedStateAdapter(std::unique_ptr<CompressionAlgorithm> algorithm = std::make_unique<RunLengthEncoding>());

    std::vector<uint8_t> encode(const void* data, size_t size, DataFormat format) override;
    std::vector<uint8_t> decode(const std::vector<uint8_t>& encoded_data, DataFormat source_format) override;
    bool isValidFormat(const void* data, size_t size, DataFormat format) const override;
    TranscodingMetadata getMetadata(const std::vector<uint8_t>& encoded_data) const override;

private:
    static constexpr uint8_t MAGIC_HEADER[4] = {'C', 'M', 'P', 'R'};
    static constexpr uint8_t ALGORITHM_RLE = 0x01;
    static constexpr uint8_t ALGORITHM_DELTA = 0x02;

    std::unique_ptr<CompressionAlgorithm> compression_algorithm_;
    
    // Header structure for compressed data
    struct CompressedHeader {
        uint8_t magic[4];           // "CMPR"
        uint8_t algorithm_id;       // 0x01=RLE, 0x02=Delta
        uint32_t original_size;     // Size of uncompressed data
        uint32_t checksum;          // Checksum of original data
        uint16_t metadata_length;   // Length of JSON metadata
        // Followed by:
        // - JSON metadata (compression ratio, timestamp)
        // - Compressed payload
    };

    // Helper methods
    std::vector<uint8_t> createHeader(const std::vector<uint8_t>& original_data, float compression_ratio) const;
    CompressedHeader parseHeader(const std::vector<uint8_t>& data, size_t& header_size) const;
    std::string createMetadataJson(float compression_ratio) const;
    std::shared_ptr<CompressionAlgorithm> selectBestAlgorithm(const std::vector<uint8_t>& data) const;
    uint8_t getAlgorithmId(const CompressionAlgorithm& algorithm) const;
    std::unique_ptr<CompressionAlgorithm> createAlgorithm(uint8_t algorithm_id) const;
};

} // namespace core
} // namespace xenocomm 