#include "xenocomm/extensions/emergence_manager.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace xenocomm {
namespace extensions {

// Placeholder implementation
// TODO: Implement actual emergence management functionality

// ---- ProtocolVariant Implementation ----

ProtocolVariant::ProtocolVariant(const std::string& id_, const std::string& desc_, const nlohmann::json& changes_, const nlohmann::json& meta_)
    : id(id_), description(desc_), changes(changes_), metadata(meta_) {}

nlohmann::json ProtocolVariant::to_json() const {
    return nlohmann::json{{"id", id}, {"description", description}, {"changes", changes}, {"metadata", metadata}};
}

ProtocolVariant ProtocolVariant::from_json(const nlohmann::json& j) {
    return ProtocolVariant(j.at("id"), j.at("description"), j.at("changes"), j.at("metadata"));
}

// ---- EmergenceManager Implementation ----

EmergenceManager::EmergenceManager(const std::string& persistencePath, const nlohmann::json& evalMetrics)
    : persistencePath_(persistencePath), evalMetrics_(evalMetrics), 
      autosaveInterval_(std::chrono::seconds(300)), lastSaveTime_(std::chrono::system_clock::now()) {
    // Try to load existing state
    try {
        loadState();
    } catch (const std::exception& e) {
        logEvent("No existing state found or failed to load: " + std::string(e.what()));
    }
}

void EmergenceManager::proposeVariant(const std::string& id, const ProtocolVariant& variant, const std::string& description, const nlohmann::json& metadata) {
    if (variants_.count(id) > 0) {
        throw std::invalid_argument("Variant with this ID already exists");
    }
    ProtocolVariant v = variant;
    v.id = id;
    v.description = description;
    v.metadata = metadata;
    variants_[id] = v;
    statusMap_[id] = VariantStatus::Proposed;
    logEvent("Proposed variant: " + id + " - " + description);
    checkAutosave();
}

ProtocolVariant EmergenceManager::getVariant(const std::string& id) const {
    auto it = variants_.find(id);
    if (it == variants_.end()) {
        throw std::out_of_range("Variant ID not found");
    }
    return it->second;
}

std::map<std::string, ProtocolVariant> EmergenceManager::listVariants(VariantStatus status) const {
    std::map<std::string, ProtocolVariant> result;
    for (const auto& [id, stat] : statusMap_) {
        if (stat == status) {
            auto it = variants_.find(id);
            if (it != variants_.end()) {
                result[id] = it->second;
            }
        }
    }
    return result;
}

void EmergenceManager::setVariantStatus(const std::string& id, VariantStatus status) {
    if (variants_.count(id) == 0) {
        throw std::out_of_range("Variant ID not found");
    }
    statusMap_[id] = status;
    logEvent("Status changed for variant: " + id + " to status " + std::to_string(static_cast<int>(status)));
    checkAutosave();
}

void EmergenceManager::logEvent(const std::string& message) const {
    // Append timestamped message to log file
    std::ofstream logFile(persistencePath_ + "/emergence_manager.log", std::ios::app);
    if (logFile.is_open()) {
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        logFile << std::ctime(&now_c) << ": " << message << std::endl;
    }
}

// --- Performance Logging and Evaluation Implementation ---

void EmergenceManager::logPerformance(const std::string& variantId, const PerformanceRecord& record) {
    performanceHistory_[variantId].push_back(record);
    logEvent("Logged performance for variant: " + variantId);
    checkAutosave();
}

std::vector<PerformanceRecord> EmergenceManager::getVariantPerformance(const std::string& variantId) const {
    auto it = performanceHistory_.find(variantId);
    if (it != performanceHistory_.end()) {
        return it->second;
    }
    return {};
}

void EmergenceManager::setEvaluationCriteria(const EvaluationCriteria& criteria) {
    evalCriteria_ = criteria;
}

EvaluationCriteria EmergenceManager::getEvaluationCriteria() const {
    return evalCriteria_;
}

std::optional<std::string> EmergenceManager::getBestPerformingVariant(const EvaluationCriteria& criteria) const {
    double bestScore = -1e9;
    std::optional<std::string> bestId;
    for (const auto& [variantId, records] : performanceHistory_) {
        if (records.size() < criteria.minSampleSize) continue;
        double score = 0.0;
        double totalWeight = 0.0;
        for (const auto& [metric, weight] : criteria.metricWeights) {
            double avg = 0.0;
            size_t count = 0;
            for (const auto& rec : records) {
                auto it = rec.metrics.customMetrics.find(metric);
                if (metric == "successRate") avg += rec.metrics.successRate;
                else if (metric == "latencyMs") avg += rec.metrics.latencyMs;
                else if (metric == "resourceUsage") avg += rec.metrics.resourceUsage;
                else if (metric == "throughput") avg += rec.metrics.throughput;
                else if (it != rec.metrics.customMetrics.end()) avg += it->second;
                else continue;
                ++count;
            }
            if (count > 0) avg /= count;
            score += avg * weight;
            totalWeight += weight;
        }
        if (totalWeight > 0) score /= totalWeight;
        if (score > bestScore) {
            bestScore = score;
            bestId = variantId;
        }
    }
    return bestId;
}

bool EmergenceManager::isSignificantlyBetter(const std::string& variantId, const std::string& baselineId, const EvaluationCriteria& criteria) const {
    auto variantData = getVariantPerformance(variantId);
    auto baselineData = getVariantPerformance(baselineId);
    if (variantData.size() < criteria.minSampleSize || baselineData.size() < criteria.minSampleSize) return false;
    double weightedImprovement = 0.0;
    double totalWeight = 0.0;
    for (const auto& [metric, weight] : criteria.metricWeights) {
        double variantAvg = 0.0, baselineAvg = 0.0;
        size_t vcount = 0, bcount = 0;
        for (const auto& rec : variantData) {
            auto it = rec.metrics.customMetrics.find(metric);
            if (metric == "successRate") variantAvg += rec.metrics.successRate;
            else if (metric == "latencyMs") variantAvg += rec.metrics.latencyMs;
            else if (metric == "resourceUsage") variantAvg += rec.metrics.resourceUsage;
            else if (metric == "throughput") variantAvg += rec.metrics.throughput;
            else if (it != rec.metrics.customMetrics.end()) variantAvg += it->second;
            else continue;
            ++vcount;
        }
        for (const auto& rec : baselineData) {
            auto it = rec.metrics.customMetrics.find(metric);
            if (metric == "successRate") baselineAvg += rec.metrics.successRate;
            else if (metric == "latencyMs") baselineAvg += rec.metrics.latencyMs;
            else if (metric == "resourceUsage") baselineAvg += rec.metrics.resourceUsage;
            else if (metric == "throughput") baselineAvg += rec.metrics.throughput;
            else if (it != rec.metrics.customMetrics.end()) baselineAvg += it->second;
            else continue;
            ++bcount;
        }
        if (vcount > 0) variantAvg /= vcount;
        if (bcount > 0) baselineAvg /= bcount;
        // Higher is better for successRate/throughput, lower is better for latency/resourceUsage
        double improvement = 0.0;
        if (metric == "latencyMs" || metric == "resourceUsage") {
            improvement = (baselineAvg - variantAvg) / (baselineAvg == 0 ? 1 : baselineAvg);
        } else {
            improvement = (variantAvg - baselineAvg) / (baselineAvg == 0 ? 1 : baselineAvg);
        }
        weightedImprovement += improvement * weight;
        totalWeight += weight;
    }
    if (totalWeight > 0) weightedImprovement /= totalWeight;
    return weightedImprovement >= criteria.improvementThreshold;
}

std::string EmergenceManager::generatePerformanceReport(const std::vector<std::string>& variantIds) const {
    std::stringstream report;
    report << "Performance Comparison Report\n";
    report << "============================\n\n";
    report << std::left << std::setw(20) << "Variant";
    for (const auto& metric : {"successRate", "latencyMs", "resourceUsage", "throughput"}) {
        report << std::setw(15) << metric;
    }
    report << "\n" << std::string(80, '-') << "\n";
    for (const auto& id : variantIds) {
        auto records = getVariantPerformance(id);
        double sr = 0, lat = 0, ru = 0, thr = 0;
        size_t n = records.size();
        for (const auto& rec : records) {
            sr += rec.metrics.successRate;
            lat += rec.metrics.latencyMs;
            ru += rec.metrics.resourceUsage;
            thr += rec.metrics.throughput;
        }
        if (n > 0) {
            sr /= n; lat /= n; ru /= n; thr /= n;
        }
        report << std::setw(20) << id;
        report << std::setw(15) << std::fixed << std::setprecision(2) << sr;
        report << std::setw(15) << std::fixed << std::setprecision(2) << lat;
        report << std::setw(15) << std::fixed << std::setprecision(2) << ru;
        report << std::setw(15) << std::fixed << std::setprecision(2) << thr;
        report << "\n";
    }
    return report.str();
}

// --- End Performance Logging and Evaluation Implementation ---

// --- Persistence and Sharing Implementation ---

nlohmann::json EmergenceManager::serializeState() const {
    nlohmann::json state;
    
    // Serialize variants
    state["variants"] = nlohmann::json::object();
    for (const auto& [id, variant] : variants_) {
        state["variants"][id] = variant.to_json();
    }
    
    // Serialize status map
    state["status"] = nlohmann::json::object();
    for (const auto& [id, status] : statusMap_) {
        state["status"][id] = static_cast<int>(status);
    }
    
    // Serialize performance history
    state["performance"] = nlohmann::json::object();
    for (const auto& [id, records] : performanceHistory_) {
        state["performance"][id] = nlohmann::json::array();
        for (const auto& record : records) {
            nlohmann::json j;
            j["timestamp"] = std::chrono::system_clock::to_time_t(record.timestamp);
            j["metrics"] = {
                {"successRate", record.metrics.successRate},
                {"latencyMs", record.metrics.latencyMs},
                {"resourceUsage", record.metrics.resourceUsage},
                {"throughput", record.metrics.throughput},
                {"customMetrics", record.metrics.customMetrics}
            };
            j["context"] = record.context;
            j["sampleSize"] = record.sampleSize;
            state["performance"][id].push_back(j);
        }
    }

    // Serialize agent contexts
    state["agents"] = nlohmann::json::object();
    for (const auto& [id, context] : agentContexts_) {
        state["agents"][id] = context.to_json();
    }

    // Serialize voting records
    state["votes"] = nlohmann::json::object();
    for (const auto& [variantId, votes] : variantVotes_) {
        state["votes"][variantId] = nlohmann::json::array();
        for (const auto& vote : votes) {
            state["votes"][variantId].push_back(vote.to_json());
        }
    }

    // Serialize adoption timestamps
    state["adoptions"] = nlohmann::json::object();
    for (const auto& [variantId, timestamp] : adoptionTimestamps_) {
        state["adoptions"][variantId] = std::chrono::system_clock::to_time_t(timestamp);
    }

    // Serialize consensus config
    state["consensusConfig"] = consensusConfig_.to_json();
    
    return state;
}

void EmergenceManager::deserializeState(const nlohmann::json& state) {
    // Clear existing state
    variants_.clear();
    statusMap_.clear();
    performanceHistory_.clear();
    agentContexts_.clear();
    variantVotes_.clear();
    adoptionTimestamps_.clear();
    
    // Deserialize variants
    for (const auto& [id, variant] : state["variants"].items()) {
        variants_[id] = ProtocolVariant::from_json(variant);
    }
    
    // Deserialize status map
    for (const auto& [id, status] : state["status"].items()) {
        statusMap_[id] = static_cast<VariantStatus>(status.get<int>());
    }
    
    // Deserialize performance history
    for (const auto& [id, records] : state["performance"].items()) {
        performanceHistory_[id] = std::vector<PerformanceRecord>();
        for (const auto& j : records) {
            PerformanceRecord record;
            record.timestamp = std::chrono::system_clock::from_time_t(j["timestamp"].get<time_t>());
            record.metrics.successRate = j["metrics"]["successRate"].get<double>();
            record.metrics.latencyMs = j["metrics"]["latencyMs"].get<double>();
            record.metrics.resourceUsage = j["metrics"]["resourceUsage"].get<double>();
            record.metrics.throughput = j["metrics"]["throughput"].get<double>();
            record.metrics.customMetrics = j["metrics"]["customMetrics"].get<std::map<std::string, double>>();
            record.context = j["context"].get<std::string>();
            record.sampleSize = j["sampleSize"].get<size_t>();
            performanceHistory_[id].push_back(record);
        }
    }

    // Deserialize agent contexts
    if (state.contains("agents")) {
        for (const auto& [id, context] : state["agents"].items()) {
            agentContexts_[id] = AgentContext::from_json(context);
        }
    }

    // Deserialize voting records
    if (state.contains("votes")) {
        for (const auto& [variantId, votes] : state["votes"].items()) {
            variantVotes_[variantId] = std::vector<VotingRecord>();
            for (const auto& vote : votes) {
                variantVotes_[variantId].push_back(VotingRecord::from_json(vote));
            }
        }
    }

    // Deserialize adoption timestamps
    if (state.contains("adoptions")) {
        for (const auto& [variantId, timestamp] : state["adoptions"].items()) {
            adoptionTimestamps_[variantId] = std::chrono::system_clock::from_time_t(timestamp.get<time_t>());
        }
    }

    // Deserialize consensus config
    if (state.contains("consensusConfig")) {
        consensusConfig_ = ConsensusConfig::from_json(state["consensusConfig"]);
    }
}

void EmergenceManager::writeJsonToFile(const std::string& filePath, const nlohmann::json& data) const {
    std::ofstream file(filePath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for writing: " + filePath);
    }
    file << data.dump(2);
}

nlohmann::json EmergenceManager::readJsonFromFile(const std::string& filePath) const {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for reading: " + filePath);
    }
    nlohmann::json data;
    file >> data;
    return data;
}

