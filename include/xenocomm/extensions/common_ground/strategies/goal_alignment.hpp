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

// Placeholder for goal compatibility matrix and goal types
typedef std::map<std::string, double> GoalCompatibilityMatrix;
struct Goal {
    std::string id;
    std::string description;
};

/**
 * @class GoalAlignmentStrategy
 * @brief Strategy for checking alignment between agent goals and intended outcomes.
 */
class GoalAlignmentStrategy : public BaseAlignmentStrategy {
public:
    GoalAlignmentStrategy()
        : BaseAlignmentStrategy("goal_alignment") {}

    void setLocalGoal(const std::string& goal) {
        localGoal_ = goal;
    }
    void setLocalIntention(const std::string& intention) {
        localIntention_ = intention;
    }
    void setGoalValidator(std::function<bool(const std::string&, const std::string&)> validator) {
        goalValidator_ = std::move(validator);
    }

protected:
    AlignmentResult doVerification(const AlignmentContext& context) override {
        std::vector<std::string> misalignments;
        
        // Get remote goals from context (assume they're stored as parameters)
        auto params = context.getParameters();
        auto itGoal = params.find("remote_goal");
        auto itIntention = params.find("remote_intention");
        
        bool hasRemoteGoal = (itGoal != params.end() && itGoal->second.type() == typeid(std::string));
        bool hasRemoteIntention = (itIntention != params.end() && itIntention->second.type() == typeid(std::string));
        
        if (!hasRemoteGoal) {
            misalignments.push_back("Remote goal not provided");
        }
        if (!hasRemoteIntention) {
            misalignments.push_back("Remote intention not provided");
        }
        
        if (misalignments.empty()) {
            const auto& remoteGoal = std::any_cast<const std::string&>(itGoal->second);
            const auto& remoteIntention = std::any_cast<const std::string&>(itIntention->second);
            
            bool goalsAligned = validateGoals(localGoal_, remoteGoal, misalignments);
            bool intentionsAligned = validateIntentions(localIntention_, remoteIntention, misalignments);
            
            bool aligned = goalsAligned && intentionsAligned;
            double confidence = aligned ? 1.0 : 0.5; // Partial alignment might be enough
            return AlignmentResult(aligned, misalignments, confidence);
        }
        
        return AlignmentResult(false, misalignments, 0.0);
    }
    
    bool isApplicable(const AlignmentContext& context) const override {
        auto params = context.getParameters();
        return !localGoal_.empty() && !localIntention_.empty() && 
               params.count("remote_goal") && params.count("remote_intention");
    }

private:
    std::string localGoal_;
    std::string localIntention_;
    std::function<bool(const std::string&, const std::string&)> goalValidator_;
    
    bool validateGoals(const std::string& local, const std::string& remote, 
                       std::vector<std::string>& misalignments) const {
        if (goalValidator_) {
            if (!goalValidator_(local, remote)) {
                misalignments.push_back("Goals don't align: " + local + " vs. " + remote);
                return false;
            }
            return true;
        }
        
        // Default validation - simple equality
        if (local != remote) {
            misalignments.push_back("Goals don't match exactly: " + local + " vs. " + remote);
            return false;
        }
        return true;
    }
    
    bool validateIntentions(const std::string& local, const std::string& remote, 
                           std::vector<std::string>& misalignments) const {
        // Simpler check for intentions
        if (local != remote) {
            misalignments.push_back("Intentions don't match: " + local + " vs. " + remote);
            return false;
        }
        return true;
    }
};

} // namespace common_ground
} // namespace xenocomm
