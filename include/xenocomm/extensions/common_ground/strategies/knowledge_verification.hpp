#pragma once
#include "base_strategy.hpp"
#include "../context.hpp"
#include "../result.hpp"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

namespace xenocomm {
namespace common_ground {

// Placeholder for knowledge concept type
struct KnowledgeConcept {
    std::string id;
    std::string description;
};

/**
 * @class KnowledgeVerificationStrategy
 * @brief Strategy for verifying shared knowledge state between agents.
 */
class KnowledgeVerificationStrategy : public BaseAlignmentStrategy {
public:
    KnowledgeVerificationStrategy()
        : BaseAlignmentStrategy("knowledge_verification") {}

    void addRequiredConcept(const KnowledgeConcept& concept) {
        requiredConcepts_[concept.id] = concept;
    }
    void setKnowledgeVerifier(std::function<bool(const std::string&, const std::string&)> verifier) {
        knowledgeVerifier_ = std::move(verifier);
    }

protected:
    AlignmentResult doVerification(const AlignmentContext& context) override {
        // For demonstration, assume context parameters are in params as "agent_knowledge"
        auto params = context.getParameters();
        std::vector<std::string> misalignments;
        auto it = params.find("agent_knowledge");
        if (it == params.end() || it->second.type() != typeid(std::vector<std::string>)) {
            misalignments.push_back("Missing or invalid knowledge parameters");
            return AlignmentResult(false, misalignments, 0.0);
        }
        const auto& agentKnowledge = std::any_cast<const std::vector<std::string>&>(it->second);
        bool verified = verifyKnowledge(agentKnowledge, misalignments);
        double confidence = verified ? 1.0 : 0.5;  // Partial knowledge might still work
        return AlignmentResult(verified, misalignments, confidence);
    }
    
    bool isApplicable(const AlignmentContext& context) const override {
        return !requiredConcepts_.empty();
    }

private:
    std::unordered_map<std::string, KnowledgeConcept> requiredConcepts_;
    std::function<bool(const std::string&, const std::string&)> knowledgeVerifier_;
    bool verifyKnowledge(const std::vector<std::string>& agentKnowledge, std::vector<std::string>& misalignments) const {
        // Simple verification: check that each required concept ID is in the agent's knowledge
        for (const auto& [id, concept] : requiredConcepts_) {
            bool found = false;
            for (const auto& knowledge : agentKnowledge) {
                if (knowledgeVerifier_ && knowledgeVerifier_(id, knowledge)) {
                    found = true;
                    break;
                } else if (id == knowledge) {  // Simple string match fallback
                    found = true;
                    break;
                }
            }
            if (!found) {
                misalignments.push_back("Missing required knowledge: " + concept.description);
            }
        }
        return misalignments.empty();
    }
};

} // namespace common_ground
} // namespace xenocomm
