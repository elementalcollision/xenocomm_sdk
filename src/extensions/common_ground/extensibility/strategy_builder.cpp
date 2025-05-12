#include "../../../../include/xenocomm/extensions/common_ground/extensibility/strategy_builder.hpp"
#include <iostream>

namespace xenocomm {
namespace common_ground {

// Minimal config stubs
struct StrategyConfig {
    std::string id;
    std::string name;
    std::string description;
    std::function<AlignmentResult(const AlignmentContext&)> verifyLogic;
    std::function<bool(const AlignmentContext&)> applicabilityCheck;
};

class SimpleStrategy : public IAlignmentStrategy {
public:
    SimpleStrategy(const StrategyConfig& config) : config_(config) {}
    std::string getId() const override { return config_.id; }
    AlignmentResult verify(const AlignmentContext& ctx) override {
        if (config_.verifyLogic) return config_.verifyLogic(ctx);
        return AlignmentResult(true, {}, 1.0);
    }
    bool isApplicable(const AlignmentContext& ctx) const override {
        if (config_.applicabilityCheck) return config_.applicabilityCheck(ctx);
        return true;
    }
private:
    StrategyConfig config_;
};

StrategyBuilder::StrategyBuilder() : config_(new StrategyConfig()) {}

StrategyBuilder& StrategyBuilder::withId(std::string id) {
    config_->id = std::move(id);
    return *this;
}
StrategyBuilder& StrategyBuilder::withName(std::string name) {
    config_->name = std::move(name);
    return *this;
}
StrategyBuilder& StrategyBuilder::withDescription(std::string description) {
    config_->description = std::move(description);
    return *this;
}
StrategyBuilder& StrategyBuilder::withVerificationLogic(std::function<AlignmentResult(const AlignmentContext&)> logic) {
    config_->verifyLogic = std::move(logic);
    return *this;
}
StrategyBuilder& StrategyBuilder::withApplicabilityCheck(std::function<bool(const AlignmentContext&)> check) {
    config_->applicabilityCheck = std::move(check);
    return *this;
}
StrategyBuilder& StrategyBuilder::withPreVerificationHook(std::function<void(const AlignmentContext&)>) { return *this; }
StrategyBuilder& StrategyBuilder::withPostVerificationHook(std::function<void(const AlignmentResult&)>) { return *this; }
StrategyBuilder& StrategyBuilder::withRequiredParameter(const std::string&) { return *this; }
template<typename T>
StrategyBuilder& StrategyBuilder::withParameter(const std::string&, T) { return *this; }

std::shared_ptr<IAlignmentStrategy> StrategyBuilder::build() {
    return std::make_shared<SimpleStrategy>(*config_);
}
std::shared_ptr<IAlignmentStrategy> StrategyBuilder::buildAndRegister() {
    // TODO: Integrate with StrategyRegistry
    std::cout << "Registering strategy: " << config_->id << std::endl;
    return build();
}

// Explicit template instantiation
template StrategyBuilder& StrategyBuilder::withParameter<int>(const std::string&, int);
template StrategyBuilder& StrategyBuilder::withParameter<double>(const std::string&, double);
template StrategyBuilder& StrategyBuilder::withParameter<std::string>(const std::string&, std::string);

} // namespace common_ground
} // namespace xenocomm
