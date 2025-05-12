#pragma once
#include "interfaces.hpp"
#include <vector>
#include <memory>
#include <functional>
#include <future>

namespace xenocomm {
namespace common_ground {

class StrategyChain {
public:
    StrategyChain() = default;

    // Add a strategy to the chain
    StrategyChain& add(std::shared_ptr<IAlignmentStrategy> strategy) {
        chain_.push_back({strategy, nullptr});
        return *this;
    }

    // Add a strategy with a condition
    StrategyChain& addWithCondition(std::shared_ptr<IAlignmentStrategy> strategy,
                                    std::function<bool(const AlignmentContext&)> condition) {
        chain_.push_back({strategy, condition});
        return *this;
    }

    // Execute the chain synchronously (stub)
    AlignmentResult execute(const AlignmentContext& context) {
        // TODO: Implement chain execution logic
        return AlignmentResult(true, {}, 1.0);
    }

    // Execute the chain asynchronously (stub)
    std::future<AlignmentResult> executeAsync(const AlignmentContext& context) {
        // TODO: Implement async execution logic
        return std::async(std::launch::async, [this, &context]() {
            return this->execute(context);
        });
    }

private:
    struct ChainEntry {
        std::shared_ptr<IAlignmentStrategy> strategy;
        std::function<bool(const AlignmentContext&)> condition;
    };
    std::vector<ChainEntry> chain_;
};

} // namespace common_ground
} // namespace xenocomm 