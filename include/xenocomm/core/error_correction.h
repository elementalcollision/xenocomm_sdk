#pragma once

#include "xenocomm/core/transmission_manager.h"
#include <vector>
#include <cstdint>
#include <optional>
#include <memory>
#include <string>

namespace xenocomm {
namespace core {

/**
 * @brief Interface for error correction algorithms.
 * 
 * This abstract class defines the interface that all error correction
 * implementations must follow. It provides methods for encoding and decoding
 * data with error detection/correction capabilities.
 */
class IErrorCorrection {
public:
    virtual ~IErrorCorrection() = default;

    /**
     * @brief Encodes data with error detection/correction information.
     * 
     * @param data The raw data to encode
     * @return std::vector<uint8_t> The encoded data with error correction information
     */
    virtual std::vector<uint8_t> encode(const std::vector<uint8_t>& data) = 0;

    /**
     * @brief Decodes data and attempts to correct any errors.
     * 
     * @param data The encoded data to decode
     * @return std::optional<std::vector<uint8_t>> The decoded data if successful, nullopt if uncorrectable
     */
    virtual std::optional<std::vector<uint8_t>> decode(const std::vector<uint8_t>& data) = 0;

    /**
     * @brief Indicates whether this algorithm can correct errors or only detect them.
     * 
     * @return true if the algorithm can correct errors
     * @return false if the algorithm can only detect errors
     */
    virtual bool canCorrect() const = 0;

    /**
     * @brief Returns the maximum number of errors that can be corrected.
     * 
     * @return int The maximum number of correctable errors, 0 if only detection is supported
     */
    virtual int maxCorrectableErrors() const = 0;

    /**
     * @brief Gets the name of the error correction algorithm.
     * 
     * @return std::string The algorithm name
     */
    virtual std::string name() const = 0;
};

/**
 * @brief CRC32 implementation for error detection.
 * 
 * Uses the IEEE 802.3 polynomial (0xEDB88320) for CRC32 calculation.
 * This implementation supports both bit-by-bit and table-driven approaches.
 */
class CRC32ErrorDetection : public IErrorCorrection {
public:
    CRC32ErrorDetection();
    ~CRC32ErrorDetection() override = default;

    std::vector<uint8_t> encode(const std::vector<uint8_t>& data) override;
    std::optional<std::vector<uint8_t>> decode(const std::vector<uint8_t>& data) override;
    bool canCorrect() const override { return false; }
    int maxCorrectableErrors() const override { return 0; }
    std::string name() const override { return "CRC32"; }

private:
    static constexpr uint32_t CRC32_POLYNOMIAL = 0xEDB88320;
    static constexpr size_t CRC_SIZE = 4;
    
    uint32_t computeCRC32(const uint8_t* data, size_t length) const;
    bool verifyCRC32(const uint8_t* data, size_t length) const;
    
    std::vector<uint32_t> crc_table_;
    void initCRCTable();
};

/**
 * @brief Reed-Solomon error correction implementation.
 * 
 * Implements Reed-Solomon error correction using GF(2^8) with configurable
 * parameters for error correction capability.
 */
class ReedSolomonCorrection : public IErrorCorrection {
public:
    /**
     * @brief Configuration for Reed-Solomon error correction.
     */
    struct Config {
        uint8_t data_shards = 223;     ///< Number of data shards (k)
        uint8_t parity_shards = 32;    ///< Number of parity shards (n-k)
        bool enable_interleaving = true; ///< Whether to use interleaving for burst error protection
    };

    /**
     * @brief Constructs a Reed-Solomon error correction instance.
     * 
     * @param config Configuration parameters for the RS algorithm
     */
    explicit ReedSolomonCorrection(const Config& config = Config{});
    ~ReedSolomonCorrection() override = default;

    std::vector<uint8_t> encode(const std::vector<uint8_t>& data) override;
    std::optional<std::vector<uint8_t>> decode(const std::vector<uint8_t>& data) override;
    bool canCorrect() const override { return true; }
    int maxCorrectableErrors() const override;
    std::string name() const override { return "Reed-Solomon"; }

    /**
     * @brief Reconfigures the Reed-Solomon parameters.
     * 
     * @param config New configuration to use
     */
    void configure(const Config& config);

    /**
     * @brief Gets the current configuration.
     * 
     * @return const Config& The current configuration
     */
    const Config& get_config() const { return config_; }

private:
    Config config_;
    // Additional implementation details will be added in the cpp file
};

/**
 * @brief Factory for creating error correction instances.
 */
class ErrorCorrectionFactory {
public:
    /**
     * @brief Creates an error correction instance based on the specified mode.
     * 
     * @param mode The error correction mode to use
     * @return std::unique_ptr<IErrorCorrection> The created error correction instance
     */
    static std::unique_ptr<IErrorCorrection> create(TransmissionManager::ErrorCorrectionMode mode);
};

} // namespace core
} // namespace xenocomm 