void EmergenceManager::saveState() const {
    std::string statePath = persistencePath_ + "/emergence_state.json";
    writeJsonToFile(statePath, serializeState());
    logEvent("Saved emergence manager state to " + statePath);
}

void EmergenceManager::loadState() {
    std::string statePath = persistencePath_ + "/emergence_state.json";
    try {
        auto state = readJsonFromFile(statePath);
        deserializeState(state);
        logEvent("Loaded emergence manager state from " + statePath);
    } catch (const std::exception& e) {
        logEvent("Failed to load state: " + std::string(e.what()));
        throw;
    }
}

void EmergenceManager::exportVariants(const std::string& filePath, const std::vector<std::string>& variantIds) const {
    nlohmann::json exportData;
    exportData["variants"] = nlohmann::json::object();
    exportData["performance"] = nlohmann::json::object();
    
    for (const auto& id : variantIds) {
        auto it = variants_.find(id);
        if (it != variants_.end()) {
            exportData["variants"][id] = it->second.to_json();
            auto perfIt = performanceHistory_.find(id);
            if (perfIt != performanceHistory_.end()) {
                exportData["performance"][id] = nlohmann::json::array();
                for (const auto& record : perfIt->second) {
                    nlohmann::json recJson;
                    recJson["timestamp"] = std::chrono::system_clock::to_time_t(record.timestamp);
                    recJson["metrics"] = {
                        {"successRate", record.metrics.successRate},
                        {"latencyMs", record.metrics.latencyMs},
                        {"resourceUsage", record.metrics.resourceUsage},
                        {"throughput", record.metrics.throughput},
                        {"custom", record.metrics.customMetrics}
                    };
                    recJson["context"] = record.context;
                    recJson["sampleSize"] = record.sampleSize;
                    exportData["performance"][id].push_back(recJson);
                }
            }
        }
    }
    
    writeJsonToFile(filePath, exportData);
    logEvent("Exported " + std::to_string(variantIds.size()) + " variants to " + filePath);
}

