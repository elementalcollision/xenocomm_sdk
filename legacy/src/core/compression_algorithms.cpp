#include "xenocomm/core/compression_algorithms.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace xenocomm {
namespace core {

namespace {

// Helper functions for pattern analysis
bool hasRepeatingPatterns(const std::vector<uint8_t>& data, size_t min_run_length = 4) {
    if (data.size() < min_run_length) return false;

    size_t run_count = 0;
    size_t max_run = 0;
    uint8_t current = data[0];
    size_t current_run = 1;

    for (size_t i = 1; i < data.size(); ++i) {
        if (data[i] == current) {
            current_run++;
            if (current_run >= min_run_length) {
                run_count++;
                max_run = std::max(max_run, current_run);
            }
        } else {
            current = data[i];
            current_run = 1;
        }
    }

    // Consider data suitable for RLE if:
    // 1. At least 20% of data is in runs, or
    // 2. Has at least one very long run (>= 8 bytes)
    return (run_count * min_run_length >= data.size() / 5) || (max_run >= 8);
}

bool hasSequentialPattern(const std::vector<uint8_t>& data, int max_delta = 3) {
    if (data.size() < 2) return false;

    size_t sequential_count = 0;
    for (size_t i = 1; i < data.size(); ++i) {
        int delta = std::abs(static_cast<int>(data[i]) - static_cast<int>(data[i-1]));
        if (delta <= max_delta) {
            sequential_count++;
        }
    }

    // Consider data sequential if at least 70% of values have small deltas
    return sequential_count >= (data.size() - 1) * 7 / 10;
}

} // namespace

// RunLengthEncoding implementation
std::vector<uint8_t> RunLengthEncoding::compress(const std::vector<uint8_t>& data) {
    if (data.empty()) return {};

    std::vector<uint8_t> compressed;
    compressed.reserve(data.size()); // Reserve space for worst case

    uint8_t current = data[0];
    uint8_t count = 1;

    for (size_t i = 1; i < data.size(); ++i) {
        if (data[i] == current && count < std::numeric_limits<uint8_t>::max()) {
            count++;
        } else {
            // Store the count and value
            compressed.push_back(count);
            compressed.push_back(current);
            current = data[i];
            count = 1;
        }
    }

    // Store the last run
    compressed.push_back(count);
    compressed.push_back(current);

    return compressed;
}

std::vector<uint8_t> RunLengthEncoding::decompress(const std::vector<uint8_t>& compressed_data) {
    if (compressed_data.empty()) return {};
    if (compressed_data.size() % 2 != 0) {
        throw CompressionError("Invalid RLE compressed data format", CompressionErrorCode::INVALID_FORMAT);
    }

    std::vector<uint8_t> decompressed;
    decompressed.reserve(compressed_data.size() * 2); // Initial estimate

    try {
        for (size_t i = 0; i < compressed_data.size(); i += 2) {
            uint8_t count = compressed_data[i];
            uint8_t value = compressed_data[i + 1];
            
            // Check for potential buffer overflow
            if (decompressed.size() + count > decompressed.capacity()) {
                if (decompressed.size() + count > decompressed.max_size()) {
                    throw CompressionError("Buffer overflow during decompression", 
                                         CompressionErrorCode::BUFFER_OVERFLOW);
                }
                decompressed.reserve((decompressed.size() + count) * 2);
            }
            
            decompressed.insert(decompressed.end(), count, value);
        }
    } catch (const std::exception& e) {
        throw CompressionError("Failed to decompress RLE data: " + std::string(e.what()),
                             CompressionErrorCode::DECOMPRESSION_FAILURE);
    }

    return decompressed;
}

bool RunLengthEncoding::isSuitableFor(const std::vector<uint8_t>& data) const {
    return hasRepeatingPatterns(data);
}

// DeltaEncoding implementation
std::vector<uint8_t> DeltaEncoding::compress(const std::vector<uint8_t>& data) {
    if (data.empty()) return {};

    if (!isSuitableFor(data)) {
        throw CompressionError("Data not suitable for delta encoding (deltas too large)",
                             CompressionErrorCode::UNSUPPORTED_ALGORITHM);
    }

    std::vector<uint8_t> compressed;
    compressed.reserve(data.size() + 1); // +1 for storing the first value

    // Store the first value as-is
    compressed.push_back(data[0]);

    // Store deltas
    for (size_t i = 1; i < data.size(); ++i) {
        int delta = static_cast<int>(data[i]) - static_cast<int>(data[i-1]);
        // Convert to signed byte representation
        compressed.push_back(static_cast<uint8_t>(delta));
    }

    return compressed;
}

std::vector<uint8_t> DeltaEncoding::decompress(const std::vector<uint8_t>& compressed_data) {
    if (compressed_data.empty()) return {};

    std::vector<uint8_t> decompressed;
    decompressed.reserve(compressed_data.size());

    try {
        // First value is stored as-is
        decompressed.push_back(compressed_data[0]);

        // Reconstruct values from deltas
        for (size_t i = 1; i < compressed_data.size(); ++i) {
            int8_t delta = static_cast<int8_t>(compressed_data[i]);
            int next_value = static_cast<int>(decompressed.back()) + delta;
            
            if (next_value < 0 || next_value > 255) {
                throw CompressionError("Delta decompression resulted in out-of-range value",
                                     CompressionErrorCode::DECOMPRESSION_FAILURE);
            }
            
            decompressed.push_back(static_cast<uint8_t>(next_value));
        }
    } catch (const std::exception& e) {
        throw CompressionError("Failed to decompress delta data: " + std::string(e.what()),
                             CompressionErrorCode::DECOMPRESSION_FAILURE);
    }

    return decompressed;
}

bool DeltaEncoding::isSuitableFor(const std::vector<uint8_t>& data) const {
    return hasSequentialPattern(data);
}

} // namespace core
} // namespace xenocomm 