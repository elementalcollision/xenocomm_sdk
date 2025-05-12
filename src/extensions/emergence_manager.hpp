#pragma once
#include <string>
#include <map>
#include <nlohmann/json.hpp>
#include <memory>
#include <vector>
#include <chrono>
#include <optional>

namespace xenocomm {
namespace extensions {

/**
 * @brief Enum for protocol variant status.
 */
enum class VariantStatus {
    Proposed,
    InTesting,
    Adopted,
    Rejected
};

/**
 * @brief Represents a protocol variant/modification.
 */
class ProtocolVariant {
public:
    std::string id;
    std::string description;
    nlohmann::json changes;
    nlohmann::json metadata;

    ProtocolVariant() = default;
    ProtocolVariant(const std::string& id_, const std::string& desc_, const nlohmann::json& changes_, const nlohmann::json& meta_);

    // Serialization/deserialization
    nlohmann::json to_json() const;
    static ProtocolVariant from_json(const nlohmann::json& j);
};

/**
 * @brief Performance metrics for a protocol variant.
 */
struct PerformanceMetrics {
    double successRate = 0.0;       // Percentage of successful operations
    double latencyMs = 0.0;         // Average operation latency in milliseconds
    double resourceUsage = 0.0;     // Normalized resource consumption (0.0-1.0)
    double throughput = 0.0;        // Operations per second
    std::map<std::string, double> customMetrics; // For protocol-specific metrics
};

/**
 * @brief Complete performance record for a variant.
 */
struct PerformanceRecord {
    std::chrono::system_clock::time_point timestamp;
    PerformanceMetrics metrics;
    std::string context;      // Additional context information
    size_t sampleSize = 0;    // Number of operations this record represents
};

/**
 * @brief Criteria for evaluating and comparing variant performance.
 */
struct EvaluationCriteria {
    std::map<std::string, double> metricWeights; // Weights for each metric
    double improvementThreshold = 0.0;           // Minimum improvement to flag (e.g., 0.05 for 5%)
    size_t minSampleSize = 1;                    // Minimum samples required for valid comparison
    bool requireStatisticalSignificance = false; // Whether to require p < 0.05
};

/**
 * @brief Agent context for variant recommendations and decision-making.
 */
struct AgentContext {
    std::string agentId;
    std::map<std::string, std::string> capabilities;  // Agent capabilities/features
    std::map<std::string, double> preferences;        // Preference weights for different metrics
    std::vector<std::string> successfulVariants;      // Previously successful variants for this agent

    // Serialization/deserialization
    nlohmann::json to_json() const;
    static AgentContext from_json(const nlohmann::json& j);
};

/**
 * @brief Record of an agent's vote on a protocol variant.
 */
struct VotingRecord {
    std::string variantId;
    std::string agentId;
    bool support;  // true = support, false = oppose
    std::string reason;
    std::chrono::system_clock::time_point timestamp;

    // Serialization/deserialization
    nlohmann::json to_json() const;
    static VotingRecord from_json(const nlohmann::json& j);
};

/**
 * @brief Configuration for the consensus mechanism.
 */
struct ConsensusConfig {
    double requiredMajority = 0.75;           // e.g., 0.75 for 75% required
    size_t minimumVotes = 3;                  // Minimum votes needed for adoption
    std::chrono::seconds votingPeriod{3600};  // How long to collect votes (default: 1 hour)
    bool requirePerformanceEvidence = true;    // Whether to require performance data

    // Serialization/deserialization
    nlohmann::json to_json() const;
    static ConsensusConfig from_json(const nlohmann::json& j);
};

/**
 * @brief Manages protocol variants and their lifecycle.
 */
class EmergenceManager {
public:
    EmergenceManager(const std::string& persistencePath, const nlohmann::json& evalMetrics);

    // --- Persistence and Sharing API ---
    void saveState() const;
    void loadState();
    void exportVariants(const std::string& filePath, const std::vector<std::string>& variantIds) const;
    void importVariants(const std::string& filePath);
    void enableAutosave(std::chrono::seconds interval = std::chrono::seconds(300));
    void disableAutosave();
    bool validateVariant(const ProtocolVariant& variant) const;
    std::string resolveConflict(const ProtocolVariant& existing, const ProtocolVariant& imported) const;