void EmergenceManager::importVariants(const std::string& filePath) {
    auto importData = readJsonFromFile(filePath);
    
    for (const auto& [id, variantJson] : importData["variants"].items()) {
        auto variant = ProtocolVariant::from_json(variantJson);
        if (!validateVariant(variant)) {
            logEvent("Skipping invalid variant during import: " + id);
            continue;
        }
        
        auto existingIt = variants_.find(id);
        if (existingIt != variants_.end()) {
            std::string resolution = resolveConflict(existingIt->second, variant);
            if (resolution == "skip") {
                logEvent("Skipping conflicting variant during import: " + id);
                continue;
            }
            // For "replace", we just continue with the import
        }
        
        variants_[id] = variant;
        statusMap_[id] = VariantStatus::Proposed;
        
        // Import performance history if available
        auto perfIt = importData["performance"].find(id);
        if (perfIt != importData["performance"].end()) {
            for (const auto& recJson : *perfIt) {
                PerformanceRecord record;
                record.timestamp = std::chrono::system_clock::from_time_t(recJson["timestamp"]);
                record.metrics.successRate = recJson["metrics"]["successRate"];
                record.metrics.latencyMs = recJson["metrics"]["latencyMs"];
                record.metrics.resourceUsage = recJson["metrics"]["resourceUsage"];
                record.metrics.throughput = recJson["metrics"]["throughput"];
                record.metrics.customMetrics = recJson["metrics"]["custom"].get<std::map<std::string, double>>();
                record.context = recJson["context"];
                record.sampleSize = recJson["sampleSize"];
                performanceHistory_[id].push_back(record);
            }
        }
    }
    
    logEvent("Imported variants from " + filePath);
}

