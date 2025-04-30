#include "xenocomm/core/data_adapters.h"
#include <algorithm>
#include <cstring>

namespace xenocomm {
namespace core {

// VectorFloat32Adapter implementation
std::vector<uint8_t> VectorFloat32Adapter::encode(const void* data, size_t size, DataFormat format) {
    validateInput(data, size);
    
    if (format != DataFormat::VECTOR_FLOAT32) {
        throw TranscodingError("VectorFloat32Adapter only supports VECTOR_FLOAT32 format");
    }

    vector_size_ = size / sizeof(float);
    std::vector<uint8_t> encoded_data(size);
    std::memcpy(encoded_data.data(), data, size);
    return encoded_data;
}

std::vector<uint8_t> VectorFloat32Adapter::decode(const std::vector<uint8_t>& encoded_data, DataFormat source_format) {
    if (source_format != DataFormat::VECTOR_FLOAT32) {
        throw TranscodingError("VectorFloat32Adapter only supports VECTOR_FLOAT32 format");
    }

    return encoded_data;
}

bool VectorFloat32Adapter::isValidFormat(const void* data, size_t size, DataFormat format) const {
    return format == DataFormat::VECTOR_FLOAT32 && data != nullptr && size % sizeof(float) == 0;
}

TranscodingMetadata VectorFloat32Adapter::getMetadata(const std::vector<uint8_t>& encoded_data) const {
    TranscodingMetadata metadata;
    metadata.format = DataFormat::VECTOR_FLOAT32;
    metadata.element_count = encoded_data.size() / sizeof(float);
    metadata.element_size = sizeof(float);
    return metadata;
}

// VectorInt8Adapter implementation
std::vector<uint8_t> VectorInt8Adapter::encode(const void* data, size_t size, DataFormat format) {
    validateInput(data, size);
    
    if (format != DataFormat::VECTOR_FLOAT32) {
        throw TranscodingError("VectorInt8Adapter requires VECTOR_FLOAT32 input format");
    }

    const float* float_data = static_cast<const float*>(data);
    size_t num_elements = size / sizeof(float);
    std::vector<uint8_t> encoded_data(num_elements);

    // Quantize each float to int8 using the scale factor
    for (size_t i = 0; i < num_elements; ++i) {
        float scaled = float_data[i] * scale_factor_;
        encoded_data[i] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, scaled)));
    }

    return encoded_data;
}

std::vector<uint8_t> VectorInt8Adapter::decode(const std::vector<uint8_t>& encoded_data, DataFormat source_format) {
    if (source_format != DataFormat::VECTOR_INT8) {
        throw TranscodingError("VectorInt8Adapter only supports decoding from VECTOR_INT8 format");
    }

    std::vector<uint8_t> decoded_data(encoded_data.size() * sizeof(float));
    float* float_data = reinterpret_cast<float*>(decoded_data.data());

    // Dequantize each int8 back to float
    for (size_t i = 0; i < encoded_data.size(); ++i) {
        float_data[i] = static_cast<float>(encoded_data[i]) / scale_factor_;
    }

    return decoded_data;
}

bool VectorInt8Adapter::isValidFormat(const void* data, size_t size, DataFormat format) const {
    if (format == DataFormat::VECTOR_FLOAT32) {
        return data != nullptr && size % sizeof(float) == 0;
    } else if (format == DataFormat::VECTOR_INT8) {
        return data != nullptr && size > 0;
    }
    return false;
}

TranscodingMetadata VectorInt8Adapter::getMetadata(const std::vector<uint8_t>& encoded_data) const {
    TranscodingMetadata metadata;
    metadata.format = DataFormat::VECTOR_INT8;
    metadata.element_count = encoded_data.size();
    metadata.element_size = sizeof(uint8_t);
    return metadata;
}

} // namespace core
} // namespace xenocomm 