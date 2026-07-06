#include "xenocomm/core/ggwave_fsk_adapter.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace xenocomm {
namespace core {

namespace {
constexpr float PI = 3.14159265358979323846f;
} // namespace

GgwaveFskAdapter::GgwaveFskAdapter(const GgwaveFskConfig& config)
    : config_(config) {}

std::vector<uint8_t> GgwaveFskAdapter::encode(
    const void* data,
    size_t size,
    DataFormat format) {
    // Validate input parameters
    validateInput(data, size);
    if (format != DataFormat::GGWAVE_FSK) {
        throw TranscodingError("Invalid format for GgwaveFskAdapter::encode");
    }

    // Create header
    FskHeader header{
        MAGIC_NUMBER,
        static_cast<uint32_t>(size),
        config_.sample_rate,
        config_.base_frequency,
        config_.frequency_spacing,
        static_cast<uint32_t>(config_.samples_per_symbol)
    };

    // Calculate total samples needed
    size_t total_samples = config_.samples_per_symbol * (size + sizeof(FskHeader));
    std::vector<float> audio_samples;
    audio_samples.reserve(total_samples);

    // Encode header
    const uint8_t* header_bytes = reinterpret_cast<const uint8_t*>(&header);
    for (size_t i = 0; i < sizeof(FskHeader); ++i) {
        generateSymbolSamples(header_bytes[i], audio_samples);
    }

    // Encode data
    const uint8_t* data_bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        generateSymbolSamples(data_bytes[i], audio_samples);
    }

    // Convert float samples to uint8_t (normalized to 0-255)
    std::vector<uint8_t> encoded;
    encoded.reserve(audio_samples.size());
    std::transform(audio_samples.begin(), audio_samples.end(), 
        std::back_inserter(encoded),
        [](float sample) {
            return static_cast<uint8_t>((sample + 1.0f) * 127.5f);
        });

    return encoded;
}

std::vector<uint8_t> GgwaveFskAdapter::decode(
    const std::vector<uint8_t>& encoded_data,
    DataFormat source_format) {
    if (source_format != DataFormat::GGWAVE_FSK) {
        throw TranscodingError("Invalid format for GgwaveFskAdapter::decode");
    }

    // Convert uint8_t samples back to float (-1.0 to 1.0)
    std::vector<float> audio_samples;
    audio_samples.reserve(encoded_data.size());
    std::transform(encoded_data.begin(), encoded_data.end(),
        std::back_inserter(audio_samples),
        [](uint8_t sample) {
            return (static_cast<float>(sample) / 127.5f) - 1.0f;
        });

    // Need enough samples for at least a header
    if (audio_samples.size() < config_.samples_per_symbol * sizeof(FskHeader)) {
        throw TranscodingError("Not enough samples to decode header");
    }

    // Decode header
    std::vector<uint8_t> header_bytes(sizeof(FskHeader));
    for (size_t i = 0; i < sizeof(FskHeader); ++i) {
        header_bytes[i] = detectSymbol(audio_samples, i * config_.samples_per_symbol);
    }
    const FskHeader* header = reinterpret_cast<const FskHeader*>(header_bytes.data());

    // Validate header
    validateHeader(*header, (audio_samples.size() / config_.samples_per_symbol) - sizeof(FskHeader));

    // Update config from header if needed
    if (header->sample_rate != config_.sample_rate ||
        header->base_freq != config_.base_frequency ||
        header->freq_spacing != config_.frequency_spacing ||
        header->samples_per_symbol != config_.samples_per_symbol) {
        GgwaveFskConfig new_config = config_;
        new_config.sample_rate = header->sample_rate;
        new_config.base_frequency = header->base_freq;
        new_config.frequency_spacing = header->freq_spacing;
        new_config.samples_per_symbol = header->samples_per_symbol;
        setConfig(new_config);
    }

    // Decode data
    std::vector<uint8_t> decoded;
    decoded.reserve(header->data_size);
    size_t data_start = sizeof(FskHeader) * config_.samples_per_symbol;
    
    for (size_t i = 0; i < header->data_size; ++i) {
        decoded.push_back(detectSymbol(audio_samples, data_start + i * config_.samples_per_symbol));
    }

    return decoded;
}