void EmergenceManager::enableAutosave(std::chrono::seconds interval) {
    autosaveEnabled_ = true;
    autosaveInterval_ = interval;
    lastSaveTime_ = std::chrono::system_clock::now();
    logEvent("Enabled autosave with interval of " + std::to_string(interval.count()) + " seconds");
}

void EmergenceManager::disableAutosave() {
    if (autosaveEnabled_) {
        autosaveEnabled_ = false;
        logEvent("Disabled autosave");
    }
}

void EmergenceManager::checkAutosave() {
    if (!autosaveEnabled_) return;
    
    auto now = std::chrono::system_clock::now();
    if (now - lastSaveTime_ >= autosaveInterval_) {
        saveState();
        lastSaveTime_ = now;
    }
}

bool EmergenceManager::validateVariant(const ProtocolVariant& variant) const {
    // Basic validation
    if (variant.id.empty()) return false;
    if (variant.changes.is_null()) return false;
    
    // Validate against evaluation metrics
    for (const auto& [metric, _] : evalMetrics_.items()) {
        if (!variant.metadata.contains(metric)) {
            return false;
        }
    }
    
    return true;
}

std::string EmergenceManager::resolveConflict(const ProtocolVariant& existing, const ProtocolVariant& imported) const {
    // Simple conflict resolution strategy
    // If the imported variant has more recent metadata, replace the existing one
    if (imported.metadata.contains("timestamp") && existing.metadata.contains("timestamp")) {
        if (imported.metadata["timestamp"] > existing.metadata["timestamp"]) {
            return "replace";
        }
    }
    return "skip";
}

