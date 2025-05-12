#include <gtest/gtest.h>
#include "xenocomm/core/error_correction.h"
#include <random>
#include <algorithm>

namespace xenocomm {
namespace core {
namespace {

class ErrorCorrectionTest : public ::testing::Test {
protected:
    // Helper function to generate random data
    std::vector<uint8_t> generateRandomData(size_t size) {
        std::vector<uint8_t> data(size);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        std::generate(data.begin(), data.end(), [&]() { return dis(gen); });
        return data;
    }
    
    // Helper function to corrupt data with bit errors
    void corruptData(std::vector<uint8_t>& data, size_t numErrors) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> posDis(0, data.size() - 1);
        std::uniform_int_distribution<> bitDis(0, 7);
        
        for (size_t i = 0; i < numErrors; i++) {
            size_t pos = posDis(gen);
            uint8_t bit = 1 << bitDis(gen);
            data[pos] ^= bit; // Flip a random bit
        }
    }
};

TEST_F(ErrorCorrectionTest, CRC32BasicTest) {
    CRC32ErrorDetection crc;
    std::vector<uint8_t> data = {'H', 'e', 'l', 'l', 'o'};
    
    auto encoded = crc.encode(data);
    EXPECT_GT(encoded.size(), data.size());
    
    auto decoded = crc.decode(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value(), data);
}

TEST_F(ErrorCorrectionTest, CRC32DetectsErrors) {
    CRC32ErrorDetection crc;
    std::vector<uint8_t> data = generateRandomData(100);
    
    auto encoded = crc.encode(data);
    corruptData(encoded, 1);
    
    auto decoded = crc.decode(encoded);
    EXPECT_FALSE(decoded.has_value());
}

TEST_F(ErrorCorrectionTest, ReedSolomonBasicTest) {
    ReedSolomonCorrection::Config config;
    config.data_shards = 4;
    config.parity_shards = 2;
    config.enable_interleaving = false;
    
    ReedSolomonCorrection rs(config);
    std::vector<uint8_t> data = generateRandomData(1000);
    
    auto encoded = rs.encode(data);
    EXPECT_GT(encoded.size(), data.size());
    
    auto decoded = rs.decode(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value(), data);
}

TEST_F(ErrorCorrectionTest, ReedSolomonCorrectsSingleErrors) {
    ReedSolomonCorrection::Config config;
    config.data_shards = 4;
    config.parity_shards = 2;
    config.enable_interleaving = false;
    
    ReedSolomonCorrection rs(config);
    std::vector<uint8_t> data = generateRandomData(1000);
    
    auto encoded = rs.encode(data);
    corruptData(encoded, 1); // Single error
    
    auto decoded = rs.decode(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value(), data);
}

TEST_F(ErrorCorrectionTest, ReedSolomonWithInterleaving) {
    ReedSolomonCorrection::Config config;
    config.data_shards = 4;
    config.parity_shards = 2;
    config.enable_interleaving = true;
    
    ReedSolomonCorrection rs(config);
    std::vector<uint8_t> data = generateRandomData(1000);
    
    auto encoded = rs.encode(data);
    
    // Corrupt a burst of consecutive bytes
    for (size_t i = 100; i < 110; i++) {
        encoded[i] ^= 0xFF;
    }
    
    auto decoded = rs.decode(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value(), data);
}

TEST_F(ErrorCorrectionTest, ReedSolomonMaxErrors) {
    ReedSolomonCorrection::Config config;
    config.data_shards = 4;
    config.parity_shards = 2;
    config.enable_interleaving = false;
    
    ReedSolomonCorrection rs(config);
    EXPECT_EQ(rs.maxCorrectableErrors(), 1); // (parity_shards / 2) = 1
    
    std::vector<uint8_t> data = generateRandomData(1000);
    auto encoded = rs.encode(data);
    
    // Test with maximum correctable errors
    corruptData(encoded, rs.maxCorrectableErrors());
    auto decoded = rs.decode(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value(), data);
    
    // Test with too many errors
    corruptData(encoded, rs.maxCorrectableErrors() + 1);
    decoded = rs.decode(encoded);
    EXPECT_FALSE(decoded.has_value());
}

TEST_F(ErrorCorrectionTest, FactoryTest) {
    auto none = ErrorCorrectionFactory::create(ErrorCorrectionMode::NONE);
    ASSERT_NE(none, nullptr);
    EXPECT_FALSE(none->canCorrect());
    
    auto checksum = ErrorCorrectionFactory::create(ErrorCorrectionMode::CHECKSUM_ONLY);
    ASSERT_NE(checksum, nullptr);
    EXPECT_FALSE(checksum->canCorrect());
    
    auto rs = ErrorCorrectionFactory::create(ErrorCorrectionMode::REED_SOLOMON);
    ASSERT_NE(rs, nullptr);
    EXPECT_TRUE(rs->canCorrect());
}

TEST_F(ErrorCorrectionTest, EmptyData) {
    std::vector<uint8_t> empty;
    
    CRC32ErrorDetection crc;
    auto crcEncoded = crc.encode(empty);
    EXPECT_TRUE(crcEncoded.empty());
    auto crcDecoded = crc.decode(empty);
    ASSERT_TRUE(crcDecoded.has_value());
    EXPECT_TRUE(crcDecoded.value().empty());
    
    ReedSolomonCorrection rs({});
    auto rsEncoded = rs.encode(empty);
    EXPECT_TRUE(rsEncoded.empty());
    auto rsDecoded = rs.decode(empty);
    ASSERT_TRUE(rsDecoded.has_value());
    EXPECT_TRUE(rsDecoded.value().empty());
}

TEST_F(ErrorCorrectionTest, NonAlignedData) {
    ReedSolomonCorrection::Config config;
    config.data_shards = 4;
    config.parity_shards = 2;
    
    ReedSolomonCorrection rs(config);
    std::vector<uint8_t> data = generateRandomData(999); // Non-aligned size
    
    auto encoded = rs.encode(data);
    auto decoded = rs.decode(encoded);
    
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value(), data);
}

TEST_F(ErrorCorrectionTest, FactoryCreation) {
    auto none = ErrorCorrectionFactory::create(ErrorCorrectionMode::NONE);
    ASSERT_NE(none, nullptr);
    ASSERT_EQ(none->name(), "None");

    auto checksum = ErrorCorrectionFactory::create(ErrorCorrectionMode::CHECKSUM_ONLY);
    ASSERT_NE(checksum, nullptr);
    ASSERT_EQ(checksum->name(), "CRC32");

    auto rs = ErrorCorrectionFactory::create(ErrorCorrectionMode::REED_SOLOMON);
    ASSERT_NE(rs, nullptr);
    ASSERT_EQ(rs->name(), "Reed-Solomon");
}

TEST_F(ErrorCorrectionTest, ReedSolomonDefaultConfigAndEncodeDecode) {
    ReedSolomonCorrection::Config defaultConfig;
    ASSERT_EQ(defaultConfig.data_shards, 223);
    ASSERT_EQ(defaultConfig.parity_shards, 32);
    ASSERT_TRUE(defaultConfig.enable_interleaving);

    ReedSolomonCorrection rs(defaultConfig);
    std::vector<uint8_t> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    auto encoded = rs.encode(data);
    EXPECT_GT(encoded.size(), data.size());
    
    auto decoded = rs.decode(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value(), data);
}

TEST_F(ErrorCorrectionTest, ReedSolomonNoInterleaving) {
    ReedSolomonCorrection::Config config;
    config.enable_interleaving = false;
    ReedSolomonCorrection rs(config);
    std::vector<uint8_t> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    auto encoded = rs.encode(data);
    
    // Corrupt a burst of consecutive bytes
    for (size_t i = 100; i < 110; i++) {
        encoded[i] ^= 0xFF;
    }
    
    auto decoded = rs.decode(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value(), data);
}

TEST_F(ErrorCorrectionTest, ReedSolomonCorrectBurstErrorsWithInterleaving) {
    ReedSolomonCorrection::Config config;
    config.data_shards = 4; 
    config.parity_shards = 2;
    config.enable_interleaving = true;
    ReedSolomonCorrection rs(config);

    std::vector<uint8_t> data = {0, 1, 2, 3, 4, 5, 6, 7}; // 2 blocks of 4 data shards
    
    auto encoded = rs.encode(data);
    
    // Corrupt a burst of consecutive bytes
    for (size_t i = 100; i < 110; i++) {
        encoded[i] ^= 0xFF;
    }
    
    auto decoded = rs.decode(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value(), data);
}

TEST_F(ErrorCorrectionTest, ReedSolomonFailWithTooManyBurstErrorsNoInterleaving) {
    ReedSolomonCorrection::Config config;
    config.data_shards = 4;
    config.parity_shards = 2;
    config.enable_interleaving = false;
    ReedSolomonCorrection rs(config);

    std::vector<uint8_t> data = {0, 1, 2, 3, 4, 5, 6, 7};
    
    auto encoded = rs.encode(data);
    
    // Corrupt a burst of consecutive bytes
    for (size_t i = 100; i < 110; i++) {
        encoded[i] ^= 0xFF;
    }
    
    auto decoded = rs.decode(encoded);
    EXPECT_FALSE(decoded.has_value());
}

} // namespace
} // namespace core
} // namespace xenocomm 