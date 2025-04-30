#include "xenocomm/core/error_correction.h"
#include "xenocomm/utils/logging.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <reed_solomon_erasure.hpp>
#include <thread>
#include <future>

namespace xenocomm {
namespace core {

// CRC32ErrorDetection implementation

CRC32ErrorDetection::CRC32ErrorDetection() : crc_table_(256, 0) {
    initCRCTable();
}

void CRC32ErrorDetection::initCRCTable() {
    // Generate CRC lookup table
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (uint32_t j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
            } else {
                crc = crc >> 1;
            }
        }
        crc_table_[i] = crc;
    }
}

uint32_t CRC32ErrorDetection::computeCRC32(const uint8_t* data, size_t length) const {
    uint32_t crc = 0xFFFFFFFF;
    
    // Process each byte using the lookup table
    for (size_t i = 0; i < length; i++) {
        uint8_t index = (crc ^ data[i]) & 0xFF;
        crc = (crc >> 8) ^ crc_table_[index];
    }
    
    return ~crc; // Final XOR
}

bool CRC32ErrorDetection::verifyCRC32(const uint8_t* data, size_t length) const {
    if (length < CRC_SIZE) {
        return false;
    }
    
    // Extract the stored CRC
    uint32_t stored_crc;
    std::memcpy(&stored_crc, data + length - CRC_SIZE, CRC_SIZE);
    
    // Compute CRC of the data without the stored CRC
    uint32_t computed_crc = computeCRC32(data, length - CRC_SIZE);
    
    return stored_crc == computed_crc;
}

std::vector<uint8_t> CRC32ErrorDetection::encode(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> encoded;
    encoded.reserve(data.size() + CRC_SIZE);
    
    // Copy the original data
    encoded.insert(encoded.end(), data.begin(), data.end());
    
    // Compute and append CRC
    uint32_t crc = computeCRC32(data.data(), data.size());
    uint8_t crc_bytes[CRC_SIZE];
    std::memcpy(crc_bytes, &crc, CRC_SIZE);
    encoded.insert(encoded.end(), crc_bytes, crc_bytes + CRC_SIZE);
    
    return encoded;
}

std::optional<std::vector<uint8_t>> CRC32ErrorDetection::decode(const std::vector<uint8_t>& data) {
    if (data.size() < CRC_SIZE) {
        LOG_ERROR("Data too short to contain CRC32");
        return std::nullopt;
    }
    
    // Verify the CRC
    if (!verifyCRC32(data.data(), data.size())) {
        LOG_WARNING("CRC32 verification failed");
        return std::nullopt;
    }
    
    // Return the data without the CRC
    return std::vector<uint8_t>(data.begin(), data.end() - CRC_SIZE);
}

// ReedSolomonCorrection implementation

namespace {
    // Helper function to calculate shard size
    size_t calculateShardSize(size_t dataSize, uint8_t dataShards) {
        return (dataSize + dataShards - 1) / dataShards;
    }

    // Helper function to store original data size
    void storeOriginalSize(std::vector<uint8_t>& data, size_t originalSize) {
        uint8_t sizeBytes[sizeof(size_t)];
        std::memcpy(sizeBytes, &originalSize, sizeof(size_t));
        
        auto insertPos = data.end() - sizeof(size_t);
        std::copy(sizeBytes, sizeBytes + sizeof(size_t), insertPos);
    }

    // Helper function to retrieve original data size
    size_t retrieveOriginalSize(const std::vector<uint8_t>& data) {
        size_t originalSize;
        std::memcpy(&originalSize, data.data() + data.size() - sizeof(size_t), sizeof(size_t));
        return originalSize;
    }

    // Helper function to interleave data
    std::vector<uint8_t> interleave(const std::vector<uint8_t>& data, uint16_t depth) {
        if (depth <= 1 || data.empty()) return data;
        
        std::vector<uint8_t> result(data.size());
        size_t rows = depth;
        size_t cols = (data.size() + depth - 1) / depth;
        
        for (size_t i = 0; i < data.size(); i++) {
            size_t row = i % rows;
            size_t col = i / rows;
            size_t new_pos = row * cols + col;
            
            if (new_pos < result.size()) {
                result[new_pos] = data[i];
            }
        }
        return result;
    }

    // Helper function to deinterleave data
    std::vector<uint8_t> deinterleave(const std::vector<uint8_t>& data, uint16_t depth) {
        if (depth <= 1 || data.empty()) return data;
        
        std::vector<uint8_t> result(data.size());
        size_t rows = depth;
        size_t cols = (data.size() + depth - 1) / depth;
        
        for (size_t i = 0; i < data.size(); i++) {
            size_t row = i / cols;
            size_t col = i % cols;
            size_t new_pos = col * rows + row;
            
            if (new_pos < result.size()) {
                result[new_pos] = data[i];
            }
        }
        return result;
    }
}

ReedSolomonCorrection::ReedSolomonCorrection(const Config& config) : config_(config) {
    LOG_INFO("Initializing Reed-Solomon correction with " + 
             std::to_string(config.data_shards) + " data shards and " +
             std::to_string(config.parity_shards) + " parity shards");
}

