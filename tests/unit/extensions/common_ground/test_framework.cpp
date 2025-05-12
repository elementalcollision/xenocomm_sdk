#include <gtest/gtest.h>
#include "xenocomm/extensions/common_ground/framework.hpp"
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

TEST(CommonGroundFrameworkTest, RegisterAndRunStandardStrategies_AllPass) {
    FrameworkConfig config{"test_framework"};
    CommonGroundFramework framework(config);
    framework.registerStandardStrategies();
    // Provide parameters that should pass all default strategies
    std::map<std::string, std::any> params = {
        {"foo", std::string("bar")}, // for knowledge
        {"baz", std::string("qux")}, // for knowledge
        {"local_goal", std::string("goalA")},
        {"remote_goal", std::string("goalA")},
        {"local_terms", std::vector<std::string>{"foo"}},
        {"remote_terms", std::vector<std::string>{"foo"}},
        {"local_context", ContextData{{"foo"}}},
        {"remote_context", ContextData{{"foo"}}}
    };
    AlignmentContext ctx = makeContext(params);
    // Should not throw and should return at least one result
    AlignmentResult result = framework.verifyAlignment(ctx);
    EXPECT_TRUE(result.isAligned());
}

TEST(CommonGroundFrameworkTest, RegisterAndRunStandardStrategies_FailsKnowledge) {
    FrameworkConfig config{"test_framework"};
    CommonGroundFramework framework(config);
    framework.registerStandardStrategies();
    // Missing required knowledge key "baz"
    std::map<std::string, std::any> params = {
        {"foo", std::string("bar")},
        {"local_goal", std::string("goalA")},
        {"remote_goal", std::string("goalA")},
        {"local_terms", std::vector<std::string>{"foo"}},
        {"remote_terms", std::vector<std::string>{"foo"}},
        {"local_context", ContextData{{"foo"}}},
        {"remote_context", ContextData{{"foo"}}}
    };
    AlignmentContext ctx = makeContext(params);
    AlignmentResult result = framework.verifyAlignment(ctx);
    EXPECT_FALSE(result.isAligned());
} 