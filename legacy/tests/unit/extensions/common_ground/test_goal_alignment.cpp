#include <gtest/gtest.h>
#include "xenocomm/extensions/common_ground/strategies/goal_alignment.hpp"
#include "xenocomm/extensions/common_ground/context.hpp"
#include <any>
#include <map>
#include <string>

using namespace xenocomm::common_ground;

AlignmentContext makeContext(const std::map<std::string, std::any>& params) {
    AgentInfo local{"local", "LocalAgent", {}};
    AgentInfo remote{"remote", "RemoteAgent", {}};
    return AlignmentContext(local, remote, params);
}

TEST(GoalAlignmentStrategyTest, CompatibleGoals) {
    GoalAlignmentStrategy strategy;
    strategy.setLocalGoal("goalA");
    strategy.setLocalIntention("intentionA");
    
    std::map<std::string, std::any> params = {
        {"remote_goal", std::string("goalA")},
        {"remote_intention", std::string("intentionA")}
    };
    AlignmentContext ctx = makeContext(params);
    AlignmentResult result = strategy.verify(ctx);
    EXPECT_TRUE(result.isAligned());
    EXPECT_EQ(result.getMisalignments().size(), 0);
    EXPECT_DOUBLE_EQ(result.getConfidenceScore(), 1.0);
}

TEST(GoalAlignmentStrategyTest, IncompatibleGoals) {
    GoalAlignmentStrategy strategy;
    strategy.setLocalGoal("goalA");
    strategy.setLocalIntention("intentionA");
    
    std::map<std::string, std::any> params = {
        {"remote_goal", std::string("goalB")},
        {"remote_intention", std::string("intentionA")}
    };
    AlignmentContext ctx = makeContext(params);
    AlignmentResult result = strategy.verify(ctx);
    EXPECT_FALSE(result.isAligned());
    EXPECT_GE(result.getMisalignments().size(), 1);
    EXPECT_DOUBLE_EQ(result.getConfidenceScore(), 0.5);
}

TEST(GoalAlignmentStrategyTest, MissingGoalParameters) {
    GoalAlignmentStrategy strategy;
    strategy.setLocalGoal("goalA");
    strategy.setLocalIntention("intentionA");
    
    std::map<std::string, std::any> params = {
        {"remote_goal", std::string("goalA")}
        // Missing remote_intention
    };
    AlignmentContext ctx = makeContext(params);
    AlignmentResult result = strategy.verify(ctx);
    EXPECT_FALSE(result.isAligned());
    EXPECT_EQ(result.getMisalignments().size(), 1);
    EXPECT_EQ(result.getMisalignments()[0], "Remote intention not provided");
    EXPECT_DOUBLE_EQ(result.getConfidenceScore(), 0.0);
}

TEST(GoalAlignmentStrategyTest, CustomGoalValidator) {
    GoalAlignmentStrategy strategy;
    strategy.setLocalGoal("goalA");
    strategy.setLocalIntention("intentionA");
    // Set custom validator that accepts any goals with the same first character
    strategy.setGoalValidator([](const std::string& local, const std::string& remote) {
        return !local.empty() && !remote.empty() && local[0] == remote[0];
    });
    
    std::map<std::string, std::any> params = {
        {"remote_goal", std::string("goalAnotherVersion")},
        {"remote_intention", std::string("intentionA")}
    };
    AlignmentContext ctx = makeContext(params);
    AlignmentResult result = strategy.verify(ctx);
    EXPECT_TRUE(result.isAligned());
    EXPECT_EQ(result.getMisalignments().size(), 0);
    EXPECT_DOUBLE_EQ(result.getConfidenceScore(), 1.0);
} 