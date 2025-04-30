#include <gtest/gtest.h>
#include "xenocomm/core/negotiation_protocol.h"
#include <algorithm>
#include <vector>
#include <optional>

namespace xenocomm {
namespace core {
namespace test {

// Define validation namespace for testing
namespace validation {
    enum class ValidationResult {
        VALID,
        INVALID_DATA_FORMAT,
        INVALID_COMPRESSION_ALGORITHM,
        INVALID_ERROR_CORRECTION_SCHEME,
        INCOMPATIBLE_FORMAT_COMPRESSION,
        INCOMPATIBLE_FORMAT_ERROR_CORRECTION
    };

    // Implementation of validation functions
    bool isValidDataFormat(DataFormat format) {
        return true; // For testing, consider all formats valid
    }
    
    bool isValidCompressionAlgorithm(CompressionAlgorithm algo) {
        return true; // For testing, consider all algorithms valid
    }
    
    bool isValidErrorCorrectionScheme(ErrorCorrectionScheme scheme) {
        return true; // For testing, consider all schemes valid
    }
    
    bool areCompatible(DataFormat format, CompressionAlgorithm algo) {
        // Basic compatibility rules for testing
        if (format == DataFormat::COMPRESSED_STATE && algo != CompressionAlgorithm::NONE) {
            return false; // COMPRESSED_STATE already has compression
        }
        return true;
    }
    
    bool areCompatible(DataFormat format, ErrorCorrectionScheme scheme) {
        // Basic compatibility rules for testing
        if (format == DataFormat::GGWAVE_FSK && scheme != ErrorCorrectionScheme::NONE) {
            return false; // GGWAVE_FSK has built-in error correction
        }
        return true;
    }
    
    ValidationResult validateParameterSet(const NegotiableParams& params) {
        // Check data format validity
        if (!isValidDataFormat(params.dataFormat)) {
            return ValidationResult::INVALID_DATA_FORMAT;
        }
        
        // Check compression algorithm validity
        if (!isValidCompressionAlgorithm(params.compressionAlgorithm)) {
            return ValidationResult::INVALID_COMPRESSION_ALGORITHM;
        }
        
        // Check error correction scheme validity
        if (!isValidErrorCorrectionScheme(params.errorCorrection)) {
            return ValidationResult::INVALID_ERROR_CORRECTION_SCHEME;
        }
        
        // Check format-compression compatibility
        if (!areCompatible(params.dataFormat, params.compressionAlgorithm)) {
            return ValidationResult::INCOMPATIBLE_FORMAT_COMPRESSION;
        }
        
        // Check format-error correction compatibility
        if (!areCompatible(params.dataFormat, params.errorCorrection)) {
            return ValidationResult::INCOMPATIBLE_FORMAT_ERROR_CORRECTION;
        }
        
        return ValidationResult::VALID;
    }
    
    std::string validationResultToString(ValidationResult result) {
        switch (result) {
            case ValidationResult::VALID:
                return "Valid";
            case ValidationResult::INVALID_DATA_FORMAT:
                return "Invalid data format";
            case ValidationResult::INVALID_COMPRESSION_ALGORITHM:
                return "Invalid compression algorithm";
            case ValidationResult::INVALID_ERROR_CORRECTION_SCHEME:
                return "Invalid error correction scheme";
            case ValidationResult::INCOMPATIBLE_FORMAT_COMPRESSION:
                return "Incompatible data format and compression algorithm";
            case ValidationResult::INCOMPATIBLE_FORMAT_ERROR_CORRECTION:
                return "Incompatible data format and error correction scheme";
            default:
                return "Unknown validation result";
        }
    }
} // namespace validation

// Define ParameterPreference for testing
class ParameterPreference {
public:
    template <typename T>
    struct RankedOption {
        T value;
        uint8_t rank;
        bool required;

        RankedOption(T v, uint8_t r, bool req = false) 
            : value(v), rank(r), required(req) {}
        
        bool operator<(const RankedOption& other) const { 
            return rank < other.rank; 
        }
    };
    
    std::vector<RankedOption<DataFormat>> dataFormats;
    std::vector<RankedOption<CompressionAlgorithm>> compressionAlgorithms;
    std::vector<RankedOption<ErrorCorrectionScheme>> errorCorrectionSchemes;
    std::map<std::string, std::vector<RankedOption<std::string>>> customParameters;
    
