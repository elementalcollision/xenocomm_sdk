#pragma once

#include "xenocomm/core/data_transcoder.h"
#include <cmath>
#include <limits>

namespace xenocomm {
namespace core {

/**
 * @brief Adapter class for handling 32-bit floating point vector data
 */
class VectorFloat32Adapter : public DataTranscoder {
public:
    std::vector<uint8_t> encode(const void* data, size_t size, DataFormat format) override;
    std::vector<uint8_t> decode(const std::vector<uint8_t>& encoded_data, DataFormat source_format) override;
    bool isValidFormat(const void* data, size_t size, DataFormat format) const override;
    TranscodingMetadata getMetadata(const std::vector<uint8_t>& encoded_data) const override;

private:
    size_t vector_size_ = 0;  // Number of float32 elements
};

/**
 * @brief Adapter class for handling quantized 8-bit integer vector data
 */
class VectorInt8Adapter : public DataTranscoder {
public:
    VectorInt8Adapter(float scale = 1.0f) : scale_factor_(scale) {}

    std::vector<uint8_t> encode(const void* data, size_t size, DataFormat format) override;
    std::vector<uint8_t> decode(const std::vector<uint8_t>& encoded_data, DataFormat source_format) override;
    bool isValidFormat(const void* data, size_t size, DataFormat format) const override;
    TranscodingMetadata getMetadata(const std::vector<uint8_t>& encoded_data) const override;

private:
    float scale_factor_;  // Scaling factor for quantization
};

} // namespace core
} // namespace xenocomm 