    // Propose a new variant
    void proposeVariant(const std::string& id, const ProtocolVariant& variant, const std::string& description, const nlohmann::json& metadata);

    // Get a variant by ID
    ProtocolVariant getVariant(const std::string& id) const;

    // List variants by status
    std::map<std::string, ProtocolVariant> listVariants(VariantStatus status) const;

    // Set the status of a variant
    void setVariantStatus(const std::string& id, VariantStatus status);

    // Logging
    void logEvent(const std::string& message) const;

    // --- Performance Logging and Evaluation API ---
    void logPerformance(const std::string& variantId, const PerformanceRecord& record);
    std::vector<PerformanceRecord> getVariantPerformance(const std::string& variantId) const;
    std::optional<std::string> getBestPerformingVariant(const EvaluationCriteria& criteria) const;
    void setEvaluationCriteria(const EvaluationCriteria& criteria);
    EvaluationCriteria getEvaluationCriteria() const;
    bool isSignificantlyBetter(const std::string& variantId, const std::string& baselineId, const EvaluationCriteria& criteria) const;
    std::string generatePerformanceReport(const std::vector<std::string>& variantIds) const;

    // --- Agent-Driven Protocol Evolution API ---
    
    // Agent registration and context management
    void registerAgent(const std::string& agentId, const AgentContext& context);
    void updateAgentContext(const std::string& agentId, const AgentContext& context);
    AgentContext getAgentContext(const std::string& agentId) const;
    
    // Variant proposal and voting
    std::string proposeVariantAsAgent(
        const std::string& agentId,
        const ProtocolVariant& variant,
        const std::string& rationale
    );
    void voteOnVariant(
        const std::string& agentId,
        const std::string& variantId,
        bool support,
        const std::string& reason
    );
    
    // Variant adoption and notification
    std::vector<std::string> getRecommendedVariants(
        const std::string& agentId,
        size_t maxResults = 5
    ) const;
    void reportVariantExperience(
        const std::string& agentId,
        const std::string& variantId,
        bool successful,
        const std::string& details
    );
    std::vector<std::string> getNewlyAdoptedVariants(
        const std::string& agentId,
        std::chrono::system_clock::time_point since
    ) const;
    
    // Consensus configuration
    void setConsensusConfig(const ConsensusConfig& config);
    ConsensusConfig getConsensusConfig() const;

private:
    std::string persistencePath_;
    nlohmann::json evalMetrics_;
    std::map<std::string, ProtocolVariant> variants_;
    std::map<std::string, VariantStatus> statusMap_;
    std::map<std::string, std::vector<PerformanceRecord>> performanceHistory_;
    EvaluationCriteria evalCriteria_;

    // Persistence and autosave members
    bool autosaveEnabled_ = false;
    std::chrono::seconds autosaveInterval_;
    std::chrono::system_clock::time_point lastSaveTime_;
    
    // Helper methods for persistence
    nlohmann::json serializeState() const;
    void deserializeState(const nlohmann::json& state);
    void checkAutosave();
    void writeJsonToFile(const std::string& filePath, const nlohmann::json& data) const;
    nlohmann::json readJsonFromFile(const std::string& filePath) const;

    // Agent-driven evolution private members
    std::map<std::string, AgentContext> agentContexts_;
    std::map<std::string, std::vector<VotingRecord>> variantVotes_;
    std::map<std::string, std::chrono::system_clock::time_point> adoptionTimestamps_;
    ConsensusConfig consensusConfig_;
    
    // Agent-driven evolution private methods
    bool checkConsensus(const std::string& variantId) const;
    void processAdoption(const std::string& variantId);
    double calculateAgentCompatibility(
        const std::string& agentId,
        const ProtocolVariant& variant
    ) const;
};

} // namespace extensions
} // namespace xenocomm 