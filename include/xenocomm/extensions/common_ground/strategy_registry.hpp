#pragma once
#include "interfaces.hpp"
#include <unordered_map>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <string>

namespace xenocomm {
namespace common_ground {

struct StrategyEntry {
    std::shared_ptr<IAlignmentStrategy> strategy;
    int priority = 0;
};

class StrategyRegistry {
public:
    StrategyRegistry() = default;

    void registerStrategy(std::shared_ptr<IAlignmentStrategy> strategy, int priority = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string id = strategy->getId();
        strategies_[id] = {strategy, priority};
        priorityMap_.emplace(priority, id);
    }

    void unregisterStrategy(const std::string& strategyId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = strategies_.find(strategyId);
        if (it != strategies_.end()) {
            // Remove from priorityMap_
            for (auto pit = priorityMap_.begin(); pit != priorityMap_.end(); ) {
                if (pit->second == strategyId) pit = priorityMap_.erase(pit);
                else ++pit;
            }
            strategies_.erase(it);
        }
    }

    bool hasStrategy(const std::string& strategyId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return strategies_.count(strategyId) > 0;
    }

    std::shared_ptr<IAlignmentStrategy> getStrategy(const std::string& strategyId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = strategies_.find(strategyId);
        return (it != strategies_.end()) ? it->second.strategy : nullptr;
    }

    std::vector<std::shared_ptr<IAlignmentStrategy>> getApplicableStrategies(const AlignmentContext& context) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::shared_ptr<IAlignmentStrategy>> result;
        for (const auto& [id, entry] : strategies_) {
            if (entry.strategy->isApplicable(context)) {
                result.push_back(entry.strategy);
            }
        }
        return result;
    }

    void setPriority(const std::string& strategyId, int priority) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = strategies_.find(strategyId);
        if (it != strategies_.end()) {
            // Remove old priority
            for (auto pit = priorityMap_.begin(); pit != priorityMap_.end(); ) {
                if (pit->second == strategyId) pit = priorityMap_.erase(pit);
                else ++pit;
            }
            it->second.priority = priority;
            priorityMap_.emplace(priority, strategyId);
        }
    }

    std::vector<std::shared_ptr<IAlignmentStrategy>> getStrategiesByPriority() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::shared_ptr<IAlignmentStrategy>> result;
        for (const auto& [priority, id] : priorityMap_) {
            auto it = strategies_.find(id);
            if (it != strategies_.end()) {
                result.push_back(it->second.strategy);
            }
        }
        return result;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, StrategyEntry> strategies_;
    std::multimap<int, std::string> priorityMap_;
};

} // namespace common_ground
} // namespace xenocomm 