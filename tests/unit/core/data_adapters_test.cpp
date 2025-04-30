#include <gtest/gtest.h>
#include "xenocomm/core/data_adapters.h"
#include <cmath>

using namespace xenocomm::core;

class VectorFloat32AdapterTest : public ::testing::Test {
protected:
    VectorFloat32Adapter adapter;
    std::vector<float> test_data = {1.0f, -2.5f, 3.14f, 0.0f, -1.0f};
};

class VectorInt8AdapterTest : public ::testing::Test {
protected:
    VectorInt8Adapter adapter{0.5f};  // Scale factor of 0.5
    std::vector<float> test_data = {1.0f, -2.5f, 3.14f, 0.0f, -1.0f};
};

// VectorFloat32Adapter Tests
TEST_F(VectorFloat32AdapterTest, EncodeValidData) {
    auto encoded = adapter.encode(test_data.data(), test_data.size() * sizeof(float), DataFormat::VECTOR_FLOAT32);
    EXPECT_EQ(encoded.size(), test_data.size() * sizeof(float));
    
    // Verify data was correctly copied
    const float* encoded_floats = reinterpret_cast<const float*>(encoded.data());
    for (size_t i = 0; i < test_data.size(); ++i) {
        EXPECT_FLOAT_EQ(encoded_floats[i], test_data[i]);
    }
}

TEST_F(VectorFloat32AdapterTest, DecodeValidData) {
    auto encoded = adapter.encode(test_data.data(), test_data.size() * sizeof(float), DataFormat::VECTOR_FLOAT32);
    auto decoded = adapter.decode(encoded, DataFormat::VECTOR_FLOAT32);
    
    EXPECT_EQ(decoded.size(), test_data.size() * sizeof(float));
    const float* decoded_floats = reinterpret_cast<const float*>(decoded.data());
    for (size_t i = 0; i < test_data.size(); ++i) {
        EXPECT_FLOAT_EQ(decoded_floats[i], test_data[i]);
    }
}

TEST_F(VectorFloat32AdapterTest, ValidateFormat) {
    EXPECT_TRUE(adapter.isValidFormat(test_data.data(), test_data.size() * sizeof(float), DataFormat::VECTOR_FLOAT32));
    EXPECT_FALSE(adapter.isValidFormat(test_data.data(), test_data.size() * sizeof(float) - 1, DataFormat::VECTOR_FLOAT32));
    EXPECT_FALSE(adapter.isValidFormat(nullptr, test_data.size() * sizeof(float), DataFormat::VECTOR_FLOAT32));
    EXPECT_FALSE(adapter.isValidFormat(test_data.data(), test_data.size() * sizeof(float), DataFormat::VECTOR_INT8));
}

TEST_F(VectorFloat32AdapterTest, GetMetadata) {
    auto encoded = adapter.encode(test_data.data(), test_data.size() * sizeof(float), DataFormat::VECTOR_FLOAT32);
    auto metadata = adapter.getMetadata(encoded);
    
    EXPECT_EQ(metadata.format, DataFormat::VECTOR_FLOAT32);
    EXPECT_EQ(metadata.element_count, test_data.size());
    EXPECT_EQ(metadata.element_size, sizeof(float));
    EXPECT_FLOAT_EQ(metadata.scale_factor, 1.0f);
}

// VectorInt8Adapter Tests
TEST_F(VectorInt8AdapterTest, EncodeValidData) {
    auto encoded = adapter.encode(test_data.data(), test_data.size() * sizeof(float), DataFormat::VECTOR_FLOAT32);
    EXPECT_EQ(encoded.size(), test_data.size());
    
    // Verify quantization
    for (size_t i = 0; i < test_data.size(); ++i) {
        float expected = std::max(0.0f, std::min(255.0f, test_data[i] * 0.5f));
        EXPECT_NEAR(static_cast<float>(encoded[i]), expected, 1.0f);
    }
}

TEST_F(VectorInt8AdapterTest, DecodeValidData) {
    auto encoded = adapter.encode(test_data.data(), test_data.size() * sizeof(float), DataFormat::VECTOR_FLOAT32);
    auto decoded = adapter.decode(encoded, DataFormat::VECTOR_INT8);
    
    EXPECT_EQ(decoded.size(), test_data.size() * sizeof(float));
    const float* decoded_floats = reinterpret_cast<const float*>(decoded.data());
    
    // Verify dequantization (with some tolerance due to quantization loss)
    for (size_t i = 0; i < test_data.size(); ++i) {
        float expected = std::max(0.0f, std::min(255.0f, test_data[i] * 0.5f)) * 2.0f;
        EXPECT_NEAR(decoded_floats[i], expected, 1.0f);
    }
}

TEST_F(VectorInt8AdapterTest, ValidateFormat) {
    EXPECT_TRUE(adapter.isValidFormat(test_data.data(), test_data.size() * sizeof(float), DataFormat::VECTOR_FLOAT32));
    EXPECT_TRUE(adapter.isValidFormat(test_data.data(), test_data.size(), DataFormat::VECTOR_INT8));
    EXPECT_FALSE(adapter.isValidFormat(nullptr, test_data.size(), DataFormat::VECTOR_INT8));
    EXPECT_FALSE(adapter.isValidFormat(test_data.data(), 0, DataFormat::VECTOR_INT8));
}

TEST_F(VectorInt8AdapterTest, GetMetadata) {
    auto encoded = adapter.encode(test_data.data(), test_data.size() * sizeof(float), DataFormat::VECTOR_FLOAT32);
    auto metadata = adapter.getMetadata(encoded);
    
    EXPECT_EQ(metadata.format, DataFormat::VECTOR_INT8);
    EXPECT_EQ(metadata.element_count, test_data.size());
    EXPECT_EQ(metadata.element_size, sizeof(uint8_t));
    EXPECT_FLOAT_EQ(metadata.scale_factor, 0.5f);
} 