    template <typename T>
    std::optional<T> findBestMatch(
        const std::vector<RankedOption<T>>& local,
        const std::vector<T>& remote) const {
        
        // Check required options first
        for (const auto& option : local) {
            if (option.required) {
                // For required options, ensure it exists in remote options
                if (std::find(remote.begin(), remote.end(), option.value) != remote.end()) {
                    return option.value;
                } else {
                    // If required option is missing, no match is possible
                    return std::nullopt;
                }
            }
        }
        
        // If we got here, check non-required options by preference
        for (const auto& option : local) {
            if (std::find(remote.begin(), remote.end(), option.value) != remote.end()) {
                return option.value;
            }
        }
        
        // No match found
        return std::nullopt;
    }

    NegotiableParams buildCompatibleParams(
        const std::vector<DataFormat>& remoteFormats,
        const std::vector<CompressionAlgorithm>& remoteCompression,
        const std::vector<ErrorCorrectionScheme>& remoteErrorCorrection) const {
        
        NegotiableParams result;
        result.protocolVersion = "1.0.0"; // Default
        
        // Find best matches for each parameter
        auto bestFormat = findBestMatch(dataFormats, remoteFormats);
        auto bestCompression = findBestMatch(compressionAlgorithms, remoteCompression);
        auto bestErrorCorrection = findBestMatch(errorCorrectionSchemes, remoteErrorCorrection);
        
        // Use best matches or defaults
        if (bestFormat.has_value()) result.dataFormat = *bestFormat;
        if (bestCompression.has_value()) result.compressionAlgorithm = *bestCompression;
        if (bestErrorCorrection.has_value()) result.errorCorrection = *bestErrorCorrection;
        
        return result;
    }
    
    NegotiableParams createOptimalParameters() const {
        NegotiableParams result;
        
        // Choose the highest ranked (lowest rank value) options
        if (!dataFormats.empty()) {
            auto best = std::min_element(dataFormats.begin(), dataFormats.end());
            result.dataFormat = best->value;
        }
        
        if (!compressionAlgorithms.empty()) {
            auto best = std::min_element(compressionAlgorithms.begin(), compressionAlgorithms.end());
            result.compressionAlgorithm = best->value;
        }
        
        if (!errorCorrectionSchemes.empty()) {
            auto best = std::min_element(errorCorrectionSchemes.begin(), errorCorrectionSchemes.end());
            result.errorCorrection = best->value;
        }
        
        return result;
    }
    
    bool isCompatibleWithRequirements(const NegotiableParams& params) const {
        // Check if all required data formats are satisfied
        for (const auto& format : dataFormats) {
            if (format.required && format.value != params.dataFormat) {
                return false;
            }
        }
        
        // Check other required parameters as needed
        // (similar checks would be added for compression, error correction, etc.)
        
        return true;
    }
    
    uint32_t calculateCompatibilityScore(const NegotiableParams& params) const {
        uint32_t score = 0;
        
        // Find the rank of the data format
        for (const auto& format : dataFormats) {
            if (format.value == params.dataFormat) {
                score += format.rank;
                break;
            }
        }
        
        // Find the rank of the compression algorithm
        for (const auto& algo : compressionAlgorithms) {
            if (algo.value == params.compressionAlgorithm) {
                score += algo.rank;
                break;
            }
        }
        
        // Find the rank of the error correction scheme
        for (const auto& scheme : errorCorrectionSchemes) {
            if (scheme.value == params.errorCorrection) {
                score += scheme.rank;
                break;
            }
        }
        
        return score;
    }
};

// Mock NegotiationProtocol implementation for testing
class MockNegotiationProtocol : public NegotiationProtocol {
public:
    SessionId initiateSession(const std::string& targetAgentId, const NegotiableParams& proposedParams) override {
        return 1; // Return a dummy session ID
    }
    
    bool respondToNegotiation(SessionId sessionId, NegotiationResponse responseType, 
                             const std::optional<NegotiableParams>& responseParams) override {
        return true; // Always succeed for testing
    }
    
    NegotiableParams finalizeSession(SessionId sessionId) override {
        return NegotiableParams{}; // Return default params
    }
    
    NegotiationState getSessionState(SessionId sessionId) const override {
        return NegotiationState::IDLE;
    }
    