bool GgwaveFskAdapter::isValidFormat(
    const void* data,
    size_t size,
    DataFormat format) const {
    if (format != DataFormat::GGWAVE_FSK || !data || 
        size < config_.samples_per_symbol * sizeof(FskHeader)) {
        return false;
    }

    try {
        // Convert first chunk of samples to get header
        const uint8_t* samples = static_cast<const uint8_t*>(data);
        std::vector<float> audio_samples;
        audio_samples.reserve(config_.samples_per_symbol * sizeof(FskHeader));
        
        for (size_t i = 0; i < config_.samples_per_symbol * sizeof(FskHeader); ++i) {
            audio_samples.push_back((static_cast<float>(samples[i]) / 127.5f) - 1.0f);
        }

        // Decode and validate header
        std::vector<uint8_t> header_bytes(sizeof(FskHeader));
        for (size_t i = 0; i < sizeof(FskHeader); ++i) {
            header_bytes[i] = detectSymbol(audio_samples, i * config_.samples_per_symbol);
        }
        const FskHeader* header = reinterpret_cast<const FskHeader*>(header_bytes.data());
        
        validateHeader(*header, (size / config_.samples_per_symbol) - sizeof(FskHeader));
        return true;
    } catch (const TranscodingError&) {
        return false;
    }
}

TranscodingMetadata GgwaveFskAdapter::getMetadata(
    const std::vector<uint8_t>& encoded_data) const {
    if (encoded_data.size() < config_.samples_per_symbol * sizeof(FskHeader)) {
        throw TranscodingError("Not enough samples to extract metadata");
    }

    // Convert and decode header
    std::vector<float> audio_samples;
    audio_samples.reserve(config_.samples_per_symbol * sizeof(FskHeader));
    
    for (size_t i = 0; i < config_.samples_per_symbol * sizeof(FskHeader); ++i) {
        audio_samples.push_back((static_cast<float>(encoded_data[i]) / 127.5f) - 1.0f);
    }

    std::vector<uint8_t> header_bytes(sizeof(FskHeader));
    for (size_t i = 0; i < sizeof(FskHeader); ++i) {
        header_bytes[i] = detectSymbol(audio_samples, i * config_.samples_per_symbol);
    }
    const FskHeader* header = reinterpret_cast<const FskHeader*>(header_bytes.data());

    if (header->magic != MAGIC_NUMBER) {
        throw TranscodingError("Invalid magic number in FSK header");
    }

    TranscodingMetadata metadata;
    metadata.format = DataFormat::GGWAVE_FSK;
    metadata.element_count = header->data_size;
    metadata.element_size = 1;  // Raw bytes
    metadata.dimensions = {
        static_cast<size_t>(header->sample_rate),
        static_cast<size_t>(header->samples_per_symbol)
    };

    return metadata;
}

void GgwaveFskAdapter::setConfig(const GgwaveFskConfig& config) {
    config_ = config;
}

const GgwaveFskConfig& GgwaveFskAdapter::getConfig() const {
    return config_;
}

float GgwaveFskAdapter::getSymbolFrequency(uint8_t symbol) const {
    return config_.base_frequency + (symbol * config_.frequency_spacing);
}

void GgwaveFskAdapter::generateSymbolSamples(
    uint8_t symbol,
    std::vector<float>& samples) const {
    float frequency = getSymbolFrequency(symbol);
    float angular_freq = 2.0f * PI * frequency;
    
    for (size_t i = 0; i < config_.samples_per_symbol; ++i) {
        float t = static_cast<float>(i) / config_.sample_rate;
        samples.push_back(config_.amplitude * std::sin(angular_freq * t));
    }
}

uint8_t GgwaveFskAdapter::detectSymbol(
    const std::vector<float>& samples,
    size_t offset) const {
    // Simple frequency detection using Goertzel algorithm
    float max_magnitude = 0.0f;
    uint8_t detected_symbol = 0;

    for (uint8_t symbol = 0; symbol < 256; ++symbol) {
        float frequency = getSymbolFrequency(symbol);
        float omega = 2.0f * PI * frequency / config_.sample_rate;
        float cos_omega = std::cos(omega);
        float sin_omega = std::sin(omega);

        float q0 = 0.0f, q1 = 0.0f, q2 = 0.0f;
        
        // Process samples
        for (size_t i = 0; i < config_.samples_per_symbol && (offset + i) < samples.size(); ++i) {
            q0 = 2.0f * cos_omega * q1 - q2 + samples[offset + i];
            q2 = q1;
            q1 = q0;
        }

        // Calculate magnitude
        float magnitude = std::sqrt(q1 * q1 + q2 * q2 - q1 * q2 * cos_omega);
        
        if (magnitude > max_magnitude) {
            max_magnitude = magnitude;
            detected_symbol = symbol;
        }
    }

    return detected_symbol;
}

void GgwaveFskAdapter::validateHeader(
    const FskHeader& header,
    size_t data_size) const {
    if (header.magic != MAGIC_NUMBER) {
        throw TranscodingError("Invalid magic number in FSK header");
    }

    if (header.data_size != data_size) {
        throw TranscodingError("Data size mismatch in FSK header");
    }

    if (header.sample_rate <= 0.0f || header.base_freq <= 0.0f || 
        header.freq_spacing <= 0.0f || header.samples_per_symbol == 0) {
        throw TranscodingError("Invalid FSK parameters in header");
    }
}

} // namespace core
} // namespace xenocomm 