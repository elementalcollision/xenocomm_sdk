#pragma once
#include "base_strategy.hpp"
#include "../context.hpp"
#include "../result.hpp"
#include <string>
#include <vector>
#include <functional>
#include <algorithm>

namespace xenocomm {
namespace common_ground {

// Placeholder for context data type
struct ContextData {
    std::vector<std::string> parameters;
};

/**
 * @class ContextSynchronizationStrategy
 * @brief Strategy for aligning contextual understanding between agents.
 */
class ContextSynchronizationStrategy : public BaseAlignmentStrategy {
public:
    ContextSynchronizationStrategy()
        : BaseAlignmentStrategy("context_synchronization") {}

    void addContextParameter(const std::string& parameter) {
        requiredParameters_.push_back(parameter);
    }
    void setContextValidator(std::function<bool(const ContextData&)> validator) {
        contextValidator_ = std::move(validator);
    }

protected:
    AlignmentResult doVerification(const AlignmentContext& context) override {
        // For demonstration, assume context parameters are in parameters as "local_context" and "remote_context" (ContextData)
        auto params = context.getParameters();
        std::vector<std::string> misalignments;
        auto itLocal = params.find("local_context");
        auto itRemote = params.find("remote_context");
        if (itLocal == params.end() || itRemote == params.end() ||
            itLocal->second.type() != typeid(ContextData) ||
            itRemote->second.type() != typeid(ContextData)) {
            misalignments.push_back("Missing or invalid context parameters");
            return AlignmentResult(false, misalignments, 0.0);
        }
        const auto& localContext = std::any_cast<const ContextData&>(itLocal->second);
        const auto& remoteContext = std::any_cast<const ContextData&>(itRemote->second);
        bool synced = synchronizeContext(localContext, remoteContext, misalignments);
        double confidence = synced ? 1.0 : 0.0;
        return AlignmentResult(synced, misalignments, confidence);
    }
    
    bool isApplicable(const AlignmentContext& context) const override {
        auto params = context.getParameters();
        return params.count("local_context") && params.count("remote_context");
    }

private:
    std::vector<std::string> requiredParameters_;
    std::function<bool(const ContextData&)> contextValidator_;
    bool synchronizeContext(const ContextData& localContext, const ContextData& remoteContext, std::vector<std::string>& misalignments) const {
        // Check that all required parameters are present in both contexts
        for (const auto& param : requiredParameters_) {
            bool inLocal = std::find(localContext.parameters.begin(), localContext.parameters.end(), param) != localContext.parameters.end();
            bool inRemote = std::find(remoteContext.parameters.begin(), remoteContext.parameters.end(), param) != remoteContext.parameters.end();
            if (!inLocal || !inRemote) {
                misalignments.push_back("Missing context parameter: " + param);
            }
        }
        // Use validator if provided
        if (contextValidator_) {
            if (!contextValidator_(localContext) || !contextValidator_(remoteContext)) {
                misalignments.push_back("Context validation failed");
            }
        }
        return misalignments.empty();
    }
};

} // namespace common_ground
} // namespace xenocomm