std::vector<uint8_t> ReedSolomonCorrection::encode(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return data;
    }

    // Calculate shard size and pad data if necessary
    size_t shardSize = calculateShardSize(data.size(), config_.data_shards);
    size_t totalShards = config_.data_shards + config_.parity_shards;
    
    // Prepare data with padding
    std::vector<uint8_t> paddedData = data;
    size_t paddedSize = shardSize * config_.data_shards;
    if (paddedData.size() < paddedSize) {
        paddedData.resize(paddedSize, 0);
        storeOriginalSize(paddedData, data.size());
    }

    // Apply interleaving if enabled
    if (config_.enable_interleaving) {
        paddedData = interleave(paddedData, 16); // Use fixed depth of 16 for now
    }

    // Initialize Reed-Solomon encoder
    reed_solomon_erasure::ReedSolomon rs(config_.data_shards, config_.parity_shards);
    
    // Prepare shard pointers
    std::vector<std::vector<uint8_t>> shards(totalShards);
    std::vector<uint8_t*> shardPtrs(totalShards);
    
    // Split data into data shards
    for (uint8_t i = 0; i < config_.data_shards; i++) {
        shards[i].resize(shardSize);
        std::copy(
            paddedData.begin() + i * shardSize,
            paddedData.begin() + (i + 1) * shardSize,
            shards[i].begin()
        );
        shardPtrs[i] = shards[i].data();
    }
    
    // Initialize parity shards
    for (uint8_t i = config_.data_shards; i < totalShards; i++) {
        shards[i].resize(shardSize, 0);
        shardPtrs[i] = shards[i].data();
    }
    
    // Encode data
    if (!rs.encode(shardPtrs.data(), shardSize)) {
        LOG_ERROR("Reed-Solomon encoding failed");
        return data;
    }
    
    // Combine all shards into final output
    std::vector<uint8_t> encoded;
    encoded.reserve(totalShards * shardSize);
    
    for (const auto& shard : shards) {
        encoded.insert(encoded.end(), shard.begin(), shard.end());
    }
    
    return encoded;
}

std::optional<std::vector<uint8_t>> ReedSolomonCorrection::decode(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return data;
    }

    size_t totalShards = config_.data_shards + config_.parity_shards;
    size_t shardSize = data.size() / totalShards;
    
    if (data.size() % totalShards != 0) {
        LOG_ERROR("Invalid data size for Reed-Solomon decoding");
        return std::nullopt;
    }
    
    // Initialize Reed-Solomon decoder
    reed_solomon_erasure::ReedSolomon rs(config_.data_shards, config_.parity_shards);
    
    // Prepare shard pointers
    std::vector<std::vector<uint8_t>> shards(totalShards);
    std::vector<uint8_t*> shardPtrs(totalShards);
    
    // Split data into shards
    for (size_t i = 0; i < totalShards; i++) {
        shards[i].resize(shardSize);
        std::copy(
            data.begin() + i * shardSize,
            data.begin() + (i + 1) * shardSize,
            shards[i].begin()
        );
        shardPtrs[i] = shards[i].data();
    }
    
    // Decode data
    if (!rs.decode(shardPtrs.data(), nullptr, shardSize)) {
        LOG_ERROR("Reed-Solomon decoding failed");
        return std::nullopt;
    }
    
    // Combine data shards
    std::vector<uint8_t> decoded;
    decoded.reserve(config_.data_shards * shardSize);
    
    for (uint8_t i = 0; i < config_.data_shards; i++) {
        decoded.insert(decoded.end(), shards[i].begin(), shards[i].end());
    }
    
    // Deinterleave if necessary
    if (config_.enable_interleaving) {
        decoded = deinterleave(decoded, 16);
    }
    
    // Retrieve original size and truncate if necessary
    size_t originalSize = retrieveOriginalSize(decoded);
    if (originalSize < decoded.size()) {
        decoded.resize(originalSize);
    }
    
    return decoded;
}

int ReedSolomonCorrection::maxCorrectableErrors() const {
    return config_.parity_shards / 2;
}

void ReedSolomonCorrection::configure(const Config& config) {
    config_ = config;
    LOG_INFO("Reconfigured Reed-Solomon correction with " + 
             std::to_string(config.data_shards) + " data shards and " +
             std::to_string(config.parity_shards) + " parity shards");
}

// ErrorCorrectionFactory implementation

std::unique_ptr<IErrorCorrection> ErrorCorrectionFactory::create(
    TransmissionManager::ErrorCorrectionMode mode) {
    switch (mode) {
        case TransmissionManager::ErrorCorrectionMode::REED_SOLOMON:
            return std::make_unique<ReedSolomonCorrection>();
        case TransmissionManager::ErrorCorrectionMode::CHECKSUM_ONLY:
            return std::make_unique<CRC32ErrorDetection>();
        case TransmissionManager::ErrorCorrectionMode::NONE:
            // For NONE mode, we'll still use CRC32 but ignore its results
            return std::make_unique<CRC32ErrorDetection>();
        default:
            LOG_ERROR("Unknown error correction mode");
            return nullptr;
    }
}

} // namespace core
} // namespace xenocomm 