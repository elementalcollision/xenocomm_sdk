#include <gtest/gtest.h>
#include "xenocomm/core/data_transcoder.h"

using namespace xenocomm::core;

// Mock implementation of DataTranscoder for testing
class MockDataTranscoder : public DataTranscoder {
public:
    std::vector<uint8_t> encode(const void* data, size_t size, DataFormat format) override {
        validateInput(data, size);
        // Simple mock implementation that just copies the data
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        return std::vector<uint8_t>(bytes, bytes + size);
    }

    std::vector<uint8_t> decode(const std::vector<uint8_t>& encoded_data, DataFormat source_format) override {
        // Simple mock implementation that returns the data as-is
        return encoded_data;
    }

    bool isValidFormat(const void* data, size_t size, DataFormat format) const override {
        return data != nullptr && size > 0;
    }

    TranscodingMetadata getMetadata(const std::vector<uint8_t>& encoded_data) const override {
        return TranscodingMetadata();
    }
};

class DataTranscoderTest : public ::testing::Test {
protected:
    MockDataTranscoder transcoder;
};

TEST_F(DataTranscoderTest, ValidateInputThrowsOnNullData) {
    EXPECT_THROW(transcoder.encode(nullptr, 10, DataFormat::VECTOR_FLOAT32), TranscodingError);
}

TEST_F(DataTranscoderTest, ValidateInputThrowsOnZeroSize) {
    uint8_t data = 0;
    EXPECT_THROW(transcoder.encode(&data, 0, DataFormat::VECTOR_FLOAT32), TranscodingError);
}

TEST_F(DataTranscoderTest, EncodeValidDataSucceeds) {
    std::vector<uint8_t> test_data = {1, 2, 3, 4};
    auto result = transcoder.encode(test_data.data(), test_data.size(), DataFormat::VECTOR_INT8);
    EXPECT_EQ(result, test_data);
}

TEST_F(DataTranscoderTest, DecodeValidDataSucceeds) {
    std::vector<uint8_t> test_data = {1, 2, 3, 4};
    auto result = transcoder.decode(test_data, DataFormat::VECTOR_INT8);
    EXPECT_EQ(result, test_data);
}

TEST_F(DataTranscoderTest, IsValidFormatChecksNullAndSize) {
    std::vector<uint8_t> test_data = {1, 2, 3, 4};
    EXPECT_TRUE(transcoder.isValidFormat(test_data.data(), test_data.size(), DataFormat::VECTOR_INT8));
    EXPECT_FALSE(transcoder.isValidFormat(nullptr, test_data.size(), DataFormat::VECTOR_INT8));
    EXPECT_FALSE(transcoder.isValidFormat(test_data.data(), 0, DataFormat::VECTOR_INT8));
}

TEST_F(DataTranscoderTest, GetMetadataReturnsDefaultValues) {
    std::vector<uint8_t> test_data = {1, 2, 3, 4};
    auto metadata = transcoder.getMetadata(test_data);
    EXPECT_EQ(metadata.format, DataFormat::VECTOR_FLOAT32);
    EXPECT_EQ(metadata.scale_factor, 1.0f);
    EXPECT_EQ(metadata.version, 1);
    EXPECT_TRUE(metadata.dimensions.empty());
    EXPECT_TRUE(metadata.compression_algorithm.empty());
} 