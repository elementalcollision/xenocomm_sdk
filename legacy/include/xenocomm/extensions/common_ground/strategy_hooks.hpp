#pragma once
#include "interfaces.hpp"
#include <vector>
#include <functional>
#include <memory>

namespace xenocomm {
namespace common_ground {

class StrategyHooks {
public:
    using PreHook = std::function<void(const IAlignmentStrategy&, const AlignmentContext&)>;
    using PostHook = std::function<void(const IAlignmentStrategy&, const AlignmentResult&)>;

    void addPreHook(PreHook hook) {
        preHooks_.push_back(std::move(hook));
    }
    void addPostHook(PostHook hook) {
        postHooks_.push_back(std::move(hook));
    }
    void clearHooks() {
        preHooks_.clear();
        postHooks_.clear();
    }

    void executePreHooks(const IAlignmentStrategy& strategy, const AlignmentContext& context) {
        for (const auto& hook : preHooks_) {
            hook(strategy, context);
        }
    }
    void executePostHooks(const IAlignmentStrategy& strategy, const AlignmentResult& result) {
        for (const auto& hook : postHooks_) {
            hook(strategy, result);
        }
    }

private:
    std::vector<PreHook> preHooks_;
    std::vector<PostHook> postHooks_;
};

} // namespace common_ground
} // namespace xenocomm 