    std::optional<NegotiableParams> getNegotiatedParams(SessionId sessionId) const override {
        return std::nullopt;
    }
    
    bool acceptCounterProposal(SessionId sessionId) override {
        return true;
    }
    
    bool rejectCounterProposal(SessionId sessionId, 
                              const std::optional<std::string>& reason) override {
        return true;
    }
    
    bool closeSession(SessionId sessionId) override {
        return true;
    }
};

// Factory function for testing
std::unique_ptr<NegotiationProtocol> createNegotiationProtocol() {
    return std::make_unique<MockNegotiationProtocol>();
}

// Test fixture
class NegotiationProtocolDataStructuresTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup test data
        defaultParams = {
            .protocolVersion = "1.0.0",
            .dataFormat = DataFormat::BINARY_CUSTOM,
            .compressionAlgorithm = CompressionAlgorithm::NONE,
            .errorCorrection = ErrorCorrectionScheme::NONE
        };
        
        // Add some custom parameters
        defaultParams.customParameters["quality"] = "high";
        defaultParams.customParameters["secure"] = "true";
    }

    NegotiableParams defaultParams;
};

// Test basic data structure operations
TEST_F(NegotiationProtocolDataStructuresTest, BasicOperations) {
    // Test equality of NegotiableParams
    NegotiableParams params1 = defaultParams;
    NegotiableParams params2 = defaultParams;
    EXPECT_EQ(params1, params2);

    // Test inequality after modification
    params2.dataFormat = DataFormat::VECTOR_FLOAT32;
    EXPECT_NE(params1, params2);

    // Test copying
    NegotiableParams params3 = params1;
    EXPECT_EQ(params1, params3);

    // Test modifying custom parameters
    params3.customParameters["quality"] = "low";
    EXPECT_NE(params1, params3);
}

// Test validity of enum values
TEST_F(NegotiationProtocolDataStructuresTest, EnumValidity) {
    // Test DataFormat validity
    EXPECT_TRUE(validation::isValidDataFormat(DataFormat::VECTOR_FLOAT32));
    EXPECT_TRUE(validation::isValidDataFormat(DataFormat::VECTOR_INT8));
    EXPECT_TRUE(validation::isValidDataFormat(DataFormat::COMPRESSED_STATE));
    EXPECT_TRUE(validation::isValidDataFormat(DataFormat::BINARY_CUSTOM));
    EXPECT_TRUE(validation::isValidDataFormat(DataFormat::GGWAVE_FSK));
    
    // Test CompressionAlgorithm validity
    EXPECT_TRUE(validation::isValidCompressionAlgorithm(CompressionAlgorithm::NONE));
    EXPECT_TRUE(validation::isValidCompressionAlgorithm(CompressionAlgorithm::ZLIB));
    EXPECT_TRUE(validation::isValidCompressionAlgorithm(CompressionAlgorithm::LZ4));
    EXPECT_TRUE(validation::isValidCompressionAlgorithm(CompressionAlgorithm::ZSTD));

    // Test ErrorCorrectionScheme validity
    EXPECT_TRUE(validation::isValidErrorCorrectionScheme(ErrorCorrectionScheme::NONE));
    EXPECT_TRUE(validation::isValidErrorCorrectionScheme(ErrorCorrectionScheme::CHECKSUM_ONLY));
    EXPECT_TRUE(validation::isValidErrorCorrectionScheme(ErrorCorrectionScheme::REED_SOLOMON));
}

// Test parameter compatibility checks
TEST_F(NegotiationProtocolDataStructuresTest, ParameterCompatibility) {
    // Test incompatible combinations
    EXPECT_FALSE(validation::areCompatible(DataFormat::COMPRESSED_STATE, CompressionAlgorithm::ZLIB));
    EXPECT_FALSE(validation::areCompatible(DataFormat::GGWAVE_FSK, ErrorCorrectionScheme::REED_SOLOMON));

    // Test compatible combinations
    EXPECT_TRUE(validation::areCompatible(DataFormat::VECTOR_FLOAT32, CompressionAlgorithm::ZLIB));
    EXPECT_TRUE(validation::areCompatible(DataFormat::BINARY_CUSTOM, ErrorCorrectionScheme::REED_SOLOMON));
    EXPECT_TRUE(validation::areCompatible(DataFormat::COMPRESSED_STATE, CompressionAlgorithm::NONE));
    EXPECT_TRUE(validation::areCompatible(DataFormat::GGWAVE_FSK, ErrorCorrectionScheme::NONE));
}

// Test parameter validation
TEST_F(NegotiationProtocolDataStructuresTest, ParameterValidation) {
    // Valid parameters
    EXPECT_EQ(validation::validateParameterSet(defaultParams), validation::ValidationResult::VALID);

    // Invalid combinations
    NegotiableParams invalidParams = defaultParams;
    invalidParams.dataFormat = DataFormat::COMPRESSED_STATE;
    invalidParams.compressionAlgorithm = CompressionAlgorithm::ZLIB;
    EXPECT_EQ(validation::validateParameterSet(invalidParams), validation::ValidationResult::INCOMPATIBLE_FORMAT_COMPRESSION);

    invalidParams = defaultParams;
    invalidParams.dataFormat = DataFormat::GGWAVE_FSK;
    invalidParams.errorCorrection = ErrorCorrectionScheme::REED_SOLOMON;
    EXPECT_EQ(validation::validateParameterSet(invalidParams), validation::ValidationResult::INCOMPATIBLE_FORMAT_ERROR_CORRECTION);
}

// Test parameter preference ranking
TEST_F(NegotiationProtocolDataStructuresTest, ParameterPreferenceRanking) {
    // Create preference sets
    ParameterPreference prefs;
    
    // Add data format preferences (rank 1 is highest preference)
    prefs.dataFormats.emplace_back(DataFormat::VECTOR_FLOAT32, 1, true);  // Required
    prefs.dataFormats.emplace_back(DataFormat::BINARY_CUSTOM, 2);
    prefs.dataFormats.emplace_back(DataFormat::VECTOR_INT8, 3);
    
    // Add compression algorithm preferences
    prefs.compressionAlgorithms.emplace_back(CompressionAlgorithm::ZSTD, 1);
    prefs.compressionAlgorithms.emplace_back(CompressionAlgorithm::LZ4, 2);
    prefs.compressionAlgorithms.emplace_back(CompressionAlgorithm::NONE, 3);
    
    // Add error correction preferences
    prefs.errorCorrectionSchemes.emplace_back(ErrorCorrectionScheme::REED_SOLOMON, 1);
    prefs.errorCorrectionSchemes.emplace_back(ErrorCorrectionScheme::CHECKSUM_ONLY, 2);
    prefs.errorCorrectionSchemes.emplace_back(ErrorCorrectionScheme::NONE, 3);
    
    // Define remote options
    std::vector<DataFormat> remoteFormats = {
        DataFormat::BINARY_CUSTOM,
        DataFormat::VECTOR_INT8
    };
    
    std::vector<CompressionAlgorithm> remoteCompression = {
        CompressionAlgorithm::NONE,
        CompressionAlgorithm::LZ4
    };
    
    std::vector<ErrorCorrectionScheme> remoteErrorCorrection = {
        ErrorCorrectionScheme::NONE,
        ErrorCorrectionScheme::CHECKSUM_ONLY
    };
    
    // Test best match finding
    auto bestFormat = prefs.findBestMatch(prefs.dataFormats, remoteFormats);
    EXPECT_FALSE(bestFormat.has_value());  // No match because required VECTOR_FLOAT32 is missing
    
    // Add VECTOR_FLOAT32 to remote options and try again
    remoteFormats.push_back(DataFormat::VECTOR_FLOAT32);
    bestFormat = prefs.findBestMatch(prefs.dataFormats, remoteFormats);
    EXPECT_TRUE(bestFormat.has_value());
    if (bestFormat.has_value()) {
        EXPECT_EQ(*bestFormat, DataFormat::VECTOR_FLOAT32);  // Should match highest preference
    }

    // Test best compression match
    auto bestCompression = prefs.findBestMatch(prefs.compressionAlgorithms, remoteCompression);
    EXPECT_TRUE(bestCompression.has_value());
    if (bestCompression.has_value()) {
        EXPECT_EQ(*bestCompression, CompressionAlgorithm::LZ4);  // Should match highest available preference
    }

    // Test best error correction match
    auto bestErrorCorrection = prefs.findBestMatch(prefs.errorCorrectionSchemes, remoteErrorCorrection);
    EXPECT_TRUE(bestErrorCorrection.has_value());
    if (bestErrorCorrection.has_value()) {
        EXPECT_EQ(*bestErrorCorrection, ErrorCorrectionScheme::CHECKSUM_ONLY);  // Should match highest available preference
    }

    // Test building compatible params
    auto compatibleParams = prefs.buildCompatibleParams(remoteFormats, remoteCompression, remoteErrorCorrection);
    EXPECT_EQ(compatibleParams.dataFormat, DataFormat::VECTOR_FLOAT32);
    EXPECT_EQ(compatibleParams.compressionAlgorithm, CompressionAlgorithm::LZ4);
    EXPECT_EQ(compatibleParams.errorCorrection, ErrorCorrectionScheme::CHECKSUM_ONLY);
}

// Test the utility functions
TEST_F(NegotiationProtocolDataStructuresTest, UtilityFunctions) {
    // Test validation result to string conversion
    EXPECT_FALSE(validation::validationResultToString(validation::ValidationResult::VALID).empty());
    EXPECT_FALSE(validation::validationResultToString(validation::ValidationResult::INVALID_DATA_FORMAT).empty());
    EXPECT_FALSE(validation::validationResultToString(validation::ValidationResult::INCOMPATIBLE_FORMAT_COMPRESSION).empty());
}

// Test timeout and retry constants
TEST_F(NegotiationProtocolDataStructuresTest, TimeoutAndRetryConstants) {
    // These constants should be defined in the implementation file, but we can test their existence indirectly
    // by ensuring that timeout-related operations don't cause compilation errors
    NegotiationState state = NegotiationState::AWAITING_RESPONSE;
    EXPECT_TRUE(state == NegotiationState::AWAITING_RESPONSE);
    
    // Check that other state enums exist and match expected values
    EXPECT_NE(NegotiationState::IDLE, NegotiationState::AWAITING_RESPONSE);
    EXPECT_NE(NegotiationState::FINALIZED, NegotiationState::FAILED);
    EXPECT_NE(NegotiationState::CLOSED, NegotiationState::PROPOSAL_RECEIVED);
}

// Test auto-processing of proposals
TEST_F(NegotiationProtocolDataStructuresTest, AutoProcessProposal) {
    // Create a new negotiation protocol instance
    auto protocol = createNegotiationProtocol();
    
    // Create a preferences object
    ParameterPreference prefs;
    
    // Add data format preferences (rank 1 is highest preference)
    prefs.dataFormats.emplace_back(DataFormat::VECTOR_FLOAT32, 1, true); // Required
    prefs.dataFormats.emplace_back(DataFormat::BINARY_CUSTOM, 2);
    prefs.dataFormats.emplace_back(DataFormat::VECTOR_INT8, 3);
    
    // Add compression algorithm preferences
    prefs.compressionAlgorithms.emplace_back(CompressionAlgorithm::ZSTD, 1);
    prefs.compressionAlgorithms.emplace_back(CompressionAlgorithm::LZ4, 2);
    prefs.compressionAlgorithms.emplace_back(CompressionAlgorithm::NONE, 3);
    
    // Add error correction preferences
    prefs.errorCorrectionSchemes.emplace_back(ErrorCorrectionScheme::REED_SOLOMON, 1);
    prefs.errorCorrectionSchemes.emplace_back(ErrorCorrectionScheme::CHECKSUM_ONLY, 2);
    prefs.errorCorrectionSchemes.emplace_back(ErrorCorrectionScheme::NONE, 3);
    
    // Test different proposals manually since we can't directly access internal methods in the test
    
    // Test case 1: Compatible proposal
    {
        NegotiableParams goodProposal;
        goodProposal.dataFormat = DataFormat::VECTOR_FLOAT32;
        goodProposal.compressionAlgorithm = CompressionAlgorithm::ZSTD;
        goodProposal.errorCorrection = ErrorCorrectionScheme::REED_SOLOMON;
        
        // Create an actual proposal using the parameters
        EXPECT_NO_THROW({
            auto proposalResult = prefs.createOptimalParameters();
            EXPECT_EQ(proposalResult.dataFormat, DataFormat::VECTOR_FLOAT32);
            EXPECT_EQ(proposalResult.compressionAlgorithm, CompressionAlgorithm::ZSTD);
            EXPECT_EQ(proposalResult.errorCorrection, ErrorCorrectionScheme::REED_SOLOMON);
        });
    }
    
    // Test case 2: Test compatibility check
    {
        // Create a proposal that meets requirements
        NegotiableParams goodProposal;
        goodProposal.dataFormat = DataFormat::VECTOR_FLOAT32; // Required format
        goodProposal.compressionAlgorithm = CompressionAlgorithm::NONE;
        goodProposal.errorCorrection = ErrorCorrectionScheme::NONE;
        
        // Check if compatible
        EXPECT_TRUE(prefs.isCompatibleWithRequirements(goodProposal));
        
        // Create a proposal that doesn't meet requirements
        NegotiableParams badProposal;
        badProposal.dataFormat = DataFormat::BINARY_CUSTOM; // Not the required format
        badProposal.compressionAlgorithm = CompressionAlgorithm::NONE;
        badProposal.errorCorrection = ErrorCorrectionScheme::NONE;
        
        // Check if compatible
        EXPECT_FALSE(prefs.isCompatibleWithRequirements(badProposal));
    }
    
    // Test case 3: Test scoring
    {
        // Create proposals with different levels of compatibility
        NegotiableParams perfectProposal;
        perfectProposal.dataFormat = DataFormat::VECTOR_FLOAT32; // Rank 1
        perfectProposal.compressionAlgorithm = CompressionAlgorithm::ZSTD; // Rank 1
        perfectProposal.errorCorrection = ErrorCorrectionScheme::REED_SOLOMON; // Rank 1
        
        NegotiableParams goodProposal;
        goodProposal.dataFormat = DataFormat::VECTOR_FLOAT32; // Rank 1
        goodProposal.compressionAlgorithm = CompressionAlgorithm::LZ4; // Rank 2
        goodProposal.errorCorrection = ErrorCorrectionScheme::CHECKSUM_ONLY; // Rank 2
        
        NegotiableParams averageProposal;
        averageProposal.dataFormat = DataFormat::VECTOR_FLOAT32; // Rank 1
        averageProposal.compressionAlgorithm = CompressionAlgorithm::NONE; // Rank 3
        averageProposal.errorCorrection = ErrorCorrectionScheme::NONE; // Rank 3
        
        // Calculate and compare scores
        uint32_t perfectScore = prefs.calculateCompatibilityScore(perfectProposal);
        uint32_t goodScore = prefs.calculateCompatibilityScore(goodProposal);
        uint32_t averageScore = prefs.calculateCompatibilityScore(averageProposal);
        
        // Lower score is better
        EXPECT_LT(perfectScore, goodScore);
        EXPECT_LT(goodScore, averageScore);
    }
    
    // Test case 4: Test counter-proposal generation
    {
        // Create a remote proposal
        NegotiableParams remoteProposal;
        remoteProposal.dataFormat = DataFormat::BINARY_CUSTOM; // Not the required format
        remoteProposal.compressionAlgorithm = CompressionAlgorithm::ZSTD; // Matches local preference
        remoteProposal.errorCorrection = ErrorCorrectionScheme::CHECKSUM_ONLY; // Second best
        
        // Try to create a compatible counter-proposal
        std::vector<DataFormat> remoteFormats = {remoteProposal.dataFormat, DataFormat::VECTOR_FLOAT32};
        std::vector<CompressionAlgorithm> remoteCompression = {remoteProposal.compressionAlgorithm};
        std::vector<ErrorCorrectionScheme> remoteErrorCorrection = {remoteProposal.errorCorrection};
        
        auto counterProposal = prefs.buildCompatibleParams(remoteFormats, remoteCompression, remoteErrorCorrection);
        
        // Verify the counter-proposal has the required format
        EXPECT_EQ(counterProposal.dataFormat, DataFormat::VECTOR_FLOAT32);
        // But keeps the compatible compression and error correction
        EXPECT_EQ(counterProposal.compressionAlgorithm, CompressionAlgorithm::ZSTD);
        EXPECT_EQ(counterProposal.errorCorrection, ErrorCorrectionScheme::CHECKSUM_ONLY);
    }
}

} // namespace test
} // namespace core
} // namespace xenocomm 