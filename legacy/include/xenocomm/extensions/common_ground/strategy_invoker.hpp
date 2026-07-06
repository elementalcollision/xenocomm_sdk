#pragma once
#include "interfaces.hpp"
#include <memory>
#include <vector>
#include <future>
#include <functional>

namespace xenocomm {
namespace common_ground {

class StrategyHooks; // Forward declaration

class StrategyInvoker {
public:
    StrategyInvoker(std::shared_ptr<StrategyHooks> hooks = nullptr) : hooks_(hooks) {}

    // Synchronous invocation (stub)
    AlignmentResult invoke(std::shared_ptr<IAlignmentStrategy> strategy, const AlignmentContext& context) {
        // TODO: Add pre/post hook logic
        return strategy->verify(context);
    }

    // Asynchronous invocation (stub)
    std::future<AlignmentResult> invokeAsync(std::shared_ptr<IAlignmentStrategy> strategy, const AlignmentContext& context) {
        // TODO: Add async logic and hooks
        return std::async(std::launch::async, [strategy, &context]() {
            return strategy->verify(context);
        });
    }

    // Batch invocation (stub)
    std::vector<AlignmentResult> invokeBatch(const std::vector<std::shared_ptr<IAlignmentStrategy>>& strategies, const AlignmentContext& context) {
        std::vector<AlignmentResult> results;
        for (const auto& strategy : strategies) {
            results.push_back(invoke(strategy, context));
        }
        return results;
    }

private:
    std::shared_ptr<StrategyHooks> hooks_;
    // TODO: Add thread pool for async execution
};

} // namespace common_ground
} // namespace xenocomm 