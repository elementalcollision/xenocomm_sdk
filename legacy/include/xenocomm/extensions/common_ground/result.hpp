#pragma once
#include "interfaces.hpp"
#include <vector>
#include <string>

namespace xenocomm {
namespace common_ground {

class AlignmentResult : public IAlignmentResult {
public:
    AlignmentResult(bool aligned, std::vector<std::string> misalignments, double confidence)
        : aligned_(aligned), misalignments_(std::move(misalignments)), confidenceScore_(confidence) {}

    bool isAligned() const override { return aligned_; }
    const std::vector<std::string>& getMisalignments() const override { return misalignments_; }
    double getConfidenceScore() const override { return confidenceScore_; }

private:
    bool aligned_;
    std::vector<std::string> misalignments_;
    double confidenceScore_;
};

} // namespace common_ground
} // namespace xenocomm
