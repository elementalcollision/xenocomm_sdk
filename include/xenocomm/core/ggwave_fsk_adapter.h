#pragma once

#include "xenocomm/core/data_transcoder.h"
#include <memory>
#include <string>
#include <vector>

namespace xenocomm {
namespace core {

/**
 * @brief Configuration for GGWAVE FSK encoding
 */
struct GgwaveFskConfig {
    float sample_rate;         ///< Audio sample rate in Hz
    float base_frequency;      ///< Base frequency for FSK in Hz
    float frequency_spacing;   ///< Frequency spacing between symbols in Hz
    size_t samples_per_symbol; ///< Number of samples per FSK symbol
    float amplitude;           ///< Signal amplitude (0.0 to 1.0)

    GgwaveFskConfig()
        : sample_rate(44100.0f)
        , base_frequency(1000.0f)
        , frequency_spacing(100.0f)
        , samples_per_symbol(256)
        , amplitude(0.5f) {}
};

/**
 * @brief Audio-based FSK encoding adapter using GGWAVE protocol
 * 
 * This adapter implements Frequency-Shift Keying (FSK) encoding for data
 * transmission over audio channels using the GGWAVE protocol.
 */
class GgwaveFskAdapter : public DataTranscoder {
public:
    explicit GgwaveFskAdapter(const GgwaveFskConfig& config = GgwaveFskConfig());
    ~GgwaveFskAdapter() override = default;

    /**
     * @brief Encode data using FSK modulation
     * 
     * @param data Raw input data as bytes
     * @param size Size of input data in bytes
     * @param format Target format for encoding (must be GGWAVE_FSK)
     * @return std::vector<uint8_t> FSK modulated audio data
     * @throws TranscodingError if encoding fails or format is invalid
     */
    std::vector<uint8_t> encode(
        const void* data,
        size_t size,
        DataFormat format) override;

    /**
     * @brief Decode FSK modulated audio data
     * 
     * @param encoded_data FSK modulated audio data
     * @param source_format Format of the encoded data (must be GGWAVE_FSK)
     * @return std::vector<uint8_t> Decoded data
     * @throws TranscodingError if decoding fails or format is invalid
     */
    std::vector<uint8_t> decode(
        const std::vector<uint8_t>& encoded_data,
        DataFormat source_format) override;

    /**
     * @brief Validate if data matches FSK format requirements
     * 
     * @param data Data to validate
     * @param size Size of data in bytes
     * @param format Format to validate against (must be GGWAVE_FSK)
     * @return true if data is valid for the format
     * @return false if data is invalid for the format
     */
    bool isValidFormat(
        const void* data,
        size_t size,
        DataFormat format) const override;

    /**
     * @brief Get metadata from FSK encoded data
     * 
     * @param encoded_data FSK encoded data to extract metadata from
     * @return TranscodingMetadata Metadata including FSK parameters
     * @throws TranscodingError if metadata extraction fails
     */
    TranscodingMetadata getMetadata(
        const std::vector<uint8_t>& encoded_data) const override;

    /**
     * @brief Update FSK configuration
     * 
     * @param config New FSK configuration to use
     */
    void setConfig(const GgwaveFskConfig& config);

    /**
     * @brief Get current FSK configuration
     * 
     * @return const GgwaveFskConfig& Current configuration
     */
    const GgwaveFskConfig& getConfig() const;

private:
    GgwaveFskConfig config_;

    struct FskHeader {
        uint32_t magic;
        uint32_t data_size;
        float sample_rate;
        float base_freq;
        float freq_spacing;
        uint32_t samples_per_symbol;
    };

    static constexpr uint32_t MAGIC_NUMBER = 0xF5CA4D2E;  // "FSK" magic

    /**
     * @brief Generate FSK symbol frequencies
     * 
     * @param symbol Symbol value to encode
     * @return float Frequency for the symbol
     */
    float getSymbolFrequency(uint8_t symbol) const;

    /**
     * @brief Generate audio samples for a symbol
     * 
     * @param symbol Symbol to generate samples for
     * @param samples Output vector for samples
     */
    void generateSymbolSamples(uint8_t symbol, std::vector<float>& samples) const;

    /**
     * @brief Detect symbol from audio samples
     * 
     * @param samples Audio samples to analyze
     * @param offset Start offset in samples
     * @return uint8_t Detected symbol value
     */
    uint8_t detectSymbol(const std::vector<float>& samples, size_t offset) const;

    /**
     * @brief Validate FSK header
     * 
     * @param header Header to validate
     * @param data_size Expected data size
     * @throws TranscodingError if header is invalid
     */
    void validateHeader(const FskHeader& header, size_t data_size) const;
};

} // namespace core
} // namespace xenocomm 