// AgentContext serialization methods
nlohmann::json AgentContext::to_json() const {
    nlohmann::json j;
    j["agentId"] = agentId;
    j["capabilities"] = capabilities;
    j["preferences"] = preferences;
    j["successfulVariants"] = successfulVariants;
    return j;
}

AgentContext AgentContext::from_json(const nlohmann::json& j) {
    AgentContext ctx;
    ctx.agentId = j["agentId"].get<std::string>();
    ctx.capabilities = j["capabilities"].get<std::map<std::string, std::string>>();
    ctx.preferences = j["preferences"].get<std::map<std::string, double>>();
    ctx.successfulVariants = j["successfulVariants"].get<std::vector<std::string>>();
    return ctx;
}

// VotingRecord serialization methods
nlohmann::json VotingRecord::to_json() const {
    nlohmann::json j;
    j["variantId"] = variantId;
    j["agentId"] = agentId;
    j["support"] = support;
    j["reason"] = reason;
    j["timestamp"] = std::chrono::system_clock::to_time_t(timestamp);
    return j;
}

VotingRecord VotingRecord::from_json(const nlohmann::json& j) {
    VotingRecord record;
    record.variantId = j["variantId"].get<std::string>();
    record.agentId = j["agentId"].get<std::string>();
    record.support = j["support"].get<bool>();
    record.reason = j["reason"].get<std::string>();
    record.timestamp = std::chrono::system_clock::from_time_t(j["timestamp"].get<time_t>());
    return record;
}

