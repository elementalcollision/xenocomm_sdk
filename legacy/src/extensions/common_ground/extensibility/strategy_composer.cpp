#include "../../../../include/xenocomm/extensions/common_ground/extensibility/strategy_composer.hpp"
#include <iostream>

namespace xenocomm {
namespace common_ground {

namespace {
class SequenceStrategy : public IAlignmentStrategy {
public:
    SequenceStrategy(std::vector<std::shared_ptr<IAlignmentStrategy>> s) : strategies_(std::move(s)) {}
    std::string getId() const override { return "sequence"; }
    AlignmentResult verify(const AlignmentContext& ctx) override {
        for (auto& s : strategies_) {
            auto res = s->verify(ctx);
            if (!res.isAligned()) return res;
        }
        return AlignmentResult(true, {}, 1.0);
    }
    bool isApplicable(const AlignmentContext& ctx) const override {
        for (auto& s : strategies_) if (!s->isApplicable(ctx)) return false;
        return true;
    }
private:
    std::vector<std::shared_ptr<IAlignmentStrategy>> strategies_;
};
}

StrategyComposer::StrategyComposer() {}

std::shared_ptr<IAlignmentStrategy> StrategyComposer::sequence(std::vector<std::shared_ptr<IAlignmentStrategy>> strategies) {
    return std::make_shared<SequenceStrategy>(std::move(strategies));
}

std::shared_ptr<IAlignmentStrategy> StrategyComposer::parallel(std::vector<std::shared_ptr<IAlignmentStrategy>> strategies, ParallelExecutionConfig) {
    // Minimal: run all, return first misalignment or aligned
    return sequence(std::move(strategies));
}

std::shared_ptr<IAlignmentStrategy> StrategyComposer::conditional(std::shared_ptr<IAlignmentStrategy> strategy, std::function<bool(const AlignmentContext&)> condition) {
    class ConditionalStrategy : public IAlignmentStrategy {
    public:
        ConditionalStrategy(std::shared_ptr<IAlignmentStrategy> s, std::function<bool(const AlignmentContext&)> c)
            : strategy_(std::move(s)), cond_(std::move(c)) {}
        std::string getId() const override { return "conditional:" + strategy_->getId(); }
        AlignmentResult verify(const AlignmentContext& ctx) override {
            if (cond_ && cond_(ctx)) return strategy_->verify(ctx);
            return AlignmentResult(true, {}, 1.0);
        }
        bool isApplicable(const AlignmentContext& ctx) const override {
            return cond_ ? cond_(ctx) && strategy_->isApplicable(ctx) : strategy_->isApplicable(ctx);
        }
    private:
        std::shared_ptr<IAlignmentStrategy> strategy_;
        std::function<bool(const AlignmentContext&)> cond_;
    };
    return std::make_shared<ConditionalStrategy>(std::move(strategy), std::move(condition));
}

std::shared_ptr<IAlignmentStrategy> StrategyComposer::fallback(std::vector<std::shared_ptr<IAlignmentStrategy>> strategies) {
    class FallbackStrategy : public IAlignmentStrategy {
    public:
        FallbackStrategy(std::vector<std::shared_ptr<IAlignmentStrategy>> s) : strategies_(std::move(s)) {}
        std::string getId() const override { return "fallback"; }
        AlignmentResult verify(const AlignmentContext& ctx) override {
            for (auto& s : strategies_) {
                auto res = s->verify(ctx);
                if (res.isAligned()) return res;
            }
            return AlignmentResult(false, {"All fallback strategies failed"}, 0.0);
        }
        bool isApplicable(const AlignmentContext& ctx) const override {
            for (auto& s : strategies_) if (s->isApplicable(ctx)) return true;
            return false;
        }
    private:
        std::vector<std::shared_ptr<IAlignmentStrategy>> strategies_;
    };
    return std::make_shared<FallbackStrategy>(std::move(strategies));
}

std::shared_ptr<IAlignmentStrategy> StrategyComposer::withRetry(std::shared_ptr<IAlignmentStrategy> strategy, RetryConfig) {
    class RetryStrategy : public IAlignmentStrategy {
    public:
        RetryStrategy(std::shared_ptr<IAlignmentStrategy> s) : strategy_(std::move(s)) {}
        std::string getId() const override { return "retry:" + strategy_->getId(); }
        AlignmentResult verify(const AlignmentContext& ctx) override {
            // Minimal: just call once
            return strategy_->verify(ctx);
        }
        bool isApplicable(const AlignmentContext& ctx) const override {
            return strategy_->isApplicable(ctx);
        }
    private:
        std::shared_ptr<IAlignmentStrategy> strategy_;
    };
    return std::make_shared<RetryStrategy>(std::move(strategy));
}

AlignmentResult StrategyComposer::combineResults(const std::vector<AlignmentResult>& results, ResultCombinationStrategy) {
    // Minimal: return first result
    if (!results.empty()) return results.front();
    return AlignmentResult(true, {}, 1.0);
}

} // namespace common_ground
} // namespace xenocomm
