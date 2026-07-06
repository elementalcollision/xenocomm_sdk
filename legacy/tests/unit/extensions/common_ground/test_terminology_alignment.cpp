#include <gtest/gtest.h>
#include "xenocomm/extensions/common_ground/strategies/terminology_alignment.hpp"
#include "xenocomm/extensions/common_ground/context.hpp"
#include <any>
#include <map>
#include <string>
#include <vector>
#include <unordered_map>

using namespace xenocomm::common_ground;

AlignmentContext makeContext(const std::map<std::string, std::any>& params) {
    AgentInfo local{"local", "LocalAgent", {}};
    AgentInfo remote{"remote", "RemoteAgent", {}};
    return AlignmentContext(local, remote, params);
}

TEST(TerminologyAlignmentStrategyTest, AllTermsPresent) {
    TerminologyAlignmentStrategy strategy;
    strategy.addCriticalTerm("foo", "Definition of foo");
    strategy.addCriticalTerm("baz", "Definition of baz");
    
    std::unordered_map<std::string, std::string> remoteTerms = {
        {"foo", "Definition of foo"},
        {"baz", "Definition of baz"}
    };
    
    std::map<std::string, std::any> params = {
        {"remote_terminology", remoteTerms}
    };
    
    AlignmentContext ctx = makeContext(params);
    AlignmentResult result = strategy.verify(ctx);
    EXPECT_TRUE(result.isAligned());
    EXPECT_EQ(result.getMisalignments().size(), 0);
    EXPECT_DOUBLE_EQ(result.getConfidenceScore(), 1.0);
}

TEST(TerminologyAlignmentStrategyTest, MissingTerm) {
    TerminologyAlignmentStrategy strategy;
    strategy.addCriticalTerm("foo", "Definition of foo");
    strategy.addCriticalTerm("baz", "Definition of baz");
    
    std::unordered_map<std::string, std::string> remoteTerms = {
        {"baz", "Definition of baz"}
        // Missing "foo"
    };
    
    std::map<std::string, std::any> params = {
        {"remote_terminology", remoteTerms}
    };
    
    AlignmentContext ctx = makeContext(params);
    AlignmentResult result = strategy.verify(ctx);
    EXPECT_FALSE(result.isAligned());
    EXPECT_EQ(result.getMisalignments().size(), 1);
    EXPECT_EQ(result.getMisalignments()[0], "Missing term definition: foo");
    EXPECT_DOUBLE_EQ(result.getConfidenceScore(), 0.5);
}

TEST(TerminologyAlignmentStrategyTest, DefinitionMismatch) {
    TerminologyAlignmentStrategy strategy;
    strategy.addCriticalTerm("foo", "Correct definition");
    strategy.setMinimumAlignmentThreshold(0.9); // Set high threshold
    
    std::unordered_map<std::string, std::string> remoteTerms = {
        {"foo", "Different definition"}
    };
    
    std::map<std::string, std::any> params = {
        {"remote_terminology", remoteTerms}
    };
    
    AlignmentContext ctx = makeContext(params);
    AlignmentResult result = strategy.verify(ctx);
    EXPECT_FALSE(result.isAligned());
    EXPECT_EQ(result.getMisalignments().size(), 1);
    EXPECT_DOUBLE_EQ(result.getConfidenceScore(), 0.0);
}

TEST(TerminologyAlignmentStrategyTest, CustomTermChecker) {
    TerminologyAlignmentStrategy strategy;
    strategy.addCriticalTerm("foo", "Definition of foo");
    // Set a custom checker that considers partial matches
    strategy.setTermAlignmentChecker([](const std::string& def1, const std::string& def2) {
        // Simple similarity: check if they contain the same word
        return def1.find("foo") != std::string::npos && 
               def2.find("foo") != std::string::npos ? 1.0 : 0.0;
    });
    
    std::unordered_map<std::string, std::string> remoteTerms = {
        {"foo", "A different definition containing foo word"}
    };
    
    std::map<std::string, std::any> params = {
        {"remote_terminology", remoteTerms}
    };
    
    AlignmentContext ctx = makeContext(params);
    AlignmentResult result = strategy.verify(ctx);
    EXPECT_TRUE(result.isAligned());
    EXPECT_EQ(result.getMisalignments().size(), 0);
    EXPECT_DOUBLE_EQ(result.getConfidenceScore(), 1.0);
}

TEST(TerminologyAlignmentStrategyTest, MissingTerminologyParameters) {
    TerminologyAlignmentStrategy strategy;
    strategy.addCriticalTerm("foo", "Definition of foo");
    
    std::map<std::string, std::any> params = {
        // Missing "remote_terminology"
    };
    
    AlignmentContext ctx = makeContext(params);
    AlignmentResult result = strategy.verify(ctx);
    EXPECT_FALSE(result.isAligned());
    EXPECT_EQ(result.getMisalignments().size(), 1);
    EXPECT_EQ(result.getMisalignments()[0], "Missing or invalid terminology definitions");
    EXPECT_DOUBLE_EQ(result.getConfidenceScore(), 0.0);
} 