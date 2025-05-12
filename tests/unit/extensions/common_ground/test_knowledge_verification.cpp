#include <gtest/gtest.h>
#include "xenocomm/extensions/common_ground/strategies/knowledge_verification.hpp"
#include "xenocomm/extensions/common_ground/context.hpp"
#include <any>
#include <map>
#include <string>
#include <vector>

using namespace xenocomm::common_ground;

// Helper to create a context with given parameters
AlignmentContext makeContext(const std::map<std::string, std::any>& params) {
    AgentInfo local{"local", "LocalAgent", {}};
    AgentInfo remote{"remote", "RemoteAgent", {}};
    return AlignmentContext(local, remote, params);
}

TEST(KnowledgeVerificationStrategyTest, AllKnowledgeMatches) {
    KnowledgeVerificationStrategy strategy;
    KnowledgeConcept concept1{"foo", "Foo description"};
    KnowledgeConcept concept2{"baz", "Baz description"};
    strategy.addRequiredConcept(concept1);
    strategy.addRequiredConcept(concept2);
    
    std::vector<std::string> agentKnowledge = {"foo", "baz"};
    std::map<std::string, std::any> params = {
        {"agent_knowledge", agentKnowledge}
    };
    
    AlignmentContext ctx = makeContext(params);
    AlignmentResult result = strategy.verify(ctx);
    EXPECT_TRUE(result.isAligned());
    EXPECT_EQ(result.getMisalignments().size(), 0);
    EXPECT_DOUBLE_EQ(result.getConfidenceScore(), 1.0);
}

TEST(KnowledgeVerificationStrategyTest, MissingKnowledgeConcept) {
    KnowledgeVerificationStrategy strategy;
    KnowledgeConcept concept1{"foo", "Foo description"};
    KnowledgeConcept concept2{"baz", "Baz description"};
    strategy.addRequiredConcept(concept1);
    strategy.addRequiredConcept(concept2);
    
    std::vector<std::string> agentKnowledge = {"foo"}; // Missing "baz"
    std::map<std::string, std::any> params = {
        {"agent_knowledge", agentKnowledge}
    };
    
    AlignmentContext ctx = makeContext(params);
    AlignmentResult result = strategy.verify(ctx);
    EXPECT_FALSE(result.isAligned());
    EXPECT_EQ(result.getMisalignments().size(), 1);
    EXPECT_EQ(result.getMisalignments()[0], "Missing required knowledge: Baz description");
    EXPECT_DOUBLE_EQ(result.getConfidenceScore(), 0.0);
}

TEST(KnowledgeVerificationStrategyTest, CustomKnowledgeVerifier) {
    KnowledgeVerificationStrategy strategy;
    KnowledgeConcept concept1{"foo", "Foo description"};
    strategy.addRequiredConcept(concept1);
    // Set a custom verifier that accepts knowledge starting with the same prefix
    strategy.setKnowledgeVerifier([](const std::string& concept, const std::string& knowledge) {
        return !concept.empty() && !knowledge.empty() && 
               concept.substr(0, 1) == knowledge.substr(0, 1);
    });
    
    std::vector<std::string> agentKnowledge = {"fantastic"}; // Starts with 'f' like 'foo'
    std::map<std::string, std::any> params = {
        {"agent_knowledge", agentKnowledge}
    };
    
    AlignmentContext ctx = makeContext(params);
    AlignmentResult result = strategy.verify(ctx);
    EXPECT_TRUE(result.isAligned());
    EXPECT_EQ(result.getMisalignments().size(), 0);
    EXPECT_DOUBLE_EQ(result.getConfidenceScore(), 1.0);
}

TEST(KnowledgeVerificationStrategyTest, MissingKnowledgeParameter) {
    KnowledgeVerificationStrategy strategy;
    KnowledgeConcept concept1{"foo", "Foo description"};
    strategy.addRequiredConcept(concept1);
    
    std::map<std::string, std::any> params = {
        // Missing "agent_knowledge" parameter
    };
    
    AlignmentContext ctx = makeContext(params);
    AlignmentResult result = strategy.verify(ctx);
    EXPECT_FALSE(result.isAligned());
    EXPECT_EQ(result.getMisalignments().size(), 1);
    EXPECT_EQ(result.getMisalignments()[0], "Missing or invalid knowledge parameters");
    EXPECT_DOUBLE_EQ(result.getConfidenceScore(), 0.0);
}
