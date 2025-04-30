#include "xenocomm/core/compressed_state_adapter.h"
#include <cstring>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <stdexcept>

namespace xenocomm {
namespace core {

CompressedStateAdapter::CompressedStateAdapter(std::unique_ptr<CompressionAlgorithm> algorithm)
    : compression_algorithm_(std::move(algorithm)) {
    if (!compression_algorithm_) {
        throw TranscodingError("Compression algorithm cannot be null");
    }
}

std::vector<uint8_t> CompressedStateAdapter::createHeader(
    const std::vector<uint8_t>& original_data,
    float compression_ratio) const {
    
    // Create JSON metadata
    std::string metadata = createMetadataJson(compression_ratio);
    
    // Calculate total header size
    size_t header_size = sizeof(CompressedHeader) + metadata.length();
    std::vector<uint8_t> header(header_size);
    
    // Fill header structure
    CompressedHeader* h = reinterpret_cast<CompressedHeader*>(header.data());
    std::memcpy(h->magic, MAGIC_HEADER, sizeof(MAGIC_HEADER));
    h->algorithm_id = getAlgorithmId(*compression_algorithm_);
    h->original_size = static_cast<uint32_t>(original_data.size());
    h->checksum = CompressionAlgorithm::calculateChecksum(original_data);
    h->metadata_length = static_cast<uint16_t>(metadata.length());
    
    // Copy metadata after header structure
    std::memcpy(header.data() + sizeof(CompressedHeader), 
                metadata.data(), 
                metadata.length());
    
    return header;
}

CompressedStateAdapter::CompressedHeader CompressedStateAdapter::parseHeader(
    const std::vector<uint8_t>& data, 
    size_t& header_size) const {
    
    if (data.size() < sizeof(CompressedHeader)) {
        throw CompressionError("Invalid compressed data: header too small",
                             CompressionErrorCode::INVALID_FORMAT);
    }
    
    // Read header structure
    CompressedHeader header;
    std::memcpy(&header, data.data(), sizeof(CompressedHeader));
    
    // Validate magic number
    if (std::memcmp(header.magic, MAGIC_HEADER, sizeof(MAGIC_HEADER)) != 0) {
        throw CompressionError("Invalid compressed data: wrong magic number",
                             CompressionErrorCode::INVALID_FORMAT);
    }
    
    // Calculate total header size including metadata
    header_size = sizeof(CompressedHeader) + header.metadata_length;
    
    if (data.size() < header_size) {
        throw CompressionError("Invalid compressed data: missing metadata",
                             CompressionErrorCode::INVALID_FORMAT);
    }
    
    return header;
}

std::string CompressedStateAdapter::createMetadataJson(float compression_ratio) const {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    std::ostringstream json;
    json << "{\"compression_ratio\":" << std::fixed << std::setprecision(2) << compression_ratio
         << ",\"timestamp\":" << timestamp
         << ",\"algorithm\":\"" << compression_algorithm_->getAlgorithmId() << "\"}";
    
    return json.str();
}

std::shared_ptr<CompressionAlgorithm> CompressedStateAdapter::selectBestAlgorithm(
    const std::vector<uint8_t>& data) const {
    
    // Create instances of available algorithms
    auto rle = std::make_shared<RunLengthEncoding>();
    auto delta = std::make_shared<DeltaEncoding>();
    
    // Check suitability for each algorithm
    if (rle->isSuitableFor(data)) {
        return rle;
    } else if (delta->isSuitableFor(data)) {
        return delta;
    }
    
    // Default to RLE for general cases
    return rle;
}

uint8_t CompressedStateAdapter::getAlgorithmId(const CompressionAlgorithm& algorithm) const {
    std::string id = algorithm.getAlgorithmId();
    if (id == "RLE") return ALGORITHM_RLE;
    if (id == "DELTA") return ALGORITHM_DELTA;
    throw CompressionError("Unknown compression algorithm",
                          CompressionErrorCode::UNSUPPORTED_ALGORITHM);
}

std::unique_ptr<CompressionAlgorithm> CompressedStateAdapter::createAlgorithm(uint8_t algorithm_id) const {
    switch (algorithm_id) {
        case ALGORITHM_RLE:
            return std::make_unique<RunLengthEncoding>();
        case ALGORITHM_DELTA:
            return std::make_unique<DeltaEncoding>();
        default:
            throw CompressionError("Unsupported compression algorithm ID",
                                 CompressionErrorCode::UNSUPPORTED_ALGORITHM);
    }
}

std::vector<uint8_t> CompressedStateAdapter::encode(const void* data, size_t size, DataFormat format) {
    if (!data || size == 0) {
        throw TranscodingError("Invalid input data");
    }
    
    if (format != DataFormat::COMPRESSED_STATE) {
        throw TranscodingError("CompressedStateAdapter only supports COMPRESSED_STATE format");
    }
    
    // Convert input to vector for easier handling
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    std::vector<uint8_t> input_data(bytes, bytes + size);
    
    // Select best algorithm if none is set
    if (!compression_algorithm_) {
        compression_algorithm_ = selectBestAlgorithm(input_data)->clone();
    }
    
    // Compress the data
    auto compressed = compression_algorithm_->compress(input_data);
    
    // Calculate compression ratio
    float compression_ratio = static_cast<float>(compressed.size()) / size;
    
    // Create header
    std::vector<uint8_t> result = createHeader(input_data, compression_ratio);
    
    // Combine header and compressed data
    result.insert(result.end(), compressed.begin(), compressed.end());
    
    return result;
}

std::vector<uint8_t> CompressedStateAdapter::decode(
    const std::vector<uint8_t>& encoded_data, 
    DataFormat source_format) {
    
    if (source_format != DataFormat::COMPRESSED_STATE) {
        throw TranscodingError("CompressedStateAdapter only supports COMPRESSED_STATE format");
    }
    
    // Parse header
    size_t header_size;
    CompressedHeader header = parseHeader(encoded_data, header_size);
    
    // Create appropriate algorithm instance
    auto algorithm = createAlgorithm(header.algorithm_id);
    
    // Extract compressed data
    std::vector<uint8_t> compressed_data(
        encoded_data.begin() + header_size,
        encoded_data.end()
    );
    
    // Decompress
    auto decompressed = algorithm->decompress(compressed_data);
    
    // Verify checksum
    uint32_t checksum = CompressionAlgorithm::calculateChecksum(decompressed);
    if (checksum != header.checksum) {
        throw CompressionError("Checksum mismatch in decompressed data",
                             CompressionErrorCode::CHECKSUM_MISMATCH);
    }
    
    return decompressed;
}

bool CompressedStateAdapter::isValidFormat(const void* data, size_t size, DataFormat format) const {
    if (!data || size == 0) return false;
    if (format != DataFormat::COMPRESSED_STATE) return false;
    
    // Check if data starts with our magic number
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    return (size >= sizeof(MAGIC_HEADER) && 
            std::memcmp(bytes, MAGIC_HEADER, sizeof(MAGIC_HEADER)) == 0);
}

TranscodingMetadata CompressedStateAdapter::getMetadata(const std::vector<uint8_t>& encoded_data) const {
    size_t header_size;
    CompressedHeader header = parseHeader(encoded_data, header_size);
    
    // Extract JSON metadata
    std::string metadata_json(
        reinterpret_cast<const char*>(encoded_data.data() + sizeof(CompressedHeader)),
        header.metadata_length
    );
    
    TranscodingMetadata metadata;
    metadata.format = DataFormat::COMPRESSED_STATE;
    metadata.original_size = header.original_size;
    metadata.compressed_size = encoded_data.size() - header_size;
    metadata.compression_ratio = static_cast<float>(metadata.compressed_size) / header.original_size;
    metadata.additional_info = metadata_json;
    
    return metadata;
}

} // namespace core
} // namespace xenocomm 