// ConsensusConfig serialization methods
nlohmann::json ConsensusConfig::to_json() const {
    nlohmann::json j;
    j["requiredMajority"] = requiredMajority;
    j["minimumVotes"] = minimumVotes;
    j["votingPeriod"] = votingPeriod.count();
    j["requirePerformanceEvidence"] = requirePerformanceEvidence;
    return j;
}

ConsensusConfig ConsensusConfig::from_json(const nlohmann::json& j) {
    ConsensusConfig config;
    config.requiredMajority = j["requiredMajority"].get<double>();
    config.minimumVotes = j["minimumVotes"].get<size_t>();
    config.votingPeriod = std::chrono::seconds(j["votingPeriod"].get<int64_t>());
    config.requirePerformanceEvidence = j["requirePerformanceEvidence"].get<bool>();
    return config;
}

// Agent registration and context management
void EmergenceManager::registerAgent(const std::string& agentId, const AgentContext& context) {
    if (agentContexts_.find(agentId) != agentContexts_.end()) {
        throw std::runtime_error("Agent ID already registered: " + agentId);
    }
    
    if (context.agentId != agentId) {
        throw std::invalid_argument("AgentContext ID must match provided agentId");
    }
    
    agentContexts_[agentId] = context;
    logEvent("Registered new agent: " + agentId);
    checkAutosave();
}

void EmergenceManager::updateAgentContext(const std::string& agentId, const AgentContext& context) {
    if (agentContexts_.find(agentId) == agentContexts_.end()) {
        throw std::runtime_error("Agent not found: " + agentId);
    }
    
    if (context.agentId != agentId) {
        throw std::invalid_argument("AgentContext ID must match provided agentId");
    }
    
    agentContexts_[agentId] = context;
    logEvent("Updated context for agent: " + agentId);
    checkAutosave();
}

AgentContext EmergenceManager::getAgentContext(const std::string& agentId) const {
    auto it = agentContexts_.find(agentId);
    if (it == agentContexts_.end()) {
        throw std::runtime_error("Agent not found: " + agentId);
    }
    return it->second;
}

// Variant proposal and voting
std::string EmergenceManager::proposeVariantAsAgent(
    const std::string& agentId,
    const ProtocolVariant& variant,
    const std::string& rationale
) {
    // Verify agent exists
    if (agentContexts_.find(agentId) == agentContexts_.end()) {
        throw std::runtime_error("Agent not found: " + agentId);
    }

    // Validate the variant
    if (!validateVariant(variant)) {
        throw std::invalid_argument("Invalid variant proposal");
    }

    // Add agent metadata to the variant
    auto enrichedVariant = variant;
    enrichedVariant.metadata["proposingAgent"] = agentId;
    enrichedVariant.metadata["proposalRationale"] = rationale;
    enrichedVariant.metadata["proposalTimestamp"] = 
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    // Add the variant to the system
    proposeVariant(variant.id, enrichedVariant, variant.description, enrichedVariant.metadata);

    // Create initial voting record for the proposing agent
    VotingRecord vote{
        variant.id,
        agentId,
        true,  // Proposing agent automatically supports their proposal
        "Initial proposal: " + rationale,
        std::chrono::system_clock::now()
    };
    variantVotes_[variant.id].push_back(vote);

    logEvent("Agent " + agentId + " proposed variant: " + variant.id);
    checkAutosave();
    
    return variant.id;
}

