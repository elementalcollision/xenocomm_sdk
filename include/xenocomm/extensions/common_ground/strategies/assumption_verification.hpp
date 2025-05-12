#pragma once
#include "base_strategy.hpp"
#include "../result.hpp"
#include <string>
#include <vector>
#include <functional>

namespace xenocomm {
namespace common_ground {

/**
 * @class AssumptionVerificationStrategy
 * @brief Strategy for surfacing and validating hidden assumptions between agents.
 */
class AssumptionVerificationStrategy : public BaseAlignmentStrategy {
public:
    AssumptionVerificationStrategy()
        : BaseAlignmentStrategy("assumption_verification") {}

    void addCriticalAssumption(const std::string& assumption) {
        criticalAssumptions_.push_back(assumption);
    }
    void setAssumptionValidator(std::function<bool(const std::string&)> validator) {
        assumptionValidator_ = std::move(validator);
    }

protected:
    AlignmentResult doVerification(const AlignmentContext& context) override {
        std::vector<std::string> misalignments;
        for (const auto& assumption : criticalAssumptions_) {
            if (!validateAssumption(assumption)) {
                misalignments.push_back("Unvalidated or missing assumption: " + assumption);
            }
        }
        bool aligned = misalignments.empty();
        double confidence = aligned ? 1.0 : 0.0;
        return AlignmentResult(aligned, misalignments, confidence);
    }
    
    bool isApplicable(const AlignmentContext&) const override {
        return !criticalAssumptions_.empty();
    }

private:
    std::vector<std::string> criticalAssumptions_;
    std::function<bool(const std::string&)> assumptionValidator_;
    bool validateAssumption(const std::string& assumption) const {
        if (assumptionValidator_) {
            return assumptionValidator_(assumption);
        }
        // Default: always true (for demonstration)
        return true;
    }
};

} // namespace common_ground
} // namespace xenocomm
