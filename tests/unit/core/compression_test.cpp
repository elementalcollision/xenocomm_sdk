#include <gtest/gtest.h>
#include "xenocomm/core/compression_algorithms.h"
#include "xenocomm/core/compressed_state_adapter.h"
#include <random>
#include <algorithm>

using namespace xenocomm::core;

class CompressionTest : public ::testing::Test {
protected:
    // Test data generation
    std::vector<uint8_t> generateRepeatingData(size_t size) {
        std::vector<uint8_t> data(size);
        for (size_t i = 0; i < size; i++) {
            data[i] = static_cast<uint8_t>(i % 4); // Creates repeating pattern
        }
        return data;
    }

    std::vector<uint8_t> generateSequentialData(size_t size) {
        std::vector<uint8_t> data(size);
        for (size_t i = 0; i < size; i++) {
            data[i] = static_cast<uint8_t>(i % 256);
        }
        return data;
    }

    std::vector<uint8_t> generateRandomData(size_t size) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);

        std::vector<uint8_t> data(size);
        std::generate(data.begin(), data.end(), [&]() { return static_cast<uint8_t>(dis(gen)); });
        return data;
    }
};

// RLE Tests
TEST_F(CompressionTest, RLECompressRepeatingData) {
    RunLengthEncoding rle;
    auto data = generateRepeatingData(100);
    
    auto compressed = rle.compress(data.data(), data.size());
    auto decompressed = rle.decompress(compressed, data.size());
    
    EXPECT_LT(compressed.size(), data.size()) << "Compressed size should be smaller for repeating data";
    EXPECT_EQ(decompressed, data) << "Decompressed data should match original";
}

TEST_F(CompressionTest, RLECompressRandomData) {
    RunLengthEncoding rle;
    auto data = generateRandomData(100);
    
    auto compressed = rle.compress(data.data(), data.size());
    auto decompressed = rle.decompress(compressed, data.size());
    
    EXPECT_EQ(decompressed, data) << "Decompressed data should match original";
}

TEST_F(CompressionTest, RLEHandleEmptyInput) {
    RunLengthEncoding rle;
    std::vector<uint8_t> empty;
    
    auto compressed = rle.compress(empty.data(), empty.size());
    EXPECT_TRUE(compressed.empty());
    
    auto decompressed = rle.decompress(compressed, 0);
    EXPECT_TRUE(decompressed.empty());
}

// Delta Encoding Tests
TEST_F(CompressionTest, DeltaCompressSequentialData) {
    DeltaEncoding delta;
    auto data = generateSequentialData(100);
    
    EXPECT_TRUE(delta.isSuitableForDeltaEncoding(data.data(), data.size()));
    
    auto compressed = delta.compress(data.data(), data.size());
    auto decompressed = delta.decompress(compressed, data.size());
    
    EXPECT_LT(compressed.size(), data.size()) << "Compressed size should be smaller for sequential data";
    EXPECT_EQ(decompressed, data) << "Decompressed data should match original";
}

TEST_F(CompressionTest, DeltaCompressRandomData) {
    DeltaEncoding delta;
    auto data = generateRandomData(100);
    
    // This might throw if the data has large deltas
    if (delta.isSuitableForDeltaEncoding(data.data(), data.size())) {
        auto compressed = delta.compress(data.data(), data.size());
        auto decompressed = delta.decompress(compressed, data.size());
        EXPECT_EQ(decompressed, data);
    }
}

TEST_F(CompressionTest, DeltaHandleEmptyInput) {
    DeltaEncoding delta;
    std::vector<uint8_t> empty;
    
    auto compressed = delta.compress(empty.data(), empty.size());
    EXPECT_TRUE(compressed.empty());
    
    auto decompressed = delta.decompress(compressed, 0);
    EXPECT_TRUE(decompressed.empty());
}

// CompressedStateAdapter Tests
TEST_F(CompressionTest, CompressedStateWithRLE) {
    auto adapter = std::make_unique<CompressedStateAdapter>(std::make_unique<RunLengthEncoding>());
    auto data = generateRepeatingData(100);
    
    auto encoded = adapter->encode(data.data(), data.size(), DataFormat::COMPRESSED_STATE);
    auto decoded = adapter->decode(encoded, DataFormat::COMPRESSED_STATE);
    
    EXPECT_EQ(decoded, data) << "Decoded data should match original";
    
    // Check metadata
    auto metadata = adapter->getMetadata(encoded);
    EXPECT_EQ(metadata.format, DataFormat::COMPRESSED_STATE);
    EXPECT_EQ(metadata.original_size, data.size());
    EXPECT_LT(metadata.compression_ratio, 1.0f) << "Should achieve some compression for repeating data";
}

TEST_F(CompressionTest, CompressedStateWithDelta) {
    auto adapter = std::make_unique<CompressedStateAdapter>(std::make_unique<DeltaEncoding>());
    auto data = generateSequentialData(100);
    
    auto encoded = adapter->encode(data.data(), data.size(), DataFormat::COMPRESSED_STATE);
    auto decoded = adapter->decode(encoded, DataFormat::COMPRESSED_STATE);
    
    EXPECT_EQ(decoded, data) << "Decoded data should match original";
    
    // Check metadata
    auto metadata = adapter->getMetadata(encoded);
    EXPECT_EQ(metadata.format, DataFormat::COMPRESSED_STATE);
    EXPECT_EQ(metadata.original_size, data.size());
    EXPECT_LT(metadata.compression_ratio, 1.0f) << "Should achieve some compression for sequential data";
}

TEST_F(CompressionTest, CompressedStateInvalidFormat) {
    auto adapter = std::make_unique<CompressedStateAdapter>();
    auto data = generateRandomData(100);
    
    EXPECT_THROW(adapter->encode(data.data(), data.size(), DataFormat::VECTOR_FLOAT32),
                 TranscodingError);
    
    auto encoded = adapter->encode(data.data(), data.size(), DataFormat::COMPRESSED_STATE);
    EXPECT_THROW(adapter->decode(encoded, DataFormat::VECTOR_FLOAT32),
                 TranscodingError);
}

TEST_F(CompressionTest, CompressedStateCorruptedHeader) {
    auto adapter = std::make_unique<CompressedStateAdapter>();
    auto data = generateRandomData(100);
    
    auto encoded = adapter->encode(data.data(), data.size(), DataFormat::COMPRESSED_STATE);
    
    // Corrupt the header
    encoded[0] = 0xFF;
    encoded[1] = 0xFF;
    
    EXPECT_THROW(adapter->decode(encoded, DataFormat::COMPRESSED_STATE),
                 TranscodingError);
} 