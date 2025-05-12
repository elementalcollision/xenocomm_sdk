#pragma once
#include "../interfaces.hpp"
#include "../types.hpp"
#include "../result.hpp"
#include <string>
#include <vector>
#include <memory>
#include <utility>

namespace xenocomm {
namespace common_ground {

/**
 * @class BaseAlignmentStrategy
 * @brief Abstract base class for alignment strategies, providing common utilities.
 */
class BaseAlignmentStrategy : public IAlignmentStrategy {
public:
    /**
     * @brief Construct a new BaseAlignmentStrategy.
     * @param id Unique identifier for the strategy.
     */
    explicit BaseAlignmentStrategy(std::string id) : id_(std::move(id)) {}
    virtual ~BaseAlignmentStrategy() = default;

    /**
     * @brief Get the unique ID of the strategy.
     */
    std::string getId() const override { return id_; }

    /**
     * @brief Main verification entry point (calls doVerification).
     */
    AlignmentResult verify(const AlignmentContext& context) override {
        validateContext(context);
        return doVerification(context);
    }

    /**
     * @brief Check if the strategy is applicable to the context (default: true).
     */
    virtual bool isApplicable(const AlignmentContext&) const override { return true; }

protected:
    /**
     * @brief Validate the context (default: no-op).
     */
    virtual void validateContext(const AlignmentContext&) const {}
    /**
     * @brief Perform the actual verification (must be implemented by derived classes).
     */
    virtual AlignmentResult doVerification(const AlignmentContext& context) = 0;

    /**
     * @brief Utility to create a result.
     */
    AlignmentResult createResult(bool aligned, double confidence = 1.0) {
        return AlignmentResult(aligned, {}, confidence);
    }
    /**
     * @brief Utility to add a misalignment to a result.
     */
    void addMisalignment(AlignmentResult& result, const std::string& description) {
        // This assumes AlignmentResult exposes a way to add misalignments (extend as needed)
        // For now, this is a placeholder.
    }

private:
    std::string id_;
};

} // namespace common_ground
} // namespace xenocomm
