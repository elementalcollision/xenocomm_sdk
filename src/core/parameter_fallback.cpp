#include "xenocomm/core/parameter_fallback.h"
#include <algorithm>
#include <cassert>

namespace xenocomm {
namespace core {

ParameterFallbackHandler::ParameterFallbackHandler(
    const NegotiationPreferences& prefs,
    const FallbackConfig& config)
    : preferences_(prefs)
    , config_(config) {
}

std::optional<NegotiableParams> ParameterFallbackHandler::handleRejection(
    const NegotiableParams& rejected,
    std::size_t attempt) const {
    
    // Check if we've exceeded max attempts
    if (attempt >= config_.maxFallbackAttempts) {
        return std::nullopt;
    }

    // Try fallbacks in order of least impactful to most impactful
    
    // 1. Try error correction fallback first (least impact on data quality)
    if (config_.allowErrorCorrectionDowngrade) {
        if (auto params = tryErrorCorrectionFallback(rejected)) {
            return params;
        }
    }

    // 2. Try compression fallback next
    if (config_.allowCompressionDowngrade) {
        if (auto params = tryCompressionFallback(rejected)) {
            return params;
        }
    }

    // 3. Try data format fallback last (most impact on data quality)
    if (config_.allowFormatDowngrade) {
        if (auto params = tryDataFormatFallback(rejected)) {
            return params;
        }
    }

    // No fallback options available
    return std::nullopt;
}

bool ParameterFallbackHandler::areParametersAcceptable(
    const NegotiableParams& params) const {
    
    // Check protocol version compatibility
    if (params.protocolVersion < preferences_.minProtocolVersion) {
        return false;
    }

    // Check if each parameter is acceptable according to preferences
    return preferences_.dataFormat.isAcceptable(params.dataFormat) &&
           preferences_.compression.isAcceptable(params.compressionAlgorithm) &&
           preferences_.errorCorrection.isAcceptable(params.errorCorrection);
}

std::optional<NegotiableParams> ParameterFallbackHandler::tryDataFormatFallback(
    const NegotiableParams& current) const {
    
    // Try to get next fallback format
    auto nextFormat = preferences_.dataFormat.getNextFallback(current.dataFormat);
    if (!nextFormat) {
        return std::nullopt;
    }

    // Create new parameters with fallback format
    NegotiableParams fallback = current;
    fallback.dataFormat = *nextFormat;

    // Ensure the new format is compatible with other parameters
    // For example, some compression algorithms might not work with certain formats
    if (isFormatCompatibleWithCompression(fallback.dataFormat, fallback.compressionAlgorithm) &&
        isFormatCompatibleWithErrorCorrection(fallback.dataFormat, fallback.errorCorrection)) {
        return fallback;
    }

    return std::nullopt;
}

std::optional<NegotiableParams> ParameterFallbackHandler::tryCompressionFallback(
    const NegotiableParams& current) const {
    
    // Try to get next fallback compression algorithm
    auto nextCompression = preferences_.compression.getNextFallback(current.compressionAlgorithm);
    if (!nextCompression) {
        return std::nullopt;
    }

    // Create new parameters with fallback compression
    NegotiableParams fallback = current;
    fallback.compressionAlgorithm = *nextCompression;

    // Check compatibility with current format
    if (isFormatCompatibleWithCompression(fallback.dataFormat, fallback.compressionAlgorithm)) {
        return fallback;
    }

    return std::nullopt;
}

std::optional<NegotiableParams> ParameterFallbackHandler::tryErrorCorrectionFallback(
    const NegotiableParams& current) const {
    
    // Try to get next fallback error correction scheme
    auto nextScheme = preferences_.errorCorrection.getNextFallback(current.errorCorrection);
    if (!nextScheme) {
        return std::nullopt;
    }

    // Create new parameters with fallback error correction
    NegotiableParams fallback = current;
    fallback.errorCorrection = *nextScheme;

    // Check compatibility with current format
    if (isFormatCompatibleWithErrorCorrection(fallback.dataFormat, fallback.errorCorrection)) {
        return fallback;
    }

    return std::nullopt;
}

// Helper functions for compatibility checking
namespace {

bool isFormatCompatibleWithCompression(DataFormat format, CompressionAlgorithm compression) {
    // Some formats might already be compressed or not suitable for compression
    if (format == DataFormat::COMPRESSED_STATE) {
        return compression == CompressionAlgorithm::NONE;
    }
    
    // All other formats are compatible with any compression algorithm
    return true;
}

bool isFormatCompatibleWithErrorCorrection(DataFormat format, ErrorCorrectionScheme scheme) {
    // Some formats might have built-in error correction
    if (format == DataFormat::GGWAVE_FSK) {
        // GGWAVE_FSK has built-in error correction, so additional schemes might conflict
        return scheme == ErrorCorrectionScheme::NONE ||
               scheme == ErrorCorrectionScheme::CHECKSUM_ONLY;
    }
    
    // All other formats are compatible with any error correction scheme
    return true;
}

} // anonymous namespace

std::optional<NegotiableParams> NegotiationPreferences::generateFallback(
    const NegotiableParams& current,
    std::size_t attempt) const {
    
    // Create a fallback handler with default config
    ParameterFallbackHandler handler(*this);
    
    // Use the handler to generate fallback parameters
    return handler.handleRejection(current, attempt);
}

} // namespace core
} // namespace xenocomm 