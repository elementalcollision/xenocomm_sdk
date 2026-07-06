#pragma once
#include "base_strategy.hpp"
#include "../context.hpp"
#include "../result.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace xenocomm {
namespace common_ground {

// Structure to represent a terminology definition
struct TermDefinition {
    std::string id;
    std::string definition;
};

/**
 * @class TerminologyAlignmentStrategy
 * @brief Strategy for ensuring shared terminology understanding between agents.
 */
class TerminologyAlignmentStrategy : public BaseAlignmentStrategy {
public:
    TerminologyAlignmentStrategy()
        : BaseAlignmentStrategy("terminology_alignment") {}

    void addCriticalTerm(const std::string& term, const std::string& definition) {
        criticalTerms_[term] = definition;
    }
    void setTermAlignmentChecker(std::function<double(const std::string&, const std::string&)> checker) {
        termChecker_ = std::move(checker);
    }
    void setMinimumAlignmentThreshold(double threshold) {
        minimumAlignmentThreshold_ = threshold;
    }

protected:
    AlignmentResult doVerification(const AlignmentContext& context) override {
        // For demonstration, assume remote terminology definitions are in context params
        auto params = context.getParameters();
        std::vector<std::string> misalignments;
        auto it = params.find("remote_terminology");
        if (it == params.end() || it->second.type() != typeid(std::unordered_map<std::string, std::string>)) {
            misalignments.push_back("Missing or invalid terminology definitions");
            return AlignmentResult(false, misalignments, 0.0);
        }
        const auto& remoteTerms = std::any_cast<const std::unordered_map<std::string, std::string>&>(it->second);
        double overallScore = checkTerminologyAlignment(remoteTerms, misalignments);
        bool aligned = overallScore >= minimumAlignmentThreshold_;
        return AlignmentResult(aligned, misalignments, overallScore);
    }
    
    bool isApplicable(const AlignmentContext& context) const override {
        auto params = context.getParameters();
        return !criticalTerms_.empty() && params.count("remote_terminology") > 0;
    }

private:
    std::unordered_map<std::string, std::string> criticalTerms_;
    std::function<double(const std::string&, const std::string&)> termChecker_;
    double minimumAlignmentThreshold_ = 0.8;

    double checkTerminologyAlignment(
        const std::unordered_map<std::string, std::string>& remoteTerms,
        std::vector<std::string>& misalignments) const {
        
        int matched = 0;
        int total = criticalTerms_.size();
        
        for (const auto& [term, definition] : criticalTerms_) {
            auto it = remoteTerms.find(term);
            if (it == remoteTerms.end()) {
                misalignments.push_back("Missing term definition: " + term);
                continue;
            }
            
            double similarity = termChecker_ ? 
                termChecker_(definition, it->second) : 
                (definition == it->second ? 1.0 : 0.0);
                
            if (similarity < minimumAlignmentThreshold_) {
                misalignments.push_back("Term definition mismatch for '" + term + "': similarity score " + 
                                       std::to_string(similarity));
                continue;
            }
            
            matched++;
        }
        
        return total > 0 ? static_cast<double>(matched) / total : 1.0;
    }
};

} // namespace common_ground
} // namespace xenocomm
