#include <gtest/gtest.h>
#include "xenocomm/extensions/common_ground/strategies/context_synchronization.hpp"
#include "xenocomm/extensions/common_ground/context.hpp"
#include <any>
#include <map>
#include <string>
#include <vector>

using namespace xenocomm::common_ground;

AlignmentContext makeContext(const std::map<std::string, std::any>& params) {
    AgentInfo local{"local", "LocalAgent", {}};
    AgentInfo remote{"remote", "RemoteAgent", {}};
    return AlignmentContext(local, remote, params);
}

TEST(ContextSynchronizationStrategyTest, AllParametersPresent) {
    ContextSynchronizationStrategy strategy;
    strategy.addContextParameter("foo");
    ContextData localCtx{{"foo", "bar"}};
    ContextData remoteCtx{{"foo", "bar"}};
    std::map<std::string, std::any> params = {
        {"local_context", localCtx},
        {"remote_context", remoteCtx}
    };
    AlignmentContext ctx = makeContext(params);
    AlignmentResult result = strategy.verify(ctx);
    EXPECT_TRUE(result.isAligned());
    EXPECT_EQ(result.getMisalignments().size(), 0);
    EXPECT_DOUBLE_EQ(result.getConfidenceScore(), 1.0);
}

TEST(ContextSynchronizationStrategyTest, MissingParameter) {
    ContextSynchronizationStrategy strategy;
    strategy.addContextParameter("foo");
    ContextData localCtx{{"foo"}};
    ContextData remoteCtx{{}};
    std::map<std::string, std::any> params = {
        {"local_context", localCtx},
        {"remote_context", remoteCtx}
    };
    AlignmentContext ctx = makeContext(params);
    AlignmentResult result = strategy.verify(ctx);
    EXPECT_FALSE(result.isAligned());
    EXPECT_EQ(result.getMisalignments().size(), 1);
    EXPECT_EQ(result.getMisalignments()[0], "Missing context parameter: foo");
    EXPECT_DOUBLE_EQ(result.getConfidenceScore(), 0.0);
}

TEST(ContextSynchronizationStrategyTest, ValidatorFails) {
    ContextSynchronizationStrategy strategy;
    strategy.addContextParameter("foo");
    ContextData localCtx{{"foo"}};
    ContextData remoteCtx{{"foo"}};
    strategy.setContextValidator([](const ContextData&) { return false; });
    std::map<std::string, std::any> params = {
        {"local_context", localCtx},
        {"remote_context", remoteCtx}
    };
    AlignmentContext ctx = makeContext(params);
    AlignmentResult result = strategy.verify(ctx);
    EXPECT_FALSE(result.isAligned());
    EXPECT_EQ(result.getMisalignments().size(), 1);
    EXPECT_EQ(result.getMisalignments()[0], "Context validation failed");
    EXPECT_DOUBLE_EQ(result.getConfidenceScore(), 0.0);
} 