void EmergenceManager::voteOnVariant(
    const std::string& agentId,
    const std::string& variantId,
    bool support,
    const std::string& reason
) {
    // Verify agent exists
    if (agentContexts_.find(agentId) == agentContexts_.end()) {
        throw std::runtime_error("Agent not found: " + agentId);
    }

    // Verify variant exists
    if (variants_.find(variantId) == variants_.end()) {
        throw std::runtime_error("Variant not found: " + variantId);
    }

    // Check if variant is in a votable state
    auto status = statusMap_[variantId];
    if (status != VariantStatus::Proposed && status != VariantStatus::InTesting) {
        throw std::runtime_error("Variant " + variantId + " is not in a votable state");
    }

    // Record the vote
    VotingRecord vote{
        variantId,
        agentId,
        support,
        reason,
        std::chrono::system_clock::now()
    };
    variantVotes_[variantId].push_back(vote);

    // Check if this vote triggers consensus
    if (checkConsensus(variantId)) {
        processAdoption(variantId);
    }

    logEvent("Agent " + agentId + " voted " + (support ? "for" : "against") + " variant: " + variantId);
    checkAutosave();
}

std::vector<std::string> EmergenceManager::getRecommendedVariants(
    const std::string& agentId,
    size_t maxResults
) const {
    // Verify agent exists
    if (agentContexts_.find(agentId) == agentContexts_.end()) {
        throw std::runtime_error("Agent not found: " + agentId);
    }

    const auto& agentContext = agentContexts_.at(agentId);
    std::vector<std::pair<std::string, double>> scores;

    // Calculate compatibility scores for all adopted variants
    for (const auto& [variantId, variant] : variants_) {
        if (statusMap_.at(variantId) == VariantStatus::Adopted) {
            double score = calculateAgentCompatibility(agentId, variant);
            scores.emplace_back(variantId, score);
        }
    }

    // Sort by compatibility score
    std::sort(scores.begin(), scores.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    // Return top N variants
    std::vector<std::string> recommendations;
    for (size_t i = 0; i < std::min(maxResults, scores.size()); ++i) {
        recommendations.push_back(scores[i].first);
    }

    return recommendations;
}

void EmergenceManager::reportVariantExperience(
    const std::string& agentId,
    const std::string& variantId,
    bool successful,
    const std::string& details
) {
    // Verify agent exists
    auto agentIt = agentContexts_.find(agentId);
    if (agentIt == agentContexts_.end()) {
        throw std::runtime_error("Agent not found: " + agentId);
    }

    // Verify variant exists
    if (variants_.find(variantId) == variants_.end()) {
        throw std::runtime_error("Variant not found: " + variantId);
    }

    // Update agent's context with the experience
    auto& context = agentIt->second;
    if (successful) {
        if (std::find(context.successfulVariants.begin(),
                      context.successfulVariants.end(),
                      variantId) == context.successfulVariants.end()) {
            context.successfulVariants.push_back(variantId);
        }
    }

    // Log the experience
    std::string eventMsg = "Agent " + agentId + " reported " +
                          (successful ? "successful" : "unsuccessful") +
                          " experience with variant " + variantId + ": " + details;
    logEvent(eventMsg);
    checkAutosave();
}

std::vector<std::string> EmergenceManager::getNewlyAdoptedVariants(
    const std::string& agentId,
    std::chrono::system_clock::time_point since
) const {
    // Verify agent exists
    if (agentContexts_.find(agentId) == agentContexts_.end()) {
        throw std::runtime_error("Agent not found: " + agentId);
    }

    std::vector<std::string> newVariants;
    for (const auto& [variantId, adoptionTime] : adoptionTimestamps_) {
        if (adoptionTime > since && statusMap_.at(variantId) == VariantStatus::Adopted) {
            newVariants.push_back(variantId);
        }
    }

    return newVariants;
}

void EmergenceManager::setConsensusConfig(const ConsensusConfig& config) {
    // Validate config
    if (config.requiredMajority <= 0.0 || config.requiredMajority > 1.0) {
        throw std::invalid_argument("Required majority must be between 0 and 1");
    }
    if (config.minimumVotes == 0) {
        throw std::invalid_argument("Minimum votes must be greater than 0");
    }
    if (config.votingPeriod.count() <= 0) {
        throw std::invalid_argument("Voting period must be positive");
    }

    consensusConfig_ = config;
    logEvent("Updated consensus configuration");
    checkAutosave();
}

ConsensusConfig EmergenceManager::getConsensusConfig() const {
    return consensusConfig_;
}

// Private helper methods

bool EmergenceManager::checkConsensus(const std::string& variantId) const {
    const auto& votes = variantVotes_.at(variantId);
    
    // Check if we have minimum required votes
    if (votes.size() < consensusConfig_.minimumVotes) {
        return false;
    }

    // Check if voting period has elapsed
    auto latestVote = std::max_element(votes.begin(), votes.end(),
        [](const VotingRecord& a, const VotingRecord& b) {
            return a.timestamp < b.timestamp;
        });
    
    auto now = std::chrono::system_clock::now();
    if (now - latestVote->timestamp < consensusConfig_.votingPeriod) {
        return false;
    }

    // Count votes
    size_t supportCount = std::count_if(votes.begin(), votes.end(),
        [](const VotingRecord& v) { return v.support; });
    
    double supportRatio = static_cast<double>(supportCount) / votes.size();
    
    // Check if we have required majority
    if (supportRatio < consensusConfig_.requiredMajority) {
        return false;
    }

    // If performance evidence is required, check if we have it
    if (consensusConfig_.requirePerformanceEvidence) {
        auto perfHistory = getVariantPerformance(variantId);
        if (perfHistory.empty()) {
            return false;
        }
    }

    return true;
}

void EmergenceManager::processAdoption(const std::string& variantId) {
    // Update variant status
    setVariantStatus(variantId, VariantStatus::Adopted);
    
    // Record adoption timestamp
    adoptionTimestamps_[variantId] = std::chrono::system_clock::now();
    
    // Log the event
    std::stringstream ss;
    ss << "Variant " << variantId << " reached consensus and was adopted. "
       << "Support ratio: " << (static_cast<double>(std::count_if(
              variantVotes_[variantId].begin(),
              variantVotes_[variantId].end(),
              [](const VotingRecord& v) { return v.support; }
          )) / variantVotes_[variantId].size());
    logEvent(ss.str());
    
    checkAutosave();
}

double EmergenceManager::calculateAgentCompatibility(
    const std::string& agentId,
    const ProtocolVariant& variant
) const {
    const auto& context = agentContexts_.at(agentId);
    double score = 0.0;
    
    // Check if the agent has successfully used this variant before
    if (std::find(context.successfulVariants.begin(),
                  context.successfulVariants.end(),
                  variant.id) != context.successfulVariants.end()) {
        score += 1.0;  // Significant boost for proven success
    }
    
    // Check capability requirements
    for (const auto& [capability, required] : variant.metadata["requiredCapabilities"].items()) {
        if (context.capabilities.find(capability) != context.capabilities.end()) {
            score += 0.5;  // Bonus for each matching capability
        }
    }
    
    // Consider agent preferences against variant characteristics
    for (const auto& [metric, weight] : context.preferences) {
        if (variant.metadata["characteristics"].contains(metric)) {
            double value = variant.metadata["characteristics"][metric].get<double>();
            score += value * weight;  // Weight the characteristic by agent's preference
        }
    }
    
    return score;
}

} // namespace extensions
} // namespace xenocomm 